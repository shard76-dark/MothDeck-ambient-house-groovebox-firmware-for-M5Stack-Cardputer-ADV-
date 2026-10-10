## 1.2.2 (2026-10-10)

Flash `releases/mothdeck-cardputer-adv.bin` (1,151,680 bytes, magic `E9`, chip `0x0009`, SHA-256 `db42620b1a79ca2c581ffc5df771400e3f5b2e7fb88eb7f2b3b9718e03121629`). Version on the Settings screen is 1.2.2. Host tests passed. Use earphones. Chris approved this image.

NimBLE is reserved at boot, before the loop windows, whether or not it is advertising. Turning BLE on starts advertising and does not allocate, so it does not fail for lack of a free block. Unload BLE waits for the next boot and frees that block for large loops. The toast says `BLE unloads after restart`. Incoming MIDI notes play and are not stored while Rec is off. Start and Continue do not arm Rec. Space arms Rec, and then those notes are written at the playhead.

The USB full-flash image is `releases/mothdeck-cardputer-adv-full.bin` (1,217,216 bytes, SHA-256 `b492f920d0eb2202fa4397fa3bbe1f3758285474724f9a92251f56530e973762`). It replaces Launcher. Install `mothdeck-cardputer-adv.bin` from Launcher.

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
