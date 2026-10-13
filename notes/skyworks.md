# SKYWORKS: voxel automation

## First iteration, 2026-10-13

The owner requested original voxel automation: manual gathering, crafting,
machines, transport, detection, redstone-like logic and liquids. They proposed
small floating islands and eventual dimensions, and authorized recommendations
and a first iteration, with engine limitations recorded. No other prototype's
game source, template, test implementation or art was read. Shared build/catalog
files, engine headers and public API documentation were used.

This asset-free single-player slice has a 48 × 12 × 48 block world, three islands,
logs, finite copper seams, crystals, a spring, mining, walking/jumping, building,
crafting, drill/tube/kiln/crate production, pump/tank volumes, switches/wires/stock
sensors and a beacon goal. Wind supplies power without a fuel/generator network.
The beacon needs twelve automatically produced ingots plus twelve spendable
ingots, four crystals and eight water. Manual smelting cannot satisfy the
automation milestone. Player instructions/recipes are in the game's README.

## Contracts

- `world.tide`: all terrain, reserves, player inventory/position, machines,
  signals and milestones belong to the match. Byte `Grid3<Block>` terrain;
  `Grid2<Machine>` stations; a bounded list indexes 256 installed stations.
  No game C or packages. The standard host supplies engine loopback networking.
- `look.tide`: local UI, cached terrain and mesh lists only. Exact cell comparison
  protects reuse when rollback replaces terrain at the same revision. Only
  exposed faces are meshed, wound outward and shaded by face color. Ordinary
  engine mesh/primitives, camera, depth, screen drawing and GUI render it.
- Thirty ticks/second; deterministic axis-separated AABB motion with gravity and
  jumping. DDA picks a block/face within six units. Falling preserves inventory;
  the return pad cannot be mined or obstructed. Machines need surface support;
  dismantle them before mining their foundation. Dismantling refunds contents.
  Aiming at a floor top extends planks horizontally as a bridging aid.
- `Craft` checks connection sender, recipe range, affordability and beacon
  milestones before spending. Continuous actions and interaction edges use
  sampled bounded input. The workshop uses commands and runs factories normally.
- Copper loses one reserve per manual harvest/drill extraction. Other harvested
  voxels are removed. First factory costs 13 wood, 8 stone and 6 ore including
  manual smelting; starter resources support the complete beacon route.
- Every fifteen ticks, sources/processors run and outputs transfer one unit.
  Incoming units cannot travel a second hop that step. Capacity/type guards
  preserve backpressure and stock. Tubes/crates hold 24, kilns independently
  hold 24 input/output, tanks 64. Installed-site order arbitrates competing inputs.
- Signals reset, seed from switches/sensors, then propagate wire OR to convergence
  within the installed-cell bound. Sensors test front stock >= 8 before processing.
  Adjacent wires gate machines; unwired machines run freely. No timed logic.
- Water is integer volume through tubes, produced by a pump above a spring.
  Spreading liquid voxels and pressure are not built.
- CMake lists game sources explicitly to exclude the C test hosts. The CLI has
  `test/tide.packages` as a folder boundary. `skyworks_page` stages the public
  page and `private_games` collects it; nothing installs into the Tide package.

## Engine gaps and workarounds

No engine/compiler change or new runtime function was required.

| Missing convenience | Actual workaround / consequence |
|---|---|
| Voxel meshing/ray queries | Game-owned exposed-face builder and DDA over `Grid3`; no greedy meshing or voxel LOD yet. |
| Block-world character controller | Game-owned deterministic AABB motion; step-up/crouch/ladders/moving platforms aren't built. |
| Tide-visible grid change identity distinguishing snapshot branches | Exact comparison with a cached local grid: 27,648 cells/frame. A cheap chunk-change/read-version interface would avoid this scan in larger worlds. A revision counter alone is wrong after rollback. |
| Grids cannot be function parameters | Helpers receive `Island` explicitly, preserving caller capability, instead of accepting a reusable grid argument. |
| Automatic lighting | Terrain face colors and a machine palette. Custom shaders/targets can supply lighting/shadows; their absence is game scope, not a renderer blocker. |

Tide already supplies persistent files and generated C world packing. Saves
aren't an engine blocker: this game simply has no save format/restore UI yet.
Portals can use existing camera/shader/target APIs later. Keyboard/mouse and a
minimum 960 × 540 layout are first-iteration scope; mobile/touch/gamepad movement,
multiplayer, renewable forestry, branching/filter tubes and dimensions aren't
built. The recommended next work is saves, build previews/in-place rotation and
a guided start, followed by a sawmill/renewable forestry, branching transport,
water-consuming recipes and richer circuits. Energy/pressure and dimensions
remain proposals, not approved engine requirements.

## Validation

Native/WASI simulation tests cover valid/invalid/duplicate crafting, conservation,
one-hop transfer, backpressure/types, switched production, sensing, water,
snapshot copy/pack/restore, walking/jumping, mining, recovery and input repair. The 84-check
journey gathers actual logs/stone/ore, spends them on machines, places/runs a line,
collects copper, builds an east bridge, mines crystals, assembles a pump line,
collects water, and crafts/places/dismantles a beacon. It teleports between
interaction vantage points rather than verifying walking the entire journey;
it grants no inventory or machines. The independent seeded factory fixture hash
after replay is `cfbcdd0138c94509` on native and WASI.

Render tests draw the game/workshop on OpenGL, WebGL and WebGPU (web validation
enabled), and replace terrain without changing revision to verify local cached
geometry updates. Native captures and collaborative preview checked the initial
view/workshop. The catalog thumbnail is a declared factory fixture through the
game's real views, not starter equipment.

Local final validation: native debug's simulation and OpenGL rendering tests
pass; web debug's WASI simulation, WebGL and WebGPU tests pass with Node 24.16.0.
The render fixture checks Tab opening/closing the workshop and cache correction
at an unchanged terrain revision. The factory hash is pinned in the test.
Docs Svelte check has zero errors/warnings; all eighteen analytics/admin tests,
production build and production prototype HTTP test pass. The docs build uses
the existing allowed placeholders for other unstaged games/examples. No CI or
deployment was requested. Other operating systems, Android and a real phone
were not run; no multiplayer implementation/coverage is claimed.
