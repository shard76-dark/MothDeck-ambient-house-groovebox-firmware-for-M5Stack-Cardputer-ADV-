# LilyGO TTGO T8 V1.8

MothDeck on a LilyGO TTGO T8 V1.8. This is a classic ESP32-WROVER: dual core, 4MB flash, 4MB PSRAM, a microSD slot, and no screen or keyboard. The Cardputer ADV image is a different build. Do not flash a TTGO binary onto a Cardputer, or the other way around.

This port is a test image. It is not a release. The sequencer, piano roll, BLE MIDI, loops, and SD patch plugins are the same 1.2.x features. On this board the loop windows, sample buffers, pattern grid, and the tracker object sit in PSRAM, so BLE and loops stay loaded together. There is no need to unload BLE to open loops.

A board with nothing attached still plays, follows MIDI clock, and takes notes over BLE MIDI. The audio task reads the radio. The screen and the keys are optional.

## Build

PlatformIO env `ttgo-t8` is the PCM5102A image. `ttgo-t8-dac` is the same firmware with the ESP32's internal 8-bit DAC on GPIO25 instead of I2S.

```bash
pio run -e ttgo-t8
pio run -e ttgo-t8-dac
pio run -e cardputer-adv
```

The app image is `.pio/build/ttgo-t8/firmware.bin`. Flash it over the board's USB serial port (CP2104 or CH9102, 115200). This is a factory app at `0x10000` in `partitions/ttgo_t8_4MB.csv`. There is no Launcher on this board.

```bash
pio run -e ttgo-t8 -t upload
```

Optional compile flags, added to `build_flags` in `platformio.ini`:

| Flag | What it changes |
| --- | --- |
| `-DMOTHDECK_HAS_TFT=1` | Drive the optional ST7789. The stock image leaves this off. |
| `-DMOTHDECK_INTERNAL_DAC=1` | GPIO25 DAC. This is what `ttgo-t8-dac` sets. |
| `-DPIN_I2S_BCLK=26` `-DPIN_I2S_WS=27` `-DPIN_I2S_DOUT=22` | PCM5102A pins. |
| `-DPIN_TFT_SCK=18` `-DPIN_TFT_MOSI=23` `-DPIN_TFT_CS=5` `-DPIN_TFT_DC=21` `-DPIN_TFT_RST=19` `-DPIN_TFT_BL=32` | ST7789 pins. |
| `-DMOTHDECK_TFT_OFFSET_X=40` `-DMOTHDECK_TFT_OFFSET_Y=53` `-DMOTHDECK_TFT_ROTATION=1` | Panel window. Rotation 1 is 240×135 landscape. |
| `-DPIN_MX_R0=` `-DPIN_MX_R1=` `-DPIN_MX_C0=` `-DPIN_MX_C1=` `-DPIN_MX_C2=` `-DPIN_MX_C3=` | Button matrix. All six must be set, or the matrix stays off. |
| `-DPIN_BTN_A=0` | Use GPIO0 as a button after boot. |

Leave GPIO 6–11 alone. Those are the flash bus. Leave GPIO12 alone. It is a strapping pin and must stay low for this 3.3V flash.

## Power and the USB port

USB is a serial adapter, not native USB. The keyboard protocol below is bytes on that UART at 115200 8N1. The boot log is the same port: `MothDeck 1.2.2 TTGO T8`.

## microSD

The socket is the ESP32's 1-bit SDMMC pins. The firmware uses the SDMMC host, not SPI. On this chip the PSRAM cache workaround occupies HSPI, so an SPI card on those pins would fight PSRAM.

| SD pin | GPIO | Notes |
| --- | --- | --- |
| CLK | 14 | SDMMC clock |
| CMD | 15 | SDMMC command. GPIO15 is also a strapping pin. It is the board's own SD pin. |
| D0 | 2 | SDMMC data. GPIO2 is a strapping pin. Do not add a pull-down. |
| D3 | 13 | Held high with the internal pull-up so the card stays in SD mode. |

Some V1.8 schematics leave the SD pull-ups unconnected. If the card does not mount, add 10k from CLK, CMD, D0, and D3 to 3.3V.

Songs, kits, loops, and plugin folders use the same `/moth` layout as the Cardputer. Unzip `releases/mothdeck-sd-pack.zip` onto the card root.

## PCM5102A (default audio)

Line level into a powered speaker or a headphone amp. This module is not a speaker amplifier.

| PCM5102A | GPIO | Notes |
| --- | --- | --- |
| BCK | 26 | Bit clock |
| LRCK | 27 | Word select |
| DIN | 22 | Data into the DAC |
| SCK | GND | No master clock. Tie the module SCK pin to ground. |
| VIN | 5V or 3.3V | Use 5V if the module silkscreen says VIN. Use 3.3V only on a module that is labeled for it. |
| GND | GND | Common ground with the T8 |
| LOUT / ROUT | amp | Line out |

