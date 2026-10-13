"""Original arithmetic-only lounge loop and cues. No recordings or samples."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).parent
RATE = 24000
TAU = math.tau

def write(name, values):
    with wave.open(str(ROOT / (name + '.wav')), 'wb') as f:
        f.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
        f.writeframes(b''.join(struct.pack('<h', round(max(-1, min(1, value)) * 28000)) for value in values))

def cue(name, notes, duration, noise=0):
    rng = random.Random(72)
    values = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        step = min(len(notes) - 1, int(t / duration * len(notes)))
        local = t % (duration / len(notes))
        frequency = notes[step]
        envelope = min(1, local / .008) * math.exp(-local * 13) * min(1, (duration - t) * 25)
        value = math.sin(TAU * frequency * t) * .55 + math.sin(TAU * frequency * 2 * t) * .12
        values.append((value + rng.uniform(-1, 1) * noise) * envelope)
    write(name, values)

cue('take', [659.25, 987.77, 1318.5], .22)
cue('bank', [523.25, 659.25, 783.99, 1046.5], .48)
cue('exit', [587.33, 880], .25)
cue('finish', [392, 523.25, 659.25, 783.99, 1046.5], .85)
cue('fooled', [440, 329.63, 261.63], .32, .1)
cue('bluff', [783.99, 1174.66, 659.25], .25, .12)
cue('dash', [110, 82.4], .15, .7)

# Eight-bar, 108 BPM groove: brushed percussion, plucked bass, soft seventh chords.
beat = 60 / 108
length = beat * 32
values = [0.0] * round(length * RATE)
rng = random.Random(91)
chords = [[130.81, 164.81, 196, 246.94], [146.83, 174.61, 220, 261.63],
          [123.47, 164.81, 196, 220], [130.81, 164.81, 196, 246.94]]
for b in range(32):
    start = round(b * beat * RATE)
    chord = chords[(b // 4) % 4]
    bass = chord[b % 4] * .5
    for j in range(round(beat * RATE)):
        index = start + j
        if index >= len(values): break
        t = j / RATE
        pluck = (math.sin(TAU * bass * t) + .18 * math.sin(TAU * bass * 2 * t)) * math.exp(-t * 9) * min(1, t * 200) * .18
        pad = sum(math.sin(TAU * f * 2 * t) for f in chord) * .013 * math.sin(math.pi * t / beat) ** 2
        kick = math.sin(TAU * (48 * t + 18 * (1 - math.exp(-t * 18)) / 18)) * math.exp(-t * 25) * .16 if b % 2 == 0 else 0
        brush = rng.uniform(-1, 1) * math.exp(-t * 45) * (.038 if b % 2 else .017)
        values[index] += pluck + pad + kick + brush
write('club', values)
