"""Small PCM helpers shared by the SD-card example and WAV converters."""
import math
import struct
import wave


# Card assets are unsigned 8-bit mono at this rate. 128 is silence.
ASSET_RATE = 22050


def clamp16(x):
    x = int(round(x))
    if x > 32767:
        return 32767
    if x < -32768:
        return -32768
    return x


def to_u8(sample):
    # Unsigned 8-bit PCM. 128 is silence. Full-scale 16-bit maps onto 0..255.
    s = clamp16(sample)
    v = (s >> 8) + 128
    if v < 0:
        return 0
    if v > 255:
        return 255
    return v


def resample(samples, src_rate, dst_rate):
    if src_rate == dst_rate or src_rate <= 0 or not samples:
        return list(samples)
    n = max(1, int(round(len(samples) * float(dst_rate) / float(src_rate))))
    out = []
    last = len(samples) - 1
    for i in range(n):
        pos = i * float(src_rate) / float(dst_rate)
        i0 = int(pos)
        if i0 >= last:
            out.append(samples[last])
            continue
        frac = pos - i0
        out.append(samples[i0] * (1.0 - frac) + samples[i0 + 1] * frac)
    return out


def write_wav(path, samples, rate, bits=8):
    # Card assets are mono. 8-bit 22050 Hz is the default (about 22 KB/s).
    # 16-bit is still accepted by the firmware for older files.
    with wave.open(path, "w") as handle:
        handle.setnchannels(1)
        handle.setframerate(rate)
        if bits == 16:
            handle.setsampwidth(2)
            handle.writeframes(b"".join(struct.pack("<h", clamp16(s)) for s in samples))
        else:
            handle.setsampwidth(1)
            handle.writeframes(bytes(to_u8(s) for s in samples))


def read_wav_mono(path):
    with wave.open(path, "r") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    if width not in (1, 2):
        raise SystemExit("only 8-bit and 16-bit WAV files are supported")
    samples = []
    if width == 2:
        count = len(raw) // 2
        unpacked = struct.unpack("<" + "h" * count, raw[: count * 2])
        for i in range(frames):
            acc = 0
            for ch in range(channels):
                acc += unpacked[i * channels + ch]
            samples.append(clamp16(acc / channels))
    else:
        for i in range(frames):
            acc = 0
            for ch in range(channels):
                acc += raw[i * channels + ch] - 128
            samples.append(clamp16((acc / channels) * 256))
    return samples, rate


def tone(n, freq, rate, shape="sine", gain=0.6, decay=0.0):
    out = []
    phase = 0.0
    step = 2 * math.pi * freq / rate
    for i in range(n):
        if shape == "square":
            sample = 1.0 if math.sin(phase) >= 0 else -1.0
        elif shape == "saw":
            sample = (phase / math.pi) % 2.0 - 1.0
        elif shape == "triangle":
            sample = 2 * abs((phase / math.pi) % 2.0 - 1.0) - 1.0
        else:
            sample = math.sin(phase)
        env = 1.0
        if decay > 0:
            env = math.exp(-i / (rate * decay))
        out.append(clamp16(sample * gain * 32767 * env))
        phase += step
    return out
