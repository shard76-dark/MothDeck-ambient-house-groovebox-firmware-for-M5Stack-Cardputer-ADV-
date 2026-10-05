#!/usr/bin/env python3
"""Turn one or more WAV files into a MothDeck loop library folder.

Each loop is rewritten as unsigned 8-bit mono PCM at 22050 Hz. Duration
is kept. 128 is silence. The firmware streams these files.

Example:
  python3 tools/wav_to_loop.py --name house --bpm 120 --bars 1 --tags drums \\
      sd-card/loops/house kick.wav hat.wav
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcmutil import ASSET_RATE, read_wav_mono, resample, write_wav  # noqa: E402


def safe_name(path, index):
    base = os.path.splitext(os.path.basename(path))[0]
    cleaned = []
    for ch in base:
        if ch.isalnum() or ch in "_-":
            cleaned.append(ch)
        else:
            cleaned.append("_")
    name = "".join(cleaned).strip("._") or ("loop%d" % index)
    return name[:20]


def main():
    parser = argparse.ArgumentParser(description="Convert WAVs into a MothDeck loop library")
    parser.add_argument("wavs", nargs="+")
    parser.add_argument("outdir")
    parser.add_argument("--name", required=True)
    parser.add_argument("--bpm", type=int, required=True)
    parser.add_argument("--bars", type=int, default=1)
    parser.add_argument("--tags", default="")
    args = parser.parse_args()
    if not (40 <= args.bpm <= 240):
        raise SystemExit("bpm must be 40..240")
    if not (1 <= args.bars <= 8):
        raise SystemExit("bars must be 1..8")

    os.makedirs(args.outdir, exist_ok=True)
    lines = [
        "mothdeck-loops 1",
        "name=%s" % args.name,
        "bpm=%d" % args.bpm,
        "bars=%d" % args.bars,
    ]
    if args.tags:
        lines.append("tags=%s" % args.tags)
    for index, wav in enumerate(args.wavs):
        samples, rate = read_wav_mono(wav)
        if rate != ASSET_RATE:
            samples = [int(round(s)) for s in resample(samples, rate, ASSET_RATE)]
            rate = ASSET_RATE
        filename = safe_name(wav, index) + ".wav"
        write_wav(os.path.join(args.outdir, filename), samples, rate)
        lines.append("loop=%s" % filename)
    with open(os.path.join(args.outdir, "manifest.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("wrote %s" % args.outdir)


if __name__ == "__main__":
    main()
