#include "Tracker.h"
#include "SongFile.h"
#include "InstrumentBank.h"
#include "DefaultSamples.h"
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

static int meanAbsDelta(Voice *voice, int count) {
  int prev = voice->UpdateVoice();
  long acc = 0;
  for (int i = 1; i < count; i++) {
    int s = voice->UpdateVoice();
    int d = s - prev;
    if (d < 0) {
      d = -d;
    }
    acc += d;
    prev = s;
  }
  return (int)(acc / (count - 1));
}

static void testDrumHitsOverlap() {
  Voice kick;
  kick.SetNote(0, false, -1, 0);
  int first = kick.UpdateVoice();
  int want = (((int)kick1[0] - 128) << 8) * kick.volume / 3;
  expect(first == want, "a single kick starts at the recorded level");

  Voice both;
  both.SetNote(0, false, -1, 0);
  for (int i = 0; i < 3000; i++) {
    both.UpdateVoice();
  }
  both.SetNote(4, false, -1, 0);
  Voice hat;
  hat.SetNote(4, false, -1, 0);
  long diff = 0;
  long hatE = 0;
  for (int i = 0; i < 800; i++) {
    int b = both.UpdateVoice();
    int h = hat.UpdateVoice();
    int d = b - h;
    diff += (long)d * (long)d;
    hatE += (long)h * (long)h;
  }
  expect(diff > hatE / 8, "a hat does not replace the kick that is still ringing");

  Voice sfx;
  sfx.SetNote(0, false, -1, 1);
  int loud = 0;
  for (int i = 0; i < 400; i++) {
    int s = sfx.UpdateVoice();
    if (s < 0) {
      s = -s;
    }
    loud += s;
  }
  expect(loud > 1000, "a sound effect still plays");
}

static void testInsertsAreSmooth() {
  Voice dry;
  Voice echo;
  dry.SetNote(0, false, 1, 4);
  echo.SetNote(0, false, 1, 4);
  echo.bps = 2.0f;
  echo.fx.delayDiv = 1;
  echo.fx.delayMix = 100;
  echo.fx.delayFb = 40;
  int dryEdge = meanAbsDelta(&dry, 6000);
  int wetEdge = meanAbsDelta(&echo, 6000);
  expect(wetEdge > 0 && wetEdge < dryEdge, "a full wet delay is darker than the dry saw, not grittier");

  Voice chor;
  chor.SetNote(0, false, 1, 4);
  chor.fx.chorus = 80;
  int chorEdge = meanAbsDelta(&chor, 4000);
  expect(chorEdge < dryEdge * 2, "chorus does not buzz the saw");

  Voice room;
  room.SetNote(0, false, 1, 4);
  room.fx.reverb = 80;
  bool changed = false;
  Voice plain;
  plain.SetNote(0, false, 1, 4);
  for (int i = 0; i < 8000; i++) {
    if (room.UpdateVoice() != plain.UpdateVoice()) {
      changed = true;
      break;
    }
  }
  expect(changed, "reverb changes the voice it is set on");
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
  testDrumHitsOverlap();
  testInsertsAreSmooth();
  testInsertsAreIndependent();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
