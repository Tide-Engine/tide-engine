#!/usr/bin/env python3
"""make_sfx.py: builds GRAVEWAKE's sound effects and music cues.

    python -P make_sfx.py [--sources DIR] [--out DIR] [--only NAME,NAME...]
    python -P make_sfx.py --list | --check [--out DIR]

Every sound is a recipe below. Some are synthesized from nothing (numpy only);
the rest are made from CC0 recordings, each pack listed in PACKS with its page,
author, licence and the file that was downloaded. Without --sources only the
synthesized sounds are rebuilt and the others are left as they are.

--sources DIR: DIR holds one folder per PACKS key. An archive (.zip, or .7z,
which needs 7-Zip) is extracted into DIR/<key>/x/; a pack that is one sound
file sits in DIR/<key>/ under the name in its URL, with %20 written as _.
PACKS[key]['root'] is where the recipes' file names start inside DIR/<key>.

Each recipe's code is the record of how its sound was processed: which take
(file and seconds) it cut, the filters, pitch changes, layers and timing.
Then everything has DC and sub-20 Hz rumble removed and is normalized: peaks
at -1 dBFS (or a recipe's own peak), or to a loudness (music, beds, voices)
with peaks held under -1 dBFS. Mono, 44.1 kHz, written as Ogg Vorbis (q4) for
loops, music and any group with a member over a second, else 16-bit WAV.
Loops are built round the circle (events wrap, beds are crossfaded or filtered
circularly) so they repeat without a seam; --check reports each loop's seam.
CREDITS.md and tools/manifest.json are written from what each recipe used.
Needs numpy and ffmpeg (with libvorbis) on PATH.
"""
import sys, os, zlib, json, argparse, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # no __pycache__ left in the game folder
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
from dsp import *  # noqa: E402,F401,F403
import dsp  # noqa: E402

OUT_DEFAULT = os.path.dirname(HERE)

# ---------------------------------------------------------------- sources
# key: name, author, licence, page, file(s) downloaded, root inside DIR/key

OGA = 'https://opengameart.org/content/'
OGF = 'https://opengameart.org/sites/default/files/'
PACKS = {
    'kenney_rpg': dict(name='RPG Audio', author='Kenney (kenney.nl)', licence='CC0 1.0',
                       page='https://kenney.nl/assets/rpg-audio',
                       file='https://kenney.nl/media/pages/assets/rpg-audio/8e99002d76-1677590336/kenney_rpg-audio.zip', root='x/Audio'),
    'kenney_impact': dict(name='Impact Sounds', author='Kenney (kenney.nl)', licence='CC0 1.0',
                          page='https://kenney.nl/assets/impact-sounds',
                          file='https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip', root='x/Audio'),
    'oga_swishes': dict(name='Swishes Sound Pack', author='artisticdude', licence='CC0 1.0',
                        page=OGA + 'swishes-sound-pack', file=OGF + 'swishes.zip', root='x/swishes'),
    'oga_weapons_apparel': dict(name='Fantasy Weapons and Apparel SFX Library', author='Jan Schupke (vehicle)', licence='CC0 1.0',
                                page=OGA + 'fantasy-weapons-and-apparel-sfx-library', file=OGF + 'weapons-apparel.zip', root='x/sfx'),
    'oga_medieval_tex1': dict(name='Medieval Sound Effects: Weapon Textures (part 1 of 2)', author='Still North Media (posted by medicinestorm)', licence='CC0 1.0',
                              page=OGA + 'medieval-sound-effects-weapon-textures', file=OGF + 'medieval_sfx_textures_1_of_2.7z', root='x'),
    'oga_medieval_tex2': dict(name='Medieval Sound Effects: Weapon Textures (part 2 of 2)', author='Still North Media (posted by medicinestorm)', licence='CC0 1.0',
                              page=OGA + 'medieval-sound-effects-weapon-textures', file=OGF + 'medieval_sfx_textures_2_of_2.7z', root='x'),
    'oga_rpg_ad': dict(name='RPG Sound Pack', author='artisticdude', licence='CC0 1.0',
                       page=OGA + 'rpg-sound-pack', file=OGF + 'rpg_sound_pack.zip', root='x/RPG Sound Pack'),
    'rd_rpg': dict(name='80 CC0 RPG SFX', author='rubberduck', licence='CC0 1.0',
                   page=OGA + '80-cc0-rpg-sfx', file=OGF + '80-CC0-RPG-SFX_0.zip', root='x'),
    'rd_metalwood': dict(name='100 CC0 metal and wood SFX', author='rubberduck', licence='CC0 1.0',
                         page=OGA + '100-cc0-metal-and-wood-sfx', file=OGF + '100-CC0-wood-metal-SFX.zip', root='x'),
    'rd_breaking': dict(name='75 CC0 breaking/falling/hit sfx', author='rubberduck', licence='CC0 1.0',
                        page=OGA + '75-cc0-breaking-falling-hit-sfx', file=OGF + 'sfx_breaking_and_falling.zip', root='x'),
    'rd_water': dict(name='40 CC0 water/splash/slime SFX', author='rubberduck', licence='CC0 1.0',
                     page=OGA + '40-cc0-water-splash-slime-sfx', file=OGF + 'water-splash-slime-sfx.zip', root='x'),
    'rd_mud': dict(name='25 CC0 mud SFX', author='rubberduck', licence='CC0 1.0',
                   page=OGA + '25-cc0-mud-sfx', file=OGF + '25-CC0-mud-sfx.zip', root='x'),
    'rd_bang': dict(name='25 CC0 bang/firework SFX', author='rubberduck', licence='CC0 1.0',
                    page=OGA + '25-cc0-bang-firework-sfx', file=OGF + '25-CC0-bang-sfx.zip', root='x'),
    'rd_creature': dict(name='80 CC0 creature SFX', author='rubberduck', licence='CC0 1.0',
                        page=OGA + '80-cc0-creature-sfx', file=OGF + '80-CC0-creature-SFX_0.zip', root='x'),
    'rd_creature2': dict(name='80 CC0 creature SFX #2', author='rubberduck', licence='CC0 1.0',
                         page=OGA + '80-cc0-creture-sfx-2', file=OGF + '80-CC0-creature-sfx-2.zip', root='x'),
    'rd_sfx100v2': dict(name='100 CC0 SFX #2', author='rubberduck', licence='CC0 1.0',
                        page=OGA + '100-cc0-sfx-2', file=OGF + 'sfx_100_v2.zip', root='x'),
    'oga_wetsquish': dict(name='8 Wet Squish, Slurp Impacts', author='Independent.nu (posted by qubodup)', licence='CC0 1.0',
                          page=OGA + '8-wet-squish-slurp-impacts', file=OGF + 'independent_nu_ljudbank-wet_squish_slurp_impacts.7z', root='x/impsplat'),
    'oga_splatter': dict(name='2 Wooden Squish/Splatter Sequences', author='Independent.nu (posted by qubodup)', licence='CC0 1.0',
                         page=OGA + '2-wooden-squish-splatter-sequences', file=OGF + 'independent_nu_ljudbank-extreme_splatter.7z', root='x/extreme_splatter'),
    'oga_bonebreak': dict(name='Deep Bone Crack/Break SFX', author='Zane Little Music', licence='CC0 1.0',
                          page=OGA + 'deep-bone-crackbreak-sfx', file=OGF + 'deep_breaks.zip', root='x'),
    'oga_cannon': dict(name='Cannon Fire', author='Thimras', licence='CC0 1.0',
                       page=OGA + 'cannon-fire', file=OGF + 'cannon_fire.ogg', root='.'),
    'oga_zombies_ad': dict(name='Zombies Sound Pack', author='artisticdude', licence='CC0 1.0',
                           page=OGA + 'zombies-sound-pack', file=OGF + 'zombies.zip', root='x/zombies'),
    'oga_undead_moans': dict(name='Undead Moans', author='AntumDeluge', licence='CC0 1.0',
                             page=OGA + 'undead-moans', file=OGF + 'undead-moans.zip', root='x'),
    'oga_zombie_moans_ep': dict(name='Zombie Moans 01', author='Emopreben', licence='CC0 1.0',
                                page=OGA + 'zombie-moans-01-by-emopreben', file=OGF + 'zombiemoans_-_wav.zip', root='x'),
    'oga_voice_fx': dict(name='Voice Effects: Zombie, Skeleton, Monster, Human Male', author='ArcadeParty', licence='CC0 1.0',
                         page=OGA + 'zombie-skeleton-monster-voice-effects', file=OGF + 'Voice%20Effects%20Zombie-Skeleton-Monster%20Human%20Male.zip',
                         root='x/Voice Effects Zombie-Skeleton-Monster Human Male'),
    'oga_monster_growls': dict(name='16 Monster Growls', author='StarNinjas', licence='CC0 1.0',
                               page=OGA + '16-monster-growls', file=OGF + 'monster_-_starninjas.zip', root='x'),
    'oga_hungry_ghoul': dict(name='Hungry Ghoul', author='Darsycho', licence='CC0 1.0',
                             page=OGA + 'hungry-ghoul', file=OGF + 'hungry-ghoul_0.ogg', root='.'),
    'oga_zombie_moans_ds': dict(name='Zombie Moans', author='Darsycho', licence='CC0 1.0',
                                page=OGA + 'zombie-moans', file=OGF + 'darsycho__zombie-moans_0.ogg', root='.'),
    'oga_fantozzi_steps': dict(name="Fantozzi's Footsteps (Grass/Sand & Stone)", author='Fantozzi (posted by qubodup)', licence='CC0 1.0',
                               page=OGA + 'fantozzis-footsteps-grasssand-stone', file=OGF + 'Fantozzi-footsteps.7z', root='x/Fantozzi-footsteps/flac'),
    'oga_heartbeat_slow': dict(name='Heartbeat sounds (slow)', author='Bart Kelsey (bart)', licence='CC0 1.0',
                               page=OGA + 'heartbeat-sounds', file=OGF + 'heartbeat_slow_0.wav', root='.'),
    'oga_breathing': dict(name='Breathing tired', author='mikeask', licence='CC0 1.0',
                          page=OGA + 'breathing-tired', file=OGF + 'breathing%20tired.wav', root='.'),
    'oga_bottle_drink': dict(name='Liquid / Bottle / Drink Set', author='qubodup', licence='CC0 1.0',
                             page=OGA + 'liquid-bottle-drink-set', file=OGF + 'qubodup-bottle-and-drink-sounds.7z', root='x/qubodup-bottle-and-drink-sounds'),
    'oga_open_chest': dict(name='Open Chest SFX', author='oiboo', licence='CC0 1.0',
                           page=OGA + 'open-chest-sfx', file=OGF + 'open%20chest_0.wav', root='.'),
    'oga_evil_cackle': dict(name='Evil Cackle Laugh 1', author='NocturnalVanguard', licence='CC0 1.0',
                            page=OGA + 'evil-cackle-laugh-1', file=OGF + 'evil_cackle_laugh_1.ogg', root='.'),
    'oga_crow': dict(name='Crow', author='fvcalderan', licence='CC0 1.0',
                     page=OGA + 'crow', file=OGF + 'crow.ogg', root='.'),
    'oga_crow_caw': dict(name='Crow Caw', author='ZeroIsNotNull', licence='CC0 1.0',
                         page=OGA + 'crow-caw', file=OGF + 'crow_caw.wav', root='.'),
    'oga_crows': dict(name='Crows singing', author='ignasd', licence='CC0 1.0',
                      page=OGA + 'crows-singing', file=OGF + 'Crows.ogg', root='.'),
    'oga_anvil': dict(name='Hammer on anvil', author='TheMightyGlider', licence='CC0 1.0',
                      page=OGA + 'hammer-on-anvil', file=OGF + 'forger.ogg', root='.'),
    'oga_smith_hammer': dict(name="Blacksmith's hammer", author='vishwajai', licence='CC0 1.0',
                             page=OGA + 'blacksmiths-hammer', file=OGF + 'blacksmithhammer.wav', root='.'),
    'oga_fire_crackling': dict(name='Fire crackling', author='AntumDeluge', licence='CC0 1.0',
                               page=OGA + 'fire-crackling', file=OGF + 'fire-1.wav', root='.'),
    'oga_freeze': dict(name='Freeze spell', author='artisticdude', licence='CC0 1.0',
                       page=OGA + 'freeze-spell-0', file=OGF + 'freeze.wav', root='.'),
    'oga_ice_spells': dict(name='Ice spells', author='Bart Kelsey (bart)', licence='CC0 1.0',
                           page=OGA + 'ice-spells', file=OGF + 'icespells.zip', root='x'),
    'oga_firearms': dict(name='The Free Firearm Sound Library', author='The Free Firearm Sound Library project (posted by EFlynn)', licence='CC0 1.0',
                         page=OGA + 'the-free-firearm-sound-library', file=OGF + 'Prepared%20SFX%20Library.7z', root='x/Prepared SFX Library'),
    'oga_junkala_wastelands': dict(name='Post Apocalyptic Wastelands [Loop Ready] (posted as Horror Atmosphere)', author='Juhani Junkala (posted by SubspaceAudio)',
                                   licence='CC0 1.0', page=OGA + 'horror-atmosphere',
                                   file=OGF + 'Juhani%20Junkala%20-%20Post%20Apocalyptic%20Wastelands%20%5BLoop%20Ready%5D.ogg', root='.'),
    'oga_ancient_grudge': dict(name='Ancient Grudge', author='Ruskerdax', licence='CC0 1.0', page=OGA + 'ancient-grudge',
                               file=OGF + 'ruskerdax_-_ancient_grudge.mp3', root='.'),
    'oga_dark_cavern': dict(name='Dark Cavern Ambient', author='Paul Wortmann', licence='CC0 1.0', page=OGA + 'dark-cavern-ambient',
                            file=OGF + 'dark_cavern_ambient_002.ogg', root='.'),
    'oga_reload_springy': dict(name='Gun reload sounds', author='SpringySpringo', licence='CC0 1.0', page=OGA + 'gun-reload-sounds',
                               file=OGF + 'assaultriflereload1_0.wav, gunreload1.wav, shotguncock_0.wav', root='.'),
    'oga_impact_qubodup': dict(name='Impact', author='qubodup', licence='CC0 1.0', page=OGA + 'impact',
                               file=OGF + 'qubodupImpact.7z', root='x/qubodupImpact'),
    'kenney_interface': dict(name='Interface Sounds', author='Kenney (kenney.nl)', licence='CC0 1.0', page='https://kenney.nl/assets/interface-sounds',
                             file='https://kenney.nl/assets/interface-sounds', root='x/Audio'),
    'oga_fireball': dict(name='Fireball 1 (JM FX Fireball 01)', author='Julien Matthey (posted by diligentcircle)', licence='CC0 1.0',
                         page=OGA + 'fireball-1', file=OGF + '105016__julien-matthey__jm-fx-fireball-01.wav', root='.'),
}

# ---------------------------------------------------------------- registry

SOUNDS = {}


def sound(name, kind='sfx', peak_db=-1.0, layers=False, lufs=None):
    """kind: sfx, loop or music. layers: the recipe adds synthesized layers to recordings.
    lufs: set the loudness to this (then peaks no higher than -1 dBFS) instead of normalizing peaks."""
    def deco(fn):
        SOUNDS[name] = dict(fn=fn, kind=kind, peak=peak_db, layers=layers, lufs=lufs)
        return fn
    return deco


def series(base, count, **kw):
    def deco(fn):
        for i in range(1, count + 1):
            sound(f'{base}_{i}', **kw)(lambda rng, i=i: fn(rng, i))
        return fn
    return deco


class MissingSources(Exception):
    pass


SOURCES = None
_used = set()
_cache = {}


def S(key, rel='', t0=0.0, t1=None):
    """A recording from a pack: rel is the file inside the pack's root; t0..t1 in seconds."""
    if SOURCES is None:
        raise MissingSources(key)
    _used.add(key)
    p = PACKS[key]
    path = os.path.normpath(os.path.join(SOURCES, key, p['root'], rel))
    if path not in _cache:
        _cache[path] = load(path)
    return cut(_cache[path], t0, t1).copy()


def peak_time(x, smooth=0.005):
    k = max(1, n_of(smooth))
    e = np.convolve(np.abs(x), np.ones(k) / k, mode='same')
    return int(np.argmax(e)) / R


def onset_time(x, thr_db=-20):
    """When x first gets within thr_db of its peak."""
    p = peak(x)
    idx = np.nonzero(np.abs(x) > p * db(thr_db))[0]
    return idx[0] / R if len(idx) else 0.0


def align(x, at_peak):
    """Pads or cuts x's start so its loudest moment lands at at_peak seconds."""
    d = at_peak - peak_time(x)
    if d >= 0:
        return np.concatenate([np.zeros(n_of(d)), x])
    return x[n_of(-d):]


def unit(x):
    return x / (peak(x) + 1e-12)


