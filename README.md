# MothDeck

MothDeck is a 4-track sampler and tracker for the [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv). It is a sibling of [MothOS](https://github.com/MothSynths): same song file, same BLE MIDI map, same voice and effect model, redrawn for the ADV's 240×135 colour screen and 56-key keyboard.

Everything it uses is on the board. There is no wiring. The speaker, headphone jack, display, keyboard, and microSD are brought up by M5Unified. A card is optional; the built-in drums, sound effects, and instruments are synthesized at build time and are not recordings of anyone else's samples.

## Features

- 4 tracks, 4 patterns, up to 256 steps (16th notes). Step timing matches MothOS: 44100 Hz, one step is `11025 / beats-per-second` samples.
- 12 built-in instruments: drums, sound effects, sine, square, saw, triangle, organ, pluck, bell, flute, bass, pad.
- Per-track delay, low pass, phaser, retrig, overdrive, pitch, whoosh, and chord.
- BLE MIDI peripheral, default name `MothSynth` (change it on the Settings page). The name is stored in NVS.
- Song slots `/moth/slot1.mos` … `slot4.mos`. A song that stays on the built-in instruments is a 3155-byte MothOS version 1 file. Plugins and loops bump the file to version 2.
- Instrument folders and loop libraries loaded from the card, assigned per track, remembered in the song, and ignored cleanly when the file is missing or bad.
- Audio runs on its own FreeRTOS task on core 1. The UI never writes the tracker. The speaker is fed 256-frame blocks only while fewer than two blocks are queued.
- Battery percent and speaker volume on the status line. The ADV reads the pack on GPIO10.

## Build

PlatformIO, pioarduino 55.03.312-1 (Arduino-ESP32 3.3.12). Libraries are pinned: M5Unified 0.2.25, M5GFX 0.2.32, M5Cardputer 1.1.1.

```bash
pip install platformio
./build.sh
```

`build.sh` regenerates the built-in waveforms, the example card tree, runs the host tests, and writes:

- `releases/mothdeck-cardputer-adv.bin` — install this from Launcher
- `releases/mothdeck-cardputer-adv-full.bin` — USB flash only; it replaces Launcher
- `releases/SHA256SUMS`

Host tests alone:

```bash
bash tools/run_tests.sh
```

The dev environment `cardputer-adv-dev` adds serial logs for the card and the speaker.

Flash mode is `qio_opi` so the octal PSRAM is usable for samples. Launcher's own Cardputer environment is `qio_qspi`. If a Launcher-installed image reboots before the UI, that PSRAM mode is the first thing to suspect; the application image itself does not change the bootloader Launcher already flashed.

## Install from Launcher

See [releases/INSTALL.md](releases/INSTALL.md) for the button-by-button steps. Short version: copy `mothdeck-cardputer-adv.bin` (the file that starts with `E9`) to a FAT32 card, boot Launcher, press Enter on the splash, and install the file from the SD browser.

### Why Exit works this way

Launcher lives in an `APP_TEST` partition and points `otadata` at the installed app (`launcherPartitionSetOtaBoot`). `esp_ota_set_boot_partition()` cannot select that test slot. In ESP-IDF 5.5 the call only erases `otadata` for a factory image; any other subtype is reduced to its low nibble, and `0x20` (TEST) becomes OTA slot 0. Calling it would boot the wrong image.

Exit erases `otadata`, which is what `launcherPartitionClearOtaBoot` does, then restarts. It refuses to restart when no other app partition exists, so a standalone USB flash does not reboot-loop if Esc is held at boot.

Hold Esc (`` ` ``) or the front button for about 0.7 seconds, or confirm the Exit page. The same hold during MothDeck's own boot runs the exit before the audio task starts. After the restart, press Enter on Launcher's splash ("Press the button to enter the Launcher!") to stay there. Doing nothing on that splash starts the installed app again whenever the boot selection still names it.

## Keyboard

Esc is the `` ` `` key. Arrows are the silkscreen keys while Fn is held: `;` up, `,` left, `.` down, `/` right. Fn+`-` and Fn+`=` change speaker volume from any page.

| Keys | Action |
| --- | --- |
| Tab / Shift-Tab | Next / previous page |
| Space | Play / stop |
| `` ` `` tap | Page list on Play, or back to Play |
| `` ` `` or front button, hold | Exit to Launcher |
| 1–4 | Select track. On Song, select a slot |
| 5–8 | Select pattern |
| 9 / 0 | Previous / next instrument on the Instrument page, otherwise cycle the BPM slot |
| `-` / `=` | Nudge the current BPM slot by 1 |
| Ctrl+1–4 | Clear that track |
| Ctrl+5–8 | Clear that pattern |
| Ctrl+N | New song |

### Play

The bottom letter row is a piano for the current octave. `Z X C V B N M ,` are C D E F G A B C. `S D G H J` are the black keys. `Q` through `]` is the next octave, chromatic. Shift adds an octave, Alt adds another, Opt subtracts one, clamped to four octaves (MIDI C2–B5).

| Key | Action |
| --- | --- |
| A | Low pass |
| F | Retrig |
| K | Phaser |
| L | Echo |
| `;` | Note length |
| `'` | Sampler mode |
| `\` | Octave |
| `.` | Copy pattern |
| `/` | Paste pattern |
| Backspace | Clear the step under the cursor |

Played keys are also sent as BLE MIDI note-on on the selected track's channel.

### Other pages

- **Instrument.** Fn-arrows move. Enter assigns the row to the selected track and loads it if it is a plugin. `R` rescans the card. `9` / `0` move as well.
- **Mixer.** 13 rows for the selected track: volume, mute, solo, drive, low pass, retrig, wobble, echo, arp, whoosh, pitch, envelope, note length. Fn-left / Fn-right or Enter change the row.
- **Song.** `S` save, `L` load, `X` delete, `T` slot status, `N` new song (length cycles 32/64/96/128), `C` copy, `V` paste, `G` paste all, `M` song/pattern, `H` master, `B` next BPM slot. Enter loads the selected slot.
- **Loops.** Enter launches onto the selected track with the current quantize. `A` auditions. `Q` cycles quantize (now, beat, bar). `S` stops the track's loop. `R` rescans. Fn-left / Fn-right change library.
- **MIDI.** Connection, advertised name, and the map below. `E` jumps to the name field.
- **Settings.** Speaker volume, backlight, BLE name (Enter to edit, Enter to apply and restart advertising), battery, heap, card.
- **Exit.** Enter confirms.

## MIDI

Advertised as a BLE MIDI peripheral. Apple-style timestamped packets, same codec as MothOS.

| Message | Map |
| --- | --- |
| Note on/off, channel 1–4 | Tracks 1–4. Notes 36–83 are C2–B5 |
| CC 0 / CC 32 bank | Instrument. MSB 0–11 built-in, 12–63 plugin id. A non-zero LSB still spreads the 14-bit value across the 12 built-ins, as MothOS does |
| CC 1 mod | Low pass |
| CC 7 / CC 39 volume | 14-bit, mapped to voice volume 0–8 |
| Pitch bend | Per channel, ± the voice pitch ratio |

Keypad notes are sent back out as note-on on the track channel. Bank and volume are sent when the matching control changes.

## Card layout

```
/moth/slot1.mos … slot4.mos
/moth/instruments/<folder>/manifest.txt
/moth/instruments/<folder>/sample.wav    (or sample.raw)
/moth/loops/<library>/manifest.txt
/moth/loops/<library>/*.wav              (or raw, or .pat)
```

`sd-card-example/moth` is generated by `python3 tools/gen_sd_examples.py`. Copy that `moth` directory to the card root.

```bash
python3 tools/wav_to_instrument.py take.wav card/moth/instruments/take --name take --root 60
python3 tools/wav_to_loop.py --name house --bpm 120 --bars 1 --tags drums \
    card/moth/loops/house kick.wav hats.wav
```

The instrument and loop manifests, the version 2 song tail, and the pattern file are specified in [docs/FORMATS.md](docs/FORMATS.md).

Plugins get ids 12–63. A song stores the folder name. If that folder is missing at load, the track falls back to the drum bank. Loop PCM is not evicted while a track holds it (six slots). Instrument PCM may recycle the oldest slot. Samples are capped (48k frames with PSRAM, 8k without; loops 120k / 16k) and prefer PSRAM. A corrupt manifest or WAV is reported on the page and skipped.

Loops resample to the project BPM by advancing the source faster when the project is faster. Launch can wait for the next beat or the next bar.

## Project layout

```
include/     headers, including the codecs the host tests compile
src/         firmware
test/        host tests (MIDI, song v1/v2, manifests, WAV, synth render)
tools/       sample generator, WAV converters, test runner, build helper
docs/        file formats
sd-card-example/
partitions/  development table, not used by Launcher
releases/    application image, full-flash image, checksums, install notes
```

## Hardware notes

Cardputer ADV: ESP32-S3, 8MB flash, octal PSRAM, ST7789 240×135, TCA8418 keyboard at I2C `0x34` (SDA 8, SCL 9), ES8311 on the same I2C bus, speaker amp enable GPIO42, microSD on a separate SPI (SCK 40, MISO 39, MOSI 14, CS 12) with GPIO5 held high before mount, battery ADC GPIO10, front button GPIO0. Grove (GPIO1 / GPIO2) is left unused. The IMU is left off.

## Licence

MIT. Copyright (c) 2024 MothSynths. See [LICENSE](LICENSE).
