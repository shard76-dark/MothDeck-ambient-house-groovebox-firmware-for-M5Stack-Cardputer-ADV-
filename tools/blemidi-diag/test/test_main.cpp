#include "BleAdvert.h"
#include "MidiProtocol.h"
#include "diag_parse.h"
#include "diag_selftest.h"
#include "diag_stats.h"

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

static void onSelf(const char *line, void *ctx) {
  int *lines = (int *)ctx;
  (*lines)++;
  std::printf("  %s\n", line);
}

static void testAdvert() {
  BleAdvertPackets pkts;
  buildBleMidiAdvert("MothDiag", "MothDiag", &pkts);
  const uint8_t adv[] = {
    0x02, 0x01, 0x06,
    0x09, 0x09, 'M', 'o', 't', 'h', 'D', 'i', 'a', 'g',
    0x11, 0x07,
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
  };
  const uint8_t scan[] = {
    0x09, 0x09, 'M', 'o', 't', 'h', 'D', 'i', 'a', 'g',
    0x03, 0x19, 0x00, 0x00,
    0x05, 0x12, 0x06, 0x00, 0x0C, 0x00
  };
  expect(pkts.advLen == (int)sizeof(adv) && std::memcmp(pkts.adv, adv, sizeof(adv)) == 0, "MothDiag advert is flags, name, MIDI UUID");
  expect(pkts.scanLen == (int)sizeof(scan) && std::memcmp(pkts.scan, scan, sizeof(scan)) == 0, "MothDiag scan response is name, appearance, interval");
  expect(!pkts.nameShortened && std::strcmp(pkts.gapName, "MothDiag") == 0, "GAP name is MothDiag");
}

static void testSelf() {
  int lines = 0;
  int failed = diagSelfTest(onSelf, &lines);
  expect(failed == 0, "parser self-test");
  expect(lines == 8, "self-test prints seven cases and a summary");
}

static void testNotes() {
  DiagMeter meter;
  meter.reset(0);
  meter.noteOn(1, 60, 100, 10);
  meter.noteOn(1, 64, 80, 20);
  meter.noteOn(10, 36, 110, 30);
  expect(meter.hanging() == 3, "three notes are held");
  meter.noteOff(1, 60);
  expect(meter.hanging() == 2, "one note released");
  meter.noteOff(1, 60);
  expect(meter.extraOffs() == 1, "a second note-off is unmatched");
  meter.noteOn(1, 64, 90, 40);
  expect(meter.hanging() == 2, "retrigger does not add a hanging note");
  meter.allOff(1);
  expect(meter.hanging() == 1, "all notes off clears that channel");
  meter.noteOff(10, 36);
  expect(meter.hanging() == 0 && meter.noteOnTotal() == 4 && meter.noteOffTotal() == 3, "held set is empty and counts include the retrigger");
  DiagHeld held[4];
  meter.noteOn(2, 67, 70, 50);
  expect(meter.copyHeld(held, 4) == 1 && held[0].ch == 2 && held[0].note == 67 && held[0].vel == 70, "notes command lists the held note");
}

static void testBpmAndJitter() {
  DiagMeter meter;
  meter.reset(0);
  meter.setWindowMs(1000);
  for (int i = 0; i < 24; i++) {
    meter.clock((uint64_t)i * 20833ULL, (uint16_t)(i * 21));
  }
  meter.packet(0);
  meter.packet(1000);
  meter.packet(3000);
  meter.packet(6000);
  DiagWindow window;
  expect(meter.takeWindow(500000, &window) == false, "stats wait for the window");
  expect(meter.takeWindow(1000000, &window), "stats fire at one second");
  expect(window.clocks == 24, "window counts the clocks");
  expect(window.hasBpmArrival && window.bpmArrival > 119.9 && window.bpmArrival < 120.1, "arrival BPM is 120");
  expect(window.hasBpmMidi && window.bpmMidi > 118.9 && window.bpmMidi < 119.2, "timestamp BPM follows 21 ms steps");
  expect(window.maxGapUs == 3000, "max packet gap");
  expect(window.jitterUs >= 815 && window.jitterUs <= 818, "packet gap jitter");
  expect(window.packets == 4, "packet count");
  DiagBpm bpm;
  meter.bpmSummary(&bpm);
  expect(bpm.clocks == 24 && bpm.hasBpmArrival && bpm.bpmArrival > 119.9 && bpm.bpmArrival < 120.1, "session arrival BPM");
  expect(bpm.hasArrivalJitter && bpm.hasMidiJitter, "session jitter is present");

  meter.parseError(2);
  meter.noteParserMismatch();
  expect(meter.parseErrors() == 2 && meter.parserMismatch() == 1, "parse errors accumulate");
  expect(meter.takeWindow(1000000, &window) == false, "window restarts after a report");
  expect(meter.takeWindow(2000000, &window), "next window reports");
  expect(window.parseErrors == 2 && window.clocks == 0 && window.hanging == 0, "next window sees the errors and no new clocks");
}

static void testReject() {
  DiagState state;
  diagStateClear(&state);
  DiagPacket pkt;
  MidiRunningState midi;
  std::memset(&midi, 0, sizeof(midi));
  const uint8_t bad[] = {0x00, 0x90, 60, 100};
  diagParse(bad, 4, &state, &pkt);
  MidiParseResult parsed;
  int n = parseBleMidiPacket(bad, 4, &midi, &parsed);
  expect(pkt.rejected && pkt.errors == 1 && n == 0 && diagMatchesParser(&pkt, &parsed), "bad header is a parse error and matches the parser");
}

int main() {
  testAdvert();
  testSelf();
  testNotes();
  testBpmAndJitter();
  testReject();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
