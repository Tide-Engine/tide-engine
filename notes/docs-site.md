# The docs site

## Example startup smoke and late scrolling (2026-10-22)

Build run 39790751086 built the site and passed exports/SEO/HTTP checks, then
timed out at draw3d's "starts in view" assertion. Its log contains no example
status or frame geometry, so the hosted cause is not established. The unchanged
smoke passes locally on Windows and Linux Chrome 155, including a one-CPU Linux
run and a Windows run with 12x CPU throttling.

The test now reselects the example and keeps its viewport on screen while waiting
for automatic startup, covering late layout changes or scroll restoration after
the original single scroll. It still requires iframe.shown and actual draw calls
within the existing guards. An unavailable example fails immediately; a timeout
reports scroll position, viewport bounds, visible status and each frame's source,
document readiness, start function and draw count. No site/runtime behavior or
timeout changed.

A controlled delayed scroll-to-top reproduces the old exact 20-second draw3d
timeout with scrollY=0, its viewport below the screen and no frames; the revised
check passes the same probe. This demonstrates the test race, not proof of the
original hosted cause. The revised complete WebGL examples/gallery/playground
smoke, all five playground compiler integration tests, focused draw3d WebGPU
smoke and all seven exports/SEO/access/admin HTTP tests pass (Node 24.19.0).
The production build passes and Svelte check reports no errors or warnings.

Integration with origin/dev 3a880ea7 is clean. Its shader/math dependency change
was rebuilt into all staged docs examples, samples and standalone games before
rebuilding the production site. All seven HTTP checks pass again. The first
merged browser run passed every rendering/playground assertion but failed at
Windows profile removal (EPERM). Teardown now waits for the child's close event,
including inherited pipes, and awaits asynchronous profile removal with the
same five retries and 200 ms retry delay. Its five-second shutdown fallback is
unchanged. The final full WebGL smoke and five compiler integration tests pass
with that teardown. This task changes only the docs runner and plugin packaging;
full native/web engine suites were not rerun and no workflow was dispatched.

## Schedule display grouped by stage (2026-10-22)

The schedule guide groups Steer and Expire together in stage 1 and separates
successive stage groups, matching the CLI. It explains stable within-stage tick
order and Fire starting after Steer even while Expire runs. No language or
runtime behavior changes. Implementation and validation: schedule-and-threads.md.

## Simpler navigation and sample source links (2026-10-22)

Package sidebar entries now use just Box2D, Box3D, Newton, Rooms, Lobby,
Steam and Voice chat. Removed the Live examples index and the separate Duel,
Towers and Sumo walkthrough pages; the examples remain on their subject pages.
Every Samples card has a separate Source link to its game folder on GitHub's
`dev` branch, including Sand's `bench/sand` and the two distinct Sumo games.
References to removed walkthroughs now link directly to source. Markdown exports
retain all fourteen source links, with export coverage checking that none vanish.

Validation: production docs build and both development/production HTTP export
tests pass, including page/sidebar/link/anchor checks (Node 24.22.0). Unstaged
playable pages still produce the build's existing allowed 404s. Muted headless
Chrome checks at 1440 and 390 pixels confirm fourteen visible source links,
no horizontal overflow, and no page exceptions; screenshots were inspected.
The collaborative preview snapshot timed out twice, so these layout checks
used the repository's Chrome DevTools workflow. Game code is unchanged.

## Optional Opus voice package (2026-10-22)

Packages now lists `voice` as the sixth optional package, with its own sidebar
page for setup, push-to-talk, receive gain/mute, optional cost, platform and
global-audience limits. Audio links to it instead of deferring voice; the spec
separates the approved feature from provisional APIs/limits. The production
build passes; the documented sample is also compiled by an installed checkout
CLI. Implementation, validation and remaining device coverage are recorded in
`notes/voice-package.md`. No live demo or site infrastructure changes.

## Bundled JetBrains editor setup (2026-10-21)

Install, Editor setup, CLI and README now describe automatic installation into
existing standard JetBrains profiles, the plugin bundled with the CLI version,
missing LSP4IJ installation, restart/locked-file guidance and first launch for
new IDEs. Custom configuration/plugin paths remain manual and Marketplace
updates retain their independent channel. No site navigation, spec or product
UI changed. Production npm build passes on Node 24.21.0 with existing allowed
unstaged-example 404s. CLI/package and full native/web validation are recorded
in notes/cli-and-packages.md, Bundled JetBrains installation.

## CLI usage data, admin tab and privacy page (2026-10-21)

The site receives the tide CLI's anonymous usage data (CLI side:
`notes/cli-and-packages.md`, Usage data).

- **`docs/telemetry.js`** (`createTelemetry({file, now})`): SQLite through
  Node's built-in `node:sqlite` (`DatabaseSync`; no dependency). Checked in
  `node:24-alpine` (Node 24.22.0, SQLite 3.53.4) and locally on 24.21.0, with
  no experimental warning. `TIDE_TELEMETRY_FILE` (`/data/telemetry.db` in
  `fly.toml`, on the existing `access_requests` volume; `.svelte-kit/telemetry/`
  locally) names the database; WAL journal. One `events` table, `STRICT`:
  `hour` (receipt time as hours since 1970, so truncated to the hour), `install`,
  `command`, `target`, `version`, `os`, `arch`, `ci` (0/1), `ms`, `outcome`,
  `error`, `files`, `packages`, indexed by `hour`. No address, user agent or
  any other request data is stored.
