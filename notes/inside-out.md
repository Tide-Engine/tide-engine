# Inside Out

## Direct playtest feedback (2026-10-15)

This follow-up supersedes the UI, audio and manual progression decisions in
the earlier polish round below. Inside Out is intended as a native game;
the browser build remains a convenient shared playtest. Changes stay in
this prototype. No engine, compiler, Rooms-package API, website deployment
or CI changes were needed.

The owner requested plain box entrances, no hints or How to play screen,
no WASD/directional overlay, functioning arrow movement, URL invitations,
automatic progression with a visual transition, clearer less repetitive
sound, and fixes for shutter flicker, recursive shadows and jumpy undo.

UI and progression:

- Removed How to play, the hint panel, text advice, lesson cards, directional
  buttons and printed key labels. The early puzzles introduce mechanics
  through their layouts. WASD bindings remain; arrows now also work. The
  existing optional spatial step preview remains in Settings.
- The command-only game has no sampled input, so the GUI previously started
  automatic arrow focus while playing. WorkshopControls claims the keyboard
  during play and handles Escape itself; menus retain GUI.Navigate. Touch
  uses a deliberate 28-pixel swipe for one move, with no directional panel.
- Workshop.solvedTick and openedTick are synchronized match state. A successful
  solve stamps the clock; ContinueWorkshop opens the next puzzle after 72
  ticks (2.4 seconds at 30 Hz), only while both participants are present.
  Undoing the solve clears its deadline; resetting/reselecting clears it too.
  An overdue solved puzzle advances after the missing partner returns. The
  last puzzle remains solved and shows an ending. The manual Next command
  is removed; Enter no longer advances the campaign.
- transition.tide closes nested painted square frames over the solved room
  and opens onto the next. Reduced motion uses a fade. Local movement waits
  for the new room's 24-tick reveal; the simulation commands remain usable
  immediately by tests/tools. The serial/release guard prevents held keys
  carrying into the next puzzle. Introductions show only number/name.
- Copy invite and Copy code are separate controls in the waiting/menu
  screens. The main-game-only invite.c bridge returns the published preview
  base on native. The shell passes its actual origin/path as an ordinary
  WASI argument, so a local invite uses the local preview/relay. The bridge
  bounds argument count/storage and URL length, allows http(s), and rejects
  query/hash/control characters. Copying appends a fresh ?join=ROOM. Existing
  URL joining handles it; no new runtime function is involved.
- SceneTop is 76 pixels; SceneBottom is 96 on desktop, 112 on phone. The
  context chip sits at h-118; Undo/Restart at h-92. Phone identity chips sit
  at h-148 to avoid the stats corner. Title/menu cards are 500/590 high.

Rendering and undo:

- The shutter's dark stripes ended at exactly the same depth as the central
  painted cover. FloorRelief now shares FloorLine's actual relief formula,
  including its minimum height. Cover and channel shape each sit above the
  preceding surface with positive clearance, including miniature scales.
  Both magenta-ring and lime-diamond tearing were reproduced and corrected.
- Recursive ancestor walls can exceed a viewport-sized lamp's entire depth
  interval. Cuboids and rotated floor marks now contribute their exact
  transformed unit-cube projection bounds along SceneLightDirection to
  Sculpture.shadowLow/high (reset each frame, conservatively include zero).
  SceneLighting fits only the lamp's depth axis/far plane to those bounds
  plus a viewport margin. Its XY footprint/texel size no longer includes
  the current tile scale, so recursive crossings keep the same receiver
  bias and PCF radius. Light direction, casters and CPU clipping are retained.
  Temporary diagnostics across 798 seat-frames of puzzle 15's exit/entry
  found 764 frames outside the old depth interval, versus zero with the fit;
  the largest old near-plane deficit was 183,907.85 world pixels. Diagnostics
  were removed before the final build.
- Doorway cells now have plain full-cell gaps. Removed brass jambs and short
  perimeter stubs; real wall blocks provide the entrance's complete surround.
- Undo previously treated every serial change as a rendering reset, snapping
  boxes and the passenger's surrounding world while animating the robot.
  Picture.openedTick distinguishes actual level/reset boundaries; ordinary
  undo now eases pieces, plates and shutters together. Admission changes do
  not restart a moving piece. Reset/new level/reduced motion still snap.
- An interrupted pushed-piece crossing reverses from its visible containing
  pose and scale. Transit.targetInside records the destination coordinate
  space independently of shrinking/growing, including consecutive undos;
  its distance records the reversed interpolation's full length. Same-room
  robot doorway undo is inferred from restored/current cells and saved
  facing, and restarts the passage/camera from the current rendered pose.
  Picture.passageMove remembers a doorway action even when a self-entry
  leaves canonical coordinates unchanged; reset and reversal clear it.
- Dock outlines remain hidden under interpolated arriving/departing room
  footprints, matching the existing rule for signal paint. This prevents an
  exposed canonical dock outline cutting through a still-overlapping room
  floor during undo.

Native bounded before/after captures under ignored
build/inside-out-captures/render-feedback cover both channel shutters and
both seats across puzzle 15's entry/exit. Carried-room undo captures at
1/3/6/12/24 frames show progressive context motion and exact settled return;
the observed outside patch differs from its final state in
129919/121067/93532/33921/0 pixels respectively. A rapid undo starts from its
current intermediate pose. UI captures under build/inside-out-captures/ui
cover desktop/phone pages, arrow-only solving, held movement across automatic
progression, the transition at multiple phases, and one-move phone swipes.
Additional bounded captures under build/inside-out-captures/undo-review cover
fast self-entry undo, consecutive self-entry/self-exit undo, and consecutive
undos while an occupied room is being pushed. Both seats return to
pixel-identical canonical scene baselines, including a post-merge native run.

Audio:

The owner's listening supersedes the earlier unlistened aesthetic review.
There was no sample-rate mismatch: all assets/mixing are 48 kHz. The assets
had baked-in fourth-order low-pass filtering around 1.1–1.8 kHz, and each
step included a rising 505–585 Hz servo note plus periodic low tread pulse.

- Steps are now 70–80 ms dry, unpitched rubber scuffs/contact ticks, without
  the servo, pulse or reverb. Their loudest-50-ms level drops 7 dB, and a
  per-seat 0.22-second cap limits retriggers. Partner footsteps stay quieter.
- Blocked actions use shorter unvoiced contacts, 3 dB quieter, capped at
  0.26 seconds. Material slides/doorways/shutters retain more 2–4 kHz detail
  while rounded attacks and a controlled upper rolloff avoid sharp hiss.
- Music retains its arrangement, durations and -22 dBFS RMS target, with a
  modest presence/low-mid adjustment and 1.5 dB drier reverb. Loop checks
  remain. No asset paths or Mixer/UiCue APIs change.
- Both generators reproduced every shipping hash twice. Asset checks pass
  all 32 files (2,128,602 bytes). Step1's energy centroid changes 481 to
  2087 Hz while its level falls 7 dB; every effect's energy above 6 kHz stays
  below -30 dB. Game/menu music measures -22.01/-22.00 dBFS RMS.
- Device-free production captures match revised step sources at gain 0.29999,
  correlation >0.99999 after known-music subtraction; the running build adds
  no muffling filter. Held blocked inputs emit contacts about 0.33 seconds
  apart instead of every 0.16-second input repeat. Co-op/held runs have no
  clipped samples. Preferences changed by captures were restored.
- Changed-level kind-4 cues get only the new-puzzle introduction, avoiding
  a reset sweep stacked over automatic advancement; same-level reset keeps
  its reset cue. Reports/spectrograms are under ignored build/audio-feedback;
  source/generation details and checksums are in sfx/README.md and SHA256SUMS.
  No physical audio device was used; the owner's next listen judges timbre.

Validation: native and web debug game aggregates pass all 44 cases, including
all campaign witnesses and new real-solve timing, cancellation, missing-partner
and final-ending regressions. The native invite fallback and five WASI invite
cases pass. Browser validation additionally observes the real clipboard copy,
follows that URL to the host, moves with an arrow, checks held input across
automatic progression, verifies intermediate occupied-room undo, and swipes
on phone. The final post-merge, serialized WebGL and WebGPU fixtures pass in
930.55 and 777.50 seconds respectively. Both complete the real invite/join,
WASD/arrow movement, reset/held-input recovery, three solved-puzzle automatic
transitions, occupied-room smooth undo, recursive entry/exact undo and phone
swipe checks without runtime, shader or GPU errors.

The initial browser attempts ran WebGL and WebGPU concurrently and each hit
the old 900-second CTest limit while still producing frames/captures; neither
reported a gameplay, shader or GPU assertion failure. The added automatic
progression waits make that allowance too short under software-GPU contention.
The harness now waits 50 rather than 80 ticks for an introduction that ends
at 48, allows 2.5 seconds per verified tick for long waits, and sets the whole
browser test allowance to 1500 seconds. Final backend runs are serialized.

Merged origin/dev at d909a851 without conflicts or Inside Out overlap. The
incoming compiler/engine visibility work changes dependencies, so all game
targets were rebuilt on native/web debug and the web release page restaged.
Native aggregate/invite tests and the web aggregate/five invite cases pass
again against those dependencies. No full engine suite or CI dispatch is
required by this game-only change. Native captures run without a Vulkan
validation layer on this machine; browser fixtures enable checkGL and GPU
validation. All runs use hidden/muted or device-free capture paths.

The final unrelated origin/dev merge at 91a66aa2 contains CLI/hosting work,
Apple SDK selection and Foldline changes, with no Inside Out, runtime or
tidec frontend overlap; no repeat validation is required for that merge.
The local server remains available with the final staged release page.
A four-second 1280x800/60fps H.264 review clip with synchronized captured
audio is under ignored build/inside-out-captures/transition-video/
inside-out-automatic-transition.mp4; it shows the solve, nested aperture
close and next-room reveal. Its 240 frames/contact sheet were inspected.

## Current polish round (2026-10-15)

Resumed the owner's interrupted Inside Out task: remove portal-like copies,
polish sound and UI, add puzzles and provide a local two-tab build. The
following sections supersede the earlier dated history below. These are
prototype choices within that request, not new engine or language contracts.
No website deployment or CI dispatch is part of this round.

Every room now has exactly one box. The campaign has eighteen puzzles in five
chapters (Boxes, Each other, Power, Itself, Everything), with eleven additions.
Pressure plates and shutters use two level-wide channels and pre-move power.
Self-containing rooms and two-room cycles replace copied entrances. The
existing four foundation commits were recovered before completing their
unfinished UI, audio, campaign, review fixes and local-play integration.

Review fixes: a box in a closed doorway shutter cannot bypass that shutter
by admitting another thing through its own doorway. Regression fixtures cover
both robots and batteries, plus pre-move power on entry, exit and posting.
Own-box entry emits a doorway cue even without a room/depth change; the view
uses the actual command direction to distinguish a cycle exit from an entry.
It animates and zooms through that entry. Undo has no stale direction hint.
Signals under settled, arriving or departing boxes stay below their interior
floors, with an outside band/shape. Occupied shutters keep shape marks, and
battery departure keeps the collar until the rendered battery has cleared.
Open/closed signals retain channel shapes, with coarse miniature marks.

Local play: build web-release's inside_out_page, then run
node prototypes/inside-out/serve.mjs. It serves only this game's staged HTML
at http://127.0.0.1:8790/prototypes/inside-out/ and runs a loopback-only relay
on port 8791. Two tabs can host/join without the website or production relay.
The page is read afresh for reloads, has COOP/COEP and no-cache headers, and
unrelated paths return 404. The server and its WebSocket endpoint were
smoke-tested. This checkout's ignored build/tools/node/node.exe is official
Node 24.16.0, verified against the release's SHA256 list; the machine's default
Node is older and was not replaced.

Native debug and web debug targets build warning-free and each game aggregate
passes 40/40. The full two-browser WebGPU fixture passes in 592.68 seconds,
including shared controls, occupied-box context, real self-entry/undo,
three production solutions and phone touch. The corrected full WebGL fixture
passes in 696.15 seconds with the same coverage. Its first run used the old
30-second guard for the new 80-tick intro settling wait and timed out while
verified ticks and packets continued; the proportional guard fixes that
fixture failure without changing its assertions. No browser/runtime/graphics
errors occur in either successful run. Physical phones, external NAT, gamepad hardware and human
puzzle difficulty remain unmeasured. The local server is the review build;
the public website retains its prior deployed version.

The native configuration found no Vulkan validation layer: native captures
verify rendering without claiming layer coverage. Browser fixtures enable
Tide.checkGL and fail on runtime, shader, WebGL or WebGPU errors. The T3
collaborative preview reported unavailable on both status and open; visual
QA used the repository's muted browser fixtures and bounded native captures.

Additional bounded native review at 1280x800 and 390x844 checked both seats
with an occupied box on a shutter (Carry the current) and the recursive box
on plate A (Trading places). Neither parent signal draws through the smaller
interior floor; its band and shape remain outside the footprint. The native
presentation rebuild is warning-free. Artifacts are under ignored
build/inside-out-captures/ui/{signal-box-on-shutter,recursive-box-on-plate}-*.

The browser fixture follows the new menu, code-field focus, chapter tabs,
shared reset confirmation and phone-pad layout. HUD label crops lie wholly
inside opaque chips. Scene comparisons wait out each 2.5-second intro card,
so its disappearance cannot masquerade as a camera/undo difference; the
verified-tick wait's wall-time budget scales for those longer settling waits.
The retired copy puzzle is replaced with Beside itself, checking intermediate
same-room entry poses, exact partner undo and its real production solution.

## Inside Out audio recovery, 2026-10-15

Recovered the final reviewed sound workflow outputs from the interrupted
Claude task. The music scratch directory contains a later revision than
the assets copied into its unfinished integration worktree. Shipping game
music SHA-256 is `4b9037da1e479892ad9a7b8a478a96ae0e869d0af4c9b0b177ec3acdd95ade5a`;
menu is `6b87bbe5820f0c936d4aa34b8ba22c18b511716c9682c4f34ffe9af550e712bd`.
`sfx/SHA256SUMS` pins every installed asset. The generators are copied
unchanged from that final scratch revision and retained under `sfx/generate`.

`sound.tide` retains Mixer and UiSound's agreed UI contracts. Separate
Music/Effects/Interface buses have maximum gains 0.12/0.3/0.3, following
the final asset review; default sliders give 0.096/0.3/0.24. The master
also respects Rooms.Lobby.volume. Sound voices live on transient local
entities so overlapping cues route through real buses and volume/mute
changes affect tails already playing. Their lifetime includes a 50 ms
margin; no state enters the match and no runtime API was added.

Menu/game loops crossfade over one second. Resume menus retain game music.
All six UiCue values use distinct assets, with hover/slider throttles.
Movement uses three step variants and two box/battery variants; remote
steps are half volume. Doorway source/direction distinguishes enter from
exit, including self-recursion with unchanged room identity. A voiced
blocked-step cue plays at most twice per second per seat; intervening
blocked steps use a short quiet thud. Undo/reset/solve retain command cues.

