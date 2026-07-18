// One playback interface over the two decoders (Helix MP3 / libopus).
//
// ui.c and main.c talk only to this; the dispatch on track format lives in
// codec.c. The two decoders share one arena (only one is ever live), so
// switching tracks between codecs is what hands the memory over.
#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

// Start `path` (SD absolute) using the decoder for `format` (lib.h FMT_*).
// Returns 0 on success, <0 on error (same codes as the underlying decoder,
// plus -21 for a format we can't play).
int codec_start(uint8_t format, const char *path, int path_len, uint32_t file_size);
void codec_stop(void);
void codec_set_paused(int paused);
int codec_at_eof(void);
int codec_pump(void);              // call often; decodes while the fifo has room
uint32_t codec_pos_seconds(void);
void codec_seek(uint32_t to_seconds, uint32_t byte_off);

// PCM fifo underruns since boot (audible gaps) — debug HUD.
extern uint32_t codec_underrun_count;

#endif
