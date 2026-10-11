#ifndef BoardConfig_h
#define BoardConfig_h

// Two boards. The Cardputer ADV build leaves MOTHDECK_BOARD_TTGO at 0.
// The ttgo-t8 env sets it. Sample caches use PSRAM only when psramFound().

#ifndef MOTHDECK_BOARD_TTGO
#define MOTHDECK_BOARD_TTGO 0
#endif

#define MOTHDECK_VERSION "1.2.2"
#define MOTHDECK_BLE_NAME_DEFAULT "Mothdeck"

#if MOTHDECK_BOARD_TTGO
// LilyGO TTGO T8 V1.8: classic ESP32-WROVER, 4MB flash, 4MB PSRAM, microSD.
// The board has no panel of its own. The confirmed display is an ST7789
// 240x135 on the pins below. Wiring is in docs/TTGO-T8.md.

#define BOARD_NAME "TTGO T8"

#ifndef MOTHDECK_LOOPS_WITH_BLE
#define MOTHDECK_LOOPS_WITH_BLE 1
#endif
#ifndef MOTHDECK_PSRAM_PATTERNS
#define MOTHDECK_PSRAM_PATTERNS 1
#endif
#ifndef MOTHDECK_INTERNAL_DAC
#define MOTHDECK_INTERNAL_DAC 0
#endif
#ifndef MOTHDECK_HAS_TFT
#define MOTHDECK_HAS_TFT 1
#endif

// Onboard microSD, 1-bit SDMMC. Silkscreen: CLK 14, CMD 15, D0 2, D3 13.
#ifndef PIN_SD_SCK
#define PIN_SD_SCK 14
#endif
#ifndef PIN_SD_MISO
#define PIN_SD_MISO 2
#endif
#ifndef PIN_SD_MOSI
#define PIN_SD_MOSI 15
#endif
#ifndef PIN_SD_CS
#define PIN_SD_CS 13
#endif
#ifndef PIN_SD_AUX
#define PIN_SD_AUX -1
#endif

// PCM5102A I2S. SCK on the module goes to GND. GPIO25 stays free for the
// internal DAC fallback.
#ifndef PIN_I2S_BCLK
#define PIN_I2S_BCLK 26
#endif
#ifndef PIN_I2S_WS
#define PIN_I2S_WS 27
#endif
#ifndef PIN_I2S_DOUT
#define PIN_I2S_DOUT 22
#endif
#ifndef PIN_I2S_DIN
#define PIN_I2S_DIN -1
#endif
#ifndef PIN_DAC
#define PIN_DAC 25
#endif

// ST7789 240x135 on VSPI. The stock image drives this panel. Pass
// -DMOTHDECK_HAS_TFT=0 to build a headless image.
#ifndef PIN_TFT_SCK
#define PIN_TFT_SCK 18
#endif
#ifndef PIN_TFT_MOSI
#define PIN_TFT_MOSI 23
#endif
#ifndef PIN_TFT_CS
#define PIN_TFT_CS 5
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC 21
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST 19
#endif
#ifndef PIN_TFT_BL
#define PIN_TFT_BL 32
#endif
#ifndef MOTHDECK_TFT_OFFSET_X
#define MOTHDECK_TFT_OFFSET_X 40
#endif
#ifndef MOTHDECK_TFT_OFFSET_Y
#define MOTHDECK_TFT_OFFSET_Y 53
#endif
#ifndef MOTHDECK_TFT_ROTATION
#define MOTHDECK_TFT_ROTATION 1
#endif

#ifndef PIN_BTN_A
#define PIN_BTN_A -1
#endif
#ifndef PIN_BATT_ADC
#define PIN_BATT_ADC 35
#endif

// 4x4 membrane, mounted with the connector on top. Firmware rows are the
// membrane's columns, and firmware columns are the membrane's rows. All -1
// leaves the pad off. The defaults are free beside SD, I2S, and the ST7789.
// The DAC image cannot use GPIO25, so that one row moves to GPIO22.
#ifndef PIN_MX_R0
#define PIN_MX_R0 4
#endif
#ifndef PIN_MX_R1
#define PIN_MX_R1 33
#endif
#ifndef PIN_MX_R2
#define PIN_MX_R2 0
#endif
#ifndef PIN_MX_R3
#if MOTHDECK_INTERNAL_DAC
#define PIN_MX_R3 22
#else
#define PIN_MX_R3 25
#endif
#endif
#ifndef PIN_MX_C0
#define PIN_MX_C0 34
#endif
#ifndef PIN_MX_C1
#define PIN_MX_C1 36
#endif
#ifndef PIN_MX_C2
#define PIN_MX_C2 39
#endif
#ifndef PIN_MX_C3
#define PIN_MX_C3 35
#endif

#else

// M5Stack Cardputer ADV (Stamp-S3A / ESP32-S3FN8, 8MB flash, no PSRAM,
// ST7789 240x135). Everything used here is on the board: no external wiring.
// Display, keyboard (TCA8418), and the ES8311 codec are brought up by
// M5Unified / M5Cardputer. These pins are the ones the firmware touches
// directly (the SD bus and the I2S lines into the ES8311).

#define BOARD_NAME "Cardputer ADV"
#define MOTHDECK_LOOPS_WITH_BLE 0
#define MOTHDECK_PSRAM_PATTERNS 0
#define MOTHDECK_INTERNAL_DAC 0
#define MOTHDECK_HAS_TFT 0

// microSD on its own SPI bus. GPIO5 is held high before mount; Launcher
// does the same on the ADV because that pin otherwise fights the card.
#define PIN_SD_SCK 40
#define PIN_SD_MISO 39
#define PIN_SD_MOSI 14
#define PIN_SD_CS 12
#define PIN_SD_AUX 5

// ES8311 I2S, from the ADV schematic. M5.Speaker owns these after begin().
// GPIO42 is DSDIN (data into the codec). It is not an amplifier enable:
// driving it high as a plain GPIO replaces the I2S stream and mutes the
// speaker. The NS4150B follows the codec headphone driver, which is
// powered by the ES8311 register write in audioStart(). GPIO46 is ASDOUT
// (microphone data out of the codec).
#define PIN_I2S_BCLK 41
#define PIN_I2S_WS 43
#define PIN_I2S_DOUT 42
#define PIN_I2S_DIN 46

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

#endif

// Recorded chords (up to four notes on one step) use PSRAM on the TTGO.
// The Cardputer build leaves this at 0, so its step word, song file, and
// piano-roll snap stay the same size.
#ifndef MOTHDECK_CHORDS
#define MOTHDECK_CHORDS MOTHDECK_BOARD_TTGO
#endif

// 1 compiles serial-monitor logs for the SD card and the speaker.
// 0 omits those Serial statements. Override with -DMOTHOS_DEV_LOG=1.
#ifndef MOTHOS_DEV_LOG
#define MOTHOS_DEV_LOG 0
#endif

// Audio rate. Step timing uses rate/4, matching MothOS (16th-note steps).
static const int kSampleRate = 44100;

// Built-ins are 0..11. 63 is the Loops instrument (notes play SD loops in
// time with the project BPM). Plugin folders use 12..62.
static const int kLoopsVoice = 63;

#endif
