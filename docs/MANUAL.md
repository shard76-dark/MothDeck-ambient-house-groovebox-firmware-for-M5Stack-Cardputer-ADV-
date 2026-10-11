# MothDeck on the LilyGO TTGO T8

This is the TTGO T8 V1.8 image. It is not the Cardputer image. Do not flash one onto the other. Use the browser installer in [flash.html](flash.html) only while a T8 is plugged in. Nothing here has been flashed or listened to.

The board is a classic ESP32-WROVER: 4MB flash, 4MB PSRAM, and a microSD socket. The display is an ST7789 240×135. The keys are a 4×4 membrane with the connector on top. Wiring is in [TTGO-T8.md](TTGO-T8.md).

Two images are built. `ttgo-t8` plays through a PCM5102A. `ttgo-t8-dac` uses the 8-bit DAC on GPIO25, and keypad row R3 moves to GPIO22 on that image.

## Hear it

There is no built-in speaker. The PCM image needs the PCM5102A. The DAC image needs a line into GPIO25.

## What it plays

Built-in drums and the shorter effects are signed 16-bit at 44100 Hz in flash. Riser, downlifter, sweep, impact, siren, and drop are `/moth/sfx/<name>.wav` in the SD pack and load into PSRAM. A missing card leaves those six silent. Drums, the short effects, loops, plugins, and BLE keep running. A kit that fits is copied into PSRAM. A larger kit streams through the 16 KB cache and does not take the loop windows. Plugin samples stay within 4096 frames. Playback uses 4-point Hermite interpolation. The speaker sample is dithered by one 16-bit LSB.

Loops, the pattern grid, chord notes, and the tracker sit in PSRAM, so BLE and loops stay loaded together.

## The pad

With no function armed, the bottom twelve keys play C through B. C is the bottom-left key.

Hold F4. F1 is Pages, F2 is File, F2 again is Take, F3 is Settings. F4 leaves the menu.

File saves, loads, deletes, and picks song slots. Take is the sampler and the chain:

| Key | Action |
| --- | --- |
| G# | record the next 8192 frames of the master mix |
| A | play that take |
| A# | play it an octave higher |
| B | store the current pattern and mutes as the next scene |
| E | clear the chain |
| F | song mode |

The take is a fixed buffer. It is not taken from the loop windows, a kit, or BLE. Recording hears the tracks and not the take. A chain walks scenes at the end of each pattern. An empty chain leaves song mode alone. A song with a chain is version 5. Older songs still load.

The serial keyboard at 115200 also takes `U`, `O`, `Y`, `J`, and `P` on the Song page.

A step holds up to four notes. The first stays in the song file. The other three live in PSRAM until the next load.

## Wi-Fi

This image does not serve the card over Wi-Fi. The Cardputer 1.6.0 file page was linked on that board. On the T8, BLE and the PSRAM cache workaround already fill internal instruction RAM, and the Wi-Fi stack overflowed that RAM by 892 bytes. The drum tables are in flash, so removing them would not make the radio fit. BLE, loops, and the drums stay.

## Flash

[flash.html](flash.html) has one button for the PCM5102A image and one for the GPIO25 DAC image. Each writes a full factory image at the start of flash. There is no Launcher on this board. The application files in `releases/` are the same bytes, named `mothdeck-ttgo-t8-1.7.0-test.bin` and `mothdeck-ttgo-t8-dac-1.7.0-test.bin`.

Unzip the SD pack onto the card root so `/moth/sfx/` is present.
