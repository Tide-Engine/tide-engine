# Docs-driven game trial: internal feedback

2026-10-06. This records the owner's requested test drive of an AI game builder, its observed authoring costs, and feedback left by the builders. It is an internal report, not a public product claim or an approved feature plan.

## Current status (2026-10-18)

The report below is historical, not a current feature backlog. Audio playback,
Spawn/Send in ordinary helpers and multi-player standard-host scripts were built
later; their contracts are in `notes/audio.md`, `notes/language.md` and
`notes/standard-host.md`. Match-handler queries remain an explicit language
restriction, rather than a compiler defect to lift without design review.

The current prototype audit fixes nested-container foreach lowering, stored
vector/swizzle writes, captured numeric literals in skinned draws, match Random
seeding affected by local singletons, and compatibility identities affected by
discard-storage source positions. `notes/language.md` and `notes/random.md`
record their regressions and validation. Broader query/helper capabilities,
resource collections, draw/physics APIs and save/input policies still need their
own review; an old prototype workaround alone does not make them requirements.

## What was tried

Twelve initial games were built: nine with Opus 5.5 and three comparison games with Sonnet 5.5. Three further runs changed existing games. The brief required multiplayer games, keyboard and pointer/touch controls, small and wide layouts, and scripted play with pictures before completion. All twelve builders reported a game that built without warnings and played. The saved counters record 151 initial builds with nine failures; including changes, 163 builds with ten failures.

The builders could run only one player through `build/trial/play.mjs`. They explicitly left second-player joins, leaves, ownership, scoring between humans, and migration untested. Some used temporary bots, stand-in seats or forced game states, then removed those changes. Later browser checks are separate evidence: the historical `docs-test-full.log` records 40 examples passing before a timeout on the unbuilt Alley page. During the continuation, all nine Opus games were rebuilt and each passed the docs' two-player browser check, including rendering, restart and stop (`continuation-held-test.log`). None of these checks establishes complete multiplayer gameplay coverage.

This was not a clean-room docs test. The subagents inherited this repository's approximately 210,000-token instruction prefix, including AGENTS.md, despite being told to learn from the bundled docs alone. Several also reported minor tool-rule deviations. The trial therefore shows feasibility in this setup, not that an agent with only public docs achieves the same result.

## Recurring feedback

- **Sound.** Pong, Connect Four, Tanks, Marbles, Sumo and Tower Defense builders wanted bounce blips, landing sounds, shots, explosions or impacts. Current user docs expose no playback API; the assets spec lists audio among future formats. This is an observed gap in game feel. Any playback API, asset type and rollback behavior remain **Provisional owner decisions**, not an implementation request from this report.
- **Ordinary helpers cannot `Spawn` or `Send`.** Pong duplicated seating code, Sonnet Pong introduced a `Reseat` event, and Sonnet Tanks duplicated bot maintenance. Connect Four could not find a clear statement about `Send` in `language/functions.md` and routed requests through a system. Current `check.c` refuses both operations in ordinary functions/methods; async functions have a separate task contract. Clarifying `Send` beside the existing spawning restriction would address a documentation gap. Allowing these operations in ordinary helpers remains **Provisional**.
- **A second scripted player.** The one-player trial harness prevented builders from staging the behaviors their multiplayer games depended on. Tanks' hit changes and Bomberman's bomb-kick change also left interactions with another human unobserved. The engine's normal host/connect paths and the docs' browser pair already support multiple machines; this feedback concerns deterministic authoring/testing tools. A scripted second client, state-triggered scripts, world-coordinate targeting and readable capture state are **Provisional tooling proposals**. Seeded single-player runs already exist.
- **Queries in match handlers.** Marbles kept a seat bitmask because `PlayerJoined` could not inspect existing marbles. Sumo chained events to check whether a pusher remained in the ring. Tower Defense kept a `Grid2<Entity>` to find a tile's tower from a command. Current docs and `check_entity_query` explicitly exclude match event handlers, command handlers and tasks; local handlers can query. Extending the match contract remains **Provisional**. Pairwise-interaction recipes were also requested: Marbles and Sumo independently used a scratch component followed by a second system.
- **Finding drawing information.** Connect Four wanted a shorter 2D route; Marbles wanted a separate 3D route. Sonnet Tanks missed `this.Snap()` and worked around respawn interpolation before finding it near the end of the page. The tanks-change builder searched rather than read the whole page. `docs/game/views.md` is still 69,553 bytes in the checked snapshot. A shorter introduction, targeted cross-links or page split are **Provisional editorial proposals**; this report changes no public structure.

