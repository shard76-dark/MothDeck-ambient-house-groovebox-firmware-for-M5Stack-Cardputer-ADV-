# Changelog

Notes for each image in `releases/` are in [releases/CHANGELOG.md](releases/CHANGELOG.md).

## 1.8.0 (2026-10-11)

The filter's wide multiply is MULL and MULSH on Xtensa. `MOTHDECK_DSP_C` keeps the portable 64-bit path. Host tests check that path against a 64-bit product, including negative numbers. The low-pass second pole sits a quarter below the first, so the slope is steeper, and a cutoff change slews for about 6 ms instead of stepping. A new note still starts on the cutoff you set. The voice count is unchanged.

Image: `releases/mothdeck-cardputer-adv.bin` (2,281,616 bytes, magic `E9`, SHA-256 `942beb403d95091251e874075907e1c06bad7442ab4e338dc173940fd71736a9`, static RAM 176308/327680). `releases/mothdeck-cardputer-adv-full.bin` is 2,347,152 bytes, SHA-256 `3e5b88f8a285de98fe2d4411f50a80f8413a8c917d93f90e87b28f4dfba3e063`. Flash used is 2,246,715 of 3,932,160 bytes in one OTA slot. Host tests passed. Nothing here was flashed or listened to. The TTGO image is a separate branch.

## 1.7.0 (2026-10-11)

The user manual is [docs/MANUAL.md](docs/MANUAL.md). The browser installer is [docs/flash.html](docs/flash.html). It writes the full-flash image with ESP Web Tools and replaces Launcher. The application image is still the file Launcher installs. Behavior matches 1.6.0. The Settings line says 1.7.0.

Image: `releases/mothdeck-cardputer-adv.bin` (2,281,600 bytes, magic `E9`, SHA-256 `9092fa25716769d1402b2769f46f1cafe890f46a140e80613527710cb3b5e2e9`, static RAM 176292/327680). `releases/mothdeck-cardputer-adv-full.bin` is 2,347,136 bytes, SHA-256 `7f39ff6d32bf73bdde18eb090bee2c7a96d1322ef3f96dbbee3bf74ab8f77132`. The same full image is `docs/flash/mothdeck-cardputer-adv-full.bin` for the browser installer. Host tests passed. Nothing here was flashed or listened to. The TTGO manual and flasher are a separate branch.

## 1.6.0 (2026-10-11)

Settings has a WiFi row. Enter starts a soft access point named `Mothdeck`, password `mothdeck`, at `192.168.4.1`. The page lists `/moth` and can download, upload, and delete files under that folder. A path that leaves `/moth` is rejected. An upload is written to `/moth/_upload.tmp` and takes the place of the old file only after the body arrives, so a short upload leaves the previous file. The transfer locks the card one chunk at a time, so a loop can still refill. Starting Wi-Fi does not stop BLE, free the loop windows, or unload a kit. If the largest internal block is under 48 KB, or the access point does not come up, the row stays off and the toast says `WiFi unchanged`.

Image: `releases/mothdeck-cardputer-adv.bin` (2,281,600 bytes, magic `E9`, SHA-256 `affa6a910a3223c712083131aa92b0e425cce5b3dc51ae42369f13f094498325`, static RAM 176292/327680). `releases/mothdeck-cardputer-adv-full.bin` is 2,347,136 bytes, SHA-256 `bf1eea39cb4e156c4f07db05302c8588a61c22daae051b8245915e38a18c4702`. Flash used is 2,246,687 of 3,932,160 bytes in one OTA slot. Host tests passed. Nothing here was flashed or listened to. The SD pack is unchanged from 1.3.0. The TTGO build of this radio does not link. BLE and the PSRAM cache workaround already fill internal instruction RAM, and Wi-Fi overflows that RAM by 892 bytes. The drum tables sit in flash, so removing them would not make the radio fit. BLE stays in the TTGO image. This 1.6.0 image is Cardputer only.

## 1.5.0 (2026-10-11)

