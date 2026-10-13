#include "MidiRing.h"
#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
static portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;
#define RING_LOCK() portENTER_CRITICAL(&ringMux)
#define RING_UNLOCK() portEXIT_CRITICAL(&ringMux)
#else
#define RING_LOCK() ((void)0)
#define RING_UNLOCK() ((void)0)
#endif

static bool unshedable(const MidiEvent &event) {
  switch (event.type) {
    case MIDI_MSG_NOTE_OFF:
    case MIDI_MSG_START:
    case MIDI_MSG_CONTINUE:
    case MIDI_MSG_STOP:
    case MIDI_MSG_SONG_POS:
    case MIDI_MSG_ALL_OFF:
      return true;
    default:
      return false;
  }
}

static bool isController(const MidiEvent &event) {
  return event.type == MIDI_MSG_MSB || event.type == MIDI_MSG_LSB;
}

// Lower is shed first. 9 is kept. Clocks are not stored in the ring.
static int shedRank(const MidiEvent &event) {
  if (isController(event)) {
    return 1;
  }
  if (event.type == MIDI_MSG_NOTE_ON) {
    return 2;
  }
  return 9;
}

MidiRing::MidiRing() {
  memset(mainQ, 0, sizeof(mainQ));
  memset(relQ, 0, sizeof(relQ));
  memset(mainClocks, 0, sizeof(mainClocks));
  memset(relClocks, 0, sizeof(relClocks));
  memset(groups, 0, sizeof(groups));
  mainHead = 0;
  mainTail = 0;
  relHead = 0;
  relTail = 0;
  groupHead = 0;
  groupTail = 0;
  pendingTicks = 0;
  dropClock = 0;
  dropNoteOn = 0;
  dropNoteOff = 0;
  dropRing = 0;
  dropParser = 0;
}

uint32_t MidiRing::mainUsed() const {
  return (mainHead + (uint32_t)kLen - mainTail) % (uint32_t)kLen;
}

uint32_t MidiRing::relUsed() const {
  return (relHead + (uint32_t)kRelease - relTail) % (uint32_t)kRelease;
}

void MidiRing::addTicks(uint16_t timestamp, int count) {
  if (count < 1) {
    return;
  }
  if (count > 255) {
    count = 255;
  }
  timestamp = (uint16_t)(timestamp & 0x1FFF);
  if (groupTail != groupHead) {
    uint32_t last = (groupHead + (uint32_t)kGroups - 1) % (uint32_t)kGroups;
    if (groups[last].timestamp == timestamp && (int)groups[last].count <= 65535 - count) {
      groups[last].count = (uint16_t)(groups[last].count + count);
      pendingTicks += (uint32_t)count;
      return;
    }
  }
  uint32_t next = (groupHead + 1) % (uint32_t)kGroups;
  if (next != groupTail) {
    groups[groupHead].timestamp = timestamp;
    groups[groupHead].count = (uint16_t)count;
    groupHead = next;
    pendingTicks += (uint32_t)count;
    return;
  }
  // The group list is full. Keep the ticks on the latest group so the step
  // still moves. A later poll applies them with that group's timestamp.
  uint32_t last = (groupHead + (uint32_t)kGroups - 1) % (uint32_t)kGroups;
  if ((int)groups[last].count <= 65535 - count) {
    groups[last].count = (uint16_t)(groups[last].count + count);
    pendingTicks += (uint32_t)count;
    return;
  }
  dropClock += (uint32_t)count;
}

void MidiRing::dropUnbound() {
  uint32_t left = pendingTicks;
  while (left > 0 && groupTail != groupHead) {
    uint32_t last = (groupHead + (uint32_t)kGroups - 1) % (uint32_t)kGroups;
    if ((uint32_t)groups[last].count <= left) {
      left -= groups[last].count;
      groupHead = last;
    } else {
      groups[last].count = (uint16_t)(groups[last].count - left);
      left = 0;
    }
  }
  pendingTicks = 0;
}

