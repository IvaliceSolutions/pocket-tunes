// Streaming Ogg demuxer — see ogg.h. Byte-at-a-time state machine so page
// boundaries can land anywhere inside the caller's read chunks.

#include "ogg.h"

enum { ST_CAPTURE, ST_HEADER, ST_SEGTABLE, ST_PAYLOAD };

void ogg_init(ogg_t *o) {
  o->pkt_len = 0;
  o->pkt_cont = 0;
  o->nsegs = 0;
  o->seg_idx = 0;
  o->seg_remain = 0;
  o->state = ST_CAPTURE;
  o->hdr_pos = 0;
  o->headers_done = 0;
  o->header_count = 0;
  o->pre_skip = 0;
  o->channels = 2;
}

static int ogg_finish_packet(ogg_t *o,
                             void (*emit)(const uint8_t *, int, void *),
                             void *user) {
  if (!o->headers_done) {
    // packet 0 = OpusHead, packet 1 = OpusTags — consume, don't emit
    if (o->header_count == 0 && o->pkt_len >= 19) {
      o->channels = o->pkt[9];
      o->pre_skip = (uint16_t)(o->pkt[10] | (o->pkt[11] << 8));
    }
    o->header_count++;
    if (o->header_count >= 2) o->headers_done = 1;
  } else if (o->pkt_len > 0) {
    emit(o->pkt, o->pkt_len, user);
  }
  o->pkt_len = 0;
  return 0;
}

int ogg_push(ogg_t *o, const uint8_t *data, int n,
             void (*emit)(const uint8_t *, int, void *), void *user) {
  static const uint8_t magic[4] = {'O', 'g', 'g', 'S'};
  int i = 0;
  while (i < n) {
    switch (o->state) {
      case ST_CAPTURE:
        // hunt for "OggS" (byte-wise; resync-safe)
        if (data[i] == magic[o->hdr_pos]) {
          o->hdr[o->hdr_pos++] = data[i];
          if (o->hdr_pos == 4) o->state = ST_HEADER;
        } else {
          o->hdr_pos = (data[i] == 'O') ? 1 : 0;
        }
        i++;
        break;

      case ST_HEADER:
        o->hdr[o->hdr_pos++] = data[i++];
        if (o->hdr_pos == 27) {
          if (o->hdr[4] != 0) return -1;  // version
          // hdr[5] flags: bit0 = continuation of the previous packet — the
          // assembly buffer already holds the partial packet, so just go on.
          o->nsegs = o->hdr[26];
          o->seg_idx = 0;
          o->hdr_pos = 0;
          o->state = o->nsegs ? ST_SEGTABLE : ST_CAPTURE;
        }
        break;

      case ST_SEGTABLE:
        o->seg_table[o->hdr_pos++] = data[i++];
        if (o->hdr_pos == o->nsegs) {
          o->hdr_pos = 0;
          o->seg_idx = 0;
          o->seg_remain = o->seg_table[0];
          o->state = ST_PAYLOAD;
          // zero-length first segment: may already terminate a packet
          while (o->seg_remain == 0) {
            if (o->seg_table[o->seg_idx] < 255)
              if (ogg_finish_packet(o, emit, user)) return -1;
            o->seg_idx++;
            if (o->seg_idx >= o->nsegs) { o->state = ST_CAPTURE; break; }
            o->seg_remain = o->seg_table[o->seg_idx];
          }
        }
        break;

      case ST_PAYLOAD: {
        int take = n - i;
        if (take > o->seg_remain) take = o->seg_remain;
        if (o->pkt_len + take > OGG_MAX_PACKET) return -1;
        for (int k = 0; k < take; k++) o->pkt[o->pkt_len++] = data[i + k];
        i += take;
        o->seg_remain -= (uint16_t)take;

        while (o->seg_remain == 0 && o->state == ST_PAYLOAD) {
          // segment done: <255 terminates the packet, 255 continues it
          if (o->seg_table[o->seg_idx] < 255)
            if (ogg_finish_packet(o, emit, user)) return -1;
          o->seg_idx++;
          if (o->seg_idx >= o->nsegs) {
            o->state = ST_CAPTURE;  // next page (packet may continue there)
          } else {
            o->seg_remain = o->seg_table[o->seg_idx];
          }
        }
        break;
      }
    }
  }
  return 0;
}
