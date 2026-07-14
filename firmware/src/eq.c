// 18-band audio equalizer for the Now Playing drawer (design 3a).
//
// The decoder taps 48 kHz mono samples into a ring; once per video frame we
// window the newest 256 samples (Hann, Q15), run a 256-point radix-2 fixed-
// point FFT (quarter-wave sine table, Q14, >>1 per stage so nothing clips),
// fold bins 1..127 into 18 log-spaced bands and convert each magnitude to a
// bar height on a log scale. Bars rise instantly and decay ~1/8 per frame.
//
// Cost: ~8k multiplies per FFT ≈ well under 1 ms at 72 MHz — a few % of a
// 60 Hz frame, dwarfed by the MP3 decode itself.

#include "eq.h"

uint8_t eq_bars[EQ_BANDS];

#define N 256
#define LOG2N 8

static int16_t ring[N];
static volatile uint32_t ring_w;

void eq_feed(int16_t s) {
  ring[ring_w & (N - 1)] = s;
  ring_w++;
}

// sin(2*pi*k/256) in Q14 via a quarter-wave table.
static const int16_t sin_q14[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756,
    5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102,
    9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406,
    12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635,
    14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};

static int32_t isin(unsigned k) {  // k in turns/256
  k &= 255;
  if (k < 64) return sin_q14[k];
  if (k < 128) return sin_q14[128 - k];
  if (k < 192) return -sin_q14[k - 128];
  return -sin_q14[256 - k];
}
static int32_t icos(unsigned k) { return isin(k + 64); }

// Hann window, Q15, symmetric: w[i] for i=0..128, w[256-i] = w[i].
static const uint16_t hann_q15[129] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593,
    705, 827, 958, 1098, 1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542,
    2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518, 4799, 5086, 5381, 5682,
    5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378,
    14778, 15178, 15580, 15981, 16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184,
    19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280, 22653, 23023, 23388, 23750,
    24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639,
    30832, 31018, 31194, 31361, 31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369,
    32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762, 32767};

// Log-spaced band edges over bins 1..128 (187.5 Hz per bin at 48 kHz).
static const uint8_t band_edge[EQ_BANDS + 1] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 15, 19, 25, 33, 44, 57, 75, 98, 128};

// int16 is enough: input is 16-bit and every stage halves — the
// butterflies' products promote to int before the shift back down.
static int16_t re[N], im[N];

static unsigned bitrev8(unsigned v) {
  v = ((v & 0x0F) << 4) | ((v & 0xF0) >> 4);
  v = ((v & 0x33) << 2) | ((v & 0xCC) >> 2);
  v = ((v & 0x55) << 1) | ((v & 0xAA) >> 1);
  return v;
}

static void fft256(void) {
  // radix-2 DIT, one >>1 per stage (256x total) keeps everything in 16 bits
  for (int stage = 0; stage < LOG2N; stage++) {
    int half = 1 << stage;          // butterflies per group
    int step = N >> (stage + 1);    // twiddle stride in turns/256
    for (int g = 0; g < N; g += half << 1) {
      for (int b = 0; b < half; b++) {
        int i = g + b, j = i + half;
        int32_t wr = icos(b * step), wi = -isin(b * step);
        int32_t tr = (wr * re[j] - wi * im[j]) >> 14;  // int promo: 30-bit safe
        int32_t ti = (wr * im[j] + wi * re[j]) >> 14;
        int32_t ar = re[i], ai = im[i];
        re[i] = (int16_t)((ar + tr) >> 1);
        im[i] = (int16_t)((ai + ti) >> 1);
        re[j] = (int16_t)((ar - tr) >> 1);
        im[j] = (int16_t)((ai - ti) >> 1);
      }
    }
  }
}

// |z| ~= max + min/2 (alpha-max beta-min, ~4% error — plenty for a meter)
static uint32_t mag(int32_t r, int32_t i) {
  uint32_t ar = r < 0 ? -r : r, ai = i < 0 ? -i : i;
  return ar > ai ? ar + (ai >> 1) : ai + (ar >> 1);
}

// log2 with 2 fractional bits: 4*ilog2(m) + next-2-bits, 0 for m==0
static int loglvl(uint32_t m) {
  if (m < 2) return 0;
  int bl = 31 - __builtin_clz(m);
  int frac = bl >= 2 ? (int)((m >> (bl - 2)) & 3) : 0;
  return bl * 4 + frac;
}

void eq_tick(void) {
  // snapshot the newest 256 samples, windowed, bit-reversed into re[]
  uint32_t base = ring_w;  // newest sample is ring[(base-1) & mask]
  for (int i = 0; i < N; i++) {
    int32_t s = ring[(base - N + i) & (N - 1)];
    unsigned w = (i < 128) ? hann_q15[i] : hann_q15[N - i];
    re[bitrev8(i)] = (int16_t)((s * (int32_t)w) >> 15);
  }
  for (int i = 0; i < N; i++) im[i] = 0;
  // imaginary part is zeroed AFTER the loop above wrote re[] — separate array
  fft256();

  for (int b = 0; b < EQ_BANDS; b++) {
    uint32_t peak = 0;
    for (int k = band_edge[b]; k < band_edge[b + 1]; k++) {
      uint32_t m = mag(re[k], im[k]);
      if (m > peak) peak = m;
    }
    // loglvl of a full-scale tone lands ~36-40; noise floor < 12
    int lvl = loglvl(peak) - 12;
    int h = lvl <= 0 ? 0 : lvl * EQ_BAR_MAX / 28;
    if (h > EQ_BAR_MAX) h = EQ_BAR_MAX;
    // rise instantly, decay ~1/8 per frame (min 1 px so it always settles)
    int cur = eq_bars[b];
    if (h >= cur) cur = h;
    else {
      int dec = cur >> 3;
      cur -= dec ? dec : 1;
      if (cur < h) cur = h;
    }
    eq_bars[b] = (uint8_t)cur;
  }
}
