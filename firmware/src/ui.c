// Pocket Tunes browser UI — design_handoff_pocket_tunes_3a at 320x288.
//
// Three browse levels (Artists sidebar → Albums → Tracks), an enriched
// Now Playing drawer (~78% tall: status row, big header, progress with
// elapsed/remaining, 18-band FFT equalizer) and a persistent mini-bar that
// keeps playback visible while browsing. Playback is independent of the
// browse cursor; closing the drawer never stops the music.
//
// Controls: D-pad + A/B navigate; in the drawer Up/Down = track, Left/Right =
// seek. START = shuffle, SELECT = repeat (kept from the previous iteration —
// the 3a mockup put these on L/R but the user chose to keep the mapping).
// X reopens the drawer while browsing; Y toggles pause from the browser.

#include "ui.h"
#include "mmio.h"
#include "gfx.h"
#include "lib.h"
#include "palette.h"
#include "mp3.h"
#include "eq.h"

// ----------------------------------------------------------------- layout
#define SIDEBAR_W 95
#define SB_X 8
#define SB_ROW_H 17
#define SB_Y0 6

#define MAIN_X (SIDEBAR_W + 5)
#define MAIN_X_MAX (SCREEN_W - 4)

#define AL_ROW_H 38
#define AL_Y0 24
#define AL_TXT_X (MAIN_X + 40)
#define AL_TXT_W (MAIN_X_MAX - AL_TXT_X)

#define TR_ROW_H 22
#define TR_Y0 24
#define TR_TXT_X (MAIN_X + 18)
#define TR_TXT_W (MAIN_X_MAX - TR_TXT_X - 10)  // 10px kept for the ▶ marker

// Now Playing drawer: ~78% of the screen
#define DRAWER_H 224
#define DRAWER_Y (SCREEN_H - DRAWER_H)
#define TITLE_X 84
#define TITLE_Y (DRAWER_Y + 24)
#define TITLE_W (MAIN_X_MAX - TITLE_X)

// Persistent mini-bar (over the browser when a track is loaded)
#define MBAR_H 42
#define MBAR_Y (SCREEN_H - MBAR_H)

// Equalizer strip inside the drawer
#define EQ_X 8
#define EQ_W (SCREEN_W - 2 * EQ_X)
#define EQ_Y (DRAWER_Y + 120)
#define EQ_H 48
#define EQ_INNER (EQ_H - 8)  // bar area height

// ------------------------------------------------------------------ state
enum { FOC_ARTISTS, FOC_ALBUMS, FOC_TRACKS };
static int focus;
static int sb_cursor, sb_scroll;
static int cur_artist;
static int album_cursor, album_scroll;
static int track_cursor, track_scroll;
// playback — independent of the browse cursor
static int play_gidx = -1;  // global track index loaded (-1 = none)
static int play_alb = -1;   // global album index of that track
static int drawer_open, playing;
static uint32_t pos_frames, anim;
static char pathbuf[288];
static int last_start_err;

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
static char *s_time(char *d, uint32_t sec) {
  d = s_udec(d, sec / 60);
  *d++ = ':';
  *d++ = (char)('0' + (sec % 60) / 10);
  *d++ = (char)('0' + sec % 10);
  *d = 0;
  return d;
}
static int clamp_scroll(int cursor, int scroll, int rows) {
  if (cursor < scroll) return cursor;
  if (cursor >= scroll + rows) return cursor - rows + 1;
  return scroll;
}

static const artist_t *sel_artist(void) { return &lib_artists[cur_artist]; }
static const album_t *sel_album(void) {
  return &lib_albums[sel_artist()->first_album + album_cursor];
}
static const album_t *play_album_p(void) { return &lib_albums[play_alb]; }
static const track_t *play_track_p(void) { return &lib_tracks[play_gidx]; }
static const artist_t *play_artist_p(void) {  // artist owning the playing album
  for (int i = 0; i < lib_n_artists; i++) {
    const artist_t *a = &lib_artists[i];
    if (play_alb >= a->first_album && play_alb < a->first_album + a->n_albums) return a;
  }
  return &lib_artists[0];
}
static int minibar_on(void) { return play_gidx >= 0 && !drawer_open; }

