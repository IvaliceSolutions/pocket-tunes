// APF target-command file access: read chunks of data-slot files into the
// 16 KB bridge RX RAM, open arbitrary files by path into a slot.
#ifndef FILE_H
#define FILE_H

#include <stdint.h>

#ifdef PT_HOST_TEST
extern uint8_t rx_ram[];
#define RX_BASE  ((const uint32_t *)rx_ram)
#define RX_BYTES ((const uint8_t *)rx_ram)
#define RX_SIZE 16384
#else
#define RX_BASE ((volatile const uint32_t *)0x10000000)
#define RX_BYTES ((volatile const uint8_t *)0x10000000)
#define RX_SIZE 16384
#endif

#define SLOT_LIBRARY 0

// Read `len` bytes (≤ RX_SIZE) of slot `id` at byte offset `off` into the RX
// RAM. Returns 0 on success, else the APF error code (1..7).
int file_read(uint16_t id, uint32_t off, uint32_t len);

// Open the file at `path` (absolute on the SD card) into slot `id`.
// Subsequent file_read(id, ...) stream from it. Returns 0 on success.
int file_open(uint16_t id, const char *path, int path_len);

// Size registered for a slot in the APF datatable (0 if empty slot).
uint32_t file_slot_size(uint16_t id);

#endif
