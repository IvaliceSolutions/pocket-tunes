#!/usr/bin/env python3
"""Compare the simulated PCM output against the ffmpeg-decoded reference.

pcm_out.txt: one "L R" pair per 48 kHz sample (from the SoC testbench).
reference_48k.raw: s16le stereo 48 kHz (ffmpeg decode of the same MP3).

Alignment is unknown (decoder delay, resampler phase), so we find the offset
by cross-correlation on L, then report correlation and SNR over the overlap.
"""
import struct
import sys

def load_sim(path):
    L, R = [], []
    for line in open(path):
        parts = line.split()
        if len(parts) == 2:
            L.append(int(parts[0]))
            R.append(int(parts[1]))
    return L, R

def load_ref(path):
    data = open(path, "rb").read()
    n = len(data) // 4
    vals = struct.unpack(f"<{n*2}h", data[:n*4])
    return list(vals[0::2]), list(vals[1::2])

def corr_at(a, b, off, n):
    sa = sb = saa = sbb = sab = 0
    for i in range(n):
        x, y = a[i], b[off + i]
        sa += x; sb += y; saa += x*x; sbb += y*y; sab += x*y
    cov = sab - sa*sb/n
    va = saa - sa*sa/n
    vb = sbb - sb*sb/n
    if va <= 0 or vb <= 0:
        return 0.0
    return cov / (va**0.5 * vb**0.5)

def main():
    simL, simR = load_sim("pcm_out.txt")
    refL, refR = load_ref("reference_48k.raw")
    # skip the sim's initial silence (decoder warm-up)
    start = 0
    while start < len(simL) and abs(simL[start]) < 50:
        start += 1
    simL, simR = simL[start:], simR[start:]
    print(f"sim samples: {len(simL)} (skipped {start} of warm-up silence)")

    n = min(8000, len(simL) - 100)
    best_off, best_c = 0, -2.0
    # ref also starts with encoder-delay silence; scan a generous window
    for off in range(0, min(9000, len(refL) - n)):
        c = corr_at(simL[:200], refL, off, 200)  # coarse scan on 200 samples
        if c > best_c:
            best_c, best_off = c, off
    c_full_L = corr_at(simL[:n], refL, best_off, n)
    c_full_R = corr_at(simR[:n], refR, best_off, n)

    # SNR after gain-normalized subtraction (L channel)
    num = den = 0
    for i in range(n):
        e = simL[i] - refL[best_off + i]
        num += refL[best_off + i] ** 2
        den += e * e
    snr = 10 * (0 if den == 0 else __import__("math").log10(num / den)) if den else 99

    print(f"alignment offset: {best_off}")
    print(f"correlation L: {c_full_L:.4f}  R: {c_full_R:.4f}  (n={n})")
    print(f"SNR L vs ref: {snr:.1f} dB")
    ok = c_full_L > 0.97 and c_full_R > 0.97
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
