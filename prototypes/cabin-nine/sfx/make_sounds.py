"""Cabin Nine's original deterministic sound palette (Python standard library)."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parent
RATE = 22050
TAU = math.tau


def save(name, seconds, sample):
    random.seed(319)
    frames = bytearray()
    for i in range(int(seconds * RATE)):
        t = i / RATE
        value = max(-1.0, min(1.0, sample(t, random)))
        frames.extend(struct.pack("<h", round(value * 32767)))
    with wave.open(str(ROOT / name), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(frames)


def engine(t, rng):
    # Integer frequencies in a four-second period give a clean loop seam.
    rumble = (math.sin(TAU * 57 * t) * 0.22
              + math.sin(TAU * 91 * t) * 0.10
              + math.sin(TAU * 114 * t + math.sin(TAU * 0.5 * t) * 0.08) * 0.075)
    air = (rng.random() * 2 - 1) * 0.045 * math.sin(math.pi * t / 4) ** 0.2
    return rumble + air


def electricity(t, rng):
    return (math.sin(TAU * 100 * t) * 0.16 + math.sin(TAU * 200 * t) * 0.08
            + math.sin(TAU * 400 * t) * 0.02)


def bell(t, rng):
    first = math.sin(TAU * 587.33 * t) * math.exp(-t * 3.2)
    second = 0 if t < 0.42 else math.sin(TAU * 440 * (t - 0.42)) * math.exp(-(t - 0.42) * 3.1)
    return (first * 0.20 + second * 0.19) * min(1, t * 60)


def loss(t, rng):
    tone = math.sin(TAU * (155 * t - 42 * t * t)) * math.exp(-t * 4)
    crack = (rng.random() * 2 - 1) * math.exp(-t * 20)
    return (tone * 0.24 + crack * 0.20) * min(1, t * 200)


def arrival(t, rng):
    result = 0
    for start, note in [(0, 440), (0.36, 523.25), (0.72, 659.25)]:
        age = t - start
        if age >= 0:
            result += math.sin(TAU * note * age) * math.exp(-age * 2.5) * min(1, age * 70) * 0.17
    return result


save("engine.wav", 4, engine)
save("electric.wav", 2, electricity)
save("chime.wav", 2.2, bell)
save("power-loss.wav", 1.5, loss)
save("arrival.wav", 2.5, arrival)
