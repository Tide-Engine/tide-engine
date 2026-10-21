# claude-bank-r1 results

Materials used: AGENTS.md, tide-skill/SKILL.md, TIDE.md, searches of llms-full.txt (Commands, GUI, Basics > Settings, The tide command > scripts / several players). SDK: onboarding/sdk/bin/tide.exe, `tide 0.4.0-dev (nightly)`. TIDE_HIDDEN=1 set in every shell.

## Commands run
- `tide version`
- `tide build` (first attempt) -> built, **no compiler errors or warnings** (build1.log). The first attempt is saved unchanged as first-attempt.tide.txt.
- Layout/UX fix only (slider -> three 1/2/3 buttons, spacing), then `tide build` -> built again with no errors.
- `tide build --web` (after both versions) -> built build/claude-bank-r1.html.
- `tide run --players 2 --script play.txt --size 640 360` (exit 0; frames 180 max, 8 captures per player).

## Design
- `Wallet {credits=3, seat}` + `Owner` spawned per player in PlayerJoined; `Pool {credits, rejected}` and `Roster` singletons.
- `local singleton Picker { amount = 1 }` is the selector (buttons 1/2/3), never sent.
- `Deposit { int amount }` command, sent by the Deposit button via `this.Send` from a `with ownership` view.
- Handler `command(Deposit, PlayerID sender)` rejects: sender != Owner.player, amount outside 1..3, amount > wallet.credits (increments Pool.rejected, visible on screen). Otherwise wallet -= amount, pool += amount.
- Views: both balances and the pool drawn for every client; only own panel has controls and an "After: you X, pool Y" preview computed from local state.

## Behavioral checks observed in captured images (640x360)
- initial-1/-2: both show 3 / 3, pool 0; each client's panel labeled "You" on its own side.
- preview-1/-2: P1 picked 2 -> its preview "After: you 1, pool 2"; P2's preview still "After: you 2, pool 1" (selector is local only).
- mid-1: after P1 deposit 2 (3->1) and P2 deposit 3 (3->0) pool 5, balances 1/0 on both clients. P1 preview turns red "Not enough credits" when amount 2 > balance 1.
- final-1/-2: P2 re-deposit of 3 with 0 balance and P1 deposit of 2 with 1 balance were rejected (Rejected: 2, balances unchanged); then P1 chose 1 and deposited: balances 0/0, pool 6 on both clients.
Not observed: a forged command from a non-owner (the UI cannot send one; only the handler guard exists), a tampered amount > 3 (same).

## Documentation gaps / friction
1. No documented way to set the game's window size (`settings` has no width/height); "640x360" had to be treated as layout from `Screen.width/height` plus `--size 640 360` for runs. The CLI says `--size` default 960x540; nothing says what a built game's default window is for a 640x360 target.
2. The AI context shows `Draw.Circle`/`Draw.Text` but not `Draw.RoundRect`, `Anchor`, `GUI.Button(Rect...)`, `GUILayout` coordinate system; I found them in the Commands and GUI sections. The y-up (Draw.SetScreen) vs y-down (GUI Rect) mismatch is only implied by examples (text at `Screen.height - 30` vs `Rect(12,12,...)`); a one-line statement in TIDE.md would help.
3. `GUI.IntSlider(rect, label, value, min, max)` shows a very narrow track inside a 200-px rect (label takes most of it), making a scripted click selection impractical; I switched to buttons. Not documented how the label/track widths split.
4. The scripts docs describe one `button left` click but not that a slider needs press+drag; unverified.
5. `--players` output filenames get `-1`/`-2` suffixes (documented) - fine.
6. Whether `Draw.SetScreen()` persists to later views is not stated; I relied on the Commands example order (SetScreen in the first view, only Draw/GUI after) and it worked.

## Unverified
- Web: only `tide build --web` succeeded; no browser run (not permitted/needed); WebGL/WebGPU rendering untested.
- Real network transports: `--players 2` is the simulated network only.
- Rollback/prediction under latency, host migration, third player (maxPlayers = 2, third join not tested).
- Slider interaction (replaced by buttons) and the visual overlap of the "Deposit" button with the pool bar edge is at most 0-2 px; "1 credits" grammar not special-cased.
