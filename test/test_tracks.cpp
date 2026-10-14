#include "MidiRing.h"
#include "Tracker.h"
#include "InstrumentBank.h"
#include "SongFile.h"
#include "MidiProtocol.h"
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

static bool gridEmpty(const Tracker &tracker) {
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < Tracker::kMaxSteps; s++) {
      if (tracker.NoteAt(t, s) != 0) {
        return false;
      }
    }
  }
  return true;
}

static void noteOn(Tracker &tracker, int channel, int note) {
  MidiEvent ev = {};
  ev.type = MIDI_MSG_NOTE_ON;
  ev.channel = (uint8_t)channel;
  ev.number = (uint8_t)note;
  ev.value = 100;
  tracker.HandleMidi(ev);
}

static void testMidiNotesRespectRec() {
  Tracker live;
  expect(!live.isPlaying && !live.recOn, "rec is off at boot");
  int head = live.trackIndex;
  noteOn(live, 0, 60);
  noteOn(live, 2, 64);
  expect(gridEmpty(live), "midi notes with rec off do not write the pattern");
  expect(live.trackIndex == head && !live.pressedOnce && !live.recOn, "play-through does not arm rec or rewind");

  transport(live, MIDI_MSG_START, 0);
  expect(live.isPlaying && !live.recOn, "midi start plays and leaves rec off");
  head = live.trackIndex;
  noteOn(live, 0, 60);
  clocks(live, 6);
  noteOn(live, 1, 67);
  expect(gridEmpty(live), "notes during midi play stay out of the pattern");
  expect(!live.recOn, "those notes still do not arm rec");

  live.SetCommand('P', 0);
  expect(!live.recOn && !live.isPlaying, "space stops the midi transport and leaves rec off");
  live.SetCommand('P', 0);
  expect(live.recOn && live.isPlaying, "space turns rec on");
  head = live.trackIndex;
  noteOn(live, 0, 60);
  int sounded = head > 0 ? head - 1 : 0;
  expect(live.NoteAt(0, sounded) == 1 && live.OctaveAt(0, sounded) == 2, "rec on writes the step that just played");
  expect(live.NoteAt(0, head) == 0, "rec does not write the step the playhead has already left");
  noteOn(live, 3, 62);
  expect(live.NoteAt(3, sounded) == 3, "rec on writes the channel's track");

  transport(live, MIDI_MSG_STOP, 0);
  expect(!live.isPlaying && live.recOn, "midi stop pauses transport and keeps rec");
  int kept = 0;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    kept += live.NoteAt(0, s) != 0;
  }
  noteOn(live, 0, 72);
  int after = 0;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    after += live.NoteAt(0, s) != 0;
  }
  expect(after == kept, "a note while stopped does not write, even if rec was on");

  transport(live, MIDI_MSG_START, 0);
  expect(live.recOn && live.isPlaying, "midi start keeps rec when it was already on");
  head = live.trackIndex;
  noteOn(live, 0, 60);
  sounded = head > 0 ? head - 1 : 0;
  expect(live.NoteAt(0, sounded) == 1, "rec stays on across midi stop and start");

  live.SetCommand('P', 0);
  expect(!live.recOn && !live.isPlaying, "space turns rec off");
  int notes = 0;
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < Tracker::kMaxSteps; s++) {
      notes += live.NoteAt(t, s) != 0;
    }
  }
  noteOn(live, 0, 65);
  int notesAfter = 0;
  for (int t = 0; t < 4; t++) {
    for (int s = 0; s < Tracker::kMaxSteps; s++) {
      notesAfter += live.NoteAt(t, s) != 0;
    }
  }
  expect(notesAfter == notes, "rec off leaves the pattern unchanged");

  Tracker keys;
  keys.SetCommand('P', 0);
  keys.SetCommand('N', 0);
  expect(keys.NoteAt(0, 0) == 1, "the keyboard still records with rec on");
  keys.SetCommand('p', 0);
  expect(!keys.recOn && !keys.isPlaying, "stopping clears rec");
  keys.SetCommand('N', 5);
  expect(keys.NoteAt(0, 0) == 1, "a key with rec off does not write");

  Tracker roll;
  transport(roll, MIDI_MSG_START, 0);
  roll.SetCommand('r', 3 | (4 << 8) | (1 << 12));
  expect(roll.NoteAt(0, 3) == 0, "a roll key does not write while midi is playing and rec is off");
  transport(roll, MIDI_MSG_STOP, 0);
  roll.SetCommand('r', 3 | (4 << 8) | (1 << 12));
  expect(roll.NoteAt(0, 3) == 5 && roll.OctaveAt(0, 3) == 1, "a stopped roll key still writes the cursor");

  Tracker held;
  held.voices[0].block.arpMode = 3;
  held.SetCommand('P', 0);
  noteOn(held, 0, 60);
  noteOn(held, 0, 64);
  expect(gridEmpty(held), "held arp does not stamp steps when rec is on");
}

static uint16_t clockMillis(int index, int bpm) {
  uint32_t ms = (uint32_t)index * 60000u / (uint32_t)(bpm * 24);
  return (uint16_t)(ms & 0x1FFF);
}

static int pushClockByte(uint8_t *buf, int n, uint16_t ts) {
  // A timestamp low byte of 0xF8..0xFF is a real-time status, so clamp it.
  // The clock byte still follows, and the parser has to count that clock.
  uint8_t bits = (uint8_t)(ts & 0x7F);
  if (bits >= 0x78) {
    bits = 0x77;
  }
  buf[n++] = (uint8_t)(0x80 | bits);
  buf[n++] = 0xF8;
  return n;
}

