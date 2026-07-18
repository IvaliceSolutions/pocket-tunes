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
#ifndef PT_HOST_TEST
#include "eq.h"  // equalizer taps the decoded PCM
#endif

#define SLOT_AUDIO 1

// ---- shared input buffer (lib.c exposes its staging buffer when idle)
extern char lib_stage[];          // 12 KB, word-aligned (defined in lib.c)
#define INBUF ((unsigned char *)lib_stage)
#define INBUF_SIZE 12288

// ---- tiny arena for Helix's mallocs (MP3InitDecoder allocates ~20 KB once).
// On the native host test we let libc's malloc serve Helix instead.
#ifndef PT_HOST_TEST
// Codec arena, SHARED with opus_play.c: only one decoder is ever live (starting
// a track stops the other), so they take turns in the same memory instead of
// each reserving its own. Sized for the larger tenant: libopus' decoder state
// is 26520 bytes (opus_decoder_get_size(2)); Helix needs 23864. libopus' own
// scratch is NOT here — VAR_ARRAYS (see build.sh) puts it on the real stack, so
// this is state only. The ~28 KB freed vs the old 57344 becomes stack, which
// the VLAs need (measured peak 10676 bytes).
char codec_arena[27648] __attribute__((aligned(8)));
static uint32_t arena_used;
void *malloc(unsigned long n) {
  n = (n + 7u) & ~7u;
  if (arena_used + n > sizeof(codec_arena)) return 0;
  void *p = &codec_arena[arena_used];
  arena_used += n;
  return p;
}
void free(void *p) { (void)p; }  // decoder is initialized once and kept
unsigned long codec_arena_size(void) { return sizeof(codec_arena); }
#else
#include <stdlib.h>
#endif

// ---- state
static HMP3Decoder hdec;
static int inited;

// Arena ownership: opus_start() resets the shared bump allocator, which
// invalidates Helix's structs. mp3_start() re-inits when it doesn't own it.
#ifndef PT_HOST_TEST
extern int codec_arena_owner;  // 0 = none, 1 = mp3, 2 = opus (opus_play.c)
#endif
static uint32_t f_size, f_off;   // audio file
static uint32_t f_audio_start;   // first byte after the ID3v2 tag
static int in_len;               // valid bytes at INBUF[0..in_len)
static unsigned char *in_ptr;
static int playing, paused, eof_decode;
static uint32_t samples_pushed;  // at 48 kHz
static short pcm[2 * 1152];      // one decoded MP3 frame (interleaved stereo)

// resampler: 16.16 phase through the decoded frame
static uint32_t rs_phase, rs_step;
static short rs_prev_l, rs_prev_r;

// ---- input ring refill -----------------------------------------------------
// M7e: SD reads are ASYNC during playback. A read at a deep offset into a
// 100+ MB file can take tens of ms on the Pocket (FAT chain walking) while
// the PCM fifo holds only 85 ms — the old blocking refill was an audible
// dropout. The ring (12 KB ≈ 300 ms of 320 kbps audio) rides the latency out
// while the read is in flight. f_off advances ONLY when data is absorbed, so
// a read stolen by a blocking op (path/art/chapter fetch) is simply
// re-issued at the same offset.
static uint32_t io_want;  // bytes requested by the in-flight read (0 = none)

static void in_compact(void) {
  int rem = in_len - (int)(in_ptr - INBUF);
  for (int i = 0; i < rem; i++) INBUF[i] = in_ptr[i];
  in_len = rem;
  in_ptr = INBUF;
}

// Non-blocking: absorb a finished read, then start the next one early
// (whenever a chunk's worth of room exists — keep the ring FULL, don't wait
// until it's nearly empty).
static void in_refill_async(void) {
  if (io_want) {
    int r = file_read_poll();
    if (r == FILE_POLL_BUSY) return;
    if (r == FILE_POLL_DONE) {
      in_compact();
      volatile const uint8_t *src = RX_BYTES;
      for (uint32_t i = 0; i < io_want; i++) INBUF[in_len + i] = src[i];
      in_len += (int)io_want;
      f_off += io_want;
    }
    io_want = 0;  // done, stolen or error: idle either way (retry below)
  }
  if (f_off >= f_size) return;
  in_compact();
  uint32_t room = (uint32_t)(INBUF_SIZE - in_len);
  if (room < RX_SIZE / 2) return;
  uint32_t want = f_size - f_off;
  if (want > RX_SIZE / 2) want = RX_SIZE / 2;  // 8 KB chunks
  if (file_read_start(SLOT_AUDIO, f_off, want) == 0) io_want = want;
}

