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
static int resume_gidx = -1;   // global track index remembered on B (-1 = none)
static uint32_t resume_sec;    // position (s) to resume that same track at
static uint32_t anim;          // free-running frame counter for the title marquee
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
static int cur_gidx(void) { return cur_album()->first_track + cur_track; }

// Estimate the file byte offset for a given time (CBR: file-size ratio, else
// nominal bitrate). Used by seek (FF/RW) and resume.
static uint32_t seek_byte_off(const track_t *t, uint32_t sec) {
  if (t->dur_s) return (uint32_t)((uint64_t)t->fsize * sec / t->dur_s);
  if (t->kbps) return sec * (t->kbps * 1000u / 8u);
  return 0;
}

static void start_current_track(void) {
  const track_t *t = cur_track_p();
  int n = lib_fetch_path(t, pathbuf, sizeof pathbuf);
  int g = cur_gidx();
  anim = 0;         // restart the title marquee from the beginning
  pos_frames = 0;
  if (n > 0 && t->format == FMT_MP3) {
    last_start_err = mp3_start(pathbuf, n, t->fsize);
    playing = (last_start_err == 0);
    // Resume where we left THIS same track (marker set on B); one-shot.
    if (playing && g == resume_gidx && resume_sec > 0) {
      uint32_t sec = resume_sec;
      if (t->dur_s && sec >= t->dur_s) sec = t->dur_s - 1;
      mp3_seek(sec, seek_byte_off(t, sec));
      pos_frames = sec * 60u;
    }
  } else {
    last_start_err = (n <= 0) ? -20 : -21;  // -20 path, -21 non-MP3
    playing = 0;
    mp3_stop();
  }
  resume_gidx = -1;  // consume the resume marker (matched or not)
  resume_sec = 0;
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
    if (pressed & (KEY_LEFT | KEY_RIGHT)) {  // FF / rewind ±10 s
      const track_t *t = cur_track_p();
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
    if (pressed & KEY_B) {
      // remember this track + position so re-opening it resumes here
      if (last_start_err == 0) {
        resume_gidx = cur_gidx();
        resume_sec = mp3_pos_seconds();
      }
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
    const album_t *al = cur_album();
    // if we B'd out of a track in THIS album, re-open on it so it resumes
    if (resume_gidx >= al->first_track &&
        resume_gidx < al->first_track + al->n_tracks)
      cur_track = resume_gidx - al->first_track;
    else
      cur_track = 0;
    start_current_track();
    return 1;
  }
  if (pressed & KEY_B) { focus = FOC_SIDEBAR; return 1; }
  return 0;
}

int ui_tick(void) {
  if (drawer_open) anim++;  // drives the title marquee
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

// Now-playing title in a fixed box at (76 .. 250). If it doesn't fit, scroll
// it left/right with a bounce so the whole title can be read. Redrawn every
// frame from ui_render_playing() so the marquee animates.
#define TITLE_X 76
#define TITLE_Y (DRAWER_Y + 12)
#define TITLE_W 174
static void render_title(const track_t *t) {
  const char *s = lib_str(t->title);
  int len = t->title.len;
  gfx_fill_rect(TITLE_X, TITLE_Y, TITLE_W, 13, COL_DRAWER_BG);
  int tw = gfx_text_px_len(s, len, 0);
  if (tw <= TITLE_W) {
    gfx_textn(TITLE_X, TITLE_Y, s, len, TITLE_X + TITLE_W, COL_TRACK_TITLE);
    return;
  }
  int overflow = tw - TITLE_W;
  int hold = 30;                          // pause units at each end
  int cyc = 2 * hold + 2 * overflow;
  int p = (int)((anim >> 1) % (uint32_t)cyc);  // >>1 → ~30 px/s
  int scroll;
  if (p < hold) scroll = 0;                       // hold at start
  else if (p < hold + overflow) scroll = p - hold;         // scroll out
  else if (p < 2 * hold + overflow) scroll = overflow;     // hold at end
  else scroll = cyc - p;                                   // scroll back
  gfx_text_scroll(TITLE_X, TITLE_Y, s, len, TITLE_W, COL_TRACK_TITLE, scroll, 0);
}

static void render_drawer(void) {
  const album_t *al = cur_album();
  const track_t *t = cur_track_p();

  gfx_fill_rect(0, DRAWER_Y, SCREEN_W, DRAWER_H, COL_DRAWER_BG);
  gfx_hline(0, DRAWER_Y, SCREEN_W, COL_FOCUS);

  draw_cover_ph(10, DRAWER_Y + 10, 56, al->hue);

  // title + play state
  render_title(t);
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
  gfx_text_small(10, SCREEN_H - 14, "^v piste \xc2\xb7 <> +/-10s \xc2\xb7 A pause \xc2\xb7 B retour",
                 COL_ARTIST_DIM);
}

void ui_render_full(void) {
  gfx_clear(COL_BG);
  render_sidebar();
  render_main();
  if (drawer_open) render_drawer();
}

void ui_render_playing(void) {
  if (drawer_open) {
    render_title(cur_track_p());  // animate the marquee even while paused
    render_progress();
  }
}
