// MP3 playback engine: streams a file through the target-command engine,
// decodes with Helix, resamples to 48 kHz and feeds the PCM fifo.
#ifndef MP3_H
#define MP3_H

#include <stdint.h>

// Open `path` (SD absolute) and start playing. Returns 0 on success.
int mp3_start(const char *path, int path_len, uint32_t file_size);
void mp3_stop(void);
void mp3_set_paused(int paused);
int mp3_at_eof(void);       // decode finished and fifo drained

// Call often (each main-loop iteration): decodes as long as the PCM fifo
// has room. Returns nonzero while actively decoding.
int mp3_pump(void);

// Playback position in seconds (from samples pushed at 48 kHz).
uint32_t mp3_pos_seconds(void);

// Seek to `to_seconds` (also becomes the reported position) by repositioning
// the file read to `byte_off`. The decoder resyncs at the next frame sync word.
void mp3_seek(uint32_t to_seconds, uint32_t byte_off);

#endif
