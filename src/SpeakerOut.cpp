#include "SpeakerOut.h"
#include "BoardConfig.h"
#include "DevLog.h"
#include <Arduino.h>

#if !MOTHDECK_BOARD_TTGO

#include <M5Unified.h>

static void es8311PowerUp() {
  static const uint8_t seq[][2] = {
      {0x00, 0x80},
      {0x01, 0xB5},
      {0x02, 0x18},
      {0x0D, 0x01},
      {0x12, 0x00},
      {0x13, 0x10},
      {0x32, 0xBF},
      {0x37, 0x08},
  };
  for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    for (int attempt = 0; attempt < 3; attempt++) {
      if (M5.In_I2C.writeRegister8(0x18, seq[i][0], seq[i][1], 100000)) {
        break;
      }
    }
  }
}

bool speakerStart(uint8_t volume) {
  M5.Speaker.end();
  auto cfg = M5.Speaker.config();
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
  bool ok = M5.Speaker.begin();
  if (ok) {
    M5.Speaker.setVolume(volume);
    DEV_LOG("AUDIO: speaker ok");
  }
  return ok;
}

void speakerSetVolume(uint8_t volume) {
  M5.Speaker.setVolume(volume);
}

int speakerQueued() {
  return (int)M5.Speaker.isPlaying(0);
}

bool speakerWrite(const int16_t *mono, int count) {
  return M5.Speaker.playRaw(mono, count, (uint32_t)kSampleRate, false, 1, 0, false);
}

#elif MOTHDECK_INTERNAL_DAC

#include <driver/dac_continuous.h>

static dac_continuous_handle_t dac = nullptr;
static uint8_t dacVol = 160;
static uint8_t dacBuf[256];

bool speakerStart(uint8_t volume) {
  dacVol = volume;
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH0;
  cfg.desc_num = 4;
  cfg.buf_size = 256;
  cfg.freq_hz = (uint32_t)kSampleRate;
  cfg.offset = 0;
  cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  if (dac_continuous_new_channels(&cfg, &dac) != ESP_OK || !dac) {
    Serial.println("AUDIO: dac alloc failed");
    return false;
  }
  if (dac_continuous_enable(dac) != ESP_OK) {
    Serial.println("AUDIO: dac enable failed");
    return false;
  }
  Serial.printf("AUDIO: internal DAC GPIO%d\n", PIN_DAC);
  return true;
}

void speakerSetVolume(uint8_t volume) {
  dacVol = volume;
}

int speakerQueued() {
  return 0;
}

bool speakerWrite(const int16_t *mono, int count) {
  if (!dac || !mono || count < 1) {
    return false;
  }
  if (count > (int)sizeof(dacBuf)) {
    count = (int)sizeof(dacBuf);
  }
  for (int i = 0; i < count; i++) {
    int s = ((int)mono[i] * (int)dacVol) / 255;
    dacBuf[i] = (uint8_t)((s >> 8) + 128);
  }
  size_t wrote = 0;
  // Block until the DAC DMA takes the block. The audio task paces here.
  esp_err_t err = dac_continuous_write(dac, dacBuf, (size_t)count, &wrote, -1);
  return err == ESP_OK && wrote == (size_t)count;
}

#else

#include <driver/i2s_std.h>

static i2s_chan_handle_t i2sTx = nullptr;
static uint8_t i2sVol = 160;
static int16_t stereo[256 * 2];

bool speakerStart(uint8_t volume) {
  i2sVol = volume;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.dma_desc_num = 8;
  chan.dma_frame_num = 256;
  if (i2s_new_channel(&chan, &i2sTx, nullptr) != ESP_OK || !i2sTx) {
    Serial.println("AUDIO: i2s channel failed");
    return false;
  }
  i2s_std_config_t i2sCfg = {};
  i2sCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)kSampleRate);
  i2sCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  i2sCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  i2sCfg.gpio_cfg.bclk = (gpio_num_t)PIN_I2S_BCLK;
  i2sCfg.gpio_cfg.ws = (gpio_num_t)PIN_I2S_WS;
  i2sCfg.gpio_cfg.dout = (gpio_num_t)PIN_I2S_DOUT;
  i2sCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(i2sTx, &i2sCfg) != ESP_OK || i2s_channel_enable(i2sTx) != ESP_OK) {
    Serial.println("AUDIO: i2s start failed");
    return false;
  }
  Serial.printf("AUDIO: PCM5102 BCLK %d WS %d DIN %d\n", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
  return true;
}

void speakerSetVolume(uint8_t volume) {
  i2sVol = volume;
}

int speakerQueued() {
  return 0;
}

bool speakerWrite(const int16_t *mono, int count) {
  if (!i2sTx || !mono || count < 1) {
    return false;
  }
  if (count > 256) {
    count = 256;
  }
  for (int i = 0; i < count; i++) {
    int s = ((int)mono[i] * (int)i2sVol) / 255;
    if (s > 32767) {
      s = 32767;
    } else if (s < -32768) {
      s = -32768;
    }
    stereo[i * 2] = (int16_t)s;
    stereo[i * 2 + 1] = (int16_t)s;
  }
  size_t wrote = 0;
  // Block until the I2S DMA takes the block. speakerQueued() stays 0.
  esp_err_t err = i2s_channel_write(i2sTx, stereo, (size_t)count * 4, &wrote, -1);
  return err == ESP_OK && wrote == (size_t)count * 4;
}

#endif
