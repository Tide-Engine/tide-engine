# Prototype catalog and staging

The owner requested an admin section for games in progress on 2026-10-09, and
later that day public game URLs, so invited players need no admin access. This
note is the catalog and staging contract. A game's own rules, tests and
validation belong in that game's note.

## Engine gap audit (2026-10-18)

This is the complete disposition of engine reports in CRACKED, UNDERFOOT,
SUNDAY SUPPER, GRAVEWAKE, the physics-game notes and `GAME_TRIAL_FEEDBACK.md`.
Inside Out and Foldline were also searched for engine gaps; their remaining
reports concern game rules, content, presentation or playtest coverage.
Original observations are historical; this table records current behavior.
The owner authorized all clear wins, leaving language/API decisions for later.
It does not approve a new feature merely because a game worked around it.

| Report | Current disposition and evidence |
|---|---|
| Nested-container `foreach` emits invalid C (SUNDAY SUPPER) | Fixed: stable receiver paths, captured indexes and waits/replay. `notes/language.md`, `prototype_codegen`. |
| Vector lanes/swizzles in stored list/map/array elements reject writes (CRACKED) | Fixed through the existing in-place path, with bounds/capability checks retained. `prototype_codegen` and two error fixtures. |
| Captured integer literals widened to shader floats leave unused C temporaries (GRAVEWAKE) | Fixed, including negative/int-to-long captures. `prototype_codegen`. |
| Local singleton insertion changes match RNG (CRACKED) | Fixed with match-only ordinals, including attached local fields. Native/WASI debug/release parity is recorded in `notes/random.md`. |
| Presentation/source positioning changes simulation identity | Fixed both singleton ordinals and compiler-created discard names. `tidec_asset_usage` regressions. |
| Attached local fields reject fixed arrays (GRAVEWAKE) | Fixed parser lookahead, not a new type rule. `local_fields` checks defaults, helper/element writes, separate machines, reset and local packing. |
| Headless tests write real saves (UNDERFOOT) | Addressed by explicit desktop `--data-dir`, including separate player directories. Defaults retain ordinary saves. `notes/standard-host.md`. |
| Whole-singleton replacement is forbidden (GRAVEWAKE) | Stale for ordinary values: current direct-system probe compiles and `singleton_params.Replace` already executes it. Resident/grid ownership restrictions remain intentional. |
| Eight-parameter shader-bound query hangs tidec (GRAVEWAKE) | Already fixed: `view_bound_query`, `notes/shaders.md`. |
| No Box3D triangle meshes (CRACKED) | Stale: static/kinematic mesh shapes and rewind queries are built, `packages/box3d/README.md`, `notes/box3d.md`. Arbitrary convex hulls/joints remain separate below. |
| No audio, Spawn/Send helpers or multi-player scripts (original game trial) | Already built: audio, caller-capability propagation and standard-host `--players`. Their notes supersede the historical trial. |
| Match event/command handlers cannot query | Explicit current spec restriction. Adding queries affects event/structural-change ordering; owner language decision. Local handlers already take queries. |
| Queries cannot be function parameters | Explicit non-storable/non-passable query-row contract in the spec. Needs a borrowing/capability design rather than copying an implicit world into helpers. |
| A system writing one component cannot set another entity's same component | Existing row/World write restriction in `check_world_call`; event routing is the current supported contract. Alias/scheduling semantics need review. |
| Local handlers cannot read match lists/maps/grids | Tick-event journaling intentionally excludes heap payloads, including query rows. The spec says these copies omit them; lifting it needs lifetime/copy-cost design. |
| `with ownership` in local handlers, local fields through query rows, mutable local query bindings | Explicit current query/handler limitations. Need caller/row ownership and local-write rules. |
| Spawn accepts component literals, not component values | Current component-composition interface; broadening it needs language/evaluation/ownership rules. |
| Input/event values cannot be helper parameters | `method_type` explicitly refuses TY_INPUT/TY_EVENT. A language change, not a missing codegen case. |
| No empty Map literal | `default` is the existing empty-map expression. A collection-literal syntax choice remains for the owner. |
| A CPU drawing helper cannot name a frag/vert | Explicit `check_mesh_draw` restriction: shader component/singleton bindings are the view's implicit context. Needs an explicit-binding/helper capability design. Ordinary unshaded drawing helpers already work. |
| A GPU helper cannot take a singleton containing Cubemap/Random | Spec deliberately requires resource parameters themselves and excludes 64-bit Random values on GPU. Field projection/specialization would be a new contract. Numeric values and direct texture parameters are supported. |
| A helper cannot borrow List<Clip> from an asset glob | `element_ok` permits resource collections only as assets. Borrowing resource collections needs ownership/lifetime rules shared with Texture/Mesh; not silently broadened. |
| No live shape-cast self/layer filtering | Current Physics.Cast/Raycast still have no filter argument. RewindQuery already has ignore-Body overloads, so the blanket original report is too broad. A common live/rewound filter API remains to choose. |
| No arbitrary hull shapes or joints | Still absent from the package API; triangles are supported. Shape/constraint APIs and runtime boundaries need their own approved task. |
| No instanced skinned draw | Still a future API, not an existing Draw.Instanced overload. Pose/instance transport and batching need renderer/runtime agreement. |
| No custom fonts, wrapping or Texture size | Still missing public APIs. Font ownership/shaping/measurement, wrapping behavior and resource metadata are API choices. Current measuring/manual layout works. |
| No quaternion.FromToRotation / float4x4.Scale | New math API choices, including degenerate/opposite-vector behavior for FromToRotation. Existing TRS and game helpers remain valid; no new runtime dependency introduced. |
| Model materials are not imported | Base color/vertex-color factors already import; embedded textures, full materials and separate render parts do not. Their representation/bindings need design. `notes/assets-and-files.md`. |
| Separate clips only bind by bone names | Matching now includes complete parent chains and permits differing source defaults. Different-name/proportion/coordinate-space retargeting remains absent and needs an explicit design. |
| Catalog members retain underscores | Current inferred-name normalizer preserves valid underscores; contracts can use exact `[Name]` aliases. Changing identifier normalization is a naming decision, not failed importing. |
| Texture/cubemap packs are large; globs keep whole folders | Ordinary textures now include BC1/BC3 plus exact RGBA fallback; cubemaps retain pixels. Pack fallback/compression policy and per-entry reachability differ from conservative declaration pruning. No new deletion/pruning policy inferred. |
| GUI focus takes game keys / ClaimKeyboard hides Escape (UNDERFOOT) | Matches documented focus/claim behavior when the match reads no Devices. Granular ownership/navigation policy needs review; the game workaround remains. Inside Out's suspected behavior was not separately playtested. |
| Package pages draw beneath game views | Views obey the existing global ordering/stage and draw-layer rules. Selecting a new package/game composition convention is a design decision. |
| Volume above 1 clips (CRACKED) | The mixer preserves gain above 1 and sums before output; integer/device output has finite headroom. Automatic limiting/compression or gain normalization would change audio policy. |
| C tests cannot call static TIDE_HELPER functions (UNDERFOOT) | Helpers are implementation details, not exported C API. Export syntax/stability and external-call capabilities need a language/C-boundary decision. |
| Touch controls, missing voice chat, art/NPC detail, campaign mechanics and human/hardware/network checks | Game/content work, a new networking feature or unmeasured coverage. Not compiler defects or implicitly authorized new runtime systems. Proximity voice remains unbuilt; no claim of human playtesting. |

