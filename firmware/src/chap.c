// Streaming reader for one track's "chapters" array in library.json.
//
// Runs DURING playback (L/R press, chapter-title refresh), so it must not
// touch lib_stage — that buffer is the codec's compressed-input ring. Instead
// the bytes are read straight out of the RX RAM window, one file_read chunk
// at a time. A full 160-chapter array is ~5 KB → a handful of reads, only on
// track start and chapter changes.

#include "chap.h"
#include "file.h"

int chap_count;
uint32_t chap_start_s[CHAP_MAX];

// ------------------------------------------------------------ byte stream
#define CCHUNK 2048
static uint32_t c_pos, c_size, c_base;
static uint32_t c_have;  // bytes valid in the RX window
static int c_err;

static uint8_t cget(void) {
  if (c_err || c_pos >= c_size) { c_err = c_err ? c_err : -1; return 0; }
  if (c_pos < c_base || c_pos >= c_base + c_have) {
    uint32_t want = c_size - c_pos;
    if (want > CCHUNK) want = CCHUNK;
    if (file_read(SLOT_LIBRARY, c_pos, want)) { c_err = -2; return 0; }
    c_base = c_pos;
    c_have = want;
  }
  return RX_BYTES[c_pos - c_base];
}

static uint8_t cur(void) { return cget(); }
static void adv(void) { c_pos++; }

static void stream_init(uint32_t off) {
  c_pos = off;
  c_base = c_have = 0;
  c_err = 0;
  c_size = file_slot_size(SLOT_LIBRARY);
}

static void skip_ws(void) {
  for (;;) {
    uint8_t c = cur();
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') adv();
    else return;
  }
}

// Skip one JSON value (nested containers included — a depth counter is
// enough here because we never need the contents).
static void skip_value(void) {
  skip_ws();
  uint8_t c = cur();
  if (c == '{' || c == '[') {
    int depth = 0, in_str = 0;
    for (;;) {
      c = cur();
      if (c_err) return;
      adv();
      if (in_str) {
        if (c == '\\') adv();
        else if (c == '"') in_str = 0;
      } else if (c == '"') in_str = 1;
      else if (c == '{' || c == '[') depth++;
      else if (c == '}' || c == ']') { if (--depth == 0) return; }
    }
  }
  if (c == '"') {
    adv();
    for (;;) {
      c = cur();
      if (c_err) return;
      adv();
      if (c == '\\') adv();
      else if (c == '"') return;
    }
  }
  while (!c_err) {  // number / true / false / null
    c = cur();
    if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0) return;
    adv();
  }
}

static uint32_t parse_uint(void) {
  skip_ws();
  uint32_t v = 0;
  uint8_t c = cur();
  while (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); adv(); c = cur(); }
  return v;
}

// Object key at cursor → k[] (truncated to 7 bytes, always fully consumed).
static int read_key(char k[8]) {
  skip_ws();
  if (cur() != '"') { c_err = -3; return -1; }
  adv();
  int n = 0;
  for (;;) {
    uint8_t c = cur();
    if (c_err) return -1;
    adv();
    if (c == '"') break;
    if (c == '\\') { c = cur(); adv(); }
    if (n < 7) k[n++] = (char)c;
  }
  k[n] = 0;
  return 0;
}

// Copy the string at cursor into out, decoding JSON escapes to UTF-8 bytes
// (the same encoding the pool strings and gfx text expect).
static int copy_string(char *out, int max) {
  skip_ws();
  if (cur() != '"') { c_err = -3; return -1; }
  adv();
  int n = 0;
  for (;;) {
    uint8_t c = cur();
    if (c_err) return -1;
    adv();
    if (c == '"') break;
    if (c == '\\') {
      uint8_t e = cur();
      adv();
      if (e == 'u') {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
          uint8_t h = cur();
          adv();
          v <<= 4;
          if (h >= '0' && h <= '9') v |= h - '0';
          else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
          else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        }
        if (v < 0x80) c = (uint8_t)v;
        else if (v < 0x800) {  // 2-byte UTF-8
          if (n < max - 2) out[n++] = (char)(0xC0 | (v >> 6));
          c = (uint8_t)(0x80 | (v & 0x3F));
        } else c = '?';
      } else if (e == 'n' || e == 't') c = ' ';
      else c = e;  // \" \\ \/
    }
    if (n < max - 1) out[n++] = (char)c;
  }
  out[n] = 0;
  return n;
}

// ---------------------------------------------------------------- public
// Walk the array element by element; `want_idx` >= 0 stops there and copies
// the title, -1 caches every start time instead.
static int walk(const track_t *t, int want_idx, char *out, int max) {
  if (!t || !t->chap_off) return 0;
  stream_init(t->chap_off);  // cursor sits on the first element's '{'
  int idx = 0;
  for (;;) {
    skip_ws();
    if (cur() != '{') { c_err = -3; break; }
    adv();
    uint32_t start = 0;
    int titled = 0;
    skip_ws();
    if (cur() == '}') adv();
    else
      for (;;) {
        char k[8];
        if (read_key(k)) break;
        skip_ws();
        if (cur() != ':') { c_err = -3; break; }
        adv();
        if (k[0] == 's' && !k[1]) start = parse_uint();
        else if (k[0] == 't' && !k[1] && want_idx == idx) titled = copy_string(out, max);
        else skip_value();
        skip_ws();
        if (cur() == ',') { adv(); continue; }
        if (cur() == '}') { adv(); break; }
        c_err = -3;
        break;
      }
    if (c_err) break;
    if (want_idx < 0) {
      if (idx < CHAP_MAX) { chap_start_s[idx] = start; chap_count = idx + 1; }
    } else if (idx == want_idx)
      return titled;
    idx++;
    skip_ws();
    if (cur() == ',') { adv(); continue; }
    if (cur() == ']') return want_idx < 0 ? chap_count : -4;  // idx not found
    c_err = -3;
    break;
  }
  if (want_idx < 0) chap_count = 0;
  return c_err;
}

int chap_load(const track_t *t) {
  chap_count = 0;
  if (!t || !t->chap_off) return 0;
  return walk(t, -1, 0, 0);
}

int chap_title(const track_t *t, int idx, char *out, int max) {
  if (max < 4) return -1;
  out[0] = 0;
  return walk(t, idx, out, max);
}

int chap_index_for(uint32_t pos_s) {
  if (chap_count == 0) return -1;
  int i = chap_count - 1;
  while (i > 0 && chap_start_s[i] > pos_s) i--;
  return i;
}
