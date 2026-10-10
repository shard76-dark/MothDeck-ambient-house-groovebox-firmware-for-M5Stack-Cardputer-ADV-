# Changelog

Notes for each image in `releases/` are in [releases/CHANGELOG.md](releases/CHANGELOG.md).

## 1.2.2 test (not released)

NimBLE is initialised at boot, before the loop pool and before the card scan, whether or not it is advertising. On and off only start or stop advertising, so turning BLE on does not allocate and does not fail. Loop windows use whatever is left. Unload BLE still waits for the next boot, and the toast says `BLE unloads after restart`. Load BLE says `BLE loads after restart`. The download image in `releases/` stays 1.2.1 until this build is confirmed on a Cardputer.

Incoming MIDI notes sound and are not stored while Rec is off. MIDI Start and Continue play the pattern and do not arm Rec. Space still arms Rec, and with Rec on those notes record on the playhead as before. A piano-roll key while the transport is running follows that same rule. Stopped, the roll still writes the cell under the cursor.

Test images, not published: `mothdeck-cardputer-adv-1.2.2-test.bin` (1,151,680 bytes, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`) and `mothdeck-cardputer-adv-1.2.2-test-dev.bin` (1,198,912 bytes, SHA-256 `3695b88977b5262df353a7a4477e8818c17bbdd0110e68218a009df81f53f3ae`). Host tests passed. Static RAM is 136004/327680 on the test image and 136260/327680 on the dev image. This environment has no Cardputer, so the heap log and pairing have not been run here. The dev image prints free and largest internal heap before NimBLE, after it, after the loop pool, and across the on/off switch (`BLE: switch on` with the before and after sizes). Those Cardputer files are the pull-request images, not a build of this branch.

The TTGO T8 port is on the long-lived `ttgo-t8` branch. It is not merged to `main`. The pre-release tag is `ttgo-t8-v1.2.2`. The stock image drives the confirmed ST7789 240×135. `ttgo-t8` plays through a PCM5102A. `ttgo-t8-dac` uses the 8-bit DAC on GPIO25. Loop windows, samples, the pattern grid, and the tracker object use PSRAM, and turning BLE on does not unload loops. BLE MIDI still plays if the panel is unplugged. Wiring is in [docs/TTGO-T8.md](docs/TTGO-T8.md). The same Rec rule is in this branch: MIDI notes are stored only while Rec is on.

On this branch a step holds up to four notes. The first stays in the pattern word, so a song file is unchanged and an older song still loads as single notes. The other three notes sit in PSRAM. With Rec on, MIDI notes that land in the same step stack, and a fifth is dropped. The piano roll shows each one as a block in that column. Backspace removes the note under the cursor. Playback sounds them together: extra drum hits, and extra oscillators on the built-in synths. Saving a song keeps the first note of each step. The Cardputer image on `main` does not have chords. Images: `mothdeck-ttgo-t8-1.2.2-test.bin` (1,628,784 bytes, SHA-256 `20ec4c632ee37547da2a6c284dd3805b329ac7ed5bedd071daa793752bab2c88`, static RAM 81216/327680) and `mothdeck-ttgo-t8-dac-1.2.2-test.bin` (1,622,784 bytes, SHA-256 `c3597a22503d4b5c1ba5b24877459e0dc84f139bc2b125fddfb0c3f83158fcba`, static RAM 80352/327680). Nothing here was flashed or listened to. The Cardputer image is `v1.2.2` on `main`. These TTGO images are the pre-release on this branch.

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
