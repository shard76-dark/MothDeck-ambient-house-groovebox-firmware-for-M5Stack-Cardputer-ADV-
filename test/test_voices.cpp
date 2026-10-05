#include "ToneSynth.h"
#include "DefaultSamples.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

static int render(int id, int freq, int n, int16_t *dst) {
  ToneVoice voice;
  toneNoteOn(&voice, id);
  int peak = 0;
  for (int i = 0; i < n; i++) {
    int s = toneSample(&voice, id, freq);
    dst[i] = (int16_t)s;
    int a = s < 0 ? -s : s;
    if (a > peak) {
      peak = a;
    }
  }
  return peak;
}

static int maxDelta(const int16_t *s, int n) {
  int mx = 0;
  for (int i = 1; i < n; i++) {
    int d = s[i] - s[i - 1];
    if (d < 0) {
      d = -d;
    }
    if (d > mx) {
      mx = d;
    }
  }
  return mx;
}

static int meanAbsDelta(const int16_t *s, int n) {
  long acc = 0;
  for (int i = 1; i < n; i++) {
    int d = s[i] - s[i - 1];
    if (d < 0) {
      d = -d;
    }
    acc += d;
  }
  return (int)(acc / (n - 1));
}

static long energy(const int16_t *s, int a, int b) {
  long acc = 0;
  for (int i = a; i < b; i++) {
    acc += (long)s[i] * s[i] / 64;
  }
  return acc;
}

static int expand8(uint8_t sample) {
  return ((int)sample - 128) << 8;
}

static long spreadDiff(const uint8_t *a, int aLen, const uint8_t *b, int bLen) {
  int n = aLen < bLen ? aLen : bLen;
  if (n < 9) {
    return 0;
  }
  long acc = 0;
  for (int k = 1; k <= 8; k++) {
    int i = n * k / 9;
    int d = expand8(a[i]) - expand8(b[i]);
    if (d < 0) {
      d = -d;
    }
    acc += d;
  }
  return acc;
}

static void testTones() {
  int16_t sine[800];
  int16_t saw[800];
  int16_t pad[16000];
  int16_t pluck[16000];
  int16_t bass[800];
  int peak = render(2, 1000, 800, sine);
  expect(peak > 8000 && peak <= 32767, "sine is loud and in range");
  int crossings = 0;
  for (int i = 1; i < 672; i++) {
    if ((sine[i - 1] < 0 && sine[i] >= 0) || (sine[i - 1] > 0 && sine[i] <= 0)) {
      crossings++;
    }
  }
  expect(crossings >= 6 && crossings <= 10, "sine at C4 is about four cycles");
  render(4, 1000, 800, saw);
  expect(maxDelta(saw, 800) > maxDelta(sine, 800) * 4, "saw lead has a sharper edge than sine");
  render(11, 1000, 16000, pad);
  int early = 0;
  int late = 0;
  for (int i = 0; i < 400; i++) {
    int a = pad[i] < 0 ? -pad[i] : pad[i];
    if (a > early) {
      early = a;
    }
  }
  for (int i = 14000; i < 15000; i++) {
    int a = pad[i] < 0 ? -pad[i] : pad[i];
    if (a > late) {
      late = a;
    }
  }
  expect(early < 2500 && late > early * 4, "pad attack is soft and the body is louder");
  render(7, 1000, 16000, pluck);
  expect(energy(pluck, 200, 1200) > energy(pluck, 12000, 14000) * 4, "pluck decays");
  int bassPeak = render(10, 500, 800, bass);
  expect(bassPeak > 2000, "bass makes a tone");
  expect(meanAbsDelta(bass, 800) < meanAbsDelta(saw, 800), "bass is darker than the saw lead");
}

static void testDrumsAndSfx() {
  expect(kick1Length > 8000 && special1Length > 1500 && kick2Length > 800, "kick, snare, and hat are longer than a click");
  expect(spreadDiff(kick1, kick1Length, special1, special1Length) > 20000, "kick and snare are different");
  expect(spreadDiff(hihat1, hihat1Length, kick2, kick2Length) > 8000, "clap and closed hat are different");
  const uint8_t *sfx[] = {sfx1, sfx2, sfx3, sfx4, sfx5, sfx6, sfx7, sfx8, sfx9, sfx10, sfx11, sfx12};
  const int lens[] = {
      sfx1Length, sfx2Length, sfx3Length, sfx4Length, sfx5Length, sfx6Length,
      sfx7Length, sfx8Length, sfx9Length, sfx10Length, sfx11Length, sfx12Length};
  bool distinct = true;
  for (int i = 1; i < 12; i++) {
    if (spreadDiff(sfx[0], lens[0], sfx[i], lens[i]) < 4000) {
      distinct = false;
    }
    if (lens[i] < 400) {
      distinct = false;
    }
  }
  expect(distinct, "each sfx differs from the riser and is not a blip");
  expect(sfx1Length > sfx7Length, "riser is longer than the blip");
}

int main() {
  testTones();
  testDrumsAndSfx();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
