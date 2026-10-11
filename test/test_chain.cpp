#include "InstrumentBank.h"
#include "SongFile.h"
#include "Tracker.h"
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

static void testSongChain() {
  SongData plain;
  std::memset(&plain, 0, sizeof(plain));
  plain.patternLength = 16;
  plain.bpms[0] = 120;
  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(plain, buf, kSongFileBytesMax);
  expect(n == kSongFileBytes && buf[4] == 1, "an empty chain stays a version 1 song");

  SongData song = plain;
  song.chain.len = 2;
  song.chain.pos = 0;
  song.chain.pattern[0] = 0;
  song.chain.pattern[1] = 2;
  song.chain.mute[1] = 0x1;
  n = songEncode(song, buf, kSongFileBytesMax);
  expect(n > kSongFileBytes && buf[4] == kSongVersionV5, "a chain is version 5");
  SongData back;
  expect(songDecode(buf, n, &back), "version 5 decodes");
  expect(back.chain.len == 2 && back.chain.pattern[1] == 2 && back.chain.mute[1] == 1, "scenes round-trip");
  expect(back.patternLength == 16, "the pattern length stays");

  SongData old;
  std::memset(&old, 0, sizeof(old));
  expect(songDecode(buf, n, &old), "decode again");
  uint8_t v1[kSongFileBytes];
  int n1 = songEncode(plain, v1, kSongFileBytes);
  SongData loaded;
  expect(songDecode(v1, n1, &loaded) && loaded.chain.len == 0, "an older song has no chain");
}

static void testPlaybackChain() {
  Tracker tracker;
  tracker.SetCommand('q', 0);
  tracker.SetCommand('$', 1);
  tracker.voices[0].mute = true;
  tracker.SetCommand('q', 0);
  expect(tracker.chain.len == 2, "two scenes were stored");
  expect(tracker.chain.pattern[0] == 0 && tracker.chain.pattern[1] == 1, "scenes keep their patterns");
  expect(tracker.chain.mute[1] == 1, "the second scene stores the mute");
  tracker.SetCommand('$', 0);
  tracker.voices[0].mute = false;
  int guard = 0;
  while (tracker.currentPattern == 0 && guard < 200000) {
    tracker.UpdateTracker();
    guard++;
  }
  expect(guard < 200000 && tracker.currentPattern == 1, "the chain advances at the pattern end");
  expect(tracker.voices[0].mute, "the scene mute is applied");
  expect(!tracker.voices[1].mute, "the other tracks stay unmuted");
  SongData song;
  tracker.CaptureSong(&song);
  expect(song.chain.len == 2 && song.chain.pattern[1] == 1, "saving the song keeps the chain");
  tracker.SetCommand('q', 1);
  expect(tracker.chain.len == 0, "clearing the chain leaves patterns alone");
  expect(tracker.currentPattern == 1, "clearing the chain does not change the pattern");
}

int main() {
  testSongChain();
  testPlaybackChain();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
