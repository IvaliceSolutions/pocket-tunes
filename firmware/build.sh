#!/bin/bash
# Build the Pocket Tunes firmware → core/projects/firmware.hex (+ palette.hex).
# Needs riscv64-elf-gcc (brew) or riscv64-unknown-elf-gcc (Ubuntu) + python3.
set -e
D="$(cd "$(dirname "$0")" && pwd)"
OUT="$D/../core/projects"

CC=""
for c in riscv64-elf-gcc riscv64-unknown-elf-gcc riscv-none-elf-gcc; do
  command -v "$c" >/dev/null && CC="$c" && break
done
[ -n "$CC" ] || { echo "no riscv gcc found"; exit 1; }
OBJCOPY="${CC%gcc}objcopy"

# regenerate palette + fonts (cheap, keeps them in sync with the scripts)
python3 "$D/scripts/palette_gen.py" "$OUT/palette.hex" "$D/src/palette.h"
python3 "$D/scripts/bdf2c.py" "$D/src/fonts.h" \
  fontmain="$D/../third_party/cozette.bdf" fontsmall="$D/../third_party/5x8.bdf" >/dev/null

HELIX="$D/helix"
# -ffunction-sections/-fdata-sections + --gc-sections at link: libopus is
# vendored whole (the encoder files are entangled in the build), so the linker
# is what actually drops every encoder path the decoder never calls.
CF="-march=rv32im -mabi=ilp32 -ffreestanding -nostdlib \
  -ffunction-sections -fdata-sections \
  -Wall -Wextra -Werror=implicit-function-declaration \
  -I $D/shim -I $HELIX/pub -I $HELIX/real -I $D/opus/include"
OBJ="$D/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"; OBJS=""

# App/UI/decoder-glue at -Os: the 128 KB CPU RAM is tight, and this code is not
# the DSP hot path, so trade a little speed for several KB of code space
# (headroom for the stack + future features).
for s in src/crt0.S src/main.c src/gfx.c src/lib.c src/chap.c src/ui.c src/file.c src/mp3.c src/eq.c src/art.c src/ogg.c src/opus_play.c src/codec.c; do
  o="$OBJ/app_$(basename "${s%.*}").o"
  "$CC" $CF -Os -c "$D/$s" -o "$o"; OBJS="$OBJS $o"
done

# libopus (M7b): fixed-point decoder, -O2 like Helix — it's a hot path too.
# Encoder/float/demo files were dropped when vendoring; --gc-sections trims
# whatever else the decoder never reaches.
OPUS="$D/opus"
# VAR_ARRAYS, not NONTHREADSAFE_PSEUDOSTACK: the pseudostack wants a
# GLOBAL_STACK_SIZE-sized scratch block out of the codec arena, and that define
# is a fixed 120000 in celt/arch.h — sized for the encoder, which we don't
# build. VAR_ARRAYS instead puts libopus' scratch in C99 VLAs on the real
# stack. Measured under qemu (scratchpad/derisk-opus, -O2, decoder-only):
# 10676 bytes of stack peak and zero scratch allocation. The arena below is
# then only the decoder state, and the bytes it gives back become stack.
OPUS_DEFS="-DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DVAR_ARRAYS -DPT_FIRMWARE -DOPUS_VERSION=\"1.5.2-pt\""
OPUS_INC="-I$OPUS/include -I$OPUS/celt -I$OPUS/silk -I$OPUS/silk/fixed -I$OPUS/src"
for f in "$OPUS"/src/*.c "$OPUS"/celt/*.c "$OPUS"/silk/*.c "$OPUS"/silk/fixed/*.c; do
  o="$OBJ/opus_$(basename "${f%.*}").o"
  "$CC" $CF $OPUS_DEFS $OPUS_INC -O2 -c "$f" -o "$o"; OBJS="$OBJS $o"
done

# Helix stays at -O2 — it IS the real-time decode hot path; keep it fast.
for s in mp3dec.c mp3tabs.c real/bitstream.c real/buffers.c real/dct32.c \
         real/dequant.c real/dqchan.c real/huffman.c real/hufftabs.c \
         real/imdct.c real/polyphase.c real/scalfact.c real/stproc.c \
         real/subband.c real/trigtabs.c; do
  o="$OBJ/helix_$(basename "${s%.*}").o"
  "$CC" $CF -O2 -c "$HELIX/$s" -o "$o"; OBJS="$OBJS $o"
done

"$CC" $CF $OBJS -T "$D/src/link.ld" -Wl,--gc-sections -lgcc -Wl,-Map="$D/firmware.map" -o "$D/firmware.elf"

"$OBJCOPY" -O binary "$D/firmware.elf" "$D/firmware.bin"

# M7a: the whole image (text + fast rodata/data copy source) lives in external
# SRAM, loaded at boot by the APF "Firmware" data slot; BRAM has no init.
# Emit the SD blob plus a 16-bit little-endian hex for the sim SRAM model.
python3 - "$D/firmware.bin" "$OUT" <<'EOF'
import sys, os
data = open(sys.argv[1], "rb").read()
data += b"\0" * (-len(data) % 2)
halves = len(data) // 2
assert len(data) <= 256 * 1024, f"firmware {len(data)} bytes > 256K SRAM"
with open(os.path.join(sys.argv[2], "firmware_sram.hex"), "w") as f:
    for i in range(halves):
        f.write(f"{data[2*i+1]:02x}{data[2*i]:02x}\n")
print(f"firmware: {len(data)} bytes -> firmware.bin + firmware_sram.hex ({halves} halfwords)")
EOF

ls -l "$D/firmware.bin" | awk '{print "firmware.bin: "$5" bytes"}'
