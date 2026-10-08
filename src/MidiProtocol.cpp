#include "MidiProtocol.h"

BleMidiTimestamp bleMidiTimestamp(uint16_t millis13) {
  uint16_t ts = (uint16_t)(millis13 & 0x1FFF);
  BleMidiTimestamp out;
  out.headerMsb = (uint8_t)(0x80 | ((ts >> 7) & 0x3F));
  out.timestampLsb = (uint8_t)(0x80 | (ts & 0x7F));
  return out;
}

static int expectedDataBytes(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      return 2;
    case 0xC0:
    case 0xD0:
      return 1;
    default:
      break;
  }
  if (status == 0xF1 || status == 0xF3) {
    return 1;
  }
  if (status == 0xF2) {
    return 2;
  }
  return 0;
}

static void pushEvent(MidiParseResult *out, const MidiEvent &event) {
  if (out->count >= kMidiMaxEvents) {
    out->overflow++;
    return;
  }
  out->events[out->count++] = event;
}

static void emitRealtime(uint8_t status, uint16_t timestamp, MidiParseResult *out) {
  MidiEvent event;
  event.channel = 0;
  event.number = 0;
  event.value = 0;
  event.value14 = timestamp;
  switch (status) {
    case 0xF8: event.type = MIDI_MSG_CLOCK; break;
    case 0xFA: event.type = MIDI_MSG_START; break;
    case 0xFB: event.type = MIDI_MSG_CONTINUE; break;
    case 0xFC: event.type = MIDI_MSG_STOP; break;
    default: return;
  }
  pushEvent(out, event);
}

static void emitMessage(uint8_t status, const uint8_t *data, MidiRunningState *state, MidiParseResult *out) {
  uint8_t kind = status & 0xF0;
  uint8_t channel = status & 0x0F;
  if (kind == 0x90 || kind == 0x80) {
    MidiEvent event;
    event.channel = channel;
    event.number = (uint8_t)(data[0] & 0x7F);
    event.value = (uint8_t)(data[1] & 0x7F);
    event.value14 = 0;
    if (kind == 0x90 && event.value > 0) {
      event.type = MIDI_MSG_NOTE_ON;
    } else {
      event.type = MIDI_MSG_NOTE_OFF;
    }
    pushEvent(out, event);
    return;
  }
  if (kind == 0xB0) {
    uint8_t cc = (uint8_t)(data[0] & 0x7F);
    uint8_t val = (uint8_t)(data[1] & 0x7F);
    MidiEvent event;
    event.channel = channel;
    event.value = val;
    if (cc < 32) {
      state->msb[cc] = val;
      state->msbKnown[cc] = 1;
      event.type = MIDI_MSG_MSB;
      event.number = cc;
      uint8_t lsb = state->lsbKnown[cc] ? state->lsb[cc] : 0;
      event.value14 = (uint16_t)(((uint16_t)val << 7) | lsb);
      pushEvent(out, event);
    } else if (cc < 64) {
      uint8_t base = (uint8_t)(cc - 32);
      state->lsb[base] = val;
      state->lsbKnown[base] = 1;
      event.type = MIDI_MSG_LSB;
      event.number = base;
      uint8_t msb = state->msbKnown[base] ? state->msb[base] : 0;
      event.value14 = (uint16_t)(((uint16_t)msb << 7) | val);
      pushEvent(out, event);
    }
    return;
  }
  if (kind == 0xE0) {
    uint8_t lsb = (uint8_t)(data[0] & 0x7F);
    uint8_t msb = (uint8_t)(data[1] & 0x7F);
    uint16_t value14 = (uint16_t)(((uint16_t)msb << 7) | lsb);
    MidiEvent low;
    low.type = MIDI_MSG_LSB;
    low.channel = channel;
    low.number = kMidiPitchCc;
    low.value = lsb;
    low.value14 = value14;
    pushEvent(out, low);
    MidiEvent high;
    high.type = MIDI_MSG_MSB;
    high.channel = channel;
    high.number = kMidiPitchCc;
    high.value = msb;
    high.value14 = value14;
    pushEvent(out, high);
  }
}

int buildBleMidiPacket(uint8_t *buf, int bufLen, uint16_t millis13, MidiMsgType type, uint8_t channel, uint8_t number, uint8_t value) {
  if (!buf || bufLen < 5) {
    return 0;
  }
  if (number == kMidiPitchCc) {
    return 0;
  }
  uint8_t status = 0;
  uint8_t data1 = (uint8_t)(number & 0x7F);
  switch (type) {
    case MIDI_MSG_NOTE_ON:
      status = (uint8_t)(0x90 | (channel & 0x0F));
      break;
    case MIDI_MSG_NOTE_OFF:
      status = (uint8_t)(0x80 | (channel & 0x0F));
      break;
    case MIDI_MSG_MSB:
      if (number > 31) {
        return 0;
      }
      status = (uint8_t)(0xB0 | (channel & 0x0F));
      break;
    case MIDI_MSG_LSB:
      if (number > 31) {
        return 0;
      }
      status = (uint8_t)(0xB0 | (channel & 0x0F));
      data1 = (uint8_t)((number + 32) & 0x7F);
      break;
    default:
      return 0;
  }
  BleMidiTimestamp ts = bleMidiTimestamp(millis13);
  buf[0] = ts.headerMsb;
  buf[1] = ts.timestampLsb;
  buf[2] = status;
  buf[3] = data1;
  buf[4] = (uint8_t)(value & 0x7F);
  return 5;
}

