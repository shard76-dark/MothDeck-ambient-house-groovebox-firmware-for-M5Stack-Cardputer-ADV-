#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_timer.h>
#include <string.h>
#include <stdlib.h>
#include "BoardConfig.h"
#include "MidiProtocol.h"
#include "MothBootJump.h"
#include "diag_ble.h"
#include "diag_mtc.h"
#include "diag_parse.h"
#include "diag_run.h"
#include "diag_selftest.h"
#include "diag_stats.h"
#include "diag_ui.h"

SET_LOOP_TASK_STACK_SIZE(16384);

static DiagMeter meter;
static DiagState diagState;
static MidiRunningState midiState;
static bool verbose = true;
static char bpmText[12] = "--";
static char lineBuf[8192];
static DiagMtcAsm mtc;
static char uiHint[48] = "Press Play on MPC";
static char uiTest[40] = "Idle";
static int uiPercent = -1;
static uint32_t progressStart = 0;
static uint32_t progressMs = 0;

struct Js {
  char *b;
  size_t cap;
  size_t n;
  bool first;
  bool overflow;
};

static void jsAdd(Js *j, const char *s) {
  if (!s || j->overflow) {
    return;
  }
  size_t len = strlen(s);
  if (j->n + len >= j->cap) {
    j->overflow = true;
    return;
  }
  memcpy(j->b + j->n, s, len);
  j->n += len;
  j->b[j->n] = 0;
}

static void jsBegin(Js *j) {
  j->n = 0;
  j->first = true;
  j->overflow = false;
  if (j->cap > 0) {
    j->b[0] = 0;
  }
  jsAdd(j, "{");
}

static void jsKey(Js *j, const char *key) {
  if (!j->first) {
    jsAdd(j, ",");
  }
  j->first = false;
  jsAdd(j, "\"");
  jsAdd(j, key);
  jsAdd(j, "\":");
}

static void jsRaw(Js *j, const char *key, const char *raw) {
  jsKey(j, key);
  jsAdd(j, raw);
}

static void jsStr(Js *j, const char *key, const char *val) {
  jsKey(j, key);
  jsAdd(j, "\"");
  if (val) {
    for (const char *p = val; *p; p++) {
      char tmp[3] = {*p, 0, 0};
      if (*p == '"' || *p == '\\') {
        tmp[0] = '\\';
        tmp[1] = *p;
      } else if ((unsigned char)*p < 32) {
        continue;
      }
      jsAdd(j, tmp);
    }
  }
  jsAdd(j, "\"");
}

static void jsBool(Js *j, const char *key, bool v) {
  jsRaw(j, key, v ? "true" : "false");
}

static void jsU64(Js *j, const char *key, uint64_t v) {
  char tmp[24];
  snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)v);
  jsRaw(j, key, tmp);
}

static void jsI32(Js *j, const char *key, int32_t v) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%ld", (long)v);
  jsRaw(j, key, tmp);
}

static void jsFixed(Js *j, const char *key, bool has, double v, int frac) {
  jsKey(j, key);
  if (!has) {
    jsAdd(j, "null");
    return;
  }
  if (v < 0) {
    v = 0;
  }
  if (v > 99999.0) {
    v = 99999.0;
  }
  int scale = frac == 1 ? 10 : 100;
  int scaled = (int)(v * (double)scale + 0.5);
  int whole = scaled / scale;
  int part = scaled % scale;
  char tmp[24];
  if (frac == 1) {
    snprintf(tmp, sizeof(tmp), "%d.%d", whole, part);
  } else {
    snprintf(tmp, sizeof(tmp), "%d.%02d", whole, part);
  }
  jsAdd(j, tmp);
}

static void jsHex(Js *j, const char *key, const uint8_t *data, int len) {
  static const char *kHex = "0123456789ABCDEF";
  jsKey(j, key);
  jsAdd(j, "\"");
  if (data && len > 0) {
    for (int i = 0; i < len; i++) {
      char tmp[3] = {kHex[data[i] >> 4], kHex[data[i] & 0x0F], 0};
      jsAdd(j, tmp);
    }
  }
  jsAdd(j, "\"");
}

