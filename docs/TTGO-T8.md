# LilyGO TTGO T8 V1.8

MothDeck on a LilyGO TTGO T8 V1.8. This is a classic ESP32-WROVER: dual core, 4MB flash, 4MB PSRAM, and a microSD slot. The board has no panel and no keyboard of its own. The display for this port is a confirmed ST7789 240×135, and the stock image drives it. The keys are a standard 4×4 membrane, mounted with the connector on top. The Cardputer ADV image is a different build, on `main`. Do not flash a TTGO binary onto a Cardputer, or the other way around.

This port lives on the long-lived `ttgo-t8` branch. It is not merged into `main`. The pre-release tag is `ttgo-t8-v1.2.2`. The sequencer, piano roll, BLE MIDI, loops, and SD patch plugins are the same 1.2.2 features, plus chords (below) and the 4×4 keypad. On this board the loop windows, sample buffers, pattern grid, chord notes, and the tracker object sit in PSRAM, so BLE and loops stay loaded together. There is no need to unload BLE to open loops. Incoming MIDI notes only play while Rec is off. Space turns Rec on, and then they record.

## Chords

A step on a track holds up to four notes. The first note stays in the pattern word, the same place a single note has always lived. The other three are stored beside it in PSRAM.

With Rec on, MIDI notes that land in the same step stack. The same pitch is not stored twice. A fifth note is left out. Rec off still only plays the note and does not write it. The piano roll draws each note as its own block in that column. A key on an empty column writes the first note. A key on another row adds a note. Backspace removes the note under the cursor and, if that was the first note, the next one takes its place. `;` changes the length of the note under the cursor. The 16-step strip still shows the first note.

Playback sounds the notes together. Drums, sound effects, and one-shot samples add extra hits. The built-in synths add oscillators and run them through the same track effect. A loop track keeps a single loop.

A song file still stores one note per step, the first of the chord. Songs from the Cardputer, and older TTGO songs, load as single notes. Extra chord notes stay in the pattern until a song is loaded. Saving a song writes that first note and leaves the others out of the file.

BLE MIDI still plays, follows clock, and takes notes if the panel is unplugged. Rebuild with `-DMOTHDECK_HAS_TFT=0` for an image that does not drive the panel pins.

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
| `-DMOTHDECK_HAS_TFT=0` | Headless image. The stock image drives the ST7789. |
| `-DMOTHDECK_INTERNAL_DAC=1` | GPIO25 DAC. This is what `ttgo-t8-dac` sets. |
| `-DPIN_I2S_BCLK=26` `-DPIN_I2S_WS=27` `-DPIN_I2S_DOUT=22` | PCM5102A pins. |
| `-DPIN_TFT_SCK=18` `-DPIN_TFT_MOSI=23` `-DPIN_TFT_CS=5` `-DPIN_TFT_DC=21` `-DPIN_TFT_RST=19` `-DPIN_TFT_BL=32` | ST7789 pins. |
| `-DMOTHDECK_TFT_OFFSET_X=40` `-DMOTHDECK_TFT_OFFSET_Y=53` `-DMOTHDECK_TFT_ROTATION=1` | Panel window. Rotation 1 is 240×135 landscape. |
| `-DPIN_MX_R0=4` `-DPIN_MX_R1=33` `-DPIN_MX_R2=0` `-DPIN_MX_R3=25` `-DPIN_MX_C0=34` `-DPIN_MX_C1=36` `-DPIN_MX_C2=39` `-DPIN_MX_C3=35` | 4×4 keypad. These are the defaults. Any one set to `-1` turns the pad off. On the DAC image, `PIN_MX_R3` is GPIO22. |
| `-DPIN_BTN_A=` | Extra button, default off. GPIO0 is keypad row R2, so leave this unset. |

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

## ST7789 240×135

This panel is the confirmed display. The stock `ttgo-t8` and `ttgo-t8-dac` images draw the same 240×135 layout as the Cardputer (piano roll, step strip, settings). The sprite is still rgb332 and is copied eight rows at a time.

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

