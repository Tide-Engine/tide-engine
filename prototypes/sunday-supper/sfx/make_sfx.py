# make_sfx.py <folder>: SUNDAY SUPPER's sounds, made from nothing but noise and
# tones: 22,050 Hz, mono, 16 bits. Run it with the folder to write them again.
import sys, os, wave
import numpy as np

RATE = 22050
out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
os.makedirs(out, exist_ok=True)
rng = np.random.default_rng(1998)


def t(seconds):
    return np.arange(int(RATE * seconds)) / RATE


def noise(seconds):
    return rng.uniform(-1, 1, int(RATE * seconds))


def low(x, cutoff):
    # One pole, run twice: what's above the cutoff goes
    a = 1 - np.exp(-2 * np.pi * cutoff / RATE)
    x = np.asarray(x, dtype=np.float64)
    for _ in range(2):
        y = np.empty_like(x)
        s = 0.0
        for i in range(len(x)):
            s += (x[i] - s) * a
            y[i] = s
        x = y
    return x


def high(x, cutoff):
    return x - low(x, cutoff)


def band(x, lo, hi):
    return high(low(x, hi), lo)


def decay(seconds, rate):
    return np.exp(-t(seconds) * rate)


def pad(x, seconds):
    n = int(RATE * seconds)
    return np.concatenate([x, np.zeros(max(0, n - len(x)))])[:max(n, len(x))]


def place(into, x, at):
    i = int(at * RATE)
    end = min(len(into), i + len(x))
    into[i:end] += x[:end - i]
    return into


def loop(x, seconds=0.4):
    # Its end faded into its start, so it goes round without a click
    n = int(RATE * seconds)
    fade = np.linspace(0, 1, n)
    body = x[:-n].copy()
    body[:n] = body[:n] * fade + x[-n:] * (1 - fade)
    return body


