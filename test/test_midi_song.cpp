#include "MidiProtocol.h"
#include "MidiMap.h"
#include "MidiRing.h"
#include "SongFile.h"
#include "DevLog.h"
#include <cstdio>
#include <cstring>
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

static void zeroState(MidiRunningState *state) {
  std::memset(state, 0, sizeof(*state));
}

static void testTimestamp() {
  BleMidiTimestamp zero = bleMidiTimestamp(0);
  expect(zero.headerMsb == 0x80 && zero.timestampLsb == 0x80, "timestamp 0 is MSB 0x80 and LSB 0x80");

  BleMidiTimestamp full = bleMidiTimestamp(0x1FFF);
  expect(full.headerMsb == 0xBF && full.timestampLsb == 0xFF, "timestamp 0x1FFF splits into MSB 0xBF and LSB 0xFF");

  BleMidiTimestamp mid = bleMidiTimestamp(0x1234);
  expect(mid.headerMsb == 0xA4 && mid.timestampLsb == 0xB4, "timestamp 0x1234 splits into MSB 0xA4 and LSB 0xB4");
}

static void testNoteRoundTrip() {
  uint8_t packet[8];
  int n = buildBleMidiPacket(packet, 8, 0x1234, MIDI_MSG_NOTE_ON, 2, 60, 100);
  expect(n == 5, "note packet is 5 bytes");
  expect(packet[0] == 0xA4 && packet[1] == 0xB4, "note packet carries timestamp MSB and LSB");
  expect(packet[2] == 0x92 && packet[3] == 60 && packet[4] == 100, "note packet is Note On ch 3 note 60");

  MidiRunningState state;
  zeroState(&state);
  MidiParseResult parsed;
  int count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_NOTE_ON, "parser enables Note On");
  expect(parsed.events[0].channel == 2 && parsed.events[0].number == 60 && parsed.events[0].value == 100, "note channel, pitch, and velocity");

  n = buildBleMidiPacket(packet, 8, 10, MIDI_MSG_NOTE_OFF, 0, 36, 0);
  count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_NOTE_OFF && parsed.events[0].number == 36, "parser enables Note Off");

  const uint8_t running[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 62, 110};
  count = parseBleMidiPacket(running, (int)sizeof(running), &state, &parsed);
  expect(count == 2 && parsed.events[1].type == MIDI_MSG_NOTE_ON && parsed.events[1].number == 62 && parsed.events[1].value == 110, "running status second note");
}

static void testMsbLsb() {
  uint8_t packet[8];
  MidiRunningState state;
  zeroState(&state);
  MidiParseResult parsed;

  int n = buildBleMidiPacket(packet, 8, 20, MIDI_MSG_MSB, 0, 7, 100);
  expect(n == 5 && packet[2] == 0xB0 && packet[3] == 7 && packet[4] == 100, "volume MSB is CC 7");
  int count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_MSB, "parser enables MSB");
  expect(parsed.events[0].number == 7 && parsed.events[0].value == 100 && parsed.events[0].value14 == (100 << 7), "MSB value14 before LSB arrives");

  n = buildBleMidiPacket(packet, 8, 21, MIDI_MSG_LSB, 0, 7, 10);
  expect(packet[3] == 39 && packet[4] == 10, "volume LSB is CC 39");
  count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_LSB, "parser enables LSB");
  expect(parsed.events[0].number == 7 && parsed.events[0].value == 10 && parsed.events[0].value14 == ((100 << 7) | 10), "LSB joins the stored MSB");

  n = buildBleMidiPacket(packet, 8, 22, MIDI_MSG_MSB, 1, 0, 4);
  count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(parsed.events[0].type == MIDI_MSG_MSB && parsed.events[0].channel == 1 && parsed.events[0].number == 0 && parsed.events[0].value == 4, "bank MSB");
  n = buildBleMidiPacket(packet, 8, 23, MIDI_MSG_LSB, 1, 0, 0);
  count = parseBleMidiPacket(packet, n, &state, &parsed);
  expect(parsed.events[0].type == MIDI_MSG_LSB && parsed.events[0].value14 == (4 << 7), "bank LSB zero keeps the MSB");

  const uint8_t bend[] = {0x80, 0x80, 0xE0, 0x00, 0x40};
  count = parseBleMidiPacket(bend, (int)sizeof(bend), &state, &parsed);
  expect(count == 2 && parsed.events[0].type == MIDI_MSG_LSB && parsed.events[1].type == MIDI_MSG_MSB, "pitch bend emits LSB then MSB");
  expect(parsed.events[0].number == kMidiPitchCc && parsed.events[0].value14 == 8192 && parsed.events[1].value14 == 8192, "pitch bend center is 8192");

  const uint8_t allOff[] = {0x80, 0x80, 0xB0, 123, 0, 0x81, 0xB1, 120, 0};
  count = parseBleMidiPacket(allOff, (int)sizeof(allOff), &state, &parsed);
  expect(count == 2, "all-notes-off and all-sound-off are both kept");
  expect(parsed.events[0].type == MIDI_MSG_ALL_OFF && parsed.events[0].channel == 0 && parsed.events[0].number == 123, "CC 123 is all notes off on its channel");
  expect(parsed.events[1].type == MIDI_MSG_ALL_OFF && parsed.events[1].channel == 1 && parsed.events[1].number == 120, "CC 120 is all sound off on its channel");

  expect(parseBleMidiPacket(packet, 1, &state, &parsed) == 0, "short packet is ignored");
  const uint8_t badHeader[] = {0x00, 0x90, 60, 100};
  expect(parseBleMidiPacket(badHeader, 4, &state, &parsed) == 0, "missing timestamp MSB is rejected");
}

