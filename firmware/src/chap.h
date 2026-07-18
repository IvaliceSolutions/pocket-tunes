// ID3 chapter navigation (audiobooks) — L/R jump between chapters.
//
// The indexer emits per-track `"chapters": [{"s": startSec, "t": "title"}]`
// in library.json; lib.c only records the array's byte offset (track_t
// .chap_off). This module re-reads the array from the file: the start times
// of the PLAYING track are cached here (one chap_load per track start), the
// title of a single chapter is fetched on demand when it changes on screen.
// Nothing is resident per library track.
#ifndef CHAP_H
#define CHAP_H

#include <stdint.h>
#include "lib.h"

// 160 chapters ≈ a 20 h audiobook cut every 7-8 min. Beyond that the extra
// chapters are unreachable by L/R but playback is unaffected.
#define CHAP_MAX 160

extern int chap_count;                  // chapters cached (0 = none)
extern uint32_t chap_start_s[CHAP_MAX]; // start second of each chapter

// Cache the start times of `t`'s chapters. Returns the count (0 if the track
// has none), <0 on a read/parse error (cache is then empty).
int chap_load(const track_t *t);

// Fetch chapter `idx`'s title into out (nul-terminated, UTF-8 like the pool
// strings). Returns the length, 0 if the chapter is unnamed, <0 on error.
int chap_title(const track_t *t, int idx, char *out, int max);

// Chapter containing playback second `pos_s` (last chapter whose start is
// <= pos_s), or -1 if nothing is cached.
int chap_index_for(uint32_t pos_s);

#endif
