# Changelog

Notes for each image in `releases/` are in [releases/CHANGELOG.md](releases/CHANGELOG.md).

## 1.2.5 (2026-10-13)

MIDI Stop, and a Bluetooth link that drops, release live notes. The pattern, loops, and sampler keep playing. CC 120 and CC 123 release live notes on the channel that sent them. A note-off finds the track that sounded the note and does not change the selected track. Clocks, then controllers, then note-ons give up queue slots. Note-offs and transport stay in order. Continue keeps the current step and the clock phase, including clocks that arrived after Song Position. An external clock clamps at 200 BPM.

The Settings page no longer prints a counter line. The MIDI page counts packets, messages, clocks, and notes. Fn+D there opens Status: shed counts, safety releases, late speaker refills, play retries, the longest MIDI drain, and the safety step length. Backspace closes it. A tonal live note with no note-off releases after that many steps. The default is 192, about 24 seconds at 120 BPM. Fn+`,` and Fn+`/` on Status step it through 32, 96, 192, and 384. Held arp is not timed. The screen redraws when the picture changes.

1.2.5 is the image to flash, and it includes the 1.2.2 through 1.2.4 behavior below. The LilyGO TTGO T8 port is unchanged on the [`ttgo-t8` branch](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/tree/ttgo-t8). That port is not merged here.

The image to flash is `releases/mothdeck-cardputer-adv.bin` (1,157,024 bytes, magic `E9`, SHA-256 `7d51c26422d82fc3fb5c5aa269a0f65338749e0f8db30d84bd0e00027909e867`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,222,560 bytes, SHA-256 `c318d72cdf92c29614f59fbfb240e67418b542f850285d7a2ae7eb402d9ab23c`). The SD pack is unchanged from 1.2.1 (89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`). Static RAM is 138012/327680. Settings version line is `v1.2.5`.

## 1.2.4 (2026-10-11)

External MIDI clock follows every `0xF8` in a BLE packet. Several clocks often share one timestamp, and the tempo is the span divided by that many ticks, smoothed over a beat. A step is still six ticks. The readout tracks 40–200 BPM. Notes on one track sound together, up to four. A fifth replaces the oldest. Note-off, including velocity 0, releases only the matching note and channel, so a stolen or retriggered note does not cut a different voice. Notes that arrive in one packet each get a voice. When the queue is nearly full, extra clocks are dropped so a chord behind them is kept. A build that reaches Chris with a change gets a new version number.

1.2.4 includes the 1.2.2 and 1.2.3 behavior below: BLE reserved at boot with Unload BLE, and MIDI notes that only play while Rec is off. The LilyGO TTGO T8 port of this version is the pre-release [ttgo-t8-v1.2.4](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/releases/tag/ttgo-t8-v1.2.4) on the [`ttgo-t8` branch](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/tree/ttgo-t8). Wiring is in [docs/TTGO-T8.md](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/blob/ttgo-t8/docs/TTGO-T8.md) on that branch. That port is not merged here, and its images are not the files below. They have not been tried on hardware.

The image to flash is `releases/mothdeck-cardputer-adv.bin` (1,153,504 bytes, magic `E9`, SHA-256 `014b2e089b5383d32a5628147f6571bff5c0c9f470a3f89b681a36508e21eea1`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,219,040 bytes, SHA-256 `0994efa5bfdf23d3fd5b662cb8db5016b2284c8a128c71b6e0efa112f433bd4d`). The SD pack is unchanged from 1.2.1 (89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`). Static RAM is 137812/327680. Settings version line is `v1.2.4`. Superseded by 1.2.5.

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
