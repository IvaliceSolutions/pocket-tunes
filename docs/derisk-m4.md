# M4 De-risk — Real-time MP3 decode on a soft RISC-V

**Verdict: GO, with large margin.** Real-time MP3 decoding on a VexRiscv soft-CPU is
not a risk. Worst measured case needs ~17 MHz at a pessimistic IPC; the core will run at
50–100+ MHz, leaving the CPU mostly idle for UI + file streaming.

## Method

Measured **exact retired instructions** to decode real MP3 files, on the true target ISA
(rv32im, no FPU — matches a typical VexRiscv config), using the fixed-point **Helix MP3
decoder** (it ships a `#ifdef __riscv` path using `mul`/`mulh`).

- Toolchain: `riscv64-elf-gcc -march=rv32im_zicsr -mabi=ilp32 -O2 -ffreestanding`.
- Simulator: `qemu-system-riscv32 -machine virt`, reading the `minstret` CSR around the
  decode loop only (init/table setup excluded).
- Inputs: 5 s of pink noise (a heavy Huffman case) at several bitrates/rates, encoded
  with LAME. Test files verified with ffprobe.
- Reproduce: `derisk-m4/build.sh` + `derisk-m4/run.sh` (harness in `derisk-m4/harness.c`).

`instr_per_audio_second = instr × sampleRate / samplesDecodedPerChannel`.
`required_MHz = instr_per_audio_second / (IPC × 1e6)`.

## Results

| Input | instr / s of audio | req MHz @IPC 1.0 | @IPC 0.7 | @IPC 0.5 (pessimistic) |
|---|---:|---:|---:|---:|
| MP3 128 kbps, 44.1 kHz, stereo | 8.0 M | 8 | 11 | 16 |
| MP3 320 kbps, 44.1 kHz, stereo | 8.3 M | 8 | 11 | 16 |
| MP3 256 kbps, 48 kHz, stereo   | 8.6 M | 8 | 12 | 17 |

- Bitrate barely matters (128→320 kbps: +3%). 48 kHz adds ~8%.
- IPC 0.5 already bakes in ~2× cycle inflation for SDRAM/cache stalls and multi-cycle
  mul — generous for a cached core running the ~45 KB working set largely from BRAM.
- Small run-to-run variance (~3%) is real content variance (fresh random noise per file).

## Footprint

- Decoder code + tables: **~45 KB** (fits BRAM).
- Working RAM: Helix heap (MP3DecInfo + sub-structs, a few–~20 KB) + PCM frame buffer
  (~9 KB) → **~20–30 KB**. Negligible against the Cyclone V's BRAM + external SDRAM.

## Implications for the architecture

- **One VexRiscv core is plenty** for MP3 decode *and* the UI/app logic. At ~9 MHz-worth
  of work for real-time audio and a 50–100 MHz clock, ~85–95% of CPU is free for the
  browser, JSON parse, and framebuffer drawing (all event-driven, cheap).
- Confirms the M2/M3 plan: firmware owns decode + UI; gateware owns I2S, video scanout,
  APF file bridge.

## Opus (future)

Not measured here (deferred). Opus (SILK+CELT) is heavier than MP3, but embedded
precedent — Cortex-M4 @ ~80 MHz decodes Opus in real time — suggests it's feasible on
VexRiscv with less headroom. A separate de-risk (build libopus for rv32im, same harness)
is warranted before committing to Opus.

## Caveats

- Instruction count, not cycles: real cycles depend on VexRiscv config (caches, mul
  latency) and SDRAM latency. The IPC 0.5 column is the conservative planning number.
- qemu models a generic rv32; final numbers should be re-confirmed on the actual VexRiscv
  once M2 stands up, but the 3–6× margin makes surprises very unlikely.
