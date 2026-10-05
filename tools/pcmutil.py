"""Small PCM helpers shared by the SD-card example and WAV converters."""
import math
import struct
import wave


def clamp16(x):
    x = int(round(x))
    if x > 32767:
        return 32767
    if x < -32768:
        return -32768
    return x


def write_wav(path, samples, rate):
    with wave.open(path, "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(b"".join(struct.pack("<h", clamp16(s)) for s in samples))


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