def place(out, x, at, gain=1.0, wrap=False):
    """Adds x into out at `at` seconds; what runs past the end is dropped, or wraps round for loops."""
    i = n_of(at) % len(out) if wrap else n_of(at)
    if wrap:
        x = x[:len(out)]
        end = i + len(x)
        if end <= len(out):
            out[i:end] += x * gain
        else:
            k = len(out) - i
            out[i:] += x[:k] * gain
            out[:end - len(out)] += x[k:] * gain
        return out
    if i >= len(out):
        return out
    end = min(len(out), i + len(x))
    out[i:end] += x[:end - i] * gain
    return out


def cap(x, seconds, fade_out=0.4):
    """Cuts x to at most seconds, fading out over the last fade_out."""
    if len(x) <= n_of(seconds):
        return x
    return fade(x[:n_of(seconds)], 0.0, fade_out)


def at_onset(x, pre=0.002, thr_db=-24):
    """Cuts x to start just before its first loud moment."""
    o = onset_time(x, thr_db)
    return fade(x[max(0, n_of(o - pre)):], 0.001, 0.0)


# ================================================================ WEAPONS

M1, M2 = 'oga_medieval_tex1', 'oga_medieval_tex2'
KI, KR = 'kenney_impact', 'kenney_rpg'


def body_thump(seconds, rng, f0=70.0, decay=18.0):
    """A low, short thump to give a hit its weight."""
    tt = t(seconds)
    f = f0 * (1 + 1.2 * np.exp(-tt / 0.008))
    return np.sin(2 * np.pi * np.cumsum(f) / R) * np.exp(-tt * decay) * np.minimum(1, tt / 0.0015)


@series('swing_light', 3, layers=True)
def swing_light(rng, i):
    # A real dagger/katana swing (its air-pop thump filtered away) with a thin synthetic edge
    src = [(M2, 'Dagger Swing.wav', 0.95, 1.40), (M1, 'Katana Swing.wav', 7.90, 8.40), (M2, 'Dagger Swing.wav', 1.50, 1.95)][i - 1]
    x = S(*src)
    x = highpass(x, 260, order=4)
    x = shelf(x, 'highshelf', 4000, 3)
    x = unit(x)
    w = whoosh(0.24, rng, 800 + 200 * i, 3600, 0.5, q=1.6, curve=2.5)
    w = align(unit(w), peak_time(x))
    y = mix((x, 0, 0), (w, 0, -10))
    y = pitch(y, [0.0, 1.0, -0.8][i - 1])
    pk = peak_time(y)
    y = fade(y[max(0, n_of(pk - 0.13)):], 0.07, 0.0)
    return trim(y, -42, fade_out=0.04)


@series('swing_heavy', 3, layers=True)
def swing_heavy(rng, i):
    # A real axe swing, slowed and lowered, over a deep synthetic whoosh
    src = [(M2, 'Axe Swing.wav', 1.30, 1.95), (M2, 'Axe Swing.wav', 5.00, 5.62), (M2, 'Axe Swing.wav', 2.40, 2.86)][i - 1]
    x = fade(S(*src), 0.01, 0.08)
    x = highpass(x, 110, order=4)
    x = pitch(x, [-3.0, -4.0, -2.0][i - 1])
    x = unit(x)
    w = whoosh(0.55, rng, 140, 1300, 0.55, q=1.1, curve=2.2, rumble=0.7)
    w = align(unit(w), peak_time(x))
    y = mix((x, 0, 0), (w, 0, -5))
    y = peq(y, 180, 3, 0.8)
    pk = peak_time(y)
    y = fade(y[max(0, n_of(pk - 0.2)):], 0.1, 0.0)
    return trim(y, -42, fade_out=0.06)


@series('hit_blade', 4, layers=True)
def hit_blade(rng, i):
    # Blade into flesh: a cut (knife slice/chop/blade), a wet squish under it and a dull thud
    cut_src = [(KR, 'knifeSlice2.ogg', 0.22, 0.60), (KR, 'chop.ogg', 0.03, 0.24), (KR, 'knifeSlice.ogg', 0.16, 0.50), ('rd_rpg', 'blade_02.ogg', 0.0, 0.31)][i - 1]
    wet_src = [('oga_wetsquish', 'impactsplat03.mp3.flac', 0.0, 0.45), ('oga_wetsquish', 'impactsplat07.mp3.flac', 0.02, 0.21),
               ('oga_wetsquish', 'impactsplat01.mp3.flac', 0.04, 0.45), ('oga_wetsquish', 'impactsplat04.mp3.flac', 0.10, 0.55)][i - 1]
    thud_src = [(KI, 'impactPunch_medium_000.ogg'), (KI, 'impactPunch_heavy_001.ogg'), (KI, 'impactPunch_medium_002.ogg'), (KI, 'impactPunch_heavy_003.ogg')][i - 1]
    c = at_onset(highpass(S(*cut_src), 400))
    w = at_onset(highpass(S(*wet_src), 120))
    th = at_onset(lowpass(S(*thud_src), 2500))
    y = mix((unit(c), 0, 0), (unit(w), 0.004, -4), (unit(th), 0.0, -7), (body_thump(0.15, rng, 85 + 10 * i), 0, -12))
    y = fade(y, 0.001, 0.08)
    return trim(y, -40, fade_out=0.06)


@series('hit_blunt', 4, layers=True)
def hit_blunt(rng, i):
    # Blunt into flesh: a heavy punch thud, a bony crunch and a hint of wet
    thud = [(KI, 'impactPunch_heavy_000.ogg'), (KI, 'impactPunch_heavy_002.ogg'), (KI, 'impactPunch_heavy_004.ogg'), (KI, 'impactPunch_heavy_001.ogg')][i - 1]
    crunch = [('oga_bonebreak', 'Deep Break 1.wav', 0.10, 0.45), ('oga_bonebreak', 'Deep Break 6.wav', 0.33, 0.66),
              ('oga_bonebreak', 'Deep Break 8.wav', 0.14, 0.50), ('oga_bonebreak', 'Deep Break 3.wav', 0.20, 0.55)][i - 1]
    wet = ('oga_wetsquish', ['impactsplat03.mp3.flac', 'impactsplat01.mp3.flac', 'impactsplat04.mp3.flac', 'impactsplat02.mp3.flac'][i - 1])
    th = at_onset(S(*thud))
    cr = at_onset(highpass(S(*crunch), 300))
    w = fade(at_onset(highpass(S(*wet, 0.0, 0.35), 200)), 0.001, 0.1)
    y = mix((unit(th), 0, 0), (unit(cr), 0.003, -6), (unit(w), 0.002, -14), (body_thump(0.25, rng, 55 + 5 * i, 14), 0, -6))
    y = soft_clip(unit(y) * 1.4, 1.2)
    return trim(y, -40, fade_out=0.06)


@series('hit_wood', 2, layers=False)
def hit_wood(rng, i):
    # A weapon hitting a wooden wall or door: a solid thunk with a short plank ring
    a = [(KI, 'impactWood_heavy_001.ogg'), (KI, 'impactWood_heavy_003.ogg')][i - 1]
    b = [(KI, 'impactPlank_medium_002.ogg'), (KI, 'impactPlank_medium_004.ogg')][i - 1]
    plank = fade(at_onset(S(*b))[:n_of(0.22)], 0.0, 0.08)
    y = mix((unit(at_onset(S(*a))), 0, 0), (unit(plank), 0.002, -5))
    return trim(y, -45, fade_out=0.05)


@series('hit_stone', 2, layers=False)
def hit_stone(rng, i):
    # Steel striking stone: a pick-on-rock clang, a little metal ring and grit falling
    a = [(KI, 'impactMining_001.ogg'), (KI, 'impactMining_003.ogg')][i - 1]
    b = [(KI, 'impactMetal_light_002.ogg'), (KI, 'impactMetal_light_004.ogg')][i - 1]
    g = [('rd_breaking', 'bfh1_rock_falling_02.ogg'), ('rd_breaking', 'bfh1_rock_falling_05.ogg')][i - 1]
    y = mix((unit(at_onset(S(*a))), 0, 0), (unit(at_onset(S(*b))), 0.001, -9), (unit(at_onset(highpass(S(*g), 500))), 0.05, -18))
    return trim(y, -45, fade_out=0.08)


@series('gore', 3, layers=True)
def gore(rng, i):
    # A kill's wet splatter: squelch, a slimy splash on top and a soft low thump
    a = [('oga_wetsquish', 'impactsplat02.mp3.flac', 0.10, 0.95), ('oga_wetsquish', 'impactsplat05.mp3.flac', 0.0, 1.1),
         ('oga_splatter', 'crack11.mp3.flac', 0.0, 0.9)][i - 1]
    b = [('rd_water', 'slime_03.ogg'), ('rd_water', 'slime_07.ogg'), ('rd_water', 'slime_11.ogg')][i - 1]
    x = at_onset(S(*a))
    s = at_onset(S(*b))
    y = mix((unit(x), 0, 0), (unit(s), 0.02, -6), (body_thump(0.2, rng, 70, 16), 0, -10))
    y = fade(y, 0.001, 0.2)
    return trim(y, -40, fade_out=0.12)


@sound('bloat_swell', layers=True)
def bloat_swell(rng):
    # A Bloater about to burst: a moan dropped half an octave and gargled, wet bubbling under it, both swelling to the
    # end (it bursts as this ends, about a second in)
    moan = zvox(S('oga_zombie_moans_ds', 'darsycho__zombie-moans_0.ogg', 0.25, 1.45), -6, 0.3, fade_out=0.05)
    bub = lowpass(at_onset(S('rd_water', 'slime_07.ogg')), 1800)
    bub2 = lowpass(at_onset(S('rd_water', 'slime_11.ogg')), 1400)
    y = mix((unit(moan), 0, 0), (unit(bub), 0.15, -5), (unit(bub2), 0.55, -4))
    y = y[:n_of(1.0)]
    y = y * np.minimum(1, (np.arange(len(y)) / R / 0.85) ** 1.5 * 0.85 + 0.15)
    return trim(fade(y, 0.02, 0.04), -45, fade_out=0.04)


@series('bloat_pop', 2, layers=True)
def bloat_pop(rng, i):
    # A Bloater bursting: a deep wet blast (a bang's low end), the splatter on top and a hard low thump, a short room
    bang = lowpass(at_onset(S('rd_bang', ['bang_03.ogg', 'bang_09.ogg'][i - 1])), 900)
    splat = at_onset(S(*[('oga_wetsquish', 'impactsplat02.mp3.flac'), ('oga_wetsquish', 'impactsplat05.mp3.flac')][i - 1]))
    crack = at_onset(S('oga_splatter', 'crack11.mp3.flac'))
    slime = at_onset(S('rd_water', ['slime_03.ogg', 'slime_07.ogg'][i - 1]))
    y = mix((unit(bang), 0, -1), (unit(splat), 0, -2), (unit(crack), 0.01, -6), (unit(slime), 0.04, -8),
            (unit(body_thump(0.45, rng, 48, 7)), 0, -2))
    y = reverb(highpass(y, 30), 0.8, wet=0.12, damp=0.6)
    y = limit(unit(y) * db(4), -1)
    return cap(trim(y, -50, fade_out=0.2), 1.8, 0.7)


@series('headshot', 2, layers=True)
def headshot(rng, i):
    # Skull crack: a sharp bone crack, crunch, a heavy thud and a splat
    crack = [('oga_bonebreak', 'Deep Break 6.wav', 0.36, 0.60), ('oga_bonebreak', 'Deep Break 2.wav', 0.15, 0.45)][i - 1]
    splat = [('oga_wetsquish', 'impactsplat08.mp3.flac', 0.55, 0.65 + 0.4), ('oga_wetsquish', 'impactsplat06.mp3.flac', 0.0, 0.6)][i - 1]
    thud = [(KI, 'impactPunch_heavy_002.ogg'), (KI, 'impactPunch_heavy_000.ogg')][i - 1]
    cr = at_onset(highpass(S(*crack), 250))
    sp = at_onset(highpass(S(*splat), 150))
    th = at_onset(S(*thud))
    snap = highpass(white(0.03, rng), 2500) * np.exp(-t(0.03) * 180)
    y = mix((unit(cr), 0, 0), (unit(snap), 0, -8), (unit(th), 0.0, -4), (unit(sp), 0.03, -5), (body_thump(0.2, rng, 60, 15), 0, -8))
    y = soft_clip(unit(y) * 1.5, 1.3)
    return trim(y, -40, fade_out=0.1)


@sound('stab')
def stab(rng):
    # A quick dagger thrust: the sharp front of a knife slice into a short squelch and a small thud
    c = at_onset(highpass(S(KR, 'knifeSlice2.ogg', 0.24, 0.40), 600))
    c = fade(c, 0.001, 0.08)
    w = fade(at_onset(S('oga_wetsquish', 'impactsplat07.mp3.flac', 0.05, 0.21)), 0.001, 0.06)
    th = at_onset(S(KI, 'impactPunch_medium_001.ogg'))
    y = mix((unit(c), 0, -2), (unit(w), 0.03, -3), (unit(th), 0.03, -9))
    return trim(y, -40, fade_out=0.05)


@sound('draw_blade', layers=True)
def draw_blade(rng):
    # A sabre pulled from its scabbard, with a thin steel ring at the end ("shing")
    x = S(M2, 'Sabre Draw Fast.wav', 11.50, 12.10)
    x = highpass(x, 300)
    x = unit(trim(x, -40))
    ring = modal([2650, 4120, 5480, 7930], [1, 0.6, 0.45, 0.25], [6, 9, 12, 16], 0.9, rng, beat=3)
    end = peak_time(x) + 0.05
    y = mix((x, 0, 0), (unit(ring), end, -16))
    return trim(y, -46, fade_out=0.15)


@sound('draw_wood', layers=False)
def draw_wood(rng):
    # A bow or wooden haft raised: a wood creak over a cloth and leather rustle
    cr = at_onset(S(KR, 'creak3.ogg'))
    cl = at_onset(S(KR, 'cloth1.ogg'))
    le = at_onset(S(KR, 'handleSmallLeather.ogg'))
    y = mix((unit(cl), 0, -2), (unit(cr), 0.08, -4), (unit(le), 0.18, -8))
    return trim(y, -45, fade_out=0.08)


@sound('empty', layers=False)
def empty(rng):
    # A dry click: a crossbow's trigger lever snapping on nothing
    x = S(M1, 'Crossbow Lever Trigger.wav', 0.12, 0.40)
    x = highpass(x, 200)
    return trim(x, -38, fade_out=0.03)


@sound('bow_draw', layers=False)
def bow_draw(rng):
    # A longbow pulled: the string creaking as the limbs bend (one draw from the take, about 0.8 s)
    x = S(M1, 'English Longbow Draw.wav', 0.0, 28.4)
    return bow_draw_cut(x)


def bow_draw_cut(x):
    # The take holds many slow draws; find a loud stretch and keep 0.8 s of it
    hop = n_of(0.05)
    nb = len(x) // hop
    e = np.sqrt(np.mean(x[:nb * hop].reshape(nb, hop) ** 2, axis=1))
    w = 16  # 0.8 s
    s = np.convolve(e, np.ones(w), 'valid')
    i = int(np.argmax(s))
    y = x[i * hop:(i + w) * hop]
    y = highpass(y, 120)
    return fade(y, 0.12, 0.12)


@series('bow_release', 2, layers=True)
def bow_release(rng, i):
    # A bow loosed: the string's crack and twang, then the arrow's whoosh leaving
    src = [(M1, 'English Longbow Shoot.wav', 1.22, 1.75), (M1, 'Scythian Recurve Shoot.wav', 0.19, 0.60)][i - 1]
    x = at_onset(highpass(S(*src), 120))
    tw = string_twang(0.35, rng, [98, 110][i - 1])
    p = highpass(S(M1, 'Scythian Recurve Arrow Pass.wav', 0.50, 1.62), 900)
    p = fade(align(unit(p), 0.12), 0.06, 0.15)[:n_of(0.4)]
    y = mix((unit(x), 0, 0), (unit(tw), 0, -4), (body_thump(0.12, rng, 120, 30), 0, -8), (unit(p), 0.0, -11))
    return trim(y, -42, fade_out=0.1)


def string_twang(seconds, rng, f0):
    """A taut string let go: a thrummed tone that drops a little in pitch and buzzes out."""
    tt = t(seconds)
    f = f0 * (1 + 0.15 * np.exp(-tt / 0.02))
    s = saw_blep(f, seconds)
    s = lowpass(s, 1800) * np.exp(-tt * 14)
    am = 1 + 0.4 * np.sin(2 * np.pi * 31 * tt)
    return s * am


@series('arrow_hit_flesh', 3, layers=True)
def arrow_hit_flesh(rng, i):
    # An arrow into a body: a sharp thwack, a short wet squelch and a low thud
    th = [(KI, 'impactPunch_medium_003.ogg'), (KI, 'impactPunch_medium_001.ogg'), (KI, 'impactPunch_medium_004.ogg')][i - 1]
    sp = [('oga_wetsquish', 'impactsplat07.mp3.flac', 0.05, 0.20), ('oga_wetsquish', 'impactsplat03.mp3.flac', 0.0, 0.3),
          ('oga_wetsquish', 'impactsplat04.mp3.flac', 0.12, 0.40)][i - 1]
    tk = at_onset(highpass(S(KI, f'impactGeneric_light_00{i}.ogg'), 800))
    y = mix((unit(at_onset(S(*th))), 0, -1), (unit(at_onset(S(*sp))), 0.005, -4), (unit(tk), 0, -8), (body_thump(0.12, rng, 95, 25), 0, -12))
    return trim(y, -40, fade_out=0.05)


