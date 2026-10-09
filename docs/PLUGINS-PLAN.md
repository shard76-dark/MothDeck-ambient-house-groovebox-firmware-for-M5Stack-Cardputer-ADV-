# SD-loadable plugins

Design note only. No firmware in this change. Implementation waits until Chris approves this revision.

Chris chose option (b). A plugin is a text file on the microSD card. It names a sound MothDeck already plays and the voice and FX numbers that already exist, and the firmware applies those numbers. The card never supplies code.

LOOPA ([aftersound.tech](https://aftersound.tech), source [ferluht/loopa](https://github.com/ferluht/loopa), GPLv3) is the source of the ideas in the shortlist. Nothing from that tree is included here. The node sketch in the first draft of this note (`osc`, `mix`, `env`, `lpf`, `drive`, `scale`, `arp` as a free graph) is withdrawn. Those names are not blocks in this firmware.

The target is the Stamp-S3A: ESP32-S3, no PSRAM, internal RAM only, BLE that needs a large contiguous block, and a mono 44100 Hz mix of four tracks.

Survey date: the `master` tree of `ferluht/loopa` as published on GitHub, plus the Dev Studio page on aftersound.tech. Midiphy LoopA is a different product and is not part of this survey.

## 1. What LOOPA ships

LOOPA devices are C++ classes compiled into the Pi program. Each one is registered with a compile-time factory. The Dev Studio on the website is the same idea in a browser: two editors titled `MyDevice.h` and `MyDevice.cpp`, a prompt you paste into an external chat model, and a build. The page classifies a device by the base it inherits: a polyphonic instrument, an audio effect, or a MIDI effect. The stock skeleton is an instrument with per-voice state, an attack parameter, a release parameter, a handler for incoming MIDI, and a float audio callback. That output is source to compile for the Pi. It is not a file the Cardputer can load.

The public tree's device folders are small. Empty slots exist so a track can have "no instrument" or "no effect". Helpers that are not separate menu devices are listed too, because they live in those folders.

### Instruments

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty slot | Draws "no instrument" and makes no sound. | None | None | Already how an unused MothDeck track behaves. Not a plugin. |
| Simple (SIMPL) | Polyphonic sine voice with attack, decay, sustain, and release. | Low | A few floats per voice | The waveforms and four envelope curves already exist. A free ADSR does not. See the shortlist. |
| Single tone (SYNTH) | One voice, two oscillators blended by a cross-mod amount, plus glide time. | Low | A few floats | Built-in Bass and Saw are already two-oscillator voices. A folder cannot pick a second wave or a glide time. See the shortlist. |
| Sampler | Plays a file as one-shot, enveloped, or looped, with cubic interpolation, loop points, and recording. | Medium if the interpolator runs every sample | The whole file, in float, plus a waveform cache | The short player already exists, capped at 4096 frames. Recording and cubic interpolation do not. |
| Sample kit | Several files, one per note, sharing the sampler path. | Same as the sampler | One buffer per note | Drum kits already do twelve pads from the card. |
| Mic (MIC) | Passes the microphone or line input through a note envelope and a release control. | Low | Small | The codec input is not an instrument. Left off the list. |

### Audio effects

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty effect | Copies the input buffer to the output. | None | None | Skip. |
| Chronoblob | Stereo ping-pong delay with fixed times around half a second and a high wet mix. It wraps the ping-pong helper. | Low, apart from the memory traffic | Two delay lines of that length | The mix is mono. The tempo delay on the FX page is the block a patch can set. |
| Ping-pong helper | The stereo delay Chronoblob uses. Not its own menu entry. | Low | Same delay lines | Same as Chronoblob. |
| Delay | Tempo delay with dry/wet and feedback. The history is on the order of two million samples per channel, and it uses a resampler library. | The resampler is heavier than a simple delay | Tens of megabytes | The FX page delay is the block. That buffer cannot exist here. |
| Simple delay | An empty class. | None | None | Skip. |
| Soft clipper | Waves the signal with a soft saturation curve. | Tiny | A few floats | The FX page Drive and the voice overdrive flag are the blocks. |
| Plateau | A room-style reverb (size, decay, dry/wet) built on a Dattorro tank and several all-pass and interpolated delay lines. | High for this chip: many delay reads per sample | The tank is large even in a cut-down form | The FX page reverb send is the block. A tank is not a small addition. |
| Tanh | Drive, bias, output, and an emphasis shelf into a saturator. | Low if the shelf is one pole and the shaper is a short polynomial | A few floats | Drive covers the level. Bias and the shelf are not a block. |
| Tape | A multi-scene looper: record, overdub, varispeed, copy a scene. | Medium, and it touches a huge buffer | The audio buffer is the loop itself | Loops already stream from the card, and they unload while BLE is on. |
| New tape | The later looper: playheads, varispeed, rewind, double the loop, scenes, save and load. The length constant is minutes of stereo at 44100 Hz. | Medium to high | Minutes of stereo audio | Same. This is the Pi's looper. |
| Loop matrix | A 2×2 grid of those tapes, with sends and scene copy. | Four loopers | Four of those buffers | Skip. |

### MIDI effects

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty MIDI effect | Lets notes through. | None | None | Skip. |
| Scale | Forces notes into a chosen scale and root: major, minor, harmonic minor, mixolydian, phrygian, or chromatic. | None on the sample loop. It rewrites note numbers. | A 128-entry map | No scale block exists. Flagged below as a new small block. |
| Arpeggiator | Holds the notes that are down and walks them at a rate that can follow the clock. | Tiny. It runs once per audio block, not per sample. | The set of held notes | `chordMult` walks two fixed intervals on a built-in tone. It is not this walker. Flagged below. |

### Shortlist

These are the LOOPA ideas to express as patches. Section 3 writes each one as keys the file can already set, and marks the ones that need a new small block before they can load.

1. Scale.
2. Arpeggiator.
3. Dual-oscillator glide voice.
4. Oscillator plus envelope.
5. Short sample player.
6. Low-pass.
7. Drive.
8. The existing tempo delay, reverb, chorus, crush, and tremolo, set as inserts from the same file.

Left off the list, because they are large buffers, stereo, or a missing input path: tape, the new tape, the loop matrix, plateau, ping-pong, and the microphone. They are not "a new small block."

## 2. What already runs

### Card plugins today

A plugin is a folder `/moth/instruments/<folder>/` with `manifest.txt`. The firmware scans up to twelve folders. Ids 12 through 62 are plugins. Id 63 is the Loops instrument. A song stores the folder name (version 2). A missing folder falls back to drums. Built-in songs stay a 3155-byte version 1 file. Inserts make version 3.

`type` is one of four:

- `sample` and `wavetable` play a file. Unsigned 8-bit mono WAV at 22050 Hz is the usual pack. A 16-bit WAV is folded to mono. `sample.raw` is little-endian int16. Without PSRAM the cap is **4096 frames**, about 8 KB. At most **four** plugins stay resident (`kLiveSlots`). A 4096-frame sample at 22050 Hz is under a fifth of a second.
- `subtractive` calls `renderSubtractive(wave, cutoff, resonance)` once at load into a **168-frame** cycle. `fm` calls `renderFm(ratio, index)` into the same size. Playback is a wavetable read. One wave only: sine, square, saw, or triangle. The FM renderer is two sines locked to an integer ratio, baked, not a live second oscillator.

The manifest keys `attack`, `decay`, `sustain`, and `release` are parsed and then ignored. Playback does not read them.

The twelve built-ins are firmware, ids 0–11: Drums, SFX, Sine, Square, Saw, Tri, Organ, Pluck, Bell, Flute, Bass, Pad. Ids 2–11 are `ToneSynth`, a live voice (`phase`, `phaseB`, `phaseC`, an LFO, noise, a one-pole, an amplitude). Saw is two detuned saws. Bass is a sub sine plus two detuned saws. Pad is two detuned triangles plus a sine. Organ is four harmonic sines. A folder cannot point at `phaseB` or change those ratios. Drum kits are a separate manifest: twelve pads, 1800 frames each, 8000 for the kit.

### Voice numbers a patch can copy

These live on the track and are already stored in the song:

| Field | Range | What it does |
| --- | --- | --- |
| `volume` | as stored | Track level. |
| `octave` | as stored | Transpose. |
| `samplerMode` | 0 or 1 | On a built-in track, the note picks which built-in tone plays. |
| `overdrive` | 0 or 1 | Hard boost and clip. Used when FX Drive is 0. |
| `envelope` | 0–3 | One of four baked curves. 0 decays. 1 rises then dies. 2 holds. 3 holds and restarts the index so it loops. |
| `envlen` | the stored length | How fast the index walks that curve. |
| `lowpass` | 0–2 | A short average of the history. Runs only when every FX insert field is off. |
| `reverb` (voice) | 0–2 | The old tapped reverb. Same condition: inserts off. |
| `delay` (voice) | 0–2 | A single history tap. Same condition. |
| `wobble` | 0–2 | `phaserMult`. A slow mix with a moving history tap. |
| `whoosh` | 0–2 | A moving average of the first few history samples. |
| `pitch` | 0–2 | `pitchMult`. From note-on, 1 sweeps the pitch down, 2 sweeps it up, for a few thousand samples. It does not glide from the previous note. |
| `arp` | 0–2 | `chordMult`. On built-in tones 2–11 only. 1 walks the note, then four semitones down, then three up. 2 walks the note, then five down, then seven up. Each stage is 1500 samples, about 34 ms. Plugin samples ignore it. |

`bend14` is the live pitch wheel. A patch does not set it.

### FX page

Each track has a `TrackFx`. The history behind it is already allocated: `kHistoryLen` is 8192 int16 samples per voice, about 186 ms, four voices, about 64 KB of static RAM. Delay, chorus, and the insert reverb share that line. A patch must not allocate another one.

| FX row | Field | Range |
| --- | --- | --- |
| Filter | `filter` | 0 off, 1 low-pass, 2 high-pass |
| Cutoff | `cutoff` | 0–127 |
| Res | `res` | 0–80 |
| Delay | `delay` | 0 off, 1 = 1/32, 2 = 1/16, 3 = 1/8 |
| Feedback | `feedback` | 0–70 |
| Mix | `mix` | 0–100 |
| Reverb | `reverb` | 0–100 |
| Crush | `crush` | 0–4 |
| Drive | `drive` | 0–100. Gain into a knee, then a soft clip. |
| Chorus | `chorus` | 0–100 |
| Tremolo | `tremolo` | 0–100, mono |

Any non-zero insert turns off the old `lowpass` / voice-reverb / voice-delay path. A patch that wants a filter uses `filter`, `cutoff`, and `res`.

### Load path today

`InstrumentBank::Scan` lists folders. `Load` / `LoadFolder` run on the UI task. The sample buffer is published in `instrumentView` only after it is full. Unload clears `ready`, waits, then frees. The audio task never parses the card and never allocates. A bad file keeps the previous instrument and the screen shows the error. A kit that does not fit says `Need Nk, Mk free`.

### BLE

As of 1.1.2 the radio is off at boot. Turning it on unloads streaming loop windows, because those windows and the NimBLE host do not fit together. A short plugin sample is ordinary heap, four times 8 KB, and can stay while BLE is on. Do not raise the 4096-frame cap.

## 3. The patch file

Header `mothdeck-patch 1`, same folder as today: `/moth/instruments/<folder>/manifest.txt`. One file is one preset. It selects one source and then sets the voice and FX fields above. Keys are `name=value`, integers except `name`, `builtin`, `wave`, and `sample`. Names follow `docs/FORMATS.md`: no slashes, no `..`. The text stays inside the 1024-byte read the loader already uses.

Unknown keys are a failed load. The previous instrument stays, and the toast names the key. That is how `scale`, `glide`, and a second oscillator stay out until their blocks exist.

### Source, one of these

`source=builtin` and `builtin=` one of `drums`, `sfx`, `sine`, `square`, `saw`, `tri`, `organ`, `pluck`, `bell`, `flute`, `bass`, `pad`. No sample buffer. Playback is the built-in engine.

`source=sample` or `source=wavetable`, plus the keys the current instrument manifest already accepts: `sample`, `root`, `rate`, `loop_start`, `loop_end`, `oneshot`, `gain`. The existing WAV and raw loader, the 4096-frame cap, and the four resident slots apply.

`source=subtractive`, plus `wave` (`sine`, `square`, `saw`, `triangle`), `cutoff` (1–100, the baked cycle filter), `resonance` (0–90), `gain`. Rendered once at load into the 168-frame cycle.

`source=fm`, plus `fm_ratio` (1–16), `fm_index` (0–100), `gain`. Same cycle.

`attack`, `decay`, `sustain`, and `release` are not accepted on a patch. They do nothing on the current instrument files either.

### Voice and FX keys

All optional. A missing key leaves that track field as it was.

`volume`, `octave`, `sampler`, `overdrive`, `envelope`, `envlen`, `lowpass`, `whoosh`, `wobble`, `pitch`, `arp`.

`filter`, `cutoff`, `res`, `delay`, `feedback`, `mix`, `reverb`, `crush`, `drive`, `chorus`, `tremolo`.

`arp` writes `chordMult` and only changes sound on built-ins 2–11. `pitch` writes the one-shot sweep, not a glide.

### What assign does

The Instrument page already lists folders under the built-ins. A patch folder shows `name=`. Enter assigns it to the selected track:

- Built-in source: the track's instrument id becomes 0–11. The voice fields and `TrackFx` are copied onto that track. Nothing is allocated. The song stores those numbers the way it already stores a voice and, if any insert is on, a version 3 effect block. The folder is not re-read when the song loads. Editing Cutoff afterward is what gets saved.
- Sample, wavetable, subtractive, or FM source: the track's instrument id becomes 12–62 and the song stores the folder name, same as a plugin today. Load fills the existing sample slot. The voice and FX copy happens once, at assign, and later edits win on save. Opening the song again loads the sample from the folder and restores the saved numbers. It does not paint the patch file back over those edits.

There is no graph editor and no extra rows. The FX page is the eleven rows that already exist. Envelope, overdrive, ArpChord, Low Pass, Pitchbend, Whoosh, and Wobble stay on the keys that already edit them. Authoring is the text file.

### Load and unload

Same task split as `InstrumentBank::LoadFolder`.

1. The UI reads the file into the existing stack buffer and parses it into a fixed struct. No heap for the text.
2. A bad header, an unknown key, a value out of range, a missing sample, or a sample over the cap frees anything this attempt allocated, toasts the reason, and leaves the previous instrument selected.
3. A sample or a cycle is allocated with the existing helper, filled, then published with `ready` set. The audio task sees the old view or the new one.
4. Unload of that buffer is the existing four-slot rule. When a fifth folder needs a slot, or the last track leaves this folder, clear `ready`, wait the same short gap the loader already waits, then free. A built-in preset has no buffer; leaving the instrument is the unload.
5. A folder that disappears on rescan drops off the list. A song that still names it falls back to drums.
6. The audio task does not read the card for this.

Current firmware rejects the new header (`Bad instrument magic`) and still lists the folder with that error. It does not hide the folder. After this parser exists, those folders load. Devices that stay on today's firmware keep showing the error until they are updated.

BLE does not unload a patch. Loop windows are the thing that leaves when the radio comes on. A track that only uses a patch does not raise the save-and-new-project question.

### Memory

A patch adds no interpreter and no per-sample node walk. The cycles are whatever the chosen built-in, sample reader, or insert already costs. The earlier 1500-cycle graph budget is withdrawn with the graph.

| Use | Budget |
| --- | --- |
| Patch text | The existing 1024-byte read. Discarded after parse. |
| Parsed preset | A few hundred bytes on the stack during load. After assign, the numbers live in the voice and `TrackFx`, which already exist. |
| Built-in source | No extra RAM. |
| Sample or cycle | Unchanged: 4096 frames, four resident, about 8 KB each. A cycle is 168 frames. |
| Delay, chorus, reverb | The 8192-sample history already on each voice. No second line. |
| BLE | No loop-pool allocation. No buffer bigger than one plugin sample. |

If the three small blocks below are approved later, their state is a 128-byte scale map plus a handful of notes and a glide phase per track. That stays under a few hundred bytes and does not touch the BLE block.

### Native code stays out

An ELF or position-independent blob needs executable RAM. This chip has no PSRAM, and the 1.1.2 link already uses all 16 KB of IRAM. The card is a SPI filesystem; code cannot run from it. A blob written into a flash partition breaks every firmware update, and a bad store crashes the audio task. A blob built from LOOPA would also be GPL inside an MIT image. A graph-plus-native hybrid is the same problem for one opcode. The file format has no escape opcode.

## 4. Each shortlist item

### Scale — new small block

No block remaps notes. A patch cannot express this.

The block, when added, is a 128-byte table from incoming note to scale note, chosen root, modes limited to major, minor, harmonic minor, mixolydian, phrygian, and chromatic. It runs where a BLE or local note becomes `SetNote`, before the voice. No audio RAM, no change while BLE is on. Until that lands, a file with `scale=` or `root=` fails the load.

Sketch, not accepted by the first parser:

```
mothdeck-patch 1
name=a-minor
source=builtin
builtin=bass
scale=minor
root=9
```

### Arpeggiator — existing stand-in, walker is a new small block

This loads today, on a built-in tone. It is the fixed three-step walk, about 34 ms a stage, not a chord you are holding:

```
mothdeck-patch 1
name=arp-bass
source=builtin
builtin=bass
arp=1
envelope=2
envlen=2
filter=1
cutoff=50
res=10
```

`arp=2` is the other pair (five down, seven up). `arp` on a sample plugin does nothing audible, because `chordMult` only runs inside the built-in waveform reader.

The LOOPA walker holds whatever notes are down and steps them on a clock division. That needs a new small block: a few note slots per track and a step counter, run once per block next to the scale remap, still no sample RAM. A file that says `arp=held` fails until that block exists. The first parser accepts only `arp=0`, `arp=1`, and `arp=2`.

### Dual-oscillator glide — preset of Bass or Saw; live blend and glide are a new small block

Bass is already a sub sine plus two detuned saws through a low-pass. Saw is two saws a few cents apart. This is the patch, and it needs no new block:

```
mothdeck-patch 1
name=glide-bass
source=builtin
builtin=bass
envelope=2
envlen=1
filter=1
cutoff=36
res=18
drive=25
pitch=1
```

`pitch=1` is the existing downward sweep from note-on. It is not a glide from the last note to the new one. `source=fm` is the other existing two-sine colour, baked at load, integer ratio only. `source=subtractive` is one wave.

A folder still cannot name a second waveform, a blend, a non-harmonic detune, or a glide time. Those are one new small block if Chris wants them: a second phase accumulator, a coarse transpose, a blend, and a glide time in milliseconds, a few integers per voice, using the oscillators `ToneSynth` already has. No new delay line. Until then the keys `osc2`, `blend`, and `glide` fail the load.

### Oscillator plus envelope — existing

Sine through Pad, or a baked subtractive cycle, plus one of the four curves:

```
mothdeck-patch 1
name=soft-pluck
source=builtin
builtin=pluck
envelope=0
envlen=3
filter=1
cutoff=70
res=8
```

A subtractive folder is the same idea when the wave should be chosen on the card. The cycle's `cutoff` is baked. The track `cutoff` still moves on the FX page:

```
mothdeck-patch 1
name=saw-pluck
source=subtractive
wave=saw
cutoff=80
resonance=10
gain=100
envelope=0
envlen=3
filter=1
cutoff=64
res=12
```

A free attack, decay, sustain, and release in milliseconds is a different block. The four curves cover this shortlist item, so that block is not required. Add it only if a later patch must set those times on a sample or a cycle. It would be four numbers and the envelope index the voice already keeps.

### Short sample — existing

```
mothdeck-patch 1
name=body
source=sample
sample=body.wav
root=60
rate=22050
oneshot=1
loop_start=0
loop_end=0
gain=100
envelope=0
envlen=2
```

Recording into the buffer, cubic interpolation, and more than one voice on the track are not blocks. A kit of one file per note stays a drum kit, not this file. The 4096-frame cap stays, including while BLE is on.

### Low-pass — existing

```
mothdeck-patch 1
name=closed
source=builtin
builtin=saw
filter=1
cutoff=32
res=20
```

`lowpass=1` is the other, coarser average, and only if every insert field is off. A patch that sets `filter` should leave `lowpass` at 0. The subtractive renderer's `cutoff` is a third, baked, one-pole on the cycle. None of these is a new block.

### Drive — existing

```
mothdeck-patch 1
name=push
source=builtin
builtin=bass
drive=60
overdrive=0
```

`overdrive=1` with `drive=0` is the older hard clip. Bias and an emphasis shelf, the rest of the tanh device, are not a block. They stay out unless Chris later asks for that colour. A shelf would be a new small block (one pole of state per voice). It is not on the shortlist.

### Delay, reverb, chorus, crush, tremolo — existing inserts

```
mothdeck-patch 1
name=room-hat
source=builtin
builtin=sine
delay=2
feedback=40
mix=30
reverb=25
chorus=15
crush=0
tremolo=0
```

`delay=2` is a sixteenth, inside the history the voice already owns. Chorus and reverb use that same line. This does not become Chronoblob (stereo, fixed half-second lines) or Plateau (a tank with its own size and decay). Those stay off the list.

## 5. Licensing

LOOPA is GNU GPLv3. MothDeck is MIT, copyright 2024 MothSynths, in `LICENSE` at the root of this repo.

GPLv3 says that if you distribute a program that is a derived work of a GPL program, you distribute that combination under the GPL. The recipient gets the source, the right to modify it, and the right to pass it on under the same terms. MIT is more permissive. It does not require those terms. You can use MIT code inside a GPL program. You cannot take GPL code, put it inside an MIT program, and keep shipping the result as MIT. Shipping them together as one firmware image is the case that matters: the image would have to be offered under GPLv3.

That includes compiling LOOPA into MothDeck, pasting a Dev Studio `MyDevice.cpp` into this tree, translating a LOOPA file into C++ line by line, or dropping a LOOPA object file on the SD card and loading it from an official MothDeck build. A user who separately compiles GPL code for their own board is outside this repo. This repo should not host it, link it, or document it as the way to add a sound.

A clean-room reimplementation stays MIT. The allowed input is the idea: a control that pulls notes into A minor, a mono bass with glide. The implementation uses MothDeck's own oscillators, filter, and envelope. It does not copy LOOPA source, comments, constants, or curves, and it does not vendor the Pi headers. The Dattorro reverb is a published algorithm as well as a LOOPA effect. It is off the shortlist because of RAM. If it were ever built, it would be from the paper, with a buffer this chip can hold, not from the Plateau sources.

This document names the devices and what they do. It does not quote their source.

## Waiting for approval

Do not implement this until Chris says so.

When it is implemented, the work is a parser and a host test for `mothdeck-patch 1`, assign-time copy onto the track, and the sample path that already exists. No ELF loader, no new delay memory, no new FX rows, and no change to the BLE gate.

New small blocks, only if he wants them in that same pass or a later one:

| Block | Why the current fields are not enough | Cost |
| --- | --- | --- |
| Scale map | Nothing remaps a note. | 128 bytes. No audio work. |
| Held-note arp | `chordMult` is a fixed interval walk on built-ins 2–11. | A few notes per track. Once per block. |
| Second oscillator and glide time | Bass and Saw are fixed dual voices. `pitchMult` is a one-shot sweep. | A few integers per voice, on oscillators that already exist. |

Not proposed: a runtime ADSR, a drive shelf, tape, ping-pong, Plateau, or the microphone.
