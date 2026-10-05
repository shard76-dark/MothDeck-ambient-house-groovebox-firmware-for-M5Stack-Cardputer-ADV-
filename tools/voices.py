"""Original synthesized drums, effects, and tonal snippets.

Nothing here is a recording. gen_samples.py bakes the built-in kit.
make_sd_pack.py writes the SD card pack from the same recipes.
"""
import math


def clamp(x):
    x = int(round(x))
    if x > 32767:
        return 32767
    if x < -32768:
        return -32768
    return x


class Noise:
    def __init__(self, seed):
        self.state = seed & 0x7FFFFFFF or 1

    def step(self):
        self.state = (1103515245 * self.state + 12345) & 0x7FFFFFFF
        return (self.state / 0x7FFFFFFF) * 2.0 - 1.0


def normalize(samples, peak=27000):
    m = 1
    for s in samples:
        a = abs(s)
        if a > m:
            m = a
    if m < 1:
        return samples
    gain = peak / m
    n = len(samples)
    fade = min(int(n * 0.02) + 8, n // 4 or 1)
    out = []
    for i, s in enumerate(samples):
        e = 1.0
        if i < 4:
            e = i / 4.0
        if i >= n - fade:
            e = (n - i) / float(fade)
        out.append(clamp(s * gain * e))
    return out


def space(dry, rate, wet=0.18, tail=0.22):
    extra = int(rate * tail)
    n = len(dry) + extra
    out = [0.0] * n
    for i, s in enumerate(dry):
        out[i] += s
    taps = (
        (int(rate * 0.013), 0.32),
        (int(rate * 0.019), 0.24),
        (int(rate * 0.029), 0.16),
        (int(rate * 0.041), 0.10),
    )
    for delay, gain in taps:
        if delay < 1:
            continue
        for i, s in enumerate(dry):
            j = i + delay
            if j < n:
                out[j] += s * gain * wet
            k = i + delay * 2
            if k < n:
                out[k] += s * gain * gain * wet * 0.45
    return out


def sine_sweep(rate, seconds, f0, f1, amp=0.8, attack=0.005, decay=0.2):
    n = int(rate * seconds)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        u = i / max(1, n - 1)
        f = f0 + (f1 - f0) * u
        phase += 2 * math.pi * f / rate
        env = 1.0
        if t < attack:
            env = t / attack
        else:
            env *= math.exp(-(t - attack) / max(0.01, decay))
        out.append(math.sin(phase) * amp * env)
    return out


def house_kick(rate, seconds=0.50, f_start=150.0, f_end=46.0, drop=26.0):
    n = int(rate * seconds)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = f_end + (f_start - f_end) * math.exp(-t * drop)
        phase += 2 * math.pi * f / rate
        body = math.tanh(math.sin(phase) * 1.35)
        amp = math.exp(-t * 4.2)
        click = math.exp(-t * 280.0) * math.sin(2 * math.pi * 1800 * t)
        punch = math.exp(-t * 90.0) * math.sin(2 * math.pi * 180 * t)
        out.append((body * 0.86 + punch * 0.22 + click * 0.18) * amp)
    return space(out, rate, wet=0.12, tail=0.18)


def rim(rate):
    n = int(rate * 0.09)
    noise = Noise(19)
    out = []
    for i in range(n):
        t = i / rate
        body = math.sin(2 * math.pi * 420 * t) * math.exp(-t * 80)
        tick = noise.step() * math.exp(-t * 220)
        out.append(body * 0.45 + tick * 0.35)
    return space(out, rate, wet=0.08, tail=0.06)


def soft_snare(rate, seconds=0.32, tone=196.0):
    n = int(rate * seconds)
    noise = Noise(7)
    out = []
    hp = 0.0
    prev = 0.0
    for i in range(n):
        t = i / rate
        nz = noise.step()
        hp = nz - prev + 0.82 * hp
        prev = nz
        body = math.sin(2 * math.pi * tone * t) * math.exp(-t * 28)
        noise_amp = math.exp(-t * 14)
        out.append(body * 0.38 + hp * 0.42 * noise_amp)
    return space(out, rate, wet=0.22, tail=0.16)


def clap(rate, seconds=0.28):
    n = int(rate * seconds)
    noise = Noise(11)
    bursts = (0.0, 0.010, 0.021, 0.034)
    out = []
    for i in range(n):
        t = i / rate
        nz = noise.step()
        env = 0.0
        for b in bursts:
            if t >= b:
                env += math.exp(-(t - b) * 70) * (0.55 if b < 0.03 else 1.0)
        tail = math.exp(-max(0.0, t - 0.04) * 9) * (1.0 if t > 0.04 else 0.0)
        out.append(nz * (env * 0.55 + tail * 0.35))
    return space(out, rate, wet=0.2, tail=0.14)


def hat(rate, seconds, seed, bright):
    n = max(8, int(rate * seconds))
    noise = Noise(seed)
    out = []
    hp = 0.0
    prev = 0.0
    for i in range(n):
        t = i / rate
        nz = noise.step()
        hp = nz - prev + (0.92 if bright else 0.78) * hp
        prev = nz
        decay = 55 if bright else 8.5
        metal = math.sin(2 * math.pi * (7400 if bright else 4200) * t)
        out.append((hp * 0.75 + metal * 0.08) * math.exp(-t * decay))
    wet = 0.1 if bright else 0.16
    return space(out, rate, wet=wet, tail=0.05 if bright else 0.12)


def perc(rate):
    n = int(rate * 0.18)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = 280 * math.exp(-t * 8) + 140
        phase += 2 * math.pi * f / rate
        out.append(math.sin(phase) * math.exp(-t * 10) * 0.8)
    return space(out, rate, wet=0.14, tail=0.08)


def tom(rate, f0=140.0):
    n = int(rate * 0.28)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = f0 * 0.55 + (f0 - f0 * 0.55) * math.exp(-t * 14)
        phase += 2 * math.pi * f / rate
        out.append(math.tanh(math.sin(phase) * 1.2) * math.exp(-t * 7))
    return space(out, rate, wet=0.14, tail=0.1)


def shaker(rate):
    n = int(rate * 0.16)
    noise = Noise(23)
    out = []
    for i in range(n):
        t = i / rate
        grain = 0.5 + 0.5 * math.sin(2 * math.pi * 28 * t)
        out.append(noise.step() * grain * math.exp(-t * 12) * 0.45)
    return space(out, rate, wet=0.08, tail=0.05)


def ride(rate):
    n = int(rate * 0.40)
    noise = Noise(29)
    out = []
    for i in range(n):
        t = i / rate
        ping = math.sin(2 * math.pi * 520 * t) * math.exp(-t * 6)
        nz = noise.step() * math.exp(-t * 5)
        out.append(ping * 0.25 + nz * 0.18)
    return space(out, rate, wet=0.18, tail=0.16)


def snap(rate):
    n = int(rate * 0.11)
    noise = Noise(31)
    out = []
    for i in range(n):
        t = i / rate
        out.append(noise.step() * math.exp(-t * 60) * 0.7)
    return space(out, rate, wet=0.1, tail=0.05)


def crash(rate):
    n = int(rate * 0.48)
    noise = Noise(37)
    out = []
    hp = 0.0
    prev = 0.0
    for i in range(n):
        t = i / rate
        nz = noise.step()
        hp = nz - prev + 0.9 * hp
        prev = nz
        out.append(hp * math.exp(-t * 3.2) * 0.55)
    return space(out, rate, wet=0.22, tail=0.2)


def eight_kick(rate):
    # Longer sine, little click, almost no noise. 808-style.
    return house_kick(rate, seconds=0.42, f_start=110, f_end=48, drop=18)


def dusty(samples, rate, seed=3):
    noise = Noise(seed)
    out = []
    prev = 0.0
    for i, s in enumerate(samples):
        # Soft lowpass plus a veil of noise, then a little crush.
        prev += (s - prev) * 0.18
        veil = noise.step() * 400 * math.exp(-i / (rate * 0.4))
        crushed = int(prev + veil)
        crushed = int(crushed / 1800) * 1800
        out.append(crushed)
    return out


def role_kick(rate):
    # Sub thump plus a short click. Dry, so it does not turn into a tom.
    n = int(rate * 0.40)
    phase = 0.0
    noise = Noise(3)
    out = []
    for i in range(n):
        t = i / rate
        f = 46 + 170 * math.exp(-t * 32)
        phase += 2 * math.pi * f / rate
        click = noise.step() * math.exp(-t * 350)
        amp = math.exp(-t * 7.2)
        out.append((math.sin(phase) * 0.92 + click * 0.22) * amp)
    return out


def role_snare(rate):
    n = int(rate * 0.24)
    noise = Noise(101)
    out = []
    prev = 0.0
    hp = 0.0
    for i in range(n):
        t = i / rate
        nz = noise.step()
        hp = nz - prev + 0.7 * hp
        prev = nz
        body = math.sin(2 * math.pi * 188 * t) * math.exp(-t * 35)
        out.append(hp * 0.82 * math.exp(-t * 11) + body * 0.18)
    return out


def role_hat(rate, seconds, seed, decay):
    n = max(8, int(rate * seconds))
    noise = Noise(seed)
    out = []
    prev = 0.0
    hp = 0.0
    for i in range(n):
        t = i / rate
        nz = noise.step()
        hp = nz - prev + 0.93 * hp
        prev = nz
        metal = math.sin(2 * math.pi * 6500 * t) * math.sin(2 * math.pi * 9100 * t)
        out.append((hp * 0.78 + metal * 0.22) * math.exp(-t * decay))
    return out


def role_clap(rate):
    n = int(rate * 0.22)
    noise = Noise(17)
    bursts = (0.0, 0.011, 0.023)
    out = []
    for i in range(n):
        t = i / rate
        env = 0.0
        for b in bursts:
            if t >= b:
                env += math.exp(-(t - b) * 55)
        out.append(noise.step() * env * 0.45)
    return out


def role_tom(rate, f0, seconds=0.26):
    n = int(rate * seconds)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = f0 * 0.62 + (f0 - f0 * 0.62) * math.exp(-t * 16)
        phase += 2 * math.pi * f / rate
        out.append(math.tanh(math.sin(phase) * 1.4) * math.exp(-t * 8))
    return out


def role_rim(rate):
    n = int(rate * 0.06)
    noise = Noise(19)
    out = []
    for i in range(n):
        t = i / rate
        tick = noise.step() * math.exp(-t * 280)
        wood = math.sin(2 * math.pi * 880 * t) * math.exp(-t * 90)
        out.append(tick * 0.55 + wood * 0.45)
    return out


def ambient_kit(rate):
    # One role per pad, recorded pitch only. No shared room tail: that smear
    # made the kit sound like one tone played in different keys.
    hits = [
        role_kick(rate),
        role_rim(rate),
        role_snare(rate),
        role_clap(rate),
        role_hat(rate, 0.045, 5, 70),
        role_hat(rate, 0.28, 9, 9),
        role_tom(rate, 92, 0.30),
        role_tom(rate, 180, 0.22),
        shaker(rate),
        ride(rate),
        snap(rate),
        crash(rate),
    ]
    return [normalize(h) for h in hits]


def kit_808(rate):
    hits = [
        eight_kick(rate),
        rim(rate),
        soft_snare(rate, 0.22, 210),
        clap(rate, 0.22),
        hat(rate, 0.045, 4, True),
        hat(rate, 0.20, 8, False),
        perc(rate),
        tom(rate, 96),
        shaker(rate),
        ride(rate),
        snap(rate),
        crash(rate),
    ]
    # Keep the 808 kit tighter than the ambient one.
    trimmed = []
    for h in hits:
        cap = int(rate * 0.36)
        trimmed.append(normalize(h[:cap]))
    return trimmed


def kit_dusty(rate):
    base = ambient_kit(rate)
    return [normalize(dusty(h, rate, 40 + i)) for i, h in enumerate(base)]


def sfx_riser(rate):
    n = int(rate * 0.75)
    noise = Noise(41)
    phase = 0.0
    out = []
    for i in range(n):
        u = i / max(1, n - 1)
        f = 180 * (2.2 ** (u * 4.2))
        phase += 2 * math.pi * min(f, rate * 0.45) / rate
        nz = noise.step() * u
        env = u ** 1.4
        out.append((math.sin(phase) * 0.55 + nz * 0.35) * env)
    return normalize(out)


def sfx_down(rate):
    n = int(rate * 0.60)
    phase = 0.0
    out = []
    for i in range(n):
        u = i / max(1, n - 1)
        f = 2400 * (0.5 ** (u * 6)) + 40
        phase += 2 * math.pi * f / rate
        env = math.exp(-u * 1.2)
        out.append(math.sin(phase) * env)
    return normalize(space(out, rate, 0.16, 0.12))


def sfx_zap(rate):
    n = int(rate * 0.20)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = 80 + 2800 * math.exp(-t * 28)
        phase += 2 * math.pi * f / rate
        sq = 1.0 if math.sin(phase) >= 0 else -1.0
        out.append(sq * math.exp(-t * 16) * 0.7)
    return normalize(out)


def sfx_sweep(rate):
    n = int(rate * 0.55)
    noise = Noise(53)
    out = []
    band = 0.0
    for i in range(n):
        u = i / max(1, n - 1)
        center = 0.5 - 0.5 * math.cos(2 * math.pi * u)
        nz = noise.step()
        coef = 0.05 + 0.5 * center
        band += (nz - band) * coef
        out.append(band * (0.35 + 0.65 * center))
    return normalize(out)


def sfx_impact(rate):
    body = house_kick(rate, 0.36, 90, 36, 14)
    noise = Noise(59)
    n = len(body)
    out = []
    for i, s in enumerate(body):
        t = i / rate
        out.append(s + noise.step() * math.exp(-t * 30) * 0.25 * 20000)
    return normalize(out)


def sfx_noise(rate):
    n = int(rate * 0.16)
    noise = Noise(61)
    out = []
    for i in range(n):
        t = i / rate
        out.append(noise.step() * math.exp(-t * 18))
    return normalize(out)


def sfx_blip(rate):
    n = int(rate * 0.12)
    out = []
    for i in range(n):
        t = i / rate
        out.append(math.sin(2 * math.pi * 880 * t) * math.exp(-t * 28))
    return normalize(out)


def sfx_siren(rate):
    n = int(rate * 0.70)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = 540 + 220 * math.sin(2 * math.pi * 3.2 * t)
        phase += 2 * math.pi * f / rate
        out.append(math.sin(phase) * 0.7)
    return normalize(out)


def sfx_reverse(rate):
    n = int(rate * 0.45)
    noise = Noise(67)
    out = []
    for i in range(n):
        u = i / max(1, n - 1)
        out.append(noise.step() * (u ** 2))
    return normalize(out)


def sfx_drop(rate):
    n = int(rate * 0.55)
    phase = 0.0
    out = []
    for i in range(n):
        t = i / rate
        f = 140 * math.exp(-t * 3.2) + 32
        phase += 2 * math.pi * f / rate
        out.append(math.tanh(math.sin(phase) * 1.8) * math.exp(-t * 1.6))
    return normalize(out)


def sfx_bubble(rate):
    n = int(rate * 0.36)
    out = [0.0] * n
    freqs = (620, 480, 360, 260)
    for k, f in enumerate(freqs):
        start = int(rate * 0.07 * k)
        phase = 0.0
        for i in range(int(rate * 0.09)):
            idx = start + i
            if idx >= n:
                break
            t = i / rate
            phase += 2 * math.pi * f / rate
            out[idx] += math.sin(phase) * math.exp(-t * 22) * 0.8
    return normalize(out)


def sfx_whoosh(rate):
    n = int(rate * 0.42)
    noise = Noise(71)
    out = []
    band = 0.0
    for i in range(n):
        u = i / max(1, n - 1)
        env = math.sin(math.pi * u) ** 1.3
        coef = 0.08 + 0.55 * u
        nz = noise.step()
        band += (nz - band) * coef
        out.append(band * env)
    return normalize(out)


def sfx_bank(rate):
    return [
        sfx_riser(rate),
        sfx_down(rate),
        sfx_zap(rate),
        sfx_sweep(rate),
        sfx_impact(rate),
        sfx_noise(rate),
        sfx_blip(rate),
        sfx_siren(rate),
        sfx_reverse(rate),
        sfx_drop(rate),
        sfx_bubble(rate),
        sfx_whoosh(rate),
    ]


DRUM_NAMES = (
    "kick", "rim", "snare", "clap", "hat", "openhat",
    "perc", "tom", "shaker", "ride", "snap", "crash",
)
SFX_NAMES = (
    "riser", "downlifter", "zap", "sweep", "impact", "noise",
    "blip", "siren", "reverse", "drop", "bubble", "whoosh",
)
