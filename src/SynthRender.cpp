#include "SynthRender.h"
#include "DspHot.h"

static int osc(int wave, int i, int n) {
  if (n < 2) {
    return 0;
  }
  int p = i % n;
  switch (wave) {
    case 1: // square
      return p < n / 2 ? 22000 : -22000;
    case 2: { // saw
      return (int)((int64_t)p * 44000 / n) - 22000;
    }
    case 3: { // triangle
      int q = p < n / 2 ? p : n - p;
      return (int)((int64_t)q * 44000 / (n / 2)) - 22000;
    }
    default: { // sine, 64-entry quarter via Bhaskara-ish polynomial on the phase
      // One-liner integer sine: map phase to -32768..32767 using a small Taylor.
      int x = (int)(((int64_t)p * 65536) / n); // 0..65535
      int q = (x >> 14) & 3;
      int a = x & 16383;
      if (q & 1) {
        a = 16383 - a;
      }
      // sin(pi/2 * a/16384) ≈ 1.57*(u) - 0.645*(u^3) with u=a/16384, scaled
      int64_t u = a;
      int64_t u3 = u * u / 16384 * u / 16384;
      int s = (int)((25736 * u - 10500 * u3) / 16384);
      if (s > 32767) {
        s = 32767;
      }
      if (q >= 2) {
        s = -s;
      }
      return s;
    }
  }
}

void renderSubtractive(int16_t *dst, int n, int wave, int cutoff, int resonance) {
  if (!dst || n < 2) {
    return;
  }
  if (wave < 0 || wave > 3) {
    wave = 0;
  }
  if (cutoff < 1) {
    cutoff = 1;
  }
  if (cutoff > 100) {
    cutoff = 100;
  }
  if (resonance < 0) {
    resonance = 0;
  }
  if (resonance > 90) {
    resonance = 90;
  }
  // One-pole lowpass. Higher cutoff keeps more of the cycle.
  int shift = 1 + (100 - cutoff) / 18;
  if (shift > 6) {
    shift = 6;
  }
  int acc = 0;
  int prev = 0;
  for (int i = 0; i < n; i++) {
    int x = osc(wave, i, n);
    acc += (x - acc) >> shift;
    int y = acc;
    if (resonance > 0) {
      int hp = x - acc;
      y += hp * resonance / 120;
    }
    // A touch of the previous sample keeps the cycle from clicking.
    y = (y * 3 + prev) / 4;
    prev = y;
    dst[i] = (int16_t)dspSat16(y);
  }
  // Remove DC so a looped cycle does not thump.
  int64_t sum = 0;
  for (int i = 0; i < n; i++) {
    sum += dst[i];
  }
  int dc = (int)(sum / n);
  for (int i = 0; i < n; i++) {
    dst[i] = (int16_t)dspSat16(dst[i] - dc);
  }
}

void renderFm(int16_t *dst, int n, int ratio, int indexAmount) {
  if (!dst || n < 2) {
    return;
  }
  if (ratio < 1) {
    ratio = 1;
  }
  if (ratio > 16) {
    ratio = 16;
  }
  if (indexAmount < 0) {
    indexAmount = 0;
  }
  if (indexAmount > 100) {
    indexAmount = 100;
  }
  int modAmp = (n * indexAmount) / 400;
  if (modAmp < 1) {
    modAmp = 1;
  }
  for (int i = 0; i < n; i++) {
    int mod = osc(0, i * ratio, n);
    int bend = (int)(((int64_t)mod * modAmp) / 32768);
    int idx = i + bend;
    if (idx < 0) {
      idx = (idx % n + n) % n;
    }
    dst[i] = (int16_t)dspSat16(osc(0, idx, n));
  }
}