static bool feedClockPackets(Tracker &tracker, int bpm, int total, int perPacket, bool ownStamp) {
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  int sent = 0;
  bool kept = true;
  while (sent < total) {
    int batch = perPacket;
    if (batch > total - sent) {
      batch = total - sent;
    }
    uint8_t buf[96];
    uint16_t ts0 = clockMillis(sent, bpm);
    int n = 0;
    buf[n++] = (uint8_t)(0x80 | ((ts0 >> 7) & 0x3F));
    if (!ownStamp) {
      uint8_t bits = (uint8_t)(ts0 & 0x7F);
      if (bits >= 0x78) {
        bits = 0x77;
      }
      buf[n++] = (uint8_t)(0x80 | bits);
      for (int i = 0; i < batch; i++) {
        buf[n++] = 0xF8;
      }
    } else {
      for (int i = 0; i < batch; i++) {
        n = pushClockByte(buf, n, clockMillis(sent + i, bpm));
      }
    }
    MidiParseResult parsed;
    parseBleMidiPacket(buf, n, &state, &parsed);
    int clocksFound = 0;
    for (int i = 0; i < parsed.count; i++) {
      if (parsed.events[i].type == MIDI_MSG_CLOCK) {
        clocksFound += parsed.events[i].value == 0 ? 1 : (int)parsed.events[i].value;
      }
      tracker.HandleMidi(parsed.events[i]);
    }
    if (clocksFound != batch || parsed.overflow != 0) {
      kept = false;
    }
    sent += batch;
  }
  return kept;
}

static int renderEnergy(Tracker &tracker, int samples) {
  int acc = 0;
  for (int i = 0; i < samples; i++) {
    int s = tracker.UpdateTracker();
    if (s < 0) {
      s = -s;
    }
    acc += s;
  }
  return acc;
}

static void noteOff(Tracker &tracker, int channel, int note) {
  MidiEvent ev = {};
  ev.type = MIDI_MSG_NOTE_OFF;
  ev.channel = (uint8_t)channel;
  ev.number = (uint8_t)note;
  tracker.HandleMidi(ev);
}

static void nearBpm(Tracker &tracker, int bpm) {
  if (bpm < 40) {
    bpm = 40;
  } else if (bpm > 240) {
    bpm = 240;
  }
  tracker.NudgeBpm(bpm - tracker.Bpm());
}

static void testExternalClockPackets() {
  const int rates[] = {60, 120, 174};
  const int groups[] = {2, 3, 4};
  for (int r = 0; r < 3; r++) {
    int bpm = rates[r];
    Tracker shared;
    if (bpm < 90) {
      nearBpm(shared, bpm);
    }
    transport(shared, MIDI_MSG_START, 0);
    int head = shared.trackIndex;
    bool kept = feedClockPackets(shared, bpm, 30, groups[r], false);
    expect(kept, "shared-timestamp clocks in one packet are all counted");
    expect(shared.trackIndex == head + 5, "30 clocks move five 16ths");
    int shown = shared.Bpm();
    expect(shown >= bpm - 3 && shown <= bpm + 3, "shared-timestamp packet tempo stays on the clock");

    Tracker stamped;
    if (bpm < 90) {
      nearBpm(stamped, bpm);
    }
    transport(stamped, MIDI_MSG_START, 0);
    head = stamped.trackIndex;
    kept = feedClockPackets(stamped, bpm, 30, groups[r], true);
    expect(kept, "timestamped clocks in one packet are all counted");
    expect(stamped.trackIndex == head + 5, "timestamped clocks move five 16ths");
    shown = stamped.Bpm();
    expect(shown >= bpm - 3 && shown <= bpm + 3, "timestamped packet tempo stays on the clock");
  }
}

