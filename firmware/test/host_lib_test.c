// Host validation of the library.json streaming parser (lib.c) and the
// chapter reader (chap.c) against a REAL indexer v2 output, plus a
// handcrafted v1 file for backward compatibility.
//
// Usage: host_lib_test <library.json>
// The file layer is stubbed: file_read copies from the in-memory file into a
// fake RX RAM, exactly the window discipline the hardware has.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib.h"
#include "chap.h"

// ---- file.h stub ----
uint8_t rx_ram[16384];
static const uint8_t *g_file;
static uint32_t g_size;

int file_read(uint16_t id, uint32_t off, uint32_t len) {
  (void)id;
  if (len > sizeof rx_ram || off > g_size || off + len > g_size) return 1;
  memcpy(rx_ram, g_file + off, len);
  return 0;
}
int file_open(uint16_t id, const char *path, int path_len) {
  (void)id; (void)path; (void)path_len;
  return 0;
}
uint32_t file_slot_size(uint16_t id) { (void)id; return g_size; }

static int fails;
#define CHECK(cond, ...) do { \
    if (!(cond)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
  } while (0)

static const artist_t *artist_named(const char *n) {
  for (int i = 0; i < lib_n_artists; i++)
    if (!strcmp(lib_str(lib_artists[i].name), n)) return &lib_artists[i];
  return 0;
}

static void use_buffer(const char *buf, uint32_t n) {
  g_file = (const uint8_t *)buf;
  g_size = n;
}

// ---------------------------------------------------------------- v2 file
static void test_v2(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fails++; printf("FAIL: cannot open %s\n", path); return; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n);
  fread(buf, 1, (size_t)n, f);
  fclose(f);
  use_buffer(buf, (uint32_t)n);

  CHECK(lib_parse() == 0, "v2 lib_parse");
  printf("v2: %d artists, %d albums, %d tracks, %d root tracks\n",
         lib_n_artists, lib_n_albums, lib_n_tracks, lib_n_root_tracks);
  CHECK(lib_n_artists == 3, "3 artists (got %d)", lib_n_artists);
  CHECK(lib_n_albums == 3, "3 albums (got %d)", lib_n_albums);
  CHECK(lib_n_tracks == 9, "9 tracks (got %d)", lib_n_tracks);
  CHECK(lib_n_root_tracks == 2, "2 root tracks (got %d)", lib_n_root_tracks);

  // Artist with only loose tracks: no Albums screen.
  const artist_t *deux = artist_named("Artiste Deux");
  CHECK(deux, "Artiste Deux present");
  if (deux) {
    CHECK(deux->n_albums == 0, "Deux: 0 albums");
    CHECK(deux->n_rtracks == 2, "Deux: 2 loose (got %d)", deux->n_rtracks);
    const track_t *t = &lib_tracks[deux->first_rtrack];
    CHECK(!strcmp(lib_str(t->title), "Single Alpha"), "Deux loose[0] title");
    CHECK(t->format == FMT_MP3, "Deux loose[0] MP3");
  }

  // Mixed artist: albums AND loose tracks.
  const artist_t *trois = artist_named("Artiste Trois");
  CHECK(trois, "Artiste Trois present");
  if (trois) {
    CHECK(trois->n_albums == 1 && trois->n_rtracks == 1, "Trois: 1 album + 1 loose");
    const album_t *al = &lib_albums[trois->first_album];
    CHECK(!strcmp(lib_str(al->title), "Album C"), "Trois album title");
    CHECK(al->year == 2020, "Trois album year");
    CHECK(!strcmp(lib_str(lib_tracks[trois->first_rtrack].title), "En vrac"),
          "Trois loose title");
  }

  // Normal artist.
  const artist_t *un = artist_named("Artiste Un");
  CHECK(un && un->n_albums == 2 && un->n_rtracks == 0, "Un: 2 albums, 0 loose");

  // Library-root loose tracks: an opus file and the chaptered audiobook.
  const track_t *rt = &lib_tracks[lib_root_first_track];
  CHECK(!strcmp(lib_str(rt[0].title), "Discours"), "root[0] = Discours");
  CHECK(rt[0].format == FMT_OPUS, "root[0] OPUS");
  CHECK(rt[0].chap_off == 0, "root[0] no chapters");
  CHECK(!strcmp(lib_str(rt[1].title), "Test Audiobook"), "root[1] = audiobook");
  CHECK(rt[1].chap_off != 0, "root[1] has chapters");

  // Paths resolve.
  char pbuf[288];
  int pl = lib_fetch_path(&rt[1], pbuf, sizeof pbuf);
  CHECK(pl > 0 && !strcmp(pbuf, "/Music/Livre audio.mp3"), "audiobook path (got %s)", pbuf);

  // Chapters: cached starts + on-demand titles.
  CHECK(chap_load(&rt[1]) == 3, "chap_load = 3 (got %d)", chap_count);
  CHECK(chap_start_s[0] == 0 && chap_start_s[1] == 4 && chap_start_s[2] == 8,
        "chapter starts 0/4/8 (got %u/%u/%u)",
        chap_start_s[0], chap_start_s[1], chap_start_s[2]);
  char title[64];
  chap_title(&rt[1], 1, title, sizeof title);
  CHECK(!strcmp(title, "Chapitre 2 - Le milieu"), "chap 1 title (got '%s')", title);
  chap_title(&rt[1], 2, title, sizeof title);
  CHECK(!strcmp(title, "Chapitre 3 - La fin"), "chap 2 title (got '%s')", title);
  CHECK(chap_index_for(0) == 0 && chap_index_for(5) == 1 && chap_index_for(100) == 2,
        "chap_index_for");
  CHECK(chap_load(&rt[0]) == 0, "no-chapter track: chap_load = 0");

  free(buf);
}

