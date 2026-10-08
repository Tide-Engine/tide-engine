# Makes the tag game's sounds from nothing but arithmetic: no samples, no recordings.
#   python make_sounds.py <folder>
import math, os, random, struct, sys, wave

RATE = 22050
folder = sys.argv[1]
os.makedirs(folder, exist_ok=True)
random.seed(7)


def write(name, samples, peak=0.7):
    top = max(1e-9, max(abs(s) for s in samples))
    scale = peak / top
    with wave.open(os.path.join(folder, name), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s * scale)) * 32767)) for s in samples))


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def add(into, at, samples):
    start = int(at * RATE)
    for i, s in enumerate(samples):
        if start + i < len(into):
            into[start + i] += s


def tone(freq, seconds, decay, partials=((1, 1.0),), bend=0.0):
    out = []
    phase = [0.0] * len(partials)
    for i in range(int(RATE * seconds)):
        t = i / RATE
        f = freq * (1 + bend * math.exp(-t * 30))
        value = 0.0
        for k, (ratio, level) in enumerate(partials):
            phase[k] += 2 * math.pi * f * ratio / RATE
            value += math.sin(phase[k]) * level * math.exp(-t * decay * (1 + 0.6 * k))
        attack = min(1.0, t / 0.004)
        out.append(value * attack)
    return out


def noise(seconds, cutoff_from, cutoff_to, shape):
    out = []
    low = 0.0
    n = int(RATE * seconds)
    for i in range(n):
        t = i / n
        cutoff = cutoff_from + (cutoff_to - cutoff_from) * t
        k = 1 - math.exp(-2 * math.pi * cutoff / RATE)
        low += (random.uniform(-1, 1) - low) * k
        out.append(low * shape(t))
    return out


# A tag: a wooden pop, and a bright note right after it
tag = silence(0.42)
add(tag, 0, tone(520, 0.12, 38, ((1, 1.0), (2.4, 0.4)), bend=0.9))
add(tag, 0.03, [s * 0.8 for s in tone(1320, 0.36, 11, ((1, 1.0), (2, 0.35), (3, 0.12)))])
add(tag, 0.09, [s * 0.5 for s in tone(1760, 0.3, 13, ((1, 1.0), (2, 0.3)))])
write('tag.wav', tag, 0.75)

# A dash: air, rising and gone
write('dash.wav', noise(0.26, 500, 4200, lambda t: math.sin(math.pi * t) ** 1.5), 0.5)

# The school bell: three strikes of a small bell, whose partials aren't a chord
bell = silence(2.6)
parts = ((1, 1.0), (2.0, 0.55), (2.76, 0.45), (5.4, 0.25), (8.93, 0.12))
for strike in range(3):
    add(bell, strike * 0.34, [s * (1.0 - strike * 0.12) for s in tone(1046, 1.9, 2.6, parts)])
write('bell.wav', bell, 0.7)

# You're it: two notes down
it = silence(0.5)
add(it, 0, tone(659, 0.22, 9, ((1, 1.0), (2, 0.5), (3, 0.25))))
add(it, 0.17, tone(494, 0.33, 7, ((1, 1.0), (2, 0.5), (3, 0.25))))
write('it.wav', it, 0.6)

# A round won: four notes up
win = silence(1.3)
for k, freq in enumerate((523, 659, 784, 1046)):
    add(win, k * 0.11, tone(freq, 0.9, 5.5, ((1, 1.0), (2, 0.4), (4, 0.12))))
write('win.wav', win, 0.65)

# Into the puddle: a slap of water, and two bubbles
splash = silence(0.4)
add(splash, 0, noise(0.3, 5200, 700, lambda t: (1 - t) ** 2.2))
add(splash, 0.08, [s * 0.5 for s in tone(700, 0.08, 30, bend=-0.5)])
add(splash, 0.16, [s * 0.4 for s in tone(940, 0.07, 34, bend=-0.5)])
write('splash.wav', splash, 0.6)

# The yard's air, to loop: a little wind, and birds that answer each other. No
# call crosses the loop's end, and the wind is the same noise all the way.
SECONDS = 12
birds = silence(SECONDS)
low = 0.0
slow = 0.0
for i in range(len(birds)):
    low += (random.uniform(-1, 1) - low) * 0.02
    slow += (random.uniform(-1, 1) - slow) * 0.0004
    birds[i] = low * (0.16 + slow * 3)
# Fade the wind into itself across the loop's seam
seam = int(RATE * 0.4)
for i in range(seam):
    w = i / seam
    birds[i] = birds[i] * w + birds[len(birds) - seam + i] * (1 - w)
del birds[len(birds) - seam:]
length = len(birds) / RATE


def chirp(base, sweep, seconds, level):
    out = []
    phase = 0.0
    n = int(RATE * seconds)
    for i in range(n):
        t = i / n
        freq = base * (1 + sweep * math.sin(math.pi * t))
        phase += 2 * math.pi * freq / RATE
        out.append(math.sin(phase) * math.sin(math.pi * t) ** 2 * level)
    return out


at = 0.5
while at < length - 1.2:
    kind = random.randrange(3)
    if kind == 0:   # Three quick notes
        base = random.uniform(3000, 4200)
        for k in range(random.randrange(2, 5)):
            add(birds, at + k * 0.09, chirp(base * (1 + 0.06 * k), 0.18, 0.06, 0.28))
    elif kind == 1:  # A long falling whistle
        add(birds, at, chirp(random.uniform(2400, 3000), -0.22, 0.24, 0.24))
    else:            # A trill, further off
        base = random.uniform(4400, 5200)
        for k in range(random.randrange(5, 9)):
            add(birds, at + k * 0.045, chirp(base, 0.1, 0.035, 0.12))
    at += random.uniform(0.7, 1.9)
write('birds.wav', birds, 0.5)

for name in sorted(os.listdir(folder)):
    print(name, os.path.getsize(os.path.join(folder, name)))
