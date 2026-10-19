# dsp.py: the signal tools GRAVEWAKE's sounds are made with: numpy only.
# Everything is mono float64 at 44,100 Hz.
import subprocess, wave
import numpy as np

R = 44100


# ---------------------------------------------------------------- basics

def n_of(seconds):
    return int(round(seconds * R))


def t(seconds):
    return np.arange(n_of(seconds)) / R


def zeros(seconds):
    return np.zeros(n_of(seconds))


def db(x):
    return 10 ** (x / 20)


def peak(x):
    return float(np.max(np.abs(x))) if len(x) else 0.0


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


def normalize(x, peak_db=-1.0):
    p = peak(x)
    return x * (db(peak_db) / p) if p > 0 else x


def pad_to(x, n):
    return np.concatenate([x, np.zeros(max(0, n - len(x)))])


def mix(*layers):
    """layers: (signal, at_seconds=0, gain_db=0). Returns their sum, as long as needed."""
    parts = []
    for layer in layers:
        if isinstance(layer, np.ndarray):
            layer = (layer,)
        x = layer[0]
        at = layer[1] if len(layer) > 1 else 0.0
        g = layer[2] if len(layer) > 2 else 0.0
        parts.append((x, n_of(at), db(g)))
    n = max(i + len(x) for x, i, _ in parts)
    out = np.zeros(n)
    for x, i, g in parts:
        out[i:i + len(x)] += x * g
    return out


def fade(x, fade_in=0.002, fade_out=0.01):
    x = x.copy()
    a = min(len(x), n_of(fade_in))
    b = min(len(x), n_of(fade_out))
    if a > 0:
        x[:a] *= np.sin(np.linspace(0, np.pi / 2, a)) ** 2
    if b > 0:
        x[len(x) - b:] *= np.cos(np.linspace(0, np.pi / 2, b)) ** 2
    return x


def trim(x, thr_db=-50, pre=0.004, post=0.03, fade_in=0.002, fade_out=0.02):
    """Cuts what's quieter than thr_db below the peak off both ends, then fades the edges."""
    p = peak(x)
    if p == 0:
        return x
    hop = 64
    n = len(x) // hop
    e = np.max(np.abs(x[:n * hop].reshape(n, hop)), axis=1) if n else np.abs(x)
    on = np.nonzero(e > p * db(thr_db))[0]
    if len(on) == 0:
        return x
    a = max(0, on[0] * hop - n_of(pre))
    b = min(len(x), (on[-1] + 1) * hop + n_of(post))
    return fade(x[a:b], fade_in, fade_out)


def cut(x, t0, t1=None):
    return x[n_of(t0): n_of(t1) if t1 is not None else None]


def env_points(points, seconds):
    """Piecewise-linear envelope through (time, value) points, sampled over seconds."""
    tt = t(seconds)
    ts = [p[0] for p in points]
    vs = [p[1] for p in points]
    return np.interp(tt, ts, vs)


# ---------------------------------------------------------------- noise

def white(seconds, rng):
    return rng.standard_normal(n_of(seconds)) * 0.3


def colored(seconds, rng, slope_db_oct=-3.0):
    """Noise with a spectral slope: -3 dB/oct is pink, -6 is brown."""
    n = n_of(seconds)
    if n == 0:
        return np.zeros(0)
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / R)
    f[0] = f[1] if len(f) > 1 else 1
    spec *= (f / 1000.0) ** (slope_db_oct / 6.0206)
    x = np.fft.irfft(spec, n)
    return x / (rms(x) + 1e-12) * 0.3


def crackle(seconds, rng, rate, decay=3000.0, hp=True):
    """Random clicks: Poisson impulses with random size, each a tiny decaying burst."""
    n = n_of(seconds)
    x = np.zeros(n)
    count = rng.poisson(rate * seconds)
    pos = rng.integers(0, max(1, n), count)
    amp = rng.pareto(2.5, count) * rng.choice([-1, 1], count)
    np.add.at(x, pos, amp)
    k = np.arange(int(R * 6 / decay) + 1)
    burst = np.exp(-k / R * decay) * rng.standard_normal(len(k))
    y = fftconv(x, burst)[:n]
    return highpass(y, 300) if hp else y


# ---------------------------------------------------------------- filters

def fftconv(a, b):
    n = len(a) + len(b) - 1
    m = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, m) * np.fft.rfft(b, m), m)[:n]


def spectral(x, gain_of_f):
    """Zero-phase filter: multiply the spectrum by gain_of_f(f). For beds and drones."""
    n = len(x)
    m = 1 << (n - 1).bit_length()
    pad = m - n
    X = np.fft.rfft(np.concatenate([x, np.zeros(pad)]))
    f = np.fft.rfftfreq(m, 1 / R)
    return np.fft.irfft(X * gain_of_f(f), m)[:n]


