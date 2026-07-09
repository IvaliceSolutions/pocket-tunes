# Pocket Tunes

A folder/metadata-driven **MP3 music player** (Opus later) as an **Analogue Pocket**
openFPGA core, with a **MiSTer** port planned. UI is the amber-on-black terminal from
`design_handoff_pocket_tunes_1b`.

## Status

| Milestone | What | State |
|---|---|---|
| M0 | Repo scaffold + `library.json` schema (indexer↔core contract) | ✅ done |
| M1 | Cross-platform library **indexer** (scan, tags, cover→RGB565, JSON) | ✅ done & validated |
| M2 | openFPGA core base: boot, video out, auto-load `library.json` into BRAM | ✅ **validated on real Pocket** |
| M3 | Amber UI in a firmware-drawn framebuffer (sidebar / main / drawer, D-pad/A/B) | 🟡 M3a: PicoRV32 SoC + 8bpp palettized fb + input — simulated, awaiting Pocket test |
| M4 | Audio: soft RISC-V + firmware MP3 decoder → PCM ring → I2S (real-time risk) | 🟡 **de-risked** — [feasible, ~17 MHz worst case](docs/derisk-m4.md) |
| M5 | Integration: pick track → stream file → decode → progress from sample position | ⬜ |
| M6 | MiSTer port (decode on ARM HPS, reuse indexer + UI logic) | ⬜ |

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
