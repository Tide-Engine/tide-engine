"""Makes Aerial's sounds: every clip is arithmetic, written as mono 16-bit WAV at 48 kHz.

Run it from anywhere: `python prototypes/aerial/sfx/make_sounds.py`. It uses
the standard library alone and a fixed seed, so it writes the same bytes
every time.
"""
import math
import os
import random
import struct
import wave

RATE = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
random.seed(20261010)


def write(name, samples, peak=0.9):
    loudest = max(1e-9, max(abs(s) for s in samples))
    scale = peak / loudest
    with wave.open(os.path.join(HERE, name), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))


def noise(count):
    return [random.uniform(-1, 1) for _ in range(count)]


def lowpass(samples, cutoff):
    """One pole, by a cutoff in Hz that can change along the clip."""
    out, held = [], 0.0
    for at, s in enumerate(samples):
        hz = cutoff(at / RATE) if callable(cutoff) else cutoff
        held += (s - held) * (1 - math.exp(-math.tau * hz / RATE))
        out.append(held)
    return out


def highpass(samples, cutoff):
    low = lowpass(samples, cutoff)
    return [s - l for s, l in zip(samples, low)]


def looped(samples, overlap):
    """Fades the clip's tail into its start, so it repeats without a click."""
    body = len(samples) - overlap
    out = samples[:body]
    for at in range(overlap):
        mix = at / overlap
        out[at] = out[at] * mix + samples[body + at] * (1 - mix)
    return out


def tone(seconds, hz, shape=math.sin):
    return [shape(math.tau * (hz(at / RATE) if callable(hz) else hz) * at / RATE) for at in range(int(seconds * RATE))]


def sweep(seconds, start, end):
    """A sine whose pitch slides, by its phase so it never jumps."""
    out, phase = [], 0.0
    count = int(seconds * RATE)
    for at in range(count):
        hz = start * (end / start) ** (at / count)
        phase += math.tau * hz / RATE
        out.append(math.sin(phase))
    return out


def shaped(samples, envelope):
    count = len(samples)
    return [s * envelope(at / count) for at, s in enumerate(samples)]


def mixed(*parts):
    count = max(len(p) for p in parts)
    return [sum(p[at] for p in parts if at < len(p)) for at in range(count)]


def decay(rate):
    return lambda t: math.exp(-t * rate)


def swell(attack, rate):
    return lambda t: min(1, t / attack) * math.exp(-max(0, t - attack) * rate)


# The engine: a stack of harmonics on a whole number of turns, so it loops as it is
second = int(RATE * 1.0)
engine = [0.0] * second
for harmonic, weight in ((1, 1.0), (2, 0.7), (3, 0.45), (4, 0.3), (6, 0.2), (8, 0.12), (11, 0.07)):
    wobble = random.uniform(0, math.tau)
    for at in range(second):
        t = at / RATE
        engine[at] += weight * math.sin(math.tau * 55 * harmonic * t + 0.25 * math.sin(math.tau * 11 * t + wobble))
rasp = looped(lowpass(noise(second + 4800), 900), 4800)
write("engine.wav", [e * (0.8 + 0.2 * math.sin(math.tau * 22 * at / RATE)) + rasp[at] * 0.5 for at, e in enumerate(engine)], 0.8)

# The boost: a roar of filtered noise over a low rumble
roar = looped(highpass(lowpass(noise(second + 9600), 2600), 180), 9600)
rumble = looped(lowpass(noise(second + 9600), 140), 9600)
write("boost.wav", [roar[at] * (0.85 + 0.15 * math.sin(math.tau * 37 * at / RATE)) + rumble[at] * 2.4 for at in range(second)], 0.85)

# The crowd: a wide murmur that swells and settles
long = int(RATE * 4.0)
murmur = looped(highpass(lowpass(noise(long + 24000), 1500), 250), 24000)
write("crowd.wav", [m * (0.75 + 0.25 * math.sin(math.tau * 0.5 * at / RATE)) for at, m in enumerate(murmur)], 0.7)

