"""Music for Inside Out: a seamless puzzling loop and a sparser title loop.

Everything is synthesized here from seeded generators, with numpy alone:

- an FM electric piano (a DX7-style body pair, a soft tine pair that fades
  in tens of milliseconds and a quiet ratio-4 bell), one undetuned voice
  per note so it folds to mono without notches, panned, with a slow
  auto-pan whose mono sum is constant; chord notes are let go before the
  next chord and melody notes just before the next note, so a damper fall
  never holds the old harmony over the new one;
- a soft-mallet bar voice (marimba-like: tuned partials 1, 4 and 10, the
  upper two short and quiet) with one quiet dotted-quarter echo;
- a round sine bass on the roots (a little second harmonic for small
  speakers, 35 ms attacks);
- a detuned, band-limited soft-saw pad of four held lines: a step of one or
  two semitones glides, a leap re-articulates with a short release, and the
  voicing is open (no seconds below C5, no minor second or minor ninth
  within it or against the piano);
- a very quiet wooden tick and brush in the middle two sections;
- a synthetic stereo hall (seeded noise, frequency-dependent decay).

The piano voicings have no minor seconds or minor ninths and avoid whole
tones below C5. The melody rests for whole bars in the first section and
in the coda, so the loop breathes; there is no noise bed.

The arrangement is rendered into a circular buffer exactly one loop long,
the reverb is a circular convolution and the master EQ a circular filter,
so every tail that rings past the end continues at the start: the last
sample leads straight into the first. A second, linear render of three
passes checks that the loop equals what a continuous performance plays.

Writes music.wav (float32 master) and music.ogg (Ogg Vorbis via ffmpeg's
libvorbis), menu.wav and menu.ogg, spectrogram and seam PNGs, and prints
a report. Needs Python 3, numpy and ffmpeg with libvorbis on PATH; takes
about 40 seconds and writes the same bytes every run on one machine.
vorbis_check.c decodes the Ogg files with the engine's own decoder, looping
as the mixer streams them.
"""
from __future__ import annotations

import hashlib
import itertools
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
SR = 48000
BPM = 80.0
BEAT = 60.0 / BPM          # 0.75 s, 36000 samples
BAR = 4.0 * BEAT           # 3 s
SWING = 0.06               # off-beat eighths land at 0.56 of the beat
TAU = 2.0 * np.pi
RMS_TARGET_DB = -22.0

PC = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "Gb": 6, "G": 7,
      "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def midi(name: str) -> int:
    return 12 * (int(name[-1]) + 1) + PC[name[:-1]]


def hz(m: float) -> float:
    return 440.0 * 2.0 ** ((m - 69.0) / 12.0)


# ---------------------------------------------------------------- envelopes

def rise(t, seconds):
    """Raised-cosine rise from 0 to 1."""
    x = np.clip(t / seconds, 0.0, 1.0)
    return 0.5 - 0.5 * np.cos(np.pi * x)


def fall(t, start, seconds):
    """Raised-cosine fall from 1 at `start` to exactly 0 `seconds` later."""
    x = np.clip((t - start) / seconds, 0.0, 1.0)
    return 0.5 + 0.5 * np.cos(np.pi * x)


def pan_gains(p):
    a = (p + 1.0) * np.pi / 4.0
    return np.sqrt(2.0) * np.cos(a), np.sqrt(2.0) * np.sin(a)


def stereo(mono, pan):
    gl, gr = pan_gains(pan)
    return np.stack([mono * gl, mono * gr], axis=1)


# -------------------------------------------------------------- instruments

EP_RELEASE = 0.3   # piano damper fall after the key is let go
EP_CLEAR = 0.2     # chord notes are let go this long before the next chord
EP_LIFT = 0.08     # melody notes are let go this long before the next note


def ep_note(ev):
    """FM electric piano note: body pair (1:1), tine pair (1:7), bell (4:1)."""
    f, vel, hold = ev["f"], ev["vel"], ev["hold"]
    rel = EP_RELEASE
    n = int((hold + rel) * SR) + 2
    t = np.arange(n) / SR
    env = rise(t, 0.006) * fall(t, hold, rel)
    tau = float(np.clip(3.2 * (262.0 / f) ** 0.65, 1.1, 6.0))
    k_tine = float(np.clip((392.0 / f) ** 1.5, 0.0, 1.0))
    k_bell = float(np.clip(2200.0 / (4.0 * f), 0.0, 1.0))
    i_body = (0.25 + 0.95 * vel) * np.exp(-t / 0.9) + 0.12
    i_tine = 1.25 * vel * k_tine * np.exp(-t / 0.03)
    body_decay = np.exp(-t / tau)
    tine_decay = np.exp(-t / (0.3 * tau))
    bell_decay = np.exp(-t / 0.45)
    body = np.sin(TAU * f * t + i_body * np.sin(TAU * f * 1.0007 * t))
    tine = np.sin(TAU * f * t + i_tine * np.sin(TAU * 7.0 * f * t))
    bell = np.sin(TAU * 4.0 * f * t + 0.6 * vel * np.sin(TAU * f * t))
    s = env * (0.60 * body * body_decay + 0.32 * tine * tine_decay
               + 0.08 * vel * k_bell * bell * bell_decay)
    return stereo(s * vel ** 1.5, ev["pan"])


def mallet_note(ev):
    """Soft-mallet bar: tuned partials 1, 4 and 10; the upper two short."""
    f, vel, bright = ev["f"], ev["vel"], ev["bright"]
    tau1 = float(np.clip(0.95 * (440.0 / f) ** 0.6, 0.45, 1.6))
    dur = min(4.6 * tau1, 4.0)
    n = int(dur * SR)
    t = np.arange(n) / SR
    env = rise(t, 0.0035) * fall(t, dur - 0.08, 0.08)
    s = np.exp(-t / tau1) * np.sin(TAU * f * t)
    s += 0.05 * np.exp(-t / 0.4) * np.sin(TAU * 2.0 * f * t)
    s += 0.17 * vel * bright * np.exp(-t / 0.14) * np.sin(TAU * 4.0 * f * t)
    if 10.0 * f < 7000.0:
        s += 0.03 * vel * bright * np.exp(-t / 0.04) * np.sin(TAU * 10.0 * f * t)
    return stereo(env * s * vel ** 1.3, ev["pan"])


def bass_note(ev):
    """Round bass: sine with a fading touch of second and third harmonic."""
    f, vel, hold = ev["f"], ev["vel"], ev["hold"]
    rel = 0.22
    n = int((hold + rel) * SR) + 2
    t = np.arange(n) / SR
    env = rise(t, 0.035) * fall(t, hold, rel) * (0.72 + 0.28 * np.exp(-t / 0.9))
    s = (np.sin(TAU * f * t)
         + 0.20 * np.exp(-t / 1.0) * np.sin(TAU * 2.0 * f * t)
         + 0.05 * np.exp(-t / 0.6) * np.sin(TAU * 3.0 * f * t))
    return stereo(env * s * vel, 0.0)


_TABLES: dict[float, np.ndarray] = {}


