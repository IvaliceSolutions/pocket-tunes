// Host test for the firmware audio chain (mp3.c: streaming + Helix + resampler).
//
// Compiles mp3.c and Helix natively, mocking the target-command file engine
// (feeds bytes from a real .mp3) and the PCM fifo (captures pushed samples).
// Dumps 48 kHz s16le stereo to stdout for comparison against ffmpeg.
//
//   cc -DPT_HOST_TEST -I../src -I../helix/pub -I../helix/real \
//      host_mp3_test.c ../src/mp3.c ../helix/*.c ../helix/real/*.c \
//      -o host_mp3_test && ./host_mp3_test in.mp3 33062 > out.raw

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"

// ---- mock the target-command file engine (file.h interface) --------------
#define RX_SIZE 16384
uint8_t rx_ram[RX_SIZE];

static uint8_t *g_file;
static uint32_t g_file_size;

// mp3.c shares lib.c's staging buffer (not compiled here)
char lib_stage[12288];

int file_read(uint16_t id, uint32_t off, uint32_t len) {
  (void)id;
  if (len > RX_SIZE) len = RX_SIZE;
  for (uint32_t i = 0; i < len; i++)
    rx_ram[i] = (off + i < g_file_size) ? g_file[off + i] : 0;
  return 0;
}

// ---- M7e async-read emulation: synchronous behind the same API ----
static uint16_t as_id; static uint32_t as_off, as_len; static int as_busy;
int file_read_start(uint16_t id, uint32_t off, uint32_t len) {
  if (as_busy) return -1;
  as_id = id; as_off = off; as_len = len; as_busy = 1;
  return 0;
}
int file_read_busy(void) { return as_busy; }
int file_read_poll(void) {
  if (!as_busy) return -1;
  as_busy = 0;
  return file_read(as_id, as_off, as_len) ? -2 : FILE_POLL_DONE;
}

int file_open(uint16_t id, const char *path, int path_len) {
  (void)id; (void)path; (void)path_len; return 0;
}
uint32_t file_slot_size(uint16_t id) { (void)id; return g_file_size; }

// ---- mock the PCM fifo (mmio pcm_free/pcm_push) --------------------------
#define CAP_MAX (48000 * 30)
static int16_t cap_l[CAP_MAX], cap_r[CAP_MAX];
static int cap_n;
uint32_t pcm_free(void) { return (cap_n < CAP_MAX - 4) ? 4096 : 0; }
void pcm_push(uint32_t s) {
  if (cap_n < CAP_MAX) {
    cap_l[cap_n] = (int16_t)(s & 0xFFFF);
    cap_r[cap_n] = (int16_t)(s >> 16);
    cap_n++;
  }
}

#include "mp3.h"

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s file.mp3 size\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror("open"); return 2; }
  fseek(f, 0, SEEK_END);
  g_file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  g_file = malloc(g_file_size);
  if (fread(g_file, 1, g_file_size, f) != g_file_size) { perror("read"); return 2; }
  fclose(f);

  if (mp3_start("/test.mp3", 9, g_file_size) != 0) {
    fprintf(stderr, "mp3_start failed\n");
    return 1;
  }
  // pump until EOF (host: pcm_free never blocks, so one pump drains everything
  // per available input; loop until the decoder reports end of file)
  int guard = 0;
  while (!mp3_at_eof() && guard++ < 100000) mp3_pump();

  fprintf(stderr, "decoded %d samples (%.3f s @48k)\n", cap_n, cap_n / 48000.0);
  for (int i = 0; i < cap_n; i++) {
    fwrite(&cap_l[i], 2, 1, stdout);
    fwrite(&cap_r[i], 2, 1, stdout);
  }
  return 0;
}
