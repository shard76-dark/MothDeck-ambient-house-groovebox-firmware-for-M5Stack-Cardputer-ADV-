#include "diag_selftest.h"

#include <stdio.h>
#include <string.h>

static bool ignoredByParser(const DiagMsg *msg) {
  if (strcmp(msg->type, "sysex") == 0 || strcmp(msg->type, "sysex_end") == 0) {
    return true;
  }
  if (strcmp(msg->type, "program") == 0 || strcmp(msg->type, "pressure") == 0 || strcmp(msg->type, "poly") == 0) {
    return true;
  }
  if (strcmp(msg->type, "tune") == 0 || strcmp(msg->type, "active") == 0 || strcmp(msg->type, "reset") == 0) {
    return true;
  }
  if (strcmp(msg->type, "song_sel") == 0 || strcmp(msg->type, "qframe") == 0 || strcmp(msg->type, "realtime") == 0) {
    return true;
  }
  if (strcmp(msg->type, "cc") == 0) {
    int cc = msg->dataLen > 0 ? msg->data[0] : -1;
    if (cc >= 64 && cc != 120 && cc != 123) {
      return true;
    }
  }
  return false;
}

static bool sameChannel(const DiagMsg *msg, const MidiEvent *ev) {
  return msg->channel == (int)ev->channel + 1;
}

static bool matchOne(const DiagMsg *msg, const MidiEvent *ev) {
  if (ev->type == MIDI_MSG_NOTE_ON) {
    return strcmp(msg->type, "note_on") == 0 && sameChannel(msg, ev) && msg->dataLen == 2 && msg->data[0] == ev->number && msg->data[1] == ev->value;
  }
  if (ev->type == MIDI_MSG_NOTE_OFF) {
    return strcmp(msg->type, "note_off") == 0 && sameChannel(msg, ev) && msg->dataLen == 2 && msg->data[0] == ev->number && msg->data[1] == ev->value;
  }
  if (ev->type == MIDI_MSG_MSB && ev->number != kMidiPitchCc) {
    return strcmp(msg->type, "cc") == 0 && sameChannel(msg, ev) && msg->data[0] == ev->number && msg->data[1] == ev->value;
  }
  if (ev->type == MIDI_MSG_LSB && ev->number != kMidiPitchCc) {
    return strcmp(msg->type, "cc") == 0 && sameChannel(msg, ev) && msg->data[0] == (uint8_t)(ev->number + 32) && msg->data[1] == ev->value;
  }
  if (ev->type == MIDI_MSG_ALL_OFF) {
    return strcmp(msg->type, "cc") == 0 && sameChannel(msg, ev) && msg->data[0] == ev->number && msg->data[1] == ev->value;
  }
  if (ev->type == MIDI_MSG_CLOCK) {
    return false;
  }
  if (ev->type == MIDI_MSG_START) {
    return strcmp(msg->type, "start") == 0 && msg->bleTs == ev->value14;
  }
  if (ev->type == MIDI_MSG_CONTINUE) {
    return strcmp(msg->type, "continue") == 0 && msg->bleTs == ev->value14;
  }
  if (ev->type == MIDI_MSG_STOP) {
    return strcmp(msg->type, "stop") == 0 && msg->bleTs == ev->value14;
  }
  if (ev->type == MIDI_MSG_SONG_POS) {
    uint16_t pos = (uint16_t)(msg->data[0] | ((uint16_t)msg->data[1] << 7));
    return strcmp(msg->type, "song_pos") == 0 && pos == ev->value14;
  }
  return false;
}

