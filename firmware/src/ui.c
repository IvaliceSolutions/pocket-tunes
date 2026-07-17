// Pocket Tunes UI — design_handoff_pocket_tunes_4a (iPod Classic) at 320x288.
//
// A stack of full-screen lists instead of the old two-pane browser:
// Bibliothèque (artists + root loose tracks) → Albums (albums + the artist's
// loose tracks, skipped when the artist has no album) → Titres → Lecture.
// Playback is independent of the browse cursors; leaving Lecture never stops
// the music (the status bar keeps a play glyph, X jumps back to it).
//
// Controls — deviations from the 4a handoff are user decisions, keep them:
//   D-pad     move cursor / on Lecture: Up-Down = prev-next track,
//             Left-Right = seek ±10 s
//   A         open / play / on Lecture: pause
//   B         back (an artist opened without Albums screen skips it on the
//             way out too)
//   X         jump to Lecture from any list (track loaded)
//   Y         play/pause from ANYWHERE (user: "Y partout")
//   L / R     previous / next chapter (audiobooks with ID3 CHAP)
//   START     shuffle   SELECT: repeat (user kept these off L/R)
// Status bar shows the Pocket RTC clock (user choice; APF exposes no battery).

#include "ui.h"
#include "mmio.h"
#include "gfx.h"
#include "lib.h"
#include "palette.h"
#include "codec.h"
#include "eq.h"
#include "art.h"
#include "chap.h"

// ----------------------------------------------------------------- layout
#define STATUS_H 24
#define LIST_Y0 28
#define ROW_H 22        // Bibliothèque / Titres rows
#define ALB_ROW_H 36    // Albums rows (two lines)
#define ROW_X 14
#define ROW_X_MAX (SCREEN_W - 14)

// Persistent mini-bar over the lists while a track is loaded (user request:
// keep playback visible without the Lecture screen)
#define MBAR_H 42
#define MBAR_Y (SCREEN_H - MBAR_H)

// Lecture (Now Playing)
#define NOW_COVER 96
#define NOW_COVER_X ((SCREEN_W - NOW_COVER) / 2)
#define NOW_COVER_Y 32
#define NOW_TITLE_Y 136
#define NOW_SUB_Y 154
#define NOW_CHAP_Y 166
#define NOW_EQ_X 20
#define NOW_EQ_W (SCREEN_W - 2 * NOW_EQ_X)
#define NOW_EQ_Y 178
#define NOW_EQ_H 40
#define NOW_BAR_X 20
#define NOW_BAR_W (SCREEN_W - 2 * NOW_BAR_X)
#define NOW_BAR_Y 232
#define NOW_TIME_Y 242
#define NOW_TOGGLE_Y 272

// ------------------------------------------------------------------ state
enum { SCR_LIB, SCR_ALBUMS, SCR_TRACKS, SCR_NOW };
static int screen, prev_screen;  // prev_screen: the list Lecture returns to
static int lib_cursor, lib_scroll;
static int cur_artist;
static int alb_cursor, alb_scroll;
static int trk_cursor, trk_scroll;
// Titres context: an album, or the artist's loose tracks (trk_album == -1)
static int trk_album = -1;
static int trk_first, trk_n;

// playback — independent of the browse cursors. The prev/next context is a
// contiguous lib_tracks[] range derived from (play_alb, play_artist):
// album > artist loose tracks > library-root loose tracks.
static int play_gidx = -1;   // global track index loaded (-1 = none)
static int play_alb = -1;    // global album index, -1 = loose track
static int play_artist = -1; // artist index, -1 = library-root track
static int play_first, play_n;
static int playing;
static uint32_t anim;
static char pathbuf[288];
static int last_start_err;

// chapter display cache (playing track only)
static char chap_buf[64];
static int chap_shown = -1;

