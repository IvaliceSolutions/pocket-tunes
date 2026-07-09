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
  "$D/src/crt0.S" "$D/src/main.c" "$D/src/gfx.c" \
  -T "$D/src/link.ld" -lgcc -o "$D/firmware.elf"

"$OBJCOPY" -O binary "$D/firmware.elf" "$D/firmware.bin"

# binary → 32-bit little-endian word hex for $readmemh, padded to full RAM
python3 - "$D/firmware.bin" "$OUT/firmware.hex" <<'EOF'
import sys, struct
data = open(sys.argv[1], "rb").read()
data += b"\0" * (-len(data) % 4)
words = struct.unpack("<%dI" % (len(data) // 4), data)
with open(sys.argv[2], "w") as f:
    for w in words:
        f.write(f"{w:08x}\n")
    for _ in range(32768 - len(words)):
        f.write("00000000\n")
print(f"firmware: {len(data)} bytes → {sys.argv[2]}")
EOF

ls -l "$D/firmware.bin" | awk '{print "firmware.bin: "$5" bytes"}'
