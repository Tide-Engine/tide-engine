# Makes Two Forks' sounds from nothing but arithmetic: no samples, no recordings.
#   python make_sounds.py <folder>
import math, os, random, struct, sys, wave

RATE = 22050
folder = sys.argv[1]
os.makedirs(folder, exist_ok=True)
random.seed(11)


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


def add(into, at, samples, level=1.0):
    start = int(at * RATE)
    for i, s in enumerate(samples):
        if start + i < len(into):
            into[start + i] += s * level


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


# Noise between two pitches: what's under `high` less what's under `low`.
def hiss(seconds, low, high, shape):
    out = []
    under_high = 0.0
    under_low = 0.0
    n = int(RATE * seconds)
    k_high = 1 - math.exp(-2 * math.pi * high / RATE)
    k_low = 1 - math.exp(-2 * math.pi * low / RATE)
    for i in range(n):
        white = random.uniform(-1, 1)
        under_high += (white - under_high) * k_high
        under_low += (white - under_low) * k_low
        out.append((under_high - under_low) * shape(i / n))
    return out


# Fat spitting in a pan: clicks, fewer as it goes on.
def crackle(seconds, rate_from, rate_to):
    out = silence(seconds)
    t = 0.0
    while t < seconds:
        rate = rate_from + (rate_to - rate_from) * (t / seconds)
        t += random.expovariate(rate)
        level = random.uniform(0.3, 1.0) * (1 - 0.6 * t / seconds)
        add(out, t, hiss(random.uniform(0.004, 0.012), 1500, 7000, lambda u: 1 - u), level)
    return out


# A step on grass: a soft thud
write('step.wav', hiss(0.085, 60, 900, lambda t: (1 - t) ** 2 * min(1, t * 40)), 0.5)

# A sausage pushed: a thump with a rustle after it
push = silence(0.2)
add(push, 0, tone(120, 0.12, 32, ((1, 1.0), (2.1, 0.3)), bend=0.5))
add(push, 0.01, hiss(0.16, 300, 2400, lambda t: (1 - t) ** 2), 0.5)
write('push.wav', push, 0.6)

# A fork that meets what it can't move: wood, knocked
bump = silence(0.16)
add(bump, 0, tone(210, 0.1, 55, ((1, 1.0), (2.7, 0.5), (5.2, 0.2)), bend=0.3))
add(bump, 0, hiss(0.02, 800, 5000, lambda t: 1 - t), 0.5)
write('bump.wav', bump, 0.55)

# A side goes on the grill: a hiss that dies down, and fat spitting
sizzle = silence(1.0)
add(sizzle, 0, hiss(1.0, 2500, 9500, lambda t: (1 - t) ** 1.6 * min(1, t * 60)))
add(sizzle, 0, crackle(0.9, 60, 12), 0.8)
write('sizzle.wav', sizzle, 0.6)

# A side goes on the grill twice: a harsher, longer hiss, and a note going down
burn = silence(1.4)
add(burn, 0, hiss(1.4, 900, 8000, lambda t: (1 - t) ** 1.1 * min(1, t * 80)))
add(burn, 0, crackle(1.2, 110, 20), 1.0)
add(burn, 0.02, tone(330, 0.5, 5, ((1, 1.0), (2, 0.4)), bend=0.0), 0.35)
add(burn, 0.2, tone(247, 0.7, 4, ((1, 1.0), (2, 0.4))), 0.35)
write('burn.wav', burn, 0.7)

# A sausage in the sea: a plop, and water coming down after it
splash = silence(0.8)
add(splash, 0, tone(190, 0.18, 16, ((1, 1.0), (1.6, 0.4)), bend=1.2))
add(splash, 0.03, hiss(0.75, 400, 5200, lambda t: (1 - t) ** 2.2 * min(1, t * 30)), 0.9)
write('splash.wav', splash, 0.65)

# The gas lights: a whoomp, and it burns
light = silence(0.6)
add(light, 0, tone(70, 0.3, 9, ((1, 1.0), (2, 0.5)), bend=0.8))
add(light, 0, hiss(0.6, 200, 3000, lambda t: math.sin(math.pi * min(1, t * 1.6)) * (1 - t)), 0.7)
write('light.wav', light, 0.6)

# The gas is cut: a click, and the flame goes
cut = silence(0.3)
add(cut, 0, tone(900, 0.03, 120, ((1, 1.0), (2.3, 0.6))))
add(cut, 0.01, hiss(0.28, 500, 4000, lambda t: (1 - t) ** 3), 0.6)
write('cut.wav', cut, 0.5)

# A move taken back: two notes, down
undo = silence(0.2)
add(undo, 0, tone(660, 0.08, 30, ((1, 1.0), (2, 0.3))))
add(undo, 0.06, tone(495, 0.12, 24, ((1, 1.0), (2, 0.3))))
write('undo.wav', undo, 0.4)

# Everything's cooked: a chord climbed a note at a time, on small bells, and rung together
served = silence(2.2)
parts = ((1, 1.0), (2.0, 0.5), (3.0, 0.22), (4.2, 0.1))
for i, freq in enumerate((523.25, 659.25, 783.99, 1046.5)):
    add(served, i * 0.11, tone(freq, 1.2, 4.5, parts), 0.8)
for freq in (523.25, 659.25, 783.99, 1046.5, 1318.5):
    add(served, 0.5, tone(freq, 1.6, 3.2, parts), 0.5)
write('served.wav', served, 0.7)

# The sea, which goes on: waves that come in and go back out, with no seam where it starts over
length = int(RATE * 7.0)
fade = int(RATE * 0.8)
raw = hiss((length + fade) / RATE, 120, 2200, lambda t: 1.0)
waves = []
for i in range(length + fade):
    t = (i % length) / length
    swell = 0.42 + 0.58 * (0.5 - 0.5 * math.cos(2 * math.pi * t * 2)) ** 1.6
    waves.append(raw[i] * swell)
sea = []
for i in range(length):
    if i < fade:
        mix = i / fade
        sea.append(waves[i] * mix + waves[length + i] * (1 - mix))
    else:
        sea.append(waves[i])
write('sea.wav', sea, 0.5)
