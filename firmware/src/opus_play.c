// Opus playback: file streaming (target commands) → Ogg demux → libopus
// fixed-point decode → PCM fifo. Mirrors mp3.c, minus the resampler (Opus is
// natively 48 kHz).
//
// Flow control: the PCM fifo holds 85 ms but one 8 KB chunk decodes to ~0.5 s
// of audio, so the demuxer must be able to PAUSE mid-chunk — ogg_push
// suspends on the packet emit refuses and re-presents it next time, and the
// decoded-but-unpushed samples of the current packet wait in pcm[]/pend.
// Nothing is dropped (the old emit threw away every chunk's tail → crackle).
// A per-call budget also bounds how long one pump can hog the CPU, so button
// sampling in the main loop never starves.
//
// Memory: libopus' decoder state (~26 KB stereo) comes out of the codec
// arena shared with mp3.c (no real heap, same trick as Helix); its temporary
// scratch is on the real stack via VAR_ARRAYS (build.sh), not the arena.
// Code lives in SRAM (M7a), which is the only reason a ~107 KB decoder fits.

#include "opus_play.h"
#include "ogg.h"
#include "file.h"
#include "mmio.h"
#include "eq.h"
#include <opus.h>

#define SLOT_AUDIO 1

// Shared 12 KB staging buffer (lib.c's; parse and playback never overlap)
extern char lib_stage[];
#define INBUF ((unsigned char *)lib_stage)
#define INBUF_SIZE 12288

// ---- libopus allocations come from the codec arena shared with mp3.c: only
// one decoder is live at a time, so the memory is reused rather than doubled.
// Reset on every start so re-opening a track can't creep the bump pointer.
#ifndef PT_HOST_TEST
extern char codec_arena[];
extern unsigned long codec_arena_size(void);
int codec_arena_owner;  // 0 = none, 1 = mp3, 2 = opus — see mp3.c
static uint32_t opus_used;
void *opus_arena_alloc(unsigned long n) {
  n = (n + 7u) & ~7u;
  if (opus_used + n > codec_arena_size()) return 0;
  void *p = &codec_arena[opus_used];
  opus_used += n;
  return p;
}
static void opus_arena_reset(void) { opus_used = 0; }
#else
void *opus_arena_alloc(unsigned long n);
static void opus_arena_reset(void) {}
#endif

static OpusDecoder *dec;
static ogg_t ogg;
static uint32_t f_size, f_off;
static int playing, paused, eof_decode;
static uint32_t samples_pushed;      // at 48 kHz
static uint32_t skip_samples;        // OpusHead pre-skip, dropped on start
static int16_t pcm[960 * 2 * 3];     // up to 60 ms stereo @ 48 kHz
static int pend_n, pend_i;           // decoded samples not yet in the fifo
static uint32_t in_len, in_off;      // current chunk window inside INBUF
static int pump_budget;              // samples this pump call may still push

// Push pending decoded samples into the fifo. 1 = fifo full, some remain.
static int flush_pending(void) {
  int ch = ogg.channels ? ogg.channels : 2;
  while (pend_i < pend_n) {
    if (skip_samples) {  // OpusHead pre-skip
      skip_samples--;
      pend_i++;
      continue;
    }
    if (!pcm_free()) return 1;
    int16_t l = pcm[ch == 2 ? 2 * pend_i : pend_i];
    int16_t r = pcm[ch == 2 ? 2 * pend_i + 1 : pend_i];
    pcm_push(((uint32_t)(uint16_t)r << 16) | (uint16_t)l);
#ifndef PT_HOST_TEST
    eq_feed((int16_t)(((int)l + (int)r) >> 1));  // mono tap for the equalizer
#endif
    samples_pushed++;
    pend_i++;
    if (pump_budget > 0) pump_budget--;
  }
  return 0;
}

// ogg_push callback: 0 = packet taken, 1 = park it (fifo full / budget spent)
static int emit_packet(const uint8_t *pkt, int len, void *user) {
  (void)user;
  if (!dec) return 0;   // no decoder: swallow, keep the stream moving
  if (pend_i < pend_n)  // re-presented after a suspend: these are its samples
    return flush_pending();
  if (pump_budget <= 0) return 1;  // cap reached: decode it next pump
  int n = opus_decode(dec, pkt, len, pcm, sizeof pcm / (2 * 2), 0);
  if (n <= 0) return 0;  // damaged packet: skip it, keep the stream going
  pend_n = n;
  pend_i = 0;
  return flush_pending();
}

