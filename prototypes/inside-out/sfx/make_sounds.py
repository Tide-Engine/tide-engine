"""Original restrained Foley and a seamless fluorescent-room ambience loop.

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


def threshold():
    """A soft breath of air through a doorway.

    Seeded noise through gentle low-pass bands, with nothing above about a
    kilohertz: the earlier version's brush reached 2.4 kHz with a 22 ms
    attack and was hard on the ears. It swells and fades on one smooth curve,
    with no impact, pitch, echo or ringing tail.
    """
    seconds = 0.42
    rng = random.Random(33)
    brush_cutoff = 1 - math.exp(-math.tau * 850 / RATE)
    brush_bass_cutoff = 1 - math.exp(-math.tau * 180 / RATE)
    air_cutoff = 1 - math.exp(-math.tau * 420 / RATE)
    air_bass_cutoff = 1 - math.exp(-math.tau * 65 / RATE)
    brush = brush_soft = brush_softer = brush_bass = 0.0
    air = air_soft = air_bass = 0.0
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        brush += brush_cutoff * (rng.uniform(-1, 1) - brush)
        brush_soft += brush_cutoff * (brush - brush_soft)
        brush_softer += brush_cutoff * (brush_soft - brush_softer)
        brush_bass += brush_bass_cutoff * (brush_softer - brush_bass)
        air += air_cutoff * (rng.uniform(-1, 1) - air)
        air_soft += air_cutoff * (air - air_soft)
        air_bass += air_bass_cutoff * (air_soft - air_bass)
        # One rounded swell: a raised cosine, a little early.
        phase = (t / seconds) ** 0.8
        swell = 0.5 - 0.5 * math.cos(math.tau * phase)
        samples.append((0.20 * (brush_softer - brush_bass) + 0.30 * (air_soft - air_bass)) * swell)
    # Keep the clip's boundary exactly silent even after PCM quantization.
    samples[0] = samples[-1] = 0.0
    write("enter", samples)


wood("step", 0.11, 290, 0.21, 20)
wood("push", 0.22, 155, 0.32, 21)
wood("stop", 0.09, 118, 0.08, 22)
threshold()
chime("undo", [(0, 659.25, 0.14), (0.07, 523.25, 0.14)], 0.8)
chime("reset", [(0, 440, 0.13), (0.12, 349.23, 0.13)], 0.9)
chime("solve", [(0, 349.23, 0.16), (0.12, 440, 0.16), (0.24, 523.25, 0.16),
                (0.38, 698.46, 0.13), (0.56, 880, 0.10)], 1.9)

# One periodic noise buffer is filtered twice. The first pass settles the
# filters onto their periodic solution; the second has the same smooth filter
# history at the loop boundary. Integer-period hum and slow air modulation
# also continue across that boundary. There are no stingers or sudden events.
duration = 12
rng = random.Random(81)
noise = [rng.uniform(-1, 1) for _ in range(RATE * duration)]
air = rumble = 0.0
ambience = []
for cycle in range(2):
    for sample, value in enumerate(noise):
        air = 0.965 * air + 0.035 * value
        rumble = 0.998 * rumble + 0.002 * value
        if cycle == 0:
            continue
        t = sample / RATE
        breeze = air * (0.035 + 0.006 * math.sin(math.tau * t / duration))
        motor = 0.019 * math.sin(math.tau * 50 * t) + 0.006 * math.sin(math.tau * 100 * t)
        ambience.append(breeze + rumble * 0.08 + motor)
write("ambience", ambience)
print("Generated eight original clips at 24 kHz; ambience peak", round(max(abs(s) for s in ambience), 4))