static int countType(const MidiParseResult &parsed, MidiMsgType type) {
  int n = 0;
  for (int i = 0; i < parsed.count; i++) {
    if (parsed.events[i].type == type) {
      n++;
    }
  }
  return n;
}

static void testClockAndNotes() {
  MidiRunningState state;
  zeroState(&state);
  MidiParseResult parsed;

  const uint8_t vel0[] = {0x80, 0x80, 0x90, 60, 0};
  int count = parseBleMidiPacket(vel0, (int)sizeof(vel0), &state, &parsed);
  expect(count == 1 && parsed.events[0].type == MIDI_MSG_NOTE_OFF && parsed.events[0].number == 60, "note on velocity 0 is note off");

  // Timestamp, clock, timestamp, note, running-status note, timestamp, clock.
  const uint8_t mixed[] = {
    0x80, 0x80, 0xF8, 0x81, 0x90, 60, 100, 0x82, 62, 110, 0x83, 0xF8
  };
  count = parseBleMidiPacket(mixed, (int)sizeof(mixed), &state, &parsed);
  expect(count == 4, "clocks and both notes are kept");
  expect(countType(parsed, MIDI_MSG_CLOCK) == 2, "two clocks in one packet");
  expect(parsed.events[1].type == MIDI_MSG_NOTE_ON && parsed.events[1].number == 60 && parsed.events[1].value == 100, "note after a leading clock");
  expect(parsed.events[2].type == MIDI_MSG_NOTE_ON && parsed.events[2].number == 62 && parsed.events[2].value == 110, "running status note between clocks");

  // A clock byte between the two data bytes of a note.
  const uint8_t mid[] = {0x80, 0x81, 0x90, 60, 0xF8, 100};
  count = parseBleMidiPacket(mid, (int)sizeof(mid), &state, &parsed);
  expect(count == 2 && countType(parsed, MIDI_MSG_CLOCK) == 1, "clock between note data bytes");
  expect(parsed.events[0].type == MIDI_MSG_CLOCK || parsed.events[1].type == MIDI_MSG_NOTE_ON, "clock does not erase the note");
  bool noteOk = false;
  for (int i = 0; i < parsed.count; i++) {
    if (parsed.events[i].type == MIDI_MSG_NOTE_ON && parsed.events[i].number == 60 && parsed.events[i].value == 100) {
      noteOk = true;
    }
  }
  expect(noteOk, "note split by a clock keeps pitch and velocity");

  const uint8_t spp[] = {0x80, 0x80, 0xF2, 0x06, 0x00, 0x81, 0xFA, 0x82, 0xFB, 0x83, 0xFC};
  count = parseBleMidiPacket(spp, (int)sizeof(spp), &state, &parsed);
  expect(count == 4, "song position and transport");
  expect(parsed.events[0].type == MIDI_MSG_SONG_POS && parsed.events[0].value14 == 6, "song position is six 16ths");
  expect(parsed.events[1].type == MIDI_MSG_START && parsed.events[2].type == MIDI_MSG_CONTINUE && parsed.events[3].type == MIDI_MSG_STOP, "start, continue, stop");

  const uint8_t sysex1[] = {0x80, 0x80, 0xF0, 0x01, 0x02};
  count = parseBleMidiPacket(sysex1, (int)sizeof(sysex1), &state, &parsed);
  expect(count == 0 && state.sysex == 1, "unterminated sysex spans the packet");
  const uint8_t sysex2[] = {0x80, 0x03, 0xF7, 0x84, 0x90, 40, 20};
  count = parseBleMidiPacket(sysex2, (int)sizeof(sysex2), &state, &parsed);
  expect(state.sysex == 0 && count == 1 && parsed.events[0].type == MIDI_MSG_NOTE_ON && parsed.events[0].number == 40, "note after a continued sysex");
}