static void jsEnd(Js *j) {
  jsAdd(j, "}");
}

static Js jsMake() {
  Js j;
  j.b = lineBuf;
  j.cap = sizeof(lineBuf);
  j.n = 0;
  j.first = true;
  j.overflow = false;
  return j;
}

static void emitLine(Js *j) {
  jsEnd(j);
  if (!j->overflow && j->n > 2 && j->b[j->n - 1] == '}') {
    Serial.println(j->b);
  }
}

static void fmtTenths(char *dst, size_t n, bool has, double v) {
  if (!has) {
    snprintf(dst, n, "--");
    return;
  }
  if (v < 0) {
    v = 0;
  }
  if (v > 9999.0) {
    v = 9999.0;
  }
  int tenths = (int)(v * 10.0 + 0.5);
  snprintf(dst, n, "%d.%d", tenths / 10, tenths % 10);
}

static void refreshScreen() {
  const DiagLinkInfo *info = diagBleInfo();
  diagUiLink(info && info->connected);
  diagUiBpm(bpmText);
  if (!diagRunActive()) {
    diagUiTest(uiTest, uiHint, uiPercent);
  }
}

static void emitSelf(const char *line, void *ctx) {
  (void)ctx;
  if (line) {
    Serial.println(line);
  }
}

static void emitLink(const DiagLinkEvent &ev, uint64_t us) {
  const char *name = "link";
  if (ev.kind == DIAG_LINK_CONNECT) {
    name = "connect";
  } else if (ev.kind == DIAG_LINK_DISCONNECT) {
    name = "disconnect";
  }
  DiagLinkInfo info = ev.info;
  diagBleRefreshRssi();
  const DiagLinkInfo *live = diagBleInfo();
  if (live && live->hasRssi && (ev.kind == DIAG_LINK_CONNECT || ev.kind == DIAG_LINK_UPDATE)) {
    info.rssi = live->rssi;
    info.hasRssi = true;
  }
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", name);
  jsU64(&j, "us", us);
  if (info.hasInterval) {
    jsI32(&j, "interval", info.interval);
    jsFixed(&j, "interval_ms", true, (double)info.interval * 1.25, 1);
    jsI32(&j, "latency", info.latency);
    jsI32(&j, "timeout", info.timeout);
    jsI32(&j, "timeout_ms", (int32_t)info.timeout * 10);
  } else {
    jsRaw(&j, "interval", "null");
    jsRaw(&j, "interval_ms", "null");
    jsRaw(&j, "latency", "null");
    jsRaw(&j, "timeout", "null");
    jsRaw(&j, "timeout_ms", "null");
  }
  if (info.hasMtu) {
    jsI32(&j, "mtu", info.mtu);
  } else {
    jsRaw(&j, "mtu", "null");
  }
  if (info.hasRssi) {
    jsI32(&j, "rssi", info.rssi);
  } else {
    jsRaw(&j, "rssi", "null");
  }
  jsStr(&j, "source", info.source[0] ? info.source : "");
  if (info.hasStatus) {
    jsI32(&j, "status", info.updateStatus);
  } else {
    jsRaw(&j, "status", "null");
  }
  if (info.hasAsk) {
    jsI32(&j, "asked_min", info.askedMin);
    jsI32(&j, "asked_max", info.askedMax);
    jsFixed(&j, "asked_min_ms", true, (double)info.askedMin * 1.25, 2);
    jsFixed(&j, "asked_max_ms", true, (double)info.askedMax * 1.25, 2);
    jsI32(&j, "asked_timeout", info.askedTimeout);
  }
  jsStr(&j, "profile", diagBleProfileName());
  emitLine(&j);
}