The Song page can sample the master mix. `U` records the next 8192 frames (about 0.19 seconds) into a fixed buffer. `O` plays that take back on top of the tracks. `Y` plays it an octave higher. The buffer is reserved with the firmware and is never allocated from the loop windows, the kit cache, or the BLE heap. Recording hears the tracks and not the take itself. Drums, loops, BLE, plugins, and the chain keep working whether or not a take is stored.

Image: `releases/mothdeck-cardputer-adv.bin` (1,748,672 bytes, magic `E9`, SHA-256 `beacfd7f7420f3c833ae09fe55e99d4244289de0526eaa3505516fd15df33e11`, static RAM 156076/327680). `releases/mothdeck-cardputer-adv-full.bin` is 1,814,208 bytes, SHA-256 `6118ad07d9d8f8cf146b926d09894c945de31259c7338373e203b3e4236ea073`. Flash used is 1,717,831 of 3,932,160 bytes in one OTA slot. DIRAM used is 240,035 of 341,760. Host tests passed. Nothing here was flashed or listened to. The SD pack is unchanged from 1.3.0. The TTGO image is a separate branch.

## 1.4.0 (2026-10-11)

The Song page stores up to eight scenes. `J` saves the current pattern and the four track mutes as the next scene. `P` clears the chain. While a chain is stored, playback walks it at the end of each pattern and applies that scene's pattern and mutes. Song mode is left as it was when the chain is empty. Loops, BLE, drums, and plugins are not unloaded.

Image: `releases/mothdeck-cardputer-adv.bin` (1,747,920 bytes, magic `E9`, SHA-256 `88ebeca4ffc5f0b81d4b940774902f8f968cdc17c96ec11a718e9a276687d15d`, static RAM 139676/327680). `releases/mothdeck-cardputer-adv-full.bin` is 1,813,456 bytes, SHA-256 `d8e558662d1c28bb75d6d57c6ae3a3e0a9efec2016ab16318d850c77e0ec2295`. Host tests passed. Nothing here was flashed or listened to. The TTGO image is a separate branch.

A song with a chain is version 5, 18 bytes after the version 4 block. An empty chain stays version 1 through 4, so older songs still load. The chain is in the song file, not a separate buffer, and it does not take the loop-window pool.

## 1.3.0 (2026-10-11)

Built-in drums and the twelve sound effects are rendered again at signed 16-bit, 44100 Hz, from the synthesizers in `tools/voices.py`. They are not an expansion of the old 8-bit tables. All twenty-four tables stay in flash. The application image is 1,747,152 bytes and the OTA slot is 3,932,160 bytes, so the second slot stays.

A drum at its recorded pitch reads one table frame per output sample. Pitched reads (sound effects, plugins, bent drums, and loops that follow the project BPM) use 4-point Hermite interpolation. The four tracks sum in 32-bit. The speaker sample is that sum with triangular dither of one 16-bit LSB, then a clamp.

The SD pack is rendered the same way. Loops stream. A kit that fits in the 8000-frame cache is copied in. A 44100 Hz kit is larger. On a board with PSRAM the whole kit is copied into that RAM. The Cardputer has none, so the kit streams through that same 16 KB: each pad keeps its first 256 frames, and a window per track refills on the other core. That cache is not the loop-window pool, and it is allocated only when a card kit is selected. Plugin samples stay at or under 4096 frames so they still load while BLE is on. An older 8-bit WAV still loads. Built-in drums use no extra RAM, so they keep playing with BLE and with loops.

1.3.0 includes the 1.2.2, 1.2.3, and 1.2.4 behavior below. The LilyGO TTGO T8 port of 1.2.4 is still the pre-release [ttgo-t8-v1.2.4](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/releases/tag/ttgo-t8-v1.2.4) on the [`ttgo-t8` branch](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/tree/ttgo-t8). That port is not merged here. A 1.3.0 port of the T8 is a separate branch and is not this image.

