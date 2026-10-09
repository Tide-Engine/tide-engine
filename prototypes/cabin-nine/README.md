# Cabin Nine

Opt-in Tide horror prototype for **exactly two human players**. Both play cabin crew on a late charter flight. Eight people board; after a lighting failure, the cabin contains nine. An extra passenger wears a familiar face and remembers an earlier version of that person's belongings.

The current prototype has a complete playable route through boarding, four authored investigations, reversible containment, a final crew report and restart. It establishes the setting and deduction loop. **A 30–60 minute blind first playthrough is the design target, not a measured claim about this build.** The four cases need cooperative playtesting and further content before calling this a polished demo of that length.

This folder is deliberately separate from the public demo, examples and installed packages. Its CMake option is off by default. The browser game has a public shareable URL at [tide-engine.dev/prototypes/cabin-nine/](https://tide-engine.dev/prototypes/cabin-nine/), with no admin sign-in needed. The **Admin → Prototypes** catalog remains authenticated.

## Build and run

From the repository root:

```powershell
cmake --preset debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset debug --target cabin_nine cabin_nine_tests cabin_nine_render
ctest --preset debug -R "^cabin_nine_"
```

For a normal player session, launch `build/debug/bin/cabin_nine.exe` on each player's machine. Use the room menu to host and join, then both press Enter when ready. Escape opens the room menu. The flight waits for both players, including if a crew member disconnects. Each player controls their own first-person camera; there is no substitute AI crew member.

The same opt-in option applies to web builds:

```powershell
cmake --preset web-debug -DTIDE_PRIVATE_PROTOTYPES=ON
cmake --build --preset web-debug --target cabin_nine cabin_nine_tests cabin_nine_render
ctest --preset web-debug -R "^cabin_nine_"
```

The generated browser game is `build/web-debug/bin/cabin_nine.html`; it is self-contained and can open directly from disk. For threaded execution, serve it with the repository's normal cross-origin-isolated web development workflow. With the opt-in option enabled, `cmake --build --preset web-debug --target private_games` stages it at `docs/static/prototypes/cabin-nine/index.html`. After deployment, anyone can open the public game link, host or join a room, and share its code. It remains outside the Samples gallery.

## Controls

| Control | Action |
|---|---|
| WASD | Walk through the cabin |
| Mouse / arrow keys | Look |
| Enter | Ready your crew member |
| E | Speak to a nearby passenger; read a nearby galley record during an investigation |
| F | Inspect the passenger's visible belongings |
| H | Escort one passenger to the rear crew compartment, or release them |
| Q | Nominate the passenger you are facing |
| C | Confirm the nomination; both crew must sign |
| Tab | Open or close the evidence notebook |
| N | Continue after a resolved case |
| R | Restart from the final report |
| Escape | Open the room menu |

There is a forward record and an aft witness record. Either player can visit either end. Both records must be collected and both players must contribute a reading. Talk to and inspect your proposed suspect before nominating them. Escorting a passenger is reversible and does not itself declare them an impostor. A confirmed accusation needs both crew signatures.

The first two mistaken confirmed accusations release the innocent passenger and reduce cabin trust. A third ends the flight's investigation. Passenger locations change between cases, so an extra chair is not a reliable identity clue. The notebook retains collected evidence for the current case.

## Implementation boundaries

- Built: a first-person aircraft interior, eight distinct recurring human identities, a changing copied identity, authored dialogue and physical observations, two evidence stations, two-player case confirmation, a single reversible holding space, trust consequences and ending/restart flow.
- The PSX look and sound palette are a prototype presentation, with procedural cabin/passenger geometry and original synthesized ambience rather than finished character art or voice acting.
- The current mystery is deliberately authored. Actor 8 is always the extra passenger internally; its displayed identity and position change. Four cases teach before/after evidence, not a procedural mystery generator.
- The inspection interaction supplies readable physical detail. It reports observations rather than an automatic innocent/impostor result. This supports details too small to read reliably at the low render resolution.
- Autonomous stalking, dynamic passenger movement, inventory-driven service work, adaptive responses to overheard player conversation, a physical landing sequence and measured 30–60 minute pacing remain design work. Their intended behavior is in [DESIGN.md](DESIGN.md).
- Validation passed on native and WASI for all eight simulation cases, and on OpenGL, WebGL and WebGPU for the renderer. Two-instance scripted captures were visually checked at 1280 by 720 and 960 by 540. The full engine test results are recorded in `notes/cabin-nine.md`. These checks do not replace a blind two-person playtest.

## Files

`game.tide` owns deterministic flight state and player actions. `story.tide` owns identities, dialogue and case evidence. `show.tide` draws the aircraft and passengers. `hud.tide` owns the local first-person interaction prompts, notebook and room integration. `test/` exercises the game through generated host accessors. `sfx/` holds the original sound-generation inputs and assets.

The intended experience, clue contract, cast and timing hypothesis are documented in [DESIGN.md](DESIGN.md). Its case table contains spoilers.
