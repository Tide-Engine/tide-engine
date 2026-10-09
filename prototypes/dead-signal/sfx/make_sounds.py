"""Generate the private prototype's original mono sound palette."""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 48000
HERE = Path(__file__).resolve().parent


def write(name, duration, voice):
    rng = random.Random(name)
    count = int(duration * RATE)
    samples = []
    for i in range(count):
        t = i / RATE
        value = max(-1.0, min(1.0, voice(t, duration, rng)))
        samples.append(struct.pack("<h", round(value * 26000)))
    with wave.open(str(HERE / (name + ".wav")), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(b"".join(samples))


def sine(hz, t):
    return math.sin(math.tau * hz * t)


def burst(t, duration, rng, bass, fall, noise):
    envelope = min(1, t * 2400) * math.exp(-t * fall) * min(1, (duration - t) * 120)
    return envelope * (sine(bass, t) * 0.6 + (rng.random() * 2 - 1) * noise)


write("pistol", 0.22, lambda t, d, r: burst(t, d, r, 92, 24, 0.65))
write("shotgun", 0.42, lambda t, d, r: burst(t, d, r, 61, 13, 0.75))
write("pulse", 0.28, lambda t, d, r: (sine(880 - t * 1300, t) * 0.45 + sine(220, t) * 0.22) * math.exp(-t * 17) * min(1, t * 1600))
write("hit", 0.10, lambda t, d, r: burst(t, d, r, 620, 48, 0.4))
write("kill", 0.25, lambda t, d, r: burst(t, d, r, 46, 19, 0.42))
write("hurt", 0.31, lambda t, d, r: burst(t, d, r, 71, 12, 0.28))
write("step", 0.12, lambda t, d, r: burst(t, d, r, 57, 38, 0.3))
write("revive", 0.95, lambda t, d, r: (sine(330, t) + sine(440, t) + sine(660, t)) * 0.18 * math.sin(math.pi * t / d) ** 2)
write("relay", 0.80, lambda t, d, r: (sine(220, t) * 0.19 + sine(330, t) * 0.16 + sine(440, t) * 0.12) * math.sin(math.pi * t / d) ** 2)
write("alarm", 1.20, lambda t, d, r: sine(130 + 90 * (0.5 + 0.5 * sine(2.5, t)), t) * 0.27 * math.sin(math.pi * t / d) ** 2)
write("pickup", 0.25, lambda t, d, r: sine(680 + 260 * t, t) * math.sin(math.pi * t / d) ** 2 * 0.3)

# Integer-frequency layers make an eight-second seamless industrial loop.
write("hum", 8.0, lambda t, d, r: sine(37, t) * 0.12 + sine(74, t) * 0.04 + sine(111, t) * 0.025 + sine(181, t) * 0.015 * (0.5 + 0.5 * sine(0.5, t)))