cheer = highpass(lowpass(noise(int(RATE * 3.0)), lambda t: 1200 + 2200 * min(1, t * 2)), 350)
write("cheer.wav", shaped(cheer, lambda t: min(1, t * 8) * (1 - t) ** 1.3 * (0.8 + 0.2 * math.sin(t * 60))), 0.85)

# The ball: a deep thump with a click on top; off the arena, a duller one
thump = mixed(shaped(sweep(0.28, 190, 48), decay(9)), shaped(lowpass(noise(int(RATE * 0.05)), 3000), decay(18)))
write("hit.wav", thump)
write("bounce.wav", mixed(shaped(sweep(0.22, 120, 42), decay(11)), shaped(lowpass(noise(int(RATE * 0.03)), 900), decay(20))), 0.8)

# A jump is a short puff of air, a dodge a longer rush
write("jump.wav", mixed(shaped(highpass(lowpass(noise(int(RATE * 0.16)), 2400), 500), swell(0.08, 9)),
                        shaped(sweep(0.12, 240, 420), decay(14))), 0.7)
write("dodge.wav", shaped(highpass(lowpass(noise(int(RATE * 0.3)), lambda t: 700 + 5200 * t), 350), swell(0.3, 6)), 0.75)

# A goal: the blast, then a horn of three stacked notes
blast = shaped(lowpass(noise(int(RATE * 1.2)), lambda t: 2400 * math.exp(-t * 3) + 90), decay(4))
boom = shaped(sweep(1.0, 95, 30), decay(3.5))
horn = [0.0] * int(RATE * 2.4)
for hz in (220.0, 277.18, 329.63, 440.0):
    for at in range(len(horn)):
        t = at / RATE
        horn[at] += 0.25 * (2 * ((hz * t) % 1) - 1)
horn = shaped(lowpass(horn, 2600), lambda t: min(1, t * 30) * (1 if t < 0.75 else max(0, 1 - (t - 0.75) * 4)))
write("goal.wav", mixed([b * 1.6 for b in blast], [b * 1.8 for b in boom], [0.0] * int(RATE * 0.12) + [h * 0.55 for h in horn]), 0.9)

# The countdown, and the higher note that ends it
write("beep.wav", shaped(mixed(tone(0.16, 660), [0.3 * s for s in tone(0.16, 1320)]), swell(0.02, 7)), 0.6)
write("go.wav", shaped(mixed(tone(0.5, 990), [0.4 * s for s in tone(0.5, 1485)]), swell(0.01, 4)), 0.65)

# Boost pads: a small chime, and a fuller one that climbs
write("pad.wav", shaped(mixed(sweep(0.18, 700, 1400), [0.4 * s for s in sweep(0.18, 1400, 2800)]), swell(0.03, 8)), 0.55)
write("bigpad.wav", shaped(mixed(sweep(0.42, 330, 1320), [0.5 * s for s in sweep(0.42, 660, 1980)],
                                 [0.25 * s for s in lowpass(noise(int(RATE * 0.42)), 4000)]), swell(0.05, 5)), 0.7)

# A demolition, a bump between two cars, and the crack of going supersonic
write("wreck.wav", mixed(shaped(lowpass(noise(int(RATE * 1.1)), lambda t: 5000 * math.exp(-t * 4) + 120), decay(4.5)),
                         shaped(sweep(0.8, 110, 28), decay(4))))
write("bump.wav", mixed(shaped(sweep(0.16, 260, 90), decay(13)), shaped(highpass(noise(int(RATE * 0.06)), 1800), decay(16))), 0.8)
write("sonic.wav", mixed(shaped(highpass(lowpass(noise(int(RATE * 0.45)), lambda t: 500 + 6000 * t), 300), swell(0.25, 7)),
                         shaped(sweep(0.3, 70, 140), swell(0.2, 6))), 0.75)

# The final whistle: one long note of the horn
last = [0.0] * int(RATE * 1.6)
for hz in (196.0, 246.94, 293.66):
    for at in range(len(last)):
        last[at] += 0.3 * (2 * ((hz * at / RATE) % 1) - 1)
write("end.wav", shaped(lowpass(last, 2200), lambda t: min(1, t * 40) * (1 if t < 0.8 else max(0, 1 - (t - 0.8) * 5))), 0.8)
