# Trial results

## Game

Created `game.tide`: a 5 by 3 top-origin board at 640 by 360, arrow-key cell movement with edge guards, two single-use coin pickups, a collected counter, completion text and R reset. The visuals are drawn with built-in shapes and text; there are no external assets. The first source attempt is preserved in `first-attempt.tide.txt`.

## Compiler and builds

Compiler: `C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe version` -> Tide 0.4.0-dev (nightly).

- `$env:TIDE_HIDDEN='1'; & 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' build`
  - Passed; produced `build/luna-collector-r2.exe`.
- `$env:TIDE_HIDDEN='1'; & 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' build --web`
  - Passed; produced `build/luna-collector-r2.html`.
- No compiler errors or warnings occurred; no source fixes were needed.

## Native behavioral and visual check

Exact bounded command:

`$env:TIDE_HIDDEN='1'; & 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' run --script behavior.txt --frames 60 --size 640 360`

The script captures start, left edge, one coin, both coins/completion and reset. Inspected the produced PNGs:

- `left-edge.png`: player remains at (0,1), both coins remain, count 0.
- `coin-one.png`: player is on (2,1), one coin is gone and count is 1.
- `complete.png`: player is on (4,2), both coins are gone, count is 2 and completion text is visible.
- `reset.png`: player returns to (0,1), coins return and count is 0.
- `start.png` captured on frame 1 before setup/drawing and is black; later captures show the rendered board correctly.

## References and gaps

Used the supplied `TIDE.md` and `llms-full.txt` sections for input, views/drawing, and CLI scripts. The local bundle documents the keyboard edge fields, top-left screen coordinates, shape/text APIs and script capture syntax. No documentation gaps blocked implementation.

Web was compile-checked only; no browser run was performed. Native validation covered left-edge blocking, both pickups, completion and reset. Other three edges and repeated key behavior were not independently captured. Real transport/networking and other platforms remain unverified.
