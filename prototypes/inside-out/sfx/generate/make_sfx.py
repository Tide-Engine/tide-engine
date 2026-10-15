#!/usr/bin/env python3
"""Inside Out sound effects and interface cues.

Every sound is synthesized here (no samples): mono, 48 kHz (the mixer's own
rate, so playback at pitch 1 resamples nothing), PCM16 WAV, each file
starting and ending on an exact zero. The longest cue, solve, ships as Ogg
Vorbis (its PCM master is masters/solve.wav) so the set stays under 1 MB.
Every random source is a seeded numpy Generator and the Vorbis encode runs
bit-exact, so a run reproduces the files byte for byte. The run also prints
a level report and draws a spectrogram contact sheet.

Levels are set by short-term loudness: every sound's loudest 50 ms,
K-weighted as in ITU-R BS.1770 (mono, -0.691 dB offset), is set to its
target in SOUNDS and its peak must stay under a ceiling. The hierarchy:
solve on top; join, start, dock, leave and seat 2.2-3.2 dB under it;
the pushes, shutters, reset and undo (which a player repeats) under those;
the steps and plates in the middle; the doorway, the bump and the
interface under the steps.

One palette for the whole set, so the cues sound like one tabletop. The
chimes are in F major (A4 = 440 Hz), the music's key, and avoid B and Bb,
which the music's chords hold either way:

  wood     a painted box: damped modes from 140 Hz to 1.1 kHz, struck soft
           (3-5 ms rise)
  floor    plaster under rubber treads: short dry broadband scuffs, no motor chirp
  servo    the robots' little motors: a soft harmonic tone with gear ripple
  voice    the robots' hum ("mm"): a rounded harmonic tone through a low-pass
  brass    latches, plate rims, battery tops: damped modes at 1.2-1.9 kHz
           with faint upper modes, never struck faster than 2.5 ms
  cell     the battery's body: modes from 260 Hz to 1.65 kHz
  mallet   a felt mallet on a celesta bar (4 ms strike)
  air      noise through a swept band-pass, almost nothing above 2 kHz
  room     a convolution with a seeded, exponentially decaying noise
           response, darker as it decays: a small room, and longer settings
           for the chimes

Usage: python make_sfx.py            render the set
       python make_sfx.py --zoom a,b draw those sounds large into zoom.png
"""
import hashlib
import math
import shutil
import struct
import subprocess
import sys
import wave
import zlib
from pathlib import Path

import numpy as np

SR = 48000
OUT = Path(__file__).resolve().parent
MASTERS = OUT / "masters"
TAU = 2.0 * math.pi
LN1000 = math.log(1000.0)
BUDGET = 1_000_000
OGG_QUALITY = 10


# ---------------------------------------------------------------- pitch

def pitch(name):
    """Equal-tempered frequency of a note name such as 'F#4' (A4 = 440 Hz)."""
    steps = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6,
             "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    midi = 12 * (int(name[-1]) + 1) + steps[name[:-1]]
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


# ---------------------------------------------------------------- buffers

def count(seconds):
    return int(round(seconds * SR))


def times(seconds):
    return np.arange(count(seconds)) / SR


def blank(seconds):
    return np.zeros(count(seconds))


def add(buf, sig, at, gain=1.0):
    """Mix sig into buf starting at time at (the end is cut at the buffer)."""
    start = count(at)
    end = min(len(buf), start + len(sig))
    if end > start:
        buf[start:end] += gain * sig[: end - start]
    return buf


def unit(x):
    peak = float(np.max(np.abs(x)))
    return x / peak if peak > 0.0 else x


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


# ---------------------------------------------------------------- envelopes

def fade_in(x, ms):
    m = min(len(x), count(ms / 1000.0))
    if m > 0:
        x[:m] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(m) / m)
    return x


def fade_out(x, ms):
    m = min(len(x), count(ms / 1000.0))
    if m > 0:
        x[len(x) - m:] *= 0.5 + 0.5 * np.cos(np.pi * (np.arange(m) + 1) / m)
    return x


def shape(seconds, points):
    """Envelope through (time, level) points, raised-cosine between them.

    Before the first point it holds the first level, after the last the last.
    """
    t = times(seconds)
    env = np.full(len(t), float(points[0][1]))
    for (t0, v0), (t1, v1) in zip(points, points[1:]):
        inside = (t >= t0) & (t < t1)
        u = (t[inside] - t0) / (t1 - t0)
        env[inside] = v0 + (v1 - v0) * (0.5 - 0.5 * np.cos(np.pi * u))
    env[t >= points[-1][0]] = points[-1][1]
    return env


def glide(seconds, points):
    """Frequency curve through (time, Hz) points, smooth in log frequency."""
    return np.exp(shape(seconds, [(t, math.log(f)) for t, f in points]))


def decay(t, t60):
    return np.exp(-LN1000 * t / t60)


# ---------------------------------------------------------------- filters

def biquad(x, coefficients):
    b0, b1, b2, a1, a2 = coefficients
    out = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x.tolist()):
        y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, y
        out[i] = y
    return np.array(out)


def rbj(kind, fc, q, gain_db=0.0):
    """Biquad coefficients from the Audio EQ Cookbook (R. Bristow-Johnson)."""
    w = TAU * fc / SR
    c, s = math.cos(w), math.sin(w)
    alpha = s / (2.0 * q)
    if kind == "lp":
        b, a = ((1 - c) / 2, 1 - c, (1 - c) / 2), (1 + alpha, -2 * c, 1 - alpha)
    elif kind == "hp":
        b, a = ((1 + c) / 2, -(1 + c), (1 + c) / 2), (1 + alpha, -2 * c, 1 - alpha)
    elif kind == "bp":
        b, a = (alpha, 0.0, -alpha), (1 + alpha, -2 * c, 1 - alpha)
    else:
        big = 10.0 ** (gain_db / 40.0)
        b = (1 + alpha * big, -2 * c, 1 - alpha * big)
        a = (1 + alpha / big, -2 * c, 1 - alpha / big)
    return b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0]


def lowpass(x, fc, q=0.7071):
    return biquad(x, rbj("lp", fc, q))


def lowpass4(x, fc):
    """Fourth-order Butterworth low-pass (24 dB per octave): noise stays dark."""
    return lowpass(lowpass(x, fc, 0.5412), fc, 1.3066)


def highpass(x, fc, q=0.7071):
    return biquad(x, rbj("hp", fc, q))


def bandpass(x, fc, q):
    return biquad(x, rbj("bp", fc, q))


def peaking(x, fc, q, gain_db):
    return biquad(x, rbj("peak", fc, q, gain_db))


def onepole(x, fc):
    k = 1.0 - math.exp(-TAU * fc / SR)
    values = x.tolist()
    y = values[0] if values else 0.0
    out = [0.0] * len(values)
    for i, v in enumerate(values):
        y += k * (v - y)
        out[i] = y
    return np.array(out)


def sweep_band(x, fc, q):
    """Band-pass with unity peak gain whose centre follows the array fc.

    A topology-preserving state-variable filter (A. Simper), stable while its
    frequency moves.
    """
    k = 1.0 / q
    g_all = np.tan(np.pi * np.clip(fc, 20.0, 0.45 * SR) / SR).tolist()
    ic1 = ic2 = 0.0
    out = [0.0] * len(x)
    for i, (v, g) in enumerate(zip(x.tolist(), g_all)):
        a1 = 1.0 / (1.0 + g * (g + k))
        a2 = g * a1
        a3 = g * a2
        v3 = v - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        out[i] = k * v1
    return np.array(out)