static void testMapping() {
  int pitch = -1;
  int octave = -1;
  midiNoteToSynth(36, pitch, octave);
  expect(pitch == 0 && octave == 0, "MIDI C2 is pitch 0 octave 0");
  midiNoteToSynth(48, pitch, octave);
  expect(pitch == 0 && octave == 1, "MIDI C3 is octave 1");
  midiNoteToSynth(83, pitch, octave);
  expect(pitch == 11 && octave == 3, "MIDI B5 is the top synth note");
  expect(synthToMidiNote(pitch, octave) == 83, "synth note maps back to MIDI");
  expect(scaleByBend(1000, 8192) == 1000, "centered bend does not change frequency");
  expect(scaleByBend(1000, 16383) > 1000 && scaleByBend(1000, 0) < 1000, "bend MSB/LSB moves frequency both ways");

  for (int inst = 0; inst < 12; inst++) {
    if (instrumentFromBank(inst, 0, false) != inst) {
      expect(false, "bank MSB selects the instrument");
      return;
    }
  }
  expect(true, "bank MSB selects instruments 0 through 11");
  expect(instrumentFromBank(12, 0, false) == 12, "bank MSB 12 selects plugin id 12");
  expect(instrumentFromBank(63, 0, false) == 63, "bank MSB 63 is the last plugin id");
  expect(instrumentFromBank(127, 0, false) == 11, "bank MSB above 63 clamps to the last built-in");

  bool volumes = true;
  for (int vol = 0; vol <= 8; vol++) {
    int packed = volumeTo14(vol);
    int back = volumeFrom14(packed);
    if (back != vol) {
      volumes = false;
    }
  }
  expect(volumes, "volume MSB/LSB round trip 0 through 8");
}

static uint16_t sumBytes(const uint8_t *data, int len) {
  uint16_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum = (uint16_t)(sum + data[i]);
  }
  return sum;
}