// Visible rows shrink when the mini-bar overlays the bottom of the browser.
static int reserved_bottom(void) { return minibar_on() ? MBAR_H + 14 : 14; }
static int sb_rows(void) { return (SCREEN_H - SB_Y0 - reserved_bottom() + SB_ROW_H - 1) / SB_ROW_H - 1; }
static int al_rows(void) { return (SCREEN_H - AL_Y0 - reserved_bottom()) / AL_ROW_H; }
static int tr_rows(void) { return (SCREEN_H - TR_Y0 - reserved_bottom()) / TR_ROW_H; }

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
static int next_rel(const album_t *pa, int cur_rel) {
  int n = pa->n_tracks;
  if (n <= 1) return 0;
  if (!shuffle) return (cur_rel + 1) % n;
  int r = (int)(rng_next() % (uint32_t)n);
  if (r == cur_rel) r = (r + 1) % n;
  return r;
}

static uint32_t seek_byte_off(const track_t *t, uint32_t sec) {
  if (t->dur_s) return (uint32_t)((uint64_t)t->fsize * sec / t->dur_s);
  if (t->kbps) return sec * (t->kbps * 1000u / 8u);
  return 0;
}

static void start_play(int gidx, int alb) {
  play_gidx = gidx;
  play_alb = alb;
  const track_t *t = &lib_tracks[gidx];
  int n = lib_fetch_path(t, pathbuf, sizeof pathbuf);
  anim = 0;
  pos_frames = 0;
  if (n > 0 && t->format == FMT_MP3) {
    last_start_err = mp3_start(pathbuf, n, t->fsize);
    playing = (last_start_err == 0);
  } else {
    last_start_err = (n <= 0) ? -20 : -21;  // -20 path, -21 non-MP3
    playing = 0;
    mp3_stop();
  }
}

// ---- marquee (selected item only)
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
  focus = FOC_ARTISTS;
  sb_cursor = sb_scroll = 0;
  cur_artist = 0;
  album_cursor = album_scroll = 0;
  track_cursor = track_scroll = 0;
  play_gidx = play_alb = -1;
  drawer_open = playing = 0;
  pos_frames = 0;
}

