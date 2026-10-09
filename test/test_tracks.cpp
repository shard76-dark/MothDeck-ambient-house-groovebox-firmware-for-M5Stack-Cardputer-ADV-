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
    if (tracker.InstAt(track, s) != id) {
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

  expect(!tracker.isPlaying, "rec is off at boot");
  tracker.SetCommand('P', 0);
  expect(tracker.isPlaying, "space starts rec");
  tracker.SetCommand('N', 0);
  expect(tracker.NoteAt(0, 0) == 1 && tracker.InstAt(0, 0) == 14, "recorded note uses track 1");
  tracker.SetCommand('T', 1);
  tracker.SetCommand('N', 3);
  expect(tracker.NoteAt(1, 0) == 4 && tracker.InstAt(1, 0) == 5, "recorded note uses track 2, not the plugin");
  expect(tracker.trackVoice[0] == 14, "recording on track 2 leaves track 1's plugin");

  tracker.SetCommand('P', 0);
  expect(!tracker.isPlaying, "transport stopped");
  tracker.SetCommand('T', 2);
  tracker.SetCommand('N', 2);
  expect(tracker.NoteAt(2, 0) == 0 && tracker.InstAt(2, 0) == 0, "live note does not stamp another track's instrument");
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
  expect(tracker.InstAt(1, 3) == 4 && tracker.InstAt(0, 0) == 0, "load does not paint the editor instrument across the song");
  expect(tracker.NoteAt(1, 3) == 2, "loaded notes stay put");
  expect(tracker.patternLength == 32 && tracker.Bars() == 2, "a saved 32-step pattern stays two bars");
}

static void clocks(Tracker &tracker, int count) {
  for (int i = 0; i < count; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = (uint16_t)((i * 20) & 0x1FFF);
    tracker.HandleMidi(clk);
  }
}

static void transport(Tracker &tracker, MidiMsgType type, int value) {
  MidiEvent ev = {};
  ev.type = type;
  ev.value14 = (uint16_t)value;
  tracker.HandleMidi(ev);
}

static void testBarsAndExternalLoop() {
  Tracker tracker;
  expect(tracker.patternLength == 16 && tracker.Bars() == 1 && tracker.PatternSlots() == 4, "a new pattern is one bar");

  tracker.SetCommand('Y', 8);
  expect(tracker.patternLength == 128 && tracker.Bars() == 8 && tracker.PatternSlots() == 2, "eight bars is 128 steps and two pattern slots");
  tracker.SetCommand('$', 3);
  expect(tracker.currentPattern == 1, "pattern 4 clamps to the last slot that fits");
  tracker.SetCommand('Y', 1);
  expect(tracker.patternLength == 16 && tracker.PatternSlots() == 4, "shrinking back to one bar restores four slots");

  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 16;
  song.bpms[0] = 120;
  song.tracks[0][0] = 5;
  song.octaves[0][0] = -1;
  song.instruments[0][0] = 9;
  song.tracks[0][20] = 3;
  song.voices[0].bend14 = 8192;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 16 && tracker.Bars() == 1 && tracker.PatternSlots() == 4, "a 16-step song loads as one bar with four slots");
  expect(tracker.NoteAt(0, 0) == 5 && tracker.OctaveAt(0, 0) == -1 && tracker.InstAt(0, 0) == 9, "packed step keeps note, octave, and instrument");
  expect(tracker.NoteAt(0, 20) == 3, "steps past the old 16 stay in the grid");
  tracker.SetCommand('Y', 4);
  expect(tracker.patternLength == 64 && tracker.NoteAt(0, 0) == 5 && tracker.NoteAt(0, 20) == 3, "changing length does not wipe steps");
  tracker.SetCommand('Y', 1);
  expect(tracker.NoteAt(0, 20) == 3, "a shorter pattern keeps the hidden steps");

  song.patternLength = 64;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 64 && tracker.Bars() == 4 && tracker.PatternSlots() == 4, "a 64-step song stays four bars and four slots");

  song.patternLength = 96;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 96 && tracker.Bars() == 6 && tracker.PatternSlots() == 2, "a 96-step song keeps six bars");

  song.patternLength = 128;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 128 && tracker.Bars() == 8 && tracker.PatternSlots() == 2, "an 8-bar song stays 128 steps");

  song.patternLength = 256;
  song.tracks[0][200] = 4;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 64 && tracker.Bars() == 4 && tracker.PatternSlots() == 4, "a 256-step file is four patterns of 64, not two of 128");
  expect(tracker.NoteAt(0, 200) == 4, "notes past step 128 stay on the grid");

  song.patternLength = 4;
  tracker.ApplySong(song);
  expect(tracker.patternLength == 16, "a short saved length rounds up to one bar");

  tracker.SetCommand('Y', 1);
  transport(tracker, MIDI_MSG_START, 0);
  expect(tracker.patternLength == 16, "the first Start does not invent a length");
  clocks(tracker, 96 * 4);
  transport(tracker, MIDI_MSG_START, 0);
  expect(tracker.patternLength == 64 && tracker.Bars() == 4, "Start back at zero after four bars adopts four bars");
  expect(tracker.currentPattern == 0, "Start stays on the current pattern");

  tracker.SetCommand('Y', 2);
  transport(tracker, MIDI_MSG_START, 0);
  clocks(tracker, 96 * 4);
  transport(tracker, MIDI_MSG_STOP, 0);
  transport(tracker, MIDI_MSG_START, 0);
  expect(tracker.patternLength == 32, "Stop cancels auto-length, so the next Start keeps two bars");

  tracker.SetCommand('Y', 2);
  transport(tracker, MIDI_MSG_START, 0);
  transport(tracker, MIDI_MSG_SONG_POS, 40);
  expect(tracker.currentPattern == 0 && tracker.trackIndex == 8, "song position is modulo the 32-step pattern");
  clocks(tracker, 96 * 2);
  transport(tracker, MIDI_MSG_SONG_POS, 0);
  expect(tracker.patternLength == 32, "a song position of zero after a locate does not learn");
  clocks(tracker, 96 * 3);
  transport(tracker, MIDI_MSG_SONG_POS, 0);
  expect(tracker.patternLength == 48 && tracker.trackIndex == 0, "the next return to zero learns three bars and restarts the pattern");

  Snap snap;
  std::memset(&snap, 0, sizeof(snap));
  tracker.SetCommand('W', 1);
  tracker.FillSnap(&snap);
  expect(snap.barIndex == 2 && snap.barCount == 3 && snap.barOrigin == 16, "Fn pages the editor to bar 2 of 3");
}