def soft_saw_table(f, size=4096):
    """One cycle of a band-limited saw, rolled off above ~1.4 kHz."""
    key = round(f, 6)
    if key not in _TABLES:
        ph = np.arange(size + 1) / size
        table = np.zeros(size + 1)
        for k in range(1, int(4500.0 / f) + 1):
            table += (1.0 / k) / (1.0 + (k * f / 1400.0) ** 2) * np.sin(TAU * k * ph)
        _TABLES[key] = table / np.max(np.abs(table))
    return _TABLES[key]


PAD_GLIDE = 0.22         # a step of one or two semitones glides over this
PAD_LEAD = 0.15          # glides and new notes begin this long before the change
PAD_ATTACK = 0.9
PAD_RELEASE = 0.6        # a note left by a leap fades over this,
PAD_RELEASE_LEAD = 0.4   # starting this long before the change


def pad_note(ev):
    """One pad line: three detuned soft saws (left, centre, right) with slow
    drift, following a MIDI pitch curve that glides.

    A line that never leaps is a whole loop long and has no envelope; its
    frequencies are scaled (by under 0.1 cent) to whole cycles over the
    loop, as are its drift rates, so it continues through the wrap.
    """
    curve = ev["curve"]
    n = len(curve)
    rng = np.random.default_rng(ev["seed"])
    t = np.arange(n) / SR
    if ev["whole"]:
        env = np.ones(n)
    else:
        env = rise(t, PAD_ATTACK) * fall(t, n / SR - PAD_RELEASE, PAD_RELEASE)
    lo = int(np.floor(curve.min()))
    tables = np.stack([soft_saw_table(hz(m)) for m in range(lo, int(np.floor(curve.max())) + 2)])
    size = tables.shape[1] - 1
    row = np.floor(curve).astype(np.int64) - lo
    between = curve - np.floor(curve)
    base = 440.0 * 2.0 ** ((curve - 69.0) / 12.0)
    out = np.zeros((n, 2))
    for cents, pan in ((-6.0, -0.7), (0.0, 0.0), (6.0, 0.7)):
        rate = rng.uniform(0.07, 0.17)
        if ev["whole"]:
            rate = max(1, round(rate * n / SR)) / (n / SR)
        drift = 1.0 + 0.0007 * np.sin(TAU * rate * t + rng.uniform(0, TAU))
        inst = base * 2.0 ** (cents / 1200.0) * drift
        if ev["whole"]:
            cycles = np.sum(inst) / SR
            inst *= round(cycles) / cycles
        ph = (rng.uniform(0.0, 1.0) + np.cumsum(inst) / SR) % 1.0
        x = ph * size
        i0 = x.astype(np.int64)
        fr = x - i0
        lower = tables[row, i0] * (1.0 - fr) + tables[row, i0 + 1] * fr
        upper = tables[row + 1, i0] * (1.0 - fr) + tables[row + 1, i0 + 1] * fr
        wave = lower * (1.0 - between) + upper * between
        gl, gr = pan_gains(pan)
        out[:, 0] += wave * gl
        out[:, 1] += wave * gr
    return out * (env / 3.0)[:, None]


def tick_note(ev):
    """A small, soft wooden tick: two short inharmonic modes."""
    rng = np.random.default_rng(ev["seed"])
    dur = 0.16
    n = int(dur * SR)
    t = np.arange(n) / SR
    f1 = 680.0 * (1.0 + rng.normal(0.0, 0.01))
    env = rise(t, 0.0035) * fall(t, dur - 0.03, 0.03)
    s = (np.exp(-t / 0.022) * np.sin(TAU * f1 * t)
         + 0.3 * np.exp(-t / 0.012) * np.sin(TAU * 1.58 * f1 * t))
    return stereo(soften(env * s, 3000.0) * ev["vel"], ev["pan"])


def smooth_band(freqs, lo, hi, lo_order=2, hi_order=4):
    f = np.maximum(freqs, 1e-9)
    hp = 1.0 / np.sqrt(1.0 + (lo / f) ** (2 * lo_order))
    lp = 1.0 / np.sqrt(1.0 + (f / hi) ** (2 * hi_order))
    return hp * lp


def soften(x, hi):
    """Zero-phase 4th-order low-pass of a short sound that starts and ends at zero."""
    pad = 2048
    y = np.concatenate([np.zeros(pad), x, np.zeros(pad)])
    freqs = np.fft.rfftfreq(len(y), 1.0 / SR)
    return np.fft.irfft(np.fft.rfft(y) * smooth_band(freqs, 1.0, hi, 1, 4), len(y))[pad:pad + len(x)]


def brush_note(ev):
    """A brushed swish: band-passed noise with a 22 ms rise."""
    rng = np.random.default_rng(ev["seed"])
    dur = 0.24
    n = int(dur * SR)
    pad = 4096
    noise = rng.standard_normal(n + 2 * pad)
    freqs = np.fft.rfftfreq(len(noise), 1.0 / SR)
    y = np.fft.irfft(np.fft.rfft(noise) * smooth_band(freqs, 900.0, 3600.0), len(noise))[pad:pad + n]
    y /= np.sqrt(np.mean(y ** 2))
    t = np.arange(n) / SR
    env = rise(t, 0.022) * np.exp(-np.maximum(t - 0.022, 0.0) / 0.06) * fall(t, dur - 0.04, 0.04)
    return stereo(env * y * ev["vel"], ev["pan"])


RENDER = {"ep": ep_note, "mallet": mallet_note, "bass": bass_note, "pad": pad_note,
          "tick": tick_note, "brush": brush_note}
KIND_STEM = {"ep": "ep", "mallet": "mallet", "bass": "bass", "pad": "pad",
             "tick": "perc", "brush": "perc"}
STEMS = ("ep", "mallet", "bass", "pad", "perc")

# Stem levels (RMS dBFS before the master gain), reverb sends, wet level.
TARGET_DB = {"ep": -25.5, "mallet": -27.0, "bass": -27.0, "pad": -31.0, "perc": -40.0}
SEND = {"ep": 0.30, "mallet": 0.45, "bass": 0.0, "pad": 0.55, "perc": 0.25}
WET_DB = -29.5


# ------------------------------------------------------------------ harmony

def seg(bar, chord, beat=0.0, beats=4.0):
    bass, upper, name = chord
    return {"bar": bar, "beat": beat, "beats": beats, "bass": PC[bass],
            "pcs": [PC[x] for x in upper.split()], "name": name}


A = [("F", "A C E G", "Fmaj9"), ("F", "G B D E", "G/F"), ("E", "G B D A", "Em11"),
     ("A", "C E G B", "Am9"), ("D", "F A C E", "Dm9"), ("Bb", "D F A C", "Bbmaj9"),
     ("G", "Bb D F A", "Gm9"), ("C", "Bb D F G", "C9sus4")]
C13 = ("C", "Bb D E A", "C13")
B = [("Bb", "D F A C", "Bbmaj9"), ("C", "E G A D", "C6/9"), ("A", "C E G D", "Am11"),
     ("D", "F A C E", "Dm9"), ("G", "Bb D F A", "Gm9"), ("Eb", "G Bb D F", "Ebmaj9"),
     ("D", "F A C E", "Dm9")]
