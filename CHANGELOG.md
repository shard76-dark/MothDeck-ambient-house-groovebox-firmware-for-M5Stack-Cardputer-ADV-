# Changelog

Notes for each image in `releases/` are in [releases/CHANGELOG.md](releases/CHANGELOG.md).

## 1.8.1 (2026-10-11)

Stop from an MPC now stops. `0xFC` holds the playhead and silences the voices, including stacked chord tones. It does not hand the pattern back to the internal clock. Space does that. Start plays from the top. Continue after a Song Position plays that 16th, so a loop that sends Song Position and Continue stays bar-aligned. Six clocks are one 16th, 24 per quarter note.

Each BLE write is copied into a 2 KB ring in the radio callback and parsed on the audio task, so a connection event that carries several packets is not collapsed to one. The tempo is the time between clock bytes, including several that share a timestamp. A connected audio block is 128 samples, about 2.9 ms, which is shorter than one clock at 174 BPM. Clock, start, continue, stop, and song position are not sent unless Settings, Clock out is on. It is off. Chords, loops, and the Bluedroid stack stay.

Song Position that jumps backward, or a return to zero after a whole number of bars, sets the pattern length from 1 to 8 bars. Bars on the Settings page locks that length until a new song. Swing is reported only when the clock itself is uneven, and the step waits for the sixth clock so the offbeat lands with the clock. MIDI clock does not carry a time signature. The bar is 4/4, 16 steps. A 3/4 or 5/4 loop is not resized, and an even clock has no swing to follow.

A build with `MOTHOS_DEV_LOG` prints each packet as `BLE rx` and, once a second, `BLE 1s` with raw `0xF8` / `0xFC` counts, parsed clocks and stops, dropped packets, and the negotiated connection interval. The release `ttgo-t8` and `ttgo-t8-dac` images leave that log out.

Image sizes are filled in after the host tests and the two firmware builds. Nothing here was flashed or listened to. The Cardputer image is a separate branch.

## 1.8.0 (2026-10-11)

The filter's wide multiply is MULL and MULSH. `MOTHDECK_DSP_C` keeps the portable 64-bit path. Host tests check that path against a 64-bit product, including negative numbers. The low-pass second pole sits a quarter below the first, and a cutoff change slews for about 6 ms. A new note still starts on the cutoff you set. The voice count is unchanged. Loops, BLE, drums, chords, the sampler, and the chain stay loaded.

Images: `mothdeck-ttgo-t8-1.8.0-test.bin` (1,896,816 bytes, magic `E9`, SHA-256 `32e5ae2d8c4f29597cf8f03169409637a0654e3b51a9165567f6ba50976ba8d4`, static RAM 100648/327680) and `mothdeck-ttgo-t8-dac-1.8.0-test.bin` (1,890,912 bytes, magic `E9`, SHA-256 `271bd50211c55e2d806d0374a3c968abb1754d8014051772ed6353300acff9b6`, static RAM 99784/327680). Factory slot margins are 134,800 and 140,704 bytes. Browser images are `docs/flash/mothdeck-ttgo-t8-full.bin` (1,962,352 bytes, SHA-256 `09e5bbcf7511dfa5ba30588415d5ebcb39bd6a8409cc0cb5f532886c969f324a`) and `docs/flash/mothdeck-ttgo-t8-dac-full.bin` (1,956,448 bytes, SHA-256 `6170d88c147289e6eab520795284b12f86129b098bc68d557dcbdecc16051c98`). Host tests passed. Nothing here was flashed or listened to.

## 1.7.0 (2026-10-11)

The manual is [docs/MANUAL.md](docs/MANUAL.md). The browser installer is [docs/flash.html](docs/flash.html), with one button for the PCM5102A image and one for the GPIO25 DAC image. Each writes a full factory image. Playback matches 1.5.0. There is no 1.6.0 image on this board: the Wi-Fi file page overflowed internal instruction RAM by 892 bytes with BLE and the PSRAM cache workaround still linked, and the drum tables are in flash so removing them would not free that RAM.

