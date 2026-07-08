# Pocket Tunes — Library Index Format (`library.json`)

The **indexer** (runs on a PC: macOS / Windows 11 / Linux) scans the user's music
folder on the SD card and emits a single `library.json` plus a `covers/` folder of
pre-rasterized thumbnails. The **core** never enumerates directories itself (openFPGA
exposes no runtime directory listing); it opens `library.json` by its known path via
APF target command `0x0192`, and later opens each audio file the same way using the
`path` recorded here.

## Design contract mapping

Mirrors the handoff state model (`design_handoff_pocket_tunes_1b/README.md`):
`library` = tree of `{ artistId, artistName, albums: [{ albumId, title, year, genre,
coverArt, tracks: [...] }] }`. Field names below are the canonical on-disk form.

## Layout on the SD card

```
/Assets/pockettunes/common/
  library.json            # this file
  covers/
    a0000.rgb565          # 46x46 drawer thumbnail (album 0)
    a0000.s.rgb565        # 26x26 list thumbnail  (album 0)
    ...
```

All `path` fields in `library.json` are **relative to the SD card root** (`/`), because
`0x0192` takes an absolute-on-card path. `coverArt` paths are relative to the directory
containing `library.json`.

## Schema (schemaVersion 1)

```jsonc
{
  "schemaVersion": 1,
  "generator": "pocket-tunes-indexer 0.1.0",
  "root": "/Music",                 // SD-relative folder that was scanned (informational)
  "counts": { "artists": 12, "albums": 40, "tracks": 512 },

  "artists": [
    {
      "id": 0,                      // stable within a single generation; index into array
      "name": "Boards of Canada",
      "albums": [
        {
          "id": 0,                  // album id, unique across the WHOLE library (not per-artist)
          "title": "Music Has the Right to Children",
          "year": 1998,             // integer or null
          "genre": "Electronic",    // string or null
          "hue": 174,               // 0-359, deterministic fallback tint when coverArt is null
          "coverArt": "covers/a0000.rgb565",     // 46x46 thumb, or null
          "coverArtSmall": "covers/a0000.s.rgb565", // 26x26 thumb, or null
          "tracks": [
            {
              "index": 0,           // 0-based position within the album (== play order)
              "title": "Wildlife Analysis",
              "durationMs": 78000,  // integer ms, or null if unknown
              "format": "MP3",      // "MP3" | "WAV" | "FLAC" | "OPUS" | "OGG"
              "codec": "MPEG 1 Layer 3",
              "bitrateKbps": 320,   // nominal/average kbps, or null
              "sampleRate": 44100,  // Hz
              "channels": 2,
              "fileSize": 3129344,  // bytes
              "coverArt": "covers/t00000.rgb565",     // this track's own 46x46 art, or null
              "coverArtSmall": "covers/t00000.s.rgb565", // 26x26, or null
              "path": "/Music/Boards of Canada/MHTRTC/01 Wildlife Analysis.mp3"
            }
          ]
        }
      ]
    }
  ]
}
```

### Field notes

- **Tree shape** — Artist folder → Album folder → track file, exactly the on-disk
  hierarchy. An audio file whose folder structure doesn't reach two levels deep is
  placed under a synthetic artist `"(unknown)"` / album derived from tags or folder name.
- **id vs index** — `artist.id` indexes the `artists` array; `album.id` is globally
  unique (so `covers/a{album.id}` is unambiguous); `track.index` is the in-album play
  order. The core keeps its cursor state in terms of these.
- **coverArt** — pre-decoded raw pixels so the core blits directly (no JPEG decoder
  on the soft-CPU). Format `RGB565` little-endian, tightly packed, no header:
  `46*46*2 = 4232` bytes and `26*26*2 = 1352` bytes. When no embedded/sidecar art is
  found, `coverArt` is `null` and the core paints the `hue` gradient placeholder.
  Covers exist at **two levels**: each **track** carries its own `coverArt` from that
  file's embedded art (`covers/t#####.*`) — important for compilations/audiobooks where
  every file has distinct art; the **album** `coverArt` is a same-folder sidecar image if
  present, else the first track's cover as a representative (`covers/a####.*` only when a
  sidecar was rasterized). The now-playing drawer should prefer the current track's cover.
- **hue** — deterministic per album (hash of "artist|album") so placeholders are stable
  across regenerations, matching the prototype's per-album hue key.
- **durationMs / bitrateKbps** — from container headers; for CBR MP3 exact, for VBR the
  average. `null` allowed; the drawer collapses metadata lines that are null/hidden.
- **path** — verbatim string passed to `0x0192`. Must use `/` separators and be valid
  on FAT32 (the indexer records the on-card path, not the PC path).

## Versioning

`schemaVersion` is bumped on any breaking change. The core refuses to load a
`schemaVersion` it doesn't understand and shows a "regenerate your library" message.
