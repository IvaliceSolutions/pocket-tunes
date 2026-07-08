#!/usr/bin/env python3
"""Convert a Quartus .rbf bitstream into the Analogue Pocket .rev format.

The Pocket's APF loads the bitstream with the bit order of every byte
reversed compared to a raw RBF, so we just flip each byte's bits.

Usage: reverse_rbf.py input.rbf output.rev
"""
import sys

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    table = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(data.translate(table))
    print(f"wrote {sys.argv[2]} ({len(data)} bytes)")

if __name__ == "__main__":
    main()
