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
  font7x13="$D/../third_party/7x13.bdf" font5x8="$D/../third_party/5x8.bdf" >/dev/null

HELIX="$D/helix"
CF="-march=rv32im -mabi=ilp32 -ffreestanding -nostdlib \
  -Wall -Wextra -Werror=implicit-function-declaration \
  -I $D/shim -I $HELIX/pub -I $HELIX/real"
OBJ="$D/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"; OBJS=""

# App/UI/decoder-glue at -Os: the 128 KB CPU RAM is tight, and this code is not
# the DSP hot path, so trade a little speed for several KB of code space
# (headroom for the stack + future features).
for s in src/crt0.S src/main.c src/gfx.c src/lib.c src/ui.c src/file.c src/mp3.c; do
  o="$OBJ/app_$(basename "${s%.*}").o"
  "$CC" $CF -Os -c "$D/$s" -o "$o"; OBJS="$OBJS $o"
done

# Helix stays at -O2 — it IS the real-time decode hot path; keep it fast.
for s in mp3dec.c mp3tabs.c real/bitstream.c real/buffers.c real/dct32.c \
         real/dequant.c real/dqchan.c real/huffman.c real/hufftabs.c \
         real/imdct.c real/polyphase.c real/scalfact.c real/stproc.c \
         real/subband.c real/trigtabs.c; do
  o="$OBJ/helix_$(basename "${s%.*}").o"
  "$CC" $CF -O2 -c "$HELIX/$s" -o "$o"; OBJS="$OBJS $o"
done

"$CC" $CF $OBJS -T "$D/src/link.ld" -lgcc -Wl,-Map="$D/firmware.map" -o "$D/firmware.elf"

"$OBJCOPY" -O binary "$D/firmware.elf" "$D/firmware.bin"

# binary → one hex file per byte lane (the RAM is four 8-bit banks so that
# Quartus RAM inference never has to deal with byte enables), padded to 32K
python3 - "$D/firmware.bin" "$OUT" <<'EOF'
import sys, os
data = open(sys.argv[1], "rb").read()
data += b"\0" * (-len(data) % 4)
words = len(data) // 4
for lane in range(4):
    with open(os.path.join(sys.argv[2], f"firmware_b{lane}.hex"), "w") as f:
        for i in range(words):
            f.write(f"{data[i*4+lane]:02x}\n")
        for _ in range(32768 - words):
            f.write("00\n")
print(f"firmware: {len(data)} bytes → firmware_b0..3.hex ({words} words)")
EOF

ls -l "$D/firmware.bin" | awk '{print "firmware.bin: "$5" bytes"}'
