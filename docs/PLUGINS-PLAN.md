# SD-loadable plugins

Design note only. No firmware change is proposed as a patch in this file, and none of LOOPA's source is included here.

LOOPA is a Raspberry Pi Zero groovebox ([aftersound.tech](https://aftersound.tech), source [ferluht/loopa](https://github.com/ferluht/loopa), GPLv3). This note surveys the devices in that public tree, compares them with what MothDeck already plays, and recommends a patch file the Cardputer can read from the microSD card. The target is the Stamp-S3A: ESP32-S3, no PSRAM, internal RAM only, BLE that needs a large contiguous block, and a mono 44100 Hz mix.

Survey date: the `master` tree of `ferluht/loopa` as published on GitHub, plus the Dev Studio page on aftersound.tech. Midiphy LoopA is a different product and is not part of this survey.

## 1. What LOOPA ships

LOOPA devices are C++ classes compiled into the Pi program. Each one is registered with a compile-time factory. The Dev Studio on the website is the same idea in a browser: two editors titled `MyDevice.h` and `MyDevice.cpp`, a prompt you paste into an external chat model, and a build. The page classifies a device by the base it inherits: a polyphonic instrument, an audio effect, or a MIDI effect. The stock skeleton is an instrument with per-voice state, an attack parameter, a release parameter, a handler for incoming MIDI, and a float audio callback. That output is source to compile for the Pi. It is not a file the Cardputer can load.

The public tree's device folders are small. Empty slots exist so a track can have "no instrument" or "no effect". Helpers that are not separate menu devices are listed too, because they live in those folders.

### Instruments

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty slot | Draws "no instrument" and makes no sound. | None | None | Already how an unused MothDeck track behaves. Not a plugin. |
| Simple (SIMPL) | Polyphonic sine voice with attack, decay, sustain, and release. | Low | A few floats per voice | MothDeck already has sine, square, saw, triangle, and shaped built-ins. Useful only as graph blocks, not as a new engine. |
| Single tone (SYNTH) | One voice, two oscillators blended by a cross-mod amount, plus glide time. | Low | A few floats | High. A mono dual-osc bass is the kind of patch this chip can run. Reimplement the idea with MothDeck's own oscillators. |
| Sampler | Plays a file as one-shot, enveloped, or looped, with cubic interpolation, loop points, and recording. | Medium if the interpolator runs every sample | The whole file, in float, plus a waveform cache | Medium. MothDeck already plays short samples and streams long loops from the card. A Pi-style record-into-RAM sampler does not fit. Keep the existing 4096-frame cap. |
| Sample kit | Several files, one per note, sharing the sampler path. | Same as the sampler | One buffer per note | Low as a new type. Drum kits already do twelve pads from the card. |
| Mic (MIC) | Passes the microphone or line input through a note envelope and a release control. | Low | Small | Low. The current firmware does not run the codec input as an instrument. The musical gain is small until that path exists. |

### Audio effects

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty effect | Copies the input buffer to the output. | None | None | Skip. |
| Chronoblob | Stereo ping-pong delay with fixed times around half a second and a high wet mix. It wraps the ping-pong helper. | Low, apart from the memory traffic | Two delay lines of that length | Low. The mix on this board is mono. A tempo delay already exists on the FX page. |
| Ping-pong helper | The stereo delay Chronoblob uses. Not its own menu entry. | Low | Same delay lines | Same as Chronoblob. Do not add a stereo delay. |
| Delay | Tempo delay with dry/wet and feedback. The history is on the order of two million samples per channel, and it uses a resampler library. | The resampler is heavier than a simple delay | Tens of megabytes | The idea is already on the FX page. That buffer cannot exist on this chip. |
| Simple delay | An empty class. | None | None | Skip. |
| Soft clipper | Waves the signal with a soft saturation curve. | Tiny | A few floats | High as a graph block. MothDeck already has a drive control. Do not copy the curve. |
| Plateau | A room-style reverb (size, decay, dry/wet) built on a Dattorro tank and several all-pass and interpolated delay lines. | High for this chip: many delay reads per sample | The tank is large even in a cut-down form, and the Pi version is much larger | Low. MothDeck already has a small reverb send. A faithful tank will fight the BLE block. |
| Tanh | Drive, bias, output, and an emphasis shelf into a saturator. | Low if the shelf is one pole and the shaper is a short polynomial | A few floats | High as a tone-plus-drive block, with an original curve. The useful part overlaps the existing drive control. |
| Tape | A multi-scene looper: record, overdub, varispeed, copy a scene. | Medium, and it touches a huge buffer | The audio buffer is the loop itself | Poor in RAM. MothDeck already streams loops from the card. |
| New tape | The later looper: playheads, varispeed, rewind, double the loop, scenes, save and load. The length constant is minutes of stereo at 44100 Hz. | Medium to high | Minutes of stereo audio | Poor. This is the Pi's main looper, not a Cardputer plugin. |
| Loop matrix | A 2×2 grid of those tapes, with sends and scene copy. | Four loopers | Four of those buffers | Skip. |

### MIDI effects

| Device | What it does | CPU | RAM | Fit |
| --- | --- | --- | --- | --- |
| Empty MIDI effect | Lets notes through. | None | None | Skip. |
| Scale | Forces notes into a chosen scale and root: major, minor, harmonic minor, mixolydian, phrygian, or chromatic. | None on the sample loop. It rewrites note numbers. | A 128-entry map | High. A house box wants this, and it costs almost nothing. |
| Arpeggiator | Holds the notes that are down and walks them at a rate that can follow the clock. | Tiny. It runs once per audio block, not per sample. | The set of held notes | High. MothDeck's existing arp flag is simpler than a held-chord walk. |

### Shortlist

These are the ones worth building, as original MothDeck blocks, in this order:

1. **Scale.** Rewrites incoming notes. No sample RAM, no interaction with the loop pool, works while BLE is on.
2. **Arpeggiator.** Same. One note at a time into the track that is already monophonic.
3. **Dual-oscillator glide voice.** The single-tone idea: two waveforms, a blend, glide. Four tracks, one voice each.
4. **Oscillator plus envelope.** The simple-instrument idea, using the waveforms MothDeck already renders. This is how an SD patch builds a bass or a pluck without a new engine.
5. **Short sample player.** The sampler idea inside the cap that already exists: 4096 frames, four plugins resident, one-shot or a short loop.
6. **Low-pass.** Already an insert. Expose it as a block inside a patch so a bass can close the filter before the track FX.
7. **Drive.** A soft clip or a one-pole tone plus a short shaper. Covers the soft clipper and the useful part of the tanh device. Original coefficients only.
8. **The existing tempo delay, reverb, chorus, crush, and tremolo stay track inserts.** They are not new plugins. A patch does not get its own delay line in the first version.

Not on the list: tape, the new tape, the loop matrix, plateau, ping-pong, the microphone instrument, and the empty delay stub. They are either huge buffers, stereo, or already solved another way.

## 2. What MothDeck calls a plugin today

A plugin is a folder on the card, `/moth/instruments/<folder>/`, with a text manifest. The firmware scans up to twelve folders. Ids 12 through 62 are plugins. Id 63 is the Loops instrument, not a plugin. A song stores the folder name. If the folder is missing at load, the track falls back to drums. Built-in songs stay a 3155-byte version 1 file. Plugins and loops make version 2.

The manifest's `type` is one of four:

- `sample` and `wavetable` play a file. Unsigned 8-bit mono WAV at 22050 Hz is the usual pack format. A 16-bit WAV is accepted and folded to mono. A `sample.raw` file is little-endian int16 with no header. The cap without PSRAM is **4096 frames**. That is about 8 KB as int16. At most **four** plugins stay loaded. A 4096-frame sample at 22050 Hz is under a fifth of a second.
- `subtractive` and `fm` have no sample file. At load the firmware renders **one 168-frame cycle** into that same kind of buffer. Playback is a wavetable read. The cycle length is chosen so a 1:1 read at 44100 Hz sits near C4.

The twelve built-ins (drums, sound effects, sine, square, saw, triangle, organ, pluck, bell, flute, bass, pad) are in the firmware, not on the card. Drum kits are a separate manifest with twelve pads, 1800 frames per pad and 8000 frames for the kit.

The FX page is not a plugin. Each track has an insert: low-pass or high-pass, cutoff, resonance, a tempo delay (off, 1/32, 1/16, 1/8) with feedback and mix, reverb send, bitcrush, drive, chorus, and tremolo. Those run inside the audio task on the four voices. There is no per-track polyphony. A chord is four tracks, or an arpeggio on one track.

Nothing on the card is executable. The manifest is data. The oscillators, the filter, and the inserts are firmware. That is already the safe half of a plugin system. What it cannot do is a patch that wires two oscillators, a filter, and an envelope differently per folder. Every subtractive plugin is the same engine with different numbers.

BLE, as of the 1.1.2 work: the radio is off at boot, and turning it on frees the loop-window pool because those windows and the NimBLE host do not fit together. A short plugin sample is not that pool. Four 8 KB samples are 32 KB of ordinary heap, not the 20 KB loop window and not the 36 KB contiguous block the controller wants. Sample plugins can stay while BLE is on. Streaming loops cannot.

## 3. How a plugin could be loaded

Three ways to put new sound on the card. Only one fits this chip.

### (a) Native code at runtime

An ELF or a position-independent blob on the card, loaded into executable memory.

What exists in the ESP-IDF world is an ELF loader that relocates a file and runs it. The documented comfortable case is a chip with PSRAM: the segments land in external RAM. This Stamp-S3A has no PSRAM. The 1.1.2 link already uses the entire 16 KB of IRAM. A plugin cannot be placed there unless some of the firmware moves out, and that firmware is the audio and interrupt path that wants to stay in IRAM.

Running from flash is how the application itself runs, through the instruction cache. That is not the same as mapping a file that was just read from the microSD. The card is a SPI filesystem. Code cannot execute from it. Getting a blob into executable flash means erasing a partition and writing it, then relocating against a frozen list of firmware symbols. Every firmware update breaks that list. The SD bus is also the disk the loop player and the sample loader use, so a loader that stalls on the card stalls the disk.

There is no process boundary. A bad store in a plugin is a crash in the audio task, and it takes BLE with it. Signing the blob needs a host toolchain pinned to this exact Xtensa ABI. That is a second build system for a device with a 56-key keyboard and no debugger in the field.

A native plugin that was compiled from LOOPA, or from Dev Studio output, would also be a GPL problem. See below. Even a clean-room native plugin is a poor trade: the RAM it would occupy is the same RAM the BLE gate is measured against, and the feature it would unlock (a new opcode the firmware does not have) is not on the shortlist. The shortlist is oscillators, envelopes, a filter, a clipper, a scale, and an arp.

**Not feasible here** as a first plugin format, and not worth a later one unless a future board has PSRAM and a spare IRAM budget.

### (b) A data patch the firmware interprets

A folder on the card describes a graph. The opcodes are blocks that already exist in MothDeck, or small new ones written for this firmware: oscillator, noise, the existing sample reader, envelope, low-pass, mix, gain, drive. Parameters carry a label so the screen can print them. The audio task runs the graph. The card never supplies code.

This matches the machine. The CPU cost is a switch and a handful of arithmetic operations per node, in firmware that is already in flash cache. The RAM cost is the graph plus, if the patch uses a sample, the existing 4096-frame buffer.

**This is the recommendation.**

### (c) A hybrid

Two hybrids get proposed. One is a graph plus a native "escape" opcode. That reintroduces every problem in (a) for the one block that does not fit the graph. None of the shortlisted devices need it. The other hybrid is a graph plus a sample file. That is still (b). MothDeck already loads the sample. The patch only points at it.

Ship (b). Do not leave a hook for native code in the file format.

### Budget

Per sample at 44100 Hz the chip has about 5400 cycles at 240 MHz. The audio task also mixes four voices, runs inserts, and drains MIDI. A fair budget for all plugin work is about **1500 cycles per sample** (roughly a third of a millisecond). Four voices times eight nodes times a cheap opcode fits. A node that is a transcendental call every sample, or a reverb tank, does not.

RAM, resident at once:

| Use | Budget |
| --- | --- |
| Parsed graph, four tracks, up to 16 nodes | Under 4 KB |
| Patch text on the card | Under 2 KB. Not kept after parse |
| Sample node | Existing rule: 4096 frames, four plugins, about 8 KB each |
| Delay or reverb inside the patch | None in the first version. The track insert already has them |
| Scale map and arp notes | Under 256 bytes |
| BLE | A patch must not allocate the loop pool and must not allocate a buffer larger than a few kilobytes. If a future opcode needs a delay line, refuse it while BLE is on instead of stealing the controller block |

Reject a file that asks for an unknown opcode, more than 16 nodes, a cycle, or a sample over the cap. The previous instrument stays selected, and the toast says why, the same way a kit load does today.

### File sketch

Path: `/moth/instruments/<folder>/manifest.txt`, beside today's instrument manifests. A new header line so old firmware skips it cleanly (`mothdeck-instrument` parsers already reject an unknown header).

```
mothdeck-patch 1
name=dust-bass
kind=instrument
voice=mono
node=osc id=1 wave=saw
node=osc id=2 wave=square coarse=-12
node=mix id=3 a=1 b=2
node=lpf id=4 in=3 cutoff=40
node=env id=5 a=2 d=30 s=70 r=15
node=amp id=6 in=4 env=5
out=6
param=cutoff node=4 min=8 max=110 label=Cutoff
```

`kind=midi` is the scale or the arp, with no `out` sample:

```
mothdeck-patch 1
name=a-minor
kind=midi
node=scale root=A mode=minor
```

A sample node points at a file in the same folder and uses the loader that already exists:

```
node=sample id=1 file=body.wav root=60 oneshot=1
```

Names follow the rules in `docs/FORMATS.md`: no slashes, no `..`. Numbers are small integers, not floats, so the parser stays the one the host tests already use.

Opcodes for the first cut: `osc` (sine, square, saw, triangle), `noise`, `sample`, `env`, `lpf`, `mix`, `gain`, `drive`, `scale`, `arp`. No `delay`, `reverb`, `tape`, or `plate`.

### API inside the firmware

This is a C++ interface in the firmware, not a table of function pointers read from the card.

- `init` reads the manifest, builds a fixed node array, and loads at most one sample. Failure leaves the previous instrument in place.
- `noteOn(track, pitch, velocity)` starts the envelope and the glide target on that track.
- `noteOff(track)` releases the envelope.
- `process(track, n, dst)` renders `n` samples into the existing voice buffer. The audio task calls it where it already calls a built-in voice. No allocation on this path.
- `setParam(track, id, value)` writes one integer the screen already edited.
- MIDI `scale` and `arp` run before `noteOn`, on the same path that already handles BLE notes. They do not render audio.

The audio task is the only writer of voice state. The UI task only replaces a whole published graph, the same way a plugin sample is published today: the voice sees either the old graph or the new one, never a half-parsed file.

### How it shows up

The Instrument page already lists plugin folders under the built-ins. A patch folder shows its `name=` on that list and assigns to the selected track with Enter, the same as a sample plugin. The song stores the folder name the same way, so version 2 does not need a new song revision for instruments.

While a patch track is selected, the FX page gains up to four rows from `param=` lines (label plus the integer). The existing insert rows stay. Four is the most that fits the 240×135 list without a new screen. MIDI patches (scale, arp) are one row on that same page, or a single choice on the track, not a new editor.

There is no Dev Studio on the device. Authoring is a text file on the card, or a later host script. The keyboard cannot edit a graph.

### BLE

Loop windows unload while BLE is on, because a streaming loop and the NimBLE host do not fit. A patch does not use those windows.

- Scale, arp, oscillator, envelope, filter, and drive stay available under BLE. They are the right instruments to play from an MPC.
- A `sample` node uses the existing 4096-frame cache. Do not raise that cap to make room for a longer file while the radio is on.
- Turning BLE on does not need the "save this project" question unless the project has streaming loops, which is the rule already. A track that only uses a patch is not a loop.
- If a later opcode wants a delay line, the loader refuses it while BLE is up and the toast says the patch needs BLE off. The first file format avoids that by not having the opcode.

## 4. Licensing

LOOPA is GNU GPLv3. MothDeck is MIT, copyright 2024 MothSynths, in `LICENSE` at the root of this repo.

GPLv3 says that if you distribute a program that is a derived work of a GPL program, you distribute that combination under the GPL. The recipient gets the source, the right to modify it, and the right to pass it on under the same terms. MIT is more permissive. It does not require those terms. You can use MIT code inside a GPL program. You cannot take GPL code, put it inside an MIT program, and keep shipping the result as MIT. Shipping them together as one firmware image is the case that matters: the image would have to be offered under GPLv3.

That includes compiling LOOPA into MothDeck, pasting a Dev Studio `MyDevice.cpp` into this tree, translating a LOOPA file into C++ line by line, or dropping a LOOPA object file on the SD card and loading it from an official MothDeck build. A user who separately compiles GPL code for their own board is outside this repo. This repo should not host it, link it, or document it as the way to add a sound.

A clean-room reimplementation stays MIT. The allowed input is the idea, not the expression: "a control that pulls notes into A minor", "a mono voice with glide and two waveforms". The implementation is written from MothDeck's own oscillators, filter, and envelope, by someone working from that description. It does not copy LOOPA source, comments, constants, or curves. It does not vendor the Pi headers. The Dattorro reverb is a published algorithm as well as a LOOPA effect. It is off the shortlist for RAM reasons. If it were ever built, it would be from the paper, with a buffer budget this chip can hold, not from the Plateau sources.

This document names the devices and what they do. It does not quote their source.

## Recommendation

Use a text patch on the SD card, interpreted by firmware blocks MothDeck already owns or can grow in a few small opcodes. Do not load native code. The first patches to support are a scale, an arpeggiator, a dual-oscillator glide voice, an oscillator-plus-envelope voice, a short sample, a low-pass, and a drive. Leave tape, ping-pong, and the big reverb on the Pi.

The first implementation, when it happens, is a parser and a host test for `mothdeck-patch 1`, then a render path that calls the existing voice code. No ELF loader, no new delay memory, and no change to the BLE gate.