static void testSongFile() {
  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 64;
  song.masterVolume = 1;
  song.bpmSlot = 2;
  song.bpms[0] = 120;
  song.bpms[1] = 140;
  song.bpms[2] = 95;
  song.bpms[3] = 180;
  song.currentVoice = 6;
  song.selectedTrack = 3;
  song.currentPattern = 1;
  song.allPatternPlay = 1;
  song.tracks[2][10] = 8;
  song.octaves[2][10] = -1;
  song.instruments[2][10] = 9;
  song.voices[1].volume = 5;
  song.voices[1].octave = 2;
  song.voices[1].bend14 = 9000;
  song.voices[1].envelopeNum = 3;
  song.voices[1].envelopeLength = 2;
  song.voices[1].lowPassMult = 2;

  uint8_t buf[kSongFileBytes];
  int n = songEncode(song, buf, kSongFileBytes);
  expect(n == kSongFileBytes, "song file is the fixed slot size");
  expect(buf[0] == 'M' && buf[1] == 'O' && buf[2] == 'T' && buf[3] == 'H', "song magic");

  SongData loaded;
  std::memset(&loaded, 0, sizeof(loaded));
  expect(songDecode(buf, n, &loaded), "song decodes");
  expect(loaded.patternLength == 64 && loaded.bpmSlot == 2 && loaded.bpms[2] == 95, "tempo and length restored");
  expect(loaded.currentVoice == 6 && loaded.selectedTrack == 3 && loaded.currentPattern == 1 && loaded.allPatternPlay == 1, "transport restored");
  expect(loaded.tracks[2][10] == 8 && loaded.octaves[2][10] == -1 && loaded.instruments[2][10] == 9, "step restored");
  expect(loaded.voices[1].volume == 5 && loaded.voices[1].bend14 == 9000 && loaded.voices[1].lowPassMult == 2, "voice restored");
  expect(loaded.voices[1].envelopeNum == 3 && loaded.voices[1].envelopeLength == 2 && loaded.voices[1].octave == 2, "envelope restored");

  buf[40] ^= 0xFF;
  expect(!songDecode(buf, n, &loaded), "corrupt song is rejected");

  n = songEncode(song, buf, kSongFileBytes);
  buf[5] = 0;
  buf[6] = 0;
  uint16_t sum = sumBytes(buf, kSongFileBytes - 2);
  buf[kSongFileBytes - 2] = (uint8_t)(sum & 0xFF);
  buf[kSongFileBytes - 1] = (uint8_t)((sum >> 8) & 0xFF);
  expect(!songDecode(buf, n, &loaded), "zero pattern length is rejected");
  expect(songDecode(buf, n - 1, &loaded) == false, "truncated song is rejected");

  song.patternLength = 16;
  n = songEncode(song, buf, kSongFileBytes);
  expect(songDecode(buf, n, &loaded) && loaded.patternLength == 16, "a 16-step song still decodes as one bar");
  song.patternLength = 128;
  n = songEncode(song, buf, kSongFileBytes);
  expect(n == kSongFileBytes && songDecode(buf, n, &loaded) && loaded.patternLength == 128, "an 8-bar song stays a version 1 file");
  song.patternLength = 32;
  n = songEncode(song, buf, kSongFileBytes);
  expect(songDecode(buf, n, &loaded) && loaded.patternLength == 32, "a 32-step song still decodes unchanged");
}

static void testSongV2() {
  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 32;
  song.bpmSlot = 0;
  song.bpms[0] = 100;
  song.bpms[1] = 120;
  song.bpms[2] = 140;
  song.bpms[3] = 90;
  song.currentVoice = 14;
  song.selectedTrack = 1;
  song.instruments[1][4] = 14;
  song.pluginCount = 1;
  song.plugins[0].id = 14;
  std::memcpy(song.plugins[0].name, "soft-saw", 9);
  song.loops[1].enabled = 1;
  song.loops[1].quantize = 2;
  std::memcpy(song.loops[1].library, "dust", 5);
  std::memcpy(song.loops[1].name, "kick.wav", 9);

  expect(songNeedsV2(song), "plugins and loops require version 2");
  int need = songEncodedSize(song);
  expect(need > kSongFileBytes && need <= kSongFileBytesMax, "version 2 is larger than a MothOS slot and bounded");
  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, kSongFileBytesMax);
  expect(n == need && buf[4] == kSongVersionV2, "version 2 encode");
  SongData loaded;
  std::memset(&loaded, 0, sizeof(loaded));
  expect(songDecode(buf, n, &loaded), "version 2 decodes");
  expect(loaded.currentVoice == 14 && loaded.instruments[1][4] == 14, "plugin id restored");
  expect(std::strcmp(loaded.plugins[0].name, "soft-saw") == 0 && loaded.plugins[0].id == 14, "plugin name restored");
  expect(loaded.loops[1].enabled == 1 && loaded.loops[1].quantize == 2, "loop assignment restored");
  expect(std::strcmp(loaded.loops[1].library, "dust") == 0 && std::strcmp(loaded.loops[1].name, "kick.wav") == 0, "loop names restored");

  buf[80] ^= 0x5A;
  expect(!songDecode(buf, n, &loaded), "corrupt version 2 song is rejected");

  n = songEncode(song, buf, kSongFileBytesMax);
  std::memcpy(song.plugins[0].name, "../evil", 8);
  expect(songEncode(song, buf, kSongFileBytesMax) == 0, "path traversal in a plugin name is rejected");

  std::memset(&song, 0, sizeof(song));
  song.patternLength = 32;
  song.bpms[0] = 120;
  expect(!songNeedsV2(song), "a built-in song stays version 1");
  n = songEncode(song, buf, kSongFileBytesMax);
  expect(n == kSongFileBytes && buf[4] == kSongVersion, "plain song is still a MothOS v1 file");
  expect(songDecode(buf, n, &loaded), "plain song still decodes");
  expect(!trackFxActive(loaded.fx[0]) && !trackFxActive(loaded.fx[1]), "version 1 leaves inserts off");
}

