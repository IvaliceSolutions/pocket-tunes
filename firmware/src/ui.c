// Pocket Tunes browser UI — implements the amber-terminal design handoff
// (design_handoff_pocket_tunes_1b) at 320x288: artist sidebar → album list →
// now-playing drawer. Cover art uses per-album hue placeholders until real
// thumbnails arrive with the file-streaming milestone.

#include "ui.h"
#include "mmio.h"
#include "gfx.h"
#include "lib.h"
#include "palette.h"
#include "mp3.h"

// ----------------------------------------------------------------- layout
#define SIDEBAR_W 95
#define MAIN_X (SIDEBAR_W + 5)
#define MAIN_X_MAX (SCREEN_W - 4)
#define SB_ROW_H 17
#define SB_ROWS 16
#define AL_ROW_H 38
#define AL_ROWS 6
#define AL_Y0 24
#define DRAWER_H 184
#define DRAWER_Y (SCREEN_H - DRAWER_H)

// ------------------------------------------------------------------ state
enum { FOC_SIDEBAR, FOC_MAIN };
static int focus, sb_cursor, sb_scroll;
static int cur_artist, main_cursor, main_scroll;
static int drawer_open, cur_track, playing;   // cur_track relative to album
static uint32_t pos_frames;
static char pathbuf[288];
static int last_start_err;  // mp3_start() return code, shown if playback fails
static void start_current_track(void);

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

static const album_t *cur_album(void) {
  const artist_t *ar = &lib_artists[cur_artist];
  return &lib_albums[ar->first_album + main_cursor];
}
static const track_t *cur_track_p(void) {
  const album_t *al = cur_album();
  return &lib_tracks[al->first_track + cur_track];
}

static void start_current_track(void) {
  const track_t *t = cur_track_p();
  int n = lib_fetch_path(t, pathbuf, sizeof pathbuf);
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
  gfx_rect(x, y, size, size, COL_DIVIDER);
}

// ------------------------------------------------------------------ input
void ui_init(void) {
  focus = FOC_SIDEBAR;
  sb_cursor = sb_scroll = 0;
  cur_artist = 0;
  main_cursor = main_scroll = 0;
  drawer_open = playing = 0;
  cur_track = 0;
  pos_frames = 0;
}

int ui_input(uint16_t pressed) {
  const artist_t *ar = &lib_artists[cur_artist];

  if (drawer_open) {
    const album_t *al = cur_album();
    if (pressed & (KEY_UP | KEY_DOWN)) {
      int n = al->n_tracks;
      cur_track += (pressed & KEY_DOWN) ? 1 : n - 1;
      cur_track %= n;
      start_current_track();
      return 1;
    }
    if (pressed & KEY_A) {
      playing = !playing;
      mp3_set_paused(!playing);
      return 1;
    }
    if (pressed & KEY_B) {
      drawer_open = 0;
      playing = 0;
      mp3_stop();
      return 1;
    }
    return 0;
  }

  if (focus == FOC_SIDEBAR) {
    if (pressed & (KEY_UP | KEY_DOWN)) {
      sb_cursor += (pressed & KEY_DOWN) ? 1 : lib_n_artists - 1;
      sb_cursor %= lib_n_artists;
      if (sb_cursor < sb_scroll) sb_scroll = sb_cursor;
      if (sb_cursor >= sb_scroll + SB_ROWS) sb_scroll = sb_cursor - SB_ROWS + 1;
      return 1;
    }
    if (pressed & KEY_A) {
      cur_artist = sb_cursor;
      focus = FOC_MAIN;
      main_cursor = main_scroll = 0;
      return 1;
    }
    return 0;
  }

  // focus == FOC_MAIN
  if (pressed & KEY_UP) {
    if (main_cursor > 0) main_cursor--;
    if (main_cursor < main_scroll) main_scroll = main_cursor;
    return 1;
  }
  if (pressed & KEY_DOWN) {
    if (main_cursor < ar->n_albums - 1) main_cursor++;
    if (main_cursor >= main_scroll + AL_ROWS) main_scroll = main_cursor - AL_ROWS + 1;
    return 1;
  }
  if (pressed & KEY_A) {
    drawer_open = 1;
    cur_track = 0;
    start_current_track();
    return 1;
  }
  if (pressed & KEY_B) { focus = FOC_SIDEBAR; return 1; }
  return 0;
}

int ui_tick(void) {
  if (drawer_open && playing) {
    pos_frames = mp3_pos_seconds() * 60u;  // progress driven by real samples
    if (mp3_at_eof()) {  // track ended → next, wrap
      cur_track = (cur_track + 1) % cur_album()->n_tracks;
      start_current_track();
      return 1;
    }
  }
  return 0;
}

// ----------------------------------------------------------------- render
static void render_sidebar(void) {
  gfx_vline(SIDEBAR_W, 0, SCREEN_H, COL_DIVIDER);
  for (int i = 0; i < SB_ROWS; i++) {
    int idx = sb_scroll + i;
    if (idx >= lib_n_artists) break;
    int y = 6 + i * SB_ROW_H;
    if (idx == sb_cursor && focus == FOC_SIDEBAR && !drawer_open)
      gfx_fill_rect(0, y - 2, SIDEBAR_W, SB_ROW_H, COL_CURSOR_BG);
    uint8_t col = (idx == cur_artist) ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM;
    const artist_t *a = &lib_artists[idx];
    gfx_textn(8, y, lib_str(a->name), a->name.len, SIDEBAR_W - 4, col);
  }
}