int ui_input(uint16_t pressed) {
  anim = 0;  // restart the selected item's marquee

  if (pressed & KEY_START) { shuffle = !shuffle; return 1; }
  if (pressed & KEY_SELECT) { repeat_mode = (repeat_mode + 1) % 3; return 1; }

  if (drawer_open) {
    if (pressed & (KEY_UP | KEY_DOWN)) {  // prev/next track, auto-plays
      const album_t *pa = play_album_p();
      int cur_rel = play_gidx - pa->first_track;
      int rel = (pressed & KEY_DOWN) ? next_rel(pa, cur_rel)
                                     : (cur_rel + pa->n_tracks - 1) % pa->n_tracks;
      start_play(pa->first_track + rel, play_alb);
      track_cursor = rel;
      track_scroll = clamp_scroll(track_cursor, track_scroll, tr_rows());
      return 1;
    }
    if (pressed & (KEY_LEFT | KEY_RIGHT)) {  // seek ±10 s
      const track_t *t = play_track_p();
      uint32_t cur = mp3_pos_seconds();
      uint32_t tgt = (pressed & KEY_RIGHT) ? cur + 10 : (cur > 10 ? cur - 10 : 0);
      if (t->dur_s && tgt >= t->dur_s) tgt = t->dur_s ? t->dur_s - 1 : 0;
      mp3_seek(tgt, seek_byte_off(t, tgt));
      pos_frames = tgt * 60u;
      return 1;
    }
    if (pressed & KEY_A) {
      playing = !playing;
      mp3_set_paused(!playing);
      return 1;
    }
    if (pressed & KEY_B) {  // close overlay only — playback keeps running
      drawer_open = 0;
      return 1;
    }
    return 0;
  }

  // browsing with a loaded track: X reopens the drawer, Y toggles pause
  if (play_gidx >= 0) {
    if (pressed & KEY_X) { drawer_open = 1; return 1; }
    if (pressed & KEY_Y) {
      playing = !playing;
      mp3_set_paused(!playing);
      return 1;
    }
  }

  if (focus == FOC_ARTISTS) {
    if (pressed & (KEY_UP | KEY_DOWN)) {
      sb_cursor += (pressed & KEY_DOWN) ? 1 : lib_n_artists - 1;
      sb_cursor %= lib_n_artists;
      sb_scroll = clamp_scroll(sb_cursor, sb_scroll, sb_rows());
      return 1;
    }
    if (pressed & KEY_A) {
      cur_artist = sb_cursor;
      focus = FOC_ALBUMS;
      album_cursor = album_scroll = 0;
      return 1;
    }
    return 0;
  }

  if (focus == FOC_ALBUMS) {
    if (pressed & KEY_UP) {
      if (album_cursor > 0) album_cursor--;
      album_scroll = clamp_scroll(album_cursor, album_scroll, al_rows());
      return 1;
    }
    if (pressed & KEY_DOWN) {
      if (album_cursor < sel_artist()->n_albums - 1) album_cursor++;
      album_scroll = clamp_scroll(album_cursor, album_scroll, al_rows());
      return 1;
    }
    if (pressed & KEY_A) {  // → tracks (does NOT start playback)
      focus = FOC_TRACKS;
      track_cursor = track_scroll = 0;
      return 1;
    }
    if (pressed & KEY_B) { focus = FOC_ARTISTS; return 1; }
    return 0;
  }

  // focus == FOC_TRACKS
  if (pressed & KEY_UP) {
    if (track_cursor > 0) track_cursor--;
    track_scroll = clamp_scroll(track_cursor, track_scroll, tr_rows());
    return 1;
  }
  if (pressed & KEY_DOWN) {
    if (track_cursor < sel_album()->n_tracks - 1) track_cursor++;
    track_scroll = clamp_scroll(track_cursor, track_scroll, tr_rows());
    return 1;
  }
  if (pressed & KEY_A) {
    int gidx = sel_album()->first_track + track_cursor;
    int alb = sel_artist()->first_album + album_cursor;
    if (gidx != play_gidx)  // same track already loaded → just reopen
      start_play(gidx, alb);
    drawer_open = 1;
    return 1;
  }
  if (pressed & KEY_B) { focus = FOC_ALBUMS; return 1; }
  return 0;
}

int ui_tick(void) {
  anim++;
  if (playing) {
    if (drawer_open) eq_tick();  // bars freeze on pause / when hidden
    pos_frames = mp3_pos_seconds() * 60u;
    if (mp3_at_eof()) {
      const album_t *pa = play_album_p();
      int cur_rel = play_gidx - pa->first_track;
      if (repeat_mode == REP_ONE) {
        start_play(play_gidx, play_alb);
      } else if (repeat_mode == REP_OFF && !shuffle && cur_rel == pa->n_tracks - 1) {
        playing = 0;
        mp3_stop();
      } else {
        int rel = next_rel(pa, cur_rel);
        start_play(pa->first_track + rel, play_alb);
        if (drawer_open || focus == FOC_TRACKS) {
          track_cursor = rel;
          track_scroll = clamp_scroll(track_cursor, track_scroll, tr_rows());
        }
      }
      return 1;
    }
  }
  return 0;
}

