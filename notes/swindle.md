# SWINDLE: competitive rooftop heist

Built on 2026-10-13 at the owner's request for a surprising, polished competitive
prototype to show publishers: friend versus friend, with quick, replayable
matches and freedom to choose fighting, sabotage or deception. The heist rules,
name and art direction are this prototype's choices, not changes to Tide.

## Rules and state

`prototypes/swindle/game.tide` owns one deterministic match singleton, `Heist`.
Four fixed thief/replica slots, participant entities with explicit owners, and
position-tagged `Figure` mirrors keep input ownership and visual interpolation
separate. Simulation runs input, play, mirror in declared phases at 60 Hz.
No C, runtime API, physics-package or compiler changes.

- Three-second countdown, ninety seconds of play, then results and an explicit
  rematch. A tied scoreboard enters sudden-death overtime; the next cash-out
  that produces a unique leader ends it.
- The prism starts at the central vault. Touching picks it up. Carrier speed
  is 188 versus 225 world units/second; its value is `min(8, 2 + floor(charge/3))`.
  Charge follows the prism through steals. Cash-outs clear it and return the
  prism after 1.2 seconds.
- A 0.17-second dash moves at 860, recharging after 1.35 seconds. Enemy contact
  gives a 0.4-second stun, decaying push and 0.6-second hit protection. A steal
  gives the new carrier 0.65-second protection. Stable seat order resolves
  simultaneous contacts. Continuous-looking visual movement is interpolated;
  the tick's movement and collision stay authoritative.
- A fake runs in the user's facing direction at 235 for 2.6 seconds. The user
  vanishes for one second; recharge takes 5.5 seconds. Enemy dashes into a fake
  burst it, stun the attacker for 0.65 seconds and count a successful bluff.
  Online opponents see an identical fake. Its owner sees their ghost and a
  YOUR FAKE label; shared keyboard play shows both ghosts and omits that label.
  This is visual concealment, not network secrecy: clients still have the
  whole match as Tide's current networking contract requires.
- One mint getaway among four rotates every twelve seconds; Gallery goes
  counterclockwise. Banking requires 0.65 seconds within 42 units, without
  being stunned or dashing. The UI shows progress and the next movement time.
- Duel, four-way FFA, or 2v2 with even/odd seats as teams. Teams share scores
  and have no friendly stealing/stuns. All humans use the same ordinary
  player-owned input path. Couch seat one comes from the host's second input.

Joined players replace open bot seats. A third player joining a duel expands
it to FFA without discarding existing scores. The host seat is the first human,
transferring to the first remaining human on departure; only it can send valid
Deal/ReturnToLobby changes. Unsupported mode/couch combinations are ignored.
Leaving releases participant ownership, drops a carried prism and gives an
empty seat to a bot when enabled. Inviting online friends from couch play
returns to Ready and releases the reserved keyboard seat.

## Replay and opponents

Rematches cycle three obstacle layouts: Penthouse planters, Gallery dividers,
Terrace long hedges. Four exits, preserved stolen value, carry slowdown,
dash recharge, deception and team escorts create the changing decisions.

Bots use a deterministic visibility graph over sixteen expanded obstacle
corners plus their start/goal, using slab segment intersection and Dijkstra.
Routes refresh every 0.3 seconds, at a reached waypoint or after a substantial
goal change. Carriers route to the getaway, opponents chase/steal, teammates
escort and intercept. A plausible nearer fake attracts a bot until revealed.
Carriers bluff/dash when threatened. The first greedy single-corner navigation
failed the full-match scoring tests and was replaced; all nine mode/layout
combinations now bank regularly.

## Presentation and packaging

`art.tide`: art deco cream/brass parapet, tiled rooftop, original fox burglars
with suits/masks, city skyline, lanterns, faceted prism, stable foot-depth order,
dash trails, ghost/fake rendering, bursts, floating callouts and brief shake.
`ui.tide`: illustrated title, custom GUI controls, modes, rooms, instructions,
scoreboards, cooldowns, results and one-click rematch. Menus use the Rooms
package's flow with overrides. A new round closes the room page for members.
The title and room scale their drawing/input rectangles together for short
screens. Short landscape play moves scores/cooldowns to the left and getaway
information to the right, reserving the height for the rooftop.

