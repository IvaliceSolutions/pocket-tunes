# Pocket Tunes — openFPGA core

M2 bring-up build: boots, outputs 400×360@60 video (scaled 4× to the Pocket's
1600×1440 LCD), auto-loads `/Assets/pockettunes/common/library.json` into a
128 KB BRAM and displays a diagnostic screen proving the whole path works:

- amber gradient band (RGB depth)
- the JSON's bytes as a 1bpp texture (data arrived intact)
- progress bar sized by the loaded byte count (full file arrived)
- bouncing block (60 fps liveness) + amber border

Simulated frame (iverilog, real library.json preloaded): `sim/out_frame.png`.

## Layout

- `platform/pocket/` — Analogue APF framework files (verbatim; their license).
  `pocket.tcl` carries the device + all pin assignments.
- `target/pocket/` — the core itself:
  - `core_top.sv` — top level under `apf_top` (standard openFPGA port list)
  - `pt_pll.v` — 74.25 → 48 MHz sys, 12 MHz + 12 MHz/90° video
  - `video_gen.sv` — 400×360@60 timing (500×400 total @ 12 MHz = exact 60 Hz)
  - `pt_display.sv` — M2 diagnostic painter
  - `library_slot.sv` — APF data slot → BRAM + byte counter
  - `data_loader.sv`, `sound_i2s.sv`, `sync_fifo.sv` — agg23 (MIT)
  - `core_bridge_cmd.v` — Analogue host/target command handler
- `projects/` — Quartus project (`pocket_tunes.qpf`, top = `apf_top`)
- `pkg/` — SD-card metadata (`Cores/jh.Tunes/`, `Platforms/`)
- `sim/` — iverilog testbenches + vendor stubs (never synthesized)
- `scripts/reverse_rbf.py` — .rbf → .rev (Pocket bit order)

## Build

**CI (recommended):** push the repo to GitHub and run the “Build core” workflow
(`.github/workflows/build-core.yml`). It compiles in the `raetro/quartus:21.1`
docker image, reverses the bitstream, and uploads an SD-ready package.

**Locally (Windows/Linux + Quartus 21.1 Lite):**

```
cd core/projects
quartus_sh --flow compile pocket_tunes
python3 ../scripts/reverse_rbf.py output_files/pocket_tunes.rbf \
    ../pkg/Cores/jh.Tunes/bitstream.rbf_r
```

## Deploy to SD

```
/Cores/jh.Tunes/          ← core/pkg/Cores/jh.Tunes/* + bitstream.rbf_r
/Platforms/pockettunes.json     ← core/pkg/Platforms/pockettunes.json
/Assets/pockettunes/common/     ← library.json + covers/ (from the indexer)
```

Then: Pocket → openFPGA → Pocket Tunes. Expected: the amber diagnostic screen,
progress bar ≈ 2/3 filled for a 65 KB library.json, JSON texture visible.
If `library.json` is missing the core still boots (slot is `required:false`):
empty texture + zero-width bar.

## Simulation

```
cd core/sim
xxd -p -c 1 /path/to/library.json > library_bytes.hex
iverilog -g2012 -o tb_video.vvp tb_video.sv ../target/pocket/video_gen.sv ../target/pocket/pt_display.sv && vvp tb_video.vvp
iverilog -g2012 -o tb_loader.vvp tb_loader.sv ../target/pocket/library_slot.sv ../target/pocket/data_loader.sv ../platform/pocket/common.v vendor_stubs.sv && vvp tb_loader.vvp
```

`tb_video` checks the exact frame timing (200,000 dots, 360×400 active, sync
counts) and renders a full frame to `out_frame.ppm`. `tb_loader` streams real
library.json bytes through emulated APF bridge writes and verifies BRAM
content + byte counter.

### Full-SoC simulation (boot + UI + MP3 decode)

`tb_soc` models the real APF boot order: firmware pushed to SRAM while the
core is in reset, then Reset Exit, then the CPU boots, parses `mini_lib.json`
and plays `audio_bytes.hex` through scripted button presses. Screens are
captured to `out_soc_{a,b,c}.ppm` (Bibliothèque / Albums / Lecture) and the
decoded PCM to `pcm_out.txt`. Inputs: `library_bytes.hex` (xxd of
mini_lib.json), `audio_bytes.hex` (xxd of a test MP3), and
`../projects/firmware_sram.hex` (written by `firmware/build.sh`).

```
cd core/sim
iverilog -g2012 -o tb_soc.vvp tb_soc.sv sram_model.v \
  ../target/pocket/pt_soc.sv ../target/pocket/pt_mem.sv ../target/pocket/vexriscv.v \
  ../target/pocket/sram_ctrl.v ../target/pocket/bridge_rx_ram.sv ../target/pocket/ss_ctrl.v \
  ../target/pocket/video_gen.sv ../target/pocket/fb_scanout.sv ../target/pocket/sync_fifo.sv \
  ../target/pocket/data_loader.sv i2s_stub.sv ../platform/pocket/common.v vendor_stubs.sv
vvp tb_soc.vvp    # ~20-30 min wall time
```

(`i2s_stub.sv` replaces `sound_i2s.sv`, which icarus can't elaborate.) The
firmware load itself takes ~1 µs per halfword ≈ 6 frames for a 195 KB image —
the TB script counts frames from reset release, never from an absolute
frame number.

## Known M2 limits (by design)

- library.json capped at 128 KB (`size_maximum` in data.json = BRAM size).
  M3 moves the library into PSRAM/SDRAM and lifts this.
- No `icon.bin` / platform `.bin` images yet (Pocket shows defaults) — polish later.
- Audio outputs valid silent I2S; playback is M4/M5.
