# Prototype catalog and staging

The owner requested an admin section for games in progress on 2026-10-09, and
later that day public game URLs, so invited players need no admin access. This
note is the catalog and staging contract. A game's own rules, tests and
validation belong in that game's note.

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

- Copper & Coil (`copper-coil`): `prototypes/copper-coil/`, a cooperative
  logic-circuit puzzle requested with a publisher demo on 2026-10-13. Two
  maintenance robots configure AND/OR/XOR/NOT consoles, test all input rows,
  commission a bridge and reunite at its exit. Six circuits share one workshop,
  with online, shared-keyboard and solo play. `copper_coil_page` stages the
  public page; `private_games` collects it and the game uses STATS. Gameplay,
  publisher material, tests and limitations are in `notes/copper-coil.md`.


- HOT SWAP (`hot-swap`): `prototypes/hot-swap/`, requested on 2026-10-13,
  a solo/two-player warehouse possession heist. Electrical creatures borrow
  cleaners, forklifts, cameras and door controllers to free a storm case and
  escape by ducts, loading shutters or shipping belt. `hot_swap_page` stages
  its public page; `private_games` collects it or native `hot_swap`, with STATS.
  Its isolated C test host is `hot_swap_tests`. Rules and validation are in
  `notes/hot-swap.md`.

- DEADWEIGHT (`deadweight`): `prototypes/deadweight/`, the owner's two-worker
  cooperative salvage prototype, approved and built on 2026-10-13. Three jobs
  in one industrial pumping station use Box3D cargo, winches, a bridge and a
  counterweight. It supports online rooms, shared keyboard and switching alone.
  `deadweight_page` stages `/prototypes/deadweight/`; `private_games` collects
  it or the native `deadweight`. Its main target uses STATS. Rules, contracts,
  tests and coverage are in `notes/deadweight.md`.

- SWINDLE (`swindle`): `prototypes/swindle/`, the competitive publisher-demo
  prototype requested on 2026-10-13. Ninety-second rooftop heists with a growing
  prism, dash steals, fake carriers, moving cash-outs, duel/FFA/2v2, bots,
  friend rooms and same-keyboard play. `swindle` has STATS; `swindle_page`
  stages `/prototypes/swindle/` with its gameplay thumbnail, collected by
  `private_games`. Rules, tests and validation are in `notes/swindle.md`.

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