bool diagMatchesParser(const DiagPacket *pkt, const MidiParseResult *parsed) {
  if (!pkt || !parsed) {
    return false;
  }
  int di = 0;
  for (int pi = 0; pi < parsed->count; pi++) {
    const MidiEvent *ev = &parsed->events[pi];
    if ((ev->type == MIDI_MSG_LSB || ev->type == MIDI_MSG_MSB) && ev->number == kMidiPitchCc) {
      while (di < pkt->count && ignoredByParser(&pkt->msg[di])) {
        di++;
      }
      if (di >= pkt->count || strcmp(pkt->msg[di].type, "pitch") != 0) {
        return false;
      }
      const DiagMsg *msg = &pkt->msg[di];
      if (ev->type == MIDI_MSG_LSB) {
        if (pi + 1 >= parsed->count) {
          return false;
        }
        const MidiEvent *hi = &parsed->events[pi + 1];
        if (hi->type != MIDI_MSG_MSB || hi->number != kMidiPitchCc) {
          return false;
        }
        if (msg->dataLen != 2 || msg->data[0] != ev->value || msg->data[1] != hi->value || msg->channel != (int)ev->channel + 1) {
          return false;
        }
        pi++;
      }
      di++;
      continue;
    }
    if (ev->type == MIDI_MSG_CLOCK) {
      // Clocks that share a timestamp are one parser event. value is the count.
      int need = ev->value == 0 ? 1 : (int)ev->value;
      for (int n = 0; n < need; n++) {
        while (di < pkt->count && ignoredByParser(&pkt->msg[di])) {
          di++;
        }
        if (di >= pkt->count || strcmp(pkt->msg[di].type, "clock") != 0 || pkt->msg[di].bleTs != ev->value14) {
          return false;
        }
        di++;
      }
      continue;
    }
    while (di < pkt->count && ignoredByParser(&pkt->msg[di])) {
      di++;
    }
    if (di >= pkt->count || !matchOne(&pkt->msg[di], ev)) {
      return false;
    }
    di++;
  }
  while (di < pkt->count && ignoredByParser(&pkt->msg[di])) {
    di++;
  }
  return di == pkt->count;
}

static void emitLine(void (*emit)(const char *line, void *ctx), void *ctx, const char *name, bool ok, const char *detail) {
  char line[192];
  if (ok) {
    snprintf(line, sizeof(line), "{\"t\":\"selftest\",\"case\":\"%s\",\"ok\":true}", name);
  } else {
    snprintf(line, sizeof(line), "{\"t\":\"selftest\",\"case\":\"%s\",\"ok\":false,\"detail\":\"%s\"}", name, detail ? detail : "mismatch");
  }
  if (emit) {
    emit(line, ctx);
  }
}

static bool parseBoth(const uint8_t *data, int len, DiagState *dstate, MidiRunningState *mstate, DiagPacket *pkt, MidiParseResult *parsed) {
  diagParse(data, len, dstate, pkt);
  parseBleMidiPacket(data, len, mstate, parsed);
  return diagMatchesParser(pkt, parsed);
}

static const DiagMsg *findType(const DiagPacket *pkt, const char *type, int which) {
  int seen = 0;
  for (int i = 0; i < pkt->count; i++) {
    if (strcmp(pkt->msg[i].type, type) == 0) {
      if (seen == which) {
        return &pkt->msg[i];
      }
      seen++;
    }
  }
  return nullptr;
}