def shaft_buzz(seconds, rng, f0=150.0):
    """The arrow's shaft quivering after it sticks: a fast-wobbling, buzzy decaying tone."""
    tt = t(seconds)
    s = saw_blep(f0 * (1 - 0.05 * tt), seconds)
    s = bandpass(s, 200, 2500)
    am = 0.5 + 0.5 * np.sin(2 * np.pi * 26 * tt) ** 2
    return s * am * np.exp(-tt * 9) * np.minimum(1, tt / 0.01)


@series('arrow_hit_wood', 2, layers=True)
def arrow_hit_wood(rng, i):
    # An arrow thunking into wood, the shaft quivering
    a = [(KI, 'impactWood_light_001.ogg'), (KI, 'impactWood_medium_002.ogg')][i - 1]
    b = [('rd_metalwood', 'wood_hit_03.ogg'), ('rd_metalwood', 'wood_hit_06.ogg')][i - 1]
    y = mix((unit(at_onset(S(*a))), 0, 0), (unit(at_onset(S(*b))), 0.001, -8), (unit(shaft_buzz(0.45, rng, [150, 128][i - 1])), 0.01, -13))
    return trim(y, -44, fade_out=0.08)


@series('arrow_hit_stone', 2, layers=False)
def arrow_hit_stone(rng, i):
    # An arrowhead cracking off stone: a hard tick, then the shaft clattering
    a = [(KI, 'impactMining_002.ogg', 0, 0.25), (KI, 'impactMining_004.ogg', 0, 0.25)][i - 1]
    b = [('rd_rpg', 'item_wood_01.ogg'), ('rd_rpg', 'item_wood_03.ogg')][i - 1]
    x = fade(highpass(S(*a), 900), 0.0005, 0.12)
    y = mix((unit(at_onset(x)), 0, 0), (unit(at_onset(highpass(S(*b), 300))), 0.07, -10))
    return trim(y, -44, fade_out=0.06)


@sound('crossbow_fire', layers=True)
def crossbow_fire(rng):
    # A crossbow shot: the prod's crack, a thick string thunk and the bolt hissing away
    x = at_onset(S(M1, 'Crossbow Shoot.wav', 0.28, 0.80))
    tw = string_twang(0.3, rng, 82)
    p = at_onset(highpass(S(M1, 'Crossbow Bolt Pass.wav', 0.50, 1.20), 1200))
    y = mix((unit(x), 0, 0), (unit(tw), 0, -8), (body_thump(0.15, rng, 90, 20), 0, -9), (unit(p), 0.03, -14))
    return trim(y, -42, fade_out=0.1)


@sound('crossbow_reload', layers=False)
def crossbow_reload(rng):
    # Spanning a crossbow, about a second: lever clicks, the nut catching the string, the bolt laid in
    clicks = S(M1, 'Crossbow Set Nut Against Sear.wav', 0.60, 1.40)
    latch = at_onset(S(M1, 'Crossbow Place Bolt.wav', 0.36, 0.80))
    bolt = at_onset(S(M1, 'Crossbow Place Bolt.wav', 1.48, 1.95))
    y = mix((unit(clicks), 0, -3), (unit(latch), 0.55, 0), (unit(bolt), 0.82, -3))
    return trim(highpass(y, 120), -45, fade_out=0.06)


@sound('arbalest_fire', layers=True)
def arbalest_fire(rng):
    # A heavy steel-bowed arbalest: a lower, bigger crack with a deep thump and a heavy bolt
    x = at_onset(S(M1, 'Crossbow Shoot.wav', 2.45, 2.95))
    x = pitch(x, -4)
    tw = string_twang(0.45, rng, 58)
    wd = at_onset(lowpass(S(KI, 'impactWood_heavy_002.ogg'), 3000))
    p = at_onset(highpass(S(M1, 'Scythian Recurve Heavy Arrow Pass.wav', 0.71, 1.30), 700))
    y = mix((unit(x), 0, 0), (unit(tw), 0, -7), (unit(wd), 0, -6), (body_thump(0.3, rng, 55, 12), 0, -4), (unit(p), 0.04, -12))
    y = soft_clip(unit(y) * 1.3, 1.1)
    return trim(y, -42, fade_out=0.12)


@sound('arbalest_reload', layers=False)
def arbalest_reload(rng):
    # Winding an arbalest with a windlass, about two seconds: a ratchet clicking up, then the latch
    r = S(M1, 'Crossbow Lever Trigger.wav', 0.62, 2.55)
    r = highpass(r, 150)
    cr = at_onset(S(KR, 'creak2.ogg'))
    latch = at_onset(S(M1, 'Crossbow Place Bolt.wav', 0.36, 0.80))
    groan_ = pitch(at_onset(S(KR, 'creak1.ogg')), -7)
    y = mix((unit(r), 0, -2), (unit(pitch(cr, -5)), 0.3, -10), (unit(groan_), 0.9, -12), (unit(latch), 1.75, 0))
    return trim(y, -45, fade_out=0.08)


@sound('repeater_fire', layers=True)
def repeater_fire(rng):
    # A repeating crossbow: the lever's clack and a quick, lighter twang
    lever = at_onset(S(M1, 'Crossbow Lever Trigger.wav', 0.12, 0.40))
    x = at_onset(S(M1, 'Crossbow Shoot.wav', 0.80, 1.30))
    x = pitch(x, 2)
    tw = string_twang(0.2, rng, 130)
    y = mix((unit(lever), 0, -4), (unit(x), 0.035, 0), (unit(tw), 0.035, -10))
    return trim(y, -42, fade_out=0.08)


@sound('repeater_reload', layers=False)
def repeater_reload(rng):
    # Refilling a repeater's magazine, about 1.5 s: bolts clattering in, a wooden knock, the lever
    a = at_onset(S('oga_weapons_apparel', 'arrow-grab-from-quiver-02.wav'))
    b = at_onset(S('oga_weapons_apparel', 'arrow-return-to-quiver-03.wav'))
    c = at_onset(S('oga_weapons_apparel', 'arrow-return-to-quiver-05.wav'))
    k = at_onset(S(KI, 'impactWood_light_003.ogg'))
    lever = at_onset(S(M1, 'Crossbow Lever Trigger.wav', 0.12, 0.40))
    y = mix((unit(a), 0, -2), (unit(b), 0.35, -1), (unit(c), 0.65, -2), (unit(k), 0.95, -6), (unit(lever), 1.2, 0))
    return trim(highpass(y, 100), -45, fade_out=0.06)


@sound('handgonne_fire', layers=True)
def handgonne_fire(rng):
    # A black-powder hand cannon: a sharp crack, a deep boom and the fizzing crackle of powder smoke
    boom = at_onset(S('oga_cannon', 'cannon_fire.ogg'))
    crack = at_onset(S('rd_bang', 'shot_01.ogg'))
    fizz = crackle(1.6, rng, 160, decay=4000) * env_points([(0, 0), (0.05, 1), (0.5, 0.5), (1.6, 0)], 1.6)
    fizz = bandpass(fizz, 1500, 9000)
    sub = body_thump(0.6, rng, 42, 6)
    y = mix((unit(boom), 0, 0), (unit(crack), 0, -5), (unit(fizz), 0.05, -16), (unit(sub), 0, -6))
    y = reverb(y, rt60=1.4, wet=0.18, predelay=0.02, damp=0.6)
    y = limit(unit(y) * db(5), -1)
    return trim(y, -50, fade_out=0.3)


@sound('handgonne_reload', layers=True)
def handgonne_reload(rng):
    # Reloading a handgonne, about two seconds: powder poured down the barrel, then the ramrod
    pour = colored(0.7, rng, -1.0) * (0.6 + 0.4 * np.abs(lowpass(rng.standard_normal(n_of(0.7)), 35)) / 0.05)
    pour = pour + 0.5 * crackle(0.7, rng, 1500, decay=7000)
    # the barrel's air column shortens as it fills, so its resonance climbs
    pour = stft_filter(pour, lambda tm, f: 0.15 + 2.5 * band_gain(f, 900 + 900 * min(max(tm / 0.7, 0), 1), 0.12) + 1.2 * band_gain(f, 3 * (900 + 900 * min(max(tm / 0.7, 0), 1)), 0.12))
    pour = bandpass(pour, 400, 9000) * env_points([(0, 0), (0.05, 1), (0.6, 0.9), (0.7, 0)], 0.7)
    scrape1 = at_onset(S('oga_weapons_apparel', 'sword-table-leg-scrape-02.wav'))
    scrape1 = fade(scrape1[:n_of(0.45)], 0.01, 0.1)
    tamp1 = fade(lowpass(at_onset(S(KI, 'impactMetal_light_001.ogg')), 3500)[:n_of(0.2)], 0.0, 0.12)
    tamp2 = fade(lowpass(at_onset(S(KI, 'impactMetal_light_003.ogg')), 3500)[:n_of(0.2)], 0.0, 0.12)
    thud = at_onset(lowpass(S(KI, 'impactSoft_medium_001.ogg'), 1500))
    y = mix((unit(pour), 0.0, -10), (unit(scrape1), 0.7, -4), (unit(tamp1), 1.15, -6), (unit(thud), 1.15, -8),
            (unit(tamp2), 1.45, -7), (unit(thud), 1.45, -10), (unit(fade(reverse(scrape1), 0.05, 0.02)), 1.6, -8))
    return trim(highpass(y, 80), -45, fade_out=0.08)


# ---------------------------------------------------------------- guns

FA = 'oga_firearms'


def gunshot(rel, t0, seconds, rng):
    """One shot of a recording as it was made, outdoors: from just before its crack, its own echo let ring for
    `seconds` and faded out; only the rumble under 40 Hz taken off."""
    x = at_onset(S(FA, rel, max(0.0, t0 - 0.05), t0 + seconds + 0.3), pre=0.001)
    x = fade(x[:n_of(seconds)], 0.0, seconds * 0.5)
    return limit(unit(highpass(x, 40)) * db(2), -1)


# Each gun's takes: the library's file and when each shot's crack comes
RIFLE_TAKES = [('AR-15/D_24P.wav', 0.547), ('AR-15/D_24P.wav', 3.908), ('AR-15/D_32P.wav', 0.703)]
# (the PPSh's single shots: its other files are bursts, fourteen shots in a 0.7 s take)
SMG_TAKES = [('PPSh/P_30P.wav', 0.968), ('PPSh/P_30P.wav', 4.386), ('PPSh/P_30P.wav', 8.241)]
SHOTGUN_TAKES = [('Mossberg/N_26P.wav', 0.785), ('Mossberg/N_30P.wav', 1.694), ('Mossberg/N_26P.wav', 4.573)]
PISTOL_TAKES = [('1911/A_42P.wav', 0.939), ('1911/A_34P.wav', 1.541), ('1911/A_42P.wav', 4.999)]


@series('rifle_fire', 3, layers=False)
def rifle_fire(rng, i):
    # An AR-15's shot and its echo off the trees
    return trim(gunshot(*RIFLE_TAKES[i - 1], 1.2, rng), -55, fade_out=0.1)


@series('smg_fire', 3, layers=False)
def smg_fire(rng, i):
    # A PPSh's shot, its echo shorter: it's fired fourteen a second
    return trim(gunshot(*SMG_TAKES[i - 1], 0.7, rng), -55, fade_out=0.08)


@series('shotgun_fire', 3, layers=True)
def shotgun_fire(rng, i):
    # A Mossberg's blast and its long echo, over a deep boom (the cannon recording's low end) and a hard low thump at
    # the crack, pushed into the limiter: the blast felt in the chest (the owner: "the shotgun SFX needs to feel more
    # impactful")
    shot = gunshot(*SHOTGUN_TAKES[i - 1], 1.8, rng)
    boom = lowpass(at_onset(S('oga_cannon', 'cannon_fire.ogg')), 160)
    boom = fade(boom[:n_of(1.2)], 0.0, 0.8)
    thump = body_thump(0.35, rng, 55, 9)
    y = mix((unit(shot), 0, 0), (unit(boom), 0, -5), (unit(thump), 0, -4))
    y = limit(unit(y) * db(5), -1)
    return trim(y, -55, fade_out=0.2)


@series('pistol_fire', 3, layers=False)
def pistol_fire(rng, i):
    # A 1911's shot
    return trim(gunshot(*PISTOL_TAKES[i - 1], 1.0, rng), -55, fade_out=0.1)


@sound('turret_fire', layers=False)
def turret_fire(rng):
    # The Engineer's turret: the SMG's shot, a little higher and drier, a machine's
    x = gunshot('PPSh/P_22P.wav', 1.143, 0.35, rng)
    return trim(pitch(x, 2), -55, fade_out=0.05)


def click(rel, seconds=0.15, lp=6000):
    return fade(lowpass(at_onset(S(KI, rel)), lp)[:n_of(seconds)], 0.0, seconds * 0.5)


@sound('mag_reload')
def mag_reload(rng):
    # A rifle's magazine changed and its bolt let go: a recording of the real handling (an airsoft rifle)
    x = at_onset(S('oga_reload_springy', 'assaultriflereload1_0.wav'), pre=0.01, thr_db=-30)
    return trim(highpass(x, 60), -50, fade_out=0.05)


@sound('shell_load', layers=True)
def shell_load(rng):
    # A shell pushed into a shotgun's tube
    push = click('impactMetal_light_000.ogg', 0.1, 5000)
    rub = bandpass(colored(0.08, rng, -1.0), 2000, 8000) * env_points([(0, 0), (0.02, 1), (0.08, 0)], 0.08)
    y = mix((unit(rub), 0.0, -12), (unit(push), 0.05, 0))
    return trim(highpass(y, 150), -45, fade_out=0.03)


@sound('pump')
def pump(rng):
    # A shotgun racked, back and forward: a recording
    x = at_onset(S('oga_reload_springy', 'shotguncock_0.wav'), pre=0.01, thr_db=-30)
    return trim(highpass(x, 60), -50, fade_out=0.05)


@sound('pistol_reload')
def pistol_reload(rng):
    # A pistol's magazine out and in, the slide racked: a recording
    x = at_onset(S('oga_reload_springy', 'gunreload1.wav'), pre=0.01, thr_db=-30)
    return trim(highpass(x, 60), -50, fade_out=0.05)


@series('bullet_flesh', 4, layers=False)
def bullet_flesh(rng, i):
    # A round into one of the dead: qubodup's meat impacts and Independent.nu's wet ones, short and tight
    if i <= 2:
        x = at_onset(S('oga_impact_qubodup', f'qubodupImpactMeat0{i}.flac'))
    else:
        x = at_onset(S('oga_wetsquish', ['impactsplat01.mp3.flac', 'impactsplat04.mp3.flac'][i - 3]))
    x = fade(x[:n_of(0.35)], 0.0, 0.15)
    return trim(highpass(x, 120), -50, fade_out=0.05)


# ---------------------------------------------------------------- magic

def roar_whoosh(seconds, rng, peak_at=0.3):
    """Fire rushing through air: a broad, rough band sweep with a low roar."""
    w = whoosh(seconds, rng, 200, 2200, peak_at, q=0.8, curve=1.6, rumble=0.9)
    rough = 1 + 0.5 * lowpass(rng.standard_normal(n_of(seconds)), 60) / 0.1
    return w * np.clip(rough, 0.2, 2.0)


@sound('staff_fire', layers=True)
def staff_fire(rng):
    # A fireball cast: a roaring whoosh that swells and flies off, with crackles riding it
    a = at_onset(S('rd_rpg', 'spell_fire_03.ogg'))
    w = roar_whoosh(1.0, rng, 0.25)
    cr = crackle(1.0, rng, 60) * env_points([(0, 0), (0.15, 1), (1.0, 0)], 1.0)
    y = mix((unit(w), 0, 0), (unit(a), 0.02, -4), (unit(cr), 0.0, -14))
    return trim(y, -45, fade_out=0.15)


def ice_tinkle(seconds, rng, count=40, lo=2500, hi=9000, spread=0.3, ring=1.0):
    """Many tiny glassy pings: inharmonic, short and high."""
    out = zeros(seconds)
    for _ in range(count):
        f = rng.uniform(lo, hi)
        d = rng.uniform(12, 40) / ring
        ping = modal([f, f * 2.32, f * 3.7], [1, 0.4, 0.2], [d, d * 1.5, d * 2], 0.5, rng)
        at = abs(rng.normal(0, spread))
        i = n_of(at)
        if i < len(out):
            out[i:i + len(ping)] += ping[:len(out) - i] * rng.uniform(0.3, 1)
    return out


