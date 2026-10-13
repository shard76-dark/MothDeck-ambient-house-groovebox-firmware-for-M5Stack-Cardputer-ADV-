#ifndef MidiRing_h
#define MidiRing_h
#include "MidiProtocol.h"

// Drops are counted by kind. The normal screens do not print these.
struct MidiDrops {
  uint32_t clock;
  uint32_t noteOn;
  uint32_t noteOff;
  uint32_t ringFull;
  uint32_t parser;
};

// One FIFO from the BLE host task to the audio task. Clocks, then
// controllers, then note-ons are the events that give up a slot. Note-off
// and transport stay in order and are not dropped while any of those are
// still queued. A short overflow tail holds them when the main ring is
// already full of note-offs and transport. New events append to that tail
// until it drains, so a later note cannot pass an earlier note-off.
class MidiRing {
public:
  static const int kLen = 256;
  static const int kReserve = 24;
  static const int kControlReserve = 8;
  static const int kRelease = 32;

  MidiRing();
  bool Push(const MidiEvent &event);
  int Poll(MidiEvent *out, int maxOut);
  void Flush();
  void AddParser(uint32_t n);
  void CopyDrops(MidiDrops *out) const;

private:
  MidiEvent mainQ[kLen];
  MidiEvent relQ[kRelease];
  uint32_t mainHead;
  uint32_t mainTail;
  uint32_t relHead;
  uint32_t relTail;
  uint32_t dropClock;
  uint32_t dropNoteOn;
  uint32_t dropNoteOff;
  uint32_t dropRing;
  uint32_t dropParser;

  uint32_t mainUsed() const;
  uint32_t relUsed() const;
  bool pushMain(const MidiEvent &event);
  bool pushRelease(const MidiEvent &event);
  bool shedOne();
  void countDrop(const MidiEvent &event);
  bool pushLocked(const MidiEvent &event);
};

#endif