int diagSelfTest(void (*emit)(const char *line, void *ctx), void *ctx) {
  int failed = 0;
  DiagState dstate;
  MidiRunningState mstate;
  DiagPacket pkt;
  MidiParseResult parsed;

  diagStateClear(&dstate);
  memset(&mstate, 0, sizeof(mstate));

  const uint8_t running[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 62, 110};
  bool runOk = parseBoth(running, (int)sizeof(running), &dstate, &mstate, &pkt, &parsed);
  const DiagMsg *second = pkt.count >= 2 ? &pkt.msg[1] : nullptr;
  if (!runOk || parsed.count != 2 || parsed.events[1].type != MIDI_MSG_NOTE_ON || parsed.events[1].number != 62 || parsed.events[1].value != 110) {
    runOk = false;
  }
  if (!second || !second->running || second->bleTs != 1 || second->data[0] != 62) {
    runOk = false;
  }
  if (pkt.msg[0].running || pkt.msg[0].bleTs != 0) {
    runOk = false;
  }
  emitLine(emit, ctx, "running_status", runOk, "running status note");
  failed += runOk ? 0 : 1;

  diagStateClear(&dstate);
  memset(&mstate, 0, sizeof(mstate));
  const uint8_t multi[] = {0x80, 0x80, 0x90, 60, 100, 0x81, 0x80, 36, 40, 0x82, 0xB0, 7, 90};
  bool multiOk = parseBoth(multi, (int)sizeof(multi), &dstate, &mstate, &pkt, &parsed);
  if (!multiOk || parsed.count != 3 || parsed.events[0].type != MIDI_MSG_NOTE_ON || parsed.events[1].type != MIDI_MSG_NOTE_OFF || parsed.events[2].type != MIDI_MSG_MSB) {
    multiOk = false;
  }
  if (parsed.events[2].number != 7 || parsed.events[2].value != 90) {
    multiOk = false;
  }
  if (pkt.count != 3 || pkt.msg[0].running || pkt.msg[1].running || pkt.msg[2].running) {
    multiOk = false;
  }
  if (pkt.msg[0].bleTs != 0 || pkt.msg[1].bleTs != 1 || pkt.msg[2].bleTs != 2) {
    multiOk = false;
  }
  emitLine(emit, ctx, "multi_message", multiOk, "several messages in one packet");
  failed += multiOk ? 0 : 1;

  diagStateClear(&dstate);
  memset(&mstate, 0, sizeof(mstate));
  // Low 7 bits of 0x7F would be the byte 0xFF, which is System Reset, so a
  // timestamp byte cannot carry it. 0x70 is the highest low half that is
  // still a timestamp (0xF0). The high half is 0x1F80, and the next low
  // half of 0 wraps it to 0.
  const uint8_t wrap[] = {0xBF, 0xF0, 0xF8, 0x80, 0xF8};
  bool wrapOk = parseBoth(wrap, (int)sizeof(wrap), &dstate, &mstate, &pkt, &parsed);
  if (!wrapOk || parsed.count != 2 || parsed.events[0].type != MIDI_MSG_CLOCK || parsed.events[1].type != MIDI_MSG_CLOCK) {
    wrapOk = false;
  }
  if (parsed.events[0].value14 != 0x1FF0 || parsed.events[1].value14 != 0) {
    wrapOk = false;
  }
  const DiagMsg *c0 = findType(&pkt, "clock", 0);
  const DiagMsg *c1 = findType(&pkt, "clock", 1);
  if (!c0 || !c1 || c0->bleTs != 0x1FF0 || c1->bleTs != 0) {
    wrapOk = false;
  }
  emitLine(emit, ctx, "timestamp_rollover", wrapOk, "13-bit timestamp wrap");
  failed += wrapOk ? 0 : 1;

  diagStateClear(&dstate);
  memset(&mstate, 0, sizeof(mstate));
  const uint8_t sysex1[] = {0x80, 0x80, 0xF0, 0x01, 0x02};
  bool sysexOk = parseBoth(sysex1, (int)sizeof(sysex1), &dstate, &mstate, &pkt, &parsed);
  const DiagMsg *sx = findType(&pkt, "sysex", 0);
  if (!sysexOk || parsed.count != 0 || mstate.sysex != 1 || dstate.sysex != 1) {
    sysexOk = false;
  }
  if (!sx || sx->sysexEnded || sx->sysexLen != 2 || pkt.sysex[sx->sysexOff] != 0x01 || pkt.sysex[sx->sysexOff + 1] != 0x02) {
    sysexOk = false;
  }
  const uint8_t sysex2[] = {0x80, 0x03, 0xF7, 0x84, 0x90, 40, 20};
  if (!parseBoth(sysex2, (int)sizeof(sysex2), &dstate, &mstate, &pkt, &parsed)) {
    sysexOk = false;
  }
  if (mstate.sysex != 0 || dstate.sysex != 0 || parsed.count != 1 || parsed.events[0].type != MIDI_MSG_NOTE_ON || parsed.events[0].number != 40 || parsed.events[0].value != 20) {
    sysexOk = false;
  }
  const DiagMsg *note = findType(&pkt, "note_on", 0);
  const DiagMsg *sx2 = findType(&pkt, "sysex", 0);
  if (!note || note->running || note->bleTs != 4 || note->channel != 1 || note->data[0] != 40 || note->data[1] != 20) {
    sysexOk = false;
  }
  if (!sx2 || !sx2->sysexEnded || sx2->sysexLen != 1 || pkt.sysex[sx2->sysexOff] != 0x03) {
    sysexOk = false;
  }
  emitLine(emit, ctx, "sysex_split", sysexOk, "sysex split across packets");
  failed += sysexOk ? 0 : 1;

  diagStateClear(&dstate);
  memset(&mstate, 0, sizeof(mstate));
  const uint8_t mixed[] = {0x80, 0x80, 0xF8, 0x81, 0x90, 60, 100, 0x82, 62, 110, 0x83, 0xF8};
  bool clockOk = parseBoth(mixed, (int)sizeof(mixed), &dstate, &mstate, &pkt, &parsed);
  if (!clockOk || parsed.count != 4 || parsed.events[0].type != MIDI_MSG_CLOCK || parsed.events[1].type != MIDI_MSG_NOTE_ON || parsed.events[2].type != MIDI_MSG_NOTE_ON || parsed.events[3].type != MIDI_MSG_CLOCK) {
    clockOk = false;
  }
  if (parsed.events[1].number != 60 || parsed.events[2].number != 62 || parsed.events[2].value != 110) {
    clockOk = false;
  }
  if (pkt.count != 4 || pkt.msg[1].running || !pkt.msg[2].running || pkt.msg[0].bleTs != 0 || pkt.msg[1].bleTs != 1 || pkt.msg[2].bleTs != 2 || pkt.msg[3].bleTs != 3) {
    clockOk = false;
  }
  const uint8_t mid[] = {0x80, 0x81, 0x90, 60, 0xF8, 100};
  if (!parseBoth(mid, (int)sizeof(mid), &dstate, &mstate, &pkt, &parsed)) {
    clockOk = false;
  }
  if (parsed.count != 2 || parsed.events[0].type != MIDI_MSG_CLOCK || parsed.events[0].value14 != 1) {
    clockOk = false;
  }
  bool sawNote = false;
  for (int i = 0; i < parsed.count; i++) {
    if (parsed.events[i].type == MIDI_MSG_NOTE_ON && parsed.events[i].number == 60 && parsed.events[i].value == 100) {
      sawNote = true;
    }
  }
  if (!sawNote || pkt.count < 2 || strcmp(pkt.msg[0].type, "clock") != 0 || strcmp(pkt.msg[1].type, "note_on") != 0 || pkt.msg[1].bleTs != 1) {
    clockOk = false;
  }
  // A status after a clock is the note when the next byte is data. A second
  // status would have made the first byte a timestamp instead.
  const uint8_t afterClock[] = {0x80, 0x80, 0xF8, 0x90, 60, 100};
  if (!parseBoth(afterClock, (int)sizeof(afterClock), &dstate, &mstate, &pkt, &parsed)) {
    clockOk = false;
  }
  if (parsed.count != 2 || parsed.events[0].type != MIDI_MSG_CLOCK || parsed.events[1].type != MIDI_MSG_NOTE_ON || parsed.events[1].number != 60 || parsed.events[1].value != 100) {
    clockOk = false;
  }
  if (pkt.count < 2 || strcmp(pkt.msg[0].type, "clock") != 0 || strcmp(pkt.msg[1].type, "note_on") != 0 || pkt.msg[1].running) {
    clockOk = false;
  }
  const uint8_t shared[] = {0x80, 0x80, 0xF8, 0xF8, 0xF8};
  if (!parseBoth(shared, (int)sizeof(shared), &dstate, &mstate, &pkt, &parsed)) {
    clockOk = false;
  }
  if (parsed.count != 1 || parsed.events[0].type != MIDI_MSG_CLOCK || parsed.events[0].value != 3) {
    clockOk = false;
  }
  emitLine(emit, ctx, "clock_with_notes", clockOk, "clock interleaved with notes");
  failed += clockOk ? 0 : 1;

  char summary[96];
  snprintf(summary, sizeof(summary), "{\"t\":\"selftest\",\"ok\":%s,\"passed\":%d,\"failed\":%d}", failed ? "false" : "true", 5 - failed, failed);
  if (emit) {
    emit(summary, ctx);
  }
  return failed;
}