Shift, Ctrl, and Alt are not sent as their own bytes. ESC (`0x1B`) then one byte is Fn plus that key, the same chords the Cardputer keyboard uses. The serial port still types the whole alphabet, so every Cardputer key still works from the USB monitor, including with the panel unplugged and with the pad off. Hold-to-exit is not on the serial port. This board has no Launcher.

### 4×4 keypad

A standard 4×4 membrane, active low, no diodes. The ribbon is on the left when the printed legend is upright. Mount it rotated 90° clockwise so the ribbon is on top. Firmware rows are the membrane's columns, and firmware columns are the membrane's rows: membrane R0 is wired as C0, R1 as C1, R2 as C2, R3 as C3, and membrane C0 is wired as R0, C1 as R1, C2 as R2, C3 as R3.

After that rotation the top-left key, as you look at it, is key 0. The printed legend in that orientation is:

```
*  7  4  1
0  8  5  2
#  9  6  3
D  C  B  A
```

The scan index is `row * 4 + (3 - col)` on the firmware row and column. Key 0 is the top-left `*`. The pins are compile-time `PIN_MX_R0`..`PIN_MX_R3` and `PIN_MX_C0`..`PIN_MX_C3`. Any one of them set to `-1` leaves the pad off.

The menu is the MothSynth 4×4, not a map of the Cardputer letters. Tap a function key on the top row, release it, then tap the key that runs that command. The footer names the armed function. Hold the same function key to open the MothDeck pages that the original sixteen commands do not cover. Press that function key again to leave. One key at a time: a third key on a diode-less pad ghosts.

```
                 connector / pins on top
        0 F1     1 F2      2 F3      3 F4
        4 G#     5 A       6 A#      7 B
        8 E      9 F      10 F#     11 G
       12 C     13 C#     14 D      15 D#
```

With no function armed, the bottom twelve keys play C through B in that layout. C is the bottom-left key. Octave is F1, then F1–F4 for octaves 0–3, which covers C2–B5 together with the roll cursor. The Cardputer's upper `q`–`]` row is not a separate pad row.

Rows idle high. One row goes low, the columns are read, then the row goes high again. A press is a column reading low. The mask has to sit still for about 20 ms. A function key that is still down after about half a second opens its extended menu. One event is delivered per poll.

#### Live, and the four function menus

These match the original MothOS keypad. The extended menus are below.

| Key | Live | F1 then | F2 then | F3 then | F4 then |
| --- | --- | --- | --- | --- | --- |
| F1 | arm F1 | octave 0 | mute | track 1 | note length 1 |
| F2 | arm F2 | octave 1 | volume | track 2 | note length 2 |
| F3 | arm F3 | octave 2 | overdrive | track 3 | note length 3 |
| F4 | arm F4 | octave 3 | solo | track 4 | play / stop / rec |
| C | C | Drums | effects off | clear pattern 1 | new song, 2 bars |
| C# | C# | SFX | low pass | clear pattern 2 | new song, 4 bars |
| D | D | Sine | retrig | clear pattern 3 | master volume |
| D# | D# | Square | wobble | clear pattern 4 | song / pattern |
| E | E | Saw | echo | pattern 1 | copy pattern |
| F | F | Tri | chord | pattern 2 | paste pattern |
| F# | F# | Organ | whoosh | pattern 3 | paste all |
| G | G | Pluck | pitch bend | pattern 4 | sampler |
| G# | G# | Bell | fade out | clear track 1 | BPM slot 1 |
| A | A | Flute | fade in | clear track 2 | BPM slot 2 |
| A# | A# | Bass | no fade | clear track 3 | BPM slot 3 |
| B | B | Pad | loop envelope | clear track 4 | BPM slot 4 |

F4 then F4 starts and stops, and starting arms Rec, the same as Space on the Cardputer. While a pattern is already playing, Rec in the Settings menu below stores notes or only plays them. A new song from F4 then C or C# clears the patterns. F1's twelve instruments are the built-ins. Plugins, patches, and the Loops instrument are in the Sound menu.

