# MothDeck

MothDeck is ambient-house groovebox firmware for the [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) (Stamp-S3A, ESP32-S3): drums, a per-track FX insert, a four-track mixer, SD kits and loops, and BLE MIDI. It is a sibling of [MothOS](https://github.com/MothSynths): same song file, same BLE MIDI map, same voice and effect model, redrawn for the ADV's 240×135 colour screen and 56-key keyboard.

## Download the binary

**Use earphones or headphones.** Plug them into the 3.5 mm jack on the side of the Cardputer ADV. This firmware does not play through the internal speaker. The jack is how you hear the drums, the kits, and the loops. Unplugging the earphones does not switch the sound to the speaker.

**Latest Cardputer ADV image (flash this):** [`releases/mothdeck-cardputer-adv.bin`](releases/mothdeck-cardputer-adv.bin)

- Version 1.1.0
- SHA-256: `f8d9ab44082ab7ff5921dca9d30eb68ed8a440430bb019df31e4d82212569a4a`
- Size: 1,134,080 bytes (app image, magic `E9`)
- BLE MIDI pairs with an MPC Live II, patterns run from 1 to 8 bars, and playback stays up if the radio cannot start. Notes are in [`releases/CHANGELOG.md`](releases/CHANGELOG.md).
- 1.1.1 is unreleased. It adds a Settings BLE on/off switch and an Unload BLE row, and it fixes SD loops that only opened two files after BLE started. The image above is still 1.1.0.
- Install notes: [`releases/INSTALL.md`](releases/INSTALL.md) — copy the `.bin` to a FAT32 card, install from [Launcher](https://github.com/bmorcelli/Launcher)
- Optional SD kits/loops: [`releases/mothdeck-sd-pack.zip`](releases/mothdeck-sd-pack.zip) · checksums: [`releases/SHA256SUMS`](releases/SHA256SUMS)


The public source and releases live in this repository. Flash the application image with [releases/INSTALL.md](releases/INSTALL.md). The same overview, with HTML meta tags for search and link previews, is [docs/index.html](docs/index.html).

## Features

- Ambient house drums on the M5Stack Cardputer ADV: twelve distinct hits at the recorded pitch (kick, rim, snare, clap, hats, toms, shaker, ride, snap, crash)
- Groovebox session on the ESP32-S3: 4 tracks, patterns of 1–8 bars (16–128 steps of 16th notes)
- Per-track FX (filter, delay, reverb, bitcrush, drive, chorus, tremolo) and a mixer with volume, mute, and solo
- SD kits, plugin instruments, and loops. Loops stream from the card instead of filling RAM
- BLE MIDI peripheral (notes, clock, transport, bank, volume, pitch bend), named MothDeck in the advertising packet
- Built-in sounds are unsigned 8-bit mono at 22050 Hz so they fit the 8MB flash and the internal SRAM
- One-second amber moth splash, then the Play page, with a checked exit back to Launcher

This repository is the Cardputer ADV firmware. The PlatformIO target is `cardputer-adv`. `cardputer-adv-dev` is that same firmware with SD and speaker logs on USB serial. The board id in `platformio.ini` is `esp32-s3-devkitc-1` because PlatformIO has no Stamp-S3A entry; the pins in `include/BoardConfig.h` are the ADV. There is no second ADV build.

Everything it uses is on the board. There is no wiring. The headphone jack, display, keyboard, and microSD are brought up by M5Unified. The internal speaker does not play with this firmware; use earphones or headphones in the 3.5 mm jack. A card is optional. The built-in drums, sound effects, and tonal instruments are synthesized when the firmware is built. They are not recordings of anyone else's samples.

## Flash it

See [releases/INSTALL.md](releases/INSTALL.md) for the button sequence. Short version:

1. Copy `releases/mothdeck-cardputer-adv.bin` (the file that starts with `E9`) to a FAT32 card.
2. Boot [Launcher](https://github.com/bmorcelli/Launcher), press Enter on its splash, and install that file from the SD browser.
3. The next boot that follows the boot selection starts MothDeck.

`mothdeck-cardputer-adv-full.bin` is a USB flash of the whole chip. It replaces Launcher. Checksums for both images and for `mothdeck-sd-pack.zip` are in `releases/SHA256SUMS`.

Unzip `releases/mothdeck-sd-pack.zip` onto the card root when you want the extra kits, loops, and plugin instruments. You should end up with a `moth` folder at the top of the card. The built-in kit does not need the card.

## Boot

Power-on turns the backlight on, draws an amber moth on a grey panel for about one second, then draws the Play page. The splash does not wait for a key. Audio starts before the screen sprite. BLE starts after that first frame, and only when a contiguous internal block of at least 36KB is still free. The card scan is last. That order keeps playback alive if the radio cannot allocate: Space still starts the tracker. The codec is not started inside `M5.begin`, and GPIO42 is the codec data line, not an amplifier pin to drive high. Sound comes out of the 3.5 mm jack. The internal speaker stays quiet.

The moth is amber (`0xFD20`) on grey (`0x1082`). The splash marks that buffer as logical RGB565 before `pushImage`, so the moth stays amber instead of blue. The UI sprite is rgb332 (32,400 bytes) and is copied to the panel eight rows at a time. A full-frame 8-bit push grew a second DMA buffer and reset the ADV once the speaker was running. Loop windows are allocated only when a loop opens.

Hold Esc (the `` ` `` key) or the front button for about 0.7 seconds to leave for Launcher, including during that boot. Exit is offered only when the `APP_TEST` slot holds a real ESP32-S3 app image (header magic `E9`). Otherwise the menu entry is grey, the page says "Launcher not found", and the hold does not erase `otadata`. Details are in [releases/INSTALL.md](releases/INSTALL.md).

## What a session looks like

Tab moves between pages: Play, Instrument, FX, Mixer, Song, Loops, MIDI, Settings, and Exit when Launcher is present. `` ` `` on any page other than Play returns to Play. On Play, a tap of `` ` `` opens the page list. Space is play and stop, except while a menu is open.

There are 4 tracks. A pattern is 1 to 8 bars, and a bar is 4 beats of 4 sixteenth-notes, so 16 to 128 steps. A new song is 1 bar. The grid is still 256 steps, so four patterns fit up through 4 bars, three fit at 5 bars, and two fit at 6, 7, or 8 bars. Keys 5–8 select a pattern and stop at the last one that fits. Step timing matches MothOS: the mix is 44100 Hz, and one step is `11025 / beats-per-second` samples. Each track keeps its own instrument. Selecting another track recalls that track's instrument. It does not copy the previous one across.

The Play page shows the bar being edited as `B3/8`. Fn+`,` and Fn+`/` page that 16-step view. While playback is running the view follows the playhead until you page it. Settings has a Bars row; Fn+`,` and Fn+`/` on that row change the length without clearing notes. Song `N` still starts a new song, and it steps the length through 1–8 bars. A song saved with 16 steps loads as 1 bar. A song saved with 32 steps stays 2 bars. Other stored lengths round up to a whole bar, capped at 8.

Audio runs on its own task on core 1. The UI reads a snapshot. It does not write the tracker from the draw path. The speaker is fed 256-frame blocks while fewer than two blocks are queued.

## Drums, kits, and the other instruments

The twelve built-in instruments are Drums, SFX, Sine, Square, Saw, Tri, Organ, Pluck, Bell, Flute, Bass, and Pad. The row after Pad is Loops. Plugin folders from the card appear under those.

Drums are twelve different hits, one per pad, played at the recorded pitch. Changing octave plays the same piece again. It does not speed the sample up. The upper row and the lower row of the same key are the same pad.

| Key | Pad | What you hear |
| --- | --- | --- |
| C | Kick | Sub with a click |
| C# | Rim | Short stick |
| D | Snare | Noise snare |
| D# | Clap | Layered clap |
| E | Closed hat | Bright short noise |
| F | Open hat | Longer hat |
| F# | Low tom | Pitched tom |
| G | Tom | Higher tom |
| G# | Shaker | Soft noise |
| A | Ride | Long metallic noise |
| A# | Snap | Short transient |
| B | Crash | Soft cymbal |

On the Instrument page, `,` and `/` load the previous or next kit onto Drums. Fn+`,` and Fn+`/` do the same. The line under the list is the kit name. Index 0 is always the built-in Ambient House kit in flash. Further kits are folders under `/moth/drums`. If the card has none, the toast says "No SD kits". `R` rescans instruments, kits, and loops. Enter assigns the highlighted row to the selected track only.

SFX is twelve different one-shots (riser, downlifter, zap, sweep, impact, noise, blip, siren, reverse, drop, bubbles, whoosh), one per key. Those do follow the octave. Sine through Pad are synthesized per sample: sine is a sine, square is rounded, saw is a detuned lead, triangle is a triangle, organ is drawbar sines, pluck closes a filter and decays, bell is decaying FM, flute is a slow sine with breath, bass is a sub plus detuned saws, and pad attacks slowly, stays detuned, and holds.

## Mixer and FX

These are different pages.

FX is the insert on the selected track: filter (off, low pass, high pass), cutoff, resonance, delay, feedback, mix, reverb send, bitcrush, drive, chorus, and tremolo. Fn+`;` and Fn+`.` move the row. Fn+`,`, Fn+`/`, and Enter change the value. Delay follows the BPM as 1/32, 1/16, or 1/8. Each track keeps its own insert. Inserts are saved only when one is in use, which makes the song file version 3. The Play-page keys A, F, K, and L still cycle the older 0–2 low pass, retrig, wobble, and echo on the selected track.

Mixer shows all four tracks as volume faders, with the instrument name on each and the selected track highlighted. Fn+`;` raises the selected track and Fn+`.` lowers it (0–8). Fn+`,` and Fn+`/` move between tracks. `1`–`4` jump to a track. `M` mutes the selected track and `S` solos it. Volume is stored in every song version.

## Loops

Two ways to use card audio, both reading the same `/moth/loops` libraries.

The Loops page lists libraries and entries. Enter launches the row onto the selected track. `A` auditions. `Q` cycles quantize: now, beat, or bar. `S` stops the loop on the track. `R` rescans. Fn+`,` and Fn+`/` change library.

The Loops instrument (the row after Pad, id 63) is assigned like any other instrument. Notes on that track start one of the audio loops, wrapping if there are fewer loops than keys, lined up with the current bar. Playback follows the project BPM, so a faster song advances the file faster.

A loop is not copied into the heap. Playback reads a short window from the card (1024 frames, double buffered, up to four streams, five hold slots). The window is allocated when the loop opens and freed when it closes. Launch can wait for the next beat or the next bar.

## Memory

Settings prints Free RAM as internal kilobytes still free, the largest free block, and whether BLE is advertising, connected, or off (`120k blk 80k adv`). That is the heap after audio and the sprite, not flash and not the whole chip. The Stamp-S3A has no PSRAM. BLE init is skipped when that largest block is under 36KB, and the MIDI page says so. Each pattern step in RAM is two bytes (note, octave, instrument). The song file on the card stays the unpacked 256-step layout.

Loads that do not fit say how many kilobytes they need and how many are free (`Need Nk, Mk free`). The previous kit or plugin stays selected. Without PSRAM the caps are:

| What | Limit |
| --- | --- |
| Plugin sample | 4096 frames, 4 loaded at once |
| Drum kit | 8000 frames total, 1800 per pad, one kit cache |
| Loop streams | 4 open, each a small window, not the whole file |

An allocation also leaves about 8KB of internal heap for the rest of the system. A corrupt manifest or WAV is reported on the page and skipped.

## Sample format

Built-in drums, built-in sound effects, the SD pack, and the default output of `tools/wav_to_instrument.py` and `tools/wav_to_loop.py` are unsigned 8-bit mono PCM at 22050 Hz. 128 is silence. That is about 22 KB/s. A 44.1 kHz stereo 16-bit file is about 176 KB/s. The mix stays 44100 Hz and stretches 22050 up to it. Prefer mono. A 16-bit WAV still loads, and stereo is folded to mono. `--raw` on the instrument converter writes little-endian int16 at the source rate.

Regenerate the flash tables with `python3 tools/gen_samples.py` and the card pack with `python3 tools/make_sd_pack.py`.

## Card layout

```
/moth/slot1.mos … slot4.mos
/moth/instruments/<folder>/manifest.txt
/moth/instruments/<folder>/sample.wav    (or sample.raw)
/moth/drums/<kit>/manifest.txt
/moth/drums/<kit>/*.wav
/moth/loops/<library>/manifest.txt
/moth/loops/<library>/*.wav              (or raw, or .pat)
```

The pack in `releases/mothdeck-sd-pack.zip` contains:

| Path | What it is |
| --- | --- |
| `moth/drums/808` | 808-style kit |
| `moth/drums/dusty` | Ambient kit, dulled |
| `moth/loops/house` | Four one-bar loops at 120 BPM (kick, hats, chord, sub), 44100 frames, about 44 KB each |
| `moth/instruments/ep` | Electric piano, one-shot |
| `moth/instruments/reese` | Detuned saw bass, looping |
| `moth/instruments/saw-lead` | Two detuned saws, looping |
| `moth/instruments/soft-pad` | Slow detuned sines, looping |
| `moth/instruments/house-pluck` | Short pluck, one-shot |
| `moth/instruments/air-bell` | FM bell, one-shot |

Kit pad order is kick, rim, snare, clap, hat, openhat, perc, tom, shaker, ride, snap, crash. `perc.wav` is the low tom. Each pad file is already inside the 1800-frame and 8000-frame cache. Plugin samples in the pack are 3600 frames at 22050 Hz, under the 4096-frame cap. A kit folder is `mothdeck-kit 1`, a `name=`, and exactly twelve `pad=` lines.

```bash
python3 tools/wav_to_instrument.py take.wav card/moth/instruments/take --name take --root 60
python3 tools/wav_to_loop.py --name house --bpm 120 --bars 1 --tags drums \
    card/moth/loops/house kick.wav hats.wav
```

Those two commands resample to 22050 Hz and write 8-bit mono WAV. Song, instrument, loop, kit, and pattern bytes are specified in [docs/FORMATS.md](docs/FORMATS.md).

Plugins get ids 12–62. Id 63 is Loops. A song stores the folder name. If that folder is missing at load, the track falls back to the drum bank. A song that stays on the built-in instruments is a 3155-byte MothOS version 1 file. Plugins and loops make version 2. An insert effect makes version 3.

## Keyboard

The drawings follow `src/Ui.cpp` and the 4×14 matrix in M5Cardputer `Keyboard.h`. Regenerate them with `python3 tools/make_keymap.py`. Each key shows the unshifted action. A caption on the key is the Fn action where that differs.

![Play / Tracker](docs/keymap-play.png)

![Instrument](docs/keymap-inst.png)

![FX](docs/keymap-fx.png)

![Mixer](docs/keymap-mixer.png)

![Song](docs/keymap-song.png)

![Loops](docs/keymap-loops.png)

![MIDI](docs/keymap-midi.png)

![Settings](docs/keymap-settings.png)

![Exit](docs/keymap-exit.png)

Esc is the grave key. The arrow legends are `;` up, `,` left, `.` down, `/` right, and they only move something while Fn is held, except on the Instrument page where `,` and `/` load kits by themselves. Fn+`-` and Fn+`=` change speaker volume by 12 on every page. Fn plus any other key still does that key's normal action.

Ctrl or Shift substitutes the key's shifted glyph before the UI lowercases it. Letter commands still match. Digit and punctuation commands do not: Ctrl+1–8 is `!@#$%^&*` and does not clear a track or pattern, and Shift+`,` is `<` rather than the high C. Ctrl+N still starts a new song.

| Keys | Action |
| --- | --- |
| Tab | Next page. Shift+Tab or Ctrl+Tab goes to the previous page. While the page list is open, Tab moves the highlight and leaves the list up |
| Space | Play / stop. While a menu is open, Space does nothing |
| `` ` `` tap | On Play, toggle the page list. On any other page, return to Play. While the page list, name editor, or Exit confirm is open, `` ` `` cancels that menu |
| `` ` `` or the front button, held ~0.7s | Exit to Launcher, including during boot |
| 1–4 | Select track. On Song, select a slot and report full or empty |
| 5–8 | Select pattern. On Song, these keys do nothing |
| 9 / 0 | On Instrument, previous / next instrument. Elsewhere, previous / next BPM slot |
| `-` / `=` | Nudge the current BPM slot by 1 (40–240). With Fn, speaker volume ±12 |
| Backspace | On Play, clear the step under the cursor. In the name editor, delete one character. On the page list or Exit confirm, cancel back to Play. On any other page, return to Play |
| Ctrl+N | New song at the current length, without advancing that length |

### Play

`Z X C V B N M ,` are C D E F G A B C for the current octave. Comma is the C above that octave. `S D G H J` are C# D# F# G# A#. `Q` through `]` is the next octave, chromatic. Shift adds an octave on letter notes, Alt adds another, Opt subtracts one. They stack and clamp to four octaves, MIDI C2–B5. On drums, both octaves of a key play the same pad at the recorded pitch.

| Key | Action |
| --- | --- |
| A | Low pass, cycles 0–2 |
| F | Retrig, cycles 0–2 |
| K | Wobble, cycles 0–2 |
| L | Echo, cycles 0–2 |
| `;` | Sends note-length `L` with the stored length. It does not step. The voice stores 4 minus that value. Fn+`;` moves the page list up when the list is open |
| `'` | Toggle sampler mode |
| `\` | Cycle the octave |
| `.` | Copy pattern. Fn+`.` moves the page list down when the list is open |
| `/` | Paste pattern. Fn+`/` pages to the next bar |
| Enter | No action |

Played keys are also sent as BLE MIDI note-on on the selected track's channel.

While the page list is open it takes every key. Fn+`;` and Fn+`.` move the highlight, Tab does the same, Enter stays on the highlighted page, and `` ` `` or Backspace returns to Play. Notes, space, track, pattern, and BPM keys do nothing until the list closes.

### Instrument

`9` / `0` and Fn+`;` / Fn+`.` move the instrument list. Enter assigns the row to the selected track only. The four names across the top of the page are tracks 1–4. `R` rescans. `,` and `/` load drum kits, as described above. 1–4 still select the track and 5–8 the pattern.

### FX

Described above. `` ` `` or Backspace returns to Play.

### Mixer

Described above. `` ` `` or Backspace returns to Play.

### Song

| Key | Action |
| --- | --- |
| 1–4 | Select the slot |
| 5–8 | No action |
| S / L / X / T | Save, load, delete, slot status |
| N | Start a new song one bar longer, wrapping from 8 back to 1. From a 1-bar song the first press is 2 bars |
| C / V / G | Copy pattern, paste pattern, paste all patterns |
| M | Toggle song mode and pattern mode |
| H | Toggle master volume |
| B | Next BPM slot |
| Enter | Load the selected slot |

`9` and `0` still move the BPM slot. Ctrl+N starts a new song at the current length and does not advance it.

### Loops

Described above. Fn+`;` and Fn+`.` move through every entry in the library. The list scrolls. If the stream windows cannot hold every audio loop, the page says how many opened.

### MIDI

The page shows the real radio state, the stored name, and the channel map. It says "MIDI advertising" only while the controller is advertising. If init failed it says `BLE off: init failed (reason)`, and the next line is the free internal heap and the largest free block measured just before `BLEDevice::init`. Under that it counts packets, parsed messages, overflows, clocks, and notes (`pk`, `msg`, `ovf`, `clk`, `note`). A stored name longer than 8 characters is shortened in the advertising packet, and the page shows that shorter name in parentheses. `E` jumps to Settings and starts editing the BLE name. Enter does nothing here.

### Settings

Rows: speaker, brightness, BLE name, BLE, Unload BLE, battery, free RAM, card, bars. Fn+`;` and Fn+`.` move the row. Fn+`,` and Fn+`/` change speaker volume or brightness by 8 when that row is selected (brightness stays at least 10). On the BLE row the same keys turn the radio off and on. Off stops advertising, disconnects a host, and ignores incoming MIDI. On advertises again. The NimBLE stack stays in memory, so On does not allocate and cannot fail for lack of memory. The choice is stored and defaults to on. With off saved, boot still initialises BLE and does not advertise.

Unload BLE is separate. Enter on that row stores the choice and skips BLE on the next boot. It does not tear the stack down in this session: NimBLE's deinit races the host task and releasing its memory blocks a later init, and either one can reset the board while the speaker is running. The row says `next boot` until restart. After that the BLE row says `unloaded`, and Enter on Load BLE initialises again if a 36KB block is free. If it is not, the toast says `not enough memory, reboot to load` and the board keeps running. Fn+`,` before the reboot cancels the unload.

On the Bars row Fn+`,` and Fn+`/` set the pattern length from 1 to 8 bars and leave the notes in place. Enter on the BLE name row starts typing. Enter again applies the name and restarts advertising when the radio is on. While the name editor is open it takes every key: glyphs are lowercased and appended, up to 16, Backspace deletes one character, and `` ` `` cancels. Space does not play and is not typed.

### Exit

The Exit page is a confirm, and it is in the page list only when a Launcher image is detected. Enter clears the OTA boot selection and restarts toward Launcher. `` ` `` or Backspace returns to Play. Notes, space, track, pattern, and BPM keys do nothing on this page.

## MIDI

On an MPC Live II, open Menu, then Preferences, then Bluetooth. Pair MothDeck, then Connect. Then open MIDI / Sync and enable MothDeck on the MIDI input ports. Turn Sync receive on when the MPC should drive the pattern clock.

Advertised as a BLE MIDI peripheral on legacy connectable advertising. The name `MothDeck` and the MIDI service UUID are both in the primary advertising packet, which is what an MPC lists. A name longer than 8 characters is shortened there; the full name is in the scan response and on the MIDI page. The stack is NimBLE, and its controller memory is internal RAM. The Stamp-S3A has no PSRAM. Audio starts before the screen sprite, and BLE starts only if a contiguous internal block of at least 36KB is still free; otherwise the MIDI page says init failed and playback keeps running. The sprite is rgb332 (32,400 bytes) and is expanded to the panel eight rows at a time, so the blit does not allocate a second full frame. Pairing is Just Works with bonding and no passkey. The host starts pairing. MIDI bytes are not gated on encryption, so a host that never finishes pairing can still connect.

Incoming BLE MIDI is parsed a whole packet at a time. One packet may hold several notes and interleaved `0xF8` clocks; Note On with velocity 0 is Note Off. Channels 1–4 play the four tracks, and any other channel plays the track that is already selected. MIDI Start, Continue, Stop, and Song Position move the transport. Clock is 24 per quarter note, and a pattern step is a 16th, so six clocks advance one step. Start puts the current pattern on bar 1 step 1 and plays that step. Continue resumes without moving. Song Position is a sixteenth-note index taken modulo the pattern length, and it does not switch patterns. The tempo shown on screen is averaged over one beat of those clocks. Space still starts and stops the internal clock.

MIDI has no message for the other machine's sequence length. Auto-length watches for Start, or a Song Position of 0, after a whole number of bars of clocks (one bar is 96 clocks, and the count has to land within half a beat of that). It rounds to 1–8 bars and adopts that length. The MPC Live II manual documents MIDI Clock, Start, Stop, and Continue. It does not document a message at the sequence loop point, and in practice the Live II keeps the clock running through the loop and does not send Start or Song Position there. When nothing comes back to zero, auto-length leaves the length set on the device. Phase still follows the clock from the last Start: a 1-bar pattern stays locked to a longer MPC sequence, and a length that does not divide the MPC sequence drifts until the next Start or Song Position. Stop, Space, and a Song Position that is not zero throw away the measurement so the next Start does not resize from a partial pass.

After a connection the link asks for a 7.5–15 ms interval, and it accepts write and write-without-response. The MIDI page and the Settings screen count packets, parsed messages, clocks, notes, and buffer overflows. Settings also repeats the free heap, the largest block, and whether BLE is off, advertising, or connected. Change the BLE name on Settings. The name is stored in NVS. Packets are Apple-style timestamped MIDI, the same codec as MothOS.

| Message | Map |
| --- | --- |
| Note on/off, channel 1–4 | Tracks 1–4. Notes 36–83 are C2–B5 |
| CC 0 / CC 32 bank | Instrument. MSB 0–11 built-in, 12–62 plugin id, 63 Loops. A non-zero LSB still spreads the 14-bit value across the 12 built-ins, as MothOS does |
| CC 1 mod | Low pass |
| CC 7 / CC 39 volume | 14-bit, mapped to voice volume 0–8 |
| Pitch bend | Per channel, ± the voice pitch ratio |
| Clock `0xF8` | 24 per quarter. Six clocks advance one 16th. Tempo is one beat of clock spacing |
| Start `0xFA` | External sync, current pattern, bar 1 step 1. Learns a loop length only after a full run of clocks |
| Continue `0xFB` | Resume external sync, no jump |
| Stop `0xFC` | Stop, leave external sync, cancel auto-length |
| Song position `0xF2` | Sixteenth index modulo the pattern length. Zero can learn the loop; any other value cancels the measurement |

Keypad notes are sent back out as note-on on the track channel. Bank and volume are sent when the matching control changes.

## Build

PlatformIO, pioarduino 55.03.312-1 (Arduino-ESP32 3.3.12). Libraries are pinned: M5Unified 0.2.25, M5GFX 0.2.32, M5Cardputer 1.1.1.

```bash
pip install platformio
./build.sh
```

`pio run -e cardputer-adv-dev` builds the same firmware with SD, speaker, heap, and MIDI-counter logs, plus Arduino error logs, on the USB serial port. The release image prints the version at boot, a line when advertising starts, and a line when audio or BLE init fails. Packet, clock, and note counts stay on the MIDI page in both builds.

`build.sh` regenerates the built-in waveforms, the example card tree, runs the host tests, and writes:

- `releases/mothdeck-cardputer-adv.bin` — install this from Launcher
- `releases/mothdeck-cardputer-adv-full.bin` — USB flash only; it replaces Launcher
- `releases/SHA256SUMS`

Host tests alone:

```bash
bash tools/run_tests.sh
```

The image is built `qio_qspi` at 80 MHz. That is the memory type on the Arduino-ESP32 `m5stack_cardputer` board (QIO flash, QSPI PSRAM type, PSRAM left disabled) and on Launcher's Cardputer environment. `-DBOARD_HAS_PSRAM` is not set, so M5GFX does not prefer a PSRAM heap that is not there. `qio_opi` would boot-init octal PSRAM on GPIO33–37, which this board uses for the display.

`partitions/cardputer_adv_8MB.csv` is the development table used by a USB flash and by the linker. Launcher does not use it. Launcher installs the application image into an OTA slot after its own `APP_TEST` image.

## Hardware

Cardputer ADV: Stamp-S3A (ESP32-S3FN8, 8MB flash, no onboard PSRAM), ST7789 240×135, TCA8418 keyboard at I2C `0x34` (SDA 8, SCL 9), ES8311 on the same I2C bus at `0x18`. GPIO42 is the codec data line (DSDIN), GPIO41 is bit clock, GPIO43 is word select, and GPIO46 is the codec microphone data. The NS4150B speaker amp follows the codec, and its enable pin is not a GPIO: the board pulls it on, and the 3.5 mm jack turns it off while a plug is inserted. This firmware still does not play through that speaker. Use earphones or headphones. microSD is a separate SPI bus (SCK 40, MISO 39, MOSI 14, CS 12) with GPIO5 held high before mount. Display pins are MOSI 35, SCLK 36, CS 37, DC 34, RST 33, backlight GPIO38. Battery ADC is GPIO10. The front button is GPIO0. Grove (GPIO1 / GPIO2) is left unused. The IMU is left off.

Octal PSRAM uses GPIO33–37. Those pins are the display, so a PSRAM module cannot be added to this Stamp-S3A, and the firmware stays on `qio_qspi`. Both hardware SPI controllers are already in use, one for the display and one for the microSD. A Grove SPI RAM board is not a supported upgrade. Extra sample room is the microSD: kits and short plugin samples stay in a small RAM cache, and loops are read from the card while they play.

The splash art is `docs/splash-moth.png`. Regenerate the embedded mask with `python3 tools/make_splash.py`.

## Site

GitHub Pages publishes the `docs/` folder on `main`. After that setting is on, the site is:

https://shard76-dark.github.io/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/

`docs/robots.txt` allows indexing and names `sitemap.xml`. `docs/sitemap.xml` lists that Pages URL. `docs/index.html` carries the description, Open Graph, Twitter card, and JSON-LD (`SoftwareApplication` and `WebSite`).

## Project layout

```
include/     headers, including the codecs the host tests compile
src/         firmware
test/        host tests (MIDI, song v1/v2/v3, per-track FX, manifests, WAV, synth render)
tools/       sample generator, WAV converters, keymap and splash tools, test runner
docs/        project site (index, robots, sitemap), file formats, keymap drawings, splash art
sd-card-example/
partitions/  development table, not used by Launcher
releases/    application image, full-flash image, SD pack, checksums, install notes
```

## Licence

MIT. Copyright (c) 2024 MothSynths. See [LICENSE](LICENSE).
