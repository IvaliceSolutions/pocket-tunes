// Cover-art loader: covers/t{gidx:05}.rgb565 → palette-indexed cache → blit.
//
// One 48x48 cover is cached (2304 B) — the playing track's. The whole file
// (4608 B of RGB565 LE) fits in a single target-command read into the 16 KB
// RX RAM; RGB565 is quantized to the palette's 6x6x6 cube on the way out.
// The RX RAM is shared with the MP3 streamer, but accesses are strictly
// sequential (single-threaded firmware) and the streamer's input buffer was
// already copied out — the next pump refill simply reuses RX afterwards.

#include "art.h"
#include "file.h"
#include "gfx.h"
#include "mmio.h"
#include "palette.h"

#define SLOT_ART 2
#define COVER_DIR "/Assets/pockettunes/common/covers/t"

static uint8_t cover_px[ART_SIZE * ART_SIZE];  // palette indices
static int cover_ok;

int art_ready(void) { return cover_ok; }

int art_load(int gidx) {
  cover_ok = 0;
  if (gidx < 0) return 0;

  // "/Assets/.../covers/t00042.rgb565"
  char path[sizeof COVER_DIR + 16];
  char *p = path;
  const char *s = COVER_DIR;
  while (*s) *p++ = *s++;
  char d[11];
  u32_to_dec((uint32_t)gidx, d);
  int n = 0;
  while (d[n]) n++;
  for (int i = 0; i < 5 - n; i++) *p++ = '0';
  for (int i = 0; i < n; i++) *p++ = d[i];
  s = ".rgb565";
  while (*s) *p++ = *s++;
  *p = 0;

  if (file_open(SLOT_ART, path, (int)(p - path))) return 0;  // no cover file
  if (file_read(SLOT_ART, 0, ART_SIZE * ART_SIZE * 2)) return 0;

  // RGB565 little-endian → 6x6x6 cube indices
  volatile const uint8_t *src = RX_BYTES;
  for (int i = 0; i < ART_SIZE * ART_SIZE; i++) {
    unsigned v = (unsigned)src[2 * i] | ((unsigned)src[2 * i + 1] << 8);
    unsigned r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
    cover_px[i] = (uint8_t)PAL_CUBE((r5 * 3) >> 4, (g6 * 3) >> 5, (b5 * 3) >> 4);
  }
  cover_ok = 1;
  return 1;
}

void art_draw(int x, int y, int shift) {
  int n = ART_SIZE >> shift;
  for (int j = 0; j < n; j++) {
    const uint8_t *row = &cover_px[(j << shift) * ART_SIZE];
    volatile uint8_t *dst = &FB_BASE[(y + j) * SCREEN_W + x];
    for (int i = 0; i < n; i++) dst[i] = row[i << shift];
  }
}
