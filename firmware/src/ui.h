// Browser UI state machine + renderer (design handoff 1b).
#ifndef UI_H
#define UI_H

#include <stdint.h>

void ui_init(void);
int ui_input(uint16_t pressed);  // returns 1 if a redraw is needed
int ui_tick(void);               // per-frame; returns 1 if a redraw is needed
void ui_render_full(void);
void ui_render_playing(void);    // cheap per-frame progress-bar update

#endif
