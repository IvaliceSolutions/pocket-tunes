# Pocket Tunes

A music player for the **Analogue Pocket**, as an openFPGA core.

Browse your library iPod-style, play **MP3** and **Opus** files — including
20-hour audiobooks with **chapter navigation** — with real cover art, a live
FFT equalizer, and sleep/wake that resumes to the second. Everything runs on a
soft RISC-V CPU inside the Pocket's FPGA: there is no OS, no Linux, no
application processor behind the curtain.

**➡️ [Guide d'utilisation (boutons & écrans)](docs/guide-utilisation.md)** (French)

## Features

- **iPod Classic-style UI** — full-screen list stack (Library → Albums →
  Tracks → Now Playing), amber-on-black pixel look, Cozette bitmap font,
  marquee scrolling for long titles, real-time clock in the status bar.
- **MP3 playback** (Helix fixed-point decoder) — up to 320 kbps CBR/VBR,
  44.1/48 kHz, ID3v2-aware (skips tags with embedded cover art cleanly).
- **Opus playback** (libopus fixed-point) — `.opus` audiobooks and music,
  streamed and demuxed (Ogg) on the fly.
- **Audiobooks** — files up to 4 GB / 20 h+, **chapter list read from the
  file's metadata** (ID3v2 CHAP for MP3, vorbis `CHAPTERxxx` comments for
  Opus), L/R to jump between chapters, resume-where-you-stopped.
- **Cover art** — the indexer pre-renders embedded/sidecar art to raw RGB565;
  the core shows a 96×96 cover on Now Playing and a thumbnail in the mini-bar.
- **Live equalizer** — 256-point FFT over the decoded PCM, 18 log-spaced bands.
- **Sleep/wake** — the Pocket's power button saves the whole player state
  (track, position, screen, modes) and restores it on wake. Save states
  (Analogue + Up) and OS screenshots (Analogue + Start) work too.
- **Shuffle & repeat**, seek, auto-advance, per-track resume.

## Using it

### 1. Install the core

Copy onto the Pocket SD card (from a [CI build artifact](../../actions) or
your own build — see below):

```
/Cores/jh.Tunes/                 ← core (bitstream.rbf_r + json)
/Platforms/pockettunes.json
```

`core/scripts/deploy_sd.sh <artifact-dir> <sd-mount>` does this for you.

### 2. Index your music

The Pocket's openFPGA framework has **no directory listing at runtime**, so a
small cross-platform indexer (Node.js, runs on macOS/Windows/Linux) scans your
music folder and writes `library.json` + pre-rendered covers:

```
node indexer/src/index.js \
  -m "/Volumes/POCKET/Assets/pockettunes/common/Music" \
  -o "/Volumes/POCKET/Assets/pockettunes/common" \
  -r /Assets/pockettunes/common/Music
```

Folder layout is free-form: `Artist/Album/tracks`, loose tracks directly in an
artist folder, or files dropped at the root of `Music/` — the UI shows each at
its natural depth. Re-run the indexer whenever the music changes.

> The indexer also transliterates accented filenames to ASCII **on disk**
> (the Pocket's file-open API cannot match non-ASCII names); display titles
> keep their accents.

### 3. Play

Launch **Tunes** from the Pocket's openFPGA menu. Controls are on one page in
the [user guide](docs/guide-utilisation.md); the short version: D-pad + A/B
navigate, Y is play/pause everywhere, X jumps back to Now Playing, L/R change
chapters, START/SELECT toggle shuffle/repeat.

## How it works

The Pocket gives an openFPGA core a Cyclone V FPGA and a file-access bridge —
nothing else. So ([`docs/architecture.md`](docs/architecture.md)):

- A **soft VexRiscv** (rv32im, 72 MHz) runs *everything*: UI, JSON parsing,
  Ogg demux, and the audio decoders themselves. The gateware provides video
  scanout, an I2S output, the APF bridge, and memories.
- **Code overlays**: firmware code lives in the cartridge-slot SRAM behind a
  4 KB I-cache; the *hot decode kernels of the active codec* are copied into a
  16 KB single-cycle **instruction TCM** at track start (profile-measured
  function set — this is what makes 320 kbps MP3 and Opus decode real-time).
- **Async SD streaming**: reads are fired asynchronously and absorbed between
  decoded frames; deep-offset reads into a 700 MB audiobook can take ~50 ms
  and never interrupt the audio.
- The **indexer ↔ core contract** is a single `library.json`
  ([format](docs/library-format.md)) plus raw RGB565 cover files named by
  track order — the core never parses tags, images, or directories.

## Building

**Firmware** (RISC-V): needs `riscv64-elf-gcc` (Homebrew: `riscv64-elf-gcc` +
`riscv64-elf-binutils`).

```
cd firmware && ./build.sh        # → firmware.bin (+ hex for simulation)
```

**Bitstream**: Quartus 21.1 — built by the GitHub Actions workflow
(`build-core.yml`, manual dispatch), which packages a ready-to-copy SD tree as
an artifact.

**Tests**: host-native decoder tests compare the real firmware code paths
byte-for-byte against ffmpeg (`firmware/test/run.sh`, `run_opus.sh`,
`run_lib.sh`), and a full-SoC iverilog simulation boots the actual bitstream
RTL + firmware and screenshots the UI (`core/sim/`, see `core/README.md`).

## Status & roadmap

Validated on a real Analogue Pocket (OS 2.6). Planned / not yet done:

- WAV/FLAC playback (enum reserved, no decoder yet)
- List-row cover thumbnails (needs external RAM for the framebuffer)
- MiSTer port (decode on the HPS, same indexer + UI)

## Credits & licenses

- [Helix MP3 decoder](https://en.wikipedia.org/wiki/Helix_Universal_Server)
  — RealNetworks RPSL/RCSL (see `firmware/helix/LICENSE.txt`).
- [libopus](https://opus-codec.org) 1.5.2 — BSD-3.
- [Cozette](https://github.com/the-moonwitch/Cozette) bitmap font — MIT.
- Core scaffold derived from Analogue's openFPGA template.
- UI design: `design_handoff_pocket_tunes_4a` (iPod Classic direction).
