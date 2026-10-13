# HOT SWAP

A solo/two-player warehouse heist in Tide. You are an electrical creature:
borrow powered cleaners, forklifts, cameras and door controllers, free the
captive storm, and deliver its case to the green getaway bay.

This first demo targets a 10–15 minute discovery run. An experienced player
can finish much faster. First-play duration and whether the cooperation is fun
still need human playtesting.

## Play

Use a desktop browser with keyboard and mouse. **Play solo** starts the heist.
**Host a friend** opens a room; copy its six-character code at the upper right,
and have the other player use **Join a friend** in the same build. Both players
need access to the served page. A localhost URL is only reachable on that PC.

| Control | Action |
| --- | --- |
| WASD / arrows | Drive a mobile body; aim a camera |
| Click a glowing machine | Possess it |
| Tab, then Q | Select a reachable body, then jump |
| E | Body action; hold to scan with a camera |
| Space | Foam, horn/lift, camera flash, or conveyor reversal |
| M | Warehouse map; you can choose jump targets on it |
| H | Controls and body abilities |
| V | Mute/unmute |
| Escape | Room menu, including New game |

Gamepad: left stick moves/aims; South uses, West performs the special action,
North jumps to the selected/nearest reachable body. The menus use Tide's
ordinary gamepad navigation. Full gamepad play has not been hardware-tested;
this prototype has no touch movement controls.

Nearby possession requires a clear path within 7.5 metres. Fixed cameras and
controllers on the same visible cable circuit also permit distant jumps.
One creature per machine; changed routes and vacated machines stay in place.

## The job

Copy the manifest at Records, using a camera or a mobile body. Power the clamp
by placing a pallet on the amber weight plate, or send a cleaner through the
upper service duct to the bypass terminal. Either combination releases the
storm case. Taking it triggers a search and closes the main shutters.

Three exits work:

- Tow the case through the upper ducts with a cleaner.
- Carry it on a forklift through reopened loading shutters.
- Drop it on the shipping belt and reverse the belt from a controller.

Cleaners leave foam that trips guards. Cameras can flash and distract them.
Forklift horns draw attention and raise nearby cleaners for fifteen seconds;
a raised cleaner can cross the service catwalk above the secure fence. Door
settings persist, so a solo player can park cargo, operate a door and return.
Two players can prepare an exit while the other moves the case, or distract
guards for each other. Getting caught reboots your body and transfers your
spark to another powered machine. The loot stays where it was.

## Build

Configure with `TIDE_PRIVATE_PROTOTYPES=ON`. Native debug targets:
`hot_swap` and `hot_swap_tests`. Web debug/release adds `hot_swap_page`, which
stages the self-contained page at `docs/static/prototypes/hot-swap/index.html`.
The game includes frame-rate stats. Serve `docs/static` on localhost to play
`/prototypes/hot-swap/`; use a server with COOP/COEP headers for browser threads.
The optional `?silent` page URL leaves the browser audio context suspended
for unattended previews. Ordinary play has sound and room volume settings.

Run `ctest --preset debug -R '^hot_swap_tests$'` and
`ctest --preset web-debug -R '^hot_swap_'`. The latter also drives two muted
browser players through a local relay on WebGL and WebGPU. All authored sounds
are synthesized by `sfx/make_sounds.py` using the Python standard library.
Implementation contracts, evidence and remaining gaps are in
[`notes/hot-swap.md`](../../notes/hot-swap.md).
