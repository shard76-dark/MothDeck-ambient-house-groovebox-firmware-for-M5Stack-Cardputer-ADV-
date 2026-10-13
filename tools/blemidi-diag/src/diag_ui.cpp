#include "diag_ui.h"

#include "BoardConfig.h"
#include "diag_ble.h"

#include <M5Cardputer.h>
#include <string.h>

static const uint16_t kBg = 0x1082;
static const uint16_t kAmber = 0xFD20;
static const uint16_t kInk = 0xFFFF;
static const uint16_t kMuted = 0x9CD3;

struct Line {
  int x, y, w, h;
  uint16_t color;
  uint8_t size;
  char text[48];
  bool drawn;
};

struct Bar {
  int x, y, w, h;
  int percent;
  bool track;
};

static Line gLink;
static Line gTest;
static Line gHint;
static Line gMidi;
static Line gArrival;
static Line gRates;
static Bar gBar;
static bool gReady = false;

static void drawLine(Line *line, const char *text, uint16_t color) {
  if (!text) {
    text = "";
  }
  if (line->drawn && line->color == color && strcmp(line->text, text) == 0) {
    return;
  }
  auto &d = M5Cardputer.Display;
  d.fillRect(line->x, line->y, line->w, line->h, kBg);
  d.setTextSize(line->size ? line->size : 1);
  d.setTextColor(color);
  d.setCursor(line->x, line->y + 1);
  d.print(text);
  line->color = color;
  strncpy(line->text, text, sizeof(line->text) - 1);
  line->text[sizeof(line->text) - 1] = 0;
  line->drawn = true;
}

static void drawBar(int percent) {
  auto &d = M5Cardputer.Display;
  if (percent < 0) {
    if (gBar.track) {
      d.fillRect(gBar.x, gBar.y, gBar.w, gBar.h, kBg);
      gBar.track = false;
      gBar.percent = -1;
    }
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  if (!gBar.track) {
    d.drawRect(gBar.x, gBar.y, gBar.w, gBar.h, kMuted);
    gBar.track = true;
    gBar.percent = 0;
  }
  if (percent == gBar.percent) {
    return;
  }
  int inner = gBar.w - 4;
  int prev = inner * (gBar.percent < 0 ? 0 : gBar.percent) / 100;
  int next = inner * percent / 100;
  if (next >= prev) {
    if (next > prev) {
      d.fillRect(gBar.x + 2 + prev, gBar.y + 2, next - prev, gBar.h - 4, kAmber);
    }
  } else {
    d.fillRect(gBar.x + 2, gBar.y + 2, inner, gBar.h - 4, kBg);
    if (next > 0) {
      d.fillRect(gBar.x + 2, gBar.y + 2, next, gBar.h - 4, kAmber);
    }
  }
  gBar.percent = percent;
}

void diagUiBegin() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_spk = false;
  cfg.external_display_value = 0;
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  cfg.clear_display = true;
  M5Cardputer.begin(cfg, true);
  pinMode(PIN_I2S_DIN, INPUT);
  if (M5Cardputer.Display.width() < 200 || M5Cardputer.Display.height() < 120) {
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);
  }
  M5Cardputer.Display.setBrightness(180);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.fillScreen(kBg);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(kAmber);
  M5Cardputer.Display.setCursor(8, 4);
  M5Cardputer.Display.print("MothDiag");
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(kMuted);
  M5Cardputer.Display.setCursor(168, 10);
  M5Cardputer.Display.print(kMothDiagVersion);
  gLink = {4, 22, 232, 12, kInk, 1, "", false};
  gTest = {4, 34, 232, 12, kInk, 1, "", false};
  gBar = {4, 48, 232, 10, -1, false};
  gMidi = {4, 60, 232, 18, kAmber, 2, "", false};
  gArrival = {4, 78, 232, 18, kAmber, 2, "", false};
  gRates = {4, 98, 232, 12, kInk, 1, "", false};
  gHint = {4, 112, 232, 22, kMuted, 1, "", false};
  gReady = true;
  diagUiLink(false);
  diagUiRates("--", "--", "--", "--");
  diagUiTest("Idle", "Press Play on MPC", -1);
}

void diagUiLink(bool connected) {
  if (!gReady) {
    return;
  }
  drawLine(&gLink, connected ? "Connected" : "Waiting", connected ? kAmber : kMuted);
}

void diagUiRates(const char *midiBpm, const char *arrivalBpm, const char *clockRate, const char *mtcRate) {
  if (!gReady) {
    return;
  }
  char midiLine[32];
  char arrivalLine[32];
  char rateLine[40];
  snprintf(midiLine, sizeof(midiLine), "MIDI %s", midiBpm && midiBpm[0] ? midiBpm : "--");
  snprintf(arrivalLine, sizeof(arrivalLine), "Arr  %s", arrivalBpm && arrivalBpm[0] ? arrivalBpm : "--");
  snprintf(rateLine, sizeof(rateLine), "Clk %s  MTC %s", clockRate && clockRate[0] ? clockRate : "--", mtcRate && mtcRate[0] ? mtcRate : "--");
  drawLine(&gMidi, midiLine, kAmber);
  drawLine(&gArrival, arrivalLine, kAmber);
  drawLine(&gRates, rateLine, kInk);
}

void diagUiTest(const char *testName, const char *hint, int percent) {
  if (!gReady) {
    return;
  }
  drawLine(&gTest, testName && testName[0] ? testName : "Idle", kInk);
  drawBar(percent);
  drawLine(&gHint, hint ? hint : "", kInk);
}
