// Streaming recursive-descent parser for library.json.
//
// Input: the file streams through the RX RAM in 16 KB chunks via target-read
// commands; the scan is strictly forward. String tokens are accumulated into
// a bounded token buffer, and the ones worth keeping are copied into the pool.

#include "lib.h"
#include "file.h"
#include "mmio.h"

artist_t lib_artists[MAX_ARTISTS];
album_t  lib_albums[MAX_ALBUMS];
track_t  lib_tracks[MAX_TRACKS];
int lib_n_artists, lib_n_albums, lib_n_tracks;
int lib_root_first_track, lib_n_root_tracks;

static char pool[POOL_SIZE];
static uint32_t pool_used;

const char *lib_str(span_t s) { return &pool[s.off]; }

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

// ------------------------------------------------------------ byte stream
// Chunks stream into the RX RAM, then get copied (fast word loop) into a
// CPU-RAM staging buffer so the parse itself runs on plain cached memory.
#define STAGE_SIZE 12288
// word-aligned: refill() copies 32-bit words into it (misaligned stores trap).
// Exported: doubles as the MP3 compressed-input buffer (see mp3.c).
char lib_stage[STAGE_SIZE] __attribute__((aligned(4)));
#define stage lib_stage
static uint32_t f_pos, f_size, win_base, win_valid;
static int f_err;

static void refill(void) {
  uint32_t want = f_size - f_pos;
  if (want > STAGE_SIZE) want = STAGE_SIZE;
  int e = file_read(SLOT_LIBRARY, f_pos, want);
  if (e) { f_err = e; win_valid = 0; return; }
  uint32_t words = (want + 3) / 4;
  uint32_t *d = (uint32_t *)stage;
  for (uint32_t i = 0; i < words; i++) d[i] = RX_BASE[i];
  win_base = f_pos;
  win_valid = want;
}

static int at_end(void) { return f_pos >= f_size || f_err; }

static char cur(void) {
  if (at_end()) return 0;
  if (f_pos < win_base || f_pos >= win_base + win_valid) refill();
  if (f_err) return 0;
  return stage[f_pos - win_base];
}

static void adv(void) { f_pos++; }

// ------------------------------------------------------------- tokenizer
static void skip_ws(void) {
  for (;;) {
    char c = cur();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') adv();
    else return;
  }
}

static int expect(char c) {
  skip_ws();
  if (cur() == c) { adv(); return 0; }
  return -1;
}

static int peek(void) {
  skip_ws();
  return at_end() ? -1 : cur();
}

#define TOK_MAX 320
static char tok[TOK_MAX + 1];
static int tok_len;

// string at cursor → tok[] (raw bytes, escape pairs copied as-is, clamped)
static int parse_string(void) {
  if (expect('"')) return -1;
  tok_len = 0;
  for (;;) {
    char c = cur();
    if (at_end()) return -1;
    if (c == '"') { adv(); tok[tok_len] = 0; return 0; }
    if (c == '\\') {
      if (tok_len < TOK_MAX) tok[tok_len++] = c;
      adv();
      c = cur();
    }
    if (tok_len < TOK_MAX) tok[tok_len++] = c;
    adv();
  }
}

static int parse_uint(uint32_t *out) {
  skip_ws();
  char c = cur();
  if (c == 'n') { adv(); adv(); adv(); adv(); *out = 0; return 0; }  // null
  int neg = 0;
  if (c == '-') { neg = 1; adv(); c = cur(); }
  if (c < '0' || c > '9') return -1;
  uint32_t v = 0;
  while (c >= '0' && c <= '9') {
    v = v * 10 + (uint32_t)(c - '0');
    adv();
    c = cur();
  }
  if (c == '.') {
    adv();
    c = cur();
    while (c >= '0' && c <= '9') { adv(); c = cur(); }
  }
  *out = neg ? 0 : v;
  return 0;
}

static int skip_value(void);

