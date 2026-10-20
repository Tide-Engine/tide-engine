# GRAVEWAKE

Ashwick burned. Up to four survivors hold a wide, flat clearing in the wood
outside it, night after night, against endless and growing hordes of the
dead, thousands at once. It's seen from above, and plays like Risk of Rain 2
in a PSX-era picture:
each picks a survivor (a Gunner, a Breacher or an Engineer), a gun to aim and
three skills on cooldowns, and the dead never stop coming, more and tougher
the longer it goes and the further into the nights.

Each night runs on a clock: three minutes to roam the clearing, kill and
level up, the dead coming thicker over its last minute, with chests hidden
about and optional missions coming and going (do one in time and everyone
picks one of the rarer items). When the clock runs out a ring of pale
wake-fire rises round you that the living can't cross and the dead walk
straight through, and the night's boss claws up in it with you, a mutant
grown from the dead: no running, a stand. Fell it and dawn breaks:
the dead burn away, whoever's down or bled out is up, and a moment later the
next night falls, harder. The chests not opened by then are gone with the
night. Mind the ground: open plague pits to go round, and fire bursting out
of it under the dead and under you.

Every kill leaves an orb of experience where the dead fell (a cyan gem,
turning), worth more the tougher they were, the party's once someone goes
within a few metres of it; at every level each takes one of three cards: their
gun a rank stronger, or an item that stacks, most of which fight on their own;
and more damage and 36 more health (a level's worth of it back, too). The
levels come about every 40 s at first and a minute apart later.
How many nights, and how long, they held out is the score.

Play at <https://tide-engine.dev/prototypes/gravewake/>. **Play** is alone;
**Host a room** and send the code to fight with friends, up to four.

## Playing

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Walk up, down and across the screen; it's a run once you haven't fired for a moment | WASD | Left stick |
| Aim (the crosshair is the cursor) | Mouse | Right stick |
| The camera nearer or further | Mouse wheel | |
| Fire | Left mouse | Right trigger |
| The gun's own skill, at the cursor | Right mouse | Left trigger or shoulder |
| Roll: a dodge, nothing the dead do touches you for most of it (two charges) | Space (or Shift) | East or South button |
| Pick a survivor, take an item | 1, 2, 3 | D-pad left, up, right |
| Reload (it loads by itself when empty) | R | West button |
| The room's menu | Escape, or **Menu** | |

### The survivors

| Survivor | Gun | Right mouse | By itself |
| --- | --- | --- | --- |
| Gunner | Carbine: automatic, any range | Phase Round: through every one of the dead down its line | Frag Grenade: into the thickest of them |
| Breacher | Pump Shotgun: fourteen pellets in a wide spread, never needs loading; takes a third less from the dead | Thumper: a grenade lobbed with a thump to burst where the cursor is, throwing the dead down | Boot: at the dead pressing in front, a side kick that sweeps a flash of light through where it reaches, flinging them back off their feet into the ones behind them; the nearest one down is stamped on |
| Engineer | Flamethrower: a jet of fire 4.8 m long, widening as it goes, burning everything in it and now and then setting it alight (bosses take it three and a half times over); ten seconds a tank (AP, Ricochet and HE Rounds aren't offered: it has no rounds) | Napalm: a glob of burning fuel lobbed to the cursor (never nearer you than 4.5 m), splashing into fire on the ground that burns the dead in it for three seconds, and you if you stand in it | Attack Drone: sent up when the dead are about; flies after you, at your shoulder, and fires on its own for twelve seconds, setting off your items |

The right mouse skill is the gun's own, and grows: a skill card (orange)
turns up in some level's offers, the skill a rank stronger, to rank III: a
phase round becomes three in a fan, then five; the Thumper two grenades, then
three; napalm's fire wider and longer, then two globs. Pips under the skill
show its rank. Round the crosshair, its arc on the left fills as it comes
back and lights up (with a chime) when it's ready; the arc on the right is
your magazine, or your loading as it fills.

### The gun grows

Your gun is where you start, and it grows with you: every offer at a level, a
chest or a boss has a gun card (violet) first, one of five for your gun, each
a rank stronger to its last (three, four or five ranks); the card says what
you'll have, and pips under it show its ranks. The gun's level (one, and every
rank) is by its name, bottom right.

| Gun | Cards |
| --- | --- |
| Carbine | Burst Fire (each shot more rounds in a tight fan), Heavy Caliber (harder, thicker rounds that push the dead back), Gas Piston (faster, more rounds), Marksman (headshots harder, and on through the dead behind), Incendiary Rounds (every third, then every other, then every round sets the dead alight, in red tracers) |
| Pump Shotgun | Buckshot (more pellets, wider), Slug Core (a heavy slug down the middle of every blast, through several of the dead), Quick Pump (pumps and loads faster), Concussion Shells (pellets throw the dead back, and close in knock them down), Dragon Shells (burning pellets that set the dead alight) |
| Flamethrower | Long Nozzle (further), Wide Nozzle (a wider cone, reaching more of the dead), Hot Fuel (harder, its fire burning brighter, then blue), Sticky Fuel (sets alight more often, and what it kills sets the nearest alight), Big Tank (more fuel, filling faster) |

Everything but the gun, the right mouse skill and the roll happens by
itself: each survivor's special as soon as it's ready and the dead are in
reach, and standing by a chest opens it, by a friend who's down raises
them. The guns never run out; only their magazines do.

The camera is high behind you looking down, north up the screen, and WASD
moves you up, down and across it whichever way you face; you turn to the
cursor, and your shots go level through it across the ground, whatever its
rise and fall, striking the dead by where they stand: the cursor on a head is
a headshot (the crosshair goes gold), on a boss's weak spot a weak hit
(orange), from the side it faces. A hit of your own (the gun, the right mouse
skill; not what fights by itself) flashes the body white, pops a mark out
round the crosshair (white, gold on a head, red with a ring thrown out for a
kill, orange on a weak spot, yellow for a crit, grey off a Warden's mail) and
sends its number up off the body in the same colours, with a crisp tick, a
heavier knock for a kill and a ding for a head; a head taken off jolts the
camera. A ring on the ground marks each
survivor, and what stands between the camera and you is cut away. There's
no jumping.

### The nights

| | |
| --- | --- |
| The clock | Three minutes a night, on the night's panel (BOSS IN 2:41) over a bar running down; over its last minute the dead come thicker, up to twice as fast. A bell and a banner ten seconds before the end. |
| The chests | Four a night, hidden about the clearing and not marked; the night's panel says how many are found. Stand by one a moment and it opens: everyone picks one of three items, whoever found it. They're gone when the next night falls. |
| Missions | Every half a minute or so an optional mission appears 14 to 28 m off, with a horn, a banner and a big gold pointer: hold a circle (12 s with someone in it), cull the dead in one (25 of them, more for a party), or catch five wisps (gold flames bobbing waist high, scattered 6 to 18 m round a place; walk through one to catch it), each within 45 to 50 s. Its circle burns in the ground in as many pieces as there are to do (a second held, one of the dead culled, a wisp caught), each lit gold as it's done; with 15 s left you're told again and the unlit pieces go red. Done, everyone picks one of three uncommon or rare items. None begins in a night's last 70 s. |
| The ring | When the clock runs out, pale wake-fire rises in a ring 17 m round you (round the middle of the party): you can't cross it till dawn; the dead walk straight through, coming three quarters as thick as before (fewer than that while any other boss stands). |
| Its boss | One claws up in the ring with you (two from the fourth night, three from the seventh), the nights taking the three in turn, pointed to (BOSS) at the screen's edge; anyone who bled out is back, by the ring's middle. Dawn doesn't come till they're down. Felled, each gives everyone an item to pick. |
| Dawn | Everything left of the dead burns away, every orb flies in, everyone's up, whole and ready. Ten seconds later the next night falls: more dangerous than the last (the danger climbs with the clock, and each night's is 1.25 times the one before), and each of its kind (mist, a blood moon, starless, the wake). |

### The items

At every level, and for every one of a night's bosses felled, three to choose
from (1, 2 or 3, shown across the middle of the screen); each stacks, the
card saying what you'll have with it (Tesla Coil III: the 4 nearest dead).
The run waits while anyone chooses, the picture and its sounds held still. A
chest gives everyone a pick too. Taking one throws up a column of light in
its colour. The ones that fight by themselves show how many you have: a drone
each over your head, a saw blade each circling you, a wider ring of fire,
more bolts of lightning, more rockets.

| Rarity | Items |
| --- | --- |
| Common | Hollow Points (damage), Hair Trigger (fire and reload faster), Kevlar Vest (health, armour), Running Shoes (speed), Leech Rounds (kills heal), Field Battery (skills back sooner), Extended Mag, Lucky Shot (hits for double), Smelling Salts (raise friends faster; not offered alone), Ricochet Rounds (hits bounce on to more of the dead), Bone Splinters (kills throw shards of bone into the dead round them), First Aid Kit (health comes back sooner and faster) |
| Uncommon | Live Wire (hits arc to more of the dead), Powder Keg (kills burst into the two nearest dead), Gun Drone (a drone each over your head shoots the nearest dead), Ember Ring (a ring of fire round you sets the dead alight), Frostbite (hits freeze), Energy Drink (another roll), Kerosene (the dead you roll past catch fire), Steel Toes (more to the dead on the ground), Bile Bomb (the Bloaters you kill burst wider and harder on the dead), AP Rounds (through one more), Saw Blades (a blade each circles you, cutting what it passes), Tesla Coil (lightning into the nearest dead), Molotov Satchel (firebombs at the thickest of the dead) |
| Rare | Mortar Flare (hits call a shell down), Second Wind (going down, straight back up, used up), HE Rounds (every round bursts where it lands), Dragon's Breath (hits set the dead alight), Thunder Drum (a shockwave throws down the dead round you), Rocket Pod (rockets at the dead round you) |
| Skill | Your right mouse skill a rank stronger (above) |
| Gun | Your gun a rank stronger in one way, first in every offer but a mission's (above) |

Uncommon and rare items turn up more as the run goes on. What you take is
yours till the game's over, through going down and bleeding out.

### The dead

They claw up out of the ground, two or three a second at first (more with
more of you) and many more as the danger climbs (Easy, Medium, Hard, Very Hard,
Insane, Impossible), tougher, more of them running (half at most, a third of
those sprinting) and a little quicker late on (a tenth at most: none outpaces
you walking as you fire), up to 4000 at once. Most come
in hordes: every five to nine seconds the ones due claw up together out of
one patch of ground 22 to 32 m from one of you, each from about the far side
of you from the last, a groan going up from them, all at one pace; a quarter
come
alone, from anywhere. One of the ordinary dead takes three of a carbine's
rounds to the body at first, about four later on, and one to the head (a head
hit does three times a body's; a shotgun pellet's half again). A head hit is
gold on the crosshair, a Lucky Shot's double yellow. Every one pressed against you slows you; a roll goes through
them. Their claws hit harder as the danger climbs, and your health comes back only
slowly (about a minute and a half from nothing), once you've gone six seconds
unhurt; First Aid Kits make it sooner and faster, Leech Rounds heal on kills, and
dawn makes everyone whole.

The ordinary dead are grey; every other kind is its own strong colour, its
body washed with it, its eyes burning it and a hard edge of it round its
outline, so it's known at a glance in a horde: red a Runner, lime a Bloater,
yellow a Spitter, blue a Warden, orange a Brute, magenta a Howler. Each has its
own body too (art/kinds): a Runner hunched with long claws, a Bloater's vast
belly, a Spitter's throat sac, a Warden's iron shoulders and shield, a Brute's
bull shoulders, a Howler tall with its neck thrown back. What each is about
to do is drawn on the ground, as a Bloater's ring is: a Runner's leap, a
Brute's blow, a Howler's reach, where bile will land.

| Kind | From | |
| --- | --- | --- |
| Runner (red) | 25 s | Lean, and quicker than any, though slower than you. From 2 to 5.5 m off, with nothing between, it crouches half a second (its eyes flaring, a strip of red on the ground where it'll leap, turning with you till the last fifth of a second, then set: step aside), then leaps 4.5 m and strikes as it lands (a roll's guard turns it). Three-fifths as tough as the rest. |
| Bloater (lime) | 40 s | Wider, slower, sick green with glowing boils, two and a half times as tough. Within 2.4 m of you it stops and swells, shuddering, glowing and hissing gas, and the ground it'll reach burns red, a ring at its edge and another growing out to meet it; it bursts 1.4 s later, hurting everyone within 3.6 m (up to 45) and tearing up the dead round it. Shoot it dead and it falls and swells the same way, bursting a second later: from far off, that's a weapon; up close, get out of the red. |
| Spitter (yellow) | 75 s | Thin and sallow, its throat glowing. It hangs back, 4 to 13 m off, and with a clear line to you its throat swells and it lobs bile, slow and high (a second or more in the air), that hurts (up to 18) and slows you for 2 s; a yellow ring on the ground is where it'll land. Step out of it, then find it and shoot it. |
| Warden (blue) | 2 min | Broad, in dark iron mail, its head bare. Rounds, blasts and blows to its body do a quarter as much (a grey mark, ARMOR over it, and a spray of sparks); its head takes them all. On its back, knocked down, its mail doesn't help, and fire burns through it. Its claws hit half again as hard. |
| Brute (orange) | 2:30 | A third bigger all round, slow, eight times as tough. Nothing staggers it or throws it down, and blows barely push it. Its blow does two and a half times a claw's and throws you back: an orange arc on the ground in front of it is where it'll land as it winds up. |
| Howler (magenta) | 3:40 | Tall and thin. It hangs back, 3 to 11 m off, and every 7 s or so stands and howls (its colour streaming off its head, a magenta ring on the ground as far as it'll reach); at the howl's end every one of the dead within 10 m of it is frenzied for 5 s, quicker, their outlines throbbing magenta. Shoot it first. |

### The bosses

Mutants twice the size of the rest, their name, health and weak spot across
the top of the screen, and a great deal of it: a fight of most of a minute
alone. A round anywhere into their bulk strikes them, feet and all; a
shotgun's pellets do half (every one finds something that big). Each has a growth that glows: a round there does several times as
much (an orange mark on the crosshair), and it glows brighter when it's
open. While one stands, fewer of the dead come. Roll through their slams,
charges and leaps.

| Boss | | Weak spot |
| --- | --- | --- |
| The Mauler | A hulk with arms like trunks. It slams (a red ring on the ground is where it comes down) and charges (a line on the ground is where it'll run); after a charge it stands spent a moment. | The sores on its back: three times as much |
| The Broodmother | A great egg sac on spindly legs, slow. She lobs a spread of bile and crouches to birth a pack of runners. | Her sac: twice as much, four times while she births |
| The Stalker | Stilt-legged, long-necked, quick. It crouches and leaps at you (a ring is where it lands), swipes close in, and shrieks, sending the dead near it into a frenzy. | The sac at its throat: half again as much, four times while it shrieks; enough there cuts the shriek short and staggers it |

Down, you fight on from the ground with a pistol, and a friend standing by
you for three seconds raises you, the gun back in hand as loaded as it was. Alone,
going down is the end. Bleed out and you're gone for a minute, or till the
night's boss rises or dawn breaks if that's sooner, watching a friend over
their shoulder; then you're back beside them with everything you had.

Every hit sprays blood out the far side, the way the round went, and it
soaks into the ground where it lands. The dead are torn apart when they die,
nothing left lying but the pieces: a pool spreading where they stood, the
trunk, arms, legs and head, lumps and gut flying and bleeding through the
air (a head shot throws the head furthest, a fountain after it); burned to
death, they fall apart in embers and charred pieces. Bloaters and Spitters
splash green bile. Fresh blood glistens, the moon catching it and bright flecks
of wet on it; old blood dries away in a few minutes, and at dawn it all dries
fast, so each night begins on clean ground. The fire, sparks and glows are
chunky blocks of light, their halos in the picture's big pixels and in hard
steps, and what spins and flies (a saw blade, a boot's sweep) leaves solid
slivers of light behind it, never a soft blur.

Development keys, not in play's interface: F2 six of the dead of a kind
(Bloaters, Spitters, Wardens, Runners, Brutes, Howlers in turn; with Shift, one
of each lined up in front of you, dazed), F3 a boss (the Mauler, the Broodmother,
the Stalker in turn), F4 two thousand more of the
dead, F5 a level (and so an offer; with Shift, a rank of each of your gun's
cards), F6 walk to the next part of the clearing,
F7 the night on (its end now, its boss rising; again while it stands, dawn),
F9 a camera over the clearing, F10 a random item (with Shift, the items that
fight by themselves, several each), F1 a mission (a hold, a
cull, the wisps in turn).

## Ashwick Wood

About 125 m across, its ground near flat, rising into the trees at its edge,
and open: little stands in the way between the landmarks. The dead claw up
out of the ground all round you, 16 to 32 m off, from every side.

| Where | What's there |
| --- | --- |
| The middle | The camp's fire, where everyone starts; worn paths out to each landmark |
| North | The chapel's ruin, its walls half down (a great brazier burning in it) |
| East | The cairn: a ring of great boulders open toward the fire, a great brazier in it |
| West | The old graveyard: ten graves the dead rise from, their stones small enough to run past; a great brazier at its edge |
| North-west | A ring of standing stones, a great brazier in it |
| South | A great dead oak, a great brazier beside it |
| About the clearing | A few sparse groves and big stones to stand behind; eleven plague pits, open graves 4 to 7.5 m long brimming with black blood, a sick light drifting on them, earth heaped at one end (nobody walks into one, you or the dead: they go round; rounds go over); and a dozen bramble thickets, dark bushes 2 to 4 m round that you wade through at half pace (the dead don't care) |

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
| `nights.tide` | The nights: each one's clock, its bosses at its end, dawn, calling the fallen back, each night's look, the night's panel |
| `hazards.tide` | The plague pits, the bramble thickets, and fire on the ground (napalm's): what they do, what's seen and heard of them |
| `boss.tide` | The bosses: the Mauler, the Broodmother and the Stalker, the night's own and the development key's, their moves and weak spots |
| `kits.tide` | The survivors' kits: the roll, the guns' own skills, the skills that use themselves and where, the Engineer's attack drone |
| `items.tide` | The items: what each does, the offer at a level, what a hit or a kill sets off, the drone and the ember ring, levels |
| `zombies.tide` | The dead and their kinds: rising, hunting, a Spitter's bile, a Warden's mail, a Bloater's burst, wounds, burning, freezing, going over |
| `weapons.tide` | The guns' numbers |
| `hands.tide` | Running, rolling, turning to the cursor, firing (each round a ray through the dead), loading, raising friends, going down |
| `missions.tide` | The optional missions: a hold, a cull, the wisps; their reward, panel and markers; and where the objectives are |
| `missiles.tide` | Frag grenades and the Thumper's, firebombs, napalm, the Mortar Flare's shells, a Spitter's bile |
| `drops.tide` | The cues for what's heard, and a survivor's refill |
| `crowd.tide` | The horde, its kinds and the bosses drawn: low-poly bodies whose clips a vert plays from a texture, thousands in a few draws |
| `kit.tide`, `look.tide`, `eye.tide` | The clearing's meshes; light, shadows, fog, bloom; the camera |
| `flames.tide` | The flamethrower's fire as it's seen: a little fluid of burning fuel, swirling, rising, pooling and cooling to smoke |
| `people.tide`, `held.tide`, `setpieces.tide`, `fx.tide`, `gore.tide` | The survivors; the gun in hand, grenades, napalm, the attack drones, the gun drones; the graves and braziers; tracers, flames, blood, sparks; the stains on the ground and the pieces of the dead |
| `hud.tide`, `sound.tide` | What's written over the picture (the offer and the survivors too), the title; what's heard |
| `art/` | The forest, ruin, people, the kinds of the dead, mutants, guns and items, with how each was made: see `art/CREDITS.md` |
| `sfx/` | The sounds, made by `sfx/tools/make_sfx.py`: see `sfx/CREDITS.md` |
| `test/` | The run played with no window (`test.c`), a tick's cost with a horde (`perf.c`, `gravewake_perf`, not a test), whole runs played by bots for the balance (`balance.c`, `gravewake_balance`, not a test), and the browser test |
| `tools/balance_math.py` | The balance on paper, read from the source: night by night, what the dead bring and the level pace |