static void emitPacket(const uint8_t *raw, int len, uint64_t us, const DiagPacket &pkt) {
  if (!verbose) {
    return;
  }
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "pkt");
  jsU64(&j, "us", us);
  jsI32(&j, "ble_ts", pkt.rejected ? 0 : pkt.bleTs);
  jsI32(&j, "hdr", pkt.hdr);
  jsI32(&j, "ts_hi", pkt.tsHi);
  jsKey(&j, "ts_lo");
  jsAdd(&j, "[");
  for (int i = 0; i < pkt.tsLoN; i++) {
    char tmp[8];
    snprintf(tmp, sizeof(tmp), "%s%u", i ? "," : "", (unsigned)pkt.tsLo[i]);
    jsAdd(&j, tmp);
  }
  jsAdd(&j, "]");
  jsI32(&j, "n", pkt.count);
  jsHex(&j, "hex", raw, len);
  emitLine(&j);
}

static void emitMidi(const DiagPacket &pkt, const DiagMsg &msg, uint64_t us) {
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "midi");
  jsU64(&j, "us", us);
  jsStr(&j, "msg", msg.type);
  if (msg.channel > 0) {
    jsI32(&j, "ch", msg.channel);
  }
  if (msg.sysexOff >= 0) {
    int off = msg.sysexOff;
    int n = msg.sysexLen;
    if (off < 0 || off > pkt.sysexLen) {
      off = 0;
      n = 0;
    }
    if (off + n > pkt.sysexLen) {
      n = pkt.sysexLen - off;
    }
    jsHex(&j, "hex", pkt.sysex + off, n);
    jsBool(&j, "ended", msg.sysexEnded);
  } else if (msg.dataLen > 0) {
    jsKey(&j, "d");
    jsAdd(&j, "[");
    for (int i = 0; i < msg.dataLen; i++) {
      char tmp[8];
      snprintf(tmp, sizeof(tmp), "%s%u", i ? "," : "", (unsigned)msg.data[i]);
      jsAdd(&j, tmp);
    }
    jsAdd(&j, "]");
  }
  if (msg.vel0) {
    jsBool(&j, "vel0", true);
  }
  jsBool(&j, "running", msg.running);
  jsI32(&j, "ble_ts", msg.bleTs);
  emitLine(&j);
}

static void emitStats(const DiagWindow &w, uint64_t us) {
  const DiagLinkInfo *info = diagBleInfo();
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "stats");
  jsU64(&j, "us", us);
  jsI32(&j, "window_ms", (int32_t)w.windowMs);
  jsI32(&j, "elapsed_ms", (int32_t)w.elapsedMs);
  jsI32(&j, "clocks", (int32_t)w.clocks);
  jsI32(&j, "clocks_total", (int32_t)w.clocksTotal);
  jsFixed(&j, "bpm_midi", w.hasBpmMidi, w.bpmMidi, 2);
  jsFixed(&j, "bpm_arrival", w.hasBpmArrival, w.bpmArrival, 2);
  jsI32(&j, "note_on", (int32_t)w.noteOn);
  jsI32(&j, "note_off", (int32_t)w.noteOff);
  jsI32(&j, "note_on_total", (int32_t)w.noteOnTotal);
  jsI32(&j, "note_off_total", (int32_t)w.noteOffTotal);
  jsI32(&j, "hanging", (int32_t)w.hanging);
  jsI32(&j, "extra_offs", (int32_t)w.extraOffs);
  jsI32(&j, "parse_errors", (int32_t)w.parseErrors);
  jsI32(&j, "parse_errors_total", (int32_t)w.parseErrorsTotal);
  jsI32(&j, "parser_mismatch", (int32_t)w.parserMismatch);
  jsI32(&j, "packets", (int32_t)w.packets);
  jsFixed(&j, "pps", true, w.pps, 2);
  jsI32(&j, "max_gap_us", (int32_t)w.maxGapUs);
  jsI32(&j, "jitter_us", (int32_t)w.jitterUs);
  jsI32(&j, "queue_drops", (int32_t)diagBleQueueDrops());
  if (info && info->hasRssi) {
    jsI32(&j, "rssi", info->rssi);
  } else {
    jsRaw(&j, "rssi", "null");
  }
  if (info && info->hasMtu) {
    jsI32(&j, "mtu", info->mtu);
  } else {
    jsRaw(&j, "mtu", "null");
  }
  if (info && info->hasInterval) {
    jsFixed(&j, "interval_ms", true, (double)info->interval * 1.25, 1);
    jsI32(&j, "latency", info->latency);
    jsI32(&j, "timeout", info->timeout);
  } else {
    jsRaw(&j, "interval_ms", "null");
  }
  jsI32(&j, "mtc_qf", (int32_t)mtc.qfWindow);
  jsI32(&j, "mtc_full", (int32_t)mtc.fullWindow);
  if (mtc.gapN > 0) {
    jsI32(&j, "mtc_gap_us", (int32_t)(mtc.gapSum / mtc.gapN));
    jsI32(&j, "mtc_gap_max_us", (int32_t)mtc.gapMax);
  }
  if (mtc.hasPos) {
    char pos[24];
    snprintf(pos, sizeof(pos), "%02d:%02d:%02d:%02d", mtc.hours, mtc.minutes, mtc.seconds, mtc.frames);
    jsStr(&j, "mtc_pos", pos);
    jsStr(&j, "mtc_rate", diagMtcRateName(mtc.rateCode));
  }
  mtc.qfWindow = 0;
  mtc.fullWindow = 0;
  mtc.gapSum = 0;
  mtc.gapN = 0;
  mtc.gapMax = 0;
  emitLine(&j);
}

