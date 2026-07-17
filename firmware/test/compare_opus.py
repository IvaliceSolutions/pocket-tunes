#!/usr/bin/env python3
# Compare the firmware Opus decode against the ffmpeg reference. Opus decoders
# differ by a constant pre-roll (pre-skip + decoder warm-up), so we align by
# best cross-correlation over a lag search, then score correlation + NRMSE per
# channel and check the dominant tone. Pure stdlib (no numpy).
import sys, array, math, cmath

def load(p):
    a = array.array('h')
    with open(p, 'rb') as f:
        a.frombytes(f.read())
    n = (len(a) // 2) * 2
    L = [float(a[i])   for i in range(0, n, 2)]
    R = [float(a[i+1]) for i in range(0, n, 2)]
    return L, R

aL, aR = load(sys.argv[1])   # firmware
bL, bR = load(sys.argv[2])   # ffmpeg reference
na, nb = len(aL), len(bL)
print(f"firmware: {na} samples ({na/48000:.3f}s)   ref: {nb} samples ({nb/48000:.3f}s)")

# Align EACH channel independently: the two channels carry different tones
# (440 L / 660 R), so no single integer lag phase-aligns both — a lag that
# locks 440 Hz can leave 660 Hz half a cycle off. Per-channel lag removes that
# artefact and measures what we care about: is each channel's waveform right.
N = min(na, nb, 48000)
def best_lag(a, b):
    bk, bc = 0, -1e18
    for k in range(-1500, 1501):
        if k >= 0:
            c = sum(a[i] * b[i+k] for i in range(0, N-k, 4))
        else:
            c = sum(a[i-k] * b[i] for i in range(0, N+k, 4))
        if c > bc: bc, bk = c, k
    return bk

def align(a, b, k):
    if k >= 0: A, B = a[:na-k], b[k:k+na-k]
    else:      A, B = a[-k:], b[:na+k]
    M = min(len(A), len(B))
    return A[:M], B[:M]

ok = True
def norm(u):
    return math.sqrt(sum(x*x for x in u))
lagL = best_lag(aL, bL); lagR = best_lag(aR, bR)
print(f"alignment lag L={lagL} ({lagL/48.0:.2f} ms)  R={lagR} ({lagR/48.0:.2f} ms)")
for a, b, name, k in ((aL, bL, 'L', lagL), (aR, bR, 'R', lagR)):
    A, B = align(a, b, k)
    denom = norm(A) * norm(B)
    corr = (sum(A[i]*B[i] for i in range(len(A))) / denom) if denom else 0.0
    rms_ref = math.sqrt(sum(x*x for x in B) / len(B)) + 1e-9
    err = math.sqrt(sum((A[i]-B[i])**2 for i in range(len(A))) / len(A))
    nrmse = err / rms_ref
    verdict = "OK" if (corr > 0.98 and nrmse < 0.20) else "FAIL"
    if verdict == "FAIL": ok = False
    print(f"  {name}: corr={corr:.4f}  nrmse={nrmse:.3f}  -> {verdict}")

# dominant tone via a coarse Goertzel scan (expect ~440 L, ~660 R)
def dominant(u, fmax=2000, step=2):
    seg = u[:min(len(u), 48000)]
    n = len(seg)
    win = [seg[i] * (0.5 - 0.5*math.cos(2*math.pi*i/(n-1))) for i in range(n)]
    best_f, best_mag = 0, -1
    f = 100
    while f <= fmax:
        w = 2*math.pi*f/48000
        cr, ci = math.cos(w), math.sin(w)
        s0 = s1 = s2 = 0.0
        coeff = 2*cr
        for x in win:
            s0 = x + coeff*s1 - s2
            s2, s1 = s1, s0
        mag = s1*s1 + s2*s2 - coeff*s1*s2
        if mag > best_mag: best_mag, best_f = mag, f
        f += step
    return best_f
for a, name, want in ((aL, 'L', 440), (aR, 'R', 660)):
    fdom = dominant(a)
    tone_ok = abs(fdom - want) < 10
    if not tone_ok: ok = False
    print(f"  {name}: dominant tone {fdom} Hz (want ~{want}) -> {'OK' if tone_ok else 'FAIL'}")

dur_ok = abs(na - nb) < 48000 * 0.12
if not dur_ok: ok = False
print(f"  duration delta = {abs(na-nb)} samples -> {'OK' if dur_ok else 'FAIL'}")

print("RESULT:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