static void testSongV3() {
  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 32;
  song.bpms[0] = 120;
  song.bpms[1] = 120;
  song.bpms[2] = 120;
  song.bpms[3] = 120;
  song.currentVoice = 2;
  song.selectedTrack = 1;
  song.voices[0].volume = 2;
  song.voices[1].volume = 6;
  song.fx[1].filter = 2;
  song.fx[1].cutoff = 40;
  song.fx[1].res = 16;
  song.fx[1].delayDiv = 2;
  song.fx[1].delayFb = 30;
  song.fx[1].delayMix = 40;
  song.fx[1].reverb = 20;
  song.fx[1].crush = 1;
  song.fx[1].drive = 10;
  song.fx[1].chorus = 20;
  song.fx[1].tremolo = 30;

  expect(songNeedsV3(song), "an insert effect requires version 3");
  expect(songNeedsV2(song), "version 3 still carries the version 2 tail");
  int plain = 0;
  {
    SongData bare = song;
    std::memset(bare.fx, 0, sizeof(bare.fx));
    plain = songEncodedSize(bare);
    expect(plain == kSongFileBytes && !songNeedsV2(bare), "clearing inserts returns to version 1");
  }
  int need = songEncodedSize(song);
  expect(need == plain + 1 + kSongTracks * (2 + kPluginNameLen * 2) + kSongTracks * kSongFxBytes, "version 3 is the empty plugin tail plus 48 effect bytes");
  expect(need <= kSongFileBytesMax, "version 3 stays inside the slot buffer");
  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, kSongFileBytesMax);
  expect(n == need && buf[4] == kSongVersionV3, "version 3 encode");
  SongData loaded;
  std::memset(&loaded, 0x5A, sizeof(loaded));
  expect(songDecode(buf, n, &loaded), "version 3 decodes");
  expect(loaded.voices[1].volume == 6 && loaded.voices[0].volume == 2, "volumes survive the effect tail");
  expect(!trackFxActive(loaded.fx[0]) && !trackFxActive(loaded.fx[2]) && !trackFxActive(loaded.fx[3]), "other tracks stay dry");
  expect(loaded.fx[1].filter == 2 && loaded.fx[1].cutoff == 40 && loaded.fx[1].delayDiv == 2, "track 2 filter and delay restored");
  expect(loaded.fx[1].reverb == 20 && loaded.fx[1].crush == 1 && loaded.fx[1].tremolo == 30, "track 2 send and crush restored");
  expect(loaded.fx[1].drive == 10 && loaded.fx[1].chorus == 20 && loaded.fx[1].delayMix == 40, "track 2 drive and chorus restored");

  buf[n / 2] ^= 0x11;
  expect(!songDecode(buf, n, &loaded), "corrupt version 3 song is rejected");

  n = songEncode(song, buf, kSongFileBytesMax);
  song.fx[1].cutoff = 200;
  song.fx[1].delayFb = 90;
  n = songEncode(song, buf, kSongFileBytesMax);
  expect(songDecode(buf, n, &loaded), "out-of-range insert bytes still decode");
  expect(loaded.fx[1].cutoff == 127 && loaded.fx[1].delayFb == 70, "decode clamps insert ranges");

  std::memset(&song, 0, sizeof(song));
  song.patternLength = 32;
  song.bpms[0] = 100;
  song.currentVoice = 14;
  song.pluginCount = 1;
  song.plugins[0].id = 14;
  std::memcpy(song.plugins[0].name, "soft-saw", 9);
  int v2 = songEncodedSize(song);
  expect(!songNeedsV3(song) && v2 > kSongFileBytes, "a plugin song without inserts stays version 2");
  song.fx[3].chorus = 10;
  expect(songEncodedSize(song) == v2 + kSongTracks * kSongFxBytes, "inserts add 48 bytes to a version 2 song");
}

