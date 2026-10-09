# Cabin Nine private prototype

Built 2026-10-09 at the owner's request: a private PSX horror prototype with at least two players, a considered game design, and an initial playable build. The chosen first version uses exactly two human cabin crew on a small night charter flight. The complete experience's target is a 30–60 minute blind first playthrough, centred on 45 minutes; this duration has not been measured or achieved by a playtest of this first build.

## Source and build boundary

`prototypes/cabin-nine/` is separate from `demo/`, `examples/` and `packages/`. `TIDE_PRIVATE_PROTOTYPES` defaults to OFF in the root CMake configuration. Turning it on builds `cabin_nine`, `cabin_nine_tests` and `cabin_nine_render` when the platform layer is enabled, on native and web builds. It does not install the game into Tide's package. The engine repository was verified private before merging this work. No publishing or workflow dispatch is part of this task.

The owner's same-day request for an admin section for prototypes adds `cabin_nine_page` and includes it in `private_games` when the opt-in game is enabled. Their subsequent sharing request makes the game public at `/prototypes/cabin-nine/`, staged at `docs/static/prototypes/cabin-nine/index.html`, with no admin sign-in required. The catalog remains admin-only. Old game addresses are unsupported, as the owner requested. The serving contract is in `notes/docs-site.md`. The docs workflow explicitly enables the option; deployment still requires a separate manual run.

`DESIGN.md` records the proposed fuller experience and its unresolved production risks. `README.md` records actual launch instructions and controls. Neither the working title nor the proposed language of the fiction changes a shared engine rule.

## What is built

- A procedurally authored 3D charter cabin with passenger seats, galleys, faceted windows, a lavatory door and a rear holding compartment. A 480 by 270 scene target, restrained colour quantization, fog, worn materials and changing lights establish the PSX presentation. Native-size text preserves the readability of evidence.
- Eight recurring human identities and an extra actor that copies a different human in each of four investigations. Its displayed identity and seat change; internal actor IDs are never shown. A successful containment produces a visible unmasking, and the successful ending brings dawn outside the cabin.
- Two first-person crew bodies, owner-bound input, the Rooms menu, two independent ready acknowledgments, and a clock that pauses when the crew count or readiness is insufficient. On disconnect the other crew's ready flag and signatures reset; a new partner can resume the existing case after both become ready again.
- Boarding introductions for all eight humans, four authored continuity puzzles, two physical record stations, a shared notebook, readable field inspections and dialogue. Earlier and later records refer to independently witnessed changes in a dressing, a cap repair, glasses repair and a paper tag. Neither nervous behaviour nor a disability supplies evidence of impersonation.
- Distance-validated typed commands. Both crew must contribute to reading records and must independently confirm a verdict; one actor cannot supply two signatures. Either crew can visit either station. Interviewing and inspecting the candidate precedes a proposal, and the candidate must occupy the rear holding space before confirmation.
- One reversible holding space. Incorrect confirmed verdicts release the innocent actor, clear them for that case and reduce trust once. Clean success, success with mistakes and loss of crew authority have separate reports. The chapter can restart without restarting the executable.
- Original synthesized engine/electrical ambience, chimes, an outage sound and an arrival tone. `sfx/make_sounds.py` produces the checked-in WAVs. No network voice subsystem or external character assets are required.

## Code contract

`game.tide` owns match state and commands. `story.tide` owns authored names, baseline descriptions, testimony and observations. `show.tide` owns all meshes, shaders and local audio. `hud.tide` identifies the local crew, displays records and sends requests; it never writes the match.

The seating graphics pass (2026-10-09) replaces independent chair/body coordinates with shared contact planes and `PassengerPose` landmarks, relative to each passenger's floor position with forward along -z. The cushion spans y=0.35..0.50 and its support ends at 0.35. The seated pelvis/thigh centre is 0.62 with a 0.12 radius, resting on the cushion; knees sit at z=-0.55 ahead of its -0.405 front edge. Shoulder and head positions leave clearance from the back padding and headrest, and soles retain their 0.01 floor clearance. Bent forearms and hands rest over the thighs. Whole-body breathing translation was removed so feet and seat contact remain fixed.

Head, shoulder and wrist landmarks also place the visible evidence and clothing in both seated and upright poses. Rowan's dressing follows the actual forearm instead of retaining a seated z offset when held. The collar mesh is now shoulder-relative. Standing crew/passenger proportions and the confirmed copy's unmask pivot retain their previous placement. Future pose or seat changes must keep these contact planes and accessory anchors together; this is authored static geometry, not a skeletal animation system.

Match and command handlers use `Roster.crew` and `Roster.passengers` with `mut World` and copied component values. They do not use mutable query rows or handler queries, which Tide does not support. The roster lists and components must change together when creating, replacing or destroying actors. `Flight.readers` and `votes` are crew-seat bitmasks; a rejoin invalidates both. Movement stays within the cabin's simple aisle rather than depending on a new character controller or navigation system.

NPC truth is concealed through presentation for cooperative players. Tide currently sends the entire match to each client; this game does not claim secure hidden human roles or private-state filtering. Voice communication may be external; the shared text evidence remains available without it.

## Validation

The prototype's simulation tests exercise the real packed command decoder and deferred handlers: minimum two players, both ready, unjoined/invalid/out-of-range requests, both record readers, distinct signatures, reversible and nonrepeat penalties, disconnect and replacement, all four investigations, all ending outcomes, restart and input bounds.

The rendering fixture draws boarding, outage, all four investigation appearances, the notebook and the final report with pixel readback. It exercises the same scene shaders on native OpenGL, WebGL and WebGPU. Colour variation checks detect an empty/flat frame; they do not prove artistic quality or text comprehension. Native headless runs with two actual program instances provide separate first-person captures for visual inspection.

Validation on 2026-10-09: the native full build and its 2,009 existing CTests passed; the web full build and 1,982 CTests (including the initial prototype simulation target) passed. The final private targets were then rebuilt without warnings and passed all eight simulation cases on native and WASI, plus rendering on native OpenGL, WebGL and WebGPU. Two-instance scripted captures were visually inspected at 1280 by 720 and 960 by 540; boarding descriptions and the notebook retain the pre-incident baseline. No CI workflow was dispatched. These checks do not substitute for a blind two-person playtest of deduction, pacing or fear.

The seating pass rebuilt all three private targets on native and web without warnings. Its own native CTests (simulation and rendering) and web CTests (WASI simulation, WebGL and WebGPU rendering) passed. `test/seating-capture.txt`, run with `--players 2 --script <copied script> --size 1280 720`, reproduces front, lap/feet, opposite row and rear views; copy it into a scratch folder to keep PNG output out of the sources. Before/after captures were visually inspected for thigh, calf, hand, shoulder and hair clearance. A further two-instance scripted run welcomed all eight passengers and escorted genuine Rowan during the first investigation; captures verified his dressing stays attached to the upright wrist. The render smoke test checks nonempty output, so visual inspection supplies the seating assessment.

## What is not built

The initial chapter is shorter and simpler than the planned polished 30–60 minute demo. It has no autonomous stalking, purposeful NPC walking, performed repair interactions, inventory/service puzzle chains, live conversation adaptation, secure voice imitation, character animation pipeline or physical landing sequence. The four fixed puzzles repeat one family of before/after reasoning. The boarding phase and written witnesses establish changes, rather than every change occurring as an observed animated action. Further content and measured playtesting are necessary before claiming the requested demo length and polish.
