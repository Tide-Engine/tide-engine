# GRAVEWAKE

Ashwick burned. Up to four survivors hold a wide, hilly clearing in the wood
outside it, night after night, against endless and growing hordes of the
dead, thousands at once. It plays like Risk of Rain 2 in a PSX-era picture:
each picks a survivor (a Gunner, a Breacher or an Engineer), a gun to aim and
three skills on cooldowns, and the dead never stop coming, more and tougher
the longer it goes and the further into the nights.

Each night, a beacon stands cold at one of the clearing's landmarks, a pale
column of wake-fire over it, and chests are hidden about the clearing. Look
about for them as long as you dare, then hold E at the beacon to light it.
That starts the vigil: the dead come thicker, most of them for the beacon,
and the night's Brute claws up. Keep near the beacon till it's full and its
Brute is down, and dawn breaks: the dead burn away, whoever's down or bled
out is up, and a moment later the next night falls, harder, at another
beacon. The chests not opened by then are gone with the night.

Every kill leaves an orb of experience where the dead fell, worth more the
tougher they were, the party's once someone goes within a few metres of it;
at every level each takes one of three items that stack, most of which fight
on their own. How many nights, and how long, they held out is the score.

Play at <https://tide-engine.dev/prototypes/gravewake/>. **Play** is alone;
**Host a room** and send the code to fight with friends, up to four.

## Playing

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Walk; forward runs, once you haven't fired for a moment | WASD | Left stick |
| Look | Mouse | Right stick |
| Over the shoulder or through the eyes | V | Right stick press |
| Jump (again in the air, with Spring Boots) | Space | South button |
| Fire | Left mouse | Right trigger |
| The secondary skill | Right mouse | Left trigger |
| Dash (two charges; knocks down the dead it goes through) | Shift | Left shoulder |
| The special skill | Q | Right shoulder |
| Pick a survivor, take an item | 1, 2, 3 | D-pad left, up, right |
| Reload | R | West button |
| Raise a friend who's down, open a chest, light the beacon (hold) | E | West button |
| The room's menu | Escape, or **Menu** | |

### The survivors

| Survivor | Gun | Right mouse | Shift | Q |
| --- | --- | --- | --- | --- |
| Gunner | Carbine: automatic, through three | Phase Round: through every one of the dead in its line | Dash | Frag Grenade |
| Breacher | Pump Shotgun: eleven pellets, never needs loading; takes a third less from the dead; a dash that bowls them over | Boot: kicks the dead in front over; one down is stamped on | Dash | Firebomb |
| Engineer | SMG: sets items off half again as often | Mine: bursts when the dead come near | Dash | Turret: fires on its own for fifteen seconds, setting off your items |

Skills fire as soon as they're ready while their key is held. The guns never
run out; only their magazines do.

### The nights

| | |
| --- | --- |
| The beacon | Five great braziers stand at the landmarks: on the spire's top, in the chapel's ruin, by the graveyard, among the standing stones and down in the ravine. Each night one of them (never last night's) burns a pale wake-fire, seen across the clearing. |
| The chests | Five a night, and two more for each survivor past the first, hidden about the clearing and not marked; the night's panel says how many are found. They're gone when the next night falls. |
| The vigil | Lit, the beacon is kept while the survivors standing are within 11 m of it: in 90 s with all of them there, slower with only some, not at all with none. The dead come 1.4 times as fast, most of them rising round it. Lighting it also calls back anyone who bled out. |
| Its Brute | One rises as it's lit (two from the fourth night, three from the seventh); dawn doesn't come till they're down. Felled, each gives everyone an item to pick. |
| Dawn | Everything left of the dead burns away, every orb flies in, everyone's up, whole and ready. Ten seconds later the next night falls: more dangerous than the last (the danger climbs with the clock, and each night's is 1.25 times the one before), and each of its kind (mist, a blood moon, starless, the wake). |
| Waiting | A night that goes on more than four and a half minutes grows impatient (IMPATIENT on its panel): the danger climbs twice as fast till its dawn. While it's unlit, a Brute comes every two and a half minutes, with no prize. |

### The items

At every level, and for every one of a night's Brutes felled, three to choose
from (1, 2 or 3, shown across the middle of the screen); each stacks. The run
waits while anyone chooses. Chests give one at random.

| Rarity | Items |
| --- | --- |
| Common | Hollow Points (damage), Hair Trigger (fire and reload faster), Kevlar Vest (health, armour), Running Shoes (speed), Leech Rounds (kills heal), Field Battery (skills back sooner), Extended Mag, Lucky Shot (hits for double), Smelling Salts (raise friends faster; not offered alone) |
| Uncommon | Live Wire (hits arc to more of the dead), Powder Keg (kills burst into the dead near them), Gun Drone (shoots the nearest dead), Ember Ring (sets the dead near you alight), Frostbite (hits freeze), Spring Boots (jumps in the air), Energy Drink (another dash), Kerosene (the dash sets the dead it goes through alight), Steel Toes (more to the dead on the ground), Gas Mask (Bloaters' bursts can't hurt you; the ones you kill burst wider), AP Rounds (through one more) |
| Rare | Mortar Flare (hits call a shell down), Second Wind (going down, straight back up, used up), HE Rounds (every round bursts where it lands), Dragon's Breath (hits set the dead alight) |

Uncommon and rare items turn up more as the run goes on. What you take is
yours till the game's over, through going down and bleeding out.

### The dead

They claw up out of the ground all round you, from every side, three a
second at first (more with more of you) and many more as the danger climbs
(Easy, Medium, Hard, Very Hard, Insane, Impossible), tougher and quicker
too, up to 4000 at once. A head hit is gold on the crosshair, a Lucky Shot's
double yellow. Every one pressed against you slows you; a dash goes through
them. None climbs a cliff alone: they pile up its face on each other.

