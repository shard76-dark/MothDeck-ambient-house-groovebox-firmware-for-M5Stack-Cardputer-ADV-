#include "AudioEngine.h"
#include "DspHot.h"
#include "Tracker.h"
#include "BoardConfig.h"
#include "DevLog.h"
#include "LoopFormat.h"
#include "BleMidi.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

static Tracker tracker;
static BleMidi *midiIn = nullptr;
static QueueHandle_t cmdQ = nullptr;
static QueueHandle_t midiOutQ = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static bool speakerOk = false;
static char audioFault[28] = "audio off: not started";
static std::atomic<int> songPending{0};
static std::atomic<int> captureState{0};
static std::atomic<int> loopPending{0};
static std::atomic<int> patPending{0};
static SongData songBuf;
static SongData captureBuf;
static LoopArm loopBuf;
static int loopTrack = 0;
static uint8_t patBuf[kPatternStepsMax];
static int patCount = 0;
static int patTrack = 0;
static Snap snapSlots[2];
static std::atomic<uint32_t> snapSeq{0};
static const int kBlock = 256;
static int16_t blocks[3][kBlock];

enum CmdType : uint8_t { CMD_KEY = 1, CMD_MIDI = 2, CMD_STOP_LOOP = 3, CMD_STOP_AUD = 4 };

struct Cmd {
  uint8_t type;
  char kind;
  int val;
  MidiEvent midi;
};

static void publishSnap(const int *peak) {
  uint32_t s = snapSeq.load(std::memory_order_relaxed);
  snapSeq.store(s + 1, std::memory_order_release);
  Snap &dst = snapSlots[((s / 2) + 1) & 1];
  for (int t = 0; t < 4; t++) {
    tracker.lastSamples[t] = peak[t];
  }
  tracker.FillSnap(&dst);
  snapSeq.store(s + 2, std::memory_order_release);
}

static void drainSide() {
  if (captureState.load(std::memory_order_acquire) == 1) {
    tracker.CaptureSong(&captureBuf);
    captureState.store(2, std::memory_order_release);
  }
  if (songPending.load(std::memory_order_acquire) == 1) {
    tracker.ApplySong(songBuf);
    songPending.store(0, std::memory_order_release);
  }
  if (loopPending.load(std::memory_order_acquire) == 1) {
    if (loopTrack < 0) {
      tracker.StartAudition(loopBuf);
    } else {
      tracker.ArmLoop(loopTrack, loopBuf);
    }
    loopPending.store(0, std::memory_order_release);
  }
  if (patPending.load(std::memory_order_acquire) == 1) {
    tracker.WritePattern(patTrack, patBuf, patCount);
    patPending.store(0, std::memory_order_release);
  }
  Cmd cmd;
  while (cmdQ && xQueueReceive(cmdQ, &cmd, 0) == pdTRUE) {
    if (cmd.type == CMD_KEY) {
      tracker.SetCommand(cmd.kind, cmd.val);
    } else if (cmd.type == CMD_MIDI) {
      tracker.HandleMidi(cmd.midi);
    } else if (cmd.type == CMD_STOP_LOOP) {
      tracker.StopLoop(cmd.val);
    } else if (cmd.type == CMD_STOP_AUD) {
      tracker.StopAudition();
    }
  }
  MidiEvent out;
  while (tracker.PopMidiOut(&out)) {
    if (midiOutQ) {
      xQueueSend(midiOutQ, &out, 0);
    }
  }
}

static void drainMidi() {
  if (!midiIn) {
    return;
  }
  MidiEvent ev[12];
  for (int pass = 0; pass < 6; pass++) {
    int n = midiIn->Poll(ev, 12);
    if (n <= 0) {
      return;
    }
    for (int i = 0; i < n; i++) {
      tracker.HandleMidi(ev[i]);
    }
  }
}

static int renderCount() {
  // A connected link renders half a block and keeps one buffer queued.
  // Local playback stays at 256 samples and two buffers.
  if (midiIn && midiIn->Connected()) {
    return kBlock / 2;
  }
  return kBlock;
}

static int queueLimit() {
  if (midiIn && midiIn->Connected()) {
    return 1;
  }
  return 2;
}