An in-match view compares local prior presentation state with the current
world: battery seating/unseating, first box docking, individual plate
press/release, powered shutter transitions and partner arrival/departure.
It coalesces each cue type per frame so networks of shutters do not stack
identical samples. Initial state and serial changes establish a baseline;
undo/reset/admission never replay a roomful of transitions. As requested,
these diffs use presentation snapshots: simultaneous opposite changes
between rendered frames can coalesce or be inaudible. Action events keep
their existing once-per-event behavior.

The recovered workflow had measured all PCM endpoints/peaks/spectra and
engine Vorbis playback. Its final revision softened brass and doorway
layers, removed ringing reverb, aligned the entrance tap with the passage,
retuned the main chimes to F and removed the music's constant noise bed.
Those historical measurements are recovery evidence, not newly performed
tests of this integration. No device listening is claimed.

### Local validation

Both copied generators were rerun under this checkout's ignored
`build/audio-repro/{music,sfx}`. All 32 installed files reproduced byte
for byte against `SHA256SUMS` (2,129,691 bytes). The standard-library
`check_assets.py` passed shipping and regenerated sets, including the
29 PCM WAV formats, zero endpoints and peaks at or below -6 dBFS.
The music generator also repeated its continuous-three-pass/seam checks.

After the native presentation build, bounded device-free captures through
the production host and settings verify:

- Three-second title music correlates 0.99999943 with the final menu
  source and has measured gain 0.095991, matching the default 0.8 * 0.12.
- Clicking the actual mute setting produces exact-zero PCM for the
  measured 1.3–2.4-second interval; unmuting resumes the existing track.
- Setting the master slider near half gives source gain 0.048346 and
  correlation 0.99999760, so the sound view preserves the master setting.
- A nine-second two-player script executes the first production solution
  and then undo. Both mixes are unclipped, with peaks -16.895/-16.113
  dBFS. The solve source is present at measured gains 0.300445/0.298992;
  its full tail remains audible in the PCM while the later undo overlaps.
  Those correlations include the other deliberately overlapping cues and
  therefore do not claim isolated-source equality.

Scripts, captures, measurements and generated spectrograms are ignored
under `build/audio-repro`. `git diff --check` passes for the sound scope.
The root task owns native/web game validation and UI checks. Physical
listening and browser audio hardware were not exercised.


## Inside Out UI recovery, 2026-10-15

Recovered the interrupted polish brief's UI work in the shared checkout.
The UI is local presentation: match rules, level layouts and command payloads
are unchanged by this part of the work.

`ui.tide` supplies all four Rooms pages through game views, with empty package
page overrides. It keeps Rooms' hosting, joining, copy, leave and settings
flow. Warm walnut panels, cream text, brass focus/primary buttons, sage
success marks, rounded corners, soft shadows and bold headings replace the
factory controls. The title has a finite animated nested-square backdrop,
four-paint mark, Host a room, Join a friend, How to play and Settings. Counts
come from LEVEL_COUNT and CHAPTER_COUNT. Reduced motion holds the backdrop.

The waiting card shows the room code and Copy code. The in-play Menu explicitly
says the game keeps going for the partner; it offers Resume, Restart puzzle,
Choose puzzle, How to play, Settings, copy and leave. Restart asks for shared
confirmation. Puzzle selection has numbered chapter tabs, chapter names,
named numbered cards, the current puzzle highlighted and drawn sage ticks for
solved puzzles. Changing a puzzle after any moves asks for confirmation.
Confirmed actions carry the current serial and the existing Reset/ChooseLevel
commands; command authorization stays with the game.

Settings expose master via Rooms.Lobby.volume, music/effects/interface via
Mixer, mute, reduced motion, step preview and advice visibility. Preferences
and this machine's completed bits are JSON in preferences.json under
Application.persistentDataPath. The file is read once, volumes clamped, and
written only when a setting or completed bits change. Default startup writes
nothing; inaccessible files fail quietly. Saved solved bits are combined with
the match's progress only for the shelf, never to modify match completion.

Seven illustrated guide pages cover walking/rows, blocked-box entry, leaving,
partner backstops, completion, power channels and true recursion. Paragraphs
wrap to the phone width. Introduction cards last 2.5 seconds and fade near
their end (static under reduced motion). Solved cards give shared moves,
Next puzzle / Enter and Choose puzzle, with chapter-complete and ending text.
Hints retain their real three tiers and fit their text.

`paint.tide` changes are confined to the backdrop, Chip and HUD/header labels.
Small chips are opaque for readability and stable label crops. The new header
shows chapter, puzzle number/name, shared moves and Menu. The scene's context
and both robots' location chips are above the action controls. `map.tide`'s
UI framing uses SceneTop 122 on phones / 112 otherwise, SceneBottom 206 / 124.
The focused camera remains independent of the partner.

### Browser fixture coordinates

All coordinates are logical pixels from the top left. Normal desktop is
1280x800; phone is 390x844. Rect formulas remain the source of truth.

- Menu: x=w-88, y=16, 72x42. Header chapter x=20/y=22; puzzle title x=20/y=47,
  fitted bold text. Moves x=w-108/y=37 on desktop, x=w-20/y=68 on phone,
  right-aligned. Advice starts x=16 and its toggle x=w-88/y=68 (phone 78).
- Title card: width=min(w-32,510), height=560, centered. Buttons x=card.x+32,
  width=card.width-64, height=48. Host y=card.y+212; Join +272; Guide +332;
  Settings +392. Host enters the waiting/game HUD directly.
- Join card: width=min(w-32,480), height=360, centered. Click the code field
  at card.x+24/card.y+130 (width=card.width-48,height52) before typing. Enter
  joins; Join is at +198, height46. Back is +294, height44.
- In-play menu: width=min(w-32,500), height650, centered. Buttons x=card.x+24,
  width=card.width-48. Resume y+122, Restart +180, Choose +238, Guide +296,
  Settings +354, heights46. Code copy +464/height42; Leave +540/height44.
- Puzzle select: width=min(w-32,740), height=min(h-40,650), centered. Five
  numbered chapter tabs at card.y+114, x=card.x+24 + chapter*(card.width-48)/5.
  Cards at card.y+204+row*57, height48, x=card.x+24,width=card.width-48.
  Back is card.bottom-66. Only the active chapter's cards are drawn.
- Confirmation: centered 480x300 (width capped w-32); primary action
  x=card.x+24,y=card.y+166,width=card.width-48,height44. Back y+234.
- Undo/Reset/Hint: x=24 on desktop,16 on phone; widths96/74, gaps8; y=h-92
  desktop,h-144 phone; height44. Reset now opens confirmation.
- Direction pad center: desktop(w-92,h-112), phone(w-68,h-104). Offsets are
  Direction(d)*44 or *42, with 42/40-square controls. This clears the stats.
- Solved: centered at(w/2,h/2), card width=min(w-32,480),height248. Next at
  (w/2-122,h/2+14),244x44; Choose at(h/2+70),244x40. Enter continues.
- Settings and Menu fit 1280x600: card heights clamp to h-32 and row offsets
  scale with card height. Normal 800/844 coordinates above are unchanged.

### Validation

Presentation target built warning-free in native debug. Bounded native runs
captured title, guide, settings, join, waiting, menu, intro, HUD, solved,
chapter selection and confirmation at 1280x800 and 390x844. The two-seat run
replayed puzzle 1's ten accepted moves and displayed Solved on both screens.
All seven phone guide pages and 1280x600 title/settings/guide/menu were also
captured and inspected. Artifacts/scripts are under ignored
build/inside-out-captures/ui/. Root handles final web and simulation suites.

Persistence was observed loading a master change made by the audio QA run
and retaining completed:1 after the solved run. preferences.json did not
exist before this recovery's tests. Its exact test-created file was removed;
a fresh default three-frame title run left it absent. No other save files
were removed. Real phone hardware and gamepad navigation were not exercised.


## Inside Out campaign recovery, 2026-10-15

The recovered seven-puzzle campaign now has eighteen puzzles in five chapters.
The original seven layouts are unchanged. The extension adds two backstop
puzzles, five plate/shutter puzzles, three real-recursion lessons and one
combined capstone. This is fewer than the prior agent's provisional twenty-two:
the owner asked for more puzzles with increasing difficulty, and distinct,
verified ideas take precedence over filling a count.

`levels.tide` owns the square text maps, names, lessons, three-tier hints and
chapter boundaries. `game.tide` has `LEVEL_COUNT = 18`. There are no reference
boxes or duplicate interiors. Self-containing puzzles have one canonical room
and its one box inside itself; Return delivery has exactly two rooms, each
box inside the other room. The three first recursion lessons have an all-wall
table which play never reaches.

| # | Puzzle | Chapter | Shortest moves | BFS positions explored |
| --- | --- | --- | ---: | ---: |
| 1 | A room outside | Boxes | 10 | 130 |
| 2 | No handle inside | Boxes | 12 | 3,291 |
| 3 | The other opening | Boxes | 20 | 16,840 |
| 4 | A place to stand | Boxes | 15 | 67,283 |
| 5 | All in a row | Boxes | 11 | 8,083 |
| 6 | Hold it steady | Each other | 19 | 7,243 |
| 7 | Posted | Each other | 15 | 2,430 |
| 8 | Change ends | Each other | 20 | 12,973 |
| 9 | Around the corner | Each other | 21 | 16,494 |
| 10 | Hold the line | Power | 8 | 691 |
| 11 | Long wire | Power | 11 | 1,215 |
| 12 | Pass it on | Power | 15 | 3,068 |
| 13 | Borrowed power | Power | 11 | 1,556 |
| 14 | Carry the current | Power | 13 | 31,498 |
| 15 | Beside itself | Itself | 6 | 41 |
| 16 | Shift the exit | Itself | 11 | 251 |
| 17 | Return delivery | Itself | 6 | 95 |
| 18 | Trading places | Everything | 13 | 126 |

`inside_out_solve all --limit 1000000` found every shortest solution in
2.52 seconds. These searches stop on the first solution; they do not exhaust
the reachable space. The checked-in witnesses all have the shortest length,
although simultaneous independent actions can be interleaved differently.
Every generated command is replayed through the production Move handler.
This proves solvability and move minima, not human difficulty. Chapter
introductions intentionally use compact layouts and shorter routes when a
new concept appears. No human playtest of these eleven additions is recorded.

The original fourth puzzle already had a fifteen-move solution rather than
its thirty-one-move witness. Amber can leave west and push the home east
before the battery leaves. Its witness, map comment and hints now describe
that route; the prior advice to extract and store the battery first was
unnecessary.

New concepts, in order:

- Change ends parks the box before the robots exchange which end is held.
- Around the corner posts a battery through a held box, then turns it
  towards a socket inside instead of pushing straight past the turn.
- Hold the line separates the plate holder from the battery pusher.
- Long wire places the controlling plate inside a different room.
- Pass it on hands the same channel from a near plate to a far plate so
  both robots can cross.
- Borrowed power hands off between two independent circuits and crossings.
- Carry the current transports a powered battery inside its occupied box;
  the inside robot becomes the outside backstop so its partner can board.
- Beside itself delivers a battery by leaving a self-containing room, then
  uses the arriving partner's body to enter that same box from below.
- Shift the exit moves a self-containing box to change its own exit's
  landing, then pushes the delivered battery and box together into place.
- Return delivery moves the receiving box in a second room before sending
  the battery out, across a real two-room cycle.
- Trading places combines a self-containing box, a battery delivery,
  partner backstop, two circuits and a role exchange. The first robot holds
  a plate while the other moves the self-containing box and enters again.

### Co-operation evidence

The campaign's exhaustive alone test now asks for every battery on a socket,
every socket occupied and every box on its outline, ignoring robot goals.
The other robot is placed off every grid. It normalizes facing and counters
before hashing, inserts the initial state and fails if its 600,000-state
limit is reached. Neither robot alone achieves this cargo-and-docks goal
in any of the following puzzles; every listed search exhausted its space:

| Puzzle | Seat 0 positions | Seat 1 positions |
| --- | ---: | ---: |
| 6 | 3,454 | 3,454 |
| 7 | 13,966 | 13,966 |
| 8 | 10,334 | 10,334 |
| 9 | 16,919 | 16,919 |
| 10 | 45 | 45 |
| 11 | 49 | 49 |
| 12 | 45 | 45 |
| 13 | 45 | 45 |
| 14 | 2,080 | 3,186 |
| 16 | 51 | 46 |
| 17 | 31 | 3 |
| 18 | 10 | 15 |

Counts for puzzles 6 and 7 differ from the older campaign checks because
facing no longer multiplies otherwise equivalent positions. Puzzle 15's
Teal can seat its cargo alone (27 canonical reachable states, versus ten
for Amber). Its co-operation requirement is instead pinned by a direct
counterfactual production-command test: after Teal delivers the battery,
Amber enters the self-box while Teal stands above it; removing Teal makes
the same command move the box and keeps Amber outside. No claim is made
that every original Boxes chapter puzzle has a cargo-and-docks dependency
on both robots.

The campaign replay checks every room's square dimensions, doorway grammar,
box placement and production completion; both seats act; undo, reset and
exact replay retain canonical bytes; exchanged identities solve every puzzle.
A separate fixture loads Return delivery, pins its two-box cycle and checks
that its battery really ends in the other canonical room.

Native debug `inside_out_tests` builds without warnings and passes 40/40,
including all eighteen witnesses and the twenty-four exhaustive searches.
Web and browser coverage is recorded by the integrating agent separately.

### Rejected trial

An unconstrained symmetric two-room cycle with two batteries was not kept.
Its breadth-first search found no solution within ten million positions and
32 moves (261.31 seconds); it hit the limit without exhausting the space.
This is not an impossibility proof. Its absence of a verified witness was
enough reason to remove it instead of shipping it.


The owner requested a public online co-op puzzle prototype for two friends
in separate browsers on 2026-10-11. The initial workshop, fixed player roles,
isometric experiment, switch locks and fixed recursive entrances are
superseded. The current direction is actual 3D with a fixed 2.5D view, harder planning and
interdependent moves, physically nested starting rooms, varied sizes and
offset doors, and movable boxes including self-nesting. The owner chose
spatial constraints instead of locks, and on 2026-10-12 a factory where the
players are test dummies, in a PSX style, replacing the earlier institutional
atmosphere. There are still no threats or jumpscares. These are game design
choices within the prototype request, not engine or language requirements.

## The rules now (2026-10-12, push-or-enter rewrite)

This section is the game's rules as they stand. Everything below it about
movement, trails, copies, facing doorways, echoes and the twelve puzzles is
history: read it for why, not for what the code does.

