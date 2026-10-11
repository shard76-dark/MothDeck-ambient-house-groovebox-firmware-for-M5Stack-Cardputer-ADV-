## 1.8.1 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (2,284,016 bytes, magic `E9`, chip `0x0009`, SHA-256 `ac308241c2e643a7b81bef473cf1f38a8544fb144572494f4e0462fd689a7e75`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (2,349,552 bytes, SHA-256 `b3c2a01ff7d0df56c694bc73240b847ad8a7c69e14d6fd5cda93e3e9c840852a`). Version on the Settings screen is 1.8.1. Static RAM is 179092/327680. Host tests passed. This image has not been tried on a Cardputer.

Stop silences the voices and holds the playhead. Start plays from the top. Song Position plus Continue plays that 16th and can set the pattern length to 1–8 bars when the position jumps backward or returns to zero after whole bars. Bars locks the length until a new song. Clock out is off, so clock, start, continue, stop, and song position are not sent. The dev build logs each received packet and the `0xF8` / `0xFC` counts per second with the negotiated link interval. MIDI clock does not carry a time signature. The bar is assumed to be 4/4.

## 1.8.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (2,281,616 bytes, magic `E9`, chip `0x0009`, SHA-256 `942beb403d95091251e874075907e1c06bad7442ab4e338dc173940fd71736a9`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (2,347,152 bytes, SHA-256 `3e5b88f8a285de98fe2d4411f50a80f8413a8c917d93f90e87b28f4dfba3e063`). Version on the Settings screen is 1.8.0. Static RAM is 176308/327680. Host tests passed. This image has not been tried on a Cardputer.

The low-pass uses a wide multiply (MULL and MULSH on the ESP32-S3, portable 64-bit C otherwise, including when `MOTHDECK_DSP_C` is set). The second pole is a quarter darker. Cutoff changes slew for about 6 ms. The voice count is unchanged.

## 1.7.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (2,281,600 bytes, magic `E9`, chip `0x0009`, SHA-256 `9092fa25716769d1402b2769f46f1cafe890f46a140e80613527710cb3b5e2e9`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (2,347,136 bytes, SHA-256 `7f39ff6d32bf73bdde18eb090bee2c7a96d1322ef3f96dbbee3bf74ab8f77132`). The browser installer is `docs/flash.html` and writes that same full image. Version on the Settings screen is 1.7.0. Static RAM is 176292/327680. Host tests passed. This image has not been tried on a Cardputer.

The manual is `docs/MANUAL.md`. Playback matches 1.6.0.

## 1.6.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (2,281,600 bytes, magic `E9`, chip `0x0009`, SHA-256 `affa6a910a3223c712083131aa92b0e425cce5b3dc51ae42369f13f094498325`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (2,347,136 bytes, SHA-256 `bf1eea39cb4e156c4f07db05302c8588a61c22daae051b8245915e38a18c4702`). Version on the Settings screen is 1.6.0. Static RAM is 176292/327680. Host tests passed. This image has not been tried on a Cardputer.

Settings, WiFi row, Enter. The access point is `Mothdeck`, password `mothdeck`, page `http://192.168.4.1/`. Files stay under `/moth`. BLE, loops, drums, plugins, the sampler, and the chain stay loaded. If the access point cannot start, the toast says `WiFi unchanged`.

## 1.5.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (1,748,672 bytes, magic `E9`, chip `0x0009`, SHA-256 `beacfd7f7420f3c833ae09fe55e99d4244289de0526eaa3505516fd15df33e11`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,814,208 bytes, SHA-256 `6118ad07d9d8f8cf146b926d09894c945de31259c7338373e203b3e4236ea073`). Version on the Settings screen is 1.5.0. Static RAM is 156076/327680. Host tests passed. This image has not been tried on a Cardputer.