static void testMidiPolyphony() {
  Tracker chord;
  chord.SetCommand('I', 2);
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  const uint8_t packet[] = {
    0x80, 0x80, 0xF8, 0x81, 0xF8, 0x82, 0xF8, 0x83, 0xF8, 0x84, 0xF8, 0x85, 0xF8,
    0x86, 0x90, 60, 100,
    0x87, 0x90, 64, 110,
    0x88, 0x90, 67, 90
  };
  MidiParseResult parsed;
  int count = parseBleMidiPacket(packet, (int)sizeof(packet), &state, &parsed);
  expect(count == 9 && parsed.overflow == 0, "a packet keeps six clocks and a three-note chord");
  transport(chord, MIDI_MSG_START, 0);
  int head = chord.trackIndex;
  for (int i = 0; i < parsed.count; i++) {
    chord.HandleMidi(parsed.events[i]);
  }
  expect(chord.trackIndex == head + 1, "six clocks in the chord packet advance one step");
  expect(chord.LiveHeld(0) == 3, "three notes in one packet all sound");
  expect(chord.LiveDown(0, 60) && chord.LiveDown(0, 64) && chord.LiveDown(0, 67), "the chord notes are the ones that were sent");
  expect(gridEmpty(chord), "play-through chord does not write the pattern");
  expect(renderEnergy(chord, 64) > 0, "the chord produces samples");
  const uint8_t bare[] = {0x80, 0xF8, 0xF8, 0xF8};
  MidiParseResult bareParsed;
  std::memset(&state, 0, sizeof(state));
  parseBleMidiPacket(bare, (int)sizeof(bare), &state, &bareParsed);
  int bareTicks = 0;
  for (int i = 0; i < bareParsed.count; i++) {
    if (bareParsed.events[i].type == MIDI_MSG_CLOCK) {
      bareTicks += bareParsed.events[i].value == 0 ? 1 : (int)bareParsed.events[i].value;
    }
  }
  expect(bareTicks == 3 && bareParsed.overflow == 0, "clocks with no extra timestamp byte are all counted");
  noteOff(chord, 0, 64);
  expect(!chord.LiveDown(0, 64) && chord.LiveDown(0, 60) && chord.LiveDown(0, 67), "note-off releases only that note");
  expect(chord.LiveHeld(0) == 2, "the other two notes stay");
  expect(renderEnergy(chord, 32) > 0, "the remaining notes still sound");

  noteOn(chord, 0, 60);
  noteOff(chord, 0, 60);
  expect(!chord.LiveDown(0, 60) && chord.LiveDown(0, 67), "retrigger note-off does not clear a different note");

  Tracker steal;
  steal.SetCommand('I', 2);
  const int chordNotes[] = {60, 64, 67, 70, 72};
  for (int i = 0; i < 5; i++) {
    noteOn(steal, 0, chordNotes[i]);
  }
  expect(steal.LiveHeld(0) == 4, "a fifth note steals the oldest voice");
  expect(!steal.LiveDown(0, 60) && steal.LiveDown(0, 72), "the oldest note is the one replaced");
  expect(steal.LiveDown(0, 64) && steal.LiveDown(0, 67) && steal.LiveDown(0, 70), "the newer notes stay");
  noteOff(steal, 0, 60);
  expect(steal.LiveHeld(0) == 4 && steal.LiveDown(0, 72), "note-off of a stolen note does not kill the new one");
  noteOff(steal, 0, 64);
  expect(!steal.LiveDown(0, 64) && steal.LiveHeld(0) == 3 && steal.LiveDown(0, 72), "note-off matches the note that is still sounding");

  Tracker repeat;
  repeat.SetCommand('I', 2);
  uint8_t fast[64];
  int n = 0;
  fast[n++] = 0x80;
  const int fastNotes[] = {60, 62, 64, 65, 67, 69};
  for (int i = 0; i < 6; i++) {
    fast[n++] = (uint8_t)(0x81 + i);
    fast[n++] = 0x90;
    fast[n++] = (uint8_t)fastNotes[i];
    fast[n++] = 100;
  }
  std::memset(&state, 0, sizeof(state));
  count = parseBleMidiPacket(fast, n, &state, &parsed);
  expect(count == 6 && parsed.overflow == 0, "six note-ons in one packet are kept");
  for (int i = 0; i < parsed.count; i++) {
    repeat.HandleMidi(parsed.events[i]);
  }
  expect(repeat.LiveHeld(0) == 4, "notes in one audio packet layer up to the voice limit");
  expect(!repeat.LiveDown(0, 60) && !repeat.LiveDown(0, 62), "the oldest repeats are the ones stolen");
  expect(repeat.LiveDown(0, 65) && repeat.LiveDown(0, 67) && repeat.LiveDown(0, 69), "the latest repeats are sounding");

  Tracker vel;
  vel.SetCommand('I', 2);
  const uint8_t vel0[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 0x90, 60, 0, 0x82, 0x90, 64, 80};
  std::memset(&state, 0, sizeof(state));
  count = parseBleMidiPacket(vel0, (int)sizeof(vel0), &state, &parsed);
  for (int i = 0; i < parsed.count; i++) {
    vel.HandleMidi(parsed.events[i]);
  }
  expect(!vel.LiveDown(0, 60) && vel.LiveDown(0, 64), "velocity 0 is note-off and does not clear the other note");

  Tracker drums;
  noteOn(drums, 0, 36);
  noteOn(drums, 0, 38);
  noteOn(drums, 0, 42);
  expect(drums.LiveHeld(0) == 3, "drum hits from one chord stay layered");
  expect(renderEnergy(drums, 32) > 0, "layered drums produce samples");
  noteOff(drums, 0, 38);
  expect(!drums.LiveDown(0, 38) && drums.LiveHeld(0) == 2, "drum note-off stops only that hit");
  expect(renderEnergy(drums, 16) > 0, "the other drum hits keep ringing");
}

static void testNoteOffFindsTrack() {
  Tracker routed;
  routed.SetCommand('I', 2);
  noteOn(routed, 5, 60);
  routed.SetCommand('T', 2);
  expect(routed.selectedTrack == 2, "the keyboard can leave the track that sounded");
  expect(routed.LiveDown(1, 60), "channel 6 sounds on track 2");
  noteOff(routed, 5, 60);
  expect(!routed.LiveDown(1, 60), "note-off finds the note on its original track");
  expect(routed.selectedTrack == 2, "note-off does not change the selected track");

  noteOn(routed, 0, 62);
  noteOn(routed, 1, 64);
  expect(routed.selectedTrack == 1, "a note on channel 2 selects track 2");
  noteOff(routed, 0, 62);
  expect(!routed.LiveDown(0, 62) && routed.LiveDown(1, 64), "the other channel's note stays");
  expect(routed.selectedTrack == 1, "that note-off still leaves the selection");

  MidiEvent all = {};
  all.type = MIDI_MSG_ALL_OFF;
  all.channel = 1;
  all.number = 123;
  routed.HandleMidi(all);
  expect(!routed.LiveDown(1, 64), "all notes off releases that channel");
  noteOn(routed, 0, 67);
  noteOn(routed, 2, 69);
  all.channel = 0;
  routed.HandleMidi(all);
  expect(!routed.LiveDown(0, 67) && routed.LiveDown(2, 69), "all notes off does not clear another channel");
}

static void testStopReleasesLiveOnly() {
  Tracker live;
  live.SetCommand('I', 2);
  noteOn(live, 0, 60);
  live.voices[0].bend14 = 1000;
  live.loopPlay[0].enabled = 1;
  live.SetCommand('r', 0 | (4 << 8) | (1 << 12));
  expect(live.NoteAt(0, 0) == 5, "the pattern has a note before stop");
  expect(live.LiveDown(0, 60), "the live note is sounding");
  transport(live, MIDI_MSG_STOP, 0);
  expect(!live.LiveDown(0, 60) && live.LiveHeld(0) == 0, "stop releases live notes");
  expect(live.loopPlay[0].enabled == 1, "stop leaves the loop running");
  expect(live.NoteAt(0, 0) == 5, "stop leaves the pattern");
  expect(live.voices[0].bend14 == 8192, "stop centers bend");
  expect(!live.isPlaying && live.recOn == false, "stop still pauses transport");
}

