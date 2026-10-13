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

// One FIFO from the BLE host task to the audio task. Clocks are a counter,
// not a slot per 0xF8. The audio task applies that many ticks, in order,
// before the next queued event. Start, Continue, Stop, and Song Position
// stay in the queue. Start clears ticks that are still pending, and ticks
// that sit before Start in the same packet are not counted. Controllers,
// then note-ons, give up a slot. Note-off and transport stay in order.
// A short overflow tail holds them when the main ring is already full of
// note-offs and transport. New events append to that tail until it drains,
// so a later note cannot pass an earlier note-off.
class MidiRing {
public:
  static const int kLen = 256;
  static const int kReserve = 24;
  static const int kControlReserve = 8;
  static const int kRelease = 32;
  static const int kGroups = 64;

  MidiRing();
  bool Push(const MidiEvent &event);
  // One parsed notification, in order. Clocks before the first Start in
  // this packet are discarded, and that Start clears ticks still pending
  // from earlier packets.
  void PushParsed(const MidiEvent *events, int count);
  int Poll(MidiEvent *out, int maxOut);
  void Flush();
  void AddParser(uint32_t n);
  void CopyDrops(MidiDrops *out) const;
  uint32_t PendingTicks() const;

private:
  struct ClockGroup {
    uint16_t timestamp;
    uint16_t count;
  };

  MidiEvent mainQ[kLen];
  MidiEvent relQ[kRelease];
  uint32_t mainClocks[kLen];
  uint32_t relClocks[kRelease];
  ClockGroup groups[kGroups];
  uint32_t mainHead;
  uint32_t mainTail;
  uint32_t relHead;
  uint32_t relTail;
  uint32_t groupHead;
  uint32_t groupTail;
  uint32_t pendingTicks;
  uint32_t dropClock;
  uint32_t dropNoteOn;
  uint32_t dropNoteOff;
  uint32_t dropRing;
  uint32_t dropParser;

  uint32_t mainUsed() const;
  uint32_t relUsed() const;
  void addTick(uint16_t timestamp);
  void dropUnbound();
  bool emitClock(MidiEvent *out, uint32_t *budget);
  bool pushMain(const MidiEvent &event, uint32_t clocksBefore);
  bool pushRelease(const MidiEvent &event, uint32_t clocksBefore);
  bool shedOne(uint32_t *orphan);
  void countDrop(const MidiEvent &event);
  bool enqueueLocked(const MidiEvent &event, uint32_t clocksBefore);
  void ingestLocked(const MidiEvent &event);
};

#endif
