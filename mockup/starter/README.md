# Moonwake

A small single-player Tide game for the initial mockup.games workspace: gather twelve moonseeds to wake a floating midnight garden. All visuals are procedural, including the courier, flowers, lighting, fireflies, and particle trails. No downloaded or generated assets are required.

- Desktop: WASD or arrows to move, Space or Shift to dash, R or the Replant button to restart.
- Gamepad: left stick to move, south button to dash, north button to restart.
- Touch: drag to move, tap the right Dash circle with another finger, tap Replant to restart. The compact HUD and touch controls remain readable in a narrow embedded preview.

From the repository root, `node mockup/scripts/prepare-starter.mjs` builds `build/index.html` and its source/artifact manifest. The installed Tide executable must support web builds; `TIDE_BIN` can select another installation. A failed or missing toolchain fails the command.

`node mockup/scripts/test-starter.mjs` builds and runs the same behavioral tests natively and under WebAssembly/WASI. It checks input sampling (including simultaneous mobile movement and dash), movement, dash, collection, completion, restart, packed-world restoration and deterministic replay, then compares the exact native/web world hashes. The web test uses the repository's WASI runner. Use Node 24.16 or newer on Windows, following `notes/testing.md`.

The game itself can serialize and restore its world, as the tests demonstrate. The ordinary browser preview does not yet expose that state to the surrounding service; a feedback pin must not claim to contain a world snapshot until that host boundary is implemented.
