# MothDeck file formats

The firmware and the host tests share these parsers. A bad file returns an error string and is skipped. It does not reset the device.

Names used in paths (`name`, `sample`, `loop`, `pattern`, folder names) are at most 23 characters and may contain letters, digits, `_`, `-`, and a single `.`. `..`, slashes, and empty names are rejected.

## Song slots

Path: `/moth/slot1.mos` through `/moth/slot4.mos`.

A song that only uses the built-in instruments, and no loop assignment, is version 1. That file is 3155 bytes and matches MothOS:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `MOTH` |
| 4 | 1 | version `1` |
| 5 | 2 | pattern length, uint16 little-endian, multiple of 4, 4..256 |
| 7 | 1 | master volume |
| 8 | 1 | bpm slot 0..3 |
| 9 | 4 | four BPM values |
| 13 | 1 | current instrument 0..11 |
| 14 | 1 | selected track 0..3 |
| 15 | 1 | current pattern 0..3 |
| 16 | 1 | song mode (non-zero = all patterns) |
| 17 | 1024 | notes, 4 tracks × 256 steps |
| 1041 | 1024 | octaves, signed |
| 2065 | 1024 | instruments per step |
| 3089 | 64 | 4 voices × 16 bytes |
| 3153 | 2 | checksum, uint16 sum of the preceding bytes |

Each voice is: volume, mute, sampler mode, overdrive, octave, envelope, envelope length, phaser, low pass, retrig, chord, pitch, delay, whoosh, then pitch-bend uint16.

Version 2 is written only when a plugin id (12..63) or a loop assignment is in use. It keeps the version 1 body, then:

| Field | Size |
| --- | --- |
| plugin count | 1 (0..8) |
| each plugin | id uint8, then 24-byte name |
| four loop records | enabled, quantize, library[24], name[24] |
| checksum | uint16 over every byte before it |

Quantize is 0 immediate, 1 next beat, 2 next bar. A version 2 file is at most 3556 bytes. MothOS rejects version 2, which is why plain songs stay version 1.

Version 3 is version 2 plus 48 bytes, 12 per track, written only when an insert effect is in use. The largest file is 3604 bytes. Each track's 12 bytes are: filter (0 off, 1 low pass, 2 high pass), cutoff 0..127, resonance 0..80, delay division (0 off, 1 = 1/32, 2 = 1/16, 3 = 1/8), delay feedback 0..70, delay mix 0..100, reverb send 0..100, bitcrush 0..4, drive 0..100, chorus 0..100, tremolo 0..100, and one reserved byte. Values above those ranges are clamped on load. A version 1 or 2 file loads with every insert off. Per-track volume is the first byte of the 16-byte voice and is stored in every version.

## Instrument folder

`/moth/instruments/<folder>/manifest.txt`

```
mothdeck-instrument 1
name=soft-pluck
type=sample
sample=sample.wav
root=60
rate=22050
loop_start=0
loop_end=0
oneshot=0
attack=0
decay=40
sustain=80
release=20
gain=100
cutoff=100
resonance=0
wave=saw
fm_ratio=2
fm_index=40
```

`type` is `sample`, `wavetable`, `subtractive`, or `fm`. `sample` and `wavetable` require `sample=`. Audio is unsigned 8-bit mono WAV at 22050 Hz (128 is silence, about 22 KB/s), or little-endian int16 raw with no header. A 16-bit mono WAV still loads, and a stereo file is mixed to mono. `root` is the MIDI note the recording is at. Loop end must be greater than loop start when it is not zero. `wave` is `sine`, `square`, `saw`, or `triangle`. Synth types are rendered once, at load, into a 168-frame cycle.

The folder name is the id stored in a song. Up to 4 instruments stay in memory. Without PSRAM a sample is kept to 4096 frames. Plugin ids are 12..63.

## Loop library

`/moth/loops/<library>/manifest.txt`

```
mothdeck-loops 1
name=house-120
bpm=120
bars=1
tags=drums,house
loop=kick.wav
loop=hats.wav
pattern=offbeat.pat
```

`bpm` is 40..240. `bars` is 1..8. Up to 16 entries in a file; the browser keeps 8. Audio is unsigned 8-bit mono WAV at 22050 Hz, or raw int16. A 16-bit WAV still loads. Playback resamples with linear interpolation so the loop's BPM matches the project BPM. Pitch moves with tempo. Launch quantize is immediate, next beat, or next bar.

The **Loops** instrument (id 63, the row after Pad) plays these audio loops from the keyboard. Notes wrap across the audio entries that fit in the stream cache (four), and the loop phase is lined up with the current bar. Playback reads the WAV from the card through a short window, so the file is not copied into the heap. A song that selects Loops is version 2. Plugin folders use ids 12..62 so they do not collide with it.

## Drum kit

`/moth/drums/<folder>/manifest.txt`

```
mothdeck-kit 1
name=808
pad=kick.wav
pad=rim.wav
pad=snare.wav
pad=clap.wav
pad=hat.wav
pad=openhat.wav
pad=perc.wav
pad=tom.wav
pad=shaker.wav
pad=ride.wav
pad=snap.wav
pad=crash.wav
```

Exactly twelve `pad=` lines, in keyboard order: C kick, C# rim, D snare, D# clap, E closed hat, F open hat, F# low tom, G tom, G# shaker, A ride, A# snap, B crash. `name` is a display label and may contain spaces. Pad filenames follow the same rules as other manifest filenames.

Audio is unsigned 8-bit mono PCM WAV at 22050 Hz (128 is silence). A 16-bit WAV still loads. On the Cardputer ADV (no PSRAM) each pad is stored up to 1800 frames and the kit up to 8000 frames, expanded to 16-bit in RAM. A longer pad is shortened. If the cache cannot be allocated, the toast reports kilobytes needed and kilobytes free, and the previous kit stays selected. The built-in kit is in flash and is not a file. On the Instrument page, `,` and `/` switch kits (Fn+`,` and Fn+`/` do the same). Selecting the built-in kit drops the SD buffers. The card is never erased by a kit change. Pads play at the recorded pitch. Changing octave does not transpose them.

A pattern file:

```
mothdeck-pattern 1
bars=1
steps=0,0,8,0,0,0,8,0
```

`bars` is 1..4. Each step is 0 (rest) or 1..12 (pitch + 1 in the current octave). Launching a pattern writes those steps onto the selected track.
