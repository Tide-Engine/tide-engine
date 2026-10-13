# DEADWEIGHT

Two workers recover enormous machinery from Pump Station 04. Cargo is a rigid
body: it can become a bridge, lift a shutter as a counterweight, swing into a
worker, or jam under a lintel. Three jobs share one industrial hall.

Play on desktop or in a desktop browser. The title's **Host a room** and
**Join a room** use Tide's room service. A second player takes worker B; the
remaining player controls both when someone leaves. **Play** also supports
two people on one keyboard, or switching between workers alone.

| Action | Main controls / online | Shared keyboard worker B |
|---|---|---|
| Walk | WASD | Arrow keys |
| Take a winch / attach hook | E | Right Shift |
| Reel in | Q | Comma |
| Pay out | F | Period |
| Unhook | X | Slash |
| Switch worker alone | Tab or Swap button | — |
| Reset current job | R | R |
| Field guide | H | H |

A gamepad uses the left stick, South to interact, right/left triggers to
reel/pay out, West to unhook and North to switch worker alone. Scroll zooms
the camera. This first demo has no touch movement controls.

Each worker starts with a rigged line and a winch radio. Letting go of the
controls keeps the chosen cable length: a suspended load stays supported,
while cargo on the floor can still coast towards its anchor. X takes the
hook off; walk within reach of a highlighted gold socket and press E to
reattach. E at another numbered console claims that winch; the old line
remains rigged and braked. A partner's active winch cannot be taken.

1. **The conveyor spine:** correct the skew through the doorway, stop while
   it spans the flooded trench and try walking on it, then haul the whole
   nine-metre section onto the loading sled.
2. **The turbine drum:** worker A pays out the ballast, which raises the
   shutter through its pulley cable. B hauls the drum through. Once it is
   settled on the sled, reel the ballast back into its rack.
3. **The pressure vessel:** A's roof hoist supports the nose; B's floor
   winch tips and pulls the vessel under the low beam. Keep enough support
   to prevent it nosediving into the sled ramp, then let it settle on its side.

The whole load must rest inside the green sled outline for a little over a
second. The job buttons let you practice in any order. Falls and impacts
briefly tumble workers and recover them nearby; cargo lost in the trench
returns to its starting position. R resets the job without clearing completed
job marks. There are no timers, cable breaks or cargo damage.

The first demo uses straight, tension-only cables; they do not collide with
walls or wrap around pillars. Cargo uses box collision envelopes. The
counterweight and shutter have vertical guides. These are prototype limits,
not additions to the Box3D package or Tide runtime.

From this repository, configure with `TIDE_PRIVATE_PROTOTYPES=ON`, build
`deadweight` natively or `deadweight_page` on the web, and run the
`deadweight_test`, `deadweight_network_test` and `deadweight_render_test`
CTest tests. `deadweight_render_test_webgpu` also runs in the web build.
`tide run` in this folder reads the two packages from `tide.packages`.