static void testSongV4() {
  SongData song;
  std::memset(&song, 0, sizeof(song));
  song.patternLength = 16;
  song.bpms[0] = 120;
  song.blocks[0].scaleMode = 2;
  song.blocks[0].scaleRoot = 9;
  song.blocks[0].arpMode = 3;
  song.blocks[0].osc2Wave = 3;
  song.blocks[0].osc2Coarse = -12;
  song.blocks[0].blend = 40;
  song.blocks[0].glideMs = 90;
  expect(songNeedsV4(song), "scale and glide require version 4");
  int need = songEncodedSize(song);
  expect(need <= kSongFileBytesMax, "version 4 stays inside the slot buffer");
  uint8_t buf[kSongFileBytesMax];
  int n = songEncode(song, buf, kSongFileBytesMax);
  expect(n == need && buf[4] == kSongVersionV4, "version 4 header");
  SongData loaded;
  expect(songDecode(buf, n, &loaded), "version 4 round trip");
  expect(loaded.blocks[0].scaleMode == 2 && loaded.blocks[0].scaleRoot == 9, "scale restored");
  expect(loaded.blocks[0].arpMode == 3 && loaded.blocks[0].osc2Wave == 3, "arp and osc2 restored");
  expect(loaded.blocks[0].osc2Coarse == -12 && loaded.blocks[0].blend == 40 && loaded.blocks[0].glideMs == 90, "coarse blend glide restored");
  expect(songDecode(buf, n - 1, &loaded) == false, "truncated version 4 is rejected");
}

static int clockTicks(const MidiEvent &event) {
  if (event.type != MIDI_MSG_CLOCK) {
    return 0;
  }
  return event.value == 0 ? 1 : (int)event.value;
}

static int drainTicks(MidiRing &ring, MidiMsgType *types, int typeCap, int *typeCount) {
  int ticks = 0;
  if (typeCount) {
    *typeCount = 0;
  }
  MidiEvent out[16];
  for (int guard = 0; guard < 40; guard++) {
    int n = ring.Poll(out, 16);
    if (n <= 0) {
      break;
    }
    for (int i = 0; i < n; i++) {
      ticks += clockTicks(out[i]);
      if (types && typeCount && *typeCount < typeCap && clockTicks(out[i]) == 0) {
        types[(*typeCount)++] = out[i].type;
      }
    }
  }
  return ticks;
}