All reproduced inconsistencies with existing contracts above are now addressed.
The remaining rows name the concrete decision/feature boundary; they are not
left as an uninvestigated set of "other possible wins". Private prototype
workarounds were not mechanically rewritten: those game revisions can follow
when their gameplay is next tested. Validation of the final array/host fixes is
recorded in `notes/standard-host.md` and `notes/language.md`.

## Catalog and public addresses

- `/admin/prototypes` is the catalog, beside Analytics and behind the admin
  sign-in. Its cards are data in the page's server load
  (`docs/src/routes/admin/prototypes/+page.server.ts`): id, title, kind,
  players, description and thumbnail. Nothing of it reaches client bundles,
  the public Samples gallery, search or the sitemap.
- Each game is public at `/prototypes/<id>/`, with no sign-in and no admin
  configuration required. A card links there in a new tab; a build without
  that game's `index.html` says In development instead.
- The public ids are listed in three places that must agree: `prototypePath`
  and `retiredPrototype` in `docs/admin.js`, and the asset gate in
  `docs/vite.config.ts`. The first lets a game's root through before any admin
  check. The other two keep a catalog id from being served or staged anywhere
  outside `/prototypes/`. An id outside the list is an ordinary unknown path.
- The games are public to anyone with the URL. Omission from the gallery,
  search and sitemap and the `noindex` header are discovery choices, not
  access control.