| Kind | From | |
| --- | --- | --- |
| Bloater | 40 s | Wider, slower, sick green with glowing boils, two and a half times as tough. Within 2.4 m of you it swells and bursts a second later, hurting everyone within 3.6 m (up to 45) and tearing up the dead round it. Shoot it dead and it bursts at once: from far off, that's a weapon. |
| Spitter | 75 s | Thin and sallow, its throat glowing. It hangs back, 4 to 13 m off, and with a clear line to you its throat swells and it lobs bile that hurts (up to 18) and slows you for 2 s. Find it and shoot it. |
| Warden | 2 min | Broad, in dark iron mail, its head bare. Rounds, blasts and blows to its body do a quarter as much (a grey mark, and they ring); its head takes them all. On its back, knocked down, its mail doesn't help, and fire burns through it. Its claws hit half again as hard. |
| Brute | the vigil | Three times the size of the rest, its health across the top of the screen. It comes for the nearest of you, and every couple of seconds it slams (a red ring on the ground is where it comes down), charges (a line on the ground is where it'll run) or, every third time, roars up the dead round it. |

Down, you fight on from the ground with a pistol, and a friend holding E for
three seconds raises you, the gun back in hand as loaded as it was. Alone,
going down is the end. Bleed out and you're gone till the beacon's lit or
dawn breaks, watching a friend over their shoulder; then you're back beside
the beacon with everything you had.

Development keys, not in play's interface: F2 six of the dead of a kind
(Bloaters, Spitters, Wardens in turn), F3 a Brute, F4 two thousand more of the
dead, F5 a level (and so an offer), F6 walk to the next part of the clearing,
F7 the night kept (dawn now), F9 a camera over the clearing, F10 a random
item.

## Ashwick Wood

About 125 m across, its ground rising into the trees at its edge, and open:
little stands in the way between the landmarks. The dead claw up out of the
ground all round you, 16 to 32 m off, from every side.

| Where | What's there |
| --- | --- |
| The middle | The camp's fire, where everyone starts; worn paths out to each landmark |
| North | The chapel's rise, 6 m up, the ruin on it (a beacon in it), a ramp down each side |
| East | The spire: a lower tier 4 m up, its top 9 m up with a beacon, a ramp to each |
| West | The old graveyard, sunk in its hollow: ten graves the dead rise from, their stones small enough to run past; a beacon at its edge |
| North-west | A ring of standing stones, a beacon in it |
| South | The ravine, cut across the clearing, boulders on its banks, a beacon at its bottom |
| About the clearing | A few sparse groves, a wagon and a cart, and big stones to stand behind |

## Build and run

From the repository root:

```sh
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target gravewake gravewake_test
ctest --preset debug -R '^gravewake'
```

For the browser page, and the browser test on both backends:

```sh
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target gravewake_page gravewake_test
ctest --preset web-debug -R '^gravewake'
```

Serve `docs/static` and visit `/prototypes/gravewake/`. Or run `tide run` in
this folder.

## Files

| File | What's in it |
| --- | --- |
| `game.tide` | The survivors, the input, joining and leaving, starting over, the development walks |
| `plan.tide`, `pieces.tide` | Ashwick Wood as one plan: the ground's heights (`Ground`), the clearing's edge, the ruin, the graves, the camp, the forest |
| `ashwick.tide` | The plan made solid: collision, the ground under feet, the lines rounds and grenades follow, the way to the survivors |
| `rounds.tide` | The run: the danger with time and the nights, how many of the dead come, of which kinds and how tough, where they rise |
| `nights.tide` | The nights: the beacons and the vigil, dawn, calling the fallen back, each night's look, the night's panel |
| `boss.tide` | The Brutes: the night's own and the impatient ones, their slam, charge and roar, drawn three times the size |
| `kits.tide` | The survivors' kits: each skill's use and cooldown, the Engineer's turret |
| `items.tide` | The items: what each does, the offer at a level, what a hit or a kill sets off, the drone and the ember ring, levels |
| `zombies.tide` | The dead and their kinds: rising, hunting, a Spitter's bile, a Warden's mail, a Bloater's burst, wounds, burning, freezing, going over |
| `weapons.tide` | The guns' numbers |
| `hands.tide` | Running and jumping, firing (each round a ray through the dead), loading, raising friends, going down |
| `missiles.tide` | Frag grenades, firebombs, mines, the Mortar Flare's shells, a Spitter's bile |
| `drops.tide` | The cues for what's heard, and a survivor's refill |
| `crowd.tide` | The horde drawn: low-poly bodies whose clips a vert plays from a texture, thousands in a few draws |
| `kit.tide`, `look.tide`, `eye.tide` | The clearing's meshes; light, shadows, fog, bloom; the camera |
| `people.tide`, `held.tide`, `setpieces.tide`, `fx.tide` | The survivors; the gun in hand, grenades, mines, turrets, drones; the graves and braziers; tracers, flames, blood, sparks |
| `hud.tide`, `sound.tide` | What's written over the picture (the offer and the survivors too), the title; what's heard |
| `fp.tide` | The first-person arms: which clips move them for each gun |
| `art/` | The forest, ruin, people, guns, items and first-person arms, with how each was made: see `art/CREDITS.md` (the arms are CC BY 4.0, by CoinCoin) |
| `sfx/` | The sounds, made by `sfx/tools/make_sfx.py`: see `sfx/CREDITS.md` |
| `test/` | The run played with no window (`test.c`), a tick's cost with a horde (`perf.c`, `gravewake_perf`, not a test), whole runs played by bots for the balance (`balance.c`, `gravewake_balance`, not a test), and the browser test |
| `tools/balance_math.py` | The balance on paper, read from the source: night by night, what the dead bring and the level pace |