The save question (`MIDI mode: loops off`) uses D for yes, D# for no, and F1 to leave BLE off.

#### Hold F1 — Edit and Sound

Hold F1. The roll is on screen. F1 leaves. F2 is the cursor page. F3 is Sound. F4 enters note entry.

Cursor page:

| Key | Action |
| --- | --- |
| G# | one step earlier |
| A | one step later |
| A# | cursor up a semitone |
| B | cursor down a semitone |
| E | delete the note under the cursor |
| F | cycle that note's length, 1–4 steps |
| F# | octave down |
| G | octave up |
| C | previous bar |
| C# | next bar |
| D | copy the previous step's first note onto the cursor |
| D# | piano roll / 16-step strip |

Note entry (F4 from the cursor page) writes a chord the way the roll does: each of the twelve note keys adds that pitch at the cursor, and a second pitch in the same column stacks. F2 and F3 move one step earlier or later. F1 returns to the cursor page. F4 switches the roll and the strip. Delete and length stay on the cursor page, so a column can be built and then trimmed.

Sound (F3 from Edit). The Instrument page is on screen. The list is every built-in, then Loops, then each folder under `/moth/instruments`, including patches.

| Key | Action |
| --- | --- |
| G# | previous row |
| A | next row |
| A# | assign that row to the selected track |
| B | rescan instruments, kits, and loops |
| E | previous drum kit |
| F | next drum kit |
| F# | sampler mode |
| G | show the Instrument page |
| C | Loops instrument on this track |

#### Hold F2 — FX and Mixer

Hold F2. F2 leaves. F1 is the FX page. F3 is the Mixer page.

FX. G# and A move through every insert row: filter, cutoff, resonance, delay, feedback, mix, reverb, crush, drive, chorus, tremolo, scale, root, arp, glide, osc2, blend, and coarse. A# lowers the value, B raises it. E and F select the previous and next track.

Mixer. G# and A raise and lower the track fader (0–8). A# mutes. B solos. E and F select the track.

#### Hold F3 — Loops

Hold F3. The Loops page is on screen. F3 leaves.

| Key | Action |
| --- | --- |
| G# | previous entry |
| A | next entry |
| A# | previous library |
| B | next library |
| E | audition |
| F | launch onto the selected track |
| F# | stop the loop on that track |
| G | quantize: now, beat, bar |
| C | rescan loops |

#### Hold F4 — Pages, file, and settings

Hold F4. F4 leaves. F1 is Pages, F2 is File, F3 is Settings.

Pages. One key opens that screen:

| Key | Page |
| --- | --- |
| G# | Play |
| A | Instrument |
| A# | FX |
| B | Mixer |
| E | Song |
| F | Loops |
| F# | MIDI |
| G | Settings |
| C | Exit. This image has no Launcher, so the footer says `Launcher not found` |
| C# | confirm Exit, only when a Launcher image is present |
| D | previous page |
| D# | next page |

File. The Song page is on screen. Slots are `/moth` song slots 1–4.

| Key | Action |
| --- | --- |
| G# | save |
| A | load |
| A# | delete the slot |
| B | slot status |
| E F F# G | select slot 1, 2, 3, 4 |
| C | new song, one bar longer, wrapping from 8 back to 1 |
| C# | new song at the current length |
| D | copy pattern |
| D# | paste pattern |

Settings.

| Key | Action |
| --- | --- |
| G# | speaker down |
| A | speaker up |
| A# | brightness down |
| B | brightness up |
| E | BLE off |
| F | BLE on |
| F# | unload BLE, or load it after an unload. Takes effect on the next boot |
| G | cancel a pending unload |
| C | pattern one bar shorter |
| C# | pattern one bar longer, 1–8 |
| D | Rec. While the pattern is playing, this stores notes or only plays them |
| D# | BLE name |

The name editor replaces the menu until you finish it. F1 cancels. F2 cycles four letter pages (`abcdefghijkl`, `mnopqrstuvwx`, `yz0123456789`, `-_`). The twelve note keys append the matching character. F3 deletes one character. F4 stores the name and restarts advertising when the radio is on. The screen shows the same name row as Settings.

