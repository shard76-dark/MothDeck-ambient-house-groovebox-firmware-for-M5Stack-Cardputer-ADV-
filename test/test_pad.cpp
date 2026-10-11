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
  if (fails) {
    printf("%d pad key mismatches\n", fails);
    return 1;
  }
  printf("pad key ok\n");
  return 0;
}