CODA = [("F", "A C E G", "Fmaj9"), ("Bb", "D F A C", "Bbmaj9"), ("Bb", "Db F G C", "Bbm6/9"),
        ("C", "Bb D F G", "C9sus4")]


def game_progression():
    p = [seg(b, c) for b, c in enumerate(A)]
    p += [seg(8 + b, c) for b, c in enumerate(A[:7])]
    p += [seg(15, A[7], 0.0, 2.0), seg(15, C13, 2.0, 2.0)]
    p += [seg(16 + b, c) for b, c in enumerate(B)]
    p += [seg(23, A[7], 0.0, 2.0), seg(23, C13, 2.0, 2.0)]
    p += [seg(24 + b, c) for b, c in enumerate(CODA)]
    return p


def menu_progression():
    return [seg(b, c) for b, c in enumerate(A)] + [seg(8 + b, c) for b, c in enumerate(CODA)]


EP_WHOLE_COST = 5.0      # a whole tone between piano neighbours below EP_WHOLE_BELOW
EP_WHOLE_BELOW = 72      # C5
PAD_WHOLE_COST = 0.6     # a pad note a whole tone from a piano note below C5
EP_SPAN = 16             # widest piano voicing, semitones


def left_hand(pc):
    return 48 + pc if pc <= 6 else 36 + pc  # G2 (43) .. F#3 (54)


def rub(a, b):
    """A minor second, or a minor ninth (where the lower note's second
    harmonic is a semitone from the upper note)."""
    return abs(a - b) in (1, 13)


def ep_rule(index, seg, v):
    """Piano: no minor second or minor ninth between any two notes, the
    left hand's root included; nothing closer than a minor third above that
    root, no second at the bottom below A3; a whole tone between neighbours
    below C5 costs as much as five semitones of movement."""
    full = [left_hand(seg["bass"])] + list(v)
    if any(rub(a, b) for a, b in itertools.combinations(full, 2)):
        return None
    if v[0] - full[0] < 3 or (v[1] - v[0] < 3 and v[0] < 57):
        return None
    return EP_WHOLE_COST * sum(1 for a, b in zip(v, v[1:]) if b - a == 2 and a < EP_WHOLE_BELOW)


def pad_rule(ep_voicings):
    """Pad: open below C5 (neighbours at least a minor third apart there),
    no minor second or minor ninth within it or against the piano and its
    left hand; a whole tone against the piano below C5 costs a little."""
    def rule(index, seg, v):
        if any(b - a < (3 if a < 72 else 2) for a, b in zip(v, v[1:])):
            return None
        ep = ep_voicings[index]
        others = [left_hand(seg["bass"])] + list(ep)
        if any(rub(a, b) for a, b in itertools.combinations(v, 2)) or any(rub(p, e) for p in v for e in others):
            return None
        return PAD_WHOLE_COST * sum(1 for p in v for e in ep if abs(p - e) == 2 and min(p, e) < 72)
    return rule


def voice_lead(prev, seg, lo, hi, center, span, rule, index, omit):
    """The allowed voicing in [lo, hi] that moves least from `prev`: as many
    notes as `prev`, from the segment's pitch classes, leaving out at most
    `omit` of them (each at a cost) by doubling others at the octave."""
    pcs = set(seg["pcs"])
    notes = [p for p in range(lo, hi + 1) if p % 12 in pcs]
    best = None
    for v in itertools.combinations(notes, len(prev)):
        missing = len(pcs) - len({p % 12 for p in v})
        if missing > omit or v[-1] - v[0] > span:
            continue
        extra = rule(index, seg, v)
        if extra is None:
            continue
        cost = sum(abs(a - b) for a, b in zip(v, prev)) + extra + 1.5 * missing
        cost += 0.15 * abs(sum(v) / len(v) - center)
        if best is None or cost < best[0]:
            best = (cost, list(v))
    if best is None:
        raise ValueError(f"no voicing of {seg['name']} in [{lo}, {hi}]")
    return best[1]


def voice_progression(prog, lo, hi, start, span, rule, omit=0):
    """Voice-lead the progression twice around, so the wrap is led too."""
    center = sum(start) / len(start)
    prev = start
    for _ in range(2):
        voicings = []
        for i, s in enumerate(prog):
            prev = voice_lead(prev, s, lo, hi, center, span, rule, i, omit)
            voicings.append(prev)
    return voicings


def bass_midi(pc):
    return 36 + pc if pc <= 6 else 24 + pc  # G1 (31) .. F#2 (42)


# -------------------------------------------------------------- arrangement

