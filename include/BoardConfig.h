#ifndef BoardConfig_h
#define BoardConfig_h

// M5Stack Cardputer ADV (Stamp-S3A / ESP32-S3FN8, 8MB flash, no PSRAM,
// ST7789 240x135). Sample caches use PSRAM only when psramFound() is true.
// Everything used here is on the board: no external wiring.
// Display, keyboard (TCA8418), and the ES8311 codec are brought up by
// M5Unified / M5Cardputer. These pins are the ones the firmware touches
// directly (SD and the amplifier enable).

#define BOARD_NAME "Cardputer ADV"
#define MOTHDECK_VERSION "1.0.0"
#define MOTHDECK_BLE_NAME_DEFAULT "MothSynth"

// 1 compiles serial-monitor logs for the SD card and the speaker.
// 0 omits those Serial statements. Override with -DMOTHOS_DEV_LOG=1.
#ifndef MOTHOS_DEV_LOG
#define MOTHOS_DEV_LOG 0
#endif

// microSD on its own SPI bus. GPIO5 is held high before mount; Launcher
// does the same on the ADV because that pin otherwise fights the card.
#define PIN_SD_SCK 40
#define PIN_SD_MISO 39
#define PIN_SD_MOSI 14
#define PIN_SD_CS 12
#define PIN_SD_AUX 5

// ES8311 I2S, from the ADV schematic. M5.Speaker owns these after begin().
#define PIN_I2S_BCLK 41
#define PIN_I2S_WS 43
#define PIN_I2S_DOUT 46
#define PIN_AMP_EN 42

// Internal I2C: TCA8418 keyboard 0x34, ES8311 codec 0x18, BMI270 0x68.
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// Front button, active low. Also sampled at boot for Exit to Launcher.
#define PIN_BTN_A 0

// Battery divider. M5.Power reads it once the board profile is detected.
#define PIN_BATT_ADC 10

// Grove HY2.0-4P. Not used by the firmware; listed so the port stays free.
#define PIN_GROVE_SDA 2
#define PIN_GROVE_SCL 1

// Audio rate. Step timing uses rate/4, matching MothOS (16th-note steps).
static const int kSampleRate = 44100;

// Built-ins are 0..11. 63 is the Loops instrument (notes play SD loops in
// time with the project BPM). Plugin folders use 12..62.
static const int kLoopsVoice = 63;

#endif
