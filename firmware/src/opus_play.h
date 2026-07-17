// Opus playback: streams a .opus file through the target-command engine,
// demuxes Ogg, decodes with libopus (fixed-point) and feeds the PCM fifo.
//
// Same shape as mp3.h so ui.c can drive either codec through one interface.
// Opus decodes natively at 48 kHz — no resampler in this path.
#ifndef OPUS_PLAY_H
#define OPUS_PLAY_H

#include <stdint.h>

int opus_start(const char *path, int path_len, uint32_t file_size);
void opus_stop(void);
void opus_set_paused(int paused);
int opus_at_eof(void);
int opus_pump(void);
uint32_t opus_pos_seconds(void);
void opus_seek(uint32_t to_seconds, uint32_t byte_off);

#endif
