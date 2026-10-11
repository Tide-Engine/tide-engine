"""Writes LevelHint from the solver's narration of every shortest solution.

The first nudge of each puzzle is written by hand, below. The second and third
are facts of the shortest way the search found: where it ends, how much work
it takes, and how it opens. Cells are named as the board names them while a
hint is up: columns A to G, rows 1 to 5.
"""
import re
import subprocess
import sys
from pathlib import Path

LEVELS = Path(__file__).resolve().parent.parent / "levels.tide"

NUDGES = [
    "A loose stone lifts from any set stone beside it, and lays over nothing. The stone you want is not the one in your path.",
    "A stone you have crossed can be lifted again from the far side, once nobody still needs it.",
    "Each of you starts beside a stone the other one needs. Hand them across the gaps; do not try to walk them round.",
    "A crate pushed over nothing fills it for good. Then the stone only has to cover what is left.",
    "Only one of these crates is worth more underwater. The other just has to get out of the way first.",
    "One crate becomes floor and the other only steps aside. Decide where the first one sinks before you move a stone.",
    "The plate is loose. Lay it where stepping off it takes you straight through the gate.",
    "Someone has to stand on the plate while the other pushes a crate through the gate. Work out who ends up where.",
    "The gate is open because a crate sits on the plate, and that crate is needed elsewhere. Plan what passes the gate before it moves, and what holds the plate after.",
    "Nothing stops on ice but an edge. Make one.",
    "Loose ice is a stone you cannot stand still on. Lay it where you only ever pass through.",
    "Loose ice is a bridge you can lay ahead of you and take up behind. Count the pieces against the gaps.",
    "Whatever you carry comes through the portal with you.",
    "A portal in someone's hand takes its twin out of play. Move one end, and the same step leads somewhere new.",
    "Crates go through portals too, and the far end is wherever you lay it.",
    "Only the portal's far end needs to travel. Keep carrying it to where the next arrival is wanted.",
    "Whoever carries a portal decides where the other one leads. Take turns being the one who moves the door.",
    "Everything loose here is a stone when you need one: the plate, the portals, the ice. The plate has two jobs, and it cannot do both at once.",
]

NAMES = {"@": "Ember", "&": "Tide"}
WAYS = {"N": "north", "E": "east", "S": "south", "W": "west"}
KINDS = {"stone": "stone", "ice": "ice", "portal P": "portal", "portal Q": "portal", "plate A": "plate", "plate B": "plate"}


def cell(text):
    x, y = text.split(",")
    return chr(64 + int(x)) + y


def sentence(line):
    who = NAMES[line[0]]
    m = re.match(r". at (\S+) lifts (.+) from (\S+)$", line)
    if m:
        return f"{who}, on {cell(m[1])}, lifts the {KINDS[m[2]]} at {cell(m[3])}."
    m = re.match(r". at (\S+) lays (.+) at (\S+)$", line)
    if m:
        return f"{who}, on {cell(m[1])}, lays the {KINDS[m[2]]} at {cell(m[3])}."
    m = re.match(r". pushes the crate at (\S+) (.): it sinks$", line)
    if m:
        return f"{who} pushes the crate at {cell(m[1])} {WAYS[m[2]]}, and it sinks."
    m = re.match(r". pushes the crate at (\S+) (.) to (\S+) ", line)
    if m:
        return f"{who} pushes the crate at {cell(m[1])} {WAYS[m[2]]}."
    m = re.match(r". steps (.) from (\S+) and (slides|jumps) to (\S+)$", line)
    if m:
        return f"{who} steps {WAYS[m[1]]} from {cell(m[2])} and {'slides' if m[3] == 'slides' else 'comes out'} at {cell(m[4])}."
    raise ValueError(line)


def times(n):
    return "once" if n == 1 else "twice" if n == 2 else f"{n} times"


if len(sys.argv) != 2:
    sys.exit("usage: hints.py <the solve program built from tools/solve.c>")
narration = subprocess.run([str(Path(sys.argv[1]).resolve()), "-t", str(LEVELS)], capture_output=True, text=True, check=True).stdout
levels, current = [], None
for line in narration.splitlines():
    head = re.match(r"(\d+) .*solved\s+moves\s+(\d+)", line)
    if head:
        current = {"moves": int(head[2]), "lines": [], "end": None}
        levels.append(current)
    elif line.strip().startswith("ends with"):
        m = re.search(r"@ at (\S+) and & at (\S+)", line)
        current["end"] = (cell(m[1]), cell(m[2]))
    elif line.strip() and not line.strip().startswith("("):
        current["lines"].append(line.strip())
assert len(levels) == 18 == len(NUDGES)

cases = []
for index, level in enumerate(levels):
    lines = level["lines"]
    lifts = {who: sum(1 for l in lines if l[0] == who and " lifts " in l) for who in "@&"}
    pushes = sum(1 for l in lines if " pushes " in l)
    sinks = sum(1 for l in lines if "sinks" in l)
    work = []
    for who in "@&":
        if lifts[who]:
            work.append(f"{NAMES[who]} lifts {times(lifts[who])}")
    if pushes:
        work.append(f"a crate is pushed {times(pushes)}" + (f" and {'one sinks' if sinks == 1 else f'{sinks} sink'}" if sinks else ""))
    facts = f"The shortest way is {level['moves']} moves. It ends with Ember on {level['end'][0]} and Tide on {level['end'][1]}."
    if work:
        facts += " Along it " + ", ".join(work) + "."
    opening = "The shortest way opens like this. " + " ".join(sentence(l) for l in lines[:6])
    if len(lines) <= 6:
        opening = "The whole of the shortest way: " + " ".join(sentence(l) for l in lines) + " Then both walk to their lanterns."
    for tier, text in enumerate([NUDGES[index], facts, opening]):
        assert '"' not in text
        cases.append(f'        case {index * 3 + tier}: return "{text}";\n')

text = LEVELS.read_text()
start = text.index("string LevelHint(int level, int tier)")
end = text.index("\nvoid LoadLevel(")
text = text[:start] + '''string LevelHint(int level, int tier)
{
    switch (level * 3 + Math.Clamp(tier, 0, 2))
    {
''' + "".join(cases) + '''    }
    return "";
}
''' + text[end:]
LEVELS.write_text(text, newline="\n")
print("Wrote", len(cases), "hints")
