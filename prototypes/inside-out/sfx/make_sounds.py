"""Original soft wooden Foley and a seamless 24-second miniature-workshop loop.

No external samples or dependencies. Run from any working directory.
"""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 24000
DEST = Path(__file__).resolve().parent


def write(name, samples):
    peak = max(abs(s) for s in samples) or 1
    gain = min(1, 0.88 / peak)
    with wave.open(str(DEST / f"{name}.wav"), "wb") as wav:
        wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(b"".join(struct.pack("<h", round(s * gain * 32767)) for s in samples))


def pluck(freq, t, length=1.3):
    if t < 0 or t >= length:
        return 0
    attack = min(1, t / 0.008)
    release = min(1, (length - t) / 0.13)
    env = attack * release * math.exp(-t * 3.8)
    return env * (math.sin(math.tau * freq * t)
                  + 0.24 * math.sin(math.tau * freq * 2 * t) * math.exp(-t * 3)
                  + 0.08 * math.sin(math.tau * freq * 3 * t) * math.exp(-t * 7))


def chime(name, notes, seconds):
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        samples.append(sum(pluck(freq, t - start) * amp for start, freq, amp in notes))
    write(name, samples)


def wood(name, seconds, frequency, noise_level, seed):
    rng = random.Random(seed)
    filtered = 0
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        filtered = 0.82 * filtered + 0.18 * rng.uniform(-1, 1)
        env = min(1, t / 0.003) * math.exp(-t * 42) * min(1, (seconds - t) / 0.012)
        samples.append(env * (0.23 * math.sin(math.tau * frequency * t)
                              + noise_level * filtered
                              + 0.06 * math.sin(math.tau * frequency * 2.31 * t)))
    write(name, samples)


wood("step", 0.11, 290, 0.21, 20)
wood("push", 0.22, 155, 0.32, 21)
wood("stop", 0.09, 118, 0.08, 22)
chime("swap", [(0, 349.23, 0.20), (0.09, 440, 0.18), (0.18, 523.25, 0.15)], 1.3)
chime("undo", [(0, 659.25, 0.14), (0.07, 523.25, 0.14)], 0.8)
chime("reset", [(0, 440, 0.13), (0.12, 349.23, 0.13)], 0.9)
chime("solve", [(0, 349.23, 0.16), (0.12, 440, 0.16), (0.24, 523.25, 0.16),
                (0.38, 698.46, 0.13), (0.56, 880, 0.10)], 1.9)

# Loop tones are integer-period frequencies. Notes' reverb tails wrap into the
# start of the buffer, so the repeat has neither a cut tail nor a click.
duration = 24
music = [0.0] * (RATE * duration)
notes = [349.23, 440, 523.25, 659.25, 523.25, 440, 293.66, 349.23,
         440, 587.33, 523.25, 440, 349.23, 440, 523.25, 698.46]
for note, freq in enumerate(notes):
    start = note * 1.5
    for sample in range(round(3.5 * RATE)):
        t = sample / RATE
        dry = pluck(freq, t, 2.4) * 0.105
        echo = pluck(freq, t - 0.24, 2.4) * 0.020 + pluck(freq, t - 0.49, 2.4) * 0.012
        music[(round(start * RATE) + sample) % len(music)] += dry + echo
for sample in range(len(music)):
    t = sample / RATE
    for freq in [174.625, 220, 261.625]:
        periodic = round(freq * duration) / duration
        music[sample] += math.sin(math.tau * periodic * t) * (0.012 + 0.005 * math.sin(math.tau * t / duration))
write("workshop", music)
print("Generated eight original clips at 24 kHz; music peak", round(max(abs(s) for s in music), 4))
