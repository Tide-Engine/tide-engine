# GRAVEWAKE

`prototypes/gravewake/`: a third- or first-person cooperative zombie survival game for
one to four, requested on 2026-10-16. The catalog and staging contract is
`notes/private-prototypes.md`; this note is the game's. How to play, the map
and the files are in the game's `README.md`.

## What the owner asked for

"New prototype, medieval zombie game; I guess on the vibes of Black Ops 2
zombies but swords, bows, crossbows, etc. We should include A LOT of them and
it should be 3D first person. It should be a polished game." Later: software
renderers being slow is fine; the owner tests performance on their machine.

Then, the same day: "i think this could work better as a roguelike in maybe a
forest or something where after each wave we can choose some upgrade or
another weapon or something like that". Asked, the owner chose all three of
the recommended answers: the forest **replaces the village** (one clearing;
the doors, windows, walls to buy from, the chest, the Runeforge's anvil and
the beacon go); after each wave everyone **picks one of three cards, with no
gold** (a weapon, a brew, an upgrade; points stay as a score); the run is
**endless and escalating** (how far they got is the score).

Then, after playing it: "i think we should have classes that we are forced to
pick at the start. so at the start you pick one (melee, range or magic or
whatever we have in here) and then you upgrade that and yourself"; "could be
fun also having one slot for some other attack or movement upgrade? ... I
really liked The Spell Brigade"; "we dont have enough space to move around nor
enough zombies"; "the contrast between the environment and the zombies makes
it hard to read whats on screen"; "We should also have some verticality; I
think something like Mega Bonk for the terrain could be fun". Asked, the owner
chose Warrior, Ranger and Mage; one ability on Q with a cooldown (fire pots
and bells becoming abilities); hordes "as many as performance allows (150+)";
and a big hilly forest arena about 80 m across. Also fixed then: the fire pot
throw (its arms were placed with the held crossbow's aim and spun), and "the
tiny one handed crossbow doesnt need a floating left hand".

Everything else is Claude's pick and Provisional: the name, Ashwick Wood and
its layout, the classes' kits, the abilities, the cards and every number, the four survivors, the weapon list,
the brews, the art direction and the controls.

## The pivot (2026-10-16, in progress)

The owner, after the classes and hordes: "PSX low poly style, lets switch to
more modern fire arms. lets get inspired by games like Megabonk and The Spell
Brigade or Vampire Survivors much more but keep the manual shooting aspect to a
degree. I want to control my main weapon but be able to have passive abilities
that do damage too ... I think Risk of Rain 2 is closer to what I want.
Essentially a roguelike but zombies with guns and a shit ton of enemies, 150
wont cut it." Asked, the owner chose: Risk of Rain 2 stages (gold from kills,
chests of stacking items, a teleporter-like event and a boss, then the next
stage, difficulty climbing with time); RoR2-style survivor kits, three to
start (a gun you aim, a secondary, a movement skill, a special); 2000 and more
of the dead, "push it"; and several PSX biomes.

The work is in phases, each merged when it plays: 1, the horde's technology
(done, below); 2, the PSX look, modern guns, the three survivor kits and
stacking items with levels (done, below; the items are offered three at a
time after each wave until chests come); 3, the RoR2 loop proper (gold from
kills, chests that sell the items, a teleporter-like event and a boss, elites,
difficulty with time, stage after stage in place of waves; built as nights,
below; the bosses became three mutants after the third playtest); 4, the
biomes, and more survivors. All the picks in phase 2 are mine (Provisional): the
kits, the items, the numbers.

### A reward after its cause is seen, chests shut, napalm smaller (2026-10-20)

The owner: "when an objective is completed or a boss killed the reward shows
instantly before i even see the boss die; we need some delay there. also chests
should be closed lol. / the napalm ability felt a bit OP and hard to not get
myself hit by it; i dont hate it i dont love it". The numbers are mine,
Provisional.

- **A reward's pick waits** (items.tide's `Owed.delay`, `Experience`;
  `Survivor.rewardWait`; boss.tide's `TakeFelled`): a boss's pick is offered 2.5 s
  after it falls (`BOSS_REWARD_WAIT`), a mission's 1.5 s after it's done, a chest's
  1 s after it bursts open; nothing's offered meanwhile (a level's waits too), and
  the wait doesn't run while the match is held for a pick.
- **Chests shut** (art/kit/tools/close_chest.py, run once over the kit's two chest
  parts, `Chest_Wood~furniture.glb` and `~metal.glb`): the kit's chest is modelled
  with its lid flung open; the script finds the lid's loose parts (starting behind
  the box, or rising above its top) and swings them 122.3 degrees shut about the
  hinge along the box's back top edge (found from where the lid's front and back
  edges are open against where they lie shut), and writes the parts back as
  split_kit.py exports them. Looked at in Blender renders of both parts.
- **Napalm smaller** (kits.tide, missiles.tide): its fire 2.0 m round (2.6) for
  3 s (4), biting 0.18 of its splash (0.25); its splash 100 (120) in 2.0 m (2.4);
  every 8 s (7); and it lands at least 4.5 m from its thrower whatever the cursor
  is on (`NAPALM_NEAREST`), so a glob aimed at the dead on top of you doesn't burn
  you. The ranks' wider and longer as before.
