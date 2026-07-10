// Minimal drawing library over the 8bpp indexed framebuffer.
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

void gfx_clear(uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);  // 1px outline

// 7x13 primary font / 5x8 small font. UTF-8 → Latin-1. Return x after the
// last glyph. The *n variants draw at most len bytes, clipped at x_max.
int gfx_text(int x, int y, const char *s, uint8_t color);
int gfx_text_small(int x, int y, const char *s, uint8_t color);
int gfx_textn(int x, int y, const char *s, int len, int x_max, uint8_t color);
int gfx_textn_small(int x, int y, const char *s, int len, int x_max, uint8_t color);

// Pixel width of a string (glyph count × cell width). small=1 uses the 5x8 font.
int gfx_text_px_len(const char *s, int len, int small);
// Marquee draw inside [x, x+box_w), shifted left by scroll_px, clipped both edges.
void gfx_text_scroll(int x, int y, const char *s, int len, int box_w,
                     uint8_t color, int scroll_px, int small);

void u32_to_hex(uint32_t v, char out[9]);
void u32_to_dec(uint32_t v, char out[11]);

#endif
