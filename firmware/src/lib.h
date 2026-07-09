// library.json streaming parser.
//
// The JSON streams through the 16 KB RX RAM chunk by chunk (single forward
// scan); every string the UI or the player needs is copied into a string pool
// in CPU RAM. Nothing else of the file stays resident.
#ifndef LIB_H
#define LIB_H

#include <stdint.h>

typedef struct {
  uint16_t off;  // offset into the string pool
  uint16_t len;
} span_t;

enum { FMT_UNK, FMT_MP3, FMT_WAV, FMT_FLAC, FMT_OPUS, FMT_OGG };

typedef struct {
  span_t   title;
  uint32_t path_off;     // byte offset of the path string inside library.json
  uint32_t fsize;        // audio file size in bytes
  uint16_t path_len;
  uint16_t dur_s;        // duration in seconds (0 = unknown)
  uint16_t kbps;         // nominal bitrate (0 = unknown)
  uint8_t  format;       // FMT_*
} track_t;

typedef struct {
  span_t   title;
  span_t   genre;        // len 0 = null
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
#define MAX_TRACKS  384
#define POOL_SIZE   8192

extern artist_t lib_artists[MAX_ARTISTS];
extern album_t  lib_albums[MAX_ALBUMS];
extern track_t  lib_tracks[MAX_TRACKS];
extern int lib_n_artists, lib_n_albums, lib_n_tracks;

// Stream-parse the library data slot. 0 on success, <0 on error.
int lib_parse(void);

const char *lib_str(span_t s);
const char *lib_format_name(uint8_t fmt);

// Re-read a track's path from library.json into out (nul-terminated).
// Returns length, or <0 on error.
int lib_fetch_path(const track_t *t, char *out, int max);

// 12 KB staging buffer, shared with the MP3 input ring (parse and playback
// are never active at the same time).
extern char lib_stage[];

#endif