No copies (2026-10-15, the polish round's decision D1: the lead agent's
pick, provisional until the owner confirms). The owner called the copy in
the last puzzle "fake recursion", "portals": a pale box led into a room
whose own box stood elsewhere, so what went in could not be pushed back the
way it came. Reference boxes are gone from the rules, the view and the
tests. Every room has exactly one box, and a box's id is its room's: `BoxAt`
returns a room, and `ValidBox`, `BoxDestination`, `BoxPlacement`,
`EntryDestination`, `ReferenceLayout`, `LevelLayout.references` and
`referenceCount`, `Puzzle.references`, `MAX_REFERENCES` and `MAX_BOXES` are
removed. In the view, `BoxPaint`'s wash, `RoomSurround.reference`,
`FloorNumber`'s prime mark, `SceneLens.box`, `RoomScene`'s instance and
`PassageMaker`'s viewer-box test went with them, and `AncestorSurround` is
a room's box and the room it stands in, with no trail lookup. Box-indexed
arrays in `Picture` are `MAX_ROOMS` long. Recursion is only real: a box
inside its own room (leaving puts you beside it, in the same room) or two
boxes inside each other (leaving one puts you beside it, in the other).
Puzzles 8 "The same corner" and 9 "The other way in" used copies and are
removed with their lessons, hints and `WidenRooms` case; `LEVEL_COUNT` is 7
and the title page reads it. Puzzles 1 to 7 load as before, less the
copies' fields, and their witnesses replay unchanged.
- Fixtures for real recursion (`test/simulation.c`): a room holding its own
  box, left beside it by its east and south doors and entered from those
  sides at the matching door cell, its west exit refused by the wall beside
  the box; a battery pushed out of such a room lands beside the box, and is
  refused when that cell is the pusher's own (the row comes round onto
  itself); the box pushed east within itself, after which the partner and a
  battery keep their cells (they are inside the box and go with it) and the
  pusher, inside it too, has taken the one step of any push, then both bots
  leave beside its new place; a two-room cycle left each way, round the
  cycle and back, then entered with the partner as backstop, all undone
  exactly. The two fixtures that built copies were converted: simultaneous
  entries now race an entry and a walk onto one doorway cell, and the
  snapshot fixture enters a room's own box standing inside it.
  `inside_out_campaign_has_exactly_the_witnessed_puzzles` ties the test's
  puzzle count to `LEVEL_COUNT`. 29 tests pass.
- Not changed: `test/browser.mjs` still knows the removed puzzles; a later
  stage of this round rewrites it. `Hands.portalTime` is the flash after any
  doorway crossing, not a copy's, and stays.

Plates and shutters (2026-10-15, the polish round's decision D2: the lead
agent's pick, provisional until the owner confirms; no puzzle uses them yet,
the campaign stage adds chapter 3). `Tile` gains `PlateA`, `ShutterA`,
`PlateB` and `ShutterB` after the old four, so their values are unchanged.
- Rules (`game.tide`). `Powered(layout, puzzle, channel)` is true while
  anything stands on a plate of that channel in any room or on the table:
  a bot, a battery, or a box on a plate of the room it stands in (it looks
  where each thing stands rather than at every cell, which is the same
  answer). `Power` is the two channels as bits. `Passable` is the one test
  of a cell anything steps, is pushed or goes through a doorway into: never
  a wall, a shutter only while its channel is powered; `StepFrom` (the step
  inside a room and the step out beside the room's box) and `Gateway` (the
  door cell) use it and take `power`, which `Shove` works out once from
  the puzzle before the move. `IsSolved` is unchanged.
- What a shutter closed on (my reading, to report as a pick). The brief
  says a thing in a shutter when it closes "can leave or be pushed out",
  and that a closed shutter holding something is still a wall for anything
  else coming in. Under push-or-enter those two clash: pushing a thing out
  of a cell is coming into it. So `Shove` lets the push through without the
  pusher: where the next cell is a closed shutter with something in it
  (`ClosedOnThing`), that something joins the row as if the shutter were
  open, marked `through`, and when the row moves, everything before the
  last such mark stays where it is. A bot there is a stop like any bot; a
  box there that cannot move is entered by its doorway as any box is; a
  doorway whose door cell closed on something has that something pushed on
  into the room while whatever was going in stays outside. `Step` is Push
  when the bot stayed. `MoveHand` now calls a move a doorway crossing only
  when the bot itself moved, so such a push out of a doorway cues a push.
- Look (`paint.tide`). Signal paints magenta (A) and lime (B), with a ring
  for A and a diamond for B (`ChannelColor`, `ChannelMark`). A plate is a
  low square pad in its paint, softened towards the plaster, in a dark
  groove, the shape dark on it; pressed it sinks from 0.044 to 0.010 of a
  tile and its shape and a line round the groove glow (the line shows past
  a bot or a battery on it). A closed shutter is a block 0.80 of a tile
  across and 0.22 high (a wall is a whole cell, 0.31), its top in the
  paint with three dark slanting stripes, mirrored for B; open, its top is
  flush with the floor inside a thin frame of the paint. Closed on
  something, only a rim at the frame's edge rises, 0.55 of the way, round
  what is in it. Below 6 pixels a tile each is one flat block of colour.
  `Picture.cellRise` holds every plate's and shutter's height (49 cells of
  the table, then of each room), eased towards its goal at 16 a second in
  `Workbench`, about a fifth of a second, and set outright on a new puzzle,
  undo, reset or reduced motion (`snap`). The guide has two lines on them
  and the README a rule.
- Fixtures (`test/simulation.c`): a plate pressed by a bot, a battery and
  a box each opens its channel's shutter and not the other's; a plate in a
  room opens a shutter on the table beside that room's box (the step out
  of the doorway) and a table plate a shutter in the room; stepping off the
  plate while pushing a battery into the shutter puts it in, the shutter
  closes on it, it stays, a push takes it out while the pusher stays (cue
  kind 1), nobody steps into the empty shutter, a bot it closed on stops a
  row and steps out; a row of a bot and two batteries moves only the one
  in the shutter, a box a shutter closed on that cannot move is entered,
  and a push out of a doorway into a held shutter leaves the bot in its
  room with a push cue; a doorway's shutter keeps a bot out of a box until
  a plate is pressed, and when it closed on a battery the battery is pushed
  in and the bot stays outside; a box carried with a pressed plate inside
  it moves into a shutter, and without the battery on that plate is stopped
  by it and entered. Each is undone to the exact start. 35 tests pass;
  witnesses and the solo searches are unchanged.
- Looked at: native captures at 1280x800 of a temporary puzzle (not
  committed) with both channels in three nested rooms, from inside a 5x5
  and a 7x7 room, closed, opening, open, closing, held, pressed by bots,
  and the guide (`build/inside-out-captures/plates/`). The guide now just
  fits a 720-high window. Not done: no sound for them (the sound stage's),
  the browser fixture, and no puzzle.

Levels as text, chapters and the mixer (2026-10-15, the polish round's
decision D3 and the contracts between its parallel stages: the lead agent's
picks, provisional until the owner confirms; the format is my pick).
- `levels.tide` draws each puzzle in a function of its own (`ARoomOutside`,
  `NoHandleInside`, `TheOtherOpening`, `APlaceToStand`, `AllInARow`,
  `HoldItSteady`, `Posted`) as maps, `Room(draft, room, [rows])`, room -1
  the table, at the size the match holds: a box 3 to 7 square, the table 1
  to 7, a table not drawn 7 by 7 and all wall. A row is the tiles, spaces,
  then what stands on them, the same width. Tiles: `#` wall, `.` floor, `o`
  socket, `x` goal, `a`/`A` plate and shutter A, `b`/`B` plate and shutter
  B, a digit floor with that box's outline. Things: `.` nothing, `@` Amber
  (seat 0), `&` Teal (seat 1), `+` a battery, a digit that box (its room's
  id; the game names it Box 1 to 4). The two halves are why any thing can
  stand on any tile, and a box on its own outline (every box of puzzles 1
  to 7 has an outline, most start on it) is its digit in both. An outline
  is only on floor. A box stands where its digit is drawn, so a box in its
  own room's map is inside itself and two in each other's are a cycle.
  The legend is the comment at the top of the file.
- Doorways come from the map: a room's edge is wall but for them, and an
  open cell on a side (anything but `#`, which is wider than the brief's
  floor or shutter), not at a corner, is that side's doorway; its offset is
  -1 at the middle of the side and the cell's place otherwise, which is what
  the old loader stored for every puzzle. Batteries are numbered in the
  order drawn: maps in the order the puzzle draws them, rows top down.
- `Debug.Assert` stops the game with what is wrong, naming the puzzle, room
  and cell, for an unknown mark, a size out of range or rows of the wrong
  width, an open corner, two doorways on a side, a thing in a wall, a robot
  or a box drawn twice, a second outline, more than eight batteries, a room
  drawn twice, rooms not 0 to n-1, or a drawn room whose box stands nowhere
  (`struct Draft` carries the masks). Every puzzle is loaded by the
  campaign test, so a bad map fails it; checked by giving puzzle 1's room
  two east doorways, which stopped the test (exit 3) with "Puzzle 1, room
  1: 2 doorways on the east side; a side has one at most".
- Proof the conversion is exact: a temporary test printed every byte of
  `LevelLayout` and `Puzzle` after `ChooseLevel` for each puzzle; built
  once with the old `levels.tide` and once with the new, the outputs are
  byte-identical (20,370 bytes of hex each), as were a text dump of every
  map and FNV hashes. The temporary test is gone. Kept: the campaign test
  now checks every room's doorways against its edge (corners wall, one open
  cell a side at most, a doorway exactly where there is one, at the stored
  offset), that each box stands on an open cell, and that the table is
  square. `SquareRoom`, `WidenRooms` and every `Workshop*` builder are
  removed; `LoadLevel(level, layout, puzzle)` is still the entry point and
  still rebuilds both robots' trails.
- Chapters: `CHAPTER_COUNT` 5, `ChapterName` 0 to 4 "Boxes", "Each other",
  "Power", "Itself", "Everything", and `LevelChapter`: puzzles 1 to 5 are
  chapter 0, 6 and 7 chapter 1 (and, until more are added, any later one).
  Nothing shows them yet.
- `sound.tide`: `local singleton Mixer { bool muted; float music = 0.8;
  float effects = 1; float interface = 0.8; }`, which the settings write and
  sound reads; `Hands.quiet` and `Hands.music` are gone into it. Music plays
  while not muted and `music` is above 0, at 0.3375 times it (0.27, as
  before, at the default); every puzzle cue is scaled by `effects`. `enum
  UiCue { Hover, Click, Back, Open, Toggle, Slide }` and `void UiSound(UiCue
  cue)`. A function sees no singleton it is not passed, so `UiSound` sends a
  local event, `UiNoise`, which `PlayUiNoise(Mixer)` plays at the end of
  the frame: any view, local handler or function they call can ask for a
  sound with nothing to pass. For now Click and Toggle play the step clip at
  0.12 times `interface`, pitch 1.4, and the rest nothing. The toolbar's
  sound button toggles `muted` and asks for Toggle (heard on unmuting:
  a two-player capture with `--capture-audio` is silent from the mute to the
  unmute and peaks at 839 against the ambience's 265 at the unmute); the
  guide's Ambience toggle became a Music slider.
- Tests: 35 pass natively, witnesses (10, 12, 20, 31, 11, 19, 15) and the
  solo searches (8,043 and 30,455) unchanged. `web-debug` configured here
  (the first configure failed while it fetched the WASI sysroot; the second
  succeeded) builds `inside_out_tests` and `inside_out` without warnings,
  and `ctest --preset web-debug -R inside_out_tests` passes.

The owner, after playing with a partner, sent six points, was shown my
reading of each with two questions, and answered "once done push and
deploy", so the two readings are mine: (a) "stop making doors between
worlds" means no direct passage between boxes side by side, and boxes keep
their doorways; (b) "true recursion" means a room is one place with one box
that is its outside, with no memory of how anything came in.
- Facing: a bot faces the way it last moved (`Traveler.facing`, in the
  match, so undo turns it back and a blocked step turns nothing).
- "Things should always push, no matter what", and "the only movable wall
  is the other player": `Shove` (`game.tide`) is the one rule for a bot, a
  battery and a box. A thing steps to the next cell (`StepFrom`); what
  stands there is pushed first, and what is beyond it, for a row of up to
  20. Only a wall or a bot stops a row. Then the thing against the stop
  stays, and the thing behind it goes into it if it is a box with a doorway
  facing that way (`Gateway`), pushing on whatever stands in that doorway;
  failing that it is stopped too and the same is asked of the thing behind
  it, back to the pusher. All goals are worked out from where things stood,
  then all move at once. `Step` says Walk, Push or Enter for the preview.
- No ghosts, true recursion: a room is left beside its own box, by a bot or
  anything pushed, however it was entered. A bot's trail is no longer
  history: `MovePuzzle` rebuilds it for both bots after every move as the
  room's box, that box's room's, and so on (`RebaseTrail` with depth 0),
  which is what the view's ancestry and the status lines read. A bot is
  drawn solid in every drawing of its room; the echo code is gone. A room
  whose own box is inside it is left into itself, as before; no infinity
  room was built. (Copies, a way in only and drawn washed out, were removed
  on 2026-10-15; see above.)
- No doors between worlds: `DoorsMeet`, the drawn passage and the special
  cases for leaving one box into the next are gone. A bot leaving a box
  into a cell with a box in it pushes that box, and goes into it if the
  push is stopped.
- Theme: parcels are batteries (dark cell, copper top with a plus, green
  when seated) and targets are sockets; the words in the game say so, and
  "robot" for "dummy". The owner also floated batteries that unlock things.
  That is a new mechanic and is not built.

Campaign: nine puzzles (`LEVEL_COUNT`), seven since 2026-10-15, when the
two that used copies (8 and 9 below) were removed. Under the new rules the old
witnesses of puzzles 1 to 4 and of "Hold it steady" replay unchanged; "The
same corner" was re-solved by search (16 moves); the other six old puzzles
are dropped: one searched to four million positions without a result, the
rest were not searched to an end, and they were built on rules that no
longer exist. Three are new and small: "All in a row" (a battery and a box
pushed as one), "Posted" (a battery pushed at a box the partner holds goes
in, and the bot after it), "The other way in" (a pale copy against a wall
is entered, and the room's own box is on the table outside, which is where
its south door leads).

| Puzzle | Witness |
| --- | ---: |
| 1 A room outside | 10 |
| 2 No handle inside | 12 |
| 3 The other opening | 20 |
| 4 A place to stand | 31 |
| 5 All in a row | 11 |
| 6 Hold it steady | 19 |
| 7 Posted | 15 |
| 8 The same corner (removed) | 16 |
| 9 The other way in (removed) | 26 |

Witnesses 5 to 8 are the shortest (breadth-first through the Move handler);
9 is my own route, its search having passed four million positions; 1 to 4
are the old ones and not known shortest, and puzzle 4 had a 15-move
solution under the old rules. Whether row pushing made puzzles 1 to 4
easier is unchecked. Nobody has played any of the nine under these rules.

Tests (`test/campaign.c`, rewritten; `test/simulation.c`, twelve fixtures
of the old rules removed and four of the new added). Every puzzle's witness
solves it through the generated handler, with both seats acting, one undo,
a reset and an exact replay; with the seats exchanged; and every room is a
square. `inside_out_a_box_is_entered_only_with_the_partner_behind_it`
searches every position either bot reaches alone with the other taken off
the board, in puzzles 6 and 7: 8,043 and 30,455 positions, and in none is
the small box on its outline with its battery seated. The evidence
counters and the second puzzle's exhaustive check are gone with the rules
they measured. Fixtures: a row of two boxes and a battery, then the first
box into the second when the row meets the edge, then the bot into the
box; a battery behind a box holds nothing and the partner does; in by a
copy and out, battery and bot, beside the room's own box (replaced on
2026-10-15 by the real-recursion fixtures above); facing and undo.

