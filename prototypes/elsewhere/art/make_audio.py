"""Rebuild Elsewhere's original, quiet score and ambience using stdlib only."""
import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050
TAU = math.tau
ROOT = Path(__file__).resolve().parent


def save(name, values):
    with wave.open(str(ROOT / (name + '.wav')), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b''.join(struct.pack('<h', round(max(-0.95, min(0.95, v)) * 32767)) for v in values))


def bell(t, frequency):
    if t < 0:
        return 0.0
    attack = 1 - math.exp(-t * 75)
    return attack * (math.sin(TAU * frequency * t) * math.exp(-t * 2.1)
                     + math.sin(TAU * frequency * 2.002 * t) * math.exp(-t * 4.3) * 0.21
                     + math.sin(TAU * frequency * 3.01 * t) * math.exp(-t * 7) * 0.07)


def frequency(note):
    return 440 * 2 ** ((note - 69) / 12)


# An original 32-second repeating miniature, four suspended/add-nine chords.
chords = [(48, 55, 59, 62), (45, 52, 55, 59), (41, 48, 52, 57), (43, 50, 55, 57)]
events = [(chord * 8 + n * 1.7, frequency(tone), 0.11) for chord, tones in enumerate(chords) for n, tone in enumerate(tones)]
melody = [76, 74, 71, 67, 69, 71, 74, 72, 76, 79, 76, 74]
events += [(2 + i * 2.4, frequency(tone), 0.065) for i, tone in enumerate(melody)]
music = []
for i in range(RATE * 32):
    t = i / RATE
    v = 0.0
    for onset, f, gain in events:
        age = (t - onset) % 32
        if age < 5:
            v += bell(age, f) * gain
        echo = (t - onset - 0.37) % 32
        if echo < 3:
            v += bell(echo, f) * gain * 0.19
    music.append(v)
save('music', music)
save('chime', [bell(i / RATE, frequency(79)) * 0.24 for i in range(RATE * 2)])
save('bloom', [sum(bell(i / RATE - n * 0.19, frequency(tone)) * 0.12 for n, tone in enumerate([60, 67, 71, 74, 79])) for i in range(RATE * 4)])

# Periodic oscillator sums avoid a click at the loop boundary. Random phases
# and a broad spectrum make a soft, non-tonal bed, without captured audio.
random.seed(73491)
for name, low, high, gain in [('air', 50, 550, 0.045), ('water', 260, 1500, 0.055)]:
    oscillators = [(random.randint(low * 8, high * 8) / 8, random.random() * TAU, random.uniform(0.2, 1)) for _ in range(50)]
    values = []
    for i in range(RATE * 8):
        t = i / RATE
        bed = sum(math.sin(TAU * f * t + phase) * level for f, phase, level in oscillators) / math.sqrt(50)
        flutter = 0.7 + 0.3 * math.sin(TAU * t / 8) ** 2
        values.append(bed * flutter * gain)
    save(name, values)
print('Elsewhere original score and ambience rebuilt')
