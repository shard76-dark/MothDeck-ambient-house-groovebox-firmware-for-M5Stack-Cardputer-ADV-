#ifndef BoardIo_h
#define BoardIo_h
#include <stdint.h>

// One key event. The Cardputer copies this out of the TCA8418. The TTGO
// fills it from the serial port or the 4x4 pad.
//
// The pad is rotated so its connector is on top. A firmware row is a
// membrane column, and a firmware column is a membrane row. Key 0 is the
// top-left key as you look at it.
inline int ttgoPadKey(int row, int col) {
  if (row < 0 || row > 3 || col < 0 || col > 3) {
    return -1;
  }
  return row * 4 + (3 - col);
}

// MothSynth note order under the function row. Key 0 is the top-left key.
// C is the bottom-left key (12). Returns -1 for the function row.
inline int ttgoNotePitch(int key) {
  if (key >= 12 && key <= 15) {
    return key - 12;
  }
  if (key >= 8 && key <= 11) {
    return key - 4;
  }
  if (key >= 4 && key <= 7) {
    return key + 4;
  }
  return -1;
}

// One step of the original MothOS 4x4 menu. latched is -1, or 0..3 while
// F1..F4 is armed. The next key runs that function and clears the latch.
// A function key with nothing armed only sets latch. cmd is 0 when the
// step is a latch change. The extended MothDeck pages are not in here.
struct TtgoPadAct {
  int latch;
  char cmd;
  int val;
};

inline TtgoPadAct ttgoBaseAct(int latched, int key) {
  TtgoPadAct act;
  act.latch = latched;
  act.cmd = 0;
  act.val = 0;
  if (key < 0 || key > 15) {
    return act;
  }
  if (latched < 0 || latched > 3) {
    if (key <= 3) {
      act.latch = key;
      return act;
    }
    act.cmd = 'N';
    act.val = ttgoNotePitch(key);
    return act;
  }
  act.latch = -1;
  if (key <= 3) {
    if (latched == 0) {
      act.cmd = 'O';
      act.val = key;
    } else if (latched == 1) {
      act.cmd = 'V';
      act.val = key;
    } else if (latched == 2) {
      act.cmd = 'T';
      act.val = key;
    } else if (key == 3) {
      act.cmd = 'P';
    } else {
      act.cmd = 'L';
      act.val = key;
    }
    return act;
  }
  int n = ttgoNotePitch(key);
  if (latched == 0) {
    act.cmd = 'I';
    act.val = n;
  } else if (latched == 1) {
    if (n < 4) {
      act.cmd = 'A';
      act.val = n;
    } else if (n < 8) {
      act.cmd = 'D';
      act.val = n - 4;
    } else {
      act.cmd = 'E';
      act.val = n - 8;
    }
  } else if (latched == 2) {
    if (n < 4) {
      act.cmd = '#';
      act.val = n;
    } else if (n < 8) {
      act.cmd = '$';
      act.val = n - 4;
    } else {
      act.cmd = '^';
      act.val = n - 8;
    }
  } else if (n < 2) {
    act.cmd = 'X';
    act.val = n == 0 ? 1 : 3;
  } else if (n == 2) {
    act.cmd = 'H';
  } else if (n == 3) {
    act.cmd = 'C';
  } else if (n < 8) {
    act.cmd = '*';
    act.val = n - 4;
  } else {
    act.cmd = 'B';
    act.val = n - 8;
  }
  return act;
}

struct DeckKeys {
  bool tab;
  bool fn;
  bool shift;
  bool ctrl;
  bool opt;
  bool alt;
  bool del;
  bool enter;
  bool space;
  char word[8];
  int wordLen;
  // TTGO pad. -1 when this event came from the serial port or the Cardputer.
  // padHold is a function key that stayed down. The menu that reads these
  // lives in the TTGO UI path.
  int padKey;
  bool padHold;
};

void boardUpdate();
bool boardPollKeys(DeckKeys *keys);
bool boardEscHeld();
bool boardBtnHeld();

void boardPushBegin();
void boardPushEnd();
void boardPushImage(int x, int y, int w, int h, const uint16_t *pixels);
void boardSetBrightness(uint8_t level);
int boardBatteryPct();
int boardBatteryMv();
bool boardHasDisplay();

// TTGO panel bring-up. The Cardputer display is started from main.
void boardBringUpTtgo();

#endif