- **`POST /telemetry`** (`handle`, before admin and releases in `server.js`
  and the Vite dev middleware): `application/json` only (415), body at most
  64 KiB (413, by header and while reading), `{"events": [...]}` of at most 100
  (400), 30 requests a minute per address (`Fly-Client-IP` on Fly, the socket
  locally; in memory, 10,000 addresses at most) (429). `validEvent` keeps an
  event only if every field is from the CLI's sets (`allowed`, the version
  pattern, a 32-hex ID, a boolean `ci`, an integer `ms` up to a day, `error`
  exactly when `failed`) and drops unknown fields; a batch with none valid is
  400 (the CLI's curl `-f` then drops it), otherwise 204, `no-store`.
- **Retention**: `prune` deletes rows before 13 calendar months back
  (`setUTCMonth`); `start()` prunes at once and every six hours (unref'd
  timer); `stop()` closes the database at shutdown.
- **Admin**: `createAdmin` takes `telemetry`; `/admin/api/telemetry?days=1|7|30|90`
  behind the same session (401 anonymous, 503 without a store or password).
  `summary(days)`: daily commands, active installs and failures; weekly
  (seven-day windows back from today) distinct installs; totals (installs in
  range, today, last 7 days; CI share); per-command runs, failures and
  installs; failures by command and kind; versions and OS/arch by distinct
  installs; successful `build` times by target at the nearest-rank median, p90
  and p99. `/admin/telemetry` ("CLI usage") follows the Analytics page's
  layout, refreshes every minute and sits between Analytics and Prototypes in
  all three pages' tabs.
- **Pages**: `docs/tools/telemetry.md` ("Usage data", in Tools) lists every
  field and value, the notice, opt-outs, the dev-build/`TIDE_HIDDEN` rules,
  queueing, the endpoint, retention and the settings folder. `/privacy`
  (`src/routes/privacy`, prerendered, in the sitemap with its own social card
  and SEO checks) covers the site's aggregate counts and the access form, CLI
  usage data, and the relay: checked against `relay/`, which keeps addresses
  only in memory (per-address connection counts while connected, lobby rate
  buckets for about a minute), never writes them to disk or logs, and passes
  ICE candidates so players connect directly or through Cloudflare TURN. A
  small "Privacy" link is in the docs pages' footer (under the pager) and at
  the bottom of the home page; the site had no global footer. `tools/cli.md`
  has a `telemetry` row and section; README has a Usage data section.
- `Dockerfile`'s COPY list, `.dockerignore`'s allowlist, and the deployment
  copies in `admin-http`, `seo` and `exports` tests include `telemetry.js`
  (the latter two also gained `prototypes.js`, which `server.js` imports).

Tests: `tests/telemetry.test.mjs` (in `npm run test:analytics`): field
validation for every allowed value and 26 invalid shapes; a summary over two
installs and two ranges with exact daily/weekly/totals/commands/failures/
versions/platforms/percentiles; hour truncation, 13-month pruning by `prune`
and `start`, and the table's columns; the HTTP handler's 405/415/400/413/204/
429/404 answers with no address or unknown-field text in the database or its
WAL; admin API sign-in, ranges and 503 without a store. `admin-http.test.mjs`
posts an event to the production `server.js`, reads it back through the admin
API and `/admin/telemetry`, and checks the stored bytes have no address.
A muted headless Chrome pass over the built site (scratch script, not kept)
checked the admin tab with 60 events at desktop and 390-pixel widths, the
privacy page, the docs page and the home footer, with no page exceptions.

Validation (Node 24.21.0): `npm run check` with no errors or warnings, the
production build (existing allowed unstaged-example 404s), `test:analytics`
25/25, `test:admin-http` 1/1, `test:seo` 3/3, `test:exports` 2/2,
`test:access` 8/8, `test:access-http` 1/1, `test:releases` 9/9.

Making it live needs a site deploy (the Docs workflow on `dev`) and a release
containing the new CLI; the volume needs nothing new.

## Prototype catalog from its own repository (2026-10-21)

The prototypes moved to `Tide-Engine/tide-prototypes` (`notes/repository.md`,
Other repositories), with their notes, cards and thumbnails. The site no longer
names any: `docs/prototypes.js` reads `prototypes.json` beside the server,
which that repository's `private_games` stages with the thumbnails and pages
under `static/prototypes/<id>/` (all ignored by git). Its ids make the public
roots `admin.js` lets through and the retired-alias patterns that `admin.js`
and the build's asset check refuse; the catalog page reads the cards at request
time and still keeps them out of client bundles, search and the sitemap.
Without the file the catalog is empty and `/prototypes/<id>/` is an ordinary
path. The Docker image takes the file when present (`prototypes.jso[n]`). The
Docs workflow checks out the repository with `PROTOTYPES_DEPLOY_KEY` and
refuses to deploy without it. Earlier entries below name game notes that are
now in that repository, and their fixed id lists are history.

Tests: `admin.test.mjs` and `admin-http.test.mjs` use stand-in cards
(`two-forks`, `paper-boat`, `lantern`), or the staged catalog's real games and
thumbnails when `docs/prototypes.json` exists, generating every retired alias
from the ids; a new case covers reading/validating the file and the patterns,
including none.

## AI game-author onboarding and Markdown exports (2026-10-21)

Added `/guide/ai` (setup, copy prompt and download controls) and
`/guide/ai-context` (compact agent overview), linked from Get started, the
introduction, first-game page and README. Project AGENTS.md/CLAUDE.md starters
and a plain starter prompt are served under `/ai/`; these describe game authoring,
not engine contributor ownership or merge rules. A portable `tide` skill is authored
under `docs/static/ai/tide`; `/ai/tide-skill.zip` adds the generated overview reference
and optional agents/openai.yaml. The UI prompt is read by the prerendered server
loader from the same static source, avoiding Vite's public-asset import warning.

`content.ts` exposes expanded page sources. `exports.ts` generates all 63 public
page Markdown downloads (`/index.md`, `/guide.md`, `/game/commands.md`, etc.),
`/llms.txt` from navigation, `/llms-full.txt` excluding the spec, and
`/llms-full-with-spec.txt` preserving its decided/provisional/open distinctions.
Sources/code are shared with HTML; includes expand to real code, website controls
are removed, page anchors remain valid and bundles have unique internal anchors.
Metadata identifies the Git revision, clean/modified/unknown state and a content
fingerprint; downloads explicitly are website-source docs, not a release archive.
No CLI release bundling or automatic update of a game's pinned files is implied.

Exports and ZIP prerender at build time and serve from a copied adapter-node
deployment without Git, repository sources or node_modules. Unknown export pages
return 404. Vite intercepts known Markdown downloads before its raw-source file
middleware. HTML normalizes `.md` only for known documentation routes, preserving
static AGENTS.md/CLAUDE.md download names and query strings. Public HTML advertises
its Markdown alternate and llms index. No MCP service or new dependency was added.

Five fresh Luna/Claude game trials informed narrow guide corrections for screen
axes, player identity versus UI seats, filters, test sizes and evidence checks.
Inputs, output sources, reports, parent corrections and native/WebGL/WebGPU probes
are recorded in `notes/ai-onboarding.md` and its evidence folder. The overview download
uses `TIDE.md`, matching the starter instructions' local lookup; server.js restores
that attachment header for the prerendered file because adapter-node does not retain
the endpoint's custom download headers. The reusable skill
passes quick_validate.py. The exact published bounded input script was executed
with this checkout's starter game after correcting its first frame to one.

Validation uses Node 24.21.0 and LLVM 23.1.2: Svelte check has zero warnings/errors;
the production build passes with existing allowed unstaged-game/example 404s.
`test:exports` tests development and a source-free production deployment, every
page/code block/inline code/link/anchor, both offline bundles, ZIP membership/CRC,
static starters, and rejection of unknown exports. Its Windows teardown stops the
server before removing its working directory. Production desktop/mobile Chrome
checks verify clipboard contents, downloads, layout and no JS exceptions. All seven
export/SEO/access-form/admin production HTTP checks pass.
The Docs workflow now runs test:exports after its production build. No CI or site
deployment is dispatched by this task; hosted/other-platform checks remain separate.


## Empty scene shorthand (2026-10-20)

The Scenes page and decided spec document `scene Name;` and `local scene Name;`
as complete empty declarations equivalent to braces. The Main example uses the
short form. Production docs/grammar validation is recorded in notes/language.md.

## Double Position correction documentation (2026-10-20)

Views and the spec document Position on double scalars/vectors, add a custom
large-world position example, retain float rendering and describe correction
precision without changing Rotation or the existing smoothing rules.
Production build/grammar validation: notes/language.md.

## Double math and matrix completion (2026-10-20)

Basics, Math and the spec now describe complete existing component-wise double
math, binary64 constants, double matrix types/builders/conversions, CPU/GPU
boundaries and accuracy limits. Prior deferrals of trig/matrices are superseded;
double noise, double quaternions and automatic camera-relative rendering remain
separate. Production/docs grammar validation: notes/language.md.

## Double numeric documentation (2026-10-20)

Basics, Math, Text and lists, JSON, Input, Determinism and the spec document the new
binary64 scalar/vector values. They keep defaults and float rendering APIs clear,
show subtract-before-narrowing for large coordinates, describe full precision
text/JSON/input and distinguish the owner-approved types from first-build
conversion/wire choices and the deferred double math/rendering work. Production
build and all 268 shared grammar cases pass; no site deployment or CI dispatch.
Compiler/runtime validation: notes/language.md.

## Shared player ceiling documentation (2026-10-20)

The language settings table, Matches player-count section and spec report the
owner-approved 65,536 maxPlayers ceiling, shared with dedicated connections.
Matches distinguishes per-match capacity from the host's aggregate capacity;
defaults and the existing layout/restart and workload-scaling guidance remain.
Compiler help agrees. Validation: production docs build passes; networking
and compiler validation are recorded in notes/netcode.md.

Recorded containers (2026-10-19): Events, Functions and the spec describe
read-driven capture, ordinary copy/borrow rules, event-turn/tick-end/confirmed
timing and current attached local state. The old omission/error and open-item
statements are removed. Provisional codec/identity details remain distinguished
from the owner's decided behavior. No navigation or deployment changes.
Validation: notes/language.md.

Query local fields (2026-10-19): the decided spec and Views, Functions, Events
and Systems describe current attached local reads, mutable bindings, helper
capabilities and verified/recorded match timing. Views includes the approved
MarkDead example. Remaining local-world component/query lifetime restrictions
stay explicit. No navigation/deployment change. Validation: language.md.

Recorded-owner event filters (2026-10-19): Events replaces the manual Owner /
Session guard workaround with with/without ownership, including verified,
replay and Shown/Hidden endpoint behavior. The decided spec records the owner's
event-turn rule and the remaining local-world/query restrictions. No navigation
or deployment changes. Validation is in notes/verified-and-local-handlers.md.

Mutable World.Set overlap (2026-10-19): Systems, Functions, Events and the
decided spec describe owner-approved active-binding replacement through
helpers, captured read-only/copy values and ordinary subsequent write-back.
The stale helper-overlap sentence on Systems is corrected too. No site
navigation or deployment changed. Validation is recorded in notes/language.md.

Editor setup discovery (2026-10-18): moved the single Editors sidebar entry
into Get started, between Install and Your first game, labelled Editor setup.
The route stays /tools/editors for existing links. Its opening table links
each editor's instructions, shows tide ide commands alongside package-manager
choices and names the official public repositories. The introduction explains
copying the generated configuration, choosing one install method, the terminal
plugins' MIT licensing, separate tidels installation and current non-MELPA
availability. The detailed configuration examples remain in their sections.
Install now has an editor-setup section and accurately limits automatic
extension installation to the VS Code family. Your first game and README
also link directly to setup. No plugin, CLI, language or licensing behavior
changed; no public plugin export needs updating.

Validation: production npm run build passes with Node 24.21.0, including the
existing link/anchor/sidebar checks and allowed unstaged-example 404s. One-off
checks of generated HTML, navigation and search.json confirm the sidebar order,
unique editor route, links to both terminal-editor sections/public repositories,
visible MELPA status, introductory setup links and searchable chooser section.
git diff --check passes. No persistent text-mirroring test, CI dispatch or
deployment was added.

Standalone editor distribution (2026-10-18): README and Editors
now have package-manager examples for the public MIT tide-emacs and
tide-vim repositories, including package-vc-install, vim-plug and lazy.nvim.
They distinguish direct Git installation from the prepared, not-yet-submitted
MELPA recipe and the archive's one-month/public-maintenance and human-review
requirements. The owner approved editor-only MIT; both standalone repositories
are published and their package-manager installs verified. Bundled-mode links
also point to the public copies rather than private engine source paths.
The merged production docs build passes on Node 24.21.0 with existing allowed
unstaged-example 404s. No deployment or CI was dispatched. Implementation and
validation are recorded in notes/editors.md.

Installed terminal editor setup (2026-10-18): the CLI page has an ide section
and command-table entry. The Emacs and Vim/Neovim sections now start with
`tide ide <editor>`, explain where to copy its output, matching bundled server
paths, automatic/manual connections and moving the installation. README and
the tool READMEs point to the same helper; manual source-checkout instructions
remain in the tool READMEs for contributors. No navigation or language-spec
change is needed. The production docs build passes with Node 24.21.0 and the
existing allowed unstaged-example 404s. CLI/editor/packaging validation is in
cli-and-packages.md. No deployment or CI was dispatched.

Terminal editor setup (2026-10-18): Editors and the repository README now link
the new manually installed Emacs mode and Vim/Neovim runtime, with copyable
configuration and tidels selection. The page distinguishes LSP client support,
unadapted clickable lenses and IDE-only embedding from basic terminal editing.
No release installation or tide editors behavior changes. The production docs
build passes on Node 24.21.0 with the existing allowed unstaged-example 404s;
editor-host checks and remaining coverage are in notes/editors.md. No deployment
or CI was requested.

Fly host loss (2026-10-17): the site was down. Its one machine (82d1990b071698,
release 291) sat on a fra host Fly reported unreachable after its 05:15 UTC
host-restart incident ("a small number of hosts remain degraded or offline").
A deploy at about 10:00 (v292, Docs run 39023241585) failed at its Deploy step
on that host. A machine on a dead host can't be cloned, and its volume can't
be attached elsewhere. Recovery: a new `access_requests` volume
(vol_4m3mm6odj5eo97gv, fra) from the latest daily snapshot
(vs_yGmLzgNyzwn9U19bZabym, about 18 hours old); the dead machine destroyed
(`machine destroy --force`); a machine run from v291's image on the new volume,
with fly.toml's env and the `app` process-group metadata. The Docs workflow
then deployed dev (run 39040810696, release 293) onto it in place, with one
machine and a passing health check. Its first attempt timed out in the docs
browser test (`draw3d: starts in view`, 20 s); the rerun of the failed job
passed. The old volume (vol_4qle7yl3xx90wd6r) is kept: access requests and
analytics written after the snapshot exist only on it, if the host comes
back. Delete it once that's settled. The local `fly.exe` points to a
`flyctl.exe` that a self-upgrade left renamed to `flyctl.exe.old`.

Focused CLI help (2026-10-17): tools/cli.md and the README now describe the
compact command overview, command/argument topics, scoped lookup and --help/-h
command aliases. CLI behavior and validation are recorded in cli-and-packages.md.
No language semantics, site navigation or deployment changed.

Scene visibility unification (2026-10-15): Scenes, State visibility, language
basics and the spec use ordinary root Visibility groups for scene audiences.
They describe scene/entity/component intersection, live shared updates,
server-only scenes with Audience.None, local placement and root policy present
before initialization. Separate SceneVisibility and scene player-membership
APIs are removed. Spec Decided records the approved scope model; Provisional
retains the API/loading details. This supersedes older scene-membership API
descriptions below. Production docs and 244 shared grammar checks pass with
Node 24.21.0. No deployment or CI was requested.

Subscription hosting guide (2026-10-15): managed hosting now documents the
approved monthly included allowance, separate purchased credit, opt-in prepaid
extra usage, shared purchase cap, cancellation and billing controls. Public
launch pricing remains undecided; the configured EUR 20 plan is sandbox-only.
The production docs build passes; console/provider checks are in managed-hosting.md.

Folder deployment guide (2026-10-15): the CLI/managed-hosting pages now start
with `tide deploy`, explain automatic folder-named project creation and the
portable `tide.deploy.json` target, explicit relinking and failed target checks.
The credit guide points to Add credit beside the balance and explains sandbox
labels and unavailable payments. Console implementation/validation is recorded
in managed-hosting.md; no docs UI or routing changes.

## Visibility language documentation (2026-10-15)

Added `/game/visibility` to the Simulation sidebar with practical shared-group,
entity, whole-component and live-audience examples; chunk/player/deck use
cases, including public Time.seed/default Random versus an authoritative private
shuffle seed; local availability versus true lifetime; and current custom-event,
public-singleton, task and Resident limits. Scenes and Networking now describe
per-recipient projection rather than the previous whole-world-only warning.
Events, Local state and C hosts document Shown/Hidden, frame endpoint
reconciliation (including transient add/remove/re-add coalescing and canceled
prediction restoration), rebuild-safe resource preparation, arrival initialization,
per-listener transitions and recipient lifecycle journal callbacks. The spec
keeps owner-agreed scope/lifecycle separate from provisional API names and
implementation choices. Ordinary Transform parenting does not inherit policy. Scoped derived placement
anchors preserve permitted Transform subtrees across hidden parents; nonspatial
subtrees receive no blanket placement disclosure. Networking, Matches and Commands
qualify full-world page snapshots/host migration/command replay, and document
recipient logical baselines, server-only commands, normalized remote input history
and refused partial-world host promotion. The practical FPS guidance explicitly
identifies Box2D/Box3D public Physics collider/rewind lists as requiring
visibility-aware package integration; omitting Resident does not protect those
ordinary public copies.

`npm ci` installed pinned docs dependencies. Node 24.21.0 runs the shared
grammar tests (244/244), production docs build and SEO checks (3/3)
successfully, including the new visibility route, sitemap and social image. Prerender
prints expected 404s for unstaged example pages in this checkout; the build
still exits successfully. Native/web engine integration validation is tracked
in the lifecycle/netcode notes. No docs deployment or CI was requested.

Final integration with `origin/dev` `2c6de193` preserves the visibility guide,
managed-hosting guide, both sidebar entries and their separate spec contracts.
The merged production docs build and all three SEO checks pass with Node
24.21.0, including page/link/anchor validation, both routes' metadata, sitemap
entries and social images. The existing allowed unstaged-example 404s remain.

## Hosting console navigation and DNS (2026-10-15)

The owner requested a Console menu entry on tide-engine.dev, clarifying that
the homepage itself remains the engine site. Header.svelte links Console to
https://console.tide-engine.dev/ in both desktop navigation and the mobile menu.
No homepage redirect or apex DNS change was made. Validation: Node 24.16.0,
Svelte check with zero errors/warnings, production build and all three SEO tests
pass. Desktop and 390-pixel mobile browser checks confirm the link target and
visible menu entry with no horizontal overflow. The collaborative preview
became unavailable during resizing, so the mobile check used muted headless
Chrome. No new persistent test was added for the two navigation anchors.

Using the installed synced Cloudflare skill and the owner's browser OAuth,
registered the official Cloudflare API MCP with DNS read/write and account/zone
lookup scopes. The active tide-engine.dev zone had no console/play records.
Created DNS-only A/AAAA records for both subdomains pointing to tide-hosting
(66.241.125.185 and 2a09:8280:1::1b8:4b3a:0), TTL 300, and registered both Fly
managed certificates. Added the Fly-provided console ACME CNAME for independent
DNS validation. Existing apex/www records are unchanged. Play HTTPS is verified;
console issuance subsequently completed with an active Let's Encrypt certificate.
Both custom-domain `/health` requests return HTTPS 200 with `{"ok":true}`, and
the console homepage returns 200 with GitHub sign-in. No provider login/payment
was performed during these probes.

Source 2ca71d1d deployed directly to tide-website as release 282, image
deployment-01M501F063DS5S441CCCWYX27T, with its existing machine/volume and a
passing health check. The public homepage now has the Console link; muted
headless Chrome verifies the live desktop and mobile menu target with no
horizontal overflow. No CI workflow was dispatched for this navigation change.

Managed hosting guide (2026-10-15): multiplayer/hosting.md and the docs sidebar
describe login/deploy, server-only bundles, Simulation ID admission and lifetime/
save controls. The account dashboard is served by hosting/ on a separate origin
from game pages and the existing docs app. GitHub OAuth/Stripe integration and
Fly configuration do not deploy or add billing to the docs server. No extra
main-site header navigation or fake account data was added. Contracts, screenshot
validation and launch requirements are recorded in managed-hosting.md.
The browser compiler receives the same source toolchain identity through CMake
as native tools, rather than using the fallback unidentified-toolchain value.

## Playground compiler ABI and Docs toolchain (2026-10-14)

Run 38555232799 exposed the cause hidden by the earlier generic timeout:
`WebAssembly.instantiate` rejects the compiler's env.saveSetjmp function import.
Docs installed Ubuntu's Clang 18.1.3, whose Wasm setjmp lowering still requires
Emscripten helpers. The local Clang 23.1.2 build used WASI libsetjmp without
those imports. Startup time was not the demonstrated cause of either hosted
failure; the preceding readiness/diagnostic change made this error visible.

Docs now reads TIDE_LLVM_VERSION from cmake/LLVM.cmake and installs the same
LLVM major through apt.llvm.org as the main Build job, setting LLVM_ROOT for
both the web build and its native tidec host. Installation uses Bash pipefail
and logs the selected compiler version. Triggers, suite/deploy gates and
publication behavior are unchanged.

Staging compiles and inspects each tool's actual Wasm imports against the
pinned browser WASI shim: callable wasi_snapshot_preview1 functions, plus only
env.memory for tidec. Unsupported imports fail with the tool/name/kind and
the LLVM setup fix. The locally built tidec is checked before downloads or
asset writes; downloaded Clang/LLD are checked before packaging. No guessed
JavaScript setjmp implementation or new runtime function is supplied.

The new integration regression submits a valid module importing env.saveSetjmp
to the real staging process and pins its early rejection. A separate two-file
setjmp/longjmp program compiled with installed Clang 18.1.8 reproduces that
actual unsupported import and is rejected too. LLVM 23's release compiler,
Clang and LLD pass staging; all five compiler/staging tests pass.

Validation: Windows Clang 23.1.2 and Node 24.19.0; all 2,286 native debug tests
and 2,249 web debug tests pass, including native GPU and WebGL/WebGPU. Actionlint
1.7.12, exact LLVM setup comparison with the main workflow, extracted Bash
syntax checks, the production docs build and `git diff --check` pass. The build
retains its existing allowed unstaged-example placeholders. The owner requested a new Docs
workflow after merge to validate Linux setup, all staged examples and deployment.

Hosted confirmation: requested [Docs run 38557265795](https://github.com/Tide-Engine/tide-engine/actions/runs/38557265795)
on 7333e64c passed and deployed successfully. Both web and native-host CMake
configuration select Clang 23.1.2. The full grammar/access/release/analytics,
SEO/HTTP and examples/gallery checks pass, including the playground's complete
WebGL smoke and all five compiler/staging tests. Compiler tools were ready in
3.3 seconds; compilation, preserved state/new defaults, syntax recovery, reset
and phone layout all pass. Fly deployment completed successfully. This confirms
the Linux toolchain correction; no failed-job retry or extra workflow was needed.

## Playground startup test guard (2026-10-14)

Run 38552733008 built the site and passed its SEO/HTTP and preceding game
checks, but the first playground edit hit the browser smoke's generic 20-second
guard. The log omitted the page's status and handled compiler diagnostics, so
it does not establish whether startup was slow or a compile/load error occurred.

The playground smoke now gives the initial compiler-tool download,
decompression and Wasm-module compilation a separate 120-second readiness
guard. Once the worker reports checking/compiling/linking, the existing
20-second build/apply guard starts. Later builds keep their original bounds.
Unexpected compiler/preview failures end successful-build waits immediately
with the page's status and diagnostics; timeout reports include those too.
Repair waits ignore the old error still visible during the input debounce.
Expected syntax errors still exercise diagnostics and last-good rendering.
Startup elapsed time is logged. No assertions, game/runtime behavior, workflow
gates or automatic retries changed. The guide explains first-build startup.

Validation: this checkout's release playground is staged with Clang 23.1.2;
Node 24.19.0 passes all four real compiler integration tests. The focused
production browser smoke passes on WebGL and WebGPU, including state migration,
syntax errors/recovery, focus, reset and phone layout. An isolated server probe
delaying compiler payloads by 21 seconds reproduces the old 20-second timeout
with status Loading compiler; the revised smoke passes with tools ready after
22.7 seconds and the unchanged build guard. An injected clang.bin HTTP 404
fails immediately with its diagnostic instead of timing out. Probe files are
removed. Svelte check has zero errors/warnings; the production build and
`git diff --check` pass, with existing allowed unstaged-example placeholders.
The whole examples/gallery smoke and other host platforms were not rerun;
the original hosted failure's cause still requires its next diagnostic log.
No CI was dispatched or site deployed.

## Browser playground (2026-10-14)

The owner requested a docs page for editing a running game and hot reloading
entirely in the browser. `/guide/playground` is in Get started and linked from
the introduction. Its Markdown explains the experiment and current limits;
`Playground.svelte` supplies the editor/preview at its explicit marker. Source
comes directly from `docs/playground/demo.tide`, the same file compiled into
the initial preview. The page starts that small build immediately and downloads
the compiler only on the first edit/Compile request. Auto reload waits 700 ms
after typing. Ctrl/Cmd+Enter compiles, Restart runs the last successful build
fresh, and Reset code restores the original source and starts it fresh. Reset
also returns the editor to the top. Builds do not take keyboard focus away
from the editor. Stale compilation results are discarded and subsequent edits
are queued; errors leave the last successful game running. Leaving the page
terminates the compiler worker and disposes its frame through the keyed page.

The compiler is the checked-in Tide transpiler compiled to WASI, with its normal
parser/checker/codegen, shaders, layout metadata and importers. The parser uses
WASI's libsetjmp with native Wasm exception handling. Browser-only host glue
provides a failing popen/pclose rather than starting curl. The worker's pinned
WASI filesystem shim hosts it and Clang/LLD 22.1.8 (CppStudio's wasm-clang-runtime
v0.1.0, SHA-256 checked). Generated game C and the standard reload host compile
with strict float contraction disabled and ordinary Wasm SIMD/atomics, then
link the checkout's release engine/platform archives and pinned wasi-sdk 24 C
runtime. This introduces no interpreted Tide subset, game/runtime ABI change,
compiler language change, remote build endpoint or build-service dependency.

`cmake --preset web-release -DTIDE_DOC_PLAYGROUND=ON`, then
`cmake --build --preset web-release --target docs_playground` builds/stages the
bundle in ignored `docs/static/playground/generated/`. Run `npm ci --prefix docs`
first for the shim license. TIDE_DOC_PLAYGROUND defaults to TIDE_DOC_EXAMPLES,
and docs_examples depends on docs_playground on web, so the existing requested
Docs workflow stages it and runs its checks without a separate deployment job.
On native, the option builds only tide_playground_demo for ordinary hidden
captures. The script caches hash-checked third-party tools in build/playground-tools,
packages the Tide/WASI and matching Clang headers and libraries, and ships the
toolchain/importer/shim notices. Generated payloads use explicit gzip inside
`.bin`, avoiding static hosts' automatic Content-Encoding treatment of `.gz`.
The worker verifies the exact compressed bytes against its manifest before
decompression. The compressed payload total is about 25.2 MiB (13.04 compiler,
7.34 linker, 1.04 transpiler, 3.80 headers/libraries); the initial game is separate.
Local warmed small-game compilation takes roughly 1–2 seconds, excluding this
first download and module compilation; this is not a cross-device timing promise.

Scope: one editable Tide file and a solo match; no asset upload, package resolver,
native-only integrations or full IDE/LSP. The actual compiler supports the normal
language and procedural drawing/shaders. Asset filenames must exist in the
virtual filesystem. The preview has noindex and uses the site's existing
cross-origin isolation, with WebGL/WebGPU selection passed from backend=.
Multiplayer web reload still closes connections, as before. Product-wide
hosted-match tick budgets remain unimplemented, not a playground guarantee.

Validation: Node 24.16.0; full native debug 2,286/2,286 and web debug
2,249/2,249, including GPU backends and the existing reload checks. The browser
pipeline's four Node integration tests compile a real game, inspect its reload
exports, recover from a source syntax error, change a component layout and
report missing assets. `test:examples` now runs those after its browser smoke.
Its focused playground fixture on WebGL and WebGPU compiles source locally,
restarts a counter at 37, retains 37 through adding a field defaulted to 7,
checks editor focus, verifies source diagnostics while rendering continues,
repairs the source and resets it. It also checks the phone layout. The
collaborative browser independently retained 56 bounces and initialized the
new field to 7, with syntax errors leaving that build active. Native hidden
390×350 capture verifies the same source. Svelte check is warning/error-free
and the production build passes with existing allowed missing-example pages.
All three production SEO checks pass, including the new page's metadata and
social image. Final desktop/390-pixel phone test captures were visually checked;
the phone capture waits for the existing sidebar's closing transition.
Real phones, Safari/Firefox, other OS builds and Android are not exercised;
no CI dispatch or site deployment is part of this task.

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

NIGHT ROUTE removal (2026-10-13): the owner scrapped the delivery prototype
after playtesting. Its source, tests, build targets, game note, catalog card,
public id, asset-gate entries and thumbnail are removed, including the local
staged page. Production HTTP checks require its former page and thumbnail
to return 404. The five remaining prototypes keep their existing serving
contract. No site deployment or CI dispatch is requested.
Validation: the production build and all nineteen analytics/admin/production
HTTP checks pass with Node 24.16.0; the former NIGHT ROUTE page and thumbnail
return 404, and no catalog card or staged copy remains.

SKYWORKS removal (2026-10-13): the owner scrapped the prototype. Its catalog card,
public id, asset-gate entries, thumbnail and test registrations are removed;
its source/build targets and game note are removed too. The existing catalog
remains unchanged. No deployment or CI dispatch is requested.
The production build and eighteen analytics/admin tests pass; the production
HTTP test checks the removed SKYWORKS page and thumbnail return 404.

## Input prediction controls (2026-10-13)

Input documents owner-approved Predict()/[DontPredict] with action-reset and
computed-fade examples. The spec separates their agreed availability from
first-build ordering, capabilities, attribute placement/conflicts and client-only
semantics. Functions, Tasks and Editors include the new input body. Docs explain
normal bool .down history, guessed-edge suppression, immediate real actions and
the unchanged authoritative missing-input rule and 0.15-second guess cutoff.
No site deployment is part of this task.

Validation: full native/web suites, grammar and production docs build pass;
counts and remaining platform coverage are in notes/language.md.

FOLDLINE catalog addition (2026-10-12): the new first-person 3D cooperative
portal prototype adds `foldline` to the server-only catalog, public root and
outside-root asset gates. Its page stages at `/prototypes/foldline/` through
`foldline_page`, collected by `private_games`. The authenticated catalog
describes its luminous atrium, linked portal views and resonance-cube puzzles.
Admin and production HTTP checks include its public HTML/assets, encoded
paths, stale outside-root copies and omission from public discovery. Game
contracts and completed validation are recorded in `notes/foldline.md`.
The analytics/admin unit suite passes 18/18 with Node 24.21.0; Svelte check
reports zero errors or warnings after restoring the declared dependencies.
The production docs build passes with the existing allowed 404s for unstaged
example and game HTML. Rebuilding with the final FOLDLINE page and gameplay
thumbnail passes; the production HTTP prototype check passes for all five
catalog ids. Staged and packaged FOLDLINE HTML and thumbnail bytes agree.

Inside Out 3D revision (2026-10-12): the existing catalog now describes
miniature 3D rooms with low walls and soft shadows. The fixed orthographic
view, its layouts (nine since 2026-10-12), online two-seat rules, recursive passages
and audio are preserved. Game-only geometry, shadow mapping and SSAO use
existing rendering APIs. Public routes and catalog authentication are unchanged.
Current validation and deployment evidence are recorded in `notes/inside-out.md`.

Source `d2da43a1` deployed to the existing Fly site as release 264, image
`deployment-01M4PHAKNDKRHZZ2MGVTEA16JV`, with passing machine health checks.
All four public prototype pages and thumbnails match the production package.
Inside Out uses the final 3D renderer and refreshed gameplay thumbnail.
Local two-browser WebGL/WebGPU play, site build and HTTP checks pass; no CI
was dispatched. The downloaded public 3D page also passes the full two-player
WebGPU fixture through the production relay in 99.6 seconds; the collaborative
preview reloads its title screen without console errors.

Inside Out nested-box revision (2026-10-11; 2D presentation superseded): the existing catalog now describes
starting inside nested rooms, varied shapes and offset doors, spatial planning
and movable self-containing boxes. Its gameplay treatment is a painted tabletop model
with two small robots (`notes/inside-out.md`). Public serving and catalog
authentication remain unchanged; Underfoot, Inside Out, CRACKED and Two Forks
are retained. Game-specific contracts and evidence are in `notes/inside-out.md`.

Integrated through `origin/dev` `c70a0c22`: all 64 playable release pages were
rebuilt/restaged and agree with the production package, as do all four
prototype thumbnails. Check has zero errors/warnings; 227 grammar, eighteen
analytics/admin and five production HTTP/SEO/access tests pass. Production
build and full WebGL example/gallery smoke pass. Inside Out native/WASI and
both two-browser GPU backends pass; exact asset hashes are in its notes.
No CI was dispatched.

Source `8c5bdf6d` deployed directly to the existing Fly site as release 263,
image `deployment-01M4PF2HHN71TH2ZJ2C4S8M4XF`, with passing health checks.
All four public prototype pages/thumbnails match the package. Inside Out's
downloaded page passes two-player WebGPU play through the production relay
in 47.7 seconds. The collaborative preview displays its updated title screen.

Inside Out catalog addition (2026-10-11): the owner's two-player online puzzle
prototype adds an eighth server-loaded card, `inside-out`, with a warm miniature
workshop description. `inside_out_page` stages at
`static/prototypes/inside-out/`, collected by `private_games` when
`TIDE_PRIVATE_PROTOTYPES` is enabled. Its public URL is
`/prototypes/inside-out/`; the catalog retains admin authentication.
The public-root/retired-alias patterns, build asset gate and existing admin/HTTP
tests include the id, with public game/assets and omission from Samples, search
and sitemap. Encoded public HTML checks now cover all eight games. Game rules,
content and gameplay validation belong in `notes/inside-out.md`.
Validation: all 18 analytics/admin unit checks and five production
SEO/access-form/admin HTTP checks pass with Node 24.21. Svelte check reports
no errors or warnings; the production site build and complete WebGL
example/gallery rendering smoke pass with all games staged, including the
initial isometric Inside Out page and gameplay thumbnail. The browser
gameplay tests are recorded in `notes/inside-out.md`; deployment is a separate
step using the existing Fly site.
After merging `origin/dev`'s confirmed-frame/compiler and user-documentation
changes, every release page was rebuilt and restaged. Site check/build, the
five production HTTP/SEO tests and complete WebGL example/gallery smoke pass
again with the merged docs and current prototype.

Delayed initialization and switch expressions (2026-10-11): Basics explains
explicitly typed delayed locals, read-before-assignment and once-only
initialization, when mut is needed, switch value syntax, final fallback,
compatible/contextual arm types, scopes and precedence. The spec separates
the owner's approved design from implemented provisional details and the
unbuilt patterns/guards. All 219 grammar checks and the production docs build
pass, using the existing allowed placeholders for unstaged game/example
pages. Compiler/editor/GPU validation stays in their subsystem notes.

Enum count and cast guidance (2026-10-11): Basics documents `EnumType.Count()`
as the compile-time number of declarations, including aliases and a member named
`Count`, and shows explicit conversions plus advancing/wrapping with `%` for
valid, consecutive zero-based enum values. It explains why sparse/negative/aliased
values need a different traversal. Editors documents count completion/hover and
the replacement spelling in parenthesized-cast diagnostics. The spec marks the
owner's count/conversion decisions as decided; `Values()` remains an open,
unimplemented proposal. `npm run build` passes with the existing allowed
missing-example 404s. No site deployment was requested.

Contextual enums (2026-10-11): Language basics and the spec document the
owner-approved `.Idle` spelling, expected-type contexts and qualified spelling
for missing context or ambiguous overloads. Editors documents expected enum
preselection and leading-dot member completion. The shared grammar recognizes
shorthand without changing leading-point floats or ordinary member scopes;
all 217 grammar tests and the production docs build pass after merging dev.
No deployment.

Automatic texture compression (2026-10-11): Assets, Meshes and textures, and the spec explain automatic eligible color BC1/BC3 assets, lossiness, exact Raw/RGBA fallback, prepared mips and bounded whole-image GPU residency. They distinguish GPU savings from the current pack-size increase and unbuilt mip streaming. Implementation evidence and final validation are in `notes/textures.md`.

Sample playback hints (2026-10-11): Editors clarifies that inlay parameter
labels describe written literals; omitted defaults and named arguments have no
label. The production docs build passes with the existing allowed missing-example
placeholders. Checker and editor regression details are in `notes/editors.md`.

Editor review (2026-10-11): the Editors page clarifies Restricted Mode's user
setting and PATH exclusions, including symlinks/junctions. The docs production
build and all 215 shared grammar checks pass; implementation, regressions and
remaining editor UI coverage are recorded in `notes/editors.md`.

Editor host follow-up (2026-10-11): Editors now explains that replacement runs
wait for process cleanup, terminal close stops a run, and unconfirmed cleanup
reports a corrective error. The production docs build passes with the existing
allowed missing-example placeholders. Actual editor-host evidence and remaining
cross-platform coverage are in `notes/editors.md`.

Skinned draw batching (2026-10-10): Animation and the spec clarify that each draw captures its current pose and consecutive compatible imported-model draws can batch even with different poses. Later pose edits and samples preserve prior captures. The proposed explicit independent-pose instance-list API remains unbuilt; no syntax changed. Renderer and shader implementation details stay in their subsystem notes. The production docs build passed with the existing allowed missing-example placeholders; no deployment was requested.

Web release payloads (2026-10-10): CLI and Assets explain compression inside a self-contained release page, built-in decoding, unchanged decoded asset values and readable/raw debug or watched pages. The implementation and generation/freshness requirements are in `notes/web.md`.

Release size (2026-10-10): Assets and the spec explain conservative omission of unreferenced asset declarations from standalone Tide-only release builds, including retained helper/type-default references, whole glob collections, unchanged validation and C/C++/prebuilt/custom-host/hot-reload exclusions. The CLI page describes native linker removal of unreachable code/data and stripped debug symbols, with runtime references and required exports preserved. The production docs build passed; missing generated game/example pages used the existing allowed placeholders. No deployment was requested.

Media formats (2026-10-10): Assets has a format/representation table covering images, models, audio, text and bytes, with build-time imports separated from runtime Decode. Assets, Audio, Editors and the spec document JPEG/TGA/BMP and MP3/FLAC, variant/budget limits, unsupported alternatives, required glTF extensions and optional uncompressed fallbacks. MP3/FLAC conversion explicitly trades compressed source size for PCM pack/RAM use; Vorbis remains the streaming choice. The audio spec's stale 48kHz conversion and built Clip.length claims are corrected to the implemented source-rate behavior and absent property. No site deployment was requested.

Degree literals (2026-10-10): Basics, Math and the spec document lowercase
decimal `deg` literals as ordinary float radians, converted at compile time
after reading their binary32 degree value. They cover fractions, exponents,
underscores, unary minus and `quaternion.Euler(0, 180deg, 0)`. The shared
TextMate grammar gives the entire literal the float scope, including in docs
code fences. The grammar suite and production docs build passed.

Quaternion Euler overload (2026-10-10): the Math page and spec document both
`quaternion.Euler(float3 radians)` and `quaternion.Euler(x, y, z)`. Each scalar
angle is in radians; the scalar form equals `quaternion.Euler(float3(x, y, z))`
with the same Z-X-Y rotation order. Existing examples keep their current form.
The production docs build passed; no site deployment was requested.

Prototype catalog reset (2026-10-11): the owner asked for every prototype but
Two Forks to be deleted and for CRACKED, a port of their Unity game
(`prototypes/cracked/`), to be registered the same way. The catalog's server
load has two cards, `cracked` first and `two-forks`; the six removed cards
(The Last Light, Cabin Nine, Dead Signal, Deadbell, Afterburn, Aerial) and
their checked-in thumbnails are gone. The public-root and outside-root
patterns in `admin.js` and the build's asset gate in `vite.config.ts` hold
exactly `two-forks` and `cracked`, so a deleted game's address is an unknown
path with no compatibility handling. `cracked` is an ordinary word, so the
sitemap, Samples and search checks look for `prototypes/cracked` and the
title `CRACKED`, not the bare word, and a public path with a segment
`cracked` outside `/prototypes/` is refused. The card's unused `horror` flag
and the candle fallback drawn for a card without a picture were removed with
the last cards that used them; every card has a thumbnail.
`static/prototypes/cracked/thumbnail.png` is checked in beside the staged
page. The admin and production HTTP tests go through both ids, and the
production test also asks for an id outside the catalog and expects 404.
Validation on Windows with Node 24.15 (below the 24.16 that
`notes/testing.md` asks for; no newer Node was installed): `npm run check`
with no errors or warnings, `npm run build` with the existing allowed
missing-example 404s, `test:analytics` 18/18, `test:admin-http` 1/1,
`test:seo` 3/3, `test:access` 8/8 and `test:access-http` 1/1, using fixture
pages as no game was staged. The manual docs workflow needs no change;
nothing was deployed. Catalog and staging: `notes/private-prototypes.md`.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Building

### Docs site

- `docs/` is the docs site, https://tide-engine.dev: SvelteKit (`docs/src/`), with every page prerendered from the Markdown files beside it. It needs Node: `npm ci`, then `npm run dev` in `docs/` serves it, `npm run check` checks its types, and `npm run build` builds it. The build fails on a dead link, a link to an anchor that isn't there, a Svelte warning, or a Markdown file missing from the sidebar (`docs/src/lib/nav.ts`, which lists every page in reading order).
- The site calls the product Tide Engine: its logo, the home page's name and pages' titles (`| Tide Engine`). The language and the command stay Tide, there as everywhere else.
- Every Markdown page has a `description` in its front matter for search results and sharing; `docs/src/lib/components/Seo.svelte` supplies canonical URLs, Open Graph and large Twitter cards, and WebSite/WebPage structured data with docs breadcrumbs. Each public page has its own 1200 × 630 PNG at `/social/<page>.png` (`home.png` for `/`), built ahead of time by `docs/src/lib/server/social-image.ts`, with the Tide logo, amber waves and the page's title. `/sitemap.xml` lists the home page, every sidebar page and `/request-access`; `/robots.txt` points to it and leaves pages and preview images crawlable. Error pages say `noindex`. `npm run test:seo` checks the built HTML, images, sitemap and production HTTP responses, and runs in the docs workflow after the build.
  - **SEO follow-up, 2026-10-08:** the home's `title` front matter supplies its search/sharing title through `Home.title`, independently of the hero. It names deterministic multiplayer explicitly; its description names rollback, the ECS language and native/WebAssembly builds. The existing home `WebSite` node names `Tide Engine`, with `Tide` as `alternateName`. Google selects the displayed site name itself; this is a preference, not a guaranteed display change. The live homepage already had the primary site-name markup when the owner's screenshot still showed an older title and HTTP/www address.
  - `server.js` permanently redirects www and the Fly hostname (`tide-website.fly.dev`), plus HTTP requests for the public apex, to the same HTTPS origin, preserving paths and queries. Case and explicit ports on these hostnames are recognized; local previews stay local. It sends `X-Robots-Tag: noindex` on raw playable HTML (`examples/<id>`, `samples/<id>`, Sand, Duel, Towers and Sumo, with their directory and `index.html` forms). Search results should land on the gallery or explanatory docs rather than a canvas with a generic title. These pages remain crawlable so the header can be read; assets and thumbnails are unaffected. SEO tests require the home `WebSite` node and check redirects and the raw-page headers using tiny deployment fixtures, without compiled games.
  - Validation passed on Windows with Node 24.19: `npm run check` (no errors or warnings), `npm run build`, all three SEO tests, and the production access-form and admin HTTP tests. Host probes use `node:http` to send the desired Host explicitly; static fixtures are registered in the temporary deployment's adapter-node asset table, since it serves the closed set recorded at build time.
- The site doesn't present Tide as C#-like (the owner, 2026-10-06: "we are our own language"): it's about how people find Tide, who shouldn't expect a C# clone and then see a different language. So nothing that says what Tide is calls it C#-like or measures it against C#: the home page, the introduction, a page's opening, the README, an editor extension's description. Saying what inspired it is fine, and so is a rule that names C# where it follows it, as `docs/spec.md`'s do. The pages for users state such rules themselves (operators' precedence, what a shift does), which reads better to someone who doesn't know C#.
- The header links Docs (`docs` in `docs/src/lib/nav.ts`) to the guide and Console to https://console.tide-engine.dev/. Both appear in the desktop and mobile menus; documentation categories belong in the sidebar.
- A page is in the folder of its subject, and the sidebar's groups follow the folders (`spec.md`, beside them, is its Reference):
  - `guide/` (Get started): what a newcomer reads first, and the demos.
  - `game/`: what a game is made of, in two groups: Simulation, the match (components, systems and their schedule, events, scenes, tasks, input), and Presentation, what one machine shows of it (views and the six other pages about drawing, below, then frags and verts, audio, local state, files, the GUI).
    - **Drawing is seven pages, cut from one** (the owner, 2026-10-08: `views.md` was 110 KB, the agents of the 2026-10-06 trial had said it was too long for a 2D game, and the session that wrote Tag grepped it for signatures where it should have read it; "split it into pages by subject, each short enough to read whole, with the first one enough for a 2D game". The owner named what the first page has and the others' subjects, and left the cut: which page each section is on, the pages' names and their order in the sidebar are Claude's picks). A section about drawing goes on the page of its subject, and a subject none of them has gets a page, so that `views.md` doesn't grow back:
      - `views.md`, Views and drawing (30 KB): what a view is, the draw functions' table, colors, smooth shapes, the screen, text, what a view sees between ticks, `Frame.dt`, devices. It's what a 2D game draws with, and it lists the other six before its first section.
      - `views-and-the-match.md` (11 KB): queries in a view, `with ownership`, `verified`.
      - `draw-settings.md` (21 KB): the two forms, blending, alpha, tints, stencil, transforms, clipping, and transparency, which is a recipe of them.
      - `meshes-and-textures.md` (14 KB): the 2D mesh, and everything about textures.
      - `3d.md`, Drawing in 3D (12 KB): 3D meshes, the primitives, big scenes, and depth and which side draws, which are settings for 3D meshes alone and so are here, where the owner's outline had every setting on one page: a 2D game reads 6 KB less, and a 3D one finds on its page the two things that go wrong first.
      - `cameras.md`, Cameras and coordinates (10 KB): screen and world coordinates, the cursor's lock, projections, a camera as matrices.
      - `render-targets.md`, Render targets and post-processing (16 KB): making one, `Draw.Blit`, formats, samples, several colors, depth.
    - Every section's text moved as it was, with its live example. What changed beside where things are: each new page has a title and a description, and a sentence or two to open it where its first section didn't; the headings Draw settings, 3D and Targets and post-processing became their pages' titles, and the sections under them went up a level; and what had ended up under a heading it isn't about, as sections were added above it, is back where it belongs: the Blit example and its text, which were under A target's depth, have a heading of their own (Post-processing, the one new heading), a target of depth alone is under What a target holds again, and the two paragraphs on what a view changes and on its `World` are in `views.md`'s opening and under Other entities.
    - **Old addresses.** `/game/views` is still a page, so no address is dead, and the anchors of what stayed still land. An anchor of a section that left, `/game/views#stencil`, goes to the page that has it now. A browser never sends an anchor to the server, so it's the page that does it (`movedSections` in the docs' `+page.svelte`: when the page loads and at `hashchange`, `goto` with `replaceState`), from a table the build makes: `split` in `docs/src/lib/server/content.ts` names the pages that took a page's sections, and `gone` the anchors whose heading became a page's title (`#_3d`, `#targets-and-post-processing`), and `moved()` takes every heading's anchor on those pages that the old page hasn't got (`ids`, which `render` returns). So nothing is listed by hand, a section added to one of the six later is covered too, and the build fails when `split` names something that isn't a page, or a `gone` anchor that's a heading of the old page again. A reader without JavaScript lands at the top of Views and drawing, which lists the pages. It was checked by hand, on the built site in a browser (`#stencil`, `#_3d`, `#targets-and-post-processing`, `#which-side-draws` typed on the open page, and `#smooth-shapes`, which stays); no test drives it. A page that's split later gets a line in `split`.
  - `packages/` (Packages, after Presentation): an overview of optional bundled packages and how to find external package docs; using and writing packages; and one API page each for `box2d`, `box3d`, `rooms`, `lobby` and `steam`. Package names lead the sidebar entries. Physics and service integrations live here; engine session and networking features remain under Multiplayer. A change to a package's components, events or rules updates its page.
  - `multiplayer/`: matches, and how machines stay in step (networking, determinism).
  - `language/`: the language those are written in (types, functions, errors, math, collections, namespaces).
  - `tools/`: `tide`, the editors and debug views.
  - `c/`: calling C, and C that runs a game.
  - A page about how something works is with the pages about using it (the schedule with systems, networking with matches), not in a section of its own.
- Pages are Markdown as GitHub shows it, plus what VitePress had and the docs use (`docs/src/lib/server/markdown.ts`): links between pages by file name (`./systems.md#order`), `::: tip`, `info`, `warning`, `danger` and `details`, `::: code-group` over fences labeled `[name]`, and, in `content.ts` beside it, `<<< @/../demo/demo.tide` for a file of the repo and the home page's hero and features in `docs/index.md`'s front matter. Anchors are made as VitePress made them, so old links still land. Search looks through an index the build makes (`/search.json`). The Windows install line is written `Invoke-RestMethod ... | Invoke-Expression`, never `irm ... | iex` (the owner, 2026-10-07): uBlock Origin blocks a page's copy button that puts the short form on the clipboard and warns of a "ClickFix" attack (its `prevent-clipboard-write` rule in `filters.txt`, since September 2026: text that starts `irm` and has `iex`; `github.com` is among the sites it exempts, so the README never showed it). The long form matches nothing of the rule as it was that day, which can change: the lasting way is an exemption, asked of uBlock's maintainers (`uBlockOrigin/uAssets`), as other tools' sites have.
- It runs on Fly.io as `tide-website`, one machine in `fra` (`docs/fly.toml`): `docs/server.js` serves the build with SvelteKit's handler, and sends www.tide-engine.dev to the name without it. In the domain's DNS on Cloudflare, not proxied, the name has A and AAAA records to the app's addresses and www a CNAME to `tide-website.fly.dev`; certificates come from Fly (`fly certs`). The image takes what the build made (`docs/Dockerfile`), as the pages need the rest of the repo.
- **One private analytics dashboard** (the owner, 2026-10-08: "some admin dashboard ... on the website that I can just see things at a glance", then "Build it now with a small built-in analytics system"): `/admin` combines public pageviews, package download requests and relay snapshots, with Today, 7, 30 and 90 UTC-day ranges. It belongs to the existing site, with no analytics vendor, separate account, tracking cookie or browser fingerprint.
  - `docs/admin.js` guards the page, its SvelteKit data routes and `/admin/api` before the built handler; it decodes paths before checking them, so encoded admin names are guarded too. `TIDE_ADMIN_PASSWORD`, a Fly secret, is required: without it all admin routes fail closed. Sign-in is an ordinary same-origin form at `/admin/login`, with bounded attempts; the session is a signed 12-hour HttpOnly, SameSite=Strict cookie, Secure on HTTPS. Changing the password invalidates sessions. Logout clears the cookie. Responses are private/no-store and noindex; admin is absent from the sitemap and disallowed in robots. The Vite preview uses the same guard. Keep the secret out of client settings and build output.
  - The shared layout sends one same-origin `/analytics/pageview` POST after initial load and each client navigation, for home and known docs routes only. Development runs, embedded frames, admin, probes and prefetched pages don't count; common bot user agents are ignored. Paths are allowlisted from the built sitemap in production and the navigation in development. An external referring hostname is sent only on the first page; paths and query strings of referrers never leave the browser. Counts are pageviews, not unique visitors or completed reading; script blockers can omit them.
  - `docs/analytics.js` serializes atomic replacements of `TIDE_ANALYTICS_FILE` (`/data/analytics.json` on the existing Fly volume), fsyncs the replacement, retains 400 UTC days and bounds referrer and version dimensions. There are no saved IP addresses, user agents or visitor identities. A failed write keeps the previous committed state; an unreadable file is reported rather than replaced with empty data. Shutdown waits for sampling and queued download writes. One website process owns this file; several processes would need a different store. Both backend modules are required by `server.js`: keep `Dockerfile`'s COPY list and `.dockerignore`'s file allowlist together when adding runtime files.
  - `docs/releases.js` records only successful GET redirects for recognized package archives, including installers and upgrades. Metadata, HEAD, checksums and failed requests don't count. Each request has its release version, platform and channel; retries can count again, and a redirect doesn't prove completion or installation. Recording runs independently of the download response, so analytics cannot delay or break a download. History starts when this version is deployed; it does not import older GitHub totals.
  - The website samples the relay every minute through Fly private networking at `TIDE_RELAY_STATS_URL` (`http://purrengine-relay.internal:8080/admin/stats`), with a shared Fly secret `TIDE_RELAY_STATS_TOKEN` in both apps. Both apps must belong to the same Fly organization. Requests have a four-second timeout and a 16 KiB response cap. Private sampling doesn't start an idle relay. Missing reports are unavailable/stale, never invented zero counts; the previous report and its time remain visible. Daily averages and peaks are of the samples, so short-lived activity between samples can be missed. Relay connections are signaling sockets, not players in matches (see `webrtc-and-rooms.md`); no player reporting protocol is added.
  - Tests: `npm run test:analytics` covers persistent counts, retention, bounds, failed writes, draining writes, auth, sessions, CSRF, encoded paths and relay failures; `npm run test:admin-http` checks the actual production handler and collector. Both run in the existing manually requested Docs workflow, alongside its release, SEO and access tests. The UI was checked with headless Chrome at desktop and phone widths, including range changes, logout, empty states and relay status. The dashboard refreshes each minute and exposes exact daily totals beneath the charts.
- **Shareable prototypes, 2026-10-09:** the owner first requested a private admin catalog, then asked for public game URLs so other players need no admin access. `/admin/prototypes` stays beside Analytics and lists Underfoot, Inside Out, CRACKED and Two Forks (2026-10-11; the six deleted games are recorded in the entry at the top of this file). Catalog data stays in its server load, absent from public Samples, search and sitemap. Cards open public games in a new tab; a build without that game's HTML says In development. The catalog explains that Play links can be shared with anyone and that hosting/joining uses the normal room flow. The catalog and staging contract is in `notes/private-prototypes.md`.
  - Every game's HTML and files stage beneath `docs/static/prototypes/<id>/`, served publicly as `/prototypes/<id>/`, including thumbnails and future external wasm, packs, sounds and textures. Self-contained web pages stage as `index.html`. `private_games` retains its target name and stages the games; the Docs workflow builds it alongside the public examples. Games under `prototypes/` are opt-in through `TIDE_PRIVATE_PROTOTYPES=ON`, which the manually requested Docs workflow enables; ordinary builds keep them off. Generated HTML stays ignored; authored assets can be checked in under the public prototype directory.
  - `admin.js` lets the public game roots (`two-forks`, `cracked`, and since 2026-10-11 `inside-out` and `underfoot`) through before checking admin configuration or sessions. Bare game, directory and `index.html` URLs have `X-Robots-Tag: noindex`, including encoded spellings; assets use ordinary static caching, HEAD, ranges and conditional requests. Old game URLs are unsupported: the owner explicitly declined compatibility redirects. The `/admin/prototypes` catalog and data routes, analytics and all other admin routes keep their existing password/session guard and private/no-store/noindex headers. Without `TIDE_ADMIN_PASSWORD`, admin fails closed while games remain available. Development uses the same routing.
  - A catalog id anywhere outside `/prototypes/` (`/samples/<id>/`, a thumbnail, a raw executable name) still returns 404 even if a stale deployment contains those bytes. The site build rejects files named for a catalog game outside `static/prototypes/` or the legacy admin root, so new builds have one public game address. Both patterns hold the current catalog ids only. The games are public to anyone with the URL; omission from the gallery, search and sitemap and the noindex header are discovery choices, not access control.
  - Validation of the original sharing change (2026-10-09, Node 24.19, the five games of that day): `npm run check`, the site build and the analytics/admin, production prototype HTTP, SEO and access-form HTTP checks passed, with full native/web suites (2,016/2,016 and 1,993/1,993). They covered anonymous HTML/assets, missing admin configuration, encoded paths, HEAD/ranges/conditional requests and retained admin/catalog protection. Removing the unwanted compatibility redirects also removed their dedicated test. The same tests now go through all four current ids.
- Every response has `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (`docs/headers.js`, in `npm run dev` too), so pages are cross-origin isolated and the demos in them run on threads. A page in a frame is only isolated when the page around it is, and moving between pages loads no new one, so every page has them: everything a page loads comes from the site itself (fonts included), or has to say it may be embedded (CORP or CORS).
- `.github/workflows/docs.yml` is part of every run of the Build workflow, whose `Docs` job calls it (`workflow_call`; the owner, 2026-10-08, who kept having to start both: "can the docs be part of this?"), so `gh workflow run build.yml --ref dev` is the release and the site. It still runs alone when asked to, for a change to the docs only: `gh workflow run docs.yml --ref dev`. Either way a run on `dev` builds and deploys it (the `FLY_API_TOKEN` secret, a deploy token for the app, which the Build workflow hands it), and a run on another ref builds without deploying. Pushes and pull requests start nothing.
  - In a Build run the site is beside the release, not part of it: neither waits for the other, so the site deploys whatever the packages and suites do, and a nightly is published whether or not the site built. A failure of either fails the run.
  - One docs job at a time for a ref, whichever workflow asked (`docs-<ref>`, with `cancel-in-progress: false`): a request made while the site deploys waits for it. It cancelled the one before it while pushes started it; that would now stop a job of a release's run.
- The workflow builds sand, Duel, Towers and Sumo for the web (`web-release`, targets `sand`, `duel`, `towers` and `sumo`) and puts them at `sand/`, `duel/`, `towers/` and `sumo/`, linked by the Samples gallery. The dedicated Duel, Towers and Sumo docs explain their implementation. Sand also serves as a benchmark (see Benchmarks); Duel is a standalone game in `examples/duel/`, and Towers and Sumo are the physics games in `examples/towers/` and `examples/sumo/` (see The physics games). To see them locally, copy each `build/web-release/bin/<game>.html` to `docs/static/<game>/index.html`; a build without them still works: the prerender lets a link to one of those four pages pass (`docs/vite.config.ts`), as nothing serves `static/` while it runs.
- A whole game is one frame on its page (the owner, 2026-10-06, of Siege, which Towers has since taken the place of, and of Alley, the game Sumo took the place of, which were two frames in one match: "make it only one game instance with a lobby setup"): an iframe in the page's Markdown, `<iframe class="demo-frame" src="/<name>/?embedded" ...>`, as Duel's is, and the game's own lobby is how a reader gets other machines in. `roomy` beside `demo-frame` (`docs/src/app.css`) gives a game that opens on a lobby the height it needs at narrow widths, as `duel` gives Duel. Nothing shows two frames of a whole game any more: `::: tide-game` is gone, with its CSS and its part of the test. A frame that was in the page as it came, and clicked before the page's script ran, shows its focus all the same (`showsFocus` in `docs/src/lib/examples.ts` looks at `document.activeElement`).
- Pages have live examples beside their code, wherever a small game says it better (`docs/guide/examples.md` lists them all, and a page that explains something a game can show gets one). Each is a standalone game in `docs/examples/<id>.tide`, included as code with `<<< @/examples/<id>.tide` inside `::: tide-example <id> <title>`. Code and the running game come from the same source, and a page shows the source of every example it runs.
  - An example starts by itself once it's in view, and stops once it's more than a screen away, so a page runs what its reader is looking at and nothing else (agreed: the owner asked why they'd have to start each one). In view is half of where it draws on screen, and still there a moment later, so scrolling past starts nothing. Stop holds until the reader presses Run, Restart starts it over, and leaving the page disposes its frames. A reader who asked their system for less motion or less data starts each one themselves. `backend=` in a docs page's address goes on to its games.
  - One comes up in one step, with nothing to watch load (the owner, 2026-10-05: the placeholder, then an empty frame, then the game was slow and distracting; about 800 ms on the live site, of which 450 an empty frame). It loads within a screen of the view, its program waiting (`wait`, see Web builds), so that once it has been in view a moment, starting it is all that's left: about 30 ms. Its frame is there unseen until its game has drawn (`Tide.start()`'s promise, then the `shown` class), over the background the examples draw on, and what it says while it loads only shows after most of a second (`.example-status.loading` in `docs/src/app.css`). A match loads its first player so, and the second once the first has its room's code. What loads in a docs build without compiled examples isn't a game (it has no `Tide.start`), which is how the page tells: no request asks first.
  - A page downloads all its examples once it has loaded and has nothing else to do (the owner, 2026-10-05: "why dont we pre-download it"), nearest the view first and one at a time, each after the examples near the view have loaded theirs (a frame's download takes no `fetchpriority`: Chrome gives every one its highest), each in a frame nobody sees with `#download` after its address (see Web builds), which is removed once it loaded (`download` in `docs/src/lib/examples.ts`). So an example the reader jumps to, or scrolls to faster than a screen ahead, has no download left: its frame asks whether its page changed (a few hundred bytes) and has it from the cache. The site sends no freshness for these pages, only an ETag, so nothing is ever stale after a deploy. Not for a reader who asked for less data or motion, nor a match's second player, whose address has the room's code; and the example in view as a page opens has no head start.
  - The page scrolls over them (`embedded`, see Web builds): the wheel always, and a finger until it taps the game, from when a swipe on the game is the game's until a tap elsewhere. A frame whose game has the focus, and so the keyboard and those swipes, shows it with an outline (`focused`, which `showsFocus` in `docs/src/lib/examples.ts` sets from the frame's own focus and blur: CSS's `:focus` isn't an iframe's whose page has the focus), and a press on the page elsewhere takes the focus back from the frame, as not every browser does that for a press on text. Sand's and Duel's frames, in their pages' Markdown, say `?embedded` in their address and show the focus the same way.
  - They're drawn for a frame about 375 pixels wide and 400 tall (360 with two players), and for a pointer first: a mouse or a finger. The keyboard is the frame's only after a click on it, which the page's text says.
  - They wear the site's colors (agreed: the owner asked for them to match), which are the dark theme's in `docs/src/app.css`, written out in each example as it has no file to share: the code background `Color(0.086, 0.086, 0.094)` behind everything, `Color(0.24, 0.25, 0.27)` for lines and `Color(0.64, 0.64, 0.68)` for quiet text, and for what stands out, the brand's amber `Color(0.98, 0.75, 0.14)` first, then the callouts' teal `Color(0.18, 0.83, 0.75)` and rose `Color(0.98, 0.44, 0.52)`. The GUI's own look is already that gray and amber, so an example with widgets only rounds them, `GUI.SetTheme(GUITheme { radius = 6 });`, but for the three whose lesson is the GUI's look. The first tutorial's two keep the tutorial's own colors.
  - Two players are two frames of one example, each a machine of its own (`docs/src/lib/examples.ts`, `docs/src/lib/server/markdown.ts`). `::: tide-players <id> <title>` starts both as they are, for the reader to bring together, like `rooms`, whose lobby is the lesson. `::: tide-match <id> [lag=<ms>] <title>` puts them in one match itself: the first frame's address says `?host`, the page waits for its room's code (the frame's `Tide.room`), and the second's says `?join=<code>` (see Web builds), so those games have a match `Main` and no lobby. A match has a slider, Ping, which sets both frames' `Tide.lag` (`lag=<ms>` is where it starts): the round trip the player who joined is to have, which that frame's game shows as its ping; the host's page adds nothing (see Web builds). Beside the slider the page writes the least it can be, "min 1 ms" (the frames' `Tide.minLag`, looked at twice a second while the match runs, and written again once two looks in a row say the same other number, as it can move by a millisecond), in the brand's color while the slider is under it. That's measured with nothing added, so a match starts with no lag, and the slider's ping starts once the least is known (`floor` in `docs/src/lib/examples.ts`), within a second of the second player joining. A container with nothing inside shows no source, for a page that has it beside another frame of the same example. They meet through the relay we host, as players anywhere do.
  - A position that a two-player example draws is `[Position]` (agreed 2026-10-05: the owner saw the other player's body jump in the first game's match), so a wrong guess about the other player glides in. Untagged, the first game's body moved 50 units in one frame at 75 ms each way, each time the other player started or stopped; tagged, 11 at most. An example one machine plays alone has nothing to correct, and stays plain; the first tutorial adds the tag where it adds the second machine.
  - One example draws a model from a file (2026-10-08): `model`, on the Assets page, whose `art/rocket.glb` `art/make_rocket.mjs` writes, a glTF as a tool exports one (five meshes, a material's color each, three nodes of one fin, no normals), in the site's colors, which the script turns into the light a material's color is. Looked at with no window (`docs_example_model --frames 90 --capture`); the Samples' 3D games are still shapes made in code.
  - The Assets page also documents PSD imports (2026-10-08): a saved RGB 8/16-bit composite as `Texture`, `Grid2<Color>` or a cubemap face, with Photoshop's Maximize Compatibility for saving and no Photoshop installation for builds. The spec and Editors page name both PNG and PSD image assets and completion formats. Runtime `grid.Decode` remains PNG-only. The docs build passed; decoding and asset behavior are checked by compiler tests rather than another live sprite example.
  - Two examples are built with a package that comes with tide: `physics-2d` with Box2D and `physics-3d` with Box3D, on the pages of `docs/packages/`. `tide_add_game` reads no `tide.packages`, so `docs/examples/CMakeLists.txt` names each with its package's folder (`TIDE_DOC_PACKAGE_<id>`) and builds it as tide builds a game that lists the package: the package's `.tide` files as `PACKAGES`, so they compile first and its systems run before the example's, its `bridge.c` with the example's C, and the library's own sources as `tide_box2d` or `tide_box3d`, which `packages/CMakeLists.txt` builds for its tests. So the top `CMakeLists.txt` adds `docs/examples` after `packages`. It's the same natively and on the web, and `docs_examples` builds them with the rest, so the docs workflow builds what it did.
  - The 2D example draws a ball's spoke and each crate's turn. Box2D's `Body.angle` is now tagged `[Rotation]`, so a view blends the shortest way across the ±π wrap. The physics pages describe the current package limits (what's lost, asleep or not sent, each checked with a game), where they apply or under Not there yet: a line goes from its page when its package no longer does it.
- To build these locally: `cmake --preset web-release -DTIDE_DOC_EXAMPLES=ON`, then `cmake --build --preset web-release --target docs_examples`. The target stages their self-contained pages at `docs/static/examples/<id>/index.html`; these generated files are ignored. The docs workflow builds them and runs `npm run test:examples` after the site build (`docs/tests/examples.mjs`): headless Chrome or Edge finds every example in the Markdown and checks that each shows its source and draws once it's scrolled into view (of two players, the first frame; the second joins through a relay in the test's process, which nothing checks), that the frame of each whole game that opens on a lobby (Towers, Sumo: the frames the Markdown marks `demo-frame roomy`) draws on its page, and that each game the Samples gallery links to draws on its own page. That's all, on purpose (the owner, 2026-10-07: "its not that deep, just get the damn docs up"): the test used to drive the pages' scrolling, two players meeting through the relay, the lobbies' buttons and the phone layout in the browser, six minutes of the flakiest kind of test there is, between a docs change and the site; what a page does around a game is the platform tests' (`web_embedded`, `web_lag`, `web_rooms`) and the games' own. It lists a page's examples, and the gallery's cards, once the page's document is parsed (`document.readyState`), never as soon as the first one is there: the views page was over a megabyte of HTML then (the frags' page still is), and listed at first sight it came up short of its last example, which failed a run on CI (2026-10-07; made to happen here with the page's own check in a loop, and on a slow connection nearly every time). `TIDE_BROWSER` selects a browser; `TIDE_EXAMPLE_BACKEND=webgpu` checks WebGPU too. `TIDE_EXAMPLES=<id>,<id>` runs only those (a game's name like an example's), and `TIDE_DOCS_SCREENSHOTS=<folder>` saves a picture of each as it runs in its page, to look at. A game whose page isn't in `docs/static/` fails there, so the workflow builds them first.
  - The quick way to look at one while writing it is the native build, with no browser: `cmake --preset debug -DTIDE_DOC_EXAMPLES=ON`, `cmake --build --preset debug --target docs_example_<id>` (dashes as underscores), then `build/debug/bin/docs_example_<id> --frames 40 --capture x.png --size 376 400`, or a `--script` for its clicks.
- The docs' Markdown fences Tide as `csharp`, which GitHub highlights; the site highlights it with the editors' grammar (`tools/tide-syntax`). The workflow tests the grammar first, through the site's Shiki: `npm run test:grammar` in `docs/` checks the scopes of `tools/tide-syntax/test/probe.tide`'s cases and of the compiler's test programs.

- **Skinned meshes and animation** (2026-10-08): `docs/game/animation.md` is the new presentation page, next to Drawing in 3D in the sidebar, linked from that page and Assets. It explains import/drawing, inferred names and optional contracts, dynamic lookup, explicit CPU sampling/blending, local bone edits, sockets and runtime GLB decoding with the match/local boundary. `docs/spec.md` now separates the agreed design, implemented provisional API details and remaining animation proposals. No automatic controller, root-motion extraction, IK or morph animation is presented as built. The shared grammar recognizes `SkinnedMesh<V, Contract>` without treating every unparameterized `SkinnedMesh` identifier as a builtin. The site build and all 202 grammar tests passed after merging the current `dev`.
  - **Rest poses and separate clips** (2026-10-10): the animation page starts with drawing a model without clips, explains `defaultPose` and an omitted `Draw.SkinnedMesh` pose, and documents `asset Animations` with direct checked clip members. Its separate-file example samples into the character's default pose so edits and sockets use the character's bone references; direct sampled-source drawing and matching-rig binding are explained alongside the retargeting limitation. Sampling binds the selected clip's targets, while a drawn source pose requires all its source asset's animation targets and edited nodes. `Pose.Blend` stays strict, so the page explains sampling both clips into target poses first. Assets and Drawing in 3D link to these workflows; Editors describes catalog completion and metadata diagnostics.

- **FBX imports** (2026-10-10): Assets, Drawing in 3D, Animation, Editors and the spec now document binary/ASCII `.fbx` for static and skinned build-time imports. They explain axis/unit conversion, animation baking, helper bones and separate textures, while runtime `SkinnedMesh.Decode` remains self-contained GLB only. The production docs build passed; missing generated example pages use the existing build's allowed placeholders. No site deployment was requested.

- **Docs audit, 2026-10-08:** checked the user pages against the spec, subsystem notes and current code. Corrected nested collection and in-place field rules, default values, explicit World helpers, parallel lists, GPU random ordering and async limits; inputs versus commands; Android touch support and phone suspension; current full-world delivery despite private-scene metadata; profiler timings, CMake/host options and presentation ordering; and misleading 3D upload/culling claims. Audio now uses separate stereo filter histories, per-frame bus settings, frame-separated restarts and a delay line that reads before overwriting. The spec's obsolete World/array statements and Audio's unbuilt status are corrected; the original Audio proposals are labeled as historical rather than changing agreed design.
  - Three new standalone examples use the existing CMake glob and source inclusion: `pointer` on Input (pointer/button edges, individual fingers and their ten slots), `maps` on Text and lists (insertion order, TryGet, set deduplication and removal), and `audio` on Audio (an asset-free sine source, frequency/volume controls and a waveform preview). All are in Live examples. They fit the usual 376 × 400 viewport; `audio` starts silent and only plays after its Play sound button is pressed. None changes the engine or compiler.
  - Checked native and web-debug builds for the three; native scripted captures checked two fingers and release, adding/tagging twice/removing a map entry, and audio play/stop. The one-second audio capture has initial and final silence and a quiet tone while playing (16-bit stereo, 48 kHz). These are manual authoring checks, not new test targets; browser rendering uses the existing docs example test.
  - Site validation passed: `npm run check` (no errors or warnings), `npm run build` (including page/link/anchor checks), all 200 grammar checks and all three SEO tests. The existing example test passed for `TIDE_EXAMPLES=pointer,maps,audio` on both WebGL and WebGPU; screenshots of the examples beside their source were inspected.
  - Further demo opportunities are fixed arrays with nested collection copies, and a room whose host leaves while another machine continues. Selective visibility has no demo because it isn't implemented. The existing rooms, prediction and physics examples already cover their current features.

- Whole games are collected on `docs/guide/samples.md` (the owner, 2026-10-06: "a big samples page where they have a thumbnail and it opens the game in a full window", with the same room flow so people can play together). Its thumbnails open the game's own page in a new tab; the gallery starts no games. The smaller teaching examples stay in their docs pages.
  - Two Forks left the Samples gallery on 2026-10-08 while the owner considered a standalone release. After briefly requiring admin sign-in on 2026-10-09, the owner's sharing request makes its game public at `https://tide-engine.dev/prototypes/two-forks/`. Old game addresses are unsupported. `sample_games` still builds it, staging beneath the public prototype path. See Shareable prototypes above for the serving contract and validation.
  - A sample has a dedicated docs page only when it includes a tutorial, guide or code walkthrough (the owner's follow-up, 2026-10-06). Play-only examples belong in Samples. Duel has a code tour; Towers and Sumo have walkthroughs. Sand's former dedicated page was mostly play instructions and source listings, so it was removed.
  - Nine games from the docs-driven trial live in `examples/samples/<name>/`, each a `game.tide` and a `tide.packages` that lists `rooms` (the menu that comes with tide: see Rooms, under Packaging and releases), so its folder works with `tide run`, and a game opens on Play, Host a room and Join a room with no code of its own for it. Until 2026-10-07 each carried a copy of one lobby file. Their original source and the earlier public draft remain in git's history; the owner's room-flow request authorizes adapting those samples.
  - Tag (`examples/samples/tag/`) is a tenth, and not one of the trial's: playground tag for up to eight, with bots, made to show Tide to people who build game makers (the owner, 2026-10-08, for Google Labs' Playground, which had launched the day before with every game in its showcase single-player: "a demo game specifically for them", then "3D and impressive looking", "msaa", and "sound... textures, shaders can be much more complicated"). Claude wrote it in that session, from the docs and the other samples; its rules, its look and its place in the gallery (after Sumo arena) are picks the owner hasn't confirmed.
    - It's five files, where the other samples are one, and `examples/samples/CMakeLists.txt` builds every `.tide` file of a sample's folder for it, in the order of their names, as tide compiles a folder's: `game.tide` is the match, `light.tide` the light, the materials and every frag, `shapes.tide` how meshes are made and the meshes of what moves, `yard.tide` what stands still, and `show.tide` the views, the sounds and the scores.
    - Its match is flat, a kid a `float2` in a square yard, and what's drawn of it is 3D with the yard's `y` as the world's `z`, on the pattern of Sumo's `light.tide`: a frag lights each pixel and paints it by what it's made of (wood, stone, leaves, brick, tiles, glass, clouds, all from noise), with a 2,048 pixel shadow map, 7,000 blades of grass a vert bends, the frame in a `RenderTarget.Window(samples: 4)`, and nine Blits to the window (half and quarter size blurs for the tilt-shift and the glow).
    - Nothing in it is a primitive drawn as it is (the owner, 2026-10-08, of the first version, which was boxes, balls and rolls: "I wonder if we could try and make meshes for them to make them look more like what they are meant to"). Every mesh in this sample is made in code as the game starts, from `shapes.tide`'s shapes: `PutLathe` (a profile swept round: trunks, the bell, a kid's shirt and cap, lamps), `PutBlob` (a ball sculpted by noise, smooth for leaves and clouds, cut in facets for rock), `PutTube`, `PutBox`, `PutRoof` and `PutPyramid`. Static glTF/OBJ imports have since been added (see the Assets page and `model` example); Tag still uses its procedural meshes. `PutTriangle` takes which way a face looks and winds it to match, which is what keeps a shape from being inside out. What stands still is added to one mesh with each corner's own paint (`Scenery.mesh`, drawn by `Baked` in one draw); what moves is a mesh of its own, drawn from a list of instances made again each frame (`Fx`): a kid's body is one mesh, tinted, and its arms, legs, hands and shoes are instances beside it.
    - Who's it is the `Recess` singleton's (`it`, `tagger`, `since`), not a field of a kid's: whoever was just caught stands for `COUNT` seconds, whoever caught them can't be caught back for `NO_BACK`, and one kid is it for `TOO_LONG` (15 seconds) at most before it moves on by itself, so a player who sits in the menu, whose kid is in the match all the while, holds nobody up.
    - It's the one sample with assets: seven clips in `sfx/`, which `sfx/make_sounds.py` makes from arithmetic alone (no recording is in them, so there's nobody's license), packed into its page. They were checked as far as a run's `--capture-audio` being sound and not silence: nobody had listened to them when this was written, Claude can't.
    - Checked by hand, with no test of its own: scripted runs with no window at 960 by 540, 1280 by 720 and 390 by 780 (touch), and two pages of the web-debug build in headless Chrome on this machine's GPU, one hosting and one joining through the relay we host, on WebGL and on WebGPU: both in the room in about two seconds, the same match in both, 144 frames a second. Not checked: a phone, a GPU that's slow, more than two players. The thumbnail is frame 700 of the 960 by 540 run, in 256 colors.
  - Two Forks (`examples/samples/two-forks/`) started as an eleventh sample: a co-op puzzle for two after Stephen's Sausage Roll (the owner, 2026-10-08: "a co-op version of Stephen's Sausage Roll that enforces teamwork in the puzzle design", then of its first, smooth look, "try to play into Stephen's Sausage Roll's graphical style too, almost like a PSX thing"). The owner has played the original eight and asked to preserve them. It is now a prototype outside the Samples gallery, with a public shareable game address above. The current game has 24 kitchens, six per region, direct kitchen-to-kitchen progression and mostly in-world feedback. The owner rejected the selection yards and explicitly chose to remove the selector for now (2026-10-09), while retaining the new puzzles and regional mechanics.
    - **The core rules** (`game.tide`): a chef is a body and a fork, a tile each; a step forward pushes with the fork, a step back with the chef's back, and a turn sweeps the fork through the corner (it pushes the way it turns) onto the chef's side (it pushes back the way it came from). A sausage is two tiles: pushed across its length it rolls onto its other side, along it it slides, and it pushes the next. What lands on a grill is cooked on the side that's down; twice is burnt, and one with no land under either half sinks. Every side of every sausage once is the level. Each chef blocks the other and the sausages with its body and fork. A gas grill is out while either chef stands on a pedal, and cooks what's lying on it when it lights again. A move is all or nothing; the chefs move the first one first in a tick, so concurrent input is an order of single moves. There's no requirement to return to the kitchen's initial chef positions.
    - **Regional mechanics:** Saltwater Coast focuses on handoffs, landings and deliveries over water; Golden Fields develops gas-valve timing and changing jobs. Highland Pass introduces `Tile.Ice` (plan `i`, walkable): after a forced shove, sausages wholly on ice glide until either half reaches a non-ice tile or a rock, the partner's body/fork or a resting sausage stops them. Every crossed tile counts as a roll where applicable. Chains glide together; iteratively removing blocked gliders makes the result independent of sausage array order. Later blockage stops the glide, while an initially blocked shove rejects the whole move. A failed turn's second push restores the first push and every glide. Cooking is checked at final landing; the first non-ice half stops a glide, so no intermediate grill contact is lost. Ember Crater introduces `Tile.Ember` (plan `+`, a non-walkable grill): `BellowsOn` is true only while both chef bodies stand on pedals. It cooks on arrival while hot, or on the transition from off to on; lighting the same side again burns it. All eleven established plans and names remain unchanged.
    - **A player alone has both chefs**, one at a time (`Kitchen.lead`, changed with Space), and a second player takes the other chef. Whoever stays when one leaves has both again. The match takes two players (`maxPlayers = 2`).
    - **State and undo:** `Board` is fixed-array data in `Kitchen`, without entities. A move is a function on the struct; `List<Past>` keeps 256 moves, restoring both chefs and all sausages. Completion bits and layout revision are match state and participate in snapshots/networking. `Scenery.layout` is local presentation only. Restart remains an undoable move. The removed selector has no state, commands, rendering or solver left in the game.
    - **Progression** (`journey.tide`, `game.tide`): the match begins in the first kitchen behind the original Rooms Play/Host/Join menu. Play goes straight into that kitchen. Cooking awards its completion bit, then after `CHEER` ticks `NextKitchen` opens the next puzzle automatically, with no selector between them. Each region has six consecutive kitchens; stable level ids remain coast 0,1,2,11,12,13; fields 3,4,8,14,15,16; highlands 5,6,9,17,18,19; crater 7,10,20,21,22,23. Completing the last wraps to the first. The meatball yards, selection pans, plate gates, travel/shortcut commands and M return action have been removed. Cross-session saves and a low-gravity region are not built.
    - **Testing skips:** N opens `NextKitchen` immediately, in the same regional order as normal progression, wrapping to the first. The on-demand guide (H, gamepad West, or touch ?) has a deliberately revealed Testing shortcuts toggle and Skip kitchen control. `Goto` synchronizes both players, resets undo/movement and gives no cooking credit. Skip is absent from ordinary play's HUD.
    - **The kitchens** (`levels.tide`): the original first three teach sliding over water, rolling to cook, and holding the gas. The other established levels retain their original state graphs. Thirteen additions develop each region: The landing / Around the pier / Double delivery (11/14/19 minimum moves); Cold storage / Valve courtyard / Trading shifts (15/16/20); First frost / The chute / Human brake (12/16/19); Two bellows / Hot corners / Pressure dance / Chimney work (7/17/23/29). Human brake has an actual partner-stopper move: a chef parked at (0,2) stops a southward glide on (0,1); without that body the sausage continues and presents the opposite side. The miner's ice and bellows modes and the shared JS rules design candidates, rejecting bounded searches that gave up. `node examples/samples/two-forks/tools/levels.mjs --ways` reproduces the paths and every pin.
    - **Solvability:** `two_forks_tests` exhausts the actual match's reachable states for all 24 kitchens, pinning shortest completion, total positions and both solo frontiers. Every kitchen is solvable together; neither chef alone can complete any of them. The C match and JS solver agree on all pins. The largest added graph is 22,920 positions. The rule/input/command/exhaustive tests remain; progression integrations cover direct boot, actual cooking and automatic advancement through every kitchen, Goto authorization/reset/credit, and snapshot restore. The obsolete map tests and `tools/map.mjs` were removed with the selector.
    - **The look:** the PSX treatment is retained: about 270 vertical pixels, flat faces, corners snapped to frame pixels, texel materials, hard shadows, color quantization and ordered dithering. No blur, glow post-process or MSAA. `shapes.tide` keeps copied procedural builders so the game stands alone. `light.tide`, `island.tide` and `environment.tide` give the coast moorings and original surf, fields trees/wheat, highlands snowy peaks/pines and the crater flowing lava/ash. Ice has cyan frost/cracks; bellows hearths visually follow the same both-pedal state as the match. Lava carries coherent crust rafts, glowing currents/fissures, cooled banks and moving vent smoke/embers; the coast water shader's math stays unchanged. Dressing stays outside playable footprints.
    - **UI** (`ui.tide`, `show.tide`): ordinary play has no header, progress dashboard, crest or button tray. Room connection/pause keeps a quiet GUI and the original package flow. Touch keeps translucent arrows, chef swap and ?. H/West/? opens a guide with controls, wrapped puzzle hints and Undo/Reset actions; it can reveal the testing skip. Map labels, pan numbers, meatball graphics and all map controls have been removed.
    - Sounds are made as Tag's are (`sfx/make_sounds.py`, arithmetic alone), and nobody had listened to them when this was written. Surf is quieter inland and silent at the crater.
    - **Validation:** all fifteen tests pass after merging the latest compiler work, natively (6.9 seconds) and under WASI (9.0). The new progression test cooks all 24 kitchens with actual two-chef inputs and verifies completion credit, automatic region-order advancement and wrap. Native two-process captures checked direct puzzle boot, shared movement/undo and a skip from one player, with both views agreeing. Muted headless Chrome checked WebGL/WebGPU direct Play, all 24 N skips/wrap, keyboard recovery/help, and phone touch movement, chef swap, guide Undo/Reset/Skip. Fresh rebuilt-artifact smoke repeated direct boot, the first region boundary and phone controls/help without runtime/rendering errors. Its subsequent move to private admin serving is validated separately above. A real phone and two machines through the relay remain untested.
  - `sample_games` builds them natively, and on the web stages public pages at `docs/static/samples/<name>/index.html`, beside the gallery's checked-in screenshots in `docs/static/samples/thumbnails/`. Two Forks stages only under `docs/static/prototypes/two-forks/`. The source globs use `LIST_DIRECTORIES false`: the CLI's hidden `.tide` cache directory must not be mistaken for a `.tide` source file. The Docs workflow builds them, and the docs' test opens each game the public gallery links to and checks it draws (see Docs site); prototype URLs and admin catalog protection have the separate HTTP tests above.

- **Rooms volume settings, 2026-10-09:** the Matches page documents the title/room Settings buttons, the volume slider and `Page.Settings`, and the local `Lobby.volume` range/default and lifetime. Back/Escape returns to the menu that opened Settings. The package README describes the same behavior. `npm run build` passed.

Confirmed presentation/physics rewind (2026-10-11): Views, confirmed-state guidance, networking, input and both physics package pages describe confirmed interpolation, opt-in read-only rewind queries and timestamp trust boundaries. C hosts forward both intervals before marking presentation. Singleton methods and the changed frame ABI are documented with the spec; the production docs build passes. Implementation and full native/web validation are in the subsystem notes.

Inside Out shared-world revision (2026-10-11): the existing public prototype
and authenticated catalog now describe two freely travelling makers, nested
rooms and canonical recursive doorways. Its gameplay thumbnail uses the
new flat workshop capture. Site check/build and all 23 analytics/admin,
production HTTP/SEO/access checks pass; staged and production HTML matches
the web-release game. Game-specific contracts and validation are recorded in
`notes/inside-out.md`. The public URL and catalog access boundaries are retained.

The revision was deployed to the existing `tide-website` Fly app directly
from `09b68b4b` (release 255); its health checks passed. Anonymous Inside Out
HTML/thumbnail requests return 200 and match the tested release bytes. A
two-browser check of those downloaded bytes with the production relay passed.
The packaged catalog retains the incoming CRACKED, Inside Out and Two Forks
entries, with no stale HTML from removed prototypes. No CI was dispatched.

Inside Out focused-view revision (2026-10-11): the catalog description and
gameplay thumbnail now show the per-maker dimension and its containing world.
The public URL and authentication/discovery rules are unchanged. The final
web-release page is restaged; Svelte check has zero errors/warnings, the
production build passes, and all 23 analytics/admin and production HTTP/SEO/
access checks pass. Camera, audio and gameplay evidence belongs in
`notes/inside-out.md`.

That focused-view revision deployed directly from `c298c929` to the existing
Fly site (release 256); health checks passed. Anonymous game and thumbnail
requests return 200 and match the validated release hashes. The packaged
catalog still contains only Inside Out, CRACKED and Two Forks.
Two-browser WebGPU gameplay of the downloaded page with the production relay
passes, including the focused-camera and recursion checks. No CI was dispatched.

Inside Out spatial-zoom revision (2026-10-11): the authenticated catalog
description now explains automatic pushing followed by blocked doorway entry.
Its new gameplay thumbnail shows Peach nested in Mint with the partner outside.
The public URL and discovery/authentication boundaries remain the same.
Svelte check reports zero errors/warnings, the production build passes, and
all eighteen analytics/admin plus five production HTTP/SEO/access tests pass.
Release, staged and packaged Inside Out HTML are byte-identical. Game-specific
camera, movement, campaign and audio evidence is in `notes/inside-out.md`.

The maker-passage follow-up retains that public serving contract. After the
clean integration through `origin/dev` `6adfbae9`, all 64 staged pages match
their release artifacts, including the incoming Underfoot and CRACKED pages.
The authenticated catalog retains Underfoot, Inside Out, CRACKED and Two
Forks. All 225 grammar checks and the full WebGL docs/gallery smoke pass;
Svelte check has zero errors/warnings, eighteen analytics/admin checks pass,
and the final production build and five HTTP/SEO/access checks pass.
Inside Out's final packaged HTML hash is recorded in `notes/inside-out.md`.

Final integration through `origin/dev` `4df2b647` brought unrelated CRACKED
and Underfoot updates, which were rebuilt/restaged before deployment. All
64 playable pages and four prototype thumbnails match the final package;
the production build and five HTTP/SEO/access checks pass again. Source
`a701de03` deployed directly to the existing Fly app as release 258 and passed
health checks. Anonymous Inside Out HTML/thumbnail requests return 200 with
the tested final hashes, and two-browser gameplay of those downloaded bytes
through the production relay passes. No CI was dispatched.

Inside Out local-camera revision (2026-10-11): its catalog description and
gameplay thumbnail now show the whole current room with one tile of outside
context. Camera targets no longer follow the partner. Public serving,
catalog authentication and the other three prototypes are unchanged. The
site check has zero errors/warnings; its production build and all eighteen
analytics/admin plus five production HTTP/SEO/access tests pass. Release,
staged and packaged Inside Out page/thumbnail bytes match; their hashes and
game-specific camera validation are in `notes/inside-out.md`.

The clean merge through `origin/dev` `39fe3e4f` brought shared compiler,
platform/web and docs updates plus current Underfoot changes. All 64 playable
pages were rebuilt/restaged and match their production-package copies; all
four prototype thumbnails match as well. Docs check has zero errors/warnings,
226 grammar tests pass, and the production build plus five HTTP/SEO/access
checks pass again. The integrated Inside Out hash is in `notes/inside-out.md`.

Source `ff775a99` deployed directly to the existing Fly site as release 259
and passed health checks. Public Inside Out HTML and thumbnail return 200
with the tested integrated hashes; the catalog retains all four prototypes.
Two-browser gameplay of the downloaded page through the production relay
passes, including fixed local framing while the partner goes off-screen.
No CI was dispatched.

Inside Out nearby-maker/icon revision (2026-10-11): its catalog description
now explains the glimpse of a partner beside the current room. The refreshed
gameplay thumbnail shows the wider fixed containing-world context and box/
player goal icons. Public serving and catalog authentication remain unchanged;
Underfoot, Inside Out, CRACKED and Two Forks are retained. Svelte check has
zero errors/warnings, the production build passes, and eighteen analytics/
admin plus five HTTP/SEO/access tests pass. All 64 playable pages and four
prototype thumbnails match their release/staged/packaged copies. Final
hashes and game-specific evidence are recorded in `notes/inside-out.md`.

Integration through `origin/dev` `430961d7` brought compiler/shared asset
runtime and Underfoot updates. All 64 playable pages were rebuilt/restaged;
their release, static and production-package bytes agree, as do all four
thumbnails. The site check stays clean, 227 grammar checks pass, and the final
production build plus five HTTP/SEO/access tests pass. The final Inside Out
hash is in `notes/inside-out.md`. No CI was dispatched.

Source `421f1f25` deployed directly to the existing Fly site as release 260
and passed health checks. Public Inside Out page/thumbnail requests return
200 with the tested final hashes. Two-player WebGPU gameplay of the downloaded
page through the production relay passes, including the nearby partner glimpse
with fixed local framing and recursive doorway travel. The collaborative
preview navigates to the public URL. No CI was dispatched.

## Shared components and Transform (2026-10-12)

Entities and Local state now document shared component definitions, explicit
local placement/access, built-in local TRS, deferred parenting and descendant
lifetime. The spec separates approved architecture from first-build API/details
and remaining prefab/editor and presentation work. Existing Transform examples
use its built-in localPosition/localScale fields. The local-state live example
and shared grammar probe use the new placement syntax. Final docs/grammar build
results are recorded in notes/language.md. Nothing is deployed by this task.

## Package documentation navigation (2026-10-12)

The owner's package-discovery request replaces the Physics sidebar group and
scattered package entries with one Packages section. `docs/packages/index.md`
lists all five bundled packages by dependency name and namespace, links their
API pages, distinguishes optional packages from built-in engine features and
explains how to find README/docs/examples for git and local packages, including
documentation at a pinned commit. README guidance is an authoring recommendation;
no documentation metadata, registry or new CLI command is implemented.

Existing physics pages, Lobby, Steam and the package usage/authoring guide move
under `docs/packages/`. Rooms' menu/API/customization sections move intact from
Matches to their own package page; Matches keeps the engine session/room guide
and links to both optional menus. The existing split-section mechanism forwards
its moved customization anchors. Internal links, bundled package README URLs and
current layout notes follow the new routes. Former whole-page routes are no
longer in navigation, search or the sitemap; no route aliases were added.
Package APIs, gameplay examples and language semantics are unchanged.

Validation: `npm run build` passes with page/sidebar, link and anchor checks;
`npm run check` reports zero errors/warnings; `git diff --check` passes. Existing
allowed 404s cover unstaged game/example HTML. Node is 24.15.0 in this shell;
no Node test suite or game was run. The collaborative browser confirms the new
overview, catalogue and navigation on the local development site. No site
deployment or CI dispatch was requested.
