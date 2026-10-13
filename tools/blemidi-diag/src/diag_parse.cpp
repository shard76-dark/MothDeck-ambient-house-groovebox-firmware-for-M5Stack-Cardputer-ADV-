#include "diag_parse.h"

#include <string.h>

void diagStateClear(DiagState *state) {
  if (state) {
    state->sysex = 0;
  }
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

// Same wrap rule as src/MidiProtocol.cpp: the low 7 bits move backward only
// when the high half has ticked.
static DiagPacket *gCollect = nullptr;

static uint16_t noteTimestamp(uint16_t *cursor, uint8_t timestampByte) {
  if (gCollect && gCollect->tsLoN < kDiagTsLoMax) {
    gCollect->tsLo[gCollect->tsLoN++] = (uint8_t)(timestampByte & 0x7F);
  }
  uint16_t low = (uint16_t)(timestampByte & 0x7F);
  uint16_t prevLow = (uint16_t)(*cursor & 0x7F);
  uint16_t high = (uint16_t)(*cursor & 0x1F80);
  if (low < prevLow) {
    high = (uint16_t)((high + 0x80) & 0x1F80);
  }
  *cursor = (uint16_t)(high | low);
  return *cursor;
}

static void pushMsg(DiagPacket *out, const char *type, int channel, const uint8_t *data, int dataLen, bool running, bool vel0, uint16_t ts, int sysexOff, int sysexLen, bool sysexEnded) {
  if (out->count >= kDiagMaxMsg) {
    out->errors++;
    return;
  }
  DiagMsg *msg = &out->msg[out->count++];
  memset(msg, 0, sizeof(*msg));
  strncpy(msg->type, type, sizeof(msg->type) - 1);
  msg->channel = channel;
  msg->dataLen = dataLen;
  if (data && dataLen > 0) {
    int n = dataLen > 2 ? 2 : dataLen;
    memcpy(msg->data, data, (size_t)n);
    msg->dataLen = n;
  }
  msg->running = running;
  msg->vel0 = vel0;
  msg->bleTs = ts;
  msg->sysexOff = sysexOff;
  msg->sysexLen = sysexLen;
  msg->sysexEnded = sysexEnded;
}

static int appendSysex(DiagPacket *out, uint8_t byte) {
  if (out->sysexLen >= kDiagSysexCap) {
    return -1;
  }
  int off = out->sysexLen;
  out->sysex[out->sysexLen++] = byte;
  return off;
}

static bool readDataBytes(const uint8_t *data, int len, int *i, uint8_t *raw, int needed, uint16_t timestamp, DiagPacket *out) {
  int got = 0;
  while (got < needed && *i < len) {
    uint8_t b = data[*i];
    if (b >= 0xF8) {
      const char *type = "realtime";
      if (b == 0xF8) {
        type = "clock";
      } else if (b == 0xFA) {
        type = "start";
      } else if (b == 0xFB) {
        type = "continue";
      } else if (b == 0xFC) {
        type = "stop";
      } else if (b == 0xFE) {
        type = "active";
      } else if (b == 0xFF) {
        type = "reset";
      }
      pushMsg(out, type, 0, nullptr, 0, false, false, timestamp, -1, 0, false);
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

static void emitChannel(uint8_t status, const uint8_t *raw, bool running, uint16_t ts, DiagPacket *out) {
  uint8_t kind = status & 0xF0;
  int channel = (status & 0x0F) + 1;
  if (kind == 0x90 || kind == 0x80) {
    bool vel0 = kind == 0x90 && (raw[1] & 0x7F) == 0;
    const char *type = "note_on";
    if (kind == 0x80 || vel0) {
      type = "note_off";
    }
    uint8_t data[2] = {(uint8_t)(raw[0] & 0x7F), (uint8_t)(raw[1] & 0x7F)};
    pushMsg(out, type, channel, data, 2, running, vel0, ts, -1, 0, false);
    return;
  }
  if (kind == 0xB0) {
    uint8_t data[2] = {(uint8_t)(raw[0] & 0x7F), (uint8_t)(raw[1] & 0x7F)};
    pushMsg(out, "cc", channel, data, 2, running, false, ts, -1, 0, false);
    return;
  }
  if (kind == 0xE0) {
    uint8_t data[2] = {(uint8_t)(raw[0] & 0x7F), (uint8_t)(raw[1] & 0x7F)};
    pushMsg(out, "pitch", channel, data, 2, running, false, ts, -1, 0, false);
    return;
  }
  if (kind == 0xA0) {
    uint8_t data[2] = {(uint8_t)(raw[0] & 0x7F), (uint8_t)(raw[1] & 0x7F)};
    pushMsg(out, "poly", channel, data, 2, running, false, ts, -1, 0, false);
    return;
  }
  if (kind == 0xC0) {
    uint8_t data[1] = {(uint8_t)(raw[0] & 0x7F)};
    pushMsg(out, "program", channel, data, 1, running, false, ts, -1, 0, false);
    return;
  }
  if (kind == 0xD0) {
    uint8_t data[1] = {(uint8_t)(raw[0] & 0x7F)};
    pushMsg(out, "pressure", channel, data, 1, running, false, ts, -1, 0, false);
  }
}

static void emitSystem(uint8_t status, const uint8_t *raw, uint16_t ts, DiagPacket *out) {
  if (status == 0xF2) {
    uint8_t data[2] = {(uint8_t)(raw[0] & 0x7F), (uint8_t)(raw[1] & 0x7F)};
    pushMsg(out, "song_pos", 0, data, 2, false, false, ts, -1, 0, false);
    return;
  }
  if (status == 0xF1) {
    uint8_t data[1] = {(uint8_t)(raw[0] & 0x7F)};
    pushMsg(out, "qframe", 0, data, 1, false, false, ts, -1, 0, false);
    return;
  }
  if (status == 0xF3) {
    uint8_t data[1] = {(uint8_t)(raw[0] & 0x7F)};
    pushMsg(out, "song_sel", 0, data, 1, false, false, ts, -1, 0, false);
    return;
  }
  if (status == 0xF6) {
    pushMsg(out, "tune", 0, nullptr, 0, false, false, ts, -1, 0, false);
  }
}

static void emitRealtimeByte(uint8_t status, uint16_t timestamp, DiagPacket *out) {
  const char *type = nullptr;
  if (status == 0xF8) {
    type = "clock";
  } else if (status == 0xFA) {
    type = "start";
  } else if (status == 0xFB) {
    type = "continue";
  } else if (status == 0xFC) {
    type = "stop";
  } else if (status == 0xFE) {
    type = "active";
  } else if (status == 0xFF) {
    type = "reset";
  } else {
    return;
  }
  pushMsg(out, type, 0, nullptr, 0, false, false, timestamp, -1, 0, false);
}

// Same rule as MothDeck's parser. A high bit is a timestamp only when the
// next byte is also a status, or when running status is already in effect
// and the next byte is its data. A status followed by data, with nothing
// running, is the message. Treating it as a timestamp drops the note.
static bool takeTimestamp(const uint8_t *data, int len, int *i, uint16_t *timestamp, bool running) {
  if (!data || !i || !timestamp || *i >= len) {
    return false;
  }
  uint8_t b = data[*i];
  if ((b & 0x80) == 0 || b >= 0xF8) {
    return false;
  }
  bool nextIsStatus = *i + 1 >= len || (data[*i + 1] & 0x80) != 0;
  if (!nextIsStatus && !running) {
    return false;
  }
  noteTimestamp(timestamp, b);
  (*i)++;
  return true;
}

void diagParse(const uint8_t *data, int len, DiagState *state, DiagPacket *out) {
  if (!out) {
    return;
  }
  memset(out, 0, sizeof(*out));
  gCollect = out;
  if (data && len >= 1) {
    out->hdr = data[0];
    out->tsHi = (uint8_t)(data[0] & 0x3F);
  }
  for (int i = 0; i < kDiagMaxMsg; i++) {
    out->msg[i].sysexOff = -1;
  }
  if (!state || !data || len < 1 || (data[0] & 0x80) == 0) {
    out->rejected = true;
    out->errors = 1;
    if (state) {
      state->sysex = 0;
    }
    gCollect = nullptr;
    return;
  }

  uint16_t timestamp = (uint16_t)((data[0] & 0x3F) << 7);
  if (len < 2) {
    out->bleTs = timestamp;
    out->rejected = true;
    out->errors = 1;
    gCollect = nullptr;
    return;
  }

  int i = 1;
  uint8_t running = 0;
  bool sysex = state->sysex != 0;
  int sysexStart = -1;

  if ((data[i] & 0x80) == 0 && sysex) {
    // Continued SysEx. The first byte is payload, not a timestamp.
  } else if (data[i] >= 0xF8) {
    // Real-time is never a timestamp. The header time stands.
  } else if ((data[i] & 0x80) == 0) {
    state->sysex = 0;
    out->bleTs = timestamp;
    out->rejected = true;
    out->errors = 1;
    gCollect = nullptr;
    return;
  } else {
    takeTimestamp(data, len, &i, &timestamp, false);
  }
  out->bleTs = timestamp;

  while (i < len) {
    if (sysex) {
      bool ended = false;
      if (sysexStart < 0) {
        sysexStart = out->sysexLen;
      }
      while (i < len) {
        uint8_t b = data[i];
        if (b == 0xF7) {
          sysex = false;
          ended = true;
          i++;
          break;
        }
        if (b >= 0xF8) {
          emitRealtimeByte(b, timestamp, out);
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
        appendSysex(out, b);
        i++;
      }
      int slen = sysexStart >= 0 ? out->sysexLen - sysexStart : 0;
      pushMsg(out, "sysex", 0, nullptr, 0, false, false, timestamp, sysexStart, slen, ended);
      sysexStart = -1;
      if (!ended && sysex) {
        break;
      }
      takeTimestamp(data, len, &i, &timestamp, running != 0);
      continue;
    }

    uint8_t b = data[i];
    bool usingRunning = false;
    if (b >= 0xF8) {
      emitRealtimeByte(b, timestamp, out);
      i++;
      takeTimestamp(data, len, &i, &timestamp, running != 0);
      continue;
    }
    if ((b & 0x80) == 0) {
      if (running == 0) {
        out->errors++;
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
      sysexStart = out->sysexLen;
      continue;
    }
    if (b == 0xF7) {
      pushMsg(out, "sysex_end", 0, nullptr, 0, false, false, timestamp, -1, 0, true);
      takeTimestamp(data, len, &i, &timestamp, running != 0);
      continue;
    }
    if (b == 0xF6) {
      emitSystem(b, nullptr, timestamp, out);
      takeTimestamp(data, len, &i, &timestamp, running != 0);
      continue;
    }
    if ((b & 0xF0) == 0xF0 && b < 0xF8 && expectedDataBytes(b) == 0) {
      takeTimestamp(data, len, &i, &timestamp, running != 0);
      continue;
    }

    int needed = expectedDataBytes(b);
    if (needed <= 0) {
      out->errors++;
      break;
    }
    uint8_t raw[2] = {0, 0};
    int dataAt = i;
    if (!readDataBytes(data, len, &i, raw, needed, timestamp, out)) {
      if (i >= len) {
        out->errors++;
        break;
      }
      if (!usingRunning && dataAt == i) {
        // The status was followed immediately by another status.
      }
      if ((data[i] & 0x80) && data[i] < 0xF8 && i + 1 < len && (data[i + 1] & 0x80) && data[i + 1] < 0xF8) {
        noteTimestamp(&timestamp, data[i]);
        i++;
      }
      out->errors++;
      continue;
    }

    if (b >= 0xF0) {
      emitSystem(b, raw, timestamp, out);
    } else {
      running = b;
      emitChannel(b, raw, usingRunning, timestamp, out);
    }
      takeTimestamp(data, len, &i, &timestamp, running != 0);
  }

  state->sysex = sysex ? 1 : 0;
  gCollect = nullptr;
}
