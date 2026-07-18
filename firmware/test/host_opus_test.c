// Host test for the firmware Opus chain (opus_play.c + ogg.c + libopus).
//
// Compiles opus_play.c, ogg.c and the decoder-only libopus natively, mocking
// the target-command file engine (feeds bytes from a real .opus) and the PCM
// fifo (captures pushed samples). Dumps 48 kHz s16le stereo to stdout for
// comparison against ffmpeg's decode of the same file. See run_opus.sh.

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

// opus_play.c shares lib.c's staging buffer (not compiled here)
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
// Pseudo-random free space (varies per CALL, so a stalled pump always
// unblocks): 0 forces mid-packet suspends, small values force partial
// flushes, larger ones let chunk reads through — the exact hardware
// conditions of the ogg suspend/resume path. Byte-exactness of the output
// against ffmpeg proves no sample is lost or duplicated across suspends.
uint32_t pcm_free(void) {
  static uint32_t fc;
  if (cap_n >= CAP_MAX - 4) return 0;
  fc = fc * 1664525u + 1013904223u;
  return (fc >> 20) & 0xFFF;  // 0..4095
}
void pcm_push(uint32_t s) {
  if (cap_n < CAP_MAX) {
    cap_l[cap_n] = (int16_t)(s & 0xFFFF);
    cap_r[cap_n] = (int16_t)(s >> 16);
    cap_n++;
  }
}

// opus_play.c (PT_HOST_TEST) expects the arena allocator from outside; on host
// we back it with libc malloc (the on-target bump arena is exercised natively
// by the firmware build, and its size is validated separately by the linker).
void *opus_arena_alloc(unsigned long n) { return malloc(n); }

#include "opus_play.h"

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s file.opus\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror("open"); return 2; }
  fseek(f, 0, SEEK_END);
  g_file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  g_file = malloc(g_file_size);
  if (fread(g_file, 1, g_file_size, f) != g_file_size) { perror("read"); return 2; }
  fclose(f);

  if (opus_start("/test.opus", 10, g_file_size) != 0) {
    fprintf(stderr, "opus_start failed\n");
    return 1;
  }
  int guard = 0;
  while (!opus_at_eof() && guard++ < 1000000) opus_pump();

  fprintf(stderr, "decoded %d samples (%.3f s @48k)\n", cap_n, cap_n / 48000.0);
  for (int i = 0; i < cap_n; i++) {
    fwrite(&cap_l[i], 2, 1, stdout);
    fwrite(&cap_r[i], 2, 1, stdout);
  }
  return 0;
}