// Remember the low 7 bits. If they wrap inside this packet, the high half
// ticks forward the way the BLE-MIDI spec tells the receiver to.
static uint16_t noteTimestamp(uint16_t *cursor, uint8_t timestampByte) {
  uint16_t low = (uint16_t)(timestampByte & 0x7F);
  uint16_t prevLow = (uint16_t)(*cursor & 0x7F);
  uint16_t high = (uint16_t)(*cursor & 0x1F80);
  if (low < prevLow) {
    high = (uint16_t)((high + 0x80) & 0x1F80);
  }
  *cursor = (uint16_t)(high | low);
  return *cursor;
}

static bool readDataBytes(const uint8_t *data, int len, int *i, uint8_t *raw, int needed, uint16_t timestamp, MidiParseResult *out) {
  int got = 0;
  while (got < needed && *i < len) {
    uint8_t b = data[*i];
    if (b >= 0xF8) {
      emitRealtime(b, timestamp, out);
      (*i)++;
      continue;
    }
    if (b & 0x80) {
      return false;
    }
    raw[got++] = b;
    (*i)++;
  }
  return got == needed;
}

int parseBleMidiPacket(const uint8_t *data, int len, MidiRunningState *state, MidiParseResult *out) {
  if (!out) {
    return 0;
  }
  out->count = 0;
  out->overflow = 0;
  if (!data || !state || len < 2 || (data[0] & 0x80) == 0) {
    return 0;
  }

  uint16_t timestamp = (uint16_t)((data[0] & 0x3F) << 7);
  int i = 1;
  uint8_t running = 0;
  bool sysex = state->sysex != 0;

  // The byte after the header is a timestamp, unless this packet is the
  // continuation of a SysEx, in which case it is raw SysEx data.
  if ((data[i] & 0x80) == 0 && sysex) {
    // continued SysEx, data[i] stays for the loop
  } else if (data[i] & 0x80) {
    noteTimestamp(&timestamp, data[i]);
    i++;
  } else {
    state->sysex = 0;
    return 0;
  }

  while (i < len) {
    if (sysex) {
      bool ended = false;
      while (i < len) {
        uint8_t b = data[i];
        if (b == 0xF7) {
          sysex = false;
          ended = true;
          i++;
          break;
        }
        if (b >= 0xF8) {
          emitRealtime(b, timestamp, out);
          i++;
          continue;
        }
        if (b & 0x80) {
          if (i + 1 >= len || (data[i + 1] & 0x80) == 0 || data[i + 1] == 0xF7 || data[i + 1] >= 0xF8) {
            noteTimestamp(&timestamp, b);
            i++;
            continue;
          }
          sysex = false;
          break;
        }
        i++;
      }
      if (!ended && sysex) {
        break;
      }
      if (i < len && (data[i] & 0x80) && data[i] < 0xF8) {
        noteTimestamp(&timestamp, data[i]);
        i++;
      }
      continue;
    }

    uint8_t b = data[i];
    bool usingRunning = false;
    if (b >= 0xF8) {
      emitRealtime(b, timestamp, out);
      i++;
      if (i < len && (data[i] & 0x80) && data[i] < 0xF8) {
        noteTimestamp(&timestamp, data[i]);
        i++;
      }
      continue;
    }
    if ((b & 0x80) == 0) {
      if (running == 0) {
        i++;
        continue;
      }
      usingRunning = true;
      b = running;
    } else {
      i++;
    }

    if (b == 0xF0) {
      sysex = true;
      continue;
    }
    if ((b & 0xF0) == 0xF0 && b < 0xF8 && expectedDataBytes(b) == 0) {
      if (i < len && (data[i] & 0x80) && data[i] < 0xF8) {
        noteTimestamp(&timestamp, data[i]);
        i++;
      }
      continue;
    }

    int needed = expectedDataBytes(b);
    if (needed <= 0) {
      break;
    }
    uint8_t raw[2];
    int dataAt = i;
    if (!readDataBytes(data, len, &i, raw, needed, timestamp, out)) {
      if (i >= len) {
        break;
      }
      // A timestamp (or a new status) arrived before this message finished.
      // Leave that byte for the next pass when it is a status; consume it
      // when the byte after it is also a status, because then it is the
      // timestamp the spec puts in front of the next message.
      if (!usingRunning && dataAt == i) {
        // The status itself was followed immediately by another status.
      }
      if ((data[i] & 0x80) && data[i] < 0xF8 && i + 1 < len && (data[i + 1] & 0x80) && data[i + 1] < 0xF8) {
        noteTimestamp(&timestamp, data[i]);
        i++;
      }
      continue;
    }

    if (b == 0xF2) {
      MidiEvent event;
      event.type = MIDI_MSG_SONG_POS;
      event.channel = 0;
      event.number = (uint8_t)(raw[0] & 0x7F);
      event.value = (uint8_t)(raw[1] & 0x7F);
      event.value14 = (uint16_t)((event.number) | ((uint16_t)event.value << 7));
      pushEvent(out, event);
    } else if (b < 0xF0) {
      running = b;
      emitMessage(b, raw, state, out);
    }

    if (i < len && (data[i] & 0x80) && data[i] < 0xF8) {
      noteTimestamp(&timestamp, data[i]);
      i++;
    }
  }

  state->sysex = sysex ? 1 : 0;
  return out->count;
}
