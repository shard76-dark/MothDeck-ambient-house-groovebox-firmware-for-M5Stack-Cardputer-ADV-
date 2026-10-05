#include "KitFormat.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void expect(bool ok, const char *message) {
  if (ok) {
    std::printf("ok  %s\n", message);
    return;
  }
  std::printf("FAIL %s\n", message);
  failures++;
}

static void testKit() {
  const char *text =
      "mothdeck-kit 1\n"
      "name=Dusty\n"
      "pad=kick.wav\n"
      "pad=rim.wav\n"
      "pad=snare.wav\n"
      "pad=clap.wav\n"
      "pad=hat.wav\n"
      "pad=openhat.wav\n"
      "pad=perc.wav\n"
      "pad=tom.wav\n"
      "pad=shaker.wav\n"
      "pad=ride.wav\n"
      "pad=snap.wav\n"
      "pad=crash.wav\n";
  KitManifest kit;
  expect(parseKitManifest(text, (int)std::strlen(text), &kit), "kit manifest parses");
  expect(std::strcmp(kit.name, "Dusty") == 0 && kit.padCount == 12, "name and 12 pads");
  expect(std::strcmp(kit.pads[0], "kick.wav") == 0 && std::strcmp(kit.pads[11], "crash.wav") == 0, "pad order");

  const char *spaced =
      "mothdeck-kit 1\n"
      "name=Ambient House\n"
      "pad=kick.wav\n"
      "pad=rim.wav\n"
      "pad=snare.wav\n"
      "pad=clap.wav\n"
      "pad=hat.wav\n"
      "pad=openhat.wav\n"
      "pad=perc.wav\n"
      "pad=tom.wav\n"
      "pad=shaker.wav\n"
      "pad=ride.wav\n"
      "pad=snap.wav\n"
      "pad=crash.wav\n";
  expect(parseKitManifest(spaced, (int)std::strlen(spaced), &kit), "spaced kit name parses");
  expect(std::strcmp(kit.name, "Ambient House") == 0, "spaced name kept");

  const char *bad = "not-a-kit\nname=x\n";
  expect(!parseKitManifest(bad, (int)std::strlen(bad), &kit), "bad kit magic rejected");
  expect(std::strstr(kit.error, "magic") != 0, "bad kit magic explains itself");

  const char *shortKit =
      "mothdeck-kit 1\n"
      "name=808\n"
      "pad=kick.wav\n";
  expect(!parseKitManifest(shortKit, (int)std::strlen(shortKit), &kit), "short kit rejected");
  expect(std::strstr(kit.error, "12") != 0, "short kit asks for 12 pads");

  const char *slash =
      "mothdeck-kit 1\n"
      "name=808\n"
      "pad=../kick.wav\n"
      "pad=rim.wav\n"
      "pad=snare.wav\n"
      "pad=clap.wav\n"
      "pad=hat.wav\n"
      "pad=openhat.wav\n"
      "pad=perc.wav\n"
      "pad=tom.wav\n"
      "pad=shaker.wav\n"
      "pad=ride.wav\n"
      "pad=snap.wav\n"
      "pad=crash.wav\n";
  expect(!parseKitManifest(slash, (int)std::strlen(slash), &kit), "pad path is rejected");

  const char *extra = "mothdeck-kit 1\nname=808\n";
  char buf[800];
  int n = (int)std::strlen(extra);
  std::memcpy(buf, extra, (size_t)n);
  for (int i = 0; i < 13; i++) {
    int w = std::snprintf(buf + n, sizeof(buf) - (size_t)n, "pad=p%d.wav\n", i);
    n += w;
  }
  expect(!parseKitManifest(buf, n, &kit), "13 pads rejected");
}

int main() {
  testKit();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