The Song page records the master mix with `U` (8192 frames, about 0.19 seconds) into a fixed buffer. `O` plays that take. `Y` plays it an octave higher. The buffer is not taken from the loop windows, the kit cache, or BLE. Recording hears the tracks and not the take. Drums, loops, BLE, plugins, and the chain stay loaded.

## 1.4.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (1,747,920 bytes, magic `E9`, chip `0x0009`, SHA-256 `88ebeca4ffc5f0b81d4b940774902f8f968cdc17c96ec11a718e9a276687d15d`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,813,456 bytes, SHA-256 `d8e558662d1c28bb75d6d57c6ae3a3e0a9efec2016ab16318d850c77e0ec2295`). Version on the Settings screen is 1.4.0. Static RAM is 139676/327680. Host tests passed. This image has not been tried on a Cardputer.

The Song page stores up to eight scenes with `J` and clears them with `P`. Playback walks the chain at each pattern end and applies that scene's pattern and mutes. An empty chain leaves song mode alone. A song with a chain is version 5. Older songs still load. Loops, BLE, drums, and plugins stay loaded.

## 1.3.0 (2026-10-11)

Flash `releases/mothdeck-cardputer-adv.bin` (1,747,152 bytes, magic `E9`, chip `0x0009`, SHA-256 `41d81f307e98d707c24d484e4d22e981ed93002ac19aee4a0f877514e8949be0`). The full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,812,688 bytes, SHA-256 `58bfba6579d6dde36baef68f5022f1fc9b39675514ec79a507ca022cacdfef0a`). The SD pack is `releases/mothdeck-sd-pack.zip` (869,531 bytes, SHA-256 `0023fdcdbe4f6b2345cffa2ad7c173b785d0ea1170a75214d7abf3052aae6c57`). Version on the Settings screen is 1.3.0. Host tests passed. Use earphones. This image has not been tried on a Cardputer. Static RAM is 139596/327680. Flash used is 1,716,303 of 3,932,160 bytes in one OTA slot.

Built-in drums and the twelve sound effects are signed 16-bit mono at 44100 Hz, rendered from `tools/voices.py` at that rate. They sit in flash. Pitched playback uses 4-point Hermite interpolation. The speaker sample is dithered by one 16-bit LSB. The SD pack uses the same format. Loops stream. Kits that fit the 8000-frame cache are copied in. Larger kits stream through that same 16 KB and do not use the loop-window pool. Plugin samples stay within 4096 frames. An older 8-bit WAV still loads. Built-in drums need no extra RAM, so they keep playing with BLE and with loops.

This image includes the 1.2.2, 1.2.3, and 1.2.4 behavior. The LilyGO TTGO T8 port of 1.2.4 is a different binary, pre-release [ttgo-t8-v1.2.4](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/releases/tag/ttgo-t8-v1.2.4) on the `ttgo-t8` branch. It is not in this folder.

## 1.2.4 (2026-10-11)

Flash the 1.2.4 application image (1,153,504 bytes, magic `E9`, chip `0x0009`, SHA-256 `014b2e089b5383d32a5628147f6571bff5c0c9f470a3f89b681a36508e21eea1`). Version on the Settings screen is 1.2.4. Host tests passed. Use earphones. This image has not been tried on a Cardputer. Static RAM is 137812/327680. Superseded by 1.3.0.

MIDI clock from an MPC follows every tick in the packet, including several `0xF8` bytes that share one timestamp. Six ticks advance one step. The BPM on screen is smoothed over a beat and tracks from 40 to 200. Notes on one channel sound together, up to four voices. A fifth replaces the oldest note. Note-off, including velocity 0, releases only that note and channel. Notes in one packet each get a voice. Extra clocks are dropped when the queue is nearly full so a chord behind them is kept.