class Song:
    def __init__(self, name, bars, prog, seed):
        self.name = name
        self.bars = bars
        self.L = int(round(bars * BAR * SR))
        self.prog = prog
        self.rng = np.random.default_rng(seed)
        self.seed = seed * 100000
        self.events = []
        self.ep_voicings = voice_progression(prog, 52, 72, [57, 60, 64, 67], EP_SPAN, ep_rule)
        self.pad_voicings = voice_progression(prog, 58, 79, [64, 67, 69, 72], 19,
                                              pad_rule(self.ep_voicings), omit=1)
        self.pad_levels = [0.5] * bars
        self.sections = []

    # timing and dynamics, humanized
    def when(self, bar, beat, jitter=0.007):
        b = beat + (SWING if abs(beat % 1.0 - 0.5) < 1e-9 else 0.0)
        j = float(np.clip(self.rng.normal(0.0, jitter), -2.5 * jitter, 2.5 * jitter))
        return bar * BAR + b * BEAT + j

    def vel(self, v, spread=0.06):
        return float(np.clip(v * (1.0 + self.rng.normal(0.0, spread)), 0.05, 1.0))

    def add(self, kind, t, **params):
        self.seed += 1
        self.events.append(dict(kind=kind, t=t, seed=self.seed, **params))

    def segment_at(self, bar, beat):
        x = bar * 4 + beat + 1e-6
        for i, s in enumerate(self.prog):
            if s["bar"] * 4 + s["beat"] <= x < s["bar"] * 4 + s["beat"] + s["beats"]:
                return i
        raise ValueError((bar, beat))

    # instruments
    def strike(self, bar, beat, which, vel, beats, roll=0.016):
        """A rolled chord; a full strike adds the root an octave above the bass.
        Its notes are let go EP_CLEAR before the next chord, so their damper
        fall ends EP_RELEASE - EP_CLEAR after the change."""
        index = self.segment_at(bar, beat)
        seg = self.prog[index]
        voicing = self.ep_voicings[index]
        notes = {"all": [left_hand(seg["bass"])] + voicing, "top2": voicing[-2:], "top3": voicing[-3:]}[which]
        change = (seg["bar"] * 4 + seg["beat"] + seg["beats"]) * BEAT
        t0 = self.when(bar, beat)
        for i, m in enumerate(notes):
            r = roll * (0.7 + 0.6 * self.rng.uniform())
            top = i == len(notes) - 1
            v = self.vel(vel) * (1.05 if top else 0.82 if m < 55 else 0.9)
            hold = min(beats * BEAT - i * r, change - EP_CLEAR - (t0 + i * r))
            self.add("ep", t0 + i * r, f=hz(m), vel=min(v, 1.0), hold=hold,
                     pan=float(np.clip((m - 62) / 24.0, -0.3, 0.3)) - 0.08)

    def ep_melody(self, bar, beat, name, vel, beats):
        m = midi(name)
        self.add("ep", self.when(bar, beat), f=hz(m), vel=self.vel(vel), hold=beats * BEAT - EP_LIFT,
                 pan=float(np.clip((m - 70) / 24.0, -0.3, 0.3)))

    def mallet(self, bar, beat, name, vel, echo=True):
        m = midi(name)
        t = self.when(bar, beat)
        v = self.vel(vel)
        p = float(np.clip(0.15 + (m - 72) / 30.0, -0.35, 0.5))
        self.add("mallet", t, f=hz(m), vel=v, pan=p, bright=1.0)
        if echo and self.echo_fits(m, t + 1.5 * BEAT):
            self.add("mallet", t + 1.5 * BEAT, f=hz(m), vel=v * 0.26, pan=-0.35, bright=0.45)

    def echo_fits(self, m, t):
        """False when an echo would land a semitone from the chord it rings over."""
        t %= self.bars * BAR
        seg = self.prog[self.segment_at(int(t // BAR), (t % BAR) / BEAT)]
        tones = set(seg["pcs"]) | {seg["bass"]}
        return m % 12 in tones or all((m - pc) % 12 not in (1, 11) for pc in tones)

    def bass(self, bar, beat, name, beats, vel):
        self.add("bass", self.when(bar, beat, 0.004), f=hz(bass_midi(PC[name])),
                 vel=self.vel(vel, 0.04), hold=beats * BEAT)

    def pads(self, levels):
        """Each pad voice is one held line through the chords. A step of one
        or two semitones glides (PAD_GLIDE, from PAD_LEAD before the change);
        a larger leap ends the note, releasing over PAD_RELEASE from
        PAD_RELEASE_LEAD before the change, and starts a new one. A voice
        that never leaps is one note around the whole loop. `levels` (one
        per bar) shape the pad stem."""
        L = self.L
        count = len(self.prog)
        times = [int(round((s["bar"] * BAR + s["beat"] * BEAT) * SR)) for s in self.prog]
        lead = int(round(PAD_LEAD * SR))
        glide = rise(np.arange(int(round(PAD_GLIDE * SR))) / SR, PAD_GLIDE)
        tail = int(round((PAD_RELEASE - PAD_RELEASE_LEAD) * SR))
        self.pad_levels = list(levels)
        for voice in range(len(self.pad_voicings[0])):
            pitches = [v[voice] for v in self.pad_voicings]
            curve = np.empty(L)
            for k in range(count):
                curve[times[k]:times[k + 1] if k + 1 < count else L] = pitches[k]
            leaps = []
            for k in range(count):
                step = pitches[k] - pitches[k - 1]
                if abs(step) > 2:
                    leaps.append(k)
                elif step:
                    at = (times[k] - lead + np.arange(len(glide))) % L
                    curve[at] = pitches[k - 1] + step * glide
            if not leaps:
                self.add("pad", 0.0, curve=curve, whole=True)
                continue
            for j, k in enumerate(leaps):
                nxt = leaps[(j + 1) % len(leaps)]
                start = times[k] - lead
                change = times[nxt] if nxt > k else times[nxt] + L
                part = curve[(start + np.arange(change + tail - start)) % L]
                part[:lead] = pitches[k]
                part[change - start:] = pitches[nxt - 1]
                self.add("pad", start / SR, curve=part, whole=False)

    def tick(self, bar, beat, vel):
        self.add("tick", self.when(bar, beat, 0.005), vel=self.vel(vel, 0.1), pan=-0.25)

    def brush(self, bar, beat, vel):
        self.add("brush", self.when(bar, beat, 0.005), vel=self.vel(vel, 0.12), pan=0.3)


def notes_from(text):
    """'bar beat note vel [beats]' entries separated by commas."""
    out = []
    for item in text.replace("\n", " ").split(","):
        parts = item.split()
        if parts:
            out.append((int(parts[0]), float(parts[1]), parts[2], float(parts[3]),
                        float(parts[4]) if len(parts) > 4 else None))
    return out


A1_MALLET = notes_from("""
0 1.0 C5 .62, 0 1.5 D5 .52, 0 2.0 E5 .68, 0 3.0 G5 .56, 0 3.5 E5 .50,
1 0.0 D5 .64, 1 1.5 B4 .52, 1 2.0 G4 .60,
2 2.0 E5 .55, 2 2.5 D5 .48, 2 3.0 B4 .54,
3 0.0 C5 .60, 3 3.5 G4 .45,
4 0.0 A4 .60, 4 0.5 C5 .52, 4 1.0 D5 .60, 4 2.0 F5 .66, 4 3.0 E5 .55,
5 0.0 D5 .62, 5 1.5 C5 .50, 5 2.0 A4 .56,
6 1.0 Bb4 .50, 6 1.5 A4 .46, 6 2.0 F4 .54, 6 3.0 G4 .50,
7 1.0 G4 .48, 7 2.0 Bb4 .50, 7 3.0 C5 .54, 7 3.5 D5 .48
""")

A2_MALLET = notes_from("""
8 0.0 E5 .70, 8 1.0 C5 .55, 8 1.5 D5 .52, 8 2.0 E5 .64, 8 3.0 A5 .60, 8 3.5 G5 .54,
9 0.0 D5 .64, 9 0.5 E5 .50, 9 1.0 D5 .55, 9 1.5 B4 .56, 9 2.5 G4 .50, 9 3.0 A4 .48,
10 2.0 B4 .52, 10 2.5 D5 .55, 10 3.0 E5 .60, 10 3.5 G5 .56,
11 0.0 E5 .64, 11 1.0 C5 .52, 11 3.5 A4 .46,
12 0.0 C5 .62, 12 0.5 D5 .54, 12 1.0 F5 .62, 12 2.0 A5 .62, 12 3.0 G5 .54,
13 0.0 F5 .60, 13 1.0 E5 .56, 13 1.5 D5 .50, 13 2.0 C5 .58,
14 0.0 D5 .56, 14 1.0 Bb4 .50, 14 2.0 A4 .52, 14 3.0 F4 .48,
15 0.0 G4 .50, 15 1.5 A4 .46, 15 2.0 Bb4 .52, 15 3.0 E5 .50
""")

B_EP_MELODY = notes_from("""
16 0.0 D5 .62 1.5, 16 1.5 E5 .52 0.5, 16 2.0 F5 .60 2.0,
17 0.0 E5 .58 1.0, 17 1.0 D5 .52 1.0, 17 2.0 A4 .55 2.0,
18 0.0 C5 .58 1.5, 18 1.5 D5 .50 0.5, 18 2.0 E5 .60 2.0,
19 0.0 F5 .58 1.0, 19 1.0 E5 .52 1.0, 19 2.0 C5 .55 2.0,
20 0.0 D5 .60 1.5, 20 1.5 F5 .52 0.5, 20 2.0 A5 .62 2.0,
21 0.0 G5 .60 2.0, 21 2.0 F5 .54 1.0, 21 3.0 D5 .50 1.0,
22 0.0 E5 .58 2.0, 22 2.0 C5 .52 1.0, 22 3.0 A4 .50 1.0,
23 0.0 Bb4 .52 2.0
""")

B_MALLET = notes_from("""
16 3.0 A5 .44, 16 3.5 F5 .40,
17 3.0 G5 .44, 17 3.5 E5 .40,
18 2.5 A5 .40, 18 3.0 G5 .42, 18 3.5 D5 .38,
19 3.5 A4 .40,
20 3.0 D5 .40, 20 3.5 F5 .40,
21 3.5 Bb4 .40,
22 1.0 G5 .40, 22 1.5 F5 .38,
23 1.0 C5 .48, 23 1.5 D5 .44, 23 2.0 E5 .52, 23 3.0 G5 .50
""")

CODA_MALLET = notes_from("""
0 0.0 G5 .50, 0 1.0 E5 .44, 0 2.0 C5 .46, 0 3.0 A4 .42,
1 0.0 F5 .48, 1 1.0 D5 .42, 1 2.0 C5 .44, 1 3.0 A4 .40,
2 0.0 G5 .46, 2 1.0 F5 .42, 2 2.0 Db5 .44, 2 3.0 Bb4 .40,
3 0.0 D5 .44, 3 1.0 C5 .40, 3 2.0 Bb4 .40, 3 3.0 G4 .38, 3 3.5 A4 .42
""")


def arrange_game():
    s = Song("music", 28, game_progression(), seed=7101)
    s.sections = [(0, "A1"), (8, "A2"), (16, "B"), (24, "coda")]

    # Electric piano comping: sparse in A1, syncopated in A2, held in B and the coda.
    for bar in range(28):
        if bar < 8:
            pattern = ([(0.0, "all", 0.50, 3.7)] if bar % 2 == 0
                       else [(0.0, "all", 0.47, 2.4), (2.5, "top2", 0.34, 1.4)])
        elif bar < 16:
            if bar == 15:
                pattern = [(0.0, "all", 0.48, 1.9), (2.0, "all", 0.42, 1.9)]
            elif bar % 2 == 0:
                pattern = [(0.0, "all", 0.50, 1.4), (1.5, "top3", 0.38, 2.3)]
            else:
                pattern = [(0.0, "all", 0.48, 2.4), (2.5, "top2", 0.36, 0.9), (3.5, "top2", 0.30, 0.45)]
        elif bar < 24:
            pattern = ([(0.0, "all", 0.44, 3.8)] if bar != 23
                       else [(0.0, "all", 0.44, 1.9), (2.0, "all", 0.40, 1.9)])
        else:
            pattern = [(0.0, "all", 0.40, 3.9)]
            if bar == 26:
                pattern.append((2.5, "top2", 0.28, 1.3))
        for beat, which, vel, beats in pattern:
            s.strike(bar, beat, which, vel, beats, roll=0.032 if bar >= 24 else 0.016)

    for bar, beat, name, vel, beats in B_EP_MELODY:
        s.ep_melody(bar, beat, name, vel, beats)

    # The melody rests: A1 states each two-bar phrase and leaves the next
    # two bars to the chords (bar 7 keeps its pickup into A2), and the coda
    # is chords alone but for the pickup into the top of the loop.
    a1 = [n for n in A1_MALLET if n[0] in (0, 1, 4, 5) or (n[0] == 7 and n[1] >= 3.0)]
    for bar, beat, name, vel, _ in a1 + A2_MALLET + B_MALLET:
        s.mallet(bar, beat, name, vel)
    for bar, beat, name, vel, _ in CODA_MALLET:
        if bar == 3 and beat >= 3.0:
            s.mallet(24 + bar, beat, name, vel)

    # Bass: whole notes in A1, roots with a fifth in A2, half notes in B, long in the coda.
    bass = [(0, 0, "F", 7.6, .8), (2, 0, "E", 3.8, .8), (3, 0, "A", 3.8, .8), (4, 0, "D", 3.8, .8),
            (5, 0, "Bb", 3.8, .8), (6, 0, "G", 3.8, .8), (7, 0, "C", 3.6, .8),
            (8, 0, "F", 2.4, .8), (8, 3, "C", 0.9, .6), (9, 0, "F", 3.6, .75),
            (10, 0, "E", 2.4, .8), (10, 3, "B", 0.9, .6), (11, 0, "A", 2.4, .8), (11, 3, "E", 0.9, .6),
            (12, 0, "D", 2.4, .8), (12, 3, "A", 0.9, .6), (13, 0, "Bb", 3.6, .8),
            (14, 0, "G", 2.4, .8), (14, 3, "D", 0.9, .6), (15, 0, "C", 1.9, .8), (15, 2, "C", 1.7, .65)]
    for bar, root, fifth in ((16, "Bb", "F"), (17, "C", "G"), (18, "A", "E"), (19, "D", "A"), (20, "G", "D")):
        bass += [(bar, 0, root, 1.9, .8), (bar, 2, fifth, 1.9, .62)]
    bass += [(21, 0, "Eb", 3.6, .8), (22, 0, "D", 3.6, .8), (23, 0, "C", 1.9, .8), (23, 2, "C", 1.7, .65),
             (24, 0, "F", 3.7, .7), (25, 0, "Bb", 7.4, .7), (27, 0, "C", 3.8, .7)]
    for bar, beat, name, beats, vel in bass:
        s.bass(bar, beat, name, beats, vel)

    s.pads([0.55] * 8 + [0.42] * 8 + [0.48] * 8 + [0.65] * 4)

    # Wooden tick and brush, only in A2 and B, and quiet.
    for bar in range(8, 24):
        a2 = bar < 16
        ramp = 0.7 if bar == 8 else 1.0
        for beat in (1.0, 3.0):
            s.tick(bar, beat, (0.5 if a2 else 0.42) * ramp)
        if bar in (9, 11, 13, 17, 21):
            s.tick(bar, 3.5, 0.22)
        for e in range(8):
            beat = e * 0.5
            off = e % 2 == 1
            if not a2 and not off:
                continue
            s.brush(bar, beat, (0.36 if off else 0.22) * (0.85 if not a2 else 1.0) * ramp)
    return s


def arrange_menu():
    s = Song("menu", 12, menu_progression(), seed=7202)
    s.sections = [(0, "A"), (8, "coda")]
    for bar in range(12):
        s.strike(bar, 0.0, "all", 0.42, 3.85, roll=0.03)
        if bar in (1, 5, 10):
            s.strike(bar, 2.5, "top2", 0.28, 1.3, roll=0.03)
    phrase = [n for n in A1_MALLET if n[0] in (0, 1, 4, 5)]
    phrase += [(3, 0.0, "C5", 0.55, None), (7, 2.0, "Bb4", 0.45, None), (7, 3.0, "C5", 0.48, None)]
    for bar, beat, name, vel, _ in phrase:
        s.mallet(bar, beat, name, vel * 0.92)
    for bar, beat, name, vel, _ in CODA_MALLET:
        if beat in (0.0, 2.0):
            s.mallet(8 + bar, beat, name, vel)
    for bar, beat, name, beats, vel in [(0, 0, "F", 7.6, .7), (2, 0, "E", 3.8, .7), (3, 0, "A", 3.8, .7),
                                        (4, 0, "D", 3.8, .7), (5, 0, "Bb", 3.8, .7), (6, 0, "G", 3.8, .7),
                                        (7, 0, "C", 3.6, .7), (8, 0, "F", 3.7, .65), (9, 0, "Bb", 7.4, .65),
                                        (11, 0, "C", 3.8, .65)]:
        s.bass(bar, beat, name, beats, vel)
    s.pads([0.62] * 12)
    return s


# ----------------------------------------------------------------- render

def add_into(buf, start, sig, wrap):
    n = len(buf)
    if wrap:
        pos = start % n
        done = 0
        while done < len(sig):
            take = min(len(sig) - done, n - pos)
            buf[pos:pos + take] += sig[done:done + take]
            done += take
            pos = 0
    else:
        a, b = max(start, 0), min(start + len(sig), n)
        if a < b:
            buf[a:b] += sig[a - start:b - start]


def periodic_curve(levels, L):
    """Per-bar levels interpolated through bar centres, periodic over the loop."""
    bars = len(levels)
    x = (np.arange(bars + 2) - 0.5) * (L / bars)
    y = [levels[-1]] + list(levels) + [levels[0]]
    return np.interp(np.arange(L), x, y)


def autopan(n, L, rate=1.5, depth=0.15):
    """A left-right sway whose mono sum is constant, whole cycles over the loop."""
    cycles = round(rate * L / SR)
    s = np.sin(TAU * cycles * np.arange(n) / L)
    return np.stack([1.0 + depth * s, 1.0 - depth * s], axis=1)


def render_stems(song, n, offsets, wrap):
    stems = {k: np.zeros((n, 2)) for k in STEMS}
    for ev in song.events:
        sig = RENDER[ev["kind"]](ev)
        for off in offsets:
            add_into(stems[KIND_STEM[ev["kind"]]], int(round(ev["t"] * SR)) + off, sig, wrap)
    stems["ep"] *= autopan(n, song.L)
    stems["pad"] *= np.tile(periodic_curve(song.pad_levels, song.L), n // song.L + 1)[:n, None]
    return stems


def make_ir(seed, seconds=3.4):
    """Stereo hall: decorrelated noise, decaying faster in the highs."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    lf = np.log2(np.maximum(freqs, 1.0))

    def below(edge):  # 1 below edge/sqrt2, 0 above edge*sqrt2, cosine in log-frequency
        x = np.clip((lf - (np.log2(edge) - 0.5)), 0.0, 1.0)
        return 0.5 + 0.5 * np.cos(np.pi * x)

    edges = [180.0, 700.0, 2200.0, 5000.0]
    lows = [below(e) for e in edges]
    masks = [lows[0]] + [lows[i] - lows[i - 1] for i in range(1, len(edges))] + [1.0 - lows[-1]]
    rt60 = [2.3, 2.2, 1.8, 1.2, 0.6]
    gain = [0.5, 1.0, 0.8, 0.45, 0.15]
    build = rise(t - 0.018, 0.06) * (t >= 0.018) * fall(t, seconds - 0.3, 0.3)
    ir = np.empty((n, 2))
    for ch in range(2):
        w = np.fft.rfft(rng.standard_normal(n))
        acc = np.zeros(n)
        for m, rt, g in zip(masks, rt60, gain):
            acc += g * np.fft.irfft(w * m, n) * np.exp(-6.91 * t / rt)
        ir[:, ch] = acc * build
    return ir / np.sqrt(np.sum(ir ** 2) / 2.0)


def master_eq(freqs):
    f = np.maximum(freqs, 1e-9)
    hp = 1.0 / np.sqrt(1.0 + (32.0 / f) ** 8)       # 4th-order high-pass at 32 Hz
    lp = 1.0 / np.sqrt(1.0 + (f / 9000.0) ** 4)     # 2nd-order low-pass at 9 kHz
    h = hp * lp
    h[freqs == 0] = 0.0
    return h


def mix(stems, gains, wet_gain, ir):
    """Dry plus reverb, circular over the buffer, through the master EQ."""
    n = len(stems["ep"])
    dry = np.zeros((n, 2))
    send = np.zeros((n, 2))
    for k in STEMS:
        dry += gains[k] * stems[k]
        send += gains[k] * SEND[k] * stems[k]
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    pre = smooth_band(freqs, 160.0, 6000.0, 2, 2)
    irp = np.zeros((n, 2))
    irp[:len(ir)] = ir
    eq = master_eq(freqs)[:, None]
    wet = np.fft.rfft(send, axis=0) * np.fft.rfft(irp, axis=0) * pre[:, None]
    wet = np.fft.irfft(wet * eq, n, axis=0)
    dry = np.fft.irfft(np.fft.rfft(dry, axis=0) * eq, n, axis=0)
    return dry, wet


def rms(x):
    return float(np.sqrt(np.mean(np.square(x))))


def db(x):
    return 20.0 * np.log10(max(x, 1e-12))


def levels_of(stems, ir):
    """Each instrument's gain to its target level, and the reverb's."""
    gains = {k: (10.0 ** (TARGET_DB[k] / 20.0) / rms(stems[k]) if rms(stems[k]) > 0 else 0.0) for k in STEMS}
    _, wet = mix(stems, gains, 1.0, ir)
    return gains, 10.0 ** (WET_DB / 20.0) / rms(wet)


def build(song, levels=None):
    """The loop, mixed with `levels` (the game's) or levels of its own."""
    print(f"[{song.name}] {len(song.events)} notes, {song.bars} bars, {song.L / SR:.1f} s", flush=True)
    stems = render_stems(song, song.L, [0], wrap=True)
    ir = make_ir(4242)
    if levels is None:
        levels = levels_of(stems, ir)
    gains, wet_gain = levels
    dry, wet = mix(stems, gains, 1.0, ir)
    loop = dry + wet_gain * wet
    master_gain = 10.0 ** (RMS_TARGET_DB / 20.0) / rms(loop)
    loop *= master_gain
    stem_report = {}
    for k in STEMS:
        x = gains[k] * stems[k] * master_gain
        stem_report[k] = (db(rms(x)), band_report(x)["above6k"] + db(rms(x)) - RMS_TARGET_DB if rms(x) else -999.0)
    w = wet * wet_gain * master_gain
    stem_report["reverb"] = (db(rms(w)), band_report(w)["above6k"] + db(rms(w)) - RMS_TARGET_DB)
    del stems, dry, wet

    # The same notes played continuously three times, nothing folded: the
    # middle pass must equal the loop, and the join from the second pass
    # into the third must equal the loop's end followed by its start.
    n = 3 * song.L + int(10 * SR)
    lin = render_stems(song, n, [0, song.L, 2 * song.L], wrap=False)
    ldry, lwet = mix(lin, gains, 1.0, ir)
    linear = (ldry + wet_gain * lwet) * master_gain
    del lin, ldry, lwet
    second = linear[song.L:2 * song.L]
    k = int(0.05 * SR)
    across = linear[2 * song.L - k:2 * song.L + k]
    wrapped = np.concatenate([loop[-k:], loop[:k]])
    continuity = {
        "second_pass_err_db": db(rms(second - loop)),
        "seam_100ms_err_db": db(rms(across - wrapped)),
        "seam_100ms_level_db": db(rms(wrapped)),
    }
    return loop, stem_report, continuity, levels


# ---------------------------------------------------------------- analysis

def true_peak(x, factor=4):
    """Peak of the band-limited periodic signal at 4x the rate."""
    peak = 0.0
    for ch in range(x.shape[1]):
        spec = np.fft.rfft(x[:, ch])
        up = np.fft.irfft(spec, len(x) * factor) * factor
        peak = max(peak, float(np.max(np.abs(up))))
    return peak


def band_report(x):
    mono_spec = np.abs(np.fft.rfft(x, axis=0)) ** 2
    power = mono_spec.sum(axis=1)
    freqs = np.fft.rfftfreq(len(x), 1.0 / SR)
    total = power.sum()

    def frac(lo, hi):
        return db(np.sqrt(power[(freqs >= lo) & (freqs < hi)].sum() / total))

    bands = {c: frac(c / np.sqrt(2), c * np.sqrt(2)) for c in (31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000)}
    return {"below35": frac(0.5, 35), "above6k": frac(6000, SR / 2 + 1), "above8k": frac(8000, SR / 2 + 1),
            "octaves": bands}


def windowed_rms(x, w):
    """Circular moving RMS over w samples, centred."""
    p = np.concatenate([x[-w:], x, x[:w]]) ** 2
    c = np.cumsum(np.concatenate([[0.0], p]))
    h = w // 2
    idx = np.arange(len(x)) + w
    return np.sqrt((c[idx + h] - c[idx - h]) / (2 * h))


def seam_report(x, bars):
    """How the wrap from the last sample to the first compares with the rest.

    Sample steps and a 5 ms second-difference roughness (a click would rank
    near 1.0); levels in 50 ms and 500 ms windows on both sides of the seam,
    compared with the same windows at every other bar line.
    """
    out = {}
    d = np.abs(np.diff(x, axis=0, append=x[:1]))     # d[-1] is the seam step
    out["seam_step"] = float(d[-1].max())
    out["step_p50"] = float(np.percentile(d.max(axis=1), 50))
    out["step_p999"] = float(np.percentile(d.max(axis=1), 99.9))
    mono = x.mean(axis=1)
    d2 = np.roll(mono, -1) - 2 * mono + np.roll(mono, 1)
    roughness = windowed_rms(d2, 240)
    out["seam_roughness_rank"] = float(np.mean(roughness < max(roughness[-1], roughness[0])))
    L = len(x)
    for ms in (50, 500):
        w = int(ms / 1000 * SR)
        jumps = []
        for b in range(bars):
            at = b * L // bars
            before = np.take(x, np.arange(at - w, at), axis=0, mode="wrap")
            after = np.take(x, np.arange(at, at + w), axis=0, mode="wrap")
            jumps.append(db(rms(after)) - db(rms(before)))
        out[f"jump{ms}"] = jumps[0]
        out[f"jump{ms}_others"] = (float(np.min(jumps[1:])), float(np.median(jumps[1:])), float(np.max(jumps[1:])))
    return out


def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-c:a", "pcm_f32le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, 2).astype(np.float64)


def ebur128(path):
    res = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-af", "ebur128=peak=true",
                          "-f", "null", "-"], capture_output=True, text=True)
    text = res.stderr[res.stderr.rfind("Summary:"):]
    i = re.search(r"I:\s+(-?[\d.]+) LUFS", text)
    p = re.search(r"Peak:\s+(-?[\d.]+) dBFS", text)
    return (float(i.group(1)) if i else None, float(p.group(1)) if p else None)


# ------------------------------------------------------------------ output

def write_wav_float(path, x):
    data = np.ascontiguousarray(x, dtype="<f4").tobytes()
    fmt = struct.pack("<HHIIHHH", 3, 2, SR, SR * 2 * 4, 8, 32, 0)
    body = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"fact" + struct.pack("<I", 4) + struct.pack("<I", len(x))
            + b"data" + struct.pack("<I", len(data)) + data)
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def encode_ogg(wav, ogg, limit_bytes):
    """Highest of a few Vorbis qualities whose file fits the size limit."""
    for q in (5, 4, 3, 2, 1):
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                        "-map_metadata", "-1", "-c:a", "libvorbis", "-q:a", str(q),
                        "-fflags", "+bitexact", "-flags:a", "+bitexact", str(ogg)], check=True)
        if ogg.stat().st_size <= limit_bytes:
            return q
    return q