Images: `mothdeck-ttgo-t8-1.7.0-test.bin` (1,896,768 bytes, magic `E9`, SHA-256 `987c6b48c24b49de28447b1de068af2c95bb4416a7c36c3de833a7281370b5dd`, static RAM 100648/327680) and `mothdeck-ttgo-t8-dac-1.7.0-test.bin` (1,890,848 bytes, magic `E9`, SHA-256 `7ce885939f8fcbbe8d7940c285ea08f611687bffdbf7c44c8187ac6788f6a109`, static RAM 99784/327680). Factory images for the browser installer are `docs/flash/mothdeck-ttgo-t8-full.bin` (1,962,304 bytes, SHA-256 `586e660664e7137f9610045e322e2b5994a68c25ed0c4f94f62f0991dc76e405`) and `docs/flash/mothdeck-ttgo-t8-dac-full.bin` (1,956,384 bytes, SHA-256 `2d4b5a3d2709fb2388bf911ca738903b52cc2649a76ed11fa341de6561d6c923`). Host tests passed. Nothing here was flashed or listened to.

## 1.5.0 (2026-10-11)

The Song page can sample the master mix. `U` records the next 8192 frames (about 0.19 seconds) into a fixed buffer. `O` plays that take. `Y` plays it an octave higher. On the 4×4 pad, Hold F4, then F2, then F2 again opens Take: G# records, A plays, A# plays an octave higher. B stores a scene and E clears the chain, which the File page does not give up a key for. The buffer is reserved with the firmware and is never allocated from the loop windows, a kit, or BLE. Recording hears the tracks and not the take. Drums, loops, BLE, chords, plugins, and the chain stay loaded.

Images: `mothdeck-ttgo-t8-1.5.0-test.bin` (1,896,768 bytes, magic `E9`, SHA-256 `390d35fb59e1732700a3df54116d16125931a636d1fc7772dfbc07b7f213022d`, static RAM 100648/327680) and `mothdeck-ttgo-t8-dac-1.5.0-test.bin` (1,890,848 bytes, magic `E9`, SHA-256 `61b1f293af37d7c43ffa6833f8337896b7dd7f958524be952d950f21c8da0704`, static RAM 99784/327680). Factory slot margins are 134,848 and 140,768 bytes. Host tests passed. Nothing here was flashed or listened to. The 1.4.0 images stay and are superseded. The Cardputer 1.5.0 image is a different binary.

## 1.4.0 (2026-10-11)

The Song page stores up to eight scenes. `J` saves the current pattern and the four track mutes as the next scene. `P` clears the chain. While a chain is stored, playback walks it at the end of each pattern and applies that scene's pattern and mutes. An empty chain leaves song mode alone. Loops, BLE, drums, chords, and plugins stay loaded. A song with a chain is version 5. Older songs still load.

Images: `mothdeck-ttgo-t8-1.4.0-test.bin` (1,895,392 bytes, magic `E9`, SHA-256 `8137d0bd4d73b3a80c00204d960bb3fd030becedaefa021fb098362f56b419db`, static RAM 84240/327680) and `mothdeck-ttgo-t8-dac-1.4.0-test.bin` (1,889,456 bytes, magic `E9`, SHA-256 `04e875f77b1fdc75ca6b626f40ffc1dd814b52bd75d113e0bd3a398dd607a12e`, static RAM 83376/327680). The factory slot is still 2,031,616 bytes (margins 136,224 and 142,160). Host tests passed. Nothing here was flashed or listened to. The 1.3.0 images stay and are superseded. The Cardputer 1.4.0 image is a different binary.

## 1.3.0 (2026-10-11)

Built-in drums and sound effects are rendered again at signed 16-bit, 44100 Hz, from `tools/voices.py`. They are not a stretch of the old 8-bit tables. A drum at its recorded pitch plays one frame per output sample. Pitched reads use 4-point Hermite interpolation. The four tracks sum in 32-bit, and the speaker sample is that sum with triangular dither of one 16-bit LSB.

