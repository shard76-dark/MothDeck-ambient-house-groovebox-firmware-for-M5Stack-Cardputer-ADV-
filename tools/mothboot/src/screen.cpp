#include "screen.h"

#include <M5Cardputer.h>
#include <string.h>

#include "version.h"

static const uint16_t kBg = 0x1082;
static const uint16_t kAmber = 0xFD20;
static const uint16_t kInk = 0xFFFF;
static const uint16_t kMuted = 0x9CD3;

struct Line {
  int x;
  int y;
  int w;
  int h;
  uint16_t color;
  uint8_t size;
  char text[64];
  bool drawn;
};

struct Bar {
  int x;
  int y;
  int w;
  int h;
  int percent;
  bool track;
};

static Line gPhase;
static Line gDetail;
static Line gHint;
static Line gPct;
static Bar gBar;
static bool gReady = false;

static void remember(Line *line, const char *text) {
  if (!text) {
    text = "";
  }
  strncpy(line->text, text, sizeof(line->text) - 1);
  line->text[sizeof(line->text) - 1] = 0;
  line->drawn = true;
}

static void drawLine(Line *line, const char *text, uint16_t color, uint8_t size) {
  if (!text) {
    text = "";
  }
  if (line->drawn && line->color == color && line->size == size && strcmp(line->text, text) == 0) {
    return;
  }
  auto &d = M5Cardputer.Display;
  d.fillRect(line->x, line->y, line->w, line->h, kBg);
  d.setTextSize(size);
  d.setTextColor(color);
  d.setCursor(line->x, line->y + 1);
  d.print(text);
  line->color = color;
  line->size = size;
  remember(line, text);
}

static void drawBar(int percent) {
  if (percent < 0) {
    if (gBar.track) {
      M5Cardputer.Display.fillRect(gBar.x, gBar.y, gBar.w, gBar.h, kBg);
      gBar.track = false;
      gBar.percent = -1;
    }
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  auto &d = M5Cardputer.Display;
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

void bootScreenBegin() {
  auto cfg = M5.config();
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_spk = false;
  cfg.external_display_value = 0;
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  cfg.clear_display = true;
  M5Cardputer.begin(cfg, true);
  if (M5Cardputer.Display.width() < 200 || M5Cardputer.Display.height() < 120) {
    pinMode(38, OUTPUT);
    digitalWrite(38, HIGH);
  }
  M5Cardputer.Display.setBrightness(180);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.fillScreen(kBg);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(kAmber);
  M5Cardputer.Display.setCursor(8, 6);
  M5Cardputer.Display.print("MothBoot");
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(kMuted);
  M5Cardputer.Display.setCursor(176, 12);
  M5Cardputer.Display.print(kMothBootVersion);

  gPhase = {8, 32, 224, 16, kInk, 1, "", false};
  gDetail = {8, 50, 224, 16, kInk, 1, "", false};
  gBar = {8, 72, 224, 14, -1, false};
  gPct = {8, 90, 224, 14, kAmber, 1, "", false};
  gHint = {8, 110, 224, 16, kMuted, 1, "", false};
  gReady = true;
  bootScreenShow("USB or keys", "1 A   2 B   S apps", "C check needs USB", -1);
}

void bootScreenShow(const char *phase, const char *detail, const char *hint, int percent) {
  if (!gReady) {
    return;
  }
  drawLine(&gPhase, phase, kInk, 1);
  drawLine(&gDetail, detail, kInk, 1);
  drawBar(percent);
  char pct[16];
  if (percent < 0) {
    pct[0] = 0;
  } else {
    snprintf(pct, sizeof(pct), "%d%%", percent);
  }
  drawLine(&gPct, pct, kAmber, 1);
  drawLine(&gHint, hint, kMuted, 1);
}