bool MidiRing::emitClock(MidiEvent *out, uint32_t *budget) {
  if (!out || !budget || *budget == 0) {
    return false;
  }
  while (groupTail != groupHead && groups[groupTail].count == 0) {
    groupTail = (groupTail + 1) % (uint32_t)kGroups;
  }
  if (groupTail == groupHead) {
    // The budget has no group left. Drop it so the following event can move.
    *budget = 0;
    return false;
  }
  ClockGroup &group = groups[groupTail];
  uint32_t take = group.count;
  if (take > *budget) {
    take = *budget;
  }
  if (take > 255) {
    take = 255;
  }
  if (take == 0) {
    groupTail = (groupTail + 1) % (uint32_t)kGroups;
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->type = MIDI_MSG_CLOCK;
  out->value = (uint8_t)take;
  out->value14 = group.timestamp;
  group.count = (uint16_t)(group.count - take);
  *budget -= take;
  if (group.count == 0) {
    groupTail = (groupTail + 1) % (uint32_t)kGroups;
  }
  return true;
}

bool MidiRing::pushMain(const MidiEvent &event, uint32_t clocksBefore) {
  uint32_t next = (mainHead + 1) % (uint32_t)kLen;
  if (next == mainTail) {
    return false;
  }
  mainQ[mainHead] = event;
  mainClocks[mainHead] = clocksBefore;
  mainHead = next;
  return true;
}

bool MidiRing::pushRelease(const MidiEvent &event, uint32_t clocksBefore) {
  uint32_t next = (relHead + 1) % (uint32_t)kRelease;
  if (next == relTail) {
    return false;
  }
  relQ[relHead] = event;
  relClocks[relHead] = clocksBefore;
  relHead = next;
  return true;
}

void MidiRing::countDrop(const MidiEvent &event) {
  switch (event.type) {
    case MIDI_MSG_CLOCK:
      dropClock++;
      break;
    case MIDI_MSG_NOTE_ON:
      dropNoteOn++;
      break;
    case MIDI_MSG_NOTE_OFF:
      dropNoteOff++;
      break;
    default:
      dropRing++;
      break;
  }
}

bool MidiRing::shedOne(uint32_t *orphan) {
  if (orphan) {
    *orphan = 0;
  }
  uint32_t used = mainUsed();
  if (used == 0) {
    return false;
  }
  uint32_t idx = mainTail;
  int victim = -1;
  int best = 9;
  for (uint32_t n = 0; n < used; n++) {
    int rank = shedRank(mainQ[idx]);
    if (rank < best) {
      best = rank;
      victim = (int)idx;
      if (rank == 1) {
        break;
      }
    }
    idx = (idx + 1) % (uint32_t)kLen;
  }
  if (victim < 0 || best > 2) {
    return false;
  }
  uint32_t clocks = mainClocks[victim];
  bool wasLast = ((uint32_t)victim + 1) % (uint32_t)kLen == mainHead;
  countDrop(mainQ[victim]);
  uint32_t dst = (uint32_t)victim;
  uint32_t src = (dst + 1) % (uint32_t)kLen;
  while (src != mainHead) {
    mainQ[dst] = mainQ[src];
    mainClocks[dst] = mainClocks[src];
    dst = src;
    src = (src + 1) % (uint32_t)kLen;
  }
  mainHead = (mainHead + (uint32_t)kLen - 1) % (uint32_t)kLen;
  if (clocks > 0) {
    if (wasLast) {
      if (orphan) {
        *orphan = clocks;
      }
    } else {
      mainClocks[victim] += clocks;
    }
  }
  return true;
}

bool MidiRing::enqueueLocked(const MidiEvent &event, uint32_t clocksBefore) {
  if (relUsed() > 0) {
    // The tail is already holding note-offs. A note-on has to follow them,
    // not be discarded, or the note never sounds. Controllers give up the
    // slot. A note-on is dropped only when the tail itself is full.
    if (isController(event)) {
      dropRing++;
      return false;
    }
    if (pushRelease(event, clocksBefore)) {
      return true;
    }
    countDrop(event);
    return false;
  }

  uint32_t used = mainUsed();
  if (isController(event) && used >= (uint32_t)(kLen - 1 - kControlReserve)) {
    dropRing++;
    return false;
  }
  if (pushMain(event, clocksBefore)) {
    return true;
  }
  if (unshedable(event)) {
    uint32_t orphan = 0;
    if (shedOne(&orphan) && pushMain(event, clocksBefore + orphan)) {
      return true;
    }
    if (pushRelease(event, clocksBefore)) {
      return true;
    }
    if (event.type == MIDI_MSG_NOTE_OFF) {
      dropNoteOff++;
    } else {
      dropRing++;
    }
    return false;
  }
  countDrop(event);
  return false;
}

static int clockTicksOf(const MidiEvent &event) {
  if (event.type != MIDI_MSG_CLOCK) {
    return 0;
  }
  return event.value == 0 ? 1 : (int)event.value;
}

void MidiRing::ingestLocked(const MidiEvent &event) {
  int ticks = clockTicksOf(event);
  if (ticks > 0) {
    addTicks(event.value14, ticks);
    return;
  }
  if (event.type == MIDI_MSG_START) {
    dropUnbound();
    enqueueLocked(event, 0);
    return;
  }
  uint32_t before = pendingTicks;
  pendingTicks = 0;
  if (!enqueueLocked(event, before)) {
    pendingTicks += before;
  }
}

bool MidiRing::Push(const MidiEvent &event) {
  RING_LOCK();
  int ticks = clockTicksOf(event);
  if (ticks > 0) {
    addTicks(event.value14, ticks);
    RING_UNLOCK();
    return true;
  }
  if (event.type == MIDI_MSG_START) {
    dropUnbound();
    bool ok = enqueueLocked(event, 0);
    RING_UNLOCK();
    return ok;
  }
  uint32_t before = pendingTicks;
  pendingTicks = 0;
  bool ok = enqueueLocked(event, before);
  if (!ok) {
    pendingTicks += before;
  }
  RING_UNLOCK();
  return ok;
}

void MidiRing::PushParsed(const MidiEvent *events, int count) {
  if (!events || count <= 0) {
    return;
  }
  RING_LOCK();
  int startAt = -1;
  for (int i = 0; i < count; i++) {
    if (events[i].type == MIDI_MSG_START) {
      startAt = i;
      break;
    }
  }
  if (startAt >= 0) {
    for (int i = 0; i < startAt; i++) {
      if (events[i].type == MIDI_MSG_CLOCK) {
        continue;
      }
      enqueueLocked(events[i], 0);
    }
    dropUnbound();
    enqueueLocked(events[startAt], 0);
    for (int i = startAt + 1; i < count; i++) {
      ingestLocked(events[i]);
    }
  } else {
    for (int i = 0; i < count; i++) {
      ingestLocked(events[i]);
    }
  }
  RING_UNLOCK();
}

int MidiRing::Poll(MidiEvent *out, int maxOut) {
  if (!out || maxOut <= 0) {
    return 0;
  }
  RING_LOCK();
  int n = 0;
  while (n < maxOut) {
    if (mainTail != mainHead) {
      if (mainClocks[mainTail] > 0) {
        if (!emitClock(&out[n], &mainClocks[mainTail])) {
          break;
        }
        n++;
        continue;
      }
      out[n++] = mainQ[mainTail];
      mainTail = (mainTail + 1) % (uint32_t)kLen;
      continue;
    }
    if (relTail != relHead) {
      if (relClocks[relTail] > 0) {
        if (!emitClock(&out[n], &relClocks[relTail])) {
          break;
        }
        n++;
        continue;
      }
      out[n++] = relQ[relTail];
      relTail = (relTail + 1) % (uint32_t)kRelease;
      continue;
    }
    if (pendingTicks > 0) {
      if (!emitClock(&out[n], &pendingTicks)) {
        break;
      }
      n++;
      continue;
    }
    break;
  }
  RING_UNLOCK();
  return n;
}

void MidiRing::Flush() {
  RING_LOCK();
  mainTail = mainHead;
  relTail = relHead;
  groupTail = groupHead;
  pendingTicks = 0;
  RING_UNLOCK();
}

void MidiRing::AddParser(uint32_t n) {
  if (n == 0) {
    return;
  }
  RING_LOCK();
  dropParser += n;
  RING_UNLOCK();
}

void MidiRing::CopyDrops(MidiDrops *out) const {
  if (!out) {
    return;
  }
  RING_LOCK();
  out->clock = dropClock;
  out->noteOn = dropNoteOn;
  out->noteOff = dropNoteOff;
  out->ringFull = dropRing;
  out->parser = dropParser;
  RING_UNLOCK();
}

uint32_t MidiRing::PendingTicks() const {
  RING_LOCK();
  uint32_t n = pendingTicks;
  RING_UNLOCK();
  return n;
}