@sound('staff_frost', layers=True)
def staff_frost(rng):
    # An icy burst: a hard, bright crack, a spray of ringing crystal shards and a cold rush of air
    crack = highpass(white(0.08, rng), 3000) * np.exp(-t(0.08) * 70)
    shards = zeros(1.2)
    for k in range(14):
        f = rng.uniform(1800, 6500)
        d = rng.uniform(4, 9)
        ping = modal([f, f * 1.51, f * 2.17, f * 2.93], [1, 0.55, 0.35, 0.2], [d, d * 1.3, d * 1.7, d * 2.2], 1.0, rng)
        place(shards, ping * rng.uniform(0.5, 1.0), abs(rng.normal(0, 0.05)))
    tk = ice_tinkle(1.0, rng, 40, 4000, 11000, 0.15, ring=1.4)
    br = whoosh(0.7, rng, 2500, 9000, 0.1, q=0.9, curve=1.2)
    ice = at_onset(S('oga_ice_spells', 'ice.wav'))
    y = mix((unit(crack), 0, -2), (unit(shards), 0.0, 0), (unit(tk), 0.0, -7), (unit(br), 0, -12), (unit(ice), 0.0, -9))
    y = reverb(y, 1.1, wet=0.22, damp=0.2, bright=14000)
    return trim(y, -45, fade_out=0.2)


@sound('staff_storm', layers=False)
def staff_storm(rng):
    # A crackling lightning zap: a hard snap, a dense arc of sparks and a low thunder crack
    tt = t(0.9)
    z = zap(0.9, rng, 1400, 110) * env_points([(0, 1), (0.1, 0.8), (0.9, 0)], 0.9)
    snap = highpass(white(0.04, rng), 1500) * np.exp(-t(0.04) * 120)
    boom = lowpass(colored(0.9, rng, -6), 300) * np.exp(-tt * 5) * np.minimum(1, tt / 0.01)
    y = mix((unit(snap), 0, 0), (unit(z), 0.005, -2), (unit(boom), 0.0, -4))
    return trim(y, -45, fade_out=0.15)


@sound('fire_burst', layers=True)
def fire_burst(rng):
    # A fireball bursting: a recorded fireball whoomp with a deeper synthetic boom and fire falling off
    a = at_onset(S('oga_fireball', '105016__julien-matthey__jm-fx-fireball-01.wav'))
    tt = t(1.2)
    boom = lowpass(colored(1.2, rng, -5), 500) * np.exp(-tt * 4) * np.minimum(1, tt / 0.008)
    cr = crackle(1.2, rng, 50) * np.exp(-tt * 2.5)
    y = mix((unit(a), 0, 0), (unit(boom), 0, -5), (unit(cr), 0.05, -14))
    return trim(y, -45, fade_out=0.2)


@sound('freeze', layers=True)
def freeze(rng):
    # Things freezing solid: ice creaking and cracking as it spreads, glassy pings, a cold hiss
    sec = 1.4
    tt = t(sec)
    dens = env_points([(0, 0.2), (0.3, 1), (1.0, 0.6), (1.4, 0)], sec)
    cr = crackle(sec, rng, 260, decay=1800) * dens
    cr = peq(highpass(cr, 600), 3500, 6, 2)
    tk = ice_tinkle(sec, rng, 30, 2500, 8000, 0.45)
    a = at_onset(S('oga_freeze', 'freeze.wav'))
    hiss = bandpass(white(sec, rng), 4000, 12000) * env_points([(0, 0), (0.2, 1), (1.4, 0)], sec)
    y = mix((unit(cr), 0, 0), (unit(tk), 0.05, -6), (unit(a), 0, -6), (unit(hiss), 0, -16))
    y = limit(unit(y) * db(4), -1)
    return trim(y, -45, fade_out=0.2)


@sound('chain_zap', layers=False)
def chain_zap(rng):
    # An electric arc jumping between targets: snapping sparks over a rising buzz
    sec = 0.7
    z = zap(sec, rng, 1800, 150) * env_points([(0, 0.6), (0.05, 1), (0.6, 0.6), (0.7, 0)], sec)
    tt = t(sec)
    buzz = saw_blep(np.linspace(180, 320, n_of(sec)), sec)
    buzz = bandpass(buzz, 300, 5000) * env_points([(0, 0), (0.03, 1), (0.7, 0)], sec)
    snaps = mix(*[(unit(highpass(white(0.03, rng), 2000) * np.exp(-t(0.03) * 150)), a, -2) for a in (0.0, 0.22, 0.43)])
    y = mix((unit(z), 0, -1), (unit(buzz), 0, -10), (snaps, 0, -3))
    return trim(y, -45, fade_out=0.1)


@sound('throw', layers=True)
def throw(rng):
    # Something thrown hard: a recorded swish, lowered, over a soft tumbling whoosh
    s = at_onset(S('oga_swishes', 'swish-3.wav'))
    s = pitch(highpass(s, 200), -4)
    w = whoosh(0.45, rng, 300, 1600, 0.35, q=1.2)
    y = mix((unit(w), 0, -2), (align(unit(s), peak_time(w)), 0, -2))
    return trim(y, -42, fade_out=0.08)


@series('explode', 2, layers=True)
def explode(rng, i):
    # A fire pot bursting: clay smashing, a whoomp of flame, burning bits falling
    clay = [('rd_breaking', 'bfh1_breaking_01.ogg'), ('rd_breaking', 'bfh1_breaking_03.ogg')][i - 1]
    pot = [(KI, 'impactGlass_heavy_001.ogg'), (KI, 'impactGlass_heavy_003.ogg')][i - 1]
    sec = 2.0
    tt = t(sec)
    whoomp = highpass(lowpass(colored(sec, rng, -5), 700), 35) * np.exp(-tt * 3.0) * np.minimum(1, tt / 0.02)
    roar = roar_whoosh(1.2, rng, 0.15)
    cr = crackle(sec, rng, 70) * np.exp(-tt * 1.6)
    bang = at_onset(S('rd_bang', ['bang_03.ogg', 'bang_09.ogg'][i - 1]))
    y = mix((unit(at_onset(S(*clay))), 0, -2), (unit(at_onset(lowpass(S(*pot), 6000))), 0, -8), (unit(bang), 0, -4),
            (unit(whoomp), 0.01, -2), (unit(roar), 0.02, -6), (unit(cr), 0.1, -14))
    y = reverb(highpass(y, 30), 1.2, wet=0.15, damp=0.6)
    y = limit(unit(y) * db(7), -1)
    return trim(y, -50, fade_out=0.3)


@sound('fire_loop', kind='loop', peak_db=-3.0, layers=True)
def fire_loop(rng):
    # A burning fire, 4 s: a recorded fire's crackle reshuffled into a longer bed, with a synthetic roar
    src = S('oga_fire_crackling', 'fire-1.wav')
    sec = 4.4
    bed = grain_shuffle(src, sec, rng, grain=0.35)
    syn = fire_bed(sec, rng, crackle_rate=10, roar=1.0)
    y = mix((unit(bed), 0, 0), (unit(syn), 0, -9))
    y = loopify(y, 0.4)
    return y


def grain_shuffle(src, seconds, rng, grain=0.3):
    """A new stretch of texture: random overlapping pieces of src, each faded in and out."""
    out = zeros(seconds + grain * 2)
    g = n_of(grain)
    win = np.hanning(g)
    pos = 0
    while pos < len(out) - g:
        s = rng.integers(0, max(1, len(src) - g))
        out[pos:pos + g] += src[s:s + g] * win
        pos += g // 2
    return out[n_of(grain):n_of(grain) + n_of(seconds)]


@sound('bell_lure', layers=False)
def bell_lure(rng):
    # A hand bell rung frantically for about 4 s, the clapper hitting both sides
    sec = 4.0
    out = zeros(sec + 1.0)
    f0 = 1180.0
    tt = 0.0
    k = 0
    while tt < sec - 0.05:
        f = f0 * (1 + rng.normal(0, 0.002))
        hit = bell(f, 1.2, rng, 'hand', strike=1.4, decay=1.0, beat=2.5)
        amp = (0.75 + 0.25 * rng.random()) * (0.85 if k % 2 else 1.0)
        place(out, hit, tt, amp)
        tt += rng.uniform(0.11, 0.16)
        k += 1
    # the bell swinging moves it nearer and farther
    m = 1 + 0.15 * np.sin(2 * np.pi * 3.3 * t(len(out) / R))
    y = out * m
    y = y * env_points([(0, 1), (sec - 0.1, 1), (sec + 1.0, 0.0)], len(y) / R) ** 0.5
    y = reverb(y, 0.9, wet=0.12, damp=0.4)
    return trim(y, -50, fade_out=0.3)


# ================================================================ ZOMBIES

ZA, ZV = 'oga_zombies_ad', 'oga_voice_fx'
RC, RC2 = 'rd_creature', 'rd_creature2'


def zvox(x, semis=0.0, grit=0.0, hp=75.0, fade_out=0.08):
    """A voice made more corpse-like: lowered or raised, rasped, the rumble below the voice cut."""
    x = highpass(x, hp, order=4)
    if semis:
        x = pitch(x, semis)
    if grit:
        x = saturate(x, 1.0 + grit * 3.0, mix_wet=min(1.0, grit * 1.5))
    return trim(x, -42, pre=0.01, fade_out=fade_out)


@series('groan', 8, lufs=-16)
def groan(rng, i):
    # Idle moans and growls, 1 to 2.5 s, each at its own pitch
    src = [('oga_zombie_moans_ds', 'darsycho__zombie-moans_0.ogg', 0.25, 2.75, 0.0, 0.0),
           ('oga_zombie_moans_ds', 'darsycho__zombie-moans_0.ogg', 3.62, 6.10, -1.5, 0.1),
           ('oga_hungry_ghoul', 'hungry-ghoul_0.ogg', 5.30, 7.65, -2.5, 0.0),
           ('oga_hungry_ghoul', 'hungry-ghoul_0.ogg', 15.10, 16.95, 1.0, 0.0),
           ('oga_zombie_moans_ep', 'ZombieMoans - Track 5 - ZombieMoan1.wav', 0.0, None, -1.0, 0.0),
           ('oga_zombie_moans_ep', 'ZombieMoans - Track 7 - ZombieMoan3.wav', 0.0, None, -3.5, 0.0),
           ('oga_undead_moans', 'undead-2.ogg', 0.0, 1.80, -2.0, 0.2),
           (ZA, 'zombie-17.wav', 0.0, None, -3.0, 0.0)][i - 1]
    key, rel, t0, t1, semis, grit = src
    x = S(key, rel, t0, t1)
    x = fade(x, 0.02, 0.12)
    if grit:
        x = rasp(x, rng, grit)
    x = zvox(x, semis, grit, fade_out=0.15)
    return cap(compress(x, -16, 2.5, 0.01, 0.2), 2.5, 0.3)


@series('snarl', 4, lufs=-13)
def snarl(rng, i):
    # Attack snarls: short, harsh, loud at the front
    key, rel, semis, grit = [(ZA, 'zombie-10.wav', -1.0, 0.3), ('oga_monster_growls', 'monster.4.ogg', -2.0, 0.2),
                             (ZA, 'zombie-4.wav', 0.5, 0.4), ('oga_monster_growls', 'monster.2.ogg', -1.0, 0.3)][i - 1]
    x = zvox(S(key, rel), semis, grit)
    return compress(x, -14, 3.0, 0.004, 0.1)


@series('scream', 4, lufs=-14)
def scream(rng, i):
    # Sprinters' shrieks: high, raw and a little roomy
    key, rel, semis, grit = [(RC, 'scream_01.ogg', 0.0, 0.3), (RC, 'scream_02.ogg', 2.0, 0.3),
                             (RC2, 'monster_19.ogg', 2.5, 0.4), (ZV, 'zombieYell4.wav', 3.0, 0.5)][i - 1]
    x = zvox(S(key, rel), semis, grit, hp=180)
    x = peq(x, 3000, 3, 1.0)
    x = reverb(x, 0.8, wet=0.12, damp=0.5)
    return trim(x, -45, fade_out=0.1)


@series('pain', 3, lufs=-14)
def pain(rng, i):
    # Short grunts when hit
    key, rel, semis = [(ZA, 'zombie-24.wav', -1.0), (ZV, 'zombieYell6.wav', -2.0), (RC, 'hurt_03.ogg', -4.0)][i - 1]
    return zvox(S(key, rel), semis, 0.3, fade_out=0.05)


@series('death', 4, lufs=-15)
def death(rng, i):
    # Death groans with a wet gurgle under them
    key, rel, semis = [(ZV, 'zombieDeath1.wav', -1.0), (ZV, 'zombieDeath2.wav', -2.0), (ZV, 'zombieDeath4.wav', -1.5), (RC2, 'die_03.ogg', -2.5)][i - 1]
    g = [(RC, 'burble_01.ogg'), (RC, 'burble_02.ogg'), (RC, 'burble_01.ogg'), (RC, 'burble_02.ogg')][i - 1]
    v = zvox(S(key, rel), semis, 0.2)
    gur = zvox(S(*g), [-3.0, -4.0, -6.0, -5.0][i - 1], 0.0)
    y = mix((unit(v), 0, 0), (unit(gur), [0.25, 0.2, 0.3, 0.25][i - 1], -7))
    return trim(y, -45, fade_out=0.15)


@sound('bite')
def bite(rng):
    # A bite and a claw rip on a player: teeth snapping in, flesh tearing, wet
    chomp = at_onset(S(RC, 'eat_02.ogg'))
    rip = at_onset(highpass(S(KR, 'knifeSlice2.ogg', 0.24, 0.42), 500))
    wet = fade(at_onset(S('oga_wetsquish', 'impactsplat03.mp3.flac', 0.0, 0.35)), 0.001, 0.1)
    th = at_onset(S(KI, 'impactPunch_medium_002.ogg'))
    y = mix((unit(chomp), 0, 0), (unit(th), 0.0, -8), (unit(wet), 0.04, -5), (unit(rip), 0.12, -6))
    return trim(y, -42, fade_out=0.08)


def nail_squeal(seconds, rng, f0=1900.0):
    """A nail dragged out of wood: a scratchy, stick-slip squeal."""
    n = n_of(seconds)
    f = f0 * (1 + 0.25 * t(seconds) / seconds + 0.03 * lowpass(rng.standard_normal(n), 30) / 0.05)
    s = bandpass(saw_blep(f, seconds), 1200, 7000)
    stick = np.clip(0.5 + lowpass(rng.standard_normal(n), 40) / 0.08, 0, 1)
    return s * stick * env_points([(0, 0), (0.03, 1), (seconds * 0.8, 0.7), (seconds, 0)], seconds)


@series('board_rip', 3, layers=True)
def board_rip(rng, i):
    # A zombie tearing a nailed board off a window: strain creak, a squealing nail, a crack, the board free
    creak = at_onset(S(KR, ['creak1.ogg', 'creak2.ogg', 'creak1.ogg'][i - 1]))
    crack = at_onset(S('rd_metalwood', ['wood_cracking_04.ogg', 'wood_breaking_01.ogg', 'wood_cracking_03.ogg'][i - 1]))
    brk = at_onset(S('rd_metalwood', ['wood_breaking_01.ogg', 'wood_breaking_02.ogg', 'wood_falling_03.ogg'][i - 1]))
    knock = at_onset(S(KI, ['impactPlank_medium_001.ogg', 'impactPlank_medium_003.ogg', 'impactWood_heavy_004.ogg'][i - 1]))
    sq = nail_squeal(0.25, rng, [1900, 2300, 1700][i - 1])
    c0 = [0.0, 0.05, 0.0][i - 1]
    y = mix((unit(pitch(creak, [-3, -2, -4][i - 1])), c0, -4), (unit(sq), 0.30, -16), (unit(crack), 0.42, 0),
            (unit(brk), 0.47, -3), (unit(knock), 0.47, -6))
    return trim(y, -45, fade_out=0.1)


@series('board_place', 3)
def board_place(rng, i):
    # A player nailing a board back: the board set against the frame, then two quick hammer knocks
    knock = [('rd_metalwood', 'wood_hammer_01.ogg'), ('rd_metalwood', 'wood_hammer_02.ogg'), ('rd_metalwood', 'wood_hammer_01.ogg')][i - 1]
    nail = [('rd_metalwood', 'hammer_02.ogg'), ('rd_metalwood', 'hammer_03.ogg'), ('rd_metalwood', 'hammer_04.ogg')][i - 1]
    setb = at_onset(S(KI, ['impactPlank_medium_000.ogg', 'impactPlank_medium_002.ogg', 'impactWood_medium_001.ogg'][i - 1]))
    k = at_onset(S(*knock))
    n = at_onset(S(*nail))
    gap = [0.17, 0.15, 0.19][i - 1]
    y = mix((unit(setb), 0, -8), (unit(k), 0.12, 0), (unit(n), 0.12, -6), (unit(pitch(k, 0.7)), 0.12 + gap, -1), (unit(n), 0.12 + gap, -5))
    return trim(y, -45, fade_out=0.06)


