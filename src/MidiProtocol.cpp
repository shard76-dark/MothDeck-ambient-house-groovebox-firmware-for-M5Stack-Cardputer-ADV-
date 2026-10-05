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
    return;
  }
  out->events[out->count++] = event;
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

int parseBleMidiPacket(const uint8_t *data, int len, MidiRunningState *state, MidiParseResult *out) {
  if (!out) {
    return 0;
  }
  out->count = 0;
  if (!data || !state || len < 2 || (data[0] & 0x80) == 0) {
    return 0;
  }

  int i = 1;
  uint8_t running = 0;
  bool sysex = false;
  while (i < len && out->count < kMidiMaxEvents) {
    uint8_t b = data[i];
    if (sysex) {
      i++;
      if (b == 0xF7) {
        sysex = false;
      }
      continue;
    }
    if (b >= 0xF8) {
      i++;
      continue;
    }
    if (b & 0x80) {
      if (i + 1 < len && (data[i + 1] & 0x80) && data[i + 1] < 0xF8) {
        i++;
        running = data[i];
        i++;
      } else if (i + 1 < len && (data[i + 1] & 0x80) == 0 && running != 0) {
        i++;
      } else {
        running = b;
        i++;
      }
      if (running == 0xF0) {
        sysex = true;
        continue;
      }
    }
    int needed = expectedDataBytes(running);
    if (needed <= 0 || i + needed > len) {
      break;
    }
    bool dataOk = true;
    for (int k = 0; k < needed; k++) {
      if (data[i + k] & 0x80) {
        dataOk = false;
        break;
      }
    }
    if (!dataOk) {
      break;
    }
    emitMessage(running, data + i, state, out);
    i += needed;
  }
  return out->count;
}