static void testContinuePhase() {
  Tracker phase;
  transport(phase, MIDI_MSG_START, 0);
  clocks(phase, 3);
  int idx = phase.trackIndex;
  transport(phase, MIDI_MSG_CONTINUE, 0);
  clocks(phase, 3);
  expect(phase.trackIndex == idx + 1, "continue keeps clocks already counted in the step");

  Tracker located;
  located.SetCommand('Y', 2);
  transport(located, MIDI_MSG_START, 0);
  transport(located, MIDI_MSG_SONG_POS, 40);
  expect(located.trackIndex == 8, "song position still lands inside the pattern");
  clocks(located, 2);
  idx = located.trackIndex;
  transport(located, MIDI_MSG_CONTINUE, 0);
  expect(located.trackIndex == idx, "continue without a new position stays on the song position");
  clocks(located, 4);
  expect(located.trackIndex == idx + 1, "clocks between song position and continue still count");

  Tracker stay;
  transport(stay, MIDI_MSG_START, 0);
  clocks(stay, 6);
  idx = stay.trackIndex;
  transport(stay, MIDI_MSG_CONTINUE, 0);
  expect(stay.trackIndex == idx, "continue with no song position keeps the current step");
}

static void testSafetyHold() {
  Tracker held;
  held.SetCommand('I', 2);
  held.SetCommand('Q', 4);
  noteOn(held, 0, 60);
  for (int i = 0; i < 1000; i++) {
    held.UpdateTracker();
  }
  expect(held.LiveDown(0, 60) && held.SafetyReleases() == 0, "a short hold does not release the note");
  for (int i = 0; i < 40000 && held.LiveDown(0, 60); i++) {
    held.UpdateTracker();
  }
  expect(!held.LiveDown(0, 60) && held.SafetyReleases() == 1, "the tonal note releases after the safety length");

  Tracker arp;
  arp.voices[0].block.arpMode = 3;
  arp.SetCommand('Q', 4);
  noteOn(arp, 0, 60);
  for (int i = 0; i < 40000; i++) {
    arp.UpdateTracker();
  }
  expect(arp.SafetyReleases() == 0, "held arp is not timed out");
}

static bool feedClockPacketsRing(Tracker &tracker, int bpm, int total, int perPacket, bool ownStamp) {
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  MidiRing ring;
  int sent = 0;
  bool kept = true;
  while (sent < total) {
    int batch = perPacket;
    if (batch > total - sent) {
      batch = total - sent;
    }
    uint8_t buf[96];
    uint16_t ts0 = clockMillis(sent, bpm);
    int n = 0;
    buf[n++] = (uint8_t)(0x80 | ((ts0 >> 7) & 0x3F));
    if (!ownStamp) {
      uint8_t bits = (uint8_t)(ts0 & 0x7F);
      if (bits >= 0x78) {
        bits = 0x77;
      }
      buf[n++] = (uint8_t)(0x80 | bits);
      for (int i = 0; i < batch; i++) {
        buf[n++] = 0xF8;
      }
    } else {
      for (int i = 0; i < batch; i++) {
        n = pushClockByte(buf, n, clockMillis(sent + i, bpm));
      }
    }
    MidiParseResult parsed;
    parseBleMidiPacket(buf, n, &state, &parsed);
    int clocksFound = 0;
    for (int i = 0; i < parsed.count; i++) {
      if (parsed.events[i].type == MIDI_MSG_CLOCK) {
        clocksFound += parsed.events[i].value == 0 ? 1 : (int)parsed.events[i].value;
      }
    }
    if (clocksFound != batch || parsed.overflow != 0) {
      kept = false;
    }
    ring.PushParsed(parsed.events, parsed.count);
    MidiEvent out[16];
    int ticks = 0;
    for (int guard = 0; guard < 8; guard++) {
      int got = ring.Poll(out, 16);
      if (got <= 0) {
        break;
      }
      for (int i = 0; i < got; i++) {
        if (out[i].type == MIDI_MSG_CLOCK) {
          ticks += out[i].value == 0 ? 1 : (int)out[i].value;
        }
        tracker.HandleMidi(out[i]);
      }
    }
    if (ticks != batch) {
      kept = false;
    }
    sent += batch;
  }
  return kept;
}

static void testRingClockTempo() {
  Tracker shared;
  transport(shared, MIDI_MSG_START, 0);
  int head = shared.trackIndex;
  bool kept = feedClockPacketsRing(shared, 120, 30, 3, false);
  expect(kept, "shared-timestamp clocks pass through the counter");
  expect(shared.trackIndex == head + 5, "counted clocks still move five 16ths");
  int shown = shared.Bpm();
  expect(shown >= 117 && shown <= 123, "shared-timestamp tempo survives the counter");

  Tracker burst;
  transport(burst, MIDI_MSG_START, 0);
  head = burst.trackIndex;
  kept = feedClockPacketsRing(burst, 200, 48, 6, true);
  expect(kept, "a 200 BPM burst passes through the counter");
  expect(burst.trackIndex == head + 8, "48 counted clocks move eight 16ths");
  shown = burst.Bpm();
  expect(shown >= 197 && shown <= 200, "a 200 BPM burst stays on the clock");

  Tracker phase;
  transport(phase, MIDI_MSG_START, 0);
  MidiRing ring;
  MidiEvent clocksOnly[3];
  for (int i = 0; i < 3; i++) {
    clocksOnly[i].type = MIDI_MSG_CLOCK;
    clocksOnly[i].channel = 0;
    clocksOnly[i].number = 0;
    clocksOnly[i].value = 0;
    clocksOnly[i].value14 = (uint16_t)(i + 1);
  }
  ring.PushParsed(clocksOnly, 3);
  MidiEvent cont = {};
  cont.type = MIDI_MSG_CONTINUE;
  ring.Push(cont);
  MidiEvent out[8];
  int n = ring.Poll(out, 8);
  int before = 0;
  bool seenCont = false;
  bool clocksAfter = false;
  for (int i = 0; i < n; i++) {
    if (out[i].type == MIDI_MSG_CLOCK) {
      int ticks = out[i].value == 0 ? 1 : (int)out[i].value;
      if (seenCont) {
        clocksAfter = true;
      } else {
        before += ticks;
      }
    } else if (out[i].type == MIDI_MSG_CONTINUE) {
      seenCont = true;
    }
    phase.HandleMidi(out[i]);
  }
  expect(before == 3 && seenCont && !clocksAfter, "clocks stay ahead of continue");
  int idx = phase.trackIndex;
  clocks(phase, 3);
  expect(phase.trackIndex == idx + 1, "continue leaves the clocks that arrived before it");
}

