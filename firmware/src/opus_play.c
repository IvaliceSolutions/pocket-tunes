// Opus playback: file streaming (target commands) → Ogg demux → libopus
// fixed-point decode → PCM fifo. Mirrors mp3.c, minus the resampler (Opus is
// natively 48 kHz).
//
// Memory: libopus' decoder state (~26 KB stereo) comes out of a static arena
// (no real heap, same trick as Helix); its temporary scratch is on the real
// stack via VAR_ARRAYS (build.sh), not the arena. Code lives in SRAM (M7a),
// which is the only reason a ~107 KB decoder fits.

#include "opus_play.h"
#include "ogg.h"
#include "file.h"
#include "mmio.h"
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
static int pump_full;                // emit() sets this when the fifo filled up

// Push one decoded packet's samples into the PCM fifo.
static void emit_packet(const uint8_t *pkt, int len, void *user) {
  (void)user;
  if (!dec || pump_full) return;
  int n = opus_decode(dec, pkt, len, pcm, sizeof pcm / (2 * 2), 0);
  if (n <= 0) return;  // damaged packet: skip it, keep the stream going

  int ch = ogg.channels ? ogg.channels : 2;
  for (int i = 0; i < n; i++) {
    if (skip_samples) { skip_samples--; continue; }  // OpusHead pre-skip
    int16_t l = pcm[ch == 2 ? 2 * i : i];
    int16_t r = pcm[ch == 2 ? 2 * i + 1 : i];
    if (!pcm_free()) { pump_full = 1; return; }
    pcm_push(((uint32_t)(uint16_t)r << 16) | (uint16_t)l);
    samples_pushed++;
  }
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
  pump_full = 0;

  // Keep ~one packet of headroom (60 ms stereo max = 2880 samples).
  while (!pump_full && pcm_free() >= 3000) {
    if (f_off >= f_size) { eof_decode = 1; break; }
    uint32_t want = f_size - f_off;
    if (want > RX_SIZE / 2) want = RX_SIZE / 2;  // 8 KB chunks
    if (file_read(SLOT_AUDIO, f_off, want)) break;

    volatile const uint8_t *src = RX_BYTES;
    for (uint32_t i = 0; i < want; i++) INBUF[i] = src[i];
    f_off += want;

    // pre-skip becomes known once OpusHead is parsed
    uint8_t had_head = ogg.headers_done;
    if (ogg_push(&ogg, INBUF, (int)want, emit_packet, 0) < 0) { eof_decode = 1; break; }
    if (!had_head && ogg.headers_done && samples_pushed == 0) skip_samples = ogg.pre_skip;
    worked = 1;
  }
  return worked;
}

void opus_seek(uint32_t to_seconds, uint32_t byte_off) {
  if (!playing) return;
  if (byte_off >= f_size) byte_off = f_size > 4096 ? f_size - 4096 : 0;
  f_off = byte_off;
  eof_decode = 0;
  samples_pushed = to_seconds * 48000u;
  skip_samples = 0;
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
