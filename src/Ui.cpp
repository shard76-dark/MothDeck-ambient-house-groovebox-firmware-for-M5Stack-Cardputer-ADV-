#include "Ui.h"
#include "ModalInput.h"
#include "AudioEngine.h"
#include "InstrumentBank.h"
#include "LoopLibrary.h"
#include "PcmHold.h"
#include "DrumKit.h"
#include "SdStorage.h"
#include "SdCard.h"
#include "LauncherExit.h"
#include "BleAdvert.h"
#include "BleMidi.h"
#include "BoardConfig.h"
#include "BoardIo.h"
#include "DevLog.h"
#if MOTHDECK_BOARD_TTGO
#include <M5GFX.h>
#else
#include <M5Cardputer.h>
#endif
#include <Preferences.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

static const uint16_t COL_BG = 0x1082;
static const uint16_t COL_BAR = 0x2104;
static const uint16_t COL_AMBER = 0xFD20;
static const uint16_t COL_TEXT = 0xEF5D;
static const uint16_t COL_DIM = 0x6B6D;
static const uint16_t COL_PLAY = 0x07E0;
static const uint16_t COL_WARN = 0xFBE0;
static const uint16_t COL_TRACK[4] = {0xFD20, 0x2D7F, 0xF81F, 0x07E0};

static const char *kPageName[] = {"Play", "Instrument", "FX", "Mixer", "Song", "Loops", "MIDI", "Settings", "Exit"};
static const int kPageCount = 9;
static const int kExitPage = 8;
static const int kFxPage = 2;
static const int kMixerPage = 3;
static const int kSongPage = 4;
static const int kLoopsPage = 5;
static const int kMidiPage = 6;
static const int kSettingsPage = 7;
static bool launcherOk = false;