static void testQuantizeAndGate() {
  Tracker early;
  early.SetCommand('P', 0);
  transport(early, MIDI_MSG_START, 0);
  noteOn(early, 0, 60);
  expect(early.NoteAt(0, 0) == 1, "a note as the step starts lands on that step");
  expect(early.NoteAt(0, 1) == 0, "it does not land a step late");
  clocks(early, 3);
  noteOn(early, 0, 64);
  expect(early.NoteAt(0, 1) == 5, "three of six clocks round to the next step");

  Tracker wrap;
  wrap.SetCommand('P', 0);
  wrap.allPatternPlay = true;
  transport(wrap, MIDI_MSG_START, 0);
  clocks(wrap, 14 * 6);
  clocks(wrap, 6);
  expect(wrap.currentPattern == 1 && wrap.trackIndex == 16, "song mode has entered the next pattern");
  noteOn(wrap, 0, 60);
  expect(wrap.NoteAt(0, 15) == 1, "the downbeat stays on the step that just played");
  expect(wrap.NoteAt(0, 16) == 0, "that note does not write the next pattern");
  clocks(wrap, 3);
  noteOn(wrap, 0, 62);
  expect(wrap.NoteAt(0, 0) == 3, "a late note at the loop end wraps to step 0 of the same pattern");
  expect(wrap.NoteAt(0, 16) == 0, "the late note does not write the next pattern");

  Tracker gate;
  gate.SetCommand('P', 0);
  transport(gate, MIDI_MSG_START, 0);
  noteOn(gate, 0, 60);
  noteOff(gate, 0, 60);
  expect(gate.NoteLenAt(0, 0) == 1, "a note-off on the same clocks is one step");
  noteOn(gate, 0, 64);
  clocks(gate, 9);
  noteOff(gate, 0, 64);
  expect(gate.NoteLenAt(0, 0) == 2, "nine clocks round to a two-step gate");
  noteOn(gate, 1, 67);
  clocks(gate, 21);
  noteOff(gate, 1, 67);
  int heldStep = -1;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    if (gate.NoteAt(1, s) == 8) {
      heldStep = s;
    }
  }
  expect(heldStep >= 0 && gate.NoteLenAt(1, heldStep) == 4, "twenty-one clocks round to four steps");
  noteOn(gate, 2, 69);
  clocks(gate, 40);
  noteOff(gate, 2, 69);
  int longStep = -1;
  for (int s = 0; s < Tracker::kMaxSteps; s++) {
    if (gate.NoteAt(2, s) == 10) {
      longStep = s;
    }
  }
  expect(longStep >= 0 && gate.NoteLenAt(2, longStep) == 4, "a longer hold stays inside the two length bits");

  SongData saved;
  gate.CaptureSong(&saved);
  expect((saved.tracks[0][0] & 0x0F) == 5, "the gate pitch stays in the note nibble");
  expect(((saved.tracks[0][0] >> 4) & 3) == 1, "the two-step gate uses the existing length bits");
  uint8_t buf[4096];
  int n = songEncode(saved, buf, (int)sizeof(buf));
  expect(n == kSongFileBytes, "gate length does not change the song version");

  Tracker off;
  noteOn(off, 0, 60);
  clocks(off, 12);
  noteOff(off, 0, 60);
  expect(gridEmpty(off), "rec off still does not write a gate");

  Tracker internal;
  internal.SetCommand('P', 0);
  internal.SetCommand('N', 0);
  for (int i = 0; i < 2757; i++) {
    internal.UpdateTracker();
  }
  expect(internal.trackIndex == 0, "half an internal step has not moved the playhead");
  noteOn(internal, 0, 64);
  expect(internal.NoteAt(0, 1) == 5, "past halfway on the internal clock rounds forward");
  expect(internal.NoteAt(0, 0) == 1, "the note from the start of the step stays put");
}

static void testExternalBpmClamp() {
  Tracker fast;
  transport(fast, MIDI_MSG_START, 0);
  for (int i = 0; i < 48; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i, 240);
    fast.HandleMidi(clk);
  }
  expect(fast.Bpm() == 200, "an external clock above 200 clamps to 200");

  Tracker mid;
  transport(mid, MIDI_MSG_START, 0);
  for (int i = 0; i < 48; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i, 180);
    mid.HandleMidi(clk);
  }
  int shown = mid.Bpm();
  expect(shown >= 177 && shown <= 183, "180 stays inside the clamp");
}

static int eventTicks(const MidiEvent &event) {
  if (event.type != MIDI_MSG_CLOCK) {
    return 0;
  }
  return event.value == 0 ? 1 : (int)event.value;
}

static void handlePolled(Tracker &tracker, MidiRing &ring, int maxOut, int *noteOns, int *noteOffs) {
  MidiEvent out[16];
  if (maxOut > 16) {
    maxOut = 16;
  }
  int n = ring.Poll(out, maxOut);
  for (int i = 0; i < n; i++) {
    if (noteOns && out[i].type == MIDI_MSG_NOTE_ON) {
      (*noteOns)++;
    }
    if (noteOffs && out[i].type == MIDI_MSG_NOTE_OFF) {
      (*noteOffs)++;
    }
    tracker.HandleMidi(out[i]);
  }
}