static void renderBlock(int16_t *dst, int count) {
  drainMidi();
  drainSide();
  int peak[4] = {0, 0, 0, 0};
  for (int i = 0; i < count; i++) {
    // 32 samples is about 0.7 ms. A note that arrived mid-block starts
    // on the next slice instead of waiting for the following buffer.
    if ((i & 31) == 0) {
      drainMidi();
    }
    dst[i] = (int16_t)dspSat16(tracker.UpdateTracker());
    for (int t = 0; t < 4; t++) {
      int a = dspAbs(tracker.lastSamples[t]);
      if (a > peak[t]) {
        peak[t] = a;
      }
    }
  }
  publishSnap(peak);
}

// Same sequence M5Unified uses for board_M5CardputerADV. Without it the
// codec stays in reset and the speaker task has nothing to clock.
static void es8311PowerUp() {
  static const uint8_t seq[][2] = {
      {0x00, 0x80},  // CSM power on
      {0x01, 0xB5},  // MCLK from BCLK
      {0x02, 0x18},  // MULT_PRE = 3
      {0x0D, 0x01},  // analog circuitry
      {0x12, 0x00},  // DAC power
      {0x13, 0x10},  // headphone driver, which feeds the NS4150B
      {0x32, 0xBF},  // DAC volume, 0 dB
      {0x37, 0x08},  // bypass the DAC equalizer
  };
  for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    for (int attempt = 0; attempt < 3; attempt++) {
      if (M5.In_I2C.writeRegister8(0x18, seq[i][0], seq[i][1], 100000)) {
        break;
      }
    }
  }
}

static void audioTask(void *arg) {
  (void)arg;
  int fill = 0;
  for (;;) {
    // Drain even while the speaker still has a buffer. The old path
    // skipped this until two blocks had finished, so a note sat for the
    // whole queue.
    drainMidi();
    int queued = (int)M5.Speaker.isPlaying(0);
    if (queued >= queueLimit()) {
      vTaskDelay(1);
      continue;
    }
    int count = renderCount();
    renderBlock(blocks[fill], count);
    if (!M5.Speaker.playRaw(blocks[fill], count, (uint32_t)kSampleRate, false, 1, 0, false)) {
      vTaskDelay(1);
      continue;
    }
    fill = (fill + 1) % 3;
  }
}

static void logAudioHeap(const char *tag) {
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  DEV_LOGF("HEAP: %s free=%u largest=%u\n", tag, (unsigned)freeB, (unsigned)largest);
}

void audioBindMidi(BleMidi *ble) {
  midiIn = ble;
}

void audioStart() {
  if (audioTaskHandle) {
    return;
  }
  logAudioHeap("before audio");
  memset(snapSlots, 0, sizeof(snapSlots));
  cmdQ = xQueueCreate(24, sizeof(Cmd));
  midiOutQ = xQueueCreate(16, sizeof(MidiEvent));
  if (!cmdQ || !midiOutQ) {
    snprintf(audioFault, sizeof(audioFault), "audio off: no queue");
    Serial.println("AUDIO: queue create failed");
    logAudioHeap("after audio");
    return;
  }
  M5.Speaker.end();
  auto cfg = M5.Speaker.config();
  // Pins and port are what _begin_audio installs for the ADV. Setting them
  // again here keeps playback alive if that profile was skipped. GPIO42 is
  // the codec data input, so it stays an I2S pin rather than a GPIO high.
  cfg.pin_bck = PIN_I2S_BCLK;
  cfg.pin_ws = PIN_I2S_WS;
  cfg.pin_data_out = PIN_I2S_DOUT;
  cfg.pin_mck = I2S_PIN_NO_CHANGE;
  cfg.i2s_port = I2S_NUM_1;
  cfg.buzzer = false;
  cfg.use_dac = false;
  cfg.sample_rate = kSampleRate;
  cfg.task_priority = 4;
  cfg.task_pinned_core = 1;
  cfg.dma_buf_len = 256;
  cfg.dma_buf_count = 8;
  cfg.magnification = 2;
  cfg.stereo = false;
  M5.Speaker.config(cfg);
  es8311PowerUp();
  speakerOk = M5.Speaker.begin();
  if (!speakerOk) {
    Serial.println("AUDIO: speaker begin failed");
  } else {
    DEV_LOG("AUDIO: speaker ok");
    DEV_LOG("Speaker: 44100 Hz");
  }
  M5.Speaker.setVolume(160);
  BaseType_t created = xTaskCreatePinnedToCore(audioTask, "mothdeck-audio", 8192, nullptr, 5, &audioTaskHandle, 1);
  if (created != pdPASS || !audioTaskHandle) {
    audioTaskHandle = nullptr;
    snprintf(audioFault, sizeof(audioFault), "audio off: no task");
    Serial.println("AUDIO: task create failed");
    logAudioHeap("after audio");
    return;
  }
  if (!speakerOk) {
    snprintf(audioFault, sizeof(audioFault), "audio off: no speaker");
  } else {
    snprintf(audioFault, sizeof(audioFault), "audio ok");
  }
  if (strcmp(audioFault, "audio ok") != 0) {
    Serial.printf("AUDIO: %s\n", audioFault);
  } else {
    DEV_LOGF("AUDIO: %s\n", audioFault);
  }
  logAudioHeap("after audio");
}

