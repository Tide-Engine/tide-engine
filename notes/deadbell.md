# Deadbell: private co-op prototype

Built 2026-10-09 under `prototypes/deadbell/`. The owner requested a private PSX roguelike prototype with massive enemy crowds, at least two players, and a demo-sized 30–60-minute experience, then authorized finishing it and merging into `dev`. The current repository is private. A follow-up added it to the signed-in admin prototype grid. The owner's later public-sharing request makes its playable URL and thumbnail public so partners need no admin access; the catalog stays signed in. It remains outside public Samples, docs examples and package installation. No deployment was dispatched.

## Building and files

`TIDE_PRIVATE_PROTOTYPES` is an opt-in CMake option, default `OFF`; the prototype subdirectory is added only when it and `TIDE_PLATFORM` are enabled. Native and web targets are `deadbell`, `deadbell_tests`, and `deadbell_smoke`. Configure each build with `-DTIDE_PRIVATE_PROTOTYPES=ON`. The game uses the existing standard host and Rooms package; `tide.packages` declares `rooms`. The test folder has its own `tide.packages` boundary, keeping its C hosts out of a normal `tide run`.

- `game.tide` holds match declarations, scenes, commands, player ownership, and match events.
- `play.tide` holds hero movement and weapons, rescue, phase progression, and the enemy director and crowd systems.
- `input.tide` resolves this machine's controls into a bounded movement vector, ground aim point, and action buttons.
- `light.tide`, `shapes.tide`, `environment.tide`, and `show.tide` construct and render the city and its low-poly characters.
- `ui.tide` is the title, loadout, personal relic draft, objectives, guide, touch controls, and result screen around Rooms.
- `audio.tide` and `sfx/` provide original procedural WAV cues, with `sfx/make_sounds.py` as their reproducible source.
- `test/deadbell_test.c` is a generated-game C harness using `tests/tide_test.h` and `tests/test_main.c`; `test/smoke.tide` and its host exercise rendering on the actual platform backends.
- `README.md` is the prototype's player guide.

The public example glob and install rules do not collect this folder. Native direct connections use the standard `--host` / `--connect` flags; room codes use Rooms. The web target emits `deadbell.html` and uses the same simulation. `deadbell_page` stages its self-contained page at `docs/static/prototypes/deadbell/index.html`, served publicly as `https://tide-engine.dev/prototypes/deadbell/`; `private_games` includes that target when enabled, or the native game on desktop. The existing manual docs workflow enables private prototypes and builds this catalog. The checked-in thumbnail is an actual first-seal combat capture and lives beside the public page. Any external game assets also belong in that game's public directory. Admin auth, former game addresses and the build-time asset guard follow `notes/docs-site.md`; the catalog data remains server-only.

## The run

The Drowned Ward, Iron Procession, and Ash Cathedral are three acts in one fixed city route. Each has three bell seals. A successful run reaches nine sanctuaries and then kills the Last Cantor. The normal pacing target is 40–50 minutes within the requested 30–60-minute envelope; this is **not a measured human playtime**. Nine full-rate captures require 24 minutes 45 seconds, before travel, draft time, contested progress, optional detours, and the final fight.

`Waiting -> March -> Hold -> Sanctuary` repeats for every seal. The ninth sanctuary leads to `Boss`, whose actual death leads to `Victory`. `Defeat` occurs when both heroes are down, or either hero's 35-second bleedout expires. A valid restart resets progression and returns to `Waiting` for new loadouts and another manual start.

- The match starts with two persistent hero entities, one per seat. Real play waits for two joined players; there are no replacement combat bots in normal play. Losing a player freezes crowd work and expedition progress until two are present again, preserving the existing heroes.
- `StartRun` and `RestartRun` accept a current non-default member, so authority is not tied to seat 0 after host migration. The UI presents these actions to the actual session host. Start still requires two connected players. Weapon and relic commands are addressed to a hero and require its owner as sender.
- A march permits at least 15 seconds of travel, then both standing heroes must be within 12 units of its bell and hold interact together for one real second. No deadline teleports players or completes an objective for them.
- The first seal is territorial: each standing hero within 15 units contributes to presence; both advance capture at full rate, one at 35% rate. The second follows a figure-eight center, with both heroes staying within 15 units. The third has two centers 24 units apart, one standing hero within 10 units of each in either assignment, and also requires the keeper's death. Stacking both heroes at the same bell grants no progress.
- Every hold needs 165 seconds of credited progress. Pressure recipes differ: one front followed by an opposing front, two rotating streams, then encirclement. Armored reinforcements join the final third of captures.
- Every completed seal clears the existing crowd, restores 30 health and stamina, and gives a personal three-card relic draft. A draft lasts at least 10 seconds when both have chosen, or automatically chooses at 25 seconds. A hero's choice is awarded once.
- A march's side cache needs both heroes within 8 units holding interact for two seconds. It gives 20 health, 40 score coins, doubles the chosen relic stack at the next draft, and adds 700 to that encounter's crowd target subject to the 8,000 cap. It is optional.

