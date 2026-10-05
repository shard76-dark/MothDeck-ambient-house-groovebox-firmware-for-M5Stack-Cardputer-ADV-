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

static bool stepsAre(const Tracker &tracker, int track, uint8_t id) {
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    if (tracker.trackInstruments[track][s] != id) {
      return false;
    }
  }
  return true;
}

static void testAssignStaysOnTrack() {
  Tracker tracker;
  expect(tracker.selectedTrack == 0 && tracker.currentVoice == 0, "boot track is drums");
  expect(tracker.trackVoice[0] == 0 && tracker.trackVoice[1] == 0, "every track starts on drums");

  tracker.SetCommand('I', 14);
  expect(tracker.trackVoice[0] == 14 && tracker.currentVoice == 14, "plugin assigns to track 1");
  expect(std::strcmp(tracker.oledInstString, "PLG14") == 0, "label is the plugin");
  expect(stepsAre(tracker, 0, 14), "track 1 steps take the plugin");
  expect(stepsAre(tracker, 1, 0) && stepsAre(tracker, 2, 0) && stepsAre(tracker, 3, 0), "other tracks stay on drums");

  tracker.SetCommand('T', 1);
  expect(tracker.selectedTrack == 1 && tracker.currentVoice == 0, "track 2 recalls drums");
  expect(std::strcmp(tracker.oledInstString, "DRUM") == 0, "label follows the selected track");
  expect(tracker.trackVoice[0] == 14 && tracker.trackVoice[1] == 0, "plugin remains on track 1");
  expect(stepsAre(tracker, 0, 14) && stepsAre(tracker, 1, 0), "track select does not copy instruments");

  tracker.SetCommand('I', 5);
  expect(tracker.trackVoice[1] == 5 && tracker.trackVoice[0] == 14, "saw assigns to track 2 only");
  expect(stepsAre(tracker, 1, 5) && stepsAre(tracker, 0, 14), "saw fills track 2 steps only");

  tracker.SetCommand('$', 2);
  expect(tracker.currentPattern == 2 && tracker.currentVoice == 5, "pattern select keeps the track instrument");
  expect(tracker.trackVoice[0] == 14 && tracker.trackVoice[1] == 5, "pattern select does not move instruments");

  tracker.SetCommand('T', 0);
  expect(tracker.currentVoice == 14 && tracker.trackVoice[1] == 5, "returning to track 1 recalls the plugin");

  tracker.SetCommand('N', 0);
  expect(tracker.tracks[0][0] == 1 && tracker.trackInstruments[0][0] == 14, "recorded note uses track 1");
  tracker.SetCommand('T', 1);
  tracker.SetCommand('N', 3);
  expect(tracker.tracks[1][0] == 4 && tracker.trackInstruments[1][0] == 5, "recorded note uses track 2, not the plugin");
  expect(tracker.trackVoice[0] == 14, "recording on track 2 leaves track 1's plugin");

  tracker.SetCommand('P', 0);
  expect(!tracker.isPlaying, "transport stopped");
  tracker.SetCommand('T', 2);
  tracker.SetCommand('N', 2);
  expect(tracker.tracks[2][0] == 0 && tracker.trackInstruments[2][0] == 0, "live note does not stamp another track's instrument");
  expect(tracker.currentVoice == 0 && tracker.trackVoice[0] == 14, "stopped track 3 stays on drums");

  tracker.SetCommand('I', 0);
  expect(tracker.trackVoice[2] == 0 && stepsAre(tracker, 2, 0), "drums bank assigns to the selected track");
  expect(tracker.trackVoice[0] == 14 && tracker.trackVoice[1] == 5, "drums assign leaves the other tracks");
}

static void testMidiBankStaysOnChannel() {
  Tracker tracker;
  tracker.SetCommand('I', 14);
  tracker.SetCommand('T', 1);
  tracker.SetCommand('I', 5);

  MidiEvent bank = {};
  bank.type = MIDI_MSG_MSB;
  bank.channel = 2;
  bank.number = 0;
  bank.value = 3;
  tracker.HandleMidi(bank);
  expect(tracker.selectedTrack == 2 && tracker.trackVoice[2] == 3, "bank selects the channel's track");
  expect(tracker.trackVoice[0] == 14 && tracker.trackVoice[1] == 5, "bank does not carry onto other tracks");
  expect(stepsAre(tracker, 2, 3) && stepsAre(tracker, 0, 14), "bank writes only that track's steps");
}

static void testSongRecallDoesNotSmear() {
  Tracker tracker;
  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 32;
  song.bpms[0] = 120;
  song.currentVoice = 9;
  song.selectedTrack = 0;
  song.tracks[1][3] = 2;
  song.instruments[1][3] = 4;
  song.tracks[2][5] = 3;
  song.instruments[2][5] = 14;
  for (int t = 0; t < 4; t++) {
    song.voices[t].volume = 2;
    song.voices[t].octave = 1;
    song.voices[t].bend14 = 8192;
  }
  tracker.ApplySong(song);
  expect(tracker.trackVoice[0] == 9 && tracker.currentVoice == 9, "empty selected track keeps its editor instrument");
  expect(tracker.trackVoice[1] == 4 && tracker.trackVoice[2] == 14 && tracker.trackVoice[3] == 0, "other tracks keep their own instruments");
  expect(tracker.trackInstruments[1][3] == 4 && tracker.trackInstruments[0][0] == 0, "load does not paint the editor instrument across the song");
  expect(tracker.tracks[1][3] == 2, "loaded notes stay put");
}

int main() {
  testAssignStaysOnTrack();
  testMidiBankStaysOnChannel();
  testSongRecallDoesNotSmear();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
