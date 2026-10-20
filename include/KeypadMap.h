#ifndef KeypadMap_h
#define KeypadMap_h

// 4x4 membrane, rotated 90 degrees clockwise (to the right) when it is
// mounted on the left of the handheld. The membrane's own rows and columns
// are the ones printed for an upright legend with the ribbon at the bottom:
//
//   C1 C2 C3 C4
//   1  2  3  A     R1
//   4  5  6  B     R2
//   7  8  9  C     R3
//   *  0  #  D     R4
//   ribbon (R1 R2 R3 R4 C1 C2 C3 C4)
//
// After a 90 degree clockwise turn the ribbon leaves to the left, and the
// printed legend (now sideways) sits like this:
//
//   *  7  4  1
//   0  8  5  2
//   #  9  6  3
//   D  C  B  A
//
// The firmware does not follow that sideways legend. It numbers keys in
// reading order as you look at the upright device: key 0 is the top-left
// key, then left to right, then the next row. Rows and columns are scanned
// in membrane order (R1..R4, C1..C4) and remapped here.
//
//   membrane (row, col) -> key = col * 4 + (3 - row)
//
// That is the same index the TTGO port produces by wiring membrane columns
// to its row drivers and then computing row * 4 + (3 - col).

inline int handheldPadKey(int membraneRow, int membraneCol) {
  if (membraneRow < 0 || membraneRow > 3 || membraneCol < 0 || membraneCol > 3) {
    return -1;
  }
  return membraneCol * 4 + (3 - membraneRow);
}

// Visual row 0..3 (top to bottom) and column 0..3 (left to right) after the
// clockwise mount. Inverse of handheldPadKey.
inline int handheldVisualKey(int visualRow, int visualCol) {
  if (visualRow < 0 || visualRow > 3 || visualCol < 0 || visualCol > 3) {
    return -1;
  }
  return visualRow * 4 + visualCol;
}

// Note order under the function row. Key 0 is the top-left key. C is the
// bottom-left key (12). Returns -1 for the function row.
inline int handheldNotePitch(int key) {
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

// One step of the MothSynth 4x4 menu, the same table the TTGO port uses.
// latched is -1, or 0..3 while F1..F4 is armed. The next key runs that
// function and clears the latch. A function key with nothing armed only
// sets the latch. cmd is 0 when the step is a latch change.
struct HandheldPadAct {
  int latch;
  char cmd;
  int val;
};

inline HandheldPadAct handheldBaseAct(int latched, int key) {
  HandheldPadAct act;
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
    act.val = handheldNotePitch(key);
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
  int n = handheldNotePitch(key);
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

#endif
