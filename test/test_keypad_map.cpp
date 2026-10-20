#include "BeatDisplay.h"
#include "KeypadMap.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(bool ok, const char *msg) {
  if (ok) {
    return;
  }
  printf("FAIL %s\n", msg);
  fails++;
}

int main() {
  int seen[16];
  memset(seen, 0, sizeof(seen));
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      int key = handheldPadKey(row, col);
      expect(key >= 0 && key < 16, "key in range");
      if (key >= 0 && key < 16) {
        seen[key]++;
      }
    }
  }
  for (int i = 0; i < 16; i++) {
    expect(seen[i] == 1, "key used once");
  }

  // 90 degrees clockwise: membrane bottom-left (*) becomes the visual top-left.
  expect(handheldPadKey(3, 0) == 0, "top-left is key 0");
  // Membrane top-left (printed 1) becomes the visual top-right.
  expect(handheldPadKey(0, 0) == 3, "printed 1 is key 3");
  // Membrane top-right (printed A) becomes the visual bottom-right.
  expect(handheldPadKey(0, 3) == 15, "printed A is key 15");
  // Membrane bottom-right (printed D) becomes the visual bottom-left.
  expect(handheldPadKey(3, 3) == 12, "printed D is key 12");

  // Visual reading order is row-major after the remap.
  expect(handheldPadKey(3, 0) == handheldVisualKey(0, 0), "visual 0,0");
  expect(handheldPadKey(2, 0) == handheldVisualKey(0, 1), "visual 0,1");
  expect(handheldPadKey(0, 3) == handheldVisualKey(3, 3), "visual 3,3");

  HandheldPadAct live = handheldBaseAct(-1, 12);
  expect(live.cmd == 'N' && live.val == 0, "bottom-left plays C");
  expect(live.latch == -1, "note does not latch");

  HandheldPadAct arm = handheldBaseAct(-1, 0);
  expect(arm.cmd == 0 && arm.latch == 0, "F1 arms");
  HandheldPadAct oct = handheldBaseAct(arm.latch, 2);
  expect(oct.cmd == 'O' && oct.val == 2 && oct.latch == -1, "F1 then F3 is octave 2");

  HandheldPadAct play = handheldBaseAct(3, 3);
  expect(play.cmd == 'P', "F4 then F4 is play");

  // next step 1 of pattern 0 sounds step 0: bar 1 beat 1.
  BarBeat start = barBeatFromNextStep(1, 16, 0);
  expect(start.bar == 1 && start.beat == 1 && start.stepInBar == 0, "first beat");
  expect(isBeatStep(1, 16, 0), "step 0 is a beat");
  // next step 2 sounds step 1, still beat 1, not a beat boundary.
  expect(!isBeatStep(2, 16, 0), "step 1 is not a beat");
  // next step 5 sounds step 4: beat 2.
  BarBeat beat2 = barBeatFromNextStep(5, 16, 0);
  expect(beat2.bar == 1 && beat2.beat == 2 && beat2.stepInBar == 4, "second beat");
  // Wrap: next step back at the pattern base sounds the last step.
  BarBeat wrapped = barBeatFromNextStep(0, 16, 0);
  expect(wrapped.local == 15 && wrapped.beat == 4, "wrap shows last beat");

  SevenSeg seg;
  formatBeatReadout(true, 1, 3, 120, &seg);
  expect(seg.digit[0] == 10 && seg.digit[1] == 1 && seg.dot[1] == 1 && seg.digit[2] == 3 && seg.digit[3] == 10,
         "playing shows bar.beat");
  formatBeatReadout(false, 1, 1, 120, &seg);
  expect(seg.digit[1] == 1 && seg.digit[2] == 2 && seg.digit[3] == 0 && seg.dot[1] == 0, "stopped shows BPM");
  formatBeatReadout(false, 1, 1, 95, &seg);
  expect(seg.digit[1] == 10 && seg.digit[2] == 9 && seg.digit[3] == 5, "two-digit BPM");

  if (fails) {
    printf("%d failed\n", fails);
    return 1;
  }
  printf("keypad map ok\n");
  return 0;
}