static int skip_container(char open, char close) {
  if (expect(open)) return -1;
  if (peek() == close) { adv(); return 0; }
  for (;;) {
    if (open == '{') {
      if (parse_string()) return -1;
      if (expect(':')) return -1;
    }
    if (skip_value()) return -1;
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == close) { adv(); return 0; }
    return -1;
  }
}

static int skip_value(void) {
  int c = peek();
  if (c == '"') return parse_string();
  if (c == '{') return skip_container('{', '}');
  if (c == '[') return skip_container('[', ']');
  if (c == 't') { adv(); adv(); adv(); adv(); return 0; }         // true
  if (c == 'f') { adv(); adv(); adv(); adv(); adv(); return 0; }  // false
  if (c == 'n') { adv(); adv(); adv(); adv(); return 0; }         // null
  uint32_t dummy;
  return parse_uint(&dummy);
}

static int key_is(const char *lit) {
  int i = 0;
  while (lit[i]) {
    if (i >= tok_len || tok[i] != lit[i]) return 0;
    i++;
  }
  return i == tok_len;
}

// copy tok[] into the pool (clamped if the pool fills up)
static span_t pool_tok(void) {
  span_t s;
  uint32_t n = (uint32_t)tok_len;
  if (pool_used + n + 1 > POOL_SIZE) n = (POOL_SIZE > pool_used + 1) ? POOL_SIZE - pool_used - 1 : 0;
  s.off = (uint16_t)pool_used;
  s.len = (uint16_t)n;
  for (uint32_t i = 0; i < n; i++) pool[pool_used + i] = tok[i];
  pool[pool_used + n] = 0;
  pool_used += n + 1;
  return s;
}

static uint8_t format_code(void) {
  if (key_is("MP3")) return FMT_MP3;
  if (key_is("WAV")) return FMT_WAV;
  if (key_is("FLAC")) return FMT_FLAC;
  if (key_is("OPUS")) return FMT_OPUS;
  if (key_is("OGG")) return FMT_OGG;
  return FMT_UNK;
}

// ---------------------------------------------------------------- schema
static int parse_track(void) {
  track_t t = {0};
  if (expect('{')) return -1;
  if (peek() == '}') { adv(); goto store; }
  for (;;) {
    if (parse_string()) return -1;
    if (expect(':')) return -1;
    if (key_is("title")) {
      if (parse_string()) return -1;
      t.title = pool_tok();
    } else if (key_is("path")) {
      skip_ws();
      t.path_off = f_pos + 1;  // f_pos sits on the opening quote
      if (parse_string()) return -1;
      t.path_len = (uint16_t)tok_len;
    } else if (key_is("fileSize")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      t.fsize = v;
    } else if (key_is("durationMs")) {
      uint32_t ms;
      if (parse_uint(&ms)) return -1;
      uint32_t s = ms / 1000u;
      t.dur_s = (uint32_t)s;  // seconds; 32-bit (20 h audiobooks)
    } else if (key_is("bitrateKbps")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      t.kbps = (v > 0xFFFF) ? 0xFFFF : (uint16_t)v;
    } else if (key_is("format")) {
      if (parse_string()) return -1;
      t.format = format_code();
    } else if (key_is("chapters")) {
      // Only the offset of the first element is kept; chap.c re-reads the
      // array from the file when the track starts. Empty array → 0 (none).
      if (expect('[')) return -1;
      if (peek() == ']') adv();
      else {
        t.chap_off = f_pos;  // peek() left the cursor on the first '{'
        for (;;) {
          if (skip_value()) return -1;
          int c = peek();
          if (c == ',') { adv(); continue; }
          if (c == ']') { adv(); break; }
          return -1;
        }
      }
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == '}') { adv(); break; }
    return -1;
  }
store:
  if (lib_n_tracks < MAX_TRACKS) lib_tracks[lib_n_tracks++] = t;
  return 0;
}

// "rootTracks": [...] — loose tracks (library root or artist root)
static int parse_track_array(void) {
  if (expect('[')) return -1;
  if (peek() == ']') { adv(); return 0; }
  for (;;) {
    if (parse_track()) return -1;
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == ']') { adv(); return 0; }
    return -1;
  }
}