static void testTempoSweep() {
  const int rates[] = {40, 120, 200};
  const int gaps[] = {15, 60};
  for (int r = 0; r < 3; r++) {
    for (int g = 0; g < 2; g++) {
      int bpm = rates[r];
      int gap = gaps[g];
      int per = (gap * bpm * 24 + 30000) / 60000;
      if (per < 1) {
        per = 1;
      }
      int total = bpm * 24 / 5;
      if (total < 72) {
        total = 72;
      }
      Tracker tracker;
      if (bpm < 90) {
        nearBpm(tracker, bpm);
      }
      transport(tracker, MIDI_MSG_START, 0);
      MidiRunningState state;
      std::memset(&state, 0, sizeof(state));
      MidiRing ring;
      int sent = 0;
      bool kept = true;
      while (sent < total) {
        int batch = per;
        if (batch > total - sent) {
          batch = total - sent;
        }
        uint8_t buf[96];
        uint16_t ts0 = clockMillis(sent, bpm);
        int n = 0;
        buf[n++] = (uint8_t)(0x80 | ((ts0 >> 7) & 0x3F));
        uint8_t bits = (uint8_t)(ts0 & 0x7F);
        if (bits >= 0x78) {
          bits = 0x77;
        }
        buf[n++] = (uint8_t)(0x80 | bits);
        for (int i = 0; i < batch; i++) {
          buf[n++] = 0xF8;
        }
        MidiParseResult parsed;
        parseBleMidiPacket(buf, n, &state, &parsed);
        int found = 0;
        for (int i = 0; i < parsed.count; i++) {
          found += eventTicks(parsed.events[i]);
        }
        if (found != batch || parsed.overflow != 0) {
          kept = false;
        }
        ring.PushParsed(parsed.events, parsed.count);
        handlePolled(tracker, ring, 2, nullptr, nullptr);
        sent += batch;
      }
      for (int guard = 0; guard < 80; guard++) {
        MidiEvent out[16];
        int n = ring.Poll(out, 16);
        if (n <= 0) {
          break;
        }
        for (int i = 0; i < n; i++) {
          tracker.HandleMidi(out[i]);
        }
      }
      int shown = tracker.Bpm();
      int slack = 4;
      expect(kept, "a BLE clock burst keeps every tick");
      expect(shown >= bpm - slack && shown <= bpm + slack, "tempo tracks 40, 120, and 200 through BLE bursts");
    }
  }
}

static void testDenseNotes() {
  Tracker tracker;
  tracker.SetCommand('I', 2);
  transport(tracker, MIDI_MSG_START, 0);
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  MidiRing ring;
  const int rounds = 24;
  int sentOn = 0;
  int sentOff = 0;
  int gotOn = 0;
  int gotOff = 0;
  for (int round = 0; round < rounds; round++) {
    uint8_t buf[128];
    uint16_t ts = clockMillis(round * 4, 200);
    int n = 0;
    buf[n++] = (uint8_t)(0x80 | ((ts >> 7) & 0x3F));
    for (int c = 0; c < 4; c++) {
      n = pushClockByte(buf, n, clockMillis(round * 4 + c, 200));
    }
    for (int note = 0; note < 4; note++) {
      int channel = note & 3;
      int pitch = 60 + note;
      uint8_t onBits = (uint8_t)((clockMillis(round * 4, 200) + 1 + note) & 0x7F);
      uint8_t offBits = (uint8_t)((clockMillis(round * 4, 200) + 8 + note) & 0x7F);
      if (onBits >= 0x78) {
        onBits = 0x70;
      }
      if (offBits >= 0x78) {
        offBits = 0x70;
      }
      buf[n++] = (uint8_t)(0x80 | onBits);
      buf[n++] = (uint8_t)(0x90 | channel);
      buf[n++] = (uint8_t)pitch;
      buf[n++] = 100;
      sentOn++;
      buf[n++] = (uint8_t)(0x80 | offBits);
      buf[n++] = (uint8_t)(0x80 | channel);
      buf[n++] = (uint8_t)pitch;
      buf[n++] = 0;
      sentOff++;
    }
    MidiParseResult parsed;
    parseBleMidiPacket(buf, n, &state, &parsed);
    expect(parsed.overflow == 0, "a dense packet does not overflow the parser");
    ring.PushParsed(parsed.events, parsed.count);
    handlePolled(tracker, ring, 16, &gotOn, &gotOff);
  }
  for (int guard = 0; guard < 200; guard++) {
    MidiEvent out[16];
    int n = ring.Poll(out, 16);
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; i++) {
      if (out[i].type == MIDI_MSG_NOTE_ON) {
        gotOn++;
      } else if (out[i].type == MIDI_MSG_NOTE_OFF) {
        gotOff++;
      }
      tracker.HandleMidi(out[i]);
    }
  }
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(gotOn == sentOn && gotOff == sentOff, "dense note bursts are not dropped");
  expect(drops.noteOn == 0 && drops.noteOff == 0 && drops.clock == 0 && drops.parser == 0, "the dense burst sheds nothing");
  expect(tracker.LiveHeld(0) == 0 && tracker.LiveHeld(1) == 0, "every dense note-off releases its note");
  int shown = tracker.Bpm();
  expect(shown >= 190 && shown <= 200, "the dense burst keeps the 200 BPM clock");

  uint8_t wide[320];
  int n = 0;
  wide[n++] = 0x82;
  for (int i = 0; i < 100; i++) {
    wide[n++] = 0xF8;
  }
  for (int i = 0; i < 4; i++) {
    wide[n++] = (uint8_t)(0x90 + i);
    wide[n++] = (uint8_t)(60 + i);
    wide[n++] = 110;
  }
  MidiParseResult wideParsed;
  std::memset(&state, 0, sizeof(state));
  parseBleMidiPacket(wide, n, &state, &wideParsed);
  int wideTicks = 0;
  int wideOns = 0;
  for (int i = 0; i < wideParsed.count; i++) {
    wideTicks += eventTicks(wideParsed.events[i]);
    if (wideParsed.events[i].type == MIDI_MSG_NOTE_ON) {
      wideOns++;
    }
  }
  expect(wideTicks == 100 && wideOns == 4 && wideParsed.overflow == 0, "a long clock run does not push the notes out");
}

