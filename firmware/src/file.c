#include "file.h"
#include "mmio.h"

// I/O latency stats (cycles): worst / last / count of completed reads
static uint32_t io_max_cyc, io_last_cyc, io_count;
void file_io_stats(uint32_t *max_cycles, uint32_t *last_cycles, uint32_t *count) {
  if (max_cycles) *max_cycles = io_max_cyc;
  if (last_cycles) *last_cycles = io_last_cyc;
  if (count) *count = io_count;
}

static void io_record(uint32_t t0) {
  uint32_t dt = REG_CYCLES - t0;
  io_last_cyc = dt;
  if (dt > io_max_cyc) io_max_cyc = dt;
  io_count++;
}

// ---- async read state ----
// as_busy: a read is in flight (stale done already cleared; only the real
// completion `done` is left to poll — that's the SD latency we hide).
static int as_busy;
static int as_stolen;   // engine taken by a blocking op while in flight
static uint32_t as_t0;

int file_read_busy(void) { return as_busy; }

// Fire one target command and wait for completion.
// Protocol: params first; 0→N edge on REG_TGT_CMD fires; done clears early in
// the bridge FSM then rises on completion; drop the level afterwards.
static int tgt_run(uint32_t cmd) {
  REG_TGT_CMD = cmd;
  while (REG_TGT_STATUS & TGT_DONE) {}   // wait for the FSM to clear stale done
  while (!(REG_TGT_STATUS & TGT_DONE)) {}
  uint32_t st = REG_TGT_STATUS;
  REG_TGT_CMD = 0;
  // the last data words can still be in flight in the bridge→RAM CDC fifo
  // when done arrives (done takes the fast path); let them land
  for (volatile int d = 0; d < 64; d++) {}
  return (int)((st >> 4) & 7);
}

// Complete (and discard) any in-flight async read so a blocking op can use
// the engine. The codec re-issues at the same offset on FILE_POLL_STOLEN.
// The stale done was already cleared in file_read_start, so this only waits
// out the real completion.
static void as_steal(void) {
  if (!as_busy) return;
  while (!(REG_TGT_STATUS & TGT_DONE)) {}
  REG_TGT_CMD = 0;
  for (volatile int d = 0; d < 64; d++) {}
  as_busy = 0;
  as_stolen = 1;
}

int file_read_start(uint16_t id, uint32_t off, uint32_t len) {
  if (as_busy) return -1;  // one at a time
  if (len > RX_SIZE) len = RX_SIZE;
  REG_TGT_ID = id;
  REG_TGT_OFFSET = off;
  REG_TGT_BRIDGEADDR = 0x10000000u;  // → RX RAM
  REG_TGT_LENGTH = len;
  as_t0 = REG_CYCLES;
  as_stolen = 0;
  REG_TGT_CMD = TGT_CMD_READ;
  // Wait out the stale `done` synchronously (a few bridge cycles — NOT the SD
  // latency). Doing this here is what makes the poll unambiguous: once we
  // return, a high `done` can only mean the read has actually completed.
  // Without it, the poll (which runs after other main-loop work, by which
  // time the read may already be done) would see the stale done, think the
  // read hadn't started, and wait for it to clear forever → deadlock.
  while (REG_TGT_STATUS & TGT_DONE) {}
  as_busy = 1;
  return 0;
}

int file_read_poll(void) {
  if (as_stolen) { as_stolen = 0; return FILE_POLL_STOLEN; }
  if (!as_busy) return -1;  // nothing started
  uint32_t st = REG_TGT_STATUS;
  if (!(st & TGT_DONE)) return FILE_POLL_BUSY;  // SD read still running
  REG_TGT_CMD = 0;
  for (volatile int d = 0; d < 64; d++) {}  // let the CDC fifo drain
  as_busy = 0;
  io_record(as_t0);
  int err = (int)((st >> 4) & 7);
  return err ? -err : FILE_POLL_DONE;
}

int file_read(uint16_t id, uint32_t off, uint32_t len) {
  as_steal();  // a blocking op owns the engine; codec re-issues later
  if (len > RX_SIZE) len = RX_SIZE;
  REG_TGT_ID = id;
  REG_TGT_OFFSET = off;
  REG_TGT_BRIDGEADDR = 0x10000000u;  // → RX RAM
  REG_TGT_LENGTH = len;
  uint32_t t0 = REG_CYCLES;
  int e = tgt_run(TGT_CMD_READ);
  io_record(t0);
  return e;
}

int file_open(uint16_t id, const char *path, int path_len) {
  as_steal();  // blocking op: complete/discard any in-flight async read
  // param struct: bytes 0-255 zero-padded null-terminated path,
  // +0x100 flags (0 = open existing), +0x104 size (unused)
  volatile uint32_t *p = REG_PARAM_RAM;
  for (int w = 0; w < 64; w++) {
    uint32_t v = 0;
    for (int b = 0; b < 4; b++) {
      int i = w * 4 + b;
      uint8_t ch = (i < path_len) ? (uint8_t)path[i] : 0;
      v |= (uint32_t)ch << (8 * (3 - b));  // big-endian: byte 0 in MSB
    }
    p[w] = v;
  }
  p[64] = 0;  // flags
  p[65] = 0;  // size

  REG_TGT_ID = id;
  return tgt_run(TGT_CMD_OPENFILE);
}

// Read one datatable word, waiting out the clock-domain crossing: re-read
// until two consecutive reads agree.
static uint32_t dt_word(uint32_t addr) {
  REG_DT_ADDR = addr;
  uint32_t a, b;
  do {
    for (volatile int d = 0; d < 24; d++) {}
    a = REG_DT_DATA;
    for (volatile int d = 0; d < 24; d++) {}
    b = REG_DT_DATA;
  } while (a != b);
  return a;
}

uint32_t file_slot_size(uint16_t id) {
  // scan the datatable (word 2i = slot id, 2i+1 = size). Empty entries read
  // as id 0 with size 0, so a zero-size "match" keeps scanning.
  for (uint32_t i = 0; i < 16; i++) {
    if ((dt_word(i * 2) & 0xFFFF) == id) {
      uint32_t size = dt_word(i * 2 + 1);
      if (size != 0) return size;
    }
  }
  return 0;
}
