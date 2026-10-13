# SWINDLE

**Steal the jewel. Sell the lie.** A very civil rooftop robbery for one to four.

A 90-second competitive heist, played as a duel, free-for-all or two teams of
two. Touch the prism to take it. Its value grows from two to eight points as
you carry it. Cash out by standing in the mint-colored getaway for 0.65 seconds.
The getaway moves every twelve seconds. Most money at the bell wins; a tie goes
to overtime until a cash-out breaks it.

Space dashes. Dash into an enemy carrier to steal the prism **and its current
value**. E sends a double running in your current direction while you vanish
for one second. Dashing into the double wastes the attack and briefly stuns
the attacker. Turn after deploying it to sell the lie.

| Action | Player 1 / online | Player 2 on the same keyboard | Gamepad |
|---|---|---|---|
| Move | WASD, physical key positions | Arrow keys | Left stick |
| Dash / steal | Space | Right Shift | South button |
| Double / vanish | E | Enter | West button |
| Menu | Escape | Escape | Menu button |

Touch supports a floating movement stick on the left and Dash/Fake regions
on the right; landscape is recommended. The menu and controls fit small
windows. Teams share their cash totals; teammates cannot stun or steal from
one another. The shared-screen mode shows both humans' ghost silhouettes.

**Play** starts immediately with bots. **Host friends** opens a room; share
its six-character code, choose the mode, then start the heist. **Join a room**
uses that code. Empty seats can be filled with bots or left empty. Rematches
rotate through the Penthouse, Gallery and Terrace. A third online player
joining a duel expands it to free-for-all. New arrivals take over an open bot
seat, including its current score. Opening an online room from a couch match
returns to the lobby and releases the second keyboard seat.

## Build and try

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target swindle swindle_tests
ctest --preset debug -R '^swindle_tests$' --output-on-failure
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target swindle_page swindle_tests
ctest --preset web-debug -R '^swindle_' --output-on-failure
```

The web page stages at `docs/static/prototypes/swindle/index.html`. Serve
`docs/static` and visit `/prototypes/swindle/`. Native builds put `swindle.exe`
and `swindle.pack` in their `bin` directory; keep them together. `tide run` in
this directory also builds it with the `rooms` package.

The game is entirely Tide: deterministic simulation, prediction and rollback,
room transport, original geometric art, particles and synthesized audio.
No gameplay C or new engine/runtime API. Sound sources are reproducible with
`python sfx/make_sounds.py` and use no recordings or third-party music.

This is a presentation-ready prototype, with three small arenas and one core
ruleset. It has no progression, matchmaking queue, account system or saved
match history. Publisher playtests and wider Internet/NAT testing remain useful
before treating its balance or connectivity as production-ready.
