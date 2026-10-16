# GRAVEWAKE

`prototypes/gravewake/`: a first-person cooperative zombie survival game for
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
difficulty with time, stage after stage in place of waves); 4, the biomes,
and more survivors. All the picks in phase 2 are mine (Provisional): the
kits, the items, the numbers.

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
  HAHAHAHA). Bleeding out, a survivor is back at their place by the fire
  `RETURN_SECONDS` (30) later with everything (`Return`). The game is over
  when nobody's standing; the score is how long it lasted. Power-ups are at
  most four a minute. `Phase.Break`, the waves' counts and the regrouping
  are gone.
- **Items at levels** (`items.tide`'s `Experience`): each level the party
  gains is a pick owed to every survivor (`Survivor.pending`); the next owed
  is offered as soon as the last is taken. Picking is 1, 2 or 3 while the
  fighting goes on (no pause: the engine has none, and it's every player's
  match); the cards sit small along the bottom (`Offers`). Offers grow rarer
  with the run's minutes.
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
  The HUD has its health across the top; it's heard roaring as it comes, at
  each roar and charge, and dying.
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
  across, `look.low` of them), half that past 14 m, a quarter past 30 m, an
  eighth past 55 m, never under one (`Fineness`). The distance is read once
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
  - Handling: SpringySpringo's real (airsoft) reloads, a rifle's, a pistol's
    and a shotgun's pump.
  - Hits: qubodup's meat impacts and Independent.nu's wet ones; Kenney's
    interface click for the hit marker.
  - Waves: a town's flood siren in a storm (TinyWorlds' recording) starts
    one; a breath of Paul Wortmann's Dark Cavern Ambient ends one, and a
    longer one is the game's end.
  - Music: two minutes of Juhani Junkala's Post Apocalyptic Wastelands under
    early waves, two of Ruskerdax's Ancient Grudge when the horde is big,
    each crossfaded round on itself.
  - Items and levels: Kenney's RPG sounds (a latch, a pouch, coins) and an
    interface chime.
  - Not yet replaced: the dead's voices (recordings already), footsteps,
    explosions, the power-ups' sounds.

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
- **The items** (`items.tide`): sixteen, stacking, in three rarities; three
  offered after each wave (uncommon and rare more often as waves go on).
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

An endless run in a forest clearing, an item at each level:

- **Survivors** (`kits.tide`), picked with 1, 2 or 3 before the run (nothing
  starts till every ready survivor has one, and a joiner picks on joining).
  Each a gun and three skills, above.
- **The run** (`rounds.tide`): the dead come all the time, more and tougher
  as the danger climbs with the minutes (above, No waves), at most 3000 up at
  once, clawing up 16 to 32 m round a random standing survivor; a claw does
  30. A Brute every three minutes (`boss.tide`).
- **Levels** (`items.tide`): every kill is the party's experience; each level
  is an item to pick for every survivor, taken while the fight goes on.
- **Points** are only a score: 10 a hit, 20 a hit to the head, 50 a kill,
  100 a kill to the head; burning and bleeding give none.
- **The dead** rise out of the graves and from among the roots round the edge
  (1.8 s), then walk a flow field to the nearest standing survivor and claw.
  Heads take double (a shotgun's pellet half again). Fire burns for 3 s,
  frost slows for 3 s and a second frost while slowed freezes solid (and
  Frostbite freezes outright), shock stuns for 0.6 s, a blast throws. A boot,
  a slam and a grenade's middle knock them over; one lying takes half as much
  again and is stamped on by the boot.
- **Headshots**: a gold hit marker (red and bigger for a kill), a crack,
  HEADSHOT over the crosshair for a kill; a crit's marker is yellow.
- **Down and out.** At 0 health a survivor goes down for 30 s and fights on
  with the pistol, unless a Second Wind puts them straight back up (one used
  up). Raised by a friend holding E for 3 s. Who bleeds out is back at the
  fire 30 s later. When nobody is left standing, the game is over.
- **Power-ups**, at most four a minute: Recharge (every gun loaded, every skill
  ready), Death's Touch (one-hit kills, 30 s), Holy Fire (every one of the
  dead).

## Contracts

- **The match** (`game.tide`, `rounds.tide`, `kits.tide`, `items.tide`,
  `zombies.tide`, `hands.tide`, `missiles.tide`, `drops.tide`, `ashwick.tide`) is
  deterministic and ordered by `enum Tick { Restart, Survivors, Hands,
  Missiles, Zombies, Field, Rounds, Late }`. `Random` is the match's (the
  items' procs and offers roll it); views roll their own local `Random`
  (`Fx.random`).
- **Ashwick Wood is one function**, `MakePlan()` (`plan.tide`): the match
  keeps its solids, risers and starts (`Ashwick`, built at `Spawned`), and
  each machine calls it again for what it draws (`Village`, a local
  singleton, because local handlers can't read the match's lists). Change one
  and both follow. The clearing's edge is `Reach(angle)`, about 38 to 50 m
  from the fire; the forest past it is drawn but nobody's ground.
