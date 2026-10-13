#include "diag_run.h"

#include "diag_ble.h"
#include "diag_ui.h"

#include <Arduino.h>
#include <stdio.h>

struct Step {
  const char *name;
  const char *hint;
  bool waitGo;
  int seconds;
  uint16_t minU;
  uint16_t maxU;
  bool changeInterval;
};

static const Step kSteps[] = {
  {"7.5-15ms", "Press Play on MPC", true, 20, 6, 12, true},
  {"7.5ms", "Keep MPC playing", false, 20, 6, 6, true},
  {"11.25ms", "Keep MPC playing", false, 20, 9, 9, true},
  {"15ms", "Keep MPC playing", false, 20, 12, 12, true},
  {"30ms", "Keep MPC playing", false, 20, 24, 24, true},
  {"MTC", "Switch MPC to MTC", true, 20, 6, 12, false},
};

static const int kStepCount = (int)(sizeof(kSteps) / sizeof(kSteps[0]));

static bool gActive = false;
static bool gWait = false;
static int gIndex = 0;
static uint32_t gStartMs = 0;
static char gLabel[40];

static void showStep(int percent) {
  const Step *step = &kSteps[gIndex];
  snprintf(gLabel, sizeof(gLabel), "Test %d/%d: %s", gIndex + 1, kStepCount, step->name);
  diagUiTest(gLabel, step->hint, percent);
}

static void logStep(const char *phase) {
  const Step *step = &kSteps[gIndex];
  Serial.printf(
    "{\"t\":\"test\",\"step\":%d,\"steps\":%d,\"name\":\"%s\",\"phase\":\"%s\",\"min\":%u,\"max\":%u}\n",
    gIndex + 1,
    kStepCount,
    step->name,
    phase ? phase : "",
    (unsigned)step->minU,
    (unsigned)step->maxU);
}

static void enterStep(uint32_t nowMs) {
  const Step *step = &kSteps[gIndex];
  if (step->changeInterval) {
    diagBleSetInterval(step->minU, step->maxU, 0, true);
  }
  if (step->waitGo) {
    gWait = true;
    showStep(-1);
    logStep("wait");
    return;
  }
  gWait = false;
  gStartMs = nowMs;
  showStep(0);
  logStep("run");
}

void diagRunStart() {
  gActive = true;
  gIndex = 0;
  enterStep(0);
}

bool diagRunGo() {
  if (!gActive) {
    diagRunStart();
  }
  if (!gActive) {
    return false;
  }
  if (!gWait) {
    return true;
  }
  gWait = false;
  gStartMs = 0;
  logStep("run");
  return true;
}

void diagRunStop() {
  gActive = false;
  gWait = false;
  diagUiTest("Idle", "", -1);
}

void diagRunPoll(uint32_t nowMs) {
  if (!gActive) {
    return;
  }
  if (gWait) {
    showStep(-1);
    return;
  }
  if (gStartMs == 0) {
    gStartMs = nowMs;
  }
  const Step *step = &kSteps[gIndex];
  uint32_t elapsed = (uint32_t)(nowMs - gStartMs);
  uint32_t total = (uint32_t)step->seconds * 1000u;
  if (total == 0) {
    total = 1;
  }
  int percent = (int)((uint64_t)elapsed * 100ull / total);
  if (percent > 100) {
    percent = 100;
  }
  showStep(percent);
  if (elapsed < total) {
    return;
  }
  gIndex++;
  if (gIndex >= kStepCount) {
    gActive = false;
    diagUiTest("Done", "", 100);
    Serial.println("{\"t\":\"test\",\"phase\":\"done\"}");
    return;
  }
  enterStep(nowMs);
}

bool diagRunActive() {
  return gActive;
}

bool diagRunWaiting() {
  return gActive && gWait;
}