`sound.tide` uses eight original arithmetic-generated WAVs and a quiet 108 BPM
lounge loop. `sfx/make_sounds.py` reproduces every source with fixed seeds.
The music toggle is local; the Rooms volume settings still apply. Audio is
presentation only. Native agents use TIDE_HIDDEN and captures; browser fixtures
always launch with --mute-audio.

The opt-in `swindle` target has STATS. `swindle_page` stages the self-contained
page at `/prototypes/swindle/`; `private_games` collects it. The server-only
catalog, three public/asset patterns, admin tests and 960x540 gameplay thumbnail
register the game. It remains absent from public Samples/search/sitemap.
`test/tide.packages` excludes C hosts from ordinary CLI game discovery.

## Validation

Seven C test groups run the actual generated simulation: host/invalid-command
guards, player departure and host transfer, countdown, second keyboard input,
pickup/steal/preserved charge/banking, replica hit and recharge, team protection,
exit changes/overtime, bounds/obstacles/nonfinite input repair, all nine complete
bot rounds, and snapshot/pack/replay equivalence. Nine final world hashes are
pinned and agree between Windows debug, optimized native and WASI debug.

Browser tests use two independent muted headless Chrome windows, a real local
relay and unordered/unreliable WebRTC. They exercise rendering with validation
enabled, hosting/joining, friend movement/dash/bluff, a full round with matching
winner images, rematch, couch controls and landscape touch. Their controlled
clock separates simulation time from software GPU compilation/capture waits.
Shared fixture helpers currently live under Inside Out's `test/` folder.

Native scripted captures cover a full heist, effects, results and nonzero audio;
the collaborative browser has drawn the game at 1280x800 without console errors.
Completed local validation:

- Windows native debug and optimized builds: 7/7 simulation groups each.
  WASI debug and release: 7/7 each. All nine fixed world hashes agree in all
  four configurations. No engine/compiler/platform source changed, so these
  are the game-scoped suites required by the repository guide.
- Complete optimized browser fixture: WebGL passed in 161.94 seconds and
  WebGPU in 34.65 seconds, including the real two-friend room, movement,
  dash/bluff, full round, winner agreement, rematch, couch and landscape touch.
  Browser diagnostics and Tide.checkGL validation are clean. Winner text pixels
  allow at most sixteen channel differences of two low color bits: independent
  software GPU contexts differed at one glyph-edge channel by one bit.
  The fixture closes the friend window before its separate couch test, rather
  than interpreting the expected host-migration diagnostic as a shader error.
- Final short-landscape layout checked by native scripted capture at 844x390,
  alongside the 1280x800 title/full-match captures and 960x540 catalog capture.
  The exported Windows executable loads its adjacent pack and captures its
  title. The 95-second audio capture has 9,085,786 nonzero samples and peak
  24,774/32,767; no physical playback device was used.
- Docs check: zero errors/warnings. Analytics/admin: 18/18. Production docs
  build and public-prototype production HTTP test pass on Node 24.21.0. The
  normal allowed 404s concern other unstaged examples in this fresh worktree.
- `build/SWINDLE-demo.zip` contains an optimized Windows executable/pack,
  self-contained browser page, START-HERE instructions, source README and
  optional dependency-free localhost launcher. The staged, exported and
  production-package SWINDLE HTML is checked for identical bytes.

Export/staged/production HTML SHA-256:
`1ac076f347e22f8345a91c32748185b6ef99e51b51f862423e686a7482b12d49`.
Demo ZIP SHA-256:
`b50e7d316c6af68e78d45c515fa92b8b4209047f5877ff590c16987d18e930eb`.

## Remaining coverage

No persistent progression or matchmaking queue. Wider balance testing is still
needed with humans; this session is not a substitute for publisher feedback.
No real phone, gamepad hardware, macOS/Linux/Android, adversarial secrecy or
cross-network TURN/NAT playtest. Those are not implied by the local two-window
tests. No CI dispatch or website deployment is part of this request.