// ----------------------------------------------------------------- render
static void level_footer(uint32_t cur, uint32_t total, const char *noun) {
  int y = minibar_on() ? MBAR_Y - 12 : SCREEN_H - 12;
  char f[48], *p = f;
  p = s_append(p, "NIVEAU : ");
  p = s_udec(p, cur);
  *p++ = '/';
  p = s_udec(p, total);
  *p++ = ' ';
  s_append(p, noun);
  int x = gfx_text_small(MAIN_X, y, f, COL_META);
  x = gfx_text_small(x + 6, y, repeat_str(), COL_TOGGLE_ON);
  if (shuffle) x = gfx_text_small(x + 6, y, "ALEA", COL_TOGGLE_ON);
  if (minibar_on()) gfx_text_small(x + 6, y, "\xc2\xb7 X lecteur", COL_HINT);
}

static void render_sidebar(void) {
  int bottom = minibar_on() ? MBAR_Y : SCREEN_H;
  gfx_vline(SIDEBAR_W, 0, bottom, COL_DIVIDER);
  int rows = sb_rows();
  for (int i = 0; i < rows; i++) {
    int idx = sb_scroll + i;
    if (idx >= lib_n_artists) break;
    int y = SB_Y0 + i * SB_ROW_H;
    int sel = (idx == sb_cursor && focus == FOC_ARTISTS && !drawer_open);
    if (sel) gfx_fill_round(0, y - 2, SIDEBAR_W, SB_ROW_H, COL_CURSOR_BG);
    uint8_t col = (idx == cur_artist) ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM;
    const artist_t *a = &lib_artists[idx];
    draw_marquee(SB_X, y, SIDEBAR_W - 4 - SB_X, lib_str(a->name), a->name.len, col, 0, sel);
  }
}

// Breadcrumb "{Artist}/" or "{Artist}/{Album}/" + clock on the right.
static void render_breadcrumb(int tracks_level) {
  const artist_t *ar = sel_artist();
  int x_max = MAIN_X_MAX - 34;  // keep room for the clock
  int x = gfx_textn_small(MAIN_X, 8, lib_str(ar->name), ar->name.len,
                          tracks_level ? MAIN_X + 80 : x_max, COL_PATH);
  x = gfx_text_small(x, 8, "/", COL_PATH);
  if (tracks_level) {
    const album_t *al = sel_album();
    x = gfx_textn_small(x, 8, lib_str(al->title), al->title.len, x_max, COL_PATH);
    gfx_text_small(x, 8, "/", COL_PATH);
  }
  draw_clock(MAIN_X_MAX, 8, COL_CLOCK);
}

static void render_albums(void) {
  const artist_t *ar = sel_artist();
  render_breadcrumb(0);
  int rows = al_rows();
  for (int i = 0; i < rows; i++) {
    int idx = album_scroll + i;
    if (idx >= ar->n_albums) break;
    const album_t *al = &lib_albums[ar->first_album + idx];
    int y = AL_Y0 + i * AL_ROW_H;
    int sel = (idx == album_cursor && focus == FOC_ALBUMS && !drawer_open);
    if (sel) gfx_rect_round(MAIN_X - 3, y - 2, MAIN_X_MAX - MAIN_X + 6, AL_ROW_H - 2, COL_FOCUS);
    draw_cover_ph(MAIN_X, y + 1, 32, al->hue);
    draw_marquee(AL_TXT_X, y + 3, AL_TXT_W, lib_str(al->title), al->title.len, COL_TITLE, 0, sel);

    char meta[48], *p = meta;
    if (al->year) p = s_udec(p, al->year);
    if (al->year && al->genre.len) p = s_append(p, " \xc2\xb7 ");
    *p = 0;
    int mx = gfx_text_small(AL_TXT_X, y + 20, meta, COL_META);
    if (al->genre.len)
      gfx_textn_small(mx, y + 20, lib_str(al->genre), al->genre.len, MAIN_X_MAX, COL_META);
  }
}