The site's auth, headers, tests and validation are in `notes/docs-site.md`,
Shareable prototypes.

## Staging a game

- A game's web page is self-contained and stages as
  `docs/static/prototypes/<id>/index.html`, through a `<id>_page` target
  (underscores for dashes) that copies the compiled page there. Generated
  `index.html` files are ignored by git.
- The thumbnail is checked in beside it as `thumbnail.png`; cards draw it at
  16:9 (960 × 540). Any future external wasm, packs, sounds or textures belong
  under the same directory.
- `private_games` (`prototypes/CMakeLists.txt`) keeps its name and collects
  every catalog game: its `<id>_page` target on the web, or the native game
  otherwise. The manually requested Docs workflow builds it with the public
  examples.
- Games under `prototypes/` are opt-in through `TIDE_PRIVATE_PROTOTYPES`, OFF
  by default; the Docs workflow configures with it ON. Nothing installs these
  games into the Tide package.
- A game's C test host stays behind a folder boundary (`test/tide.packages`)
  so `tide run` in the game's folder does not compile it as game code, as Two
  Forks' does.

Every catalog game's own target is built with `STATS` (the owner,
2026-10-13: prototypes show their frame rate, as `tide run --stats` does), so
its page has the stats drawer's line in a corner. A new game says it too.

To hand the owner a build to try, give a local address, not a path to find
(the owner, 2026-10-13): build the game's `<id>_page` target, serve
`docs/static` on localhost and name the game's URL.

Adding a game is a card, its id in the three patterns above, its thumbnail,
its `<id>_page` target and its `private_games` block, plus the id in
`prototypeIds` of `docs/tests/admin.test.mjs` and `admin-http.test.mjs`.

## Games

Removal (2026-10-15): the owner deleted Elsewhere, Copper & Coil, HOT SWAP,
DEADWEIGHT and SWINDLE. Their sources, assets, tests, game notes, build
targets, catalog cards and public-id registrations are removed. Their
implementation and validation records remain in git history. Former public
pages and assets are ordinary unknown paths; no compatibility routes remain.
Validation: native/web debug configuration with prototypes enabled succeeds;
the removed games have no targets or registered tests. Node 24.21.0 passes
the production docs build, zero-warning Svelte check and all nineteen
analytics/admin/production HTTP checks. Former pages, thumbnails, WASM,
packs and publisher PDFs return 404, including encoded ids; the five
remaining catalog cards and their public assets retain their serving tests.
Source, static and packaged directories are absent; note references and
`git diff --check` pass. No engine/compiler/runtime changes, CI or deployment.

Removal (2026-10-13): the owner scrapped NIGHT ROUTE after finding its delivery
and parcel-rule loop unfun. Its source, tests, thumbnail, game note, build
targets and catalog/public-id registrations are removed. The single-player
implementation and validation record remain in git history at `43f402a5`.
No engine code was changed by the prototype or its removal. No CI/deployment.
Validation: native/web debug configuration succeeds with no NIGHT ROUTE tests;
the production docs build and all nineteen analytics/admin/production HTTP
checks pass, including 404s for the removed page and thumbnail.

Removal (2026-10-13): the owner scrapped SKYWORKS. Its source, tests, thumbnail,
game note, build targets and catalog/public-id registrations are removed. Its
implementation and engine-gap record remain in git history at `7c047a87`.
No engine code was changed by the prototype or its removal.
Validation: native/web debug configuration succeeds with no SKYWORKS tests;
the docs production build, eighteen analytics/admin checks and production HTTP
check pass, including 404s for its removed page and thumbnail. No CI/deployment.

- FOLDLINE (`foldline`): `prototypes/foldline/`, a new first-person 3D
  cooperative portal puzzle requested on 2026-10-12. Its chambers sit in a
  sunlit research atrium. Linked portals show the actual
  destination scene from the traveller's transformed camera. It uses the
  same opt-in option; `private_games` collects `foldline_page` or `foldline`,
  staging `/prototypes/foldline/` beside its gameplay thumbnail. Its C test
  host is `foldline_test`, separate from CLI game discovery under `test/`.
  Rules, rendering contracts and validation belong in `notes/foldline.md`.