// ------------------------------------------------------------ small utils
static char *s_append(char *d, const char *s) {
  while (*s) *d++ = *s++;
  *d = 0;
  return d;
}
static char *s_udec(char *d, uint32_t v) {
  char t[11];
  u32_to_dec(v, t);
  return s_append(d, t);
}
// "M:SS", or "H:MM:SS" past an hour (20 h audiobooks)
static char *s_time(char *d, uint32_t sec) {
  if (sec >= 3600) {
    d = s_udec(d, sec / 3600);
    *d++ = ':';
    uint32_t m = (sec % 3600) / 60;
    *d++ = (char)('0' + m / 10);
    *d++ = (char)('0' + m % 10);
  } else
    d = s_udec(d, sec / 60);
  *d++ = ':';
  *d++ = (char)('0' + (sec % 60) / 10);
  *d++ = (char)('0' + sec % 10);
  *d = 0;
  return d;
}
static int s_len(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static int clamp_scroll(int cursor, int scroll, int rows) {
  if (cursor < scroll) return cursor;
  if (cursor >= scroll + rows) return cursor - rows + 1;
  return scroll;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int minibar_on(void);  // below (needs play state)
static int list_h(void) { return SCREEN_H - LIST_Y0 - (minibar_on() ? MBAR_H : 0); }
static int lib_rows(void) { return list_h() / ROW_H; }
static int alb_rows(void) { return list_h() / ALB_ROW_H; }
static int trk_rows(void) { return list_h() / ROW_H; }

// Bibliothèque rows: the artists then the library-root loose tracks.
static int n_lib_rows(void) { return lib_n_artists + lib_n_root_tracks; }
// Albums rows: the artist's albums then its loose tracks.
static int n_alb_rows(void) {
  const artist_t *a = &lib_artists[cur_artist];
  return a->n_albums + a->n_rtracks;
}

static const track_t *play_track_p(void) { return &lib_tracks[play_gidx]; }

static int minibar_on(void) { return play_gidx >= 0 && screen != SCR_NOW; }

static void derive_play_ctx(void) {
  if (play_alb >= 0) {
    play_first = lib_albums[play_alb].first_track;
    play_n = lib_albums[play_alb].n_tracks;
  } else if (play_artist >= 0) {
    play_first = lib_artists[play_artist].first_rtrack;
    play_n = lib_artists[play_artist].n_rtracks;
  } else {
    play_first = lib_root_first_track;
    play_n = lib_n_root_tracks;
  }
  if (play_n < 1) play_n = 1;
}

static void derive_trk_ctx(void) {
  const artist_t *a = &lib_artists[cur_artist];
  if (trk_album >= 0) {
    trk_first = lib_albums[trk_album].first_track;
    trk_n = lib_albums[trk_album].n_tracks;
  } else {
    trk_first = a->first_rtrack;
    trk_n = a->n_rtracks;
  }
}

// ---- repeat mode: cycled with SELECT
enum { REP_ALL, REP_ONE, REP_OFF };
static int repeat_mode = REP_ALL;
static const char *repeat_str(void) {
  return repeat_mode == REP_ONE ? "RPT1" : repeat_mode == REP_OFF ? "RPT0" : "RPT";
}

// ---- shuffle: toggled with START; xorshift stirred by the cycle counter
static int shuffle;
static uint32_t rng_state = 0x2545F491u;
static uint32_t rng_next(void) {
  uint32_t x = rng_state ^ REG_CYCLES;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rng_state = x ? x : 0x2545F491u;
  return rng_state;
}
static int next_rel(int cur_rel) {
  if (play_n <= 1) return 0;
  if (!shuffle) return (cur_rel + 1) % play_n;
  int r = (int)(rng_next() % (uint32_t)play_n);
  if (r == cur_rel) r = (r + 1) % play_n;
  return r;
}

static uint32_t seek_byte_off(const track_t *t, uint32_t sec) {
  if (t->dur_s) return (uint32_t)((uint64_t)t->fsize * sec / t->dur_s);
  if (t->kbps) return sec * (t->kbps * 1000u / 8u);
  return 0;
}

static void start_play(int gidx, int alb, int artist) {
  play_gidx = gidx;
  play_alb = alb;
  play_artist = artist;
  derive_play_ctx();
  const track_t *t = &lib_tracks[gidx];
  int n = lib_fetch_path(t, pathbuf, sizeof pathbuf);
  anim = 0;
  if (n > 0) {
    last_start_err = codec_start(t->format, pathbuf, n, t->fsize);
    playing = (last_start_err == 0);
    art_load(gidx);   // real cover (placeholder if none)
    chap_load(t);     // audiobook chapter starts (count 0 if none)
    chap_shown = -1;
    chap_buf[0] = 0;
  } else {
    last_start_err = -20;  // path unavailable
    playing = 0;
    codec_stop();
  }
}

// ---- marquee (selected item / Lecture lines)
static void draw_marquee(int x, int y, int box_w, const char *s, int len,
                         uint8_t color, int small, int animate) {
  int tw = gfx_text_px_len(s, len, small);
  if (tw <= box_w || !animate) {
    if (small) gfx_textn_small(x, y, s, len, x + box_w, color);
    else gfx_textn(x, y, s, len, x + box_w, color);
    return;
  }
  int overflow = tw - box_w;
  int hold = 30;
  int cyc = 2 * hold + 2 * overflow;
  int p = (int)((anim >> 1) % (uint32_t)cyc);
  int scroll;
  if (p < hold) scroll = 0;
  else if (p < hold + overflow) scroll = p - hold;
  else if (p < 2 * hold + overflow) scroll = overflow;
  else scroll = cyc - p;
  gfx_text_scroll(x, y, s, len, box_w, color, scroll, small);
}

// centered when it fits, marquee when it doesn't
static void draw_centered(int y, int box_x, int box_w, const char *s, int len,
                          uint8_t color, int small) {
  int tw = gfx_text_px_len(s, len, small);
  if (tw <= box_w) {
    int x = box_x + (box_w - tw) / 2;
    if (small) gfx_textn_small(x, y, s, len, x + tw, color);
    else gfx_textn(x, y, s, len, x + tw, color);
  } else
    draw_marquee(box_x, y, box_w, s, len, color, small, 1);
}

// hue (0..359) → two-tone placeholder colors from the 6x6x6 cube
static void hue_colors(uint16_t hue, uint8_t *bright, uint8_t *dark) {
  static const uint8_t hb[12][3] = {
      {5, 1, 1}, {5, 3, 1}, {5, 5, 1}, {3, 5, 1}, {1, 5, 1}, {1, 5, 3},
      {1, 5, 5}, {1, 3, 5}, {1, 1, 5}, {3, 1, 5}, {5, 1, 5}, {5, 1, 3}};
  const uint8_t *c = hb[(hue / 30) % 12];
  *bright = PAL_CUBE(c[0], c[1], c[2]);
  *dark = PAL_CUBE(c[0] / 2, c[1] / 2, c[2] / 2);
}

static void draw_cover_ph(int x, int y, int size, uint16_t hue) {
  uint8_t hi, lo;
  hue_colors(hue, &hi, &lo);
  gfx_fill_rect(x, y, size, size / 2, hi);
  gfx_fill_rect(x, y + size / 2, size, size - size / 2, lo);
  gfx_rect_round(x, y, size, size, COL_DIVIDER);
}

// hue for the playing track's placeholder: its album's, or hashed from the
// title for loose tracks (stable across sessions)
static uint16_t play_hue(void) {
  if (play_alb >= 0) return lib_albums[play_alb].hue;
  const track_t *t = play_track_p();
  const char *s = lib_str(t->title);
  uint32_t h = 5381;
  for (int i = 0; i < t->title.len; i++) h = h * 33 + (uint8_t)s[i];
  return (uint16_t)(h % 360);
}

// "HH:MM" from the Pocket RTC (BCD 0x00HHMMSS). Right-aligned at x_right.
static void draw_clock(int x_right, int y, uint8_t color) {
  uint32_t v = REG_RTC;
  char b[6];
  b[0] = (char)('0' + ((v >> 20) & 0xF));
  b[1] = (char)('0' + ((v >> 16) & 0xF));
  b[2] = ':';
  b[3] = (char)('0' + ((v >> 12) & 0xF));
  b[4] = (char)('0' + ((v >> 8) & 0xF));
  b[5] = 0;
  gfx_text_small(x_right - 5 * 5, y, b, color);
}

// ------------------------------------------------------------------ input
void ui_init(void) {
  screen = prev_screen = SCR_LIB;
  lib_cursor = lib_scroll = 0;
  cur_artist = 0;
  alb_cursor = alb_scroll = 0;
  trk_cursor = trk_scroll = 0;
  trk_album = -1;
  play_gidx = -1;
  play_alb = play_artist = -1;
  playing = 0;
}

// open the artist under the Bibliothèque cursor (design: no albums → skip
// the Albums screen entirely)
static void open_artist(int idx) {
  cur_artist = idx;
  const artist_t *a = &lib_artists[idx];
  if (a->n_albums > 0) {
    screen = SCR_ALBUMS;
    alb_cursor = alb_scroll = 0;
  } else {
    trk_album = -1;
    derive_trk_ctx();
    screen = SCR_TRACKS;
    trk_cursor = trk_scroll = 0;
  }
}

static void enter_now(void) {
  prev_screen = (screen == SCR_NOW) ? prev_screen : screen;
  screen = SCR_NOW;
}

// play the track at trk range position rel (Titres screen context)
static void play_from_tracks(int rel) {
  int gidx = trk_first + rel;
  if (gidx != play_gidx)  // same track → keep position (user: reprendre)
    start_play(gidx, trk_album, cur_artist);
  enter_now();
}

int ui_input(uint16_t pressed) {
  anim = 0;  // restart the selected item's marquee

  if (pressed & KEY_START) { shuffle = !shuffle; return 1; }
  if (pressed & KEY_SELECT) { repeat_mode = (repeat_mode + 1) % 3; return 1; }

  // Y = play/pause from anywhere (user decision)
  if ((pressed & KEY_Y) && play_gidx >= 0) {
    playing = !playing;
    codec_set_paused(!playing);
    return 1;
  }

  if (screen == SCR_NOW) {
    if (pressed & (KEY_UP | KEY_DOWN)) {  // prev/next in context, auto-plays
      int cur_rel = play_gidx - play_first;
      int rel = (pressed & KEY_DOWN)
                    ? next_rel(cur_rel)
                    : (cur_rel + play_n - 1) % play_n;
      start_play(play_first + rel, play_alb, play_artist);
      return 1;
    }
    if (pressed & (KEY_LEFT | KEY_RIGHT)) {  // seek ±10 s
      const track_t *t = play_track_p();
      uint32_t cur = codec_pos_seconds();
      uint32_t tgt = (pressed & KEY_RIGHT) ? cur + 10 : (cur > 10 ? cur - 10 : 0);
      if (t->dur_s && tgt >= t->dur_s) tgt = t->dur_s - 1;
      codec_seek(tgt, seek_byte_off(t, tgt));
      return 1;
    }
    if ((pressed & (KEY_L | KEY_R)) && chap_count > 0) {  // chapter nav
      const track_t *t = play_track_p();
      uint32_t cur = codec_pos_seconds();
      int ci = chap_index_for(cur);
      int ni = ci;
      if (pressed & KEY_R) {
        if (ci + 1 < chap_count) ni = ci + 1;
        else return 0;  // already in the last chapter
      } else {
        // L: restart the chapter, or jump to the previous one near its start
        if (cur > chap_start_s[ci] + 3) ni = ci;
        else ni = ci > 0 ? ci - 1 : 0;
      }
      uint32_t tgt = chap_start_s[ni];
      if (t->dur_s && tgt >= t->dur_s) tgt = t->dur_s - 1;
      codec_seek(tgt, seek_byte_off(t, tgt));
      return 1;
    }
    if (pressed & KEY_A) {  // toggle play/pause
      playing = !playing;
      codec_set_paused(!playing);
      return 1;
    }
    if (pressed & KEY_B) {  // back to the list we came from
      screen = prev_screen;
      return 1;
    }
    return 0;
  }

  // list screens: X jumps to Lecture once a track is loaded
  if ((pressed & KEY_X) && play_gidx >= 0) { enter_now(); return 1; }

  if (screen == SCR_LIB) {
    int n = n_lib_rows();
    if (n == 0) return 0;
    if (pressed & KEY_UP) {
      if (lib_cursor > 0) lib_cursor--;
      lib_scroll = clamp_scroll(lib_cursor, lib_scroll, lib_rows());
      return 1;
    }
    if (pressed & KEY_DOWN) {
      if (lib_cursor < n - 1) lib_cursor++;
      lib_scroll = clamp_scroll(lib_cursor, lib_scroll, lib_rows());
      return 1;
    }
    if (pressed & KEY_A) {
      if (lib_cursor < lib_n_artists) {
        open_artist(lib_cursor);
      } else {  // library-root loose track → play it
        int gidx = lib_root_first_track + (lib_cursor - lib_n_artists);
        if (gidx != play_gidx) start_play(gidx, -1, -1);
        enter_now();
      }
      return 1;
    }
    return 0;
  }

  if (screen == SCR_ALBUMS) {
    const artist_t *a = &lib_artists[cur_artist];
    int n = n_alb_rows();
    if (pressed & KEY_UP) {
      if (alb_cursor > 0) alb_cursor--;
      alb_scroll = clamp_scroll(alb_cursor, alb_scroll, alb_rows());
      return 1;
    }
    if (pressed & KEY_DOWN) {
      if (alb_cursor < n - 1) alb_cursor++;
      alb_scroll = clamp_scroll(alb_cursor, alb_scroll, alb_rows());
      return 1;
    }
    if (pressed & KEY_A) {
      if (alb_cursor < a->n_albums) {  // open the album (no autoplay)
        trk_album = a->first_album + alb_cursor;
        derive_trk_ctx();
        screen = SCR_TRACKS;
        trk_cursor = trk_scroll = 0;
      } else {  // the artist's loose track → play it
        int gidx = a->first_rtrack + (alb_cursor - a->n_albums);
        if (gidx != play_gidx) start_play(gidx, -1, cur_artist);
        enter_now();
      }
      return 1;
    }
    if (pressed & KEY_B) { screen = SCR_LIB; return 1; }
    return 0;
  }

  // screen == SCR_TRACKS
  if (pressed & KEY_UP) {
    if (trk_cursor > 0) trk_cursor--;
    trk_scroll = clamp_scroll(trk_cursor, trk_scroll, trk_rows());
    return 1;
  }
  if (pressed & KEY_DOWN) {
    if (trk_cursor < trk_n - 1) trk_cursor++;
    trk_scroll = clamp_scroll(trk_cursor, trk_scroll, trk_rows());
    return 1;
  }
  if (pressed & KEY_A) {
    play_from_tracks(trk_cursor);
    return 1;
  }
  if (pressed & KEY_B) {  // the no-album artist skipped Albums on the way in
    screen = (lib_artists[cur_artist].n_albums > 0) ? SCR_ALBUMS : SCR_LIB;
    return 1;
  }
  return 0;
}

int ui_tick(void) {
  anim++;
  if (playing) {
    if (screen == SCR_NOW) {
      eq_tick();  // bars freeze on pause / when hidden
      // chapter title follows playback (fetched only when it changes)
      if (chap_count > 0) {
        int ci = chap_index_for(codec_pos_seconds());
        if (ci != chap_shown) {
          chap_shown = ci;
          chap_title(play_track_p(), ci, chap_buf, sizeof chap_buf);
        }
      }
    }
    if (codec_at_eof()) {
      int cur_rel = play_gidx - play_first;
      if (repeat_mode == REP_ONE) {
        start_play(play_gidx, play_alb, play_artist);
      } else if (repeat_mode == REP_OFF && !shuffle && cur_rel == play_n - 1) {
        playing = 0;
        codec_stop();
      } else {
        start_play(play_first + next_rel(cur_rel), play_alb, play_artist);
      }
      return 1;
    }
  }
  return 0;
}

// ----------------------------------------------------------------- render
// status bar: ‹ Menu | screen title | play glyph + RTC clock
static void render_status(const char *title, int title_len) {
  gfx_vgrad(0, 0, SCREEN_W, STATUS_H, GRAD_MINIBAR, GRAD_MINIBAR_N);
  gfx_hline(0, STATUS_H - 1, SCREEN_W, COL_DIVIDER);
  if (screen != SCR_LIB) gfx_text_small(6, 8, "< Menu", COL_PATH);
  draw_centered(6, 60, SCREEN_W - 120, title, title_len, COL_TITLE, 0);
  if (play_gidx >= 0 && screen != SCR_NOW)
    gfx_text_small(SCREEN_W - 6 - 25 - 14, 8, playing ? ">" : "||", COL_TOGGLE_ON);
  draw_clock(SCREEN_W - 6, 8, COL_CLOCK);
}

static void row_divider(int y, int h) { gfx_hline(6, y + h - 2, SCREEN_W - 12, COL_DRAWER_BG); }

static void row_highlight(int y, int h) {
  gfx_fill_round(4, y - 1, SCREEN_W - 8, h - 2, COL_CURSOR_BG);
}

// pixels reserved on the right of a track row for its duration text
// (shared by the full render and the marquee repaint so the boxes match)
static int dur_reserve(const track_t *t) {
  if (!t->dur_s) return 0;
  char b[16];
  s_time(b, t->dur_s);
  return gfx_text_px_len(b, s_len(b), 1) + 6;
}

// one track row (Bibliothèque loose / Albums loose / Titres): optional
// number, play marker, title, duration on the right
static void render_track_row(int y, int h, const track_t *t, int num, int sel) {
  if (sel) row_highlight(y, h);
  int x = ROW_X;
  if (num > 0) {
    char b[12];
    u32_to_dec((uint32_t)num, b);
    gfx_text_small(x, y + 5, b, COL_META);
    x += 20;
  }
  if (t->dur_s) {
    char b[16];
    s_time(b, t->dur_s);
    gfx_text_small(ROW_X_MAX - (dur_reserve(t) - 6), y + 5, b, COL_META);
  }
  if (play_gidx >= 0 && &lib_tracks[play_gidx] == t) {
    gfx_text_small(x, y + 5, ">", COL_FOCUS);
    x += 9;
  }
  draw_marquee(x, y + 3, ROW_X_MAX - dur_reserve(t) - x, lib_str(t->title),
               t->title.len, COL_TITLE, 0, sel);
  row_divider(y, h);
}

static void render_lib(void) {
  render_status("Biblioth\xc3\xa8que", 13);  // len in BYTES (è is 2)
  int rows = lib_rows(), n = n_lib_rows();
  if (n == 0) {
    gfx_text(ROW_X, LIST_Y0 + 10, "Biblioth\xc3\xa8que vide", COL_META);
    return;
  }
  for (int i = 0; i < rows; i++) {
    int idx = lib_scroll + i;
    if (idx >= n) break;
    int y = LIST_Y0 + i * ROW_H;
    int sel = (idx == lib_cursor);
    if (idx < lib_n_artists) {  // artist folder row
      if (sel) row_highlight(y, ROW_H);
      const artist_t *a = &lib_artists[idx];
      draw_marquee(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, lib_str(a->name),
                   a->name.len, COL_TITLE, 0, sel);
      gfx_text(ROW_X_MAX - 7, y + 3, ">", COL_HINT);
      row_divider(y, ROW_H);
    } else {
      render_track_row(y, ROW_H, &lib_tracks[lib_root_first_track + (idx - lib_n_artists)],
                       0, sel);
    }
  }
}

static void render_albums(void) {
  const artist_t *a = &lib_artists[cur_artist];
  render_status(lib_str(a->name), a->name.len);
  int rows = alb_rows(), n = n_alb_rows();
  for (int i = 0; i < rows; i++) {
    int idx = alb_scroll + i;
    if (idx >= n) break;
    int y = LIST_Y0 + i * ALB_ROW_H;
    int sel = (idx == alb_cursor);
    if (idx < a->n_albums) {  // album folder row: title + "year · genre"
      const album_t *al = &lib_albums[a->first_album + idx];
      if (sel) row_highlight(y, ALB_ROW_H);
      draw_marquee(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, lib_str(al->title),
                   al->title.len, COL_TITLE, 0, sel);
      gfx_text(ROW_X_MAX - 7, y + 10, ">", COL_HINT);
      char meta[48], *p = meta;
      if (al->year) p = s_udec(p, al->year);
      if (al->year && al->genre.len) p = s_append(p, " \xc2\xb7 ");
      *p = 0;
      int mx = gfx_text_small(ROW_X, y + 20, meta, COL_META);
      if (al->genre.len)
        gfx_textn_small(mx, y + 20, lib_str(al->genre), al->genre.len,
                        ROW_X_MAX - 14, COL_META);
      row_divider(y, ALB_ROW_H);
    } else {  // the artist's loose track (mixed case): two-line track row
      const track_t *t = &lib_tracks[a->first_rtrack + (idx - a->n_albums)];
      if (sel) row_highlight(y, ALB_ROW_H);
      int x = ROW_X;
      if (play_gidx >= 0 && &lib_tracks[play_gidx] == t) {
        gfx_text_small(x, y + 5, ">", COL_FOCUS);
        x += 9;
      }
      draw_marquee(x, y + 3, ROW_X_MAX - x, lib_str(t->title), t->title.len,
                   COL_TITLE, 0, sel);
      char m[24], *p = m;
      if (t->dur_s) {
        p = s_time(p, t->dur_s);
        p = s_append(p, " \xc2\xb7 ");
      }
      s_append(p, lib_format_name(t->format));
      gfx_text_small(ROW_X, y + 20, m, COL_META);
      row_divider(y, ALB_ROW_H);
    }
  }
}

static void render_tracks(void) {
  const artist_t *a = &lib_artists[cur_artist];
  if (trk_album >= 0) {
    const album_t *al = &lib_albums[trk_album];
    render_status(lib_str(al->title), al->title.len);
  } else
    render_status(lib_str(a->name), a->name.len);
  int rows = trk_rows();
  for (int i = 0; i < rows; i++) {
    int idx = trk_scroll + i;
    if (idx >= trk_n) break;
    render_track_row(LIST_Y0 + i * ROW_H, ROW_H, &lib_tracks[trk_first + idx],
                     idx + 1, idx == trk_cursor);
  }
}

// ---- Lecture (Now Playing) ------------------------------------------------
static void render_now_title(void) {
  const track_t *t = play_track_p();
  gfx_fill_rect(20, NOW_TITLE_Y, SCREEN_W - 40, 13, COL_BG);
  draw_centered(NOW_TITLE_Y, 20, SCREEN_W - 40, lib_str(t->title), t->title.len,
                COL_TRACK_TITLE, 0);
}

static void render_now_chapter(void) {
  if (chap_count <= 0) return;
  gfx_fill_rect(20, NOW_CHAP_Y, SCREEN_W - 40, 9, COL_BG);
  char line[96], *p = line;
  p = s_append(p, "Chap. ");
  p = s_udec(p, (uint32_t)(chap_shown < 0 ? 1 : chap_shown + 1));
  *p++ = '/';
  p = s_udec(p, (uint32_t)chap_count);
  if (chap_buf[0]) {
    p = s_append(p, " \xc2\xb7 ");
    p = s_append(p, chap_buf);
  }
  draw_centered(NOW_CHAP_Y, 20, SCREEN_W - 40, line, s_len(line), COL_SUBTITLE, 1);
}

static void render_now_eq(void) {
  gfx_fill_round(NOW_EQ_X, NOW_EQ_Y, NOW_EQ_W, NOW_EQ_H, COL_EQ_BG);
  int inner = NOW_EQ_H - 8;
  int bw = (NOW_EQ_W - 8 - 17 * 2) / 18;
  int x0 = NOW_EQ_X + (NOW_EQ_W - (18 * bw + 17 * 2)) / 2;
  int base = NOW_EQ_Y + NOW_EQ_H - 4;
  for (int b = 0; b < EQ_BANDS; b++) {
    int h = eq_bars[b] * inner / EQ_BAR_MAX;
    if (h <= 0) continue;
    uint8_t c = (h > inner * 7 / 10) ? COL_EQ_HI
              : (h > inner * 4 / 10) ? COL_EQ_MID : COL_EQ_LO;
    gfx_fill_rect(x0 + b * (bw + 2), base - h, bw, h, c);
  }
  for (int y = NOW_EQ_Y + 2; y < NOW_EQ_Y + NOW_EQ_H - 2; y += 3)
    gfx_hline(NOW_EQ_X + 2, y, NOW_EQ_W - 4, COL_EQ_BG);
}

static void render_now_progress(void) {
  const track_t *t = play_track_p();
  uint32_t sec = codec_pos_seconds();
  gfx_fill_round(NOW_BAR_X, NOW_BAR_Y, NOW_BAR_W, 5, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t w = (uint32_t)NOW_BAR_W * sec / t->dur_s;
    if (w > (uint32_t)NOW_BAR_W) w = NOW_BAR_W;
    if (w > 4) gfx_fill_round(NOW_BAR_X, NOW_BAR_Y, (int)w, 5, COL_PROGRESS_FILL);
  }
  gfx_fill_rect(NOW_BAR_X, NOW_TIME_Y, NOW_BAR_W, 9, COL_BG);
  char b[16], *p;
  s_time(b, sec);
  gfx_text_small(NOW_BAR_X, NOW_TIME_Y, b, COL_META);
  if (t->dur_s) {
    uint32_t rem = sec < t->dur_s ? t->dur_s - sec : 0;
    p = b;
    *p++ = '-';
    s_time(p, rem);
    int tw = gfx_text_px_len(b, s_len(b), 1);
    gfx_text_small(NOW_BAR_X + NOW_BAR_W - tw, NOW_TIME_Y, b, COL_META);
  }
  // "Piste i/n" centered between the two times
  char c[24];
  p = c;
  p = s_append(p, "Piste ");
  p = s_udec(p, (uint32_t)(play_gidx - play_first) + 1);
  *p++ = '/';
  s_udec(p, (uint32_t)play_n);
  draw_centered(NOW_TIME_Y, NOW_BAR_X + 40, NOW_BAR_W - 80, c, s_len(c), COL_META, 1);
}

static void render_now(void) {
  render_status("Lecture", 7);

  if (art_ready()) {
    art_draw_big(NOW_COVER_X, NOW_COVER_Y);
    gfx_rect_round(NOW_COVER_X, NOW_COVER_Y, NOW_COVER, NOW_COVER, COL_DIVIDER);
  } else
    draw_cover_ph(NOW_COVER_X, NOW_COVER_Y, NOW_COVER, play_hue());
  if (!playing)  // pause badge over the cover corner
    gfx_text(NOW_COVER_X + NOW_COVER - 16, NOW_COVER_Y + 4, "||", COL_WHITE);

  render_now_title();
  if (!playing && last_start_err) {
    char e[16], *q = e;
    q = s_append(q, "ERR ");
    s_udec(q, (uint32_t)(-last_start_err));
    draw_centered(NOW_COVER_Y + NOW_COVER / 2, 20, SCREEN_W - 40, e, s_len(e), COL_WHITE, 0);
  }

  // "{Artist}" or "{Artist} · {Album}"
  {
    char sub[128], *p = sub;
    if (play_artist >= 0) {
      const artist_t *ar = &lib_artists[play_artist];
      const char *nm = lib_str(ar->name);
      for (int i = 0; i < ar->name.len && p < sub + 80; i++) *p++ = nm[i];
    }
    if (play_alb >= 0) {
      const album_t *al = &lib_albums[play_alb];
      if (p != sub) p = s_append(p, " \xc2\xb7 ");
      const char *ti = lib_str(al->title);
      for (int i = 0; i < al->title.len && p < sub + 126; i++) *p++ = ti[i];
    }
    *p = 0;
    if (p != sub) draw_centered(NOW_SUB_Y, 20, SCREEN_W - 40, sub, s_len(sub), COL_SUBTITLE, 1);
  }

  render_now_chapter();
  render_now_eq();
  render_now_progress();

  // bottom toggles: shuffle | format · bitrate | repeat
  const track_t *t = play_track_p();
  gfx_text_small(NOW_BAR_X, NOW_TOGGLE_Y, "ALEA",
                 shuffle ? COL_TOGGLE_ON : COL_TOGGLE_OFF);
  {
    char m[32], *p = m;
    p = s_append(p, lib_format_name(t->format));
    if (t->kbps) {
      p = s_append(p, " \xc2\xb7 ");
      p = s_udec(p, t->kbps);
      p = s_append(p, " kbps");
    }
    draw_centered(NOW_TOGGLE_Y, 60, SCREEN_W - 120, m, s_len(m), COL_META, 1);
  }
  {
    const char *r = repeat_str();
    int tw = gfx_text_px_len(r, s_len(r), 1);
    gfx_text_small(SCREEN_W - NOW_BAR_X - tw, NOW_TOGGLE_Y, r,
                   repeat_mode == REP_OFF ? COL_TOGGLE_OFF : COL_TOGGLE_ON);
  }
}

// ---- persistent mini-bar (lists only): cover, title marquee, progress ----
static void render_minibar_progress(void) {
  const track_t *t = play_track_p();
  int bw = SCREEN_W - 42 - 34;
  gfx_fill_rect(42, MBAR_Y + 24, bw, 3, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t w = (uint32_t)bw * codec_pos_seconds() / t->dur_s;
    if (w > (uint32_t)bw) w = bw;
    gfx_fill_rect(42, MBAR_Y + 24, (int)w, 3, COL_PROGRESS_FILL);
  }
}

static void render_minibar(void) {
  const track_t *t = play_track_p();
  gfx_vgrad(0, MBAR_Y, SCREEN_W, MBAR_H, GRAD_MINIBAR, GRAD_MINIBAR_N);
  gfx_hline(0, MBAR_Y, SCREEN_W, COL_DIVIDER);

  if (art_ready()) art_draw_mini(8, MBAR_Y + 9);
  else draw_cover_ph(8, MBAR_Y + 9, 24, play_hue());

  draw_marquee(42, MBAR_Y + 6, SCREEN_W - 42 - 34, lib_str(t->title),
               t->title.len, COL_TITLE, 0, 1);
  render_minibar_progress();
  gfx_text(SCREEN_W - 24, MBAR_Y + 10, playing ? ">" : "||", COL_TOGGLE_ON);
}

void ui_render_full(void) {
  lib_scroll = clamp_scroll(lib_cursor, lib_scroll, lib_rows());
  alb_scroll = clamp_scroll(alb_cursor, alb_scroll, alb_rows());
  trk_scroll = clamp_scroll(trk_cursor, trk_scroll, trk_rows());

  gfx_clear(COL_BG);
  if (screen == SCR_NOW && play_gidx >= 0) render_now();
  else {
    if (screen == SCR_ALBUMS) render_albums();
    else if (screen == SCR_TRACKS) render_tracks();
    else render_lib();
    if (minibar_on()) render_minibar();
  }
}

// Per-frame animation: selected row marquee on lists; title/chapter/EQ/
// progress on Lecture — without repainting the whole screen.
static void animate_selection(void) {
  if (screen == SCR_LIB) {
    if (lib_cursor < lib_scroll || lib_cursor >= lib_scroll + lib_rows()) return;
    int y = LIST_Y0 + (lib_cursor - lib_scroll) * ROW_H;
    if (lib_cursor < lib_n_artists) {
      const artist_t *a = &lib_artists[lib_cursor];
      gfx_fill_rect(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, 13, COL_CURSOR_BG);
      draw_marquee(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, lib_str(a->name),
                   a->name.len, COL_TITLE, 0, 1);
    } else {
      const track_t *t = &lib_tracks[lib_root_first_track + (lib_cursor - lib_n_artists)];
      int x = ROW_X + (play_gidx >= 0 && &lib_tracks[play_gidx] == t ? 9 : 0);
      int w = ROW_X_MAX - dur_reserve(t) - x;
      gfx_fill_rect(x, y + 3, w, 13, COL_CURSOR_BG);
      draw_marquee(x, y + 3, w, lib_str(t->title), t->title.len, COL_TITLE, 0, 1);
    }
  } else if (screen == SCR_ALBUMS) {
    const artist_t *a = &lib_artists[cur_artist];
    if (alb_cursor < alb_scroll || alb_cursor >= alb_scroll + alb_rows()) return;
    int y = LIST_Y0 + (alb_cursor - alb_scroll) * ALB_ROW_H;
    if (alb_cursor < a->n_albums) {
      const album_t *al = &lib_albums[a->first_album + alb_cursor];
      gfx_fill_rect(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, 13, COL_CURSOR_BG);
      draw_marquee(ROW_X, y + 3, ROW_X_MAX - ROW_X - 14, lib_str(al->title),
                   al->title.len, COL_TITLE, 0, 1);
    } else {
      const track_t *t = &lib_tracks[a->first_rtrack + (alb_cursor - a->n_albums)];
      int x = ROW_X + (play_gidx >= 0 && &lib_tracks[play_gidx] == t ? 9 : 0);
      gfx_fill_rect(x, y + 3, ROW_X_MAX - x, 13, COL_CURSOR_BG);
      draw_marquee(x, y + 3, ROW_X_MAX - x, lib_str(t->title), t->title.len,
                   COL_TITLE, 0, 1);
    }
  } else if (screen == SCR_TRACKS) {
    if (trk_cursor < trk_scroll || trk_cursor >= trk_scroll + trk_rows()) return;
    int y = LIST_Y0 + (trk_cursor - trk_scroll) * ROW_H;
    const track_t *t = &lib_tracks[trk_first + trk_cursor];
    int x = ROW_X + 20 + (play_gidx >= 0 && &lib_tracks[play_gidx] == t ? 9 : 0);
    int w = ROW_X_MAX - dur_reserve(t) - x;
    gfx_fill_rect(x, y + 3, w, 13, COL_CURSOR_BG);
    draw_marquee(x, y + 3, w, lib_str(t->title), t->title.len, COL_TITLE, 0, 1);
  }
}

void ui_render_playing(void) {
  if (screen == SCR_NOW && play_gidx >= 0) {
    render_now_title();
    render_now_chapter();
    render_now_eq();
    render_now_progress();
  } else {
    animate_selection();
    if (minibar_on()) render_minibar();  // cheap: cover comes from the cache
  }
}

// ------------------------------------------------- savestate (sleep / wake)
// 64-byte blob: everything needed to come back exactly where the user left.
// v2: screen stack + the two play-context handles (album / artist) — the
// track ranges are re-derived on wake so a re-indexed library can't leave a
// stale range behind.
#define SS_MAGIC 0x50545332u  /* "PTS2" */

void ui_get_state(uint32_t w[SS_WORDS]) {
  for (int i = 0; i < SS_WORDS; i++) w[i] = 0;
  w[0] = SS_MAGIC;
  w[1] = ((uint32_t)(uint16_t)play_gidx) | ((uint32_t)(uint16_t)play_alb << 16);
  w[2] = codec_pos_seconds();
  w[3] = (playing ? 1u : 0) | (shuffle ? 2u : 0) | ((uint32_t)repeat_mode << 2) |
         ((uint32_t)screen << 4) | ((uint32_t)prev_screen << 6);
  w[4] = ((uint32_t)(uint16_t)lib_cursor) | ((uint32_t)(uint8_t)alb_cursor << 16) |
         ((uint32_t)(uint8_t)trk_cursor << 24);
  w[5] = ((uint32_t)(uint8_t)cur_artist) | ((uint32_t)(uint8_t)play_artist << 8) |
         ((uint32_t)(uint16_t)trk_album << 16);
  w[6] = (play_gidx >= 0) ? lib_tracks[play_gidx].fsize : 0;
}

void ui_apply_state(const uint32_t w[SS_WORDS]) {
  if (w[0] != SS_MAGIC || (lib_n_artists == 0 && lib_n_root_tracks == 0)) return;

  // modes first — cheap and always valid
  shuffle = (w[3] >> 1) & 1;
  repeat_mode = clampi((int)((w[3] >> 2) & 3), 0, 2);

  // browse position, clamped against the (possibly re-indexed) library
  cur_artist = lib_n_artists ? clampi((int)(w[5] & 0xFF), 0, lib_n_artists - 1) : 0;
  lib_cursor = clampi((int)(w[4] & 0xFFFF), 0, n_lib_rows() - 1);
  lib_scroll = clamp_scroll(lib_cursor, 0, lib_rows());
  alb_cursor = clampi((int)((w[4] >> 16) & 0xFF), 0,
                      n_alb_rows() ? n_alb_rows() - 1 : 0);
  alb_scroll = clamp_scroll(alb_cursor, 0, alb_rows());

  // Titres context: only restore it if it still exists
  int t_alb = (int)(int16_t)(w[5] >> 16);
  int scr = clampi((int)((w[3] >> 4) & 3), SCR_LIB, SCR_NOW);
  int prev = clampi((int)((w[3] >> 6) & 3), SCR_LIB, SCR_TRACKS);
  trk_album = (t_alb >= 0 && t_alb < lib_n_albums) ? t_alb : -1;
  if (lib_n_artists) derive_trk_ctx();
  else { trk_first = 0; trk_n = 0; }
  if (trk_n == 0 && (scr == SCR_TRACKS || prev == SCR_TRACKS)) {
    if (scr == SCR_TRACKS) scr = SCR_LIB;
    if (prev == SCR_TRACKS) prev = SCR_LIB;
  }
  trk_cursor = trk_n ? clampi((int)((w[4] >> 24) & 0xFF), 0, trk_n - 1) : 0;
  trk_scroll = clamp_scroll(trk_cursor, 0, trk_rows());
  screen = scr;
  prev_screen = prev;

  // playback: only if the saved track still exists and looks like the same file
  int gidx = (int)(int16_t)(w[1] & 0xFFFF);
  int alb = (int)(int16_t)(w[1] >> 16);
  int part = (int)(int8_t)((w[5] >> 8) & 0xFF);
  if (alb >= lib_n_albums) alb = -1;
  if (part >= lib_n_artists) part = -1;
  if (gidx >= 0 && gidx < lib_n_tracks &&
      lib_tracks[gidx].fsize == w[6] && w[6] != 0) {
    start_play(gidx, alb, part);
    if (last_start_err == 0) {
      const track_t *t = &lib_tracks[gidx];
      uint32_t pos = w[2];
      if (t->dur_s && pos >= t->dur_s) pos = 0;
      if (pos > 0) codec_seek(pos, seek_byte_off(t, pos));
      if (!(w[3] & 1)) {  // was paused
        playing = 0;
        codec_set_paused(1);
      }
    }
  } else if (screen == SCR_NOW) {
    screen = prev_screen;  // the track vanished: fall back to its list
  }
}
