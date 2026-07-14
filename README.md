# Pocket Tunes

A folder/metadata-driven **MP3 music player** (Opus later) as an **Analogue Pocket**
openFPGA core, with a **MiSTer** port planned. UI is the amber "90s console" design
from `design_handoff_pocket_tunes_3a` (3-level browser, full-screen player with a
live FFT equalizer, persistent mini-bar).

**➡️ [Guide d'utilisation (boutons & écrans)](docs/guide-utilisation.md)**

## Status

| Milestone | What | State |
|---|---|---|
| M0 | Repo scaffold + `library.json` schema (indexer↔core contract) | ✅ done |
| M1 | Cross-platform library **indexer** (scan, tags, cover→RGB565, JSON, ASCII filename transliteration) | ✅ **validated on real Pocket** |
| M2 | openFPGA core base: boot, video out, auto-load `library.json` into BRAM | ✅ **validated on real Pocket** |
| M3 | Amber UI in a firmware-drawn framebuffer: 3-level browse (artists → albums → tracks) + now-playing drawer, D-pad/A/B, bounce-scrolling (marquee) titles | ✅ **validated on real Pocket** |
| M4 | Audio: soft **VexRiscv** RISC-V + Helix firmware MP3 decoder → PCM ring → I2S, **real-time** (~4× margin @72 MHz) | ✅ **validated on real Pocket** |
| M5 | Playback: pick track → stream file → decode → progress; FF/RW seek, auto-advance (loops), **shuffle** (START), ID3v2-tag skip | ✅ **validated on real Pocket** |
| M6 | MiSTer port (decode on ARM HPS, reuse indexer + UI logic) | ⬜ planned |

**Backlog / polish:** real RGB565 cover thumbnails (indexer already emits them), repeat-one / repeat-off modes, WAV/PCM playback, Opus, PCM-flush-on-seek, skip-leading-silence. A `-Os` pass is needed before large additions — the firmware is near the CPU's 128 KB RAM ceiling.

## Why this architecture

Two openFPGA constraints drive the design (details in
[`docs/architecture.md`](docs/architecture.md)):

1. **No application CPU on Pocket** → a soft **RISC-V (VexRiscv)** in the FPGA runs the
   MP3 decoder *and* the app/UI logic; gateware does only video scanout, I2S, and the
   APF file bridge.
2. **No runtime directory listing** → an external **indexer** builds `library.json`; the
   core opens files by the paths inside it (target command `0x0192`).

## Layout

- [`docs/`](docs) — architecture + [library format](docs/library-format.md) (the contracts).
- [`indexer/`](indexer) — the cross-platform library indexer (Node). **Runnable now.**
- `firmware/` — RISC-V C: app logic + MP3 decode + UI rendering (M3+).
- `core/` — openFPGA Verilog core, derived from the Pocket core template (M2+).