@sound('rise', layers=True)
def rise(rng):
    # A zombie clawing out of a grave, about 1.5 s: earth shifting and spilling, a low heave
    parts = [('rd_mud', 'mud_04.ogg', 0.0, -2), ('rd_mud', 'mud_17.ogg', 0.25, -3), ('rd_mud', 'mud_02.ogg', 0.55, -1),
             ('rd_mud', 'mud_19.ogg', 0.85, -4), ('rd_breaking', 'bfh1_rock_falling_01.ogg', 0.95, -10)]
    layers = [(unit(at_onset(lowpass(S(k, f), 7000))), at, g) for k, f, at, g in parts]
    sec = 1.6
    rumble = lowpass(colored(sec, rng, -6), 160) * env_points([(0, 0), (0.4, 1), (1.2, 0.8), (1.6, 0)], sec)
    y = mix(*layers, (unit(rumble), 0, -8))
    return trim(y, -45, fade_out=0.2)


# ================================================================ PLAYER AND WORLD

FZ = 'oga_fantozzi_steps'


@series('step_stone', 4)
def step_stone(rng, i):
    # Boots on cobbles: a gritty scuff on stone with a hard heel tap under it
    a = ['Fantozzi-StoneL1.flac', 'Fantozzi-StoneR1.flac', 'Fantozzi-StoneL2.flac', 'Fantozzi-StoneR2.flac'][i - 1]
    x = fade(at_onset(S(FZ, a)), 0.001, 0.08)
    heel = at_onset(S(KI, f'footstep_concrete_00{i - 1}.ogg'))
    y = mix((unit(highpass(x, 120)), 0, 0), (unit(heel), 0, -6))
    return trim(y, -40, fade_out=0.04)


@series('step_wood', 4)
def step_wood(rng, i):
    # Boots on floorboards: a hollow board thump with a little wooden click
    thump = at_onset(S(KI, f'footstep_wood_00{i - 1}.ogg'))
    top = at_onset(S('rd_sfx100v2', f"sfx100v2_footstep_wood_0{[1, 2, 3, 4][i - 1]}.ogg"))
    y = mix((unit(lowpass(thump, 3000)), 0, 0), (unit(highpass(top, 250)), 0, -5))
    y = peq(y, 180, 3, 1.0)
    return trim(y, -40, fade_out=0.04)


@series('step_dirt', 4)
def step_dirt(rng, i):
    # Boots on a packed-earth path: heel and toe in dirt, a little grit
    x = at_onset(S(KR, f'footstep0{[0, 1, 2, 3][i - 1]}.ogg'))
    g = fade(at_onset(S(FZ, ['Fantozzi-SandL1.flac', 'Fantozzi-SandR1.flac', 'Fantozzi-SandL2.flac', 'Fantozzi-SandR2.flac'][i - 1])), 0.001, 0.1)
    y = mix((unit(x), 0, 0), (unit(highpass(g, 300)), 0, -12))
    return trim(y, -40, fade_out=0.04)


@sound('land')
def land(rng):
    # Landing from a jump: both boots hit, a body thump and a rustle of clothes
    a = at_onset(S(KR, 'footstep04.ogg'))
    b = at_onset(S(KR, 'footstep08.ogg'))
    th = at_onset(lowpass(S(KI, 'impactSoft_heavy_001.ogg'), 1200))
    cl = at_onset(S(KR, 'cloth2.ogg'))
    y = mix((unit(a), 0, 0), (unit(b), 0.025, -2), (unit(th), 0.0, -4), (unit(cl), 0.02, -12))
    return trim(y, -42, fade_out=0.06)


def gasp(seconds, rng, vowel='ah', shift=1.0):
    """A sharp, unvoiced intake of breath: noise through a mouth's formants, rising a little."""
    x = white(seconds, rng)
    fm = VOWELS[vowel]

    def g(tm, f):
        k = shift * (1 + 0.15 * min(max(tm / seconds, 0), 1))
        return formant_gain(f / k, fm) + 0.15 * band_gain(f, 5000, 1.5)
    y = stft_filter(x, g, 1024, 256)
    return highpass(y, 300) * env_points([(0, 0), (0.025, 1), (seconds * 0.45, 0.7), (seconds, 0)], seconds)


@series('hurt', 3, layers=True)
def hurt(rng, i):
    # The player is hit: a body blow, then a sharp gasp
    th = at_onset(S(KI, ['impactPunch_heavy_003.ogg', 'impactPunch_heavy_000.ogg', 'impactPunch_heavy_004.ogg'][i - 1]))
    gs = gasp([0.32, 0.26, 0.36][i - 1], rng, ['ah', 'eh', 'oh'][i - 1], [1.0, 1.15, 0.9][i - 1])
    y = mix((unit(th), 0, 0), (body_thump(0.2, rng, 65, 15), 0, -8), (unit(gs), 0.05, -5))
    return trim(y, -42, fade_out=0.08)


@sound('heartbeat', kind='loop', peak_db=-2.0, layers=True)
def heartbeat(rng):
    # A racing heart, 1.6 s with two beats: a recorded lub-dub given a knock you can hear on small speakers
    beat = S('oga_heartbeat_slow', 'heartbeat_slow_0.wav', 0.0, 0.62)
    beat = saturate(lowpass(beat, 400), 2.5, 0.6)
    sec = 1.6
    out = zeros(sec)
    knock = lambda f, g: thunk(0.18, rng, f, 30, click=0.05, click_hz=800) * db(g)
    for at in (0.04, 0.84):
        place(out, unit(beat), at, 1.0, wrap=True)
        place(out, knock(70, -10), at + 0.0, 1.0, wrap=True)
        place(out, knock(62, -6), at + 0.30, 1.0, wrap=True)
    return out


@sound('downed', layers=True)
def downed(rng):
    # The player falls, badly hurt: a body hitting the ground, a dull boom, a ring in the ears, a sinking chord
    sec = 2.8
    tt = t(sec)
    fall = at_onset(lowpass(S(KI, 'impactSoft_heavy_002.ogg'), 1500))
    cl = at_onset(S(KR, 'cloth3.ogg'))
    boom = lowpass(colored(sec, rng, -6), 120) * np.exp(-tt * 2.2) * np.minimum(1, tt / 0.01)
    glide = 2 ** (-3 / 12 * np.clip(tt / 2.0, 0, 1))
    tones = sum(saw_blep(f * glide * (1 + d), sec) for f, d in [(73.4, 0.0), (73.4, 0.004), (87.3, -0.003), (110.0, 0.002)])
    tones = svf(tones, 900 * np.exp(-tt * 0.9) + 120, 0.8, 'lp') * env_points([(0, 0), (0.15, 1), (2.0, 0.7), (sec, 0)], sec)
    ring = sine(3150, sec) * env_points([(0, 0), (0.3, 1), (1.6, 0.6), (sec, 0)], sec)
    y = mix((unit(fall), 0, 0), (unit(cl), 0.01, -10), (unit(boom), 0, -3), (unit(tones), 0.05, -7), (unit(ring), 0.1, -26))
    y = reverb(y, 2.2, wet=0.25, damp=0.7)
    return trim(y, -50, fade_out=0.4)


def chime_note(f, seconds, rng, bright=1.0):
    return bell(f, seconds, rng, 'chime', strike=0.4 * bright, decay=1.0, beat=0.8)


@sound('revive')
def revive(rng):
    # A teammate back on their feet: a rising, hopeful arpeggio of chimes over a swelling choir chord
    notes = [293.66, 369.99, 440.0, 587.33, 739.99, 880.0]
    out = zeros(2.6)
    for k, f in enumerate(notes):
        place(out, chime_note(f * 2, 1.6, rng), 0.09 * k, 0.8 + 0.04 * k)
    ch = choir([293.66, 369.99, 440.0, 587.33], 2.2, rng, 'ah', voices_per=3, attack=0.5, release=0.8)
    y = mix((out, 0, 0), (unit(ch), 0.1, -9))
    y = reverb(y, 1.8, wet=0.3, damp=0.3, bright=12000)
    return trim(y, -50, fade_out=0.4)


@sound('breath', kind='loop', peak_db=-3.0)
def breath(rng):
    # Heavy, tired breathing, 3 s: two breaths of a recording, a touch slower, hum removed, looped
    x = S('oga_breathing', 'breathing_tired.wav', 0.0, 2.65)
    x = peq(peq(x, 132.5, -30, 6), 265, -15, 6)  # a steady 133 Hz hum in the room
    x = highpass(x, 160, order=4)
    x = peq(x, 3000, 2, 1.0)
    x = resample(x, 0.8)
    return loopify(x, 0.25)


@sound('coins')
def coins(rng):
    # Paying: a handful of coins jingling
    a = at_onset(S(KR, 'handleCoins.ogg'))
    b = at_onset(S('rd_rpg', 'item_coins_02.ogg'))
    y = mix((unit(b), 0, 0), (unit(a), 0.04, -3))
    return trim(y, -45, fade_out=0.08)


@sound('deny', layers=True)
def deny(rng):
    # Can't afford it: a dull wooden knock and a low, falling two-note buzz
    k = at_onset(S(KI, 'impactWood_medium_003.ogg'))
    sec = 0.42
    tt = t(sec)
    f = np.where(tt < 0.16, 130.8, 110.0)
    tone = lowpass(square_blep(f, sec, 0.35), 900) * env_points([(0, 0), (0.01, 1), (0.14, 0.8), (0.16, 0.2), (0.18, 0.9), (0.36, 0.6), (sec, 0)], sec)
    y = mix((unit(k), 0, 0), (unit(tone), 0.02, -9))
    return trim(y, -45, fade_out=0.05)


@sound('door_open')
def door_open(rng):
    # A heavy door: the bar thrown off and dropped, the latch, then a long low creak as it swings open
    bar = at_onset(S(KI, 'impactPlank_medium_003.ogg'))
    drop = at_onset(S('rd_metalwood', 'wood_slam_04.ogg'))
    latch = at_onset(S(KR, 'doorOpen_1.ogg'))
    cr1 = pitch(at_onset(S('oga_rpg_ad', 'world/door.wav')), -4)
    cr2 = pitch(at_onset(S(KR, 'creak2.ogg')), -6)
    stop = at_onset(lowpass(S(KI, 'impactWood_heavy_002.ogg'), 1500))
    y = mix((unit(bar), 0, -2), (unit(drop), 0.22, -4), (unit(latch), 0.38, -4), (unit(cr1), 0.55, 0), (unit(cr2), 0.9, -7), (unit(stop), 1.95, -9))
    return trim(highpass(y, 60), -48, fade_out=0.15)


@sound('debris_clear', layers=True)
def debris_clear(rng):
    # A heap of crates and cart wreckage collapsing out of the way, about 1.5 s
    MW = 'rd_metalwood'
    parts = [(MW, 'wood_breaking_02.ogg', 0.0, 0), (MW, 'wood_falling_01.ogg', 0.05, -1), (KI, 'impactWood_heavy_000.ogg', 0.12, -3),
             (MW, 'wood_falling_05.ogg', 0.3, -2), ('rd_breaking', 'bfh1_wood_falling_01.ogg', 0.45, -3), (KI, 'impactPlank_medium_002.ogg', 0.62, -5),
             (MW, 'wood_falling_02.ogg', 0.8, -5), ('rd_breaking', 'bfh1_rock_falling_01.ogg', 0.5, -12), (MW, 'wood_falling_04.ogg', 1.05, -8)]
    sec = 1.6
    rumble = lowpass(colored(sec, rng, -6), 150) * env_points([(0, 0), (0.05, 1), (0.8, 0.6), (sec, 0)], sec)
    y = mix(*[(unit(at_onset(S(k, f))), at, g) for k, f, at, g in parts], (unit(rumble), 0, -8))
    return trim(y, -48, fade_out=0.2)


@sound('power_on', layers=True)
def power_on(rng):
    # The great brazier lit: a deep inrush, a roaring ignition, then fire and a rising horn call
    sec = 4.2
    tt = t(sec)
    rush = whoosh(0.7, rng, 120, 1500, 0.95, q=0.8, curve=2.0, rumble=0.8)
    ign = mix((lowpass(colored(1.5, rng, -5), 600) * np.exp(-t(1.5) * 2.5) * np.minimum(1, t(1.5) / 0.01), 0, 0),
              (unit(at_onset(S('rd_rpg', 'spell_fire_06.ogg'))), 0, -3))
    bed = grain_shuffle(S('oga_fire_crackling', 'fire-1.wav'), 3.6, rng, 0.35)
    bed = unit(bed) * env_points([(0, 0), (0.15, 1), (3.0, 0.7), (3.6, 0)], 3.6)
    sw = env_points([(0, 0), (0.6, 0.55), (2.2, 0.85), (3.0, 1.0), (3.6, 0)], 3.6)
    glide = lambda f0, f1: f0 + (f1 - f0) * np.clip(t(3.6) / 1.4, 0, 1) ** 0.6
    h1 = horn(glide(110.0, 146.83), 3.6, rng, swell=sw)
    h2 = horn(220.0, 3.6, rng, swell=sw * np.clip((t(3.6) - 1.3) / 0.8, 0, 1))
    h3 = horn(293.66, 3.6, rng, swell=sw * np.clip((t(3.6) - 2.0) / 0.8, 0, 1))
    horns = reverb(h1 + 0.7 * h2 + 0.55 * h3, 2.0, wet=0.25, damp=0.5)
    y = mix((unit(rush), 0, -4), (unit(ign), 0.62, 0), (bed, 0.62, -9), (unit(horns), 0.7, -4))
    return cap(trim(y, -50, fade_out=0.4), 4.5, 0.6)


def sparkle(seconds, rng, count=30, lo=2500, hi=9000, rise=0.0):
    """Little glittering pings, optionally climbing in pitch over the time."""
    out = zeros(seconds + 0.5)
    for _ in range(count):
        at = rng.uniform(0, seconds)
        u = at / seconds
        f = rng.uniform(lo, hi) * 2 ** (rise * (u - 0.5))
        p = modal([f, f * 2.01], [1, 0.3], [rng.uniform(10, 25)] * 2, 0.4, rng) * rng.uniform(0.4, 1)
        place(out, p, at)
    return out


@sound('chest_open', layers=True)
def chest_open(rng):
    # The mystery chest's old lid creaking open, then a magical shimmer
    lid = at_onset(S('oga_open_chest', 'open_chest_0.wav', 0.0, 1.95))
    lid = fade(highpass(lid, 80), 0.002, 0.3)
    arp = zeros(1.6)
    for k, f in enumerate([880.0, 1108.7, 1318.5, 1760.0, 2217.5, 2637.0]):
        place(arp, bell(f, 1.2, rng, 'glock', strike=0.3), 0.06 * k, 0.7)
    sp = sparkle(1.4, rng, 30, 3000, 9000, rise=1.0)
    pad = choir([440.0, 554.37, 659.25], 1.8, rng, 'oo', voices_per=2, attack=0.4, release=0.8)
    magic = reverb(mix((arp, 0, 0), (unit(sp), 0.1, -8), (unit(pad), 0.0, -12)), 1.6, wet=0.35, damp=0.3, bright=12000)
    y = mix((unit(lid), 0, 0), (unit(magic), 0.55, -3))
    return trim(y, -50, fade_out=0.4)


@sound('chest_spin')
def chest_spin(rng):
    # The chest choosing a weapon, 4.5 s: magical chimes cycling, slowing down, and a bright ding to finish
    scale = [440.0, 523.25, 587.33, 659.25, 783.99, 880.0, 1046.5, 1174.66]
    pattern = [0, 4, 2, 6, 1, 5, 3, 7, 2, 4]
    sec = 4.5
    out = zeros(sec + 2.0)
    tm, k = 0.0, 0
    while tm < 3.3:
        u = tm / 3.3
        gap = 0.075 + 0.26 * u ** 2.2
        f = scale[pattern[k % len(pattern)]] * 2
        place(out, bell(f, 0.6, rng, 'glock', strike=0.25, decay=1.6) * (0.55 + 0.25 * rng.random()), tm)
        tm += gap
        k += 1
    ding_at = 3.5
    ding = mix(*[(bell(f, 1.6, rng, 'chime', strike=0.6, decay=1.4), 0, g) for f, g in [(1760.0, 0), (2217.46, -4), (2637.02, -5), (880.0, -6)]])
    place(out, unit(ding) * 1.4, ding_at)
    pad = choir([220.0, 329.63, 440.0], sec, rng, 'oo', voices_per=2, attack=0.6, release=0.6)
    place(out, unit(pad) * 0.18, 0.0)
    place(out, unit(sparkle(0.8, rng, 25, 3000, 10000)) * 0.35, ding_at)
    y = reverb(out, 1.2, wet=0.22, damp=0.3, bright=12000)
    return cap(trim(y, -50, fade_out=0.4), 4.8, 0.8)


@sound('chest_close')
def chest_close(rng):
    # The lid swinging shut: a short creak, a heavy wooden clap, the hasp
    cr = pitch(at_onset(S(KR, 'creak3.ogg')), -3)
    clap = at_onset(S(KR, 'doorClose_4.ogg'))
    th = at_onset(S('rd_metalwood', 'wood_close_01.ogg'))
    hasp = at_onset(S(KR, 'metalLatch.ogg'))
    y = mix((unit(cr), 0, -8), (unit(clap), 0.18, 0), (unit(th), 0.18, -4), (unit(hasp), 0.3, -8))
    return trim(y, -45, fade_out=0.08)