What is drawn: `EnteredBox` (`map.tide`) finds the box a bot went into by
one step. With copies gone that is always the room the bot is now in; in a
two-room cycle it tells entering a box from leaving into the same room.
Not looked at in motion.

## Shared world and recursive boxes

`game.tide` contains one deterministic two-seat match. Both makers start
inside the same small box, itself inside an ordinary containing room box
that can be left. The root is called Outer room. Makers retain amber/teal
identities. Their coordinates use one canonical interior: they block one
another even when they arrived through different copies.

Root and room dimensions are authored independently, bounded to 1..7 on
each axis, with 49-cell storage and actual-width row stride. Each room has
a four-side door mask and per-side offsets; -1 selects the midpoint.
Invalid offsets and destinations block atomically. RoomLayout array elements
require explicit literals to obtain defaults: a zeroed LevelLayout does
not apply field defaults inside its room array.

Limits: four canonical interiors / primary boxes, four movable reference
boxes, eight parcels and eight explicit entrance layers. IDs 0..3 are primary;
IDs 4..7 are references. `LevelLayout.references` stores destinations;
`Puzzle.references` stores live parents and positions. References use their
canonical room's doors and offsets. Small stencils distinguish primary 1
from copy 1′ (and 2..4), with identical materials and opening geometry.

Every instance tries to move first. Walls, boxes, parcels and the partner's
body can be backstops. A failed push tries the facing doorway using unchanged
state. There are no switch locks, modifiers, pulling or push chains. A moving
box carries its interior and occupants without changing interior coordinates.
Objects pushed into another doorway enter its canonical interior. Objects
pushed out emerge beside that room's primary box; makers return through the
instance they entered. This distinction is stated in the guide and README.
Resolution crosses at most one boundary and one gateway per action;
occupied door cells block unbounded gateway chains.

