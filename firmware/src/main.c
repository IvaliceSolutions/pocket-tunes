// Pocket Tunes firmware — M3b: the amber library browser.
//
// Parses library.json (loaded by the APF data slot), then runs the
// sidebar/albums/drawer UI from the design handoff. No audio yet.

#include <stdint.h>
#include "mmio.h"
#include "gfx.h"
#include "palette.h"
#include "lib.h"
#include "ui.h"

static uint16_t keys_now, keys_prev;

static void error_screen(int code) {
  gfx_clear(COL_BG);
  gfx_text(10, 10, "POCKET TUNES", COL_ARTIST_ACTIVE);
  gfx_hline(10, 28, SCREEN_W - 20, COL_DIVIDER);
  if (code == -2) {
    gfx_text(10, 44, "Aucune bibliotheque.", COL_TITLE);
    gfx_text_small(10, 66, "Lancez pocket-tunes-indexer puis copiez", COL_ARTIST_DIM);
    gfx_text_small(10, 78, "library.json dans /Assets/pockettunes/common/", COL_ARTIST_DIM);
  } else {
    gfx_text(10, 44, "library.json illisible.", COL_TITLE);
    gfx_text_small(10, 66, "Regenerez-la avec pocket-tunes-indexer.", COL_ARTIST_DIM);
    char b[12];
    u32_to_dec((uint32_t)(-code), b);
    gfx_text_small(10, 90, "code:", COL_ARTIST_DIM);
    gfx_text_small(40, 90, b, COL_ARTIST_DIM);
  }
}

int main(void) {
  // instant feedback while the JSON parse runs (~100 ms for a 64 KB library)
  gfx_clear(COL_BG);
  gfx_text(10, 10, "POCKET TUNES", COL_ARTIST_ACTIVE);
  gfx_text_small(10, 32, "chargement de la bibliotheque...", COL_ARTIST_DIM);

  int err = lib_parse();
  if (err) {
    error_screen(err);
    for (;;) wait_vblank();
  }

  ui_init();
  ui_render_full();

  for (;;) {
    wait_vblank();
    keys_prev = keys_now;
    keys_now = (uint16_t)REG_KEYS;
    uint16_t pressed = keys_now & (uint16_t)~keys_prev;

    int redraw = 0;
    if (pressed) redraw |= ui_input(pressed);
    redraw |= ui_tick();

    if (redraw) ui_render_full();
    else ui_render_playing();
  }
  return 0;
}