static const char *kNoteName[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct PianoKey {
  char key;
  int pitch;
  int add;
};

static const PianoKey kPiano[] = {
    {'z', 0, 0}, {'x', 2, 0}, {'c', 4, 0}, {'v', 5, 0}, {'b', 7, 0}, {'n', 9, 0}, {'m', 11, 0}, {',', 0, 1},
    {'s', 1, 0}, {'d', 3, 0}, {'g', 6, 0}, {'h', 8, 0}, {'j', 10, 0},
    {'q', 0, 1}, {'w', 1, 1}, {'e', 2, 1}, {'r', 3, 1}, {'t', 4, 1}, {'y', 5, 1},
    {'u', 6, 1}, {'i', 7, 1}, {'o', 8, 1}, {'p', 9, 1}, {'[', 10, 1}, {']', 11, 1},
};

static M5Canvas *canvas = nullptr;
// rgb332 rows are expanded here and pushed as RGB565. Keeping the strip in
// BSS means the panel update never mallocs a second full frame. An 8-bit
// pushSprite does that (LovyanGFX grows a DMA buffer toward 240*135*2) and
// that allocation reset the ADV once I2S DMA was running.
static const int kSpriteW = 240;
static const int kSpriteH = 135;
static const int kBlitRows = 8;
static uint16_t blitBuf[kSpriteW * kBlitRows];
static void pushCanvas();
static Preferences prefs;
static int page = 0;
static int cursor = 0;
static int mixRow = 0;
static int songSlot = 0;
static int loopLib = 0;
static int loopRow = 0;
static int quantize = 2;
static bool overlay = false;
static bool naming = false;
static char bleName[20] = MOTHDECK_BLE_NAME_DEFAULT;
static char edit[20];
static char toast[48];
static uint32_t toastUntil = 0;
static uint8_t outVol = 160;
static uint8_t bright = 180;
static uint32_t escSince = 0;
static bool escDown = false;
static bool escFired = false;
static Snap snap;
static SdStorage storage;
// Play opens on the piano roll. Enter switches back to the 16-step strip.
// The cursor is the UI's; the audio task still owns the steps.
static bool rollView = true;
static int rollStep = 0;
static int rollPitch = 24;
static int rollOrigin = 0;
static bool rollFollow = true;

static const int kRollY = 24;
static const int kRollH = 96;
static const int kRollRows = 48;
static const int kRowH = 2;
static const int kGridX = 22;
static const uint16_t COL_KEY_W = 0xC618;
static const uint16_t COL_KEY_B = 0x0000;
static const uint16_t COL_LANE_W = 0x18C3;
static const uint16_t COL_LANE_B = 0x0861;
static const uint16_t COL_BEAT = 0x3186;
static const uint16_t COL_BARLINE = 0x52AA;
static const uint16_t COL_CURSOR = 0xFFFF;

static void toastSet(const char *msg) {
  snprintf(toast, sizeof(toast), "%s", msg ? msg : "");
  toastUntil = millis() + 2200;
}

static uint8_t bleOn = 0;
static uint8_t bleLoad = 1;
static bool bleUnloadPending = false;

static void savePrefs() {
  prefs.putUChar("vol", outVol);
  prefs.putUChar("bri", bright);
  prefs.putString("ble", bleName);
  prefs.putUChar("bleOn", bleOn);
  prefs.putUChar("bleEn", bleOn);
  prefs.putUChar("bleLoad", bleLoad);
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static bool pianoKey(char c, int *pitch, int *add) {
  for (unsigned i = 0; i < sizeof(kPiano) / sizeof(kPiano[0]); i++) {
    if (kPiano[i].key == c) {
      *pitch = kPiano[i].pitch;
      *add = kPiano[i].add;
      return true;
    }
  }
  return false;
}

static void playNote(char c, bool shift, bool alt, bool opt) {
  int pitch = 0;
  int add = 0;
  if (!pianoKey(c, &pitch, &add)) {
    return;
  }
  int oct = (int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0);
  audioReadSnap(&snap);
  oct = (int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0);
  oct = clampi(oct, 0, 3);
  // PlayPitch is a tracker method. Route it as a note command plus octave.
  audioCommand('O', oct);
  audioCommand('N', pitch);
  audioCommand('O', snap.octave);
}

static int pageSpan() {
  return launcherOk ? kPageCount : kExitPage;
}

static int stepPage(int from, int delta) {
  int n = pageSpan();
  if (n < 1) {
    return 0;
  }
  if (from < 0 || from >= n) {
    from = 0;
  }
  return (from + delta + n) % n;
}

static void requestExit() {
  if (!launcherInstalled()) {
    launcherOk = false;
    toastSet("Launcher not found");
    return;
  }
  canvas->fillSprite(COL_BG);
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(8, 36);
  canvas->println("Returning to Launcher");
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(8, 52);
  canvas->println("Clears the boot slot");
  canvas->setCursor(8, 68);
  canvas->println("Press Enter on splash");
  pushCanvas();
  delay(500);
  if (!exitToLauncher()) {
    launcherOk = false;
    toastSet("Launcher not found");
  }
}

const char *uiBleName() {
  return bleName;
}

static bool prefsLoaded = false;

void uiLoadPrefs() {
  if (prefsLoaded) {
    return;
  }
  prefs.begin("mothdeck", false);
  outVol = prefs.getUChar("vol", 160);
  bright = prefs.getUChar("bri", 180);
  String stored = prefs.getString("ble", MOTHDECK_BLE_NAME_DEFAULT);
  snprintf(bleName, sizeof(bleName), "%s", stored.c_str());
  // 1.1.2 advertises Mothdeck. A stored MothDeck / MothSynth, or any name
  // from before this image, is replaced once so the primary packet matches.
  bool migrateName = prefs.getUChar("nm12", 0) == 0;
  if (!bleName[0] || migrateName || strcmp(bleName, "MothDeck") == 0 || strcmp(bleName, "MothSynth") == 0) {
    snprintf(bleName, sizeof(bleName), "%s", MOTHDECK_BLE_NAME_DEFAULT);
  }
  if (migrateName || strcmp(bleName, stored.c_str()) != 0) {
    prefs.putString("ble", bleName);
    prefs.putUChar("nm12", 1);
  }
  // bleEn is new in 1.1.2. A 1.1.1 bleOn of 1 must not start the radio.
  bleOn = prefs.getUChar("bleEn", 0) ? 1 : 0;
  bleLoad = prefs.getUChar("bleLoad", 1) ? 1 : 0;
  prefsLoaded = true;
}

bool uiBleEnabled() {
  uiLoadPrefs();
  return bleOn != 0;
}

bool uiBleWantLoad() {
  uiLoadPrefs();
  return bleLoad != 0;
}

static uint16_t rgb332to565(uint8_t c) {
  uint32_t r3 = c >> 5;
  uint32_t g3 = (c >> 2) & 7;
  uint32_t b2 = c & 3;
  uint32_t r5 = (r3 << 2) | (r3 >> 1);
  uint32_t g6 = (g3 << 3) | g3;
  uint32_t b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

static void pushCanvas() {
  if (!canvas || !canvas->getBuffer() || !boardHasDisplay()) {
    return;
  }
  const uint8_t *src = static_cast<const uint8_t *>(canvas->getBuffer());
  boardPushBegin();
  for (int y = 0; y < kSpriteH; y += kBlitRows) {
    int rows = kSpriteH - y;
    if (rows > kBlitRows) {
      rows = kBlitRows;
    }
    for (int row = 0; row < rows; row++) {
      const uint8_t *in = src + (size_t)(y + row) * kSpriteW;
      uint16_t *out = blitBuf + row * kSpriteW;
      for (int x = 0; x < kSpriteW; x++) {
        out[x] = rgb332to565(in[x]);
      }
    }
    boardPushImage(0, y, kSpriteW, rows, blitBuf);
  }
  boardPushEnd();
}

static void logUiHeap(const char *tag) {
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  DEV_LOGF("HEAP: %s free=%u largest=%u\n", tag, (unsigned)freeB, (unsigned)largest);
}

void uiBegin() {
  uiLoadPrefs();
  logUiHeap("before sprite");
#if MOTHDECK_BOARD_TTGO
  // Offscreen rgb332. With a panel it is pushed in strips. Headless, the
  // push is a no-op and the sprite sits in PSRAM.
  canvas = new M5Canvas();
  canvas->setPsram(true);
#else
  canvas = new M5Canvas(&M5Cardputer.Display);
#endif
  // rgb332, 240*135 = 32400 bytes, half of the RGB565 canvas. The strip
  // blit above is what makes that safe next to I2S: pushSprite would
  // allocate another ~32KB DMA buffer to expand the frame.
  canvas->setColorDepth(static_cast<uint8_t>(8));
  void *buf = canvas->createSprite(kSpriteW, kSpriteH);
  if (!buf) {
    Serial.println("UI: sprite alloc failed");
  } else {
    DEV_LOGF("UI: sprite 8-bit bytes=%u\n", (unsigned)(kSpriteW * kSpriteH));
  }
  logUiHeap("after sprite");
  canvas->setTextSize(1);
  canvas->setTextColor(COL_TEXT);
  boardSetBrightness(bright);
  audioSetSpeakerVolume(outVol);
  launcherOk = launcherInstalled();
}

void uiMountStorage(bool withLoops) {
  instrumentBank.Scan();
  loopLibrary.Scan();
  drumKit.Scan();
  if (!withLoops) {
    return;
  }
  char err[48];
  err[0] = 0;
  loopLibrary.PreloadInstrument(err, (int)sizeof(err));
  if (loopLibrary.AudioCount() > loopLibrary.ReadyCount() && err[0]) {
    toastSet(err);
  }
}

static void statusBar(BleMidi &ble) {
  canvas->fillRect(0, 0, 240, 12, COL_BAR);
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 2);
  canvas->print("MothDeck");
  canvas->setTextColor(COL_TEXT);
  canvas->printf(" T%d", snap.track + 1);
  canvas->printf(" %d", snap.bpm);
  canvas->printf(" P%d", snap.pattern + 1);
  canvas->setTextColor(snap.playing ? COL_PLAY : COL_DIM);
  canvas->print(snap.playing ? " PLAY" : " STOP");
  canvas->setTextColor(ble.Connected() ? COL_PLAY : COL_DIM);
  canvas->print(ble.Connected() ? " MIDI" : " midi");
  int pct = boardBatteryPct();
  canvas->setTextColor(COL_TEXT);
  if (pct >= 0) {
    canvas->setCursor(196, 2);
    canvas->printf("%d%%", pct);
  }
}

static bool loopsBlocked = false;
static bool bleAsk = false;

static void legend(const char *text) {
  canvas->fillRect(0, 122, 240, 13, COL_BAR);
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 124);
  canvas->print(text);
}

static void drawToast() {
  if (!toast[0] || (int32_t)(millis() - toastUntil) > 0) {
    return;
  }
  canvas->fillRect(4, 100, 232, 18, COL_WARN);
  canvas->setTextColor(COL_BG);
  canvas->setCursor(8, 104);
  canvas->print(toast);
}

static void voiceName(int id, char *dst, int n) {
  if (n < 2) {
    return;
  }
  if ((id >= 0 && id <= 11) || id == kLoopsVoice) {
    snprintf(dst, n, "%s", InstrumentBank::BuiltinName(id));
    return;
  }
  for (int i = 0; i < instrumentBank.Count(); i++) {
    const PluginInfo &p = instrumentBank.At(i);
    if (p.id == (uint8_t)id && (p.loaded || p.name[0])) {
      snprintf(dst, n, "%s", p.name[0] ? p.name : p.folder);
      return;
    }
  }
  snprintf(dst, n, "P%d", id);
}

static int rollPatLen() {
  int n = snap.patternLength ? (int)snap.patternLength : 16;
  if (n > 128) {
    n = 128;
  }
  if (n < 1) {
    n = 16;
  }
  return n;
}

static int rollStepPx(int patLen) {
  int w = kSpriteW - kGridX;
  if (patLen * 8 <= w) {
    return 8;
  }
  if (patLen * 6 <= w) {
    return 6;
  }
  return 4;
}

static int rollVisible(int stepW) {
  int n = (kSpriteW - kGridX) / stepW;
  if (n < 1) {
    n = 1;
  }
  return n;
}

static bool blackSemitone(int pitch) {
  int semi = pitch % 12;
  if (semi < 0) {
    semi += 12;
  }
  return semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
}

static void clampRoll() {
  int patLen = rollPatLen();
  rollStep = clampi(rollStep, 0, patLen - 1);
  rollPitch = clampi(rollPitch, 0, kRollRows - 1);
  int vis = rollVisible(rollStepPx(patLen));
  if (rollStep < rollOrigin) {
    rollOrigin = rollStep;
  }
  if (rollStep >= rollOrigin + vis) {
    rollOrigin = rollStep - vis + 1;
  }
  int maxOrigin = patLen > vis ? patLen - vis : 0;
  rollOrigin = clampi(rollOrigin, 0, maxOrigin);
}

static void followRollHead() {
  if (!rollFollow || !snap.playing) {
    return;
  }
  int patLen = rollPatLen();
  int len = snap.patternLength ? (int)snap.patternLength : patLen;
  int head = (int)snap.step - (int)snap.pattern * len;
  rollStep = clampi(head, 0, patLen - 1);
}

static uint8_t rollPacked(int step, int which) {
  if (step < 0 || step >= 128) {
    return 0;
  }
#if MOTHDECK_CHORDS
  if (which > 0 && which <= 3) {
    return snap.rollChord[step][which - 1];
  }
#else
  (void)which;
#endif
  return snap.roll[step];
}

// Step where the note under the cursor starts, or -1. A hold of 2–4
// covers the steps after it, and edits land on that start cell.
// cellOut receives the packed note that matched, primary or extra.
static int rollCoverStartAt(uint8_t *cellOut) {
  int from = rollStep - 3;
  if (from < 0) {
    from = 0;
  }
  int limit = snap.rollCount;
  if (limit > 128) {
    limit = 128;
  }
  int slots = 1;
#if MOTHDECK_CHORDS
  slots = 4;
#endif
  for (int s = from; s <= rollStep && s < limit; s++) {
    for (int which = 0; which < slots; which++) {
      uint8_t cell = rollPacked(s, which);
      int note = cell & 0x0F;
      if (note < 1 || note > 12) {
        continue;
      }
      int len = ((cell >> 4) & 3) + 1;
      if (s + len <= rollStep) {
        continue;
      }
      int oct = (cell >> 6) & 3;
      if ((note - 1) + oct * 12 == rollPitch) {
        if (cellOut) {
          *cellOut = cell;
        }
        return s;
      }
    }
  }
  return -1;
}

static int rollCoverStart() {
  return rollCoverStartAt(nullptr);
}

#if MOTHDECK_CHORDS
static int rollPitchPack(int step) {
  int pitch = rollPitch % 12;
  int oct = rollPitch / 12;
  return (step & 0xFF) | ((pitch & 0x0F) << 8) | ((oct & 0x0F) << 12) | (1 << 24);
}
#endif

static void drawRollBlock(int s, uint8_t cell, int origin, int stepW, uint16_t noteColor) {
  int note = cell & 0x0F;
  if (note < 1 || note > 12) {
    return;
  }
  int len = ((cell >> 4) & 3) + 1;
  int oct = (cell >> 6) & 3;
  int pitch = (note - 1) + oct * 12;
  if (pitch < 0 || pitch >= kRollRows) {
    return;
  }
  int x0 = kGridX + (s - origin) * stepW;
  int x1 = x0 + len * stepW - 1;
  if (x1 < kGridX || x0 >= kSpriteW) {
    return;
  }
  if (x0 < kGridX) {
    x0 = kGridX;
  }
  if (x1 >= kSpriteW) {
    x1 = kSpriteW - 1;
  }
  int w = x1 - x0 + 1;
  if (w < 1) {
    return;
  }
  int y = kRollY + (kRollRows - 1 - pitch) * kRowH;
  canvas->fillRect(x0, y, w, kRowH, noteColor);
}

static void drawRollDigit(int x, int y, int digit) {
  static const uint8_t kBits[4][5] = {
      {0x7, 0x1, 0x7, 0x4, 0x7},
      {0x7, 0x1, 0x7, 0x1, 0x7},
      {0x5, 0x5, 0x7, 0x1, 0x1},
      {0x7, 0x4, 0x7, 0x1, 0x7},
  };
  if (digit < 2 || digit > 5) {
    return;
  }
  const uint8_t *row = kBits[digit - 2];
  for (int dy = 0; dy < 5; dy++) {
    uint8_t bits = row[dy];
    for (int dx = 0; dx < 3; dx++) {
      if (bits & (4 >> dx)) {
        canvas->fillRect(x + dx, y + dy, 1, 1, COL_TEXT);
      }
    }
  }
}

static void drawRoll() {
  followRollHead();
  clampRoll();
  int patLen = rollPatLen();
  int stepW = rollStepPx(patLen);
  int vis = rollVisible(stepW);
  int origin = rollOrigin;

  canvas->setTextColor(COL_TRACK[snap.track & 3]);
  canvas->setCursor(2, 14);
  canvas->printf("T%d", (snap.track & 3) + 1);
  canvas->setTextColor(COL_TEXT);
  canvas->printf(" %s%d", kNoteName[rollPitch % 12], rollPitch / 12 + 2);
  int bar = rollStep / 16 + 1;
  int bars = patLen / 16;
  if (bars < 1) {
    bars = 1;
  }
  uint8_t coverCell = 0;
  int cover = rollCoverStartAt(&coverCell);
  canvas->setTextColor(COL_DIM);
  if (cover >= 0) {
    int len = ((coverCell >> 4) & 3) + 1;
    canvas->printf(" x%d  B%d/%d", len, bar, bars);
  } else {
    canvas->printf("  rest  B%d/%d", bar, bars);
  }
  if (snap.hint[0]) {
    canvas->setTextColor(COL_AMBER);
    canvas->setCursor(168, 14);
    canvas->printf("%.8s", snap.hint);
  }

  for (int p = 0; p < kRollRows; p++) {
    int y = kRollY + (kRollRows - 1 - p) * kRowH;
    bool black = blackSemitone(p);
    canvas->fillRect(kGridX, y, kSpriteW - kGridX, kRowH, black ? COL_LANE_B : COL_LANE_W);
    if (black) {
      canvas->fillRect(8, y, 8, kRowH, COL_KEY_B);
    } else {
      canvas->fillRect(8, y, 13, kRowH, COL_KEY_W);
    }
  }
  for (int oct = 0; oct < 4; oct++) {
    int topPitch = oct * 12 + 11;
    int y = kRollY + (kRollRows - 1 - topPitch) * kRowH;
    drawRollDigit(1, y + 10, oct + 2);
    int cPitch = oct * 12;
    int cy = kRollY + (kRollRows - 1 - cPitch) * kRowH;
    canvas->fillRect(19, cy, 2, kRowH, COL_AMBER);
  }

  for (int i = 0; i <= vis; i++) {
    int step = origin + i;
    if (step > patLen || (step % 4) != 0) {
      continue;
    }
    int x = kGridX + i * stepW;
    if (x >= kSpriteW) {
      break;
    }
    uint16_t line = (step % 16) == 0 ? COL_BARLINE : COL_BEAT;
    canvas->fillRect(x, kRollY, 1, kRollH, line);
  }

  int from = origin - 3;
  if (from < 0) {
    from = 0;
  }
  int to = origin + vis;
  if (to > patLen) {
    to = patLen;
  }
  int count = snap.rollCount;
  if (count > 128) {
    count = 128;
  }
  uint16_t noteColor = COL_TRACK[snap.track & 3];
  for (int s = from; s < to && s < count; s++) {
    drawRollBlock(s, snap.roll[s], origin, stepW, noteColor);
#if MOTHDECK_CHORDS
    for (int slot = 0; slot < 3; slot++) {
      drawRollBlock(s, snap.rollChord[s][slot], origin, stepW, noteColor);
    }
#endif
  }

  int len = snap.patternLength ? (int)snap.patternLength : patLen;
  int head = (int)snap.step - (int)snap.pattern * len;
  if (head >= origin && head < origin + vis && head < patLen) {
    int x = kGridX + (head - origin) * stepW;
    canvas->fillRect(x, kRollY, 1, kRollH, COL_PLAY);
  }
  if (rollStep >= origin && rollStep < origin + vis) {
    int x = kGridX + (rollStep - origin) * stepW;
    int y = kRollY + (kRollRows - 1 - rollPitch) * kRowH - 1;
    int h = kRowH + 2;
    if (y < kRollY) {
      h -= kRollY - y;
      y = kRollY;
    }
    if (y + h > kRollY + kRollH) {
      h = kRollY + kRollH - y;
    }
    if (h > 1 && stepW > 1) {
      canvas->drawRect(x, y, stepW, h, COL_CURSOR);
    }
  }

  if (audioFaultText()[0] && strcmp(audioFaultText(), "audio ok") != 0) {
    legend(audioFaultText());
  } else {
    legend("Fn move  ; hold  Ent steps");
  }
}

static void drawTrackVoices(int y) {
  for (int t = 0; t < 4; t++) {
    char name[8];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    int x = t * 60;
    if (t == snap.track) {
      canvas->fillRect(x, y - 1, 59, 11, COL_TRACK[t]);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(x + 2, y);
    canvas->printf("%d %.6s", t + 1, name);
  }
}

static void drawPlay() {
  for (int t = 0; t < 4; t++) {
    int y = 16 + t * 12;
    canvas->setTextColor(t == snap.track ? COL_TRACK[t] : COL_DIM);
    canvas->setCursor(2, y);
    canvas->printf("%d", t + 1);
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(14, y);
    char name[8];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    canvas->printf("%.6s", name);
    int lv = snap.level[t];
    if (lv < 0) {
      lv = -lv;
    }
    int w = lv / 800;
    if (w > 40) {
      w = 40;
    }
    canvas->fillRect(70, y + 1, 40, 6, COL_BAR);
    canvas->fillRect(70, y + 1, w, 6, COL_TRACK[t]);
    if (snap.mute[t]) {
      canvas->setTextColor(COL_WARN);
      canvas->setCursor(114, y);
      canvas->print("M");
    }
    if (snap.loopOn[t]) {
      canvas->setTextColor(COL_PLAY);
      canvas->setCursor(124, y);
      canvas->print("L");
    }
  }
  int y = 66;
  for (int s = 0; s < 16; s++) {
    int x = 2 + s * 14;
    bool on = snap.notes[snap.track][s] > 0;
    bool head = (int)snap.barOrigin + s == (int)snap.step;
    uint16_t c = head ? COL_AMBER : (on ? COL_TRACK[snap.track] : COL_BAR);
    canvas->fillRect(x, y, 12, 10, c);
    if (on) {
      canvas->setTextColor(head ? COL_BG : COL_TEXT);
      canvas->setCursor(x + 1, y + 1);
      int note = snap.notes[snap.track][s];
      if (note >= 1 && note <= 12) {
        canvas->print(kNoteName[note - 1][0]);
      }
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 82);
  unsigned bars = snap.barCount ? snap.barCount : 1;
  unsigned bar = snap.barIndex ? snap.barIndex : 1;
  canvas->printf("B%u/%u  P%u  Oct %u  %s", bar, bars, (unsigned)snap.pattern + 1, snap.octave,
                 snap.songMode ? "Song" : "Patt");
  canvas->setCursor(2, 94);
  canvas->setTextColor(COL_TEXT);
  canvas->print(snap.hint);
  canvas->setCursor(2, 106);
  canvas->setTextColor(COL_DIM);
  canvas->print("Z row piano   Q row +oct");
  if (audioFaultText()[0] && strcmp(audioFaultText(), "audio ok") != 0) {
    legend(audioFaultText());
  } else {
    legend("Spc play  Fn ,/ bar  Ent roll");
  }
}

static int instCount() {
  return 13 + instrumentBank.Count();
}

static void instLabel(int index, char *dst, int n) {
  if (index < 12) {
    snprintf(dst, n, "%02d %s", index, InstrumentBank::BuiltinName(index));
    return;
  }
  if (index == 12) {
    snprintf(dst, n, "12 Loops");
    return;
  }
  const PluginInfo &p = instrumentBank.At(index - 13);
  snprintf(dst, n, "%s %s", p.loaded ? "*" : " ", p.name[0] ? p.name : p.folder);
}

static bool rowIsTrackVoice(int idx) {
  int id = snap.trackVoice[snap.track];
  if (idx < 12) {
    return id == idx;
  }
  if (idx == 12) {
    return id == kLoopsVoice;
  }
  if (idx - 13 >= instrumentBank.Count()) {
    return false;
  }
  const PluginInfo &p = instrumentBank.At(idx - 13);
  return p.loaded && p.id == (uint8_t)id;
}

static void drawInst() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Assign to track ");
  canvas->printf("%d", snap.track + 1);
  drawTrackVoices(28);
  int total = instCount();
  if (total < 1) {
    total = 12;
  }
  int top = cursor - 2;
  if (top < 0) {
    top = 0;
  }
  for (int row = 0; row < 5; row++) {
    int idx = top + row;
    if (idx >= total) {
      break;
    }
    char line[28];
    instLabel(idx, line, (int)sizeof(line));
    int y = 42 + row * 11;
    canvas->setCursor(2, y);
    canvas->setTextColor(idx == cursor ? COL_BG : COL_TEXT);
    if (idx == cursor) {
      canvas->fillRect(0, y - 2, 240, 11, COL_AMBER);
    }
    canvas->print(line);
    if (rowIsTrackVoice(idx)) {
      canvas->print(" =");
    }
    if (idx >= 13) {
      const PluginInfo &p = instrumentBank.At(idx - 13);
      if (p.error[0] && idx == cursor) {
        canvas->print(" ");
        canvas->print(p.error);
      }
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 100);
  if (cursor == 12) {
    canvas->print("Notes play SD loops, synced");
  } else {
    canvas->printf("Kit %.14s   , /", drumKit.Name(drumKit.Selected()));
  }
  legend(", / kit   Ent assign   R scan");
}

static const char *kFxLabel[] = {
  "Filter", "Cutoff", "Res", "Delay", "Feedback", "Mix", "Reverb", "Crush", "Drive", "Chorus", "Tremolo",
  "Scale", "Root", "Arp", "Glide", "Osc2", "Blend", "Coarse"
};
static const int kFxRows = 18;
static const char *kScaleName[] = { "off", "major", "minor", "harm", "mixo", "phry", "chrom" };
static const char *kRootName[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *kOsc2Name[] = { "off", "sine", "square", "saw", "tri" };

static int packFx(int row, int dir) {
  return (row & 0xFF) | ((dir & 0xFF) << 8);
}

static int fxValue(int row) {
  switch (row) {
    case 0: return snap.fxFilter;
    case 1: return snap.fxCutoff;
    case 2: return snap.fxRes;
    case 3: return snap.fxDelay;
    case 4: return snap.fxFb;
    case 5: return snap.fxMix;
    case 6: return snap.fxRev;
    case 7: return snap.fxCrush;
    case 8: return snap.fxDrive;
    case 9: return snap.fxChorus;
    case 10: return snap.fxTrem;
    case 11: return snap.scaleMode;
    case 12: return snap.scaleRoot;
    case 13: return snap.arpMode == 3 ? 3 : snap.arp[snap.track];
    case 14: return snap.glideMs;
    case 15: return snap.osc2Wave;
    case 16: return snap.blend;
    default: return snap.osc2Coarse;
  }
}

static void tweakFx(int dir) {
  audioCommand('f', packFx(mixRow, dir));
}

static void fxText(int row, char *dst, int n) {
  int v = fxValue(row);
  if (row == 0) {
    snprintf(dst, n, "%s", v == 1 ? "low pass" : (v == 2 ? "high pass" : "off"));
  } else if (row == 3) {
    snprintf(dst, n, "%s", v == 1 ? "1/32" : (v == 2 ? "1/16" : (v == 3 ? "1/8" : "off")));
  } else if (row == 11) {
    snprintf(dst, n, "%s", (v >= 0 && v <= 6) ? kScaleName[v] : "off");
  } else if (row == 12) {
    snprintf(dst, n, "%s", (v >= 0 && v <= 11) ? kRootName[v] : "C");
  } else if (row == 13) {
    snprintf(dst, n, "%s", v == 3 ? "held" : (v == 1 ? "pat 1" : (v == 2 ? "pat 2" : "off")));
  } else if (row == 14) {
    snprintf(dst, n, "%d ms", v);
  } else if (row == 15) {
    snprintf(dst, n, "%s", (v >= 0 && v <= 4) ? kOsc2Name[v] : "off");
  } else {
    snprintf(dst, n, "%d", v);
  }
}

static void drawFx() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->printf("FX  track %d  %s", snap.track + 1, snap.inst);
  int top = mixRow - 2;
  if (top < 0) {
    top = 0;
  }
  for (int row = 0; row < 7; row++) {
    int idx = top + row;
    if (idx >= kFxRows) {
      break;
    }
    int y = 30 + row * 12;
    if (idx == mixRow) {
      canvas->fillRect(0, y - 1, 240, 12, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    canvas->print(kFxLabel[idx]);
    char value[12];
    fxText(idx, value, (int)sizeof(value));
    canvas->setCursor(120, y);
    canvas->print(value);
  }
  legend("1-4 track   Fn , / value   Ent +");
}

static void drawMixer() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Mixer");
  if (snap.solo) {
    canvas->print("  solo");
  }
  for (int t = 0; t < 4; t++) {
    int x = 6 + t * 58;
    bool sel = t == (int)snap.track;
    if (sel) {
      canvas->fillRect(x, 28, 56, 90, COL_BAR);
      canvas->drawRect(x, 28, 56, 90, COL_TRACK[t]);
    }
    char name[10];
    voiceName(snap.trackVoice[t], name, (int)sizeof(name));
    canvas->setTextColor(sel ? COL_TRACK[t] : COL_TEXT);
    canvas->setCursor(x + 4, 32);
    canvas->printf("%d %.5s", t + 1, name);
    int fy = 46;
    int fh = 52;
    canvas->drawRect(x + 22, fy, 12, fh, COL_DIM);
    int vol = snap.vol[t];
    if (vol < 0) {
      vol = 0;
    } else if (vol > 8) {
      vol = 8;
    }
    int h = vol * (fh - 2) / 8;
    canvas->fillRect(x + 23, fy + fh - 1 - h, 10, h, sel ? COL_AMBER : COL_TRACK[t]);
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(x + 4, 104);
    canvas->printf("vol %d", vol);
    if (snap.mute[t]) {
      canvas->setTextColor(COL_WARN);
      canvas->print(" M");
    }
  }
  legend("Fn ;/. vol   Fn ,/ track   M S");
}

static void drawSong() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print(sdCard.Mounted() ? "Song slots  /moth" : "No SD card");
  for (int s = 0; s < 4; s++) {
    bool has = false;
    SdResult st = storage.Status(s, &has);
    int y = 32 + s * 14;
    if (s == songSlot) {
      canvas->fillRect(0, y - 2, 168, 13, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    if (st == SD_NO_CARD) {
      canvas->printf("Slot %d  ---", s + 1);
    } else {
      canvas->printf("Slot %d  %s", s + 1, has ? "saved" : "empty");
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(4, 92);
  canvas->printf("Bars %u/8  %s  Mstr %u", snap.barCount ? snap.barCount : 1, snap.songMode ? "song" : "patt",
                 2 - snap.masterVolume);
  canvas->setCursor(4, 104);
  canvas->print("S save L load X del T stat");
  legend("N new  C copy  V paste  G all");
}

static void drawLoops() {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  if (loopsBlocked) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(2, 40);
    canvas->print("MIDI mode: loops off");
    canvas->setCursor(2, 56);
    canvas->print("Turn BLE off to load them");
    legend("Settings  Fn ,  BLE off");
    return;
  }
  if (!sdCard.Mounted()) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(2, 40);
    canvas->print("Insert a card for loops");
    canvas->setCursor(2, 54);
    canvas->print("/moth/loops/<name>/");
    legend("R rescan   Tab page");
    return;
  }
  canvas->printf("Loops  q:%s  trk %d", quantize == 0 ? "now" : (quantize == 1 ? "beat" : "bar"), snap.track + 1);
  int n = loopLibrary.Count();
  if (n == 0) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(2, 40);
    canvas->print("No libraries on the card");
    legend("R rescan");
    return;
  }
  loopLib = clampi(loopLib, 0, n - 1);
  const LoopLibInfo &lib = loopLibrary.At(loopLib);
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 30);
  canvas->printf("%s  %dbpm  %db", lib.name, lib.bpm, lib.bars);
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 42);
  canvas->print(lib.tags);
  if (lib.error[0]) {
    canvas->setTextColor(COL_WARN);
    canvas->setCursor(2, 54);
    canvas->print(lib.error);
  } else if (loopLibrary.LimitLine()[0]) {
    canvas->setTextColor(COL_WARN);
    canvas->setCursor(2, 54);
    canvas->print(loopLibrary.LimitLine());
  }
  int visible = 5;
  int top = 0;
  if (lib.entryCount > visible) {
    top = loopRow - visible + 1;
    if (top < 0) {
      top = 0;
    }
    if (top > lib.entryCount - visible) {
      top = lib.entryCount - visible;
    }
  }
  for (int i = 0; i < visible && top + i < lib.entryCount; i++) {
    int idx = top + i;
    int y = 66 + i * 10;
    if (idx == loopRow) {
      canvas->fillRect(0, y - 1, 220, 10, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    canvas->print(lib.entries[idx].kind == LOOP_PATTERN ? "PAT " : "WAV ");
    canvas->print(lib.entries[idx].file);
  }
  legend("Ent launch  A aud  Q quant  S stop");
}

static void drawMidi(BleMidi &ble) {
  bool onAir = ble.Advertising();
  if (ble.Connected()) {
    canvas->setTextColor(COL_PLAY);
  } else if (onAir) {
    canvas->setTextColor(COL_AMBER);
  } else {
    canvas->setTextColor(COL_WARN);
  }
  canvas->setCursor(2, 16);
  canvas->print(ble.StatusLine());
  if (ble.Connected()) {
    canvas->setTextColor(COL_TEXT);
    canvas->setCursor(148, 16);
    canvas->print(ble.LinkLine());
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 28);
  canvas->print(ble.DiagLine());
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 40);
  BleAdvertPackets advert;
  buildBleMidiAdvert(bleName, MOTHDECK_BLE_NAME_DEFAULT, &advert);
  if (advert.nameShortened) {
    canvas->printf("Name %s (%s)", bleName, advert.advName);
  } else {
    canvas->printf("Name %s", bleName);
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 54);
  canvas->print("Ch 1-4 = tracks");
  canvas->setCursor(2, 66);
  canvas->print("Notes 36-83  C2-B5");
  canvas->setCursor(2, 78);
  canvas->print("CC0/32 bank  CC1 low pass");
  canvas->setCursor(2, 90);
  canvas->print("CC7/39 volume  bend pitch");
  canvas->setCursor(2, 102);
  MidiCounters ctr;
  ble.CopyCounters(&ctr);
  canvas->printf("pk %lu  msg %lu  ovf %lu", (unsigned long)ctr.packets, (unsigned long)ctr.messages, (unsigned long)ctr.overflows);
  canvas->setCursor(2, 114);
  canvas->printf("clk %lu  note %lu", (unsigned long)ctr.clocks, (unsigned long)ctr.notes);
  legend("E edit name in Settings");
}

static void drawSettings(BleMidi &ble) {
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Settings");
  const char *rows[] = {"Speaker", "Brightness", "BLE name", "BLE", "Unload", "Battery", "Radio", "Card", "Bars"};
  int top = cursor > 3 ? cursor - 3 : 0;
  for (int i = 0; i < 5; i++) {
    int idx = top + i;
    if (idx > 8) {
      break;
    }
    int y = 30 + i * 12;
    if (idx == cursor && !naming) {
      canvas->fillRect(0, y - 1, 240, 12, COL_AMBER);
      canvas->setTextColor(COL_BG);
    } else {
      canvas->setTextColor(COL_TEXT);
    }
    canvas->setCursor(4, y);
    if (idx == 4) {
      canvas->print(ble.Resident() ? "Unload BLE" : "Load BLE");
    } else if (idx == 6) {
#if MOTHOS_DEV_LOG
      canvas->print("Heap");
#else
      canvas->print("Radio");
#endif
    } else {
      canvas->print(rows[idx]);
    }
    canvas->setCursor(100, y);
    if (idx == 0) {
      canvas->printf("%u", outVol);
    } else if (idx == 1) {
      canvas->printf("%u", bright);
    } else if (idx == 2) {
      canvas->print(naming ? edit : bleName);
    } else if (idx == 3) {
      canvas->print(ble.SwitchLine());
    } else if (idx == 4) {
      if (bleUnloadPending) {
        canvas->print("next boot");
      }
    } else if (idx == 5) {
      int mv = boardBatteryMv();
      int pct = boardBatteryPct();
      if (pct < 0) {
        canvas->print("n/a");
      } else {
        canvas->printf("%dmV %d%%", mv, pct);
      }
    } else if (idx == 6) {
      const char *bleWord = ble.Connected() ? "conn" : (ble.Advertising() ? "adv" : "off");
#if MOTHOS_DEV_LOG
      unsigned freeKb = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
      unsigned blkKb = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
      canvas->printf("%uk blk %uk %s", freeKb, blkKb, bleWord);
#else
      canvas->print(bleWord);
#endif
    } else if (idx == 7) {
      canvas->print(sdCard.Mounted() ? "mounted" : "none");
    } else {
      canvas->printf("%u", snap.barCount ? snap.barCount : 1);
    }
  }
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(4, 96);
  canvas->print(ble.CounterLine());
  canvas->setCursor(4, 108);
  canvas->printf("v%s  %s", MOTHDECK_VERSION, BOARD_NAME);
  if (!naming && cursor == 6) {
    legend(ble.StatusLine());
  } else if (!naming && cursor == 3) {
    legend("Fn , off   Fn / on");
  } else if (!naming && cursor == 4) {
    legend(ble.Resident() ? (bleUnloadPending ? "Ent unload  Fn , keep" : "Ent unload") : "Ent load");
  } else {
    legend(naming ? "Ent apply  Bksp  ` cancel" : (cursor == 8 ? "Fn ,/ bars" : "Lf/Rt change  Ent name"));
  }
}

static void drawExit() {
  if (!launcherOk) {
    canvas->setTextColor(COL_DIM);
    canvas->setCursor(2, 36);
    canvas->print("Launcher not found");
    canvas->setCursor(2, 52);
    canvas->print("Exit stays off.");
    canvas->setCursor(2, 68);
    canvas->print("Hold ` does nothing.");
    legend("` back to Play");
    return;
  }
  canvas->setTextColor(COL_AMBER);
  canvas->setCursor(2, 16);
  canvas->print("Exit to Launcher");
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(2, 36);
  canvas->print("Enter rewrites the boot");
  canvas->setCursor(2, 48);
  canvas->print("selection back to Launcher");
  canvas->setCursor(2, 60);
  canvas->print("and restarts.");
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(2, 78);
  canvas->print("Hold ` or the side button");
  canvas->setCursor(2, 90);
  canvas->print("at any time, including boot.");
  canvas->setCursor(2, 102);
  canvas->print("Then press Enter on splash.");
  legend("Ent exit   ` back to Play");
}

static void drawOverlay() {
  canvas->fillRect(40, 18, 160, 100, COL_BAR);
  canvas->drawRect(40, 18, 160, 100, COL_AMBER);
  int rowH = launcherOk ? 10 : 9;
  for (int i = 0; i < kPageCount; i++) {
    bool grey = (i == kExitPage && !launcherOk);
    canvas->setCursor(48, 21 + i * rowH);
    if (grey) {
      canvas->setTextColor(COL_DIM);
      canvas->print("  ");
      canvas->print(kPageName[i]);
      continue;
    }
    if (i == page) {
      canvas->setTextColor(COL_AMBER);
      canvas->print("> ");
    } else {
      canvas->setTextColor(COL_TEXT);
      canvas->print("  ");
    }
    canvas->print(kPageName[i]);
  }
  if (!launcherOk) {
    canvas->setTextColor(COL_DIM);
    canvas->setCursor(48, 21 + kPageCount * rowH);
    canvas->print("Launcher not found");
  }
}

static void drawBleAsk() {
  canvas->fillRect(8, 28, 224, 78, COL_BAR);
  canvas->drawRect(8, 28, 224, 78, COL_AMBER);
  canvas->setTextColor(COL_TEXT);
  canvas->setCursor(16, 40);
  canvas->print("MIDI mode: loops off.");
  canvas->setCursor(16, 56);
  canvas->print("Save song? Y / N");
  canvas->setTextColor(COL_DIM);
  canvas->setCursor(16, 76);
  canvas->print("` cancels");
}

#if MOTHDECK_BOARD_TTGO
static char padLegend[48];
static int padLatch = -1;
static int padMenu = -1;
static int padSub = 0;
static bool padEntry = false;
static bool padNaming = false;
static int padGlyph = 0;
static int padOct = 1;

static void padLegendSet(const char *msg) {
  if (!msg || !msg[0]) {
    padLegend[0] = 0;
    return;
  }
  snprintf(padLegend, sizeof(padLegend), "%s", msg);
}
#endif

void uiDraw(BleMidi &ble) {
  if (!canvas) {
    return;
  }
  loopsBlocked = ble.UserEnabled() && !MOTHDECK_LOOPS_WITH_BLE;
  audioReadSnap(&snap);
  if (!boardHasDisplay()) {
    return;
  }
  canvas->fillSprite(COL_BG);
  statusBar(ble);
  switch (page) {
    case 0:
      if (rollView) {
        drawRoll();
      } else {
        drawPlay();
      }
      break;
    case 1: drawInst(); break;
    case kFxPage: drawFx(); break;
    case kMixerPage: drawMixer(); break;
    case kSongPage: drawSong(); break;
    case kLoopsPage: drawLoops(); break;
    case kMidiPage: drawMidi(ble); break;
    case kSettingsPage: drawSettings(ble); break;
    default: drawExit(); break;
  }
#if MOTHDECK_BOARD_TTGO
  if (padLegend[0]) {
    legend(padLegend);
  }
#endif
  drawToast();
  if (bleAsk) {
    drawBleAsk();
  } else if (overlay) {
    drawOverlay();
  }
  if (canvas->getBuffer()) {
    pushCanvas();
  }
}

static void assignInstrument() {
  if (cursor < 12) {
    audioCommand('I', cursor);
    toastSet(InstrumentBank::BuiltinName(cursor));
    return;
  }
  if (cursor == 12 && loopsBlocked) {
    toastSet("MIDI mode: loops off");
    return;
  }
  if (cursor == 12) {
    char err[48];
    err[0] = 0;
    int n = loopLibrary.PreloadInstrument(err, (int)sizeof(err));
    audioCommand('I', kLoopsVoice);
    if (n <= 0) {
      toastSet(err[0] ? err : "No loop samples");
    } else if (loopLibrary.AudioCount() > n && err[0]) {
      toastSet(err);
    } else {
      toastSet("Loops");
    }
    return;
  }
  int index = cursor - 13;
  char err[48];
  PatchAssign patch;
  memset(&patch, 0, sizeof(patch));
  int id = instrumentBank.Load(index, err, (int)sizeof(err), &patch);
  if (id < 0) {
    toastSet(err[0] ? err : "Load failed");
    return;
  }
  if (patch.active) {
    patch.instrument = (uint8_t)id;
    audioStagePatch(patch);
    audioCommand('J', id);
  } else {
    audioCommand('I', id);
  }
  toastSet(instrumentBank.At(index).name);
}

static bool doSave() {
  SongData song;
  if (!audioCapture(&song)) {
    toastSet("Capture failed");
    return false;
  }
  instrumentBank.FillSongRefs(&song);
  SdResult r = storage.Save(songSlot, song);
  toastSet(SdStorage::ResultText(r, songSlot));
  return r == SD_SAVED;
}

static void doLoad() {
  SongData song;
  SdResult r = storage.Load(songSlot, &song);
  if (r != SD_LOADED) {
    toastSet(SdStorage::ResultText(r, songSlot));
    return;
  }
  char err[48];
  instrumentBank.PrepareSong(&song, err, (int)sizeof(err));
  if (err[0]) {
    toastSet(err);
  }
  char lerr[48];
  lerr[0] = 0;
  bool hadLoops = false;
  for (int t = 0; t < 4; t++) {
    if (song.loops[t].enabled) {
      hadLoops = true;
    }
  }
  if (loopsBlocked) {
    for (int t = 0; t < 4; t++) {
      song.loops[t].enabled = 0;
    }
    if (hadLoops) {
      snprintf(lerr, sizeof(lerr), "MIDI mode: loops off");
    }
  } else {
    loopLibrary.PrepareSong(&song, lerr, (int)sizeof(lerr));
  }
  audioApplySong(song);
  if (!loopsBlocked) {
    for (int t = 0; t < 4; t++) {
      LoopArm arm;
      if (loopLibrary.TrackArm(t, &arm)) {
        audioArmLoop(t, arm);
      }
    }
  }
  if (!err[0]) {
    toastSet(lerr[0] ? lerr : SdStorage::ResultText(r, songSlot));
  }
}

static void launchLoop(bool audition) {
  if (loopsBlocked) {
    toastSet("MIDI mode: loops off");
    return;
  }
  if (loopLibrary.Count() <= 0) {
    toastSet("No libraries");
    return;
  }
  uint8_t steps[64];
  int count = 0;
  bool pattern = false;
  LoopArm arm;
  char err[48];
  if (!loopLibrary.LoadEntry(loopLib, loopRow, &arm, steps, &count, &pattern, err, (int)sizeof(err))) {
    toastSet(err[0] ? err : "Loop failed");
    return;
  }
  if (pattern) {
    audioWritePattern(snap.track, steps, count);
    toastSet("Pattern on track");
    return;
  }
  arm.quantize = (uint8_t)quantize;
  if (audition) {
    audioAudition(arm);
    toastSet("Audition");
  } else {
    audioArmLoop(snap.track, arm);
    toastSet(quantize == 0 ? "Loop" : "Loop queued");
  }
}

static void cycleKit(int dir) {
  int n = drumKit.Count();
  if (n < 1) {
    n = 1;
  }
  int s = drumKit.Selected();
  s = (s + dir + n) % n;
  char err[48];
  if (!drumKit.Select(s, err, (int)sizeof(err))) {
    toastSet(err[0] ? err : "Kit failed");
    return;
  }
  if (n < 2) {
    toastSet("No SD kits");
    return;
  }
  toastSet(drumKit.Name(s));
}

static bool projectHasLoops() {
  audioReadSnap(&snap);
  for (int t = 0; t < 4; t++) {
    if (snap.loopOn[t] || snap.trackVoice[t] == kLoopsVoice) {
      return true;
    }
  }
  return false;
}

static void silenceLoops() {
  for (int t = 0; t < 4; t++) {
    audioStopLoop(t);
  }
  audioStopAudition();
  delay(30);
  loopLibrary.DropAudio();
  pcmHoldDropAll();
}

static void restoreLoops() {
  if (!pcmHoldReserved()) {
    pcmHoldReservePreferred();
  }
  if (!pcmHoldReserved()) {
    pcmHoldReserveFit();
  }
  char err[48];
  err[0] = 0;
  loopLibrary.PreloadInstrument(err, (int)sizeof(err));
  if (loopLibrary.AudioCount() > loopLibrary.ReadyCount() && err[0]) {
    toastSet(err);
  }
}

static void stopRec() {
  // 'p' stops only when transport is already running, so a stale snap
  // cannot toggle Rec back on.
  audioCommand('p', 0);
}

static void finishBleOn(BleMidi &ble) {
  if (!ble.Resident()) {
    // The stack is created at boot, before the loop pool. Starting it
    // after the card scan would allocate, and that is the failure this
    // switch is not allowed to have. The next boot brings it up.
    bleOn = 1;
    bleLoad = 1;
    bleUnloadPending = false;
    savePrefs();
    toastSet("BLE loads after restart");
    return;
  }
  stopRec();
#if !MOTHDECK_LOOPS_WITH_BLE
  silenceLoops();
#endif
  bleLoad = 1;
  bleUnloadPending = false;
  ble.SetUserEnabled(true);
  bleOn = 1;
  savePrefs();
  ble.SetPendingUnload(false);
  toastSet("BLE on, rec off");
}

static void finishBleOff(BleMidi &ble) {
  if (ble.Resident()) {
    ble.SetUserEnabled(false);
  }
  bleOn = 0;
  savePrefs();
#if !MOTHDECK_LOOPS_WITH_BLE
  restoreLoops();
#endif
  toastSet("BLE off");
}

static void requestBleOn(BleMidi &ble) {
  if (ble.UserEnabled()) {
    return;
  }
#if MOTHDECK_LOOPS_WITH_BLE
  finishBleOn(ble);
#else
  if (projectHasLoops()) {
    bleAsk = true;
    return;
  }
  finishBleOn(ble);
#endif
}

static void answerBleAsk(char c, bool cancel, BleMidi &ble) {
  if (cancel) {
    bleAsk = false;
    toastSet("BLE stays off");
    return;
  }
  if (c != 'y' && c != 'n') {
    return;
  }
  if (c == 'y' && !doSave()) {
    bleAsk = false;
    toastSet("Save failed, BLE stays off");
    return;
  }
  audioReadSnap(&snap);
  int bars = snap.barCount ? (int)snap.barCount : 1;
  audioCommand('X', bars - 1);
  delay(40);
  bleAsk = false;
  finishBleOn(ble);
}

static void doLoadBle(BleMidi &ble) {
  if (ble.Resident()) {
    bleLoad = 1;
    bleUnloadPending = false;
    savePrefs();
    ble.SetPendingUnload(false);
    toastSet("BLE stays loaded");
    return;
  }
  // Same boot path as a normal start: NimBLE first, then the loop pool.
  // This session keeps the windows it already has.
  bleLoad = 1;
  bleUnloadPending = false;
  savePrefs();
  toastSet("BLE loads after restart");
}

static void doUnloadBle(BleMidi &ble) {
  // NimBLE deinit from the UI task calls nimble_port_stop and then
  // nimble_port_deinit without waiting for the host task to leave
  // nimble_port_run. deinit(true) also releases controller memory and the
  // library will not init again until reboot. Either path can reset the
  // board while the speaker DMA is running. The choice is stored and BLE
  // is skipped on the next boot. The stack stays resident until then.
  if (!ble.Resident()) {
    toastSet("BLE unloaded");
    return;
  }
  bleLoad = 0;
  bleUnloadPending = true;
  savePrefs();
  ble.SetPendingUnload(true);
  toastSet("BLE unloads after restart");
}

static void handleChar(char c, bool ctrl, bool shift, bool alt, bool opt, bool fn, BleMidi &ble) {
  if (fn) {
    if (c == ';') {
      if (page == 0 && rollView) {
        rollPitch = clampi(rollPitch + 1, 0, kRollRows - 1);
        return;
      }
      if (page == 1) {
        cursor = clampi(cursor - 1, 0, instCount() - 1);
      } else if (page == kFxPage) {
        mixRow = clampi(mixRow - 1, 0, kFxRows - 1);
      } else if (page == kMixerPage) {
        audioCommand('v', clampi((int)snap.vol[snap.track] + 1, 0, 8));
      } else if (page == kLoopsPage) {
        int n = 0;
        if (loopLibrary.Count() > 0) {
          n = loopLibrary.At(loopLib).entryCount - 1;
        }
        loopRow = clampi(loopRow - 1, 0, n < 0 ? 0 : n);
      } else if (page == kSettingsPage) {
        cursor = clampi(cursor - 1, 0, 8);
      }
      return;
    }
    if (c == '.') {
      if (page == 0 && rollView) {
        rollPitch = clampi(rollPitch - 1, 0, kRollRows - 1);
        return;
      }
      if (page == 1) {
        cursor = clampi(cursor + 1, 0, instCount() - 1);
      } else if (page == kFxPage) {
        mixRow = clampi(mixRow + 1, 0, kFxRows - 1);
      } else if (page == kMixerPage) {
        audioCommand('v', clampi((int)snap.vol[snap.track] - 1, 0, 8));
      } else if (page == kLoopsPage) {
        int n = 0;
        if (loopLibrary.Count() > 0) {
          n = loopLibrary.At(loopLib).entryCount - 1;
        }
        loopRow = clampi(loopRow + 1, 0, n < 0 ? 0 : n);
      } else if (page == kSettingsPage) {
        cursor = clampi(cursor + 1, 0, 8);
      }
      return;
    }
    if (c == ',') {
      if (page == 1) {
        cycleKit(-1);
      } else if (page == kFxPage) {
        tweakFx(-1);
      } else if (page == kMixerPage) {
        audioCommand('T', (snap.track + 3) & 3);
      } else if (page == kLoopsPage && loopLibrary.Count() > 0) {
        loopLib = (loopLib + loopLibrary.Count() - 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == kSettingsPage && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol - 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 1) {
        bright = (uint8_t)clampi((int)bright - 8, 10, 255);
        boardSetBrightness(bright);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 3) {
        bleAsk = false;
        finishBleOff(ble);
      } else if (page == kSettingsPage && cursor == 4 && bleUnloadPending) {
        doLoadBle(ble);
      } else if (page == kSettingsPage && cursor == 8) {
        int bars = snap.barCount ? (int)snap.barCount : 1;
        audioCommand('Y', clampi(bars - 1, 1, 8));
      } else if (page == 0 && rollView) {
        rollFollow = false;
        rollStep = clampi(rollStep - 1, 0, rollPatLen() - 1);
        clampRoll();
      } else if (page == 0) {
        audioCommand('W', -1);
      }
      return;
    }
    if (c == '/') {
      if (page == 1) {
        cycleKit(1);
      } else if (page == kFxPage) {
        tweakFx(1);
      } else if (page == kMixerPage) {
        audioCommand('T', (snap.track + 1) & 3);
      } else if (page == kLoopsPage && loopLibrary.Count() > 0) {
        loopLib = (loopLib + 1) % loopLibrary.Count();
        loopRow = 0;
      } else if (page == kSettingsPage && cursor == 0) {
        outVol = (uint8_t)clampi((int)outVol + 8, 0, 255);
        audioSetSpeakerVolume(outVol);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 1) {
        bright = (uint8_t)clampi((int)bright + 8, 10, 255);
        boardSetBrightness(bright);
        savePrefs();
      } else if (page == kSettingsPage && cursor == 3) {
        requestBleOn(ble);
      } else if (page == kSettingsPage && cursor == 4) {
        if (ble.Resident()) {
          doUnloadBle(ble);
        } else {
          doLoadBle(ble);
        }
      } else if (page == kSettingsPage && cursor == 8) {
        int bars = snap.barCount ? (int)snap.barCount : 1;
        audioCommand('Y', clampi(bars + 1, 1, 8));
      } else if (page == 0 && rollView) {
        rollFollow = false;
        rollStep = clampi(rollStep + 1, 0, rollPatLen() - 1);
        clampRoll();
      } else if (page == 0) {
        audioCommand('W', 1);
      }
      return;
    }
    if (c == '-' || c == '=') {
      int d = c == '-' ? -12 : 12;
      outVol = (uint8_t)clampi((int)outVol + d, 0, 255);
      audioSetSpeakerVolume(outVol);
      savePrefs();
      toastSet(c == '-' ? "Quieter" : "Louder");
      return;
    }
  }

  if (ctrl && c >= '1' && c <= '4') {
    audioCommand('^', c - '1');
    return;
  }
  if (ctrl && c >= '5' && c <= '8') {
    audioCommand('#', c - '5');
    return;
  }
  if (ctrl && c == 'n') {
    int bars = snap.barCount ? (int)snap.barCount : 1;
    audioCommand('X', bars - 1);
    toastSet("New song");
    return;
  }

  if (c >= '1' && c <= '4') {
    if (page == kSongPage) {
      songSlot = c - '1';
      audioCommand('Z', 0);
      bool has = false;
      storage.Status(songSlot, &has);
      toastSet(has ? "Slot full" : "Slot empty");
    } else {
      audioCommand('T', c - '1');
    }
    return;
  }
  if (c >= '5' && c <= '8' && page != kSongPage) {
    audioCommand('$', c - '5');
    return;
  }
  if (c == '9') {
    if (page == 1) {
      cursor = clampi(cursor - 1, 0, instCount() - 1);
    } else {
      audioCommand('B', (snap.bpmSlot + 3) & 3);
    }
    return;
  }
  if (c == '0') {
    if (page == 1) {
      cursor = clampi(cursor + 1, 0, instCount() - 1);
    } else {
      audioCommand('B', (snap.bpmSlot + 1) & 3);
    }
    return;
  }
  if (c == '-' || c == '=') {
    audioCommand('b', c == '=' ? 1 : -1);
    return;
  }

  if (page == 0) {
    int pitch = 0;
    int add = 0;
    if (pianoKey(c, &pitch, &add)) {
      int oct = clampi((int)snap.octave + add + (shift ? 1 : 0) + (alt ? 1 : 0) - (opt ? 1 : 0), 0, 3);
      if (rollView) {
        rollPitch = clampi(pitch + oct * 12, 0, kRollRows - 1);
      }
      // Stopped, the roll writes the cell under the cursor. While the
      // transport is running, 'N' records at the playhead only if Rec is
      // on. Rec off, including MIDI play-through, only sounds.
      if (rollView && !snap.playing) {
        int lenCode = 0;
        if (rollStep >= 0 && rollStep < (int)snap.rollCount) {
          uint8_t cell = snap.roll[rollStep];
          if ((cell & 0x0F) > 0) {
            lenCode = (cell >> 4) & 3;
          }
        }
        audioCommand('r', (rollStep & 0xFF) | ((pitch & 0x0F) << 8) | ((oct & 0x0F) << 12) | (lenCode << 16));
        return;
      }
      if (oct != (int)snap.octave) {
        audioCommand('O', oct);
        audioCommand('N', pitch);
        audioCommand('O', snap.octave);
      } else {
        audioCommand('N', pitch);
      }
      return;
    }
    if (c == 'a') audioCommand('A', 1);
    else if (c == 'f') audioCommand('A', 2);
    else if (c == 'k') audioCommand('A', 3);
    else if (c == 'l') audioCommand('D', 0);
    else if (c == ';') {
      int cover = rollView ? rollCoverStart() : -1;
      if (cover >= 0) {
#if MOTHDECK_CHORDS
        audioCommand('g', rollPitchPack(cover));
#else
        audioCommand('g', cover);
#endif
      } else {
        audioCommand('L', (snap.envLen) & 3);
      }
    }
    else if (c == '\'') audioCommand('*', 3);
    else if (c == '\\') audioCommand('O', ((int)snap.octave + 1) & 3);
    else if (c == '.') audioCommand('*', 0);
    else if (c == '/') audioCommand('*', 1);
    return;
  }

  if (page == 1) {
    if (c == 'r') {
      instrumentBank.Scan();
      drumKit.Scan();
      loopLibrary.Scan();
      if (loopsBlocked) {
        toastSet("MIDI mode: loops off");
      } else {
        loopLibrary.PreloadInstrument(nullptr, 0);
        toastSet(sdCard.Mounted() ? "Rescanned" : "No SD card");
      }
    } else if (c == ',') {
      cycleKit(-1);
    } else if (c == '/') {
      cycleKit(1);
    }
    return;
  }
  if (page == kSongPage) {
    if (c == 's') doSave();
    else if (c == 'l') doLoad();
    else if (c == 'x') toastSet(SdStorage::ResultText(storage.Delete(songSlot), songSlot));
    else if (c == 't') {
      bool has = false;
      SdResult st = storage.Status(songSlot, &has);
      if (st == SD_OK) {
        toastSet(has ? "Slot full" : "Slot empty");
      } else {
        toastSet(SdStorage::ResultText(st, songSlot));
      }
    } else if (c == 'n') {
      int bars = snap.barCount ? (int)snap.barCount : 1;
      bars = bars >= 8 ? 1 : bars + 1;
      audioCommand('X', bars - 1);
      toastSet("New song");
    } else if (c == 'c') audioCommand('*', 0);
    else if (c == 'v') audioCommand('*', 1);
    else if (c == 'g') audioCommand('*', 2);
    else if (c == 'm') audioCommand('C', 0);
    else if (c == 'h') audioCommand('H', 0);
    else if (c == 'b') audioCommand('B', (snap.bpmSlot + 1) & 3);
    return;
  }
  if (page == kMixerPage) {
    if (c == 'm') audioCommand('V', 0);
    else if (c == 's') audioCommand('V', 3);
    return;
  }
  if (page == kLoopsPage) {
    if (c == 'a') launchLoop(true);
    else if (c == 'q') {
      quantize = (quantize + 1) % 3;
      toastSet(quantize == 0 ? "Quantize now" : (quantize == 1 ? "Quantize beat" : "Quantize bar"));
    } else if (c == 's') audioStopLoop(snap.track);
    else if (c == 'r') {
      loopLibrary.Scan();
      toastSet("Rescanned loops");
    }
    return;
  }
  if (page == kMidiPage && c == 'e') {
    page = kSettingsPage;
    cursor = 2;
    naming = true;
    snprintf(edit, sizeof(edit), "%s", bleName);
  }
}

static KeyEvent eventFrom(const DeckKeys &st) {
  KeyEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.tab = st.tab;
  ev.enter = st.enter;
  ev.del = st.del;
  ev.space = st.space;
  ev.fn = st.fn;
  ev.shift = st.shift;
  ev.ctrl = st.ctrl;
  ev.alt = st.alt;
  ev.opt = st.opt;
  for (int i = 0; i < st.wordLen; i++) {
    char raw = st.word[i];
    if (raw == ' ') {
      continue;
    }
    ev.ch = (char)tolower((unsigned char)raw);
    break;
  }
  return ev;
}

static void applyModal(const ModalAction &action, char typed, BleMidi &ble) {
  if (action.pageDelta) {
    page = stepPage(page, action.pageDelta);
  }
  if (action.cancelToPlay) {
    page = 0;
    overlay = false;
    naming = false;
  }
  if (action.closeMenu) {
    overlay = false;
  }
  if (action.backspace && edit[0]) {
    edit[strlen(edit) - 1] = 0;
  }
  if (action.append && typed && typed != ' ') {
    int n = (int)strlen(edit);
    if (n < 16) {
      edit[n] = typed;
      edit[n + 1] = 0;
    }
  }
  if (action.cancelName) {
    naming = false;
  }
  if (action.applyName) {
    if (edit[0]) {
      snprintf(bleName, sizeof(bleName), "%s", edit);
      savePrefs();
      ble.Restart(bleName);
      toastSet("BLE name set");
    }
    naming = false;
  }
  if (action.confirmExit) {
    requestExit();
  }
}

static void handleState(const DeckKeys &st, BleMidi &ble) {
  if (activeModal(overlay, naming, page == kExitPage) != MODAL_NONE) {
    return;
  }
  if (st.tab) {
    page = stepPage(page, (st.shift || st.ctrl) ? -1 : 1);
    overlay = false;
    naming = false;
    return;
  }
  if (st.enter) {
    if (page == 0) {
      rollView = !rollView;
      if (rollView) {
        rollFollow = true;
      }
      toastSet(rollView ? "Piano roll" : "Step view");
    } else if (page == 1) assignInstrument();
    else if (page == kFxPage) tweakFx(1);
    else if (page == kSongPage) doLoad();
    else if (page == kLoopsPage) launchLoop(false);
    else if (page == kSettingsPage && cursor == 2) {
      naming = true;
      snprintf(edit, sizeof(edit), "%s", bleName);
    } else if (page == kSettingsPage && cursor == 4) {
      if (ble.Resident()) {
        doUnloadBle(ble);
      } else {
        doLoadBle(ble);
      }
    }
    return;
  }
  if (st.del) {
    if (page == 0) {
      if (rollView) {
        int cover = rollCoverStart();
#if MOTHDECK_CHORDS
        if (cover >= 0) {
          audioCommand('e', rollPitchPack(cover));
        } else {
          audioCommand('e', rollStep);
        }
#else
        audioCommand('e', cover >= 0 ? cover : rollStep);
#endif
      } else {
        audioCommand('_', 0);
      }
    } else if (page != 0) {
      page = 0;
      overlay = false;
    }
    return;
  }
  if (st.space) {
    if (!audioRunning()) {
      toastSet(audioFaultText());
    }
    rollFollow = true;
    audioCommand('P', 0);
    return;
  }
  for (int i = 0; i < st.wordLen; i++) {
    char raw = st.word[i];
    if (raw == ' ') {
      continue;
    }
    char c = (char)tolower((unsigned char)raw);
    handleChar(c, st.ctrl, st.shift, st.alt, st.opt, st.fn, ble);
  }
}

#if MOTHDECK_BOARD_TTGO

static const char *kPadGlyphs[] = {"abcdefghijkl", "mnopqrstuvwx", "yz0123456789", "-_"};

static void padLive() {
  padMenu = -1;
  padSub = 0;
  padEntry = false;
  padNaming = false;
  naming = false;
  padLegendSet(0);
}

static void padGoto(int p) {
  if (p == kExitPage && !launcherOk) {
    toastSet("Launcher not found");
    return;
  }
  if (p < 0 || p >= kPageCount) {
    return;
  }
  page = p;
  overlay = false;
  if (!padNaming) {
    naming = false;
  }
}

static void padOpen(int which) {
  padLatch = -1;
  padMenu = which;
  padSub = 0;
  padEntry = false;
  padNaming = false;
  naming = false;
  audioReadSnap(&snap);
  padOct = snap.octave;
  if (which == 0) {
    padGoto(0);
    rollView = true;
    rollFollow = false;
    padLegendSet("Edit  F1 close  F3 sound");
  } else if (which == 1) {
    padGoto(kFxPage);
    padLegendSet("FX  F2 close  F3 mixer");
  } else if (which == 2) {
    padGoto(kLoopsPage);
    padLegendSet("Loops  F3 close");
  } else {
    padLegendSet("Pages  F4 close  F2 file");
  }
}

static void padWriteRoll(int pitch) {
  audioReadSnap(&snap);
  page = 0;
  rollFollow = false;
  if (rollView && !snap.playing) {
    audioCommand('r', (rollStep & 0xFF) | ((pitch & 0x0F) << 8) | ((padOct & 0x0F) << 12));
  } else {
    if (padOct != (int)snap.octave) {
      audioCommand('O', padOct);
    }
    audioCommand('N', pitch);
  }
}

static void padDeleteRoll() {
  page = 0;
  rollView = true;
  rollFollow = false;
  int cover = rollCoverStart();
#if MOTHDECK_CHORDS
  if (cover >= 0) {
    audioCommand('e', rollPitchPack(cover));
  } else {
    audioCommand('e', rollStep);
  }
#else
  audioCommand('e', cover >= 0 ? cover : rollStep);
#endif
}

static void padHoldRoll() {
  audioReadSnap(&snap);
  page = 0;
  rollView = true;
  rollFollow = false;
  int cover = rollCoverStart();
  if (cover >= 0) {
#if MOTHDECK_CHORDS
    audioCommand('g', rollPitchPack(cover));
#else
    audioCommand('g', cover);
#endif
  } else {
    audioCommand('L', (int)snap.envLen & 3);
  }
}

static void padDupStep() {
  audioReadSnap(&snap);
  int src = rollStep - 1;
  if (src < 0 || src >= (int)snap.rollCount) {
    toastSet("Empty");
    return;
  }
  uint8_t cell = snap.roll[src];
  if ((cell & 0x0F) == 0) {
    toastSet("Empty");
    return;
  }
  int pitch = (cell & 0x0F) - 1;
  int len = (cell >> 4) & 3;
  int oct = (cell >> 6) & 3;
  page = 0;
  rollView = true;
  rollFollow = false;
  audioCommand('r', (rollStep & 0xFF) | ((pitch & 0x0F) << 8) | ((oct & 0x0F) << 12) | (len << 16));
}

static void padNudgeOct(int dir) {
  padOct = clampi(padOct + dir, 0, 3);
  audioCommand('O', padOct);
}

static void padEditKey(int key) {
  if (padEntry) {
    if (key == 0) {
      padEntry = false;
      padLegendSet("Edit  F1 close  F4 notes");
      return;
    }
    if (key == 1) {
      rollFollow = false;
      rollStep = clampi(rollStep - 1, 0, rollPatLen() - 1);
      clampRoll();
      return;
    }
    if (key == 2) {
      rollFollow = false;
      rollStep = clampi(rollStep + 1, 0, rollPatLen() - 1);
      clampRoll();
      return;
    }
    if (key == 3) {
      rollView = !rollView;
      toastSet(rollView ? "Piano roll" : "Step view");
      return;
    }
    int pitch = ttgoNotePitch(key);
    if (pitch >= 0) {
      padWriteRoll(pitch);
    }
    return;
  }
  if (key == 1) {
    padSub = 0;
    padGoto(0);
    rollView = true;
    padLegendSet("Edit  F1 close  F4 notes");
    return;
  }
  if (key == 2) {
    padSub = 1;
    padGoto(1);
    padLegendSet("Sound  F1 close  F2 edit");
    return;
  }
  if (key == 3) {
    padEntry = true;
    padGoto(0);
    rollView = true;
    rollFollow = false;
    padLegendSet("Notes  F1 cursor  F2/F3 step");
    return;
  }
  if (padSub == 1) {
    if (key == 4) {
      cursor = clampi(cursor - 1, 0, instCount() - 1);
      padGoto(1);
    } else if (key == 5) {
      cursor = clampi(cursor + 1, 0, instCount() - 1);
      padGoto(1);
    } else if (key == 6) {
      padGoto(1);
      assignInstrument();
    } else if (key == 7) {
      instrumentBank.Scan();
      drumKit.Scan();
      loopLibrary.Scan();
      if (loopsBlocked) {
        toastSet("MIDI mode: loops off");
      } else {
        loopLibrary.PreloadInstrument(nullptr, 0);
        toastSet(sdCard.Mounted() ? "Rescanned" : "No SD card");
      }
    } else if (key == 8) {
      cycleKit(-1);
    } else if (key == 9) {
      cycleKit(1);
    } else if (key == 10) {
      audioCommand('*', 3);
    } else if (key == 11) {
      padGoto(1);
    } else if (key == 12) {
      audioCommand('I', kLoopsVoice);
    }
    return;
  }
  page = 0;
  rollView = true;
  if (key == 4) {
    rollFollow = false;
    rollStep = clampi(rollStep - 1, 0, rollPatLen() - 1);
    clampRoll();
  } else if (key == 5) {
    rollFollow = false;
    rollStep = clampi(rollStep + 1, 0, rollPatLen() - 1);
    clampRoll();
  } else if (key == 6) {
    rollFollow = false;
    rollPitch = clampi(rollPitch + 1, 0, kRollRows - 1);
  } else if (key == 7) {
    rollFollow = false;
    rollPitch = clampi(rollPitch - 1, 0, kRollRows - 1);
  } else if (key == 8) {
    padDeleteRoll();
  } else if (key == 9) {
    padHoldRoll();
  } else if (key == 10) {
    padNudgeOct(-1);
  } else if (key == 11) {
    padNudgeOct(1);
  } else if (key == 12) {
    audioCommand('W', -1);
  } else if (key == 13) {
    audioCommand('W', 1);
  } else if (key == 14) {
    padDupStep();
  } else if (key == 15) {
    rollView = !rollView;
    toastSet(rollView ? "Piano roll" : "Step view");
  }
}

static void padFxKey(int key) {
  if (key == 0) {
    padSub = 0;
    padGoto(kFxPage);
    padLegendSet("FX  F2 close  F3 mixer");
    return;
  }
  if (key == 2) {
    padSub = 1;
    padGoto(kMixerPage);
    padLegendSet("Mix  F2 close  F1 fx");
    return;
  }
  if (padSub == 1) {
    audioReadSnap(&snap);
    if (key == 4) {
      audioCommand('v', clampi((int)snap.vol[snap.track] + 1, 0, 8));
    } else if (key == 5) {
      audioCommand('v', clampi((int)snap.vol[snap.track] - 1, 0, 8));
    } else if (key == 6) {
      audioCommand('V', 0);
    } else if (key == 7) {
      audioCommand('V', 3);
    } else if (key == 8) {
      audioCommand('T', (snap.track + 3) & 3);
    } else if (key == 9) {
      audioCommand('T', (snap.track + 1) & 3);
    }
    padGoto(kMixerPage);
    return;
  }
  padGoto(kFxPage);
  if (key == 4) {
    mixRow = clampi(mixRow - 1, 0, kFxRows - 1);
  } else if (key == 5) {
    mixRow = clampi(mixRow + 1, 0, kFxRows - 1);
  } else if (key == 6) {
    tweakFx(-1);
  } else if (key == 7) {
    tweakFx(1);
  } else if (key == 8) {
    audioReadSnap(&snap);
    audioCommand('T', (snap.track + 3) & 3);
  } else if (key == 9) {
    audioReadSnap(&snap);
    audioCommand('T', (snap.track + 1) & 3);
  }
}

static void padLoopKey(int key) {
  padGoto(kLoopsPage);
  int libs = loopLibrary.Count();
  int rows = 0;
  if (libs > 0) {
    rows = loopLibrary.At(loopLib).entryCount - 1;
  }
  if (rows < 0) {
    rows = 0;
  }
  if (key == 4) {
    loopRow = clampi(loopRow - 1, 0, rows);
  } else if (key == 5) {
    loopRow = clampi(loopRow + 1, 0, rows);
  } else if (key == 6 && libs > 0) {
    loopLib = (loopLib + libs - 1) % libs;
    loopRow = 0;
  } else if (key == 7 && libs > 0) {
    loopLib = (loopLib + 1) % libs;
    loopRow = 0;
  } else if (key == 8) {
    launchLoop(true);
  } else if (key == 9) {
    launchLoop(false);
  } else if (key == 10) {
    audioReadSnap(&snap);
    audioStopLoop(snap.track);
    toastSet("Loop stop");
  } else if (key == 11) {
    quantize = (quantize + 1) % 3;
    toastSet(quantize == 0 ? "Quantize now" : (quantize == 1 ? "Quantize beat" : "Quantize bar"));
  } else if (key == 12) {
    loopLibrary.Scan();
    toastSet("Rescanned loops");
  }
}

static void padSlot(int slot) {
  songSlot = slot;
  padGoto(kSongPage);
  bool has = false;
  storage.Status(songSlot, &has);
  toastSet(has ? "Slot full" : "Slot empty");
}

static void padFileKey(int key) {
  padGoto(kSongPage);
  audioReadSnap(&snap);
  int bars = snap.barCount ? (int)snap.barCount : 1;
  if (key == 4) {
    doSave();
  } else if (key == 5) {
    doLoad();
  } else if (key == 6) {
    toastSet(SdStorage::ResultText(storage.Delete(songSlot), songSlot));
  } else if (key == 7) {
    bool has = false;
    SdResult st = storage.Status(songSlot, &has);
    if (st == SD_OK) {
      toastSet(has ? "Slot full" : "Slot empty");
    } else {
      toastSet(SdStorage::ResultText(st, songSlot));
    }
  } else if (key >= 8 && key <= 11) {
    padSlot(key - 8);
  } else if (key == 12) {
    bars = bars >= 8 ? 1 : bars + 1;
    audioCommand('X', bars - 1);
    toastSet("New song");
  } else if (key == 13) {
    audioCommand('X', bars - 1);
    toastSet("New song");
  } else if (key == 14) {
    audioCommand('*', 0);
  } else if (key == 15) {
    audioCommand('*', 1);
  }
}

static void padNameKey(int key, BleMidi &ble) {
  if (key == 0) {
    padNaming = false;
    naming = false;
    toastSet("Name kept");
    padLegendSet("Settings  F4 close");
    return;
  }
  if (key == 1) {
    padGlyph = (padGlyph + 1) % 4;
    toastSet(kPadGlyphs[padGlyph]);
    return;
  }
  if (key == 2) {
    int n = (int)strlen(edit);
    if (n > 0) {
      edit[n - 1] = 0;
    }
    return;
  }
  if (key == 3) {
    if (edit[0]) {
      snprintf(bleName, sizeof(bleName), "%s", edit);
      savePrefs();
      ble.Restart(bleName);
      toastSet("BLE name set");
    }
    padNaming = false;
    naming = false;
    padLegendSet("Settings  F4 close");
    return;
  }
  int index = key - 4;
  const char *pageChars = kPadGlyphs[padGlyph];
  int nchars = (int)strlen(pageChars);
  if (index < 0 || index >= nchars) {
    return;
  }
  int n = (int)strlen(edit);
  if (n < 16) {
    edit[n] = pageChars[index];
    edit[n + 1] = 0;
  }
}

static void padSettingsKey(int key, BleMidi &ble) {
  padGoto(kSettingsPage);
  audioReadSnap(&snap);
  int bars = snap.barCount ? (int)snap.barCount : 1;
  if (key == 4) {
    cursor = 0;
    outVol = (uint8_t)clampi((int)outVol - 8, 0, 255);
    audioSetSpeakerVolume(outVol);
    savePrefs();
  } else if (key == 5) {
    cursor = 0;
    outVol = (uint8_t)clampi((int)outVol + 8, 0, 255);
    audioSetSpeakerVolume(outVol);
    savePrefs();
  } else if (key == 6) {
    cursor = 1;
    bright = (uint8_t)clampi((int)bright - 8, 10, 255);
    boardSetBrightness(bright);
    savePrefs();
  } else if (key == 7) {
    cursor = 1;
    bright = (uint8_t)clampi((int)bright + 8, 10, 255);
    boardSetBrightness(bright);
    savePrefs();
  } else if (key == 8) {
    cursor = 3;
    finishBleOff(ble);
  } else if (key == 9) {
    cursor = 3;
    requestBleOn(ble);
  } else if (key == 10) {
    cursor = 4;
    if (ble.Resident()) {
      doUnloadBle(ble);
    } else {
      doLoadBle(ble);
    }
  } else if (key == 11) {
    cursor = 4;
    if (bleUnloadPending) {
      doLoadBle(ble);
    }
  } else if (key == 12) {
    cursor = 8;
    audioCommand('Y', clampi(bars - 1, 1, 8));
  } else if (key == 13) {
    cursor = 8;
    audioCommand('Y', clampi(bars + 1, 1, 8));
  } else if (key == 14) {
    audioCommand('R', 0);
  } else if (key == 15) {
    cursor = 2;
    naming = true;
    padNaming = true;
    padGlyph = 0;
    snprintf(edit, sizeof(edit), "%s", bleName);
    padLegendSet("Name  F1 cancel  F4 apply");
    toastSet(kPadGlyphs[0]);
  }
}

static void padPagesKey(int key) {
  if (key >= 4 && key <= 11) {
    padGoto(key - 4);
    return;
  }
  if (key == 12) {
    padGoto(kExitPage);
    return;
  }
  if (key == 13) {
    if (launcherOk) {
      requestExit();
    } else {
      toastSet("Launcher not found");
    }
    return;
  }
  if (key == 14) {
    page = stepPage(page, -1);
  } else if (key == 15) {
    page = stepPage(page, 1);
  }
}

static void padSongKey(int key, BleMidi &ble) {
  if (key == 0) {
    padSub = 0;
    padLegendSet("Pages  F4 close  F2 file");
    return;
  }
  if (key == 1) {
    padSub = 1;
    padGoto(kSongPage);
    padLegendSet("File  F4 close  F1 pages");
    return;
  }
  if (key == 2) {
    padSub = 2;
    padGoto(kSettingsPage);
    padLegendSet("Settings  F4 close  F1 pages");
    return;
  }
  if (padSub == 1) {
    padFileKey(key);
  } else if (padSub == 2) {
    padSettingsKey(key, ble);
  } else {
    padPagesKey(key);
  }
}

static void handlePad(int key, bool hold, BleMidi &ble) {
  if (key < 0 || key > 15) {
    return;
  }
  if (bleAsk) {
    if (key == 0) {
      answerBleAsk(0, true, ble);
    } else if (key == 14) {
      answerBleAsk('y', false, ble);
    } else if (key == 15) {
      answerBleAsk('n', false, ble);
    }
    return;
  }
  if (padNaming) {
    padNameKey(key, ble);
    return;
  }
  if (hold && key <= 3) {
    padOpen(key);
    return;
  }
  if (padMenu >= 0) {
    if (key == padMenu && !(padMenu == 0 && padEntry)) {
      padLive();
      toastSet("Play");
      return;
    }
    if (padMenu == 0) {
      padEditKey(key);
    } else if (padMenu == 1) {
      padFxKey(key);
    } else if (padMenu == 2) {
      padLoopKey(key);
    } else {
      padSongKey(key, ble);
    }
    return;
  }
  TtgoPadAct act = ttgoBaseAct(padLatch, key);
  padLatch = act.latch;
  if (act.cmd == 0) {
    if (padLatch == 0) {
      padLegendSet("F1 inst  hold = edit");
    } else if (padLatch == 1) {
      padLegendSet("F2 fx  hold = fx");
    } else if (padLatch == 2) {
      padLegendSet("F3 track  hold = loops");
    } else if (padLatch == 3) {
      padLegendSet("F4 song  hold = pages");
    }
    return;
  }
  padLegendSet(0);
  audioCommand(act.cmd, act.val);
}

#endif

void uiPoll(BleMidi &ble) {
  loopsBlocked = ble.UserEnabled() && !MOTHDECK_LOOPS_WITH_BLE;
  bool esc = boardEscHeld();
  bool btn = boardBtnHeld();
  if (esc || btn) {
    if (!escDown) {
      escSince = millis();
      escDown = true;
      escFired = false;
    } else if (!escFired && (uint32_t)(millis() - escSince) > 700) {
      escFired = true;
      if (launcherInstalled()) {
        launcherOk = true;
        requestExit();
      } else {
        launcherOk = false;
        if (page == kExitPage) {
          page = 0;
        }
        toastSet("Launcher not found");
      }
    }
  } else {
    escDown = false;
    escFired = false;
  }
  DeckKeys st;
  if (boardPollKeys(&st)) {
#if MOTHDECK_BOARD_TTGO
    if (st.padKey >= 0) {
      handlePad(st.padKey, st.padHold, ble);
      return;
    }
#endif
    KeyEvent ev = eventFrom(st);
    if (bleAsk) {
      if (st.del || ev.ch == '`') {
        answerBleAsk(0, true, ble);
        return;
      }
      if (ev.ch == 'y' || ev.ch == 'n') {
        answerBleAsk(ev.ch, false, ble);
      }
      return;
    }
    ModalKind kind = activeModal(overlay, naming, page == kExitPage);
    if (kind != MODAL_NONE) {
      ModalAction action;
      dispatchModal(kind, ev, &action);
      applyModal(action, ev.ch, ble);
      return;
    }
    if (ev.ch == '`' && !ev.fn) {
      if (page == 0) {
        overlay = !overlay;
      } else {
        page = 0;
        overlay = false;
      }
      return;
    }
    handleState(st, ble);
  }
}