@sound('chest_flee', layers=True)
def chest_flee(rng):
    # The chest flying off to somewhere else, about 3 s: a mocking cackle, a crow, a whoosh rising away
    cackle = S('oga_evil_cackle', 'evil_cackle_laugh_1.ogg', 1.82, 3.80)
    cackle = reverb(pitch(highpass(cackle, 150), -3), 1.6, wet=0.35, damp=0.5)
    crow = pitch(at_onset(S('oga_crow_caw', 'crow_caw.wav')), -1)
    sec = 2.2
    rise = whoosh(sec, rng, 200, 3500, 0.75, q=1.0, curve=1.5, rumble=0.4)
    rise = svf(rise, np.linspace(8000, 1500, n_of(sec)), 0.7, 'lp')
    sp = reverse(reverb(sparkle(0.8, rng, 25, 3000, 9000), 1.2, wet=0.6))
    y = mix((unit(cackle), 0, 0), (unit(crow), 0.35, -5), (unit(rise), 0.6, -4), (unit(sp), 1.2, -12))
    return cap(trim(y, -50, fade_out=0.4), 3.2, 0.6)


@sound('chest_land', layers=True)
def chest_land(rng):
    # The chest crashing down in its new place: a heavy thud, dust, and a chime
    a = at_onset(S(KI, 'impactWood_heavy_000.ogg'))
    b = at_onset(S(KI, 'impactPlank_medium_000.ogg'))
    dust = at_onset(S('rd_breaking', 'bfh1_rock_falling_01.ogg'))
    ch = mix(*[(chime_note(f, 1.8, rng), 0.0, g) for f, g in [(1318.5, 0), (1661.2, -4), (1975.5, -5)]])
    y = mix((unit(a), 0, 0), (unit(b), 0.005, -4), (body_thump(0.4, rng, 48, 9), 0, -3), (unit(dust), 0.05, -14), (unit(ch), 0.08, -9))
    y = reverb(y, 1.2, wet=0.15, damp=0.4)
    return trim(y, -50, fade_out=0.3)


@sound('drink', layers=False)
def drink(rng):
    # Downing a potion: the cork, three glugs, and a satisfied breath out
    BD = 'oga_bottle_drink'
    cork = at_onset(S(BD, 'bottle-open-03.flac', 0.85, 1.08))
    g1 = at_onset(highpass(S(BD, 'swallow-01.flac'), 150))
    g2 = at_onset(highpass(S(BD, 'swallow-04.flac'), 150))
    g3 = at_onset(highpass(S(BD, 'swallow-03.flac'), 150))
    ah = at_onset(highpass(S(RC, 'breath.ogg'), 200))
    y = mix((unit(cork), 0, -3), (unit(g1), 0.3, 0), (unit(g2), 0.62, -1), (unit(g3), 0.95, -2), (unit(pitch(ah, -2)), 1.35, -4))
    return trim(y, -48, fade_out=0.15)


@sound('bottle_smash')
def bottle_smash(rng):
    # An empty bottle shattering on the ground
    a = at_onset(S(KI, 'impactGlass_heavy_002.ogg'))
    b = at_onset(S('rd_breaking', 'bfh1_glass_breaking_06.ogg'))
    c = at_onset(S('rd_breaking', 'bfh1_glass_breaking_02.ogg'))
    y = mix((unit(a), 0, -3), (unit(b), 0.0, 0), (unit(c), 0.03, -5))
    return trim(y, -48, fade_out=0.1)


@sound('forge', layers=True)
def forge(rng):
    # The Runeforge at work, about 4 s: hammer on anvil, then a magical shimmer rising to a peak
    a = at_onset(S('oga_anvil', 'forger.ogg'))
    hm = at_onset(S('oga_smith_hammer', 'blacksmithhammer.wav'))
    sec = 4.2
    tt = t(sec)
    riser = colored(sec, rng, -1)
    riser = stft_filter(riser, lambda tm, f: band_gain(f, 600 * 2 ** (3.2 * min(max((tm - 1.0) / 3.0, 0), 1)), 0.6))
    riser *= env_points([(0, 0), (1.2, 0), (3.6, 1), (sec, 0)], sec)
    sp = sparkle(2.6, rng, 60, 2000, 7000, rise=2.5)
    ch = choir([146.83 * 2, 220.0 * 2, 293.66 * 2], 2.8, rng, 'ah', voices_per=3, attack=1.6, release=0.5)
    magic = reverb(mix((unit(riser), 1.0, -6), (unit(sp), 1.4, -6), (unit(ch), 1.3, -8)), 1.8, wet=0.3, damp=0.3, bright=12000)
    y = mix((unit(a), 0, 0), (unit(hm), 1.15, -2), (unit(magic), 0, -2))
    return trim(y[:n_of(sec + 0.6)], -50, fade_out=0.3)


@sound('forge_ready')
def forge_ready(rng):
    # The upgraded weapon ready: a bright, ringing chime chord with a glitter
    ch = mix(*[(bell(f, 2.0, rng, 'chime', strike=0.7), d, g) for f, d, g in [(1174.66, 0, 0), (1479.98, 0.03, -3), (1760.0, 0.06, -3), (2349.3, 0.09, -6)]])
    sp = sparkle(0.6, rng, 20, 4000, 10000)
    y = reverb(mix((unit(ch), 0, 0), (unit(sp), 0.02, -10)), 1.5, wet=0.3, damp=0.2, bright=14000)
    return trim(y, -50, fade_out=0.3)


@sound('powerup_spawn')
def powerup_spawn(rng):
    # A glowing power-up appearing: glitter climbing up, a soft swell under it
    sp = sparkle(0.9, rng, 40, 1800, 6000, rise=2.0)
    sw = colored(1.0, rng, -2)
    sw = stft_filter(sw, lambda tm, f: band_gain(f, 800 * 2 ** (2.5 * min(max(tm, 0), 1)), 0.5))
    sw *= env_points([(0, 0), (0.7, 1), (1.0, 0)], 1.0)
    pad = choir([523.25, 659.25, 783.99], 1.2, rng, 'oo', voices_per=2, attack=0.5, release=0.5)
    y = mix((unit(sp), 0, -2), (unit(sw), 0, -10), (unit(pad), 0, -10))
    y = reverb(y, 1.4, wet=0.3, damp=0.2, bright=12000)
    return trim(y, -50, fade_out=0.3)


@sound('powerup_loop', kind='loop', peak_db=-6.0)
def powerup_loop(rng):
    # A power-up's soft magical hum, 2 s. Every partial and wobble repeats a whole number of times in 2 s
    sec = 2.0
    tt = t(sec)
    out = np.zeros(len(tt))
    for f, a in [(220.0, 1.0), (221.5, 0.8), (330.0, 0.5), (440.0, 0.35), (441.5, 0.3), (660.0, 0.15), (880.0, 0.1), (1320.0, 0.06)]:
        out += a * np.sin(2 * np.pi * f * tt)
    out *= 0.85 + 0.15 * np.sin(2 * np.pi * 1.0 * tt)
    # a breathy shimmer: noise filtered round the circle (an FFT of the whole loop) so it wraps too
    nz = rng.standard_normal(len(tt))
    Z = np.fft.rfft(nz)
    f = np.fft.rfftfreq(len(tt), 1 / R)
    nz = np.fft.irfft(Z * (band_gain(f, 4500, 1.0) + 0.6 * band_gain(f, 1800, 0.4)), len(tt))
    nz *= 0.75 + 0.25 * np.sin(2 * np.pi * 0.5 * tt + 1.0)
    return unit(out) + 0.12 * unit(nz)


@sound('powerup_grab')
def powerup_grab(rng):
    # Picking up a power-up: a quick bright upward run and a ringing chime
    out = zeros(1.6)
    for k, f in enumerate([987.77, 1318.5, 1567.98, 1975.53, 2637.02]):
        place(out, bell(f, 0.9, rng, 'glock', strike=0.3), 0.035 * k, 0.6 + 0.1 * k)
    ch = chime_note(2637.02, 1.4, rng)
    y = reverb(mix((out, 0, 0), (unit(ch), 0.17, -4), (unit(sparkle(0.4, rng, 15, 5000, 11000)), 0.15, -12)), 1.2, wet=0.25, damp=0.2, bright=14000)
    return trim(y, -50, fade_out=0.25)


def brass_chord(freqs, seconds, rng, attack=0.04, release=0.3, gains=None):
    out = zeros(seconds)
    for k, f in enumerate(freqs):
        g = gains[k] if gains else 1.0
        tt = t(seconds)
        sw = np.minimum(1, tt / attack) * np.clip((seconds - tt) / release, 0, 1) * (0.85 + 0.15 * np.exp(-tt * 3))
        out += g * horn(f * (1 + rng.normal(0, 0.0015)), seconds, rng, swell=sw)
    return out


@sound('pu_max_ammo', layers=True)
def pu_max_ammo(rng):
    # Full ammo: a rising brass fanfare and a clatter of bolts into a quiver
    out = zeros(2.6)
    notes = [(293.66, 0.0, 0.16), (369.99, 0.16, 0.16), (440.0, 0.32, 0.16), (587.33, 0.48, 1.3)]
    for f, at, d in notes:
        place(out, brass_chord([f, f / 2], d, rng, release=0.08 if d < 0.5 else 0.6), at)
    clat = mix((unit(at_onset(S('oga_weapons_apparel', 'arrow-return-to-quiver-02.wav'))), 0, 0),
               (unit(at_onset(S('oga_weapons_apparel', 'arrow-return-to-quiver-06.wav'))), 0.25, -2))
    tim = frame_drum(0.6, rng, 110, slap=0.6)
    y = mix((unit(out), 0, 0), (unit(clat), 0.05, -9), (unit(tim), 0.48, -6))
    y = reverb(y, 1.6, wet=0.22, damp=0.4)
    return trim(y, -50, fade_out=0.3)


@sound('pu_instakill')
def pu_instakill(rng):
    # One-hit kills: a swelling breath into a dark choir stab and a deep, doomful bell
    sw = reverse(reverb(mix(bell(587.33, 1.2, rng, 'chime'), (sparkle(0.3, rng, 10, 2000, 6000), 0, -6)), 1.4, wet=0.8))
    sw = sw[-n_of(0.6):]
    hit = 0.6
    ch = choir([73.42, 146.83, 174.61, 220.0, 277.18], 1.9, rng, 'oh', voices_per=3, attack=0.03, release=0.9, vib=0.008)
    b = bell(146.83, 3.0, rng, 'church', strike=1.2)
    boom = lowpass(colored(1.5, rng, -6), 120) * np.exp(-t(1.5) * 3) * np.minimum(1, t(1.5) / 0.005)
    y = mix((unit(sw), 0, -6), (unit(ch), hit, 0), (unit(b), hit, -3), (unit(boom), hit, -4))
    y = reverb(y, 2.4, wet=0.25, damp=0.5)
    return cap(trim(y, -50, fade_out=0.4), 3.2, 0.8)


@sound('pu_double', layers=True)
def pu_double(rng):
    # Double points: the same bright two-note call twice, the second higher, with coins
    out = zeros(2.0)
    for at, base in [(0.0, 880.0), (0.36, 1174.66)]:
        for d, f in [(0.0, base), (0.11, base * 1.5)]:
            place(out, bell(f, 1.0, rng, 'glock', strike=0.4), at + d, 0.9)
            place(out, brass_chord([f / 2], 0.14, rng, release=0.06), at + d, 0.35)
    coins_ = at_onset(S('rd_rpg', 'item_coins_03.ogg'))
    y = mix((unit(out), 0, 0), (unit(coins_), 0.36, -8))
    y = reverb(y, 1.3, wet=0.25, damp=0.3, bright=12000)
    return trim(y, -50, fade_out=0.3)


@sound('pu_nuke', layers=True)
def pu_nuke(rng):
    # A holy blast that kills them all: a white flash of noise, a huge bright choir chord, bells, thunder rolling
    sec = 4.5
    flash = colored(1.0, rng, -2) * np.exp(-t(1.0) * 5) * np.minimum(1, t(1.0) / 0.003)
    ch = choir([146.83, 220.0, 293.66, 369.99, 440.0, 587.33], 3.0, rng, 'ah', voices_per=3, attack=0.05, release=1.4, vib=0.01)
    bells = mix((bell(587.33, 4.0, rng, 'church', strike=1.0), 0, 0), (bell(880.0, 3.5, rng, 'church', strike=0.8), 0.02, -4))
    th = S('rd_sfx100v2', 'sfx100v2_thunder_01.ogg', 0.6, 5.2)
    th = fade(th, 0.01, 0.8)
    boom = lowpass(colored(2.0, rng, -6), 100) * np.exp(-t(2.0) * 2) * np.minimum(1, t(2.0) / 0.005)
    y = mix((unit(flash), 0, -4), (unit(boom), 0, -2), (unit(ch), 0.0, -2), (unit(bells), 0.0, -6), (unit(th), 0.1, -1))
    y = reverb(y, 2.6, wet=0.3, damp=0.5)
    y = limit(unit(y) * db(3), -1)
    return cap(trim(y, -50, fade_out=0.6), sec + 0.3, 1.0)


@sound('pu_carpenter', layers=True)
def pu_carpenter(rng):
    # Every window boarded: rapid hammering that speeds up, then a short brass flourish
    out = zeros(2.8)
    tm = 0.0
    k = 0
    while tm < 1.45:
        src = ['wood_hammer_01.ogg', 'wood_hammer_02.ogg'][k % 2]
        h = at_onset(S('rd_metalwood', src))
        place(out, pitch(unit(h), rng.uniform(-1, 1)), tm, 0.7 + 0.3 * rng.random())
        tm += max(0.07, 0.17 - 0.012 * k)
        k += 1
    fl = zeros(1.2)
    for f, at in [(392.0, 0.0), (493.88, 0.09), (587.33, 0.18)]:
        place(fl, brass_chord([f, f / 2], 1.0 - at, rng, release=0.4), at)
    y = mix((unit(out), 0, 0), (unit(reverb(fl, 1.4, wet=0.2)), 1.5, -2))
    return trim(y, -50, fade_out=0.3)


@sound('pu_end')
def pu_end(rng):
    # A power-up wearing off: soft chimes falling away and dimming
    out = zeros(1.8)
    for k, f in enumerate([1174.66, 880.0, 698.46, 587.33]):
        place(out, bell(f, 1.0, rng, 'glock', strike=0.2) * (1 - 0.15 * k), 0.13 * k)
    out = svf(out, np.linspace(9000, 1500, len(out)), 0.7, 'lp')
    y = reverb(out, 1.5, wet=0.35, damp=0.4)
    return trim(y, -50, fade_out=0.3)


@sound('points_tick')
def points_tick(rng):
    # A tiny, soft tick for score
    sec = 0.05
    tt = t(sec)
    y = sine(2350, sec) * np.exp(-tt * 140) + 0.4 * sine(4700, sec) * np.exp(-tt * 220)
    y += 0.15 * highpass(white(sec, rng), 3000) * np.exp(-tt * 600)
    return fade(y, 0.0005, 0.01)


@series('orb', 3, layers=False)
def orb(rng, i):
    # An experience orb taken: Kenney's glass tings, brightened, a soft pluck an octave up under each, a little room
    glass = highpass(at_onset(S('kenney_interface', ('glass_001.ogg', 'glass_003.ogg', 'glass_005.ogg')[i - 1])), 900)
    pluck = pitch(highpass(at_onset(S('kenney_interface', 'pluck_001.ogg')), 600), 12)
    y = mix((unit(glass), 0, 0), (unit(pluck), 0.0, -9))
    y = reverb(y, rt60=0.6, wet=0.14, predelay=0.008, damp=0.4)
    return trim(unit(y) * db(-2), -48, fade_out=0.05)


@sound('hitmarker')
def hitmarker(rng):
    # A small dry tick when a hit lands: Kenney's interface click
    return trim(highpass(at_onset(S('kenney_interface', 'click_002.ogg')), 800), -50, fade_out=0.01)


# The Engineer's flamethrower, made from nothing: a jet's roar to loop while the trigger's held, and its lighting
@sound('flamer_loop', kind='loop', peak_db=-3.0)
def flamer_loop(rng):
    # A gas jet tearing through air (a broad hiss, rough and quick-flickering), a low roar under it, a few crackles
    sec = 2.4
    n = n_of(sec)
    flick = 1 + 0.35 * lowpass(rng.standard_normal(n), 14) / 0.05
    jet = bandpass(colored(sec, rng, -2.0), 500, 6500) * np.clip(flick, 0.4, 1.8)
    roar = lowpass(highpass(colored(sec, rng, -5.0), 45), 420) * np.clip(1 + 0.3 * lowpass(rng.standard_normal(n), 5) / 0.03, 0.5, 1.6)
    bed = fire_bed(sec, rng, crackle_rate=18, roar=0.6)
    y = mix((unit(jet), 0, -3), (unit(roar), 0, 0), (unit(bed), 0, -8))
    return loopify(y, 0.3)


