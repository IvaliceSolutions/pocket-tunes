# Pocket Tunes — Architecture

A folder/metadata-driven MP3 (later Opus) music player as an Analogue Pocket openFPGA
core, with a MiSTer port planned. UI is the amber-on-black terminal from
`design_handoff_pocket_tunes_1b`.

## Platform constraints that shape everything

1. **No application CPU on Pocket.** openFPGA cores are pure FPGA gateware (Cyclone V
   5CEBA4, ~49k LE + external SDRAM). There is no ARM to run decoder software on (unlike
   MiSTer). → We instantiate a **soft RISC-V (VexRiscv)** and run the decoder + app logic
   as firmware on it.
2. **No runtime directory enumeration.** APF target commands allow `0x0180` data-slot
   read (random access by offset), `0x0190` get-filename, `0x0192` open-file-by-path —
   but **no "list a folder"**. → An external **indexer** builds `library.json`
   (see `library-format.md`); the core consumes it and opens audio files by the paths
   inside it.

## Block diagram (Pocket)

```
        SD card (FAT32)                         Analogue OS / APF
   /Music/**  +  /Assets/pockettunes/common/library.json
        │                                             │
        │  0x0192 open-by-path, 0x0180 read-at-offset │  (bridge)
        ▼                                             ▼
 ┌───────────────────────────── core_top.sv (gateware) ─────────────────────────┐
 │                                                                              │
 │   APF bridge  ◄──►  file DMA / stream engine  ◄──►  SDRAM (compressed +      │
 │                                                     PCM ring + framebuffer)  │
 │                                                                              │
 │   VexRiscv (soft-CPU) ── runs firmware/ ──┐                                  │
 │     • parse library.json                  │                                  │
 │     • UI state machine + layout           │  writes RGB565 framebuffer       │
 │     • MP3 decode (libhelix) → PCM ring     │  in SDRAM                         │
 │                                           ▼                                  │
 │   video scanout  ──────────────► Pocket LCD 1600x1440 (scaled amber UI)       │
 │   PCM ring ──► sound_i2s.sv ───► Pocket audio out                            │
 │                                                                              │
 └──────────────────────────────────────────────────────────────────────────────┘
```

### Division of labor

- **Firmware (RISC-V C)** owns *policy*: JSON parse, the `focus/cursor/drawer/...` state
  model from the handoff, playback control, MP3 decode, progress from decoded sample
  position, drawing the UI into the framebuffer (text via a bitmap font blit, cover
  thumbnails via raw RGB565 blit).
- **Gateware (Verilog)** owns *mechanism*: APF bridge glue, a file-read DMA path, the
  SDRAM controller, video scanout of the framebuffer, and I2S out of the PCM ring.

Rationale: the heavy, fiddly, frequently-changing logic (parsing, layout, decode) lives
in C where it's fast to iterate and portable to MiSTer; only the real-time I/O plumbing
is in HDL.

## Audio pipeline

```
file bytes (SDRAM in-buf) → MP3 frame decode (firmware) → PCM samples → PCM ring (SDRAM)
                                                                   → sound_i2s @ 48kHz
```

Real-time budget is the main risk (M4). MP3 @ 44.1/48k stereo on a cached VexRiscv at
~50-100 MHz is comfortably feasible with a fixed-point decoder (libhelix / minimp3-style).
We prove this on a standalone bench before wiring the UI. WAV/PCM is a trivial passthrough.
Opus (later) reuses the exact pipeline with a libopus build; it's the tight case.

## MiSTer port (M6)

MiSTer has an ARM HPS running Linux → decoding and library parsing move to a small HPS
companion app; the same indexer output and the same UI *logic* (ported to the MiSTer
framebuffer/OSD conventions) are reused. The FPGA side is mostly I2S + video.

## Repo layout

```
pocket-tunes/
  docs/            architecture + library format (contracts)
  indexer/         cross-platform library indexer (Node) → library.json + covers/
  firmware/        RISC-V C: app logic + MP3 decode + UI rendering
  core/            openFPGA core (Verilog), derived from the Pocket core template
```
