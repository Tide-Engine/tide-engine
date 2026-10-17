"""GRAVEWAKE's balance on paper: the numbers read from the game's own source (weapons.tide's Info, rounds.tide's Danger,
ZombieHealth and SpawnRate, items.tide's LevelSpan and Might, zombies.tide's experience a kill), and what follows from
them, minute by minute: how much health the dead bring a second, how much each gun deals out (in bursts, and kept up
through its reloads), how many shots a zombie takes, and the level pace if a survivor kills what comes and picks up the
share of the orbs it does in play. The bots (test/balance.c) say how it really goes; this says why.

    python balance_math.py [players] [orbs taken, 0-1]
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


rounds = read('rounds.tide')
items = read('items.tide')
weapons = read('weapons.tide')
zombies = read('zombies.tide')
orbs = read('orbs.tide')

danger_every, = number(rounds, r'float Danger\(Match match, int players\)\s*\{\s*return \(1 \+ match\.clock / ([0-9.]+)\)')
danger_more, = number(rounds, r'float Danger\(Match match, int players\)\s*\{\s*return .*?\(1 \+ ([0-9.]+) \* \(players - 1\)\)')
health_base, health_exp = number(rounds, r'float ZombieHealth\(float danger\)\s*\{\s*return ([0-9.]+) \* Math\.Pow\(danger, ([0-9.]+)\)')
spawn_text = re.search(r'float SpawnRate\(float danger\)\s*\{\s*return ([^;]+);', rounds, re.S).group(1)
m = re.fullmatch(r'([0-9.]+) \* (?:Math\.Pow\(danger, ([0-9.]+)\)|danger)', spawn_text.strip())
spawn_base, spawn_exp = float(m.group(1)), float(m.group(2) or 1)
span_base, span_mul = number(items, r'float LevelSpan\(int level\)\s*\{\s*return ([0-9.]+) \* ([0-9.]+) \* Math\.Pow')
growth_text = re.search(r'float LevelSpan\(int level\)\s*\{\s*return [0-9.]+ \* [0-9.]+ \* Math\.Pow\(([^,]+), level - 1\)', items, re.S).group(1)
span_growth = eval(growth_text)
might_level, = number(items, r'float Might\(Survivor s\)\s*\{\s*return \(1 \+ ([0-9.]+) \* \(s\.level - 1\)\)')
xp_kill, = number(zombies, r'DropOrb\(match, zombie\.at, ([0-9.]+)\)')

guns = {}
for m in re.finditer(r'case Weapon\.(\w+):\s*return WeaponInfo \{(.*?)\};', weapons, re.S):
    fields = dict(re.findall(r'(\w+) = ([0-9.]+|true|false|"[^"]*")', m.group(2)))
    g = {k: fields.get(k) for k in fields}
    guns[m.group(1)] = {
        'name': g.get('name', '').strip('"'),
        'damage': float(g['damage']), 'rate': float(g['rate']), 'mag': int(g['mag']), 'reload': float(g['reload']),
        'shells': g.get('shells') == 'true', 'pellets': int(g.get('pellets', 1)), 'pierce': int(g.get('pierce', 1)),
        'range': float(g.get('range', 70)), 'spread': float(g['spread']),
    }

players = int(sys.argv[1]) if len(sys.argv) > 1 else 1
taken = float(sys.argv[2]) if len(sys.argv) > 2 else 0.8


def danger(seconds):
    return (1 + seconds / danger_every) * (1 + danger_more * (players - 1))


def zombie_health(d):
    return health_base * d ** health_exp


def spawn_rate(d):
    return spawn_base * d ** spawn_exp


def span(level):
    return span_base * span_mul * span_growth ** (level - 1)


print(f'GRAVEWAKE on paper, {players} player(s), {taken:.0%} of the orbs taken')
print(f'  danger = (1 + t/{danger_every:g}) x (1 + {danger_more:g} per extra player); zombie health {health_base:g} x d^{health_exp:g}; '
      f'{spawn_base:g} x d^{spawn_exp:g} a second; a kill {xp_kill:g} xp; a level {span_base:g} x {span_mul:g} x {span_growth:.4f}^(L-1); +{might_level:.0%} damage a level')
print()
print('The guns (before items and levels):')
print(f'  {"gun":<14}{"a hit":>7}{"a shot":>8}{"burst/s":>9}{"kept up/s":>11}{"pierce":>8}{"range":>7}')
for key, g in guns.items():
    shot = g['damage'] * g['pellets']
    burst = shot / g['rate']
    load = g['reload'] * g['mag'] if g['shells'] else g['reload']
    kept = shot * g['mag'] / (g['mag'] * g['rate'] + load)
    g['kept'] = kept
    print(f'  {g["name"]:<14}{g["damage"]:>7.0f}{shot:>8.0f}{burst:>9.0f}{kept:>11.0f}{g["pierce"]:>8d}{g["range"]:>7.0f}')
print()
print('Minute by minute: what the dead bring, and what each gun keeps up (times its pierce, at most, in a crowd)')
header = f'  {"min":>4}{"danger":>8}{"zombie hp":>11}{"come/s":>8}{"hp/s in":>9}'
for g in guns.values():
    if g['name'] != 'Sidearm':
        header += f'{g["name"][:10] + " shots":>18}'
print(header)
for minute in (0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15):
    t = minute * 60
    d = danger(t)
    hp = zombie_health(d)
    come = min(spawn_rate(d), 10 * 60)
    line = f'  {minute:>4}{d:>8.2f}{hp:>11.0f}{come:>8.1f}{hp * come:>9.0f}'
    for g in guns.values():
        if g['name'] == 'Sidearm':
            continue
        shots = hp / (g['damage'] * g['pellets'])
        line += f'{shots:>10.1f} to kill'
    print(line)
print()
print('The level pace if every one of the dead that comes is killed (the most experience there is), with that share of the orbs:')
xp = 0.0
level = 1
t = 0.0
marks = []
while t < 20 * 60 and level < 40:
    d = danger(t)
    xp += spawn_rate(d) * xp_kill * taken
    while xp >= span(level):
        xp -= span(level)
        level += 1
        marks.append((level, t))
    t += 1
print('  ' + ', '.join(f'L{l} {t / 60:.1f} min' for l, t in marks[:16]))
gaps = [marks[i][1] - (marks[i - 1][1] if i else 0) for i in range(len(marks))]
print('  seconds between picks: ' + ', '.join(f'{g:.0f}' for g in gaps[:16]))
