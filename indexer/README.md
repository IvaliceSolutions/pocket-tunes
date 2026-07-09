# Pocket Tunes Indexer

Cross-platform (macOS / Windows 11 / Linux) tool that scans a music folder and
produces the `library.json` + `covers/` that the Pocket Tunes core reads. The core
cannot enumerate SD directories at runtime, so this does it ahead of time.

## Use

```bash
npm install
node src/index.js --music <music-folder> --out <output-folder> --sd-root <on-card-path>
```

- `--music`  folder to scan (point it at the Pocket's `Music` folder, or a copy).
- `--out`    where `library.json` and `covers/` are written. On the card this is
             `/Assets/pockettunes/common/`.
- `--sd-root` the absolute path of the music folder **as seen on the Pocket SD card**.
             Baked into every track path so the core can open files with target
             command `0x0192`.

> **Important — music must live under `/Assets` (or `/Saves`).** The Pocket's
> `0x0192` open-file command only opens paths inside those two trees, so put your
> music under `/Assets/pockettunes/common/Music/…` and pass a matching `--sd-root`
> (e.g. `/Assets/pockettunes/common/Music`). Music left in a top-level `/Music`
> folder cannot be opened by the core.

Example, card mounted at `/Volumes/POCKET`:

```bash
node src/index.js -m "/Volumes/POCKET/Assets/pockettunes/common/Music" -o "/Volumes/POCKET/Assets/pockettunes/common" -r "/Assets/pockettunes/common/Music"
```

Re-run whenever the library changes ("library refresh").

## What it reads

- Tree from folder structure: `Artist/Album/track`. Nested album subfolders (CD1/CD2)
  flatten into the album; loose files fall into `(singles)` / `(unknown)` buckets.
- Tags via `music-metadata` (ID3, Vorbis comments, etc.): title, artist, album, year,
  genre, track/disc number, duration, bitrate, sample rate, channels.
- Cover art: a same-folder `cover/folder/front.*` image, else the first embedded
  picture. Rasterized to raw RGB565 thumbnails (46×46 + 26×26) so the core blits them
  directly — no image decoder on the FPGA soft-CPU.

Output schema: [`../docs/library-format.md`](../docs/library-format.md).

## Packaging (later)

Ship as a single per-OS binary via Node SEA (`node --experimental-sea-config`) so end
users don't need Node installed. Both deps (`music-metadata`, `jimp`) are pure-JS, so no
native rebuilds per platform.
