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

Report the concrete changes and checks that ran, plus any tool, backend or behavior that remains unverified. Do not claim an unobserved gameplay or networking result.
