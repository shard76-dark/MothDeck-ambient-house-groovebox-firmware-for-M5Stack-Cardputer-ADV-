#include "MidiRing.h"
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

static MidiEvent ev(MidiMsgType type, uint8_t note = 0) {
  MidiEvent event;
  std::memset(&event, 0, sizeof(event));
  event.type = type;
  event.number = note;
  event.value = 100;
  return event;
}

static void testOrder() {
  MidiRing ring;
  expect(ring.Push(ev(MIDI_MSG_NOTE_ON, 60)), "note-on enters an empty ring");
  expect(ring.Push(ev(MIDI_MSG_NOTE_OFF, 60)), "note-off enters behind it");
  MidiEvent out[4];
  int n = ring.Poll(out, 4);
  expect(n == 2 && out[0].type == MIDI_MSG_NOTE_ON && out[1].type == MIDI_MSG_NOTE_OFF, "note-off stays behind its note-on");
  expect(out[0].number == 60 && out[1].number == 60, "both events keep the note");
}

static void testClockShed() {
  MidiRing ring;
  int kept = 0;
  for (int i = 0; i < 400; i++) {
    if (ring.Push(ev(MIDI_MSG_CLOCK))) {
      kept++;
    }
  }
  int limit = MidiRing::kLen - 1 - MidiRing::kReserve;
  expect(kept == limit, "clocks stop at the note reserve");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.clock == (uint32_t)(400 - kept) && drops.noteOff == 0, "shed clocks are counted on their own");
  expect(ring.Push(ev(MIDI_MSG_NOTE_ON, 60)), "a note still enters after clocks are shed");
  expect(ring.Push(ev(MIDI_MSG_NOTE_OFF, 60)), "a note-off still enters after clocks are shed");
}

static void testControllerShed() {
  MidiRing ring;
  int kept = 0;
  while (ring.Push(ev(MIDI_MSG_MSB, 7))) {
    kept++;
  }
  int limit = MidiRing::kLen - 1 - MidiRing::kControlReserve;
  expect(kept == limit, "controllers stop before the last note slots");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.ringFull > 0 && drops.noteOn == 0 && drops.noteOff == 0, "a shed controller is not a note");
  expect(ring.Push(ev(MIDI_MSG_NOTE_ON, 60)), "a note enters after controllers are shed");
  expect(ring.Push(ev(MIDI_MSG_NOTE_OFF, 60)), "a note-off enters after controllers are shed");
}

static bool contains(MidiRing &ring, MidiMsgType type, uint8_t note) {
  MidiEvent out[16];
  for (int guard = 0; guard < 40; guard++) {
    int n = ring.Poll(out, 16);
    if (n <= 0) {
      return false;
    }
    for (int i = 0; i < n; i++) {
      if (out[i].type == type && out[i].number == note) {
        return true;
      }
    }
  }
  return false;
}

static void testNoteOffKept() {
  MidiRing ring;
  int ons = 0;
  while (ring.Push(ev(MIDI_MSG_NOTE_ON, (uint8_t)(ons & 0x7F)))) {
    ons++;
  }
  expect(ons == MidiRing::kLen - 1, "note-ons fill the ring");
  MidiDrops before;
  ring.CopyDrops(&before);
  expect(ring.Push(ev(MIDI_MSG_NOTE_OFF, 60)), "a note-off is kept when the ring is full");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.noteOff == before.noteOff && drops.noteOn == before.noteOn + 1, "the ring sheds a note-on to keep the note-off");
  expect(contains(ring, MIDI_MSG_NOTE_OFF, 60), "the kept note-off can be read back");
}

static void testStopKept() {
  MidiRing ring;
  while (ring.Push(ev(MIDI_MSG_NOTE_ON, 1))) {
  }
  expect(ring.Push(ev(MIDI_MSG_STOP)), "stop is kept when the ring is full of note-ons");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.noteOn >= 1 && drops.ringFull == 0, "stop sheds a note-on rather than itself");
  expect(contains(ring, MIDI_MSG_STOP, 0), "the stop can be read back");
}

static void testBothRings() {
  MidiRing ring;
  int offs = 0;
  while (ring.Push(ev(MIDI_MSG_NOTE_OFF, (uint8_t)(offs & 0x7F)))) {
    offs++;
  }
  int cap = (MidiRing::kLen - 1) + (MidiRing::kRelease - 1);
  expect(offs == cap, "note-offs fill the main ring and the tail");
  MidiDrops drops;
  ring.CopyDrops(&drops);
  expect(drops.noteOff == 1, "a note-off is counted only when both rings are full");
  ring.AddParser(4);
  ring.CopyDrops(&drops);
  expect(drops.parser == 4 && drops.noteOff == 1, "parser drops stay separate");
}

int main() {
  testOrder();
  testClockShed();
  testControllerShed();
  testNoteOffKept();
  testStopKept();
  testBothRings();
  if (failures) {
    std::printf("%d failed\n", failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
