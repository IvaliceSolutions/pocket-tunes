#include "gfx.h"
#include "mmio.h"
#include "fonts.h"

// word-wise fill for speed (4 pixels per store)
void gfx_clear(uint8_t color) {
  uint32_t w = color | (color << 8) | ((uint32_t)color << 16) | ((uint32_t)color << 24);
  volatile uint32_t *p = FB_BASE32;
  for (int i = 0; i < SCREEN_W * SCREEN_H / 4; i++) p[i] = w;
}

static inline void px(int x, int y, uint8_t c) {
  FB_BASE[y * SCREEN_W + x] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) px(x + i, y + j, color);
}

void gfx_hline(int x, int y, int w, uint8_t color) { gfx_fill_rect(x, y, w, 1, color); }
void gfx_vline(int x, int y, int h, uint8_t color) { gfx_fill_rect(x, y, 1, h, color); }

void gfx_rect(int x, int y, int w, int h, uint8_t color) {
  gfx_hline(x, y, w, color);
  gfx_hline(x, y + h - 1, w, color);
  gfx_vline(x, y, h, color);
  gfx_vline(x + w - 1, y, h, color);
}

// Decode one UTF-8 sequence to a Latin-1 codepoint ('?' if outside 32..255).
// Advances *i past the sequence.
static unsigned utf8_next(const char *s, int len, int *i) {
  unsigned b0 = (unsigned char)s[(*i)++];
  if (b0 < 0x80) return (b0 >= 32) ? b0 : '?';
  if ((b0 & 0xE0) == 0xC0 && *i < len) {
    unsigned b1 = (unsigned char)s[(*i)++];
    unsigned cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
    return (cp >= 32 && cp <= 255) ? cp : '?';
  }
  // 3/4-byte sequences: skip continuations, render '?'
  while (*i < len && ((unsigned char)s[*i] & 0xC0) == 0x80) (*i)++;
  return '?';
}

// Draw at most `len` bytes, clipped to x < x_max. Returns final x.
static int draw_glyphs(int x, int y, const char *s, int len, int x_max,
                       uint8_t color, const unsigned char *font, int gw, int gh) {
  int i = 0;
  while (i < len && s[i]) {
    unsigned cp = utf8_next(s, len, &i);
    if (x + gw > x_max) break;
    const unsigned char *g = font + (cp - 32) * gh;
    for (int r = 0; r < gh; r++) {
      unsigned bits = g[r];
      for (int c = 0; c < gw; c++)
        if (bits & (0x80u >> c)) px(x + c, y + r, color);
    }
    x += gw;
  }
  return x;
}

int gfx_text(int x, int y, const char *s, uint8_t color) {
  return draw_glyphs(x, y, s, 0x7FFF, SCREEN_W, color, &font7x13[0][0], FONT7X13_W, FONT7X13_H);
}

int gfx_text_small(int x, int y, const char *s, uint8_t color) {
  return draw_glyphs(x, y, s, 0x7FFF, SCREEN_W, color, &font5x8[0][0], FONT5X8_W, FONT5X8_H);
}

int gfx_textn(int x, int y, const char *s, int len, int x_max, uint8_t color) {
  return draw_glyphs(x, y, s, len, x_max, color, &font7x13[0][0], FONT7X13_W, FONT7X13_H);
}

int gfx_textn_small(int x, int y, const char *s, int len, int x_max, uint8_t color) {
  return draw_glyphs(x, y, s, len, x_max, color, &font5x8[0][0], FONT5X8_W, FONT5X8_H);
}

void u32_to_hex(uint32_t v, char out[9]) {
  for (int i = 7; i >= 0; i--) {
    unsigned d = v & 0xF;
    out[i] = d < 10 ? '0' + d : 'a' + d - 10;
    v >>= 4;
  }
  out[8] = 0;
}

void u32_to_dec(uint32_t v, char out[11]) {
  char tmp[11];
  int n = 0;
  do { tmp[n++] = '0' + (v % 10); v /= 10; } while (v);
  for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
  out[n] = 0;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--) *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, unsigned long n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d < s) while (n--) *d++ = *s++;
  else { d += n; s += n; while (n--) *--d = *--s; }
  return dst;
}

// freestanding memset — gcc emits calls to it for struct zero-initializers
void *memset(void *dst, int c, unsigned long n) {
  unsigned char *d = dst;
  while (n--) *d++ = (unsigned char)c;
  return dst;
}
