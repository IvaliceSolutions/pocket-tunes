// Pocket Tunes browser UI — the amber-terminal design handoff
// (design_handoff_pocket_tunes_1b) at 320x288. Three explicit navigation
// levels — Artists (sidebar) → Albums → Tracks — with a now-playing drawer
// that overlays the track list. Playback is independent of the browse cursor:
// music keeps running while you browse, so "where am I" and "what's playing"
// never conflate. The currently-selected item at every level scrolls its title
// with a bounce (marquee) when it's too long to fit.

#include "ui.h"
#include "mmio.h"
#include "gfx.h"
#include "lib.h"
#include "palette.h"
#include "mp3.h"

// ----------------------------------------------------------------- layout
#define SIDEBAR_W 95
#define SB_X 8
#define SB_ROW_H 17
#define SB_ROWS 16
#define SB_Y0 6
#define SB_TXT_W (SIDEBAR_W - 4 - SB_X)

#define MAIN_X (SIDEBAR_W + 5)
#define MAIN_X_MAX (SCREEN_W - 4)

#define AL_ROW_H 38
#define AL_ROWS 6
#define AL_Y0 24
#define AL_TXT_X (MAIN_X + 40)
#define AL_TXT_W (MAIN_X_MAX - AL_TXT_X)

#define TR_ROW_H 22
#define TR_ROWS 11
#define TR_Y0 24
#define TR_TXT_X (MAIN_X + 18)
#define TR_TXT_W (MAIN_X_MAX - TR_TXT_X - 10)  // 10px kept for the ▶ marker

#define DRAWER_H 184
#define DRAWER_Y (SCREEN_H - DRAWER_H)
#define TITLE_X 76
#define TITLE_Y (DRAWER_Y + 12)
#define TITLE_W 174

// ------------------------------------------------------------------ state
enum { FOC_ARTISTS, FOC_ALBUMS, FOC_TRACKS };
static int focus;
static int sb_cursor, sb_scroll;          // artists (sidebar)
static int cur_artist;                     // artist whose albums are shown
static int album_cursor, album_scroll;     // albums level cursor
static int track_cursor, track_scroll;     // tracks level cursor
// playback state — independent of the browse cursor above
static int play_gidx = -1;                 // global track index loaded (-1 none)
static int play_alb = -1;                  // global album index of that track
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
static const album_t *sel_album(void) {  // album the tracks level is showing
  return &lib_albums[sel_artist()->first_album + album_cursor];
}
static const album_t *play_album_p(void) { return &lib_albums[play_alb]; }
static const track_t *play_track_p(void) { return &lib_tracks[play_gidx]; }

// Estimate the file byte offset for a given time (CBR: file-size ratio, else
// nominal bitrate). Used by seek (FF/RW).
static uint32_t seek_byte_off(const track_t *t, uint32_t sec) {
  if (t->dur_s) return (uint32_t)((uint64_t)t->fsize * sec / t->dur_s);
  if (t->kbps) return sec * (t->kbps * 1000u / 8u);
  return 0;
}

// ---- shuffle: toggled with START. A tiny xorshift PRNG stirred by the
// free-running cycle counter (unpredictable exactly when a track ends).
static int shuffle;
static uint32_t rng_state = 0x2545F491u;
static uint32_t rng_next(void) {
  uint32_t x = rng_state ^ REG_CYCLES;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rng_state = x ? x : 0x2545F491u;
  return rng_state;
}
// Next track index in the album: random when shuffle is on, else sequential.
// Both wrap; shuffle avoids immediately repeating the current track.
static int next_rel(const album_t *pa, int cur_rel) {
  int n = pa->n_tracks;
  if (n <= 1) return 0;
  if (!shuffle) return (cur_rel + 1) % n;
  int r = (int)(rng_next() % (uint32_t)n);
  if (r == cur_rel) r = (r + 1) % n;
  return r;
}

// Load and start playing lib_tracks[gidx] (in album `alb`).
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

