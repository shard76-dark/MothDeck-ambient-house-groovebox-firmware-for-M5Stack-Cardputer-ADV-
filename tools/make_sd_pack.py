#!/usr/bin/env python3
"""Build releases/mothdeck-sd-pack.zip.

Unzip onto the microSD root so moth/ sits next to nothing else the card
needs. Every sample is synthesized here. No third-party recordings.
"""
import math
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import write_wav  # noqa: E402
from voices import (  # noqa: E402
    DRUM_NAMES,
    Noise,
    ambient_kit,
    kit_808,
    kit_dusty,
    normalize,
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGE = os.path.join(ROOT, "build", "sd-pack")
ZIP_PATH = os.path.join(ROOT, "releases", "mothdeck-sd-pack.zip")

# Unsigned 8-bit mono at 22050 Hz (see pcmutil.ASSET_RATE). Playback stretches
# that up to the 44100 Hz mix. No-PSRAM limits: a plugin sample is 4096 frames,
# a kit pad is 1800, and a whole kit is 8000 int16 frames after decode. Pad
# caps below sum to 6950 so a 22050 Hz kit still fits. Loops are streamed.
KIT_RATE = 22050
PAD_CAP = {
    "kick": 1500,
    "snare": 800,
    "clap": 600,
    "hat": 350,
    "openhat": 700,
    "rim": 250,
    "perc": 550,
    "tom": 550,
    "shaker": 350,
    "ride": 550,
    "snap": 200,
    "crash": 550,
}
LOOP_RATE = 22050
LOOP_BPM = 120
LOOP_FRAMES = 44100  # one bar at LOOP_RATE and LOOP_BPM
INST_RATE = 22050
INST_FRAMES = 3600


def fit(samples, limit):
    if len(samples) <= limit:
        return samples
    out = list(samples[:limit])
    fade = min(180, max(8, limit // 10))
    for i in range(fade):
        out[-1 - i] = int(out[-1 - i] * (i / float(fade)))
    return out


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def tone_note(n, rate, freq, kind):
    out = []
    phase = 0.0
    phase_b = 0.0
    for i in range(n):
        t = i / rate
        if kind == "ep":
            mod = math.exp(-t * 22)
            tine = math.exp(-t * 55)
            phase += 2 * math.pi * freq / rate
            sample = math.sin(phase + math.sin(phase) * mod * 3.2 + math.sin(phase * 11) * tine * 1.4)
            sample *= math.exp(-t * 3.4)
        elif kind == "reese":
            phase += 2 * math.pi * freq * 0.992 / rate
            phase_b += 2 * math.pi * freq * 1.012 / rate
            saw = (phase / math.pi) % 2.0 - 1.0
            saw_b = (phase_b / math.pi) % 2.0 - 1.0
            sub = math.sin(2 * math.pi * freq * 0.5 * t)
            sample = math.tanh(sub * 0.7 + saw * 0.28 + saw_b * 0.28)
            if t < 0.01:
                sample *= t / 0.01
        elif kind == "lead":
            phase += 2 * math.pi * freq * 0.995 / rate
            phase_b += 2 * math.pi * freq * 1.006 / rate
            saw = (phase / math.pi) % 2.0 - 1.0
            saw_b = (phase_b / math.pi) % 2.0 - 1.0
            sample = math.tanh((saw + saw_b) * 0.6)
            if t < 0.008:
                sample *= t / 0.008
        elif kind == "pad":
            phase += 2 * math.pi * freq * 0.994 / rate
            phase_b += 2 * math.pi * freq * 1.008 / rate
            a = math.sin(phase)
            b = math.sin(phase_b)
            sample = (a + b) * 0.35
            gate = min(1.0, t / 0.22)
            sample *= gate
        elif kind == "pluck":
            phase += 2 * math.pi * freq / rate
            saw = (phase / math.pi) % 2.0 - 1.0
            sample = math.sin(phase) * 0.4 + saw * 0.25
            sample *= math.exp(-t * 8)
        else:  # bell
            phase += 2 * math.pi * freq / rate
            index = 2.4 * math.exp(-t * 3.5)
            sample = math.sin(phase + math.sin(phase * 3.5) * index)
            sample *= math.exp(-t * 2.2)
        out.append(int(max(-32768, min(32767, sample * 26000))))
    return out


def loop_bar(kind):
    rate = LOOP_RATE
    n = LOOP_FRAMES
    step = n // 4
    out = [0.0] * n
    noise = Noise(80 if kind == "hats" else 90)
    if kind == "kick":
        hit = ambient_kit(rate)[0]
        for beat in range(4):
            at = beat * step
            for i, s in enumerate(hit):
                if at + i < n:
                    out[at + i] += s * (0.9 if beat % 2 == 0 else 0.55)
        # offbeat hat whisper
        hat = fit(ambient_kit(rate)[4], step // 3)
        for beat in range(4):
            at = beat * step + step // 2
            for i, s in enumerate(hat):
                if at + i < n:
                    out[at + i] += s * 0.35
    elif kind == "hats":
        for i in range(8):
            at = i * (n // 8)
            length = step // 5 if i % 2 == 0 else step // 3
            for k in range(length):
                if at + k >= n:
                    break
                t = k / rate
                out[at + k] += noise.step() * math.exp(-t * (40 if i % 2 == 0 else 12)) * 9000
    elif kind == "chord":
        freqs = (196.0, 246.9, 293.7)
        for f in freqs:
            phase = 0.0
            for i in range(n):
                phase += 2 * math.pi * f / rate
                gate = 0.55 + 0.45 * math.sin(2 * math.pi * (i / step))
                if gate < 0:
                    gate = 0
                out[i] += math.sin(phase) * gate * 5000
    else:
        phase = 0.0
        for beat in range(4):
            at = beat * step
            for i in range(step):
                if at + i >= n:
                    break
                t = i / rate
                f = 49 * math.exp(-t * 6) + 40
                phase += 2 * math.pi * f / rate
                out[at + i] += math.sin(phase) * math.exp(-t * 3.2) * 14000
    peak = max(1.0, max(abs(s) for s in out))
    return [int(max(-32768, min(32767, s * 26000 / peak))) for s in out]


def kit_dir(folder, title, hits):
    base = os.path.join(STAGE, "moth", "drums", folder)
    os.makedirs(base, exist_ok=True)
    lines = ["mothdeck-kit 1", "name=%s" % title]
    for name, data in zip(DRUM_NAMES, hits):
        capped = fit(data, PAD_CAP[name])
        write_wav(os.path.join(base, name + ".wav"), capped, KIT_RATE)
        lines.append("pad=%s.wav" % name)
    write(os.path.join(base, "manifest.txt"), "\n".join(lines) + "\n")


def instrument(folder, name, kind, root, oneshot, loop_at):
    rate = INST_RATE
    frames = INST_FRAMES
    data = tone_note(frames, rate, 440.0 * (2 ** ((root - 69) / 12.0)), kind)
    base = os.path.join(STAGE, "moth", "instruments", folder)
    os.makedirs(base, exist_ok=True)
    write_wav(os.path.join(base, "sample.wav"), data, rate)
    loop_start = 0
    loop_end = 0
    if not oneshot:
        loop_start = int(frames * loop_at)
        loop_end = frames - 8
    text = (
        "mothdeck-instrument 1\n"
        "name=%s\n"
        "type=sample\n"
        "sample=sample.wav\n"
        "root=%d\n"
        "rate=%d\n"
        "oneshot=%d\n"
        "loop_start=%d\n"
        "loop_end=%d\n"
        "gain=100\n"
    ) % (name, root, rate, 1 if oneshot else 0, loop_start, loop_end)
    write(os.path.join(base, "manifest.txt"), text)


def readme():
    return """# MothDeck SD pack

Unzip this archive onto the microSD card root. You should end up with a `moth` folder beside this file. Eject the card, boot MothDeck, and on the Instrument page press `R` to rescan.

The built-in drum kit is already **Ambient House** and does not need the card. This pack adds two more kits, a Loops instrument's samples, six sample instruments, and six patches (scale, held arp, glide, and the other shortlist sounds).

## What is on the card

| Path | What it is |
| --- | --- |
| `moth/drums/808` | 808-style kit. Sine kick, short snare and clap, tight hats |
| `moth/drums/dusty` | The ambient kit with a lowpass, a little noise, and bit reduction |
| `moth/loops/house` | Four one-bar loops at 120 BPM: kick groove, hats, chord, sub |
| `moth/instruments/ep` | Electric piano, one-shot |
| `moth/instruments/reese` | Detuned saw bass with a sub, looping |
| `moth/instruments/saw-lead` | Two detuned saws, looping |
| `moth/instruments/soft-pad` | Slow detuned sines, looping |
| `moth/instruments/house-pluck` | Short decaying pluck, one-shot |
| `moth/instruments/air-bell` | FM bell, one-shot |
| `moth/instruments/a-minor` | Patch: built-in bass locked to A minor |
| `moth/instruments/held-arp` | Patch: built-in saw, held-note arp |
| `moth/instruments/glide-bass` | Patch: bass plus a saw an octave down, 90 ms glide |
| `moth/instruments/saw-pluck` | Patch: subtractive saw, short envelope, low-pass |
| `moth/instruments/body` | Patch: short pluck sample |
| `moth/instruments/room-drive` | Patch: sine with drive, delay, reverb, and chorus |

## Drums

On the Instrument page, `,` and `/` (or Fn+`,` and Fn+`/`) change the kit used by the Drums instrument. The line under the list shows the kit name. The first entry is always the built-in kit. The card is not erased. If a kit does not fit in free RAM the toast says how many kilobytes it needs and how many are free, and the previous kit stays selected.

Keyboard order for every kit, low octave white keys first:

| Key | Pad |
| --- | --- |
| C | Kick |
| C# | Rim |
| D | Snare |
| D# | Clap |
| E | Closed hat |
| F | Open hat |
| F# | Low tom |
| G | Tom |
| G# | Shaker |
| A | Ride |
| A# | Snap |
| B | Soft crash |

A kit folder is `mothdeck-kit 1`, a `name=`, and exactly twelve `pad=` lines in that order. Audio is unsigned 8-bit mono PCM WAV at 22050 Hz (128 is silence, about 22 KB/s). The speaker mix is still 44100 Hz. A 16-bit mono WAV still loads, and stereo is folded to mono. On this board each pad is kept to 1800 frames and the twelve together to 8000 frames, in internal RAM, after the file is expanded to 16-bit. These files are already inside that limit. The low tom is `perc.wav`. Loops are streamed from the card instead of being copied into RAM.

## Loops instrument

Assign **Loops** (the row after Pad) to a track. Each note starts one of the audio loops in `/moth/loops`, wrapping if there are fewer loops than keys, and lines the loop up with the current bar. Playback follows the project BPM, so a faster song speeds the loop up. The loops in this pack are one bar at 120 BPM, unsigned 8-bit mono, 22050 Hz, 44100 frames (about 44 KB each). They stream from the card in a small window, so the whole file is not kept in RAM.

The Loops page can still audition and launch a single loop onto a track. That is separate from the Loops instrument, and both use the same cache.

## Plugins

Assign a plugin from the Instrument list with Enter. The folder name is what a song remembers. Sample folders follow `mothdeck-instrument 1`. The six patch folders follow `mothdeck-patch 1`: they set a built-in or a short sample plus scale, arp, glide, and the FX rows. On the FX page, Scale, Root, Arp, Glide, Osc2, Blend, and Coarse change those after assign. Without PSRAM a plugin sample can be at most 4096 frames and at most four stay loaded. The sample folders here are unsigned 8-bit mono, 22050 Hz, 3600 frames. `body` is 1800 frames. Both fit on the Cardputer ADV. Patches stay loaded while BLE is on. Loops do not.

## Sources and licence

Every WAV in this zip was synthesized by `tools/make_sd_pack.py` and `tools/voices.py` in the MothDeck tree. There are no sampled breaks, no third-party recordings, and no content taken from another project. The pack is MIT, Copyright (c) 2024 MothSynths, the same licence as the firmware.

The public GitHub account `shard76-dark` has no visible repositories, so no looper-project instruments were imported.
"""


def main():
    if os.path.isdir(STAGE):
        for dirpath, dirnames, filenames in os.walk(STAGE, topdown=False):
            for name in filenames:
                os.remove(os.path.join(dirpath, name))
            for name in dirnames:
                os.rmdir(os.path.join(dirpath, name))
    kit_dir("808", "808", kit_808(KIT_RATE))
    kit_dir("dusty", "Dusty", kit_dusty(KIT_RATE))
    instrument("ep", "EP", "ep", 60, True, 0.2)
    instrument("reese", "Reese", "reese", 36, False, 0.15)
    instrument("saw-lead", "Saw-lead", "lead", 60, False, 0.1)
    instrument("soft-pad", "Soft-pad", "pad", 60, False, 0.28)
    instrument("house-pluck", "House-pluck", "pluck", 60, True, 0.2)
    instrument("air-bell", "Air-bell", "bell", 72, True, 0.2)
    loop_base = os.path.join(STAGE, "moth", "loops", "house")
    os.makedirs(loop_base, exist_ok=True)
    loops = (
        ("kick-groove.wav", "kick"),
        ("hat-bed.wav", "hats"),
        ("chord.wav", "chord"),
        ("sub.wav", "sub"),
    )
    lines = [
        "mothdeck-loops 1",
        "name=house-120",
        "bpm=%d" % LOOP_BPM,
        "bars=1",
        "tags=house,ambient",
    ]
    for filename, kind in loops:
        write_wav(os.path.join(loop_base, filename), loop_bar(kind), LOOP_RATE)
        lines.append("loop=%s" % filename)
    write(os.path.join(loop_base, "manifest.txt"), "\n".join(lines) + "\n")
    example = os.path.join(ROOT, "sd-card-example", "moth", "instruments")
    for name in (
        "a-minor",
        "held-arp",
        "glide-bass",
        "saw-pluck",
        "body",
        "room-drive",
    ):
        src = os.path.join(example, name)
        dst = os.path.join(STAGE, "moth", "instruments", name)
        if not os.path.isdir(src):
            raise SystemExit("missing example patch %s" % src)
        shutil.copytree(src, dst)
    write(os.path.join(STAGE, "README.md"), readme())
    os.makedirs(os.path.dirname(ZIP_PATH), exist_ok=True)
    with zipfile.ZipFile(ZIP_PATH, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for dirpath, _, filenames in os.walk(STAGE):
            for name in filenames:
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, STAGE)
                archive.write(full, rel)
    print("wrote", ZIP_PATH)


if __name__ == "__main__":
    main()
