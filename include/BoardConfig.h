#ifndef BoardConfig_h
#define BoardConfig_h

// Two boards. The Cardputer ADV build leaves MOTHDECK_BOARD_HANDHELD at 0.
// The esp32devkit_handheld env sets it. Pins for that board are the DOIT
// ESP32 DevKit V1 (30-pin, ESP32-D0WD). Wiring is in the README.

#ifndef MOTHDECK_BOARD_HANDHELD
#define MOTHDECK_BOARD_HANDHELD 0
#endif

#define MOTHDECK_VERSION "1.2.12"
#define MOTHDECK_BLE_NAME_DEFAULT "Mothdeck"

// 1 compiles serial-monitor logs for the SD card and the speaker.
// 0 omits those Serial statements. Override with -DMOTHOS_DEV_LOG=1.
#ifndef MOTHOS_DEV_LOG
#define MOTHOS_DEV_LOG 0
#endif

#if MOTHDECK_BOARD_HANDHELD

#define BOARD_NAME "ESP32 DevKit"

// Classic ESP32-WROOM-32. No PSRAM, no microSD on this carrier, no USB MIDI
// (the DevKit USB port is a UART bridge). BLE MIDI is the radio transport.
// Strapping pins GPIO0, GPIO2, GPIO12, and GPIO15 are left open. Flash pins
// GPIO6-11 are not on the header. Input-only GPIO34-39 are the keypad
// columns and need external pull-ups. GPIO33 is ADC1 so the battery divider
// can be read while the radio is up.

#ifndef PIN_SD_SCK
#define PIN_SD_SCK -1
#endif
#ifndef PIN_SD_MISO
#define PIN_SD_MISO -1
#endif
#ifndef PIN_SD_MOSI
#define PIN_SD_MOSI -1
#endif
#ifndef PIN_SD_CS
#define PIN_SD_CS -1
#endif
#ifndef PIN_SD_AUX
#define PIN_SD_AUX -1
#endif

// PCM5102A I2S Philips. The module SCK pin is tied to GND on the carrier
// so the DAC uses its PLL. Do not drive SCK from a GPIO.
#ifndef PIN_I2S_BCLK
#define PIN_I2S_BCLK 26
#endif
#ifndef PIN_I2S_WS
#define PIN_I2S_WS 25
#endif
#ifndef PIN_I2S_DOUT
#define PIN_I2S_DOUT 27
#endif
#ifndef PIN_I2S_DIN
#define PIN_I2S_DIN -1
#endif

// Shared I2C bus: SSD1306 at 0x3C and HT16K33 at 0x70.
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 21
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 22
#endif
#ifndef MOTHDECK_OLED_ADDR
#define MOTHDECK_OLED_ADDR 0x3C
#endif
#ifndef MOTHDECK_HT16K33_ADDR
#define MOTHDECK_HT16K33_ADDR 0x70
#endif

// 4x4 membrane in membrane order (R1..R4, C1..C4). The clockwise remap is
// in KeypadMap.h. Columns are input-only; the board has 10k pull-ups.
#ifndef PIN_MX_R0
#define PIN_MX_R0 16
#endif
#ifndef PIN_MX_R1
#define PIN_MX_R1 17
#endif
#ifndef PIN_MX_R2
#define PIN_MX_R2 18
#endif
#ifndef PIN_MX_R3
#define PIN_MX_R3 19
#endif
#ifndef PIN_MX_C0
#define PIN_MX_C0 34
#endif
#ifndef PIN_MX_C1
#define PIN_MX_C1 35
#endif
#ifndef PIN_MX_C2
#define PIN_MX_C2 36
#endif
#ifndef PIN_MX_C3
#define PIN_MX_C3 39
#endif

// Active high. GPIO -> 470 ohm -> LED -> GND.
#ifndef PIN_LED_STEP
#define PIN_LED_STEP 4
#endif
#ifndef PIN_LED_BEAT
#define PIN_LED_BEAT 5
#endif
#ifndef PIN_LED_PLAY
#define PIN_LED_PLAY 13
#endif
#ifndef PIN_LED_REC
#define PIN_LED_REC 14
#endif

// Battery divider midpoint. 100k / 100k from the cell, so the pin sees half.
#ifndef PIN_BATT_ADC
#define PIN_BATT_ADC 33
#endif
#ifndef PIN_BTN_A
#define PIN_BTN_A -1
#endif

#else

// M5Stack Cardputer ADV (Stamp-S3A / ESP32-S3FN8, 8MB flash, no PSRAM,
// ST7789 240x135). Sample caches use PSRAM only when psramFound() is true.
// Everything used here is on the board: no external wiring.
// Display, keyboard (TCA8418), and the ES8311 codec are brought up by
// M5Unified / M5Cardputer. These pins are the ones the firmware touches
// directly (the SD bus and the I2S lines into the ES8311).

#define BOARD_NAME "Cardputer ADV"

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

// Audio rate. Step timing uses rate/4, matching MothOS (16th-note steps).
static const int kSampleRate = 44100;

// Built-ins are 0..11. 63 is the Loops instrument (notes play SD loops in
// time with the project BPM). Plugin folders use 12..62.
static const int kLoopsVoice = 63;

#endif
