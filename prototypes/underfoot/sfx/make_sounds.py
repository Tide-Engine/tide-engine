"""Underfoot's sounds: stone, wood, ice and a slow, low loop for a crypt, all made here.

No samples and no dependencies. Run from any directory; it writes beside itself.
"""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 24000
DEST = Path(__file__).resolve().parent


def write(name, samples, level=0.88):
    peak = max(abs(s) for s in samples) or 1
    gain = min(1, level / peak)
    with wave.open(str(DEST / f"{name}.wav"), "wb") as wav:
        wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<h", round(s * gain * 32767)) for s in samples))


def bell(freq, t, length=1.6, decay=3.2):
    """A soft struck bar: a fundamental and two partials that die sooner."""
    if t < 0 or t >= length:
        return 0.0
    env = min(1, t / 0.006) * min(1, (length - t) / 0.2) * math.exp(-t * decay)
    return env * (math.sin(math.tau * freq * t)
                  + 0.32 * math.sin(math.tau * freq * 2.76 * t) * math.exp(-t * 6)
                  + 0.12 * math.sin(math.tau * freq * 5.4 * t) * math.exp(-t * 11))


def render(seconds, voice):
    return [voice(i / RATE) for i in range(round(seconds * RATE))]


def thud(name, seconds, freq, noise, seed, drop=0.0, damp=38.0):
    """Stone on stone: a low knock with a little grit, optionally falling in pitch."""
    rng = random.Random(seed)
    low = 0.0
    phase = 0.0
    out = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        low = 0.80 * low + 0.20 * rng.uniform(-1, 1)
        phase += math.tau * freq * (1 - drop * t / seconds) / RATE
        env = min(1, t / 0.002) * math.exp(-t * damp) * min(1, (seconds - t) / 0.01)
        out.append(env * (0.6 * math.sin(phase) + noise * low + 0.12 * math.sin(phase * 2.4)))
    write(name, out)


def hiss(name, seconds, colour, seed, swell):
    """Filtered air: sliding on ice, a crate scraped along."""
    rng = random.Random(seed)
    low = 0.0
    band = 0.0
    out = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        white = rng.uniform(-1, 1)
        low += colour * (white - low)
        band = 0.86 * band + 0.14 * (white - low)
        env = swell(t / seconds) * min(1, t / 0.01) * min(1, (seconds - t) / 0.03)
        out.append(env * (low if colour < 0.2 else band))
    write(name, out, 0.6)


thud("step", 0.10, 210, 0.16, 11)
thud("stop", 0.12, 96, 0.10, 12, damp=30)
thud("lay", 0.20, 132, 0.30, 13, drop=0.25, damp=24)
hiss("push", 0.24, 0.08, 14, lambda x: math.sin(math.pi * x) ** 0.6)
hiss("slide", 0.42, 0.5, 15, lambda x: math.sin(math.pi * x) ** 1.5)
write("lift", render(0.5, lambda t: 0.5 * bell(392.0, t, 0.5, 9) + 0.4 * bell(587.33, t - 0.05, 0.45, 10)))
write("portal", render(1.1, lambda t: sum(
    0.3 * bell(f, t - d, 1.0, 4.5) for d, f in [(0, 466.16), (0.06, 698.46), (0.12, 932.33), (0.19, 1396.9)])
    + 0.1 * math.sin(math.tau * (300 + 900 * t) * t) * math.exp(-t * 6)))
write("sink", render(0.9, lambda t: 0.5 * math.sin(math.tau * (190 - 120 * min(1, t / 0.5)) * t) * math.exp(-t * 5)
                    * min(1, t / 0.004) + 0.3 * bell(98.0, t - 0.3, 0.6, 7)))
write("gate", render(0.6, lambda t: 0.45 * bell(830.6, t, 0.6, 9) + 0.35 * bell(1244.5, t - 0.07, 0.5, 11)
                    + 0.2 * bell(311.1, t, 0.4, 14)))
write("undo", render(0.5, lambda t: 0.4 * bell(587.33, t, 0.5, 8) + 0.4 * bell(440.0, t - 0.07, 0.4, 8)))
write("reset", render(0.8, lambda t: sum(0.3 * bell(f, t - d, 0.6, 6) for d, f in [(0, 587.33), (0.09, 440.0), (0.18, 293.66)])))
write("solve", render(2.6, lambda t: sum(a * bell(f, t - d, 2.2, 2.4) for d, f, a in [
    (0, 293.66, 0.26), (0.13, 440.0, 0.26), (0.26, 587.33, 0.26), (0.39, 739.99, 0.24),
    (0.55, 880.0, 0.22), (0.80, 1174.66, 0.18)])))

# The loop: four slow chords under a few struck notes. Every pitch is a whole
# number of cycles long and every tail wraps round, so the seam is silent.
DURATION = 32
music = [0.0] * (RATE * DURATION)


def whole(freq):
    return round(freq * DURATION) / DURATION


chords = [[73.42, 110.0, 146.83, 174.61], [69.30, 103.83, 138.59, 164.81],
          [73.42, 110.0, 155.56, 174.61], [65.41, 98.0, 138.59, 155.56]]
for index, chord in enumerate(chords):
    start = index * 8.0
    for i in range(round(11.0 * RATE)):
        t = i / RATE
        env = min(1, t / 3.0) * min(1, (11.0 - t) / 3.0)
        env = env * env * (3 - 2 * env)
        at = (round(start * RATE) + i) % len(music)
        clock = at / RATE
        tone = 0.0
        for voice, freq in enumerate(chord):
            f = whole(freq)
            tone += math.sin(math.tau * f * clock + voice) * (0.024 if voice else 0.034)
            tone += math.sin(math.tau * whole(freq * 2.006) * clock) * 0.004
        music[at] += tone * env * (0.85 + 0.15 * math.sin(math.tau * clock / 16))

# A music box that has lost most of its teeth.
melody = [(0.0, 659.25), (1.2, 698.46), (2.4, 659.25), (5.0, 493.88), (9.5, 622.25), (10.7, 659.25),
          (14.0, 466.16), (17.0, 659.25), (18.2, 698.46), (21.5, 415.30), (25.0, 622.25), (26.2, 587.33),
          (29.5, 440.0)]
for start, freq in melody:
    for i in range(round(4.0 * RATE)):
        t = i / RATE
        dry = bell(freq, t, 3.2, 1.6) * 0.050
        echo = bell(freq, t - 0.33, 3.2, 2.2) * 0.020 + bell(freq, t - 0.66, 3.2, 2.6) * 0.010
        music[(round(start * RATE) + i) % len(music)] += dry + echo
write("night", music, 0.5)
print("Wrote thirteen clips at", RATE, "Hz")
