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

// Lower is shed first. 9 is kept.
static int shedRank(const MidiEvent &event) {
  if (event.type == MIDI_MSG_CLOCK) {
    return 0;
  }
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
  mainHead = 0;
  mainTail = 0;
  relHead = 0;
  relTail = 0;
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

bool MidiRing::pushMain(const MidiEvent &event) {
  uint32_t next = (mainHead + 1) % (uint32_t)kLen;
  if (next == mainTail) {
    return false;
  }
  mainQ[mainHead] = event;
  mainHead = next;
  return true;
}

bool MidiRing::pushRelease(const MidiEvent &event) {
  uint32_t next = (relHead + 1) % (uint32_t)kRelease;
  if (next == relTail) {
    return false;
  }
  relQ[relHead] = event;
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

bool MidiRing::shedOne() {
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
      if (rank == 0) {
        break;
      }
    }
    idx = (idx + 1) % (uint32_t)kLen;
  }
  if (victim < 0 || best > 2) {
    return false;
  }
  countDrop(mainQ[victim]);
  uint32_t dst = (uint32_t)victim;
  uint32_t src = (dst + 1) % (uint32_t)kLen;
  while (src != mainHead) {
    mainQ[dst] = mainQ[src];
    dst = src;
    src = (src + 1) % (uint32_t)kLen;
  }
  mainHead = (mainHead + (uint32_t)kLen - 1) % (uint32_t)kLen;
  return true;
}

bool MidiRing::pushLocked(const MidiEvent &event) {
  if (relUsed() > 0) {
    if (!unshedable(event)) {
      countDrop(event);
      return false;
    }
    if (pushRelease(event)) {
      return true;
    }
    if (event.type == MIDI_MSG_NOTE_OFF) {
      dropNoteOff++;
    } else {
      dropRing++;
    }
    return false;
  }

  uint32_t used = mainUsed();
  if (event.type == MIDI_MSG_CLOCK && used >= (uint32_t)(kLen - 1 - kReserve)) {
    dropClock++;
    return false;
  }
  if (isController(event) && used >= (uint32_t)(kLen - 1 - kControlReserve)) {
    dropRing++;
    return false;
  }
  if (pushMain(event)) {
    return true;
  }
  if (unshedable(event)) {
    if (shedOne() && pushMain(event)) {
      return true;
    }
    if (pushRelease(event)) {
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

bool MidiRing::Push(const MidiEvent &event) {
  RING_LOCK();
  bool ok = pushLocked(event);
  RING_UNLOCK();
  return ok;
}

int MidiRing::Poll(MidiEvent *out, int maxOut) {
  if (!out || maxOut <= 0) {
    return 0;
  }
  RING_LOCK();
  int n = 0;
  while (n < maxOut && mainTail != mainHead) {
    out[n++] = mainQ[mainTail];
    mainTail = (mainTail + 1) % (uint32_t)kLen;
  }
  while (n < maxOut && relTail != relHead) {
    out[n++] = relQ[relTail];
    relTail = (relTail + 1) % (uint32_t)kRelease;
  }
  RING_UNLOCK();
  return n;
}

void MidiRing::Flush() {
  RING_LOCK();
  mainTail = mainHead;
  relTail = relHead;
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
