"""Original arithmetic-only sounds; no recordings or third-party samples."""
from pathlib import Path
import math
import struct
import wave

ROOT = Path(__file__).resolve().parent
RATE = 24000

def write(name, duration, notes, gain=.22):
    data = []
    for i in range(int(duration * RATE)):
        t = i / RATE
        value = 0
        for start, frequency, length in notes:
            age = t - start
            if 0 <= age < length:
                envelope = min(1, age / .008) * math.exp(-age * 6 / length) * min(1, (length - age) / .035)
                value += envelope * (math.sin(2 * math.pi * frequency * age) + .15 * math.sin(4 * math.pi * frequency * age))
        data.append(struct.pack('<h', round(max(-.95, min(.95, value * gain)) * 32767)))
    with wave.open(str(ROOT / (name + '.wav')), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(b''.join(data))

write('step', .12, [(0, 180, .1)], .11)
write('switch', .28, [(0, 440, .18), (.07, 660, .18)])
write('blocked', .16, [(0, 130, .14)], .14)
write('power', 1.4, [(0, 261.63, .9), (.14, 329.63, .9), (.28, 392, .9), (.42, 523.25, .9)], .18)
write('complete', 2.3, [(0, 392, .7), (.18, 523.25, .8), (.36, 659.25, .9), (.62, 783.99, 1.4)], .19)
write('reset', .25, [(0, 330, .2), (.07, 220, .16)], .15)
