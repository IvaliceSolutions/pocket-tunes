// Hardware interface of the Pocket Tunes SoC (see core/target/pocket/pt_soc.sv)
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

#define LIBRARY_BASE ((volatile const char *)0x10000000)
#define FB_BASE      ((volatile uint8_t *)0x20000000)
#define FB_BASE32    ((volatile uint32_t *)0x20000000)

#define SCREEN_W 320
#define SCREEN_H 288

#define MMIO32(off) (*(volatile uint32_t *)(0xF0000000u + (off)))
#define REG_KEYS        MMIO32(0x00)
#define REG_FRAME       MMIO32(0x04)
#define REG_LIB_BYTES   MMIO32(0x08)
#define REG_VBLANK      MMIO32(0x0C)
#define REG_CYCLES      MMIO32(0x10)

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
