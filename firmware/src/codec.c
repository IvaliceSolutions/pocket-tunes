#include "codec.h"
#include "lib.h"
#include "mp3.h"
#include "opus_play.h"

enum { C_NONE, C_MP3, C_OPUS };
static int active = C_NONE;

int codec_start(uint8_t format, const char *path, int path_len, uint32_t file_size) {
  codec_stop();
  if (format == FMT_MP3) {
    active = C_MP3;
    return mp3_start(path, path_len, file_size);
  }
  if (format == FMT_OPUS || format == FMT_OGG) {
    active = C_OPUS;
    return opus_start(path, path_len, file_size);
  }
  active = C_NONE;
  return -21;  // unsupported format
}

void codec_stop(void) {
  if (active == C_MP3) mp3_stop();
  else if (active == C_OPUS) opus_stop();
  active = C_NONE;
}

void codec_set_paused(int paused) {
  if (active == C_MP3) mp3_set_paused(paused);
  else if (active == C_OPUS) opus_set_paused(paused);
}

int codec_at_eof(void) {
  if (active == C_MP3) return mp3_at_eof();
  if (active == C_OPUS) return opus_at_eof();
  return 0;
}

int codec_pump(void) {
  if (active == C_MP3) return mp3_pump();
  if (active == C_OPUS) return opus_pump();
  return 0;
}

uint32_t codec_pos_seconds(void) {
  if (active == C_MP3) return mp3_pos_seconds();
  if (active == C_OPUS) return opus_pos_seconds();
  return 0;
}

void codec_seek(uint32_t to_seconds, uint32_t byte_off) {
  if (active == C_MP3) mp3_seek(to_seconds, byte_off);
  else if (active == C_OPUS) opus_seek(to_seconds, byte_off);
}

int codec_is_seekable(void) { return active != C_NONE; }