static void render_tracks(void) {
  const album_t *al = sel_album();
  render_breadcrumb(1);
  int rows = tr_rows();
  for (int i = 0; i < rows; i++) {
    int idx = track_scroll + i;
    if (idx >= al->n_tracks) break;
    const track_t *t = &lib_tracks[al->first_track + idx];
    int y = TR_Y0 + i * TR_ROW_H;
    int sel = (idx == track_cursor && focus == FOC_TRACKS && !drawer_open);
    if (sel) gfx_rect_round(MAIN_X - 3, y - 2, MAIN_X_MAX - MAIN_X + 6, TR_ROW_H - 2, COL_FOCUS);

    char num[12];
    u32_to_dec((uint32_t)idx + 1, num);
    gfx_text_small(MAIN_X, y + 2, num, COL_META);

    draw_marquee(TR_TXT_X, y, TR_TXT_W, lib_str(t->title), t->title.len, COL_TITLE, 0, sel);

    char m[24], *p = m;
    if (t->dur_s) { p = s_time(p, t->dur_s); p = s_append(p, " \xc2\xb7 "); }
    s_append(p, lib_format_name(t->format));
    gfx_text_small(TR_TXT_X, y + 12, m, COL_META);

    if (al->first_track + idx == play_gidx)
      gfx_text_small(MAIN_X_MAX - 6, y + 2, ">", COL_FOCUS);
  }
}

static void render_main(void) {
  if (focus == FOC_TRACKS) {
    render_tracks();
    level_footer((uint32_t)track_cursor + 1, sel_album()->n_tracks, "pistes");
  } else {
    render_albums();
    if (focus == FOC_ARTISTS)
      level_footer((uint32_t)sb_cursor + 1, lib_n_artists, "artistes");
    else
      level_footer((uint32_t)album_cursor + 1, sel_artist()->n_albums, "albums");
  }
}

// ---- drawer pieces --------------------------------------------------------
static void render_progress(void) {
  const track_t *t = play_track_p();
  int bar_x = 10, bar_w = SCREEN_W - 20, bar_y = DRAWER_Y + 94;
  uint32_t sec = pos_frames / 60;
  gfx_fill_round(bar_x, bar_y, bar_w, 5, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t w = (uint32_t)bar_w * sec / t->dur_s;
    if (w > (uint32_t)bar_w) w = bar_w;
    if (w > 4) gfx_fill_round(bar_x, bar_y, (int)w, 5, COL_PROGRESS_FILL);
  }
  // elapsed / -remaining
  char b[12], *p;
  gfx_fill_rect(bar_x, bar_y + 9, bar_w, 9,
                GRAD_DRAWER[(bar_y + 9 - DRAWER_Y) * GRAD_DRAWER_N / DRAWER_H]);
  s_time(b, sec);
  gfx_text_small(bar_x, bar_y + 9, b, COL_META);
  if (t->dur_s) {
    uint32_t rem = sec < t->dur_s ? t->dur_s - sec : 0;
    p = b;
    *p++ = '-';
    s_time(p, rem);
    int tw = gfx_text_px_len(b, 11, 1);
    gfx_text_small(bar_x + bar_w - tw, bar_y + 9, b, COL_META);
  }
}

static void render_eq(void) {
  gfx_fill_round(EQ_X, EQ_Y, EQ_W, EQ_H, COL_EQ_BG);
  // 18 bars, 2px gaps, centered
  int bw = (EQ_W - 8 - 17 * 2) / 18;                  // inner width minus gaps
  int x0 = EQ_X + (EQ_W - (18 * bw + 17 * 2)) / 2;
  int base = EQ_Y + EQ_H - 4;                          // bars grow upward
  for (int b = 0; b < EQ_BANDS; b++) {
    int h = eq_bars[b] * EQ_INNER / EQ_BAR_MAX;
    if (h <= 0) continue;
    uint8_t c = (h > EQ_INNER * 7 / 10) ? COL_EQ_HI
              : (h > EQ_INNER * 4 / 10) ? COL_EQ_MID : COL_EQ_LO;
    gfx_fill_rect(x0 + b * (bw + 2), base - h, bw, h, c);
  }
  // CRT scanlines over the bars: 1 dark line every 3 rows
  for (int y = EQ_Y + 2; y < EQ_Y + EQ_H - 2; y += 3)
    gfx_hline(EQ_X + 2, y, EQ_W - 4, COL_EQ_BG);
}

