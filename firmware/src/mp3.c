// MP3 playback: file streaming (target commands) → Helix decode → linear
// resample to 48 kHz → PCM fifo.
//
// Memory notes: the 12 KB compressed-input buffer is shared with the JSON
// parser's staging buffer (never active at the same time); Helix's structs
// live in a fixed arena (no real heap).

#include "mp3.h"
#include "file.h"
#include "mmio.h"
#include "mp3dec.h"

#define SLOT_AUDIO 1

// ---- shared input buffer (lib.c exposes its staging buffer when idle)
extern char lib_stage[];          // 12 KB, word-aligned (defined in lib.c)
#define INBUF ((unsigned char *)lib_stage)
#define INBUF_SIZE 12288

// ---- tiny arena for Helix's mallocs (MP3InitDecoder allocates ~20 KB once).
// On the native host test we let libc's malloc serve Helix instead.
#ifndef PT_HOST_TEST
static char arena[22528] __attribute__((aligned(8)));
static uint32_t arena_used;
void *malloc(unsigned long n) {
  n = (n + 7u) & ~7u;
  if (arena_used + n > sizeof(arena)) return 0;
  void *p = &arena[arena_used];
  arena_used += n;
  return p;
}
void free(void *p) { (void)p; }  // decoder is initialized once and kept
#else
#include <stdlib.h>
#endif

// ---- state
static HMP3Decoder hdec;
static int inited;
static uint32_t f_size, f_off;   // audio file
static int in_len;               // valid bytes at INBUF[0..in_len)
static unsigned char *in_ptr;
static int playing, paused, eof_decode;
static uint32_t samples_pushed;  // at 48 kHz
static short pcm[2 * 1152];      // one decoded MP3 frame (interleaved stereo)

// resampler: 16.16 phase through the decoded frame
static uint32_t rs_phase, rs_step;
static short rs_prev_l, rs_prev_r;

static void in_compact_and_fill(void) {
  // move remaining bytes to the front
  int rem = in_len - (int)(in_ptr - INBUF);
  for (int i = 0; i < rem; i++) INBUF[i] = in_ptr[i];
  in_len = rem;
  in_ptr = INBUF;
  // top up from the file
  while (in_len < INBUF_SIZE - (int)RX_SIZE / 2 && f_off < f_size) {
    uint32_t want = f_size - f_off;
    if (want > RX_SIZE / 2) want = RX_SIZE / 2;  // 8 KB chunks
    if (file_read(SLOT_AUDIO, f_off, want)) break;
    volatile const uint8_t *src = RX_BYTES;
    for (uint32_t i = 0; i < want; i++) INBUF[in_len + i] = src[i];
    in_len += (int)want;
    f_off += want;
  }
}

int mp3_start(const char *path, int path_len, uint32_t file_size) {
  if (!inited) {
    hdec = MP3InitDecoder();
    if (!hdec) return -10;
    inited = 1;
  }
  mp3_stop();
  if (file_open(SLOT_AUDIO, path, path_len)) return -1;
  f_size = file_size ? file_size : file_slot_size(SLOT_AUDIO);
  if (f_size < 128) return -2;
  f_off = 0;
  in_len = 0;
  in_ptr = INBUF;
  samples_pushed = 0;
  rs_phase = 0;
  rs_step = 0;
  rs_prev_l = rs_prev_r = 0;
  eof_decode = 0;
  playing = 1;
  paused = 0;
  in_compact_and_fill();
  return 0;
}

void mp3_stop(void) {
  playing = 0;
  paused = 0;
  eof_decode = 0;
}

void mp3_set_paused(int p) { paused = p; }
int mp3_is_playing(void) { return playing && !paused; }

int mp3_at_eof(void) {
  return playing && eof_decode;
}

uint32_t mp3_pos_seconds(void) { return samples_pushed / 48000u; }

// resample `n` interleaved stereo samples at `sr` to 48 kHz, pushing to the fifo
static void resample_push(int n, int sr, int nch) {
  if (rs_step == 0) rs_step = ((uint32_t)sr << 16) / 48000u;
  uint32_t step = rs_step;
  // interpolate between prev-frame last sample and this frame
  while ((rs_phase >> 16) < (uint32_t)n) {
    uint32_t i = rs_phase >> 16;
    uint32_t frac = rs_phase & 0xFFFF;
    short l0, r0, l1, r1;
    if (i == 0) { l0 = rs_prev_l; r0 = rs_prev_r; }
    else if (nch == 2) { l0 = pcm[(i - 1) * 2]; r0 = pcm[(i - 1) * 2 + 1]; }
    else { l0 = r0 = pcm[i - 1]; }
    if (nch == 2) { l1 = pcm[i * 2]; r1 = pcm[i * 2 + 1]; }
    else { l1 = r1 = pcm[i]; }
    int32_t l = l0 + (((int32_t)(l1 - l0) * (int32_t)frac) >> 16);
    int32_t r = r0 + (((int32_t)(r1 - r0) * (int32_t)frac) >> 16);
    pcm_push(((uint32_t)(uint16_t)(int16_t)r << 16) | (uint16_t)(int16_t)l);
    samples_pushed++;
    rs_phase += step;
  }
  rs_phase -= (uint32_t)n << 16;
  if (nch == 2) { rs_prev_l = pcm[(n - 1) * 2]; rs_prev_r = pcm[(n - 1) * 2 + 1]; }
  else rs_prev_l = rs_prev_r = pcm[n - 1];
}

int mp3_pump(void) {
  if (!playing || paused || eof_decode) return 0;

  int worked = 0;
  // keep ~1 frame of headroom: one 44.1k frame → ≤1254 samples at 48k
  while (pcm_free() >= 1400) {
    if (in_len - (in_ptr - INBUF) < 2048) {
      in_compact_and_fill();
      if (in_len < 4 && f_off >= f_size) { eof_decode = 1; break; }
    }
    int avail = in_len - (int)(in_ptr - INBUF);
    int off = MP3FindSyncWord(in_ptr, avail);
    if (off < 0) {  // no sync in buffer: discard and refill
      in_ptr = INBUF + in_len;
      if (f_off >= f_size) { eof_decode = 1; break; }
      continue;
    }
    in_ptr += off;
    avail -= off;
    int bytes_left = avail;
    int err = MP3Decode(hdec, &in_ptr, &bytes_left, pcm, 0);
    if (err) {
      if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
        if (f_off >= f_size) { eof_decode = 1; break; }
        in_compact_and_fill();
        continue;
      }
      in_ptr++;  // bad frame: force progress
      continue;
    }
    MP3FrameInfo fi;
    MP3GetLastFrameInfo(hdec, &fi);
    int nch = fi.nChans ? fi.nChans : 2;
    int n = fi.outputSamps / nch;
    if (n > 0 && fi.samprate > 0) resample_push(n, fi.samprate, nch);
    worked = 1;
  }
  return worked;
}