@sound('flamer_start', layers=False)
def flamer_start(rng):
    # The jet lit: a quick rush of fire through air and a soft thump of the gas catching
    tt = t(0.6)
    w = roar_whoosh(0.6, rng, peak_at=0.08)
    thump = lowpass(colored(0.6, rng, -5), 300) * np.exp(-tt * 12) * np.minimum(1, tt / 0.004)
    y = mix((unit(w), 0, 0), (unit(thump), 0, -3))
    return trim(y, -45, fade_out=0.15)


# A hit of your own landing, made from nothing: crisp enough to hear over the gun, short enough for an SMG's rate
@sound('hit_tick')
def hit_tick(rng):
    # A small struck plate, the head's ding's little brother (the owner liked that one, and couldn't hear the first tick
    # over the guns): a bright metallic tink, its partials ringing a tenth of a second, over a snap of noise and a knock
    y = modal([2650, 2650 * 2.76, 2650 * 5.40], [1.0, 0.45, 0.2], [32, 48, 70], 0.16, rng)
    snap = highpass(white(0.025, rng), 4000) * np.exp(-t(0.025) * 220)
    y = mix((unit(y), 0, -1), (unit(snap), 0, -6), (body_thump(0.06, rng, 220, 70), 0, -10))
    return trim(soft_clip(unit(y) * 1.3, 1.2), -48, fade_out=0.02)


@sound('skill_ready')
def skill_ready(rng):
    # The right mouse's skill back: two soft rising notes of a small bell, quiet, so it's heard but not heeded over a fight
    a = modal([1318.5, 1318.5 * 2.76], [1.0, 0.25], [10, 18], 0.5, rng)
    b = modal([1975.5, 1975.5 * 2.76], [1.0, 0.25], [8, 16], 0.6, rng)
    y = mix((unit(a), 0, -2), (unit(b), 0.07, 0))
    return trim(y, -50, fade_out=0.15)


@sound('thump')
def thump(rng):
    # The grenade launcher: a hollow "thoomp", a low pressure drop down a tube with a ring of its metal, and a puff of gas
    tt = t(0.45)
    f = 150 * np.exp(-tt / 0.06) + 55
    drop = np.sin(2 * np.pi * np.cumsum(f) / R) * np.exp(-tt * 14) * np.minimum(1, tt / 0.002)
    tube = bandpass(white(0.45, rng), 250, 900) * np.exp(-tt * 30)
    ring = modal([420, 420 * 2.3], [1.0, 0.4], [22, 35], 0.45, rng)
    puff = lowpass(white(0.45, rng), 1800) * np.exp(-tt * 18) * np.minimum(1, tt / 0.01)
    y = mix((unit(drop), 0, 0), (unit(tube), 0, -6), (unit(ring), 0, -16), (unit(puff), 0.01, -10))
    return trim(soft_clip(unit(y) * 1.4, 1.3), -45, fade_out=0.1)


@sound('hit_kill')
def hit_kill(rng):
    # The kill: the tick doubled and lower, a thick knock under it and a short ring after, so it's told from a hit
    tt = t(0.22)
    ping = (np.sin(2 * np.pi * 2350 * tt) + 0.5 * np.sin(2 * np.pi * 3720 * tt + 0.7)) * np.exp(-tt * 30)
    tick = (np.sin(2 * np.pi * 3150 * tt) + 0.6 * np.sin(2 * np.pi * 4870 * tt)) * np.exp(-tt * 110)
    snap = highpass(white(0.22, rng), 2500) * np.exp(-tt * 140)
    y = mix((unit(tick), 0, -3), (unit(tick), 0.028, -5), (unit(ping), 0.028, -7), (unit(snap), 0, -6),
            (body_thump(0.16, rng, 95, 26), 0, -1))
    return trim(soft_clip(unit(y) * 1.6, 1.4), -48, fade_out=0.04)


@sound('hit_head')
def hit_head(rng):
    # The head: a bright metallic ding (a struck plate's partials), laid over the skull's crack
    y = modal([1840, 1840 * 2.76, 1840 * 5.40, 1840 * 8.93], [1.0, 0.55, 0.3, 0.15], [9, 14, 22, 30], 0.45, rng)
    snap = highpass(white(0.03, rng), 4000) * np.exp(-t(0.03) * 200)
    y = mix((unit(y), 0, -1), (unit(snap), 0, -7))
    return trim(y, -50, fade_out=0.08)


# ================================================================ MUSIC: perk jingles

_SEMI = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def hz(name):
    """'D4', 'F#3', 'Bb2' -> Hz (A4 = 440)."""
    acc = 0
    rest = name[1:]
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    midi = 12 * (int(rest) + 1) + _SEMI[name[0]] + acc
    return 440.0 * 2 ** ((midi - 69) / 12)


def mel(text, start=0.0):
    """'D4/1 F4/.5 r/.5 D3+F3+A3/2!' -> [(beat, beats, Hz, velocity)]. '!' accents a note."""
    events = []
    b = start
    for tok in text.split():
        accent = tok.endswith('!')
        tok = tok.rstrip('!')
        n, d = tok.split('/')
        d = float(d)
        if n != 'r':
            for part in n.split('+'):
                events.append((b, d, hz(part), 1.0 if accent else 0.78))
        b += d
    return events


def play(out, events, bpm, inst, gain_db=0.0, offset=0.0, wrap=False):
    spb = 60.0 / bpm
    for beat, dur, f, vel in events:
        place(out, inst(f, dur * spb, vel), offset + beat * spb, db(gain_db), wrap)
    return out


def lute_inst(rng, ring=0.35, bright=0.45):
    def inst(f, d, v):
        x = pluck(f, d + ring + 0.3, rng, bright=bright + 0.3 * v, damping=0.9975)
        e = env_points([(0, 1), (d + 0.04, 1), (d + ring, 0)], len(x) / R)
        return lute_body(x * e) * v
    return inst


def strum(rng, chord, d, v, spread=0.014, ring=0.3, bright=0.45):
    inst = lute_inst(rng, ring, bright)
    out = zeros(d + ring + 0.5)
    for k, f in enumerate(chord):
        place(out, inst(f, d - k * spread, v * (0.8 + 0.2 * rng.random())), k * spread)
    return out


def strums(out, chords, bpm, rng, gain_db=0.0, ring=0.3, bright=0.45):
    """chords: [(beat, beats, 'D3+A3+D4+F4', velocity)]"""
    spb = 60.0 / bpm
    for beat, dur, names, v in chords:
        place(out, strum(rng, [hz(n) for n in names.split('+')], dur * spb, v, ring=ring, bright=bright), beat * spb, db(gain_db))
    return out


def recorder_inst(rng, vib=0.25, breath=0.06):
    return lambda f, d, v: recorder(f, d + 0.04, rng, vibrato=vib, breath=breath, chiff=0.35 * v, attack=0.02, release=0.05) * v


def hurdy_inst(rng, buzz=0.0, bright=0.65):
    return lambda f, d, v: bowed(f, d + 0.02, rng, bright=bright, buzz=buzz, attack=0.025, release=0.05) * v


def tambourine(rng, v=1.0, sec=0.25):
    jing = modal([5200, 6800, 7900, 9400, 11200], [1, 0.8, 0.7, 0.5, 0.4], [28, 32, 36, 40, 45], sec, rng, beat=40)
    skin = highpass(white(sec, rng), 3000) * np.exp(-t(sec) * 60)
    return (jing + 0.6 * skin) * v


def drum_boom(rng, v=1.0, f0=72.0):
    return frame_drum(0.7, rng, f0, slap=0.35, decay=8.0) * v


def drum_tak(rng, v=1.0):
    return snare_tabor(0.3, rng, 210, snare=0.5) * v


def wood_clop(rng, f0=800.0, v=1.0):
    return thunk(0.12, rng, f0, 45, click=0.8, click_hz=2500) * v


def tavern(x, wet=0.16):
    return reverb(x, 0.9, wet=wet, damp=0.45, predelay=0.01)


def finish_jingle(x, max_seconds=10.0):
    x = tavern(x)
    x = compress(x, -16, 2.0, 0.01, 0.15)
    return cap(trim(x, -55, fade_out=0.6), max_seconds, 0.8)


@sound('jingle_1', kind='music', lufs=-16)
def jingle_1(rng):
    # Ironhide Ale: sturdy and drum-led. D dorian, 104 bpm: hurdy-gurdy tune over a drone, big frame drums
    bpm = 108
    out = zeros(12)
    tune = mel('D4/1! D4/.5 E4/.5 F4/1 G4/1 A4/1.5! G4/.5 F4/1 E4/1 D4/1! F4/.5 A4/.5 C5/1 A4/1 G4/.5 F4/.5 E4/1')
    play(out, tune, bpm, hurdy_inst(rng, buzz=0.3, bright=0.7), 0)
    play(out, mel('D5/1 D5/.5 E5/.5 F5/1 G5/1 A5/1.5 G5/.5 F5/1 E5/1 D5/1 F5/.5 A5/.5 C6/1 A5/1 G5/.5 F5/.5 E5/1'), bpm, recorder_inst(rng, 0.2), -13)
    spb = 60 / bpm
    drone = bowed(hz('D3'), 14 * spb + 1.2, rng, bright=0.4, buzz=0.5, attack=0.15, release=0.8) + 0.8 * bowed(hz('A2'), 14 * spb + 1.2, rng, bright=0.35, attack=0.15, release=0.8)
    place(out, unit(drone) * db(-9), 0)
    strums(out, [(0, 2, 'D3+A3+D4+F4', 0.9), (2, 2, 'D3+A3+D4+F4', 0.7), (4, 2, 'C3+G3+C4+E4', 0.9), (6, 2, 'A2+E3+A3+C4', 0.7),
                 (8, 2, 'D3+A3+D4+F4', 0.9), (10, 2, 'F3+A3+C4+F4', 0.7), (12, 2, 'C3+G3+C4+E4', 0.9)], bpm, rng, -6)
    # drums: boom on 1 and 3 with a pickup, tak on 2 and 4, a roll into the last hit
    for bar in range(4):
        b0 = bar * 4
        for b, kind, v in [(0, 'B', 1.0), (1, 'T', 0.8), (2, 'B', 0.9), (2.5, 'B', 0.6), (3, 'T', 0.85), (3.5, 'T', 0.4)]:
            if bar == 3 and b >= 1:
                continue
            place(out, drum_boom(rng, v) if kind == 'B' else drum_tak(rng, v), (b0 + b) * spb, db(-2 if kind == 'B' else -5))
    for k, b in enumerate([13.0, 13.25, 13.5, 13.75]):
        place(out, drum_tak(rng, 0.5 + 0.12 * k), b * spb, db(-6))
    place(out, drum_boom(rng, 1.0, 62), 14 * spb, db(0))
    place(out, drum_tak(rng, 1.0), 14 * spb, db(-5))
    place(out, strum(rng, [hz(n) for n in 'D2+A2+D3+A3+D4+F4'.split('+')], 1.4, 1.0, ring=1.0), 14 * spb, db(-4))
    place(out, hurdy_inst(rng, 0.4)(hz('D4'), 1.3, 1.0), 14 * spb, db(-1))
    place(out, recorder_inst(rng, 0.3)(hz('D5'), 1.3, 1.0), 14 * spb, db(-13))
    return finish_jingle(out)


@sound('jingle_2', kind='music', lufs=-16)
def jingle_2(rng):
    # Quickhand Mead: a fast, nimble jig. G major, 6/8: recorder runs, lute and tambourine
    bpm = 396  # eighth notes
    out = zeros(10)
    tune = mel('D5/1 B4/1 G4/1 G4/1 B4/1 D5/1 E5/1 C5/1 A4/1 A4/2 B4/1 C5/1 B4/1 A4/1 B4/1 G4/1 E4/1 D4/1 E4/1 F#4/1 G4/2 A4/1 '
               'B4/1 A4/1 G4/1 D5/1 C5/1 B4/1 C5/1 B4/1 A4/1 E5/2 C5/1 B4/1 D5/1 B4/1 A4/1 F#4/1 A4/1 G4/3! r/3')
    play(out, tune, bpm, recorder_inst(rng, 0.12, 0.05), 0)
    bass = {0: ('G2', 'G3+B3+D4'), 1: ('C3', 'C3+E3+G3'), 2: ('A2', 'A3+C4+E4'), 3: ('D3', 'D3+F#3+A3'),
            4: ('G2', 'G3+B3+D4'), 5: ('A2', 'A3+C4+E4'), 6: ('D3', 'D3+F#3+A3+C4'), 7: ('G2', 'G3+B3+D4')}
    lute = lute_inst(rng, 0.2, 0.5)
    spb = 60 / bpm
    for bar in range(8):
        root, chord = bass[bar]
        for half in (0, 3):
            b = bar * 6 + half
            place(out, lute(hz(root), 2 * spb, 0.9), b * spb, db(-5))
            if not (bar == 7 and half == 3):
                place(out, strum(rng, [hz(n) for n in chord.split('+')], 1 * spb, 0.6, spread=0.008, ring=0.12, bright=0.6), (b + 2) * spb, db(-9))
            place(out, tambourine(rng, 0.9 if half == 0 else 0.6), b * spb, db(-14))
            place(out, tambourine(rng, 0.35), (b + 2) * spb, db(-16))
    place(out, drum_boom(rng, 0.8, 80), 42 * spb, db(-6))
    place(out, strum(rng, [hz(n) for n in 'G2+D3+G3+B3+D4+G4'.split('+')], 0.9, 1.0, ring=0.8), 42 * spb, db(-4))
    return finish_jingle(out)


@sound('jingle_3', kind='music', lufs=-16)
def jingle_3(rng):
    # Second Wind Cider: gentle and hopeful. F major waltz: a soft recorder over lute arpeggios
    bpm = 84
    out = zeros(11)
    play(out, mel('A4/1 C5/1 F5/1 E5/1.5 D5/.5 C5/1 D5/1 Bb4/1 G4/1 A4/3!'), bpm, recorder_inst(rng, 0.35, 0.07), 0)
    arps = ['F2 C3 A3 C4 F4 C4', 'C3 G3 E4 G3 C4 G3', 'Bb2 F3 D4 F3 C3 G3', 'F2 C3 A3 C4 F4 A4']
    lute = lute_inst(rng, 0.6, 0.35)
    spb = 60 / bpm
    for bar, notes in enumerate(arps):
        for k, n in enumerate(notes.split()):
            if bar == 3 and k > 3:
                break
            place(out, lute(hz(n), 0.5 * spb, 0.75 if k else 0.95), (bar * 3 + k * 0.5) * spb, db(-6))
    place(out, strum(rng, [hz(n) for n in 'F2+C3+F3+A3+C4+F4'.split('+')], 1.5, 0.8, spread=0.04, ring=1.2, bright=0.3), 10.5 * spb, db(-5))
    for b in (0, 3, 6, 9):
        place(out, bell(hz('F6') if b != 6 else hz('D6'), 1.5, rng, 'glock', strike=0.2), b * spb, db(-22))
    place(out, bell(hz('A6'), 2.0, rng, 'chime', strike=0.2), 9 * spb, db(-18))
    return finish_jingle(out)


@sound('jingle_4', kind='music', lufs=-16)
def jingle_4(rng):
    # Twinstrike Brew: punchy, every hit doubled. A minor at 140 bpm: buzzing hurdy-gurdy, paired drum hits
    bpm = 140
    out = zeros(9)
    notes = ['A4', 'C5', 'E5', 'D5', 'C5', 'Bb4', 'A4', None, 'A4', 'C5', 'E5', 'G5', 'F5', 'E5']
    ev = []
    for k, n in enumerate(notes):
        if n:
            ev += [(k, 0.42, hz(n), 1.0), (k + 0.5, 0.42, hz(n), 0.85)]
    play(out, ev, bpm, hurdy_inst(rng, buzz=0.7, bright=0.85), 0)
    play(out, [(b, d, f * 2, v) for b, d, f, v in ev], bpm, recorder_inst(rng, 0.0, 0.03), -15)
    spb = 60 / bpm
    for bar in range(4):
        b0 = bar * 4
        hits = [(0, 'B'), (0.5, 'B'), (1, 'T'), (1.5, 'T'), (2, 'B'), (2.5, 'B'), (3, 'T'), (3.5, 'T')]
        for b, kind in hits:
            if bar == 3 and b >= 2:
                continue
            place(out, drum_boom(rng, 0.9, 78) if kind == 'B' else drum_tak(rng, 0.75), (b0 + b) * spb, db(-3 if kind == 'B' else -6))
    chords = [(0, 'A2+E3+A3+C4'), (2, 'A2+E3+A3+C4'), (4, 'F2+C3+F3+A3'), (6, 'G2+D3+G3+Bb3'), (8, 'A2+E3+A3+C4'), (10, 'C3+G3+C4+E4'), (12, 'D3+A3+D4+F4')]
    for b, c in chords:
        for d in (0, 0.5):
            place(out, strum(rng, [hz(n) for n in c.split('+')], 0.35 * spb, 0.9, spread=0.006, ring=0.1, bright=0.6), (b + d) * spb, db(-8))
    for b in (14, 14.5):
        place(out, drum_boom(rng, 1.0, 70), b * spb, db(0))
        place(out, drum_tak(rng, 1.0), b * spb, db(-4))
        place(out, strum(rng, [hz(n) for n in 'A2+E3+A3+C4+E4+A4'.split('+')], 0.4 if b == 14 else 1.2, 1.0, spread=0.006, ring=0.15 if b == 14 else 0.9, bright=0.7), b * spb, db(-3))
        place(out, hurdy_inst(rng, 0.8, 0.9)(hz('A4'), 0.3 if b == 14 else 0.9, 1.0), b * spb, db(-2))
    return finish_jingle(out)