This image includes the 1.2.2 and 1.2.3 behavior: BLE is reserved at boot, Unload BLE frees that block on the next boot, and incoming notes only play while Rec is off. The LilyGO TTGO T8 port is a different binary, pre-release [ttgo-t8-v1.2.4](https://github.com/shard76-dark/MothDeck-ambient-house-groovebox-firmware-for-M5Stack-Cardputer-ADV-/releases/tag/ttgo-t8-v1.2.4) on the `ttgo-t8` branch. It is not in this folder.

The USB full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,219,040 bytes, SHA-256 `0994efa5bfdf23d3fd5b662cb8db5016b2284c8a128c71b6e0efa112f433bd4d`). It replaces Launcher. Install `mothdeck-cardputer-adv.bin` from Launcher.

The SD pack is the same file as 1.2.1 (`mothdeck-sd-pack.zip`, 89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`).

## 1.2.3 (2026-10-11)

Flash the 1.2.3 application image (1,151,680 bytes, magic `E9`, chip `0x0009`, SHA-256 `45837a975607b7933722ae37fa8492c1d06b53418b3837596f43b79820e8e8ec`). Version on the Settings screen is 1.2.3. Host tests passed. Use earphones. This image has not been tried on a Cardputer. Static RAM is 136004/327680. Superseded by 1.2.4.

Incoming MIDI notes play and are not stored while Rec is off. Start and Continue do not arm Rec. Space arms Rec, and then those notes are written at the playhead. A piano-roll key while the transport is running follows that same rule. Stopped, the roll still writes the cell under the cursor.

The USB full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,217,216 bytes, SHA-256 `39a727811b019e9e1f1de4c9d16af38be31071df78266ed88c68ebe0e242d0f1`). It replaces Launcher. Install `mothdeck-cardputer-adv.bin` from Launcher.

The SD pack is the same file as 1.2.1 (`mothdeck-sd-pack.zip`, 89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`).

## 1.2.2 (2026-10-10)

