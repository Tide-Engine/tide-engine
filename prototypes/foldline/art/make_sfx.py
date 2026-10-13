# make_sfx.py <folder>: FOLDLINE's sounds, made from nothing: 22,050 Hz, mono, 16 bits, as an old console's were.
import sys, os, wave
import numpy as np

RATE = 22050
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rng = np.random.default_rng(1986)

def t(seconds):
    return np.arange(int(RATE * seconds)) / RATE

def noise(seconds):
    return rng.uniform(-1, 1, int(RATE * seconds))

def low(x, cutoff):
    # One pole, run twice: what's above the cutoff goes
    a = 1 - np.exp(-2 * np.pi * cutoff / RATE)
    y = np.empty_like(x)
    for _ in range(2):
        s = 0.0
        for i in range(len(x)):
            s += (x[i] - s) * a
            y[i] = s
        x = y.copy()
    return y

def high(x, cutoff):
    return x - low(x, cutoff)

def band(x, lo, hi):
    return high(low(x, hi), lo)

def decay(seconds, rate):
    return np.exp(-t(seconds) * rate)

def loop(x, seconds=0.25):
    # Its end faded into its start, so it goes round without a click
    n = int(RATE * seconds)
    fade = np.linspace(0, 1, n)
    body = x[:-n].copy()
    body[:n] = body[:n] * fade + x[-n:] * (1 - fade)
    return body

def crush(x, steps=64):
    return np.round(x * steps) / steps

def save(name, x, peak=0.8):
    x = np.asarray(x, dtype=np.float64)
    x = x / max(np.max(np.abs(x)), 1e-9) * peak
    with wave.open(os.path.join(out, name + '.wav'), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((x * 32767).astype('<i2').tobytes())

def sweep(seconds, start, end, curve=1.0):
    x = t(seconds) / seconds
    freq = start + (end - start) * x ** curve
    return np.sin(2 * np.pi * np.cumsum(freq) / RATE)

# Steps on carpet: a dull thump and a scuff
for i in range(4):
    n = 0.16
    thump = np.sin(2 * np.pi * (78 + i * 7) * t(n)) * decay(n, 34)
    scuff = low(noise(n), 900 + i * 150) * decay(n, 26) * 0.8
    save(f'step{i + 1}', thump + scuff, 0.6)

# A fold's end going onto a wall: a falling note that opens out
n = 0.55
zap = sweep(n, 1500, 180, 0.4) * decay(n, 5)
ring = np.sin(2 * np.pi * 220 * t(n)) * np.sin(2 * np.pi * 7 * t(n)) * (1 - decay(n, 9)) * decay(n, 4) * 0.5
save('open', crush(zap + ring + band(noise(n), 2000, 6000) * decay(n, 18) * 0.3), 0.7)

# One that didn't take: a crackle, and a note that drops away
n = 0.35
gate = (rng.uniform(0, 1, int(RATE * n)) > 0.82).astype(float)
crackle = high(noise(n), 2500) * gate * decay(n, 9)
save('fizzle', crush(crackle + sweep(n, 520, 90, 0.6) * decay(n, 10) * 0.6, 32), 0.6)

# Through a fold: a breath of air, over before it's noticed
n = 0.13
save('cross', low(noise(n), 700) * np.sin(np.pi * t(n) / n) ** 2 + np.sin(2 * np.pi * 95 * t(n)) * decay(n, 30) * 0.5, 0.4)

# A plate going down and coming up
n = 0.3
clunk = low(noise(n), 500) * decay(n, 40)
save('plate_on', clunk + (np.sin(2 * np.pi * 330 * t(n)) + np.sin(2 * np.pi * 495 * t(n)) * 0.5) * decay(n, 9) * 0.6, 0.6)
save('plate_off', clunk + (np.sin(2 * np.pi * 247 * t(n)) + np.sin(2 * np.pi * 185 * t(n)) * 0.5) * decay(n, 11) * 0.6, 0.5)

# The gate: a shutter's rumble and its ratchet, round and round
n = 1.25
rumble = low(noise(n), 140) * 2 + np.sign(np.sin(2 * np.pi * 32 * t(n))) * 0.25
ratchet = np.zeros(int(RATE * n))
for k in range(10):
    at = int(k * RATE * 0.1)
    m = min(int(RATE * 0.03), len(ratchet) - at)
    ratchet[at:at + m] += high(noise(0.03), 1800)[:m] * decay(0.03, 160)[:m]
save('gate', loop(low(rumble, 600) + ratchet * 0.7), 0.6)

# The cube: taken up, thrown, and landing
n = 0.14
save('pickup', sweep(n, 300, 620) * decay(n, 14) + low(noise(n), 1200) * decay(n, 40) * 0.4, 0.5)
n = 0.3
save('throw', band(noise(n), 500, 3000) * np.sin(np.pi * t(n) / n) ** 3, 0.55)
n = 0.4
save('thud', np.sin(2 * np.pi * 62 * t(n)) * decay(n, 14) + low(noise(n), 700) * decay(n, 45) + np.sin(2 * np.pi * 410 * t(n)) * decay(n, 30) * 0.15, 0.75)

# The hands: wet slaps, and something low coming up with them
n = 1.9
growl = np.zeros(int(RATE * n))
for f, a in ((49, 1.0), (51.5, 0.8), (98, 0.5), (147, 0.3), (196.5, 0.2)):
    growl += np.sign(np.sin(2 * np.pi * f * t(n) + np.sin(2 * np.pi * 5 * t(n)) * 0.8)) * a
growl = low(growl, 500) * (0.6 + 0.4 * np.sin(2 * np.pi * 11 * t(n))) * np.minimum(t(n) * 6, 1) * np.minimum((n - t(n)) * 2, 1)
slaps = np.zeros(int(RATE * n))
for k in range(9):
    at = int(rng.uniform(0, 0.7) * RATE)
    m = int(RATE * 0.09)
    slaps[at:at + m] += low(noise(0.09), 1600) * decay(0.09, 38) * rng.uniform(0.5, 1)
rise = sweep(n, 180, 620, 2) * (t(n) / n) ** 2 * 0.25
save('grab', growl * 0.8 + slaps + rise, 0.85)

# Under the red: bubbles
n = 2.5
bubbles = np.zeros(int(RATE * n))
for k in range(46):
    at = int(rng.uniform(0, n - 0.12) * RATE)
    d = rng.uniform(0.04, 0.11)
    m = int(RATE * d)
    f = rng.uniform(110, 420)
    bubbles[at:at + m] += sweep(d, f, f * 1.9)[:m] * np.sin(np.pi * np.arange(m) / m) * rng.uniform(0.3, 1)
save('under', loop(low(bubbles, 900) + low(noise(n), 160) * 1.4), 0.6)

# The pit, heard from its edge: something thick turning over, and breath that isn't anyone's
n = 6.0
slow = low(noise(n), 90) * 3 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.5 * t(n)))
breath = band(noise(n), 350, 900) * np.maximum(np.sin(2 * np.pi * (1 / 3) * t(n)), 0) ** 3 * 0.45
plops = np.zeros(int(RATE * n))
for k in range(14):
    at = int(rng.uniform(0, n - 0.2) * RATE)
    d = rng.uniform(0.06, 0.16)
    m = int(RATE * d)
    f = rng.uniform(70, 190)
    plops[at:at + m] += sweep(d, f, f * 1.7)[:m] * np.sin(np.pi * np.arange(m) / m) * rng.uniform(0.3, 0.9)