int opus_start(const char *path, int path_len, uint32_t file_size) {
  opus_stop();
  // The MP3 side may have owned the arena; take it over from scratch.
  opus_arena_reset();
#ifndef PT_HOST_TEST
  codec_arena_owner = 2;
#endif
  dec = (OpusDecoder *)opus_arena_alloc((unsigned long)opus_decoder_get_size(2));
  if (!dec) return -10;
  if (opus_decoder_init(dec, 48000, 2) != OPUS_OK) return -11;
  if (file_open(SLOT_AUDIO, path, path_len)) return -1;
  f_size = file_size ? file_size : file_slot_size(SLOT_AUDIO);
  if (f_size < 128) return -2;
  f_off = 0;
  samples_pushed = 0;
  skip_samples = 0;
  pend_n = pend_i = 0;
  in_len = in_off = 0;
  eof_decode = 0;
  playing = 1;
  paused = 0;
  ogg_init(&ogg);
  return 0;
}

void opus_stop(void) { playing = 0; paused = 0; eof_decode = 0; }
void opus_set_paused(int p) { paused = p; }
int opus_at_eof(void) { return playing && eof_decode; }
uint32_t opus_pos_seconds(void) { return samples_pushed / 48000u; }

int opus_pump(void) {
  if (!playing || paused || eof_decode) return 0;
  int worked = 0;
  pump_budget = 2880;  // ≤60 ms of audio per call: keep the UI loop live

  // NOTE: leftover pend samples are NOT flushed here. A partial flush always
  // leaves the demuxer suspended on that same packet, and the resume path
  // below re-presents it to emit_packet, which finishes the flush and
  // reports it consumed. Flushing early would clear pend and make the
  // re-presented packet look new — it would be decoded (and heard) twice.

  for (;;) {
    if (in_off < in_len) {  // (resume) demux the current chunk
      uint8_t had_head = ogg.headers_done;
      int r = ogg_push(&ogg, INBUF + in_off, (int)(in_len - in_off),
                       emit_packet, 0);
      if (r < 0) { eof_decode = 1; return 1; }
      if (r > 0) worked = 1;
      in_off += (uint32_t)r;
      if (!had_head && ogg.headers_done && samples_pushed == 0)
        skip_samples = ogg.pre_skip;
      if (in_off < in_len || ogg.suspended || pump_budget <= 0)
        return worked;  // fifo full or budget spent — resume next call
    }
    // chunk exhausted: read the next one
    if (f_off >= f_size) {
      if (!ogg.suspended && pend_i >= pend_n) eof_decode = 1;
      return 1;
    }
    if (pcm_free() < 1200) return worked;  // let the fifo drain first
    uint32_t want = f_size - f_off;
    if (want > RX_SIZE / 2) want = RX_SIZE / 2;  // 8 KB chunks
    if (file_read(SLOT_AUDIO, f_off, want)) return worked;

    // Word copy out of the RX window: `want` is always a multiple of 4
    // (RX_SIZE/2 chunks) and both buffers are word-aligned. This loop is hot
    // (per profile), so 1 op per 4 bytes instead of per byte matters.
    volatile const uint32_t *srcw = RX_BASE;
    uint32_t *dstw = (uint32_t *)INBUF;
    uint32_t nwords = (want + 3u) >> 2;
    for (uint32_t i = 0; i < nwords; i++) dstw[i] = srcw[i];
    f_off += want;
    in_len = want;
    in_off = 0;
    worked = 1;
  }
}

void opus_seek(uint32_t to_seconds, uint32_t byte_off) {
  if (!playing) return;
  if (byte_off >= f_size) byte_off = f_size > 4096 ? f_size - 4096 : 0;
  f_off = byte_off;
  eof_decode = 0;
  samples_pushed = to_seconds * 48000u;
  skip_samples = 0;
  pend_n = pend_i = 0;  // decoded leftovers belong to the old position
  in_len = in_off = 0;  // so does the chunk window
  // Resync: Ogg pages are self-framing (OggS capture pattern), so the demuxer
  // finds the next page boundary on its own; the decoder rebuilds its state
  // from the first intra packet. Keep the channel/pre-skip info we already have.
  uint16_t keep_skip = ogg.pre_skip;
  uint8_t keep_ch = ogg.channels;
  ogg_init(&ogg);
  ogg.pre_skip = keep_skip;
  ogg.channels = keep_ch;
  ogg.headers_done = 1;  // mid-stream: no headers to wait for
  if (dec) opus_decoder_ctl(dec, OPUS_RESET_STATE);
}