FLT, DEMP, and XSMT are usually tied on the module. Leave them.

## Internal DAC (fallback image)

`ttgo-t8-dac` plays 8-bit audio on GPIO25 (DAC1). It does not drive the PCM5102A pins.

Do not connect a speaker directly. Use a series 1k resistor, then 10nF to ground, then a series capacitor of about 10µF into an amplifier. This is a monitor output. It is not a hi-fi output.

## Optional ST7789 240×135

The stock `ttgo-t8` image does not touch these pins. Rebuild with `-DMOTHDECK_HAS_TFT=1` to draw the same 240×135 layout as the Cardputer (piano roll, step strip, settings). The sprite is still rgb332 and is copied eight rows at a time.

The panel sits on VSPI. The SD card uses the SDMMC host, so the two do not share a bus.

| ST7789 | GPIO |
| --- | --- |
| SCK | 18 |
| MOSI | 23 |
| CS | 5 |
| DC | 21 |
| RST | 19 |
| BL | 32 |
| GND | GND |
| VCC | 3.3V |

The driver window is a 135×240 panel inside a 240×320 framebuffer, offset X 40 and Y 53, rotation 1. If the picture is shifted, change `MOTHDECK_TFT_OFFSET_X`, `MOTHDECK_TFT_OFFSET_Y`, or `MOTHDECK_TFT_ROTATION` and rebuild.

## Keys

### Serial

Type into the USB serial monitor at 115200. The UI lowercases letters.

| Bytes | Key |
| --- | --- |
| `Enter` (`\r` or `\n`) | Enter. On Play, this toggles the piano roll and the 16-step strip. |
| Tab (`\t`) | Next page |
| Backspace (`\b` or DEL `0x7F`) | Backspace |
| Space | Play / stop |
| `` ` `` | Page list on Play, or back to Play |
| Printable ASCII | That key (`z` `x` `c` play notes, `s` saves on the Song page, and so on) |
| ESC (`0x1B`) then one byte | Fn plus that key. Fn+`;` is ESC then `;`. |

Shift, Ctrl, and Alt are not sent this way. Hold-to-exit is not on the serial port.

### Button matrix

A 2×4 matrix, active low. Columns use `INPUT_PULLUP`. Rows idle high and one row is driven low to scan. All six pins default to `-1`, which leaves the matrix off. Set every pin or none of them.

| | C0 | C1 | C2 | C3 |
| --- | --- | --- | --- | --- |
| R0 | `z` | `x` | `c` | `v` |
| R1 | space | `1` | `p` | `/` |

An example that stays clear of the SD socket, the PCM5102A, and GPIO25, when the TFT is not fitted:

```
-DPIN_MX_R0=33 -DPIN_MX_R1=32 -DPIN_MX_C0=4 -DPIN_MX_C1=16 -DPIN_MX_C2=17 -DPIN_MX_C3=21
```

GPIO34, GPIO35, GPIO36, and GPIO39 are input-only and have no internal pull-up. Do not use them as matrix columns unless you add external pull-ups. GPIO35 is the battery sense pin.

`PIN_BTN_A` defaults to off. GPIO0 is the ROM download strap, so it is not a button during reset. After boot, `-DPIN_BTN_A=0` reads it. Holding it does not exit to Launcher: this image has no Launcher slot.

## Battery

GPIO35, about a 2:1 divider into the 3.3V ADC. The percent runs from 3300 mV to 4200 mV. Below 3300 mV the Settings row says `n/a`. Some V1.8 boards use a different divider. Treat the number as a guide.

## BLE, loops, and patterns

The radio starts at boot, before the sprite and before the speaker DMA, the same resident-stack rule as the 1.2.2 Cardputer test. This chip's Arduino build uses Bluedroid. The Cardputer build uses NimBLE. Advertising still follows the stored switch, and the default is off. The air name is `Mothdeck`. Rec starts off. Turning BLE on stops Rec. It does not unload loops, and it does not ask to save the song.

Loop windows are the same shape as the Cardputer (five slots, 1024 frames, double buffered, 20480 bytes) and they are allocated in PSRAM first. Sample instruments use the same allocator, with a 48000-frame cap when PSRAM is present. The pattern grid is 4 × 256 × 2 bytes (2048) plus a copy buffer of 4 × 128 × 2 bytes (1024). Both are allocated in PSRAM. If that allocation fails, the firmware uses static internal arrays and still boots. The tracker object is about 68KB and is allocated in PSRAM when audio starts, because the classic ESP32 DRAM segment left beside Bluedroid is too small for it.

Unload BLE is still in Settings. It is not required to load loops on this board. It takes effect on the next boot, same as the Cardputer.

## Headless

With `MOTHDECK_HAS_TFT` left at 0, nothing is drawn. Serial keys and BLE MIDI still work. Connect from a host that speaks BLE MIDI, send clock and notes, and the pattern plays out of the PCM5102A (or GPIO25 on the DAC image). Plug in the line out before you raise the volume.