#### Wiring

Each column needs an external 10k to 3.3V. GPIO34, GPIO35, GPIO36, and GPIO39 are input-only and have no internal pull-up. Leave GPIO6–11, GPIO12, GPIO16, and GPIO17 alone (flash, the 3.3V-flash strap, and PSRAM). Do not use GPIO1 or GPIO3: the USB UART drives them.

| Membrane, legend upright | Firmware | PCM image | DAC image | Notes |
| --- | --- | --- | --- | --- |
| C0 | R0 | GPIO4 | GPIO4 | Output, idle high |
| C1 | R1 | GPIO33 | GPIO33 | Output, idle high |
| C2 | R2 | GPIO0 | GPIO0 | Output, idle high. GPIO0 must be high at reset or the chip enters download mode. The scan only pulls it low after boot. |
| C3 | R3 | GPIO25 | GPIO22 | Output, idle high. This is the one wire that moves. The PCM image uses GPIO25. The DAC image uses GPIO25 for audio, so R3 moves to GPIO22. |
| R0 | C0 | GPIO34 | GPIO34 | Input. External 10k to 3.3V |
| R1 | C1 | GPIO36 | GPIO36 | Input. External 10k to 3.3V |
| R2 | C2 | GPIO39 | GPIO39 | Input. External 10k to 3.3V |
| R3 | C3 | GPIO35 | GPIO35 | Input. External 10k to 3.3V. This is also the battery pin. The percent is not shown while the pad uses it. |

At boot the rows sit high. If any column reads low, or any key is down, the pad is turned off (`UI: keypad off`) and every one of the eight pins is left as an input. Release the keys before reset. Fit the four 10k pull-ups. GPIO35 with only the battery divider sits near 2V and reads low, so the pad stays off until the 10k is fitted. With the 10k fitted the column reads high, the pad starts (`UI: keypad`), and the battery reading is wrong, so the firmware leaves the percent as `n/a`.

`PIN_BTN_A` stays off. This image has no Launcher slot.

## Battery

GPIO35 is about a 2:1 divider into the 3.3V ADC. The percent would run from 3300 mV to 4200 mV. The stock keypad uses GPIO35 as column C3 with a 10k to 3.3V, so the divider is no longer a battery reading. Settings says `n/a`. Move `PIN_MX_C3` and `PIN_BATT_ADC` apart, and rebuild, if you want the percent back. Some V1.8 boards use a different divider. Treat the number as a guide.

## BLE, loops, and patterns

The radio starts at boot, before the sprite and before the speaker DMA, the same resident-stack rule as the 1.2.2 Cardputer test. This chip's Arduino build uses Bluedroid. The Cardputer build uses NimBLE. Advertising still follows the stored switch, and the default is off. The air name is `Mothdeck`. Rec starts off. Turning BLE on stops Rec. It does not unload loops, and it does not ask to save the song.

Loop windows are the same shape as the Cardputer (five slots, 1024 frames, double buffered, 20480 bytes) and they are allocated in PSRAM first. Sample instruments use the same allocator, with a 48000-frame cap when PSRAM is present. The pattern grid is 4 × 256 × 2 bytes (2048) plus a copy buffer of 4 × 128 × 2 bytes (1024). Both are allocated in PSRAM. If that allocation fails, the firmware uses static internal arrays and still boots. The tracker object is about 68KB and is allocated in PSRAM when audio starts, because the classic ESP32 DRAM segment left beside Bluedroid is too small for it.

Unload BLE is still in Settings. It is not required to load loops on this board. It takes effect on the next boot, same as the Cardputer.

## Panel unplugged

The stock image expects the ST7789. Playback does not depend on it: the audio task still follows BLE MIDI clock and notes, and serial keys still work. A build with `-DMOTHDECK_HAS_TFT=0` leaves the panel pins alone. Plug in the line out before you raise the volume.
