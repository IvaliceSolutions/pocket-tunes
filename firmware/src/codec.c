#include <stdint.h>
#include "codec.h"
#include "lib.h"
#include "mp3.h"
#include "opus_play.h"

enum { C_NONE, C_MP3, C_OPUS };
static int active = C_NONE;

// PCM fifo underruns (audible gaps): counted by the codec pumps on the
// empty-while-playing transition. For the debug HUD.
uint32_t codec_underrun_count;

// ---- instruction-TCM overlay (M7e) ----------------------------------------
// The 16 KB TCM at 0x6000_0000 holds the ACTIVE codec's hot kernels. Both
// codecs' overlays sit in the SRAM firmware image at these LMAs; codec_start
// word-copies the right one in before decoding. Which overlay is resident
// tracks `loaded` so re-starting the same codec skips the copy.
#ifndef PT_HOST_TEST
extern uint32_t __itcm_load_opus[], __itcm_load_mp3[];
extern uint32_t __itcm_opus_size, __itcm_mp3_size;  // linker: byte sizes
#define ITCM_BASE ((volatile uint32_t *)0x60000000u)
static int itcm_loaded;  // 0 none, 1 mp3, 2 opus

static void itcm_load(const uint32_t *src, uint32_t bytes) {
  volatile uint32_t *dst = ITCM_BASE;
  uint32_t nwords = (bytes + 3u) >> 2;
  for (uint32_t i = 0; i < nwords; i++) dst[i] = src[i];
  // Both overlays share the TCM addresses (0x6000_0000+), so the 4 KB I-cache
  // may still hold the PREVIOUS codec's lines for this region. fence.i makes
  // VexRiscv's IBusCachedPlugin flush the cache, so the next fetch refills the
  // freshly-copied overlay from the TCM instead of stale bytes.
  __asm__ volatile(".word 0x0000100f" ::: "memory");  /* fence.i (raw: -march has no zifencei) */
}
#endif

int codec_start(uint8_t format, const char *path, int path_len, uint32_t file_size) {
  codec_stop();
  if (format == FMT_MP3) {
#ifndef PT_HOST_TEST
    if (itcm_loaded != 1) {
      itcm_load(__itcm_load_mp3, (uint32_t)(uintptr_t)&__itcm_mp3_size);
      itcm_loaded = 1;
    }
#endif
    active = C_MP3;
    return mp3_start(path, path_len, file_size);
  }
  if (format == FMT_OPUS || format == FMT_OGG) {
#ifndef PT_HOST_TEST
    if (itcm_loaded != 2) {
      itcm_load(__itcm_load_opus, (uint32_t)(uintptr_t)&__itcm_opus_size);
      itcm_loaded = 2;
    }
#endif
    active = C_OPUS;
    return opus_start(path, path_len, file_size);
  }
  active = C_NONE;
  return -21;  // unsupported format
}

void codec_stop(void) {
  if (active == C_MP3) mp3_stop();
  else if (active == C_OPUS) opus_stop();
  active = C_NONE;
}

void codec_set_paused(int paused) {
  if (active == C_MP3) mp3_set_paused(paused);
  else if (active == C_OPUS) opus_set_paused(paused);
}

int codec_at_eof(void) {
  if (active == C_MP3) return mp3_at_eof();
  if (active == C_OPUS) return opus_at_eof();
  return 0;
}

int codec_pump(void) {
  if (active == C_MP3) return mp3_pump();
  if (active == C_OPUS) return opus_pump();
  return 0;
}

uint32_t codec_pos_seconds(void) {
  if (active == C_MP3) return mp3_pos_seconds();
  if (active == C_OPUS) return opus_pos_seconds();
  return 0;
}

void codec_seek(uint32_t to_seconds, uint32_t byte_off) {
  if (active == C_MP3) mp3_seek(to_seconds, byte_off);
  else if (active == C_OPUS) opus_seek(to_seconds, byte_off);
}

int codec_is_seekable(void) { return active != C_NONE; }