The reduced trial bundle also omitted pages that its links referenced, including CLI documentation. This is a bundle limitation, not evidence that those pages are absent from the public docs.

## Bugs distinguished from feature requests

Commit `4328fe3` fixes invalid generated C for comparison blocks or parallel loops beside query parameters, a finger's `press.up` disappearing from a view after sampling, and `pointer.position` resetting after a finger lifts. Regression coverage is in `compiler/tests/e2e/query_blocks*`, `tests/test_devices.c` and `tests/test_gui.c`. The same commit states the `Screen` dimension types, clarifies who reads server input and corrects `Instanced` typos. Those resolved findings should not remain on a feature wish list.

Historical logs record all 1,439 native and 1,425 web tests passing before the later dev merge touched code generation. The continuation rebuilt native and web targets, then passed the compiler overlap checks: 1,373 native and 1,368 web tests. Docs type checks and site builds passed for both the fixes branch and the held draft. The full current docs browser test passed all 34 source examples plus the Towers and Sumo lobby pages; the nine held prompt games passed separately. These results are in `build/trial/continuation-*.log`, using bundled Node 24.19 rather than the historical Windows Node 24.15.

## Recorded cost provenance

These are historical estimates from `build/trial/results.json` and `cost.mjs`, not an invoice, current pricing or a pricing recommendation. The script reconstructs usage from per-agent transcripts at the original session's xhigh effort. For incomplete output snapshots it uses the larger of reported tokens and an estimate from thinking plus text/tool characters at 3.2 characters per token.

| Work | Prefix-subtracted estimate | Mean elapsed time |
| --- | --- | --- |
| Nine Opus initial games | $5.26–$9.36; mean $6.50 | 24.4 min |
| Three Sonnet initial games | $2.05–$4.38; mean $3.42 | 16.0 min |
| Two Opus changes | $1.17 and $2.16 | 8.8 min |
| One Sonnet change | $0.81 | 7.2 min |

The saved script assumes dollars per million tokens of 4/20 input/output for Opus, 2/10 for Sonnet, 0.20 for cache reads, and cache writes at 1.25 times input. Its `product` estimate subtracts the inherited prefix and leaves 6,000 tokens for product instructions/tools while retaining the same observed work. Across all 15 runs, reconstructed request cost is $127.70 and the prefix-subtracted estimate is $72.92; initial games alone are $117.25 and $68.78. These are different accounting assumptions, not measurements of a clean rerun. Elapsed times came from concurrent transcript timestamps, not controlled latency benchmarks.

## Evidence and publication status

Primary local evidence is preserved in `build/trial/BRIEF.md`, `BRIEF-TWEAK.md`, `results.json`, `cost.mjs`, game folders and saved build/test logs. Feedback is in the saved session `edb5a13e-ea9d-48f1-b99c-5c046d914221.jsonl`; relevant one-based lines are 303 (Pong), 367 (Sonnet Pong), 397 (Sonnet Tanks), 484 (Connect Four), 513 (Tanks), 568 (Bomberman), 701 (Marbles), 728 (Sumo), 767 (Tower Defense), and 1018/1034/1058 (changes). The owner's test-drive authorization is line 67; the final handoff is line 1165.

The earlier public draft starts at commit `581daac` on `purrteam/ai-game-builder-concept-1d30f2`; local merge `7438049` preserves its corrected Sumo navigation. After reviewing the continuation, the owner requested a larger Samples gallery with thumbnails opening full-page games and the existing host/join flow. That replaces the standalone AI page: the nine games are adapted into self-contained folders under `examples/samples/`, alongside links to the four existing full games. The original trial sources, cost evidence and draft remain preserved in history and scratch files. No clean rerun or new model trial was needed or authorized.

The gallery continuation passed all 1450 native and 1435 web tests, the existing teaching-example browser checks, and all nine sample games' Play/Host/typed Join/Menu/Resume flows with two connected browsers, including a touch phone viewport, on both WebGL and WebGPU. The gallery was checked at desktop and phone sizes: thirteen loaded thumbnails, separate game tabs, no embedded games or game-program downloads. An installed CLI also built and captured the self-contained Pong folder with `tide run`. Logs and reviewed gallery screenshots remain under `build/trial/` and `docs/build/trial/` in this checkout.

The Linux Docs workflow runs the sample browser checks with WebGL. WebGPU pixel checks were validated on Windows: Build CI already excludes them on Linux because headless Chrome's readback is unreliable. An added WebGPU invocation in the Linux Docs job hit that same limitation and was removed to follow the existing CI convention.
