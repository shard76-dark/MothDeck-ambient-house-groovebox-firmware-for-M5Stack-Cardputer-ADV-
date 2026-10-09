#!/usr/bin/env python3
"""Build sd-card-example/ with original synthesized instruments and loops.

Copy the moth/ folder onto a FAT32 card. Nothing here is a recorded sample.
WAV files are unsigned 8-bit mono at 22050 Hz (128 is silence).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import tone, write_wav  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "sd-card-example", "moth")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def pluck(n, rate, freq):
    body = tone(n, freq, rate, "sine", 0.55, 0.18)
    spark = tone(n, freq * 2.7, rate, "sine", 0.18, 0.04)
    return [max(-32768, min(32767, a + b)) for a, b in zip(body, spark)]


def kick_bar(rate, bpm, beats=4):
    bar = int(rate * 60 / bpm * beats)
    out = [0] * bar
    hit = tone(int(rate * 0.18), 70, rate, "sine", 0.85, 0.05)
    # Drop the pitch by rewriting the front of the hit.
    click = tone(int(rate * 0.01), 1800, rate, "square", 0.25, 0.005)
    step = int(rate * 60 / bpm)
    for beat in range(beats):
        at = beat * step
        for i, sample in enumerate(hit):
            if at + i < bar:
                out[at + i] += sample // (1 if beat % 2 == 0 else 2)
        if beat % 2 == 0:
            for i, sample in enumerate(click):
                if at + i < bar:
                    out[at + i] += sample
    return [max(-32768, min(32767, s)) for s in out]


def hat_bar(rate, bpm, beats=8):
    bar = int(rate * 60 / bpm * 4)
    out = [0] * bar
    noise_len = int(rate * 0.04)
    step = bar // beats
    phase = 1
    for beat in range(beats):
        at = beat * step
        for i in range(noise_len):
            if at + i >= bar:
                break
            phase = (1103515245 * phase + 12345) & 0x7FFFFFFF
            env = math.exp(-i / (rate * 0.012))
            amp = 9000 if beat % 2 else 14000
            out[at + i] += int(((phase % 2000) - 1000) * env * amp / 1000)
    return [max(-32768, min(32767, s)) for s in out]


def pad_loop(rate, seconds, freq):
    n = int(rate * seconds)
    a = tone(n, freq, rate, "triangle", 0.28, 0)
    b = tone(n, freq * 1.5, rate, "sine", 0.12, 0)
    return [max(-32768, min(32767, x + y)) for x, y in zip(a, b)]


def main():
    rate = 22050
    inst = os.path.join(OUT, "instruments")
    pluck_dir = os.path.join(inst, "soft-pluck")
    os.makedirs(pluck_dir, exist_ok=True)
    # 3600 frames stays under the 4096-frame plugin cache.
    frames = 3600
    write_wav(os.path.join(pluck_dir, "sample.wav"), pluck(frames, rate, 261.63), rate)
    write(
        os.path.join(pluck_dir, "manifest.txt"),
        "\n".join(
            [
                "mothdeck-instrument 1",
                "name=soft-pluck",
                "type=sample",
                "sample=sample.wav",
                "root=60",
                "rate=22050",
                "loop_start=900",
                "loop_end=3500",
                "attack=0",
                "decay=40",
                "sustain=70",
                "release=30",
                "gain=90",
                "oneshot=0",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "glass-fm", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-instrument 1",
                "name=glass-fm",
                "type=fm",
                "wave=sine",
                "root=60",
                "fm_ratio=3",
                "fm_index=35",
                "gain=80",
                "attack=5",
                "decay=30",
                "sustain=60",
                "release=40",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "warm-saw", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-instrument 1",
                "name=warm-saw",
                "type=subtractive",
                "wave=saw",
                "cutoff=55",
                "resonance=20",
                "gain=75",
                "attack=10",
                "decay=20",
                "sustain=80",
                "release=25",
                "",
            ]
        ),
    )

    house = os.path.join(OUT, "loops", "house-120")
    os.makedirs(house, exist_ok=True)
    write_wav(os.path.join(house, "kick.wav"), kick_bar(rate, 120), rate)
    write_wav(os.path.join(house, "hats.wav"), hat_bar(rate, 120), rate)
    write(
        os.path.join(house, "offbeat.pat"),
        "\n".join(
            [
                "mothdeck-pattern 1",
                "bars=1",
                "steps=0,0,8,0,0,0,8,0,0,0,8,0,0,0,5,0",
                "",
            ]
        ),
    )
    write(
        os.path.join(house, "manifest.txt"),
        "\n".join(
            [
                "mothdeck-loops 1",
                "name=house-120",
                "bpm=120",
                "bars=1",
                "tags=drums,house",
                "loop=kick.wav",
                "loop=hats.wav",
                "pattern=offbeat.pat",
                "",
            ]
        ),
    )

    ambient = os.path.join(OUT, "loops", "ambient-90")
    os.makedirs(ambient, exist_ok=True)
    write_wav(os.path.join(ambient, "pad.wav"), pad_loop(rate, 60 / 90 * 4, 220.0), rate)
    write(
        os.path.join(ambient, "manifest.txt"),
        "\n".join(
            [
                "mothdeck-loops 1",
                "name=ambient-90",
                "bpm=90",
                "bars=1",
                "tags=pad,ambient",
                "loop=pad.wav",
                "",
            ]
        ),
    )
    # Six mothdeck-patch folders. They sit beside the older instrument
    # manifests. body.wav is a short original pluck under the 4096-frame cap.
    body = os.path.join(inst, "body")
    os.makedirs(body, exist_ok=True)
    write_wav(os.path.join(body, "body.wav"), pluck(1800, rate, 196.0), rate)
    write(
        os.path.join(body, "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=body",
                "source=sample",
                "sample=body.wav",
                "root=55",
                "rate=22050",
                "oneshot=1",
                "gain=100",
                "envelope=0",
                "envlen=2",
                "scale=off",
                "arp=0",
                "osc2=off",
                "glide=0",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "a-minor", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=a-minor",
                "source=builtin",
                "builtin=bass",
                "scale=minor",
                "root=9",
                "envelope=2",
                "envlen=1",
                "filter=1",
                "cutoff=42",
                "res=12",
                "arp=0",
                "osc2=off",
                "glide=0",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "held-arp", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=held-arp",
                "source=builtin",
                "builtin=saw",
                "arp=held",
                "scale=off",
                "envelope=2",
                "envlen=2",
                "filter=1",
                "cutoff=70",
                "res=8",
                "osc2=off",
                "glide=0",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "glide-bass", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=glide-bass",
                "source=builtin",
                "builtin=bass",
                "osc2=saw",
                "coarse=-12",
                "blend=40",
                "glide=90",
                "scale=off",
                "arp=0",
                "envelope=2",
                "envlen=1",
                "filter=1",
                "cutoff=36",
                "res=16",
                "drive=20",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "saw-pluck", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=saw-pluck",
                "source=subtractive",
                "wave=saw",
                "cutoff=80",
                "resonance=10",
                "gain=100",
                "envelope=0",
                "envlen=3",
                "filter=1",
                "cutoff=64",
                "res=12",
                "scale=off",
                "arp=0",
                "osc2=off",
                "glide=0",
                "",
            ]
        ),
    )
    write(
        os.path.join(inst, "room-drive", "manifest.txt"),
        "\n".join(
            [
                "mothdeck-patch 1",
                "name=room-drive",
                "source=builtin",
                "builtin=sine",
                "drive=55",
                "delay=2",
                "feedback=35",
                "mix=28",
                "reverb=30",
                "chorus=10",
                "envelope=0",
                "envlen=2",
                "scale=off",
                "arp=0",
                "osc2=off",
                "glide=0",
                "",
            ]
        ),
    )
    print("wrote %s" % OUT)
    print("patch folders: a-minor held-arp glide-bass saw-pluck body room-drive")


if __name__ == "__main__":
    main()
