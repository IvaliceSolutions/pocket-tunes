// Minimal streaming Ogg demuxer for .opus files (M7b).
//
// Feed it file bytes in order; it emits the logical packets (Opus frames).
// Handles page boundaries, packet continuation (255-lacing), and skips the
// OpusHead/OpusTags header packets, exposing the pre-skip sample count.
// Single logical stream only (what opusenc/ffmpeg produce for .opus).
#ifndef OGG_H
#define OGG_H

#include <stdint.h>

#define OGG_MAX_PACKET 2048  // largest audio packet we accept (music ~500 B)

typedef struct {
  // packet assembly
  uint8_t  pkt[OGG_MAX_PACKET];
  uint16_t pkt_len;
  uint8_t  pkt_cont;     // current packet continues into the next page
  uint8_t  suspended;    // a finished packet is held, emit refused it
  // page walk
  uint8_t  seg_table[255];
  uint8_t  nsegs;
  uint8_t  seg_idx;
  uint16_t seg_remain;   // bytes left in the current segment
  uint8_t  state;        // parser state (header/segtable/payload)
  uint8_t  hdr_pos;
  uint8_t  hdr[27];
  // stream info
  uint8_t  headers_done; // OpusHead + OpusTags consumed
  uint8_t  header_count;
  uint16_t pre_skip;     // from OpusHead: samples to drop at 48 kHz
  uint8_t  channels;     // from OpusHead
} ogg_t;

void ogg_init(ogg_t *o);

// Push up to `n` bytes; calls emit(pkt, len, user) for each COMPLETE audio
// packet. emit returns 0 to accept the packet, nonzero to SUSPEND: the
// demuxer holds that packet and stops consuming input, and the next
// ogg_push re-presents the SAME packet to emit before anything else (so a
// full PCM fifo pauses the stream instead of dropping audio).
// Returns the number of input bytes consumed (resume with data+consumed),
// or -1 on a malformed stream — the caller should stop the track.
int ogg_push(ogg_t *o, const uint8_t *data, int n,
             int (*emit)(const uint8_t *pkt, int len, void *user), void *user);

#endif
