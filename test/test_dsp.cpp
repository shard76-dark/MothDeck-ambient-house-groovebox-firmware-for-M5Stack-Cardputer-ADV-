#include "DspHot.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

static int hermiteRef(int sm1, int s0, int s1, int s2, int frac, int den) {
  if (frac <= 0) {
    return s0;
  }
  if (frac >= den) {
    return s1;
  }
  double t = (double)frac / (double)den;
  double c0 = s0;
  double c1 = 0.5 * (s1 - sm1);
  double c2 = sm1 - 2.5 * s0 + 2.0 * s1 - 0.5 * s2;
  double c3 = 0.5 * (s2 - sm1) + 1.5 * (s0 - s1);
  return (int)std::llround(((c3 * t + c2) * t + c1) * t + c0);
}

static void testHermite() {
  expect(dspHermite(1, 2, 3, 4, 0, 1000) == 2, "hermite frac 0 is the current frame");
  expect(dspHermite(1, 2, 3, 4, 1000, 1000) == 3, "hermite frac at the end is the next frame");
  const int pts[][4] = {
      {0, 0, 1000, 0},
      {-8000, -4000, 4000, 8000},
      {32767, -32768, 100, -100},
      {12, 40, 40, 12},
  };
  bool close = true;
  for (int p = 0; p < 4; p++) {
    for (int frac = 0; frac <= 1000; frac += 50) {
      int got = dspHermite(pts[p][0], pts[p][1], pts[p][2], pts[p][3], frac, 1000);
      int want = hermiteRef(pts[p][0], pts[p][1], pts[p][2], pts[p][3], frac, 1000);
      int d = got - want;
      if (d < 0) {
        d = -d;
      }
      if (d > 1) {
        close = false;
      }
    }
  }
  expect(close, "hermite stays within one LSB of the float curve");
}

static void testDither() {
  uint32_t state = 1;
  int quiet = 0;
  long sum = 0;
  const int n = 4000;
  for (int i = 0; i < n; i++) {
    int y = dspDither16(1000, &state);
    sum += y;
    int d = y - 1000;
    if (d < 0) {
      d = -d;
    }
    if (d > 1) {
      quiet = 1;
    }
  }
  expect(!quiet, "dither moves a 16-bit sample by at most one LSB");
  double mean = (double)sum / (double)n;
  expect(mean > 999.5 && mean < 1000.5, "dither does not shift the average");
  expect(dspDither16(40000, &state) == 32767, "dither clamps the top");
  expect(dspDither16(-40000, &state) == -32768, "dither clamps the bottom");
}

int main() {
  testHermite();
  testDither();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
