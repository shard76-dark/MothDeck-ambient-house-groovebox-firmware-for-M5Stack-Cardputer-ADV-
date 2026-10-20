#include "BoardConfig.h"

#if !MOTHDECK_BOARD_HANDHELD

void handheldBringUp() {}

#else

#include "HandheldIo.h"
#include "KeypadMap.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>

static bool oledOk = false;
static bool segOk = false;
static bool padOk = false;
static uint8_t fb[1024];

static const int kPadDebounceMs = 20;
static const int kPadHoldMs = 450;
static int padRows[4];
static int padCols[4];
static uint16_t padDown = 0;
static uint16_t padCandidate = 0;
static uint32_t padCandidateMs = 0;
static int holdKey = -1;
static uint32_t holdSince = 0;
static bool holdSent = false;

static const uint8_t kSegDigit[11] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x00,
};

static bool i2cWrite(uint8_t addr, const uint8_t *data, size_t len) {
  Wire.beginTransmission(addr);
  for (size_t i = 0; i < len; i++) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

static bool i2cCmd(uint8_t addr, uint8_t cmd) {
  return i2cWrite(addr, &cmd, 1);
}

static bool oledCmd(uint8_t cmd) {
  uint8_t buf[2] = {0x00, cmd};
  return i2cWrite(MOTHDECK_OLED_ADDR, buf, 2);
}

static void oledInit() {
  static const uint8_t seq[] = {
      0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
      0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
  };
  oledOk = true;
  for (size_t i = 0; i < sizeof(seq); i++) {
    if (!oledCmd(seq[i])) {
      oledOk = false;
      return;
    }
  }
  memset(fb, 0, sizeof(fb));
}

static void segInit() {
  segOk = i2cCmd(MOTHDECK_HT16K33_ADDR, 0x21) && i2cCmd(MOTHDECK_HT16K33_ADDR, 0x81) &&
          i2cCmd(MOTHDECK_HT16K33_ADDR, 0xE4);
  if (!segOk) {
    return;
  }
  int blank[4] = {10, 10, 10, 10};
  int dots[4] = {0, 0, 0, 0};
  handheldShowDigits(blank, dots);
}

static void colMode(int pin) {
  if (pin >= 34) {
    pinMode(pin, INPUT);
  } else {
    pinMode(pin, INPUT_PULLUP);
  }
}

static uint16_t readMask() {
  uint16_t now = 0;
  for (int r = 0; r < 4; r++) {
    digitalWrite(padRows[r], LOW);
    delayMicroseconds(40);
    for (int c = 0; c < 4; c++) {
      if (digitalRead(padCols[c]) == LOW) {
        int key = handheldPadKey(r, c);
        if (key >= 0) {
          now |= (uint16_t)(1u << key);
        }
      }
    }
    digitalWrite(padRows[r], HIGH);
  }
  return now;
}

static void padInit() {
  padOk = false;
  const int rows[4] = {PIN_MX_R0, PIN_MX_R1, PIN_MX_R2, PIN_MX_R3};
  const int cols[4] = {PIN_MX_C0, PIN_MX_C1, PIN_MX_C2, PIN_MX_C3};
  for (int i = 0; i < 4; i++) {
    if (rows[i] < 0 || cols[i] < 0) {
      Serial.println("UI: keypad off");
      return;
    }
    padRows[i] = rows[i];
    padCols[i] = cols[i];
  }
  for (int i = 0; i < 4; i++) {
    pinMode(padRows[i], OUTPUT);
    digitalWrite(padRows[i], HIGH);
    colMode(padCols[i]);
  }
  delayMicroseconds(80);
  if (readMask() != 0) {
    for (int i = 0; i < 4; i++) {
      pinMode(padRows[i], INPUT);
    }
    Serial.println("UI: keypad stuck");
    return;
  }
  padOk = true;
  padDown = 0;
  padCandidate = 0;
  holdKey = -1;
  holdSent = false;
  Serial.println("UI: keypad");
}

static void ledInit() {
  const int pins[4] = {PIN_LED_STEP, PIN_LED_BEAT, PIN_LED_PLAY, PIN_LED_REC};
  for (int i = 0; i < 4; i++) {
    if (pins[i] < 0) {
      continue;
    }
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], LOW);
  }
}

void handheldBringUp() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  oledInit();
  segInit();
  ledInit();
  padInit();
  if (PIN_BATT_ADC >= 0) {
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
  }
  Serial.printf("UI: oled %s  seg %s\n", oledOk ? "ok" : "absent", segOk ? "ok" : "absent");
}

