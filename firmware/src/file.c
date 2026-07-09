#include "file.h"
#include "mmio.h"

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

int file_read(uint16_t id, uint32_t off, uint32_t len) {
  if (len > RX_SIZE) len = RX_SIZE;
  REG_TGT_ID = id;
  REG_TGT_OFFSET = off;
  REG_TGT_BRIDGEADDR = 0x10000000u;  // → RX RAM
  REG_TGT_LENGTH = len;
  return tgt_run(TGT_CMD_READ);
}

int file_open(uint16_t id, const char *path, int path_len) {
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