def save(name, x, peak=0.8):
    x = np.asarray(x, dtype=np.float64)
    x = x / max(np.max(np.abs(x)), 1e-9) * peak
    with wave.open(os.path.join(out, name + '.wav'), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes((x * 32767).astype('<i2').tobytes())


def tone(seconds, freq):
    return np.sin(2 * np.pi * freq * t(seconds))


def sweep(seconds, start, end, curve=1.0):
    x = t(seconds) / seconds
    freq = start + (end - start) * x ** curve
    return np.sin(2 * np.pi * np.cumsum(freq) / RATE)


def thump(seconds, freq, rate):
    # A knock on wood: a low body that dies fast, and a click of noise on top
    body = sweep(seconds, freq * 1.6, freq, 0.3) * decay(seconds, rate)
    click = band(noise(seconds), 600, 3000) * decay(seconds, rate * 4)
    return body + click * 0.6


# A knock: three on a wooden door
knock = np.zeros(int(RATE * 1.2))
for i, at in enumerate([0.0, 0.28, 0.52]):
    place(knock, thump(0.18, 110 - i * 6, 28), at)
save('knock', knock, 0.85)

# A key in a lock: a scrape and a heavy click
lock = np.zeros(int(RATE * 0.7))
place(lock, band(noise(0.25), 1500, 5000) * np.linspace(0.2, 1, int(RATE * 0.25)) * 0.4, 0.0)
place(lock, thump(0.12, 380, 60) * 0.9, 0.3)
place(lock, band(noise(0.05), 2000, 7000) * decay(0.05, 80), 0.31)
save('lock', lock, 0.7)
unlock = np.zeros(int(RATE * 0.6))
place(unlock, band(noise(0.18), 1500, 5000) * 0.35, 0.0)
place(unlock, thump(0.1, 450, 70), 0.2)
save('unlock', unlock, 0.6)

# A door opening: hinges that creak, a little rising
creak_len = 1.1
x = t(creak_len)
wobble = 1 + 0.25 * np.sin(2 * np.pi * 7 * x) + 0.1 * np.sin(2 * np.pi * 13 * x)
creak = sweep(creak_len, 340, 520, 0.7) * wobble
creak = band(np.sign(creak) * np.abs(creak) ** 0.4 * (0.6 + 0.4 * noise(creak_len)), 200, 2500)
envelope = np.minimum(1, x * 8) * np.minimum(1, (creak_len - x) * 4)
save('door_open', creak * envelope, 0.55)
# ...and shut: a soft thud and the latch
shut = np.zeros(int(RATE * 0.6))
place(shut, thump(0.35, 70, 14), 0)
place(shut, thump(0.06, 900, 120) * 0.4, 0.05)
save('door_shut', shut, 0.85)

# The doorknob tried: a rattle of metal, twice
knob = np.zeros(int(RATE * 1.0))
for at in [0.0, 0.12, 0.42, 0.5, 0.58]:
    place(knob, band(noise(0.07), 1200, 6000) * decay(0.07, 50), at)
    place(knob, thump(0.05, 520, 90) * 0.5, at)
save('knob', knob, 0.6)

# A floorboard giving under a foot
board_len = 0.6
x = t(board_len)
board = sweep(board_len, 160, 110) * (0.7 + 0.3 * noise(board_len))
board = band(board, 80, 900) * np.minimum(1, x * 20) * np.exp(-x * 5)
save('creak', board, 0.6)

# Footsteps along the upstairs hall: slow, heavy, someone who doesn't want to be heard
steps = np.zeros(int(RATE * 3.0))
for i in range(5):
    at = 0.15 + i * 0.58
    place(steps, thump(0.2, 85, 22) * (0.7 + 0.1 * i), at)
    if i % 2 == 1:
        place(steps, board[: int(RATE * 0.3)] * 0.5, at + 0.04)
save('steps', steps, 0.7)

# The phone: the receiver lifted, and set down
pickup = np.zeros(int(RATE * 0.5))
place(pickup, thump(0.08, 600, 80), 0)
place(pickup, band(noise(0.15), 300, 1500) * decay(0.15, 20) * 0.3, 0.05)
save('pickup', pickup, 0.6)
hangup = np.zeros(int(RATE * 0.5))
place(hangup, thump(0.1, 300, 50), 0)
place(hangup, tone(0.06, 1800) * decay(0.06, 60) * 0.2, 0.02)
save('hangup', hangup, 0.6)

# The cuckoo clock striking nine: a bellows' two notes, nine times
chime = np.zeros(int(RATE * 7.5))
for i in range(9):
    at = i * 0.8
    for f, d in ((660, 0.0), (523, 0.22)):
        note = (tone(0.3, f) + 0.4 * tone(0.3, f * 2.01)) * decay(0.3, 9)
        place(chime, band(note + noise(0.3) * 0.08, 200, 4000), at + d)
save('chime', chime, 0.5)

# Cutlery on a plate
clink = np.zeros(int(RATE * 1.5))
for at, f in ((0.0, 3100), (0.35, 2700), (0.8, 3500), (1.1, 2900)):
    place(clink, (tone(0.25, f) + 0.5 * tone(0.25, f * 1.47)) * decay(0.25, 22), at)
save('clink', clink, 0.35)

# A sack dropped into a wooden bin
thud = np.zeros(int(RATE * 0.8))
place(thud, thump(0.5, 55, 9), 0)
place(thud, band(noise(0.4), 200, 2000) * decay(0.4, 10) * 0.4, 0)
save('thud', thud, 0.9)

# Corn leaves pushed aside, or a tarp dragged off
rustle_len = 1.4
x = t(rustle_len)
rustle = band(noise(rustle_len), 1500, 7000) * (0.4 + 0.6 * np.abs(np.sin(2 * np.pi * 3.3 * x))) * np.minimum(1, x * 6) * np.exp(-x * 1.5)
save('rustle', rustle, 0.5)

# A dog, far off: two short barks
dog = np.zeros(int(RATE * 1.6))
for at in (0.0, 0.45):
    bark_len = 0.22
    y = t(bark_len)
    bark = sweep(bark_len, 420, 300) * (1 + 0.6 * np.sin(2 * np.pi * 900 * y))
    bark = band(bark * np.minimum(1, y * 40) * np.exp(-y * 14) + noise(bark_len) * 0.15 * np.exp(-y * 20), 250, 2200)
    place(dog, bark, at)
save('dog', low(dog, 1400), 0.45)

# Coyotes: a rising howl, and yips under it
coyote = np.zeros(int(RATE * 4.0))
howl_len = 2.6
y = t(howl_len)
freq = 520 + 380 * np.sin(np.pi * y / howl_len) ** 0.7 + 30 * np.sin(2 * np.pi * 5.5 * y)
howl = np.sin(2 * np.pi * np.cumsum(freq) / RATE) * np.sin(np.pi * y / howl_len) ** 1.5
place(coyote, howl, 0.2)
for i in range(6):
    yip_len = 0.12
    yy = t(yip_len)
    yip = sweep(yip_len, 900, 1400) * np.sin(np.pi * yy / yip_len)
    place(coyote, yip * 0.5, 2.3 + i * 0.22 + rng.uniform(0, 0.08))
save('coyote', low(coyote, 2600), 0.4)

# A rooster at first light
rooster = np.zeros(int(RATE * 2.2))
for at, length, lo, hi in ((0.0, 0.25, 500, 700), (0.3, 0.2, 650, 800), (0.55, 1.1, 820, 640)):
    y = t(length)
    call = sweep(length, lo, hi) * (1 + 0.5 * np.sin(2 * np.pi * 45 * y))
    call = np.tanh(call * 2) * np.sin(np.pi * y / length) ** 0.5
    place(rooster, band(call, 300, 3500), at)
save('rooster', rooster, 0.45)

# A sting: something found that shouldn't be there
sting_len = 3.5
y = t(sting_len)
sting = (np.sin(2 * np.pi * 55 * y) + 0.6 * np.sin(2 * np.pi * 58.3 * y) + 0.3 * np.sin(2 * np.pi * 110.7 * y)) * np.exp(-y * 0.9)
sting += band(noise(sting_len), 2000, 6000) * np.exp(-y * 3) * 0.3
sting += sweep(sting_len, 900, 1150) * np.exp(-y * 1.5) * 0.12
save('sting', sting * np.minimum(1, y * 30), 0.8)

# The owl
owl = np.zeros(int(RATE * 1.8))
for at, length in ((0.0, 0.35), (0.5, 0.2), (0.75, 0.6)):
    y = t(length)
    hoot = tone(length, 380) * np.sin(np.pi * y / length) ** 2
    place(owl, hoot + noise(length) * 0.05 * np.sin(np.pi * y / length), at)
save('owl', low(owl, 900), 0.4)

# Footsteps, four of each: gravel, boards, the yard's dirt
for i in range(4):
    length = 0.22
    y = t(length)
    grit = band(noise(length), 700 + i * 120, 5000) * np.exp(-y * 22) * np.minimum(1, y * 300)
    save(f'step_gravel{i}', grit + thump(length, 90, 40) * 0.2, 0.45)
    wood = thump(length, 95 + i * 7, 30) + band(noise(length), 300, 1500) * np.exp(-y * 40) * 0.2
    save(f'step_wood{i}', wood, 0.45)
    soil = band(noise(length), 200 + i * 40, 1400) * np.exp(-y * 26) * np.minimum(1, y * 300) + thump(length, 70, 35) * 0.3
    save(f'step_dirt{i}', soil, 0.4)

# Loops: crickets in the dark, the wind over the fields, the house's own hum, the fire, the clock
cricket_len = 6.0
y = t(cricket_len)
crickets = np.zeros_like(y)
for k in range(9):
    f = 4100 + k * 230
    rate = 2.2 + k * 0.37
    chirp = np.sin(2 * np.pi * f * y) * (np.sin(2 * np.pi * 28 * y + k) > 0.2)
    gate = (np.sin(2 * np.pi * rate * y + k * 1.7) > 0.55).astype(float)
    crickets += chirp * gate * (0.4 + 0.6 * rng.uniform())
crickets = low(crickets, 6000)
save('crickets', loop(crickets), 0.25)

wind_len = 8.0
y = t(wind_len)
gust = 0.5 + 0.35 * np.sin(2 * np.pi * y / 5.5) + 0.15 * np.sin(2 * np.pi * y / 2.3 + 1)
save('wind', loop(band(noise(wind_len), 120, 900) * gust), 0.4)

hum_len = 6.0
y = t(hum_len)
hum = np.sin(2 * np.pi * 60 * y) * 0.4 + np.sin(2 * np.pi * 120 * y) * 0.2 + low(noise(hum_len), 300) * 0.6
save('room', loop(hum), 0.2)

fire_len = 5.0
y = t(fire_len)
fire = low(noise(fire_len), 500) * 0.8
for i in range(80):
    at = rng.uniform(0, fire_len - 0.05)
    place(fire, band(noise(0.03), 1000, 6000) * decay(0.03, 150) * rng.uniform(0.3, 1.0), at)
save('fire', loop(fire), 0.35)

tick = np.zeros(int(RATE * 2.0))
for i in range(2):
    place(tick, thump(0.04, 1900 - i * 300, 160) * 0.6, i * 1.0)
save('tick', tick, 0.25)
print('written to', out)
