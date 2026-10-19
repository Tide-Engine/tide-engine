"""GRAVEWAKE's balance on paper: the numbers read from the game's own source (weapons.tide's Info, nights.tide's
NIGHT_STEP, Crowd and the vigil's, rounds.tide's ZombieHealth and SpawnRate, items.tide's LevelSpan and Might,
orbs.tide's OrbWorth), and what follows from them, night by night: how much health the dead bring a second, how many
shots one of them takes from each gun, and the level pace if the party kills what comes and picks up the share of the
orbs it does in play, with each night looked about for `roam` seconds before its beacon's lit. The bots
(test/balance.c) say how it really goes; this says why.

    python balance_math.py [players] [orbs taken, 0-1] [roam seconds]
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)


def read(name):
    return open(os.path.join(GAME, name), encoding='utf-8').read()


def number(text, pattern):
    m = re.search(pattern, text, re.S)
    if not m:
        raise SystemExit(f'not found: {pattern}')
    return [float(g) for g in m.groups()]


nights = read('nights.tide')
rounds = read('rounds.tide')
items = read('items.tide')
weapons = read('weapons.tide')
orbs = read('orbs.tide')

night_step, = number(nights, r'const float NIGHT_STEP = ([0-9.]+);')
danger_every, = number(nights, r'const float DANGER_EVERY = ([0-9.]+);')
vigil_seconds, = number(nights, r'const float VIGIL_SECONDS = ([0-9.]+);')
vigil_rush, = number(nights, r'const float VIGIL_RUSH = ([0-9.]+);')
dawn_seconds, = number(nights, r'const float DAWN_SECONDS = ([0-9.]+);')
crowd_more, = number(nights, r'float Crowd\(int players\)\s*\{\s*return 1 \+ ([0-9.]+) \* \(players - 1\);')
health_base, health_exp = number(rounds, r'float ZombieHealth\(float danger\)\s*\{\s*return ([0-9.]+) \* Math\.Pow\(danger, ([0-9.]+)\)')
spawn_base, = number(rounds, r'float SpawnRate\(float danger\)\s*\{\s*return ([0-9.]+) \* danger;')
level_first, = number(items, r'const float LEVEL_FIRST = ([0-9.]+);')
level_growth, = number(items, r'const float LEVEL_GROWTH = ([0-9.]+);')
might_level, = number(items, r'float Might\(Survivor s\)\s*\{\s*return \(1 \+ ([0-9.]+) \* \(s\.level - 1\)\)')
orb_base, orb_exp = number(orbs, r'float OrbWorth\(float danger\)\s*\{\s*return ([0-9.]+) \* Math\.Pow\(danger, ([0-9.]+)\)')

guns = {}
for m in re.finditer(r'case Weapon\.(\w+):\s*(?://[^\n]*\n\s*)?return WeaponInfo \{(.*?)\};', weapons, re.S):
    g = dict(re.findall(r'(\w+) = ([0-9.]+|true|false|"[^"]*")', m.group(2)))
    guns[m.group(1)] = {
        'name': g.get('name', '').strip('"'),
        'damage': float(g['damage']), 'rate': float(g['rate']), 'mag': int(g['mag']), 'reload': float(g['reload']),
        'shells': g.get('shells') == 'true', 'bottomless': g.get('bottomless') == 'true', 'pellets': int(g.get('pellets', 1)),
        'pierce': int(g.get('pierce', 1)), 'range': float(g.get('range', 70)), 'proc': float(g.get('proc', 1)),
    }

players = int(sys.argv[1]) if len(sys.argv) > 1 else 1
taken = float(sys.argv[2]) if len(sys.argv) > 2 else 0.8
roam = float(sys.argv[3]) if len(sys.argv) > 3 else 120
crowd = 1 + crowd_more * (players - 1)


def danger(seconds, night):
    return (1 + seconds / danger_every) * night_step ** max(0, night - 1)


def zombie_health(d):
    return health_base * d ** health_exp


def span(level):
    return level_first * level_growth ** (level - 1) * crowd


print(f'GRAVEWAKE on paper, {players} survivor(s), {taken:.0%} of the orbs taken, {roam:g} s looked about each night')
print(f'  danger = (1 + t/{danger_every:g}) x {night_step:g}^(night-1); zombie health {health_base:g} x d^{health_exp:g}; '
      f'{spawn_base:g} x d a second x {crowd:g} (the crowd) x {vigil_rush:g} in a vigil; an orb {orb_base:g} x d^{orb_exp:g}; '
      f'a level {level_first:g} x {level_growth:g}^(L-1) x {crowd:g}; +{might_level:.0%} damage a level')
print()
print('The guns (before items and levels):')
print(f'  {"gun":<14}{"a hit":>7}{"a shot":>8}{"burst/s":>9}{"kept up/s":>11}{"pierce":>8}{"range":>7}{"procs/s":>9}')
for key, g in guns.items():
    shot = g['damage'] * g['pellets']
    burst = shot / g['rate']
    load = 0 if g['bottomless'] else g['reload'] * g['mag'] if g['shells'] else g['reload']
    kept = shot * g['mag'] / (g['mag'] * g['rate'] + load)
    procs = g['pellets'] * g['proc'] * g['mag'] / (g['mag'] * g['rate'] + load)
    print(f'  {g["name"]:<14}{g["damage"]:>7.0f}{shot:>8.0f}{burst:>9.0f}{kept:>11.0f}{g["pierce"]:>8d}{g["range"]:>7.0f}{procs:>9.1f}')
print()

# A run, night by night, a second at a time: looked about for `roam`, then the vigil, then dawn.
t = 0.0
level = 1
xp = 0.0
marks = []
print(f'  {"night":>5}{"ends":>7}{"danger":>8}{"zombie hp":>11}{"come/s":>8}{"hp/s in":>9}{"level":>7}')
for night in range(1, 9):
    for phase, seconds, rush in (('roam', roam, 1.0), ('vigil', vigil_seconds, vigil_rush), ('dawn', dawn_seconds, 0.0)):
        for _ in range(int(seconds)):
            d = danger(t, night)
            come = min(spawn_base * d * crowd * rush, 600)
            xp += come * orb_base * d ** orb_exp * taken
            while xp >= span(level):
                xp -= span(level)
                level += 1
                marks.append((level, t))
            t += 1
    d = danger(t, night)
    come = spawn_base * d * crowd
    print(f'  {night:>5}{t / 60:>6.1f}m{d:>8.2f}{zombie_health(d):>11.0f}{come:>8.1f}{come * zombie_health(d):>9.0f}{level:>7}')
print()
print('Shots a zombie takes, by night (its end):')
t = 0.0
for night in range(1, 9):
    t += roam + vigil_seconds + dawn_seconds
    d = danger(t, night)
    line = f'  night {night}: '
    for g in guns.values():
        if g['name'] == 'Sidearm':
            continue
        line += f'{g["name"]} {zombie_health(d) / (g["damage"] * g["pellets"]):.1f}  '
    print(line)
print()
print('The level pace if every one of the dead that comes is killed, with that share of the orbs:')
print('  ' + ', '.join(f'L{l} {t / 60:.1f} min' for l, t in marks[:20]))
gaps = [marks[i][1] - (marks[i - 1][1] if i else 0) for i in range(len(marks))]
print('  seconds between picks: ' + ', '.join(f'{g:.0f}' for g in gaps[:24]))