@sound('jingle_5', kind='music', lufs=-16)
def jingle_5(rng):
    # Fleetfoot Tonic: skipping and airy. D lydian at 116 bpm: high recorder and bells in long-short skips
    bpm = 116
    out = zeros(10)
    tune = ('D5/.75 E5/.25 F#5/.75 A5/.25 B5/.75 A5/.25 F#5/1 G#5/.75 A5/.25 F#5/.75 E5/.25 D5/.75 E5/.25 F#5/1 '
            'A5/.75 B5/.25 C#6/.75 B5/.25 A5/.75 F#5/.25 E5/1 F#5/.75 E5/.25 D5/.75 C#5/.25 D5/2!')
    play(out, mel(tune), bpm, recorder_inst(rng, 0.15, 0.08), 0)
    play(out, mel(tune), bpm, lambda f, d, v: bell(f * 2, 0.8, rng, 'glock', strike=0.15) * v, -16)
    spb = 60 / bpm
    chords = ['D3+A3+D4+F#4', 'E3+B3+D4+G#4', 'A2+E3+A3+C#4', 'D3+A3+D4+F#4']
    for bar, c in enumerate(chords):
        for b in (0.5, 1.5, 2.5, 3.5):
            if bar == 3 and b > 2:
                continue
            place(out, strum(rng, [hz(n) for n in c.split('+')], 0.3 * spb, 0.6, spread=0.008, ring=0.12, bright=0.65), (bar * 4 + b) * spb, db(-12))
        for b in (0, 1, 2, 3):
            place(out, tambourine(rng, 0.5 if b % 2 else 0.8, 0.2), (bar * 4 + b) * spb, db(-17))
            place(out, tambourine(rng, 0.3, 0.15), (bar * 4 + b + 0.75) * spb, db(-19))
        place(out, lute_inst(rng, 0.3)(hz(c.split('+')[0]) / 2 * 2, 2 * spb, 0.8), bar * 4 * spb, db(-8))
    place(out, strum(rng, [hz(n) for n in 'D3+A3+D4+F#4+A4'.split('+')], 1.0, 0.8, ring=1.0, bright=0.6), 14 * spb, db(-8))
    place(out, bell(hz('D7'), 1.8, rng, 'chime', strike=0.2), 14 * spb, db(-16))
    return finish_jingle(out)


@sound('jingle_6', kind='music', lufs=-16)
def jingle_6(rng):
    # Packmule Stout: plodding and comic. C major oompah at 92 bpm, a bassoon-ish low tune, hooves, a sad trombone end
    bpm = 98
    out = zeros(11.5)
    spb = 60 / bpm
    low = lambda f, d, v: bowed(f, d * 0.85, rng, bright=0.35, buzz=0.25, attack=0.02, release=0.06) * v
    play(out, mel('C3/1 G3/1 E3/.5 F3/.5 G3/1 A3/1 F3/1 D3/.5 E3/.5 F3/1 G3/.5 F3/.5 E3/.5 D3/.5 C3/1 G2/1'), bpm, low, 0)
    oom = [(0, 'C2', 'C3+E3+G3'), (2, 'G2', 'C3+E3+G3'), (4, 'F2', 'F3+A3+C4'), (6, 'C2', 'F3+A3+C4'), (8, 'G2', 'G3+B3+D4'), (10, 'C2', 'C3+E3+G3')]
    lute = lute_inst(rng, 0.15, 0.4)
    for b, root, chord in oom:
        place(out, lute(hz(root), 0.6 * spb, 1.0), b * spb, db(-4))
        place(out, strum(rng, [hz(n) for n in chord.split('+')], 0.35 * spb, 0.7, spread=0.006, ring=0.08, bright=0.5), (b + 1) * spb, db(-9))
    for k in range(24):
        place(out, wood_clop(rng, [820, 640][k % 2], 0.7 if k % 2 == 0 else 0.5), k * 0.5 * spb, db(-15))
    # the sad trombone: four falling notes, the last sagging further
    tb = [('E3', 12.0, 0.5), ('Eb3', 12.5, 0.5), ('D3', 13.0, 0.5), ('Db3', 13.5, 1.75)]
    for name, b, d in tb:
        sec = d * spb + 0.15
        f = np.full(n_of(sec), hz(name))
        if d > 1:
            f = f * 2 ** (-1.5 / 12 * np.clip((t(sec) - 0.25) / sec, 0, 1)) * (1 + 0.012 * np.sin(2 * np.pi * 5.5 * t(sec)) * np.clip(t(sec) / 0.4, 0, 1))
        sw = env_points([(0, 0), (0.03, 1), (sec - 0.08, 0.85), (sec, 0)], sec)
        place(out, horn(f, sec, rng, swell=sw, bright=0.7), b * spb, db(-3))
    place(out, wood_clop(rng, 520, 1.0), 13.5 * spb, db(-10))
    return finish_jingle(out)


# ================================================================ MUSIC: cues and beds

def kick(rng, v=1.0, f0=52.0):
    """An electronic kick: a falling sine and a click."""
    body = body_thump(0.4, rng, f0, 9.0)
    cl = highpass(white(0.008, rng), 2500) * np.exp(-t(0.008) * 400)
    out = unit(body)
    out[:len(cl)] += 0.25 * unit(cl)
    return out * v


def snare(rng, v=1.0):
    """A dry electronic snare: a tone and a burst of noise."""
    tone = sine(185.0, 0.12) * np.exp(-t(0.12) * 30)
    noise = bandpass(white(0.22, rng), 1200, 7000) * np.exp(-t(0.22) * 18)
    return mix((unit(tone), 0, -6), (unit(noise), 0, 0)) * v


def hat(rng, v=1.0, open_=False):
    sec = 0.18 if open_ else 0.05
    return highpass(white(sec, rng), 7000) * np.exp(-t(sec) * (14 if open_ else 80)) * v


def supersaw(f, seconds, voices=3, detune=0.008, phase_rng=None):
    """A few saws a little apart, for a thick synth tone."""
    out = zeros(seconds)
    for k in range(voices):
        d = 1 + detune * (k - (voices - 1) / 2)
        out += saw_blep(f * d, seconds, phase0=phase_rng.uniform(0, 1) if phase_rng is not None else 0.0)
    return out / voices


def periodic_noise(seconds, rng, gain_of_f):
    """Noise exactly `seconds` long, filtered round the circle so it loops with no seam."""
    n = n_of(seconds)
    Z = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / R)
    x = np.fft.irfft(Z * gain_of_f(f), n)
    return x / (rms(x) + 1e-12)


def periodic_lfo(seconds, rng, max_hz=0.3, smooth=1.0):
    """A smooth random wobble in -1..1 that repeats exactly every `seconds`."""
    n = n_of(seconds)
    tt = np.arange(n) / R
    out = np.zeros(n)
    kmax = max(1, int(max_hz * seconds))
    for k in range(1, kmax + 1):
        out += rng.normal() / k ** smooth * np.sin(2 * np.pi * k / seconds * tt + rng.uniform(0, 6.3))
    return out / (peak(out) + 1e-12)


def circular_stft_filter(x, gain_fn):
    """stft_filter for a loop: run over three copies and keep the middle, so the filter has no seam.
    gain_fn(time_s, f) is called with time folded into the loop's length."""
    n = len(x)
    sec = n / R
    y = stft_filter(np.concatenate([x, x, x]), lambda tm, f: gain_fn(tm % sec, f))
    return y[n:2 * n]


def ring_reverb(out, rt60, wet, damp=0.6):
    """A loop's reverb, its tail folded back round to the start so the echoes carry over the seam."""
    n = len(out)
    w = reverb(out, rt60, wet=1.0, dry=0.0, damp=damp)
    ring = np.zeros(n)
    for k in range(0, len(w), n):
        seg = w[k:k + n]
        ring[:len(seg)] += seg
    return out + wet * ring


@sound('round_end', kind='music', lufs=-20)
def round_end(rng):
    # A wave held: a breath of the cavern's dark ambience, swelling and gone (Paul Wortmann)
    x = S('oga_dark_cavern', 'dark_cavern_ambient_002.ogg', 30.0, 37.0)
    return trim(fade(x, 1.5, 3.5), -55, fade_out=0.5)


@sound('game_over', kind='music', lufs=-19)
def game_over(rng):
    # Everyone's down: the cavern's deepest stretch, slowly gone
    x = S('oga_dark_cavern', 'dark_cavern_ambient_002.ogg', 60.0, 72.0)
    return trim(fade(x, 0.5, 6.0), -60, fade_out=0.5)


@sound('level_up')
def level_up(rng):
    # A level for the party: Kenney's confirmation chime
    return trim(S('kenney_interface', 'confirmation_002.ogg'), -50, fade_out=0.05)


@sound('item_uncommon')
def item_uncommon(rng):
    # Gear taken up: a latch and the leather of a pouch (Kenney's RPG sounds)
    y = mix((unit(at_onset(S(KR, 'metalLatch.ogg'))), 0, 0), (unit(at_onset(S(KR, 'handleSmallLeather.ogg'))), 0.12, -4))
    return trim(y, -50, fade_out=0.05)


@sound('item_rare')
def item_rare(rng):
    # Something rare: the latch, and coins
    y = mix((unit(at_onset(S(KR, 'metalLatch.ogg'))), 0, 0), (unit(at_onset(S(KR, 'handleCoins.ogg'))), 0.1, -2))
    return trim(y, -50, fade_out=0.05)


def music_loop(key, rel, t0, seconds, xfade=4.0):
    """A stretch of a track made to go round: its end crossfaded into its start."""
    x = S(key, rel, t0, t0 + seconds + xfade)
    return loopify(x, xfade)


@sound('music_low', kind='loop', lufs=-24)
def music_low(rng):
    # Under early waves, two minutes of Juhani Junkala's Post Apocalyptic Wastelands
    return music_loop('oga_junkala_wastelands', 'wastelands.ogg', 20.0, 120.0)


@sound('music_high', kind='loop', lufs=-20)
def music_high(rng):
    # When the horde is big, two minutes of Ruskerdax's Ancient Grudge
    return music_loop('oga_ancient_grudge', 'ancient_grudge.mp3', 15.0, 120.0)


# ================================================================ building

def group_of(name):
    base, _, tail = name.rpartition('_')
    return base if tail.isdigit() and base else name


def load_manifest(path):
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    return {}


def write_credits(manifest, out_dir):
    used = sorted({k for m in manifest.values() for k in m['packs']})
    lines = ['# GRAVEWAKE sound credits', '',
             'Every file here is either synthesized (made from nothing by `tools/make_sfx.py`, released',
             'with the game) or made from CC0 (public domain) recordings, cut, filtered, pitched, layered',
             'and normalized by `tools/make_sfx.py`, whose recipe for each file records exactly how.',
             'No attribution is required for any of it; it is given here anyway.', '',
             '## Packs used', '',
             '| Key | Pack | Author | Licence | Page |', '|---|---|---|---|---|']
    for k in used:
        p = PACKS[k]
        lines.append(f"| `{k}` | {p['name']} | {p['author']} | {p['licence']} | {p['page']} |")
    lines += ['', '## Files', '', '| File | Seconds | Source |', '|---|---|---|']
    for name in sorted(manifest):
        m = manifest[name]
        if m['packs']:
            src = ', '.join(f"{PACKS[k]['name']} by {PACKS[k]['author']} ({PACKS[k]['licence']}, {PACKS[k]['page']})" for k in m['packs'])
            if m['layers']:
                src += ' + synthesized layers'
        else:
            src = 'synthesized'
        lines.append(f"| {m['file']} | {m['seconds']:.2f} | {src} |")
    lines.append('')
    with open(os.path.join(out_dir, 'CREDITS.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines))


def check(out_dir):
    total = 0
    for name in SOUNDS:
        files = [f for f in (f'{name}.wav', f'{name}.ogg') if os.path.exists(os.path.join(out_dir, f))]
        if len(files) != 1:
            print(f'  {name}: {len(files)} files')
            continue
        path = os.path.join(out_dir, files[0])
        x = load(path)
        total += os.path.getsize(path)
        line = f'  {files[0]:24s} {len(x) / R:6.2f}s  peak {20 * np.log10(peak(x) + 1e-12):6.1f}  loudness {loudness(x):6.1f}'
        if SOUNDS[name]['kind'] == 'loop':
            line += f'  seam {loop_seam_error(x):.2f}'
        print(line)
    print(f'{total / 1e6:.2f} MB')


def main():
    global SOURCES
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--sources', help='folder holding the extracted packs, one folder per key')
    ap.add_argument('--out', default=OUT_DEFAULT)
    ap.add_argument('--only', help='comma-separated names (or group names) to build')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--check', action='store_true', help='decode every file in --out and report length, peak, loudness, loop seams')
    a = ap.parse_args()
    if a.list:
        for n in SOUNDS:
            print(n)
        return
    if a.check:
        check(a.out)
        return
    SOURCES = a.sources
    os.makedirs(a.out, exist_ok=True)
    man_path = os.path.join(HERE, 'manifest.json')
    manifest = load_manifest(man_path)
    names = list(SOUNDS)
    if a.only:
        want = set(a.only.split(','))
        names = [n for n in names if n in want or group_of(n) in want]
    built = {}
    for name in names:
        spec = SOUNDS[name]
        rng = np.random.default_rng(zlib.crc32(name.encode()))
        _used.clear()
        t0 = time.time()
        try:
            x = np.asarray(spec['fn'](rng), dtype=np.float64)
        except MissingSources:
            print(f'  skip {name}: needs --sources')
            continue
        except Exception:
            import traceback
            traceback.print_exc()
            print(f'  FAILED {name}')
            continue
        # no DC or sub-audio rumble in anything; loops are filtered round the circle
        if spec['kind'] == 'loop':
            x = dc_block_loop(x)
        else:
            x = highpass(x, 20)
            x = fade(x, 0.0, 0.004 if spec['kind'] == 'sfx' else 0.05)
        if spec['lufs'] is not None:
            x = x * db(spec['lufs'] - loudness(x))
            if peak(x) > db(-1.0):
                x = limit_loop(x, -1.0) if spec['kind'] == 'loop' else limit(x, -1.0)
        else:
            x = normalize(x, spec['peak'])
        built[name] = dict(x=x, packs=sorted(_used), kind=spec['kind'], layers=spec['layers'] and bool(_used))
        print(f'  {name:22s} {len(x) / R:6.2f}s  {time.time() - t0:5.1f}s to make')
    # one format per group: Ogg when it's a loop or music or any of the group is over a second
    groups = {}
    for name in SOUNDS:
        g = group_of(name)
        if name in built:
            secs = len(built[name]['x']) / R
            kind = built[name]['kind']
        elif name in manifest:
            secs = manifest[name]['seconds']
            kind = SOUNDS[name]['kind']
        else:
            continue
        cur = groups.get(g, False)
        groups[g] = cur or kind != 'sfx' or secs > 1.0
    for name, b in built.items():
        ext = 'ogg' if groups[group_of(name)] else 'wav'
        path = os.path.join(a.out, f'{name}.{ext}')
        other = os.path.join(a.out, f"{name}.{'wav' if ext == 'ogg' else 'ogg'}")
        if os.path.exists(other):
            os.remove(other)
        x = b['x']
        if ext == 'ogg':
            save_ogg(path, x)
            back = load(path)
            if peak(back) > db(-0.5):
                # Vorbis can overshoot a peak a little; bring the decoded peak back under -1 dBFS
                save_ogg(path, x * db(-1.0) / peak(back))
        else:
            save_wav(path, x)
        manifest[name] = dict(file=os.path.basename(path), seconds=round(len(x) / R, 3), bytes=os.path.getsize(path),
                              packs=b['packs'], layers=b['layers'], kind=b['kind'])
    # a group that changed format leaves its other members' files to be rewritten: rename by re-encoding
    for name, m in list(manifest.items()):
        if name not in SOUNDS:
            del manifest[name]
            continue
        want_ext = 'ogg' if groups.get(group_of(name)) else 'wav'
        if not m['file'].endswith(want_ext) and name not in built:
            print(f'  note: {name} should be .{want_ext}; rebuild it with --only {name}')
    with open(man_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    write_credits(manifest, a.out)
    total = sum(m['bytes'] for m in manifest.values())
    print(f'{len(built)} built, {len(manifest)} in the folder, {total / 1e6:.2f} MB')


if __name__ == '__main__':
    main()
