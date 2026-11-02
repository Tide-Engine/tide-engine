---
name: tide
description: Write, modify and debug games in the Tide language and engine, using its documentation, compiler and bounded gameplay captures. Use for Tide game projects, .tide/.tprefab files and Tide APIs; not for unrelated products named Tide.
---

# Tide game development

Read the project's instructions and inspect its existing game before changing it. Stay within the requested game task; this skill does not authorize deployment, toolchain upgrades or changes to the engine.

Read the project's pinned `TIDE.md` overview when present, otherwise [references/overview.md](references/overview.md), included in the downloaded skill. If this skill was copied without its reference, fetch https://tide-engine.dev/guide/ai-context.md. If neither is accessible, explain which context is missing rather than inventing APIs.

Run `tide version` and check any documentation revision header. Website docs can be newer than an installed compiler; prefer the project's pinned references and use its compiler to resolve mismatches. `tide help build` and `tide help run` describe the available CLI. Do not silently upgrade Tide.

Use https://tide-engine.dev/llms.txt to select the relevant clean Markdown reference. Load only topics the task needs: input/commands for player actions, systems/entities for queries and structural changes, views/GUI for presentation, or the specific package's pinned docs. An optional local `llms-full.txt` bundle can be searched offline. A spec's open proposals are not implemented APIs.

Implement a small compilable step, build with `tide build`, and fix diagnostics against the reference. Preserve the match/local boundary: views cannot write the match, UI submits typed commands, and handlers validate the authenticated sender and game rules. Locals and parameters are read-only unless `mut`; entity changes are deferred. Do not introduce a C host, engine source dependency or build system for an ordinary folder-based game.

Validate the requested behavior, not just compilation. Before native runs set `TIDE_HIDDEN=1` in your own shell and bound them with `--frames` or a script that quits, for example `tide run --frames 120 --capture smoke.png`. Inspect the resulting image; use documented input scripts for interaction. Supplement numerical gameplay checks with state-change logs or assertions, and distinguish unclear image inspection from evidence that the game failed. `--players` runs separate game processes with a simulated network; it does not test real transports. Build with `tide build --web` when web is a target, and distinguish a web build from actual browser testing. Automated Chrome/Edge launches require `--mute-audio` and bounded execution.

For performance tasks, first reproduce the situation with a saved input script.
Capture an optimized native desktop run with
`tide run --release --script play.txt --frames 360 --profile profile.json --profile-warmup 60`.
Read https://tide-engine.dev/tools/debug-views.md for the current report schema
and timing meanings. Inspect actual CPU work, all system/view rows and their
source locations, GPU availability/sample counts, and engine draw diagnostics.
Check the schema version, measured frames, build metadata and dropped-row
counters. Source locations identify declarations; worker categories aggregate
across jobs, and inclusive helper/engine time belongs to the caller.
Hidden-run FPS is fixed simulation timing; overlapping CPU/worker/GPU intervals
must not be added. Connect costs to work counts: expensive mesh recording with
few frustum rejections suggests inspecting prior selection, not automatically
disabling culling. Check the relevant camera, destination and shadow behavior.
Form one hypothesis, change one thing and compare the same script, warmup, seed,
resolution, saves, player count and optimized build mode. Inspect images and
relevant simulation values/hashes, repeat alternating runs when noise matters,
and report observer overhead and unavailable coverage. Profiling a game does
not itself authorize changing engine code or gameplay/visual requirements.

Report the concrete changes and checks that ran, plus any tool, backend or behavior that remains unverified. Do not claim an unobserved gameplay or networking result.
