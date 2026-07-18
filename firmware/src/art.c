// Cover-art loader: covers/t{gidx:05}.rgb565 → framebuffer. See art.h.
//
// The source file is 96x96 RGB565 LE (18432 B, indexer v2) or the older
// 48x48 (4608 B), told apart by file size. RGB565 is quantized to the
// palette's 6x6x6 cube on the way out. The RX RAM is shared with the audio
// streamer, but accesses are strictly sequential (single-threaded firmware)
// and the streamer's input was already copied out — the next pump refill
// simply reuses RX afterwards.

#include "art.h"
#include "file.h"
#include "gfx.h"
#include "mmio.h"
#include "palette.h"

#define SLOT_ART 2
#define COVER_DIR "/Assets/pockettunes/common/covers/t"

static uint8_t mini_px[ART_MINI * ART_MINI];  // mini-bar thumbnail cache
static int cover_ok;
static int src_px;  // source cover size: 96 or 48

int art_ready(void) { return cover_ok; }

static uint8_t quant(unsigned lo, unsigned hi) {
  unsigned v = lo | (hi << 8);
  unsigned r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
  return (uint8_t)PAL_CUBE((r5 * 3) >> 4, (g6 * 3) >> 5, (b5 * 3) >> 4);
}

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
  uint32_t fsz = file_slot_size(SLOT_ART);
  if (fsz >= (uint32_t)(ART_BIG * ART_BIG * 2)) src_px = 96;
  else if (fsz >= 48 * 48 * 2) src_px = 48;
  else return 0;

  // cache the mini thumbnail: every (src_px/24)th pixel of every such row
  int step = src_px / ART_MINI;
  for (int j = 0; j < ART_MINI; j++) {
    // one source row per mini row (2 reads happen to stay within RX anyway,
    // but row-at-a-time keeps this trivially correct for both sizes)
    if (file_read(SLOT_ART, (uint32_t)(j * step) * src_px * 2, (uint32_t)src_px * 2))
      return 0;
    volatile const uint8_t *src = RX_BYTES;
    for (int i = 0; i < ART_MINI; i++)
      mini_px[j * ART_MINI + i] = quant(src[2 * i * step], src[2 * i * step + 1]);
  }
  cover_ok = 1;
  return 1;
}

void art_draw_big(int x, int y) {
  if (!cover_ok) return;
  // stream the file in 48-row chunks through RX (96px: 2 x 9216 B; 48px: 1 x 4608 B)
  int rows_per = 48;
  int dup = ART_BIG / src_px;  // 1 (96px source) or 2 (48px, pixel-doubled)
  for (int base = 0; base < src_px; base += rows_per) {
    int nrows = src_px - base;
    if (nrows > rows_per) nrows = rows_per;
    if (file_read(SLOT_ART, (uint32_t)base * src_px * 2,
                  (uint32_t)nrows * src_px * 2))
      return;
    volatile const uint8_t *src = RX_BYTES;
    for (int j = 0; j < nrows; j++) {
      for (int rep = 0; rep < dup; rep++) {
        volatile uint8_t *dst =
            &FB_BASE[(y + (base + j) * dup + rep) * SCREEN_W + x];
        for (int i = 0; i < src_px; i++) {
          uint8_t c = quant(src[(j * src_px + i) * 2], src[(j * src_px + i) * 2 + 1]);
          for (int k = 0; k < dup; k++) dst[i * dup + k] = c;
        }
      }
    }
  }
}

void art_draw_mini(int x, int y) {
  if (!cover_ok) return;
  for (int j = 0; j < ART_MINI; j++) {
    volatile uint8_t *dst = &FB_BASE[(y + j) * SCREEN_W + x];
    for (int i = 0; i < ART_MINI; i++) dst[i] = mini_px[j * ART_MINI + i];
  }
}