- **The ground is one function too**, `Ground(x, z)` (`plan.tide`): Perlin
  rolling ground laid flat round the fire, the graveyard's hollow (`Bump`),
  the ravine (`Ditch`), the chapel's rise and the spire's two tiers (`Mesa`,
  short sides) with their ramps (`RampTo`), and a rise into the trees past
  the edge. Nothing of it is stored:
  the match asks it where feet, the dead, drops, rounds and grenades meet the
  ground, and the picture draws it as a 193 × 193 heightfield at 1.25 m
  (`MakeShapes`), with rock where it's steep, casting the moon's shadows.
  Survivors can't walk up more than 0.75 m a metre (`Passable`), so cliffs
  are climbed by ramp, Leap or not at all; jumping down is fine.
- **Collision** is boxes in 8 m buckets, on the ground: circles pushed out,
  standing on the highest top within 0.45 m (the ground's or a box's). Props,
  tree trunks, the ruin's walls, the fence, the boulders and the gravestones
  have boxes.
- **Navigation**: a 1 m grid, 112 × 112 cells from (-56, -56) (a byte field
  reaches 254 m of path); cells past the clearing's edge, where the ground
  climbs more than 0.6 a metre (`STEEP`, the cliffs), and every cell a box
  taller than a step overlaps (a fence thinner than a cell too) are walls.
  One of the dead already in a wall's cell (risen or thrown there) may walk
  out of it. Every six ticks `Wayfinding` spreads a
  breadth-first field from the standing survivors' cells; each zombie steps
  downhill, or straight at its target when the
  path is clear. The soak test checks that every one of the dead reaches the
  survivors (none hunts for two minutes).
- **Seats**: `Party` holds four seats (0 Wat, 1 Maud, 2 Godric, 3 Agnes).
  A joiner takes the first free seat; a leaver's survivor is destroyed.
- **Items**: `Survivor.items` (a count of each), `offers` and `choosing`;
  `dealt` is the wave they were offered for. `Survivor.role` is `None` till a
  survivor is picked; `Fight` does nothing until then. `Choose` (at
  `Tick.Survivors`) takes one on `pick1`..`pick3` and sends `Picked`;
  `RunRounds` holds the countdown while any ready survivor is `choosing`.
- **Skills**: `Survivor.cooldowns` (secondary, utility, special); `UseSkill`
  in `Fight`, before the gun. A throw and the boot are acts (`Act.Throw`,
  `Act.Kick`) that land part way in; the gun waits for them.
- **Commands**: `Restart` (Rooms' New game, Play, or use on the game-over
  screen) sets `Match.restarting`; the `Tick.Restart` systems clear the dead,
  missiles and drops, re-outfit the survivors and reset the match (command
  handlers can't query). `Cheat` is the development keys (F4 a horde, F5 the
  wave's end, F6 the places in turn, F10 a random item).
- **Inputs**: `ready` is `[Absolute]`; the rest are held bools read with
  `.down`, look is clamped to half a radian a tick. Mouse look is read only
  while the cursor is locked.
- **What's local**: the camera (`Me`), the plan's copy (`Village`), the kit's
  placements (`Kit`), poses (`local Pose pose` on survivors and zombies,
  sampled once a frame), walk clocks, hit flashes, voices and drop hums
  (`local` fields), the feed (with the others' picks), effects and sounds.
  Hits, kills, shots (`Loosed`, and a `Traced` line for each round), skills,
  picks and cues reach views through local handlers.

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
  dressing, survivors, first-person arms and weapons) has the `Wobble` vert:
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
- Kit placements are culled by distance each frame. The first-person arms
  draw after a depth clear, hung from the eye (`FpEye`: 0.15 m lower for the
  made clips, eased between), with sway, bob, a lowered raise, a sprint carry
  and a shot's recoil on top of the clips.
- Behind the title, the camera circles the fire at 6.5 m, looking past it
  into the trees.

## Tests and validation

- `gravewake_test` (native and WASI, `test/test.c`): two players through the
  real input: the risers and starts clear of every box and on walkable
  cells; nothing starting till both have picked a survivor (a Gunner with the
  carbine's 30 rounds, a Breacher with the shotgun's 6); the first wave shot
  down with the carbine (kills, a score, the party's level climbing for
  both); three items offered each, none twice, one taken by each, the
  countdown held while one is still choosing; items taken through the input
  (Kevlar's health, an extended magazine loaded at once, a Second Wind, and
  every other), a Field Battery's shorter cooldown, a Spring Boots jump in
  the air; a Second Wind putting one straight back up; the Breacher downed,
  the pistol firing from the ground with the shotgun's rounds kept, items
  kept, raised by the other with the shotgun back as loaded as it was; one
  bleeding out and back at the wave's end with everything; a carbine round
  and a shotgun blast into one of the dead; every skill (a phase round, a
  slide, a frag; a boot, a leap's slam, a firebomb's burn, the boot's stamp;
  a mine laid and set off by the dead, a blink, a turret that fires and then
  goes); Frostbite freezing and the Mortar Beacon calling a shell, Live Wire
  arcing to another of the dead, a Powder Keg kill burning the one beside
  it, the drone and the ember ring wounding without a shot; then both
  holding the fire for three fresh waves, shooting the nearest, taking the
  third item after each (every one of the dead reaches them: the longest any
  hunted was 11 s), then the match from a snapshot and through its packed
  bytes hashes the same. In the soak a crowd's claws can land together for
  more than anyone's health, so it keeps them whole.
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
  gamepad feel, host migration mid-wave, balance past wave ten, how the
  cards' numbers stack deep into a run.
- Late joiners start with the hand crossbow and nothing taken, a few waves
  behind the others.
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
