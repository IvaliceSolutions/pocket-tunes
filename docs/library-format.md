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
    t00000.rgb565         # 96x96 Lecture cover (global track 0)
    t00000.s.rgb565       # 32x32 list thumbnail (global track 0)
    a0000.rgb565          # 96x96 album sidecar cover (album 0), when present
    ...
```

All `path` fields in `library.json` are **relative to the SD card root** (`/`), because
`0x0192` takes an absolute-on-card path. `coverArt` paths are relative to the directory
containing `library.json`.

## Schema (schemaVersion 2)

v2 (design 4a) lets loose files keep their real depth instead of being buried
under synthetic `"(unknown)"`/`"(singles)"` buckets:

- **`rootTracks` (top level)** — audio files directly in the music root; shown
  mixed into the top-level Bibliothèque list.
- **`artists[].rootTracks`** — audio files directly in an artist folder outside
  any album; an artist with only these skips the Albums screen entirely, an
  artist with both shows them appended after its albums.
- **`tracks[].chapters`** — ID3 CHAP chapters (audiobooks), only present when
  non-empty: `[{ "s": startSeconds, "t": "title" }, ...]` sorted by start. The
  core navigates them with L/R.

```jsonc
{
  "schemaVersion": 2,
  "generator": "pocket-tunes-indexer 0.2.0",
  "root": "/Music",                 // SD-relative folder that was scanned (informational)
  "counts": { "artists": 12, "albums": 40, "tracks": 512, "rootTracks": 3, "trackCovers": 90 },

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
          "coverArt": "covers/a0000.rgb565",     // 96x96 cover, or null
          "coverArtSmall": "covers/a0000.s.rgb565", // 32x32 list thumb, or null
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
              "coverArt": "covers/t00000.rgb565",     // this track's own 96x96 art, or null
              "coverArtSmall": "covers/t00000.s.rgb565", // 32x32, or null
              "chapters": [ { "s": 0, "t": "Chapitre 1" } ],  // OMITTED when the file has none
              "path": "/Music/Boards of Canada/MHTRTC/01 Wildlife Analysis.mp3"
            }
          ]
        }
      ],
      "rootTracks": [ /* same track shape; OMITTED when empty */ ]
    }
  ],

  "rootTracks": [ /* same track shape; OMITTED when empty. MUST stay the last key:
                     the core numbers tracks in file order (covers depend on it). */ ]
}
```

### Field notes

- **Tree shape** — Artist folder → Album folder → track file, exactly the on-disk
  hierarchy. Audio directly in an artist folder lands in that artist's `rootTracks`;
  audio directly in the music root lands in the top-level `rootTracks`.
- **Key order is part of the contract** — the core assigns each track a global
  index in *file order* (each artist's `albums` then its `rootTracks`, artists in
  order, top-level `rootTracks` last) and cover files are named `t{gidx:05}` after
  that order. The indexer emits keys accordingly; don't reorder by hand.
- **id vs index** — `artist.id` indexes the `artists` array; `album.id` is globally
  unique (so `covers/a{album.id}` is unambiguous); `track.index` is the in-album play
  order. The core keeps its cursor state in terms of these.
- **coverArt** — pre-decoded raw pixels so the core blits directly (no JPEG decoder
  on the soft-CPU). Format `RGB565` little-endian, tightly packed, no header:
  `96*96*2 = 18432` bytes and `32*32*2 = 2048` bytes (the core also accepts the
  older 48x48 files by size, pixel-doubled). When no embedded/sidecar art is
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

`schemaVersion` is bumped on any structural change. v2 is a superset of v1: the
core's parser skips unknown keys, so it reads v1 files (no `rootTracks`, the old
synthetic buckets appear as regular artists) and v1-era cores simply ignore the
v2 additions.