static int parse_album(void) {
  album_t a = {0};
  a.first_track = (uint16_t)lib_n_tracks;
  if (expect('{')) return -1;
  if (peek() == '}') { adv(); goto store; }
  for (;;) {
    if (parse_string()) return -1;
    if (expect(':')) return -1;
    if (key_is("title")) {
      if (parse_string()) return -1;
      a.title = pool_tok();
    } else if (key_is("genre")) {
      if (peek() == '"') {
        if (parse_string()) return -1;
        a.genre = pool_tok();
      } else if (skip_value()) return -1;
    } else if (key_is("year")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      a.year = (uint16_t)v;
    } else if (key_is("hue")) {
      uint32_t v;
      if (parse_uint(&v)) return -1;
      a.hue = (uint16_t)(v % 360);
    } else if (key_is("tracks")) {
      if (expect('[')) return -1;
      if (peek() == ']') adv();
      else
        for (;;) {
          if (parse_track()) return -1;
          int c = peek();
          if (c == ',') { adv(); continue; }
          if (c == ']') { adv(); break; }
          return -1;
        }
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == '}') { adv(); break; }
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
  if (peek() == '}') { adv(); goto store; }
  for (;;) {
    if (parse_string()) return -1;
    if (expect(':')) return -1;
    if (key_is("name")) {
      if (parse_string()) return -1;
      ar.name = pool_tok();
    } else if (key_is("albums")) {
      if (expect('[')) return -1;
      if (peek() == ']') adv();
      else
        for (;;) {
          if (parse_album()) return -1;
          int c = peek();
          if (c == ',') { adv(); continue; }
          if (c == ']') { adv(); break; }
          return -1;
        }
      ar.n_albums = (uint16_t)(lib_n_albums - ar.first_album);
    } else if (key_is("rootTracks")) {
      ar.first_rtrack = (uint16_t)lib_n_tracks;
      if (parse_track_array()) return -1;
      ar.n_rtracks = (uint16_t)(lib_n_tracks - ar.first_rtrack);
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == '}') { adv(); break; }
    return -1;
  }
store:
  if (lib_n_artists < MAX_ARTISTS && (ar.n_albums > 0 || ar.n_rtracks > 0))
    lib_artists[lib_n_artists++] = ar;
  return 0;
}

int lib_fetch_path(const track_t *t, char *out, int max) {
  int n = t->path_len;
  if (n >= max) n = max - 1;
  if (file_read(SLOT_LIBRARY, t->path_off, (uint32_t)n)) return -1;
  volatile const uint8_t *src = RX_BYTES;
  for (int i = 0; i < n; i++) out[i] = (char)src[i];
  out[n] = 0;
  return n;
}

int lib_parse(void) {
  lib_n_artists = lib_n_albums = lib_n_tracks = 0;
  lib_root_first_track = lib_n_root_tracks = 0;
  pool_used = 0;
  f_pos = 0;
  win_valid = 0;
  f_err = 0;

  f_size = file_slot_size(SLOT_LIBRARY);
  if (f_size < 2) return -2;  // no library registered

  if (expect('{')) return -1;
  for (;;) {
    if (parse_string()) return -1;
    if (expect(':')) return -1;
    if (key_is("artists")) {
      if (expect('[')) return -1;
      if (peek() == ']') adv();
      else
        for (;;) {
          if (parse_artist()) return -1;
          int c = peek();
          if (c == ',') { adv(); continue; }
          if (c == ']') { adv(); break; }
          return -1;
        }
    } else if (key_is("rootTracks")) {  // schema v2: loose tracks at the root
      lib_root_first_track = lib_n_tracks;
      if (parse_track_array()) return -1;
      lib_n_root_tracks = lib_n_tracks - lib_root_first_track;
    } else {
      if (skip_value()) return -1;
    }
    int c = peek();
    if (c == ',') { adv(); continue; }
    if (c == '}') break;
    return -1;
  }
  if (f_err) return -4;  // a chunk read failed mid-parse
  return (lib_n_artists > 0 || lib_n_root_tracks > 0) ? 0 : -3;
}
