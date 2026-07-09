// library.json parser — zero-copy: all strings are (offset,len) spans into
// the library slot RAM at 0x1000_0000.
#ifndef LIB_H
#define LIB_H

#include <stdint.h>

typedef struct {
  uint32_t off;
  uint16_t len;
} span_t;

enum { FMT_UNK, FMT_MP3, FMT_WAV, FMT_FLAC, FMT_OPUS, FMT_OGG };

typedef struct {
  span_t   title;
  span_t   path;         // audio file path on the SD card (used from M5 on)
  uint16_t dur_s;        // duration in seconds (0 = unknown)
  uint16_t kbps;         // nominal bitrate (0 = unknown)
  uint8_t  format;       // FMT_*
} track_t;

typedef struct {
  span_t   title;
  span_t   genre;        // len 0 = null
  span_t   cover_small;  // relative path, len 0 = none (M5)
  span_t   cover_large;
  uint16_t year;         // 0 = null
  uint16_t hue;          // 0..359 placeholder tint
  uint16_t first_track;  // index into lib_tracks[]
  uint16_t n_tracks;
} album_t;

typedef struct {
  span_t   name;
  uint16_t first_album;  // index into lib_albums[]
  uint16_t n_albums;
} artist_t;

#define MAX_ARTISTS 48
#define MAX_ALBUMS  96
#define MAX_TRACKS  640

extern artist_t lib_artists[MAX_ARTISTS];
extern album_t  lib_albums[MAX_ALBUMS];
extern track_t  lib_tracks[MAX_TRACKS];
extern int lib_n_artists, lib_n_albums, lib_n_tracks;

// Parse the library slot. Returns 0 on success, <0 on error.
int lib_parse(void);

// Resolve a span to a pointer in the slot RAM.
const char *lib_str(span_t s);

const char *lib_format_name(uint8_t fmt);

#endif