static void testClockBurstLock() {
  Tracker deck;
  nearBpm(deck, 180);
  transport(deck, MIDI_MSG_START, 0);
  int head = deck.trackIndex;
  for (int i = 0; i < 48; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i, 180);
    deck.HandleMidi(clk);
  }
  expect(deck.Bpm() >= 176 && deck.Bpm() <= 184, "a full-rate 180 clock locks");
  int mid = deck.trackIndex;
  expect(mid == head + 8, "48 full-rate clocks are eight steps");

  for (int i = 48; i < 54; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value = 7;
    clk.value14 = clockMillis(i, 180);
    deck.HandleMidi(clk);
  }
  expect(deck.Bpm() >= 176 && deck.Bpm() <= 184, "duplicate clocks stay on 180");
  int movedDup = deck.trackIndex - mid;
  if (movedDup < 0) {
    movedDup += deck.patternLength;
  }
  expect(movedDup == 1, "duplicate clocks do not race the playhead");

  Tracker flood;
  nearBpm(flood, 180);
  transport(flood, MIDI_MSG_START, 0);
  int fhead = flood.trackIndex;
  for (int i = 0; i < 48 * 7; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = (uint16_t)((i * 2) & 0x1FFF);
    flood.HandleMidi(clk);
  }
  expect(flood.Bpm() >= 170 && flood.Bpm() <= 190, "a 2 ms burst stays near 180");
  int moved = flood.trackIndex - fhead;
  expect(moved >= 4 && moved <= 12, "a 2 ms burst moves with the tempo, not the raw count");
}

static void testThinnedClockAndSync() {
  Tracker deck;
  nearBpm(deck, 180);
  expect(deck.Bpm() == 180, "the deck starts at 180");
  transport(deck, MIDI_MSG_START, 0);
  int head = deck.trackIndex;
  for (int i = 0; i < 7; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i * 4, 180);
    deck.HandleMidi(clk);
  }
  expect(deck.Bpm() >= 176 && deck.Bpm() <= 184, "a thinned 180 BPM clock stays near 180");
  expect(deck.trackIndex == head + 4, "thinned clocks step at the musical rate");

  Tracker plain;
  nearBpm(plain, 180);
  transport(plain, MIDI_MSG_START, 0);
  for (int i = 0; i < 48; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i, 180);
    plain.HandleMidi(clk);
  }
  expect(plain.Bpm() >= 177 && plain.Bpm() <= 183, "a dense 180 BPM clock stays near 180");

  Tracker hold;
  hold.SetCommand('S', SYNC_TRANSPORT);
  nearBpm(hold, 180);
  int parked = hold.trackIndex;
  for (int i = 0; i < 8000; i++) {
    hold.UpdateTracker();
  }
  expect(hold.trackIndex == parked, "transport only waits for start");
  transport(hold, MIDI_MSG_START, 0);
  int started = hold.trackIndex;
  for (int i = 0; i < 24; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i * 4, 180);
    hold.HandleMidi(clk);
  }
  expect(hold.Bpm() == 180, "transport only keeps the deck BPM");
  expect(hold.trackIndex == started && hold.isPlaying, "transport only does not chase the clock");
  for (int i = 0; i < 8000; i++) {
    hold.UpdateTracker();
  }
  expect(hold.trackIndex != started, "transport only then runs on the deck clock");

  Tracker home;
  home.SetCommand('S', SYNC_INTERNAL);
  int bpm = home.Bpm();
  int at = home.trackIndex;
  transport(home, MIDI_MSG_START, 0);
  for (int i = 0; i < 24; i++) {
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value14 = clockMillis(i, 120);
    home.HandleMidi(clk);
  }
  expect(!home.isPlaying && home.Bpm() == bpm && home.trackIndex == at, "internal ignores MIDI clock and transport");

  Tracker wide;
  nearBpm(wide, 180);
  MidiEvent quarter = {};
  quarter.type = MIDI_MSG_MTC;
  quarter.value = 0x00;
  quarter.value14 = 0;
  wide.HandleMidi(quarter);
  expect(wide.isPlaying && wide.Bpm() == 180 && wide.trackIndex == 0, "a quarter frame starts transport and keeps the BPM");
  for (int i = 0; i < 30000; i++) {
    wide.UpdateTracker();
  }
  expect(!wide.isPlaying, "MTC ends when the frames stop");

  Tracker locked;
  nearBpm(locked, 180);
  const uint8_t pieces[] = {0x00, 0x10, 0x21, 0x30, 0x40, 0x50, 0x60, 0x72};
  for (int i = 0; i < 8; i++) {
    MidiEvent ev = {};
    ev.type = MIDI_MSG_MTC;
    ev.value = pieces[i];
    ev.value14 = (uint16_t)(i * 10);
    locked.HandleMidi(ev);
  }
  expect(locked.isPlaying && locked.Bpm() == 180, "matching MTC keeps the deck BPM");
  expect(locked.trackIndex == 12, "matching MTC seeks one second at 180");

  uint8_t packet[] = {0x80, 0x80, 0xF1, 0x21};
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  MidiParseResult parsed;
  int count = parseBleMidiPacket(packet, (int)sizeof(packet), &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_MTC && parsed.events[0].value == 0x21, "a quarter frame survives the BLE parser");
}

