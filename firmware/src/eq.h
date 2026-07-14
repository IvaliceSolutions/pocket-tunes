// Audio-reactive equalizer: 256-point fixed-point FFT over the decoded PCM,
// folded into 18 log-spaced bands (the drawer's LED-meter strip).
#ifndef EQ_H
#define EQ_H

#include <stdint.h>

#define EQ_BANDS 18
#define EQ_BAR_MAX 34  // bar height in pixels (strip inner height)

// Latest smoothed bar heights, 0..EQ_BAR_MAX.
extern uint8_t eq_bars[EQ_BANDS];

// Feed one mono PCM sample (called from the decoder at 48 kHz).
void eq_feed(int16_t s);

// Recompute the bars from the newest samples. Call once per video frame while
// playing; when paused simply stop calling it — the bars hold their frame.
void eq_tick(void);

#endif
