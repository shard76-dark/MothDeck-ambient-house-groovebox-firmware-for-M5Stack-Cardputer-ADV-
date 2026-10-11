#ifndef DspHot_h
#define DspHot_h
#include <stdint.h>

// Sample-loop helpers. The ESP32-S3 (Xtensa LX7) path uses CLAMPS, MIN, MAX,
// ABS, and MULL, which a comparison or a 64-bit product does not always
// become. The host build uses the same arithmetic so the firmware tests
// match the device.
// Products passed to dspLerp8, dspPole, and dspMul must fit in signed 32
// bits: a full-scale sample times a Q15 coefficient does (65536 * 32767),
// and a shot fraction is only 0..999. The Q10 filter state does not, so
// that pole stays an int64 in C, where the compiler already emits MULL
// plus MULSH.

inline int dspSat16(int x) {
#if defined(__XTENSA__)
  __asm__ __volatile__("clamps %0, %1, 15" : "=a"(x) : "a"(x));
  return x;
#else
  if (x > 32767) {
    return 32767;
  }
  if (x < -32768) {
    return -32768;
  }
  return x;
#endif
}

// a + (b - a) * frac / 256. frac is 0..255.
inline int dspLerp8(int a, int b, int frac) {
  int d = b - a;
  if (frac <= 0) {
    return a;
  }
  if (frac >= 256) {
    return b;
  }
#if defined(__XTENSA__)
  int p;
  __asm__ __volatile__("mull %0, %1, %2" : "=a"(p) : "a"(d), "a"(frac));
  return a + (p >> 8);
#else
  return a + ((d * frac) >> 8);
#endif
}

// |x|. Not for INT_MIN.
inline int dspAbs(int x) {
#if defined(__XTENSA__)
  int y;
  __asm__ __volatile__("abs %0, %1" : "=a"(y) : "a"(x));
  return y;
#else
  return x < 0 ? -x : x;
#endif
}

// Clamp to lo..hi. lo must be <= hi.
inline int dspClamp(int x, int lo, int hi) {
#if defined(__XTENSA__)
  __asm__ __volatile__("max %0, %1, %2" : "=a"(x) : "a"(x), "a"(lo));
  __asm__ __volatile__("min %0, %1, %2" : "=a"(x) : "a"(x), "a"(hi));
  return x;
#else
  if (x > hi) {
    return hi;
  }
  if (x < lo) {
    return lo;
  }
  return x;
#endif
}

// Low 32 bits of a*b. The mathematical product must fit in signed 32 bits.
inline int dspMul(int a, int b) {
#if defined(__XTENSA__)
  int p;
  __asm__ __volatile__("mull %0, %1, %2" : "=a"(p) : "a"(a), "a"(b));
  return p;
#else
  return (int)((int64_t)a * b);
#endif
}

// (a * b) >> sh, sh is 1..30, and a*b fits in signed 32 bits.
inline int dspMulQ(int a, int b, int sh) {
  return dspMul(a, b) >> sh;
}

// (a * b) >> sh for a full signed 64-bit product. sh is 0..31.
// On Xtensa this is MULL plus MULSH. Define MOTHDECK_DSP_C to use the
// portable path even on device. Both results are the same integer.
inline int dspMulShift(int a, int b, int sh) {
  if (sh < 0) {
    sh = 0;
  } else if (sh > 31) {
    sh = 31;
  }
#if defined(__XTENSA__) && !defined(MOTHDECK_DSP_C)
  int lo;
  int hi;
  __asm__ __volatile__("mull %0, %2, %3\n\tmulsh %1, %2, %3" : "=&a"(lo), "=&a"(hi) : "a"(a), "a"(b));
  if (sh == 0) {
    return lo;
  }
  unsigned uhi = (unsigned)hi;
  unsigned ulo = (unsigned)lo;
  return (int)((uhi << (32 - sh)) | (ulo >> sh));
#else
  return (int)(((int64_t)a * (int64_t)b) >> sh);
#endif
}

// 4-point Hermite. frac is 0..den. frac 0 returns s0 and frac >= den returns s1.
// The curve matches the float Hermite within one LSB. Products need 64 bits.
inline int dspHermite(int sm1, int s0, int s1, int s2, int frac, int den) {
  if (den < 1) {
    den = 1;
  }
  if (frac <= 0) {
    return s0;
  }
  if (frac >= den) {
    return s1;
  }
  int64_t t = ((int64_t)frac << 16) / den;
  int64_t c0 = (int64_t)s0 << 16;
  int64_t c1 = ((int64_t)s1 - sm1) << 15;
  int64_t c2 = ((int64_t)sm1 << 16) - (((int64_t)s0 * 5) << 15) + ((int64_t)s1 << 17) - ((int64_t)s2 << 15);
  int64_t c3 = ((int64_t)(s2 - sm1) << 15) + ((((int64_t)s0 - s1) * 3) << 15);
  int64_t y = c3;
  y = ((y * t) >> 16) + c2;
  y = ((y * t) >> 16) + c1;
  y = ((y * t) >> 16) + c0;
  return (int)((y + 32768) >> 16);
}

// Triangular dither of one 16-bit LSB, then clamp. *state is the PRNG.
// A value already inside the 16-bit range moves by at most one count.
inline int dspDither16(int x, uint32_t *state) {
  uint32_t s = state ? *state : 0xA5A5u;
  s = s * 1664525u + 1013904223u;
  int u1 = (int)((s >> 16) & 0xFF);
  s = s * 1664525u + 1013904223u;
  int u2 = (int)((s >> 16) & 0xFF);
  if (state) {
    *state = s;
  }
  int num = u1 - u2;
  int adj = (num + (num >= 0 ? 128 : -128)) / 256;
  return dspSat16(x + adj);
}

// state += (x - state) * coef / 32768. coef is 1..32767.
// x and *state must already be sample-sized (±32768) so the product fits MULL.
inline int dspPole(int *state, int x, int coef) {
  if (coef < 1) {
    coef = 1;
  } else if (coef > 32767) {
    coef = 32767;
  }
  int d = x - *state;
  int step;
#if defined(__XTENSA__)
  int p;
  __asm__ __volatile__("mull %0, %1, %2" : "=a"(p) : "a"(d), "a"(coef));
  step = p >> 15;
#else
  step = (d * coef) >> 15;
#endif
  *state += step;
  return *state;
}

#endif
