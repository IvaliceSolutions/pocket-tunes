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

"$CC" -march=rv32im -mabi=ilp32 -O2 -ffreestanding -nostdlib \
  -Wall -Wextra -Werror=implicit-function-declaration \
  "$D/src/crt0.S" "$D/src/main.c" "$D/src/gfx.c" "$D/src/lib.c" "$D/src/ui.c" "$D/src/file.c" \
  -T "$D/src/link.ld" -lgcc -o "$D/firmware.elf"

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