Seeds shuffle relic offers and change crowd positions and composition; the map and objective route stay fixed. A retry derives a new seed. Loadouts are Carbine, Scattergun, and Chime Lance, with twelve stackable relics. Coins are a run statistic, not a currency shop. There is no campaign save, permanent power progression, or expanded content beyond this expedition.

`Main.pace` is a fixture hook that accelerates phase clocks and hold progress only; movement, attacks, rescue, cache and bell channels still use real `Time.dt`. `Main.testBots` is a separate explicit fixture hook, not a lobby option. Neither accelerated fixtures nor scripted captures measure normal human duration or completion balance.

## Combat, co-op, and fairness

Both heroes have primary fire, a heavy cone, a dash, a noise lure, and interact. There is no fixed escort or support role. Movement and ground aim are sent as bounded inputs; camera, screen, menus and feedback stay local.

The heavy cone deals useful damage independently and primes surviving enemies for four seconds plus Kindling stacks. Initial ordinary enemies have 72 health and survive the 48-damage base heavy, so the cooperative mechanic is available from the first encounter. A primary or heavy hit by the other seat consumes priming for a 100-damage base rupture bonus, increased by Black Powder. Heavy attacks interrupt ordinary enemy windups, while wardens retain their warning and resist knockback. Bellguards also resist knockback.

Rupture kills accumulate mending credit. Four credits trigger a small heal for each standing hero, with a two-second cooldown and bounded credit storage; heal amount is that hero's 4 base plus Twin Mercy stacks. The result counter records kills finished with rupture, rather than every nonfatal rupture hit.

Noise lures appear 18 units toward aim and last six seconds plus Brass Echo stacks. They attract ordinary enemies within 60 units; wardens resist them. If both lures reach an enemy it chooses the nearest, with seat 0 as a deterministic tie break.

Every hero has a shared 0.65-second damage grace interval, so a large simultaneous crowd cannot apply every overlapping contact at once. Dash provides short invulnerability and spends stamina. A downed hero can crawl at 1.3 units per second but cannot attack, dash, or lure. A standing partner within 6 units can hold interact to rescue: three seconds of progress, sped up by Second Wind, with gradual progress decay when interrupted. Rescue restores 55% of maximum health, stamina, and three seconds of damage protection.

Ordinary melee warns for 0.65 seconds around a saved attack point. Cantors warn for 1.2 seconds with a 3.5-unit radius; wardens warn for 1.5 seconds with an 8-unit radius. Cantor and warden attacks check **both** standing heroes against that saved point when the warning ends; ordinary melee checks its selected target. Keeper health is 4,500 / 6,300 / 8,100; the final boss has 60,000 health and gains faster, longer-reaching casts at its health thresholds. A timer never grants boss victory.

## Crowd work and presentation

Every enemy is an ECS entity with its own position, health, priming, target and warning state. The director grows pressure from hundreds in the first encounter to an 8,000-entity target, counting the live keeper or boss in that total. It spawns bounded batches around the players and mirrors a clamped spawn offset if an edge would put it too close to its player center.

Hero systems write two fixed `HeroPulse` packets. Each enemy independently tests those two packets and follows a target with deterministic formation offsets. It never scans other enemies or solves pairwise crowd collisions. The four-system order is `DriveHero -> Rescue -> AdvanceExpedition -> Crowd`; the enemy kernel can split over rows without shared mutable enemy work. Enemies carry a run generation and are removed when it no longer matches, so restart cannot keep the previous run's crowd.

The simulation is a flat walkable city. Ruins and skyline are presentation; nearby obstructions are kept low enough to match the walkable floor. The current implementation has no physical enemy separation, navigation mesh, destructible city, or rigid-body crowd simulation.

Rendering gathers culled enemies into five model families and draws each family with instancing. It uses a fixed elevated camera following the local hero from a 31-unit baseline, nominal 270-line integer display scaling, point sampling, projected vertex snapping, ordered dithering with 31 channel steps, and a muted fog palette. The HUD is drawn at screen resolution. A 512-pixel moon depth map supplies shadows for visible pieces. Material IDs are rounded before shader branches: interpolated raw float IDs produced spurious material bands despite each triangle using one material.

Color and shape carry the useful distinctions: bright cyan priming, brass bell objectives, mauve caches, taller armored and warden silhouettes, and rings above the opaque crowd for players, rescue and threats. Threat rings use `attackAt` and their actual durations and radii, and draw after the bodies with depth disabled. Rescue progress is `hero.revive / 3`, which stays accurate because rescue upgrades speed accumulation rather than changing the threshold. Local attack, rupture, lure and rescue effects are bounded at 600 fragments and 24 waves; they never alter the match.

## Validation

