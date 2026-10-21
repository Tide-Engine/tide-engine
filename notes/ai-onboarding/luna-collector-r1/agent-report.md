# Luna collector trial results

## Game

`game.tide` implements a 5×3 board with match-owned position and collection state. Arrow key down edges move one cell with bounds checks. Coins at (2,1) and (4,2) are guarded by per-coin flags; count and completion text are drawn, and R restores the initial state. `Draw.SetScreen()` renders the row-0-at-top board with built-in shapes only.

`first-attempt.tide.txt` preserves the initial source before visual inspection. It compiled without diagnostics. Inspecting its capture revealed that the initial screen-space Y formula put row 0 at the bottom; the source was corrected and native/web builds rerun. No compiler errors were encountered.

## Commands and checks

Compiler used: `C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe`, reported as `tide 0.4.0-dev (nightly)`.

Each native command ran in PowerShell after `$env:TIDE_HIDDEN = '1'`.

- `& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' build` — passed, native executable generated.
- `& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' build --web` — passed, web HTML generated.
- `& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' run . --script play.txt --size 640 360` — bounded run ended normally, emitted 640×360 captures `initial.png`, `one-coin.png`, `complete.png`, `reset.png`, and `edges.png`.
- `& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' run . --frames 120 --capture initial-no-input.png --size 640 360` — bounded no-input capture confirmed initial player at (0,1), both coins visible, count 0/2, and row 0 at the top.

Inspected the actual PNGs. The scripted one-coin capture shows count 1/2 and the player on the first coin; the completion capture shows count 2/2 and the completion message; the edges capture shows the player at (0,0) with count 0 after reset and the scripted edge route. The scripted `initial.png` timing did not represent a pristine initial frame, and `reset.png` was black, so those two script captures do not independently verify startup/reset presentation. The separate no-input capture verifies startup presentation. The R input reset the displayed count and restored both coins in the scripted run, but the black reset image prevents confirming the player's reset position from that capture.

## Documentation and limits

Used the supplied `TIDE.md`, `tide-skill/SKILL.md`, and offline `llms-full.txt`, including its Input, Views and drawing, screen coordinates, and CLI scripts sections. No missing API reference blocked implementation. The compact overview describes screen drawing and Y-up world coordinates; for a top-row-first grid, the screen-coordinate convention in the Views reference was needed.

Web compilation was checked, but no browser was launched, so browser rendering/behavior is unverified. No automated assertion or standalone edge-by-edge state inspection was available; behavioral checks above are visual observations of bounded scripted captures. Reset position remains unverified by a clear capture.