static void testUsbClockAndNotes() {
  const int rates[] = {40, 120, 200};
  for (int r = 0; r < 3; r++) {
    int bpm = rates[r];
    Tracker tracker;
    if (bpm < 90) {
      nearBpm(tracker, bpm);
    }
    transport(tracker, MIDI_MSG_START, 0);
    MidiRing ring;
    int total = 96;
    int gotOn = 0;
    int gotOff = 0;
    for (int i = 0; i < total; i++) {
      uint8_t raw[4] = {0x0F, 0xF8, 0, 0};
      MidiParseResult parsed;
      parseUsbMidiStream(raw, 4, clockMillis(i, bpm), &parsed);
      ring.PushParsed(parsed.events, parsed.count);
      if ((i % 6) == 0) {
        uint8_t note[8] = {
          0x09, 0x90, (uint8_t)(60 + (i & 3)), 100,
          0x08, 0x80, (uint8_t)(60 + (i & 3)), 0
        };
        parseUsbMidiStream(note, 8, clockMillis(i, bpm), &parsed);
        ring.PushParsed(parsed.events, parsed.count);
      }
      handlePolled(tracker, ring, 2, &gotOn, &gotOff);
    }
    for (int guard = 0; guard < 80; guard++) {
      MidiEvent out[16];
      int n = ring.Poll(out, 16);
      if (n <= 0) {
        break;
      }
      for (int i = 0; i < n; i++) {
        if (out[i].type == MIDI_MSG_NOTE_ON) {
          gotOn++;
        } else if (out[i].type == MIDI_MSG_NOTE_OFF) {
          gotOff++;
        }
        tracker.HandleMidi(out[i]);
      }
    }
    int shown = tracker.Bpm();
    int slack = 4;
    expect(shown >= bpm - slack && shown <= bpm + slack, "USB MIDI clock tracks 40, 120, and 200");
    expect(gotOn == 16 && gotOff == 16, "USB note-on and note-off pairs are kept");
    expect(tracker.LiveHeld(0) == 0, "USB note-offs release the note");
  }
}

static void feedTempo(Tracker &deck, uint32_t *cursor, int bpm, int clocks, int copies) {
  uint32_t origin = cursor ? *cursor : 0;
  if (copies < 1) {
    copies = 1;
  } else if (copies > 8) {
    copies = 8;
  }
  for (int i = 0; i < clocks; i++) {
    uint32_t at = origin + (uint32_t)i * 60000u / (uint32_t)(bpm * 24);
    MidiEvent clk = {};
    clk.type = MIDI_MSG_CLOCK;
    clk.value = (uint8_t)copies;
    clk.value14 = (uint16_t)(at & 0x1FFF);
    deck.HandleMidi(clk);
  }
  if (cursor) {
    *cursor = origin + (uint32_t)clocks * 60000u / (uint32_t)(bpm * 24);
  }
}

static void testWorkingTempos() {
  const int rates[] = {80, 100, 120, 138};
  uint32_t cursor = 100;
  Tracker deck;
  transport(deck, MIDI_MSG_START, 0);
  for (int i = 0; i < 4; i++) {
    int bpm = rates[i];
    int before = deck.trackIndex;
    feedTempo(deck, &cursor, bpm, 32, 1);
    expect(deck.Bpm() == bpm, "80, 100, 120, and 138 lock within one beat");
    int moved = deck.trackIndex - before;
    if (moved < 0) {
      moved += deck.patternLength;
    }
    expect(moved >= 4 && moved <= 7, "a clean clock moves about one step per six ticks");
    before = deck.trackIndex;
    feedTempo(deck, &cursor, bpm, 32, 6);
    expect(deck.Bpm() == bpm, "a burst of copies does not pull 80-138 off the clock");
    moved = deck.trackIndex - before;
    if (moved < 0) {
      moved += deck.patternLength;
    }
    expect(moved >= 4 && moved <= 7, "copies do not race the playhead");
  }
  const int jump[] = {80, 138, 100, 120, 180, 100};
  for (int i = 0; i < 6; i++) {
    feedTempo(deck, &cursor, jump[i], 36, 6);
    expect(deck.Bpm() == jump[i], "tempo jumps across 80-180 within two beats");
  }
}

static void testMultiChannelBurst() {
  Tracker deck;
  const uint8_t packet[] = {
    0x80, 0x80,
    0x90, 60, 100, 64, 90,
    0x91, 62, 100,
    0x92, 65, 100,
    0x93, 67, 100,
    0x94, 69, 80,
    0x99, 36, 110
  };
  MidiRunningState state;
  std::memset(&state, 0, sizeof(state));
  MidiParseResult parsed;
  int count = parseBleMidiPacket(packet, (int)sizeof(packet), &state, &parsed);
  expect(count == 7 && parsed.overflow == 0, "one packet keeps notes on six channels");
  expect(parsed.events[0].channel == 0 && parsed.events[1].channel == 0, "running status stays on its channel");
  expect(parsed.events[2].channel == 1 && parsed.events[5].channel == 4 && parsed.events[6].channel == 9, "each status keeps its channel");
  for (int i = 0; i < parsed.count; i++) {
    deck.HandleMidi(parsed.events[i]);
  }
  expect(deck.LiveDown(0, 60) && deck.LiveDown(0, 64) && deck.LiveDown(0, 69), "track 1 layers its own channels");
  expect(deck.LiveDown(1, 62) && deck.LiveDown(1, 36), "track 2 sounds channel 2 and channel 10 together");
  expect(deck.LiveDown(2, 65) && deck.LiveDown(3, 67), "tracks 3 and 4 sound with the others");
  expect(deck.selectedTrack == 3, "channels above 4 do not steal the selection");
  expect(renderEnergy(deck, 32) > 0, "the four tracks sound at once");
  noteOff(deck, 9, 36);
  expect(!deck.LiveDown(1, 36) && deck.LiveDown(1, 62), "a note-off on channel 10 leaves channel 2");
  noteOff(deck, 0, 60);
  expect(!deck.LiveDown(0, 60) && deck.LiveDown(0, 64) && deck.LiveDown(2, 65), "one note-off does not clear the other tracks");
}

int main() {
  testAssignStaysOnTrack();
  testMidiBankStaysOnChannel();
  testSongRecallDoesNotSmear();
  testBarsAndExternalLoop();
  testNoteHold();
  testMidiNotesRespectRec();
  testExternalClockPackets();
  testMidiPolyphony();
  testNoteOffFindsTrack();
  testStopReleasesLiveOnly();
  testContinuePhase();
  testSafetyHold();
  testRingClockTempo();
  testQuantizeAndGate();
  testExternalBpmClamp();
  testTempoSweep();
  testDenseNotes();
  testClockBurstLock();
  testThinnedClockAndSync();
  testUsbClockAndNotes();
  testWorkingTempos();
  testMultiChannelBurst();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
