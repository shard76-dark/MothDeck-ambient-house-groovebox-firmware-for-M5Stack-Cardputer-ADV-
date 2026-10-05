#include "Tracker.h"
#include "SongFile.h"
#include "InstrumentBank.h"
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

bool instrumentView(int id, ExtSampleView *out) {
  (void)id;
  if (out) {
    std::memset(out, 0, sizeof(*out));
  }
  return false;
}

static int packFx(int row, int dir) {
  return (row & 0xFF) | ((dir & 0xFF) << 8);
}

static bool fxSame(const TrackFx &a, const TrackFx &b) {
  return std::memcmp(&a, &b, sizeof(TrackFx)) == 0;
}

static void testFxStaysOnTrack() {
  Tracker tracker;
  tracker.SetCommand('f', packFx(0, 1));
  for (int i = 0; i < 4; i++) {
    tracker.SetCommand('f', packFx(1, 1));
  }
  tracker.SetCommand('f', packFx(6, 1));
  TrackFx track1 = tracker.voices[0].fx;
  expect(track1.filter == 1 && track1.cutoff == 32 && track1.reverb == 10, "track 1 insert steps");
  expect(!trackFxActive(tracker.voices[1].fx) && !trackFxActive(tracker.voices[2].fx), "other tracks start dry");

  tracker.SetCommand('T', 1);
  tracker.SetCommand('f', packFx(0, 1));
  tracker.SetCommand('f', packFx(0, 1));
  tracker.SetCommand('f', packFx(3, 1));
  tracker.SetCommand('f', packFx(8, 1));
  expect(fxSame(tracker.voices[0].fx, track1), "editing track 2 leaves track 1");
  expect(tracker.voices[1].fx.filter == 2 && tracker.voices[1].fx.delayDiv == 1 && tracker.voices[1].fx.drive == 10, "track 2 has its own insert");
  expect(!trackFxActive(tracker.voices[2].fx), "track 3 stays dry");

  tracker.SetCommand('A', 0);
  expect(!trackFxActive(tracker.voices[1].fx), "effects off clears the selected track");
  expect(fxSame(tracker.voices[0].fx, track1), "effects off does not clear the other track");
}

static void testVolumeRoundTrip() {
  Tracker tracker;
  tracker.SetCommand('T', 0);
  tracker.SetCommand('v', 1);
  tracker.SetCommand('T', 2);
  tracker.SetCommand('v', 7);
  tracker.SetCommand('T', 1);
  tracker.SetCommand('f', packFx(9, 1));
  expect(tracker.voices[0].volume == 1 && tracker.voices[2].volume == 7, "mixer volume is per track");
  expect(tracker.voices[1].fx.chorus == 10 && tracker.voices[0].fx.chorus == 0, "chorus stays on the track that was edited");

  SongData song;
  tracker.CaptureSong(&song);
  expect(song.voices[0].volume == 1 && song.voices[2].volume == 7, "capture keeps each fader");
  expect(song.fx[1].chorus == 10 && !trackFxActive(song.fx[0]), "capture keeps inserts apart");
  expect(songNeedsV3(song), "a chorus insert is saved as version 3");

  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, kSongFileBytesMax);
  SongData loaded;
  expect(n > kSongFileBytes && songDecode(buf, n, &loaded), "mixer song encodes and decodes");
  Tracker restored;
  restored.ApplySong(loaded);
  expect(restored.voices[0].volume == 1 && restored.voices[2].volume == 7 && restored.voices[3].volume == 2, "loaded faders match");
  expect(restored.voices[1].fx.chorus == 10 && !trackFxActive(restored.voices[0].fx), "loaded inserts stay on their track");

  restored.SetCommand('T', 2);
  restored.SetCommand('v', 3);
  expect(restored.voices[2].volume == 3 && restored.voices[0].volume == 1, "a later fader edit does not move the other tracks");
}

static int energy(Voice *voice, int count) {
  int sum = 0;
  for (int i = 0; i < count; i++) {
    int s = voice->UpdateVoice();
    if (s < 0) {
      s = -s;
    }
    if (s > 30000) {
      return 100000000;
    }
    sum += s;
  }
  return sum;
}

static void testInsertsAreIndependent() {
  Voice dry;
  Voice filtered;
  Voice twin;
  dry.SetNote(0, false, 1, 4);
  filtered.SetNote(0, false, 1, 4);
  twin.SetNote(0, false, 1, 4);
  filtered.fx.filter = 1;
  filtered.fx.cutoff = 0;
  bool changed = false;
  bool twinMatches = true;
  bool bounded = true;
  for (int i = 0; i < 800; i++) {
    int a = dry.UpdateVoice();
    int b = filtered.UpdateVoice();
    int c = twin.UpdateVoice();
    if (a != b) {
      changed = true;
    }
    if (a != c) {
      twinMatches = false;
    }
    if (a > 30000 || a < -30000 || b > 30000 || b < -30000) {
      bounded = false;
    }
  }
  expect(changed, "a low-pass insert changes only the voice it is set on");
  expect(twinMatches, "a voice with no insert matches another dry voice");
  expect(bounded, "filter output stays in range");

  Voice echo;
  echo.bps = 2.0f;
  echo.SetNote(0, false, 1, 4);
  echo.fx.delayDiv = 3;
  echo.fx.delayFb = 70;
  echo.fx.delayMix = 50;
  echo.fx.drive = 80;
  echo.fx.crush = 3;
  echo.fx.reverb = 80;
  echo.fx.chorus = 60;
  echo.fx.tremolo = 80;
  int hot = energy(&echo, 8000);
  expect(hot < 100000000 && hot > 0, "stacked inserts stay bounded");
}

int main() {
  testFxStaysOnTrack();
  testVolumeRoundTrip();
  testInsertsAreIndependent();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
