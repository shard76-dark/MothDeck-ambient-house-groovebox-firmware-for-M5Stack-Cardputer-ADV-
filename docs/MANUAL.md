# MothDeck manual

MothDeck is groovebox firmware for the M5Stack Cardputer ADV. Use earphones or headphones in the 3.5 mm jack. The internal speaker stays quiet. This image has not been flashed or listened to on a Cardputer.

The browser installer is [flash.html](flash.html). It writes the full-flash image and replaces Launcher. The application image in `releases/` is the file Launcher installs, and it leaves Launcher in place. Checksums are in `releases/SHA256SUMS`.

## Hear it

Plug in earphones before you play. Unplugging them does not move the sound to the internal speaker.

## A song

Four tracks. A pattern is 1 to 8 bars of 16th notes. The grid holds four patterns while a pattern is 4 bars or shorter, and fewer once a pattern is longer. A new song is one bar.

Play opens on a piano roll. Time runs across and pitch runs down the left. A note is a block one to four steps long. Enter switches to the 16-step strip.

## Moving

Tab steps through Play, Instrument, FX, Mixer, Song, Loops, MIDI, Settings, and Exit. Exit is in the list only when a Launcher image is on the flash. Shift+Tab goes back one page. Grave on any page other than Play returns to Play. On Play, grave opens the page list.

Space starts and stops. Starting arms Rec. Stopping clears Rec.

## Play

Fn+`;` and Fn+`.` move pitch. Fn+`,` and Fn+`/` move time. A piano key while stopped writes that pitch at the cursor. Backspace deletes the note under the cursor. `;` on a note cycles its length from one to four steps.

## Instruments

The Instrument page assigns the selected track only. Drums and sound effects are signed 16-bit at 44100 Hz, in flash. A drum at its recorded pitch plays one frame per output sample. Pitched reads use 4-point Hermite interpolation. `,` and `/` load the previous or next kit from `/moth/drums`. A kit that fits the 16 KB cache is copied in. A larger kit streams through that cache and does not take the loop windows. Plugin samples stay within 4096 frames so they still load while BLE is on.

Enter on a patch folder applies that patch to the selected track. Scale, root, arp, glide, a second oscillator, blend, and coarse are the extra rows on the FX page.

## FX and the mixer

Each track has its own filter, delay, reverb send, bitcrush, drive, chorus, and tremolo. The mixer page is four volume faders. Mute and solo are per track.

## Song page

`S` saves the slot, `L` loads it, `X` deletes it, `T` shows whether the slot has a song. Keys 1–4 pick the slot. `N` starts a new song and steps the length from 1 to 8 bars. `G` turns song mode on, which plays every pattern in order when the chain is empty.

`J` stores the current pattern and the four track mutes as the next scene, up to eight. `P` clears the chain. While a chain is stored, playback walks it at the end of each pattern. An empty chain leaves song mode alone. A song with a chain is version 5. An older song still loads.

`U` records the next 8192 frames of the master mix, about 0.19 seconds, into a fixed buffer. `O` plays that take on top of the tracks. `Y` plays it an octave higher. The buffer is part of the firmware. Recording hears the tracks and not the take. Drums, loops, BLE, plugins, and the chain keep working whether or not a take is stored.

## Loops

Loops stream from the card. BLE is off at boot, so the loop windows are reserved then. Turning BLE on frees that pool before the radio starts. Turning it off loads the windows that still fit. If a library cannot open every loop, the page says how many opened.

## MIDI

The air name is `Mothdeck`. Forget an older `MothDeck` bond on the other machine before pairing. Notes on channels 1–4 play tracks 1–4. Any other channel plays the selected track. Incoming notes are stored only while Rec is on. MIDI Start and Continue do not arm Rec.

An external clock counts every clock byte in a packet. Six clocks advance one 16th. The tempo readout tracks about 40–200 BPM. A track sounds up to four notes at once. A fifth replaces the oldest. Note-off releases only the matching note.

## Settings

Rows are Speaker, Brightness, BLE name, BLE, Unload BLE, Battery, Radio, Card, Bars, and WiFi.

Fn+`,` and Fn+`/` change the highlighted row. BLE on and off do not unload loops by themselves. If the open project has loops, turning BLE on asks you to save, then frees the loop windows and starts advertising. Unload BLE takes effect on the next boot. The toast says `BLE unloads after restart`.

The WiFi row starts an access point named `Mothdeck`, password `mothdeck`. Open `http://192.168.4.1/` for the files under `/moth`. Download, upload, and delete stay inside that folder. A path with `..` is rejected. An upload is stored as `/moth/_upload.tmp` and replaces the old file only after the whole body arrives. The card is locked one chunk at a time, so a loop can still refill. If the access point cannot start, the toast says `WiFi unchanged` and BLE stays as it was.

## The card

Unzip `releases/mothdeck-sd-pack.zip` onto the card root.

| Path | What it is |
| --- | --- |
| `/moth/slot1.mos` … `slot4.mos` | Songs |
| `/moth/drums/` | Kits |
| `/moth/loops/` | Loop libraries |
| `/moth/instruments/` | Sample instruments and patches |
| `/moth/sfx/` | Long effects, used by the TTGO image |

Song bytes are in [FORMATS.md](FORMATS.md).

## LilyGO TTGO T8

The T8 port is a different binary on its own branch. Do not flash a Cardputer image onto a T8, or the other way around. The T8 factory slot and its instruction RAM cannot hold this Wi-Fi stack beside BLE, so the T8 image does not include the file page. Its manual and flasher are on that branch.

## What has not been tried

Host tests passed for the parsers, the chain, the sampler, and the Wi-Fi paths. The images in this tree have not been flashed or listened to.
