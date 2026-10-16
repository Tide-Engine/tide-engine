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
(done, below); 2, the PSX look and modern guns on a first survivor kit; 3, the
RoR2 loop (gold, chests and items, the teleporter, bosses, scaling); 4, more
survivors, then the biomes. Until 2 and 3 land, the game is the classes,
cards and waves below with a much bigger horde.

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

## How it plays

Endless waves in a forest clearing, a card after each, with medieval things
in place of guns and perks:

- **Classes** (`abilities.tide`), picked with 1, 2 or 3 before the first
  wave (nothing starts till every ready survivor has one, and a joiner picks
  on joining): the Warrior (Arming Sword and Leap; melee weapons), the Ranger
  (Hunting Bow and Dash; bows, crossbows, thrown, the handgonne; twice the
  reserve), the Mage (Staff of Embers and Frost Nova; the three staffs, whose
  charges come back on their own, 0.7 a second, more with Wellspring). A
  class's weapon cards are only its own weapons (`OfRole`); staffs are tiers
  1 to 3.
- **The ability on Q**, one at a time, on a cooldown a fifth shorter (and a
  quarter stronger) for each honing: Leap (8 s; up at 9.5 m/s and ahead at
  7.5, slamming down everything within 3.2 m where it lands), Dash (3.5 s;
  19 m/s for 0.18 s the way they press), Frost Nova (9 s; freezes everything
  within 5.5 m for 2.2 s), Fire Pot (7 s), Grave Bell (25 s), War Cry (10 s;
  throws down everything within 4.5 m), Blink (6 s; up to 9 m ahead, not
  through anything). Fire pots and bells are no longer counted items.
- **Waves.** A horde: `(16 + 9w + 1.1w²) × (1 + 0.6 per extra player)` of
  the dead (26, 38, 53 ... 88 by the fifth, 216 by the tenth), at most 150 up
  at once. Health `60 + 40(w-1)` to wave nine, then ×1.09 a wave; a claw does
  30. A spawn every `max(0.07, 0.75 × 0.9^(w-1)) / (1 + 0.3 per extra player)`
  seconds, at one of the five risers nearest a random standing survivor, but
  none within 7 m of them. Runners from wave three (12% more a wave, 92% at
  most), sprinters among them from wave nine.