static void emitMtc(const DiagMtcEvent &ev, uint64_t us) {
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "mtc");
  jsU64(&j, "us", us);
  jsStr(&j, "kind", ev.full ? "full" : "quarter");
  if (ev.quarter) {
    jsI32(&j, "piece", ev.piece);
    jsI32(&j, "nibble", ev.nibble);
  }
  if (ev.hasGap) {
    jsI32(&j, "gap_us", (int32_t)ev.gapUs);
  }
  if (ev.hasPos) {
    char pos[24];
    snprintf(pos, sizeof(pos), "%02d:%02d:%02d:%02d", ev.hours, ev.minutes, ev.seconds, ev.frames);
    jsStr(&j, "pos", pos);
    jsStr(&j, "rate", diagMtcRateName(ev.rateCode));
    jsBool(&j, "complete", ev.complete);
  }
  emitLine(&j);
}

static void applyMessage(const DiagPacket &pkt, const DiagMsg &msg, uint64_t us) {
  if (strcmp(msg.type, "qframe") == 0 && msg.dataLen >= 1) {
    DiagMtcEvent ev;
    if (diagMtcQuarter(&mtc, msg.data[0], us, &ev)) {
      emitMtc(ev, us);
    }
  } else if (strcmp(msg.type, "sysex") == 0 && msg.sysexEnded) {
    int off = msg.sysexOff;
    int n = msg.sysexLen;
    if (off >= 0 && n > 0 && off + n <= pkt.sysexLen) {
      DiagMtcEvent ev;
      if (diagMtcFull(pkt.sysex + off, n, us, &mtc, &ev)) {
        emitMtc(ev, us);
      }
    }
  }
  if (strcmp(msg.type, "note_on") == 0 && msg.dataLen == 2) {
    meter.noteOn(msg.channel, msg.data[0], msg.data[1], us);
  } else if (strcmp(msg.type, "note_off") == 0 && msg.dataLen == 2) {
    meter.noteOff(msg.channel, msg.data[0]);
  } else if (strcmp(msg.type, "cc") == 0 && msg.dataLen == 2 && (msg.data[0] == 120 || msg.data[0] == 123)) {
    meter.allOff(msg.channel);
  } else if (strcmp(msg.type, "clock") == 0) {
    meter.clock(us, msg.bleTs);
  }
}

static void handlePacket(const uint8_t *raw, int len, uint64_t us) {
  static DiagPacket pkt;
  diagParse(raw, len, &diagState, &pkt);
  MidiParseResult parsed;
  parseBleMidiPacket(raw, len, &midiState, &parsed);
  meter.packet(us);
  meter.parseError(pkt.errors);
  if (!diagMatchesParser(&pkt, &parsed)) {
    meter.noteParserMismatch();
  }
  emitPacket(raw, len, us, pkt);
  for (int i = 0; i < pkt.count; i++) {
    applyMessage(pkt, pkt.msg[i], us);
    emitMidi(pkt, pkt.msg[i], us);
  }
}

