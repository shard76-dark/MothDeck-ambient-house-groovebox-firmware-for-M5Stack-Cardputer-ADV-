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