- **Between waves** (`rounds.tide`'s `Regroup`, `cards.tide`): whoever is
  down stands up, whoever bled out comes back at their place by the fire,
  everyone is healed and their arrows, bolts, charges and pots filled, Second
  Wind alone gives back its rising, and each survivor is dealt three cards.
  The next wave starts five seconds after the last card is taken, or after
  45 s for one who hasn't taken one (whose cards are then lost). Late joiners
  start with the hand crossbow and are dealt at the next wave's end.
- **The cards.** Taken with 1, 2 or 3 (the pad's d-pad left, up, right).
  The first is for the class: a weapon of it not carried, from the two newest
  tiers the night has reached (`weapons.tide`'s `Tier`: 1 from the first
  wave, 2 from the third, 3 from the sixth, 4 from the ninth; from the tenth
  wave a growing share come runeforged), always after the first wave and 45%
  after; otherwise a class upgrade: the Runeforge (from the second wave, while
  the weapon in hand isn't forged), an edge (whetstone, broadheads, focus:
  +20% to every blow, missile and spell), and the class's own: Wide Swings
  (+1 cleave, +0.12 rad arc), Bloodthirst (4 health a kill), Deep Quiver
  (+50% reserve), Bodkin Heads (+1 pierce), Volley (+1 missile fanned 0.07
  rad), Steady Hands (draw and reload ×0.85), Split Spell (+1 fireball fanned
  0.09 rad, frost +2 struck and +0.05 rad, storm +2 in the chain), Wellspring
  (+50% charges and reserve, faster coming back, reloads ×0.8). The second is
  for themselves: a brew not yet drunk (55%, drunk there and then), or a heart
  of oak (+40 health), hobnailed boots (kick reach +0.3 m, one more knocked
  over, damage ×(1 + boots), stamps ×(1 + 0.4 boots), less stamina) or a
  fleet foot (+10% speed, +25% sprint). The third is for the ability: another
  in its place, or the one they have honed (at most four honings). Everything
  taken stays through downs and bleeding out, until the game's over.
- **Points** are only a score now: 10 a hit, 20 a hit to the head, 50 a
  kill, 100 a kill to the head; burning and bleeding give none. They show
  over the wave's tally and on the game-over table.
- **The dead** rise out of eight graves on the clearing's west side and from
  among the roots at fourteen places round its edge (1.8 s), then walk a
  flow field to the nearest standing survivor and claw (50 a hit, 1.1 s
  between). Heads take more. Elements: fire burns for 3 s, frost slows for
  3 s and a second frost blow while slowed freezes solid for 1.2 s, lightning
  stuns for 0.6 s (the Staff of Storms chains), bleeding ticks for 3 s, a
  blast bursts round the hit; burning and bleeding pay no points and show no
  hit marker until they kill.
- **Melee, after Dying Light** (the owner, 2026-10-16: "maybe we can get
  inspired by dying light's melee combat, including the kicking and them
  tumbling over"). F kicks (0.55 s, a fifth of the stamina bar): up to three
  of the dead within 1.9 m in front go over backwards (25 damage, thrown back
  at 6 m/s); one already down is stamped on, on the head, for
  `220 + 0.4 × the wave's health`. A heavy blow from something blunt, swept
  wide or two-handed also takes them off their feet. One thrown back knocks
  over the next it falls into. Down (`ZombieState.Fallen`), one goes over
  with the knockback clip (0.83 s), lies 1.3 to 2.5 s and gets up with UAL2's
  `LayToIdle` (1.53 s); lying, it takes half as much again from every blow,
  and its head, chest and hips (`Body`) are where the lying body has them, for
  blows and missiles alike. Killed while down it stays down; killed by a
  blow that knocks down, it's thrown over as it dies.
- **Headshots** are their own thing: a gold hit marker (red and bigger for a
  kill), a crack every time, a higher tick to the one who hit, HEADSHOT over
  the crosshair for a kill, and the head comes off.
- **Down and out.** At 0 health a survivor is downed for 30 s and fights on
  from the ground: with the weapon in hand if it shoots, or a last stand's
  hand crossbow of 12 bolts if it doesn't (`LastStand`, in the third slot,
  put away again by `StandUp`). They're raised by a friend holding E for 3 s.
  Alone, down is the end unless they drank Second Wind Cider (a rising a
  wave). Who bleeds out comes back at the wave's end. When nobody is left
  standing, the game is over; use (or the menu's New game) starts again from
  the first wave, with nothing kept.
- **Weapons.** 33 and bare hands (`weapons.tide`): 18 melee (slash, chop,
  thrust, or a wide sweep; a heavy blow on right mouse), four bows (hold to
  draw, let go to loose), four crossbows, a handgonne, three staffs and two
  thrown. A survivor carries one (two with Packmule Stout) and starts with
  the hand crossbow; a weapon card replaces the one in hand. The stab and the
  kick are always to hand (the owner, 2026-10-16: "maybe we just have one
  weapon and we need to pick between melee or not? we already have a stab and
  the kick so we dont need another melee"). The Runeforge multiplies a
  weapon's damage (×3 melee, ×2 staffs, ×2.5 the rest), its magazine (×1.5)
  and reserve (×1.6), and gives it an element. V stabs with a dagger
  whatever's in hand; G throws a fire pot; Q rings a grave bell, which draws
  the dead within 35 m of path for 7 s and then bursts.
- **Power-ups**, at most four a wave (2.8% a kill), in a fixed rotation with
  random skips: Full Quivers (ammunition), Death's Touch (one-hit kills,
  30 s), Holy Fire (every one of the dead). Double Gold and the carpenter
  went with the village.

## Contracts

- **The match** (`game.tide`, `rounds.tide`, `cards.tide`, `zombies.tide`,
  `hands.tide`, `missiles.tide`, `drops.tide`, `ashwick.tide`) is
  deterministic and ordered by `enum Tick { Restart, Survivors, Hands,
  Missiles, Zombies, Field, Rounds, Late }`. `Random` is the match's (the
  cards are dealt from it); views roll their own local `Random`
  (`Fx.random`).
- **Ashwick Wood is one function**, `MakePlan()` (`plan.tide`): the match
  keeps its solids, risers and starts (`Ashwick`, built at `Spawned`), and
  each machine calls it again for what it draws (`Village`, a local
  singleton, because local handlers can't read the match's lists). Change one
  and both follow. The clearing's edge is `Reach(angle)`, about 34 to 46 m
  from the fire; the forest past it is drawn but nobody's ground.
- **The ground is one function too**, `Ground(x, z)` (`plan.tide`): Perlin
  rolling ground laid flat round the fire, a hill, a hollow and a ridge
  (`Bump`), a cliff-top and a shelf (`Mesa`, short sides) each with a ramp
  (`Ramp`), and a rise into the trees past the edge. Nothing of it is stored:
  the match asks it where feet, the dead, drops, bells and missiles meet the
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
  breadth-first field from the standing survivors' cells, and another from
  stuck bells; each zombie steps downhill, or straight at its target when the
  path is clear. The soak test checks that every one of the dead reaches the
  survivors (none hunts for two minutes).
- **Seats**: `Party` holds four seats (0 Wat, 1 Maud, 2 Godric, 3 Agnes).
  A joiner takes the first free seat; a leaver's survivor is destroyed.
- **Cards**: `Survivor.cards`, `cardItems` and `choosing`; `dealt` is the
  wave they were dealt for, so a survivor is dealt once a break. A weapon
  card's item is the `Weapon`, plus `FORGED_CARD` (100) when it comes forged;
  an ability card's is the `Ability`. `Survivor.role` is `None` till a class
  is picked; `Fight` does nothing until then.
- **The horde's cost** (measured with `gravewake_perf`, release, this
  Windows machine): posing each of the dead in the match (`Brace`, for the
  head, chest and hips blows and missiles strike) was 92% of a tick; 150 of
  them cost 1.69 ms a tick. Now one within 3.5 m of a survivor is posed every
  other tick and the rest every sixth, staggered (`Zombie.phase`), keeping
  their points about their feet between: 0.71 ms for 150 crowding two
  survivors (4.2% of a 60 Hz frame), 0.98 ms for 200. The picture poses the
  dead past 22 m every other frame and past 45 m every fourth, and the dead,
  survivors and kit draw with `[LOD]` (shadows picked through the player's
  camera). Not measured: a browser frame with 150 on screen.
  `Choose` (at `Tick.Survivors`) takes one on `pick1`..`pick3` and sends
  `Picked` for what's seen and heard; `RunRounds` holds the countdown while
  any ready survivor is `choosing`. While choosing, 1 and 2 take cards, not
  weapons.
- **Commands**: `Restart` (Rooms' New game, Play, or use on the game-over
  screen) sets `Match.restarting`; the `Tick.Restart` systems clear the dead,
  missiles and drops, re-outfit the survivors and reset the match (command
  handlers can't query). `Cheat` is the development keys (F5 the wave's end,
  F6 the places in turn, F10 the next weapon).
- **Inputs**: `ready` is `[Absolute]`; the rest are held bools read with
  `.down`, look is clamped to half a radian a tick. Mouse look is read only
  while the cursor is locked.
- **What's local**: the camera (`Me`), the plan's copy (`Village`), the kit's
  placements (`Kit`), poses (`local Pose pose` on survivors and zombies,
  sampled once a frame), walk clocks, hit flashes, voices, gibs and drop hums
  (`local` fields), the feed (with the others' picks), effects and sounds.
  Hits, kills, blows, missiles, picks and cues reach views through local
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

- The scene draws into a multisampled `Rgba16f` window target and `Develop`
  brings it to the window with ACES, bloom (quarter-size bright pass and
  blur), the hurt and downed reds and the game-over fade.
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
  cells; nothing starting till both have picked a class (a Warrior with the
  arming sword and Leap, a Ranger with the hunting bow, twice its arrows and
  Dash); the first wave held with the sword (kills, a score); the cards dealt
  (a weapon of the class not carried, a card for themselves, one for the
  ability), one taken by each, the countdown held while one is still
  choosing; every kind of card taken through the input (the Runeforge, the
  quiver's reserve, the heart's health, an edge, boots, fleet, wide swings,
  bloodthirst, a honing, a new ability, Ironhide's health, Packmule's second
  hand, a forged weapon card); a sword-carrier downed into the last stand
  with every brew kept and raised by the other; one bleeding out and back at
  the wave's end with everything; a staff's charges coming back; a dash and a
  blink moving them, a leap's slam, a frost nova, a war cry and a fire pot
  landing on one of the dead; a crossbow bolt into the dead; a grave bell
  rung and the lure field reaching it; a bleeding wound, a forged Falchion's
  slow and freeze; a kick, a stamp, getting up, a maul's knockdown; then both
  holding the fire for three waves, taking the third card after each (every
  one of the dead reaches them within three minutes: the clearing is wide),
  then the match from a snapshot and through its packed bytes hashes the
  same. The soak once found one of the dead stuck at the camp's fence (1 m
  cells missed a thin box) and one risen into a gravestone's cell.
- `gravewake_play_webgl` / `_webgpu` (`test/browser.mjs`, label
  `private-prototype`, `webgpu`, run serially): the page in headless Chrome
  with frame control and `checkGL`: the title, Play, the first wave, a swing,
  a crossbow shot, the wave ended by F5 and a card taken, then a walk round
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
  - An input can't be passed to a function (`Unleash` takes the move as a
    `float2`).
  - No instanced skinned draw (`Draw.SkinnedInstanced` is a listed future
    API): the horde bakes its clips into a texture a vert reads instead.
  - Local fields can't be fixed arrays (`List<Shard>` holds the gibs'
    pieces), can't be read through a query's rows (the gibs' blood is a
    per-entity view), events can't be passed to functions (their fields
    are), and there's no `float4x4.Scale` (`TRS` with a scale).
  - The web pack carries each texture raw and block-compressed, and globs
    pack whole folders, so the page is about 21 MB after pruning the kit,
    decimating trees, Ogg sounds and three 256-texel textures.