def _biquad_coeffs(kind, f0, q=0.7071, gain_db=0.0):
    f0 = min(max(f0, 5.0), R * 0.49)
    w = 2 * np.pi * f0 / R
    cw, sw = np.cos(w), np.sin(w)
    alpha = sw / (2 * q)
    A = 10 ** (gain_db / 40)
    if kind == 'lp':
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]; a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == 'hp':
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]; a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == 'bp':
        b = [alpha, 0, -alpha]; a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == 'peak':
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]; a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    elif kind == 'lowshelf':
        sa = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa)]
        a = [(A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa]
    elif kind == 'highshelf':
        sa = 2 * np.sqrt(A) * alpha
        b = [A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa)]
        a = [(A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa]
    else:
        raise ValueError(kind)
    b = np.array(b) / a[0]
    a = np.array(a) / a[0]
    return b, a


def _impulse_response(b, a, max_len=1 << 17):
    # Long enough for the poles to die to -140 dB
    poles = np.roots(a)
    r = float(np.max(np.abs(poles))) if len(poles) else 0.0
    if r >= 1:
        raise ValueError('unstable filter')
    n = max(64, min(max_len, int(np.ceil(np.log(1e-7) / np.log(max(r, 1e-9)))) + 8))
    h = np.zeros(n)
    x1 = x2 = y1 = y2 = 0.0
    b0, b1, b2 = b
    a1, a2 = a[1], a[2]
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, xi
        y2, y1 = y1, yi
        h[i] = yi
    return h


def biquad(x, kind, f0, q=0.7071, gain_db=0.0):
    """Causal RBJ biquad, applied as a convolution with its impulse response."""
    b, a = _biquad_coeffs(kind, f0, q, gain_db)
    h = _impulse_response(b, a)
    return fftconv(x, h)[:len(x)]


def lowpass(x, f0, q=0.7071, order=2):
    for _ in range(order // 2):
        x = biquad(x, 'lp', f0, q)
    return x


def highpass(x, f0, q=0.7071, order=2):
    for _ in range(order // 2):
        x = biquad(x, 'hp', f0, q)
    return x


def bandpass(x, lo, hi, order=2):
    return lowpass(highpass(x, lo, order=order), hi, order=order)


def peq(x, f0, gain_db, q=1.0):
    return biquad(x, 'peak', f0, q, gain_db)


def shelf(x, kind, f0, gain_db):
    return biquad(x, kind, f0, 0.7071, gain_db)


def svf(x, fc, q=0.7, mode='lp'):
    """Time-varying state-variable filter (TPT). fc: a number or an array per sample."""
    n = len(x)
    fc = np.broadcast_to(np.asarray(fc, dtype=float), (n,))
    g = np.tan(np.pi * np.clip(fc, 10, R * 0.45) / R)
    k = 1.0 / q
    a1 = 1 / (1 + g * (g + k))
    ic1 = ic2 = 0.0
    out = np.empty(n)
    lp = mode == 'lp'; bp = mode == 'bp'; hp = mode == 'hp'
    xs = x.tolist(); gs = g.tolist(); a1s = a1.tolist()
    for i in range(n):
        gi = gs[i]
        v3 = xs[i] - ic2
        v1 = a1s[i] * ic1 + gi * a1s[i] * v3
        v2 = ic2 + gi * v1
        ic1 = 2 * v1 - ic1
        ic2 = 2 * v2 - ic2
        if lp:
            out[i] = v2
        elif bp:
            out[i] = v1
        elif hp:
            out[i] = xs[i] - k * v1 - v2
        else:
            out[i] = xs[i] - k * v1  # notch
    return out


def stft_filter(x, gain_fn, nfft=2048, hop=512):
    """Time-varying zero-phase filter: gain_fn(time_s, f_array) -> gains for that frame."""
    n = len(x)
    win = np.hanning(nfft + 1)[:-1]
    xp = np.concatenate([np.zeros(nfft), x, np.zeros(nfft)])
    out = np.zeros(len(xp))
    norm = np.zeros(len(xp))
    f = np.fft.rfftfreq(nfft, 1 / R)
    for s in range(0, len(xp) - nfft, hop):
        tm = (s + nfft / 2 - nfft) / R
        X = np.fft.rfft(xp[s:s + nfft] * win)
        y = np.fft.irfft(X * gain_fn(tm, f), nfft)
        out[s:s + nfft] += y * win
        norm[s:s + nfft] += win ** 2
    out = out / np.maximum(norm, 1e-6)
    return out[nfft:nfft + n]


def butter_mag(f, fc, order, kind='lp'):
    r = np.maximum(f, 1e-6) / fc
    if kind == 'lp':
        return 1 / np.sqrt(1 + r ** (2 * order))
    return 1 / np.sqrt(1 + (1 / r) ** (2 * order))


def band_gain(f, center, width_oct, floor=0.0):
    """A smooth bell in log-frequency: 1 at center, half power one width_oct away."""
    d = np.log2(np.maximum(f, 1.0) / center) / max(width_oct, 1e-3)
    return floor + (1 - floor) * np.exp(-0.5 * (d * 1.1774) ** 2)


# ---------------------------------------------------------------- dynamics and color

def soft_clip(x, drive=1.0):
    return np.tanh(x * drive) / np.tanh(drive)


def limit(x, ceiling_db=-1.0, lookahead=0.003, release=0.08):
    """Look-ahead peak limiter: the gain never lets a peak above the ceiling."""
    c = db(ceiling_db)
    a = np.abs(x)
    la = max(1, n_of(lookahead))
    # running max over the look-ahead window
    pad = np.concatenate([a, np.zeros(la)])
    m = a.copy()
    for k in range(1, la + 1):
        m = np.maximum(m, pad[k:k + len(a)])
    need = np.minimum(1.0, c / np.maximum(m, 1e-12))
    # release: gain may only rise slowly; smooth with a one-pole (min-tracking)
    rel = np.exp(-1.0 / max(1, n_of(release)))
    g = np.empty_like(need)
    cur = 1.0
    nl = need.tolist()
    for i in range(len(nl)):
        v = nl[i]
        cur = v if v < cur else v + (cur - v) * rel
        g[i] = cur
    # smooth the attack so the gain change isn't a step
    k = np.hanning(2 * la + 1)
    k /= k.sum()
    gs = np.convolve(np.concatenate([g, np.full(la, g[-1])]), k, mode='same')[:len(g)]
    gs = np.minimum(gs, g)
    y = x * gs
    return np.clip(y, -c, c)


def compress(x, thr_db=-18, ratio=3.0, attack=0.005, release=0.12, makeup_db=0.0):
    hop = 64
    n = len(x)
    e = np.abs(x)
    m = len(e) // hop + 1
    blocks = np.pad(e, (0, m * hop - n)).reshape(m, hop).max(axis=1)
    lvl = 20 * np.log10(np.maximum(blocks, 1e-9))
    over = np.maximum(0, lvl - thr_db)
    gr = -over * (1 - 1 / ratio)
    # smooth gain reduction
    aa = np.exp(-hop / (attack * R)); rr = np.exp(-hop / (release * R))
    sm = np.empty_like(gr); cur = 0.0
    for i, v in enumerate(gr.tolist()):
        cur = v + (cur - v) * (aa if v < cur else rr)
        sm[i] = cur
    gain = np.interp(np.arange(n), np.arange(m) * hop + hop / 2, sm)
    return x * db(gain + makeup_db)


def saturate(x, amount=2.0, mix_wet=1.0):
    p = peak(x) or 1.0
    y = np.tanh(x / p * amount) / np.tanh(amount) * p
    return x * (1 - mix_wet) + y * mix_wet


def rasp(x, rng, amount=0.3):
    """Rough texture: amplitude jitter from fast noise, like a ragged throat."""
    j = lowpass(rng.standard_normal(len(x)), 90)
    j = j / (peak(j) + 1e-9)
    return x * (1 + amount * j)


def loudness(x):
    """Roughly LUFS: a K-like weighting, 400 ms blocks, absolute and relative gates."""
    y = shelf(highpass(x, 60), 'highshelf', 1500, 4)
    win, hop = n_of(0.4), n_of(0.1)
    if len(y) <= win:
        blocks = np.array([np.mean(y ** 2)])
    else:
        c = np.concatenate([[0.0], np.cumsum(y ** 2)])
        starts = np.arange(0, len(y) - win, hop)
        blocks = (c[starts + win] - c[starts]) / win
    lv = 10 * np.log10(blocks + 1e-15)
    g = blocks[lv > -70]
    if len(g) == 0:
        return -70.0
    rel = 10 * np.log10(np.mean(g)) - 10
    g = blocks[(lv > -70) & (lv > rel)]
    return float(10 * np.log10(np.mean(g) + 1e-15) - 0.691)


def dc_block_loop(x, f0=20.0):
    """Removes DC and sub-audio rumble from a loop, round the circle (no seam)."""
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / R)
    return np.fft.irfft(X * butter_mag(f, f0, 2, 'hp'), len(x))


def limit_loop(x, ceiling_db=-1.0):
    """The limiter, run round a loop (three copies, the middle kept) so its gain matches across the seam."""
    n = len(x)
    y = limit(np.concatenate([x, x, x]), ceiling_db)
    return y[n:2 * n]


# ---------------------------------------------------------------- time and pitch

def resample(x, ratio):
    """Plays x ratio times as fast (pitch up by ratio), band-limited by FFT."""
    n = len(x)
    padn = n + 2048
    xp = np.concatenate([x, np.zeros(padn - n)])
    m = int(round(padn / ratio))
    X = np.fft.rfft(xp)
    k = m // 2 + 1
    if k <= len(X):
        Y = X[:k].copy()
    else:
        Y = np.concatenate([X, np.zeros(k - len(X), dtype=complex)])
    y = np.fft.irfft(Y, m) * (m / padn)
    return y[:int(round(n / ratio))]


def pitch(x, semitones):
    return resample(x, 2 ** (semitones / 12))


def varispeed(x, ratio_curve):
    """Reads x at a speed that changes over time (ratio per output sample, a function of 0..1)."""
    out_len = int(len(x) / np.mean(ratio_curve(np.linspace(0, 1, 512))))
    r = ratio_curve(np.linspace(0, 1, out_len))
    pos = np.cumsum(r)
    pos = pos[pos < len(x) - 1]
    return np.interp(pos, np.arange(len(x)), x)


def reverse(x):
    return x[::-1].copy()


# ---------------------------------------------------------------- space

def reverb_ir(rt60=1.8, seconds=None, predelay=0.012, damp=0.5, rng=None, early=True, bright=8000.0):
    """A synthetic room/hall response: early taps, then noise that dies faster up high."""
    rng = rng if rng is not None else np.random.default_rng(7)
    seconds = seconds or min(rt60 * 1.3, 12.0)
    n = n_of(seconds)
    tt = np.arange(n) / R
    bands = [(0, 250, 1.25), (250, 1000, 1.0), (1000, 4000, 1 - 0.35 * damp), (4000, 22050, 1 - 0.65 * damp)]
    ir = np.zeros(n)
    base = rng.standard_normal(n)
    spec = np.fft.rfft(base)
    f = np.fft.rfftfreq(n, 1 / R)
    for lo, hi, k in bands:
        sel = (f >= lo) & (f < hi)
        part = np.fft.irfft(np.where(sel, spec, 0), n)
        ir += part * np.exp(-6.9 * tt / (rt60 * k))
    ir *= np.minimum(1, tt / 0.03) ** 1.5
    ir = lowpass(ir, bright)
    if early:
        for _ in range(10):
            d = rng.uniform(0.004, 0.06)
            i = n_of(d)
            if i < n:
                ir[i] += rng.uniform(-1, 1) * 3.0 * np.exp(-d * 20)
    ir = np.concatenate([np.zeros(n_of(predelay)), ir])
    return ir / (np.sqrt(np.sum(ir ** 2)) + 1e-12)


def reverb(x, rt60=1.8, wet=0.3, dry=1.0, predelay=0.012, damp=0.5, seed=7, tail=True, bright=8000.0):
    ir = reverb_ir(rt60, predelay=predelay, damp=damp, rng=np.random.default_rng(seed), bright=bright)
    w = fftconv(x, ir)
    if not tail:
        w = w[:len(x)]
    out = np.zeros(len(w))
    out[:len(x)] += x * dry
    # scale the wet so it sits at a similar loudness to the dry
    return out + w * wet


# ---------------------------------------------------------------- loops

def loopify(x, xfade):
    """Crossfades the end into the start (equal power), so the result loops without a seam."""
    n = n_of(xfade)
    body = x[:len(x) - n].copy()
    ramp = np.linspace(0, np.pi / 2, n)
    body[:n] = x[:n] * np.sin(ramp) + x[len(x) - n:] * np.cos(ramp)
    return body


def loop_seam_error(x):
    """How big the jump is across the loop point, relative to the typical sample step."""
    steps = np.abs(np.diff(x))
    return float(abs(x[0] - x[-1]) / (np.percentile(steps, 99) + 1e-12))


# ---------------------------------------------------------------- oscillators and instruments

def phase_of(freq, n):
    freq = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    return np.cumsum(freq) / R


def sine(freq, seconds, phase0=0.0):
    n = n_of(seconds)
    return np.sin(2 * np.pi * (phase_of(freq, n) + phase0))


def saw_blep(freq, seconds, phase0=0.0):
    n = n_of(seconds)
    f = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    dt = f / R
    p = (np.cumsum(dt) + phase0) % 1.0
    y = 2 * p - 1
    # PolyBLEP corrections at the wrap
    m1 = p < dt
    tt = p[m1] / dt[m1]
    y[m1] -= tt + tt - tt * tt - 1
    m2 = p > 1 - dt
    tt = (p[m2] - 1) / dt[m2]
    y[m2] -= tt * tt + tt + tt + 1
    return y


def square_blep(freq, seconds, duty=0.5):
    a = saw_blep(freq, seconds)
    b = saw_blep(freq, seconds, phase0=duty)
    return (a - b) * 0.5


def additive(freq, seconds, amps, phases=None, max_hz=18000.0):
    """Harmonic series on a (possibly moving) fundamental. amps[k] for harmonic k+1."""
    n = n_of(seconds)
    ph = phase_of(freq, n)
    f0 = float(np.max(freq)) if np.ndim(freq) else float(freq)
    out = np.zeros(n)
    for k, a in enumerate(amps):
        h = k + 1
        if a == 0 or f0 * h > max_hz:
            continue
        p0 = phases[k] if phases is not None else 0.0
        out += a * np.sin(2 * np.pi * (ph * h + p0))
    return out


def modal(freqs, amps, decays, seconds, rng=None, attack=0.0005, beat=0.0):
    """A struck object: decaying sines. decays are e-folding rates (1/s). beat splits each mode in two."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    out = np.zeros(n)
    rng = rng if rng is not None else np.random.default_rng(1)
    for f, a, d in zip(freqs, amps, decays):
        if f >= R / 2 - 200:
            continue
        ph = rng.uniform(0, 2 * np.pi)
        e = np.exp(-tt * d)
        if beat:
            df = beat * rng.uniform(0.3, 1.0)
            out += a * e * 0.5 * (np.sin(2 * np.pi * (f - df / 2) * tt + ph) + np.sin(2 * np.pi * (f + df / 2) * tt + ph * 1.7))
        else:
            out += a * e * np.sin(2 * np.pi * f * tt + ph)
    if attack > 0:
        out *= np.minimum(1, tt / attack)
    return out


def pluck(freq, seconds, rng, bright=0.5, damping=0.996, pick=0.2):
    """Karplus-Strong string, computed one period at a time (vectorized per block)."""
    exact = R / freq
    N = int(np.floor(exact - 0.5))
    n = n_of(seconds)
    # excitation: filtered noise burst with a pick-position comb
    exc = rng.uniform(-1, 1, N)
    exc = exc - np.mean(exc)
    lpk = max(1, int(1 + (1 - bright) * 6))
    if lpk > 1:
        exc = np.convolve(exc, np.ones(lpk) / lpk, mode='same')
    pp = max(1, int(N * pick))
    exc = exc - np.roll(exc, pp) * 0.6
    blocks = n // N + 2
    y = np.zeros(blocks * N + 1)
    y[1:N + 1] = exc
    for b in range(1, blocks):
        prev = y[(b - 1) * N + 1: b * N + 1]
        prev1 = y[(b - 1) * N: b * N]
        y[b * N + 1:(b + 1) * N + 1] = damping * 0.5 * (prev + prev1)
    y = y[1:n + 1 + 1]
    # retune: the loop's period is N + 0.5 samples, a little short; stretch to the exact period
    ratio = (N + 0.5) / exact
    y = np.interp(np.arange(n) * ratio, np.arange(len(y)), y, right=0)
    return y


def lute_body(x):
    """A small wooden body: a few resonances and an air mode."""
    y = x * 0.6
    for f, g, q in [(110, 4, 3.0), (260, 5, 3.5), (520, 3, 3.0), (1150, 2, 2.0), (2600, -3, 1.0)]:
        y = peq(y, f, g, q)
    return highpass(y, 70)


def recorder(freq, seconds, rng, vibrato=0.25, breath=0.07, chiff=0.5, attack=0.03, release=0.06):
    """A wooden flute: near-sine, a little odd harmonic, breath noise and a chiff at the start."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    vib_on = np.clip((tt - 0.12) / 0.2, 0, 1)
    vr = rng.uniform(4.8, 5.8)
    f = freq * (1 + vibrato * 0.006 * vib_on * np.sin(2 * np.pi * vr * tt + rng.uniform(0, 6)))
    # a slight scoop up into the note
    f = f * (1 - 0.012 * np.exp(-tt / 0.02))
    tone = additive(f, seconds, [1.0, 0.12, 0.22, 0.04, 0.07, 0.02, 0.025])
    br = colored(seconds, rng, -1.0)
    br = spectral(br, lambda ff: band_gain(ff, freq * 2.0, 1.2, 0.05)) * breath
    ch = spectral(white(seconds, rng), lambda ff: band_gain(ff, freq * 3.5, 1.0)) * np.exp(-tt / 0.018) * chiff
    e = np.minimum(1, tt / attack) * np.clip((seconds - tt) / release, 0, 1)
    return (tone + br + ch) * e


def bowed(freq, seconds, rng, bright=0.6, buzz=0.0, attack=0.06, release=0.08, vib=0.004):
    """A bowed/wheel string (hurdy-gurdy-ish): band-limited saw, body formants, optional buzz."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    f = freq * (1 + vib * np.sin(2 * np.pi * rng.uniform(4.5, 5.5) * tt) * np.clip((tt - 0.1) / 0.3, 0, 1))
    f = f * (1 + 0.002 * lowpass(rng.standard_normal(n), 20) / 0.05)
    s = saw_blep(f, seconds)
    s = s + 0.05 * lowpass(rng.standard_normal(n), 6000)  # bow noise
    s = lowpass(s, 900 + 4500 * bright)
    for fc, g, q in [(300, 5, 1.5), (900, 4, 2.0), (2200, 3, 2.0), (3500, 2, 2.5)]:
        s = peq(s, fc, g, q)
    if buzz:
        # the trompette's chien: a rattle locked to the wheel
        rate = 2 * np.pi * 9.0
        gate = (np.sin(rate * tt) > 0.3).astype(float)
        gate = lowpass(gate, 400)
        s = s * (1 - buzz * 0.4 + buzz * 0.6 * gate) + buzz * 0.15 * gate * highpass(saw_blep(freq * 2, seconds), 1500)
    e = np.minimum(1, tt / attack) * np.clip((seconds - tt) / release, 0, 1)
    return s * e


def frame_drum(seconds, rng, f0=90.0, slap=0.5, decay=14.0, tone=1.0):
    """A tabor/frame drum hit: membrane modes and a skin slap."""
    ratios = [1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50]
    amps = [1.0, 0.55, 0.35, 0.3, 0.2, 0.15, 0.12, 0.1]
    decs = [decay * (1 + 0.6 * i) for i in range(len(ratios))]
    n = n_of(seconds)
    tt = np.arange(n) / R
    # the pitch drops a little as the skin relaxes
    body = np.zeros(n)
    for r, a, d in zip(ratios, amps, decs):
        f = f0 * r * (1 + 0.08 * np.exp(-tt / 0.03))
        body += a * np.exp(-tt * d) * np.sin(2 * np.pi * np.cumsum(f) / R + rng.uniform(0, 6))
    sl = bandpass(white(seconds, rng), 400, 5000) * np.exp(-tt * 70) * slap * 3
    return body * tone + sl


def snare_tabor(seconds, rng, f0=180.0, snare=0.6):
    d = frame_drum(seconds, rng, f0, slap=0.8, decay=22)
    tt = t(seconds)
    sn = bandpass(white(seconds, rng), 1500, 9000) * np.exp(-tt * 28) * snare * 2.5
    return d + sn


def formant_gain(f, formants):
    """Sum of resonance bells: formants = [(freq, bw_hz, gain_db), ...]."""
    g = np.zeros_like(f, dtype=float)
    for fc, bw, gdb in formants:
        g += db(gdb) / (1 + ((f - fc) / (bw / 2)) ** 2)
    return g


VOWELS = {
    # bass/tenor-ish formants (Hz, bandwidth, dB)
    'ah': [(700, 110, 0), (1150, 120, -6), (2600, 160, -16), (3300, 200, -22)],
    'oh': [(450, 80, 0), (800, 100, -9), (2600, 150, -20), (3300, 200, -26)],
    'oo': [(330, 70, 0), (700, 90, -14), (2400, 140, -26), (3300, 200, -30)],
    'eh': [(500, 80, 0), (1700, 120, -8), (2500, 140, -14), (3300, 200, -20)],
    'mm': [(250, 60, 0), (1200, 300, -30), (2500, 300, -36)],
}


def voice(freq, seconds, rng, vowel='ah', vib=0.012, vib_rate=None, jitter=0.004, breath=0.04,
          attack=0.25, release=0.4, tilt=-10.0, max_hz=7000.0, vowel_to=None):
    """One sung voice: harmonics weighted by a formant envelope; drifting pitch, vibrato, breath."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    vr = vib_rate or rng.uniform(4.6, 5.6)
    vib_env = np.clip((tt - 0.2) / 0.5, 0, 1)
    drift = lowpass(rng.standard_normal(n), 3) * 30 * jitter
    f = freq * (1 + vib * vib_env * np.sin(2 * np.pi * vr * tt + rng.uniform(0, 6)) + drift)
    ph = np.cumsum(f) / R
    out = np.zeros(n)
    fa = VOWELS[vowel]
    fb = VOWELS[vowel_to] if vowel_to else None
    mix_v = np.clip(tt / seconds, 0, 1) if fb else None
    K = int(max_hz // freq)
    for k in range(1, K + 1):
        fk = freq * k
        g = formant_gain(np.array([fk]), fa)[0]
        amp = g * 10 ** (tilt * np.log2(k) / 20)
        if fb:
            g2 = formant_gain(np.array([fk]), fb)[0] * 10 ** (tilt * np.log2(k) / 20)
            amp = amp * (1 - mix_v) + g2 * mix_v
        out += amp * np.sin(2 * np.pi * k * ph + rng.uniform(0, 6))
    br = white(seconds, rng)
    br = spectral(br, lambda ff: formant_gain(ff, fa)) * breath * 3
    e = np.minimum(1, tt / attack) ** 1.5 * np.clip((seconds - tt) / release, 0, 1)
    shimmer = 1 + 0.08 * lowpass(rng.standard_normal(n), 6) / 0.02
    return (out / max(peak(out), 1e-9) + br) * e * np.clip(shimmer, 0.6, 1.4)


def choir(notes, seconds, rng, vowel='ah', voices_per=4, detune_cents=9.0, **kw):
    """Several voices on each note: a chord sung by a small, slightly ragged choir."""
    out = np.zeros(n_of(seconds))
    for note in notes:
        for v in range(voices_per):
            c = rng.uniform(-detune_cents, detune_cents)
            delay = rng.uniform(0, 0.08)
            x = voice(note * 2 ** (c / 1200), seconds - delay, rng, vowel=vowel, **kw)
            out[n_of(delay):n_of(delay) + len(x)] += x
    return out


def organ(freq, seconds, rng, stops=(1.0, 0.5, 0.35, 0.25, 0.0, 0.15, 0.0, 0.1), attack=0.08, release=0.25, chorus=0.0015):
    n = n_of(seconds)
    tt = np.arange(n) / R
    out = additive(freq, seconds, list(stops))
    if chorus:
        out += 0.6 * additive(freq * (1 + chorus), seconds, list(stops))
    wind = spectral(white(seconds, rng), lambda f: band_gain(f, freq * 4, 2.0)) * 0.02
    e = np.minimum(1, tt / attack) * np.clip((seconds - tt) / release, 0, 1)
    return (out + wind) * e


def horn(freq, seconds, rng, swell=None, attack=0.08, release=0.3, bright=1.0):
    """A brass-ish tone whose brightness follows its loudness."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    e = swell if swell is not None else np.minimum(1, tt / attack) * np.clip((seconds - tt) / release, 0, 1)
    f = freq * (1 + 0.003 * np.sin(2 * np.pi * 5.2 * tt) * np.clip((tt - 0.3) / 0.4, 0, 1))
    ph = np.cumsum(f) / R
    out = np.zeros(n)
    K = int(9000 // float(np.max(freq)))
    for k in range(1, K + 1):
        # higher harmonics come in as it gets louder
        a = e ** (1 + (k - 1) * 0.45 / bright) / k ** 0.7
        out += a * np.sin(2 * np.pi * k * ph)
    return out


def bell(freq, seconds, rng, kind='church', strike=1.0, decay=1.0, beat=0.6):
    """Bells. freq is the strike note (the nominal)."""
    if kind == 'church':
        # hum, prime, tierce, quint, nominal, deciem, undeciem, duodeciem, upper octave, more
        ratios = [0.25, 0.5, 0.6, 0.75, 1.0, 1.25, 1.33, 1.5, 2.0, 2.5, 2.66, 3.0, 3.76, 4.2]
        amps = [0.55, 0.6, 0.55, 0.25, 1.0, 0.35, 0.18, 0.3, 0.25, 0.14, 0.1, 0.12, 0.07, 0.06]
        decs = [0.35, 0.55, 0.7, 1.1, 0.9, 1.8, 2.2, 2.0, 2.8, 3.6, 4.0, 4.4, 5.5, 6.5]
    elif kind == 'hand':
        ratios = [1.0, 2.04, 2.62, 3.24, 4.02, 5.1, 6.3, 7.5]
        amps = [1.0, 0.5, 0.4, 0.3, 0.25, 0.15, 0.1, 0.08]
        decs = [2.5, 4.0, 6.0, 7.0, 9.0, 12.0, 15.0, 18.0]
    elif kind == 'chime':
        ratios = [1.0, 2.0, 3.01, 4.2, 5.43, 6.8]
        amps = [1.0, 0.35, 0.18, 0.12, 0.06, 0.04]
        decs = [1.2, 2.2, 3.5, 5.0, 7.0, 9.0]
    elif kind == 'glock':
        ratios = [1.0, 2.76, 5.40, 8.93]
        amps = [1.0, 0.25, 0.08, 0.03]
        decs = [2.0, 7.0, 16.0, 30.0]
    else:
        raise ValueError(kind)
    decs = [d / decay for d in decs]
    x = modal([freq * r for r in ratios], amps, decs, seconds, rng, beat=beat)
    tt = t(seconds)
    clang = bandpass(white(seconds, rng), freq * 2, min(18000, freq * 14)) * np.exp(-tt * 60) * 0.6 * strike
    return x + clang


def thunk(seconds, rng, f0=120.0, decay=25.0, click=0.4, click_hz=2500.0):
    """A dull knock: a low body with a short pitched drop and a click of noise."""
    tt = t(seconds)
    f = f0 * (1 + 0.6 * np.exp(-tt / 0.012))
    body = np.sin(2 * np.pi * np.cumsum(f) / R) * np.exp(-tt * decay)
    ck = bandpass(white(seconds, rng), click_hz * 0.4, click_hz * 2) * np.exp(-tt * decay * 6) * click * 3
    return body + ck


def whoosh(seconds, rng, f_lo=300.0, f_hi=2500.0, peak_at=0.5, q=1.2, curve=2.0, rumble=0.0):
    """Air moving past: band noise whose band sweeps up and down with loudness."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    u = tt / seconds
    # a bump in loudness centred at peak_at
    e = np.where(u < peak_at, (u / peak_at) ** curve, ((1 - u) / (1 - peak_at)) ** (curve * 0.8))
    fc = f_lo + (f_hi - f_lo) * e
    x = svf(colored(seconds, rng, -2.0), fc, q, 'bp')
    x = x * e
    if rumble:
        r = lowpass(colored(seconds, rng, -4.0), 180) * e ** 1.5
        x = x / (peak(x) + 1e-9) + rumble * r / (peak(r) + 1e-9)
    return x


def fire_bed(seconds, rng, crackle_rate=14.0, roar=1.0):
    """Burning wood: a low roar of breathy noise and crackles and pops on top."""
    n = n_of(seconds)
    roar_n = colored(seconds, rng, -4.5)
    flick = 1 + 0.5 * lowpass(rng.standard_normal(n), 3) / 0.03
    roar_n = lowpass(highpass(roar_n, 60), 900) * np.clip(flick, 0.3, 2.0)
    cr = crackle(seconds, rng, crackle_rate, decay=2500)
    pops = crackle(seconds, rng, crackle_rate * 0.15, decay=600, hp=False)
    pops = bandpass(pops, 200, 3000)
    hiss = bandpass(white(seconds, rng), 2500, 9000) * 0.05 * np.clip(flick, 0.3, 2)
    return roar * roar_n / (peak(roar_n) + 1e-9) * 0.5 + cr / (peak(cr) + 1e-9) * 0.6 + pops / (peak(pops) + 1e-9) * 0.5 + hiss


def zap(seconds, rng, density=900.0, f_buzz=120.0):
    """Electric arc: a dense, randomly gated train of sharp clicks with a mains-like buzz."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    imp = np.zeros(n)
    cnt = rng.poisson(density * seconds)
    pos = rng.integers(0, n, cnt)
    np.add.at(imp, pos, rng.standard_normal(cnt))
    gate = (lowpass(rng.standard_normal(n), 25) > -0.01).astype(float)
    gate = lowpass(gate, 300)
    k = np.exp(-np.arange(200) / 18.0)
    cr = fftconv(imp, k)[:n]
    cr = highpass(cr, 1200)
    buzz = square_blep(f_buzz * (1 + 0.02 * np.sin(2 * np.pi * 7 * tt)), seconds, 0.15)
    buzz = bandpass(buzz, 200, 6000)
    return cr / (peak(cr) + 1e-9) * gate + 0.35 * buzz * gate


# ---------------------------------------------------------------- files

def load(path, t0=0.0, t1=None):
    """Decodes any audio file to mono 44.1 kHz float with ffmpeg."""
    cmd = ['ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', str(R), '-f', 'f32le', '-']
    out = subprocess.run(cmd, capture_output=True, check=True).stdout
    x = np.frombuffer(out, dtype=np.float32).astype(np.float64)
    return cut(x, t0, t1)


def _to_i16(x):
    return np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')


def save_wav(path, x):
    with wave.open(path, 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(R)
        f.writeframes(_to_i16(x).tobytes())


def save_ogg(path, x, quality=4):
    data = np.clip(x, -1, 1).astype('<f4').tobytes()
    cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(R), '-ac', '1', '-i', '-',
           '-c:a', 'libvorbis', '-q:a', str(quality), '-map_metadata', '-1',
           '-fflags', '+bitexact', '-flags:a', '+bitexact', path]  # same input, same bytes
    subprocess.run(cmd, input=data, check=True)
