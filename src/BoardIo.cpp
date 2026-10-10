#include "BoardIo.h"
#include "BoardConfig.h"
#include <Arduino.h>
#include <string.h>

#if !MOTHDECK_BOARD_TTGO

#include <M5Cardputer.h>

void boardUpdate() {
  M5Cardputer.update();
}

bool boardPollKeys(DeckKeys *keys) {
  if (!keys) {
    return false;
  }
  auto &kb = M5Cardputer.Keyboard;
  if (!kb.isChange() || !kb.isPressed()) {
    return false;
  }
  Keyboard_Class::KeysState st = kb.keysState();
  memset(keys, 0, sizeof(*keys));
  keys->tab = st.tab;
  keys->fn = st.fn;
  keys->shift = st.shift;
  keys->ctrl = st.ctrl;
  keys->opt = st.opt;
  keys->alt = st.alt;
  keys->del = st.del;
  keys->enter = st.enter;
  keys->space = st.space;
  for (char c : st.word) {
    if (keys->wordLen >= (int)sizeof(keys->word)) {
      break;
    }
    keys->word[keys->wordLen++] = c;
  }
  return true;
}

bool boardEscHeld() {
  return M5Cardputer.Keyboard.isKeyPressed('`');
}

bool boardBtnHeld() {
  return M5Cardputer.BtnA.isPressed();
}

void boardPushBegin() {
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.startWrite();
}

void boardPushEnd() {
  M5Cardputer.Display.endWrite();
  M5Cardputer.Display.setSwapBytes(false);
}

void boardPushImage(int x, int y, int w, int h, const uint16_t *pixels) {
  M5Cardputer.Display.pushImage(x, y, w, h, pixels);
}

void boardSetBrightness(uint8_t level) {
  M5Cardputer.Display.setBrightness(level);
}

int boardBatteryPct() {
  return M5.Power.getBatteryLevel();
}

int boardBatteryMv() {
  return M5.Power.getBatteryVoltage();
}

bool boardHasDisplay() {
  return true;
}

void boardBringUpTtgo() {}

#else

static bool fnNext = false;
static uint8_t mxLast = 0;

static const char kMxKey[8] = {'z', 'x', 'c', 'v', ' ', '1', 'p', '/'};

static bool matrixOn() {
  return PIN_MX_R0 >= 0 && PIN_MX_R1 >= 0 && PIN_MX_C0 >= 0 && PIN_MX_C1 >= 0 && PIN_MX_C2 >= 0 && PIN_MX_C3 >= 0;
}

static void matrixBegin() {
  if (!matrixOn()) {
    return;
  }
  const int rows[2] = {PIN_MX_R0, PIN_MX_R1};
  const int cols[4] = {PIN_MX_C0, PIN_MX_C1, PIN_MX_C2, PIN_MX_C3};
  for (int i = 0; i < 2; i++) {
    pinMode(rows[i], OUTPUT);
    digitalWrite(rows[i], HIGH);
  }
  for (int i = 0; i < 4; i++) {
    pinMode(cols[i], INPUT_PULLUP);
  }
}

static int matrixScan(char *out) {
  if (!matrixOn() || !out) {
    return 0;
  }
  const int rows[2] = {PIN_MX_R0, PIN_MX_R1};
  const int cols[4] = {PIN_MX_C0, PIN_MX_C1, PIN_MX_C2, PIN_MX_C3};
  uint8_t now = 0;
  for (int r = 0; r < 2; r++) {
    digitalWrite(rows[r], LOW);
    delayMicroseconds(20);
    for (int c = 0; c < 4; c++) {
      if (digitalRead(cols[c]) == LOW) {
        now |= (uint8_t)(1u << (r * 4 + c));
      }
    }
    digitalWrite(rows[r], HIGH);
  }
  uint8_t pressed = (uint8_t)(now & ~mxLast);
  mxLast = now;
  for (int i = 0; i < 8; i++) {
    if (pressed & (1u << i)) {
      *out = kMxKey[i];
      return 1;
    }
  }
  return 0;
}

static bool fillFromByte(int b, DeckKeys *keys) {
  if (b < 0 || !keys) {
    return false;
  }
  if (b == 0x1B) {
    fnNext = true;
    return false;
  }
  memset(keys, 0, sizeof(*keys));
  if (fnNext) {
    keys->fn = true;
    fnNext = false;
  }
  if (b == '\r' || b == '\n') {
    keys->enter = true;
    return true;
  }
  if (b == '\t') {
    keys->tab = true;
    return true;
  }
  if (b == '\b' || b == 0x7F) {
    keys->del = true;
    return true;
  }
  if (b == ' ') {
    keys->space = true;
    return true;
  }
  if (b >= 32 && b < 127) {
    keys->word[0] = (char)b;
    keys->wordLen = 1;
    return true;
  }
  return false;
}

