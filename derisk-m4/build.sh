#!/bin/bash
# Build + run the rv32im MP3 decode instruction-count benchmark under qemu.
set -e
D="$(cd "$(dirname "$0")" && pwd)"
H="$D/helix"
OPT="${1:--O2}"

riscv64-elf-gcc -march=rv32im_zicsr -mabi=ilp32 "$OPT" -ffreestanding -nostdlib \
  -I"$D/shim" -I"$H/pub" -I"$H/real" \
  "$D/crt0.S" "$D/harness.c" "$D/mp3_data.c" \
  "$H/mp3dec.c" "$H/mp3tabs.c" \
  "$H/real/bitstream.c" "$H/real/buffers.c" "$H/real/dct32.c" "$H/real/dequant.c" \
  "$H/real/dqchan.c" "$H/real/huffman.c" "$H/real/hufftabs.c" "$H/real/imdct.c" \
  "$H/real/polyphase.c" "$H/real/scalfact.c" "$H/real/stproc.c" "$H/real/subband.c" \
  "$H/real/trigtabs.c" \
  -T "$D/link.ld" -lgcc -o "$D/harness.elf"

echo "built harness.elf ($OPT), size: $(ls -l "$D/harness.elf" | awk '{print $5}') bytes"
