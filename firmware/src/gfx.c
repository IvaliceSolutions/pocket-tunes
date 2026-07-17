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

// Vertical gradient fill: the LUT holds one palette index per band, top→bottom.
void gfx_vgrad(int x, int y, int w, int h, const unsigned char *lut, int n) {
  for (int j = 0; j < h; j++)
    gfx_hline(x, y + j, w, lut[j * n / h]);
}

// Filled rectangle with 2px-cut corners — the "4px radius" of design 3a reads
// as a 2px chamfer at our 320x288 scale.
void gfx_fill_round(int x, int y, int w, int h, uint8_t color) {
  for (int j = 0; j < h; j++) {
    int inset = 0;
    if (j == 0 || j == h - 1) inset = 2;
    else if (j == 1 || j == h - 2) inset = 1;
    gfx_hline(x + inset, y + j, w - 2 * inset, color);
  }
}

// 1px outline with the same cut corners (focus borders on rows).
void gfx_rect_round(int x, int y, int w, int h, uint8_t color) {
  gfx_hline(x + 2, y, w - 4, color);
  gfx_hline(x + 2, y + h - 1, w - 4, color);
  gfx_vline(x, y + 2, h - 4, color);
  gfx_vline(x + w - 1, y + 2, h - 4, color);
  px(x + 1, y + 1, color);
  px(x + w - 2, y + 1, color);
  px(x + 1, y + h - 2, color);
  px(x + w - 2, y + h - 2, color);
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

// Font tables hold 191 glyphs: 32..126 then 160..255. Map a codepoint to its
// row, folding the unused 127..159 hole (and anything odd) to '?'.
static int glyph_row(unsigned cp) {
  if (cp >= 32 && cp <= 126) return (int)cp - 32;
  if (cp >= 160 && cp <= 255) return (int)cp - 160 + 95;
  return '?' - 32;
}

// Draw at most `len` bytes, clipped to x < x_max. Returns final x.
static int draw_glyphs(int x, int y, const char *s, int len, int x_max,
                       uint8_t color, const unsigned char *font, int gw, int gh) {
  int i = 0;
  while (i < len && s[i]) {
    unsigned cp = utf8_next(s, len, &i);
    if (x + gw > x_max) break;
    const unsigned char *g = font + glyph_row(cp) * gh;
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
  return draw_glyphs(x, y, s, 0x7FFF, SCREEN_W, color, &fontmain[0][0], FONTMAIN_W, FONTMAIN_H);
}

int gfx_text_small(int x, int y, const char *s, uint8_t color) {
  return draw_glyphs(x, y, s, 0x7FFF, SCREEN_W, color, &fontsmall[0][0], FONTSMALL_W, FONTSMALL_H);
}

int gfx_textn(int x, int y, const char *s, int len, int x_max, uint8_t color) {
  return draw_glyphs(x, y, s, len, x_max, color, &fontmain[0][0], FONTMAIN_W, FONTMAIN_H);
}

int gfx_textn_small(int x, int y, const char *s, int len, int x_max, uint8_t color) {
  return draw_glyphs(x, y, s, len, x_max, color, &fontsmall[0][0], FONTSMALL_W, FONTSMALL_H);
}

// Pixel width the string would occupy (counts glyphs, not bytes — multi-byte
// UTF-8 sequences count as one glyph).
int gfx_text_px_len(const char *s, int len, int small) {
  int gw = small ? FONTSMALL_W : FONTMAIN_W;
  int i = 0, n = 0;
  while (i < len && s[i]) { utf8_next(s, len, &i); n++; }
  return n * gw;
}

// Marquee: draw text inside the window [x, x+box_w), shifted left by scroll_px,
// with per-pixel clipping on BOTH edges (so partial glyphs at the borders don't
// spill). Used to scroll long titles that don't fit.
void gfx_text_scroll(int x, int y, const char *s, int len, int box_w,
                     uint8_t color, int scroll_px, int small) {
  const unsigned char *font = small ? &fontsmall[0][0] : &fontmain[0][0];
  int gw = small ? FONTSMALL_W : FONTMAIN_W;
  int gh = small ? FONTSMALL_H : FONTMAIN_H;
  int cx0 = x, cx1 = x + box_w;
  int gx = x - scroll_px;
  int i = 0;
  while (i < len && s[i]) {
    unsigned cp = utf8_next(s, len, &i);
    if (gx >= cx1) break;             // rest is past the right edge
    if (gx + gw > cx0) {              // at least partly inside the window
      const unsigned char *g = font + glyph_row(cp) * gh;
      for (int r = 0; r < gh; r++) {
        unsigned bits = g[r];
        for (int c = 0; c < gw; c++) {
          int px_x = gx + c;
          if (px_x < cx0 || px_x >= cx1) continue;
          if (bits & (0x80u >> c)) px(px_x, y + r, color);
        }
      }
    }
    gx += gw;
  }
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

// freestanding glue for libopus (fixed-point paths only)
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
void abort(void) { for (;;) {} }