- **Measured** (`gravewake_balance`, seeds 1 to 16): the Gunner's middle run 1458
  s, the Breacher's 581, the Engineer's 206 (most Engineers still die in the first
  night's end, in the Mauler's slam: the bots don't roll).
- **Tests**: the Mauler felled, its pick not on offer (2.5 s to wait), then on offer
  three seconds on; napalm's splash 141, its fire 2.0 m for 3 s. Native debug
  `gravewake_test` and the web debug WASI one pass.

### Napalm burns its maker; what's coming back, round the crosshair (2026-10-20)

The owner: "my own napalm fire should hurt me"; "we need some indicator on the
crosshair or some sound or something when the ability is ready, even when
reloading some way to know the progress without glancing down would be
appreciated." The shapes and numbers are mine, Provisional.

- **Napalm burns the living** (hazards.tide's `Burn`, `BLAZE_HURT` back): every
  survivor standing in a fire on the ground, its maker too, takes 12 a second times
  `Harm` (a bite every half second), not while rolling, as the eruptions' fire did.
  The bots step out of one again (test/balance.c).
- **Round the crosshair** (hud.tide's `Overlay`; `Feed.readyFlash`, `wasReady`):
  two short arcs just outside its ticks. On its left, the right mouse's skill:
  grey, filling out from its middle as it comes back; ember orange and thicker once
  it's ready, flaring as it comes (a ring thrown out and the arc swelling a moment).
  On its right, the gun: its magazine running down from full (bone, amber at a
  quarter, red empty), and while loading, its loading filling in blue; none for a
  gun never loaded (the shotgun). Each on a dark track. They fill from their middles,
  so the arcs' angles needn't run either way.
- **A chime when it's back** (sound.tide's `Handling`, `Ears.skillReady`;
  `skill_ready.ogg`, made by `make_sfx.py`): two soft rising notes of a small bell,
  quiet, as the right mouse's skill comes back.
- **Measured** (`gravewake_balance`, the Engineer, seeds 1 to 16): the middle run
  about 270 s (about 515 before its fire burned it), most dying in the first night's
  end: the bots don't roll, and walk out of their napalm late.
- **Tests**: napalm's maker standing in its fire loses 46 in two-thirds of a second.
  Native debug `gravewake_test` and the web debug WASI one pass, built again with
  LLVM 23 (back on this machine).
- **Validation**: looked at in standard-host captures round the cursor: the skill's
  arc ready, used and filling back, the magazine running down, the loading in blue.
  Not heard: the chime's loudness.

### The gun's own right mouse, brambles for the eruptions, a drone, fire like a fluid (2026-10-20)

The owner, after playing the flamethrower round: "can we make it visually
impressive? almost like a light fluid simulation"; "the leftover smoke makes it
hard to view stuff"; then "i still think the flamethrower kills too fast and
maybe still a tad too far / the random exploding circles are out of place, we
can maybe just add bushes in the map that make you walk slower ... / the normal
hit markers are still hard to hear, i like the critical/headshot effect ... /
the turret is funny, its a tripod sliding around... lol maybe we dont do that,
maybe its just a flying drone as a started idk. in general the C4 also feels
meh. im starting to think the right click should be one ability that is related
to the gun you hold. the rifle has the sniper like shot, the shotgun maybe
shoots a grenade with a thump sound and the flamethrower maybe shoots fire and
leaves the ground burning for a bit or something. these should be able to take
upgrades to become better." What was made of each, and the numbers, are mine,
Provisional.

- **Fire like a fluid** (flames.tide, new; this machine's only, nothing of the
  match): each puff throws a gout of 18 blobs of burning fuel, most down the
  cone's middle, spread over the time to the next puff as though they'd come out
  all along (a stream, not clumps), thrown at the speed that the air's drag
  (`FLAME_DRAG` 2.3 a second) slows to a stop about at its reach (`Loosed.reach`).
  Each frame they slow, the hot rise (`FLAME_LIFT`, as heat squared), a field of
  Perlin noise turns them, more as they slow (`FLAME_SWIRL`), they swell and cool
  through white-gold, yellow, orange to a dull red (`FireHue`); a hot one landing
  splashes and runs along the ground, and three in ten pool there a second or so,
  flickering, tongues of flame off them and a scorch under; fire licks up round
  the dead it reaches (`Lick`, from `Struck`); one cooled gives off a thin wisp of
  smoke one time in eight (`SMOKE_CHANCE`). Each is drawn (`FireBlob`) as a ragged
  tongue of light in three hard steps, its middle hottest, added; a lamp at the
  middle of each survivor's hot fire and one among what's pooled (`FireLight`).
  Iterated in captures: specks like confetti at first (blobs a pixel or two in the
  low picture), then a milky white cloud (overlaps adding up): now each is two or
  three times the size, a quarter as bright, redder as it cools. The smoke, first
  thick, near black and lasting, piled up over the survivor into a black cloud
  (the owner's picture): a wisp now, a sixth as opaque, gone in under a second.
- **The flamethrower slower and shorter**: 4.8 m (5.5), 7 a puff (13), a puff in
  seventeen sets one alight (`FLAME_IGNITE` 0.06; a third): a walker burns down in
  about two thirds of a second, not a quarter. A boss takes a puff 3.5 times over
  (`FLAME_BOSS`, boss.tide's `BossWound`): a great body in the flames, no weak spot
  to find and no catching fire. (5 a puff and 0.04 measured too weak, below.)
- **The right mouse is the gun's own** (kits.tide's `SecondaryOf`, `SkillOf`,
  `UseSecondary`; items.tide's cards): the carbine's phase round as it was; the
  shotgun's **Thumper**, a grenade lobbed (`ThrowTo`'s new pace, 24 m/s across the
  ground) to burst where the cursor is, 300 times might in 4 m, the dead thrown
  down, a "thoomp" (`thump.ogg`), every 5 s; the flamethrower's **Napalm**, a glob
  of burning fuel (`Shot.Napalm`, drawn alight, the fire shedding off it in
  flight) lobbed to the cursor, 120 in its splash and setting the dead alight,
  and where it splashes a fire on the ground (hazards.tide's `Blaze`, its
  `seat`, `seconds` and `hurt`), 2.6 m round for 4 s, biting the dead in it,
  bosses too, a quarter of the splash every half second and setting them alight,
  never the living; every 7 s. Skill cards are now only the gun's skill a rank
  stronger (`CardPhaseRound`, `CardThumper`, `CardNapalm`; the card's offered only
  below rank III): the Thumper two grenades, then three, a quarter and half again
  as hard; napalm's fire a quarter wider and half again as long, then two globs.
  `Survivor.secondary` is gone. The C4 (`Shot.Mine`, `charges`, `detonations`, the
  charge rings), the Leap and the Blink (`leaping`, `Slam`, `BlinkBlast`) are gone.
- **The Breacher's boot by itself**: the shotgun's grenade took the right mouse,
  so the boot is the Breacher's special (`AutoSkill` takes the survivor's entity
  for it), kicking when one of the dead is within its reach in front, at rank I;
  the Firebomb left the kits (the Molotov Satchel still throws them).
- **The attack drone** (kits.tide's `Sentry`, `DRONE_HIGH`; held.tide's
  `DrawTurret`, `DronePlace`; art/guns/drone.glb by `art/guns/tools/make_drone.py`):
  the Engineer's special flies: sent up over their shoulder, it keeps 2.2 m off at
  2.4 m over the ground, over whatever stands in the way (no more pushing out of
  boxes), firing as before. Drawn: an olive armoured quadcopter, a hazard stripe,
  four arms out to motor pods, a gun slung under its nose, a red eye that glows,
  skids (588 triangles), bobbing, tipped the way it flies, its four rotors turning.
  `TurretFeet`, the tripod's model and `TURRET_HIGH` went.
- **Brambles for the eruptions** (hazards.tide; plan.tide's `brambles`,
  `BRAMBLES`, `BRAMBLE_SLOW`; ashwick.tide's `Ashwick.brambles`; hands.tide's
  `InBrambles` in `Steer`): the ground cracking and bursting near the survivors
  every few seconds is gone (`Erupt`, `Cracked`, `Erupted`, `Match.eruptWait`, the
  bots' stepping out of fires); in its place, a dozen bramble thickets 2.2 to 4 m
  round out between the landmarks, clear of them, the paths, the camp, the
  graveyard and the pits, each thick with the kit's bushes (and a fern in five)
  darkened to 0.55. A survivor walking in one goes at half pace; a roll's as it was;
  the dead don't care. The Blaze and its drawing (flames, light, the ground's glow,
  a faint ring) stay, for napalm.
- **A hit heard** (sfx/tools/make_sfx.py's `hit_tick`; sound.tide): a small struck
  plate's tink, the head's ding's little brother, ringing a tenth of a second, at
  0.85 (the first tick, at 0.5, was lost under the guns).
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16; the bots
  now use the gun's skill: a phase round down a line of three, a grenade or napalm
  into three or more): the Gunner's middle run 1123 s (990 the last round), the
  Breacher's 597 (849), the Engineer's 214 at 5 a puff, 205 at 7, about 515 with
  bosses taking flames 3.5 times over (about 630 at 13 a puff and 5.5 m); nine of
  the sixteen Engineers die in the first night's end, standing within the
  flamethrower's reach of the Mauler, inside its slam, and the bots don't roll. A
  player rolls; if the Engineer's too weak at a boss, the levers are `FLAME_BOSS`
  and the puff's 7. The Breacher lost the firebomb that used itself and gained a
  grenade by hand. The night's end fights 15 to 30 s.
- **Tests**: the Thumper does 432 to one 7 m off at the cursor; the boot by itself
  kicks one close over and stamps on one down; napalm 6 m off splashes (177), its
  fire 2.6 m round for 4 s burns one walked into it (367 more, alight) and goes
  out; the drone flies 7.8 m after its maker to 2.2 m from them, 2.4 m up; the
  flamethrower held two seconds burns the two in its cone (3 m, and 4 m 1.2 m
  aside) and sets at least one alight; 12 thickets, half a second's walk 1.85 m in
  one and 3.69 on open ground; a skill card a rank, to III (the phase round's fan).
  The eruption's checks, the C4's, the leap's and the blink's went. The cull
  mission's check now stands its survivor on whichever side of the circle has
  nothing standing in the way and moves the other dead off its line (a round stops
  in the first it strikes; the run's changed draws had put a trunk, then the dead,
  between). The generated archetype numbers moved again (`arch4_Blaze`, `arch5_Orb`,
  `arch6_Zombie`, `arch7_Chest`). Native debug `gravewake_test` and the web debug
  WASI one pass.
- **Built with Visual Studio's clang** (18.1.8): `C:/Program Files/LLVM` was gone
  from this machine partway through (not this work's doing), so the debug, release
  and web builds were configured afresh and found the toolchain's fallback.
- **Validation**: looked at in standard-host captures: the jet into the line-up
  and into a horde, after a long burst (the smoke clearing), napalm splashing into
  fire among the dead with the drone at the survivor's shoulder, the Thumper's
  burst at the cursor, walking into a thicket. Not heard: the sounds' loudness.

### The Engineer's flamethrower (2026-10-20)

The owner: "i wonder if we should change the engineer's weapon; its too similar
to the rifle. maybe a flamethrower could be a fun change. so rifle is long
range, shotgun is close/long mix and the flames is mostly close range
thingy." The numbers, the look and the sound are mine, Provisional.

- **The gun** (weapons.tide's `Weapon.Flamer`, `WeaponInfo.cone`; kits.tide's
  `RoleWeapon`): the Engineer's in the SMG's place (the turret and the drones still
  carry an SMG). A puff every 0.07 s while it's held, 13 a puff times might, to
  every one of the dead in a cone 5.5 m long across the ground and 0.3 m either
  side of the aim a metre out (3.3 m wide at its end), and any in front within 1.3 m
  (`FLAME_NEAR`), the 14 nearest (`FLAME_MOST`), short of what stands in the way;
  each puff sets one alight one time in three (`FLAME_IGNITE`), so a body in the
  flames burns within about a fifth of a second. A tank is 140 puffs (ten seconds),
  1.8 s to fill; an Extended Mag half again. No headshots, no shove or reel (the
  dead walk on through it: the risk of close range). Flames lick in under a
  Warden's mail. Items go off a puff in eight as often as a round's (`proc` 0.12),
  each one the puff reaches.
- **How** (hands.tide's `Scorch`, from `Fight` when `cone > 0`; items.tide's `Land`
  takes a `Blow` and `ignite`, defaulting to a round's): its puffs are
  `Blow.Flame`, aimed (the hit marker, the numbers), drawn and heard apart:
  no blood or flesh sound a puff, an ember now and then; burned to death in it,
  the dead fall apart charred, as burning ones do; the marker stays up through a
  stream without popping each puff, and a body's numbers add up in orange while the
  puffs keep coming (`Tally.flame`, `FLAME_MERGE` 0.3 s); the dead flash a little
  (0.3), bosses 0.25.
- **Shorter, and a nozzle to lengthen it** (the owner, after playing: "we should
  decrease a bit the range of the fire; we can allow upgrades of course but it
  feels a bit too powerful right from the start"): it was 7.5 m and 0.4 a side (6
  m wide at its end, not the 3 written first). AP Rounds are its upgrade: shown to
  the Engineer as a Long Nozzle (hud.tide's offer, items.tide's `Nozzle`), each a
  metre more (hands.tide's `FLAME_NOZZLE`). `Loosed.reach` carries how far it
  reaches, and the fire's drawn as far (flames.tide's `Gout`: thrown at the speed
  that, with the air's drag, carries it there).
- **Offers** (items.tide's `Useless`): Ricochet Rounds and HE Rounds, what a round
  does, aren't offered a survivor whose gun is the flamethrower.
- **Seen** (fx.tide's `MuzzleFlash`): no flash; each puff throws eight blocks of
  flame down the cone, 14 to 19 m/s, as far as it reaches, swelling and cooling
  from orange to red and rising a little, two hotter yellow ones near the nozzle,
  now and then smoke off its end, its orange light along the way. (A first try,
  pale blocks half again as big at alpha 1.5, bloomed to white.)
- **The model** (art/guns/flamer.glb, `art/guns/tools/make_flamer.py`, Blender, from
  primitives in the guns' palette; guns.json's line put in by it): a steel body
  over a red fuel tank with a brass band, a hose from its valve up into the barrel,
  a slotted heat shield, a flared nozzle with a glowing pilot light under it, a fore
  grip (people.tide's `ForeGrip` 0.44) and a short stock; 808 triangles.
- **Heard** (sfx/tools/make_sfx.py, synthesized): `flamer_start`, a rush of fire
  through air and a soft thump as it lights; `flamer_loop`, 2.1 s, a gas jet's
  rough hiss over a low roar and crackles, looped while it's held (sound.tide's
  `Ears.flame`, and `othersFlame` for the last other heard, where they are; they stop
  0.15 s after the last puff and while the run's held for a pick). A kill of yours in
  it gives the kill's knock, quieter. CREDITS.md and manifest.json got their rows
  by hand (the tool rewrites CREDITS.md's names as `.wav`).
- **Measured, shorter** (the bot fires within 6.5 m and goes to the night's boss
  till 5.5 m off): the Engineer's middle run about 630 s, five of sixteen dying in
  the first night's end, in the Mauler's slam (6.5 m: the flames now need you
  inside it, and the bots don't roll); fights 27 to 32 s. A player rolls; if the
  Engineer's too weak at a boss, the lever is the puff's 13.
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16; the bot
  fires within 8.5 m and goes to the night's boss till 6 m off): at 11 a puff the
  Engineer's middle run 860 s and the night's end fights 28 to 29 s (the SMG's 972
  and 22 to 26); at 13 (kept), 1147 and 1182 s over the two halves, fights 28 and
  29 s, against the Gunner's 990 and the Breacher's 849: within the runs' spread
  (150 to 1460 s a seed). The fights are longer: flames don't find a weak spot,
  and a boss has to be stood near.
- **Tests**: the Engineer's checks hold the flamethrower; new, held half a second
  it burns the two in its cone (3 m ahead, 5 m ahead 1.5 m aside: 493 and 314) and
  sets both alight, not one 10 m off nor one behind, six puffs of its tank. The
  check that one bled out is back beside a friend a minute later allows 6 m (they're
  set 2.4 m from where the friend stood, and the run's changed path had the friend
  walk on to 4.2 m). Native debug `gravewake_test` and the web debug WASI one pass.
- **Validation**: looked at in standard-host captures: the jet into the line-up
  (Shift+F2), the three in it burning, their numbers growing; the model in Blender
  from the side and three quarters. Not heard: the roar's loudness is my guess.
- **Gaps**: the flamethrower's own items: nothing uses its being a flame (a wider
  cone, a longer one); the bots don't roll, so they stand in the dead it doesn't stop.

### Hits you feel, the dead harder to kill (2026-10-20)

The owner: "i want you to polish the experience; VFX, SFX, balancing etc. ...
some VFX/SFX for when you hit something, like a hitmarker thing / make enemies
harder to kill, right now its 1 or 2 bullets, maybe 3-4 is a better spot? with
the occasional harder hitting headshot". What was made of it and every number
are mine, Provisional.

- **Why the old marker didn't read**: it was there, small, but every hit of a
  survivor's showed it and ticked, the drones', saw blades', Tesla bolts' and
  the rest's too, so with a few items it was lit all the time and a round of
  your own didn't stand out. `Wound.aimed` (and `Struck.aimed`) now says a hit
  came from the survivor's own hand: a round through `Shoot` (the gun, the phase
  round; `Land`'s new last argument, false for drones and turrets) and the boot,
  stamp, leap and blink. Only those mark, number, tick and flash hard. `Struck`
  also carries `damage`, what it took.
- **The marker** (hud.tide's `ShowMarker`, `Overlay`): four strokes round the
  cursor on a dark edge, popped out 1.6 times as big as the hit lands and
  settling in 70 ms; white a hit, gold the head, red a kill (and a ring thrown
  out from it), orange a weak spot, yellow a crit, small grey off mail. A
  shotgun's pellets in one tick are one mark, the best of them.
- **Numbers** (hud.tide's `Tally`, `Tallies`, `TALLIES_MOST` 48, `TALLY_LIFE`
  0.8 s): each hit of your own hand puts what it did over the body, in bold
  with a dark edge all round, popping in 1.9 times as big, rising and drifting
  aside, fading over its last quarter second; in the marker's colours (white,
  gold, orange, red for a crit, grey off mail), bigger for a head, a weak spot, a
  crit or a kill. Pellets landing on one body in the same frame add up into one
  number (a first try merged within 0.1 s, which ran the carbine's rounds 0.09 s
  apart together). Drawn under the panels, held still during a pick (`Me.dt`).
- **The dead's flash**: a hit of your own hand whitens the body 0.9 (0.35 for
  the rest, as all were), gone in a fifth of a second.
- **Sounds** (sfx/tools/make_sfx.py, synthesized, no recordings): `hit_tick`
  (70 ms, two inharmonic partials over a snap of noise and a small knock),
  `hit_kill` (the tick twice and lower, a thick knock and a short ring) and
  `hit_head` (a struck plate's ding laid over the skull's crack, low for a weak
  spot). Played flat (not placed) for your own hand's hits only, one of each a
  frame (sound.tide's `Ears.ticked`, `knocked`, `dinged`, cleared by `Listen`),
  so a shotgun blast is one. The Kenney click (`hitmarker`) is now only the mail's.
  The tool rewrote CREDITS.md's file names as `.wav` (the ones it writes before
  `to_ogg.sh`); that was put back and the three rows added by hand.
- **A headshot kill jolts the camera** a little (eye.tide's `FeelStruck`, 0.18).
- **Harder to kill** (rounds.tide's `ZOMBIE_HEALTH` 67, was 25; weapons.tide's
  `WeaponInfo.head` 3, was 2): a carbine round is 25, so one of the ordinary
  dead takes three to the body at the start and one to the head. Measured over a
  run (`gravewake_balance`'s new line, the toughest ordinary one up against the
  first survivor's might): 2.4 to 2.9 rounds at 2 and 5 minutes, 3.0 to 3.7 at 10,
  4.5 to 6.8 at 15 (it had been 0.9 to 1.1, 1.4 and 2.1 to 2.3). The shotgun's
  pellets keep 1.5 on the head.
- **What came with it** (all measured against the same build's numbers):
  - Fewer of the dead, each worth more: `SPAWN_RATE` 2.6 a danger (4) and
    `ORB_WORTH` 0.75 (0.5). At four a second the carbine (some 2.5 kills a
    second now) was overrun in the first minute; the levels come about when
    they did (L17 near 395 s either way).
  - What fights by itself and the skills twice as hard, so they don't go limp
    (to the dead they're three quarters of what they were): a drone's 28, a
    turret's 20, a saw blade's 44, Tesla 90, the Thunder Drum 120, a rocket 220,
    the ember ring 10 a stack, a Mortar Flare's shell 200, the boot 260 and its
    stamp 480, a leap 300, a blink's burst 180, C4 640, a frag 360, a firebomb
    180, a phase round 200; burning 80 and bleeding 60 a bite besides their share
    of the dead's health.
  - The carbine fires every 0.09 s (0.1): with three rounds a kill the Gunner
    had lost most.
  - `BOSS_HEALTH` 9500 (8000): the doubled items had cut the night's end fights
    by a fifth.
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16 each, the
  middle run; `test/balance.c` now also prints the rounds above and the night's
  end fights, from its clock running out to dawn): before, the Gunner 1084 s,
  the Breacher 603, the Engineer 920, fights 16 to 31 s; after, the Gunner 990,
  the Breacher 849, the Engineer 972, fights 19 to 26 s; 13 or 14 of each 16
  going down. The first pass (no quicker carbine, bosses at 8000): the Gunner
  608, the Breacher 701, the Engineer 790 over seeds 1 to 8. So about as hard
  as before, and the Breacher, which had been far behind, with the others: its
  boot and pellets lost least. Kills a second fell by about half (14 to 17 from
  14 to 32). The levers: `ZOMBIE_HEALTH`, `SPAWN_RATE`, `ORB_WORTH`, the head's 3.
- **Tests**: unchanged and passing (their checks are of ratios and kills); the
  numbers they print moved with the doubled skills (a C4 charge 1184 to the one
  at it, a Rocket Pod 998 to one 10 m off, a phase round rank III 588 to each).
  Native debug `gravewake_test` and the web debug WASI one pass; the web debug
  page builds. The browser tests weren't run (the owner asked not to).
- **Validation**: looked at in standard-host captures: the dead lined up (Shift+F2)
  and shot, the white flash, the marker and a 25 over a body hit, a gold 75 over
  a Bloater's head; a horde (F4) shot into, the red kill marker and its ring over
  the crowd. Not heard: the sounds' loudness against the guns is my guess.

### No Gas Mask: the Bile Bomb (2026-10-20)

The owner: "there is also an ability that makes bloaters not hurt me anymore.
that shouldnt be a thing, it removes way too much danger."

- **The Gas Mask is gone** (zombies.tide's `Hunt`, `Rupture`; items.tide's
  `Item.BileBomb`): a Bloater's burst hurts every survivor in it again. Its place
  among the uncommon items is the Bile Bomb, all of it what it had beside the
  mask: the Bloaters you kill burst 30% wider on the dead (as it was) and half
  again as hard (new), more each. Mine, Provisional.
- **Tests**: the Gas Mask's check (a burst beside its wearer not hurting them) is
  now the Bile Bomb's: a Bloater its holder shoots dead bursts on one of the dead
  4.2 m from it, past a burst's 3.6 m, for 114. Native debug `gravewake_test` and
  the web debug WASI one pass.

### The floor gone black and speckled: the stains read by their colour (2026-10-20)

The owner, with a picture of the clearing's floor black, flecked all over with
red and a pink glint: "sometimes the floor looks like this, idk why but it
seems like its not rendering or something?"

- **Why**: the stains' drying (gore.tide's `Dry`, every 2 s over the whole
  clearing) draws black with a little alpha, meant to fade them. But an alpha
  blend (the engine's, premultiplied, as the spec has it) darkens what's there
  and raises its alpha, `a + d * (1 - a)`: it can't lower one. So every texel of
  the clearing went toward black at full alpha, blood or none, and the floor
  (look.tide's `Earth`), reading how much blood there was by the stains' alpha,
  laid black over all of it, and since the last round the wet flecks and the
  glint on it too. A few minutes after the last wipe (a run's start, the next
  night) it was all of it: long in the menu, or a long night. (Before the
  flecks it had been a floor going slowly darker.)
- **The fix** (look.tide's `Earth`): how much blood is there is how much colour's
  left (the sum of the stains' red, green and blue, over their alpha, which `Sample`
  gives), times their alpha's share as before: fresh blood full, dried blood (its
  colour darkened by the drying) fading away, a texel never bled on nothing
  whatever its alpha. The drying is as it was, and so dries as it was meant to.
- **Validation**: a standard-host capture after 150 s in the menu: before, the
  owner's picture (black, flecked); after, the floor as it should be. Fresh blood
  looks as it did. Native debug `gravewake_test` passes.

### A wider shotgun, crisp effects, shining blood cleared at dawn, a side kick, C4 (2026-10-20)

The owner: "i think the shotgun shots should spread more"; "the saw VFX look
very blurry again, i want effects to not look blury, think almost minecraft
style except a bit more shape to things"; "the blood could shine more, it
looks fun but it quickly fades away since now everything is red haha, we
should clear blood between nights i think"; "the "boot" ability feels weak, we
might want to clearly depict what its hitting and make an animation that makes
it clear, maybe its a sideways kick for example"; "the "mine" one is also
boring, its so small, you cant even see it being places ... it also explodes on
the first enemy which means it misses its highest potential. i wonder if it
should be remote C4 or something, you place it then you choose to explode it".
The numbers and shapes are mine, Provisional.

- **The shotgun** (weapons.tide): its spread 0.13 radians (0.065), and wider, it
  was more a spray than a slug, so fourteen pellets of 26 (eleven of 24), a
  pellet's items a quarter as likely as a round's (0.3).
- **Crisp effects** (fx.tide's `GlowMote`; look.tide's `Develop`; held.tide's
  `DrawSaws`, `Finish.light`): every glowing mote a chunky block, a square with
  its corners eased, solid with a dim edge and nothing past it (they'd been soft
  rounds in three steps); the bloom added in tenths, so a halo is a ring or two of
  light, not a haze. The saw blades lost their soft gleam: their steel lights
  itself (`Finish.light`, a material's own light, new), and behind each blade four
  solid slivers of light along its circle, dimmer each, as pixel art draws a swing.
- **Blood** (look.tide's `Earth`; gore.tide's `Soak`, `WipeNightGore`): the moon's
  glint off it a tight crimson one (a first try, broad, turned the pools pink), and
  hard flecks of wet on it, bright red, a few near white, where it's fresh. At dawn
  it dries eight times as often and three times as much each time, most of it gone
  in the dawn's ten seconds, and when the next night falls the ground's wiped
  clean.
- **The boot** (people.tide's `KickOut`; kits.tide's `Kick`; fx.tide's `Flourish`,
  `Bleed`; eye.tide's `FeelKick`): a side kick, the body turned side on to the
  cursor (the right leg's side, whichever side of the hips the rig has it), the
  knee drawn up, the leg driven out level, the body leaning away; a sweep of pale
  blocks of light through the whole of where it reaches, its edge a hard line;
  each of the dead it strikes a white flash and chips of light; the screen jolts
  when it lands. Harder: 130 a kick (80) and 240 a stamp (160), each thrown back at
  12 m/s (7), knocking over what it's thrown into.
- **C4** (kits.tide's `UseSecondary`; missiles.tide's `Fly`, `C4_...`;
  `Survivor.charges`, `detonations`, `Missile.fuse`; held.tide's `DrawMissile`;
  hud.tide's `ChargeRings`): the Mine became a C4 charge, set down 1.2 m the cursor's
  way (two, three side by side a rank), waiting: right mouse again blows them
  (the slot says BLOW), 320 at the heart in 5 m, the dead in it thrown down; the
  cooldown after the blowing. Left 90 s it goes off by itself. A block of olive
  bound in dark tape, a box of wires and a red light blinking on it, lit a little
  by itself so it's seen at night; round each of yours a throbbing red ring as wide
  as it'll reach. The Engineer bot sets one as they come and blows it with four in
  its reach (or after 4 s).
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16): the
  Engineer's middle run 996 s (966 before); the Breacher's 396 with the wider
  spread alone, 640 with the fourteen pellets (588 before).
- **Tests**: a C4 charge waits two seconds with one of the dead at it, and blown,
  does 592 to it. Native debug `gravewake_test` and (below) the web debug one pass.
- **Validation**: standard-host captures of the saw blades (crisp blades and
  slivers), the side kick's sweep, the C4 (set down, its ring, blown), the blood
  (glistening; at dawn mostly dry; the next night clean). Ticks of 2000 of the
  dead 2.3 ms.

### A round stops in the first it strikes (2026-10-20)

The owner: "bulelts seem to travel across zombies and damage all; this is a
satisfying but it makes that "penetrates 1 more zombie" useless and the
starting weapon too powerful for hordes".

- **Pierce 1, every gun** (weapons.tide's `WeaponInfo.pierce`): the carbine's
  rounds went through three of the dead and the shotgun's eleven pellets four
  each; now every gun's round stops in the first it strikes, and AP Rounds take
  it on into one more each (now doubling what a round strikes, not a third more).
  The phase round still goes through a whole line, and Ricochet Rounds and HE
  Rounds reach the others. (The SMG's were one already.)
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16): the
  Engineer's runs the same, to the second (its SMG unchanged: the seeds hold);
  the Breacher's middle run 588 s from 868, 14 of 16 going down, its shotgun no
  longer clearing a crowd; the Gunner's 1248 s from 740, which isn't what one
  would expect of a weaker carbine, and is likely how a run that gets through its
  first nights snowballs and one that doesn't is over (its kills a second about
  the same). The Breacher is where to give back if it's too hard: its pellets'
  damage, or a pierce of 2 for the shotgun alone.
- **Tests**: the carbine's round stops in the first of four in a row, and with an
  AP Round strikes two. Native debug `gravewake_test` passes.

### More cards, upgrades that show, a right mouse skill to change (2026-10-20)

The owner: "let's work on upgrades. I wonder if the right click ability should
start empty or at least be swappable for others? not sure. at the very least
we need more cards and we also need to make them feel powerful, not just
functionally but visually too. like the drone, if we upgrade it maybe we just
get 2 drones." What was made of it, and the numbers, are mine, Provisional.

- **The right mouse skill, swappable** (items.tide's `Item.Card...`, `CardSkill`,
  `RollCard`, `Take`; kits.tide's `SecondaryOf`, `RankLine`, `UseSecondary`;
  `Survivor.secondary`, `secondaryRank`): it starts as the kit's (starting with
  none would leave the first minutes with only the gun), and in three level
  offers of ten a skill card (orange) takes the last place: another of five
  (Phase Round, Boot, Mine, and the Leap and the Blink, given to nobody since the
  kits were made) in place of yours, ready at once, or yours a rank stronger, to
  rank III, each rank 15% sooner back and showing: a phase round three rounds in
  a fan, then five, a quarter, then half again as hard; the boot further, wider,
  then all round, 30% then 60% harder, at more; a mine two, then three, side by
  side; a leap a quarter, then half again as wide where it lands, harder, a wider
  ring of earth thrown up; a blink a burst of violet light throwing down the dead
  where it leaves, then where it lands too. Pips under the skill show the rank.
  A leap now carries its way (the air had slowed it to 2 m: 5.4 now); a blink
  goes to the cursor, as far as 8 m (+2 a rank).
- **Seven more cards** (items.tide; ITEM_COUNT 30): common, Ricochet Rounds (a
  hit bounces on to one more of the dead within 7 m for 60%, +1 each: a hot
  streak and sparks) and Bone Splinters (a kill throws 3 shards of bone, +2 each,
  into the nearest dead within 5 m, each 45% of one of the dead's health: pale
  streaks); uncommon, Saw Blades (a blade each circling 2.3 m out, waist high,
  cutting what's within 0.75 m of it every 0.12 s for 22: two crossed squares of
  steel spinning, a gleam, a trail behind and sparks off its edge), Tesla Coil
  (every 1.2 s lightning from over your head into the 2 nearest dead within 9 m,
  +1 each, 45 each) and Molotov Satchel (a firebomb at the thickest of the dead
  every 6 s, 20% sooner each); rare, Thunder Drum (every 5 s, 10% sooner each, a
  shockwave throws down the dead within 4 m, +1 m each, for 60: a flash, a ring
  of pale light rushing out, dust) and Rocket Pod (every 3 s, 2 rockets, +1 each,
  over your shoulders at the nearest dead within 22 m, 110 each in 2.6 m: a motor's
  flame and smoke behind them). Each bites by itself, as the drone and the ember
  ring do, and sets off nothing more.
- **Upgrades that show**: Gun Drones are a drone each (to six; past that they fire
  faster), in a ring over your head going slowly round, firing in turn, each a red
  light under it; the Ember Ring a ring of flame at its edge, wider and thicker each
  (it had been a few embers inside it); Saw Blades, Tesla bolts and rockets one
  more each. A card says what you'll have with it ("Tesla Coil III: the 4 nearest
  dead", items.tide's `ItemLineAt`), its level in Roman numerals. Taking one throws
  up a column of light in its colour round you, a ring of it rushing out over the
  ground and a flash, more for rarer ones (fx.tide's `Anoint`).
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 16; the bots
  pass over skill cards, using any but their kit's badly): the middle runs the
  Gunner 740 s (732 before), the Engineer 966 (732), the Breacher 868 (612); most
  still go down. Taking skill cards at random, as they first did, the Gunner and
  the Engineer had done worse (a blink never used, a leap into the thick of them).
  Ticks of 2000 of the dead 1.93 ms, of 4000 4.2 ms.
- **Development**: Shift+F10 gives the items that fight by themselves, several
  each (four drones, three saw blades, two Tesla Coils and Ember Rings, a Rocket
  Pod, a Thunder Drum).
- **Tests**: three Gun Drones each fire in turn; a Saw Blade cuts one of the dead
  on its circle (1.2 s); a Tesla Coil strikes the two nearest; a Thunder Drum
  throws one 3 m off down; a Rocket Pod has 2 rockets in the air and hurts one
  10 m off; Ricochet Rounds hurt the one beside the one shot; Bone Splinters the
  one beside one shot dead; a Molotov Satchel throws; a skill card makes the right
  mouse a leap, another a leap rank II, which carries 5.4 m and knocks down the one
  it lands by; a phase round rank III strikes three of the dead across its fan; a
  blink rank II goes 7 m, the one by where it left thrown down. A skill card comes
  only in an offer's last place. (Two older checks made sure of themselves: a
  survivor made another kit has its kit's skill, and the ground's burst keeps a
  spare of the dead well away for the burning after.)
- **Validation**: native debug `gravewake_test` and the web debug WASI one pass.
  Looked at in standard-host captures: a Breacher with the Shift+F10 items in a
  horde (the drones, the blades, the ring of fire, bolts, a rocket's burst), and
  eight offers in a row (a skill card in place of the Boot, Tesla Coil III, Saw
  Blades IV).

### Each kind its own body (2026-10-19)

The owner: "also lets give each different enemy type a unique look too; its
easy to read now with the colors, this is just from an art and polish
prespective". The shapes are mine, Provisional.

- **Six bodies** (art/kinds, by art/kinds/tools/make_kinds.py: the bosses' way of
  growing a body from one of the dead of art/people, with the horde's clips):
  a Runner lean and hunched, long thin limbs and longer claws, its head thrust
  out, bone through its spine and elbows; a Bloater with a vast sac of a belly
  hanging out front, pustules glowing on it, fat limbs; a Spitter hunched, its
  throat a great glowing sac of bile; a Warden squared up, iron on its shoulders
  and a shield on its back, its head bare (a helmet was tried, and taken off: its
  bare head is where it's to be shot); a Brute with a bull's shoulders and
  forearms, a little sunk head, short heavy legs, muscle heaped on its back and
  bone through its shoulders; a Howler stretched thin and tall, its long neck
  thrown back, a frill of spines round its throat, its throat and lungs glowing.
  Each about 1100 triangles, drawn in one more instanced draw a kind (crowd.tide's
  `Kinds`, `KindMesh`, `ShambleKind`); `ShowFrame` and `ShowState` take the set of
  clips they play from. The scaling by kind that had stood in for shapes (a
  Spitter's 0.9 wide, and so on) is gone but the Brute's third bigger; the
  shader's Spitter throat glow is gone too (its body's sac glows). The growths
  that glow burn brighter as a kind winds up (a Spitter's throat, a Runner's
  crouch, a Howler's howl).
- **Their colours kept, their shapes showing**: the kind's colour washes a third
  through its body (it was a half), its glow all over a little less, and its
  outline in the colour is a crisp edge, not a soft one (seen from above, most of a
  body is turned from the eye, and the soft edge had washed each shape flat in its
  colour). Looked at zoomed out over a crowd, every kind's still known at a glance
  by its colour; nearer, by its shape too.
- **Hit spots** (zombies.tide's `KindHead`, `KindChest`, `KindHips`): where each
  kind's head, chest and hips are, standing, moved by its body's marks
  (kinds.json) less those of a body make_kinds.py left as it was: a Howler's head
  0.46 m higher, a Brute's sunk, a Runner's and a Spitter's low and forward. A
  hunched one's head is near its chest, so a round at its chest is a head shot
  more often.
- **Development**: Shift+F2 lines up one of each kind and one of the ordinary
  dead 3.5 m in front of you, dazed half a minute and fifty times as tough, your
  skills held off as long, to look at them by.
- **Validation**: native debug `gravewake_test` and the web debug WASI one pass
  (the Warden's mail check now lets the kind's body settle before it aims: it had
  aimed at the Spitter's chest, which is by its head now). Looked at in Blender
  (each body front, side and back) and in standard-host captures (the line up
  zoomed in; Runners at the default zoom; a crowd of all the kinds zoomed out).
  The kinds' texture of numbers is 2.8 MB, as the horde's and the mutants' are
  (1.9 and 2.0): the page about 3 MB more. Ticks of 2000 of the dead 2.06 ms.

### Bosses that last, survivors who can die (2026-10-19)

The owner: "i killed the boss way too quickly with the shotgun; its still
easy", then "oh its still ahrd to die". The numbers are mine, Provisional.

- **Bosses that last** (boss.tide's `BOSS_HEALTH`, `BOSS_PELLET`; hands.tide's
  `Shoot`): a boss's health is 8000 a danger (times the party, times its kind's
  share; it was 2000), and a shotgun pellet does half to one: every one of the
  eleven finds something that big, so the shotgun had done twice what the carbine
  and the SMG do to a boss (some 480 a second against their 230 to 250, before
  items).
- **Survivors who can die** (items.tide's `LEVEL_HEALTH`): a level adds 18
  health, and brings 18 back (it was 30). By the last levels a survivor had had
  eleven times the health they began with against claws only three times as hard
  (rounds.tide's `Harm`), so the dead couldn't touch them; now it's about six times.
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 8; the night's
  end's fight, from the clock running out to the boss down): the last round's
  build felled each boss 8 to 13 s after it rose (the middle fight) and the
  middle runs went 1460 s (the Gunner, at the cap), 900 (the Engineer), 1196 (the
  Breacher). Tried: a level's 12 and bosses' 8000: fights 19 to 27 s, the runs
  623, 396 and 572 s, every one but three going down, most in a night's end (too
  far); 15 and 6000: fights 17 to 23 s, runs 826, 433, 638; 18 and 8000 (kept):
  fights 19 to 32 s, runs 1462 (the Gunner, three of eight going down), 724 (the
  Engineer, seven going down) and 580 (the Breacher, all eight). The bots don't
  roll a boss's slam or keep off it as a player does; they've always done worse
  than the owner, who found the last round's easy.
- **Tests**: as before (the survivors' health a level, 18, in the test's
  `max_health`). Native debug `gravewake_test` and the web debug WASI one pass.
  Ticks of 2000 of the dead 2.5 ms.

### Runners, Brutes and Howlers, each kind its own colour, the orbs as gems (2026-10-19)

The owner: "the XP orbs are hard to spot now", then "we need more variety of
enemies and also an easy way to distinguish them, i imagine we might want to
color code enemies for a high contrast". The kinds, their numbers and colours
are mine, Provisional.

- **Three more kinds** (zombies.tide's `ZombieKind`, `Hunt`, `TakeWound`;
  rounds.tide's `KindFor`): a **Runner** (from 25 s; 0.6 as tough, always
  sprinting and 15% quicker than a sprinter): from 2.2 to 5.5 m off with
  nothing between, it crouches 0.45 s facing whom it's after, then leaps the way
  it faces at 13 m/s for 0.35 s (4.5 m) and strikes as it lands (0.8 of a claw;
  a roll's guard turns it), every 3 s at most. A **Brute** (from 2:30; a third
  bigger all round, its spots to hit and its reach too, eight times as tough, a
  shambler 1.35 times as quick): no blow staggers it or throws it down, a blow's
  push moves it a quarter as far, and its own does 2.5 claws and throws a survivor
  back at 9 m/s (not one rolling). A **Howler** (from 3:40; tall, thin, 1.5 as
  tough): it stands back 3 to 11 m off; within 22 m of whom it's after, every 7 s
  it stands and howls 1.1 s, and at the howl's end every one of the dead within
  10 m of it is frenzied 5 s (the Stalker's frenzy: 1.6 times as quick). Shares:
  Runners one in 33 up to one in 12, Brutes one in 200 up to one in 50, Howlers
  one in 125 up to one in 40, as the danger climbs. Their orbs are worth 1.5, 6
  and 3 of the ordinary dead's. Heard: a Runner's leap a high snarl, a Brute's
  swing a low bellow and its blow a heavy thud, a Howler's howl a scream pitched
  down. F2 now goes through all six kinds.
- **Each kind its own colour** (crowd.tide's `KindMark`, `CrowdFlesh`): the
  ordinary dead grey as they were; every other kind its body washed half with its
  colour, a glow of it all over, its eyes burning it, and a hard edge of it round
  its outline in place of the pale one: red a Runner, lime a Bloater, yellow a
  Spitter, blue a Warden, orange a Brute, magenta a Howler. A Runner crouched or
  a Howler howling burns brighter, its colour streaming up off its head (fx.tide's
  `Winding`); a howl throws a ring of magenta out over the ground as far as it
  reaches (`ShowHowl`), and the frenzied throb a magenta edge. Looked at zoomed
  out over a crowd, every kind's known at a glance.
- **The orbs as gems** (orbs.tide's `ShowOrb`; fx.tide's `OrbGem`): they'd been
  small soft glows, and the hard steps of light (the last round) cut them to
  specks. Now a cyan diamond 0.44 to 0.64 m tall (more for more), turning, lit on
  one face and shaded on the other, a glint and a dark rim, solid and hard-edged,
  drawn over the murk, with a glint off it now and then. Cyan is none of the
  kinds' colours. (Finding them in the captures took a while: in the first ones
  every orb lay south of the survivor, under the screen's bottom edge, and the
  ones killed on the screen were within ORB_REACH and gone to them at once.)
- **Measured** (`gravewake_balance`, 25 minutes at most, seeds 1 to 12): the
  Gunner at the cap (1460 s, the last round's 1257 and 1459 over seeds 1 to 4
  and 5 to 12); the Engineer's middle run 1118 s and the Breacher's 977, half of
  each or more going down (the last round's at the cap, 1059 over 5 to 12 for the
  Breacher): the new kinds come close, which the shotgun and the SMG are for, and
  the bots don't roll a Runner's leap or keep off a Brute. Ticks of 2000 of the
  dead 2.2 to 2.4 ms (1.93 the last round, one run), of 4000 4.6 ms.
- **Tests**: a Runner 4.5 m off crouches, leaps and lands its blow; a Brute
  kicked stays on its feet and its blow throws the survivor back at 9 m/s; a
  Howler 8 m off stands, howls, and the three of the dead behind it are frenzied.
  The check of the dead never getting stuck leaves the Howlers out, as it does the
  Spitters (they hang back on purpose).
- **Validation**: native debug `gravewake_test` and the web debug WASI one pass.
  Looked at in standard-host captures: all six kinds coming at a Breacher zoomed
  out (each colour plain), the gems where the dead fell. (A build to look at them
  in, with the dead's harm made nothing, drew none of the dead at all; not looked
  into, as it was thrown away: noted in case it's the engine's.)

### Torn apart at once, pixelled light, missions you can see (2026-10-19)

The owner, after playing: "when the zombies die i think they should just
explode, them laying for a while makes it hard to know where I can walk or
not"; "some of the VFX are very smooth looking, i would like to give them a
more pixelated look"; "and the missions, would be nice to visually see in the
ring the progress vs just a HUD thing"; "i also missed a mission, probably my
fault but i just didnt realize it was active". How each was answered, and the
numbers, are mine, Provisional.

- **Torn apart** (zombies.tide's `Zombie.torn`, `Hunt`; crowd.tide's
  `GatherZombie`; gore.tide's `Spill`, `Fling`, `Gore.torn`; fx.tide's `Bleed`,
  `Fx.kills`): a kill no longer leaves a body lying 4.5 s. It's not drawn from
  the frame it dies and gone the next tick, and in its place its trunk, two arms,
  two legs and head fly (a head shot's head furthest), with 4 or 5 lumps and ropes
  of gut (8 in a burst), the blood as before. Burned to death, it falls apart in
  embers, ash and charred, bloodless pieces with a scorch under them (it had
  just fallen). Many kills at once throw fewer lumps and less blood each, so the
  fight stays in sight and the motes' lists don't fill. The pieces lie 12 s (22
  before; 1500 at most, 1000 before), and drip less. Kept: a Bloater killed still
  falls and swells a second under its red ring before it bursts (it's the one
  thing on the ground to keep off), and at dawn the dead still burn and fall.
- **Pixelled light** (look.tide's `Develop`, `Earth`; fx.tide's `GlowMote`,
  `StreakLight`; hud.tide's `GroundRing`): the bloom is read once for each of the
  picture's big pixels, not smoothly across them, so a glow's halo is in pixels
  too; a glowing mote's light is in three hard rings (bright middle, nothing past
  the outer) and a streak's in two, not a smooth falloff; a fire's embers on the
  ground in four steps. The rings drawn over the picture (a survivor's, a
  Bloater's, a fire's) are squares on the picture's pixels, not smooth lines.
  Lamps' light on the ground is still smooth (banding it would change the whole
  look; the owner's call).
- **The mission's circle shows what's done** (hazards.tide's `Hazards.mission`;
  look.tide's `Earth`): the circle burns in the ground, a band at its inside
  edge in as many pieces as there are to do (12 for a hold's seconds, 25 or more
  for a cull's dead, 5 for the wisps, round the place they're scattered, 19.5 m),
  each lit gold as it's done, clockwise from the north, the rest a dim brown;
  with 15 s left the unlit ones throb red. Its pointer says what's done (HOLD
  7/12 s), over the circle's far edge from inside it. The gold motes off the
  circle, a few now. (A first try was too bright: the lit pieces washed out
  near white, and the unlit ones near as bright.)
- **Missions you notice** (missions.tide's `MISSION_NEAR`, `MISSION_FAR`,
  `MISSION_HURRY`, `MISSION_NEW`, `MissionHurry`; hud.tide's `Pointer`): one
  begins 14 to 28 m off (22 to 40 before: always off the screen), with a louder
  horn, its banner up 4 s (OPTIONAL MISSION: ...), and its pointer half again as
  big and throbbing fast its first 6 s; with 15 s left, not done, it's told
  again (a banner and a horn) and its pointer stays a little big. A banner's
  fade in is now from when it changed (it had assumed 2.6 s).
- **Measured** (`gravewake_balance`, 25 minutes at most), against the last round's build on the same seeds, within the noise either
  way: the Gunner's middle run 1459 s over seeds 5 to 12 (that build's 1460),
  1257 over 1 to 4 (1463); the Breacher's 1059 over 5 to 12, five or more of
  the eight going down (1461), and at the cap over 1 to 4 (1308); the Engineer
  at the cap over 1 to 4 and 5 to 12 (that build's 1182 over 1 to 8). A run
  still goes down in its first few nights or snowballs to the cap. Nothing in
  the match counted the bodies lying (the dead alive are counted without
  them), so only the chances moved. Ticks of 2000 of the dead 1.93 ms, of 4000
  3.99 ms.
- **Tests**: a kill leaves nothing (gone 4 ticks after the shot); the checks of
  a kill take one gone as killed. The ground's burst check holds the party's
  experience still and lets a level due land first (a level's pick holds the
  match, and it makes the survivors whole: the run's chances had moved).
- **Validation**: native debug `gravewake_test` and the web debug WASI one pass.
  Looked at in standard-host captures: a horde shot into (no bodies left, pieces
  and blood flying), a fire before and after (its halo and motes in hard steps),
  the hold's circle 7 of 12 lit, and its big pointer as it begins.

### A Bloater's fuse shown, wisps for the hunt, a walking turret (2026-10-19)

The owner, after playing: "the explody zombies should only display that
radius when they are going to explode and it shouldnt be instant, they should
have some animation and the circle should show where it is about to explode.
right now it shows all the time and the explosion is instant"; "one of the
missions was killing a scaled zombie with some weird crown of fire; that felt
awkward"; "the enginner ability (turret) feels weird since it doesnt move
with me or anything i just leave it and forget about it since im moving away.
it also seems pretty OP". How each was answered, and the numbers, are mine,
Provisional.

- **A Bloater's fuse** (zombies.tide's `BLOAT_FUSE`, `BLOAT_DEAD_FUSE`; hud.tide's
  `BloaterRings`; hazards.tide's `Hazards.fuses`; look.tide's `Earth`; crowd.tide's
  `GatherZombie`; fx.tide's `Swelling`): nothing's drawn round one till it swells.
  Near a survivor it stops and swells for 1.4 s (it was 0.9); killed, it no longer
  bursts at once but falls and swells, bursting a second later (a chain of them
  ripples out). As it swells: it shudders, its belly and boils swell and glow,
  foul gas hisses out of it faster and faster, its light goes from sick green to
  red; the ground its burst will reach glows a dull red, a hard red ring at its
  edge (drawn over the dead too) and a second growing out from under it to meet
  the first as it bursts. Red, not the missions' gold (a first try, sick green to
  red and bright, read as a gold mission circle). Its burst hurts survivors within
  3.6 m across the ground (it was measured from a metre up, so a lying one's had
  reached a little less than its ring).
- **Wisps in place of the hunt** (missions.tide's `MissionKind.Wisps`,
  `ScatterWisps`, `WispAt`, `WispCaught`; `Match.wisps`): the hunt's elite (a
  Warden ten times as tough, gold motes circling its head) is gone. In its place:
  five wisps, gold flames bobbing waist high, each lighting the ground round it,
  scattered 6 to 18 m round a place (one to each fifth of the way round), on open
  ground the dead's way reaches, out of the fires; walk within 1.6 m of one to
  catch it (a burst of sparks, a chime), all five in 45 s. The nearest is pointed
  to (WISP). A mission that's about roaming, as the owner wants the game to be.
- **A walking turret** (kits.tide's `Sentry`, `TURRET_HEEL`, `TURRET_SPEED`,
  `TURRET_LOST`, `TURRET_HURT`, `TURRET_RATE`; held.tide's `TurretFeet`): after
  Risk of Rain 2's Engineer's walking turrets, it scuttles after its maker,
  hopping, keeping 2.2 m off (the farther behind, the quicker, to 8.5 m/s: a run's
  7, so it keeps up; 25 m off, stuck behind something, it's at their heel at
  once), and fires as it goes, facing the way it walks with nothing to shoot.
  Weaker: 12 s in place of 15 (on an 18 s cooldown), a round of 10 every 0.14 s in
  place of 11 every 0.09: 71 a second in place of 122, about 47 on the average in
  place of 102.
- **Measured** (`gravewake_balance`, 25 minutes at most): the Engineer, 8 runs,
  five or more went down before it; the middle run 1182 s (the last round's 1152 of 4); the Gunner
  at the cap (1463), the Breacher 1308 (1204). Within the noise: the bots stand
  near the middle, where the turret had stood by them anyway, and it follows them
  now. Ticks of 2000 of the dead the same as before the change, measured side by
  side (2.3 to 2.8 ms; the machine's slower today than the last round's 1.81).
- **Tests**: a Bloater swells 1.40 s before it bursts; shot dead from 8 m it's
  still there, dying and swelling, 3 ticks after, and bursts 1.03 s after; the
  turret walks 7.8 m after its maker, to 2.2 m from them; the wisps, 5 of them 8.9
  to 17.3 m from their place, caught one at a time, all caught, done.
- **Validation**: native debug `gravewake_test` and the web debug WASI one pass.
  Looked at in standard-host captures: a Bloater shot down by the turret, lying
  and swelling, the red ground and its two rings; a wisp; the turret trailing
  its Engineer as they walk. (The archetypes' numbers moved with the elite's
  `Spawn` gone: the Orb's 4 and the Zombie's 5 now.)

### A stand at the night's end: the wake-fire's ring, quicker dead (2026-10-19)

The owner, of the last round's bots never going down: "I haven't had the
chance to test but I'm not sure difficulty being lower is better". The
levers, and how each was measured, are mine, Provisional.

- **The wake-fire's ring** (nights.tide's `Reckon`, `RING_REACH`, `InRing`,
  `RingFlames`, `RingRises`; `Match.ringAt`, `Match.ringReach`; hands.tide's
  `Steer`; look.tide's `Earth`): as the night's clock runs out a ring of pale
  wake-fire 17 m round rises about the survivors standing (its middle
  theirs), and the night's bosses claw up in it, 8 m from its middle to 4 m
  short of its fire. The living can't cross it till dawn (their feet held
  inside it, a body's width short); the dead walk straight through. So the
  night's end is a stand, as a Dark Souls boss's arena is, not a chase: the
  beacon's vigil had been where the bots died, and with nowhere to hold
  they'd never gone down. Whoever bled out is back by its middle. Drawn: its
  line burning pale in the ground, a flame standing every 1.5 m of it (the
  nearest), embers off them, a burst of them as it rises; the night's panel
  says the wake-fire holds you in till dawn.
- **Quicker dead** (zombies.tide's `Haste`, `HASTE_MOST`): from danger 4, up
  to 40% quicker by danger 16. A sprinter (4.8 m/s) then outpaces a survivor
  walking as they fire (5.6) though not one running (7), so backing off while
  shooting stops being safe late in a run.
- **Measured** (`gravewake_balance`, 25 minutes at most): the last round's
  build, every solo run at the cap with no downs. Closer hordes (16 to 26 m)
  alone: the same; the ring alone: none of 3 died. Quicker dead (30%) without
  the ring: 4 of 12 (bosses twice as tough in 9 of them, which changed
  nothing); with it, 7 of 21, the deaths now mostly in the ring. Both, the
  dead 40% quicker (what's kept): 5 of 12 solo runs died
  (the Gunner 1028 s, the Breacher 553 and 950, the Engineer 390 and 850),
  about half of them in a night's end; the medians: the Gunner 1460 s (the
  cap), the Breacher 1204, the Engineer 1153; the pair at the cap. Two variations measured the same within the noise and were dropped:
  the dead three quarters as thick in the ring (in place of `BOSS_THIN`'s
  0.45, kept for the owner's "a boss's fight lost in the crowd": 7 of 18
  died) and orbs worth three quarters (to level as fast as before the last
  round's 4 a second a danger: 6 of 18 died, levels 34 to 36 at the cap
  against 36 to 37). In all, 18 of 48 solo runs died before the cap; before
  the last round, the medians were 962 s (the Gunner), 1097 (the Breacher),
  1176 (the Engineer), so most runs died: still a little easier than that
  for the bots, who back off from the dead perfectly. A run that gets
  through the fourth night mostly snowballs to the cap. The levers left:
  `HASTE_MOST`, `RING_REACH`, `Harm`, `NIGHT_STEP`.
- **Tests**: the night kept now checks the ring rises round the one standing,
  the boss claws up in it 7.5 m from its middle to 3.5 m short of its fire,
  the one who bled out is back by its middle, walking out at it stops 0.32 m
  short of its line, and it's gone at dawn.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (100 s) pass. Looked at in standard-host captures: the ring rising as the Mauler
  claws up, its line and flames, the survivor walking into it and held.

### Nights on a clock, plague pits and fire, more blood (2026-10-18)

The owner: "the maps need some hazzard to avoid or have to walk around and
maybe the bosses are time based instead of that one mission? more in line
with The Spell Brigade, right now that mission just feels like something
annoying to do; i just wanna roam, shoot and level the fuck up haha. also we
need to play more into satisfaction and gore aspect, not enough blood and
juices flowing". How each works is my pick, Provisional.

- **Nights on a clock** (nights.tide's `Reckon`, `NIGHT_SECONDS`,
  `Match.nightEnds`, `Match.reckoning`): the beacon, its kindling, the vigil,
  the impatient night and its bosses are gone. Each night is three minutes of
  roaming (the night's panel: BOSS IN and the time over a bar running down,
  red in the last ten seconds, a bell and a banner then: `WarnBoss`); over its
  last minute the dead come thicker, up to twice as fast (`Rush`,
  `NIGHT_RUSH`, `RUSH_SECONDS`). When it runs out its bosses (as many as
  before) claw up 24 to 32 m from one of the survivors standing (each a
  random one), pointed to at the screen's edge (missions.tide's
  `Objectives`, BOSS), and whoever bled out is back beside one of them; with
  them down, dawn, as before. `Boss.vigil` is `Boss.own`. Bled out is back
  beside a friend after `BACK_AFTER` (60 s) too (`ComeBack`, `Survivor.gone`
  counting at last), so a party isn't a survivor short for a whole night. No
  mission begins in a night's last 70 s (`MISSION_ROOM`); the bosses rising
  calls off any going. The five beacons' braziers burn at the landmarks
  (`plan.fires`, `BrazierPlace`), to know each by in the dark. Development:
  F7 the night on: its end now (cheat 6), and with its bosses up, dawn; F3's
  bosses come at once (`DevBoss`).
- **Plague pits** (plan.tide's pits, `PITS`, `PIT_RIM`; look.tide's `Earth`):
  eleven open mass graves, 4 to 7.5 m by 2.2 to 3, out in the open between
  the landmarks (clear of them, the worn paths, the graveyard and each other
  by 14 m), each a box 0.7 m over the ground: a wall to the dead's way (they
  go round), too high to step onto, under the rounds (they go over at 1.36
  m). Drawn in the ground's frag from a list of them (hazards.tide's
  `Hazards`): black blood to a rounded, frayed edge, darkest deep in, a bone
  awash here and there, a sick light drifting on it (they read in the dark),
  a lip of pale stones and trodden mud round it, the earth dug out heaped at
  one end (a grave mound).
- **The ground erupting** (hazards.tide's `Erupt`, `Burn`, `Blaze`): every
  `EruptGap` seconds (nine less the night, at least three; over the root of
  the party's `Crowd`), from 15 s into a night, the ground cracks near one of
  the survivors standing, 5 to 12 m off (half the time under the thickest of
  the dead there, kits.tide's `Thickest`), never within 3 m of one nor in
  another fire: its seams glow brighter (in `Earth`), smoke and sparks rise,
  a red ring with an orange one growing out to meet it (`BlazeRings`). 1.6 s
  later it bursts (`ERUPT_HURT` 24 times `Harm`, and thrown, unless
  rolling, the roll's guard; the dead in it take 1.6 times their health, a
  burst: torn apart) and burns 8 s, biting every half second: survivors
  standing in it 12 a second times `Harm` (not while rolling), the dead in it
  set alight (a Fire wound of nothing: `burn` 3 s). Its light, flames, embers
  and smoke; a fire's sound when it's the nearest; all out at dawn.
  `Match.blazes` keeps them for what's placed round them (chests and missions
  keep out: `Afire`).
- **Blood** (fx.tide's `Bleed`, gore.tide's `Spill`, `Splash`, `Tumble`):
  the stains had never been drawn from above. Their camera, set once, was
  gone by the time they were drawn into, so nothing landed (`AimStains` now
  sets it each time); and the ground read their colour as their colour over
  their alpha, which blew up where faint ones lay (alpha blending keeps far
  less alpha than colour): it's their hue at blood's darkness now, darker
  where it's thick. Every hit sprays drops out the far side the way the round
  went (more, bigger, 0.5 to 5 m/s on), and half of them, landing, soak in
  as stains smeared the way they flew (`Mote.stain`, `MURK_MOST` 2600); a
  hit leaves drops under it and a smear out the far side. A kill: a burst, a
  long fan of spray, a pool spreading under it over 2.5 s (`Pool`), seven
  splats thrown up to 4.5 m; five to seven pieces (twelve for a burst),
  bigger, ropes of gut among them, each trailing drops through the air that
  stain where they fall; a head shot a fountain from the neck, the head
  thrown, and the body's head gone (crowd.tide's `Shambler.headless`: the
  frag drops what's within 0.18 m of where the head is on the rest mesh, a raw
  stump round it). Bile: a Bloater's burst and a Spitter's splash leave
  green stains (`Splat.tint`), the burst's thrown bile soaking in too. Old
  blood dries away: every 2 s the stains fade by 2.5% (`Dry`), gone in a few
  minutes, so the clearing doesn't go red for good.
- **Tests**: the harness holds the eruptions and the night's end off (its
  clock 100 s short of it) but where they're the point. New: a pit stops a
  survivor walking at it 0.32 m short and its middle's a wall to the dead's
  way; a round goes over it into one of the dead beyond; the ground cracks
  3 to 12.5 m from a survivor; its burst does 25 to one standing in it and
  throws them, nothing to one rolling, tears one of the dead apart; burning,
  it bites a survivor in it and sets one of the dead walking in alight; it
  goes out. The night kept: the clock run out raises its boss 24 to 32 m off
  and brings the one who bled out back beside the other; not dawn while it
  stands; dawn once it's down; the next night's clock from the start; one who
  bleeds out with no boss coming is back a minute later.
- **More of the dead, from both sides** (rounds.tide's `SpawnRate`,
  `HORDE_SWING`, `RisingToward`, `Match.hordeTurn`; nights.tide's
  `NIGHT_RUSH`): four a second a danger (three), each horde from about the
  far side of whom it's for from the last (within 0.8 rad), and the night's
  last minute up to twice as fast (1.5 first). With no beacon to hold, the
  bots had stopped going down at all: the vigil, standing in the crowd by a
  place, was where they died (above, Phase 3's notes), and a survivor (5.6
  m/s firing, 7 running) outruns any of the dead (4.8 at a sprint). These
  didn't change that (below); they're kept for the carnage and the levels
  (about 45 kills a second by the tenth minute, against 30), not for the
  bots.
- **The bots** (`gravewake_balance`, 25 minutes at most; they step out of
  fires and roll out of a ring about to burst, go for the night's boss when
  it's up and far; a 7th argument, `still`, holds the ground from erupting):
  four runs of each alone and two of the pair, all at the cap but one
  Engineer (783 s): 0 downs, 7 nights, level 36 or 37, 44 to 47 kills a
  second (the pair 67). The same with the ground held still (three Gunners
  and Breachers to a 20-minute cap: all at it, 0 downs), so the eruptions
  aren't what makes it easy. Before this round, the Gunner's median was 962
  s, the Breacher's 1097, the Engineer's 1176. The bots kite perfectly,
  which a player with the dead off the screen's edge won't; the levers if
  it's too easy in play: the dead's pace (`GaitSpeed`, `GaitFor`), `Harm`,
  `SpawnRate`, `NIGHT_STEP`, and something to hold (the missions' hold).
- **Cost** (`gravewake_perf`, release, the eruptions and the night's end
  held off): 2000 of the dead 1.80 ms a tick (2.1 before), 4000 3.36 ms
  (4.1): noise on a loaded machine; nothing new runs for each of the dead in
  the tick (a fire looks up the dead in it twice a second). The frame: `Earth` loops the
  pits and the fires per pixel (eleven and up to twelve), and the stains get
  a fade every 2 s.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (96 s) pass. Looked at in standard-host captures: a pit near the stones (black,
  a sick light on it, the mound at its end) and walked into; an eruption
  cracking, bursting and burning; the boss rising at the key with its marker
  (BOSS 27 m) at the screen's edge clear of the boss's bar; the Gunner
  firing into two thousand of the dead, the ground red and sprays fanning
  out down the line of fire; the dead drawn headless (a test build) to see
  the cut takes the head and nothing else. The browser tests weren't run
  (the owner asked not to).

### Held still at a pick, Bloaters' rings, the right mouse back, level rounds (2026-10-18)

The owner: "when selecting upgrades i can see and hear my character still
animating / explosions need a radius or something, maybe not my own but when
the guys explode or are about to explode i have no idea how far or when they
will explode / i think the right click ability might still be cool to be
manual? idk i like this new "automatic" abilities direction but i miss the
right click power / when i shoot from a slight slope i completely miss the
zombies, we should likely ignore elevation?" How each works is my pick,
Provisional.

- **Held still at a pick** (eye.tide's `Prepare`): while the match is paused
  the picture's clock (`Me.dt`, and so `Look.time`) stops. The survivor's
  pose had gone on (`PoseSurvivor` timed itself by `Frame.dt` and walked its
  stride on the velocity left from the last tick, so its footfalls sounded);
  it takes `Me.dt` now. The effects, the dead's voices and gore already ran
  on `Me.dt`. Two captures half a second apart during an offer are the same
  to the pixel.
- **Bloaters' rings** (hud.tide's `BloaterRings`, `GroundRing`): round each
  Bloater within `BLOAT_SHOWN` (22 m), a faint sick ring as wide as its
  burst (`BLOAT_REACH`, 3.6 m); swelling, the ring red and pulsing, and a
  second, orange, growing out from it to meet the first as the fuse runs out
  (`BLOAT_FUSE`). Drawn over the picture, on the ground's height. Not at the
  end. (The survivors' rings use `GroundRing` too.)
- **The right mouse button's skill by hand** (kits.tide's `UseSecondary`):
  the secondary (the Phase Round, the boot, the mine) is on the right mouse
  button again (a pad's left trigger or shoulder), as soon as it's ready
  while it's held, at the cursor: the Phase Round from the gun down the
  aim's line (the cursor's headshot rule holds), the boot toward the cursor
  (stamping on the nearest one down), the mine the cursor's way; not
  mid-roll. The special (the grenade, the firebomb, the turret) still uses
  itself (`AutoSkill`, now only that slot; `BestLine` and the automatic
  boot, mine and Phase Round are gone). The roll is Space or Shift only. The
  HUD's boxes read RMB, SPACE, AUTO.
- **Level rounds** (hands.tide's `Shoot`, `FlatHit`, `BODY_HIT`;
  ashwick.tide's `WallRay`): a round goes level from the gun along the
  aim's way across the ground, at the gun's height whatever the ground does,
  stopped only by boxes (`RayHit`'s `ground` off), and strikes each of the
  dead whose line from its hips to its head (upright, about one place;
  lying, along the ground) it passes within `BODY_HIT` (0.42 m, times its
  size) of, a boss whose column it crosses. What it strikes is a headshot
  only when the cursor's on that one's head, a weak hit only when it's on a
  boss's weak spot and the round comes from the side the spot faces
  (`Mutant.weakFace`, `Spots.weakFace`: the Mauler's sores face back, the
  others' sacs forward); the round's own way into a body no longer decides
  either. A round aimed 3.5 m over one of the dead's feet strikes it (43).
  The Phase Round, now level, goes its whole 90 m.
- **Tests**: the harness holds only the special now; the secondary's checks
  press the right mouse button. New: a round aimed high over one of the dead
  strikes it. The shot Bloater's orb check counts only the orbs it drops
  (a level Phase Round kills down its whole line, and their orbs lay far
  off). The dodged slam allows a point of regained health.
- **The bots** press the secondary as they did before it used itself (the
  Phase Round with three or more within 20 m and on target, the boot close
  in, a mine with three within 8 m). Medians, eight runs alone and four of
  the pair (the last round's in brackets): the Gunner 962 s (916), the
  Breacher 1097 s (1117), the Engineer 1176 s (1006), the pair at the cap.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (136 s) pass. Looked at in standard-host captures: six Bloaters walking in
  with their rings, one swelling (red, the orange ring growing), and two
  frames half a second apart during an offer, the same to the pixel. The
  browser tests weren't run (the owner asked not to).

### A roll, skills by themselves, shared chests, missions (2026-10-18)

The owner, after playing from above: "maybe instead of dashing its more so a
rolling that makes you dodge like in dark soul games / i do like mixing this
with a dark souls style games / objective was not clear, didnt even find it;
i do like the rotating missions in `The Spell Brigade` where they just give
you an optional extra enhancement; also i want players to share chests and
for them to be an option so we all play together for the same goals; sharing
XP too but i think thats already the case; XP should be very clear and very
prominent in the UI / i think all actions expect roll and fire should be
automatic, so firing grenades or maybe even extra abilities we find. similar
to the drone, its automatic, i think everything other than dodge and main
weapon firing should be automatic. / for the bosses the hitboxes felt weird,
they are so big yet shooting at their feet felt like was always a miss. /
headshots, we need to make these make sense, my gutt is telling me it could
be based on mouse position, if you shoot while aiming at the head it deals a
headshot?" How each works is my pick, Provisional. (The Spell Brigade's
optional missions grant an Augmentation that changes how a spell works, by
GameGrin's review; here a mission's done for a rarer item pick.)

- **The roll** (kits.tide's `StartRoll`, hands.tide's `Steer` and
  `Rolling`), in the dash's place for everyone: Space (or Shift, or the right
  mouse button; a pad's East or South button or left trigger), the way the
  move points on the screen, else the way they face. `ROLL_SECONDS` (0.5 s),
  `ROLL_SPEED` 9.5 m/s eased from 1.3 to 0.5 of it (4.2 m); for its first
  `ROLL_GUARD` (0.36 s, `Guarded`) nothing the dead do lands: claws, bile,
  bursts, a boss's slam, charge and leap (`TakeHurt` sends `Dodged`, a swish
  and a pale ring, in place of the blow; no slime, no shove; the screen
  doesn't redden). No shot while rolling (a load goes on). It goes through
  the dead without knocking them about (it's a dodge; the dash's blow, the
  Breacher's bowling over and `Barged` are gone). Two charges, 2.5 s each
  (the dash's 4.5), an Energy Drink one more; Kerosene sets alight the dead it
  passes. `Survivor.dash`, `dashes`, `dashWait`, `dashWay` are `roll`,
  `rolls`, `rollWait`, `rollWay`; `Skill.Slide` is `Skill.Roll`. The body
  tucks low and turns head over heels the roll's way (people.tide,
  `ROLL_TURNS`, `ROLL_MIDDLE`), the gun out of the hands for it.
- **Skills by themselves** (kits.tide's `AutoSkill`): the secondary and the
  special have no keys; each goes as soon as it's ready and one of the dead
  is in its reach (`AutoReach`), where it does the most: the Phase Round
  down the line (`BestLine`) with the most of the dead within `AUTO_LINE`
  (0.8 m) of it, of lines toward the nearest `AUTO_TRIES` (24); a grenade or
  a firebomb into the thickest knot (`Thickest`, within `AUTO_KNOT`, 4 m), 3
  to 18 m (15 for the firebomb) off; the boot at the nearest close by,
  whichever way, at the head of one down (it stamps the nearest down, where
  it had taken the first in the list); a mine underfoot with any within 7 m;
  a turret toward the cursor with any within 16 m. A boss counts as five.
  With nobody in reach it looks again `AUTO_LOOK` (0.25 s) later, so the
  herd's gathered only then. They don't take the hands (`Act.Throw`,
  `Act.Kick`, `Survivor.throwing` and `landed` are gone): a throw leaves the
  hand at once and the boot lands at once, its leg out for `KICK_POSE`
  (`Survivor.kicked`). The HUD's three boxes read AUTO, SPACE, AUTO.
- **Standing by, not E**: `Hands.use` (and `secondary`, `utility`,
  `special`) are gone. Standing by a chest opens it (`CHEST_SECONDS` 1.5),
  by the cold beacon lights it (`KINDLE_SECONDS` 2.5, long enough not to by
  walking past), by a friend who's down raises them (not while rolling).
  Enter or E still rises again after the end.
- **Chests are the party's**: opening one sends every survivor `Owed`, a
  pick of three as at a level, whoever found it ("A CHEST: EVERYONE PICKS AN
  ITEM"). Four a night whatever the party (`CHESTS_A_NIGHT`; it was five and
  two more a survivor, each an item for its opener alone).
- **Missions** (missions.tide): optional, one at a time, only while the
  night's beacon is cold (lighting it calls the one going off; `ResetMissions`
  at nightfall and a new run). The first `MISSION_FIRST` (25 s) into a night,
  then 20 to 35 s after each ends, one appears 22 to 40 m from a standing
  survivor on open ground, 14 m or more from the beacon: a hold (some
  survivor in a 6 m circle for 12 s all told, half again as fast for each
  more, within 50 s), a cull (25 times `Crowd` of the dead killed in a 9 m
  circle within 45 s, counted in zombies.tide's `TakeWound`), or a hunt (an
  elite rises there, a Warden 10 times as tough, running, `Zombie.elite`,
  within 60 s). Done, every survivor's owed a boon (`Owed.boons`,
  `Survivor.boons`): a pick of three uncommon or rare items, offered before
  the level's (`RollItem`'s `least`). Out of time, nothing. A panel under the
  night's, a gold circle of motes on the ground or over the elite, banners
  and sounds. F1 starts one of each kind in turn (`Cheat` 40 to 42,
  `Match.devMission`).
- **Where the objectives are** (missions.tide's `Objectives`, hud.tide's
  `Pointer`): the cold beacon ("BEACON"), the lit one when a survivor's out
  of its reach ("BACK TO THE BEACON") and the mission: a bobbing marker over
  it with its distance while it's on the screen, else an arrow at the
  screen's edge the way it is, slid clear of the panels.
- **Experience across the top** (hud.tide): a bar near half the screen wide,
  LEVEL and the share filled, easing as it fills and glowing as orbs come in.
  (Experience was the party's already: `Match.xp`.)
- **Bosses' bulk** (zombies.tide's `Spots.base`, `columnRadius`,
  `columnHigh`; hands.tide's `ColumnHit`): an upright column from a boss's
  feet to the top of its head, its radius the mutant's (`Mutant.radius`, 0.7
  to 1.1 m), struck by any round into it (a body hit), with the spheres for
  the head, chest, hips and weak spot inside it; the cursor's picking uses it
  too. A round at the Mauler's feet does a body hit's 43, where it had
  passed between the spheres.
- **Headshots by the cursor** (eye.tide's `Pointing`, hands.tide's
  `BodyFirst` and `Shoot`): on one of the dead, the cursor's line says what
  it's on, the head, a boss's weak spot or the rest; the aim's the head's
  middle, the weak spot's or the chest's. A round that strikes one of the
  dead whose head the aim is within `AIM_HEAD` (1.25) of its radius is a
  headshot (a weak hit, the same for its weak spot), whatever of it the
  round goes into first: from behind, through its back, a headshot still
  (86 where a body hit's 43). The crosshair's gold on a head, orange on a
  weak spot, warm on the rest.
- **Tests**: the harness holds the automatic skills (`autos`) and the
  missions (`missions`) off but in the checks about them; `only_skill` lets
  one skill go at a time. New: a roll of 4.2 m, through one of the dead
  untouched, its charges and cooldown; each automatic skill at its mark (the
  Phase Round, a grenade into a knot 8 m off, the boot and its stamp, a
  firebomb, a mine, a turret, a turret's Frostbite); a chest opened by
  standing by it and a pick for both, the one away too; a hold done in 8 s
  with both in the circle, a cull of three, a hunt's elite shot down; a slam
  rolled through with no health lost; a round at the Mauler's feet; a
  headshot from behind by the cursor. The four chests and the 2.5 s beacon.
  (Archetype numbers moved again: `arch4_Zombie`, `arch5_Beacon`,
  `arch6_Orb`.)
- **The bots** don't press skills any more; they roll out of a press of the
  dead and walk to what they used to press E at (and ignore the missions,
  but a hunt's elite comes for them). Eight runs alone, four of the pair,
  25 minutes at most, medians (the flat map's round in brackets): the Gunner
  916 s (831), the Breacher 1117 s (588), the Engineer 1006 s (552), the
  pair at the cap (1480), six nights and none down. Their skills now go off
  whenever ready and in reach, where before they used them sparingly; a
  player using them by hand often would have been nearer this. A try at
  harder nights (`NIGHT_STEP` 1.32, `Harm` at `danger^0.45`) didn't shorten
  the runs (the Gunner and the Engineer lasted longer: more danger is more
  experience, more might, and the skills scale with it), so it's back as it
  was; the owner's play first.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (105 s) pass. `gravewake_perf` (release, which now holds the skills back
  while its horde gathers, else they thinned it): 2000 of the dead 2.10 ms a
  tick, 4000 4.09 ms, the skills going off as it's timed. Looked at in
  standard-host captures: the experience bar, the
  beacon's and a hold's markers (at the edge, clear of the panels, and over
  it), the mission's panel and banner, a roll frame by frame. The browser
  tests weren't run (the owner asked not to).

### Flat, from above only, no jumping (2026-10-18)

The owner, after trying the view from above: "i think we should make the map
mostly flat now and just not be able to jump; we should also remove all
other points of views and stick to top down". Done as asked; the landmarks
that were heights became my picks (Provisional): a cairn and a dead oak.

- **The ground** (plan.tide's `Ground`): a gentle roll (Perlin, 0.35 and 0.1
  m, where it had been 1.1 and 0.35), laid flat round the fire and under the
  ruin, rising into the trees past the edge as before. The chapel's rise, the
  spire's two tiers, the four ramps, the graveyard's hollow and the ravine are
  gone, with `Mesa`, `Ramp`, `RampTo`, `Bump` and `Ditch`. The cliff
  machinery (`STEEP`, `Passable`, the dead's piling and `CLIMB_COST`) stays,
  with nothing left to use it but the rise past the edge.
- **The landmarks**, each still a beacon's: the chapel's ruin on level ground
  (its floor and walls at 0); the graveyard, level; the standing stones; the
  cairn east (`CAIRN`, where the spire was: six boulders in a ring 4.4 m
  across, open toward the fire, round the beacon); a great dead oak south
  (`OAK`, where the ravine ran, the beacon beside it). From above, the oak at
  twice its size hid the survivor whole, and the cairn's boulders at 1.7 to 2
  times crowded the beacon: the oak is at its own size, the boulders 1 to
  1.25. The first night may now be any beacon (the spire's top had been
  kept for later as the narrowest). The worn paths run to the landmarks.
  F6's walks visit them.
- **No jumping**: `Hands.jump`, `Survivor.airJumps`, `JUMP_SPEED` and the
  Spring Boots item (a jump in the air) are gone; `ITEM_COUNT` is 23. The
  Leap and Blink skills, which no survivor has, stay.
- **Only the view from above**: `View`, `Feel.view` and V, `Hands.look` and
  `Feel.sensitivity`, `ShoulderFrom`, the first-person eye's bob, dip, recoil
  and widening at a run, and the first-person arms (`fp.tide`, `art/fp`,
  CoinCoin's CC BY 4.0 kit, so its credit is gone from the title and
  `art/CREDITS.md`; held.tide's `DrawHands`; look.tide's `Lens.hands` pass,
  `Layers.Hands` and `Develop`'s compositing of it) are gone. `AimedAt` is
  the aim no further than the gun's range, `AimFrom` from the gun toward it;
  moving and the dash go by the screen (`TOP_YAW`), running is any way; a
  throw always lands at the aim. `Me.playing` is whether a survivor is seen
  (not the title, nor F9's views); `Me.ears` is where this machine hears from
  (the survivor's head while playing), which the sounds' distances and a
  blast's shake now use: from above they had measured from the camera, 13 m
  up, so nothing was ever near (a fault of the last round's view from
  above). The mouse's wheel zooms always.
- **What's in the way is cut away** (look.tide's `InTheWay`): from above,
  anything tall south of a survivor (a tree, the oak, a ruin's wall) stands
  between them and the camera. `KitLit` and `KitCutout` don't draw what's in
  a cone from the eye to this machine's survivor (`Look.focus`, a metre over
  their feet), `CUT_RADIUS` (2.6 m) across at the survivor, ragged at its
  edge; shadows are drawn as before. Friends under a tree are found by their
  rings.
- **Tests**: shots, throws and the dash go where the cursor is: the harness's
  `put` sets the aim (`Hands.aim`) with the survivor's turn, and dashes with
  no way pressed go the way they face; the over-the-shoulder shot, Spring
  Boots, the five high places and their piles, and the ramps and jumping at
  the rise's face are gone. The soak's longest any hunted is 32 s of 180,
  from 164.
- **The bots** set the aim at their target (a hand's error on it), move in
  the screen's axes, take no ramps and don't jump when stuck. Eight runs
  alone, four of the pair, 25 minutes at most, medians (the hordes' round in
  brackets): the Gunner 831 s (576), the Breacher 588 s (424), the Engineer
  552 s (616), the pair 1480 s, the cap (591), three nights. With no cliffs
  to be cornered under or ramps to climb, the bots hold out longer; people
  seeing all round them from above likely will too. The levers are as before
  (`REGEN_RATE`, `Harm`, the rates).
- **Cost** (`gravewake_perf`, release): 2000 of the dead 1.88 ms a tick, 4000
  4.14 ms; packed, 1.70 MB at 4000.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (102 s) pass; the release page builds (21.1 MB, the arms' kit gone). Looked
  at in standard-host captures from above: the camp, each landmark by F6's
  walks (the oak and the cairn twice, after shrinking them), the cut-away
  through the oak. The browser tests weren't run (the owner asked not to).

### Hordes, and a view from above (2026-10-18)

The owner, after the third round: "i think zombies should spawn in "hordes"
more so than one randomly / also I want to play with a top down camera too".
How both work is my pick, Provisional.

- **Hordes** (rounds.tide's `SpawnDead`): the rate is as it was (`SpawnRate`,
  `Crowd`, `VIGIL_RUSH`, `BOSS_THIN`); what changed is where and when they
  come. Of the dead owed, `STRAY_SHARE` (a quarter, kept as a fraction in
  `Match.strays`, no roll) rise alone as before, anywhere 16 to 32 m round;
  the rest wait in `Match.hordePool`. Every `HORDE_EVERY_LEAST` to
  `HORDE_EVERY_MOST` seconds (5 to 9; the first `HORDE_FIRST`, 3 s, into a
  night), once at least `HORDE_LEAST` (6) wait, a horde forms: its middle
  (`RisingPlace`) 22 to 32 m from a standing survivor or, in a vigil,
  `VIGIL_FOCUS` of the time from the beacon; its patch `HORDE_SPREAD` (0.9)
  times the root of how many, 2 to 12 m across; one gait rolled for all of it
  (`GaitFor`; Bloaters and Wardens keep theirs), so a horde comes at one pace
  and the runners' hordes are seen as such; at most `HORDE_MOST` (400). It
  claws up within `HORDE_RISE` (1 s), `HORDE_PER_TICK` (12) a tick at most,
  each where the ground in its patch is open (`HordePlace`, four tries, else
  its middle). `HordeRose` is heard (the rise and a few groans from all over
  the patch, from wherever it is) and seen (earth thrown up over the patch).
  At the start, alone, that's about a dozen and a half every seven seconds.
  `ResetHordes` at a new night and a new run; `Match.scattered` (development
  and tests: `few`, `perf.c`) sends every one alone, as before. The
  development key's rush still comes six a tick, alone.
- **The Powder Keg takes the nearest**: its burst had taken the first of the
  dead its cells listed within reach, not the nearest its line says; in a
  horde those were often not the ones beside the kill. It now keeps the
  `2 + keg / 2` nearest (items.tide's `Earn`).
- **The view from above** (`View.Above`; `View { Eyes, Shoulder, Above }`
  replaces the bool `third`: `Feel.view`, `Hands.view`, `Survivor.view`, and
  `Me.outside` for any eye that isn't the survivor's own, `Me.above` for
  this one). V goes round them: over the shoulder (still the first), from
  above, through the eyes.
  - The camera (eye.tide's `Prepare`): `TOP_HIGH` (11.5 m) over the
    survivor's feet and `TOP_BACK` (7 m) south of them, looking down at a
    metre above their feet, north up the screen (`TOP_YAW` 0, so the chapel
    is at the top and the graveyard on the left), `TOP_FOV` 52 degrees; the
    mouse's wheel takes it between 0.6 and 1.6 times as far (`Feel.zoom`).
    A blast's shake moves it a little. The ears are the survivor's, turned
    as the screen is. The F9 views are as they were.
  - The cursor (`Pointing`, a view): the lock still holds the real one; the
    mouse moves `Me.cursor` across the screen, starting where the look was.
    The right stick, once it's pushed, aims its way from the survivor
    (`PAD_REACH`, 9 m) till the mouse moves. What the cursor's on is
    `Me.pointed`, which the input sends (`Hands.aim`): on one of the dead
    (hands.tide's `BodyFirst` along the line from the eye), its chest, or a
    boss's weak spot when the cursor's on that (from above every cursor on
    the dead would otherwise be on a head, and every round a headshot); else
    where the line crosses the gun's height above the survivor's feet, so a
    shot goes level and passes under the cursor (the first version took the
    gun's height above the ground under the cursor, which the camera sees
    higher on the screen: the tracers passed over the cursor); where the
    ground rises into the line first, the gun's height above where it meets
    it.
  - The match (hands.tide): `FaceAim` turns the survivor to the aim each tick
    (yaw, and pitch for the gun's line), not when it's within 0.4 m of their
    gun; moving goes by the screen (`MoveYaw`: `TOP_YAW` from above, else
    the look), and running is any way, not only forward; `AimedAt` is the aim
    (no further than the gun's range), so `AimFrom` sends the rounds from the
    gun to it, as over the shoulder. The dash goes by the screen too. A
    grenade or a firebomb is lobbed to land on the ground under the aim
    (kits.tide's `ThrowTo`: no further than `THROW_MOST`, 20 m, a flight a
    second per 13 m, 0.35 to 1.3 s), a grenade's fuse its flight and a tenth
    of a second, so it bursts there rather than bouncing on. The boot, the
    mine, the turret and the Phase Round go the way they face, which is the
    cursor's.
  - The HUD: the crosshair (and the hit marker, HEADSHOT) is drawn at the
    cursor, warmer when it's on one of the dead; the blows' arcs go round
    the survivor; the over-the-shoulder ring where a shot would stop isn't
    drawn (from above the dead are nearly always in the line). A ring on the
    ground round each survivor, in their colour, finds them in a crowd. The
    first-person arms aren't drawn; tracers start at the gun.
  - Not done: trees between the camera and the survivor aren't faded (the
    groves are sparse since the props went, and in the captures the survivor
    stayed in sight); no aim assist; the camera doesn't lead toward the
    cursor.
- **Tests** (`gravewake_test`): from above, turned away, the aim put on one
  of the dead's chest: turned to it (0.000 off) and the carbine's round hits
  (43); half a second up the screen while facing east: 3.09 m north, none
  east, still facing the aim; a grenade at a spot 12 m off bursts 0.40 m from
  it. A horde of 30 claws up within a second, all within its 4.9 m patch,
  23 m from whom it's for. The Powder Keg check stands two of the dead alone,
  one beside the other: with the dead in hordes, others had stood in the line
  of fire. The soak's longest any hunted is 164 s (of 180), up from 138:
  hordes rise further off.
- **The bots** (`gravewake_balance`, 25 minutes at most, eight runs alone,
  four of the pair, medians, with the last round's in brackets): the Gunner
  576 s (409), the Breacher 424 s (206), the Engineer 616 s (918), the pair
  591 s (652); a night or two. The Breacher's shotgun and boot do well
  against a horde come close together; the Engineer's turret, which takes
  the nearest one at a time, less so.
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  (151 s) pass. Looked at in standard-host captures from above: the camp,
  firing at the cursor (the tracer through it), the rings, a grenade thrown
  to the cursor, a walk south through a grove, a horde coming in. The
  browser tests weren't run (the owner asked not to).

### After the third playtest: harder, clearer, three bosses (2026-10-18)

The owner, after playing Phase 3: "ok the game is too easy and it gets a bit
hard to read things; i think we need to work on the visibility aspect.
between the glowing orbs and then everything being just dark its hard to see
whats going on. I could basically stay alive forever by just dashing, even if
I got hit I got my life back so quickly it didnt really matter. I think we
need some contrast improvement and maybe just cleanup some map props that are
there just for the sakes of being there. they add noise to already a full
scene. we need more boss variety and also it would be nice if the boss wasnt
just a scaled up human/zombie looking dude, we can do mutants thats fine. it
would also be nice being able to be strategic about where we shoot maybe? if
they have some glowing back or whatever and that deals more damage for
example. the boss fight is also messy cause we keep getting surrounded. Idk
how `The Spell Brigade` did it but they made the movement feel and be so
impactful." Then: "also some ability allows me to kill multiple enemies at
once by killing one i think? that ended up allowing me to kill entire hordes
easily too". Everything below is my pick, Provisional, for their next play.

- **Harder.** Health came back at 60% a second after 3.5 s unhurt, whole in
  under two: now `REGEN_RATE` (4%) a second after `REGEN_DELAY` (4 s), 25 s
  from nothing (hands.tide). The dead's blows grow with the danger
  (`Harm(danger)`, rounds.tide: `danger^0.4`, about 1.7 times at danger 4 and
  2.5 at 10) for a claw (30), bile (18), a Bloater's burst (45) and a boss's
  blows; a claw had been 30 at every danger, which the levels' health soon
  made nothing.
- **The dash** (kits.tide, hands.tide's `DashThrough`): 4.5 s a charge (3.5).
  The Gunner's and the Engineer's throw the dead aside (`knock` 8 / size)
  instead of knocking them down, which had left the way out flat; the
  Breacher's still bowls them over. A dash that strikes sends `Barged`: a
  jolt of the camera for whoever made it (eye.tide, more for more struck) and
  the thump of bodies (sound.tide).
- **Movement** (game.tide): walk 5.6 m/s (4.6), run 7.0 (6.8), grip on the
  ground 22 (14, `GRIP`: how fast feet take up the way asked), gravity 24
  (20), a jump 7.0 (6.2): quicker to start, stop and turn, a jump as high and
  sooner down. What I could find on The Spell Brigade (reviews; I've no way
  to play it) says it has no dash at all: quick, responsive movement and big
  hits,
  so the feel went into the walk, the dash's thump and the hits; the owner's
  play decides it.
- **Chain kills** (items.tide's Powder Keg): every kill burst into `2 + keg`
  of the dead for `ZombieHealth × (0.25 + 0.25 keg)`: half of what one can
  take at a stack, so with a fast gun a kill felled the ones it burst into,
  and a crowd fell a kill at a time. Now `2 + keg / 2` of them, for
  `0.15 + 0.1 keg` (a quarter at a stack). Kills by burning or bleeding are
  `echo` now (zombies.tide's `TakeWound`), so fire spreading through a crowd
  doesn't set off kegs either (an item's own kills already didn't).
- **Fewer of the dead while a boss stands** (rounds.tide's `BOSS_THIN`,
  0.45 of the rate while any isn't dying): the boss's fight had been lost in
  the crowd round it.
- **Three bosses, mutants** (boss.tide, crowd.tide, `art/mutants/`): the
  Brute (a walker drawn three times the size) is gone. `BossKind` is the
  Mauler, the Broodmother or the Stalker (`MutantOf`: name, size, radius,
  speed, health against the others, hit spots, tint, the HUD's hint); a
  night's take them in turn (`KindForNight`: `(night - 1 + k) % 3`), the
  impatient ones at random. Health `2000 × danger × Crowd × kind` (1, 1.1,
  0.75). Their moves (`BossMove`, `NextMove` picks):
  - The Mauler (2.4 m/s): within 7 m the slam (1.1 s windup, 6.5 m, 45; a
    red ring), every third move and out to 26 m the charge (0.9 s windup,
    15 m/s for 1.3 s, 35; a line on the ground), then `Recover`, 1.6 s spent.
    Weak spot: the sores on its back, three times.
  - The Broodmother (1.7 m/s): a volley (0.9 s windup, five globs of bile in
    a spread, `Lob`), and a birth (2.4 s crouched: `6 + 2 × night` runners at
    half a walker's health, shoved out). Her sac: twice, four times while she
    births.
  - The Stalker (4.2 m/s): out to 30 m a leap at where its target will be
    (0.75 s crouched, 0.8 s through the air, 40 within 4.2 m of where it
    lands; a ring), within 5 m a swipe (0.55 s, 3.2 m, 30), and every third
    move a shriek
    (1.8 s: every one of the dead within 18 m `Frenzied` for 7 s,
    `FRENZY_PACE` 1.6 times as fast, their eyes red-hot). Its throat: half
    again, four times while it shrieks, and a tenth of its health there
    (`Boss.hurt`, `SHRIEK_CUT`) cuts the shriek short into a 1.3 s `Stagger`.
- **Weak spots**: each boss's hit spots are `Spots` in `Herd.bosses` (head
  and chest radii, the weak spot, how wide, how many times), found by
  `Body.boss`; the dead's are a man's at their size (`SpotsOf`). They were
  first fields of `Body`, which grew the packed world 8% for the thousands
  that never use them; apart, it's 2% (the frenzy and the index). A round is
  on the weak spot if where it first meets the body is within
  `WEAK_PATCH` (1.15) of its radius of the spot, so a spot partly inside the
  chest's ball still counts (the chest's ball of one of the dead at the
  Stalker's size had buried its throat: each mutant has its own head and
  chest radii). The HUD's marker is orange and big on one (`Struck.weak`),
  with a low crack; the boss bar names it and says where the weak spot is;
  the spot glows on the mesh (the growths' `uv2.x` 0.5, `Shambler.weak`),
  brighter when it's open (birth, volley, shriek: motes rise from it too).
- **The mutants' art** (`art/mutants/tools/make_mutants.py`, Blender 4.4):
  three of the dead from `art/people` (the king for the Mauler, the maid for
  the Broodmother, the wanderer for the Stalker), each bone scaled along and
  across and bent from `RECIPES`, the meshes deformed by that pose and the
  pose made the rest (`inherit_scale` off while posing, so a scale isn't
  passed down unless the name ends in `*`), growths and spikes put on bones;
  then the catalog's clips baked into `anim.png` as the horde's are, and each
  body's head, chest, hips and weak spot measured into `mutants.json` (which
  `MutantOf` copies by hand: rebaking means reading them again). Gotchas met:
  the clips are quaternion keys, so bones posed in Euler mode stood in a
  T-pose; and scales compounded down the arms until inheriting was turned
  off. crowd.tide draws them with the horde's `Mutate` vert (`Mutants`,
  `MutantMesh`, `PoseMutant` per move: walk, run, claw, slam, crouch, jump,
  land, hit, death, rise).
- **Readability** (crowd.tide's `CrowdFlesh`, look.tide, nights.tide's
  `NightLook`): the horde's skin paler and less grimy, a little sky light on
  it, a rim toward the eye fading slowly with distance, eyes that glow; the
  bosses darker, their own colour, so the fires don't wash them out. The
  nights brighter: the moon (0.52, 0.57, 0.74; was 0.4, 0.45, 0.63), sky light
  (0.09, 0.1, 0.13; was 0.06, 0.07, 0.1), fog 0.007 (0.009), exposure 1.75
  (1.6), each night's kind brightened alike; bloom from 1.1 (0.9), weighed 0.4
  (0.6). Orbs (orbs.tide) are quiet: drawn within 45 m (70), smaller, their
  halo faint (alpha 0.12, was 0.45), lower and bobbing less; hundreds lie in a
  fight. The ash and embers drifting over everything (fx.tide) are gone.
- **Props gone** (plan.tide), the ones there for their own sake: the camp's
  bag, bucket and rope; the ruin's chest and loose bricks; the cart and the
  wagon east and the second fire by them; the loose gravestones (the graves'
  own stay); the grass and mushrooms over the clearing. The groves are five
  trees, not seven; the standing stones a wider ring (6.5 m, was 4.8),
  smaller.
- **Development**: F3 a boss of each kind in turn (`Cheat` 30 to 32, through
  `Match.devBoss`).
- **Tests** (`gravewake_test`): the dash's cooldown (4.5 s), a Gunner's dash
  throwing one of the dead aside without knocking it down, the Breacher's
  still knocking down; the Mauler slamming, charging and standing spent; a
  carbine round to its hips 49, to its back's sores 147; felled as the
  night's own, a pick each; the Broodmother's volley (five globs) and birth
  (eight runners); the Stalker's leap (7.3 m, landing where it aimed), its
  shriek frenzying one of the dead beside it, and a round to its throat
  (196 of 1000) cutting the shriek into a stagger.
- **The bots** (`gravewake_balance`, 25 minutes at most, the medians; eight
  runs alone, four of a Gunner and a Breacher): the Gunner 409 s (1014 in
  Phase 3), a night; the Breacher 206 s (1172), every run dying in the first
  vigil; the Engineer 918 s (607), three nights; the pair 652 s (to the cap),
  2.5 nights. The bots don't dodge, don't dash out and stand in the vigil's
  crowd, which is just what the owner said they no longer had to fear; the
  Breacher's bot fights up close and suffers most. A first pass was harder
  still (regen 2.5% after 5 s, `Harm` at `danger^0.5`: the Breacher 197 s,
  the pair 259 s); this is softened from it. The levers: `REGEN_RATE`,
  `REGEN_DELAY` and `Harm`'s power.
- **Cost** (`gravewake_perf`, release): this machine was loaded by other
  work, and runs of the same build ranged 3.9 to 8.7 ms at 4000; alternating
  this round's and Phase 3's builds, they came out the same (4.5 to 5.6 ms
  both). Packed, 1.70 MB at 4000 (1.67 MB).
- **Validation**: native debug `gravewake_test` and the web debug WASI one
  pass (156 s). The browser tests weren't run (the owner asked not to). Looked
  at in standard-host captures: the horde by night from the camp and from
  afar, each night's kind, each boss in its moves and its weak spot's glow,
  the orbs lying after a fight.

### Phase 3: the nights, and a design pass (2026-10-17)

The owner, after a review of the design I gave them (the run had no shape:
everything rode one clock and nothing traded time for power; a kill's
experience didn't grow with the dead's health, so the level picks dried up
just as the run got hard; four players faced six times one's health a
second with four times the guns, shared one survivor's chests and paused for
twice as many picks; aiming had little to aim at; the SMG was the weakest
gun in a horde; most items were stat bumps, and none touched the game's own
moves): "yeah go ahead and make a coherent game design as you see fit. I will
then test and we will tweak". Everything below is my pick, Provisional,
waiting on their play. Phase 3 as first planned (gold, chests that sell)
became this: the owner had chosen free chests found by roaming.

- **Nights** (`nights.tide`). The run is a string of them. As one falls
  (`Nightfall`), its beacon stands cold at one of five landmarks
  (`BeaconPlace`: the spire's top, the chapel's ruin, the graveyard's edge,
  the standing stones, the ravine's bottom; never last night's; the first
  never the spire's top), a pale wake-fire in a great brazier and a column of
  it 34 m up, an additive streak with no fog so it's seen from anywhere; and
  its chests are hidden (`ChestsTonight`: five, two more a survivor past the
  first). Holding E within `KINDLE_REACH` (3 m) for `KINDLE_SECONDS` (1.5 s)
  lights it (`Kindling`, which falls back while nobody holds it, as prying a
  chest does). The vigil: the dead come `VIGIL_RUSH` (1.4) times as fast,
  `VIGIL_FOCUS` (65%) of them rising round the beacon; its Brutes rise 24 to
  32 m off (`VigilBrutes`: one, two from the fourth night, three from the
  seventh); the beacon fills in `VIGIL_SECONDS` (90) with every standing
  survivor within `VIGIL_REACH` (11 m, across the ground), in proportion to
  how many of them are (none, none); a ring of embers on the ground marks the
  reach. Full, with its Brutes down: dawn (`Phase.Dawn`, `DAWN_SECONDS`, 10).
  Every one of the dead catches fire and falls in the first two seconds (by a
  hash of where it stands, no orb, no burst), the Brutes too; every orb lying
  flies to the nearest survivor from anywhere; each survivor gets `Dawned`
  (up, whole, ready; one bled out back beside the beacon). Then the next night
  falls. Chests and beacons carry their night (`Chest.night`, `Beacon.night`)
  and go when it's over.
- **The trade**: a night's chests are a set, not a timer's, and they're lost
  at dawn; the danger climbs with the clock meanwhile. So looking about longer
  finds more items and costs danger; lighting early is safer and leaves
  things. The night's panel (top left) says how many of its chests are found,
  never where. A night that goes on too long grows impatient: past
  `IMPATIENT_AFTER` (270 s of it, lit or not, past a night looked about for
  two or three minutes and kept in 90 s) the danger runs ahead of the clock
  (`Match.late` grows with it, so it climbs twice as fast) till its dawn,
  "IMPATIENT" on the panel. While it's unlit, a Brute comes every
  `BOSS_EVERY` (150 s) of its own clock, which gives no pick (a big orb
  only). The night's own give every survivor a pick. (The bots found the
  need: with the levels coming on, a Breacher could stall to the 25-minute
  cap in an unlit night, and then, with that closed, in a lit one it
  couldn't keep for the dead round it.)
- **The fallen**: one who bleeds out is gone till the beacon's lit
  (`Recalled`, sent to every survivor as it catches) or dawn, beside it, with
  everything; meanwhile their eye watches a standing friend over the shoulder
  (eye.tide). It was 30 s at the fire, which cost nothing in a party; now a
  party a survivor short has a reason to light the beacon sooner. Alone,
  going down still ends the run (nobody standing).
- **The danger** (`Danger`): `(1 + (clock + late) / DANGER_EVERY) × NIGHT_STEP^(night - 1)`,
  150 s and 1.25. Nothing for the party's size any more: a party brings
  `Crowd` (1 + 0.8 a survivor past the first) times as many of the dead, as
  tough as one survivor's. Four used to start at Medium with 28% runners and
  face 6x one's health a second; now about 3.4x for 4x the guns.
- **Experience** (`OrbWorth`, orbs.tide): an orb is `0.5 × danger^1.1`, as the
  dead's health grows, times its kind's (`KindWorth`: a Bloater's and a
  Warden's 3, a Spitter's 2; a Brute's 50). A level takes
  `LEVEL_FIRST (40) × LEVEL_GROWTH (1.28)^(level - 1) × Crowd`, so each
  survivor's picks come about as often in a party as alone (they had come
  twice as often for four, each one pausing the run till all had chosen). On
  paper (`tools/balance_math.py`, everything killed, 80% of the orbs taken,
  two minutes looked about each night) a pick every 25 s at first, 35 s in
  the second night, about a minute by the fourth; before, the gaps had grown
  to three minutes by the fifteenth. The pause stays as the owner asked; with
  Dawn it waits too.
- **New dead** (zombies.tide's `ZombieKind`, rounds.tide's `KindFor`, one roll
  into a local): **Spitters** from 75 s (2%, +0.6% a step of danger, 5% at
  most): thin and sallow, a glowing throat (`Shambler.spit`). A few times a
  second one looks for a clear line (`LongRay` from its head) to whom it's
  after, 4 to 20 m off; with one it stands its ground within 13 m, its throat
  swells `SPIT_WINDUP` (0.8 s, the claw clip's first half, a gargled retch),
  and it lobs bile (`Shot.Spit`, a missile of the dead's, seat -1) at where
  the survivor will be (half their way, `SPIT_LEAD`), arcing over the dead;
  it splashes on the first survivor or the ground: up to 18 (half at the
  edge) and `Slimed` 2 s (60% of their speed, a green edge on the screen).
  **Wardens** from 2 min (2%, +0.8% a step, 8% at most): broad, dark iron mail
  below the neck with rings and the moon's glint, the head bare; twice the
  health, always shambling; blows to the body (not burning, bleeding or a
  shove) do `WARDEN_MAIL` (25%), the head all of it, and on its back the mail
  doesn't help; a turned blow shows sparks, a grey hit mark and a ring of
  iron. Claws 1.5x. Both are what aiming is for in a horde, with the Bloater.
- **Items** (24, was 16; `Survivor.items` is `int[24]`):
  - New commons: Smelling Salts (raising 50% faster each; not offered alone).
  - New uncommons: Energy Drink (a dash more: `DashCharges`), Kerosene (the
    dead the dash goes through catch fire; 0.4 m wider each), Steel Toes (50%
    more to the dead on the ground, each), Gas Mask (Bloaters' bursts can't
    hurt you; the ones you kill burst 30% wider each), AP Rounds (a round
    goes through one more each).
  - New rares (there were two): HE Rounds (a round bursts on the first of the
    dead it hits, 1.5 + 1 m a stack across, 30 + 10% of its damage to six at
    most; a pellet as often as it sets items off; `Shot.Pop`, a small light
    burst), Dragon's Breath (25% a stack, times the proc, a hit sets the dead
    alight).
  - Mortar Beacon is the Mortar Flare now: the night's beacon is the word.
  - Reworked to scale with the danger: the Powder Keg's burst hurts `2 + keg`
    of the dead for `ZombieHealth × (0.25 + 0.25 keg)` (it was a flat 23
    times might, nothing by the fourth minute); the Ember Ring sets the dead
    within `2.5 + 0.5 a stack` m alight every second (the fire's own damage
    grows with the danger).
  - Items reach rounds through `Arms` (whose, what's held, might, crit), so
    the Engineer's turret keeps its maker's as it's set down and its rounds
    go through `Land` (`TURRET_PROC`, 0.6): their items fight for them.
- **The Engineer**: the SMG is 15 a round (14), 50 a magazine (40) and sets
  items off 1.5 times as often (`proc`); with the turret carrying items, the
  Engineer is the survivor of builds.
- **The Gunner**, the weakest with the bots once the Engineer's turret
  carried items: the carbine is 25 a round (22) and goes through three (two).
- **The Brutes**: `BossHealth` is `2000 × danger × Crowd` (it was 2500 ×
  danger, half again for each before it, nothing for the party); their blows,
  speed and roars grow with their night (`Boss.night`), not with how many
  came before.
- **Points are gone.** They bought nothing once gold went; the HUD shows each
  survivor's kills, and the end says how many nights and how long.
- **Night looks** (`NightLook`, eased each frame in eye.tide's Prepare): the
  first moonlit; then mist (fog 0.015), a blood moon, starless, the wake (a
  sick green), round again from the mist; dawn a grey-rose light. The camp's
  fire and the wagon's still burn; the spire's and the graveyard's fires
  became beacons, cold but on their night.
- **The dead's way** (ashwick.tide): two places at the spire held stragglers
  for good, and the soak's "longest any hunted" had been 160 to 171 s of a
  180 s limit. At the foot of a face, the band of cliff cells two wide:
  `Reaches` let a step inside a cliff's cells rise not at all, so where the
  ground climbed a hair away from the face, one walking off it was held; a
  step may now rise as walkable ground does (`STEEP` a metre). Beside the
  spire ramp's low side, the field (a cliff cell costing as open ground) sent
  a few straight over the wall, which takes a pile, and too few to pile
  pressed against it; a step up a face (from a cell lower by `CLIMB_RISE`,
  half a metre, kept per cell in `Ashwick.heights`, bytes) now costs
  `CLIMB_COST` (5) to the way, down or along it one. So a way round a few
  metres longer is taken; the dead still pile up every face the test asks
  (1.5 to 2.6 m up). The longest any hunted in the soak is now 105 s.
- **HUD**: the night's panel top left (the night and its kind; "Light the
  beacon in the chapel's ruin" and the chests found; the vigil's bar, who's
  near it, "FELL THE BRUTE" once it's full; dawn's count to the next), an
  arrow at the screen's edge toward the beacon for one outside its reach
  during the vigil, "Hold E to light the beacon" by it, banners for nightfall,
  the vigil and dawn, kills in place of points, the Energy Drink's dash pips.
- **Development keys**: F2 six of a kind of the dead, Bloaters, Spitters and
  Wardens in turn (`Cheat` 20-22); F7 the night kept (dawn now, `Cheat` 8).
- **The bots** (`gravewake_balance`, which now lights each night's beacon once
  its chests are found, 150 s in (its 7th argument) or a friend's bled out,
  holding E through the hits; takes the ramps to the spire's top and the
  ruin; keeps within 8 m of a lit beacon; shoots a Brute that's close, or
  when the beacon's full; and prints how many nights each run kept and how
  it ended). Ten runs a survivor alone, six of a Gunner and a Breacher, four
  of four, 25 minutes at most, the medians: the Gunner 1014 s [180-1476], 2
  nights [0-4]; the Breacher 1172 s [320-1479], 1.5 nights [0-3]; the
  Engineer 607 s [463-1476], 1 night [1-3]; the pair and the four to the cap,
  3 and 2 nights. A pick about every 25 s at first and 40 s by the eighth
  minute, about two items a minute in all. A third of the solo runs end
  about a fifth of the way into the first vigil: the bots don't step out of
  a Brute's slam or charge, which are shown before they land, so I didn't
  tune that down to them. The strongest runs reach the cap at night 4 or 5:
  at a high danger the dead up at once (4000 at most) cap the pressure while
  the levels keep adding damage. The levers for that are the damage a level
  gives (`Might`, 24%) and `NIGHT_STEP`; the owner's play first.
  Along the way: the first vigil had been a wall (1.75 times the dead, a
  Brute of 2500 × danger 18 to 28 m off: most Gunners died in it), now 1.4,
  2000 and 24 to 32 m; the Gunner, the weakest, got the carbine's 25 and
  three; the nights went to 1.25; and the impatient night came in (above).
- **Cost** (`gravewake_perf`, release): 2000 of the dead 2.00 ms a tick
  (2.03 before), 3000 3.14 ms (3.37), 4000 4.03 ms (4.27). The perf tool now
  turns down offers (with a level's worth of orbs coming sooner, a pick had
  held the run still and the tick cheap) and raises only walkers (Bloaters
  had burst through the horde before the timing).
- **Tests** (`gravewake_test`): the first night's beacon cold and not the
  spire's, its seven chests (two survivors) on open ground 16 m apart and
  10 m from the beacon, one opened; a Spitter 9 m off spits and slimes; a
  carbine round does a quarter to a Warden's mail and twice a walker's to its
  head; three in a line take two rounds' worth, and three with an AP Round;
  HE Rounds wound the one beside; Dragon's Breath and Kerosene set alight; a
  Gas Mask keeps a Bloater's burst off; the turret with its maker's
  Frostbite freezes; the impatient Brute isn't the night's; then a night
  kept: one bled out still gone 40 s on, the beacon lit by E in 1.5 s and
  them back beside it, its one Brute, the vigil's six seconds with nobody,
  one and both near it (0, 0.033, 0.067), not dawn while the Brute stands,
  dawn with it down (every one of the dead dying, the orbs in, both up), and
  the second night at another beacon with seven new chests and the danger up
  more than 1.15 times. The soak leaves Spitters out of the longest hunted:
  they hang back on purpose. The orb section clears orbs lying from before.

### Phase 1: the horde (built 2026-10-16)

- **Drawn without skeletons** (`crowd.tide`): the six zombie bodies are cut to
  about 700 triangles each by `art/crowd/tools/make_crowd.py` (Blender: seams
  welded first, then decimated, which had torn the packs' split meshes to
  shreds; Blender 4.4's actions need their slot chosen or nothing plays) and
  their ten clips (walk, run, sprint, idle, claw, fall, get up, two deaths,
  rise) baked into one `[Raw]` texture, `art/crowd/anim.png` (546 by 1248, a
  column per corner, two rows per frame: the corner's place, a byte an axis
  across the bake's box, and its normal). A vert, `Shamble`, reads two frames
  and blends; each body is one `Draw.Instanced` of a `Shambler` list
  (transform, frame rows, blend, flash, burn, frost, tint), gathered each frame
  by a per-zombie view (`GatherZombie`) and drawn into the picture and the
  moon's shadows. What a zombie shows comes from its state (`ShowState`): the
  same clip choices the skinned poses made. The byte positions jitter a little
  between frames, which suits the look the owner asked for next.
- **Struck without skeletons**: a zombie's head, chest and hips come from its
  state (`Muster`: hunched on its feet, flat on its back as it lies, half in
  the ground rising). `Brace`, the per-zombie pose sampling in the match, and
  the gibs (pieces that were the whole skinned body drawn with bones
  squeezed) are gone; a kill bursts blood instead.
- **The `Herd`** (`zombies.tide`): every one of the dead not dying, rebuilt each
  tick after the dead move (`HerdClear`, `Muster`) into a grid of 2 m cells
  (first and next slot lists). Blows, kicks, abilities, spells, missiles and
  bursts gather only the dead near them, only when something lands (`Near`);
  the dead's crowding walks the cells directly, a dozen close neighbours of
  two dozen looked at.
- **Numbers** (`gravewake_perf`, release, this Windows machine, the horde
  crowding two survivors): 500 of the dead 0.35 ms a tick, 1000 0.63 ms, 2000
  1.19 ms, 2500 1.47 ms, 3000 1.82 ms (10.9% of a 60 Hz frame). Before the
  herd's grid and the lazy gathering: 2000 cost 11 ms. A release run of the
  game with 2030 entities: the whole frame's CPU 3.0 ms, the GPU 1.0 ms in 121
  draw calls. `MAX_ALIVE` is 3000. Waves are now `(40 + 30w + 4w²)` (74 the
  first, 740 the tenth, 2,240 the twentieth for one), health `40 + 25(w-1)`;
  F4 (development) brings 2000 more, six a tick (`Match.rush`).
- **Not measured**: a browser frame with thousands on screen, rollback's
  re-simulation with several players (each tick re-run costs as above), and
  the match's world hash and snapshot at a megabyte of state.
- The dead's voices are held to a few a second (`Ears.voices`).

### After the first playtest (2026-10-17)

The owner, playing it: "the map sucks (first one) / the zombies spawn only
close to me i think? they just come from one direction / sounds need a
rework". Asked, the map's faults were a boring layout and that it looked bad;
the sounds', weak guns and the wrong mood.

- **Spawns** (`rounds.tide`'s `SpawnDead`): each of the dead claws up at a
  random place in a ring 16 to 32 m round a random standing survivor, any
  direction, where the ground is open, walkable and on the survivors' way
  field (eight tries, then any riser 7 m or more off). They had come from the
  five risers nearest a survivor, so a horde poured out of one corner.
- **The map** (`plan.tide`): a new layout at 44 m (`CLEARING`): the chapel's
  rise (6 m, the ruin on it, a ramp each side), the beacon's spire (a 4 m
  tier, a 9 m top with the beacon, a ramp to each), the graveyard sunk deeper
  with a brazier, a ravine across the south (3 m, banks under 0.5 a metre so
  the dead cross it), standing stones, big stones for cover, trees in six
  groves instead of scattered, grass in tufts round them (fewer, smaller,
  darker). `Ground` is built in layers: rolling ground, the hollow and the
  ditch, then each tier (`Mesa`) and its ramp (`RampTo`, which meets the
  ground at its foot whatever its height there). `Earth` paints worn paths
  from the fire to each ramp, the graveyard and the ravine, and higher ground
  paler. The night is clearer: a brighter moon and sky, fog 0.009 (was
  0.015). F6's walks visit the new places.
- **Sounds** (`sfx/tools/make_sfx.py`): every gunshot is layered (the
  recording's crack, a low thump at the gun's own pitch, a bright snap, a
  shotgun's wall of low noise), driven and compressed hard, and played at
  full volume for this machine's own. The church bells, choirs and lutes are
  gone: the waves start with a rising synth and a deep impact and end on a
  minor-to-major chord; game over is an impact and a drone sliding down; the
  beds are a dark synth pad with a heartbeat pulse under early waves and a
  120 bpm track (kick, saw bass, snare, hats, arpeggio) when the horde is big
  (150 up, or from wave six); levels and uncommon and rare items have synth
  arpeggios; the night has thunder in place of crows and owls.

### No waves, levels and the Brute (2026-10-17)

(Since Phase 3, above: the run is nights; the danger, the spawn rate, the
Brutes' timing, coming back after bleeding out and the levels' cost are as
it says, not as below.)

The owner, after the second playtest: "ambient sound might need to go away /
lets maybe remove the rounds system and just give upgrades based on leveling
up / also did we add "bosses"? i envison them as big monsters with maybe attack
patterns or something". The picks below are mine (Provisional).

- **The ambient bed is gone** (wind, thunder, far moans); the fires, torches,
  music and the dead's own voices are what's heard between fights.
- **The run** (`rounds.tide`): once every ready survivor has picked, a
  breath (`START_SECONDS`, 4), the siren, and the dead come without end.
  `Match.danger`, worked out each tick, is `(1 + minutes / 1.5) × (1 + 0.25
  per survivor past the first)`; the dead come at `1.4 × danger^1.25` a
  second (as many as six a tick), each with `40 × danger^1.35` health,
  runners from danger 1.4 and sprinters from 3.5. The HUD shows the run's
  time and the danger in Risk of Rain 2's words (Easy to Impossible, then
  HAHAHAHA). Nothing sounds when it starts (the siren that did is gone). Bleeding out, a survivor is back at their place by the fire
  `RETURN_SECONDS` (30) later with everything (`Return`). The game is over
  when nobody's standing; the score is how long it lasted. Power-ups are at
  most four a minute. `Phase.Break`, the waves' counts and the regrouping
  are gone.
- **Items at levels** (`items.tide`'s `Experience`): each level the party
  gains is a pick owed to every survivor (`Survivor.pending`); the next owed
  is offered as soon as the last is taken. Offers grow rarer with the run's
  minutes. The owner, playing it: "the upgrade screen should pause the game,
  in multiplayer it should wait for all". So while any ready survivor is
  choosing, `Match.paused` (set by `Pause`, first thing each tick) has every
  system of the run wait: the clock and the danger, spawning, the dead, the
  Brutes, what flies, turrets, drones and the ember ring, the survivors'
  moving, guns, skills, reviving and returning, power-ups. Choosing and the
  offers go on. The engine has no pause of its own; this is the game's, in
  the match, so every machine waits alike. The cards are big across the
  middle, the picture dimmed (`Offers`); one who has chosen sees who it's
  waiting for.
- **The Brute** (`boss.tide`): every three minutes from the third
  (`BOSS_FIRST`, `BOSS_EVERY`), 22 to 34 m from a survivor, rising 2.5 s. The
  plague king's body three times the size (`BOSS_SIZE`), dark red, drawn in
  the horde's own instanced draws; in the herd as one body three times as big
  (`Body.size` scales every hit sphere and the boot's reach), health `2500 ×
  danger × (1 + 0.5 per Brute before)`, never knocked over or frozen; Death's
  Touch takes a twentieth a hit. It walks at the nearest standing survivor
  (2.3 m/s) and, after each move's cooldown (2 to 2.5 s): every third time a
  roar (1.6 s; at 0.6 s, `14 + 6n` of the dead claw up 3 to 11 m round it);
  else within 7 m a slam (1.1 s with its arms up and a ring of red sparks on
  the ground 6.5 m across, then 45 damage and a throw to everyone in it, the
  dead in it knocked over), else within 26 m a charge (0.9 s lining up, a
  line of sparks where it'll run, then 15 m/s for 1.3 s: 35 damage and a
  throw to anyone it meets, the dead flung aside, stopped by walls). A
  survivor is thrown by `Shoved`. Felled (`BossFelled`): every survivor owes
  a pick, the one who felled it gets 1000 points, the party 25 experience.
  The HUD has its health across the top; it's heard roaring as it comes (its
  own scream, two-fifths the pitch), at each roar and charge, and dying.
- **Tests**: `gravewake_test` plays the run, not waves: a minute and a half
  of it, a level's offer taken mid-fight, the bled-out survivor back at the
  fire after 30 s, a Brute that slams, charges and roars and, felled, owes
  each a pick, then three minutes holding the fire. `tick_safe` now keeps the
  standing survivors whole through each tick (in a run that only gets worse,
  a crowd's claws can land together for more than anyone's health, and the
  run ends when nobody's standing); single-target checks thin the dead to a
  few with no more coming (`few`), and the checks that want a crowd have one.

### After the second playtest (2026-10-17)

The owner: "The sounds still suck. It sounds like a Roblox game. For the PSX
thing, it's a shame that we can't see in the distance because of the
pixelization. I wonder if there's a way to make pixels denser based on
distance." Asked, they chose pixels finer with distance, and for the sounds:
"maybe use existing sounds from people".

- **Pixels by distance** (`look.tide`): the scene is drawn at the window's
  size (`lens.scene`, with its depth kept as `lens.sceneDepth`), and
  `Develop` cuts it into pixels whose size is the scene's distance there:
  the nearest about 270 up the window (`look.block` of the window's pixels
  across, `look.low` of them), a third finer past 18 m and half their size
  past 40 m, no finer (`Fineness`; each a whole number of the window's
  pixels, so 4, 3 and 2 at 1080 lines). It went to an eighth at first, down
  to the window's own pixels far off; the owner: "i like it better (more
  visible) but distant stuff feels too detailed almost now". The distance is read once
  for the nearest-size pixel round each pixel (`NEAR / (1 - depth)`), so a
  pixel is one size throughout. The first-person arms and gun draw into their
  own target (`lens.hands`, cleared see-through each frame, with the scene's
  camera) in place of clearing the scene's depth, which would have made the
  whole scene read as far; `Develop` lays them over at the nearest size. The
  dither is in each pixel's own size, and `Snapped` puts corners on the grid
  of the pixels at their own distance.
- **Sounds by people** (`sfx/tools/make_sfx.py`, `sfx/CREDITS.md`): my
  synthesis is out of everything the owner hears most.
  - Guns: The Free Firearm Sound Library's recordings as they were made,
    each shot's own echo off the trees left ringing (0.7 to 1.8 s), three
    takes a gun picked at random each shot. No added thump, snap or drive.
    The SMG's first takes were cut from the PPSh's bursts (a shot every
    50 ms), so each sounded like a burst (the owner: "the SMG one shot
    sounds like a burst shot"). They're now its single shots (`P_30P`). The
    other guns' files have one shot each: the Mossberg's second onset is an
    echo 0.2 s after every blast, and the AR-15's 32 ms pair is one shot's
    crack and boom.
  - Handling: SpringySpringo's real (airsoft) reloads, a rifle's, a pistol's
    and a shotgun's pump.
  - Hits: qubodup's meat impacts and Independent.nu's wet ones; Kenney's
    interface click for the hit marker.
  - Waves: a town's flood siren in a storm (TinyWorlds' recording) started
    one, till the owner heard it as "a toilet flush" (2026-10-17): it's gone,
    and the run starts with no cue; a breath of Paul Wortmann's Dark Cavern
    Ambient is a Brute's fall, and a longer one the game's end.
  - Music: two minutes of Juhani Junkala's Post Apocalyptic Wastelands under
    early waves, two of Ruskerdax's Ancient Grudge when the horde is big,
    each crossfaded round on itself.
  - Items and levels: Kenney's RPG sounds (a latch, a pouch, coins) and an
    interface chime.
  - Not yet replaced: the dead's voices (recordings already), footsteps,
    explosions, the power-ups' sounds.

### Third person (2026-10-17)

The owner: "lets try a 3rd person mode, with first person its hard to see
around you making getting back stabbed too easy". Third person is the default
(`Feel.third`, local, so each machine picks its own view). V or the pad's
right-stick press switches between it and first person.

- The camera (`ShoulderFrom` in hands.tide, used by eye.tide's `Prepare` and
  by the aim) sits 3.2 m behind the head, 0.7 m to the right and 0.35 m up
  (`SHOULDER_BACK`, `SHOULDER_RIGHT`, `SHOULDER_UP`). It's pulled in toward
  the head where the ground would be between them, and kept 0.35 m above
  the ground. `Me.third` is this frame's answer. The development views (F9)
  aren't third person.
- The view is sent with the hands (`Hands.third`, stored as
  `Survivor.third`), so the aim is in the match and every machine shoots
  alike.
- First version: shots went from the eye toward the point 40 m along the
  camera's line. The owner: "it doesnt go where i aim nor from the gun;
  worst of both worlds", and the carried gun was upside down. Now:
  - `AimedAt` is what the crosshair is on: the first of the dead, the
    ground or a wall along the camera's line, starting level with the head,
    so nothing behind the survivor is aimed at.
  - `AimFrom` gives where the shots leave and which way: from the gun in
    the hands (`GunHand`, below under The survivors' bodies) toward
    `AimedAt`. The guns and the
    Phase Round use it; in first person it's the eye along the look.
  - When something is between the gun and that point (a rock the camera
    sees over, one of the dead nearer the gun) and the shot would stop
    more than 14 px from the crosshair, the HUD (`Overlay`) draws a red
    ring where it stops and dims the crosshair.
  - Every carried gun (`DrawCarried`) is laid along its shots' line at the
    palm, top up (`quaternion.LookRotation`), in place of a fixed turn off
    the wrist bone, which had it upside down. Its muzzle is kept per seat
    (`Grip.muzzles`), and every survivor's tracers start there.
- Seen from behind: your own survivor and gun draw as everyone else's do
  (`DrawSurvivor`, `DrawCarried`). The first-person arms aren't drawn.
- Test: the gun section fires one carbine shot over the shoulder, turned
  so the camera's line is on the zombie's chest (`aim_over_shoulder`).

### Playtest notes: the drone, the kick, the gun in the hands, the orbs (2026-10-17)

The owner, playing: "the drone is right in my face (next to my crosshair)"; "the
kicking animation in 3rd person also needs some work"; "the gun is close but
not exactly on their hands; probably the same with the other guns"; "XP orbs
are way too big, also lets add some satisfying sfx to it".

- **The drone** (`items.tide`'s `DroneAt`, in the match: its shots leave from
  there) hovers over the left shoulder, 2.35 m up and 0.45 m back; over the
  right it sat by the over-the-shoulder camera's crosshair.
- **The kick** in third person (`PoseSurvivor`): the gun stays in both hands
  (a kick had dropped them to the clip's arms) and the right leg comes up
  level ahead (`upperLegR` turned -1.75 rad about the body's right, the knee
  a little bent through it), the other knee giving, the body leaning back
  0.3 rad, on the act's timing as the first-person boot has it.
- **The gun in the hands**: after the right arm's reach and the hand's turn,
  the gun is put where the fist came to (between the wrist and the middle
  knuckles, 0.55 of the way): an arm too short for where the gun was asked
  to be had left the hand short of it. The left hand then reaches for the gun
  as it is.
- **Orbs** are smaller (0.045 m plus a little for more, was 0.11, a halo 2.2
  times that). Taking one is a glass chime (`sfx/orb_1..3`: Kenney's
  interface glass tings with a soft pluck an octave up and a little room,
  `make_sfx.py`'s `orb`), each in a quick run of them a step up a C
  pentatonic scale, two octaves, starting again at the bottom after 0.7 s
  without one; a dozen a second at most.

### Experience as orbs (2026-10-17)

(An orb's worth grows with the danger since Phase 3, above; at dawn every orb
flies in.)

The owner: "lets make XP drop as orbs so you need to get close to pick them
up (not THAT close), that should avoid camping too much". The numbers are mine
(Provisional).

- `orbs.tide`: each kill leaves an `Orb` (half a point of experience, as a
  kill was) on the ground where the dead fell, off a pile too; a Brute's is
  one of 25. The nearest survivor on their feet within `ORB_REACH` (5 m of
  their chest) draws it, and it flies to them at `ORB_SPEED` (14 m/s),
  following them; reaching them it's the party's (`GainXp`, which also
  levels, as many times as it's enough). One nobody comes for is gone after
  `ORB_SECONDS` (40 s), blinking its last 6 s. At most `ORBS_MOST` (800) lie
  at once; past that a kill's experience is the party's at once. Restarts
  clear them. Experience is still the party's, whoever takes it.
- Seen: a small blue-green light with a soft halo bobbing over the ground,
  bigger for more (gathered per orb into `Fx.orbSprites`, one instanced
  draw); a few sparks when taken. Heard: a soft tick for this machine's own,
  rising in pitch through a quick run of them, a dozen a second at most.
- Test: a kill from 8 m leaves one that lies there a second untaken; from
  4 m it's taken in about a quarter second; one left alone is gone after
  40 s, its experience with it. The 90 s run still levels the party (its
  survivor stands 6 m off what it shoots and walks among them).

### Piles of the dead, and more of them (2026-10-17)

(A step up a face costs the dead's way 5 since Phase 3, above, not 1; the
spawn rate and the dead's health are Phase 3's.)

The owner, on the climbing below: "what if we make them only be able to climb
on each other? so they create a mountain of zombies similar to day z or
whatever the movie was! ... also, if we can include even more zombies maybe we
should". (World War Z's piles.) The numbers are mine (Provisional).

- **No one climbs a cliff alone.** A zombie may step up into a cliff's cell
  only when it's within a scramble of that cell's ledge (`Reaches`:
  `ashwick.ledges`, the highest ground within two cells, less `STEP_UP` and
  `SCRAMBLE`, 1.1 m), so banks and low ledges are still walked and scrambled
  up alone; anything taller only from up a pile.
- **The pile** (zombies.tide's `Hunt`): `Zombie.lift` is how high one is off
  the ground (kept in the herd too, `Herd.lifts`). One pressed against a
  cliff it can't get up (`pressing`), or behind others already up off the
  ground (within 1.3 m ahead, `liftedAhead`), climbs at `CLIMB_RATE` (1.3 m/s,
  a sprinter half again) onto one beside it no more than a body above it
  (`onto`: within 0.85 m, `LAYER`, 0.55 m, higher). What's under it (within
  0.6 m and lower) only stops it falling, never lifts it; with nothing under
  it, it comes down at `FALL_SPEED` (5 m/s). The dead push apart only from
  others at their own height, so one on top stands over the ones beneath. The
  dying drop too, so shooting the bottom brings down what's on it. Once one is
  near the ledge it walks over. In a pile they show the rise's clawing.
- **They go straight at a cliff**: a cliff's cell costs as much as open
  ground to the way (`CLIMB_COST` 1, was 3), so the dead pile up whatever face
  is between them and the survivors; ramps are taken when they're on the way.
- Seen (a fixed camera over the rise's east face, a horde of 1500): from 13 s
  in, 100 to 270 of the dead up off the ground, piles to 4.5 m, spilling over
  the rim by 19 s. Survivors on high ground have a while, then the dead come up
  every side; four alone can't get up the ruin's cliff at all.
- **More of the dead**: `SpawnRate` is `3 x danger^1.25` (was 1.4: three a
  second at the start, thirty-five ten minutes in), up to ten a tick (was
  six), `MAX_ALIVE` 4000 (was 3000); each a quarter weaker (`ZombieHealth`
  `30 x danger^1.35`), a kill half the experience (so levels come as they
  did).
- **Numbers** (`gravewake_perf`, release, this machine): 2000 of the dead
  2.07 ms a tick, 3000 3.37 ms, 4000 4.27 ms (26% of 60 Hz); before the piles
  2000 cost 1.19 ms. The first try asked the ground's height for every
  neighbour (4.46 ms at 2000); now once a zombie a tick, and the ledges are
  kept per cell.
- **Tests**: every high spot is reached by a horde of 500, piling 2.4 to 5 m
  up a face. With twice the dead, the checks that aren't about the crowd
  were moved off it: the 90 s run keeps the survivors whole through each
  tick (a crowd's claws could down one within a tick), a Second Wind is tried
  with only a few about, and the guns' and skills' target is moved to open
  ground near the fire, turned so 9 m in front of it is flat and clear, and
  held still (`to_open`; the leap is from 3.5 m, the mine 1.8 m, as nothing
  walks to them now).

### A bigger, more open clearing (2026-10-17)

The owner: "we should make the map bigger; also include less obstructions i want
movement to feel relatively free, having boxes or graves stopping the flow is a
bit annoying". The scale and what was taken out are my picks.

- **1.4 times as wide** (plan.tide): `CLEARING` 62 (was 44), about twice the
  ground. The landmarks moved out with the edge, each its own size: the
  chapel to (0, 42), the spire to (43, -4) with the beacon at (46, -1), the
  graveyard (-38, 4), the stones (-36, 34), the ravine (-48, -38) to (45, -32).
  The ramps keep their shape relative to what they climb. The ruin's walls
  are now laid from `CHAPEL.y`, and the graveyard's graves, trees and brazier
  from `GRAVES`. The wagon, cart, second fire, big stones and groves moved out
  1.4 times. The edge has 36 risers (26) and 210 + 240 trees (150 + 170), with
  undergrowth and grass to match.
- **Grids to match**: nav `NAV_X0` -78 by `NAV_W` 156 (-56, 112), buckets 20
  (14), the herd's `HERD_W` 78 (56), the stains' `STAIN_REACH` 78 (2048 px
  now about 7.6 cm each). The ground mesh is 240 cells of 1.25 m out to 150 m
  (192 to 120). The overhead development eyes are higher (98 m and (36, 42,
  -36)). The F6 walk's places come from the landmarks' constants.
- **Out of the way**: the camp's crate; the east's two barrels, crate, anvil
  and fence; the graveyard's four iron fences. Gravestones are 0.75 the kit's
  size and no box (they're passed through, as grass is), and there are 5 loose
  stones, not 9. Two of the eight big stones are gone (the two nearest the
  fire). Groves have 7 tries, not 9, each kept 2.6 m clear of the last (it
  was 1.6), out to 8.5 m. What still blocks: the ruin's walls, the braziers,
  the bench and cauldron by the fire, the wagon and cart, six big stones,
  trees' trunks.
- **Cost** (`gravewake_perf`, release): 2000 of the dead 2.03 ms a tick
  (2.07 before); the grid of twice the cells doesn't show.
- **Tests**: the places in test.c moved with the landmarks (`NAV_HALF`,
  `NAV_SIDE` for the grid). Every high spot is still reached by a horde of
  500 (piles 2.4 to 3.5 m up a face), all four ramps walked, the rise's face
  not climbed by jumping (0.7 m). Two checks had leaned on how the old run
  went: the stamp now clears the dead within 8 m and sets the one down
  freshly fallen (others the kick had knocked down were nearer, and the boot
  stamps the first it finds), and done sliding.
- Looked at from overhead (F9) and at the F6 walk's places.

### Bloaters (2026-10-17)

The owner: "we should add other types of zombies, maybe mutants that are
chunkier and explode when they get close or when you kill them (dealing damage
to zombies but also potentially the player)". The numbers and look are my
picks, not confirmed.

- **What one is** (zombies.tide): `Zombie.kind`, `ZombieKind.Bloater`. Its
  health is `BLOAT_HEALTH` (2.5) times the danger's, it always shambles, at
  `BLOAT_PACE` (0.8) of that, and `Body.size` is `BLOAT_SIZE` (1.25), wider to
  hit and to wade through (`Wading` scales by size). It doesn't swing.
- **The fuse**: a survivor within `BLOAT_NEAR` (2.4 m) and it stops and swells
  (`fuse` counts up, a `Sfx.Swell` cue). At `BLOAT_FUSE` (0.9 s) it bursts.
  Knocked down it still bursts; frozen, it doesn't start.
- **The burst** (`Rupture`, from `Hunt`): killed, it bursts the tick after the
  blow; swelling, when its time's up. Within `BLOAT_REACH` (3.6 m), by how near
  (the square root of 1 minus distance over reach, as missiles' bursts): the
  dead take twice the danger's health, from 40% to all of it, are thrown
  back and, past 0.3, knocked down. That's credited to whoever killed it, as
  `echo`, so it sets off no Powder Kegs. Survivors standing take `BLOAT_HURT`
  (45), from 40% to all of it, through armour, and are shoved. Then it's gone
  at once (`Destroy`). One that burst by itself sent a killing `Struck` of
  its own for the gore, and gives no orb or kill. A Bloater killed gives 50
  more points and an orb of 1.5 (the dead's is 0.5).
- **Rising** (rounds.tide's `BloaterShare`): none in the first
  `BLOATERS_FROM` (40) s, then 2.5%, one percent more a step of danger, 7% at
  most. The random draw is made into a local before the `Spawn`. `Match.bloaters`
  (development key F2, `Cheat` 7) makes the next six rise as Bloaters.
- **Drawn** (crowd.tide, `Shambler.bloat`): 1.18 by 1.04 by 1.16 the size, and
  the vert pushes out the belly (and a little the shoulders) round its height
  in the body. Skin lerped toward a sick dark green under the grime and
  blood, boils (Perlin spots) that glow faintly. Swelling, the belly grows and
  throbs ever faster and the boils glow hard. The first tries were a pale
  glowing gold and swelled into a stretched blob; looked at in captures till
  they read as the dead.
- **Burst seen and heard**: `Shot.Bile` (fx.tide): a sick green flash, bile and
  blood sprayed out, a thin foul haze. Gore from the `Struck`. `bloat_swell`
  (a moan half an octave down, gargled, over wet bubbling, swelling to the
  end) and `bloat_pop_1/2` (a bang's low end, wet splats, a slime splash, a
  hard low thump) from `make_sfx.py`, CC0 sources credited.
- **Tests**: a Bloater 1.8 m from a survivor bursts at 0.90 s, the survivor
  taking 37 and a walker beside it 194. Shot dead from 8 m, it bursts at once,
  the walker beside it hurt and the survivor not. `sturdy()` now always
  gives a walker: the orb test's kill had been a Bloater, gone the tick after.

### Wading through the dead; the dash as a shoulder charge (2026-10-17)

The owner: "we should slow the player down when touching zombies"; "make the
dash maybe animate into a shoulder bash? makes more sense visually as to what
is actually happening (hitting things in front)".

- **Wading** (hands.tide's `Wading`, in `Steer`, which now reads the `Herd`):
  each of the dead pressed against a survivor (on its feet, risen, about as
  high, within `TOUCH_GAP` 0.1 m of touching) takes `TOUCH_SLOW` (22%) of
  their speed, 60% at most (`TOUCH_SLOW_MOST`). A dash goes through at full
  speed, so it's the way out of a crowd. The dead still don't hold a survivor;
  they only slow one.
- **The shoulder charge** (people.tide's `PoseSurvivor`; local `bash` and
  `bashTurn`): while a dash lasts, the whole body (legs included) turns
  side-on to the way it goes (`BASH_SIDE`, about 63 degrees), leading with
  the shoulder that turns it least from the aim (the left going ahead or
  left, the right going right; picked as the dash starts). It leans into it
  (`BASH_LEAN` 0.6 rad, hips to chest, the head kept up), and the gun comes in
  low down the way it goes. In 0.06 s, out over 0.22 s after the dash.
  Looked at in captures, ahead and to the right: the first try always led
  with the left shoulder, and dashing right turned the body to face the
  camera.
- **Tests**: a second's sideways walk covers 4.35 m alone and 1.77 m with three
  of the dead kept pressed against the survivor (beside and behind); it must
  be under half. The felled-Brute check now moves the Brute to open ground
  south of the fire first; this change moved where the run took it (up on
  the clearing's east edge, out of the gun's line).

### Legs that can be shot (2026-10-17)

The owner: "i cant shoot legs? of the zombies". A round, the crosshair's stop
and a thrown missile met a zombie only at three balls (head, chest, hips), so
anything below about 0.6 m went between its legs. Now `LegsHit`
(missiles.tide) adds two balls (`LEG_RADIUS` 0.2, larger for a missile) from
the hips away from the chest: down to the ground when it stands, along it
when it's down, and the Brute's scaled by its size. A leg is a body hit, not
a head. Test: the carbine aimed 0.35 m up a zombie's legs wounds it.

### No climbing cliffs by jumping (2026-10-17)

The owner: "i can spam jump and climb steep hills". Every jump left the
ground, each landing a little further up, so any slope could be climbed a hop
at a time.

- **Footing** (hands.tide's `Steer`): ground (not a box's top) rising more
  than `SLIDE_STEEP` (0.9) a metre is no footing: `Survivor.sliding`. On it
  one can't walk any way uphill (`Uphill()`, the slope's flat direction),
  is pulled down it at `SLIDE_PULL` (22 m/s²), and can't jump. A ramp
  rises 0.75 at most, so all of them stay walkable.
- **Ramp heads** (plan.tide's `RampTo`): a ramp climbed the whole way to the
  rise's edge, where it met the mesa's rounded lip and turned steep, so
  survivors slid back off the top of the west ramp. Now each ramp reaches
  its top `RAMP_FLAT` (a fifth of its length) before its head, and runs flat
  to the edge from there.
- **Test**: holding forward and jumping every fifth of a second for 6 s at the
  face of the chapel's rise must stay below 2.5 m (0.3 m at most now), and
  walking up each of the four ramps from its foot must reach its top. The
  dead still reach every high spot (piles 2.0 to 4.8 m).

### The balance: bots playing whole runs (2026-10-17)

(The results below are from before the nights; Phase 3, above, has the
bots lighting beacons and the results since.)

The owner: "do a balancing pass on the characters, we need some tools or math
or maybe even simulate full matches and see which are too strong which are too
weak, how often we should give the player an upgrade etc".

- **`gravewake_balance`** (test/balance.c, a release build; not a test): bots
  play whole runs through the real input (aiming with the look at a human's
  turn rate and with some error, backing off from the dead, going for orbs and
  chests when it's safe, using each survivor's skills, reviving a friend).
  `gravewake_balance <roles> [runs] [minutes] [first seed] [quiet|loud]
  [items]`: roles one letter a survivor (G, B, E; "GB" a pair); items the
  survivors start with ("GD2,PK1"), to weigh an item. It prints each run, then
  the middle and the quartiles of how long runs lasted, kills a second, items
  and their pace, levels, how much experience was picked up, downs, and when
  each level came. Several go at once in the background; a 16-minute solo run
  takes about a minute.
- **`tools/balance_math.py`**: the same numbers on paper, read from the
  source (`Danger`, `ZombieHealth`, `SpawnRate`, `LevelSpan`, `Might`, the guns'
  `Info`): the health the dead bring a second against what each gun deals,
  through its reloads, and the level pace.
- **Changes from it**: the Mortar's shell 100 (was 160); Frostbite 12% a
  stack for 2 s (was 8% for 1.5 s); the Keg hits two of the dead at most,
  0.5 m more reach a stack (was 1.5 m); Leech Rounds heal from a store that
  refills at 5% of health a second; a level gives 30 health (was 15); the
  Breacher takes 35% less harm (`Armor()`, Kevlar 6% a stack, 60% at most).
- **Results** (median seconds lasted, 16-minute cap, quartiles in brackets):
  Gunner 449 [409–766] to 564; Engineer 695; Breacher 185 and then, with the
  dash through the dead, 217 [178–356], and with the shotgun that doesn't
  load, 575 [450–915]; Gunner and Breacher together 708 [534–807]. Items came
  1.2 to 1.4 a minute. Starting with two Mortars takes a Gunner to 885 s (two
  Pocket Kegs, 715 s): the Mortar is still the strongest item.

### No more flying dead; a shotgun that doesn't load (2026-10-17)

The owner, with a screenshot of a knot of the dead in mid-air: "zombies
sometimes just fly lol ... i dont mind them crowding on top of each other what
i mind is them just flying"; "the shotgun shouldnt need to reload, we already
are slow"; "the shotgun SFX needs to feel more impactful".

- **What held them up** (zombies.tide's `Hunt`): two of the dead about as
  high, each climbing the other at once, leapfrogged up into the air with
  nothing under either. Now one climbs only onto one lower than itself, and
  of two about as high (within 0.12 m) only the one later in the herd climbs.
  And what holds one up has to be under it: within 0.6 m and 0.25 to 0.88 m
  lower (`props`), two of them once it's more than one and a half bodies up,
  so a pile spreads at its foot as a heap does; with too few under it, it
  comes down at `FALL_SPEED`.
- **Test**: through the piling (a horde of 500 at each high spot), every tick,
  each of the dead up off the ground and not falling has another of them
  within 0.7 m and 0.2 to 1 m lower, or a cliff beside it (clawing at its
  face), or isn't so for more than a sixth of a second (`lone`, a tick's lag
  of the herd's places allowed). The leapfrogging pairs failed it.
- **The shotgun** never loads (`WeaponInfo.bottomless`: it fires as long as
  the trigger's held; the HUD shows its name alone). Its sound is the
  recording over the cannon recording's low end (low-passed at 160 Hz) and a
  hard low thump, pushed into the limiter (`make_sfx.py`'s `shotgun_fire`),
  and a blast shakes the eye harder (0.5, was 0.3).

### Gore: blood soaking the ground, pieces flying (2026-10-17)

The owner: "i also want to add more gore, more blood, body parts flying,
maybe decals of blood if we can render a shit tone of them or maybe we just
draw to a texture that overlays the terrain". The texture it is (`gore.tide`,
this machine's alone; picks mine, Provisional):

- **The stains**: `Lens.stains`, an Rgba8 target 2048 texels across the
  NAV area (112 m: about 5.5 cm a texel), seen straight down by an ortho
  camera (`look.toStain`), never cleared but when it's made and at each new
  run (`RunBegan`). A target keeps what's drawn into it from frame to frame
  (`docs/spec.md`), so each splat is drawn once, the frame it lands
  (`Soak`, at `Layers.Shadows`, before the scene: alpha-blended instanced
  quads, `SplatAt` and `SplatBlood`, a ragged blot darker in the middle with
  drops round its edge), and costs nothing after. `Earth` reads it at the
  ground's place and lays its colour over the soil, with a wet shine.
- **What stains**: a hit, drops under it (and a smear the way the blow went
  on a head shot); a kill, a pool where it falls and four sprays thrown on
  the blow's way (stretched 1.5 to 3 times along it); every flying piece a
  drop each 0.07 s and a smear where it comes down. Up to 4000 splats a
  frame.
- **Pieces** (`Gib`): a kill by a blow (not burning or bleeding) throws 3 or
  4 (a burst, 6): a limb (skin), a lump or a scrap (flesh), the head too on
  a head shot; thrown on along the blow with some up and some scatter,
  tumbling, bouncing (the ground's `Ground`), then lying `GIB_SECONDS`
  (22 s), sinking away their last 2. At most `GIBS_MOST` (600), the oldest
  going. Drawn as boxes in one instanced draw (`Meat`: mottled raw red over
  the piece's colour, a wet shine fading as it dries over 12 s).

### The grip, turning in place, and the dash for everyone (2026-10-17)

The owner: "the gun still doesnt properly sit in the player's hands"; "looking
around for example could not move the legs until you move too far and then
they also move"; "i want movement to be more part of the experience. i like
the dash, the other 2 characters feel like shit, i think they should all
share the dash and maybe dashing through enemies pushes them or makes them
fall". The numbers are mine (Provisional).

- **The grip** (`PoseSurvivor`): the right hand turned down the grip's right
  side (its knuckles down and a little forward, its thumb over the left), its
  fingers closed round it (`CurlRight`: each finger joint 1.1 rad about the
  hand's own x, the first joints most), and the gun put through the palm:
  0.6 of the way from the wrist to the knuckles, `GRIP_IN` (2.8 cm) toward
  where the fingers close, its origin (the top of the grip) `GRIP_UP` (5 cm)
  above. The gun had been laid at the knuckles' line, so the grip stood out of
  the back of the hand. The left hand is palm up under the handguard, its
  fingers closed up round it (`CurlLeft`, 0.7 rad, the other way). The
  hands' bones are named in `Rig` (Index2..4, Middle2..4, Ring2..4,
  Pinky2..4, Thumb2, each side).
- **Turning in place**: the feet keep a facing of their own in the world
  (`Survivor.feetYaw`, local). Moving, it turns toward the way they go as
  before. Standing, it stays put while the aim turns over it, the hips and
  spine taking the twist, till it's past `STAND_TWIST` (1 rad); then the feet
  step round to under the aim at `STEP_TURN` (4.5 rad/s) with the walk clip
  blended in short and quick.
- **The dash** is every survivor's Shift (`SkillOf`: the Breacher's Leap and
  the Engineer's Blink are gone from their kits; their code stays, unused).
  Two charges (`Survivor.dashes`, `DASHES`), each back `Cooldown` (3.5 s, less
  with Field Batteries) after it's spent, one at a time (`dashWait`); 0.3 s
  between dashes. 18 m/s for 0.26 s. Going through the dead it knocks each it
  comes level with off its feet and flings it aside off its line
  (`DashThrough`: within 1.1 m, not one already down; a Blunt blow of 30 x
  might); a Brute is shoved, not knocked over. The eye widens in a dash (up
  to +9 degrees, eased), and dust kicks up behind it (a streak of air behind it
  went: the owner, "the dash line VFX is bad").
- **Test**: two dashes then none, the first back after the cooldown; a dash
  through one of the dead knocks it down and hurts it; Field Batteries
  shorten a spent dash's wait to 2.975 s.

### No power-ups; chests found, not shown (2026-10-17)

(Chests are a set a night since Phase 3, above, not on a timer.)

The owner: "that ability that kills everything should maybe go; in general i
dont like the drops from zombies being random abilities. lets make the chests
be random, but not be pinged; players should discover them by roaming
around".

- **The dead drop nothing but orbs.** Recharge, Death's Touch and Holy Fire
  are gone: the `Drop` component, its pick-up, the HUD's skull, the banner,
  their sounds and hum (the `sfx` files stay, undeclared, so unpacked), and
  `Match.instakill`, `drops`, `dropMinute`, `dropTurn`. `drops.tide` keeps the
  cues and `Refill` (a survivor back at the fire).
- **Chests** (`chests.tide`): `CHESTS_START` (4) stand about the clearing as
  the run starts, one a tick at random open places 16 m apart and 14 m from
  the survivors, then another every `CHEST_EVERY` (35 s), up to
  `CHESTS_MOST` (7). Nothing points to them: no beam, no HUD marks, no line
  in the corner. Each has a dim warm light on the ground round it (5 m) and
  a glint off its lid now and then, seen near. By one, "Hold E to open the
  chest" and its bar as before.

### Chests, and levels half as often (2026-10-17)

(Both replaced since: the chests by the owner's next note, below, then by
Phase 3's nights; the levels' cost by Phase 3's.)

The owner: "could be fun adding chests around the map with upgrades /
speaking of upgrades they are too frequent right now". Asked, they chose free
chests on a timer, and level picks about half as often. The numbers are mine
(Provisional).

- **Chests** (`chests.tide`): the first 25 s into the run (`CHEST_FIRST`), then
  one every 40 s (`CHEST_EVERY`), at most three standing (`CHESTS_MOST`); each
  where the ground and the cells round it are open (`Open`), at least 14 m
  from every survivor and 12 m from the other chests, in the clearing. Holding
  E within 2.5 m (`CHEST_REACH`; not by a friend who's down: raising wins) for
  1.2 s (`CHEST_SECONDS`) opens it: `Prying` sends twice a tick's share and
  `ChestShut` takes one back each tick, so letting go shuts it again, as
  reviving does. Its item is `RollItem`'s (as an offer's, by the run's
  minutes; `Offer` now rolls through it too), given at once with no choosing
  (`Given`, as F10's), and the chest is gone. Restarts clear them.
- Seen and heard: the kit's wooden chest, a little big and bright, shaking as
  it's pried; a warm light on the ground round it; a faint gold beam 9 m up
  and sparks rising, so one is found across the clearing at night; a diamond
  on the HUD where each is (kept on screen when it's off to the side or
  behind, with its distance); "Hold E to open the chest" and a bar by one; a
  line in the corner when one comes; a gold burst and the chest's sound
  (`sfx/chest_open.ogg`) when it opens, the item's name as the banner.
- **Levels** (`items.tide`): `LevelSpan` is as much as two levels took
  (`12 x 2.22 x 1.4884^(level-1)`), so a pick comes half as often; each level
  is worth two of the old: +24% damage (`Might`) and +30 health (`MaxHealth`,
  `Experience`). A Brute felled still gives everyone a pick.
- **Test**: a chest comes away from the survivors on open ground; E held 20
  ticks and let go shuts it again; held, it opens in about 1.2 s and gives one
  item. Tests that want a zombie on its feet take one on flat ground
  (`flat_ground`), and Death's Touch is cleared before the boot's check: the
  slower levels moved where the run is at those checks.

### The survivors' bodies: legs that go where they move, arms on the gun (2026-10-17)

The owner: "could we get some IK on the character and proper animations like
strafing etc". The picks are mine (Provisional).

- **Clips** (`art/people/tools/make_moves.py`, both catalogs): UAL1's
  `Jog_Fwd_Loop` (Jog), `Pistol_Idle_Loop` (Pistol_Idle), `Jump_Loop` and
  `Jump_Land`. Mesh2Motion's add-on set (CC0) has strafes and walking
  backwards, but they're slow walks (0.8 and 1.2 m/s against a survivor's 4.6
  to 6.8): played at speed their feet blur. So, as RoR2 does it:
- **Legs** (`people.tide`'s `PoseSurvivor`): a gun idle standing; moving,
  the Jog (the Sprint past 6 m/s going forward) by the metres walked over its
  stride, its whole body turned toward the way they go (`legYaw`, up to
  `LEG_TURN`, 75 degrees, eased), played backwards going back. In the air
  `Jump_Loop`, landing the first of `Jump_Land` for a third of a second. The
  abdomen, torso and chest turn back to the aim (40, 35, 25%) and bend to its
  pitch, the head with them.
- **The gun** is placed, not carried by the clip: from the right shoulder
  along the line to what it's aimed at (over the shoulder `AimedAt`, else 30
  m down the look), its top up; kicked back 7 cm by a shot, pulled in and
  dipped for a reload. `Survivor.held` (local) has it; `DrawCarried` draws it
  there and keeps its muzzle for the tracers.
- **IK** (`Reach`, `AimHand`, over fp.tide's `TurnBone`): the right arm
  reaches its wrist just behind and under the grip and the left under the
  barrel (`ForeGrip`: 0.2 m ahead of the grip, the shotgun's 0.24, the SMG's
  0.14; the pistol's grip cupped; the magazine while loading), each elbow
  bent toward a pole down and out. Then each hand is turned so its middle
  knuckle goes round the gun and rolled about it so its thumb is up the
  barrel: the support hand's palm up under the handguard. These bodies'
  arms are short (18 cm upper arm, 24 cm forearm), so the gun is held close.
  A throw and the boot keep the clip's arms and the gun in the hand.
- **Shots** still leave from the match's `GunHand`, now where the drawn
  barrel is: 0.12 m right, 1.36 m up and 0.08 m ahead of the feet, then
  0.45 m along the look (it swings with the pitch).

### The dead climb, and nowhere is out of their reach (2026-10-17)

(`CLIMB_COST` went to 1 with the piles, below, and is 5 for a step up a face
since Phase 3, above.)

The owner: "I can cheese a lot of it by staying at the highest point since
they can only come from one direction. we either work it through map design
or we allow zombies to stack on each other and come from all direction. also
if i get to the house in some spots everyone just stops moving". I picked
climbing (Provisional): map changes alone still leave choke points.

- **The chapel's rise was cut off from the clearing.** A ramp's height
  blended toward the ground under it, which at the rise's rim was still the
  rise's own cliff, so a band of each ramp near the top climbed more than
  `STEEP` (0.6 a metre) and was walls to the dead. A survivor on the rise
  (the ruin, "the house") was out of the field: every one of the dead
  stood still. `RampTo` now takes `below`, the ground before the rise
  was raised, and stays at or above the rise (`Math.Max`), so the slope is
  smooth to the top. The spire's ramps go the same way.
- **A survivor in a wall's cell** (a wall's box is grown 0.12 m into the
  cells, the chest's too) had the field start in a cell the dead never step
  into, and the ones beside it stood still. `Seed` starts the field from
  the cells the dead can go in within 1.5 m of them, with no box in between
  (not the far side of the wall).
- **The dead climb** (`ashwick.tide`): cells are open (0), walls (1) or
  cliffs (2), the ground steeper than `STEEP` inside the clearing.
  `Walkable` is anything but a wall; `Open` is flat ground, where the dead
  rise. `Spread` counts a cliff's cell as `CLIMB_COST` (3) steps, a cell
  walked again when a shorter way turns up. Going up a cliff, a step is cut
  short so they rise no faster than `CLIMB_RATE` (1.3 m/s, a sprinter half
  again; `Climbed`), the Brute the same. Climbing, they show the middle of
  the rise clip, clawing, over and over (`Zombie.climbing`). Survivors still
  can't climb (`Passable`), so high ground is worth having, but the dead
  come up every side of it, slowly, while they're shot.
- **Test**: survivors outside the ruin's north wall, by the chest, in the
  ruin, on the spire's tier and on the beacon's top; at each the dead reach
  them (all climbing, 5 to 12 s). The run's own Brute is held off meanwhile.

### Phase 2: the PSX look, guns, kits and items (built 2026-10-16)

- **The look** is under Rendering.
- **The guns** (`weapons.tide`, `art/guns`): Quaternius' Ultimate Gun Pack
  (CC0), four of its forty cut to GLBs by `art/guns/tools/make_guns.py`
  (recoloured to a palette as the pack's preview shows them, scaled, the grip
  at the origin, `guns.json` giving each muzzle), plus the pack's tripod for
  the turret. Every round is a ray (`Shoot`): the walls and ground by
  `LongRay` (RayHit two metres at a time, as RayHit looks only in the bucket
  at a segment's middle), the dead by `Along` (the herd's cells within a
  lying body's length of the segment, row by row), nearest first, through as
  many as the gun pierces. Magazines only: there's always more ammunition.
  The shotgun loads a shell at a time and a shot cuts its loading short.
  Down, a survivor fights on with a 12-round pistol; their gun's rounds are
  kept (`stowed`) for when they're up.
- **The kits** (`kits.tide`), picked before the first wave: the Gunner
  (carbine; Phase Round, Slide, Frag Grenade), the Breacher (pump shotgun;
  Boot, Leap, Firebomb), the Engineer (SMG; Mine, Blink, Turret). Skills are
  on right mouse, Shift and Q (the pad's left trigger and shoulders), fire as
  soon as they're ready while held, and have their own cooldowns (3 to 18 s).
  Sprinting went: a survivor runs whenever they go forward and haven't fired
  for 0.35 s, as RoR2 can. ADS, the stab and the kick key went; the Boot is
  the Breacher's.
- **The items** (`items.tide`): sixteen (24 since Phase 3), stacking, in three
  rarities; three offered after each wave (uncommon and rare more often as
  waves go on).
  Damage, fire rate, health, speed, leech, cooldowns, magazine, crits; Live
  Wire (hits arc), Powder Keg (kills burst), Gun Drone, Ember Ring, Frostbite,
  Spring Boots; Mortar Beacon (shells called down), Second Wind. A hit's
  items roll in `Land`, at the shooter, scaled by the gun's proc coefficient
  (a shotgun pellet 0.3); what an item sets off is marked `echo` on its
  Wound and sets off nothing more, so kegs don't chain.
- **Levels**: every kill is the party's experience (`Match.xp`, `level`; 12
  for the second level, ×1.22 each after); each level is +12% damage and +15
  health for every survivor (`Experience` copies the level to each).
- **Numbers** (`gravewake_perf`, release, this machine; it now has both
  survivors Gunners firing into the horde with two drones, an ember ring and
  Live Wire each, the dead never dying): 1000 of the dead 0.62 ms a tick,
  2000 1.19 ms, 3000 1.82 ms: the guns and items cost next to nothing over
  the horde itself.
- **Sounds**: the guns' shots are cut from The Free Firearm Sound Library
  (CC0; AR-15, Mossberg, PPSh, 1911), their echoes short and a little of the
  wood's reverb in their place; reloads, shells and the pump are Kenney's
  metal impacts (`sfx/tools/make_sfx.py`'s guns section). Hits are heard for
  kills, heads and this machine's own only; deaths come out of the dead's
  voice budget.
- **Gone with the medieval game**: the 33 medieval weapons, the cards, the
  brews, the Runeforge, the classes, abilities, the lure bell and its field,
  staffs and spells, bows and missiles that fly. `art/weapons` keeps its
  files; the game loads only the fire pot, the kick's leg and the cannonball
  from it now.

## How it plays

A run of nights in a forest clearing (Phase 3, above), an item at each level:

- **Survivors** (`kits.tide`), picked with 1, 2 or 3 before the run (nothing
  starts till every ready survivor has one, and a joiner picks on joining).
  Each a gun, a skill on the right mouse button, one that uses itself, and
  the roll on Space.
- **The run** (`rounds.tide`, `nights.tide`): night after night, each ended
  by its beacon's vigil and a dawn. The dead come all the time, more and
  tougher as the danger climbs with the clock and the nights, more for a
  party, at most 4000 up at once, most in hordes clawing up together 22 to
  32 m from a standing survivor, a quarter alone 16 to 32 m round (in a
  vigil, mostly round the beacon); a claw does 30
  (a Warden's 45) times `Harm(danger)`. Health comes back at 4% a second
  after 4 s unhurt. A night's bosses (the Mauler, the Broodmother, the
  Stalker, in turn) rise as its beacon's lit; an unlit night sends one every
  150 s (`boss.tide`); fewer of the dead come while one stands.
- **Kinds of the dead**: walkers; Bloaters from 40 s, Spitters from 75 s,
  Wardens from 2 min (Phase 3, above).
- **Levels** (`items.tide`): every kill leaves an orb (`orbs.tide`), the
  party's experience when one of them comes near; each level is an item to
  pick for every survivor, and the run waits till all have. Chests (`chests.tide`)
  give one at random.
- **The score** is the nights kept and the time; the HUD counts each one's
  kills (there are no points).
- **The dead** walk a flow field to the nearest standing survivor and claw.
  Heads take three times (a shotgun's pellet half again); a boss's weak spot
  takes its own (two to four times). Fire burns for 3 s,
  frost slows for 3 s and a second frost while slowed freezes solid (and
  Frostbite freezes outright), shock stuns for 0.6 s, a blast throws. A boot,
  a dash, a slam and a grenade's middle knock them over; one lying takes half
  as much again and is stamped on by the boot.
- **Headshots**: a gold hit marker (red and bigger for a kill), a crack and a
  ding, a gold number, HEADSHOT over the crosshair and a jolt for a kill; a
  crit's marker is yellow; one a Warden's mail turns is grey. Only hits of the
  survivor's own hand mark (above, 2026-10-20).
- **Down and out.** At 0 health a survivor goes down for 30 s and fights on
  with the pistol, unless a Second Wind puts them straight back up (one used
  up). Raised by a friend standing by them for 3 s. Who bleeds out is back beside
  the beacon when it's lit, or at dawn. When nobody is left standing (alone,
  going down), the game is over.
- **No power-ups** (since 2026-10-17, below): items come from levels, the
  nights' bosses and chests.

## Contracts

- **The match** (`game.tide`, `rounds.tide`, `nights.tide`, `kits.tide`,
  `items.tide`, `orbs.tide`, `chests.tide`, `boss.tide`, `zombies.tide`,
  `hands.tide`, `missiles.tide`, `drops.tide`, `ashwick.tide`) is
  deterministic and ordered by `enum Tick { Restart, Survivors, Hands,
  Missiles, Zombies, Field, Rounds, Late }`. `Random` is the match's (the
  items' procs and offers, the spawns, the beacon's place roll it; rolls are
  drawn into locals before a call uses them); views roll their own local
  `Random` (`Fx.random`). Dawn's staggered fall is a hash of where each of
  the dead stands, not a roll.
- **The phases**: `Waiting` (picking, then `START_SECONDS`), `Fighting` (a
  night, `Match.night`, its own clock `nightClock`, its beacon lit or not:
  `Match.vigil`), `Dawn` (`DAWN_SECONDS`, nothing rises, then `Nightfall`),
  `Over`. `Match.kept` is the nights kept, the score with `lasted`. The
  pause (`Pause`) holds `Fighting` and `Dawn` alike.
- **A night's things carry its number** (`Beacon.night`, `Chest.night`, a
  boss's `night` for its blows): `Keep` and `ChestShut` destroy what isn't
  tonight's as the next falls; `Restart` clears them all.
- **Ashwick Wood is one function**, `MakePlan()` (`plan.tide`): the match
  keeps its solids, risers and starts (`Ashwick`, built at `Spawned`), and
  each machine calls it again for what it draws (`Village`, a local
  singleton, because local handlers can't read the match's lists). Change one
  and both follow. The clearing's edge is `Reach(angle)`, about 38 to 50 m
  from the fire; the forest past it is drawn but nobody's ground. The five
  beacons' braziers are the plan's (`BeaconPlace`, nights.tide, from the
  landmarks' constants), each with a box.
- **The ground is one function too**, `Ground(x, z)` (`plan.tide`): a
  gentle Perlin roll laid flat round the fire and under the ruin, and a rise
  into the trees past the edge (it was hills, a rise, a spire and a ravine
  till 2026-10-18, above). Nothing of it is stored but each nav cell's middle as bytes
  (`Ashwick.heights`, for the dead's way): the match asks it where feet, the
  dead, drops, rounds and grenades meet the ground, and the picture draws it
  as a 193 × 193 heightfield at 1.25 m (`MakeShapes`), with rock where it's
  steep, casting the moon's shadows. Survivors can't walk up more than 0.75 m
  a metre (`Passable`); there's no jumping.
- **Collision** is boxes in 8 m buckets, on the ground: circles pushed out,
  standing on the highest top within 0.45 m (the ground's or a box's). Props,
  tree trunks, the ruin's walls, the boulders, the braziers and the beacons
  have boxes.
- **Navigation**: a 1 m grid, 156 × 156 cells from (-78, -78) (a byte field
  reaches 254 steps of path); cells past the clearing's edge and every cell a
  box taller than a step overlaps are walls; where the ground climbs more
  than 0.6 a metre (`STEEP`) is a cliff. Every six ticks `Wayfinding` spreads
  a breadth-first field from the standing survivors' cells: a step 1, a step
  up a face `CLIMB_COST` (5). Each zombie steps downhill, or straight at its
  target when the path is clear and it's within 14 m. One of the dead already
  in a wall's cell (risen or thrown there) may walk out of it. The soak test
  checks that every one of the dead but the Spitters, who hang back, reaches
  the survivors.
- **Seats**: `Party` holds four seats (0 Wat, 1 Maud, 2 Godric, 3 Agnes).
  A joiner takes the first free seat; a leaver's survivor is destroyed.
- **Items**: `Survivor.items` (a count of each, `int[24]`), `offers` and
  `choosing`, `pending` (picks owed). `Survivor.role` is `None` till a
  survivor is picked; `Fight` does nothing until then. `Choose` (at
  `Tick.Survivors`) takes one on `pick1`..`pick3` and sends `Picked`. What a
  round's items do goes through `Arms` (`ArmsOf`, or a turret's copy) and
  `Land`.
- **Skills**: `Survivor.cooldowns` (secondary, roll, special); in `Fight`,
  before the gun, `StartRoll` on the roll's press, `UseSecondary` while the
  right mouse button's held, and `AutoSkill` for the special. None takes the hands. The roll's charges are `rolls`,
  `RollCharges` at most.
- **Missions** (missions.tide): `Match.mission` and its fields, run by
  `Missions` at `Tick.Rounds` + 3; a cull's kills and a hunt's elite counted
  in `TakeWound`; rewards through `Owed`.
- **Commands**: `Restart` (Rooms' New game, Play, or use on the game-over
  screen) sets `Match.restarting`; the `Tick.Restart` systems clear the dead,
  missiles, turrets, bosses, orbs, chests and the beacon, re-outfit the
  survivors and reset the match (command handlers can't query). `Cheat` is
  the development keys (3 a horde, 4 a level, 5 an item, 6 a boss of any
  kind, 8 the night kept, 10-17 the places in turn, 20-22 six of a kind of the
  dead, 30-32 a boss of a kind).
- **The herd** (`Herd`, zombies.tide, rebuilt each tick): every hittable
  body (the dead's `Muster`, the bosses' `BossMuster`) in a grid of cells for
  `Near` and `Along`; a boss's hit spots apart in `Herd.bosses`
  (`Body.boss`, `SpotsOf`).
- **Inputs**: `ready` is `[Absolute]`; `move` is across and up the screen;
  `aim` is what the cursor's on (eye.tide's `Pointing`, `Me.pointed`, moved
  by the mouse only while the cursor is locked); the rest are held bools
  read with `.down`.
- **What's local**: the camera (`Me`, from above, watching a friend while
  bled out; the zoom in `Feel`; the cursor), the
  plan's copy (`Village`), the kit's placements (`Kit`), poses, walk clocks,
  hit flashes and voices (`local` fields), the night's light (`NightLook`
  into `Look`), the feed, effects and sounds. Hits, kills, shots (`Loosed`,
  and a `Traced` line for each round), skills, picks, the nights' turns
  (`NightFell`, `VigilBegan`, `DawnBroke`) and cues reach views through local
  handlers.

## Art

- Ashwick Wood is Quaternius' Stylized Nature, Medieval Village and Fantasy
  Props MegaKits (CC0), split by texture into 63 parts of 40 pieces
  (`art/kit/tools/split_kit.py`, `pieces.txt`; trees and bushes decimated)
  with 19 textures shrunk for the web and darkened by the kits' occlusion
  (`make_textures.py`). Each part draws instanced with its texture. The
  nature kit's vertex colours are wind weights, not colour (its trees had
  come out red), so `split_kit.py` drops them for its textures; the twisted
  trees' leaves (the kit's red variant) are its white mask tinted an autumn
  olive. Placed by `MakePlan`: 96 trees close round the edge and 110 more out
  to 60 m in the fog, bushes, ferns and plants along the edge, grass and
  mushrooms over the clearing, the ruin's stone walls (`StoneWall`, with
  fallen segments) and floor, the graveyard's iron fence, the camp's props.
  The ground is the `Earth` frag: loam, moss, drifts of fallen leaves
  thickening toward the trees, trodden bare round the fire.
- The village (2026-10-16, replaced the same day by the forest): the notes
  below on its doors, windows and well are its history. The items it alone
  used (the chest, the beacon's lever, the window planks, the coin and hammer
  power-ups, the well) are no longer made or packed (`art/items/README.md`);
  `sfx/` still has the door, board, chest, coin and forge sounds, which the
  game no longer declares, so they aren't packed.
- The bosses (`art/mutants`, 2026-10-18): three of the dead reshaped into
  mutants by `make_mutants.py` and baked as the horde is (above, After the
  third playtest; `art/mutants/README.md`).
- People: six of the dead and four survivors from Ultimate Modular Men and
  Women, recoloured in Blender (`art/people/tools/make_bodies.py`); clips from
  the men's pack and Universal Animation Library 1 and 2, retargeted onto
  both skeletons (`make_moves.py`, two catalogs, the same clips and timing).
- Weapons and items: Quaternius' Medieval Weapons and Ultimate RPG where they
  have one, the rest modelled from primitives by `make_weapons.py` and
  `make_items.py` in the same flat-coloured style. `weapons.json` holds each
  model's grip, tip and length.
- First-person arms (`art/fp`, `fp.tide`; the owner, 2026-10-16: "the weapons,
  maybe we try to find some proper animated FP weapons? your job is meh"):
  CoinCoin's first-person kit from OpenGameArt (CC BY 4.0, credited in
  `art/CREDITS.md` and on the title screen), its bow, tomahawk and knife files
  joined on one skeleton by `art/fp/tools/make_fp.py`, recoloured flat, with
  clips made on the same rig for what the kit lacks. Every weapon goes
  through it (`Grasp`): one-handed blades, axes, maces and thrown axes on the
  kit's tomahawk clips; two-handed ones on the same paths with the left hand
  added; polearms, crossbows, the handgonne, the hand crossbow, staffs,
  javelins, a pot tossed and a brew drunk on made clips; the kit's own bow
  (mirrored into the left hand), arrow, draw, hold, loose and nock; the
  kit's knife stab for V. A weapon model sits on the rig's `weapon` bone
  (`Seat`). Blows land when the clip's do (`StrikeTime` maps the act's hit
  time onto each clip's impact). Aiming a crossbow brings its line
  under the eye (held.tide, below) instead of a clip. The old procedural fists, forearms and
  swing keyframes are gone. A survey of free first-person packs found nothing
  better that may be redistributed (Fab/Unity/Mixamo licences, itch.io behind
  a challenge); the kit is semi-stylized, a little more detailed than
  Quaternius' style.
- The bow, after the owner's note ("the bow gets in your face too, its hard
  to aim with it since you cant see anything"): the kit's bow is held to the
  left and drawn to the cheek, the crosshair clear at full draw.
- The owner's notes on the arms (2026-10-16, with screenshots: "the sword is way
  up, it should be lower"; "the crossbow is a better example, yet i dont
  understand how we are holding it"; "why did the bow become left handed"):
  - A one-handed weapon is held lower and further right than the kit's
    tomahawk, leaned out (`GraspShift`, `GraspTurn`), and a long blade is
    tipped forward in the fist (`BladeSeat`, 38 degrees; two-handed 28) so it
    reaches away from the eye instead of up across the picture.
  - The bow is no longer mirrored: it's in the right hand, as the kit has it,
    on the right of the screen.
  - Crossbows and the handgonne are held by the stock as a pistol is, the fist
    round the grip, the barrel ahead (`make_fp.py`'s X and H clips), the left
    hand under the barrel. The importer mirrors x, so the weapon bone's ahead
    is its -x (`Seat`); the barrels had pointed back at the eye.
  - Head hits follow the animation (the owner: "headshots dont follow the
    animation"): `Brace` (a match system, `Tick.Zombies` + 1) poses each of
    the dead as the picture does (`ZombiePose`, shared with `PoseZombie`) and
    keeps its head, chest and hips (`Zombie.headAt`, `chestAt`, `hipsAt`), which
    `BodyOf` gives blows and missiles. A hunched walker's head is about 1.4 m
    up and 0.35 m ahead of its feet, where the fixed point had been 1.62 m
    straight up; a fallen one's is on the ground. The pose is sampled from the
    game's assets, so the C test now loads the pack.
- Aiming, after the owner's next notes ("the weapons that shoot dont really look
  like they are aiming towards the center of the screen, specially when aiming
  down sights"; "the foot (kick) animation, it needs to come further out the
  screen"): a crossbow's or the handgonne's barrel line is turned each frame
  onto the point 30 m down the crosshair, and when aimed brought under the eye
  on the eye's way (held.tide, before sway and bob are added). A drawn bow's
  arrow (the game's arrow model, on the kit's `fleche` bone) is laid from the
  cheek onto the crosshair the same way. Blades are seated turned and tipped
  away from the eye (`BladeSeat`), staffs too; a one-handed weapon's empty left
  arm is kept down (`TurnBone`), as the kit's swings flung it across the
  picture. The kick's boot comes up higher and nearer, 15% bigger. The
  fireball's smoke starts once it's 3 m from the eye.
- Kills blow the dead apart (the owner: "lets maybe make it explode; i like
  the head exploding, so maybe if I kill it lets make the piece of it that im
  shooting do the same making the other pieces sort of separate and
  ragdoll"): a kill by anything but fire or bleeding bursts the piece struck
  (the head on a head hit, else the trunk) in a cloud of blood and bits, and
  the other pieces (of head, trunk, two arms, two legs) fly off from the blow
  and from each other, tumble, bounce and lie till the body's gone (people.tide
  `Gib`, `Tumble`, local only). Each piece is the whole skinned body drawn with
  its death pose and every other bone squeezed to nothing about its joint
  (`PartPose`, `PlaceBone`), so it ends in a stump; no extra meshes. A blast
  throws them twice as hard.
- Validation of the arms (2026-10-16): native debug `gravewake_test` and the
  web debug WASI test pass; the web page builds. Every weapon was looked at in
  standard-host captures (idle, mid-attack, aimed or heavy). The browser tests
  weren't run: the owner asked to stop running them for this game.
- Sounds: 144 Ogg Vorbis files from CC0 recordings (Kenney, OpenGameArt) and
  synthesis, cut and mixed by `sfx/tools/make_sfx.py`; sources in
  `sfx/CREDITS.md`. Buses Effects, Music and Ambience under a master limiter
  (`audio Limit`, ceiling 0.86).
- Thumbnail: a two-process `--players 2` run, frame 960, Maud in the square
  with the dead coming through a window (`docs/static/prototypes/gravewake/`).
- Late polish (2026-10-16): the wall torches got their models (the flames had
  burned in mid-air), the chapel sells grave bells (Q's bell existed but
  nothing gave one), and the Runeforge's bleed and a second frost blow's
  freeze do something (bleed did nothing beyond damage; nothing set
  `frozen`).
- The map, after the owner's first play (2026-10-16: "doors are rotated weird
  or sections have holes that make no sense"):
  - The kit's door (`Door_4_Round`) is hinged at its origin and spans 1.1 m
    to its left (the importer mirrors x), so a gate's leaf goes at
    `gate.at + along * 0.55`; it had sat beside its arch, half over the wall,
    and from the far side the arch looked empty. Doors and iron gates turn
    about a hinge as they open (`Put.swing`, `Put.pivot`), into the gate's far
    side; the iron gates had risen 3.3 m and hung in the air.
  - House doorways that aren't gates have a shut door (`ShutDoor`), and every
    window that isn't a barricade has the kit's frame and glass (a quarter of
    them lit warm, `Put.glow` read by `KitGlass`) or closed shutters: they had
    been dark holes into the houses' empty insides.
  - Free-standing walls (`StoneWall`) had their two pieces back to front, so
    both faces showed plaster; both now show stone.
  - Two real holes, on the smithy's north side: (-12..-10, -14) led out of the
    village, and (10..12, -14) round the stable door into the stables. Walls
    close both, and the pocket behind the second.
- F10 (development) puts the next weapon in hand, to look at each in turn.
- The owner's next notes (2026-10-16): an invisible collider in the middle of
  the square (the well had a box and no model: the square now has a modelled
  well, `art/items` `well`), and props you walked through (stalls, the cart,
  barrels, crates, benches, chairs, the anvil and the weapon stand now have
  boxes from their kit footprints, `PlaceSolid`). The test also found the
  tavern's north-east window landing the dead in the bar's barrels, where
  they stuck for good; the barrels moved, and the test now checks every
  window's landing and every grave against the solids.
- Validation of these (2026-10-16): native debug `gravewake_test` and the
  web debug WASI test pass, with the new checks (window landings and graves
  clear of solids; a kick knocks one down, a stamp lands, it gets up; a
  maul's heavy blow knocks one down); `gravewake_play_webgl` (330 s) and
  `_webgpu` (273 s) pass. Looked at in standard-host captures: every gate
  shut and open from both sides, the eight areas in four directions, the
  well, the bell stall, the kick, a two-player fight with knockdowns.

- The roguelike's validation (2026-10-16): native debug `gravewake_test`
  (36 s) and the web debug WASI test (48 s) pass, and the web debug page
  builds. The browser tests weren't run (the owner asked to stop running them
  for this game); `test/browser.mjs` was brought up to date (the cards, the
  walk round the clearing) and checked to parse. Looked at in standard-host
  captures: the title's circle round the fire, the clearing from eight
  places, overhead, the cards, the first wave, and every weapon in hand
  idle, mid-attack and aimed or heavy (unchanged from the last round). The
  thumbnail is a one-player run looking north across the fire, frame 800
  (since retaken on the hilly clearing: a Warrior south of the fire, the
  ruin's cliff behind it, frame 1400 of the fourth wave).

- Readability (the owner: "the contrast between the environment and the
  zombies makes it hard to read"): the dead have a pale green-white rim
  (`Skin.ghost`) added after the fog, dimmed only slowly with distance, so a
  shape among the trees reads as one of them; the moon is a little brighter
  and the fog thinner (0.015) for the wider clearing.
- Validation of the classes, terrain and hordes (2026-10-16): native debug
  `gravewake_test` (40 s) and the web debug WASI test pass; release
  `gravewake_perf` as above. Looked at in standard-host captures: the class
  screen, a leap, the cards, the ruin's cliff and ramp, the hill, the hollow,
  a fifth-wave horde. The browser tests weren't run, as the owner asked;
  `test/browser.mjs` was updated and checked to parse.

## Rendering

- **The PSX look** (2026-10-16, the owner's "PSX low poly style"; pixels by
  distance since 2026-10-17, above): the scene drew into an `Rgba16f` target
  270 lines high (`LINES`), its width the window's shape, no multisampling,
  and `Develop` read it back with `Filter.Point` so its pixels stayed sharp. Every world draw (the ground, kit,
  dressing, survivors and weapons) has the `Wobble` vert:
  each corner's clip position snapped to the low picture's pixel grid
  (`Snapped`, `look.low`), so still things shimmer a little as the eye moves
  and moving ones wobble. The horde's `Shamble` snaps the same way. `Develop`
  ends with a 4 by 4 ordered (Bayer) dither in the low picture's pixels and
  32 steps a channel. The kit's textures are `Filter.Point` (no mips). The
  bloom works at half the low picture. The HUD stays at the window's size.
- `Develop` brings the scene to the window with ACES, bloom (a half-size
  bright pass of the low picture and blur), the hurt and downed reds and the
  game-over fade.
- Light: a moon with a 2048-texel orthographic shadow map following the
  camera (snapped to its texels), a sky dome, fog, and up to 28 point lamps
  nearest the camera: the ruin's two torches (`WallTorch`), the two fires'
  braziers (always burning), fireballs and burning dead.
- Kit placements are culled by distance each frame; what stands between the
  eye and this machine's survivor isn't drawn (`InTheWay`, above). The
  camera is above (`TOP_HIGH` 11.5 m over the feet, `TOP_BACK` 7 m south,
  `TOP_FOV` 52, the wheel's zoom 0.6 to 1.6). The first-person arms and
  their pass are gone (2026-10-18).
- Behind the title, the camera circles the fire at 6.5 m, looking past it
  into the trees.

## Tests and validation

- `gravewake_test` (native and WASI, `test/test.c`): two players through the
  real input: the risers and starts clear of every box and on walkable
  cells; nothing starting till both have picked a survivor (a Gunner with the
  carbine's 30 rounds, a Breacher with the shotgun's 8); the first night: its
  beacon cold, its seven chests; the dead shot down with the
  carbine for a minute and a half (kills, the party's level climbing for
  both); three items offered each, one taken by each, the run held still
  while one is still choosing; items taken through the input (Kevlar's
  health, an extended magazine loaded at once, a Second Wind, an Energy
  Drink's third dash, and every other), a Field Battery's shorter cooldown;
  a Second Wind putting one straight back up;
  the Breacher downed, the pistol firing from the ground with the shotgun's
  rounds kept, items kept, raised by the other with the shotgun back as
  loaded as it was; a carbine round and a shotgun blast into one of the dead
  from the gun to the aim, and at the legs; from above, turning to the
  cursor, moving up the screen, a grenade bursting at the cursor; a horde
  rising in its patch; every skill (a phase round, the dash
  twice and a third time through one of the dead, a frag; a boot, a
  firebomb's burn, the boot's stamp; a mine laid and set off by the dead, a
  turret that fires and then goes); Frostbite freezing and the Mortar Flare
  calling a shell, Live Wire arcing to another of the dead, a Powder Keg
  kill hurting the one beside it, the drone and the ember ring wounding
  without a shot, the drone holding its fire through two seconds of a pick's
  pause (the owner thought it kept killing while paused, 2026-10-20: the
  dead's health and its life don't change); wading slowed by three
  of the dead; a Bloater's swelling and burst, and one shot dead from afar;
  the Spitter, the Warden and the newer items (Phase 3, above); a chest
  opened, and one let go shutting again; orbs lying, taken from 4 m, gone
  after 40 s; the Mauler's slam, charge and spent stand, its weak spot
  against its hips, and one of the night's own felled owing each a pick; the
  Broodmother's volley and birth; the Stalker's leap, its shriek's frenzy,
  and one cut short by its throat; then both holding the fire for three minutes,
  shooting the nearest, taking the third item at each level (every one of
  the dead but the Spitters reaches them: the longest any hunted is 32 s of
  180 on the flat map); a night kept (Phase 3); then the match from a snapshot and
  through its packed bytes hashes the same. In the soak a crowd's claws can
  land together for more than anyone's health, so it keeps them whole.
- `gravewake_play_webgl` / `_webgpu` (`test/browser.mjs`, label
  `private-prototype`, `webgpu`, run serially): the page in headless Chrome
  with frame control and `checkGL`: the title, Play, a Breacher, the first
  wave, a firebomb, a blast and the boot, the wave ended by F5 and an item
  taken, then a walk round
  six places in the clearing, each a different picture, with no runtime,
  shader or GPU errors. Not run since the owner asked to stop (2026-10-16). Software WebGL is
  slow on this page, so the test's frames have per-frame deadlines and a
  900 s limit.
- The browser test replaces `requestPointerLock` with a promise that never
  settles: while it ran with the real one, the owner's mouse was held twice
  (2026-10-16), which stopped when the test's Chrome was killed.
- Validation (2026-10-16): native debug (configured with
  `TIDE_PRIVATE_PROTOTYPES=ON`) builds `gravewake` and `gravewake_test` with
  no warnings and `gravewake_test` passes; web debug builds `gravewake_page`
  and passes all three (`gravewake_test` under WASI 13 s,
  `gravewake_play_webgl` 398 s on SwiftShader, `gravewake_play_webgpu`
  300 s), with Node 24.21.0. The docs production build, `svelte-check` and
  the 19 analytics/admin/production HTTP checks pass. Looks were checked from
  standard-host captures: every area, the torches, the bell stall, the title's
  whole circle, a two-player run. The full native and web suites were not
  run (only this game, its catalog entry and notes changed). Not run:
  Linux/macOS/MinGW, Android, sanitizers, a live Internet room. No CI.

## Gaps and engine notes

- No touch controls: a phone can open the page but not play; cards are taken
  with keys or the d-pad only (the cursor is locked in play).
- Not measured: frame cost on a real GPU in a browser with the forest's
  trees (the owner is testing it), live play over the Internet with four,
  gamepad feel, host migration mid-night, the balance with people rather than
  the bots (who don't dodge a boss's slam, charge or leap, or dash, and find a
  high beacon only by the ramps they're told of).
- A late joiner picks a survivor and is owed a pick for every level the
  party has, offered one after another, the run waiting.
- What the engine made awkward, worked round in the game:
  - tidec emits an unused `const int32_t` temporary for an int literal given
    to a float field of a frag's values struct (`Skin { eyes = 0 }`), which
    `-Werror` rejects. `people.tide` writes `0.0`.
  - No empty `Map` literal: `mut Map<string, int> pieceOf = default;`.
  - A draw with a frag can't be made in a helper function, so drawing code
    lives in views.
  - Queries can't be passed to functions: systems copy the dead into a
    `List<Body>` each tick for strikes, missiles and bursts.
  - Command handlers can't query, so `Restart` sets a flag that systems act
    on next tick.
  - A system with `mut Survivor` can't change another survivor: reviving
    sends `Helped` to the downed one.
  - `Spawn` takes a component literal, not a local value, so `TakeSeat`
    repeats `Outfitted()`'s fields.
  - A singleton can't be assigned whole: `ResetMatch` resets field by field.
  - Local handlers can't read the match's lists (the `Village` copy) or use
    `with ownership` (they compare seats).
  - Animation catalog members keep the clips' underscores (`idle_Sword`,
    `hitRecieve_2`).
  - No `quaternion.FromToRotation`: `held.tide` has its own `FromTo`.
  - No custom fonts: the title is the built-in bold face.
  - The Rooms package's views draw before the game's `Develop`, so the
    title's words are drawn by the game's own `TitleCard` view on the HUD
    layer instead of in `TitlePage`.
  - An input can't be passed to a function (`UseSkill` takes the move as a
    `float2`).
  - `Spawn` takes component literals only, not a value of the component's
    type (`TakeSeat` spells the new survivor out rather than spawning
    `Outfitted(...)`).
  - A view of exactly eight parameters with a query, whose frag read a
    singleton the view didn't take, hung tidec in codegen (fixed in this
    work, `notes/shaders.md`).
  - Generated archetype names (`arch5_Zombie`) move when a component is
    added, so the C tests name them again; token-pasting macros won't
    stand in for them.
  - No instanced skinned draw (`Draw.SkinnedInstanced` is a listed future
    API): the horde bakes its clips into a texture a vert reads instead.
  - Local fields can't be fixed arrays (`List<Shard>` holds the gibs'
    pieces), can't be read through a query's rows (the gibs' blood is a
    per-entity view), events can't be passed to functions (their fields
    are), and there's no `float4x4.Scale` (`TRS` with a scale).
  - The web pack carries each texture raw and block-compressed, and globs
    pack whole folders, so the page is about 21 MB after pruning the kit,
    decimating trees, Ogg sounds and three 256-texel textures.