bool audioRunning() {
  return audioTaskHandle != nullptr;
}

const char *audioFaultText() {
  return audioFault;
}

void audioCommand(char kind, int val) {
  if (!cmdQ) {
    return;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_KEY;
  cmd.kind = kind;
  cmd.val = val;
  xQueueSend(cmdQ, &cmd, 0);
}

void audioMidi(const MidiEvent &event) {
  if (!cmdQ) {
    return;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_MIDI;
  cmd.midi = event;
  xQueueSend(cmdQ, &cmd, 0);
}

bool audioCapture(SongData *song) {
  if (!song) {
    return false;
  }
  if (captureState.load() != 0) {
    return false;
  }
  captureState.store(1);
  uint32_t start = millis();
  while (captureState.load() != 2) {
    if ((uint32_t)(millis() - start) > 300) {
      captureState.store(0);
      return false;
    }
    delay(1);
  }
  *song = captureBuf;
  captureState.store(0);
  return true;
}

void audioApplySong(const SongData &song) {
  while (songPending.load() != 0) {
    delay(1);
  }
  songBuf = song;
  songPending.store(1);
}

void audioArmLoop(int track, const LoopArm &arm) {
  while (loopPending.load() != 0) {
    delay(1);
  }
  loopBuf = arm;
  loopTrack = track;
  loopPending.store(1);
}

void audioStopLoop(int track) {
  if (!cmdQ) {
    return;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_STOP_LOOP;
  cmd.val = track;
  xQueueSend(cmdQ, &cmd, 0);
}

void audioAudition(const LoopArm &arm) {
  audioArmLoop(-1, arm);
}

void audioStopAudition() {
  if (!cmdQ) {
    return;
  }
  Cmd cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.type = CMD_STOP_AUD;
  xQueueSend(cmdQ, &cmd, 0);
}

void audioWritePattern(int track, const uint8_t *steps, int count) {
  if (!steps || count <= 0) {
    return;
  }
  while (patPending.load() != 0) {
    delay(1);
  }
  if (count > kPatternStepsMax) {
    count = kPatternStepsMax;
  }
  memset(patBuf, 0, sizeof(patBuf));
  memcpy(patBuf, steps, count);
  patCount = count;
  patTrack = track;
  patPending.store(1);
}

bool audioPopMidi(MidiEvent *event) {
  if (!event || !midiOutQ) {
    return false;
  }
  return xQueueReceive(midiOutQ, event, 0) == pdTRUE;
}

void audioReadSnap(Snap *out) {
  if (!out) {
    return;
  }
  for (int n = 0; n < 4; n++) {
    uint32_t s = snapSeq.load(std::memory_order_acquire);
    if (s & 1) {
      continue;
    }
    *out = snapSlots[(s / 2) & 1];
    if (snapSeq.load(std::memory_order_acquire) == s) {
      return;
    }
  }
}

void audioSetSpeakerVolume(uint8_t volume) {
  M5.Speaker.setVolume(volume);
}