static void render_title(void) {
  const track_t *t = play_track_p();
  gfx_fill_rect(TITLE_X, TITLE_Y, TITLE_W, 13,
                GRAD_DRAWER[(TITLE_Y - DRAWER_Y) * GRAD_DRAWER_N / DRAWER_H]);
  draw_marquee(TITLE_X, TITLE_Y, TITLE_W, lib_str(t->title), t->title.len,
               COL_TRACK_TITLE, 0, 1);
}

static void render_drawer(void) {
  const artist_t *par = play_artist_p();
  const album_t *al = play_album_p();
  const track_t *t = play_track_p();

  gfx_vgrad(0, DRAWER_Y, SCREEN_W, DRAWER_H, GRAD_DRAWER, GRAD_DRAWER_N);
  gfx_hline(0, DRAWER_Y, SCREEN_W, COL_FOCUS);
  gfx_hline(0, DRAWER_Y + 1, SCREEN_W, COL_DIVIDER);

  // status row: badges left, clock right
  int x = gfx_text_small(10, DRAWER_Y + 6, repeat_str(),
                         repeat_mode == REP_OFF ? COL_TOGGLE_OFF : COL_TOGGLE_ON);
  gfx_text_small(x + 8, DRAWER_Y + 6, "ALEA", shuffle ? COL_TOGGLE_ON : COL_TOGGLE_OFF);
  draw_clock(SCREEN_W - 10, DRAWER_Y + 6, COL_CLOCK);

  // header: cover + title/subtitle/meta
  draw_cover_ph(10, DRAWER_Y + 20, 64, al->hue);
  render_title();
  if (!playing && last_start_err) {
    char e[16], *q = e;
    q = s_append(q, "ERR ");
    s_udec(q, (uint32_t)(-last_start_err));
    gfx_text_small(SCREEN_W - 10 - 7 * 5, TITLE_Y + 2, e, COL_WHITE);
  }

  // "{artist} · {album}"
  int sx = gfx_textn_small(TITLE_X, DRAWER_Y + 42, lib_str(par->name), par->name.len,
                           TITLE_X + 110, COL_SUBTITLE);
  sx = gfx_text_small(sx, DRAWER_Y + 42, " \xc2\xb7 ", COL_SUBTITLE);
  gfx_textn_small(sx, DRAWER_Y + 42, lib_str(al->title), al->title.len,
                  MAIN_X_MAX, COL_SUBTITLE);

  // "Piste n/total · MP3 · 128 kbps" + play state
  char l[48], *p = l;
  p = s_append(p, "Piste ");
  p = s_udec(p, (uint32_t)(play_gidx - al->first_track) + 1);
  *p++ = '/';
  p = s_udec(p, al->n_tracks);
  p = s_append(p, " \xc2\xb7 ");
  p = s_append(p, lib_format_name(t->format));
  if (t->kbps) {
    p = s_append(p, " \xc2\xb7 ");
    p = s_udec(p, t->kbps);
    p = s_append(p, " kbps");
  }
  gfx_text_small(TITLE_X, DRAWER_Y + 56, l, COL_META);
  gfx_text_small(TITLE_X, DRAWER_Y + 70, playing ? "> LECTURE" : "|| PAUSE", COL_PLAYSTATE);

  render_progress();
  render_eq();

  gfx_text_small(10, SCREEN_H - 14,
                 "^v \xc2\xb7 <>10s \xc2\xb7 A pause \xc2\xb7 START alea \xc2\xb7 SEL rpt \xc2\xb7 B",
                 COL_HINT);
}