bool handheldOledOk() { return oledOk; }
bool handheldSegOk() { return segOk; }
bool handheldPadOk() { return padOk; }

bool handheldPollKey(int *key, bool *hold) {
  if (!padOk || !key) {
    return false;
  }
  uint16_t raw = readMask();
  uint32_t ms = millis();
  if (raw != padCandidate) {
    padCandidate = raw;
    padCandidateMs = ms;
    return false;
  }
  if ((uint32_t)(ms - padCandidateMs) < (uint32_t)kPadDebounceMs) {
    return false;
  }
  uint16_t rose = (uint16_t)(raw & ~padDown);
  uint16_t fell = (uint16_t)(padDown & ~raw);
  padDown = raw;
  if (fell) {
    holdKey = -1;
    holdSent = false;
  }
  if (rose && (rose & (rose - 1)) == 0) {
    int found = 0;
    while ((rose & 1) == 0) {
      rose >>= 1;
      found++;
    }
    holdKey = found;
    holdSince = ms;
    holdSent = false;
    if (key) {
      *key = found;
    }
    if (hold) {
      *hold = false;
    }
    return true;
  }
  if (holdKey >= 0 && raw == (uint16_t)(1u << holdKey) && !holdSent &&
      (uint32_t)(ms - holdSince) >= (uint32_t)kPadHoldMs) {
    holdSent = true;
    if (key) {
      *key = holdKey;
    }
    if (hold) {
      *hold = true;
    }
    return true;
  }
  return false;
}

void handheldLeds(bool step, bool beat, bool play, bool rec) {
  const int pins[4] = {PIN_LED_STEP, PIN_LED_BEAT, PIN_LED_PLAY, PIN_LED_REC};
  const bool on[4] = {step, beat, play, rec};
  for (int i = 0; i < 4; i++) {
    if (pins[i] < 0) {
      continue;
    }
    digitalWrite(pins[i], on[i] ? HIGH : LOW);
  }
}

void handheldShowDigits(const int digit[4], const int dot[4]) {
  if (!segOk || !digit) {
    return;
  }
  uint16_t buf[8];
  memset(buf, 0, sizeof(buf));
  const int pos[4] = {0, 1, 3, 4};
  for (int i = 0; i < 4; i++) {
    int d = digit[i];
    if (d < 0 || d > 10) {
      d = 10;
    }
    uint16_t bits = kSegDigit[d];
    if (dot && dot[i]) {
      bits |= 0x80;
    }
    buf[pos[i]] = bits;
  }
  uint8_t raw[17];
  raw[0] = 0x00;
  for (int i = 0; i < 8; i++) {
    raw[1 + i * 2] = (uint8_t)(buf[i] & 0xFF);
    raw[2 + i * 2] = (uint8_t)(buf[i] >> 8);
  }
  i2cWrite(MOTHDECK_HT16K33_ADDR, raw, sizeof(raw));
}

void handheldOledClear() {
  memset(fb, 0, sizeof(fb));
}

static void plot(int x, int y, bool on) {
  if (x < 0 || x > 127 || y < 0 || y > 63) {
    return;
  }
  int i = x + (y >> 3) * 128;
  uint8_t bit = (uint8_t)(1u << (y & 7));
  if (on) {
    fb[i] |= bit;
  } else {
    fb[i] &= (uint8_t)~bit;
  }
}

void handheldOledFill(int x, int y, int w, int h, bool on) {
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      plot(x + xx, y + yy, on);
    }
  }
}

void handheldOledGlyph(int x, int y, const uint8_t col[5]) {
  for (int c = 0; c < 5; c++) {
    for (int r = 0; r < 7; r++) {
      if (col[c] & (1u << r)) {
        plot(x + c, y + r, true);
      }
    }
  }
}

void handheldOledFlush() {
  if (!oledOk) {
    return;
  }
  oledCmd(0x21);
  oledCmd(0);
  oledCmd(127);
  oledCmd(0x22);
  oledCmd(0);
  oledCmd(7);
  for (int off = 0; off < 1024; off += 16) {
    Wire.beginTransmission(MOTHDECK_OLED_ADDR);
    Wire.write(0x40);
    for (int i = 0; i < 16; i++) {
      Wire.write(fb[off + i]);
    }
    if (Wire.endTransmission() != 0) {
      oledOk = false;
      return;
    }
  }
}

int handheldBatteryMv() {
  if (PIN_BATT_ADC < 0) {
    return -1;
  }
  int half = analogReadMilliVolts(PIN_BATT_ADC);
  if (half < 0) {
    return -1;
  }
  return half * 2;
}

#endif