Flash the 1.2.2 application image (1,151,680 bytes, magic `E9`, chip `0x0009`, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`). Version on the Settings screen is 1.2.2. Host tests passed. Use earphones. Chris approved that image. Superseded by 1.2.3.

NimBLE is reserved at boot, before the loop windows, whether or not it is advertising. Turning BLE on starts advertising and does not allocate, so it does not fail for lack of a free block. Unload BLE waits for the next boot and frees that block for large loops. The toast says `BLE unloads after restart`.

The 1.2.2 full-flash image was 1,217,216 bytes, SHA-256 `b492f920d0eb2202fa4397fa3bbe1f3758285474724f9a92251f56530e973762`. It replaces Launcher. Install the application image from Launcher.

The SD pack is the same file as 1.2.1 (`mothdeck-sd-pack.zip`, 89,409 bytes, SHA-256 `46398772022192f49ccfaeebd69bed8e3075b8596d824cd17cb7e7ad1a7f11e9`).

## 1.2.1 (2026-10-09)

Flash `releases/mothdeck-cardputer-adv.bin` (1,152,112 bytes, magic `E9`, chip `0x0009`, SHA-256 `d01b62bd90a58c1345aa8c451771770db7e4ff6504a4d3669b88363176e7bafe`). Version on the Settings screen is 1.2.1. Host tests passed. Use earphones. This image has not been tried on a Cardputer. The piano roll Chris liked is unchanged. BLE is still untested on hardware.

Notices on the screen no longer read as memory errors. Turning BLE on while a song has loops asks `MIDI mode: loops off.` and `Save song? Y / N`. `` ` `` cancels. A library that cannot open every loop says `Loops: N of M ready`. If BLE cannot start, the row says `restart` and the toast says `BLE loads after restart`. A kit or plugin that does not fit says `Kit unchanged` or `Sound unchanged`, and the previous one stays. The release Settings screen has a Radio row (`off`, `adv`, or `conn`) instead of a Free RAM readout. Heap figures stay on the MIDI page and in the dev build's serial log.

## 1.2.0 (2026-10-09)

Flash `releases/mothdeck-cardputer-adv.bin` (1,153,152 bytes, magic `E9`, chip `0x0009`, SHA-256 `a802ece13876ab053dac6bc8e2c8e8feab1d5b936c3b59d9f32de618946172ee`). Version on the Settings screen is 1.2.0. Host tests passed. Use earphones.

Chris tried the piano roll on a Cardputer and said it looked good. BLE in 1.1.2 and in this 1.2.0 image has not been tried on hardware. The pairing change is the same security 1.1.0 used, with the loop windows freed before the radio starts. This environment has no Cardputer, so latency numbers below are estimates from the buffer sizes.

Play opens on a piano roll. Keys run down the left and time runs across. A note is a filled block one to four steps long. Fn+`;` and Fn+`.` move pitch. Fn+`,` and Fn+`/` move time. A piano key while stopped writes that pitch at the cursor. Backspace deletes the note. `;` on a note cycles its hold. Enter switches back to the 16-step strip. Patterns are still 1 to 8 bars. Settings has a Bars row, and Fn+`,` and Fn+`/` there set the length without clearing notes. On the Song page, `N` starts a new song and steps that length from 1 to 8. Keys 5–8 select a pattern and stop at the last slot that fits.

BLE is off at boot, and Rec is off at boot. Settings has a BLE row: Fn+`,` turns the radio off, Fn+`/` turns it on. Off stops advertising, disconnects a host, ignores incoming MIDI, and loads loop windows again. On turns Rec off. If the open project has a loop on a track, or a track on the Loops instrument, the screen warns that loops are unavailable under MIDI and a new project will load, then asks Y to save or N to skip. Grave or Backspace cancels and leaves BLE off. After you save or skip, the windows are freed and the radio starts. Later On calls do not allocate again. The choice is stored. A 1.1.1 setting of on is not read, so the radio stays off until you turn it on.

Unload BLE is the next row. Enter stores it and the next boot skips the stack. This session does not tear the stack down. After that boot the BLE row says unloaded, and Enter on Load BLE initialises again when a 36KB block is free. If it is not, the toast says there is not enough memory and the board keeps running.

The air name is `Mothdeck`. On an MPC that already bonded to `MothDeck`, forget that device and pair `Mothdeck`.

1.1.0 paired. The next build reserved the loop windows first and the MPC's pairing exchange failed, even when the radio looked up. This image frees those windows before NimBLE starts, which is the heap 1.1.0 paired with. The bond is still Just Works, no passkey, and the MIDI service is unchanged.

While a host is connected, the MIDI page shows the interval the other side accepted (`link 15.0ms`). The link asks for 7.5–15 ms a quarter of a second after connect. Connected playback drains MIDI more often and renders shorter blocks. If the MPC accepts 15 ms, expect about 25 ms end to end. If it stays at 30 ms, expect about 40 ms. The page shows the number that was negotiated.

SD loops open again as a set. They had been stopping at two files after BLE had taken the largest block. The windows are one pool, reserved while BLE is off. Turning BLE on frees that pool. Turning it off loads what still fits. A stored 256-step song loads as four patterns of 64, so the notes stay where they were. If a library still cannot open every file, the Loops page says how many opened.

A folder under `/moth/instruments` can be a patch. The file starts with `mothdeck-patch 1` and names a sound that already exists, plus the voice and FX numbers for one track. The card has no code. Older instrument folders load as before. On the Instrument page, highlight the folder and press Enter. `R` rescans. On the FX page the rows under Tremolo are Scale, Root, Arp, Glide, Osc2, Blend, and Coarse. Fn+`;` and Fn+`.` move the row. Fn+`,`, Fn+`/`, and Enter change the value.

Scale is off, major, minor, harmonic minor, mixolydian, phrygian, or chromatic. Root is C through B. Keyboard, sequencer, and BLE notes snap to the nearest scale tone, and a tie goes down. Arp is off, pat 1, pat 2, or held. Held keeps up to eight MIDI notes and steps once per 16th. The Cardputer keyboard is one note and does not send note-off, so a held chord comes from BLE. Glide is the time from the previous note to the new one, in steps of 10 ms, up to 500 ms on the device. Osc2 is a second sine, square, saw, or triangle, mixed by Blend and transposed by Coarse (−24..24). A song stores those rows only when one of them is in use, which makes the file version 4.

The SD pack still has the 808 and Dusty kits, the house loops, and the six sample instruments. It also has six patches: `a-minor`, `held-arp`, `glide-bass`, `saw-pluck`, `body`, and `room-drive`. Short plugins stay loaded while BLE is on. Loops do not.

## 1.1.0 (2026-10-09)

Flash `releases/mothdeck-cardputer-adv.bin` (1,134,080 bytes, magic `E9`, chip `0x0009`, SHA-256 `f8d9ab44082ab7ff5921dca9d30eb68ed8a440430bb019df31e4d82212569a4a`). Version on the Settings screen is 1.1.0. Host tests passed. BLE MIDI connect to an MPC Live II was confirmed on the build this image is based on. The 1–8 bar pattern length in this image has passed host tests and has not yet been confirmed on that MPC. Use earphones.

MothDeck shows up as a Bluetooth MIDI device. The name `MothDeck` and the MIDI service UUID sit in the primary advertising packet. Hardware that never reads the scan response, including the MPC Live II, can list it and pair. On the MPC: Menu, Preferences, Bluetooth, Pair, then Connect. Then MIDI / Sync, and enable MothDeck on the input ports. Turn Sync receive on when the MPC should run the clock.

Notes on MIDI channels 1–4 play tracks 1–4. Any other channel plays the track already selected. Note On with velocity 0 is Note Off, and hits are one-shots. CC 0 and CC 32 select the bank, CC 1 is the low pass, CC 7 and CC 39 are volume, and pitch bend bends the voice. Clock is 24 per quarter note. A step is a sixteenth, so six clocks move one step. A clock does not restart the pattern. Start puts the current pattern on bar 1 step 1. Continue resumes. Stop stops, and Space goes back to the internal clock. Song Position is a sixteenth-note index taken modulo the pattern length.

A pattern is 1 to 8 bars (16 to 128 steps). A new song is 1 bar. Play shows the bar as `B3/8`. Fn+`,` and Fn+`/` page that view. Settings has a Bars row, and the same Fn keys set the length without clearing notes. Song `N` starts a new song and steps the length from 1 to 8. Keys 5–8 select a pattern and stop at the last slot that fits in the 256-step grid: four patterns through 4 bars, three at 5 bars, two at 6–8 bars. A song saved with 16 steps loads as 1 bar. A song saved with 32 steps stays 2 bars. The file layout is unchanged.

MIDI has no message for the other machine's sequence length. If Start, or a Song Position of 0, arrives after a whole number of bars of clocks, the pattern adopts that length, capped at 8 bars. The MPC Live II manual documents Clock, Start, Stop, and Continue. It does not document a message when a sequence loops, and the Live II keeps the clock running through the loop without sending Start or Song Position. When nothing returns to zero, the length set on the device is the one that plays. A 1-bar pattern stays locked to a longer MPC sequence. A length that does not divide the MPC sequence drifts until the next Start or Song Position.

Playback no longer dies when Bluetooth starts. Audio and the tracker start before the sprite and before BLE. BLE is skipped when the largest free internal block is under 36KB, and the MIDI page says init failed. Space still plays. The screen sprite is rgb332 and is copied to the panel eight rows at a time, which keeps a second full-frame buffer from being allocated next to the speaker DMA.

Notes and clock in the same Bluetooth packet both count. An earlier parser treated a clock byte as the end of the packet and dropped the notes after it, and it kept only a handful of messages per packet. The parser now takes every message in the packet, including clocks between the data bytes of a note, running status inside one packet, and a SysEx that continues into the next packet. Incoming MIDI goes into a 128-event ring that the audio task drains, so drawing the screen does not stall it. Overflows are counted on the MIDI page (`pk`, `msg`, `ovf`, `clk`, `note`). After connect, the link asks for a 7.5–15 ms interval and accepts writes with or without a response.

Each live step is stored as two bytes (note, octave, instrument) instead of three separate grids. The song file on the card is still the old unpacked format. The release serial port prints the version, the advertising name, and a line when audio or BLE fails. Heap traces, advertising bytes, and a once-a-second MIDI count are on the dev build (`cardputer-adv-dev`). The counts stay on screen in the release.

## 2026-10-05

Flash `releases/mothdeck-cardputer-adv.bin` (1,129,152 bytes, magic `E9`, chip `0x0009`, SHA-256 `ac13390947360fa20de01e39a5ceb9e564af5612c051aa9a440c347dc976dc72`). That image is the build below. Host tests passed. It has not been listened to on a Cardputer. Use earphones.

Drums, sound effects, and one-shot samples on one track overlap. A new hit used to restart the only playback cursor, so a hat cut a kick that was still ringing. Each track keeps eight one-shots and mixes them. A loop on that track is mixed with the voice and then runs through the insert, instead of replacing the voice.

The insert was stair-stepped. The delay line kept every other sample, so delay, chorus, and reverb aliased. Chorus and tremolo ran their LFOs at audio rate. The filter moved in whole samples, so a dark cutoff stuck and then jumped. The line is full rate now (about 186 ms; a 1/8 delay clamps to that instead of folding). Chorus and tremolo are slow sines, the filter keeps a fractional state, and delay and reverb lowpass their tails. Bitcrush is still the control that is supposed to sound lo-fi. On the ESP32-S3 the output clamp, the delay and reverb poles, and the fractional delay read use CLAMPS and MULL. The same helpers cover the rest of the sample loop where the product still fits in 32 bits: shot interpolation, drive gain, chorus and tremolo, the synth lowpass and amplitude, pitch bend, and the PCM saturator. The filter's fractional state stays a wide multiply.

The README, the Pages landing page, and `releases/INSTALL.md` say to use earphones or headphones. The Cardputer ADV internal speaker does not play with this firmware. The codec and I2S pins already match M5Unified's ADV speaker profile, and the NS4150B enable is the jack switch on the board rather than a GPIO this image can turn on. No speaker-path change is in this note.

The README, `docs/FORMATS.md`, and `releases/INSTALL.md` now describe this ADV image end to end: boot and splash, drums and kit loading, Mixer and FX, streamed loops, the 8-bit 22050 Hz sample format, the free/total memory row, the SD pack, and the hardware limits of the Stamp-S3A. This tree builds one target, `cardputer-adv`.

The moth splash was blue because `pushImage` sends a `uint16_t` buffer as already byte-swapped RGB565. Amber `0xFD20` left the chip as `0x20FD`, which this panel draws as blue, while the grey background still looks grey. The splash now marks the buffer as logical RGB565, so the moth is amber on grey.

That same image reset a moment after Play. The UI sprite had been cut to 8-bit, so every frame was expanded onto the panel through the SPI DMA path, and the loop windows (about 21KB) sat in BSS before BLE and the speaker started. The sprite is RGB565 again, and a loop window is allocated only when that loop opens. Boot stays on Play.

Built-in drums, built-in sound effects, and the SD pack are unsigned 8-bit mono PCM at 22050 Hz (128 is silence, about 22 KB/s). The mix is still 44100 Hz. 16-bit WAV still loads. `tools/wav_to_instrument.py` and `tools/wav_to_loop.py` write that format. `--raw` stays little-endian int16.

Drums are separate kit pieces again. Each of the twelve pads is its own sound (kick, rim, snare, clap, closed hat, open hat, low tom, tom, shaker, ride, snap, crash), played at the recorded pitch. The octave row no longer speeds the same sample up, which is what made them sound like one hit in different keys. `,` and `/` on the Instrument page load the previous or next kit from `/moth/drums` onto the Drums instrument. The line under the list is the kit name.

Settings used to print free internal heap as a bare `6k`. That number is bytes still free in the internal heap after BLE, the screen buffer, and the audio DMA, divided by 1024. It is not total RAM and not flash. The row is now `free/total` (for example `86/312k`), and the legend is the largest block a load can use.

Card audio no longer copies a whole file into the heap and then copies it again. Drum kits and plugin samples are read straight into a small cache (8000 kit frames, 1800 per pad, 4096 per plugin, four plugins). Loops stream from the card. A load that does not fit says how many kilobytes it needs and how many are free. The Stamp-S3A cannot grow RAM with a PSRAM chip: those pins are the display, and both SPI buses are already in use. The microSD is the sample memory.

Sound is back on the build that lit the panel. That build set `internal_spk` off so the speaker would not run inside `M5.begin`. On the Cardputer ADV that flag is what assigns I2S (BCLK GPIO41, WS GPIO43, data GPIO42) and registers the ES8311 power-up callback. `Speaker.begin()` later then ran with no data pin, so the codec stayed in reset and nothing reached the NS4150B. GPIO42 is the codec data input, not an amp-enable to drive high. The speaker profile is installed again during display bring-up, and `audioStart()` (still after the splash and the Play frame) writes the ES8311 registers, starts I2S on those pins, and sets the volume to 160.

Boot shows a one-second moth splash with the backlight already on, then the Play page, then the card scan, the speaker, and BLE. The splash does not wait for a key. Regenerate it with `python3 tools/make_splash.py`.

Boot no longer sits on a black panel. M5.begin was saving a brightness of 0 from before the ST7789 exists, clearing the screen, and writing that 0 back, so the backlight stayed off until late in setup. The backlight is turned on before the card scan, the speaker, or BLE. External display probes are not run at startup.

The Mixer page is now FX: filter, BPM-synced delay, reverb send, bitcrush, drive, chorus, and tremolo, each stored on the selected track and saved in a version 3 song. A new Mixer page shows all four tracks as volume faders. Fn+`;` and Fn+`.` change the selected track's volume, Fn+`,` and Fn+`/` select the track, and `` ` `` returns to Play. Per-track volume was already in the song file. Songs with no insert effect stay version 1 or 2.

The built-in drum kit is Ambient House: a deep kick with a pitch drop, a soft snare, a clap, closed and open hats, and the rest of a small kit, each with a short room in the sample. Sound effects are twelve different one-shots (riser, zap, impact, siren, and the rest), one per key. Sine stays a sine. Square, saw, triangle, organ, pluck, bell, flute, bass, and pad are voiced in real time from those names: the pad attacks slowly, stays detuned and dark, and holds. The Instrument page can load an SD drum kit into Drums (Fn comma and slash) and a Loops instrument that triggers card loops in time with the BPM. Kits and loops live in mothdeck-sd-pack.zip, not in the firmware image. otadata is still erased only after a Launcher image is verified.

Exit to Launcher is enabled only when the APP_TEST slot contains a valid ESP32-S3 app image (magic E9). A full-flash layout, an empty slot, or a corrupt header greys the menu entry and shows "Launcher not found". Holding Esc does not restart, and otadata is erased only after that check passes.

An open menu takes the keyboard. The page list, the BLE name editor, and the Exit confirm consume every key, so notes, recording, transport, track, pattern, and BPM changes do not reach the page underneath. Fn arrows or Tab move the page list, Enter confirms it, and grave or Backspace cancels. Holding grave or the front button still leaves for Launcher.

Instrument assignment stays on the track that was selected. Each track keeps its own instrument, including a plugin or anything other than the drum bank. Selecting another track recalls that track's instrument and does not copy the previous one across. The Instrument page lists the instrument on tracks 1–4, and the Play page shows each track's name.
