# Luna Bank onboarding trial

## Result

Created `game.tide` for a two-player shared bank. Each joining player gets an owned wallet with three credits and a stable seat number. The local GUI has an integer amount slider from one to three and a Deposit button. The button submits an entity-targeted typed `Deposit` command. Its handler checks the connection-supplied sender against `Owner.player`, bounds the amount, rejects amounts above the wallet balance, then debits the wallet and credits the shared pool. Both simulated clients render the pool and both wallet balances.

The 640×360 view was visually inspected in `final-1.png` and `final-2.png`; both showed pool 3, player 1 at 0, player 2 at 3. `selected-1.png` showed player 1's local selector at 3 while `selected-2.png` showed player 2's at 1. Native script logs showed `accepted 3: pool 3, balance 0`, followed by `rejected 2: balance 0` on both simulated processes. The repeated lines are consistent with rollback re-executing the deterministic command handler; the captured final state is consistent on both clients.

## First attempt and compiler corrections

Saved the untouched first attempt as `first-attempt.tide.txt` before fixing its compiler error.

Command (with `$env:TIDE_HIDDEN = '1'`):

```powershell
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' build .
```

Initial diagnostic:

```text
game.tide:36:14: error: local variable 'row' needs an initial value
```

The initial source used `for (var row in wallets)`. The offline overview/query examples use `foreach`; changing it to `foreach (var row in wallets)` fixed the issue.

A later instrumentation edit briefly produced an unexpected literal `\` from a PowerShell replacement and failed compilation. I corrected the newline and reran the behavioral run and both builds successfully. This was not in the preserved first attempt.

## Commands and checks

All commands used the absolute trial SDK executable, not an installed `tide`:

```powershell
$env:TIDE_HIDDEN = '1'
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' version
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' help run
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' help run --script
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' build .
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' build . --web
& 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe' run . --players 2 --size 640 360 --script validation-script.txt --frames 150 --capture final.png
```

Both builds passed on Tide `0.4.0-dev (nightly)`. The last command completed after the script's `150 quit`, captured each client's screens, and emitted accepted/rejected command logs.

`validation-script.txt` changes player 1's slider to 3, captures the local preview, submits the deposit, then changes the slider to 2 and submits again. The first command transfers all three credits. The second exceeds that sender's remaining balance and is rejected. An earlier bounded run with `deposit-script.txt` also exercised the default amount 1 deposit.

## Documentation used and gaps

Used only the supplied `TIDE.md` and `llms-full.txt`: GUI (`IntSlider`, `Button`, screen coordinates), Commands (entity-targeted send and authenticated `PlayerID sender`), views with queries, `PlayerJoined`, opaque player IDs with a separate seat index, CLI script syntax, and bounded multi-player runs. The only initial syntax gap was resolved directly by matching the bundle's `foreach` query examples. No API mismatch blocked the task.

The bundle says `--size` sets a native run's window size (default 960×540) and the settings table has no window-size setting. The game was therefore launched and verified at 640×360 with `--size 640 360`; a persistent default window-size override is not available through the documented game settings.

## Unverified coverage

- Web compilation passed, but no browser/WebGL/WebGPU run was performed.
- `--players 2` tests the SDK's simulated network, not real UDP/WebRTC transports.
- The script verified a valid owned deposit and an over-balance rejection. It did not attempt a forged command against the other player's wallet; sender validation is present in the handler but that adversarial path was not independently exercised.