The native generated-game harness passes ten groups (2026-10-09): immediate player join and command guards; first-act priming and cooperative rupture; dash, downed crawl, interrupted rescue and return grace; optional team cache and once-only personal draft; every seal, keeper, final boss death and restart; fixed-seed repeat and changed-seed crowd; overlapping lures; same-bell rejection and both valid split-bell assignments; and both-inside / one-outside cases for cantor and warden ground attacks.

The crowd regression lets the actual director populate 8,000 real enemy entities, runs combat inputs, copies and restores the world, reruns those inputs, then packs and unpacks the resulting state. Its expected world hash is `beea342b979203e0`, and the packed result is 833,040 bytes. This is a deterministic fixture, not a frame-rate promise.

The same ten regression groups pass natively in debug and release and under WASI, with the identical pinned hash. The render fixture passes native OpenGL, browser WebGL and WebGPU at 960 x 540 and 390 x 780. It populates 8,000 real rows, gathers 3,973 nearby bodies, checks varied image samples, and verifies that presentation leaves the world hash unchanged. It also exercises all four portrait touch actions, movement, and aim through the actual view-to-input path. A child `test/tide.packages` keeps these fixtures out of a normal `tide run`.

Two headless Chrome pages connected through a local relay and real WebRTC, selected preparation/start, moved, fired and primed enemies, opened the guide, and resized to portrait. `Tide.checkGL` was enabled; both runs completed with no JavaScript or GL errors. Chrome was muted. Native scripted two-player captures cover preparation, combat, and the first personal draft. One normal-speed input script completed the first seal and reached its sanctuary at 3:11 with 6,094 kills, without altering health, clocks or objective progress. A two-second offline audio capture from each player contained nonzero PCM (peak 2,912, RMS about 780). Assets and procedural geometry are original to this prototype.

A scratch release simulation probe on Windows, Ryzen 9 7900X and Clang 23.1.2 retained twelve snapshots and hashed after each of 600 measured combat ticks, following warmup. The actual director population ranged from 7,664 to 8,000. Later runs averaged 0.547–0.594 ms per tick/copy/hash with one offered thread and 0.164–0.177 ms with twelve; respective p95 ranges were 0.600–0.725 and 0.534–0.537 ms. All configurations produced identical final hashes. CPU contention was present; an initial single-thread run averaged 1.328 ms. Protected heroes and an extended-health warden sustained this measurement. It excludes drawing, audio, transport and actual restore/replay, and establishes no minimum hardware or complete-game frame-rate promise.

The existing native suite passed 2,009/2,009 tests and the existing web suite 1,981/1,981 before the prototype was enabled. Its own targets and tests were then built and run on both. A full unaccelerated human co-op run, public distribution, and additional platforms have not been validated by this prototype work.

A complete normal-pace controller soak (seed 123, `pace=1`, two actually joined owners) reached Victory after 116,323 ticks, or 32:18.7 of simulated gameplay. It used normal inputs and validated command packets for movement, weapons, warning dodges, lures, rescue and all nine personal drafts; no health, position, clock, objective, spawn or boss state was changed by the harness. Peak population was 8,000, with 302,527 kills and 51,921 rupture kills. The final fight lasted 3:38.4; seat 1 fell once and was rescued 3.1 seconds later. Final HP was 115/63 and world hash `2f31e3fb2aa1b267`. This proves full native progression within the requested duration, while the human pacing target remains open to playtesting.

Integration with the concurrent Cabin Nine and The Last Light changes preserves their builds and private catalog. Deadbell uses the existing root opt-in option. After merging those changes, the three games' scoped checks passed 5/5 natively and 9/9 on the web, including browser play checks and both graphics backends. Deadbell's optimized web build also passes all three checks with the same simulation hash. Native and web debug/release builds have no compiler diagnostics. No CI workflow or deployment was dispatched.

The original follow-up catalog integration built `private_games` on native debug and web release; staged Deadbell HTML had the same SHA-256 as the compiled page. Its own native tests passed 2/2 and optimized web tests 3/3. Site check reported zero errors/warnings, the production site built, all 18 analytics/admin unit tests and three SEO tests passed. The original production admin HTTP regression checked the actual compiled Deadbell page and thumbnail bytes, the catalog link, authenticated access, encoded paths, ranges and stale public aliases. The owner's public-sharing request supersedes those authentication rules; current serving validation is in `notes/docs-site.md`. These changes do not dispatch CI or deploy the website.

The original muted production Chrome checks at 1280 x 900 and 390 x 844 showed four cards with loaded screenshots and no horizontal overflow. Deadbell opened the then-protected page in a new tab and drew through WebGL with cross-origin isolation and `checkGL` enabled, without JavaScript exceptions or GL errors. The latest `dev` merged cleanly, retaining the other prototypes; native/web catalog builds and Deadbell's 2/2 native and 3/3 web tests passed again for the shared build-file overlap.
