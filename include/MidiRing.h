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
// not a slot per 0xF8, and a poll applies that counter before the next
// event. The ring holds one chord, not a backlog: a note-on older than
// kStaleMs is dropped instead of sounding late. Start, Continue, Stop, and
// Song Position stay in the queue. Start clears ticks that are still
// pending, and ticks that sit before Start in the same packet are not
// counted. Controllers, then note-ons, give up a slot. Note-off and
// transport stay in order. A short overflow tail holds them when the main
// ring is already full of note-offs and transport. New events append to
// that tail until it drains, so a later note cannot pass an earlier note-off.
class MidiRing {
public:
  static const int kLen = 48;
  static const int kReserve = 8;
  static const int kControlReserve = 8;
  static const int kRelease = 16;
  static const int kGroups = 32;
  static const int kStaleMs = 40;

  MidiRing();
  // Arrival time, in milliseconds, stamped on the next Push. 0 disables
  // the stale-note drop for events pushed while it stays 0.
  void setArrival(uint16_t ms);
  bool Push(const MidiEvent &event);
  // One parsed notification, in order. Clocks before the first Start in
  // this packet are discarded, and that Start clears ticks still pending
  // from earlier packets.
  void PushParsed(const MidiEvent *events, int count);
  // `nowMs` is the consumer's millisecond clock. A note-on whose arrival
  // is older than kStaleMs is dropped. 0 keeps every note.
  int Poll(MidiEvent *out, int maxOut, uint16_t nowMs = 0);
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
  uint16_t mainAt[kLen];
  uint16_t relAt[kRelease];
  uint16_t arrivalMs;
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
  void addTicks(uint16_t timestamp, int count);
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
