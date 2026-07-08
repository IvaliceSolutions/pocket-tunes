#!/bin/bash
# Run harness.elf under qemu with a hard time cap so a hang can't wedge the shell.
D="$(cd "$(dirname "$0")" && pwd)"
CAP="${1:-15}"
qemu-system-riscv32 -machine virt -bios none -nographic -no-reboot -kernel "$D/harness.elf" &
QPID=$!
sleep "$CAP"
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null
echo "[qemu stopped after ${CAP}s cap]"