def kweight(x):
    """ITU-R BS.1770 K-weighting at SR: the head's high shelf (+4 dB above
    about 1.7 kHz) and the RLB high-pass at 38 Hz, from their analog
    parameters."""
    gain, q, fc = 4.0, 0.7071752369554193, 1681.974450955533
    big = 10.0 ** (gain / 40.0)
    w = TAU * fc / SR
    c, s = math.cos(w), math.sin(w)
    alpha = s / (2.0 * q)
    root = 2.0 * math.sqrt(big) * alpha
    b = (big * ((big + 1) + (big - 1) * c + root), -2 * big * ((big - 1) + (big + 1) * c),
         big * ((big + 1) + (big - 1) * c - root))
    a = ((big + 1) - (big - 1) * c + root, 2 * ((big - 1) - (big + 1) * c), (big + 1) - (big - 1) * c - root)
    shelf = (b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0])
    return biquad(biquad(x, shelf), rbj("hp", 38.13547087613982, 0.5003270373253953))


def loudness(x, ms):
    """Short-term loudness: the loudest window of ms milliseconds (hop an
    eighth of it) of the K-weighted signal, in LUFS-like dB (mono)."""
    k = kweight(np.concatenate([x, np.zeros(count(0.4))]))
    width = count(ms / 1000.0)
    sums = np.concatenate([[0.0], np.cumsum(k * k)])
    starts = np.arange(0, max(1, len(k) - width), max(1, width // 8))
    best = float(np.max(sums[starts + width] - sums[starts])) / width
    return -0.691 + 10.0 * math.log10(best + 1e-30)


# ---------------------------------------------------------------- sources

def white(seconds, rng):
    return 0.3 * rng.standard_normal(count(seconds))


def wander(seconds, rng, rate, smooth, depth):
    """Slow random roughness between 1 - depth and 1 (stick-slip, crinkle)."""
    total = count(seconds)
    hold = max(1, int(round(SR / rate)))
    held = np.repeat(rng.random(total // hold + 2), hold)[:total]
    soft = onepole(onepole(held, smooth), smooth)
    lo, hi = float(soft.min()), float(soft.max())
    soft = (soft - lo) / (hi - lo) if hi > lo else np.ones(total)
    return 1.0 - depth + depth * soft


def modes(seconds, spec, rise_ms, scale=1.0, damp=1.0):
    """Struck object: a sum of exponentially decaying sines.

    spec is (Hz, T60 seconds, amplitude) per mode; scale moves every mode,
    damp lengthens (>1) or shortens every decay. The rise is how soft the
    strike is: 2.5-3 ms a soft tick, 3-5 ms a felt or rubber thunk.
    """
    t = times(seconds)
    out = np.zeros(len(t))
    for hz, t60, amp in spec:
        if hz * scale < 5000.0:
            out += amp * decay(t, t60 * damp) * np.sin(TAU * hz * scale * t)
    return fade_in(out, rise_ms)


def contact(seconds, rng, fc, t60, rise_ms):
    """Short low-passed noise: the scuff of two surfaces meeting."""
    t = times(seconds)
    return fade_in(lowpass4(white(seconds, rng), fc) * decay(t, t60), rise_ms)


def phase(freq):
    ph = np.cumsum(TAU * np.asarray(freq) / SR)
    return ph - ph[0]


def servo(freq, ripple):
    """The robots' little motors: soft harmonics with gear-tooth ripple."""
    ph = phase(freq)
    tone = (np.sin(ph) + 0.42 * np.sin(2 * ph) + 0.2 * np.sin(3 * ph)
            + 0.09 * np.sin(4 * ph) + 0.04 * np.sin(5 * ph))
    gear = 1.0 - ripple * (0.5 + 0.5 * np.cos(ph / 6.0))
    return lowpass(tone * gear, 2200.0, 0.6)


def voice(freq):
    """The robots' hum: a rounded harmonic tone, low-passed to an 'mm'."""
    ph = phase(freq)
    tone = sum(np.sin(k * ph) / k ** 1.7 for k in range(1, 10))
    tone = peaking(tone, 560.0, 1.4, 3.0)
    return lowpass(lowpass(tone, 1500.0, 0.6), 1500.0, 0.6)


def soft_tone(freq):
    ph = phase(freq)
    return np.sin(ph) + 0.2 * np.sin(2 * ph) + 0.05 * np.sin(3 * ph)


MALLET = (
    # ratio, amplitude, share of the note's T60, scaled by brightness
    (1.0, 1.0, 1.0, False),
    (1.0009, 0.22, 0.85, False),  # a second bar a hair sharp: slow shimmer
    (2.0, 0.20, 0.32, True),
    (3.0, 0.07, 0.16, True),
    (4.16, 0.04, 0.06, True),     # the brief bar 'tink'
)


def mallet(hz, seconds, t60=1.2, bright=1.0, rise_ms=4.0):
    """Felt mallet on a celesta bar: nearly pure, a soft 4 ms strike."""
    t = times(seconds)
    out = np.zeros(len(t))
    for ratio, amp, life, scaled in MALLET:
        if hz * ratio > 4200.0:
            continue
        out += (amp * bright if scaled else amp) * decay(t, t60 * life) * np.sin(TAU * hz * ratio * t)
    return fade_in(out, rise_ms)


def pad(freqs, seconds, rng, cutoff):
    """Soft pad: three detuned band-limited saws per note, low-passed."""
    t = times(seconds)
    out = np.zeros(len(t))
    for hz in freqs:
        for cents in (-6.0, 0.0, 5.0):
            fr = hz * 2.0 ** (cents / 1200.0)
            start = rng.uniform(0.0, TAU)
            for k in range(1, 9):
                if fr * k > 2600.0:
                    break
                out += np.sin(TAU * fr * k * t + start * k) / k
    return lowpass(lowpass(out, cutoff, 0.6), cutoff, 0.6)


# ---------------------------------------------------------------- materials

BOX = ((142, 0.16, 1.0), (231, 0.10, 0.55), (348, 0.075, 0.40),
       (517, 0.05, 0.25), (766, 0.035, 0.20), (1130, 0.022, 0.12),
       (1850, 0.016, 0.06), (2650, 0.010, 0.025))
FLOOR = ((92, 0.08, 1.0), (158, 0.06, 0.45), (262, 0.04, 0.2))
# Brass keeps a little upper-mode detail, with a rounded onset rather than
# a hard transient, so a latch reads clearly without a sharp metallic ping.
BRASS = ((1240, 0.035, 1.0), (1870, 0.026, 0.6), (2710, 0.018, 0.22), (3550, 0.012, 0.10))
BRASS_RISE = 2.8
CELL = ((262, 0.06, 1.0), (608, 0.04, 0.5), (1020, 0.034, 0.42), (1650, 0.024, 0.26))
PLATE = ((165, 0.06, 1.0), (290, 0.045, 0.5), (470, 0.035, 0.3), (720, 0.02, 0.15))
STOP_SOFT = ((130, 0.08, 1.0), (215, 0.06, 0.5), (350, 0.04, 0.3), (560, 0.025, 0.15))
STOP_FIRM = ((118, 0.10, 1.0), (196, 0.07, 0.55), (318, 0.05, 0.35), (505, 0.035, 0.2))
UIWOOD = ((620, 0.03, 1.0), (1010, 0.022, 0.6), (1560, 0.016, 0.35), (2300, 0.010, 0.15))

# Room settings: decay (T60 of the main band, seconds), the frequency above
# which it darkens and dies sooner, how soft the response's onset is, and
# the seed of its noise.
NEAR = dict(rt60=0.45, damp=2200.0, rise_ms=10.0, seed=11)
CHIME = dict(rt60=1.1, damp=3000.0, rise_ms=18.0, seed=12)
HALL = dict(rt60=1.3, damp=3000.0, rise_ms=20.0, seed=13)

# Bands of the response: below 250 Hz, to 900 Hz, to damp, to twice damp and
# above, each with its share of the T60 and its level.
ROOM_LIFE = (0.85, 1.0, 0.8, 0.5, 0.3)
ROOM_GAIN = (0.7, 1.0, 0.75, 0.35, 0.1)
ROOM_PREDELAY = 0.005
_RESPONSES = {}


def below(freqs, edge):
    """1 an half octave below edge, 0 a half octave above, raised cosine in
    log frequency between."""
    x = np.clip(np.log2(np.maximum(freqs, 1.0) / edge) + 0.5, 0.0, 1.0)
    return 0.5 + 0.5 * np.cos(np.pi * x)


def room_response(rt60, damp, rise_ms, seed):
    """Seeded noise split into five bands, each decaying exponentially at its
    own rate, after a 5 ms pre-delay and a raised-cosine onset; unit energy.
    Noise has no discrete modes, so the tail decays as a hiss would, with no
    metallic ring."""
    key = (rt60, damp, rise_ms, seed)
    if key not in _RESPONSES:
        rng = np.random.default_rng(seed)
        n = count(rt60 * 1.05 + ROOM_PREDELAY)
        t = np.arange(n) / SR
        spectrum = np.fft.rfft(rng.standard_normal(n))
        freqs = np.fft.rfftfreq(n, 1.0 / SR)
        lows = [below(freqs, edge) for edge in (250.0, 900.0, damp, 2.0 * damp)]
        masks = [lows[0]] + [lows[i] - lows[i - 1] for i in range(1, 4)] + [1.0 - lows[3]]
        out = np.zeros(n)
        for mask, life, gain in zip(masks, ROOM_LIFE, ROOM_GAIN):
            out += gain * np.fft.irfft(spectrum * mask, n) * decay(t, rt60 * life)
        onset = np.clip((t - ROOM_PREDELAY) / (rise_ms / 1000.0), 0.0, 1.0)
        out *= 0.5 - 0.5 * np.cos(np.pi * onset)
        fade_out(out, 150.0 * rt60)
        _RESPONSES[key] = out / math.sqrt(float(np.sum(out * out)))
    return _RESPONSES[key]


def room(x, mix, rt60, damp, rise_ms, seed):
    """The sound in its room: x convolved with the room's response (FFT),
    high-passed at 150 Hz, added at mix times the dry signal's RMS. The
    tail must fit in x (pad x with silence)."""
    response = room_response(rt60, damp, rise_ms, seed)
    size = 1 << int(math.ceil(math.log2(len(x) + len(response))))
    wet = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(response, size), size)[: len(x)]
    tail = highpass(wet, 150.0)
    level = rms(tail)
    return x + tail * (mix * rms(x) / level) if level > 0.0 else x


# ---------------------------------------------------------------- robots

def step(seed, seconds, texture):
    """A brief, dry rubber scuff with a small surface tick. There is no
    pitched servo, note sweep or periodic tread modulation to repeat with
    every cell. Seed and broad noise colour distinguish the three takes."""
    rng = np.random.default_rng(seed)
    scuff = lowpass4(highpass(white(seconds, rng), 420.0), 3400.0)
    scuff = peaking(scuff, texture, .65, 2.0)
    scuff *= shape(seconds, [(0, 0), (0.005, 1), (0.018, .55), (seconds - .012, 0)])
    tick = contact(seconds, rng, 4400.0, .024, 2.5)
    pat = modes(seconds, FLOOR, 3.0, scale=1.8, damp=.45)
    return .65 * unit(scuff) + .28 * unit(tick) + .20 * unit(pat)


def nose(seconds, rng):
    """The robot's rubber nose meets a wall: the floor's modes raised a
    little (so small speakers still carry it), a 5 ms rise, and a scuff of
    noise under 400 Hz."""
    return modes(seconds, FLOOR, 5.0, 1.25) + 0.15 * contact(seconds, rng, 400.0, 0.03, 3.0)


def bump():
    """An unobtrusive rubber contact; no voiced 'nope' on failed moves."""
    seconds = 0.105
    rng = np.random.default_rng(301)
    return unit(nose(seconds, rng)) + .16 * unit(contact(seconds, rng, 3000.0, .025, 3.0))


def bump_soft():
    """An even smaller contact for a held key repeating into a wall."""
    seconds = 0.065
    rng = np.random.default_rng(302)
    return room(nose(seconds, rng), 0.06, **NEAR)


def threshold(entering):
    """A robot steps through a doorway and shrinks into (or grows out of) a
    room: a broad surface brush between 700 and 1400 Hz, keeping detail up
    to 3.4 kHz, with only a little hollow wooden body under it; and
    a soft wooden tap as it lands inside, at the lip (entering, 0.38 s of
    the 0.55 s passage), or as it pushes off (leaving). Smooth swells and a
    dry texture replace the strongly resonant, muffled sweep."""
    seconds = 0.6 if entering else 0.44
    rng = np.random.default_rng(401 if entering else 402)
    t = times(seconds)
    if entering:
        u = np.clip(t / 0.46, 0.0, 1.0)
        air_hz = 1400.0 * (700.0 / 1400.0) ** u
        bend = 1.25 * (0.8 / 1.25) ** u
        air_env = [(0, 0), (0.19, 1), (0.40, 0.5), (seconds - 0.012, 0)]
        body_env = [(0, 0), (0.14, 0.4), (0.30, 1), (0.42, 0.5), (seconds - 0.012, 0)]
        tap_at, tap_gain = 0.38, 0.55
    else:
        u = t / seconds
        air_hz = 700.0 * (1400.0 / 700.0) ** u
        bend = 0.8 * (1.25 / 0.8) ** u
        air_env = [(0, 0), (0.03, 0.15), (0.26, 1), (0.36, 0.5), (seconds - 0.01, 0)]
        body_env = [(0, 0), (0.06, 0.5), (0.2, 1), (0.32, 0.5), (seconds - 0.01, 0)]
        tap_at, tap_gain = 0.012, 0.3
    air = lowpass4(sweep_band(white(seconds, rng), air_hz, .8), 3400.0) * shape(seconds, air_env)
    hollow = np.zeros(len(t))
    for hz, _, amp in BOX[:3]:
        hollow += amp * sweep_band(white(seconds, rng), hz * bend, 18.0)
    hollow = lowpass4(hollow, 1500.0) * shape(seconds, body_env)
    tap = add(blank(seconds), modes(seconds - tap_at, BOX, 4.5, scale=1.35), tap_at)
    out = unit(air) + 0.2 * unit(hollow) + tap_gain * unit(tap)
    return room(out, 0.08, **NEAR)


# ---------------------------------------------------------------- boxes and batteries

def push_box(seed, seconds, stop, scale):
    """A painted wooden box slides one cell on plaster: band-passed scrape
    with stick-slip roughness and the box's own resonances ringing in it,
    then a soft wooden thunk (3.5 ms rise) and a little floor thud as it
    stops."""
    rng = np.random.default_rng(seed)
    noise = white(seconds, rng)
    scrape = unit(bandpass(noise, 600.0 * scale, 0.7))
    ring = sum(amp * bandpass(noise, hz * scale, 9.0) for hz, _, amp in BOX[:4])
    scrape = lowpass4(scrape + 0.6 * unit(ring), 3400.0) * wander(seconds, rng, 140.0, 45.0, 0.55)
    scrape *= shape(seconds, [(0, 0), (0.02, 1), (stop - 0.03, 0.75), (stop + 0.008, 0)])
    rest = seconds - stop
    thunk = blank(seconds)
    add(thunk, modes(rest, BOX, 3.5, scale), stop)
    add(thunk, modes(rest, FLOOR, 4.0), stop, 0.4)
    add(thunk, contact(rest, rng, 3200.0, 0.03, 2.5), stop, 0.35)
    out = 0.42 * unit(scrape) + unit(thunk)
    return room(out, 0.12, **NEAR)


def push_battery(seed, seconds, scale):
    """A small heavy battery is nudged: a small clack of its body (2 ms
    rise) and copper top, a short slide with detail up to 3.6 kHz, and a
    tiny settle."""
    rng = np.random.default_rng(seed)
    clack = blank(seconds)
    add(clack, modes(seconds, CELL, 2.0, scale), 0.003)
    add(clack, modes(seconds, BRASS, BRASS_RISE, scale), 0.003, 0.15)
    slide = lowpass4(bandpass(white(seconds, rng), 1400.0 * scale, 0.7), 3600.0)
    slide *= wander(seconds, rng, 220.0, 70.0, 0.5)
    slide *= shape(seconds, [(0, 0), (0.012, 0), (0.03, 1), (seconds - 0.05, 0.6), (seconds - 0.03, 0)])
    settle = add(blank(seconds), modes(0.05, CELL, 2.0, scale * 1.06), seconds - 0.05)
    out = unit(clack) + 0.3 * unit(slide) + 0.22 * unit(settle)
    return room(out, 0.10, **NEAR)


def seat():
    """A battery drops into its socket: a soft click-clack of brass and the
    battery's body, a small latch 22 ms later, then a warm rising fifth on
    the mallet, A4 to E5."""
    seconds = 0.72
    click = blank(seconds)
    add(click, modes(seconds, BRASS, BRASS_RISE), 0.004, 0.7)
    add(click, modes(seconds, CELL, 2.0, 0.9), 0.004, 0.6)
    add(click, modes(seconds, BRASS, BRASS_RISE, 1.12), 0.026, 0.35)
    chime = blank(seconds)
    add(chime, mallet(pitch("A4"), seconds, 1.0, 0.8), 0.075, 0.55)
    add(chime, mallet(pitch("E5"), seconds, 0.9, 0.7), 0.175, 0.5)
    out = 0.7 * unit(click) + unit(chime)
    return room(out, 0.2, **CHIME)


def unseat():
    """A battery leaves its socket: a soft unlatch and a short blip gliding
    down from E5 to A4."""
    seconds = 0.2
    unlatch = blank(seconds)
    add(unlatch, modes(seconds, BRASS, BRASS_RISE, 0.95), 0.004, 0.35)
    add(unlatch, modes(seconds, CELL, 2.0), 0.004, 0.3)
    e5, a4 = pitch("E5"), pitch("A4")
    blip = soft_tone(glide(seconds, [(0.0, e5), (0.02, e5), (0.15, a4)]))
    blip *= shape(seconds, [(0, 0), (0.018, 0), (0.026, 1), (0.08, 0.6), (seconds - 0.01, 0)])
    out = 0.45 * unit(unlatch) + unit(blip)
    return room(out, 0.12, **NEAR)


def dock():
    """A box lands on its outline: a deep soft wooden thunk (the box's modes
    lowered and lengthened, 4.5 ms rise) with a floor thud, and a low warm
    fifth, D4 then A4."""
    seconds = 0.75
    rng = np.random.default_rng(601)
    thunk = blank(seconds)
    add(thunk, modes(seconds, BOX, 4.5, 0.8, 1.4), 0.004)
    add(thunk, modes(seconds, FLOOR, 5.0), 0.004, 0.5)
    add(thunk, contact(seconds, rng, 700.0, 0.04, 3.0), 0.004, 0.1)
    chime = blank(seconds)
    add(chime, mallet(pitch("D4"), seconds, 1.3, 0.7), 0.055, 0.5)
    add(chime, mallet(pitch("A4"), seconds, 1.1, 0.6), 0.095, 0.38)
    out = unit(thunk) + 0.7 * unit(chime)
    return room(out, 0.18, **CHIME)


# ---------------------------------------------------------------- plates and shutters

def plate_down():
    """A pressure plate goes down: a soft brass tick as the rim gives, then
    the plate's low clunk 27 ms later."""
    seconds = 0.12
    out = blank(seconds)
    add(out, unit(modes(seconds, BRASS, BRASS_RISE, 0.9)), 0.003, 0.4)
    clunk = modes(seconds, PLATE, 3.0) + 0.3 * modes(seconds, FLOOR, 4.0, 1.5)
    add(out, unit(clunk), 0.03)
    return room(out, 0.08, **NEAR)


def plate_up():
    """A pressure plate springs back: a light soft 'tock' (the plate's modes
    a fifth higher and shorter, 2.5 ms rise) with a faint brass tick in
    it."""
    seconds = 0.09
    out = blank(seconds)
    add(out, unit(modes(seconds, PLATE, 2.5, 1.45, 0.6)), 0.003)
    add(out, unit(modes(seconds, BRASS, BRASS_RISE, 0.95)), 0.003, 0.4)
    return room(out, 0.08, **NEAR)


def shutter(opening):
    """A shutter slides down into the floor (opening) or rises out of it:
    a wooden panel sliding in its groove (noise through a band-pass falling
    950 to 470 Hz, or rising 380 to 720 Hz, with stick-slip), a little motor
    under it, and a soft stop. Rising sits lower, and stops firmer with a
    small latch."""
    seconds = 0.30 if opening else 0.32
    stop = 0.205 if opening else 0.215
    rng = np.random.default_rng(701 if opening else 702)
    t = times(seconds)
    u = np.clip(t / stop, 0.0, 1.0)
    if opening:
        band, motor_hz, stop_modes, rise = 950.0 * (470.0 / 950.0) ** u, 150.0 * 0.8 ** u, STOP_SOFT, 4.0
    else:
        band, motor_hz, stop_modes, rise = 380.0 * (720.0 / 380.0) ** u, 110.0 * 1.25 ** u, STOP_FIRM, 3.0
    slide = lowpass4(sweep_band(white(seconds, rng), band * 1.5, .8), 3600.0)
    slide *= wander(seconds, rng, 160.0, 50.0, 0.45)
    slide *= shape(seconds, [(0, 0), (0.03, 1), (stop - 0.03, 0.85), (stop + 0.006, 0)])
    motor = servo(motor_hz, 0.4 if not opening else 0.35)
    motor *= shape(seconds, [(0, 0), (0.03, 1), (stop - 0.02, 0.8), (stop + 0.01, 0)])
    rest = seconds - stop
    halt = add(blank(seconds), modes(rest, stop_modes, rise), stop)
    if opening:
        out = 0.5 * unit(slide) + 0.3 * unit(motor) + 0.8 * unit(halt)
    else:
        add(halt, modes(rest, BRASS, BRASS_RISE, 0.85), stop, 0.15)
        out = 0.45 * unit(slide) + 0.35 * unit(motor) + unit(halt)
    return room(out, 0.12, **NEAR)


# ---------------------------------------------------------------- undo, reset, puzzle

def undo():
    """Tape rewind: a soft tone sweeping up an octave, D4 to D5, with a
    little flutter, swelling like a note played backwards and easing out,
    plus a faint whirr following it."""
    seconds = 0.26
    rng = np.random.default_rng(801)
    t = times(seconds)
    top = 0.2
    hz = pitch("D4") * 2.0 ** np.clip(t / top, 0.0, 1.0) * (1.0 + 0.02 * np.sin(TAU * 17.0 * t))
    swell = np.where(t < top, np.exp((t - top) / 0.07), 1.0)
    swell = fade_in(swell * shape(seconds, [(0, 1), (top, 1), (seconds - 0.012, 0)]), 25.0)
    tone = soft_tone(hz) * swell
    whirr = lowpass4(sweep_band(white(seconds, rng), 2.0 * hz, 4.0), 2000.0) * swell
    out = unit(tone) + 0.18 * unit(whirr)
    return room(out, 0.10, **NEAR)


def reset():
    """The puzzle sweeps back: breathy air sweeping down, a soft tone gliding
    from A4 down to D4, and a settle on D4 (mallet, a low D3 under it and a
    faint wooden tap)."""
    seconds = 0.62
    rng = np.random.default_rng(802)
    t = times(seconds)
    air_hz = 1400.0 * (240.0 / 1400.0) ** np.clip(t / 0.36, 0.0, 1.0)
    air = lowpass4(sweep_band(white(seconds, rng), air_hz, 1.0), 1400.0)
    air *= shape(seconds, [(0, 0), (0.1, 1), (0.3, 0.4), (0.42, 0)])
    d4, a4 = pitch("D4"), pitch("A4")
    hum = glide(seconds, [(0.0, a4), (0.04, a4), (0.29, d4)])
    tone = soft_tone(hum) * shape(seconds, [(0, 0), (0.03, 0), (0.08, 0.7), (0.28, 0.8), (0.42, 0)])
    settle = blank(seconds)
    add(settle, mallet(d4, seconds, 0.9, 0.6), 0.29, 0.6)
    add(settle, mallet(pitch("D3"), seconds, 0.9, 0.4), 0.29, 0.25)
    add(settle, modes(seconds, BOX, 4.0, 1.5), 0.29, 0.2)
    out = 0.45 * unit(air) + 0.5 * unit(tone) + unit(settle)
    return room(out, 0.16, **CHIME)


def start():
    """A puzzle begins: a rising F major triad on the mallet, F4 A4 C5, with
    a soft F3 under the first note. Solve's arpeggio opens with the same
    three notes and carries them on."""
    seconds = 1.1
    out = blank(seconds)
    add(out, mallet(pitch("F4"), seconds, 1.2, 0.8), 0.01, 0.5)
    add(out, mallet(pitch("F3"), seconds, 1.4, 0.5), 0.01, 0.18)
    add(out, mallet(pitch("A4"), seconds, 1.2, 0.8), 0.15, 0.45)
    add(out, mallet(pitch("C5"), seconds, 1.2, 0.8), 0.30, 0.5)
    return room(out, 0.22, **CHIME)


def solve():
    """A puzzle is solved: a mallet arpeggio through Fmaj9 (F4 A4 C5 E5 G5
    C6) landing softly on F5 and A5, over a pad (F3 C4 G4 A4 E5) that swells
    in for a second and fades."""
    seconds = 2.6
    rng = np.random.default_rng(901)
    bells = blank(seconds)
    for name, at, gain in (("F4", 0.0, 0.42), ("A4", 0.12, 0.42), ("C5", 0.24, 0.42),
                           ("E5", 0.36, 0.38), ("G5", 0.48, 0.36), ("C6", 0.62, 0.30),
                           ("F5", 0.86, 0.30), ("A5", 0.89, 0.22)):
        add(bells, mallet(pitch(name), seconds, 1.8, 0.75), at + 0.01, gain)
    chord = [pitch(name) for name in ("F3", "C4", "G4", "A4", "E5")]
    swell = pad(chord, seconds, rng, 1100.0)
    swell *= shape(seconds, [(0, 0), (0.08, 0), (0.8, 1), (1.2, 0.9), (2.45, 0)])
    out = unit(bells) + 0.28 * unit(swell)
    return room(out, 0.24, **HALL)


def partner(joining):
    """The partner arrives (A4 then D5) or leaves (D5 then A4, drooping): the
    robots' hum voice, scooping into each note when arriving, doubled softly
    by the mallet."""
    seconds = 0.52
    a4, d5 = pitch("A4"), pitch("D5")
    if joining:
        hum = glide(seconds, [(0.0, a4 * 0.95), (0.012, a4 * 0.95), (0.04, a4), (0.165, a4),
                              (0.19, d5 * 0.98), (0.215, d5)])
        env = [(0, 0), (0.012, 0), (0.026, 1), (0.15, 0.7), (0.172, 0.25), (0.19, 1.0),
               (0.30, 0.65), (seconds - 0.015, 0)]
        first, second = a4, d5
    else:
        hum = glide(seconds, [(0.0, d5), (0.165, d5), (0.19, a4 * 1.01), (0.22, a4), (0.33, a4),
                              (seconds - 0.03, a4 * 0.955)])
        env = [(0, 0), (0.012, 0), (0.028, 0.9), (0.15, 0.65), (0.172, 0.2), (0.192, 0.85),
               (0.30, 0.5), (seconds - 0.015, 0)]
        first, second = d5, a4
    hummed = voice(hum) * shape(seconds, env)
    bells = blank(seconds)
    add(bells, mallet(first, seconds, 0.8, 0.6), 0.012, 0.8)
    add(bells, mallet(second, seconds, 0.9, 0.6), 0.178, 0.9 if not joining else 1.0)
    out = 0.75 * unit(hummed) + unit(bells)
    return room(out, 0.14, **CHIME)


# ---------------------------------------------------------------- interface

def ui_hover():
    """Barely there: a tiny soft wooden tick (0.7 ms rise, 12 ms decay)."""
    seconds = 0.03
    return add(blank(seconds), modes(seconds, ((1180, 0.012, 1.0), (1900, 0.008, 0.35)), 0.7), 0.002)


def ui_click():
    """A soft wooden click: small-block modes at 620 Hz-2.3 kHz with a low
    body."""
    seconds = 0.06
    out = blank(seconds)
    add(out, modes(seconds, UIWOOD, 0.9), 0.002)
    add(out, modes(seconds, ((310, 0.025, 1.0),), 1.5), 0.002, 0.3)
    return out


def ui_back():
    """The click a quarter lower, struck softer."""
    seconds = 0.06
    out = blank(seconds)
    add(out, modes(seconds, UIWOOD, 1.2, 0.74, 0.9), 0.002)
    add(out, modes(seconds, ((240, 0.025, 1.0),), 2.0), 0.002, 0.35)
    return out


def ui_open():
    """A page lifted: noise through a band-pass rising 600 Hz to 1.5 kHz
    (kept under 2.4 kHz) with a little crinkle, over a soft low breath; 70 ms
    swell."""
    seconds = 0.2
    rng = np.random.default_rng(1001)
    t = times(seconds)
    band = 600.0 * (1500.0 / 600.0) ** (t / seconds)
    swish = lowpass4(sweep_band(white(seconds, rng), band, 0.9), 2400.0)
    breath = lowpass4(white(seconds, rng), 650.0)
    out = (unit(swish) + 0.35 * unit(breath)) * wander(seconds, rng, 90.0, 60.0, 0.35)
    return out * shape(seconds, [(0, 0), (0.07, 1), (0.12, 0.6), (seconds - 0.006, 0)])


def ui_toggle():
    """A small switch: two soft clicks 35 ms apart, the second higher."""
    seconds = 0.085
    out = blank(seconds)
    add(out, modes(seconds, UIWOOD, 0.9, 1.05), 0.003, 0.75)
    add(out, modes(seconds, UIWOOD, 0.9, 1.2), 0.038)
    return out


def ui_slide():
    """A slider step: the tiniest tick."""
    seconds = 0.022
    return add(blank(seconds), modes(seconds, ((1100, 0.008, 1.0), (1750, 0.006, 0.3)), 0.8), 0.002)


# ---------------------------------------------------------------- the set

# name, builder, loudness (the loudest 50 ms, K-weighted, dB), peak ceiling
# dBFS, allowed seconds, fade-out ms, final low-pass Hz, format.
SOUNDS = (
    ("step1", lambda: step(101, 0.075, 1500.0), -30.0, -10.0, (0.06, 0.09), 12, 6000.0, "wav"),
    ("step2", lambda: step(102, 0.080, 1750.0), -30.0, -10.0, (0.06, 0.09), 12, 6000.0, "wav"),
    ("step3", lambda: step(103, 0.070, 1300.0), -30.0, -10.0, (0.06, 0.09), 12, 6000.0, "wav"),
    ("push_box1", lambda: push_box(201, 0.23, 0.165, 1.0), -19.3, -7.0, (0.18, 0.26), 15, 6000.0, "wav"),
    ("push_box2", lambda: push_box(202, 0.25, 0.18, 0.95), -19.3, -7.0, (0.18, 0.26), 15, 6000.0, "wav"),
    ("push_battery1", lambda: push_battery(211, 0.16, 1.0), -23.7, -7.0, (0.12, 0.20), 12, 6000.0, "wav"),
    ("push_battery2", lambda: push_battery(212, 0.18, 0.94), -23.7, -7.0, (0.12, 0.20), 12, 6000.0, "wav"),
    ("bump", bump, -31.0, -12.0, (0.09, 0.12), 12, 6000.0, "wav"),
    ("bump_soft", bump_soft, -34.0, -12.0, (0.05, 0.08), 12, 6000.0, "wav"),
    ("enter", lambda: threshold(True), -29.5, -10.0, (0.55, 0.65), 20, 6000.0, "wav"),
    ("exit", lambda: threshold(False), -29.5, -10.0, (0.38, 0.48), 20, 6000.0, "wav"),
    ("seat", seat, -18.0, -7.0, (0.50, 0.80), 120, 6000.0, "wav"),
    ("unseat", unseat, -19.0, -9.0, (0.15, 0.25), 15, 6000.0, "wav"),
    ("dock", dock, -17.7, -7.0, (0.50, 0.80), 120, 6000.0, "wav"),
    ("plate_down", plate_down, -22.2, -9.0, (0.08, 0.14), 12, 6000.0, "wav"),
    ("plate_up", plate_up, -27.5, -10.0, (0.06, 0.12), 12, 6000.0, "wav"),
    ("shutter_open", lambda: shutter(True), -20.5, -7.0, (0.25, 0.35), 15, 6000.0, "wav"),
    ("shutter_close", lambda: shutter(False), -19.8, -7.0, (0.25, 0.35), 15, 6000.0, "wav"),
    ("undo", undo, -20.0, -10.0, (0.20, 0.30), 8, 6000.0, "wav"),
    ("reset", reset, -19.5, -8.0, (0.50, 0.70), 120, 6000.0, "wav"),
    ("start", start, -17.0, -7.0, (0.80, 1.20), 200, 6000.0, "wav"),
    ("solve", solve, -14.9, -6.0, (2.00, 3.00), 450, 6000.0, "ogg"),
    ("join", lambda: partner(True), -17.0, -7.0, (0.40, 0.60), 60, 6000.0, "wav"),
    ("leave", lambda: partner(False), -17.5, -8.0, (0.40, 0.60), 60, 6000.0, "wav"),
    ("ui_hover", ui_hover, -40.2, -20.0, (0.02, 0.04), 5, 4500.0, "wav"),
    ("ui_click", ui_click, -29.8, -12.0, (0.04, 0.08), 8, 4500.0, "wav"),
    ("ui_back", ui_back, -30.6, -14.0, (0.04, 0.08), 8, 4500.0, "wav"),
    ("ui_open", ui_open, -27.2, -16.0, (0.15, 0.25), 8, 4500.0, "wav"),
    ("ui_toggle", ui_toggle, -29.8, -12.0, (0.06, 0.10), 8, 4500.0, "wav"),
    ("ui_slide", ui_slide, -38.6, -18.0, (0.015, 0.03), 4, 4500.0, "wav"),
)


def master(x, target, ceiling, fade_ms, lowpass_hz):
    """High-pass at 35 Hz, broad presence EQ and the final fourth-order
    low-pass, a 2 ms fade in
    and the given fade out, the mean taken out with a clip-long Hann bump
    (a sub-audio offset that keeps both ends at zero), the loudest 50 ms set
    to the target (K-weighted) under the peak ceiling, then 16-bit samples
    with an exact zero at both ends."""
    # Restore material detail around 2-4 kHz instead of burying every cue
    # under the same low-mid body. Upper treble still has a gentle ceiling.
    y = peaking(highpass(x, 35.0), 280.0, .65, -1.5)
    y = lowpass4(peaking(y, 2400.0, .65, 2.5), lowpass_hz)
    fade_out(fade_in(y, 2.0), fade_ms)
    bump_shape = np.hanning(len(y))
    y -= bump_shape * (float(np.mean(y)) / float(np.mean(bump_shape)))
    y *= 10.0 ** ((target - loudness(y, 50.0)) / 20.0)
    peak = float(np.max(np.abs(y)))
    if peak > 10.0 ** (ceiling / 20.0):
        raise ValueError(f"peak {db(peak):.1f} dBFS above the ceiling {ceiling}")
    pcm = np.clip(np.round(y * 32767.0), -32768, 32767).astype("<i2")
    pcm[0] = pcm[-1] = 0
    return pcm


def write_wav(path, pcm):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(SR)
        out.writeframes(pcm.tobytes())


def encode_ogg(wav, ogg):
    """Ogg Vorbis through ffmpeg's libvorbis, bit-exact and without metadata,
    so the same master gives the same bytes."""
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                    "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", str(OGG_QUALITY),
                    "-fflags", "+bitexact", "-flags:a", "+bitexact", str(ogg)], check=True)


def decode(path):
    """A file's samples as ffmpeg decodes them (float32 mono at SR)."""
    raw = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(path),
                          "-f", "f32le", "-ac", "1", "-ar", str(SR), "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, "<f4").astype(np.float64)


# ---------------------------------------------------------------- measuring

def db(value):
    return 20.0 * math.log10(value) if value > 0.0 else -200.0


def band_share(power, hz, lo, hi):
    return 10.0 * math.log10(float(power[(hz >= lo) & (hz < hi)].sum()) / float(power.sum()) + 1e-30)


def presence(x):
    """The loudest 2 ms (1 ms hop) of the 2-5 kHz band (a zero-phase FFT
    band-pass with 8th-order skirts), dBFS: how hard a click lands."""
    n = len(x) + 4096
    hz = np.fft.rfftfreq(n, 1.0 / SR)
    gain = 1.0 / np.sqrt(1.0 + (2000.0 / np.maximum(hz, 1e-9)) ** 8) / np.sqrt(1.0 + (hz / 5000.0) ** 8)
    y = np.fft.irfft(np.fft.rfft(x, n) * gain, n)[: len(x)]
    width = count(0.002)
    sums = np.concatenate([[0.0], np.cumsum(y * y)])
    starts = np.arange(0, max(1, len(y) - width), width // 2)
    return db(math.sqrt(float(np.max(sums[starts + width] - sums[starts])) / width))


def measure(x):
    """x is float samples (PCM / 32768 or a decode)."""
    peak = float(np.max(np.abs(x)))
    power = np.abs(np.fft.rfft(x)) ** 2
    hz = np.fft.rfftfreq(len(x), 1.0 / SR)
    above6 = highpass(highpass(x, 6000.0, 0.5412), 6000.0, 1.3066)
    width = count(0.002)
    sums = np.concatenate([[0.0], np.cumsum(above6 * above6)])
    starts = np.arange(0, max(1, len(x) - width), width // 2)
    worst = math.sqrt(float(np.max(sums[starts + width] - sums[starts])) / width)
    # Attack of the first onset: on the Hilbert envelope (smoothed over
    # 0.5 ms), the time from 10% to 90% of the highest level reached within
    # 50 ms of the moment the sound first reaches 10% of its peak.
    size = 1 << int(math.ceil(math.log2(2 * len(x))))
    spectrum = np.fft.fft(x, size)
    weights = np.zeros(size)
    weights[0] = weights[size // 2] = 1.0
    weights[1:size // 2] = 2.0
    hilbert = np.abs(np.fft.ifft(spectrum * weights))[: len(x)]
    hilbert = np.convolve(hilbert, np.ones(count(0.0005)) / count(0.0005), mode="same")
    onset = int(np.argmax(hilbert >= 0.1 * hilbert.max()))
    local = float(hilbert[onset:onset + count(0.05)].max())
    attack = int(np.argmax(hilbert[onset:] >= 0.9 * local)) / SR * 1000.0
    start10 = int(np.argmax(hilbert[:onset + 1] >= 0.1 * local))
    attack += (onset - start10) / SR * 1000.0
    edge = np.abs(x[:16]).max(), np.abs(x[-16:]).max()
    return {
        "seconds": len(x) / SR,
        "peak": db(peak),
        "rms": db(rms(x)),
        "dc": float(np.mean(x)),
        "band25": band_share(power, hz, 2000.0, 5000.0),
        "above6k": band_share(power, hz, 6000.0, 1e9),
        "hf_burst": db(worst),
        "presence": presence(x),
        "centroid": float((hz * power).sum() / power.sum()),
        "attack_ms": attack,
        "s50": loudness(x, 50.0),
        "m400": loudness(x, 400.0),
        "ends": (float(x[0]) * 32768.0, float(x[-1]) * 32768.0, float(edge[0]) * 32768.0,
                 float(edge[1]) * 32768.0),
    }


# ---------------------------------------------------------------- contact sheet

GLYPHS = {
    "A": ".#. #.# ### #.# #.#", "B": "##. #.# ##. #.# ##.", "C": ".## #.. #.. #.. .##",
    "D": "##. #.# #.# #.# ##.", "E": "### #.. ##. #.. ###", "F": "### #.. ##. #.. #..",
    "G": ".## #.. #.# #.# .##", "H": "#.# #.# ### #.# #.#", "I": "### .#. .#. .#. ###",
    "J": "..# ..# ..# #.# .#.", "K": "#.# #.# ##. #.# #.#", "L": "#.. #.. #.. #.. ###",
    "M": "#.# ### ### #.# #.#", "N": "##. #.# #.# #.# #.#", "O": ".#. #.# #.# #.# .#.",
    "P": "##. #.# ##. #.. #..", "Q": ".#. #.# #.# ##. .##", "R": "##. #.# ##. #.# #.#",
    "S": ".## #.. .#. ..# ##.", "T": "### .#. .#. .#. .#.", "U": "#.# #.# #.# #.# ###",
    "V": "#.# #.# #.# #.# .#.", "W": "#.# #.# ### ### #.#", "X": "#.# #.# .#. #.# #.#",
    "Y": "#.# #.# .#. .#. .#.", "Z": "### ..# .#. #.. ###",
    "0": "### #.# #.# #.# ###", "1": ".#. ##. .#. .#. ###", "2": "##. ..# .#. #.. ###",
    "3": "##. ..# .#. ..# ##.", "4": "#.# #.# ### ..# ..#", "5": "### #.. ##. ..# ##.",
    "6": ".## #.. ### #.# ###", "7": "### ..# .#. .#. .#.", "8": "### #.# ### #.# ###",
    "9": "### #.# ### ..# ##.", "_": "... ... ... ... ###", ".": "... ... ... ... .#.",
    "-": "... ... ### ... ...", " ": "... ... ... ... ...", ":": "... .#. ... .#. ...",
    "/": "..# ..# .#. #.. #..", "(": ".#. #.. #.. #.. .#.", ")": ".#. ..# ..# ..# .#.",
    "+": "... .#. ### .#. ...", ">": "#.. .#. ..# .#. #..",
}


def draw_text(img, x, y, text, color, scale=2):
    for ch in text.upper():
        rows = GLYPHS.get(ch, GLYPHS[" "]).split()
        for r, row in enumerate(rows):
            for c, bit in enumerate(row):
                if bit == "#":
                    y0, x0 = y + r * scale, x + c * scale
                    if y0 + scale <= img.shape[0] and x0 + scale <= img.shape[1]:
                        img[y0:y0 + scale, x0:x0 + scale] = color
        x += 4 * scale


STOPS = ((0.0, (4, 4, 14)), (0.3, (46, 18, 92)), (0.55, (140, 40, 110)),
         (0.75, (226, 92, 50)), (0.9, (250, 182, 62)), (1.0, (255, 250, 196)))
F_LO, F_HI, FLOOR_DB = 40.0, 20000.0, -100.0


def colour(v):
    xs = [s[0] for s in STOPS]
    return np.stack([np.interp(v, xs, [s[1][i] for s in STOPS]) for i in range(3)], axis=-1)


def freq_row(hz, height):
    return int(round((height - 1) * (1.0 - math.log(hz / F_LO) / math.log(F_HI / F_LO))))


def spectrogram(x, width, height):
    """Log-frequency spectrogram (40 Hz-20 kHz) in absolute dBFS: a full
    scale sine reads 0 dB, the floor is -100 dB."""
    win = 384 if len(x) < count(0.3) else 768
    w = np.hanning(win)
    padded = np.concatenate([np.zeros(win // 2), x, np.zeros(win // 2 + 1)])
    centres = np.linspace(0, len(x) - 1, width).round().astype(int)
    frames = np.stack([padded[c:c + win] * w for c in centres])
    mag = np.abs(np.fft.rfft(frames, 8192, axis=1)) / (w.sum() / 2.0)
    level = 20.0 * np.log10(mag + 1e-9)
    bins = np.fft.rfftfreq(8192, 1.0 / SR)
    rows = F_LO * (F_HI / F_LO) ** (np.arange(height)[::-1] / (height - 1))
    grid = np.stack([np.interp(rows, bins, level[c]) for c in range(width)], axis=1)
    return colour(np.clip((grid - FLOOR_DB) / -FLOOR_DB, 0.0, 1.0))


def write_png(path, img):
    height, width, _ = img.shape
    raw = b"".join(b"\x00" + img[r].tobytes() for r in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    path.write_bytes(png)


def contact_sheet(path, items, cols, cell=(440, 420, 112)):
    """Each cell: name and numbers, the waveform at full scale (dashed lines
    at -6 dBFS), the spectrogram with lines at 100 Hz, 1 kHz, 3 kHz and (red)
    6 kHz. Each cell's time axis fits that sound."""
    cell_w, spec_w, spec_h = cell
    label_h, wave_h, gap, header = 16, 34, 14, 30
    cell_h = label_h + wave_h + 4 + spec_h + gap
    rows = (len(items) + cols - 1) // cols
    img = np.zeros((header + rows * cell_h + 4, cols * cell_w + 12, 3), np.uint8)
    img[:] = (26, 23, 20)
    draw_text(img, 12, 8, "INSIDE OUT SFX  48KHZ  LOG 40-20K  LINES 100 1K 3K 6K(RED)  -100..0 DBFS",
              (220, 210, 190))
    for i, (name, x, m) in enumerate(items):
        cx = 12 + (i % cols) * cell_w
        cy = header + (i // cols) * cell_h
        draw_text(img, cx, cy, f"{name} {m['seconds'] * 1000:.0f}MS PK{m['peak']:.1f} S50 {m['s50']:.1f} "
                               f"HF6 {m['above6k']:.0f}", (235, 225, 200))
        wy = cy + label_h
        img[wy:wy + wave_h, cx:cx + spec_w] = (40, 36, 32)
        mid = wy + wave_h // 2
        img[mid, cx:cx + spec_w] = (70, 64, 58)
        for edge in (mid - wave_h // 4, mid + wave_h // 4):
            img[edge, cx:cx + spec_w:3] = (120, 50, 40)
        edges = np.linspace(0, len(x), spec_w + 1).astype(int)
        for c in range(spec_w):
            seg = x[edges[c]:max(edges[c + 1], edges[c] + 1)]
            top = mid - int(round(float(seg.max()) * (wave_h // 2 - 1)))
            bottom = mid - int(round(float(seg.min()) * (wave_h // 2 - 1)))
            img[min(top, bottom):max(top, bottom) + 1, cx + c] = (200, 190, 170)
        sy = wy + wave_h + 4
        img[sy:sy + spec_h, cx:cx + spec_w] = spectrogram(x, spec_w, spec_h).astype(np.uint8)
        for hz, tint in ((100.0, (90, 90, 90)), (1000.0, (90, 90, 90)), (3000.0, (90, 90, 90)),
                         (6000.0, (255, 60, 60))):
            img[sy + freq_row(hz, spec_h), cx:cx + spec_w:2] = tint
    write_png(path, img)


# ---------------------------------------------------------------- main

def render(name, build, target, ceiling, fade_ms, lp_hz, fmt):
    """Render one sound to its shipping file; returns (path, samples as the
    file plays them)."""
    pcm = master(build(), target, ceiling, fade_ms, lp_hz)
    if fmt == "wav":
        path = OUT / f"{name}.wav"
        write_wav(path, pcm)
        return path, pcm.astype(np.float64) / 32768.0
    MASTERS.mkdir(exist_ok=True)
    wav = MASTERS / f"{name}.wav"
    write_wav(wav, pcm)
    path = OUT / f"{name}.ogg"
    encode_ogg(wav, path)
    return path, decode(path)


def main():
    """Render the set. With --zoom a,b,... it only draws those sounds large
    into zoom.png (an inspection aid) and writes no sound file."""
    if len(sys.argv) > 2 and sys.argv[1] == "--zoom":
        zoom = sys.argv[2].split(",")
        picked = []
        for name, build, target, ceiling, _, fade_ms, lp_hz, _ in SOUNDS:
            if name in zoom:
                x = master(build(), target, ceiling, fade_ms, lp_hz).astype(np.float64) / 32768.0
                picked.append((name, x, measure(x)))
        contact_sheet(OUT / "zoom.png", picked, 1, cell=(1240, 1200, 300))
        return
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg (with libvorbis) is needed on PATH for the Ogg files")
    items, problems, total = [], [], 0
    print(f"{'file':<18}{'ms':>6}{'peak':>7}{'rms':>7}{'S50':>7}{'M400':>7}{'dc':>9}{'2-5k':>6}{'>6k':>6}"
          f"{'hf2ms':>7}{'pres':>7}{'cent':>6}{'atk':>6}{'bytes':>8}  sha256")
    for name, build, target, ceiling, (shortest, longest), fade_ms, lp_hz, fmt in SOUNDS:
        path, x = render(name, build, target, ceiling, fade_ms, lp_hz, fmt)
        data = path.read_bytes()
        total += len(data)
        m = measure(x)
        items.append((name, x, m))
        print(f"{path.name:<18}{m['seconds'] * 1000:6.0f}{m['peak']:7.1f}{m['rms']:7.1f}{m['s50']:7.1f}"
              f"{m['m400']:7.1f}{m['dc']:9.1e}{m['band25']:6.0f}{m['above6k']:6.0f}{m['hf_burst']:7.0f}"
              f"{m['presence']:7.1f}{m['centroid']:6.0f}{m['attack_ms']:6.1f}{len(data):8d}  "
              f"{hashlib.sha256(data).hexdigest()}")
        if not shortest <= m["seconds"] <= longest:
            problems.append(f"{name}: {m['seconds']:.3f} s outside {shortest}-{longest} s")
        if m["peak"] > -6.0:
            problems.append(f"{name}: peak {m['peak']:.2f} dBFS above -6")
        if fmt == "wav" and (m["ends"][0] != 0 or m["ends"][1] != 0):
            problems.append(f"{name}: endpoints {m['ends']}")
        if fmt == "ogg" and max(m["ends"]) > 8.0:
            problems.append(f"{name}: decoded ends reach {max(m['ends']):.1f} LSB")
        if m["above6k"] > -30.0:
            problems.append(f"{name}: {m['above6k']:.1f} dB of its energy above 6 kHz")
    contact_sheet(OUT / "contact_sheet.png", items, 3)
    print(f"total {total} bytes in {len(items)} files")
    if total >= BUDGET:
        problems.append(f"set is {total} bytes")
    for problem in problems:
        print("PROBLEM", problem)
    if problems:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
