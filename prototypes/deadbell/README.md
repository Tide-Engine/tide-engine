# Deadbell

An internal, unpublished co-op roguelike prototype for **two players**. Two lamplighters enter a dead city to recover its bell seals, wake the tower, and survive the thing that answers. Its identity is an elevated 3D PSX look and a crowd large enough that cutting a passage matters as much as killing an enemy.

## Play the private build

From the repository root:

```powershell
cmake --preset release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset release --target deadbell
.\build\release\bin\deadbell.exe
```

Open the menu, host a room, and share its code with your partner. Your partner joins the room from their own copy of the same build. Both players choose a weapon; the host then selects **Begin expedition**. A single player can inspect the city and prepare a room, but the expedition requires two joined players.

For two native instances on the same machine, start one with `--host` and the other with `--connect 127.0.0.1`. Use the host's address for two machines on the same network.

The web build is opt-in too:

```powershell
cmake --preset web-release -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-release --target deadbell
```

This produces `build/web-release/bin/deadbell.html`. Serve the build directory through HTTP before opening the page. To stage it for the site's signed-in private prototype grid, build `deadbell_page` (or `private_games` for the whole catalog). Its protected address is `/admin/prototypes/deadbell/`; both players sign in before hosting or joining. The prototype is excluded from public examples and package installation.

## The run

The run has a beginning, an escalation, and an ending: **the Drowned Ward, the Iron Procession, and the Ash Cathedral**, three seals in each district, then the Last Cantor. The intended successful run is 30–60 minutes, with a normal pacing target around 40–50 minutes. An automated normal-speed run completed every seal and the boss in **32:19**, using ordinary combat and rescue inputs. A human pair's playtime remains unmeasured.

Move toward the brass bell together, use noise lures to bend the crowd away from your route, and hold interact near it together to start the alarm. Each seal needs 165 seconds of capture progress:

- **Territorial bell:** fight inside its circle. Two standing players capture at full speed; one captures slowly.
- **Procession bell:** follow its moving circle while enemies stream across the route.
- **Split bell:** cover two separate circles and kill their keeper. Either player can take either circle; your partner needs room to fight on their own side.

The later districts add armored fronts and ranged threats, with a configured ceiling of **8,000 active enemy entities** in the finale. A sanctuary after every seal restores some health and offers each player a personal relic choice.

The mauve side cache offers a voluntary risk: both players hold interact beside it to recover health and double their next selected relic, while increasing the encounter's crowd target. You can finish without collecting it. The Last Cantor keeps the horde in play and must actually be killed; waiting out a timer grants no victory.

## Fight as a pair

Both players can attack, dash, throw a lure, and rescue their partner. There is no permanent support role.

A heavy attack hurts enemies and **primes** survivors for four seconds. Primed enemies glow cyan. Your partner's primary or heavy attack **ruptures** that charge for a large damage bonus. Your heavy remains useful when you are separated; the reward for coordinating is a faster opening through the crowd. Rupture kills also earn small, bounded team heals.

For example, throw a lure beyond the flank of an incoming column. As it bunches up, one player sweeps a heavy attack across the front and the other fires through the cyan targets. Move through the opening before the next front arrives, then swap who sets up the next rupture. Saving a heavy attack for your partner's rescue can be worth more than spending it on the nearest enemy.

Large numbers of ordinary dead form the pressure. Specialist enemies are the decisions: runners close quickly, bellguards endure more damage and knockback, and cantors punish players who remain in their warned ground circles. The enormous keepers resist lures and heavy interrupts. Orange ground circles show where an attack will land; move outside before they fill.

## Build together

Choose between the piercing **Carbine**, the wide **Scattergun**, and the longer, slower **Chime Lance**. Each player then chooses their own stacking relics. For example, a Scattergun user can take Kindling to extend priming and Deep Breath for more heavy attacks, while a Carbine partner takes Black Powder to strengthen ruptures. The basic cooperative combo works before any upgrade is found.

The city route is fixed. Run seeds change relic offers and enemy positions and composition; retries choose a fresh seed. There is no permanent power grind or save between runs in this build.

A downed partner can crawl but cannot attack. Clear a pocket, place a lure away from the rescue, and hold interact within rescue range for three seconds. Rescue progress fades gradually when interrupted, and the revived player receives brief damage protection. Both players falling, or either player bleeding out after 35 seconds, ends the run. The result screen returns the team to preparation so you can change weapons before another attempt.

Menus and the field guide do not pause combat. If a player disconnects, combat and expedition progress wait for two joined players again.

## Controls

| Action | Keyboard and mouse | Gamepad |
|---|---|---|
| Move | WASD or arrow keys | Left stick |
| Aim | Mouse position | Right stick |
| Primary attack | Left mouse button | Right trigger |
| Heavy attack / prime | Right mouse button | Left trigger |
| Dash | Space | Bottom face button |
| Throw noise lure | Q | Left face button |
| Interact / revive | Hold E | Hold top face button |
| Control guide | H | Back / View |

Choose your weapon in the lobby. During a sanctuary, choose one of the three personal upgrades on screen. The room host starts the run once both players have joined.

## Verify a change

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target deadbell deadbell_tests deadbell_smoke
ctest --preset debug -R '^deadbell_'
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target deadbell deadbell_tests deadbell_smoke
ctest --preset web-debug -R '^deadbell_'
```

The tests exercise the two-player gate, combat and rescue, personal drafts, all nine seals, a real boss kill, seeded replay, and 8,000 simulated enemies. Implementation notes and the limits of validation are in [notes/deadbell.md](../../notes/deadbell.md).

Windows web tests require Node 24.16 or newer; set `TIDE_NODE` at configure time if the supported runtime is outside `PATH`.
