// Real cover art for the playing track (drawer + mini-bar).
//
// The indexer writes covers/t{gidx:05}.rgb565 (48x48, RGB565 LE) named by the
// track's enumeration order in library.json — the same order the firmware
// parses — so the path is derived from the global track index and nothing has
// to be stored per track. List thumbnails stay hue placeholders for now (a
// per-row cache doesn't fit the 128 KB RAM).
#ifndef ART_H
#define ART_H

#define ART_SIZE 48  // cover pixels (square)

// Fetch the cover of track `gidx` into the cache via the Art data slot.
// Returns 1 if a cover is now cached, 0 if not (missing file → placeholder).
int art_load(int gidx);

// 1 when the cache holds the cover of the last art_load'ed track.
int art_ready(void);

// Blit the cached cover at (x, y): shift=0 → 48x48, shift=1 → 24x24.
void art_draw(int x, int y, int shift);

// Pixel-doubled blit: 96x96 (Now Playing, design 4a).
void art_draw_2x(int x, int y);

#endif
