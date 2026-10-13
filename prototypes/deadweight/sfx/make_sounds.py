"""Original synthesized cues, with no recorded or licensed material."""
import math
from pathlib import Path
import struct
import wave

RATE = 22050
ROOT = Path(__file__).resolve().parent


def save(name, seconds, sample):
    data = bytearray()
    for i in range(round(seconds * RATE)):
        value = max(-1, min(1, sample(i / RATE, i)))
        data.extend(struct.pack('<h', round(value * 32767)))
    with wave.open(str(ROOT / name), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(data)


save('winch.wav', 1, lambda t, _: 0.22 * math.sin(math.tau * 90 * t)
     + 0.08 * math.sin(math.tau * 270 * t) * (0.6 + 0.4 * math.sin(math.tau * 12 * t)))
save('impact.wav', 0.4, lambda t, i: math.exp(-t * 16) *
     (0.33 * math.sin(math.tau * 110 * t) + 0.17 * math.sin(math.tau * 311 * t)
      + 0.09 * ((i * 7919 % 997) / 498.5 - 1)))
save('secured.wav', 0.7, lambda t, _: 0.22 * math.sin(math.tau * (330 if t < 0.18 else 440 if t < 0.36 else 550) * t)
     * min(1, t * 35) * min(1, (0.7 - t) * 12))