The image to flash is `releases/mothdeck-cardputer-adv.bin` (1,747,152 bytes, magic `E9`, SHA-256 `41d81f307e98d707c24d484e4d22e981ed93002ac19aee4a0f877514e8949be0`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,812,688 bytes, SHA-256 `58bfba6579d6dde36baef68f5022f1fc9b39675514ec79a507ca022cacdfef0a`). The SD pack is `releases/mothdeck-sd-pack.zip` (869,531 bytes, SHA-256 `0023fdcdbe4f6b2345cffa2ad7c173b785d0ea1170a75214d7abf3052aae6c57`). Static RAM is 139596/327680. Settings version line is `v1.3.0`. Host tests passed. This image has not been tried on a Cardputer.

## 1.2.4 (2026-10-11)

External MIDI clock follows every `0xF8` in a BLE packet. Several clocks often share one timestamp, and the tempo is the span divided by that many ticks, smoothed over a beat. A step is still six ticks. The readout tracks 40–200 BPM. Notes on one track sound together, up to four. A fifth replaces the oldest. Note-off, including velocity 0, releases only the matching note and channel, so a stolen or retriggered note does not cut a different voice. Notes that arrive in one packet each get a voice. When the queue is nearly full, extra clocks are dropped so a chord behind them is kept. A build that reaches Chris with a change gets a new version number.

1.2.4 was the previous image to flash, and it includes the 1.2.2 and 1.2.3 behavior below: BLE reserved at boot with Unload BLE, and MIDI notes that only play while Rec is off. The LilyGO TTGO T8 port of this version is the pre-release [ttgo-t8-v1.2.4](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/releases/tag/ttgo-t8-v1.2.4) on the [`ttgo-t8` branch](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/tree/ttgo-t8). Wiring is in [docs/TTGO-T8.md](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/blob/ttgo-t8/docs/TTGO-T8.md) on that branch. That port is not merged here, and its images are not the files below. They have not been tried on hardware.

The image published as 1.2.4 was `releases/mothdeck-cardputer-adv.bin` (1,153,504 bytes, magic `E9`, SHA-256 `014b2e089b5383d32a5628147f6571bff5c0c9f470a3f89b681a36508e21eea1`). The full-flash image was `releases/mothdeck-cardputer-adv-full.bin` (1,219,040 bytes, SHA-256 `0994efa5bfdf23d3fd5b662cb8db5016b2284c8a128c71b6e0efa112f433bd4d`). The SD pack was unchanged from 1.2.1 (89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`). Static RAM was 137812/327680. Settings version line was `v1.2.4`. Superseded by 1.3.0.

## 1.2.3 (2026-10-11)

Incoming MIDI notes play and are not written into the pattern while Rec is off. MIDI Start and Continue do not arm Rec. Space still arms it, and with Rec on those notes record on the playhead. A piano-roll key while the transport is running follows that same rule. Stopped, the roll still writes the cell under the cursor.

The image published as 1.2.3 is `releases/mothdeck-cardputer-adv.bin` (1,151,680 bytes, magic `E9`, SHA-256 `45837a975607b7933722ae37fa8492c1d06b53418b3837596f43b79820e8e8ec`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,217,216 bytes, SHA-256 `39a727811b019e9e1f1de4c9d16af38be31071df78266ed88c68ebe0e242d0f1`). The SD pack is unchanged from 1.2.1 (89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`). Static RAM is 136004/327680. Settings version line is `v1.2.3`. Superseded by 1.2.4.

## 1.2.2 (2026-10-10)

BLE memory is reserved at boot, before the loop windows, so turning BLE on only starts advertising and does not fail for lack of a free block. Unload BLE skips the stack on the next boot and frees that block for a large loop library. The toast says `BLE unloads after restart`.

The image published as 1.2.2 is `releases/mothdeck-cardputer-adv.bin` (1,151,680 bytes, magic `E9`, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,217,216 bytes, SHA-256 `b492f920d0eb2202fa4397fa3bbe1f3758285474724f9a92251f56530e973762`). The SD pack is unchanged (89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`). Static RAM is 136004/327680. Superseded by 1.2.3.

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
