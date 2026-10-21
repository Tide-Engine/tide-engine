# This Tide game

Read `TIDE.md` in this folder when present; otherwise read https://tide-engine.dev/guide/ai-context.md before writing Tide code. Find exact topic references through https://tide-engine.dev/llms.txt. Check the installed version with `tide version`; the website may be newer. Prefer this project's pinned guide/toolchain and do not silently upgrade either.

- Inspect the existing game and preserve its conventions. A game is a folder of `.tide` files, with no imports or required project generator. Optional packages are listed in `tide.packages`.
- Keep gameplay in match components/singletons and local presentation in local state. Views read the match; input and validated typed commands carry player actions into it.
- Use `tide build` and, when targeting web, `tide build --web`. Fix compiler diagnostics using the relevant reference rather than guessing APIs.
- In the agent's own shell, set `TIDE_HIDDEN=1` before native runs (`$env:TIDE_HIDDEN = '1'` in PowerShell, `export TIDE_HIDDEN=1` in POSIX). Use a bounded run such as `tide run --frames 120 --capture smoke.png`, then inspect the picture. Use `--script` for behavioral checks; never leave an automated interactive game running. Automated Chrome/Edge runs use `--mute-audio`.
- Report the builds and behavior checks actually performed. A successful compile does not establish gameplay correctness or coverage on untested platforms.

Add this game's established design decisions, target platforms and extra validation commands here as they are agreed.
