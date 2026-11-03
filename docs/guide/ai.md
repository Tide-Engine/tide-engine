---
description: "Give your AI coding agent a Tide overview, persistent project instructions and an optional portable skill, with downloadable Markdown documentation."
---

# Using Tide with AI

An AI agent can work with Tide without knowing it beforehand. Give it the [quick reference](./ai-context.md), access to the relevant references, and a compiler it can use to check its work. Start with the guide; add project instructions or the skill when you want the setup to carry across conversations.

<div data-tide-ai-setup></div>

## Start a conversation

Copy the starter prompt above and add what you want to build. It points the agent to the compact overview and asks it to validate its changes. If your chat cannot fetch links, download the guide and attach it instead. For work needing more APIs without internet access, also attach the [full documentation bundle](/llms-full.txt); ask the agent to search the relevant topic rather than read it all at once.

The starter prompt is also plain text:

<<< @/static/ai/starter-prompt.txt

Tell the agent the gameplay you want, the intended platforms and any existing project conventions. An agent with a terminal can compile and inspect captures. A chat without filesystem or execution tools can suggest code, but you will need to build and test it yourself.

## Play a game interactively

An agent with process input/output and image tools can play the same running
game dynamically: `tide run --interactive play.txt` draws its first frame and
waits. Send `capture menu.png`, inspect the PNG, queue future-frame script
lines such as `2 mouse 640 353` and `3 button left`, then send `step 30`.
Wait for `tide: interactive ready 31`, inspect another capture and choose the
next actions. The game waits while the agent thinks; `quit` or closing input
ends it. Executed actions are saved to `play.txt` for normal scripted replay.
For an agent's terminal tools, use a persistent terminal session (a PTY when
needed) and retain its session handle. Launch the command without waiting for
the game to exit. Write subsequent control lines to that same session. After
`capture`, use the agent's image-viewing tool to actually inspect the picture,
then choose the next input from what it shows. Keep repeating this loop in the
same process; `play.txt` is the automatic recording, not a command inbox. The
agent needs terminal-session input and image viewing, with no additional file
control channel or game-specific integration.

See [Interactive play](../tools/cli.md#interactive-play) for the protocol,
multiple players and limits. This uses ordinary player input and rendered
pictures; it does not require game-specific agent code.

## Profile a game

An agent can collect a structured report from the standard host without opening
the profiler panel or writing a custom C host:

```sh
tide run --release --script play.txt --frames 360 --profile profile.json --profile-warmup 60
```

Set `TIDE_HIDDEN=1` in the agent's shell before native runs. Use a script that
reaches the situation being investigated; interactive play can record it first.
Keep the same seed, resolution, player count and saves for comparisons. The
report is written when the run ends and identifies measured frames, CPU work,
system/view source locations, GPU totals and per-pass timings with shader membership,
and engine draw counters. For GPU optimization, rank `gpu_passes.rows` and inspect
its coverage counters before changing rendering work.
It measures actual work rather than treating the hidden run's fixed simulation
step as a frame-time result. Capture is currently native desktop only.
Check the schema version, measured-frame count, build metadata and dropped-row
counters before ranking costs. Source locations identify declarations; worker
rows are aggregate categories, and helpers remain inside their caller's time.

Ask the agent to connect cost to useful work: for example, how many mesh
instances were tested and rejected while recording an expensive view. A low
culling rejection rate suggests inspecting earlier visibility selection; it
does not prove culling can be removed, especially for another camera or shadow
pass. Make one change, replay the same scenario in an optimized build, compare
the report and inspect captures. Include simulation checks when the change
could affect the match, and repeat measurements when noise could explain the
difference. See [Profile captures](../tools/debug-views.md#profile-captures) for
timing meanings, limits and the culling example.

## Keep project context between conversations

Put the [starter AGENTS.md](/ai/AGENTS.md) in your **game folder** and keep it in source control. Merge it into existing instructions rather than overwriting them. It points to the overview and contains game build/run guidance. Add your own project's important decisions and validation commands as they become established. This is separate from the Tide engine repository's contributor guide.

[Codex reads AGENTS.md](https://learn.chatgpt.com/docs/agent-configuration/agents-md) as project instructions. Current [Claude Code versions can read AGENTS.md](https://code.claude.com/docs/en/memory#agentsmd), subject to their instruction-file settings. For a Claude setup that does not load it, put the [starter CLAUDE.md](/ai/CLAUDE.md) beside it; the file imports `AGENTS.md` rather than duplicating its rules. If you already have `CLAUDE.md`, add the import to it:

```text
@AGENTS.md
```

Other agents have their own discovery rules. Explicitly ask your agent to read the project instructions if its client does not load them automatically. A web link alone is not guaranteed to be fetched or remembered in a new chat.

For an offline project, save the overview as `TIDE.md` next to `AGENTS.md`; the starter instructions prefer that local copy. Keep its source-revision header when updating it.

## Install the optional Tide skill

Download [tide-skill.zip](/ai/tide-skill.zip). It contains one `tide` folder with `SKILL.md` and a generated `references/overview.md`, the same overview shown in the docs. The skill guides an agent through version checks, topic lookup, implementation and bounded validation. It does not install Tide or grant the agent additional permissions.

Extract the `tide` folder into your agent's project skill directory:

- **Codex:** `.agents/skills/tide/`, with `SKILL.md` directly inside it. Invoke it as `$tide`, or let the agent select it for a matching task. See the [OpenAI skill documentation](https://learn.chatgpt.com/docs/build-skills).
- **Claude Code:** `.claude/skills/tide/`, with `SKILL.md` directly inside it. Invoke it as `/tide`, or let Claude select it. See [Claude's skill documentation](https://code.claude.com/docs/en/skills).
- **Other compatible agents:** install the folder using the client's [Agent Skills](https://agentskills.io/home) support. The format is portable; discovery and installation paths depend on the client.

The skill and `AGENTS.md` serve different purposes: project instructions retain this game's rules, while the skill supplies a reusable Tide workflow. Either can use the same overview. No MCP server is required to read the docs or use the local compiler.

## Match the documentation to your tools

Run `tide version` when starting a project or diagnosing a compiler error. The website and its downloads identify their documentation source revision; they are not a release archive and may describe features newer than your installed Tide. Keep a known-working compiler version and the downloaded guide together when pinning a project. Prefer that project's versioned references when supplied, and verify unfamiliar syntax with the compiler.

Updating the guide or skill is an explicit download; existing project copies are not silently replaced. Do not solve a docs/compiler mismatch by upgrading the user's toolchain without their agreement.

## Reference downloads

Every documentation page has a clean Markdown export at its page URL plus `.md`, for example `/game/commands.md`; the introduction is `/guide.md`. Exports include source examples as code, without needing the website's embedded game controls.

- [Quick reference](./ai-context.md): the first attachment for a new conversation. Use the download button above for the Markdown file.
- [Documentation index](/llms.txt): a small list of topics and Markdown links for agents with web access.
- [Full documentation bundle](/llms-full.txt): an optional single file for offline search.
- [Project instructions](/ai/AGENTS.md), [Claude import](/ai/CLAUDE.md), and [starter prompt](/ai/starter-prompt.txt): editable starting points for your game.

Keep descriptions of your game's intent and decisions in the project. When the agent repeatedly misunderstands a Tide feature, retain the failing request and compiler diagnostic so the guide can be corrected against a real example.