static void cmdStatus() {
  const DiagLinkInfo *info = diagBleInfo();
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "status");
  jsStr(&j, "name", kMothDiagName);
  jsStr(&j, "version", kMothDiagVersion);
  jsBool(&j, "connected", info && info->connected);
  jsBool(&j, "advertising", info && info->advertising);
  jsBool(&j, "verbose", verbose);
  jsI32(&j, "stats_ms", (int32_t)meter.windowMs());
  if (info && info->hasMtu) {
    jsI32(&j, "mtu", info->mtu);
  } else {
    jsRaw(&j, "mtu", "null");
  }
  if (info && info->hasInterval) {
    jsFixed(&j, "interval_ms", true, (double)info->interval * 1.25, 1);
    jsI32(&j, "latency", info->latency);
    jsI32(&j, "timeout", info->timeout);
    jsStr(&j, "source", info->source[0] ? info->source : "");
  } else {
    jsRaw(&j, "interval_ms", "null");
  }
  if (info && info->hasAsk) {
    jsFixed(&j, "asked_min_ms", true, (double)info->askedMin * 1.25, 2);
    jsFixed(&j, "asked_max_ms", true, (double)info->askedMax * 1.25, 2);
  }
  if (info && info->hasStatus) {
    jsI32(&j, "status", info->updateStatus);
  }
  jsStr(&j, "profile", diagBleProfileName());
  if (info && info->hasRssi) {
    jsI32(&j, "rssi", info->rssi);
  } else {
    jsRaw(&j, "rssi", "null");
  }
  jsI32(&j, "hanging", (int32_t)meter.hanging());
  jsI32(&j, "extra_offs", (int32_t)meter.extraOffs());
  jsI32(&j, "note_on", (int32_t)meter.noteOnTotal());
  jsI32(&j, "note_off", (int32_t)meter.noteOffTotal());
  jsI32(&j, "clocks", (int32_t)meter.clockTotal());
  jsI32(&j, "parse_errors", (int32_t)meter.parseErrors());
  jsI32(&j, "parser_mismatch", (int32_t)meter.parserMismatch());
  jsI32(&j, "queue_drops", (int32_t)diagBleQueueDrops());
  const char *why = diagBleFailReason();
  if (why && why[0]) {
    jsStr(&j, "radio", why);
  } else {
    jsStr(&j, "radio", "ok");
  }
  emitLine(&j);
}

static void cmdBpm() {
  DiagBpm bpm;
  meter.bpmSummary(&bpm);
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "bpm");
  jsI32(&j, "clocks", (int32_t)bpm.clocks);
  jsFixed(&j, "bpm_midi", bpm.hasBpmMidi, bpm.bpmMidi, 2);
  jsFixed(&j, "bpm_arrival", bpm.hasBpmArrival, bpm.bpmArrival, 2);
  jsFixed(&j, "arrival_jitter_us", bpm.hasArrivalJitter, bpm.arrivalJitterUs, 1);
  jsFixed(&j, "midi_jitter_ms", bpm.hasMidiJitter, bpm.midiJitterMs, 2);
  jsI32(&j, "midi_span_ms", (int32_t)bpm.midiSpanMs);
  jsU64(&j, "arrival_span_us", bpm.arrivalSpanUs);
  emitLine(&j);
}

static void cmdNotes() {
  DiagHeld held[128];
  int n = meter.copyHeld(held, 128);
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "notes");
  jsI32(&j, "n", meter.hanging());
  jsI32(&j, "listed", n);
  jsKey(&j, "held");
  jsAdd(&j, "[");
  for (int i = 0; i < n; i++) {
    char item[80];
    snprintf(
      item,
      sizeof(item),
      "%s{\"ch\":%d,\"note\":%d,\"vel\":%d,\"on_us\":%llu}",
      i ? "," : "",
      held[i].ch,
      held[i].note,
      held[i].vel,
      (unsigned long long)held[i].onUs);
    jsAdd(&j, item);
  }
  jsAdd(&j, "]");
  emitLine(&j);
}

