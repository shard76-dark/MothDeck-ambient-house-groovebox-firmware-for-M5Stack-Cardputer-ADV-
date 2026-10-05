#!/usr/bin/env python3
"""Synthesize the built-in drum, sfx, and instrument tables.

MothOS shipped copyrighted .h sample dumps that are not in this tree.
These are short, original, deterministic waveforms so the firmware runs
with no SD card. Regenerate with: python3 tools/gen_samples.py
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "include", "DefaultSamples.h")
OUT_C = os.path.join(ROOT, "src", "default_samples.cpp")


def clamp(x):
    x = int(round(x))
    if x > 32767:
        return 32767
    if x < -32768:
        return -32768
    return x


def env(i, n, attack=0.01, decay=0.25):
    t = i / max(1, n - 1)
    if t < attack:
        return t / attack
    rest = (t - attack) / max(1e-6, 1 - attack)
    return math.exp(-rest / max(0.02, decay))


def kick(n, f0, f1):
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        f = f1 + (f0 - f1) * math.exp(-t * 7)
        phase += 2 * math.pi * f / 22050
        click = math.exp(-i / 12.0) * (1 if (i // 3) % 2 == 0 else -1)
        s = math.sin(phase) * 0.9 + click * 0.35
        out.append(clamp(s * env(i, n, 0.002, 0.18) * 28000))
    return out


def snare(n, tone):
    out = []
    phase = 0.0
    state = 1
    for i in range(n):
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        noise = (state / 0x7FFFFFFF) * 2 - 1
        phase += 2 * math.pi * tone / 22050
        s = noise * 0.75 + math.sin(phase) * 0.35
        out.append(clamp(s * env(i, n, 0.001, 0.12) * 26000))
    return out


def hat(n, seed, bright):
    out = []
    state = seed
    hp = 0.0
    prev = 0.0
    for i in range(n):
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        noise = (state / 0x7FFFFFFF) * 2 - 1
        hp = noise - prev + 0.85 * hp
        prev = noise
        s = (hp if bright else noise * 0.4 + hp * 0.6)
        out.append(clamp(s * env(i, n, 0.001, 0.05 if bright else 0.09) * 22000))
    return out


def blip(n, f, ratio):
    out = []
    for i in range(n):
        t = i / 22050
        mod = math.sin(2 * math.pi * f * ratio * t)
        s = math.sin(2 * math.pi * f * t + mod * 2.5)
        out.append(clamp(s * env(i, n, 0.005, 0.2) * 24000))
    return out


def cycle(n, kind):
    out = []
    for i in range(n):
        p = i / n
        if kind == "sine":
            s = math.sin(2 * math.pi * p)
        elif kind == "square":
            s = 0.7 if p < 0.5 else -0.7
        elif kind == "saw":
            s = 2 * p - 1
        elif kind == "tri":
            s = 1 - 4 * abs(p - 0.5)
        elif kind == "organ":
            s = (math.sin(2 * math.pi * p) + 0.5 * math.sin(4 * math.pi * p) + 0.25 * math.sin(6 * math.pi * p)) / 1.75
        elif kind == "pluck":
            s = math.sin(2 * math.pi * p) * (1 - p) + 0.3 * math.sin(4 * math.pi * p)
        elif kind == "bell":
            s = math.sin(2 * math.pi * p) * 0.6 + math.sin(2 * math.pi * p * 2.76) * 0.4
        elif kind == "flute":
            s = math.sin(2 * math.pi * p + 0.4 * math.sin(4 * math.pi * p))
        elif kind == "bass":
            s = math.sin(2 * math.pi * p) * 0.85 + (2 * p - 1) * 0.15
        else:
            s = math.sin(2 * math.pi * p) * (0.6 + 0.4 * math.sin(2 * math.pi * p * 3))
        out.append(clamp(s * 26000))
    # Drop DC.
    dc = sum(out) / len(out)
    return [clamp(s - dc) for s in out]


def main():
    tables = []
    tables.append(("kick1", kick(1200, 180, 48)))
    tables.append(("kick2", kick(900, 120, 36)))
    tables.append(("kick3", kick(1500, 90, 30)))
    tables.append(("snare1", snare(1000, 190)))
    tables.append(("snare2", snare(800, 240)))
    tables.append(("snare3", snare(1300, 160)))
    tables.append(("special1", blip(900, 320, 2)))
    tables.append(("special2", blip(1100, 180, 3)))
    tables.append(("special3", blip(700, 440, 1.5)))
    tables.append(("hihat1", hat(500, 3, True)))
    tables.append(("hihat2", hat(700, 9, False)))
    tables.append(("hihat3", hat(400, 17, True)))
    for i, f in enumerate((520, 340, 760, 220, 980, 410, 640, 280, 860, 150, 1100, 480), start=1):
        tables.append(("sfx%d" % i, blip(480 + (i * 37) % 200, f, 1 + (i % 4))))
    for name, kind in (
        ("instrument1", "sine"),
        ("instrument2", "square"),
        ("instrument3", "saw"),
        ("instrument4", "tri"),
        ("instrument5", "organ"),
        ("instrument6", "pluck"),
        ("instrument7", "bell"),
        ("instrument8", "flute"),
        ("instrument9", "bass"),
        ("instrument10", "pad"),
    ):
        tables.append((name, cycle(168, kind)))

    lines = [
        "#ifndef DefaultSamples_h",
        "#define DefaultSamples_h",
        "#include <stdint.h>",
        "// Synthesized stand-ins. See tools/gen_samples.py.",
        "",
    ]
    body = [
        '#include "DefaultSamples.h"',
        "",
    ]
    for name, data in tables:
        lines.append("extern const int16_t %s[];" % name)
        lines.append("extern const int %sLength;" % name)
        body.append("const int16_t %s[] = {" % name)
        row = []
        for i, s in enumerate(data):
            row.append(str(s))
            if len(row) == 16:
                body.append("  " + ",".join(row) + ",")
                row = []
        if row:
            body.append("  " + ",".join(row))
        body.append("};")
        body.append("const int %sLength = %d;" % (name, len(data)))
        body.append("")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(body))
    print("wrote %d tables" % len(tables))


if __name__ == "__main__":
    main()