static void testClockQueue() {
  MidiRunningState state;
  zeroState(&state);
  MidiParseResult parsed;
  MidiRing ring;

  const uint8_t chord[] = {
    0x80, 0x80, 0xF8, 0x81, 0xF8, 0x82, 0xF8, 0x83, 0xF8, 0x84, 0xF8, 0x85, 0xF8,
    0x86, 0x90, 60, 100,
    0x87, 0x90, 64, 110,
    0x88, 0x90, 67, 90
  };
  int count = parseBleMidiPacket(chord, (int)sizeof(chord), &state, &parsed);
  expect(count == 9 && countType(parsed, MIDI_MSG_CLOCK) == 6, "parser still sees six clocks and a chord");
  ring.PushParsed(parsed.events, parsed.count);
  MidiMsgType kinds[8];
  int kindCount = 0;
  int ticks = drainTicks(ring, kinds, 8, &kindCount);
  expect(ticks == 6, "the chord packet keeps every clock in the counter");
  expect(kindCount == 3 && kinds[0] == MIDI_MSG_NOTE_ON && kinds[1] == MIDI_MSG_NOTE_ON && kinds[2] == MIDI_MSG_NOTE_ON, "the chord is queued behind those clocks");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.clock == 0 && drops.noteOn == 0, "the chord packet drops nothing");

  const uint8_t started[] = {
    0x80, 0x80, 0xF8, 0x81, 0xF8, 0x82, 0xF8, 0x83, 0xF8,
    0x84, 0xFA,
    0x85, 0xF8, 0x86, 0xF8, 0x87, 0xF8, 0x88, 0xF8, 0x89, 0xF8, 0x8A, 0xF8,
    0x8B, 0x90, 60, 100
  };
  zeroState(&state);
  count = parseBleMidiPacket(started, (int)sizeof(started), &state, &parsed);
  expect(countType(parsed, MIDI_MSG_CLOCK) == 10 && countType(parsed, MIDI_MSG_START) == 1, "parser sees clocks on both sides of start");
  ring.PushParsed(parsed.events, parsed.count);
  expect(ring.PendingTicks() == 0, "ticks after start are bound to the note");
  MidiEvent out[16];
  int n = ring.Poll(out, 16);
  expect(n >= 2 && out[0].type == MIDI_MSG_START, "start is ahead of the clocks that followed it");
  int after = 0;
  bool note = false;
  bool clockBeforeStart = false;
  for (int i = 0; i < n; i++) {
    if (i == 0) {
      continue;
    }
    int got = clockTicks(out[i]);
    if (got > 0) {
      if (note) {
        clockBeforeStart = true;
      }
      after += got;
    } else if (out[i].type == MIDI_MSG_NOTE_ON) {
      note = true;
    }
  }
  expect(after == 6 && note && !clockBeforeStart, "clocks before start in the packet are discarded");

  const uint8_t between[] = {
    0x80, 0x80, 0xF2, 0x08, 0x00,
    0x81, 0xF8, 0x82, 0xF8, 0x83, 0xF8,
    0x84, 0xFB
  };
  zeroState(&state);
  count = parseBleMidiPacket(between, (int)sizeof(between), &state, &parsed);
  ring.PushParsed(parsed.events, parsed.count);
  n = ring.Poll(out, 16);
  expect(n >= 3 && out[0].type == MIDI_MSG_SONG_POS, "song position stays ahead of the clocks");
  int mid = 0;
  bool cont = false;
  for (int i = 1; i < n; i++) {
    int got = clockTicks(out[i]);
    if (got > 0 && !cont) {
      mid += got;
    } else if (out[i].type == MIDI_MSG_CONTINUE) {
      cont = true;
    }
  }
  expect(mid == 3 && cont, "clocks between song position and continue stay in order");

  uint8_t burst[160];
  int bytes = 0;
  burst[bytes++] = 0x80;
  const int burstClocks = 36;
  for (int i = 0; i < burstClocks; i++) {
    uint32_t ms = (uint32_t)i * 60000u / (uint32_t)(200 * 24);
    uint8_t bits = (uint8_t)(ms & 0x7F);
    if (bits >= 0x78) {
      bits = 0x77;
    }
    burst[bytes++] = (uint8_t)(0x80 | bits);
    burst[bytes++] = 0xF8;
  }
  const uint8_t notes[] = {60, 64, 67};
  const uint8_t vels[] = {100, 110, 90};
  for (int i = 0; i < 3; i++) {
    burst[bytes++] = 0xB0;
    burst[bytes++] = 0x90;
    burst[bytes++] = notes[i];
    burst[bytes++] = vels[i];
  }
  zeroState(&state);
  count = parseBleMidiPacket(burst, bytes, &state, &parsed);
  int burstTicks = 0;
  for (int i = 0; i < parsed.count; i++) {
    if (parsed.events[i].type == MIDI_MSG_CLOCK) {
      burstTicks += parsed.events[i].value == 0 ? 1 : (int)parsed.events[i].value;
    }
  }
  expect(burstTicks == burstClocks && countType(parsed, MIDI_MSG_NOTE_ON) == 3, "a 200 BPM burst keeps the clocks and the chord");
  expect(parsed.overflow == 0, "the burst fits in the parser");
  ring.PushParsed(parsed.events, parsed.count);
  ring.CopyDrops(&drops);
  kindCount = 0;
  ticks = drainTicks(ring, kinds, 8, &kindCount);
  expect(ticks == burstClocks && drops.clock == 0, "the 200 BPM burst is counted and not dropped");
  expect(kindCount == 3 && kinds[0] == MIDI_MSG_NOTE_ON, "the chord follows the 200 BPM burst");
}

static void testDevLogDefault() {
  expect(MOTHOS_DEV_LOG == 0, "MOTHOS_DEV_LOG stays off unless the compiler sets it");
  DEV_LOG("SD OLED DAC logs are compiled out");
  DEV_LOGF("DAC: %d\n", 0);
}

int main() {
  testDevLogDefault();
  testTimestamp();
  testNoteRoundTrip();
  testMsbLsb();
  testClockAndNotes();
  testMapping();
  testSongFile();
  testSongV2();
  testSongV4();
  testSongV3();
  testClockQueue();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