static void cmdNak(const char *reason) {
  Js j = jsMake();
  jsBegin(&j);
  jsStr(&j, "t", "cmd");
  jsBool(&j, "ok", false);
  jsStr(&j, "reason", reason);
  emitLine(&j);
}

static bool parseInterval(const char *text, uint16_t *minU, uint16_t *maxU);

static void handleCommand(char *line) {
  while (*line == ' ') {
    line++;
  }
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\r')) {
    line[--n] = 0;
  }
  if (n == 0) {
    return;
  }
  for (size_t i = 0; i < n; i++) {
    if (line[i] >= 'A' && line[i] <= 'Z') {
      line[i] = (char)(line[i] - 'A' + 'a');
    }
  }
  if (strcmp(line, "help") == 0) {
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "help");
    jsStr(&j, "name", kMothDiagName);
    jsStr(&j, "version", kMothDiagVersion);
    jsRaw(&j, "commands", "[\"help\",\"status\",\"reset\",\"verbose on|off\",\"stats <ms>\",\"bpm\",\"notes\",\"selftest\",\"run\",\"go\",\"stop\",\"interval\",\"gap\",\"profile\",\"hint\",\"launcher\"]");
    emitLine(&j);
    return;
  }
  if (strcmp(line, "status") == 0) {
    cmdStatus();
    return;
  }
  if (strcmp(line, "reset") == 0) {
    meter.reset((uint64_t)esp_timer_get_time());
    diagStateClear(&diagState);
    memset(&midiState, 0, sizeof(midiState));
    diagMtcReset(&mtc);
    snprintf(bpmText, sizeof(bpmText), "--");
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "reset");
    jsBool(&j, "ok", true);
    emitLine(&j);
    refreshScreen();
    return;
  }
  if (strcmp(line, "verbose on") == 0 || strcmp(line, "verbose off") == 0 || strcmp(line, "verbose") == 0) {
    if (strcmp(line, "verbose on") == 0) {
      verbose = true;
    } else if (strcmp(line, "verbose off") == 0) {
      verbose = false;
    }
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "verbose");
    jsBool(&j, "on", verbose);
    emitLine(&j);
    return;
  }
  if (strcmp(line, "stats") == 0 || strncmp(line, "stats ", 6) == 0) {
    if (line[5] == ' ') {
      char *end = nullptr;
      long ms = strtol(line + 6, &end, 10);
      if (end == line + 6 || *end != 0 || ms < 100 || ms > 60000) {
        cmdNak("stats ms");
        return;
      }
      meter.setWindowMs((uint32_t)ms);
    }
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "stats_cfg");
    jsI32(&j, "window_ms", (int32_t)meter.windowMs());
    emitLine(&j);
    return;
  }
  if (strcmp(line, "bpm") == 0) {
    cmdBpm();
    return;
  }
  if (strcmp(line, "notes") == 0) {
    cmdNotes();
    return;
  }
  if (strcmp(line, "selftest") == 0 || strcmp(line, "self-test") == 0) {
    diagSelfTest(emitSelf, nullptr);
    return;
  }
  if (strcmp(line, "run") == 0) {
    diagRunStart();
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "run");
    jsBool(&j, "ok", true);
    emitLine(&j);
    return;
  }
  if (strcmp(line, "go") == 0) {
    diagRunGo();
    if (!diagRunActive()) {
      progressStart = millis();
    }
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "go");
    jsBool(&j, "ok", true);
    emitLine(&j);
    return;
  }
  if (strcmp(line, "stop") == 0) {
    diagRunStop();
    progressMs = 0;
    uiPercent = -1;
    snprintf(uiTest, sizeof(uiTest), "Idle");
    refreshScreen();
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "stop");
    jsBool(&j, "ok", true);
    emitLine(&j);
    return;
  }
  if (strcmp(line, "gap") == 0) {
    diagBleRequestGap();
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "gap");
    jsBool(&j, "ok", true);
    emitLine(&j);
    return;
  }
  if (strcmp(line, "launcher") == 0) {
    diagUiTest("MothBoot", "", -1);
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "launcher");
    jsBool(&j, "ok", true);
    emitLine(&j);
    Serial.flush();
    delay(150);
    if (!mothRebootToLauncher()) {
      cmdNak("launcher");
    }
    return;
  }
  if (strncmp(line, "hint ", 5) == 0) {
    strncpy(uiHint, line + 5, sizeof(uiHint) - 1);
    uiHint[sizeof(uiHint) - 1] = 0;
    refreshScreen();
    return;
  }
  if (strncmp(line, "test ", 5) == 0) {
    strncpy(uiTest, line + 5, sizeof(uiTest) - 1);
    uiTest[sizeof(uiTest) - 1] = 0;
    refreshScreen();
    return;
  }
  if (strncmp(line, "progress ", 9) == 0) {
    long sec = strtol(line + 9, nullptr, 10);
    if (sec < 1 || sec > 3600) {
      cmdNak("progress");
      return;
    }
    progressMs = (uint32_t)sec * 1000u;
    progressStart = 0;
    uiPercent = 0;
    refreshScreen();
    return;
  }
  if (strncmp(line, "profile ", 8) == 0) {
    const char *name = line + 8;
    if (strcmp(name, "deck") != 0 && strcmp(name, "apple") != 0 && strcmp(name, "read") != 0 && strcmp(name, "mtu64") != 0 && strcmp(name, "mtu128") != 0 && strcmp(name, "noreq") != 0) {
      cmdNak("profile");
      return;
    }
    if (!diagBleStoreProfile(name)) {
      cmdNak("profile");
      return;
    }
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "profile");
    jsStr(&j, "name", name);
    jsBool(&j, "restarting", true);
    emitLine(&j);
    Serial.flush();
    delay(150);
    ESP.restart();
    return;
  }
  if (strcmp(line, "interval off") == 0) {
    diagBleIntervalOff();
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "interval");
    jsBool(&j, "ok", true);
    jsBool(&j, "on", false);
    emitLine(&j);
    return;
  }
  if (strncmp(line, "interval delay ", 15) == 0) {
    long ms = strtol(line + 15, nullptr, 10);
    if (ms < 0 || ms > 5000) {
      cmdNak("interval");
      return;
    }
    const DiagLinkInfo *info = diagBleInfo();
    uint16_t minU = info && info->hasAsk ? info->askedMin : 6;
    uint16_t maxU = info && info->hasAsk ? info->askedMax : 12;
    diagBleSetInterval(minU, maxU, (uint32_t)ms, false);
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "interval");
    jsBool(&j, "ok", true);
    jsI32(&j, "delay_ms", (int32_t)ms);
    emitLine(&j);
    return;
  }
  if (strncmp(line, "interval ", 9) == 0) {
    uint16_t minU = 0;
    uint16_t maxU = 0;
    if (!parseInterval(line + 9, &minU, &maxU)) {
      cmdNak("interval");
      return;
    }
    diagBleSetInterval(minU, maxU, 0, true);
    Js j = jsMake();
    jsBegin(&j);
    jsStr(&j, "t", "interval");
    jsBool(&j, "ok", true);
    jsI32(&j, "min", minU);
    jsI32(&j, "max", maxU);
    emitLine(&j);
    return;
  }
  cmdNak("unknown");
}

