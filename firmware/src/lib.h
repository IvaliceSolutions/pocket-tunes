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
  uint32_t dur_s;        // duration in seconds (0 = unknown). 32-bit: a 20 h
                         // audiobook is 72000 s, well past a uint16's 18.2 h.
  uint32_t chap_off;     // byte offset of the first element of the track's
                         // "chapters" array in library.json (0 = no chapters);
                         // starts/titles are re-read on demand, see chap.h
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

// Schema v2 (design 4a): an artist folder can hold albums AND loose tracks
// sitting directly at its root ("rootTracks"); the library root can hold loose
// tracks too. Both are stored in lib_tracks[] like album tracks — global track
// order (= cover file numbering) is: each artist's albums then its root
// tracks, artists in file order, then the library-root tracks last.
typedef struct {
  span_t   name;
  uint16_t first_album;  // index into lib_albums[]
  uint16_t n_albums;     // 0 = no Albums screen: jump straight to the tracks
  uint16_t first_rtrack; // index into lib_tracks[] of the artist's loose tracks
  uint16_t n_rtracks;
} artist_t;

// Trimmed to fund the 2.3 KB cover cache on the tight 128 KB RAM (current
// library: 3 artists / 3 albums / 111 tracks / ~4.7 KB pool).
#define MAX_ARTISTS 40
#define MAX_ALBUMS  72
#define MAX_TRACKS  176
#define POOL_SIZE   6144

extern artist_t lib_artists[MAX_ARTISTS];
extern album_t  lib_albums[MAX_ALBUMS];
extern track_t  lib_tracks[MAX_TRACKS];
extern int lib_n_artists, lib_n_albums, lib_n_tracks;

// Loose tracks at the library root (schema v2), a contiguous range of
// lib_tracks[] parsed after every artist. 0 tracks on a v1 library.
extern int lib_root_first_track, lib_n_root_tracks;

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
