"""Original synthetic cues. Python standard library; no sampled recordings."""
from pathlib import Path
import math
import struct
import wave

RATE = 22050
ROOT = Path(__file__).parent

def write(name, seconds, sample):
    values = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        value = max(-0.8, min(0.8, sample(t, seconds)))
        values.append(struct.pack('<h', round(value * 32767)))
    with wave.open(str(ROOT / f'{name}.wav'), 'wb') as output:
        output.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
        output.writeframes(b''.join(values))

def sine(t, frequency):
    return math.sin(t * frequency * math.tau)

write('jump', .25, lambda t,d: .3 * (1-t/d)**2 * sine(t, 450+2200*t))
write('switch', .16, lambda t,d: .24 * (1-t/d)**2 * (sine(t, 180)+.4*sine(t, 900)))
write('spray', .30, lambda t,d: .19 * (1-t/d) * sine(t, 1700+1000*sine(t, 90)))
write('flash', .22, lambda t,d: .24 * (1-t/d)**2 * sine(t, 1100-2400*t))
write('caught', .55, lambda t,d: .23 * (1-t/d) * (sine(t, 330-280*t)+.3*sine(t, 90)))
write('unlock', .6, lambda t,d: .23 * (1-t/d) * sine(t, [440,550,660][min(2,int(t*5))]))
write('win', 1.8, lambda t,d: .23 * (1-t/d) * sine(t, [330,440,550,660,880,660][min(5,int(t*4))]))
# Integer-frequency oscillators meet at the loop seam. Quiet factory hum.
write('warehouse', 2, lambda t,d: .08*sine(t,60)+.03*sine(t,120)+.02*sine(t,240)*(0.6+0.4*sine(t,1)))