void boardUpdate() {}

bool boardPollKeys(DeckKeys *keys) {
  char mx = 0;
  if (matrixScan(&mx) && fillFromByte((unsigned char)mx, keys)) {
    return true;
  }
  while (Serial.available() > 0) {
    int b = Serial.read();
    if (fillFromByte(b, keys)) {
      return true;
    }
  }
  return false;
}

bool boardEscHeld() {
  return false;
}

bool boardBtnHeld() {
  if (PIN_BTN_A < 0) {
    return false;
  }
  return digitalRead(PIN_BTN_A) == LOW;
}

#if MOTHDECK_HAS_TFT

#include <M5GFX.h>

class TtgoPanel : public M5GFX {
public:
  TtgoPanel() {
    auto bus = new Bus_SPI();
    auto bcfg = bus->config();
    bcfg.freq_write = 40000000;
    bcfg.pin_sclk = PIN_TFT_SCK;
    bcfg.pin_mosi = PIN_TFT_MOSI;
    bcfg.pin_miso = -1;
    bcfg.pin_dc = PIN_TFT_DC;
    bcfg.spi_3wire = true;
#if defined(VSPI_HOST)
    bcfg.spi_host = VSPI_HOST;
#else
    bcfg.spi_host = SPI3_HOST;
#endif
    bus->config(bcfg);
    auto panel = new Panel_ST7789();
    auto pcfg = panel->config();
    pcfg.pin_cs = PIN_TFT_CS;
    pcfg.pin_rst = PIN_TFT_RST;
    pcfg.memory_width = 240;
    pcfg.memory_height = 320;
    pcfg.panel_width = 135;
    pcfg.panel_height = 240;
    pcfg.offset_x = MOTHDECK_TFT_OFFSET_X;
    pcfg.offset_y = MOTHDECK_TFT_OFFSET_Y;
    panel->config(pcfg);
    panel->setBus(bus);
    setPanel(panel);
  }
};

static TtgoPanel *panel = nullptr;
static bool tftOk = false;

void boardBringUpTtgo() {
  if (PIN_TFT_BL >= 0) {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
  }
  panel = new TtgoPanel();
  tftOk = panel->init();
  if (tftOk) {
    panel->setRotation(MOTHDECK_TFT_ROTATION);
    panel->setBrightness(200);
    panel->fillScreen(0x1082);
    Serial.println("UI: ST7789 up");
  } else {
    Serial.println("UI: ST7789 did not start");
  }
  matrixBegin();
  if (PIN_BTN_A >= 0) {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
  }
}

void boardPushBegin() {
  if (panel && tftOk) {
    panel->setSwapBytes(true);
    panel->startWrite();
  }
}

void boardPushEnd() {
  if (panel && tftOk) {
    panel->endWrite();
    panel->setSwapBytes(false);
  }
}

void boardPushImage(int x, int y, int w, int h, const uint16_t *pixels) {
  if (panel && tftOk && pixels) {
    panel->pushImage(x, y, w, h, pixels);
  }
}

void boardSetBrightness(uint8_t level) {
  if (panel && tftOk) {
    panel->setBrightness(level);
  }
  if (PIN_TFT_BL >= 0) {
    digitalWrite(PIN_TFT_BL, level > 8 ? HIGH : LOW);
  }
}

bool boardHasDisplay() {
  return tftOk;
}

#else

void boardBringUpTtgo() {
  matrixBegin();
  if (PIN_BTN_A >= 0) {
    pinMode(PIN_BTN_A, INPUT_PULLUP);
  }
  Serial.println("UI: headless, serial keys and BLE MIDI");
}

void boardPushBegin() {}
void boardPushEnd() {}
void boardPushImage(int x, int y, int w, int h, const uint16_t *pixels) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)pixels;
}
void boardSetBrightness(uint8_t level) {
  (void)level;
}
bool boardHasDisplay() {
  return false;
}

#endif

int boardBatteryMv() {
  if (PIN_BATT_ADC < 0) {
    return 0;
  }
  // T8 divider is about 2:1 into a 3.3V ADC. 12-bit.
  int raw = analogRead(PIN_BATT_ADC);
  return (raw * 3300 * 2) / 4095;
}

int boardBatteryPct() {
  int mv = boardBatteryMv();
  if (mv < 3300) {
    return -1;
  }
  int pct = (mv - 3300) * 100 / (4200 - 3300);
  if (pct < 0) {
    return 0;
  }
  if (pct > 100) {
    return 100;
  }
  return pct;
}

#endif