save('pit', loop(slow + breath + low(plops, 600), 0.5), 0.6)

# The ceiling's lamps: mains hum, round and round, and one failing
n = 2.25
hum = sum(np.sin(2 * np.pi * f * t(n)) * a for f, a in ((60, 1), (120, 0.6), (180, 0.3), (240, 0.12), (360, 0.06)))
save('hum', loop(hum + high(noise(n), 4000) * 0.05), 0.5)
n = 0.4
buzz = np.sign(np.sin(2 * np.pi * 120 * t(n))) * (rng.uniform(0, 1, int(RATE * n)) > 0.35)
save('buzz', high(buzz, 900) * decay(n, 5) + high(noise(n), 3000) * decay(n, 20) * 0.5, 0.5)

# The building: three low notes that never agree
n = 8.25
drone = sum(np.sin(2 * np.pi * f * t(n) + np.sin(2 * np.pi * r * t(n)) * 0.6) * a for f, a, r in ((41, 1, 0.125), (43.5, 0.8, 0.25), (61.5, 0.5, 0.375), (123.5, 0.12, 0.125)))
save('drone', loop(drone + low(noise(n), 70) * 1.5, 0.25), 0.6)

# An open fold: a fifth that beats
n = 2.25
save('fold', loop(sum(np.sin(2 * np.pi * f * t(n)) * a for f, a in ((110, 1), (111, 0.7), (165, 0.5), (166.5, 0.4), (330, 0.1)))), 0.5)

# Seen: four notes that shouldn't be together, struck
n = 1.6
sting = sum(np.sin(2 * np.pi * f * t(n)) for f in (220, 233.1, 311.1, 466.2, 493.9))
save('sting', crush(sting * decay(n, 2.4) + high(noise(n), 2000) * decay(n, 30) * 2 + np.sin(2 * np.pi * 55 * t(n)) * decay(n, 3) * 2, 48), 0.85)

# The menu: a tick, and a heavier one
n = 0.05
save('tick', high(noise(n), 1500) * decay(n, 120) + np.sin(2 * np.pi * 880 * t(n)) * decay(n, 90) * 0.5, 0.4)
n = 0.22
save('accept', np.sin(2 * np.pi * 165 * t(n)) * decay(n, 14) + low(noise(n), 800) * decay(n, 50), 0.6)
print(sorted(os.listdir(out)))