static bool parseIntervalToken(const char *begin, const char *end, uint16_t *units) {
  size_t n = (size_t)(end - begin);
  if (n == 3 && strncmp(begin, "7.5", 3) == 0) {
    *units = 6;
    return true;
  }
  if (n == 5 && strncmp(begin, "11.25", 5) == 0) {
    *units = 9;
    return true;
  }
  if (n == 0 || n > 4) {
    return false;
  }
  char buf[8];
  memcpy(buf, begin, n);
  buf[n] = 0;
  char *stop = nullptr;
  long ms = strtol(buf, &stop, 10);
  if (stop == buf || *stop != 0 || ms < 8 || ms > 400 || (ms * 4) % 5 != 0) {
    return false;
  }
  *units = (uint16_t)(ms * 4 / 5);
  return true;
}

static bool parseInterval(const char *text, uint16_t *minU, uint16_t *maxU) {
  const char *dash = strchr(text, '-');
  if (!dash) {
    if (!parseIntervalToken(text, text + strlen(text), minU)) {
      return false;
    }
    *maxU = *minU;
    return true;
  }
  if (!parseIntervalToken(text, dash, minU) || !parseIntervalToken(dash + 1, text + strlen(text), maxU)) {
    return false;
  }
  return *maxU >= *minU;
}

