"""Original, deterministic sounds; no recordings or downloaded samples."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 16000
ROOT = Path(__file__).resolve().parent


def write(name, seconds, source):
    rng = random.Random(1709 + sum(map(ord, name)))
    with wave.open(str(ROOT / (name + ".wav")), "wb") as out:
        out.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
        samples = bytearray()
        for i in range(int(seconds * RATE)):
            t = i / RATE
            left, right = source(t, rng)
            samples.extend(struct.pack("<hh", *(int(max(-0.88, min(0.88, v)) * 32767) for v in (left, right))))
        out.writeframes(samples)


def ambient(t, rng):
    # All tonal frequencies complete whole cycles across this eight-second loop.
    a = math.sin(t * math.tau * 43) * 0.065 + math.sin(t * math.tau * 57.125) * 0.025
    b = math.sin(t * math.tau * 85.875) * (0.018 + 0.008 * math.sin(t * math.tau / 8))
    hiss = rng.uniform(-1, 1) * 0.008
    pipe = math.sin(t * math.tau * 261.875) * 0.008 * (0.5 + 0.5 * math.sin(t * math.tau / 4))
    return a + b + hiss + pipe, a - b * 0.5 + hiss * 0.7 - pipe


def knock(t, rng):
    v = 0.0
    for start, gain in ((0, 0.3), (0.27, 0.22), (0.56, 0.16)):
        d = t - start
        if d >= 0:
            v += gain * math.exp(-d * 26) * (math.sin(d * math.tau * 87) + rng.uniform(-0.5, 0.5))
            v += gain * 0.17 * math.exp(-d * 4) * math.sin(d * math.tau * 183)
    return v, v * 0.74


def bell(t, rng):
    v = sum(math.sin(t * math.tau * f) * g * math.exp(-t * decay)
            for f, g, decay in ((294, 0.17, 1.7), (611, 0.05, 2.3), (834, 0.03, 2.8), (1231, 0.012, 4)))
    return v, v * 0.85


def scrape(t, rng):
    fade = min(1, t * 14) * math.exp(-t * 2.8)
    v = (rng.uniform(-1, 1) * 0.11 + math.sin(t * math.tau * (160 - 48 * t)) * 0.13) * fade
    return v, v * (0.7 + 0.2 * math.sin(t * 4))


def paper(t, rng):
    v = rng.uniform(-1, 1) * 0.14 * math.exp(-t * 15) + math.sin(t * math.tau * 97) * 0.12 * math.exp(-t * 27)
    return v, v * 0.8


def breath(t, rng):
    pulse = math.sin(t * math.pi / 4) ** 2
    v = (rng.uniform(-1, 1) * 0.028 + math.sin(t * math.tau * 131) * 0.009) * pulse
    return v * 0.8, v


write("room", 8, ambient)
write("knock", 1.7, knock)
write("bell", 2.8, bell)
write("scrape", 2, scrape)
write("paper", 0.5, paper)
write("breath", 4, breath)
