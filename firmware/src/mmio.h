// Hardware interface of the Pocket Tunes SoC (see core/target/pocket/pt_soc.sv)
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

// (the old always-resident library slot is gone — files stream through the
// 16 KB RX RAM at 0x10000000, see file.h)
#define FB_BASE      ((volatile uint8_t *)0x20000000)
#define FB_BASE32    ((volatile uint32_t *)0x20000000)

#define SCREEN_W 320
#define SCREEN_H 288

#define MMIO32(off) (*(volatile uint32_t *)(0xF0000000u + (off)))
#define REG_KEYS        MMIO32(0x00)
#define REG_FRAME       MMIO32(0x04)
#define REG_VBLANK      MMIO32(0x0C)
#define REG_CYCLES      MMIO32(0x10)
#define REG_RTC         MMIO32(0x14)  /* BCD 0x00HHMMSS (Pocket RTC) */

// target-command engine (see pt_soc.sv)
#define REG_TGT_ID         MMIO32(0x20)
#define REG_TGT_OFFSET     MMIO32(0x24)
#define REG_TGT_BRIDGEADDR MMIO32(0x28)
#define REG_TGT_LENGTH     MMIO32(0x2C)
#define REG_TGT_CMD        MMIO32(0x30)
#define REG_TGT_STATUS     MMIO32(0x34)
#define TGT_CMD_READ     1u
#define TGT_CMD_OPENFILE 2u
#define TGT_CMD_GETFILE  3u
#define TGT_ACK  1u
#define TGT_DONE 2u

// PCM fifo: write pushes {R,L} signed, read = free space in samples
#define REG_PCM         MMIO32(0x40)
#ifndef PT_HOST_TEST
static inline uint32_t pcm_free(void) { return REG_PCM; }
static inline void pcm_push(uint32_t s) { REG_PCM = s; }
#else
uint32_t pcm_free(void);
void pcm_push(uint32_t s);
#endif

// datatable window
#define REG_DT_ADDR     MMIO32(0x50)
#define REG_DT_DATA     MMIO32(0x54)

#define REG_PARAM_RAM   ((volatile uint32_t *)0xF0001000u)  // param struct base

// Pocket key bitmap (cont1_key)
#define KEY_UP     (1u << 0)
#define KEY_DOWN   (1u << 1)
#define KEY_LEFT   (1u << 2)
#define KEY_RIGHT  (1u << 3)
#define KEY_A      (1u << 4)
#define KEY_B      (1u << 5)
#define KEY_X      (1u << 6)
#define KEY_Y      (1u << 7)
#define KEY_L      (1u << 8)
#define KEY_R      (1u << 9)
#define KEY_SELECT (1u << 14)
#define KEY_START  (1u << 15)

static inline void wait_vblank(void) {
  while (REG_VBLANK) {}   // if already in vblank, wait for it to end
  while (!REG_VBLANK) {}  // then wait for the next one to start
}

#endif
