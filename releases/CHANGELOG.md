## 2026-10-05

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