static void render_main(void) {
  const artist_t *ar = &lib_artists[cur_artist];

  // folder-path header: "{Artist}/"
  int x = gfx_textn_small(MAIN_X, 8, lib_str(ar->name), ar->name.len, MAIN_X_MAX - 6, COL_PATH);
  gfx_text_small(x, 8, "/", COL_PATH);

  for (int i = 0; i < AL_ROWS; i++) {
    int idx = main_scroll + i;
    if (idx >= ar->n_albums) break;
    const album_t *al = &lib_albums[ar->first_album + idx];
    int y = AL_Y0 + i * AL_ROW_H;

    if (idx == main_cursor && focus == FOC_MAIN && !drawer_open)
      gfx_rect(MAIN_X - 3, y - 2, MAIN_X_MAX - MAIN_X + 6, AL_ROW_H - 2, COL_FOCUS);

    draw_cover_ph(MAIN_X, y + 1, 32, al->hue);

    gfx_textn(MAIN_X + 40, y + 3, lib_str(al->title), al->title.len, MAIN_X_MAX, COL_TITLE);

    char meta[48], *p = meta;
    if (al->year) p = s_udec(p, al->year);
    if (al->year && al->genre.len) p = s_append(p, " \xc2\xb7 ");
    *p = 0;
    int mx = gfx_text_small(MAIN_X + 40, y + 20, meta, COL_ARTIST_DIM);
    if (al->genre.len)
      gfx_textn_small(mx, y + 20, lib_str(al->genre), al->genre.len, MAIN_X_MAX, COL_ARTIST_DIM);
  }
}

static void render_progress(void) {
  const track_t *t = cur_track_p();
  int bar_x = 10, bar_w = SCREEN_W - 20, bar_y = DRAWER_Y + 74;
  gfx_fill_rect(bar_x, bar_y, bar_w, 5, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t sec = pos_frames / 60;
    uint32_t w = (uint32_t)bar_w * sec / t->dur_s;
    if (w > (uint32_t)bar_w) w = bar_w;
    gfx_fill_rect(bar_x, bar_y, (int)w, 5, COL_FOCUS);
  }
}

static void render_drawer(void) {
  const album_t *al = cur_album();
  const track_t *t = cur_track_p();

  gfx_fill_rect(0, DRAWER_Y, SCREEN_W, DRAWER_H, COL_DRAWER_BG);
  gfx_hline(0, DRAWER_Y, SCREEN_W, COL_FOCUS);

  draw_cover_ph(10, DRAWER_Y + 10, 56, al->hue);

  // title + play state
  gfx_textn(76, DRAWER_Y + 12, lib_str(t->title), t->title.len, 250, COL_TRACK_TITLE);
  if (!playing && last_start_err) {
    char e[16], *q = e; q = s_append(q, "ERR "); s_udec(q, (uint32_t)(-last_start_err));
    gfx_text_small(SCREEN_W - 10 - 7 * 5, DRAWER_Y + 14, e, COL_WHITE);
  } else {
    gfx_text_small(SCREEN_W - 10 - 9 * 5, DRAWER_Y + 14,
                   playing ? "> LECTURE" : "|| PAUSE", COL_PLAYSTATE);
  }

  // "{album} · piste n/total"
  int sx = gfx_textn_small(76, DRAWER_Y + 32, lib_str(al->title), al->title.len, 200,
                           COL_ARTIST_DIM);
  char sub[32], *p = sub;
  p = s_append(p, " \xc2\xb7 piste ");
  p = s_udec(p, (uint32_t)cur_track + 1);
  *p++ = '/';
  p = s_udec(p, al->n_tracks);
  gfx_text_small(sx, DRAWER_Y + 32, sub, COL_ARTIST_DIM);

  render_progress();

  // metadata lines (design: each field collapsible; absent = collapsed)
  char l1[64], *q = l1;
  if (t->dur_s) { q = s_time(q, t->dur_s); q = s_append(q, " total"); }
  if (al->genre.len) {
    if (q != l1) q = s_append(q, " \xc2\xb7 ");
    // genre is a span — draw separately after the buffer part
  }
  int lx = gfx_text_small(10, DRAWER_Y + 90, l1, COL_ARTIST_DIM);
  if (al->genre.len) {
    lx = gfx_textn_small(lx, DRAWER_Y + 90, lib_str(al->genre), al->genre.len, 260, COL_ARTIST_DIM);
  }
  if (al->year) {
    char yb[16], *yp = yb;
    yp = s_append(yp, " \xc2\xb7 ");
    yp = s_udec(yp, al->year);
    gfx_text_small(lx, DRAWER_Y + 90, yb, COL_ARTIST_DIM);
  }

  char l2[32], *r = l2;
  r = s_append(r, lib_format_name(t->format));
  if (t->kbps) {
    r = s_append(r, " \xc2\xb7 ");
    r = s_udec(r, t->kbps);
    r = s_append(r, " kbps");
  }
  gfx_text_small(10, DRAWER_Y + 102, l2, COL_ARTIST_DIM);

  // control hints pinned at the bottom
  gfx_text_small(10, SCREEN_H - 14, "^v piste \xc2\xb7 A lecture/pause \xc2\xb7 B fermer",
                 COL_ARTIST_DIM);
}

void ui_render_full(void) {
  gfx_clear(COL_BG);
  render_sidebar();
  render_main();
  if (drawer_open) render_drawer();
}

void ui_render_playing(void) {
  if (drawer_open && playing) render_progress();
}