The factory slot is 2,031,616 bytes. The twelve drums and the six shorter effects (zap, noise, blip, reverse, bubble, whoosh) stay in flash. The six long effects (riser, downlifter, sweep, impact, siren, drop) are `/moth/sfx/<name>.wav` in the SD pack. A scan copies each one into PSRAM. A missing card leaves those six silent and leaves drums, the short effects, loops, plugins, and BLE running. Loop windows stay in their own PSRAM pool. A kit that fits is copied into PSRAM. A larger kit streams through the 16 KB cache and does not take the loop windows. Plugin samples stay within 4096 frames. An older unsigned 8-bit WAV still loads.

This image still has chords and the rotated 4×4 menu. The pre-release is `ttgo-t8-v1.3.0`. `ttgo-t8-v1.2.4` stays and is superseded. The Settings line and the boot log say `1.3.0`. The Cardputer image on `main` is also called 1.3.0 and is a different binary: that board's 8MB layout holds all twenty-four tables in flash.

Images: `mothdeck-ttgo-t8-1.3.0-test.bin` (1,894,608 bytes, magic `E9`, SHA-256 `cbbc7017ada1760708682b1b2fce0aa3b6cd645f1e75c4a274502d0bf9608b39`, static RAM 84184/327680) and `mothdeck-ttgo-t8-dac-1.3.0-test.bin` (1,888,656 bytes, magic `E9`, SHA-256 `72dc194cbbaaf180f88728910713f69b07fb5c860d69056a14ee6c48d433e221`, static RAM 83320/327680). Host tests passed. Nothing here was flashed or listened to.

## 1.2.4 (2026-10-11)

External MIDI clock follows every `0xF8` in a BLE packet. Several clocks often share one timestamp, and the tempo is the span divided by that many ticks, smoothed over a beat. A step is still six ticks. The readout tracks 40–200 BPM. Notes on one track sound together, up to four, on top of a stored chord. A fifth replaces the oldest. Note-off, including velocity 0, releases only the matching note and channel. This image still has chords and the rotated 4×4 menu. The pre-release is `ttgo-t8-v1.2.4`. `ttgo-t8-v1.2.3` stays and is superseded. The Settings line and the boot log say `1.2.4`. The Cardputer image on `main` is also called 1.2.4 and is a different binary.

Images: `mothdeck-ttgo-t8-1.2.4-test.bin` (1,636,256 bytes, magic `E9`, SHA-256 `0844c6b5b6715601e812b7328d6072b23e7479608d9cc4f0056d44e2b1103a96`, static RAM 82296/327680) and `mothdeck-ttgo-t8-dac-1.2.4-test.bin` (1,630,288 bytes, magic `E9`, SHA-256 `46d1bc56162f96589c97edb2b2289961bd57f5b2cf9e7131449fa6c865a8a4ee`, static RAM 81432/327680). Host tests passed. Nothing here was flashed or listened to.

## 1.2.3 (2026-10-11)

Incoming MIDI notes play and are not written into the pattern while Rec is off. MIDI Start and Continue do not arm Rec. Space still arms it, and with Rec on those notes record on the playhead. A piano-roll key while the transport is running follows that same rule. Stopped, the roll still writes the cell under the cursor.

This TTGO image also has chords and the rotated 4×4 MothSynth menu. Those were previously published by refreshing the pre-release `ttgo-t8-v1.2.2`, which reused the version number. That tag stays. This pre-release is `ttgo-t8-v1.2.3`. The Settings line and the boot log say `1.2.3`. From this version on, a build that reaches Chris with a change gets a new version number. The Cardputer image on `main` is also called 1.2.3 and is a different binary: it does not have chords or this keypad.

The stock image drives the confirmed ST7789 240×135. `ttgo-t8` plays through a PCM5102A. `ttgo-t8-dac` uses the 8-bit DAC on GPIO25. Loop windows, samples, the pattern grid, chord notes, and the tracker object use PSRAM, and turning BLE on does not unload loops. BLE MIDI still plays if the panel is unplugged. Wiring is in [docs/TTGO-T8.md](docs/TTGO-T8.md).

The keypad is a 4×4 membrane with the connector on top. Membrane rows are firmware columns and membrane columns are firmware rows, so the top-left key is key 0. The menu is the MothSynth function-key layout: tap F1–F4, then a key. Hold a function key for the MothDeck pages (roll, instruments and patches, FX, mixer, loops, song file, and settings, including BLE, Rec, and pattern length). GPIO35 is a keypad column, so the battery percent stays `n/a`. The PCM image and the DAC image differ by one wire: keypad R3 is GPIO25 on the PCM image and GPIO22 on the DAC image.