// Blocking prime for start/seek (user-initiated; not the steady-state path).
// file_read steals any in-flight async read; the next pump's poll sees
// STOLEN and re-issues cleanly.
static void in_compact_and_fill(void) {
  in_compact();
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
#ifndef PT_HOST_TEST
  if (codec_arena_owner != 1) {  // Opus (or nobody) held the arena → rebuild
    arena_used = 0;
    inited = 0;
    codec_arena_owner = 1;
  }
#endif
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
  // Skip an ID3v2 tag up front. Its size is declared in the header, so jump
  // past it wholesale — otherwise MP3FindSyncWord scans into the tag's embedded
  // cover-art JPEG, whose 0xFFEx markers (e.g. 0xFFE0 JFIF) look exactly like
  // MP3 frame syncs, and the decoder grinds byte-by-byte and never plays.
  if (!file_read(SLOT_AUDIO, 0, 10)) {
    volatile const uint8_t *h = RX_BYTES;
    if (h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
      uint32_t sz = ((uint32_t)(h[6] & 0x7F) << 21) | ((uint32_t)(h[7] & 0x7F) << 14) |
                    ((uint32_t)(h[8] & 0x7F) << 7) | (uint32_t)(h[9] & 0x7F);
      uint32_t tag = 10u + sz + ((h[5] & 0x10) ? 10u : 0u);  // +footer if present
      if (tag < f_size) f_off = tag;
    }
  }
  f_audio_start = f_off;
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

int mp3_at_eof(void) {
  return playing && eof_decode;
}

uint32_t mp3_pos_seconds(void) { return samples_pushed / 48000u; }

void mp3_seek(uint32_t to_seconds, uint32_t byte_off) {
  if (!playing) return;
  if (byte_off < f_audio_start) byte_off = f_audio_start;  // never seek into the ID3 tag
  // clamp so there's at least a frame of data left to decode
  if (byte_off + 512 >= f_size) byte_off = f_size > 2048 ? f_size - 2048 : 0;
  f_off = byte_off;
  in_len = 0;
  in_ptr = INBUF;
  eof_decode = 0;
  samples_pushed = to_seconds * 48000u;  // keeps mp3_pos_seconds() consistent
  rs_phase = 0;
  rs_prev_l = rs_prev_r = 0;
  in_compact_and_fill();  // refill the input window from the new offset
  // mp3_pump() will MP3FindSyncWord() from here and resync within a frame.
}

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
#ifndef PT_HOST_TEST
    eq_feed((int16_t)((l + r) >> 1));  // mono tap for the drawer equalizer
#endif
    samples_pushed++;
    rs_phase += step;
  }
  rs_phase -= (uint32_t)n << 16;
  if (nch == 2) { rs_prev_l = pcm[(n - 1) * 2]; rs_prev_r = pcm[(n - 1) * 2 + 1]; }
  else rs_prev_l = rs_prev_r = pcm[n - 1];
}

int mp3_pump(void) {
  if (!playing || paused || eof_decode) return 0;

#ifndef PT_HOST_TEST
  {  // audible-gap detector: fifo ran empty while we were supposed to play
    extern uint32_t codec_underrun_count;
    static int was_empty;
    if (samples_pushed && pcm_free() >= 4095) {
      if (!was_empty) { codec_underrun_count++; was_empty = 1; }
    } else was_empty = 0;
  }
#endif

  int worked = 0;
  int frames = 0;
  in_refill_async();  // absorb a finished SD read / start the next one early
  // keep ~1 frame of headroom: one 44.1k frame → ≤1254 samples at 48k.
  // At most 2 frames per call: after a seek the fifo is empty and an
  // unbounded loop decodes 3-4 frames back to back (~50 ms) — long enough
  // for the main loop to miss a quick button tap.
  while (frames < 2 && pcm_free() >= 1400) {
    int avail = in_len - (int)(in_ptr - INBUF);
    if (avail < 2048) {
      if (f_off >= f_size && !io_want) {
        if (avail < 4) { eof_decode = 1; break; }
        // tail of the file: decode what remains
      } else if (avail < 2048) {
        // low input, refill in flight: let the fifo carry us, come back
        break;
      }
    }
    avail = in_len - (int)(in_ptr - INBUF);
    int off = MP3FindSyncWord(in_ptr, avail);
    if (off < 0) {  // no sync in buffer: discard and refill
      in_ptr = INBUF + in_len;
      if (f_off >= f_size && !io_want) { eof_decode = 1; break; }
      break;
    }
    in_ptr += off;
    avail -= off;
    int bytes_left = avail;
    int err = MP3Decode(hdec, &in_ptr, &bytes_left, pcm, 0);
    if (err) {
      if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
        if (f_off >= f_size && !io_want) { eof_decode = 1; break; }
        break;  // needs more input: the async refill will bring it
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
    frames++;
  }
  return worked;
}
