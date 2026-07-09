// Pocket Tunes firmware — M3a bring-up screen.
//
// Proves on real hardware: CPU boots and runs from BRAM, draws through the
// palettized framebuffer, reads the D-pad from MMIO, and reads library.json
// from the data-slot RAM. Amber-terminal styling previews the real UI.

#include <stdint.h>
#include "mmio.h"
#include "gfx.h"
#include "palette.h"

#define N_ROWS 6

static uint16_t keys_now, keys_prev;
static inline uint16_t pressed(void) { return keys_now & ~keys_prev; }

static void draw_static(void) {
  gfx_clear(COL_BG);

  // header
  gfx_text(8, 6, "POCKET TUNES", COL_ARTIST_ACTIVE);
  gfx_text_small(8 + 12 * 7 + 6, 10, "M3a soft-cpu online", COL_ARTIST_DIM);
  gfx_hline(8, 22, SCREEN_W - 16, COL_DIVIDER);

  // palette proof: amber ramp + a slice of the color cube
  for (int i = 0; i < 32; i++) gfx_fill_rect(8 + i * 6, 28, 6, 8, (uint8_t)i);
  for (int i = 0; i < 36; i++) gfx_fill_rect(8 + i * 5, 38, 5, 8, (uint8_t)(32 + 180 + i));

  // library.json preview (first 3 x 42 chars), proves CPU reads the slot
  uint32_t lib_bytes = REG_LIB_BYTES;
  gfx_text_small(8, 54, "library.json:", COL_PATH);
  char buf[43];
  if (lib_bytes == 0) {
    gfx_text_small(8, 64, "(not loaded)", COL_ARTIST_DIM);
  } else {
    const volatile char *lib = LIBRARY_BASE;
    for (int line = 0; line < 3; line++) {
      for (int i = 0; i < 42; i++) {
        uint32_t idx = line * 42 + i;
        char c = (idx < lib_bytes) ? lib[idx] : ' ';
        buf[i] = (c >= 32 && c < 127) ? c : ' ';
      }
      buf[42] = 0;
      gfx_text_small(8, 64 + line * 9, buf, COL_TITLE);
    }
  }

  gfx_text_small(8, 96, "^v move   A open   B close", COL_ARTIST_DIM);
}

static void draw_rows(int cursor, int open_row) {
  static const char *names[N_ROWS] = {
      "Amber Wolves", "Night Bus", "Kite & Cove", "Marble Season",
      "Low Tide", "Paper Moths"};
  for (int r = 0; r < N_ROWS; r++) {
    int y = 112 + r * 20;
    gfx_fill_rect(8, y, 200, 18, r == cursor ? COL_CURSOR_BG : COL_BG);
    gfx_rect(8, y, 200, 18, r == cursor ? COL_FOCUS : COL_BG);
    gfx_text(14, y + 2, names[r], r == cursor ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM);
    if (r == open_row) gfx_text_small(214, y + 5, "* OPEN", COL_PLAYSTATE);
  }
}

static void draw_hud(void) {
  char hex[9], dec[11];
  gfx_fill_rect(8, SCREEN_H - 14, SCREEN_W - 16, 10, COL_BG);
  gfx_text_small(8, SCREEN_H - 14, "frame", COL_ARTIST_DIM);
  u32_to_dec(REG_FRAME, dec);
  gfx_text_small(40, SCREEN_H - 14, dec, COL_TITLE);
  gfx_text_small(100, SCREEN_H - 14, "lib bytes", COL_ARTIST_DIM);
  u32_to_dec(REG_LIB_BYTES, dec);
  gfx_text_small(152, SCREEN_H - 14, dec, COL_TITLE);
  gfx_text_small(210, SCREEN_H - 14, "keys", COL_ARTIST_DIM);
  u32_to_hex(keys_now, hex);
  gfx_text_small(238, SCREEN_H - 14, hex, COL_TITLE);
}

int main(void) {
  int cursor = 0, open_row = -1;

  draw_static();
  draw_rows(cursor, open_row);

  for (;;) {
    wait_vblank();
    keys_prev = keys_now;
    keys_now = (uint16_t)REG_KEYS;

    uint16_t p = pressed();
    int dirty = 0;
    if (p & KEY_UP)   { cursor = (cursor + N_ROWS - 1) % N_ROWS; dirty = 1; }
    if (p & KEY_DOWN) { cursor = (cursor + 1) % N_ROWS; dirty = 1; }
    if (p & KEY_A)    { open_row = cursor; dirty = 1; }
    if (p & KEY_B)    { open_row = -1; dirty = 1; }

    if (dirty) draw_rows(cursor, open_row);
    draw_hud();
  }
  return 0;
}
