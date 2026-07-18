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

// ---- async read (M7e): SD reads on real hardware can take tens of ms (FAT
// chain walks deep into multi-hundred-MB files) while the PCM fifo only holds
// 85 ms — a blocking read mid-playback is an audible dropout. The codecs
// start a read, keep decoding from their buffered input, and poll.
//
// Contract: only ONE async read at a time, and any BLOCKING file op (path /
// art / chapter fetches) may STEAL the engine: it completes the in-flight
// read, discards the data, runs, and file_read_poll then reports
// FILE_POLL_STOLEN. The codec must only advance its file offset when poll
// says done — a stolen read is simply re-issued at the same offset, so no
// byte is ever lost.
#define FILE_POLL_BUSY 0
#define FILE_POLL_DONE 1
#define FILE_POLL_STOLEN (-100)
int file_read_start(uint16_t id, uint32_t off, uint32_t len);  // 0 = started
int file_read_poll(void);  // DONE / BUSY / STOLEN / <0 = APF error (idle)
int file_read_busy(void);  // 1 while a started read is in flight

// I/O latency instrumentation (cycles via REG_CYCLES): worst and last
// complete-read duration since boot, and total reads. For the debug HUD.
void file_io_stats(uint32_t *max_cycles, uint32_t *last_cycles, uint32_t *count);

// Open the file at `path` (absolute on the SD card) into slot `id`.
// Subsequent file_read(id, ...) stream from it. Returns 0 on success.
int file_open(uint16_t id, const char *path, int path_len);

// Size registered for a slot in the APF datatable (0 if empty slot).
uint32_t file_slot_size(uint16_t id);

#endif
