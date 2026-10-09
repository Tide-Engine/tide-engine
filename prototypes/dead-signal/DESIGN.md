# Dead Signal

Private cooperative shooter prototype. These are implementation choices for this
prototype, not approved Tide language or engine design. The owner can revise them.

## The experience

Two to four operators descend through an abandoned communications blacksite.
Something in the transmission has remade its workers and machines. Cut the six
sectors off from the signal, destroy its custodian, and get the crew out together.

The intended first complete expedition takes **30–60 minutes**, with roughly
40–45 minutes as the tuning center. This is a **design target, not a measured
human playtest result**. Automated navigation and aiming runs establish completion
and expose softlocks; they cannot establish human pacing or enjoyment.

The feel is fast first-person combat: strafe, choose a firing lane, land a heavy
shot, and move before the return fire arrives. PSX cues come from the point-filtered
low-resolution image, coarse geometry, quantized lighting and color, restrained
industrial palette, and original short electronic sounds. Modern readable aiming,
teammate labels, health feedback and navigation remain part of the presentation.

The expedition has a beginning, a changing arsenal, a build assembled during the
run, a final confrontation, and an ending. It does not require a permanent grind,
procedurally generated levels, or a large campaign.

## Expedition arc

| Sector | Job in the experience | New pressure or reward |
|---|---|---|
| Intake | Learn movement, shooting and rescuing a friend in a complete first sector | Scattergun, nail driver, visible projectile tells |
| Foundry | Establish flank angles and heavier fights | Breach launcher becomes selectable; armored custodians reward crossfire |
| Reservoir | Stretch fights across alternate approaches | Arc cutter becomes selectable; grouped enemies make chaining valuable |
| Choir | Mix charging crowds with ranged cover | Lieutenant variants, reinforcement approaches, stronger relic combinations |
| Transmission | Test the crew's ability to move and regroup | Dense combined attacks and the last build choice |
| Extraction | Pay off the expedition | Three-phase custodian, supporting attackers, then a joint extraction |

Each authored map has corridor loops, cover, five or six encounter sites, three
relay terminals and two optional supply alcoves. Encounter sites activate when
a living player approaches with line of sight. The team can choose approaches
and take risks exploring another site while a fight remains active.

There are 33 encounter sites across the six sectors. Each has three combat beats;
reinforcements enter when the current attack has at most three survivors. A short
0.65-second transition supports an arrival tell. There is no waiting for a long
survival clock, and all progression comes from clearing threats and completing
objectives. Compositions vary among a breach, ranged battery, pursuit pack,
combined assault and armored siege. Later attacks use different valid positions
near the room's approaches rather than stacking the entire population at once.

Intake's first contact eases into the crowd: eight, ten and twelve attackers for
two operators, before later sites use the full composition. Hunters stop and
flash during a quarter-second melee windup; leaving their reach avoids the hit.

The common sector objective is deliberate for this initial implementation: clear
all encounter sites, sever three relays, then leave together. The maps and combat
composition change; the objective verb remains consistent. More varied objective
verbs should earn their complexity in a human playtest, rather than add chores
before the shooting and co-op prove enjoyable.

## Cooperation

Every player is a complete shooter. There is no compulsory medic, turret operator
or inventory porter. All players can use every discovered weapon, revive any
teammate, open a cache and activate a relay.

- **Crossfire:** an enemy remembers the last shooter for 1.6 seconds. A different
  teammate's hit during that window deals 35% more damage. It also bypasses a
  bulwark's frontal protection. Shooting its exposed side or rear works without
  a mark; cooperation offers another strong answer.
- **Space control:** charging attackers draw movement while ranged enemies wind
  up visible, dodgeable projectiles and hunters telegraph close swings. Splitting firing angles helps; wandering too
  far makes a rescue harder. Reverse breadth-first routes let enemies follow
  corridors rather than become harmless behind walls.
- **Recovery:** a downed player can crawl and use a weak emergency shot. A living
  teammate within 2.8 meters holds Use for 2.1 seconds to revive them at 40% health
  with two seconds of protection. There is no permanent bleedout or spectator
  elimination. A rescue relic improves the channel and returned health.
- **Joint objectives:** two distinct living players hold Use near a relay for
  1.8 seconds. Nearby enemies interrupt its progress. Two living players must
  also hold at the exit for two seconds. Progress resets when cooperation stops;
  a lone player cannot leave a permanent half-completed latch.
- **Shared discoveries:** opening either optional cache supplies every living
  teammate. Relays grant ammunition to every living teammate. Individual drops
  remain small health or ammunition opportunities during fights.

Friendly fire is off. Player collision does not block narrow doorways. The crew
has two real human-owned seats at minimum; there are no automatic replacement
bots. Combat, projectile lifetimes and expedition time suspend when fewer than
two real players remain. A replacement occupies the first available seat and
joins at the current sector's start.

## Arsenal and build

The scattergun delivers nine pellets on a deliberate rhythm; the nail driver is
rapid accurate fire; the breach launcher is a moving explosive with bounded area
damage; the arc cutter is shorter-range fire that chains to at most two nearby
enemies with clear lines of sight. Walls stop all attacks. Explosions never harm
teammates. Empty ammunition falls back to a weaker emergency shot, so a supply
mistake cannot make the expedition impossible.

All players start with the first two guns. Foundry unlocks the launcher; Reservoir
unlocks the arc cutter. Weapons are switches in approach, not classes or random
rarities. Kills and caches replenish ammunition; the pace should encourage using
the arsenal rather than hoarding every strong weapon for a distant boss.

Between sectors the team receives three distinct relic offers. Each player
chooses independently, and the next sector starts after all current teammates
choose. Eight relics cover health, firing speed, damage, movement, rescue,
crossfire, kill sustain and armor/arc range. Repeated choices remain useful, with
caps on movement, firing speed and rescue strength. Builds persist through a
checkpoint retry and are reset for a new expedition.

## Failure, finale and replay

All living operators going down produces a team wipe. Retry restarts the current
sector, resets its encounters, relays and supplies, and restores the crew with
their chosen relics. It does not erase the expedition's completed sectors or the
preceding 30 minutes. The wipe count remains visible as a performance measure.

The final custodian appears after Extraction's encounter sites and relays are
complete. At 70% and 35% health it calls supporting attackers and increases its
attack frequency. The crew must keep crossing angles while managing those
attackers. Victory requires clearing the remaining threats and extracting
together; killing the boss alone does not cut straight to a victory screen.

Replay comes from the crew's approaches, relic combinations and weapon choices.
The maps are authored and the checkpoint policy is generous. Those are intentional
prototype choices: roguelike texture without committing to run-ending punishment
or a procedural content pipeline.

## What a human playtest must decide

The completion target, enemy dose and damage are provisional. A first session with
two actual players should measure completion time, sector time, wipe frequency,
rescues, weapon usage, confusing navigation and time spent waiting for an ally.
The most useful questions are whether flanking happens without explanation,
whether relay holds feel like a brief regroup or a repeated chore, whether late
combat introduces enough new pressure, and whether players want another run.

Short completion should be corrected with meaningful encounter or route content,
not health inflation or forced waiting. Excessive duration should be corrected
by removing repetitive beats. A full 30–60-minute human pacing claim remains open
until that session happens.