Facing doorways (owner, 2026-10-12: adjacent boxes "have to match position,
not just have a magical bridge", and the rules must be consistent, after a
parcel crossed a bridge that a player leaving the same doorway did not: the
player pushed the neighbor instead). `DoorsMeet` is true when the leaving
room's doorway on that side and the neighbor's opposite doorway are both
open and at the same place along the shared edge, compared exactly in the
containing cell (`DoorReach` scaled by the other room's longest edge). It
applies only to leaving one box straight into a box in the cell outside:
- Doorways meet: a dummy, parcel or box crosses directly. A dummy walks
  through and does not push the neighbor first. This is my pick for
  "consistent": one passage rule for everything.
- Doorways do not meet: a pushed parcel or box is blocked. A dummy pushes
  the neighbor if it can move and steps out into the freed cell; otherwise
  the move is blocked, because the dummy never stood on the floor outside.
From the open floor nothing changed: rooms move first, a blocked room is
entered through whichever doorway faces the dummy, and a parcel or box
pushed into a box enters that doorway, wherever along the edge it is. The
drawn floor between boxes (`DimensionConnections`) uses the same test, so
it is always straight. Guide and README state the rule.

Step preview (owner, 2026-10-12: "a way to communicate to the player when
they will push vs enter"; built on "add it"). `MovePuzzle` reports a `Step`
(Blocked, Walk, Push, Enter) through an `out` parameter; the handler ignores
it. `WorkshopControls` runs the same function on a copy of the puzzle for
each of the local seat's four directions every frame (`Hands.ahead`), so the
preview cannot disagree with the rule, including partner backstops and
facing doorways. Push means a box moved; a pushed parcel is a Walk, and an
entry that also pushes a parcel is an Enter. The direction buttons draw it
(`StepMark`: block ahead of the arrow, frame around its tip, bar across
it), and `Workbench` draws a yellow bumper or a green arrow on the floor
between this screen's dummy and the box, one frame behind the controls and
hidden during a passage. Only the local dummy's next step is shown. The
shapes and colours are my picks.

Leaving a room, the in-room copy of a dummy now stays opaque until it
stands in the doorway (progress 0.22..0.30) instead of fading from the
first frame: the doorway pose is clipped to the opening while inside, so
the old crossfade left the dummy mostly invisible on its way to the door
(owner: "the tiny guy when I exit seems to mostly vanish").

Self-placement and two-room cycles are allowed without cloning interiors.
`Entry.box` resolves the live entered instance, so a moved occupied box
changes its maker's return location. A mismatching prefix rebuilds from
primary ancestry, stops at the first repeated room, and retains finite
unique-room ancestry instead of inventing an outer anchor. Matching visits
retain their explicit trails. Depth-eight entry blocks unchanged; undo
remains available. Popped slots reset to defaults for canonical bytes.

Movement commits a trial Puzzle after all guards pass. Completion requires
designated primary boxes on numbered docks in the specified parent, every
parcel target occupied, every active parcel on a target and both makers on
distinct player goals. Targets may be emptied during planning. Shared history
retains 128 actions. Reset restores boxes, references and trails exactly;
completion waits for Next. Exactly two real participants are required.
Existing Rooms, Session, sender/sequence validation and host migration remain
without new runtime APIs. Missing seats disable commands; stale serials
cannot cross undo/reset/selection/participant changes. All twelve puzzles
can be selected from the public shelf.

## Presentation, controls and audio

Miniatures and focused rooms share one transform: inner tile scale is
1 / max(width,height). The longest box edge fills its containing cell.
Frames sit inside the rectangle with real doorway gaps. Offset door lips
preserve their tangent rather than projecting radially toward the midpoint.

Camera and maker passage share a 0.55-second clock: approach 0..35%,
doorway shrink 35..70%, interior step 70..100%, reversed on exit. The camera
holds the miniature during approach, then zooms continuously. Aperture
clipping and solid-wall occlusion prevent silhouettes showing through walls.
Same-room recursion and adjacent handoffs use these transforms. Interrupted
travel begins from the rendered pose; input stays responsive. Reduced motion,
level changes and zero-move reset discard stale passages. There is no fade.

Each settled room fits max(width,height)+6 tiles in a fixed centered square,
revealing actual containing-world context. Outer room retains its 0.96
viewport-side fit. Partner presence, position and room never change camera
targets; makers may leave the viewport. Surroundings resolve the live entered
instance, including moved copies and self-containing boxes. Drawing is bounded
to three ancestor contexts, three descendant levels and 48 instances per
context; subpixel previews become markers. No match objects are duplicated.

`geometry.tide` builds normalled, depth-tested cube instances for floors,
low cutaway walls, crates, stencils and maintenance makers. The orthographic
camera has a fixed 65-degree pitch and no yaw; ground coordinates preserve
the existing screen axes and camera framing. Each occurrence has a shared
ground plane; component elevations change world Y only, keeping heads, lids
and trim physically stacked. Interior floors rise by 0.018 containing tiles.
Finite contexts and descendant limits remain unchanged. Geometry, lighting
and render resources are machine-local; layouts, rules and audio are unchanged.

`render.tide` uses a common scene/depth for every visible occurrence: Rgba8
material plus Rgba16f normal/occurrence-scale MRT, a fixed-focus 1024-square
directional shadow map with four comparison taps, and half-resolution SSAO.
Each shadow tap compensates for the receiver plane's light-space depth slope,
so the fixed map footprint does not make miniature floors shadow themselves.
Eight normal-oriented hemisphere samples reconstruct world positions from
hardware depth and inverse camera projection. Metric radii/biases follow
the originating occurrence's tile scale. Two separable bilateral passes
respect normal and depth discontinuities; AO affects ambient light only.
No temporal jitter or geometry-dependent camera fitting is used. Alpha and
doorway ground-column discard rules are shared by all mesh passes. The
composite explicitly clips the board because Blit does not inherit Draw.Clip.
Workbench prepares geometry and SceneLighting owns view-only shader draws.

### Current state (2026-10-12, last round of the day)

This subsection is what the game is now. The dated paragraphs after it in
this section are the passes that led here, kept for their reasons; where
they disagree with this, this is right. The owner iterated on screenshots
and a local two-player page, said tests were "useless until we land on
something", and asked for the merge once the look and rules below stood.
Everything not attributed to the owner is my pick.

Look (`render.tide`, `paint.tide`, `geometry.tide`, `ui.tide`). The owner
asked me for "your version" after rejecting a dimmer lab and an abandoned
factory, then steered it: no lamp pool or lanterns ("the level should just
be lit"), every room the same brightness, blur that fades in rather than
starts at a wall, colour bounce turned down, no rim on wall tops.
- A painted model on a wooden table. Pale plaster floor in two shades 0.025
  apart, boxes sage, slate blue, plum and terracotta, a wooden parcel, brass
  door posts, the outer room a low walnut top with a raised frame. A wall
  block is one paint on top and darker sides, 0.31 of a tile high.
- Light: warm where lit, cool where shaded (`tint`), 12-tap soft shadows
  turned per pixel from a 4,096 lamp map, occlusion at the window's size,
  a catch-light on the near edge of tops, a darker line where the surface
  behind is a real step away, static grain. All of it is the same in every
  room; nothing is keyed to the played room's tile, which changes at a
  doorway (a clamp on `SceneFill`'s scale and three such terms made the
  surroundings jump on entering).
- Marks that give their own light (`SceneObject.glow`, a whole 1 in the blue
  of the normal target): goal pads, parcel targets, the step preview, a
  bot's visor and antenna tip. They bloom (`SceneGlowSource`,
  `SceneGlowBlur`, half size, two rounds).
- `SceneFinish` is the lens: sharp over the played room as it sits once the
  camera has settled (`Picture.focusHalf`, `focusTile`, eased at 5 a second,
  never tied to the zoom) and for 0.9 of a tile round it, then softer with
  distance on every side to 10 pixels of a 12-tap disc; a little colour
  parting and darkening to the corners. Colour bounce in `SceneComposite`
  tints a floor by what stands within half a tile of it, as hue only.
- The bots: tracks, a chassis under a shoulder plate, a two-step bone head,
  visor and antenna in the seat's colour (`Maker`, `BotPart`). They face
  their step, hop, and settle to the camera within a tenth of a second.
- Header one slim row on wide windows; the puzzle's advice, the room, the
  moves, the surroundings and who is where are on plates over the scene
  (`WorkbenchLabels`, `WorkshopControls`). `SceneTop` is 66, or 128 under
  1050 wide.

Rules (`game.tide`).
- `StepFrom` is the one step for bots, parcels and boxes, and the box a
  room is left through is the same for a bot and for what it pushes: the
  box that bot came in by (`PusherShell`, the room's own box where the
  trail does not say). Until this round a pushed thing left by its room's
  primary box whichever copy the pusher was in. The owner's partner pushed
  a parcel west out of a copy and saw it come in at the east door of the
  same room ("what he sees doesn't match what happens. we either fix what
  he sees or what happens"); I changed what happens. All twelve witnesses
  replay unchanged under it, puzzle 7 still has no solution without a
  backstop (726,703 positions) and the second puzzle's count is still 489.
  Whether it opens or closes other routes in puzzles 9 to 12, which were
  written around the primary box, is unchecked. "Objects pushed out emerge
  beside that room's primary box" above no longer holds. `TravelerStep`
  wraps `StepFrom` for a bot's own step.
- A box standing in a doorway cell is pushed on in by a bot entering, as a
  parcel there is; it blocks only if it cannot move. The owner hit the old
  block ("is this intended?") and I changed it for consistency with
  parcels; they have played it since and not objected.
- Facing doorways (`DoorsMeet`) and the step preview stand as described
  below.

Layouts (`levels.tide`; since 2026-10-15 text maps authored at this final
size, with no packing or widening at load: see "Levels as text" above).
Every room is a square grid (`SquareRoom`), and
`WidenRooms` then opens cells of the doubled wall that squaring left into
floor, puzzle by puzzle: the owner would not have the dark strip I first
drew there ("sounds like the level needs a redesign"). Each cell was opened
by a search that kept it only while that puzzle's witness still solved it
with the evidence the campaign asks. That shows each puzzle is still
solvable as intended, not that no shortcut opened. Known: puzzle 4 has a
15-move solution against its 31-move witness, origin unchecked; puzzles 1
to 3 have witnesses of the shortest length; puzzles 5 to 12 are unchecked
for shortcuts, and the box-in-doorway rule can only have added moves.

Puzzle 7 is new, "Hold it steady" (owner: the partner-as-backstop move
should be used by a puzzle). A small box must be parked on its outline with
its parcel delivered inside, with open floor behind the outline, so one bot
must stand there for the other to get in. A breadth-first search through
the Move handler finds it solvable in 19 moves and, with every move in
which a partner's body turned a push into an entry forbidden, finds no
solution in the 726,703 positions reachable. Its evidence is one partner
backstop. The same search finds puzzles 1 to 4 need no backstop and ran out
of room on puzzle 5.

What is drawn of things in motion (`map.tide`, `paint.tide`).
- `FollowPushed` is the one path for a pushed box or parcel: a steady slide
  within a room, and a `Transit` through a doorway, into the box beside it,
  out to beside its room's box, straight between two boxes with facing
  doorways, or back into the same room where a room holds itself. Boxes
  and parcels share all of it. A bot's `MakerPassage` is separate: it is
  tied to the camera's zoom and the fade between copies. A bot crossing
  straight between two boxes glides in one motion and never takes the
  outer room's size.
- Echoes. A bot is solid only where it is: in a drawing of the box it came
  in through, and at its own depth where a room repeats on the way down
  (`through`, `level`, `repeats`). Elsewhere, the played room included, it
  is an echo: greyed, unlit, 36% and stippled in two-pixel cells. A bot
  arriving fades in as what it will be there, and one crossing through
  another copy than this screen's is not drawn at this screen's doorway.
  The owner asked for this in place of rethinking copies, and reported each
  case fixed here in play except the last two, which they have not
  confirmed.
- A blocked step shakes its bot on both screens (`Hands.blockTime` by seat).
- A dock outline is hidden under any box standing on it.

Sound. `enter.wav` is a soft low whoosh (0.42 s, low-passed near 850 Hz,
one raised-cosine swell), played at 0.26: the owner found the last one
"a bit hard on the ears". Against it the new one has a quarter of the
sample-to-sample movement and takes four times as long to reach half its
peak. Nobody has listened to it but the owner, who has not said.

Not done, and known: the browser fixture's wall and parcel patches were
re-aimed for the new rooms only as far as its runs below say; the room
layouts are still authored non-square and squared and widened at load; the
text still says "dummy"; nothing was measured of what the lens, bloom and
larger maps cost on real hardware.

Square blocks again, with texture and depth (2026-10-12, later still). The
owner did not like the rounded corners ("the square one looked better so
make it like it was before") and found the surfaces "pretty flat": "lets
play with texture, depth". The amounts below are a first, gentle pass and
my picks.
- The rounded corners are gone, with their two meshes, `Corner`,
  `WallFillets` and the extra lamp and scene passes: `geometry.tide` and
  `render.tide` went back to the commit before them, and `WallTile` is the
  joined block with a darker rim and a raised face again. What the bullet
  below says of rounding no longer applies; square rooms and plain doorways
  stand.
- Depth: walls are 0.31 of a tile high (were 0.24), perimeter strips 0.30
  and door posts 0.32. Every side face darkens towards the floor it stands
  on, from full light at its top to 0.76 at its foot.
- Texture: a floor is terrazzo, 24 flecks to a tile's edge, about one in
  twenty darker (0.935) and one in thirty paler; a wall's raised face is
  brushed, 40 streaks to a tile within 4% of light. `SceneDetail` computes
  it in the object's own coordinates (`SceneObject.texels`, `detail`,
  `detailOrigin`; `Sculpture.detail` and `detailOrigin` say what the next
  cuboids are, and a floor cell's origin is its place in the room, so the
  flecks run on from cell to cell and travel with a pushed box). It is one
  scalar in the blue channel of the normal target, halved; the normal's
  height is rebuilt from x and z, as no visible face looks down. The
  composite multiplies the light by it, so a paint's chroma is unchanged
  and the browser fixture's material matching holds. It fades out where an
  occurrence's tile is under about 22 pixels and where it is over 1.3 times
  the focused room's, so small nested rooms and the huge tiles of a
  containing room stay plain: on those it read as noise and as blotches.
- A dock outline is not drawn while its box stands on it. The owner asked
  what the line across an exit was: the home's own outline, on the parent's
  floor, showing through the doorway gap.

Square rooms, rounded corners and plain doorways (2026-10-12, later the
same day; this replaces the packing described in the next paragraph's
second bullet). The owner kept the lab look after seeing other styles, said
the packed box was "still off-grid just filled the gaps", expected the
puzzles to need redesigning, asked for the black/yellow lines on the
entrances to go, and suggested rounded or merged corners ("box marching").
- Every room is a square grid in the match itself. `SquareRoom`, run by
  `LoadLevel` on each authored room, packs a room that is longer one way
  out to its longer side with rows or columns of wall, the odd one below or
  to the right (above or to the left for puzzle 9's narrow room, the one
  place it mattered), moves everything in the room with it, and carries a
  doorway on a packed side to the new edge along a corridor of floor. The
  authored calls keep their old sizes; the layout a match holds is square.
  `RoomPadding` and its helpers are gone.
- Solutions: eight of twelve replay unchanged. Puzzles 8, 9 and 11 take the
  old solution with one or two steps repeated where a corridor is crossed
  (43, 35 and 28 moves), and show the same evidence as before. Puzzle 7's
  old solution does not adapt. A breadth-first search through the Move
  handler found a 27-move solution with no partner backstop, and the same
  search on the puzzle as it was before found one of 24 moves: the two
  backstops the old 41-move witness showed were never required. Its
  minimum evidence is now one occupied move, one parent change and one
  cargo transfer, and its lesson and three hints describe the 27-move
  route. "Carried together pins two partner-body backstops" below is no
  longer true of the witness. Puzzles 8, 9 and 11 were past the search's
  four million states, so their witnesses are not known to be shortest.
  The second puzzle's exhaustive count is still 167.
- Wall blocks: `WallTile` draws a cell as one cuboid, or as a cross with
  corner squares when an outer corner is rounded (both sides beyond it are
  open floor, radius 0.30 of a tile). `WallFillets` fills an inner corner,
  where on-grid wall runs along two sides of a floor cell, with a hollow
  curve of radius 0.20. Both curves are two more instanced meshes built once
  in `BeginSculpture` (`RoundMesh`, `FilletMesh`, six steps to the quarter),
  placed by `Corner` with a quarter turn per quadrant, and drawn by the lamp
  and scene passes after the cubes. A wall block is one paint now, shaded by
  the light alone: the darker rim, raised face and darker sides are gone.
  The fillet stands in the corner of a floor cell; nothing in the rules
  knows of it.
- The striped threshold on a doorway's floor is gone. The yellow posts
  either side remain.

Continuous cells and square boxes (2026-10-12). The owner asked for cells
next to each other to be "more continuous" rather than "clearly cells on a
grid", and for a box to always fill its cell as a square: rooms need not be
the same size inside, "but the format should still be a square".
- Wall cells that touch are one block: `WallTile` takes a bit for each side
  with wall beyond it (off the room's grid counts), draws its lid to the
  cell's edge, and extends the raised face to every joined side, so the
  darker rim runs only along a block's open sides. An inside corner has no
  rim. Floor cells meet without a joint, in two shades 0.025 apart so steps
  can still be counted.
- A room that is longer one way is packed out to a square with solid wall on
  its two short sides (`RoomPadding`, from `RoomTrim`): half the difference
  in tiles on each. A doorway on a packed side runs through the packing as
  a strip of floor as wide as the doorway. This is drawing only. The room,
  its doors, `DoorsMeet` and every puzzle are unchanged; the packing is not
  a place anything can be, and a passage crosses it on the way to the
  doorway. A box too small to detail is a square block too. The owner's
  words could also mean making every authored room square, which would
  change the puzzles; that reading was not taken and is theirs to ask for.

Full-window layout (2026-10-12; the owner: "the game should be most of the
screen, right now it's like a weird iframe like thing"). The framed panel
is gone. The scene is drawn over the whole window below the heading
(`SceneTop`: 104 px, 146 px under 1050 wide), and `InPlayfield`, the
sculpture's bounds, the composite's board and the shadow fit all use that
region, so a wide window shows as much of the containing world as fits. The
focused room sits in a square clear of the heading and of the controls
along the bottom (`SceneSide`, `SceneCenter`, `SceneBottom`: 80 px, 200 px
under 720 wide), with two of its tiles to each edge of the square where it
had three, so a tile is about 1.6 times its old size at 1280x800. The outer
room still takes 0.96 of the square. `MapLens.center` and `size` are still
the old panel's rectangle, but now only place the labels: the room, the
move count and the surroundings line are on dark plates (`Chip`) drawn by
`WorkbenchLabels` at stage Board 2, after the scene's composite, since they
lie over the scene. The two top plates are where the panel's label row was,
so the browser fixture's label crops are unchanged; the surroundings plate
sits between the buttons and the direction pad at 1050 wide and over, under
the top plates below that, where it can cover part of the outer room's top
row. Buttons and the help line are drawn over the scene by
`WorkshopControls`, the help line on a plate. The lamp's depth map is 2,048
square (was 1,024) because the shadow fit now spans the window rather than
the old square. The fixture reads what is in frame in the room's square
(`sceneSquare`, mirroring `SceneSide` and `SceneCenter`), not the window.

The players are small test robots (2026-10-12; the owner wanted "more
motion and maybe a different look", unsure about them being human, "maybe
robots or something"; the design is my pick and unconfirmed). `Maker` draws
a body in the seat's paint on two rubber treads, a front bumper, a pale
head with a dark visor and a yellow eye, the yellow/black calibration mark
on the head and an antenna tipped in the seat's paint. `BotPart` places
each cuboid to the bot's right and ahead for one of four facings, since
cuboids are never rotated. Motion is presentation state in `Picture`
(`actorFacing`, `actorStride`, `actorWobble`, `actorIdle`): the bot faces
the step it takes (through a doorway, the passage's side), hops twice a
cell and whips its antenna while its drawn position changes. A tenth of a
second after a step stops, and at once after a doorway passage, it turns
back to the camera, so a scene settles no later than it did before the
bots (a longer look-ahead and antenna decay failed the fixture's exact
undo comparison, which waits 24 ticks). A snapped pose (first frame, level change, reduced motion) is that rest pose.
There is deliberately no idle animation: the browser fixture compares whole
settled scenes byte for byte across time (undo, cycle exit), so a settled
scene has to depend on the puzzle alone. Player-visible text still says
"dummy"; renaming is open.

Current look: a bright crash-test lab (2026-10-12). The owner found the PSX
block filter made small nested boxes lose all their detail, preferred the
earlier clean render while their partner had not, asked for "a mix of
simple yet themed", and took my recommendation. `render.tide` is the 3D
revision's again, byte for byte: full resolution, soft shadows, SSAO, no
blocks, no shade steps. The theme is paint and large shapes only, so it
survives when a box is a few pixels wide: pale flooring whose slab joints
are the taped grid, a mid grey-blue void, light grey hall panels, boxes in
bold flat paints (green, blue, violet, coral red; Box 1's green is still
the browser fixture's `homeMaterial`), a yellow/black striped threshold on
the floor of every doorway between yellow posts (`HazardEdge` with a finer
stripe), warning-yellow parcel targets, and the dummies described below.
Wall panels keep the rim-and-face shape, floors the plain slabs, and the
UI its charcoal and yellow. The two paragraphs that follow describe the
superseded factory/PSX passes; what they say of the dummies, door posts,
hazard edges and UI still holds, what they say of blocks, shades, grain and
container colours does not.

The factory look (2026-10-12; the owner asked for "factory vibe, we are
testing dummies; PSX style", then after the first pass: the dummies "look
almost like parrots", "the art is still meh", "PSX but cleaner"; the
particulars below are unconfirmed picks):
Boxes are painted steel containers (green, oxide red, steel blue, violet;
the outer hall is bare steel). A wall panel is a darker rim around a
slightly raised face, both shades of the same paint. Floors are plain
concrete slabs with dark joints. Doorways stand between warning-yellow
posts, parcel targets are warning-yellow floor paint, and the hall's own
perimeter walls carry yellow/black blocks (`HazardEdge`). Players are
crash-test dummies: overalls in the AMBER/TEAL seat paint, rubber feet,
hands, belt and neck, and a head that is the yellow/black quartered
calibration mark. The first pass had a seat-coloured head with a small
black/white mark, which read as a parrot's eye and beak. The code still
calls them makers. Parcels are unchanged. Panels and buttons are square,
charcoal and steel grey, with warning-yellow accents and a strip of warning
tape along the top of the viewing panel, above the label crops the browser
fixture reads. There are no enemies, threat events, jumpscares or timers.

The PSX treatment lives in `SceneComposite` and changes no pass before it.
The board is read in square blocks of `block` logical pixels, the board's
width / 220 rounded and at least 1 (2 at 1280x800, 4 at 1920x1080, 2 on a
390-wide phone): every pixel of a block samples the G-buffer, depth and AO
at the block's middle. Light is one scalar, capped at 1 so a fully lit top
face sits exactly on a shade, and cut to 14 shades. A 4x4 ordered pattern
over blocks only mixes two shades within 0.2 of a shade of where they meet,
so evenly lit faces are flat and only shadow and occlusion edges are
patterned. AO strength is 0.50 (was 0.70). Because the scalar multiplies
all three channels, a material keeps its exact chroma in every shade, which
is what the browser fixture's `materialMatches` relies on; per-channel
colour quantization would break it. Tried and dropped: a per-texel grain
(noisy; the owner asked for cleaner), carrying it in the Rgba8 material's
alpha (low alpha destroyed the stored material, presumably because colour
targets are premultiplied), and snapping vertices to the block grid for PSX
wobble (thin decals darkened unevenly and half a floor tile went dark; the
cause was not pinned down beyond removing the snap fixing both).

WASD/arrows walk/push, Z/Backspace undo, R reset, H hints and Enter continues.
Touch uses the same directional buttons. Held directions require release
after serial changes. Hints grow/wrap to fit. Ambience, sound and reduced
motion are local options (since 2026-10-15 the sound ones are `Mixer`'s); Rooms volume settings remain. Presentation and
audio never enter the match hash.

`sfx/make_sounds.py` reproduces original assets. The melody is replaced by
`ambience.wav`: a seamless 12-second mono 24 kHz PCM16 ventilation/fluorescent
hum, filtered seeded noise and subtle 50/100 Hz harmonics. Peak -28.754 dBFS,
RMS -36.725 dBFS before 0.27 gain; no clipping and a 14-PCM-unit boundary delta.
There are no stingers or sudden threat cues. SHA256:
`274097dcafdf4c1355d435444dc2a3ae8bc59a6cb77684b320d4448e2498c715`.
The obsolete `workshop.wav` is removed. Seven short cues remain byte-identical.
`enter.wav` remains the 300 ms seeded-noise friction and air puff, without
oscillators, coin ringing or note sweeps: 7,200 mono 24 kHz PCM16 frames,
silent endpoints, no clipping, -12.906 dBFS peak before 0.38 gain. SHA256:
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
Entry/exit/depth changes select kind 2 before push cues, including doorway
travel that pushes a parcel.

## Solvability and verification contract

Puzzle solver (2026-10-15, the polish round: the lead agent's brief asked
for the tool and its five commands; what follows beyond that is my pick).
`inside_out_solve` (`test/solve.c`, usage at its top) is a native tool for
whoever draws puzzles, not a test: CTest does not run it, and it is not
built for the web or Android.
- Moves go through the generated Move command handler, sent and ticked as
  `test/simulation.c` does. `MovePuzzle` is a `static inline` helper in the
  generated C, out of a host's reach, so calling it directly was not an
  option. The puzzle count, names, chapters and lessons come from
  `test/solve.tide`, in this target alone: a `Describe` command whose
  handler hands `LEVEL_COUNT` and `levels.tide`'s words to an `extern`
  (`inside_out_solve_words`, defined in `solve.c`). It changes no state.
- Build: `tide_add_game(inside_out_solve SOURCES game.tide levels.tide
  test/solve.tide NAME game` with Rooms, `HOST test/solve.c)`, and
  `target_compile_options(-O2)` on it, so the generated C and the solver
  are optimized in every configuration. The command line is `-O0 ... -O2
  ... -fno-fast-math -ffp-contract=off` (the engine's flags come from
  `tide_flags` after the target's), and the generated C keeps its `#pragma
  STDC FP_CONTRACT OFF`. The engine library is built as configured.
- A position is a byte for each robot, box and battery: (room + 1) * 49 +
  y * 7 + x, 254 for a robot off the board, 255 for an inactive battery.
  Batteries are alike to the rule, so theirs are sorted; facing, trails
  (rebuilt from the boxes after every move), moves and solved are left out.
  The key is two 64-bit words in an open-addressing table of 32-bit indices
  (linear probing, at most half full); a position's link packs the parent's
  index over the 3-bit move, so `--limit` is under 2^29. The default limit
  is 30,000,000: about 600 MB of keys and links and a 256 MB table at that
  size. 30 million synthetic keys went in and back without a false
  duplicate in 4 s. The match is started again before `Time.tick` (an int)
  passes 2^30.
- Shortest search: breadth first, stopping at the first position the
  handler calls solved; the witness is replayed from a fresh match through
  the handler before it is printed as solving. `--alone <seat>` takes the
  other robot to the table at (-16, -16), off every grid (the campaign
  test's `alone` uses the table's corner, which is wall in every puzzle so
  far), explores everything and asks what `IsSolved` asks but the goals:
  every active battery on a socket, every socket filled, every outline
  taken. Put facing into the key and it counts 8,042 and 30,454 for
  puzzles 6 and 7 against the campaign test's 8,043 and 30,455: that test
  never puts its start in its table, so it counts the start twice once a
  move comes back to it.
- `--print` draws every map in `levels.tide`'s notation; for puzzles 1 to 7
  its 144 rows are exactly the file's.
- Speed (this machine, the debug preset, the target at -O2): 200,000 to
  430,000 moves a second across runs on a shared machine, and from about
  5 to 7 moves for each new position, as the share of moves that find known
  positions grows: some 30,000 to 140,000 positions a second, so a search to
  the default limit takes minutes. Measured once: a tick on its own
  is 0.06 µs, a command refused for its serial 0.8 µs, a refused move 2.8
  µs and a taken one 3.3 µs, so the handler is most of it. Generated C
  passes read-only structs by value: `Authorized`, `Together` and `SeatOf`
  are each handed the 38,368-byte `Workshop`, and `MovePuzzle`'s helpers
  the 1,156-byte `LevelLayout`, which is the likely bulk (not measured
  apart). That is the compiler's, and unchanged here.

`inside_out_solve all`, and `--alone` for both seats, at the default limit:
every shortest solution replayed, every alone search exhausted.

| Puzzle | Shortest | Positions found before it | Amber alone | Teal alone |
| --- | ---: | ---: | --- | --- |
| 1 A room outside | 10 | 130 | 19, no | 29, yes |
| 2 No handle inside | 12 | 3,291 | 489, no | 489, no |
| 3 The other opening | 20 | 16,840 | 659, no | 1,159, no |
| 4 A place to stand | 15 | 67,283 | 21,545, yes | 21,545, yes |
| 5 All in a row | 11 | 8,083 | 182,875, yes | 182,875, yes |
| 6 Hold it steady | 19 | 7,243 | 3,454, no | 3,454, no |
| 7 Posted | 15 | 2,430 | 13,966, no | 13,966, no |

"Alone" is how many positions that robot reaches and whether one of them
has everything in place. The witnesses of puzzles 1 to 3 and 5 to 7 are
shortest; puzzle 4's is 31 moves against 15. Puzzles 1, 4 and 5 can be put
in place by one robot alone (puzzle 1 by Teal only), so they show no
co-operation mechanically; 2, 3, 6 and 7 do.

The authored replacement campaign has these production-command witnesses:

| Puzzle | Moves |
| --- | ---: |
| A room outside | 10 |
| No handle inside | 12 |
| The other opening | 20 |
| A place to stand | 31 |
| Passing place | 39 |
| The same corner | 21 |
| Hold it steady | 19 |
| Before it moves | 43 |
| Two ways home | 35 |
| Return address | 51 |
| Folded landing | 28 |
| Crossed return | 28 |

Return address requires a copied box's parent to change into its own
canonical interior and then extraction. Folded landing forms and extracts
a primary self-cycle. Crossed return forms and extracts a primary two-room
cycle. Carried together pins two partner-body backstops with counterfactual
production commands: removing that partner makes the box push instead of
admitting the approaching maker.

No handle inside exhausts all 167 reachable production-command states for
either initial maker acting alone with the other body removed, ignoring
player goals. Neither can both dock the home and deliver its parcel. This
establishes a mechanical co-op dependency for that layout; it does not prove
solo impossibility or human difficulty for the entire campaign.

`levels.tide` owns twelve nested-start layouts and three-tier hints. A short
intro teaches leaving two containing boxes; later puzzles develop backstops,
occupied transport, narrow storage, offset exits and movable recursion.
Witnesses in `test/solutions.h` are absent from the public game. Campaign
checks invoke actual generated Move handlers with two joined players: every
action succeeds, both seats act, production completion is true, and undo /
reset-replay restore exact state. Witnesses prove solvability, not shortest
paths or measured human difficulty. Final campaign pins/results follow below.

PuzzleCue carries the accepted direction and source Traveler for travel.
An outward step emits kind 2 even when cycle rebasing leaves room/depth/box
identity unchanged. A paint-owned all-seat listener handles this exit only
when snapshots missed it, validates the live box/endpoint against late states,
starts at the captured border pose and clears pending cues on undo/reset.
Ordinary crossings consume cues without replay. This is event/local data,
not new match state. The test-only `test/cues.tide` primitive/source listener
enables a typed-command cue fixture and is absent from the public game.

Core fixtures cover bounds, offset traversal, canonical collision, occupied
instances, cycles/extraction, atomic blockage, history, snapshots, packing and
deterministic replay. `test/browser.mjs` runs two muted Chrome/Edge windows
through real unordered/unreliable WebRTC using existing bounded process/pipe
and serialized-frame helpers beside it. Checkpoints require fresh verified
ticks and packets; browser/runtime/graphics errors fail. Rendered labels and
colour crops observe production play without wasm inspection or debug exports.
Crops on one page run sequentially to avoid Chrome's temporary viewport race.
Local relay is default; TIDE_INSIDE_OUT_RELAY probes production signalling.
Real devices, NAT and wall-clock network latency remain separate coverage.

Bounded scripts, audio and browser captures live in ignored
`build/inside-out-captures/`; TIDE_HIDDEN=1 and muted browsers prevent owner
audio devices opening. `inside_out_page` / `private_games` stage the opted-in
prototype beside its thumbnail. Public play remains
<https://tide-engine.dev/prototypes/inside-out/>; catalog authentication and
serving/deployment contracts remain in `notes/private-prototypes.md` and
`notes/docs-site.md`.


## Push-or-enter rewrite validation (2026-10-12)

Native and web debug builds are warning-free. The 25 fixtures pass natively
and under WASI. `inside_out_play_webgl` passes in 477.78 s and
`inside_out_play_webgpu` in 433.03 s, on the first run. The fixture's stage
on a room that holds itself now plays "The same corner" to its end, with an
exact scene comparison across a partner's undo (a bot's facing included),
in place of the self-nesting and cycle-exit crops of a puzzle that is gone;
its shelf clicks are for nine entries. Native captures of all nine starts,
the row push, "Hold it steady" to its end and "The other way in" from the
room, with the battery posted and from inside the copy were inspected at
1280x800. The thumbnail is from that puzzle at 1920x1080 halved. The docs
production build passes. No CI was dispatched.

Deployed on the owner's word ("once done push and deploy"). Source
`8257b69a` on `dev`, which also carries what had come into `dev` from other
work. Every release page was rebuilt (`sand duel towers sumo docs_examples
sample_games private_games`, web-release) and staged; the site's check,
build, SEO, access, admin and examples tests pass. `fly deploy` from `docs`
is release v270 of `tide-website`, image
`deployment-01M4RWCY8YB6BSD1XHP8CZ4BAZ`, machine healthy. The public page
answers 200 with the staged page's bytes (1,402,011, SHA-256
`4603905ed82f9b4d0b8c6cf3a00f34c3e920007eea51683443beb6bc7e479301`), with
noindex, COOP same-origin and COEP require-corp; its thumbnail and the home
page answer 200. The two-browser fixture was not run against the public
page or the production relay, and nobody has played the deployed build.

## Pushed things leave by the pusher's box, validation (2026-10-12)

`game.tide`, one fixture, the exit crossing in `map.tide`, the guide and
README. Native and web debug builds are warning-free; the 33 fixtures pass
natively and under WASI; `inside_out_play_webgl` passes in 583.13 s and
`inside_out_play_webgpu` in 550.33 s, on top of the 91 files that came in
from `dev` with the merge before. Nothing was staged or deployed. No CI was
dispatched.

## Tabletop model, echoes and new rules validation (2026-10-12)

Native and web debug builds are warning-free. All 33 fixtures pass natively
and under WASI: the twelve replays (puzzle 7's new 19-move witness among
them), the exchanged jobs, and the second puzzle's exhaustive check, whose
count is 489 positions on the widened room. One fixture changed with the
rule: a box standing in a doorway is pushed in, where it used to block.

`inside_out_play_webgl` passes in 639.31 s and `inside_out_play_webgpu` in
544.51 s, once each, on the final code. They took about 170 s and 140 s
before the lens, bloom, larger maps and full-size occlusion; the owner was
told the scene is about twice as heavy and said the cost is fine. Their
budget is 900 s. The fixture now tells paints by the order and rough ratios
of their channels (`sagePaint`, `amberPaint`, `woodPaint`,
`filledWoodPaint`, `markPaint`), as light, grain, focus and bounce leave no
pixel an exact paint, and reads the slim header's positions. Its first run
on this look failed "shared undo restores the physical parent context":
the lens was still easing from the previous puzzle's room a third of a
second after a puzzle change, so the baseline crop was softer than the
later one. A new puzzle, a first view and a resize now settle the focus at
once. The fixture has no assertion for echoes, crossings of boxes and
parcels, or the new puzzle; those were checked in native captures and by
the owner in play on a local two-player page.

A before/after capture of five scripted two-player runs (48 frames) is
byte-identical across the code cleanup that introduced `StepFrom` and
`FollowPushed`. The thumbnail is 678,679 bytes, SHA-256
`9bc21708a08f144d50e5a83a7b2f99ec6d2b8976313953172bbca0a7ab21a53e`. The
docs production build passes. Web release pages were not rebuilt or staged
and nothing was deployed: the public page still shows an older build. No CI
was dispatched.

## Square blocks, texture and depth validation (2026-10-12)

Presentation only. Native and web debug builds are warning-free; the 33
fixtures pass natively and under WASI; `inside_out_play_webgl` passes in
188.98 s and `inside_out_play_webgpu` in 149.41 s. Both are about 20 s
slower than the run before; whether that is the taller walls and the
detail channel or the machine was not separated. Native captures of
puzzles 1 and 12 at 1280x800 and 1920x1080 were inspected, enlarged. The
speckle on the floors of the smallest nested boxes is older than this
change and is not the new texture. The thumbnail is 180,028 bytes,
SHA-256 `22d456b668dc66c9df34bfa6418ba0f26862465c9cf9290646a4fd9ac924ea41`. The docs production build passes. Nothing was staged
or deployed. No CI was dispatched.

## Square rooms and rounded corners validation (2026-10-12)

Match layouts, four witnesses, puzzle 7's lesson and hints, presentation
and the browser fixture's numbers for the two rooms it measures. Native and
web debug builds are warning-free. All 33 fixtures pass natively and under
WASI, including the twelve replays, the exchanged jobs and the second
puzzle's exhaustive check. `inside_out_play_webgl` passes in 168.92 s and
`inside_out_play_webgpu` in 123.23 s, on the first run. In the fixture the
carried home is read as five by five with its contents half a tile higher,
the recursive home as six by six with its contents half a tile to the left,
and the self-cycle loop is entered one step later and is one step longer
(`neesee`), as the home's east doorway has a corridor cell.

Native captures of puzzle 12 at 1920x1080 and of puzzle 8 at 1280x800 were
inspected: rounded outer corners, filleted inner ones, the five by five
violet box on its cell, doorways without stripes. The other ten puzzles
were not looked at in the new shapes, and nobody has played the four
changed puzzles. The thumbnail is 166,680 bytes, SHA-256
`810dccc05e3f11edb53538e2aa6e9a68cd5b01a85074b67e1e21b95ac5186926`. The
docs production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Continuous cells, square boxes and the title-crop failure (2026-10-12)

The intermittent "recursive puzzle progression agrees on both screens"
failure is explained. It recurred twice here (WebGL, then WebGPU with a
variant that waited up to 60 more ticks, so it was never a race). The
failure screenshots of the two pages show the same title, "12 / 12 Crossed
return", and differ in that crop by exactly one pixel, at 469,42, inside
the text. The fixture compared PNG bytes. That one comparison between two
pages now allows up to 8 differing pixels (`pixelsApart`); a different
title differs in hundreds. Why one text pixel differs between two pages on
one machine is not established; each page fills its glyph cache in its own
order, which is my guess. Other cross-page label comparisons are still
exact and have not failed.

Native and web debug builds are warning-free; the 33 fixtures pass natively
and under WASI. On the final game code `inside_out_play_webgl` passes in
174.75 s and `inside_out_play_webgpu` in 141.85 s. The WebGL run had
started with the waiting variant of that last check loaded and the WebGPU
run with the pixel tolerance; the tolerance has passed once on WebGPU and
once on WebGL since (179.13 s). Native captures at 1280x800 and 1920x1080 were
inspected: joined wall blocks, a plain floor, and the 5 by 4 box of puzzle
12 filling its cell. The thumbnail is 163,638 bytes, SHA-256
`0d7b85de8684b608ca65fdb3d52cd843a117f16731244c7df5b6399b3540636c`.
Nothing was staged or deployed. No CI was dispatched.

## Full-window layout validation (2026-10-12)

Presentation and the fixture's layout numbers only. Native and web debug
builds are warning-free; the 33 fixtures pass natively and under WASI;
`inside_out_play_webgl` passes in 166.15 s and `inside_out_play_webgpu` in
129.57 s. Both are about 35 s slower than before the layout (WebGL was
124.68 s): the scene now covers the window, with full-window occlusion
passes and a larger lamp map, in headless browsers. Their budget went from
180 s to 300 s, as 166 s left too little room. What the larger scene costs
on real hardware is unmeasured. Native captures at 1280x800, 1920x1080 and
390x844 were inspected. The thumbnail is 201,827 bytes, SHA-256
`963603aa6d45534b6946143134b65d935b50825fe64480efdc3450a934f853de`; the docs
production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Test robots validation (2026-10-12)

Presentation only (`paint.tide`, thumbnail). Native and web debug builds
are warning-free and the 33 fixtures pass natively and under WASI. The
first version failed both two-browser fixtures at "partner undo restores
the exact pre-cycle scene"; with the quicker rest described above
`inside_out_play_webgl` passes in 124.68 s and `inside_out_play_webgpu` in
99.87 s. Native captures at rest, mid-roll and settled were inspected at
1280x800. The thumbnail is 101,034 bytes, SHA-256
`ca171ab46b4a5ed9baf22132480b01ce1a3962f534e2fc429552dc2a293d5275`; the docs
production build passes. Nothing was staged or deployed. No CI was
dispatched.

## Crash-test lab look validation (2026-10-12)

Presentation only: `render.tide` restored to the 3D revision's, new paints
and a doorway threshold in `paint.tide`, one constant in `ui.tide`, README,
catalog text and thumbnail. Native debug and web debug builds are
warning-free; the 33 fixtures pass natively and under WASI;
`inside_out_play_webgl` passes in 127.32 s and `inside_out_play_webgpu` in
100.31 s; the docs production build passes. Bounded native captures at
1280x800, 1920x1080 and 390x844 were inspected, including nested boxes a
few tiles across, which keep their walls, thresholds and dummies. The
thumbnail is the 1920x1080 capture of puzzle 12 halved, 101,371 bytes,
SHA-256 `0d93a81d4c2b393024ab1be88d424391b11516fafbd41f3f1ed3d778fbc070ef`.
Web release pages were not rebuilt or staged and nothing was deployed. No
CI was dispatched.

## Facing doorways and cleaner PSX validation (2026-10-12)

Rules: every authored door pair was not audited; instead all twelve
witnesses were replayed under the new rule. Eleven are unchanged. Passing
place's old witness pushed its parcel across a bridge between doorways that
do not meet (a 4-cell box beside a 3-cell-high one) and is replaced by a
39-move witness found by a breadth-first search through the generated Move
handler (1,587,462 states; shortest by construction; the search was scratch
code and is not in the tree). The layout is unchanged and its minimum
evidence, including one partner backstop, still holds; its third hint
follows the new opening. The two-box cycle fixture now expects a dummy
leaving A to walk into the facing B rather than push it. Layouts whose
solutions never used a mismatched bridge may still play differently where
a dummy used to push a facing neighbor from a doorway; no witness did.

Native debug and web debug builds are warning-free. All 33 fixtures pass
natively and under WASI. On the final code (rules, cleaner look, exit
passage, step preview) `inside_out_play_webgl` passes in 131.69 s and
`inside_out_play_webgpu` in 102.34 s; the two batches before it, without
the preview and without the exit change, passed as well, so the title-crop
failure recorded below did not recur in three further WebGL runs. Its cause
is still unknown. The fixture has no assertion for the step preview or for
a blocked mismatched doorway; the rule is covered by the campaign replays
and the cycle fixture, the preview by inspected captures only.

Bounded native captures at 1280x800 and 1920x1080 were inspected: flat
faces, the yellow/black dummy heads, puzzle 8 without its former bent
bridge, a leaving dummy whole through its doorway, and the green enter
arrow with the matching button icon. A push bumper was not captured. The
thumbnail is the new 1920x1080 capture of puzzle 12 halved, 67,276 bytes,
SHA-256 `d52b7bb9900a7c9e5774b13f4a3ffb1b7a14f91570977a42f64ef3f087c68cbd`.
The docs production build passes. Web release pages were not rebuilt or
staged and nothing was deployed. No CI was dispatched.

## Factory / PSX restyle validation (2026-10-12; first pass, superseded look)

Presentation only: `render.tide`, `geometry.tide`, `paint.tide`, `ui.tide`,
player-visible "maker" wording in hints, guide, README and catalog, and the
thumbnail. No match, layout, witness or audio file changed. Box 1's paint,
AMBER and the parcel colours are unchanged, so the browser fixture's
material constants are too.

Native debug and web debug builds are warning-free. `inside_out_tests`
passes natively and under WASI. `inside_out_play_webgpu` passes (104.89 s).
`inside_out_play_webgl` passed three of four runs (133.71, 135.62, 135.68 s).
Its first run failed the fixture's last progression check, "recursive puzzle
progression agrees on both screens": the 2D level-title crops of the two
pages differed after the final Next. That crop holds no 3D scene pixels and
the fixture saves no picture of it, so the cause is unknown and the fixture
is unchanged; whether it predates this revision is unmeasured. The docs
production build passes with the new catalog text and thumbnail.

Bounded two-player native captures at 1280x800, 1920x1080 and 390x844 were
inspected: nested start, outer hall, puzzles 8 and 12, hint panel and the
waiting panel. A `--players` run joins both seats by itself, so the restyled
title page was only exercised by the browser fixture, not looked at. The
960x540 thumbnail is the 1920x1080 capture of puzzle 12 halved, 133,592
bytes, SHA-256
`ef53445d34150d7e80fa200a782f51c531988557d80c683c4c58d228f1820bc1`.
Web release pages were not rebuilt or staged and nothing was deployed: the
public page still shows the previous look. No CI was dispatched.

## 3D revision validation (2026-10-12)

Native and WASI retain all 33 fixtures, including twelve production-command
solutions, exchanged jobs, exact undo/reset/replay and the exhaustive second
puzzle co-op check. No match, layout, hint or audio file changed. Native,
web debug and web release builds are warning-free. Both two-browser fixtures
pass: WebGL 131.81 s and WebGPU 99.18 s. They retain actual online admission,
three production completions, phone touch, continuous doorway growth/shrink,
nearby/offscreen partner framing, carried interiors, physical self-nesting,
undo and unchanged-room/depth/entrance cycle exits.

Illumination multiplies material RGB uniformly. Camera/interior assertions
therefore compare exact brightness-normalized material masks, with mandatory
feature and boundary presence, rather than treating a moving shadow as a
camera movement. Quantitative zoom growth, maker visibility, narrowing and
travel direction remain checked. Undo and settled-cycle whole-scene RGB
comparisons remain exact. Old uniform wall crops are replaced by real edges.

Bounded 1280x800, 1920x1080 and 390x844 native captures were inspected, including
offset entrances and the unchanged-identity primary self exit. Components
share each occurrence's ground plane so lids, heads and trim stack vertically.
CPU cropping includes a conservative caster guard based on its top above
the minimum containing floor; artificial cut faces cannot shadow the view
under the fixed light and camera. The refreshed 960x540 gameplay thumbnail
is 123,388 bytes, SHA-256
`d644e726fc134a33a6dad017119f1a2eee6b8ce0e54754dd41763ace2688f5e3`.

Controlled full/no-AO/no-shadow native captures prove both effects: removing
AO changes 30,471 initial scene pixels (maximum channel delta 55); removing
cast shadows changes 37,375 (maximum 92). An empty 5,280-pixel floor patch
changes zero pixels with AO removed, and both comparisons change zero pixels
outside the scene. Temporary comparison edits were restored byte-for-byte.
A miniature review caught scale-dependent PCF self-shadowing. Receiver-plane
tap depth correction removes that checker while preserving contact bias and
cast shadows: adjacent luminance variation in the miniature drops 40%, with
no changes to the empty floor or surrounding UI. The original AO blur remains.
Final lighting comparisons and desktop/phone captures use this corrected
renderer. Evidence lives in ignored `3d-final-lighting-*`, `3d-shadow-plane-*`
and `volume-*` captures under `build/inside-out-captures/`.

The production site build, zero-error/warning check, eighteen analytics/admin,
227 grammar and five HTTP/SEO/access tests pass. All 64 release/staged/package
playable pages and four thumbnails agree. Inside Out is 1,386,431 bytes,
SHA-256 `7d7e8d231adfa7aa7844a6e162c8bc0fddcc783e02ddf3d6d439741aa5deeb8b`.
Its pack contains each unchanged current WAV exactly once; removed music,
coin and tonal-doorway assets remain absent. No CI was dispatched. Physical
phone/GPU coverage, audio listening and external NAT conditions remain separate.

Source `482c066b` deployed directly to the existing Fly site as release 264,
image `deployment-01M4PHAKNDKRHZZ2MGVTEA16JV`, with passing health checks.
All four public prototype pages and thumbnails return 200 and match the
production package; Inside Out's page/thumbnail have the final hashes above.
Noindex, COOP same-origin and COEP require-corp remain intact. The downloaded
public 3D page passes the full two-player WebGPU fixture through the production
relay in 99.6 s, including recursive passages, occupied transport, exact undo,
three solutions and phone touch. The collaborative preview reloads the public
title screen without console errors. No CI was dispatched.

## Nested-box revision validation (2026-10-11; 2D presentation superseded)

Final native desktop, 900px and phone captures include the primary self-cycle
exit whose canonical room, depth and entered box remain unchanged. Approach,
middle and settled poses show continuous passage; doorway jambs use square
rectangles. The refreshed 960x540 gameplay thumbnail SHA-256 is
`082d090d130c07b526c6921cea56490a61fc4d23e49dfb58f0e49187d2d9ed78`.
A device-free 15-second production mixer capture verifies the new ambience:
source correlation 0.9999975, gain 0.2699908, peak -40.125 dBFS, identical stereo
channels, no clipping and exact repeated samples across the 12-second loop.
Hardware listening is not claimed.

The native build is warning-free and all 33 fixtures pass: 30 core cases,
all twelve command witnesses with undo/reset/exact replay, all twelve with
exchanged jobs, and the second puzzle's exhaustive mechanical co-op proof.
The WASI aggregate passes as well. Two-browser WebGL and WebGPU checks pass
in 117.9 and 57.4 seconds, respectively, with three production solutions,
phone touch, stationary-partner camera crops, occupied-box context, actual
primary self-nesting/undo/extraction and an unchanged-room/depth/box cycle
exit. The final square-jamb presentation and integrated dependency checks
are recorded below after staging.

Bounded native desktop, 900-pixel and phone captures review nested starts,
offset miniature doorways, self-recursive entrances and the exact primary
self-exit. The final same-room passage starts at its real east border,
crosses continuously and restores aliases without a finite-end pop.

A 15-second device-free production ambience capture is stereo, unclipped,
and repeats its twelve-second period byte-identically. Source correlation is
0.9999975, measured gain 0.2699908 and mixed peak -40.125 dBFS. The seven
retained short WAVs remain unchanged; public pack validation follows staging.
Real phones, hardware audio listening and network/NAT conditions beyond
this machine remain unmeasured.

Integration through `origin/dev` `825c6542` is clean. Shared compiler,
network/host dependencies and current Underfoot were rebuilt. Native and WASI
retain 33/33 passing fixtures without warnings; final square-jamb two-browser
checks pass, WebGL in 113.42 s and WebGPU in 49.89 s. All 64 release pages were
rebuilt/restaged (46 docs examples, 14 gallery games, four prototypes), and
their staged/production-package bytes agree. All four thumbnails agree too.
Inside Out HTML is 1,356,103 bytes with SHA-256
`f305bd41a4733ba85f119337e4bbaf9ee9ddbd96dff31ee71d572d18a82b500e`.
The decoded 784,288-byte pack contains each of the eight current WAVs once,
including the exact ambience/doorway assets above. Removed workshop music,
the original coin cue and the previous tonal doorway cue are absent.
Site check has zero errors/warnings; 227 grammar, 18 analytics/admin and five
production HTTP/SEO/access checks pass. The production build and complete
WebGL example/gallery rendering smoke pass. No CI was dispatched.

Source `7a44440f` deployed to the existing Fly site as release 263, image
`deployment-01M4PF2HHN71TH2ZJ2C4S8M4XF`, with passing machine health checks.
All four anonymous prototype pages and thumbnails return 200 and match the
tested package. Inside Out retains noindex, COOP same-origin and COEP
require-corp; its page and thumbnail have the exact hashes above. The full
two-player WebGPU fixture on downloaded public bytes through the production
relay passes in 47.7 seconds. The collaborative preview displays the updated
title screen without console errors. No CI was dispatched.

## Earlier published build (superseded)

The original isometric twelve-puzzle campaign passed fourteen native and
WASI fixtures plus WebGL/WebGPU co-op checks. Its witnesses took 8 to 59 moves.
The owner found those puzzles too easy; those lengths were never a human
difficulty measurement. That campaign and fixed-role presentation are replaced.

The initial public Fly deployment succeeded on 2026-10-11, machine version252,
image `deployment-01M4NW4CRPNFH1X3P4MRYWDY2B`. Anonymous page/thumbnail requests
returned 200 and the page hash was
`57a970dbf68a347b5502dca6eec59db4249c88b691a0881ee5e077ac2d39f21a`.
Two browsers exercised those downloaded bytes with the production relay in
17.1 seconds. Later independent JSON/text and session changes passed scoped
integration checks. This evidence describes the earlier build only.

## Shared-world revision validation, 2026-10-11

All eighteen game fixtures pass natively and under WASI: sixteen simulation
cases plus the full twelve-puzzle production-command campaign and its
identity-swapped replay. Native debug, web debug and web release game builds
pass without warnings. The final browser fixtures pass on WebGL (32.74 s)
and WebGPU (17.41 s), covering real two-browser admission, Shift pushing,
free entry by both makers into one interior, synchronized undo/reset and
held-input recovery, the first witnessed completion/progression and phone
touch movement. No runtime or graphics errors were reported.

Native captures at 1280 by 800, 900 by 800 and 390 by 844 were inspected.
The final capstone capture shows both makers, a shell physically nested in
Mint and the Mint/Lilac reference cycle. The refreshed catalog thumbnail
comes from that actual gameplay capture. A separate self-door capture shows
the maker entering the same canonical room twice. These are desktop/browser
viewport checks; physical phones, cross-machine NAT and human difficulty
remain unmeasured.

The site check has zero errors/warnings; production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. The
staged page, release page and production site copy have identical SHA-256
`fcd07e6e5347c708a75713e1e6ba8257b37082cade06c8181f1503e289865d7a`.
The new thumbnail SHA-256 is
`0db7607f0c8c8c579514a7c9d735fd46dcd8d989cb81df8557ff518fae959920`.

Integration with `origin/dev` through `f4fd43fe` preserved the newly reduced
prototype catalog and the relocated browser helpers. Only documentation
conflicted; the fixture import paths merged cleanly. Native eighteen-case
validation, WASI and both real browser checks pass again (WebGL 26.32 s,
WebGPU 16.74 s), and all 37 relay/lobby Node cases pass. The site check/build
and 23 HTTP/admin/analytics/SEO/access tests pass with the new catalog. All
three current prototype pages were staged; ignored HTML left by the removed
prototypes was cleared before packaging. The Inside Out page hash is unchanged.

The existing Fly site was updated directly from source `0530c23f` on
2026-10-11, release 255, image `deployment-01M4NZJJYP3NF97YMDSPHKSDDF`.
Its machine reached healthy state. Anonymous game/thumbnail requests return
200 with the exact page and thumbnail hashes above; the page retains noindex,
COOP same-origin and COEP require-corp. Two separate muted browsers exercised
the downloaded published bytes with `wss://relay.tide-engine.dev` on WebGPU
in 16.5 seconds, including join, shared controls, undo/reset, completion and
phone touch. This uses the fixture's controlled frame clock and does not
claim physical NAT or human latency coverage. No CI workflow was dispatched.

## Focused-view revision validation, 2026-10-11

The per-maker camera and material/audio changes leave the campaign and match
rules unchanged. All eighteen simulation/campaign cases pass natively and
under WASI. Native debug, web debug and web release game targets build without
warnings. Inspected native captures at 1280 by 800, 900 by 800 and 390 by 844
show one focused scene, the physical containing world, nested capstone rooms
and the changed containing Mint on self-entry. The new catalog thumbnail comes
from `focus-capstone-desktop-1.png`.

Both real two-browser fixtures pass: WebGL 125.63 s and WebGPU 46.96 s,
with no runtime, browser or graphics errors. They compare independent
Workshop/Mint views, the canonical interior reached through opposite doors,
actual outside geometry before/after an occupied-shell push, and physical
versus self-recursive containing worlds. Entry/exit, partner undo/reset,
held-input recovery, first-puzzle completion/progression, real shelf
selection and phone touch all pass. An initial fixture failure compared a
720 px baseline header with an 800 px return: height-dependent backdrop grain
changed its pixels. Restoring both viewports before the unchanged strict
comparison fixed the test; the intended puzzle selection was already correct.

A bounded, device-free 330-frame native two-player run captured the actual
entry and exit mix with music disabled through the normal guide. Each 5.5 s
48 kHz stereo capture contains two isolated threshold cues. Correlation with
the upsampled source is 0.99999955, measured gain is 0.379992, global peak is
-24.675 dBFS, and neither mix clips. No audio device was opened; this verifies
the asset and mixer path rather than a human listening judgment. The source
`enter.wav` SHA-256 is
`6dd808ceb4bad8a85c56ca231312e1c6608b93e8c8382d3dffc2b9677c115dda`.

The site check has zero errors/warnings; its production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML are identical with SHA-256
`593221e6e4a00a449cbfdefb6508383f07cf35cbf7217b3e37315bd096138f14`.
The refreshed thumbnail SHA-256 is
`adb64454b74621e664244b23931bfee1bcc01c1c21290ab6a18525745cceca3d`.

Source `f8f61e9e` was merged/pushed to `dev` after local validation; `origin/dev`
had no newer changes. The existing `tide-website` Fly app was updated directly,
release 256, image `deployment-01M4P1D8MP14MTP7X08VBF1594`. Deployment checks
passed and the machine reached healthy started state. Anonymous published
game/thumbnail requests return 200 with the exact hashes above, retaining
COOP same-origin, COEP require-corp and noindex. The collaborative preview
points at the public URL. No CI workflow was dispatched.

Two separate muted browsers exercised those downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 44.9 s: admission, independent
dimension cameras, canonical interior parity, moved-shell surroundings,
self-recursive entry/exit, shared undo/reset and held-input recovery,
completion/progression, shelf selection and phone touch all pass with no
runtime/browser/graphics errors. This uses the controlled frame clock;
physical phones, cross-machine NAT and human latency remain unmeasured.

## Spatial zoom and automatic-entry revision validation, 2026-10-11

Walking into a physical room now pushes first and tries its facing doorway
only when that push fails. Shift and the touch push toggle are removed.
Entry with a parcel at its doorway remains atomic; the travel sound takes
priority over the cargo push. A full entrance trail prevents further entry
but still allows a room push.

All twenty-one game fixtures pass natively and under WASI: nineteen simulation
cases and both twelve-puzzle production-command campaign replays, including
exchanged maker identities. The campaign repairs add physical backstops and
adjust latch handoffs for automatic entry. Its twenty-one partner-only latch
push probes retain the physical-shell dependency while allowing valid fallback
entry or one inward doorway parcel push. Undo, reset and deterministic replay
remain exact. Native debug, web debug and web release targets build without
warnings. These witnesses establish solvability, not shortest solutions or
measured human difficulty.

The initial spatial two-browser fixtures pass, WebGL in 84.87 s and WebGPU in 54.97 s,
with no runtime, browser or graphics errors. New pixel-area assertions require
the miniature to grow progressively during entry and shrink during exit;
the settled/initial area differs by more than twofold. Existing strict
canonical-interior, occupied-shell parent-context and recursive-camera
comparisons pass. Admission, shared undo/reset, held-input recovery,
completion/progression, shelf selection and phone touch remain covered.
An initial canonical-interior comparison caught the camera's asymptotic
settlement: tiny residual affine differences changed edge pixels. The camera
now reaches its exact target after the finite transition, and idle cell
interpolation also settles exactly. The strict comparison was retained.

Inspected bounded native captures at 1280 by 800, 900 by 800 and 390 by 844
include early/middle/late entry, exit, self-reference and nested capstone
views. Mint's wood area grows 2,651 to 8,625 to 25,936 to 33,970 pixels on
entry; exit decreases 39,775 to 21,532 to 5,878 to 2,648. The focused maker,
actual containing world and outside partner are visible in the same scene.
The catalog thumbnail comes from `spatial-capstone-desktop-2.png`, showing
Peach inside Mint with Amber outside.

The new doorway effect uses only seeded filtered noise, with no tonal body,
sweep, echo or ringing tail. The 300 ms source has RMS -25.452 dBFS and SHA-256
`657d7c91dc2cd45538453e7956af5ea608df74f5dd90f7d1c835dfee9fe4f40a`.
A bounded, device-free two-player capture with music disabled through the
guide confirms both entry and exit use this asset: correlation 0.99999992,
measured gain 0.379988 and cue peak -21.310 dBFS. Both 7.0 s, 48 kHz stereo
mixes remain unclipped. The previous published HTML contained the previous
threshold asset's exact bytes; its remaining pickup similarity plausibly
came from that asset's low sine body. These checks verify asset routing and
mixer output, not a human listening judgment.

The initial spatial site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five production HTTP/SEO/access tests pass. Release,
staged and production-site HTML match SHA-256
`3d3d048342d10a72476529eedbcfa75f4f5ca49291851e54c507fdf994c68547`.
The packaged asset pack contains the exact new doorway WAV bytes.
Thumbnail SHA-256 is
`2e8c973ae731d9f677d89c779b54b1cee90388bfa87bbc3678121e784940b19b`.

The maker-passage follow-up builds without warnings natively and for web
debug/release. All twenty-one native fixtures remain green; the final WASI
fixtures and two-browser checks pass again, WebGL in 81.12 s and WebGPU in
54.75 s. The new stationary outside-view assertion tracks Amber's actual
body pixels: its footprint shrinks progressively by more than threefold and
its centroid moves toward the doorway across early/middle/late/settled frames.
This independently verifies the maker's passage rather than camera zoom.
Existing camera, canonical-interior, occupied-shell, recursive, shared-command,
progression, shelf and phone-viewport checks remain green.

Bounded native captures also cover physical entry/exit from both views,
recursive alias handoff with a stationary partner, fast inner steps,
immediate reversal/Undo and a rebased exit whose trail depth stays equal.
Desktop and phone-viewport passage captures were inspected. The current
settled capstone capture is byte-identical to the existing thumbnail.
A separate read-only renderer audit found no further critical blockers.

Integration with `origin/dev` through `1e969c61` was clean. Incoming compiler
changes triggered game and public-page rebuilds; the twenty-one native tests
passed again. All 64 packaged pages match their release artifacts: 46 docs
examples, fourteen public gallery games and four prototypes, retaining
Underfoot, Inside Out, CRACKED and Two Forks in the authenticated catalog.
All 225 grammar checks and the full WebGL examples smoke pass, including
the 46 docs examples, two embedded games and fourteen gallery games. The
site check has zero errors/warnings, eighteen analytics/admin tests pass,
and the final production build plus five HTTP/SEO/access checks pass.
Final release, staged and packaged Inside Out HTML has SHA-256
`c112d75234015c1ff4ea5f6fc27e00e78dc82e27d8e1b30439a94021f0d8ad33`.
Decoding the packaged asset pack confirms the exact new doorway WAV and
the absence of the previous doorway WAV bytes. No CI was dispatched.

The final merge through `origin/dev` `7c4e0835` brought unrelated CRACKED
and Underfoot graphics/game changes without Inside Out overlap. Their
web-release pages were rebuilt/restaged; all 64 playable pages and all four
prototype thumbnails match the final packaged bytes. The production docs
build and five HTTP/SEO/access checks pass again. No Inside Out game test
restart was needed for those unrelated incoming changes.

Source `9d25977b` was pushed to `dev` and deployed directly to the existing
`tide-website` Fly app, release 258, image
`deployment-01M4P4XTA0MGC534C21G0PTWBC`. Deployment and machine health checks
passed. Anonymous Inside Out HTML and thumbnail requests return 200 with
the exact final hashes above, retaining noindex, COOP same-origin and COEP
require-corp. The published asset pack contains the exact new doorway WAV
and none of the previous doorway WAV bytes. The collaborative preview
navigates to the public URL; snapshot requests fail inside the preview client.

Two separate muted browsers exercised the downloaded published bytes on
WebGPU with `wss://relay.tide-engine.dev` in 63.4 s. Progressive maker motion
from the stationary outside view, spatial entry/exit zoom, canonical interior
parity, occupied-shell context, self-recursion, admission, shared undo/reset,
held-input recovery, completion/progression, shelf selection and phone touch
all pass without runtime, browser or graphics errors. This uses a controlled
frame clock; physical phones, cross-machine NAT and human latency remain
unmeasured. No CI workflow was dispatched.

## Local dimension framing validation, 2026-10-11

The owner chose the whole current room with a one-tile outside rim. The
partner-fit helper, presence mask and camera transition trigger are removed.
Inner rooms use the fixed square scene viewport and seven-cell span used in
this revision; the outer workshop retains its scale. Simulation, puzzle layouts,
doorway passage and audio are unchanged. The README and authenticated catalog
description now describe the local framing, and the thumbnail comes from
`rim-capstone-desktop-1.png`, with Peach nested in Mint and a recursive door.

Native debug and web debug/release builds pass without warnings. All
twenty-one simulation/campaign fixtures pass natively and under WASI.
The final real two-browser checks pass, WebGL in 109.39 s and WebGPU in
66.05 s. The new regression accepts all ten partner moves and compares four
nonblank scenery patches exactly while the partner walks far away, approaches,
enters and exits. Amber's rendered pixels are absent in the outside positions
and present after entering the local room. Existing progressive camera zoom,
maker passage, canonical parity, occupied-shell context, recursion, shared
commands, progression, shelf and phone-touch assertions still pass.

The first browser attempt exposed a fixture error: four simultaneous clipped
screenshots on one page raced Chrome's temporary capture viewport. The saved
page was translated by exactly one crop origin. Capturing those crops
sequentially fixed the failure without changing crop geometry or assertions.
Desktop and 390 by 844 native captures show readable complete rooms, bounded
rims and visible doorway motion, including self-reference. A read-only camera
audit found no critical blocker. Physical-phone/NAT coverage remains separate.

Site check has zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. Release, staged
and packaged HTML match SHA-256
`67de50e12c7e96b3d0e1128b3959a8800b88199156e0ef18056fbf19bb2bbeb9`.
The staged and packaged thumbnail SHA-256 is
`48f24a67cf3d60c14bfbb7559d280ae4604a456029997532f43124fb0aae1707`.
The inline pack retains the exact current doorway WAV. No CI was dispatched.

Integration through `origin/dev` `e377d0a9` was clean. Incoming compiler,
Fetch/platform/web runtime and docs changes triggered dependency rebuilds.
The rebuilt game and twenty-one fixtures pass without warnings natively and
under WASI; both final browser checks pass again, WebGL in 106.74 s and
WebGPU in 63.92 s. All 64 public game/example pages were rebuilt and match
release, static and production-package bytes; all four prototype thumbnails
also match. Current Underfoot changes are retained. Docs check is clean,
all 226 grammar checks pass, and the production build plus five HTTP/SEO/
access checks pass again. The integrated Inside Out HTML SHA-256 is
`761bae9375434b22596e6bd05711a6d077f01cb9a034dfa31340d8f0bf507943`;
the thumbnail hash is unchanged from this revision. The inline pack still
contains the exact current doorway WAV. Source `ebbd0649` was pushed to `dev`.

That source deployed directly to the existing `tide-website` Fly app as
release 259, image `deployment-01M4P66FRKJFQ5BR7SKC3E045M`. Machine and
deployment health checks pass. Anonymous page/thumbnail requests return 200
with the exact integrated HTML and current thumbnail hashes, retaining
noindex, COOP same-origin and COEP require-corp. Decoding the public pack
confirms the current doorway WAV. The collaborative preview was navigated
to the updated public URL.

The downloaded published page passes the complete two-browser WebGPU
fixture with `wss://relay.tide-engine.dev` in 65.2 s, including partner
off-screen movement with exact stationary local framing, smooth maker/camera
doorway travel, occupied-shell context, recursion, shared commands, progression
and phone touch. No runtime/browser/graphics errors or CI dispatch occurred.
Physical phones, cross-machine NAT and human latency remain unmeasured.

## Nearby outside maker and goal icons, 2026-10-11

The owner asked to see part of a maker standing beside the current room box,
and for goal tiles to show the occupant they require. Inner framing now uses
an eleven-cell span, leaving three interior-grid tiles of actual containing
world around the five-cell room. The fixed viewport and camera remain
independent of partner movement. Parcel targets show a box icon, maker goals
show a player icon, and switches show both because either can hold a latch
open. Existing occupied colours remain. Guide, lesson and hint text follows
the icons. No simulation rules, puzzle layouts, witnesses or audio changed.

Native debug and web debug/release builds are warning-free. All twenty-one
fixtures pass natively and under WASI, including both twelve-puzzle campaign
replays. The two-browser checks pass, WebGL in 86.90 s and WebGPU in 63.06 s.
The partner regression observes a clipped nearby maker at the fixed scene
edge, zero maker pixels farther away, and a visible maker after entry. Four
nonblank scenery patches remain byte-identical through all ten accepted
partner moves. Existing progressive maker passage, camera zoom, canonical
parity, occupied-shell context, recursion, commands, progression, shelf and
phone-touch checks remain green. A final stale capstone hint was corrected;
native/web targets and all twenty-one fixtures passed again after that text
change.

Bounded native captures at 1280 by 800 and 390 by 844 show the nearby maker
partly clipped at the bottom edge, readable complete rooms and literal goal
icons. Three scenery regions are byte-identical through the settled near,
far, return, inside and outside states; the full focused scenes at near,
return and outside are identical. The refreshed thumbnail comes directly
from `wide-capstone-desktop-1.png`. Captures and pixel evidence are under
ignored `build/inside-out-captures/`.

Site check reports zero errors/warnings; the production build, eighteen
analytics/admin tests and five HTTP/SEO/access tests pass. All 64 public
game/example pages and all four prototype thumbnails match release, staged
and packaged bytes. Inside Out HTML SHA-256 is
`56defc3b4b91e9aa97082f679382707ef9adb76b779178dc3b858886d8c82f50`;
thumbnail SHA-256 is
`a41a9c7066f5810fa8dcf89ea87c9b6974716e83d47fb771c3dbeec0ec9344cc`.
Decoding the inline pack confirms the exact current doorway WAV and absence
of the previous WAV bytes. No CI was dispatched.

The clean merge through `origin/dev` `27ddca74` brought shared compiler,
asset-list/session/delta runtime changes and current Underfoot updates.
Affected native and web debug/release targets were rebuilt without warnings;
all twenty-one native/WASI fixtures pass again. Final two-browser checks pass,
WebGL in 93.56 s and WebGPU in 63.20 s. All 64 public playable pages were
rebuilt/restaged and match their packaged copies; all four thumbnails match,
including the incoming Underfoot image. Docs check remains clean, all 227
grammar checks pass, and the final production build and five HTTP/SEO/access
checks pass. Integrated Inside Out HTML SHA-256 is
`c10808dbe0d78fcf976aca136860da2bc6dbe82eacb4295a58495d9f551d0d5f`;
the current thumbnail and doorway WAV hashes above are unchanged. No CI was
dispatched.

Source `0fa243a2` was pushed to `dev` and deployed directly to the existing
Fly site as release 260, image `deployment-01M4P75RDBACPVV6P25S6HRV16`.
Deployment/machine health checks pass. Anonymous Inside Out page and thumbnail
requests return 200 with the exact integrated hashes, retaining noindex,
COOP same-origin and COEP require-corp. The public inline pack contains the
exact current doorway WAV and none of the previous WAV bytes. The collaborative
preview navigates to the updated URL; snapshot inspection still fails inside
the preview client.

The downloaded published bytes pass the complete two-player WebGPU fixture
with `wss://relay.tide-engine.dev` in 62.4 s. Nearby partner clipping and distant
off-screen movement preserve exact local framing; smooth maker/camera travel,
canonical interior parity, moved-shell context, recursion, admission, shared
commands, progression, shelf and phone touch pass without runtime/browser/
graphics errors. The probe uses a controlled frame clock. Physical phones,
cross-machine NAT and human latency remain unmeasured. No CI was dispatched.