def write_png(path, rgb):
    h, w, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


STOPS = [(0.0, (0, 0, 4)), (0.2, (40, 11, 84)), (0.4, (101, 21, 110)), (0.6, (159, 42, 99)),
         (0.75, (212, 72, 66)), (0.88, (245, 125, 21)), (1.0, (252, 255, 164))]


def colormap(v):
    xs = [s[0] for s in STOPS]
    return np.stack([np.interp(v, xs, [s[1][c] for s in STOPS]) for c in range(3)], axis=-1).astype(np.uint8)


def spectrogram_png(path, x, width, height, win, marks=(), seam=None, floor_db=-100.0,
                    fmin=25.0, fmax=20000.0):
    """Log-frequency spectrogram (top 20 kHz, bottom 25 Hz) over a level strip.

    Dotted guides: green 35 Hz, cyan 6 kHz, white 8 kHz; grey ticks on the
    left at 100 Hz, 1 kHz and 10 kHz. White ticks on top mark the times in
    `marks` (seconds); a seam is marked by ticks top and bottom only. The
    strip below draws each column's RMS from -60 to 0 dBFS (white), with
    dotted lines at -22 (green) and -3 dBFS (red).
    """
    mono = x.mean(axis=1)
    n = len(mono)
    hop = max(1, (n - win) // (width - 1))
    frames = np.lib.stride_tricks.sliding_window_view(mono, win)[::hop][:width]
    spec = np.abs(np.fft.rfft(frames * np.hanning(win), axis=1))
    level = 20 * np.log10(np.sqrt(np.mean(frames ** 2, axis=1)) + 1e-9)
    sdb = 20 * np.log10(spec + 1e-12)
    sdb -= sdb.max()
    rows = fmin * (fmax / fmin) ** (np.arange(height)[::-1] / (height - 1))
    bins = rows / (SR / win)
    i0 = np.clip(np.floor(bins).astype(int), 0, spec.shape[1] - 2)
    w = np.clip(bins - i0, 0.0, 1.0)
    img = (sdb[:, i0] * (1 - w) + sdb[:, i0 + 1] * w).T
    rgb = colormap(np.clip((img - floor_db) / -floor_db, 0.0, 1.0))

    def row_of(f):
        return int(round((height - 1) * (1 - np.log(f / fmin) / np.log(fmax / fmin))))

    for f, color in ((35.0, (80, 255, 120)), (6000.0, (80, 220, 255)), (8000.0, (255, 255, 255))):
        rgb[row_of(f), ::6] = color
    for f in (100.0, 1000.0, 10000.0):
        rgb[row_of(f), :10] = (170, 170, 170)
    strip = np.full((90, width, 3), 18, dtype=np.uint8)
    for ref, color in ((-22.0, (80, 255, 120)), (-3.0, (255, 90, 90))):
        strip[int(round((0 - ref) / 60 * 89)), ::5] = color
    ys = np.clip(np.round((0 - level) / 60 * 89).astype(int), 0, 89)
    strip[ys, np.arange(len(ys))] = 255
    rgb = np.concatenate([rgb, np.full((4, width, 3), 60, dtype=np.uint8), strip], axis=0)
    col_seconds = hop / SR
    for m in marks:
        c = int(round(m / col_seconds))
        if 0 <= c < width:
            rgb[:10, c] = 255
    if seam is not None:
        c = int(round((seam - win / 2 / SR) / col_seconds))
        if 0 <= c < width:
            rgb[:14, max(c - 1, 0):c + 2] = (255, 255, 0)
            rgb[-14:, max(c - 1, 0):c + 2] = (255, 255, 0)
    write_png(path, rgb)


def waveform_png(path, x, seam_index, width=1200, height=360):
    """Both channels, one sample per column, the seam at the yellow ticks."""
    img = np.full((height, width, 3), 16, dtype=np.uint8)
    half = height // 2
    peak = max(float(np.max(np.abs(x))), 1e-9)
    for ch, color, top in ((0, (120, 200, 255), 0), (1, (255, 170, 90), half)):
        mid = top + half // 2
        img[mid, :] = 50
        ys = np.clip(np.round(mid - x[:width, ch] / peak * (half // 2 - 4)).astype(int), top, top + half - 1)
        for c in range(width):
            a, b = sorted((ys[c], ys[c - 1] if c else ys[c]))
            img[a:b + 1, c] = color
    img[:10, seam_index] = (255, 255, 0)
    img[-10:, seam_index] = (255, 255, 0)
    write_png(path, img)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fmt_seam(s):
    o50, o500 = s["jump50_others"], s["jump500_others"]
    return (f"step {s['seam_step']:.5f} (median step {s['step_p50']:.5f}, 99.9% {s['step_p999']:.5f}); "
            f"roughness rank {s['seam_roughness_rank']:.2f}; level change across the seam "
            f"{s['jump50']:+.1f} dB in 50 ms windows (other bar lines {o50[0]:+.1f}/{o50[1]:+.1f}/{o50[2]:+.1f} "
            f"min/median/max), {s['jump500']:+.1f} dB in 500 ms ({o500[0]:+.1f}/{o500[1]:+.1f}/{o500[2]:+.1f})")


def produce(song, limit_bytes, levels=None):
    loop, stems, continuity, levels = build(song, levels)
    name = song.name
    wav, ogg = HERE / f"{name}.wav", HERE / f"{name}.ogg"
    write_wav_float(wav, loop)
    q = encode_ogg(wav, ogg, limit_bytes)
    dec = decode(ogg)

    print("  stems, RMS dBFS in the master (and their energy above 6 kHz, dB of the master's total): "
          + ", ".join(f"{k} {v[0]:.1f} ({v[1]:.0f})" for k, v in stems.items()))
    print(f"  continuity vs a continuous three-pass render: middle pass error {continuity['second_pass_err_db']:.1f} dBFS; "
          f"100 ms across the seam error {continuity['seam_100ms_err_db']:.1f} dBFS "
          f"(signal there {continuity['seam_100ms_level_db']:.1f} dBFS)")
    for label, x in (("master", loop), ("ogg", dec)):
        bands = band_report(x)
        print(f"  {label}: {len(x)} frames ({len(x) / SR:.3f} s), peak {db(float(np.max(np.abs(x)))):.2f} dBFS, "
              f"true peak {db(true_peak(x)):.2f} dBTP, RMS {db(rms(x)):.2f} dBFS "
              f"(L {db(rms(x[:, 0])):.2f}, R {db(rms(x[:, 1])):.2f}), DC {np.mean(x, axis=0).round(7).tolist()}")
        print(f"    energy below 35 Hz {bands['below35']:.1f} dB, above 6 kHz {bands['above6k']:.1f} dB, "
              f"above 8 kHz {bands['above8k']:.1f} dB of the total")
        print("    octave bands (dB of total): " + ", ".join(f"{int(c) if c >= 63 else c}: {v:.1f}"
                                                       for c, v in bands["octaves"].items()))
        print(f"    seam: {fmt_seam(seam_report(x, song.bars))}")
        if label == "ogg":
            m = min(len(x), len(loop))
            err = x[:m] - loop[:m]
            edge = int(0.1 * SR)
            print(f"    vs master: error {db(rms(err)):.1f} dBFS overall, first 100 ms {db(rms(err[:edge])):.1f}, "
                  f"last 100 ms {db(rms(err[-edge:])):.1f}; correlation "
                  f"{float(np.sum(x[:m] * loop[:m]) / np.sqrt(np.sum(x[:m] ** 2) * np.sum(loop[:m] ** 2))):.6f}")
    lufs, tp = ebur128(ogg)
    print(f"  ffmpeg ebur128 on the ogg: {lufs} LUFS integrated, true peak {tp} dBFS")
    print(f"  {ogg.name}: libvorbis q{q}, {ogg.stat().st_size} bytes, sha256 {sha256(ogg)}")
    print(f"  {wav.name}: {wav.stat().st_size} bytes, sha256 {sha256(wav)}")

    marks = [bar * BAR for bar, _ in song.sections]
    spectrogram_png(HERE / f"{name}_spectrogram.png", dec, 1400, 520, 8192, marks=marks)
    k = 3 * SR
    around = np.concatenate([dec[-k:], dec[:k]])
    spectrogram_png(HERE / f"{name}_seam_spectrogram.png", around, 1200, 520, 2048, seam=3.0)
    w = int(0.0125 * SR)
    waveform_png(HERE / f"{name}_seam_waveform.png", np.concatenate([dec[-w:], dec[:w]]), w)
    return levels


def main():
    """Both loops; the menu is mixed with the game's instrument levels."""
    which = sys.argv[1:] or ["music", "menu"]
    game = arrange_game()
    if "music" in which:
        levels = produce(game, 1_000_000)
    else:
        levels = levels_of(render_stems(game, game.L, [0], wrap=True), make_ir(4242))
    if "menu" in which:
        produce(arrange_menu(), 500_000, levels)


if __name__ == "__main__":
    main()