static void pollSerial() {
  static char cmd[96];
  static size_t used = 0;
  while (Serial.available() > 0) {
    int ch = Serial.read();
    if (ch < 0) {
      return;
    }
    if (ch == '\n') {
      cmd[used] = 0;
      used = 0;
      handleCommand(cmd);
      continue;
    }
    if (ch == '\r') {
      continue;
    }
    if (used + 1 < sizeof(cmd)) {
      cmd[used++] = (char)ch;
    } else {
      used = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  mothBootGuard();
  memset(&midiState, 0, sizeof(midiState));
  diagStateClear(&diagState);
  diagMtcReset(&mtc);
  meter.reset(0);
  bool radio = diagBleBegin();
  diagUiBegin();
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  uint32_t heldSince = millis();
  bool held = false;
  while ((uint32_t)(millis() - heldSince) < 450) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.BtnA.isPressed() || digitalRead(PIN_BTN_A) == LOW) {
      held = true;
      break;
    }
    delay(15);
  }
  if (held) {
    diagUiTest("MothBoot", "", -1);
    delay(200);
    mothRebootToLauncher();
  }
  refreshScreen();
  delay(200);
  uint64_t us = (uint64_t)esp_timer_get_time();
  meter.reset(us);
  Js hello = jsMake();
  jsBegin(&hello);
  jsStr(&hello, "t", "hello");
  jsStr(&hello, "name", kMothDiagName);
  jsStr(&hello, "version", kMothDiagVersion);
  jsStr(&hello, "image", kMothDiagImage);
  jsStr(&hello, "profile", diagBleProfileName());
  jsI32(&hello, "baud", 115200);
  emitLine(&hello);
  const DiagLinkInfo *info = diagBleInfo();
  Js radioLine = jsMake();
  jsBegin(&radioLine);
  jsStr(&radioLine, "t", "radio");
  jsBool(&radioLine, "ok", radio && info && info->radioOk);
  jsBool(&radioLine, "advertising", info && info->advertising);
  jsStr(&radioLine, "name", kMothDiagName);
  jsStr(&radioLine, "service", "03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
  const char *why = diagBleFailReason();
  if (why && why[0]) {
    jsStr(&radioLine, "reason", why);
  }
  emitLine(&radioLine);
  mothMarkAppValid();
}

void loop() {
  uint32_t nowMs = millis();
  uint64_t nowUs = (uint64_t)esp_timer_get_time();
  diagBleMaintain(nowMs);
  diagBleRefreshRssi();

  DiagLinkEvent ev;
  int links = 0;
  while (links < 4 && diagBlePopLink(&ev)) {
    emitLink(ev, nowUs);
    links++;
  }

  uint8_t raw[256];
  int packets = 0;
  while (packets < 4) {
    uint64_t us = 0;
    int n = diagBlePopPacket(raw, (int)sizeof(raw), &us);
    if (n <= 0) {
      break;
    }
    handlePacket(raw, n, us);
    packets++;
  }

  DiagWindow window;
  if (meter.takeWindow(nowUs, &window)) {
    fmtTenths(bpmText, sizeof(bpmText), window.hasBpmArrival, window.bpmArrival);
    emitStats(window, nowUs);
  }
  diagRunPoll(nowMs);
  if (!diagRunActive() && progressMs > 0) {
    if (progressStart == 0) {
      progressStart = nowMs;
    }
    uint32_t elapsed = (uint32_t)(nowMs - progressStart);
    uiPercent = (int)((uint64_t)elapsed * 100ull / progressMs);
    if (uiPercent >= 100) {
      uiPercent = 100;
      progressMs = 0;
    }
  }
  refreshScreen();
  pollSerial();
  if (packets == 0 && links == 0) {
    delay(2);
  }
}