A step holds up to four notes. The first stays in the pattern word, so a song file is unchanged and an older song still loads as single notes. The other three notes sit in PSRAM. With Rec on, MIDI notes that land in the same step stack, and a fifth is dropped. The piano roll shows each one as a block in that column. Playback sounds them together. Saving a song keeps the first note of each step.

Images: `mothdeck-ttgo-t8-1.2.3-test.bin` (1,634,448 bytes, magic `E9`, SHA-256 `2763b41df41099bda4726999d883b86071176c1ff1d277b2ee08857ad3db88ba`, static RAM 81336/327680) and `mothdeck-ttgo-t8-dac-1.2.3-test.bin` (1,628,448 bytes, magic `E9`, SHA-256 `ac5d73f8e8cdea928507cdab3326bcc503b7296a92d7273a8ece67135dc8a195`, static RAM 80472/327680). Host tests passed. Nothing here was flashed or listened to. Superseded by `ttgo-t8-v1.2.4`.

## 1.2.2 (2026-10-10)

BLE memory is reserved at boot, before the loop windows, so turning BLE on only starts advertising and does not fail for lack of a free block. Unload BLE skips the stack on the next boot and frees that block for a large loop library. The toast says `BLE unloads after restart`.

On `main` that is the published Cardputer release v1.2.2 (1,151,680 bytes, magic `E9`, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`), superseded by 1.2.3. On this branch the same version number was reused for chords and the 4×4 menu. Those images stay on the pre-release `ttgo-t8-v1.2.2`: `mothdeck-ttgo-t8-1.2.2-test.bin` (1,634,448 bytes, SHA-256 `b94e5c44c4b5c39852b8280d19ad6df694a0b390de42e10eea38109369c6c539`, static RAM 81336/327680) and `mothdeck-ttgo-t8-dac-1.2.2-test.bin` (1,628,448 bytes, SHA-256 `dd462557d5300b21026f6246477075e798f9460474b4604dce8cd1ca4caf5e8b`, static RAM 80472/327680). Superseded by `ttgo-t8-v1.2.3`. The files in `releases/` on this branch are still the Cardputer 1.2.1 image.

## 1.2.1 (2026-10-09)

Screen notices no longer mention memory or read as errors. MIDI mode asks `Save song? Y / N` before loops unload. A short library says `Loops: N of M ready`. BLE that cannot start says `restart`, and the toast says `BLE loads after restart`. Kits and plugins that do not fit say `Kit unchanged` or `Sound unchanged`.

The image to flash is `releases/mothdeck-cardputer-adv.bin` (1,152,112 bytes, magic `E9`, SHA-256 `d01b62bd90a58c1345aa8c451771770db7e4ff6504a4d3669b88363176e7bafe`). Notes are in [releases/CHANGELOG.md](releases/CHANGELOG.md). This image has not been tried on a Cardputer.

## 1.2.0 (2026-10-09)

A plugin on the card can be a patch: `mothdeck-patch 1` in `/moth/instruments/<folder>/manifest.txt`. It names a sound that already exists (a built-in, a short sample, a subtractive cycle, or an FM cycle) and the voice, insert, scale, arp, and glide numbers for one track. The card still has no code. Older `mothdeck-instrument 1` folders load as before.

Three small blocks are new. Scale lock snaps a note to major, minor, harmonic minor, mixolydian, phrygian, or chromatic, nearest pitch class, ties down. The held arpeggiator keeps up to eight MIDI notes and walks them once per 16th. A second oscillator (sine, square, saw, or triangle), a coarse transpose of −24..24 semitones, a blend, and a glide time mix into built-in tones and short samples. None of these allocate a delay line.

Assign a folder with Enter on the Instrument page. Scale, Root, Arp, Glide, Osc2, Blend, and Coarse are the extra rows on the FX page. A song stores those rows only when one of them is in use, which makes the file version 4 (at most 3636 bytes). Interval walks `arp=1` and `arp=2` stay in the old voice byte and do not force version 4.

A built-in patch uses no heap. A subtractive or FM cycle is 168 frames, 336 bytes. A sample stays capped at 4096 frames, 8192 bytes, and at most four stay loaded. That sample heap is not the loop-window pool (five windows of 1024 frames, double buffered, 20480 bytes). Turning BLE on still unloads loops and leaves these plugins loaded.

The image to flash is `releases/mothdeck-cardputer-adv.bin` (1,153,152 bytes, magic `E9`, SHA-256 `a802ece13876ab053dac6bc8e2c8e8feab1d5b936c3b59d9f32de618946172ee`). Notes for that image are in [releases/CHANGELOG.md](releases/CHANGELOG.md). Chris confirmed the piano roll on a Cardputer. BLE in this image has not been tried on hardware.

## 1.1.2 (shipped in 1.2.0)

BLE pairs with an MPC Live II again. 1.1.0 did. 1.1.1 stopped, and the pairing code itself did not change: same Just Works bond (no passkey, host starts pairing), same MIDI characteristic (read, write, write without response, notify), same primary advert (flags, complete name, MIDI UUID).

What broke it was the loop-window pool. 1.1.0 initialised NimBLE with that 20KB still free. The controller takes one contiguous block of about 36KB, and the host pools need roughly another 24KB after that. 1.1.1 reserved the pool first and kept it whenever the largest leftover block was still 36KB. That is only enough for the controller. The host then came up short, and the MPC's pairing exchange, which needs those buffers, failed. The radio could still look like it was advertising.

1.1.2 does not start BLE at boot. The pool stays available and the radio is idle. Turning BLE on closes the streams, frees the pool, then initialises NimBLE the way 1.1.0 did. Turning it off stops advertising and loads the windows that fit beside the stack. A reboot with BLE still off never initialises NimBLE, so the full windows come back. The old NVS key `bleOn` is ignored, so a 1.1.1 "on" does not advertise.

The air name is `Mothdeck` (eight characters, complete in the primary packet). A stored `MothDeck` or `MothSynth` is replaced on boot. Rec starts off. Turning BLE on turns Rec off. If the open project has loops, the screen warns that loops are unavailable under MIDI and a new project will load, then asks Y to save or N to skip. `` ` `` or Backspace leaves BLE off.

MIDI notes were waiting on the audio queue. The audio task drained the ring once per 256-sample block and skipped that drain while two blocks were queued, about 17 ms worst case after the packet was already in the ring, on top of the link interval (15 ms if the 7.5–15 ms request stuck, often ~30 ms if the MPC kept its own). Connected playback now renders 128-sample blocks, keeps one buffer queued, and drains the ring while waiting and every 32 samples (about 0.7 ms). That is about 4 ms on the device plus one 256-sample I2S period (about 6 ms) that is left alone so local playback does not underrun. The interval request is sent 250 ms after connect, not inside the connect callback, and the negotiated interval is printed on serial and on the MIDI page (`link 15.0ms`). If the MPC accepts 15 ms, expect about 25 ms end to end. If it stays at 30 ms, expect about 40 ms, and the page shows that number. These are estimates from the buffer sizes. This environment has no Cardputer to measure.

These notes are included in the 1.2.0 image.

## 1.1.1 (shipped in 1.2.0)

Settings can turn the BLE radio off and on without unloading NimBLE, and a separate Unload BLE row skips the stack on the next boot. SD loops load as a set again while the radio is off. A stored 256-step song loads as four patterns of 64. Play opens on a piano roll (Enter returns to the 16-step strip). The behavior that shipped is the 1.2.0 section above: BLE is off at boot, and turning it on unloads loops.

## 1.1.0 (2026-10-09)

BLE MIDI pairs with an MPC Live II, patterns run from 1 to 8 bars, and playback stays up if the radio cannot start. Flash [`releases/mothdeck-cardputer-adv.bin`](releases/mothdeck-cardputer-adv.bin) (1,134,080 bytes, magic `E9`, chip `0x0009`, SHA-256 `f8d9ab44082ab7ff5921dca9d30eb68ed8a440430bb019df31e4d82212569a4a`).
