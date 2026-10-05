#include "MidiProtocol.h"
#include "MidiMap.h"
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

  expect(parseBleMidiPacket(packet, 1, &state, &parsed) == 0, "short packet is ignored");
  const uint8_t badHeader[] = {0x00, 0x90, 60, 100};
  expect(parseBleMidiPacket(badHeader, 4, &state, &parsed) == 0, "missing timestamp MSB is rejected");
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
  testMapping();
  testSongFile();
  testSongV2();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
