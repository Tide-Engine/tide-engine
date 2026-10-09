"""Original car-soccer sounds. Standard library only; no recorded samples."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 48000
ROOT = Path(__file__).resolve().parent
TAU = math.tau


def save(name, samples):
    samples = list(samples)
    peak = max(1.0, max(abs(sample) for sample in samples))
    data = b"".join(struct.pack("<h", round(max(-1, min(1, sample / peak)) * 24000)) for sample in samples)
    with wave.open(str(ROOT / (name + ".wav")), "wb") as output:
        output.setparams((1, 2, RATE, len(samples), "NONE", "not compressed"))
        output.writeframes(data)


def engine():
    # Integer harmonics over a one-second period make the loop seamless.
    for i in range(RATE):
        t = i / RATE
        pulse = 0.5 + 0.5 * math.sin(TAU * 32 * t)
        yield (0.36 * math.sin(TAU * 64 * t) + 0.19 * math.sin(TAU * 128 * t)
               + 0.11 * math.sin(TAU * 192 * t) + 0.06 * math.sin(TAU * 320 * t)) * (0.72 + 0.28 * pulse)


def boost():
    rng = random.Random(713)
    filtered = 0.0
    samples = []
    for i in range(RATE):
        filtered = filtered * 0.72 + rng.uniform(-1, 1) * 0.28
        t = i / RATE
        samples.append(filtered * 0.9 + math.sin(TAU * 90 * t) * 0.13 + math.sin(TAU * 180 * t) * 0.05)
    # Crossfade the noise at the seam; both endpoints approach the same value.
    fade = 960
    for i in range(fade):
        amount = i / fade
        joined = samples[i] * amount + samples[-fade + i] * (1 - amount)
        samples[i] = joined
        samples[-fade + i] = joined
    return samples


def impact():
    rng = random.Random(127)
    for i in range(int(RATE * 0.22)):
        t = i / RATE
        envelope = min(t * 2000, 1) * math.exp(-t * 27)
        yield envelope * (0.56 * math.sin(TAU * (85 * t - 85 * t * t)) + 0.36 * rng.uniform(-1, 1))


def jump():
    for i in range(int(RATE * 0.24)):
        t = i / RATE
        envelope = min(t * 400, 1) * math.exp(-t * 18)
        yield envelope * (0.45 * math.sin(TAU * (180 * t + 650 * t * t)) + 0.2 * math.sin(TAU * 90 * t))


def kickoff():
    for i in range(int(RATE * 0.18)):
        t = i / RATE
        envelope = min(t * 250, 1) * min((0.18 - t) * 60, 1)
        yield envelope * (0.32 * math.sin(TAU * 880 * t) + 0.1 * math.sin(TAU * 1760 * t))


def goal():
    rng = random.Random(99)
    for i in range(int(RATE * 1.8)):
        t = i / RATE
        envelope = min(t * 30, 1) * math.exp(-t * 2.0)
        fanfare = sum(math.sin(TAU * frequency * t) for frequency in (220, 277.1826, 329.6276, 440)) * 0.12
        crowd = rng.uniform(-1, 1) * 0.18 * min(t * 4, 1)
        yield envelope * (fanfare + crowd)


if __name__ == "__main__":
    for name, function in (("engine", engine), ("boost", boost), ("impact", impact),
                           ("jump", jump), ("kickoff", kickoff), ("goal", goal)):
        save(name, function())
        print(name + ".wav")
