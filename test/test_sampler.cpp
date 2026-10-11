#include "MixCapture.h"
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

int main() {
  expect(!mixCaptureReady(), "a take starts empty");
  expect(mixCaptureMix() == 0, "playback is silent until a take is armed");
  mixCaptureArm();
  for (int i = 0; i < 100; i++) {
    mixCaptureFeed(i * 100);
  }
  expect(!mixCaptureReady(), "a short arm is still recording");
  for (int i = 100; i < kCaptureFrames; i++) {
    mixCaptureFeed((i & 1) ? 10000 : -10000);
  }
  expect(mixCaptureReady() && mixCaptureLength() == kCaptureFrames, "the buffer fills and stops");
  mixCaptureFeed(32000);
  expect(mixCaptureLength() == kCaptureFrames, "a full take does not grow");
  mixCapturePlay(1, 1);
  int first = mixCaptureMix();
  expect(first == 0, "the first stored sample is the start of the ramp");
  int loud = 0;
  for (int i = 0; i < 2000; i++) {
    int s = mixCaptureMix();
    if (s < 0) {
      s = -s;
    }
    if (s > loud) {
      loud = s;
    }
  }
  expect(loud > 8000, "playback reaches the stored square");
  mixCapturePlay(2, 1);
  for (int i = 0; i < 5000; i++) {
    mixCaptureMix();
  }
  expect(mixCaptureMix() == 0, "double speed finishes inside 5000 calls");
  mixCapturePlay(1, 1);
  for (int i = 0; i < 4000; i++) {
    mixCaptureMix();
  }
  int mid = mixCaptureMix();
  if (mid < 0) {
    mid = -mid;
  }
  expect(mid > 8000, "unity speed is still in the take after 4000 calls");
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
