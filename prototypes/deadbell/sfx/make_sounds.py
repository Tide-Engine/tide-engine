"""Original synthesised sounds; run from any directory to reproduce the WAVs."""
from pathlib import Path
import math
import random
import struct
import wave

RATE = 22050
DEST = Path(__file__).parent
RNG = random.Random(20261009)


def write(name, data, peak):
    gain = peak / max(0.00001, max(abs(x) for x in data))
    with wave.open(str(DEST / (name + ".wav")), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", round(max(-1, min(1, x * gain)) * 32767)) for x in data))


def tone(hz, seconds, decay, metal=False):
    ratios = [(1, 1), (2.76, .32), (5.4, .18), (8.93, .07)] if metal else [(1, 1), (2, .22), (3, .09)]
    return [sum(math.sin(math.tau * hz * r * i / RATE) * a * math.exp(-i / RATE * decay * (1 + j * .2))
                for j, (r, a) in enumerate(ratios)) * min(1, i / (RATE * .006))
            for i in range(int(seconds * RATE))]


def noise(seconds, fade, smooth=.3):
    low, data = 0, []
    for i in range(int(seconds * RATE)):
        low += (RNG.uniform(-1, 1) - low) * smooth
        data.append(low * math.exp(-i / RATE * fade) * min(1, i / (RATE * .002)))
    return data


def add(into, source, at=0, volume=1):
    start = int(at * RATE)
    for i, value in enumerate(source):
        if start + i < len(into):
            into[start + i] += value * volume


shot = noise(.24, 28)
add(shot, tone(95, .2, 25), volume=.65)
write("shot", shot, .57)
heavy = noise(.6, 10, .13)
add(heavy, tone(58, .6, 8), volume=1.6)
add(heavy, tone(311, .5, 10, True), volume=.15)
write("heavy", heavy, .68)
bell = tone(196, 3.6, 1.2, True)
add(bell, tone(293.66, 2.8, 1.5, True), at=.23, volume=.3)
write("bell", bell, .55)
rupture = noise(.8, 7, .11)
add(rupture, tone(49, .8, 5), volume=2)
add(rupture, tone(392, .7, 7, True), volume=.35)
write("rupture", rupture, .75)
rescue = [0.0] * int(1.7 * RATE)
for i, hz in enumerate((196, 246.94, 293.66, 392)):
    add(rescue, tone(hz, 1.1, 4, True), i * .15, .6)
write("rescue", rescue, .6)
win = [0.0] * int(5 * RATE)
for i, hz in enumerate((98, 146.83, 196, 246.94, 293.66, 392)):
    add(win, tone(hz, 3.4, .9, True), i * .22, .45)
write("win", win, .65)

# A long, seamlessly repeating original minor-key bed. Crowds and rituals
# alter its volume in game; percussion is sparse enough to leave space for
# combat tells and bell notes.
length = 32
music = [0.0] * int(length * RATE)
for i in range(len(music)):
    t = i / RATE
    # Integer cycles per loop ensure the drone crosses its seam continuously.
    music[i] = (.12 * math.sin(math.tau * 1568 * t / length)
                + .08 * math.sin(math.tau * 2352 * t / length)
                + .035 * math.sin(math.tau * 3136 * t / length)) * (.65 + .15 * math.sin(math.tau * t / 16))
for bar, root in enumerate((98, 87.307, 73.416, 82.407, 98, 110, 73.416, 87.307)):
    for beat in range(8):
        at = bar * 4 + beat * .5
        add(music, tone(root if beat < 4 else root * 1.5, 1.2, 3.5), at, .19)
        if beat % 2 == 0:
            add(music, noise(.22, 18, .06), at, .17)
        if beat in (3, 7):
            add(music, tone(root * 4, .55, 6, True), at, .10)
write("procession", music, .38)
print("Generated seven original mono PCM clips.")
