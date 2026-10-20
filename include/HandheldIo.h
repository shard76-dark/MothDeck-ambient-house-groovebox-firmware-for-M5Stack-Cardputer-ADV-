#ifndef HandheldIo_h
#define HandheldIo_h
#include <stdint.h>

// SSD1306, HT16K33, 4x4 keypad, four LEDs, and the battery divider.
// Compiled for the DevKit handheld only.

void handheldBringUp();
bool handheldOledOk();
bool handheldSegOk();
bool handheldPadOk();

// One debounced edge. key is the visual index from handheldPadKey.
// hold is set once when that key has stayed down about half a second.
bool handheldPollKey(int *key, bool *hold);

void handheldLeds(bool step, bool beat, bool play, bool rec);

// digit 0..9, or 10 for blank. dot lights the decimal on that digit.
void handheldShowDigits(const int digit[4], const int dot[4]);

void handheldOledClear();
void handheldOledFill(int x, int y, int w, int h, bool on);
void handheldOledGlyph(int x, int y, const uint8_t col[5]);
void handheldOledFlush();

// Millivolts at the cell, or -1 if the divider pin is not configured.
int handheldBatteryMv();

#endif
