// Real cover art for the playing track (Lecture + mini-bar).
//
// The indexer writes covers/t{gidx:05}.rgb565 (RGB565 LE, no header) named by
// the track's enumeration order in library.json — the same order the firmware
// parses — so the path is derived from the global track index and nothing has
// to be stored per track. Since v2 the file is 96x96 (18432 B); older 48x48
// (4608 B) files are detected by size and pixel-doubled, so a card indexed
// with the previous indexer still shows its covers.
//
// Memory: no full-size cache — 96x96 would cost 9 KB of the tight BRAM. The
// Lecture blit streams the file through the RX window on each full render
// (a keypress-rate event); only a 24x24 thumbnail for the mini-bar is cached.
#ifndef ART_H
#define ART_H

#define ART_BIG 96   // Lecture cover, drawn pixels
#define ART_MINI 24  // mini-bar thumbnail (cached)

// Open the cover of track `gidx` and cache the mini thumbnail.
// Returns 1 if a cover is available, 0 if not (missing file → placeholder).
int art_load(int gidx);

// 1 when art_load found a cover for the current track.
int art_ready(void);

// Stream-blit the cover at 96x96 (Lecture). Reads the file via the RX window;
// call only from full renders, never per frame.
void art_draw_big(int x, int y);

// Blit the cached 24x24 thumbnail (mini-bar) — cheap, fine per frame.
void art_draw_mini(int x, int y);

#endif
