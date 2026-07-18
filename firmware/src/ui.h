// iPod Classic-style UI (design handoff 4a): state machine + renderer.
// Screen stack Bibliothèque → Albums → Titres → Lecture; playback is
// independent of the browse cursors. See ui.c's header for the button map.
#ifndef UI_H
#define UI_H

#include <stdint.h>

void ui_init(void);
int ui_input(uint16_t pressed);  // returns 1 if a redraw is needed
int ui_tick(void);               // per-frame; returns 1 if a redraw is needed
void ui_render_full(void);
void ui_render_playing(void);    // cheap per-frame progress-bar update

// Savestate (sleep/wake): serialize/restore the whole player state.
#define SS_WORDS 16
void ui_get_state(uint32_t w[SS_WORDS]);
void ui_apply_state(const uint32_t w[SS_WORDS]);

#endif
