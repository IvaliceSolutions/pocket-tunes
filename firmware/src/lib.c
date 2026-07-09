// Minimal recursive-descent parser for library.json (see docs/library-format.md).
// Tolerant: unknown keys are skipped, caps are clamped, and any structural
// error aborts with a negative code (the UI shows a "regenerate" message).

#include "lib.h"
#include "mmio.h"

artist_t lib_artists[MAX_ARTISTS];
album_t  lib_albums[MAX_ALBUMS];
track_t  lib_tracks[MAX_TRACKS];
int lib_n_artists, lib_n_albums, lib_n_tracks;

static const char *buf;
static uint32_t pos, end;

const char *lib_str(span_t s) { return (const char *)LIBRARY_BASE + s.off; }

const char *lib_format_name(uint8_t fmt) {
  switch (fmt) {
    case FMT_MP3:  return "MP3";
    case FMT_WAV:  return "WAV";
    case FMT_FLAC: return "FLAC";
    case FMT_OPUS: return "OPUS";
    case FMT_OGG:  return "OGG";
    default:       return "?";
  }
}

// ---------------------------------------------------------------- scanner

static void skip_ws(void) {
  while (pos < end) {
    char c = buf[pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
    else break;
  }
}

static int expect(char c) {
  skip_ws();
  if (pos < end && buf[pos] == c) { pos++; return 0; }
  return -1;
}

static int peek(void) {
  skip_ws();
  return pos < end ? buf[pos] : -1;
}

// span of the string at cursor (cursor on opening quote); raw bytes, escapes
// left as-is except that we step over backslash pairs
static int parse_string(span_t *out) {
  if (expect('"')) return -1;
  uint32_t start = pos;
  while (pos < end) {
    char c = buf[pos];
    if (c == '\\') pos += 2;
    else if (c == '"') {
      uint32_t len = pos - start;
      if (out) {
        out->off = start;
        out->len = (len > 0xFFFF) ? 0xFFFF : (uint16_t)len;
      }
      pos++;
      return 0;
    } else pos++;
  }
  return -1;
}

// unsigned integer; accepts null (→0), a leading '-' (→0), and ignores any
// fractional part
static int parse_uint(uint32_t *out) {
  skip_ws();
  if (pos + 4 <= end && buf[pos] == 'n') { pos += 4; *out = 0; return 0; }  // null
  int neg = 0;
  if (pos < end && buf[pos] == '-') { neg = 1; pos++; }
  if (pos >= end || buf[pos] < '0' || buf[pos] > '9') return -1;
  uint32_t v = 0;
  while (pos < end && buf[pos] >= '0' && buf[pos] <= '9') {
    v = v * 10 + (uint32_t)(buf[pos] - '0');
    pos++;
  }
  if (pos < end && buf[pos] == '.') {  // skip fraction
    pos++;
    while (pos < end && buf[pos] >= '0' && buf[pos] <= '9') pos++;
  }
  *out = neg ? 0 : v;
  return 0;
}

static int skip_value(void);

static int skip_container(char open, char close) {
  if (expect(open)) return -1;
  if (peek() == close) { pos++; return 0; }
  for (;;) {
    if (open == '{') {
      if (parse_string(0)) return -1;
      if (expect(':')) return -1;
    }
    if (skip_value()) return -1;
    int c = peek();
    if (c == ',') { pos++; continue; }
    if (c == close) { pos++; return 0; }
    return -1;
  }
}

static int skip_value(void) {
  int c = peek();
  if (c == '"') return parse_string(0);
  if (c == '{') return skip_container('{', '}');
  if (c == '[') return skip_container('[', ']');
  if (c == 't') { pos += 4; return 0; }               // true
  if (c == 'f') { pos += 5; return 0; }               // false
  if (c == 'n') { pos += 4; return 0; }               // null
  uint32_t dummy;
  return parse_uint(&dummy);
}

static int span_eq(span_t s, const char *lit) {
  const char *p = lib_str(s);
  uint16_t i = 0;
  while (lit[i]) {
    if (i >= s.len || p[i] != lit[i]) return 0;
    i++;
  }
  return i == s.len;
}

// ---------------------------------------------------------------- schema

static uint8_t format_code(span_t s) {
  if (span_eq(s, "MP3")) return FMT_MP3;
  if (span_eq(s, "WAV")) return FMT_WAV;
  if (span_eq(s, "FLAC")) return FMT_FLAC;
  if (span_eq(s, "OPUS")) return FMT_OPUS;
  if (span_eq(s, "OGG")) return FMT_OGG;
  return FMT_UNK;
}

static int parse_track(void) {
  track_t t = {0};
  if (expect('{')) return -1;
  if (peek() == '}') { pos++; goto store; }
  for (;;) {
    span_t key;
    if (parse_string(&key)) return -1;
    if (expect(':')) return -1;
    if (span_eq(key, "title")) {
      if (parse_string(&t.title)) return -1;
    } else if (span_eq(key, "path")) {
      if (parse_string(&t.path)) return -1;
    } else if (span_eq(key, "durationMs")) {
      uint32_t ms;
      if (parse_uint(&ms)) return -1;
      uint32_t s = ms / 1000u;
      t.dur_s = (s > 0xFFFF) ? 0xFFFF : (uint16_t)s;
    } else if (span_eq(key, "bitrateKbps")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      t.kbps = (v > 0xFFFF) ? 0xFFFF : (uint16_t)v;
    } else if (span_eq(key, "format")) {
      span_t f;
      if (parse_string(&f)) return -1;
      t.format = format_code(f);
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { pos++; continue; }
    if (c == '}') { pos++; break; }
    return -1;
  }
store:
  if (lib_n_tracks < MAX_TRACKS) lib_tracks[lib_n_tracks++] = t;
  return 0;
}

static int parse_album(void) {
  album_t a = {0};
  a.first_track = (uint16_t)lib_n_tracks;
  if (expect('{')) return -1;
  if (peek() == '}') { pos++; goto store; }
  for (;;) {
    span_t key;
    if (parse_string(&key)) return -1;
    if (expect(':')) return -1;
    if (span_eq(key, "title")) {
      if (parse_string(&a.title)) return -1;
    } else if (span_eq(key, "genre")) {
      if (peek() == '"') { if (parse_string(&a.genre)) return -1; }
      else if (skip_value()) return -1;
    } else if (span_eq(key, "coverArtSmall")) {
      if (peek() == '"') { if (parse_string(&a.cover_small)) return -1; }
      else if (skip_value()) return -1;
    } else if (span_eq(key, "coverArt")) {
      if (peek() == '"') { if (parse_string(&a.cover_large)) return -1; }
      else if (skip_value()) return -1;
    } else if (span_eq(key, "year")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      a.year = (uint16_t)v;
    } else if (span_eq(key, "hue")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      a.hue = (uint16_t)(v % 360);
    } else if (span_eq(key, "tracks")) {
      if (expect('[')) return -1;
      if (peek() == ']') pos++;
      else
        for (;;) {
          if (parse_track()) return -1;
          int c = peek();
          if (c == ',') { pos++; continue; }
          if (c == ']') { pos++; break; }
          return -1;
        }
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { pos++; continue; }
    if (c == '}') { pos++; break; }
    return -1;
  }
store:
  a.n_tracks = (uint16_t)(lib_n_tracks - a.first_track);
  if (lib_n_albums < MAX_ALBUMS) lib_albums[lib_n_albums++] = a;
  return 0;
}

static int parse_artist(void) {
  artist_t ar = {0};
  ar.first_album = (uint16_t)lib_n_albums;
  if (expect('{')) return -1;
  if (peek() == '}') { pos++; goto store; }
  for (;;) {
    span_t key;
    if (parse_string(&key)) return -1;
    if (expect(':')) return -1;
    if (span_eq(key, "name")) {
      if (parse_string(&ar.name)) return -1;
    } else if (span_eq(key, "albums")) {
      if (expect('[')) return -1;
      if (peek() == ']') pos++;
      else
        for (;;) {
          if (parse_album()) return -1;
          int c = peek();
          if (c == ',') { pos++; continue; }
          if (c == ']') { pos++; break; }
          return -1;
        }
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { pos++; continue; }
    if (c == '}') { pos++; break; }
    return -1;
  }
store:
  ar.n_albums = (uint16_t)(lib_n_albums - ar.first_album);
  if (lib_n_artists < MAX_ARTISTS && ar.n_albums > 0) lib_artists[lib_n_artists++] = ar;
  return 0;
}

int lib_parse(void) {
  buf = (const char *)LIBRARY_BASE;
  pos = 0;
  end = REG_LIB_BYTES;
  lib_n_artists = lib_n_albums = lib_n_tracks = 0;

  if (end < 2) return -2;  // no library loaded
  if (expect('{')) return -1;
  for (;;) {
    span_t key;
    if (parse_string(&key)) return -1;
    if (expect(':')) return -1;
    if (span_eq(key, "artists")) {
      if (expect('[')) return -1;
      if (peek() == ']') pos++;
      else
        for (;;) {
          if (parse_artist()) return -1;
          int c = peek();
          if (c == ',') { pos++; continue; }
          if (c == ']') { pos++; break; }
          return -1;
        }
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { pos++; continue; }
    if (c == '}') break;
    return -1;
  }
  return (lib_n_artists > 0) ? 0 : -3;
}
