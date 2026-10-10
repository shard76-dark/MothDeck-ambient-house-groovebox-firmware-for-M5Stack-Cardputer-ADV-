#include "Tracker.h"
#include "InstrumentBank.h"
#include "SongFile.h"
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

static void noteOn(Tracker &tracker, int channel, int note) {
  MidiEvent ev = {};
  ev.type = MIDI_MSG_NOTE_ON;
  ev.channel = (uint8_t)channel;
  ev.number = (uint8_t)note;
  ev.value = 100;
  tracker.HandleMidi(ev);
}

static int extraCount(const Snap &snap, int step) {
  int n = 0;
  for (int slot = 0; slot < 3; slot++) {
    if ((snap.rollChord[step][slot] & 0x0F) > 0) {
      n++;
    }
  }
  return n;
}

static void testStackAndSong() {
  Tracker tracker;
  tracker.SetCommand('I', 2);
  tracker.SetCommand('P', 0);
  expect(tracker.recOn && tracker.isPlaying, "space arms rec");

  noteOn(tracker, 0, 60);
  noteOn(tracker, 0, 60);
  noteOn(tracker, 0, 64);
  noteOn(tracker, 0, 67);
  noteOn(tracker, 0, 71);
  noteOn(tracker, 0, 72);

  expect(tracker.NoteAt(0, 0) == 1 && tracker.OctaveAt(0, 0) == 2, "the first midi note stays the step word");
  Snap snap;
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(extraCount(snap, 0) == 3, "three extra notes stack on the same step");
  expect((snap.rollChord[0][0] & 0x0F) == 5, "the second pitch is the first extra");
  expect((snap.rollChord[0][1] & 0x0F) == 8, "the third pitch is the second extra");
  expect((snap.rollChord[0][2] & 0x0F) == 12, "the fourth pitch is the third extra");
  expect(std::strcmp(tracker.hint, "Chord 4") == 0, "a fifth note is left out");
  expect(tracker.NoteAt(0, 0) == 1, "the fifth note does not replace the first");

  SongData song;
  tracker.CaptureSong(&song);
  expect(song.tracks[0][0] == 1, "the song stores the first note only");
  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, (int)sizeof(buf));
  expect(n == kSongFileBytes, "a chord does not grow the song file");
  SongData back;
  std::memset(&back, 0, sizeof(back));
  expect(songDecode(buf, n, &back), "the chord song still decodes as version 1");
  Tracker loaded;
  loaded.ApplySong(back);
  std::memset(&snap, 0, sizeof(snap));
  loaded.FillSnap(&snap);
  expect(loaded.NoteAt(0, 0) == 1 && extraCount(snap, 0) == 0, "loading a song keeps the first note and drops extras");

  SongData oldSong;
  std::memset(&oldSong, 0, sizeof(oldSong));
  oldSong.patternLength = 16;
  oldSong.bpms[0] = 100;
  oldSong.tracks[1][4] = 7;
  oldSong.octaves[1][4] = 1;
  oldSong.instruments[1][4] = 3;
  oldSong.voices[0].bend14 = 8192;
  tracker.ApplySong(oldSong);
  tracker.SetCommand('T', 1);
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(tracker.NoteAt(1, 4) == 7 && tracker.OctaveAt(1, 4) == 1, "an older song note loads");
  expect(extraCount(snap, 4) == 0, "an older song leaves every extra empty");
  tracker.SetCommand('T', 0);
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(extraCount(snap, 0) == 0, "loading a song clears extras on the other tracks");
}

static void testRollEdit() {
  Tracker tracker;
  tracker.SetCommand('P', 0);
  tracker.SetCommand('P', 0);
  expect(!tracker.isPlaying && !tracker.recOn, "stopped roll is not recording");

  tracker.SetCommand('r', 2 | (0 << 8) | (1 << 12));
  tracker.SetCommand('r', 2 | (4 << 8) | (1 << 12) | (2 << 16));
  expect(tracker.NoteAt(0, 2) == 1 && tracker.OctaveAt(0, 2) == 1, "the first roll key is the step word");
  expect(tracker.NoteLenAt(0, 2) == 1, "a new extra does not copy the first note's length onto it");

  Snap snap;
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(extraCount(snap, 2) == 1, "a second roll key stacks in the column");
  expect((snap.rollChord[2][0] & 0x0F) == 5, "the extra is E");
  expect(((snap.rollChord[2][0] >> 4) & 3) == 0, "the extra starts as one step");

  int packG = 2 | (4 << 8) | (1 << 12) | (1 << 24);
  tracker.SetCommand('g', packG);
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(tracker.NoteLenAt(0, 2) == 1, "hold on the extra leaves the first note");
  expect(((snap.rollChord[2][0] >> 4) & 3) == 1, "hold on the extra steps from 1 to 2");

  int packE = 2 | (0 << 8) | (1 << 12) | (1 << 24);
  tracker.SetCommand('e', packE);
  expect(tracker.NoteAt(0, 2) == 5, "removing the first note promotes the extra");
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(extraCount(snap, 2) == 0, "the column has one note after the promotion");
  expect(tracker.NoteLenAt(0, 2) == 2, "the promoted note keeps its hold");

  tracker.SetCommand('r', 2 | (7 << 8) | (2 << 12));
  tracker.SetCommand('*', 0);
  tracker.SetCommand('e', 2);
  expect(tracker.NoteAt(0, 2) == 0, "a whole-step clear removes the column");
  tracker.SetCommand('*', 1);
  std::memset(&snap, 0, sizeof(snap));
  tracker.FillSnap(&snap);
  expect(tracker.NoteAt(0, 2) == 5 && extraCount(snap, 2) == 1, "copy and paste keep the extra");

  tracker.SetCommand('P', 0);
  noteOn(tracker, 0, 60);
  int notes = 0;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    notes += tracker.NoteAt(0, s) != 0;
  }
  tracker.SetCommand('p', 0);
  noteOn(tracker, 0, 64);
  int after = 0;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    after += tracker.NoteAt(0, s) != 0;
  }
  expect(notes == after, "rec off does not add a chord note");
}

static void testPlayback() {
  Tracker tracker;
  tracker.SetCommand('I', 2);
  tracker.SetCommand('P', 0);
  noteOn(tracker, 0, 60);
  noteOn(tracker, 0, 64);
  bool heard = false;
  for (int i = 0; i < 8000; i++) {
    tracker.UpdateTracker();
    if (tracker.lastSamples[0] != 0) {
      heard = true;
      break;
    }
  }
  expect(heard, "a synth chord produces samples");

  Tracker drums;
  drums.SetCommand('P', 0);
  noteOn(drums, 0, 60);
  noteOn(drums, 0, 64);
  bool drum = false;
  for (int i = 0; i < 8000; i++) {
    drums.UpdateTracker();
    if (drums.lastSamples[0] != 0) {
      drum = true;
      break;
    }
  }
  expect(drum, "a drum chord produces samples");
}

int main() {
  testStackAndSong();
  testRollEdit();
  testPlayback();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