// ---- persistent mini-bar ---------------------------------------------------
static void render_minibar(void) {
  const track_t *t = play_track_p();
  const album_t *al = play_album_p();
  gfx_vgrad(0, MBAR_Y, SCREEN_W, MBAR_H, GRAD_MINIBAR, GRAD_MINIBAR_N);
  gfx_hline(0, MBAR_Y, SCREEN_W, COL_DIVIDER);

  draw_cover_ph(6, MBAR_Y + 5, 32, al->hue);

  // title (marquee) + thin progress line under it
  draw_marquee(46, MBAR_Y + 6, SCREEN_W - 46 - 34, lib_str(t->title), t->title.len,
               COL_TITLE, 0, 1);
  int bw = SCREEN_W - 46 - 34;
  gfx_fill_rect(46, MBAR_Y + 24, bw, 3, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t w = (uint32_t)bw * (pos_frames / 60) / t->dur_s;
    if (w > (uint32_t)bw) w = bw;
    gfx_fill_rect(46, MBAR_Y + 24, (int)w, 3, COL_PROGRESS_FILL);
  }

  // play/pause glyph
  gfx_text(SCREEN_W - 24, MBAR_Y + 10, playing ? ">" : "||", COL_TOGGLE_ON);
}

void ui_render_full(void) {
  // re-clamp scrolls: the row count changes when the mini-bar (dis)appears
  sb_scroll = clamp_scroll(sb_cursor, sb_scroll, sb_rows());
  album_scroll = clamp_scroll(album_cursor, album_scroll, al_rows());
  track_scroll = clamp_scroll(track_cursor, track_scroll, tr_rows());

  gfx_clear(COL_BG);
  render_sidebar();
  render_main();
  if (drawer_open) render_drawer();
  else if (minibar_on()) render_minibar();
}

// Per-frame animation: marquee of the selected item / drawer title, progress,
// equalizer — without repainting the whole screen.
static void animate_selection(void) {
  if (focus == FOC_ARTISTS) {
    if (sb_cursor < sb_scroll || sb_cursor >= sb_scroll + sb_rows()) return;
    int y = SB_Y0 + (sb_cursor - sb_scroll) * SB_ROW_H;
    const artist_t *a = &lib_artists[sb_cursor];
    uint8_t col = (sb_cursor == cur_artist) ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM;
    gfx_fill_round(0, y - 2, SIDEBAR_W, SB_ROW_H, COL_CURSOR_BG);
    draw_marquee(SB_X, y, SIDEBAR_W - 4 - SB_X, lib_str(a->name), a->name.len, col, 0, 1);
  } else if (focus == FOC_ALBUMS) {
    const artist_t *ar = sel_artist();
    if (album_cursor < album_scroll || album_cursor >= album_scroll + al_rows()) return;
    int y = AL_Y0 + (album_cursor - album_scroll) * AL_ROW_H;
    const album_t *al = &lib_albums[ar->first_album + album_cursor];
    gfx_fill_rect(AL_TXT_X, y + 3, AL_TXT_W, 13, COL_BG);
    draw_marquee(AL_TXT_X, y + 3, AL_TXT_W, lib_str(al->title), al->title.len, COL_TITLE, 0, 1);
  } else {
    const album_t *al = sel_album();
    if (track_cursor < track_scroll || track_cursor >= track_scroll + tr_rows()) return;
    int y = TR_Y0 + (track_cursor - track_scroll) * TR_ROW_H;
    const track_t *t = &lib_tracks[al->first_track + track_cursor];
    gfx_fill_rect(TR_TXT_X, y, TR_TXT_W, 13, COL_BG);
    draw_marquee(TR_TXT_X, y, TR_TXT_W, lib_str(t->title), t->title.len, COL_TITLE, 0, 1);
  }
}

void ui_render_playing(void) {
  if (drawer_open) {
    render_title();
    render_progress();
    render_eq();
  } else {
    animate_selection();
    if (minibar_on()) render_minibar();
  }
}