// ---------------------------------------------------------------- v1 file
// Old schema: no rootTracks, synthetic "(unknown)"/"(singles)" buckets,
// "chapters" always present (sometimes empty). Must still parse.
static const char V1[] =
  "{\"schemaVersion\":1,\"root\":\"/Music\","
  "\"counts\":{\"artists\":1,\"albums\":1,\"tracks\":2},"
  "\"artists\":[{\"id\":0,\"name\":\"(unknown)\",\"albums\":[{\"id\":0,"
  "\"title\":\"(singles)\",\"year\":null,\"genre\":null,\"hue\":12,"
  "\"coverArt\":null,\"tracks\":["
  "{\"index\":0,\"title\":\"Vieille piste\",\"durationMs\":1000,"
  "\"format\":\"MP3\",\"bitrateKbps\":64,\"fileSize\":9000,\"chapters\":[],"
  "\"path\":\"/Music/old.mp3\"},"
  "{\"index\":1,\"title\":\"Avec chapitres\",\"durationMs\":9000,"
  "\"format\":\"MP3\",\"fileSize\":9000,"
  "\"chapters\":[{\"s\":0,\"t\":\"Intro\"},{\"s\":7,\"t\":\"Suite\"}],"
  "\"path\":\"/Music/old2.mp3\"}]}]}]}";

static void test_v1(void) {
  use_buffer(V1, (uint32_t)(sizeof V1 - 1));
  CHECK(lib_parse() == 0, "v1 lib_parse");
  CHECK(lib_n_artists == 1 && lib_n_albums == 1 && lib_n_tracks == 2, "v1 counts");
  CHECK(lib_n_root_tracks == 0, "v1: no root tracks");
  CHECK(lib_tracks[0].chap_off == 0, "v1: empty chapters → none");
  CHECK(lib_tracks[1].chap_off != 0, "v1: chapters captured");
  CHECK(chap_load(&lib_tracks[1]) == 2, "v1 chap_load = 2");
  char title[32];
  chap_title(&lib_tracks[1], 0, title, sizeof title);
  CHECK(!strcmp(title, "Intro"), "v1 chap 0 title (got '%s')", title);
  CHECK(chap_start_s[1] == 7, "v1 chap 1 start");
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <library.json>\n", argv[0]); return 2; }
  test_v2(argv[1]);
  test_v1();
  if (fails) { printf("RESULT: FAIL (%d)\n", fails); return 1; }
  printf("RESULT: PASS\n");
  return 0;
}
