#!/usr/bin/env python3
"""Turn a WAV file into a MothDeck instrument folder.

Writes manifest.txt plus an unsigned 8-bit mono WAV at 22050 Hz (128 is
silence). Pass --raw to keep little-endian int16 at the source rate.

Example:
  python3 tools/wav_to_instrument.py kick.wav sd-card/instruments/kick \\
      --name kick --root 36 --oneshot
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import ASSET_RATE, read_wav_mono, resample, write_wav  # noqa: E402

# Matches the no-PSRAM plugin cache in src/InstrumentBank.cpp.
PLUGIN_CAP = 4096


def fade_trim(samples, limit):
    if len(samples) <= limit:
        return list(samples)
    out = [int(round(s)) for s in samples[:limit]]
    fade = min(180, max(8, limit // 10))
    for i in range(fade):
        out[-1 - i] = int(out[-1 - i] * (i / float(fade)))
    return out


def main():
    parser = argparse.ArgumentParser(description="Convert a WAV into a MothDeck instrument")
    parser.add_argument("wav")
    parser.add_argument("outdir")
    parser.add_argument("--name", required=True)
    parser.add_argument("--root", type=int, default=60)
    parser.add_argument("--loop-start", type=int, default=0)
    parser.add_argument("--loop-end", type=int, default=0)
    parser.add_argument("--oneshot", action="store_true")
    parser.add_argument("--raw", action="store_true", help="write sample.raw as little-endian int16 at the source rate")
    parser.add_argument("--gain", type=int, default=100)
    args = parser.parse_args()

    samples, rate = read_wav_mono(args.wav)
    os.makedirs(args.outdir, exist_ok=True)
    sample_name = "sample.raw" if args.raw else "sample.wav"
    sample_path = os.path.join(args.outdir, sample_name)
    loop_start = args.loop_start
    loop_end = args.loop_end
    if args.raw:
        with open(sample_path, "wb") as handle:
            handle.write(b"".join(struct.pack("<h", int(round(s))) for s in samples))
    else:
        if rate != ASSET_RATE:
            scale = float(ASSET_RATE) / float(rate)
            loop_start = int(round(loop_start * scale))
            loop_end = int(round(loop_end * scale))
            samples = resample(samples, rate, ASSET_RATE)
            rate = ASSET_RATE
        if len(samples) > PLUGIN_CAP:
            print("trimmed %d frames to %d (plugin cache)" % (len(samples), PLUGIN_CAP))
            samples = fade_trim(samples, PLUGIN_CAP)
        else:
            samples = [int(round(s)) for s in samples]
        if loop_end > len(samples):
            loop_end = len(samples)
        if loop_start < 0 or loop_start >= loop_end:
            loop_start = 0
            loop_end = 0
        write_wav(sample_path, samples, rate)
    manifest = os.path.join(args.outdir, "manifest.txt")
    with open(manifest, "w", encoding="utf-8") as handle:
        handle.write("mothdeck-instrument 1\n")
        handle.write("name=%s\n" % args.name)
        handle.write("type=sample\n")
        handle.write("sample=%s\n" % sample_name)
        handle.write("root=%d\n" % args.root)
        handle.write("rate=%d\n" % rate)
        handle.write("loop_start=%d\n" % loop_start)
        handle.write("loop_end=%d\n" % loop_end)
        handle.write("oneshot=%d\n" % (1 if args.oneshot else 0))
        handle.write("gain=%d\n" % args.gain)
    print("wrote %s (%d Hz, %d frames)" % (args.outdir, rate, len(samples)))


if __name__ == "__main__":
    main()
