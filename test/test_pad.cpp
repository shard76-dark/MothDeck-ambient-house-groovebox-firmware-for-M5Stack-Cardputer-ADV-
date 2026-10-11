#include "BoardIo.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;

static void expect(int row, int col, int key) {
  int got = ttgoPadKey(row, col);
  if (got != key) {
    printf("ttgoPadKey(%d,%d)=%d want %d\n", row, col, got, key);
    fails++;
  }
}

static void expectAct(int latched, int key, int latch, char cmd, int val) {
  TtgoPadAct got = ttgoBaseAct(latched, key);
  if (got.latch != latch || got.cmd != cmd || got.val != val) {
    printf("act latched %d key %d -> latch %d cmd %c val %d, want latch %d cmd %c val %d\n", latched, key,
           got.latch, got.cmd ? got.cmd : '-', got.val, latch, cmd ? cmd : '-', val);
    fails++;
  }
}

int main() {
  expect(0, 3, 0);
  expect(0, 2, 1);
  expect(0, 1, 2);
  expect(0, 0, 3);
  expect(1, 3, 4);
  expect(1, 0, 7);
  expect(2, 1, 10);
  expect(3, 3, 12);
  expect(3, 0, 15);
  expect(-1, 0, -1);
  expect(0, 4, -1);
  expect(4, 0, -1);
  for (int vr = 0; vr < 4; vr++) {
    for (int vc = 0; vc < 4; vc++) {
      expect(vr, 3 - vc, vr * 4 + vc);
    }
  }
  expectAct(-1, 0, 0, 0, 0);
  expectAct(-1, 12, -1, 'N', 0);
  expectAct(-1, 15, -1, 'N', 3);
  expectAct(-1, 8, -1, 'N', 4);
  expectAct(-1, 11, -1, 'N', 7);
  expectAct(-1, 4, -1, 'N', 8);
  expectAct(-1, 7, -1, 'N', 11);
  expectAct(0, 0, -1, 'O', 0);
  expectAct(0, 3, -1, 'O', 3);
  expectAct(0, 12, -1, 'I', 0);
  expectAct(0, 7, -1, 'I', 11);
  expectAct(1, 0, -1, 'V', 0);
  expectAct(1, 3, -1, 'V', 3);
  expectAct(1, 12, -1, 'A', 0);
  expectAct(1, 15, -1, 'A', 3);
  expectAct(1, 8, -1, 'D', 0);
  expectAct(1, 11, -1, 'D', 3);
  expectAct(1, 4, -1, 'E', 0);
  expectAct(1, 7, -1, 'E', 3);
  expectAct(2, 0, -1, 'T', 0);
  expectAct(2, 3, -1, 'T', 3);
  expectAct(2, 12, -1, '#', 0);
  expectAct(2, 15, -1, '#', 3);
  expectAct(2, 8, -1, '$', 0);
  expectAct(2, 11, -1, '$', 3);
  expectAct(2, 4, -1, '^', 0);
  expectAct(2, 7, -1, '^', 3);
  expectAct(3, 0, -1, 'L', 0);
  expectAct(3, 2, -1, 'L', 2);
  expectAct(3, 3, -1, 'P', 0);
  expectAct(3, 12, -1, 'X', 1);
  expectAct(3, 13, -1, 'X', 3);
  expectAct(3, 14, -1, 'H', 0);
  expectAct(3, 15, -1, 'C', 0);
  expectAct(3, 8, -1, '*', 0);
  expectAct(3, 11, -1, '*', 3);
  expectAct(3, 4, -1, 'B', 0);
  expectAct(3, 7, -1, 'B', 3);
  if (fails) {
    printf("%d pad key mismatches\n", fails);
    return 1;
  }
  printf("pad key ok\n");
  return 0;
}
