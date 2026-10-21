# Credit Pool onboarding trial results

## Outcome

Created `game.tide`, a 640×360 two-player match game. Each joining player gets a Wallet with three credits and a stable join-order seat. The shared Bank starts with a zero pool. The selector is a per-client local singleton, bounded from one through three by local buttons. Deposit submits a typed `Deposit` command targeted at the player's wallet. The handler checks the connection-provided sender against `Owner.player`, validates the amount, and rejects amounts above that wallet's balance before transferring credits.

The clean first attempt is preserved in `first-attempt.tide.txt`. It compiled, but hard-coded `PlayerID(1)`/`PlayerID(2)` display lookups did not provide a reliable player roster in the multiplayer run, so the final version assigns seats when players join and stores credits on their owned wallet entities.

## Compiler errors and attempt notes

The initial `game.tide` copied from `first-attempt.tide.txt` built without compiler errors. The first capture showed player 1 with 3 credits and player 2 with 0, demonstrating that treating IDs as fixed display slots was unsuitable. The final code instead uses `Wallet` entities with `Owner` and a join-order seat.

During cleanup, I tried a combined ownership filter spelling. This compiler diagnostic was produced and corrected by using a separate `with` filter for each condition:

```text
game.tide:54:62: error: expected parameter name, found ')'
   54 | view DepositControls(mut DepositUI ui, with Wallet, ownership)
      |                                                              ^
```

## Commands and checks

Compiler: `C:/Users/blenm/.t3/worktrees/PurrEngine/t3-cc8a7a3e/build/onboarding/sdk/bin/tide.exe`, version `tide 0.4.0-dev (nightly)`.

Every shell used for Tide commands set `$env:TIDE_HIDDEN='1'`.

```powershell
& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' version
& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' build .
& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' build . --web
& 'C:\Users\blenm\.t3\worktrees\PurrEngine\t3-cc8a7a3e\build\onboarding\sdk\bin\tide.exe' run . --players 2 --script play2.txt --size 640 360
```

The native and web builds succeeded. The bounded native script clicks the first player's selector twice, deposits three, attempts the same deposit again with the now-empty wallet, then has player two deposit one. At frame 220, the player 1 capture `final-1.png` shows pool 4, player 1 balance 0, player 2 balance 2, and selector 3. This confirms the extra deposit was rejected (the pool remains 4), and the selector remained client-local (the second player's capture shows selector 1).

## Failure and unverified coverage

`final-2.png`, the second process's capture from the same two-player script, shows the local selector and buttons but omits the shared pool and both wallet labels. Thus the requirement that both clients show the pool and both balances is **not verified and appears to fail in the supplied simulated multiplayer run**. The first client's image does show both balances and the shared pool after both deposits settle. The second process may be exposing a view/replication issue; this trial did not diagnose or replace that behavior with unrelated game logic.

The native `--players 2` run uses the CLI's simulated network and is not proof of real transport behavior. Web was compiled but not run in a browser. No external assets were used. `initial-1.png`, `initial-2.png`, `result-1.png`, `result-2.png`, `final-1.png`, and `final-2.png` are the inspected captures; the early captures were taken before multiplayer state settled and are superseded by the final bounded run.