// ---- marquee: draw text in [x, x+box_w). If `animate` and it overflows,
// scroll it left/right with a bounce (only the selected item animates).
static void draw_marquee(int x, int y, int box_w, const char *s, int len,
                         uint8_t color, int small, int animate) {
  int tw = gfx_text_px_len(s, len, small);
  if (tw <= box_w || !animate) {
    if (small) gfx_textn_small(x, y, s, len, x + box_w, color);
    else gfx_textn(x, y, s, len, x + box_w, color);
    return;
  }
  int overflow = tw - box_w;
  int hold = 30;                               // pause units at each end
  int cyc = 2 * hold + 2 * overflow;
  int p = (int)((anim >> 1) % (uint32_t)cyc);  // >>1 → ~30 px/s
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
  gfx_rect(x, y, size, size, COL_DIVIDER);
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
  anim = 0;  // any key press restarts the selected item's marquee

  if (pressed & KEY_START) { shuffle = !shuffle; return 1; }  // toggle shuffle

  if (drawer_open) {
    if (pressed & (KEY_UP | KEY_DOWN)) {  // prev/next track (wraps), auto-play
      const album_t *pa = play_album_p();
      int cur_rel = play_gidx - pa->first_track;
      int rel = (pressed & KEY_DOWN) ? next_rel(pa, cur_rel)          // shuffle-aware
                                     : (cur_rel + pa->n_tracks - 1) % pa->n_tracks;
      start_play(pa->first_track + rel, play_alb);
      track_cursor = rel;  // keep the track list in sync behind the drawer
      track_scroll = clamp_scroll(track_cursor, track_scroll, TR_ROWS);
      return 1;
    }
    if (pressed & (KEY_LEFT | KEY_RIGHT)) {  // FF / rewind ±10 s
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

  if (focus == FOC_ARTISTS) {
    if (pressed & (KEY_UP | KEY_DOWN)) {
      sb_cursor += (pressed & KEY_DOWN) ? 1 : lib_n_artists - 1;
      sb_cursor %= lib_n_artists;
      sb_scroll = clamp_scroll(sb_cursor, sb_scroll, SB_ROWS);
      return 1;
    }
    if (pressed & KEY_A) {  // → albums
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
      album_scroll = clamp_scroll(album_cursor, album_scroll, AL_ROWS);
      return 1;
    }
    if (pressed & KEY_DOWN) {
      if (album_cursor < sel_artist()->n_albums - 1) album_cursor++;
      album_scroll = clamp_scroll(album_cursor, album_scroll, AL_ROWS);
      return 1;
    }
    if (pressed & KEY_A) {  // → tracks (does NOT start playback yet)
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
    track_scroll = clamp_scroll(track_cursor, track_scroll, TR_ROWS);
    return 1;
  }
  if (pressed & KEY_DOWN) {
    if (track_cursor < sel_album()->n_tracks - 1) track_cursor++;
    track_scroll = clamp_scroll(track_cursor, track_scroll, TR_ROWS);
    return 1;
  }
  if (pressed & KEY_A) {  // open drawer for the selected track
    int gidx = sel_album()->first_track + track_cursor;
    int alb = sel_artist()->first_album + album_cursor;
    if (gidx != play_gidx)  // same track already loaded → resume, don't restart
      start_play(gidx, alb);
    drawer_open = 1;
    return 1;
  }
  if (pressed & KEY_B) { focus = FOC_ALBUMS; return 1; }
  return 0;
}

int ui_tick(void) {
  anim++;  // drives the marquee at every level
  if (playing) {
    pos_frames = mp3_pos_seconds() * 60u;  // progress from real samples
    if (mp3_at_eof()) {  // track ended → next in the album (shuffle-aware, wraps)
      const album_t *pa = play_album_p();
      int rel = next_rel(pa, play_gidx - pa->first_track);
      start_play(pa->first_track + rel, play_alb);
      if (drawer_open || focus == FOC_TRACKS) {
        track_cursor = rel;
        track_scroll = clamp_scroll(track_cursor, track_scroll, TR_ROWS);
      }
      return 1;
    }
  }
  return 0;
}

// ----------------------------------------------------------------- render
static void level_footer(uint32_t cur, uint32_t total, const char *noun) {
  char f[48], *p = f;
  p = s_append(p, "NIVEAU : ");
  p = s_udec(p, cur);
  *p++ = '/';
  p = s_udec(p, total);
  *p++ = ' ';
  p = s_append(p, noun);
  int x = gfx_text_small(MAIN_X, SCREEN_H - 12, f, COL_ARTIST_DIM);
  if (shuffle) gfx_text_small(x + 8, SCREEN_H - 12, "\xc2\xb7 ALEA", COL_FOCUS);
}

static void render_sidebar(void) {
  gfx_vline(SIDEBAR_W, 0, SCREEN_H, COL_DIVIDER);
  for (int i = 0; i < SB_ROWS; i++) {
    int idx = sb_scroll + i;
    if (idx >= lib_n_artists) break;
    int y = SB_Y0 + i * SB_ROW_H;
    int sel = (idx == sb_cursor && focus == FOC_ARTISTS && !drawer_open);
    if (sel) gfx_fill_rect(0, y - 2, SIDEBAR_W, SB_ROW_H, COL_CURSOR_BG);
    uint8_t col = (idx == cur_artist) ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM;
    const artist_t *a = &lib_artists[idx];
    draw_marquee(SB_X, y, SB_TXT_W, lib_str(a->name), a->name.len, col, 0, sel);
  }
}

static void render_albums(void) {
  const artist_t *ar = sel_artist();
  int x = gfx_textn_small(MAIN_X, 8, lib_str(ar->name), ar->name.len, MAIN_X_MAX - 6, COL_PATH);
  gfx_text_small(x, 8, "/", COL_PATH);

  for (int i = 0; i < AL_ROWS; i++) {
    int idx = album_scroll + i;
    if (idx >= ar->n_albums) break;
    const album_t *al = &lib_albums[ar->first_album + idx];
    int y = AL_Y0 + i * AL_ROW_H;
    int sel = (idx == album_cursor && focus == FOC_ALBUMS && !drawer_open);
    if (sel) gfx_rect(MAIN_X - 3, y - 2, MAIN_X_MAX - MAIN_X + 6, AL_ROW_H - 2, COL_FOCUS);
    draw_cover_ph(MAIN_X, y + 1, 32, al->hue);
    draw_marquee(AL_TXT_X, y + 3, AL_TXT_W, lib_str(al->title), al->title.len, COL_TITLE, 0, sel);

    char meta[48], *p = meta;
    if (al->year) p = s_udec(p, al->year);
    if (al->year && al->genre.len) p = s_append(p, " \xc2\xb7 ");
    *p = 0;
    int mx = gfx_text_small(AL_TXT_X, y + 20, meta, COL_ARTIST_DIM);
    if (al->genre.len)
      gfx_textn_small(mx, y + 20, lib_str(al->genre), al->genre.len, MAIN_X_MAX, COL_ARTIST_DIM);
  }
}

static void render_tracks(void) {
  const artist_t *ar = sel_artist();
  const album_t *al = sel_album();
  // breadcrumb "{Artist}/{Album}/"
  int x = gfx_textn_small(MAIN_X, 8, lib_str(ar->name), ar->name.len, MAIN_X_MAX - 40, COL_PATH);
  x = gfx_text_small(x, 8, "/", COL_PATH);
  x = gfx_textn_small(x, 8, lib_str(al->title), al->title.len, MAIN_X_MAX - 6, COL_PATH);
  gfx_text_small(x, 8, "/", COL_PATH);

  for (int i = 0; i < TR_ROWS; i++) {
    int idx = track_scroll + i;
    if (idx >= al->n_tracks) break;
    const track_t *t = &lib_tracks[al->first_track + idx];
    int y = TR_Y0 + i * TR_ROW_H;
    int sel = (idx == track_cursor && focus == FOC_TRACKS && !drawer_open);
    if (sel) gfx_rect(MAIN_X - 3, y - 2, MAIN_X_MAX - MAIN_X + 6, TR_ROW_H - 2, COL_FOCUS);

    char num[12];
    u32_to_dec((uint32_t)idx + 1, num);
    gfx_text_small(MAIN_X, y + 2, num, COL_ARTIST_DIM);

    draw_marquee(TR_TXT_X, y, TR_TXT_W, lib_str(t->title), t->title.len, COL_TITLE, 0, sel);

    char m[24], *p = m;
    if (t->dur_s) { p = s_time(p, t->dur_s); p = s_append(p, " \xc2\xb7 "); }
    s_append(p, lib_format_name(t->format));
    gfx_text_small(TR_TXT_X, y + 12, m, COL_ARTIST_DIM);

    if (al->first_track + idx == play_gidx)  // currently loaded track marker
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

static void render_progress(void) {
  const track_t *t = play_track_p();
  int bar_x = 10, bar_w = SCREEN_W - 20, bar_y = DRAWER_Y + 74;
  gfx_fill_rect(bar_x, bar_y, bar_w, 5, COL_PROGRESS_BG);
  if (t->dur_s) {
    uint32_t sec = pos_frames / 60;
    uint32_t w = (uint32_t)bar_w * sec / t->dur_s;
    if (w > (uint32_t)bar_w) w = bar_w;
    gfx_fill_rect(bar_x, bar_y, (int)w, 5, COL_FOCUS);
  }
}

static void render_title(void) {
  const track_t *t = play_track_p();
  gfx_fill_rect(TITLE_X, TITLE_Y, TITLE_W, 13, COL_DRAWER_BG);
  draw_marquee(TITLE_X, TITLE_Y, TITLE_W, lib_str(t->title), t->title.len, COL_TRACK_TITLE, 0, 1);
}

static void render_drawer(void) {
  const album_t *al = play_album_p();
  const track_t *t = play_track_p();

  gfx_fill_rect(0, DRAWER_Y, SCREEN_W, DRAWER_H, COL_DRAWER_BG);
  gfx_hline(0, DRAWER_Y, SCREEN_W, COL_FOCUS);

  draw_cover_ph(10, DRAWER_Y + 10, 56, al->hue);

  render_title();
  if (!playing && last_start_err) {
    char e[16], *q = e; q = s_append(q, "ERR "); s_udec(q, (uint32_t)(-last_start_err));
    gfx_text_small(SCREEN_W - 10 - 7 * 5, DRAWER_Y + 14, e, COL_WHITE);
  } else {
    gfx_text_small(SCREEN_W - 10 - 9 * 5, DRAWER_Y + 14,
                   playing ? "> LECTURE" : "|| PAUSE", COL_PLAYSTATE);
  }

  // "{album} · piste n/total"
  int sx = gfx_textn_small(76, DRAWER_Y + 32, lib_str(al->title), al->title.len, 200, COL_ARTIST_DIM);
  char sub[32], *p = sub;
  p = s_append(p, " \xc2\xb7 piste ");
  p = s_udec(p, (uint32_t)(play_gidx - al->first_track) + 1);
  *p++ = '/';
  p = s_udec(p, al->n_tracks);
  gfx_text_small(sx, DRAWER_Y + 32, sub, COL_ARTIST_DIM);

  if (shuffle) gfx_text_small(76, DRAWER_Y + 50, "ALEA", COL_FOCUS);

  render_progress();

  char l1[64], *q = l1;
  if (t->dur_s) { q = s_time(q, t->dur_s); q = s_append(q, " total"); }
  int lx = gfx_text_small(10, DRAWER_Y + 90, l1, COL_ARTIST_DIM);
  if (al->genre.len)
    lx = gfx_textn_small(lx, DRAWER_Y + 90, lib_str(al->genre), al->genre.len, 260, COL_ARTIST_DIM);
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

  gfx_text_small(10, SCREEN_H - 14, "^v piste \xc2\xb7 <> 10s \xc2\xb7 A pause \xc2\xb7 START alea \xc2\xb7 B fermer",
                 COL_ARTIST_DIM);
}

void ui_render_full(void) {
  gfx_clear(COL_BG);
  render_sidebar();
  render_main();
  if (drawer_open) render_drawer();
}

// Redraw just the currently-selected item's title so its marquee animates,
// without repainting (and flickering) the whole screen every frame.
static void animate_selection(void) {
  if (focus == FOC_ARTISTS) {
    if (sb_cursor < sb_scroll || sb_cursor >= sb_scroll + SB_ROWS) return;
    int y = SB_Y0 + (sb_cursor - sb_scroll) * SB_ROW_H;
    const artist_t *a = &lib_artists[sb_cursor];
    uint8_t col = (sb_cursor == cur_artist) ? COL_ARTIST_ACTIVE : COL_ARTIST_DIM;
    gfx_fill_rect(0, y - 2, SIDEBAR_W, SB_ROW_H, COL_CURSOR_BG);
    draw_marquee(SB_X, y, SB_TXT_W, lib_str(a->name), a->name.len, col, 0, 1);
  } else if (focus == FOC_ALBUMS) {
    const artist_t *ar = sel_artist();
    if (album_cursor < album_scroll || album_cursor >= album_scroll + AL_ROWS) return;
    int y = AL_Y0 + (album_cursor - album_scroll) * AL_ROW_H;
    const album_t *al = &lib_albums[ar->first_album + album_cursor];
    gfx_fill_rect(AL_TXT_X, y + 3, AL_TXT_W, 13, COL_BG);
    draw_marquee(AL_TXT_X, y + 3, AL_TXT_W, lib_str(al->title), al->title.len, COL_TITLE, 0, 1);
  } else {  // FOC_TRACKS
    const album_t *al = sel_album();
    if (track_cursor < track_scroll || track_cursor >= track_scroll + TR_ROWS) return;
    int y = TR_Y0 + (track_cursor - track_scroll) * TR_ROW_H;
    const track_t *t = &lib_tracks[al->first_track + track_cursor];
    gfx_fill_rect(TR_TXT_X, y, TR_TXT_W, 13, COL_BG);
    draw_marquee(TR_TXT_X, y, TR_TXT_W, lib_str(t->title), t->title.len, COL_TITLE, 0, 1);
  }
}

void ui_render_playing(void) {
  if (drawer_open) {
    render_title();      // animate the drawer title marquee (even while paused)
    render_progress();
  } else {
    animate_selection();  // animate the selected browse item's marquee
  }
}