- SUNDAY SUPPER (`sunday-supper`): `prototypes/sunday-supper/`, a first-person
  cooperative horror story for one or two, requested on 2026-10-16 as a first
  playable slice (Friday evening to Saturday morning at the Hale farm). Its
  people are Quaternius' and its props Kenney's (CC0). It uses the same opt-in
  option; `private_games` collects `sunday_supper_page` or `sunday_supper`,
  staging `/prototypes/sunday-supper/` beside its gameplay thumbnail. Its
  story, contracts, tests and validation are in `notes/sunday-supper.md`.

- GRAVEWAKE (`gravewake`): `prototypes/gravewake/`, a first-person
  cooperative zombie survival game for one to four, requested on 2026-10-16
  in the vein of Black Ops 2's zombies with medieval weapons (33 of them),
  and made a roguelike the same day: endless waves in a forest clearing, a
  card after each. Its forest, people and weapons are Quaternius' (CC0), its
  first-person arms CoinCoin's (CC BY 4.0), its sounds CC0 recordings. It uses the same opt-in option; `private_games` collects
  `gravewake_page` or `gravewake`, staging `/prototypes/gravewake/` beside its
  gameplay thumbnail. Its rules, contracts, tests and validation are in
  `notes/gravewake.md`.

- Two Forks (`two-forks`): `examples/samples/two-forks/`. `sample_games`
  builds it with the samples and `sample_two_forks_page` stages it under the
  prototype root alone; `private_games` depends on that target. Its contract
  is in `notes/docs-site.md`.
- CRACKED (`cracked`): `prototypes/cracked/`, a port of the owner's Unity
  game, for one to eight players. `prototypes/CMakeLists.txt` adds the folder
  when `TIDE_PRIVATE_PROTOTYPES` is on and it has a `CMakeLists.txt`, and
  `private_games` collects `cracked_page` or `cracked`. Its contract and
  validation are in `notes/cracked.md`.
- Inside Out (`inside-out`): `prototypes/inside-out/`, the owner's two-player
  online cooperative puzzle, added on 2026-10-11. Both makers now start inside
  nested boxes and work outward through varied rooms and offset doorways,
  including movable self-nesting and cyclic containment. The owner requested polished 2D and planning
  puzzles after the initial build. It uses the same option, and
  `private_games` collects `inside_out_page` or `inside_out`, staging the
  public page at `/prototypes/inside-out/` beside its gameplay thumbnail.
  Its rules, puzzle verification and validation are in `notes/inside-out.md`.
  The subsequent per-maker view shows the current dimension with its actual
  containing world beyond the walls. Its catalog description and gameplay
  thumbnail reflect that camera; public/authentication boundaries are unchanged.
  The latest revision zooms continuously through the room's physical miniature,
  frames the local maker's whole current room with a fixed outside margin
  showing part of a maker standing beside the box,
  permits the partner to leave the view, and enters a facing doorway only after the
  automatic room push is blocked. Shift and the touch push toggle are removed.
  Spatial constraints replace switch locks. The current look (2026-10-12) is a painted tabletop model where the
  players are two small robots, with ventilation ambience
  and no threats or jumpscares; the catalog description and thumbnail follow it.

- Underfoot (`underfoot`): `prototypes/underfoot/`, a puzzle for one or two
  that the owner asked for on 2026-10-11 as a mashup of puzzle games, with
  free rein and a challenge. It uses the same option; `private_games` collects
  `underfoot_page` or `underfoot`, and the page stages at
  `/prototypes/underfoot/` beside its gameplay thumbnail. Its rules, puzzles,
  tests and validation are in `notes/underfoot.md`.

Removal (2026-10-11): the owner asked for every prototype but Two Forks to be
deleted. The Last Light, Cabin Nine, Dead Signal, Deadbell, Afterburn
(`rocket-league`) and Aerial are gone, with their tests, their
browser-playtest fixtures, their thumbnails, their notes and their catalog
cards. Their ids left the site's patterns too, so their old addresses are
unknown paths with no compatibility handling. Their sources, notes and the
Last Light browser-fixture history are in git. Inside Out isn't one of them:
the owner asked for it the same day, and it reached `dev` while the removal
was being made. Its browser playtest used three of The Last Light's test
helpers (`browser-process.mjs`, `browser-devtools.mjs`,
`frame-control.mjs`), which moved to `prototypes/inside-out/test/` as they
were; the helpers' own unit tests are gone with The Last Light.
