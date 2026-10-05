#!/usr/bin/env python3
"""Turn a WAV file into a MothDeck instrument folder.

Writes manifest.txt plus a 16-bit mono WAV (or raw int16) the firmware
can load from /moth/instruments/<name>/ without reflashing.

Example:
  python3 tools/wav_to_instrument.py kick.wav sd-card/instruments/kick \\
      --name kick --root 36 --oneshot
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import read_wav_mono, write_wav  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description="Convert a WAV into a MothDeck instrument")
    parser.add_argument("wav")
    parser.add_argument("outdir")
    parser.add_argument("--name", required=True)
    parser.add_argument("--root", type=int, default=60)
    parser.add_argument("--loop-start", type=int, default=0)
    parser.add_argument("--loop-end", type=int, default=0)
    parser.add_argument("--oneshot", action="store_true")
    parser.add_argument("--raw", action="store_true", help="write sample.raw instead of sample.wav")
    parser.add_argument("--gain", type=int, default=100)
    args = parser.parse_args()

    samples, rate = read_wav_mono(args.wav)
    os.makedirs(args.outdir, exist_ok=True)
    sample_name = "sample.raw" if args.raw else "sample.wav"
    sample_path = os.path.join(args.outdir, sample_name)
    if args.raw:
        import struct
        with open(sample_path, "wb") as handle:
            handle.write(b"".join(struct.pack("<h", s) for s in samples))
    else:
        write_wav(sample_path, samples, rate)
    manifest = os.path.join(args.outdir, "manifest.txt")
    with open(manifest, "w", encoding="utf-8") as handle:
        handle.write("mothdeck-instrument 1\n")
        handle.write("name=%s\n" % args.name)
        handle.write("type=sample\n")
        handle.write("sample=%s\n" % sample_name)
        handle.write("root=%d\n" % args.root)
        handle.write("rate=%d\n" % rate)
        handle.write("loop_start=%d\n" % args.loop_start)
        handle.write("loop_end=%d\n" % args.loop_end)
        handle.write("oneshot=%d\n" % (1 if args.oneshot else 0))
        handle.write("gain=%d\n" % args.gain)
    print("wrote %s" % args.outdir)


if __name__ == "__main__":
    main()