static void testNoteHold() {
  Tracker tracker;
  expect(!tracker.isPlaying, "stopped for roll edits");

  tracker.SetCommand('r', 0 | (0 << 8) | (1 << 12));
  expect(tracker.NoteAt(0, 0) == 1 && tracker.NoteLenAt(0, 0) == 1 && tracker.OctaveAt(0, 0) == 1, "a new roll note is one step");

  tracker.SetCommand('r', 4 | (7 << 8) | (2 << 12) | (2 << 16));
  expect(tracker.NoteAt(0, 4) == 8 && tracker.OctaveAt(0, 4) == 2 && tracker.NoteLenAt(0, 4) == 3, "roll write stores pitch, octave, and hold");
  expect(tracker.InstAt(0, 4) == 0, "roll write uses the track instrument");

  tracker.SetCommand('g', 4);
  expect(tracker.NoteLenAt(0, 4) == 4, "hold steps from 3 to 4");
  tracker.SetCommand('g', 4);
  expect(tracker.NoteLenAt(0, 4) == 1 && tracker.NoteAt(0, 4) == 8, "hold wraps to one step and keeps the pitch");
  tracker.SetCommand('g', 4);
  expect(tracker.NoteLenAt(0, 4) == 2, "hold steps to two");

  tracker.SetCommand('*', 0);
  tracker.SetCommand('e', 4);
  expect(tracker.NoteAt(0, 4) == 0 && tracker.NoteLenAt(0, 4) == 0, "backspace clears the cursor step");
  tracker.SetCommand('*', 1);
  expect(tracker.NoteAt(0, 4) == 8 && tracker.NoteLenAt(0, 4) == 2, "copy and paste keep the hold");

  tracker.SetCommand('I', 9);
  expect(tracker.NoteLenAt(0, 4) == 2 && tracker.InstAt(0, 4) == 9 && tracker.NoteAt(0, 4) == 8, "changing instrument keeps the hold");
  expect(tracker.NoteLenAt(0, 0) == 1 && tracker.InstAt(0, 0) == 9, "a one-step note stays one step");

  SongData song;
  tracker.CaptureSong(&song);
  expect(song.tracks[0][0] == 1, "a one-step note is stored as the pitch byte");
  expect(song.tracks[0][4] == (uint8_t)(8 | (1 << 4)), "a two-step note stores the length code in bits 4-5");

  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, (int)sizeof(buf));
  expect(n == kSongFileBytes, "a builtin song with a hold stays version 1");
  SongData back;
  std::memset(&back, 0, sizeof(back));
  expect(songDecode(buf, n, &back), "song with a hold decodes");
  Tracker loaded;
  loaded.ApplySong(back);
  expect(loaded.NoteAt(0, 0) == 1 && loaded.NoteLenAt(0, 0) == 1, "a one-step note round-trips");
  expect(loaded.NoteAt(0, 4) == 8 && loaded.NoteLenAt(0, 4) == 2 && loaded.OctaveAt(0, 4) == 2, "a held note round-trips");

  SongData ext;
  std::memset(&ext, 0, sizeof(ext));
  ext.patternLength = 64;
  ext.bpms[0] = 120;
  ext.tracks[0][3] = (uint8_t)(5 | (3 << 4));
  ext.octaves[0][3] = 0;
  ext.instruments[0][3] = 4;
  ext.voices[0].bend14 = 8192;
  loaded.ApplySong(ext);
  expect(loaded.patternLength == 64, "the hold test keeps a 64-step pattern");
  expect(loaded.NoteAt(0, 3) == 5 && loaded.NoteLenAt(0, 3) == 4 && loaded.InstAt(0, 3) == 4, "a length nibble above 15 loads as a four-step hold");

  ext.tracks[0][3] = 5;
  loaded.ApplySong(ext);
  expect(loaded.NoteAt(0, 3) == 5 && loaded.NoteLenAt(0, 3) == 1, "a plain note byte is still one step");

  loaded.SetCommand('Y', 4);
  loaded.SetCommand('r', 20 | (2 << 8) | (1 << 12) | (1 << 16));
  Snap snap;
  std::memset(&snap, 0, sizeof(snap));
  loaded.FillSnap(&snap);
  expect(snap.rollCount == 64, "the roll image covers the whole pattern");
  uint8_t cell = snap.roll[20];
  expect((cell & 0x0F) == 3 && ((cell >> 4) & 3) == 1 && ((cell >> 6) & 3) == 1, "snap roll packs note, hold, and octave");
  expect(snap.roll[0] == 0 && snap.roll[3] == (uint8_t)(5 | (0 << 6)), "empty steps stay empty and the one-step note is packed");
}

int main() {
  testAssignStaysOnTrack();
  testMidiBankStaysOnChannel();
  testSongRecallDoesNotSmear();
  testBarsAndExternalLoop();
  testNoteHold();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
