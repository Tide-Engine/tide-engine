# Releases, installing and CI

## Node security updates (2026-10-23)

Every Build and Docs `actions/setup-node` step requests `^24.22.1` with
`check-latest: true`. This requires the October 21 security fixes while
selecting subsequent 24.x patches even when the runner caches an older release.
Workflow parsing and structural comparison verify that only these Node inputs
changed; triggers, jobs, tests and publication/deployment gates are unchanged.
The docs and relay images explicitly pin `node:24.22.1-alpine`; later image
security updates require advancing that pin. Service-repository pins are
maintained separately in `Tide-Engine/tide-services`.

Docs dependencies also advance markdown-it to 15.0.3 and Vite to 8.3.3 for
their published security fixes, preserving the existing compatible ranges.
Both docs and release-note dependency audits report zero vulnerabilities.
Release-note tests pass 3/3. Full local validation is recorded in
`notes/testing.md`; docs and relay validation are in their subsystem notes.
No workflow was manually dispatched.

## MicroDuck sanitizer timeout (2026-10-22)

Run `39842031157` at `f8468b30` confirms the arm-hash correction:
all ordinary platform suites and all three package jobs pass. Its only
completed underlying failure is the MicroDuck two-client rollback case's
180-second Linux sanitizer timeout. Docs is still installing tools when
inspected, so the overall run is not complete and publication is skipped.

Only that case's instrumented deadline becomes 600 seconds; ordinary budgets,
simulation work, assertions, sanitizers and workflow gates stay unchanged.
A fresh Windows ASan/UBSan reproduction passes in 184.59 seconds with zero
resyncs and no sanitizer diagnostics. Scope, CTest metadata validation and
remaining hosted Linux confirmation are in `notes/newton.md`, Instrumented
MicroDuck rollback budget. Full local debug suites pass 2,593 native and
2,455 web tests, retaining the existing disabled right-turn limitation.
No manual dispatch or retry was performed.

## Matching JetBrains plugin in platform packages (2026-10-21)

The JetBrains job also builds and uploads tide-jetbrains-installer.zip, an
offline Tide/LSP4IJ bundle with the engine's exact version and upstream notices.
The checks matrix waits for that job; package entries download the artifact to
build/jetbrains before cmake/package.cmake adds it to editors/ and stages its
notices under licenses/jetbrains. Packaging rejects absent/mismatched versions.
The installed-package smoke step runs jetbrains_test.cmake with the real bundle,
using isolated profiles and checking every installed Tide/LSP4IJ byte.

The original single-plugin ZIP remains the Marketplace/release asset. The
offline archive travels inside each platform package; no new public release
asset, trigger, publication condition or Marketplace channel is introduced.
YAML parsing and the job-dependency graph pass. Local Gradle, version/package
fixtures, actual-archive install, docs and full native/web validation are in
notes/cli-and-packages.md, Bundled JetBrains installation. No workflow was
dispatched; actual hosted Linux/macOS packaging remains CI coverage.

## Automatic runs and releases without a commit back (2026-10-21)

With the repository public, Actions cost nothing, and the owner asked for workflows to run automatically and for releases not to hold up `dev`. Build now runs on pushes to `dev` and `release`, on pull requests, and on request. Its concurrency group per ref still finishes the current run and keeps only the newest waiting push, so a burst of pushes to `dev` publishes one nightly, of the last; a pull request's newer push cancels its older run. Pull requests publish nothing and, from forks, get no secrets (Docs then builds without the prototypes and doesn't deploy).

A stable release no longer commits anything: `.releaserc.json` lost `@semantic-release/exec` (which wrote `VERSION`) and `@semantic-release/git` (which committed `chore(release): x [skip ci]` to `release`), so semantic-release only tags the commit and makes the GitHub release. `release` therefore never moves ahead of `dev`; a stable release is `git push origin dev:release`, a fast-forward the `release` ruleset allows, and the tag lands on a `dev` commit.

Development builds take their version from the newest `v*` tag in their history (`git describe --tags --abbrev=0`, its pre-release part dropped: `0.5.0-dev` after `v0.5.0-nightly.3`), with `VERSION` as the floor for checkouts without tags (CI's shallow ones, an archive). It's read once, when `TIDE_VERSION` isn't cached yet. `VERSION` is now changed by hand only, to raise that floor.

Validation: configure checks of the tag and fallback paths; the workflow was checked as YAML; no run was dispatched for this change (its first automatic run is the push itself).

## Tide Marketplace channels (2026-10-21)

The owner approved publishing both engine channels to the same Tide plugin
listing, io.github.blenminer.tide: dev uses the opt-in nightly channel and
release uses Stable (Gradle's default channel). No build publishes to the
former PurrLang ID. The owner reports submitting the first Tide Stable upload;
Marketplace approval/access is not established by this local change.

The Release job now exposes published=true after its nightly step has either
created the GitHub release successfully or verified the existing release and
all its assets on a retry. The stable output stays semantic-release's
new_release_published. Marketplace requires the successful Release dependency,
its true publication output, and either dev or release. It passes the selected
version and nightly/default channel as quoted Gradle properties, using the
existing JETBRAINS_MARKETPLACE_TOKEN. A missing token retains the existing
explicit skip. Upload errors fail the job. Other refs and unpublished versions
do not reach Marketplace. Build triggers, suites and release gates are unchanged.

The plugin README describes both channels and the custom nightly repository
https://plugins.jetbrains.com/plugins/nightly/list. Plugin channel selection
does not change the installed Tide/tidels engine channel. The public plugin
repository retains source only; publication remains this engine workflow's.

Actionlint 1.7.12 passes with only its unsupported existing concurrency.queue
key excluded. Parsed workflow comparison confirms the only behavior changes
are the nightly publication output and Marketplace routing/arguments. Executing
the exact Bash blocks against isolated local command stubs checks both channel
argument sets, missing-token skips, Gradle failures, successful new/existing
nightly releases, failed Git pushes/uploads and invalid existing releases.
Failed releases never set the publication output. No real token, tag, release
or Marketplace upload is used by those checks. Full native debug passes all
2,485 CTests, including 36 native GPU checks (96.98 seconds). Full web debug
passes all 2,391 CTests, including WebGL, WebGPU and Windows ANGLE D3D11
(282.52 seconds), with Clang 23.1.2 and Node 24.19.0.
No CI run or actual Marketplace publication was dispatched.

Implementation commit 6892c6d0 was merged into dev at 5bef2893. Incoming
compiler/CLI/build and Unity work merged cleanly; neither the Marketplace
workflow nor the plugin directory changed during that integration, so the
scoped workflow checks required no repeat. The full engine suite results above
precede that unrelated integration. The public plugin main was fast-forwarded
to efc67411de0ce9c4508d45d629a1379b71d30f1f with only its README changed.
All 24 public historical trees still match their canonical plugin subtrees;
40 historical paths and 89 unique blobs were audited. The public remote still
contains main only, with no engine tags or additional release workflow. This
enables the next manually requested Build; no older run was retried.

## Docs export regression gate (2026-10-21)

The existing Docs workflow's Build the site step now runs `npm run test:exports`
after `npm run build`, before its existing HTTP/browser checks and deployment.
It validates prerendered AI/Markdown downloads in a source-free copied deployment
as well as Vite's development responses. The workflow parses as YAML, its scoped
comparison and whitespace check pass; actionlint is not available on PATH here.
Local production/export/UI and fresh-model validation are recorded in
`notes/docs-site.md` and `notes/ai-onboarding.md`. No triggers, release gates or
deployment conditions changed, and no workflow was dispatched.


## Audio completion and browser scheduling (2026-10-18)

Run `39128917687`, at `7580cd99`, failed Android's audio-device state-reclamation
assertion and Windows web's hidden-host room session. All other platform
suites, Windows/macOS package checks, Docs and JetBrains verification passed.
Linux packaging stopped at its failed Android suite; the aggregate Build gates
failed/cancelled and Release was skipped. No nightly was published.

The audio completion ordering fix and deterministic old-code failure are in
`notes/audio.md`. The custom browser tests now use the existing CPU reservation
and graceful profile shutdown, described in `notes/testing.md` and
`notes/webrtc-and-rooms.md`. No check, timeout or publication gate was removed.

Local full native debug passes 2,375 tests (436.73 seconds), including the
audio device and native GPU checks. Full web debug passes 2,285 tests (275.42
seconds), including WebGL/WebGPU and the new deterministic audio regression.
Validation used Clang 23.1.2, Node 24.16.0 and Chrome Headless Shell
154.0.8037.92; hosted Windows uses the separately pinned 155 build.

With CTest and its child browsers restricted to two CPUs, each of the three
room cases passes three consecutive debug runs and three optimized runs.
The hidden-host case takes 11.49--11.68 seconds in debug and 11.79--11.99
optimized, versus 30.23 seconds in the original concurrent schedule. The
original immediate repeats failed with locked profiles; corrected repeats
reuse their profiles successfully. These local timings demonstrate scheduling
and shutdown effects, not the exact cause of the hosted disconnect.

Affinity-only threaded stress still advertises this machine's 24 CPUs to
Chrome. It reached its unchanged 45-second guard during constrained/load
checks, then passed three isolated optimized repeats (40.19, 10.47 and 5.57
seconds). A scratch generated page reporting two CPUs as well as running on
two passes in 26.48 seconds, with 408 identical ticks, both threads participating
and a C worker sleep observed. The generated page was restored afterward;
no worker-count override was added to the repository. Both runners' early-exit
controls fail immediately as intended. Node syntax and whitespace checks pass.
Actual Android/AAudio, other operating systems, MinGW and sanitizer confirmation
remain the next requested CI run's coverage; no workflow was dispatched.

## Nightly package/test fixture corrections (2026-10-15)

Owner-requested run `38761765659` at 08473d4b completed with four failing
checks: Linux/macOS package cache comparisons and Windows native/MinGW
dedicated-server harness shutdown. Windows packaging, Linux native/sanitizers,
all three web suites, macOS native, Docs deployment and JetBrains verification
passed. The aggregate Build jobs failed/cancelled and Release was skipped;
no nightly was published. The two underlying fixture corrections and their
reproductions are in cli-and-packages.md and dedicated-servers.md. No workflow,
publication gate, client verification assertion or timeout was removed.

Local full native debug passes all 2,342 tests, including GPU validation and
both transports (412.07 seconds, Node 24.16.0/Clang 23.1.2). Full web debug
passes all 2,255 tests, including WebGL/WebGPU (269.88 seconds). The final
server harness passes its focused transport rerun too.

Merged the current dev work, including visibility and folder deployment links;
the only conflict was adjacent CLI notes, both retained. The merged full native
debug suite passes all 2,359 tests (119.78 seconds), and full web debug passes
all 2,272 (282.54 seconds), including WebGL/WebGPU. The refreshed installed
CLI also passes the complete native/web cache, stripping, failure-recovery,
debug/release web-watch and bounded dedicated-server integration cases.

Fresh requested run `38767273771` at dd5cb803 found a separate macOS package
failure before the cache checks: the newer runner image combines standalone
CLT 27 with default Xcode 26.6, exposing hardcoded SDK discovery inconsistent
with Apple's selected linker. The fix, native cross-platform discovery fixture
and remaining actual-Mac validation are in cli-and-packages.md, Selected Apple
SDK. This changes the CLI's automatic SDK lookup, not the workflow or its gates.
Local validation after that correction passes all 2,360 native debug and 2,272
web debug CTests, plus the discovery regression on Windows/Linux. The old code
fails that regression. Actual Apple linking remains the next package run's gate.

Run `38768886333` confirms the Apple fix on the same newer macOS image:
all package smoke/C++/cache/watch checks pass, including both repaired-build
comparisons. Windows native instead failed only tide_cli_installers: the
partial PowerShell warm-up took 10.2 seconds, but the first timed case still
spent 18.5 seconds between its release-list return and first mock download,
using only 0.125 seconds of CPU in that interval. It hit the unchanged
30-second guard after both mock downloads. The exact assembly/file wait in
release selection/scratch setup is not identified; the earlier assertion that
the cmdlet-only warm-up covered all first-load costs was too broad.

The preflight now executes the actual mocked fresh installer with the same
release fixture, real checksum and isolated modules/TEMP, stopping before
unpacking. It keeps its existing 300-second startup guard; all nine cases keep
their 30-second guards and assertions. No real installation, PATH/profile or
registry write is performed. A temporary one-time 31-second release-selection
stall fails the old first case and is absorbed by the full preflight; the new
nine cases then all pass. Ordinary and poisoned inherited module/cache-path
runs pass all nine cases locally. A separate 31-second stall injected only
after preflight still fails the first timed case at 30.02 seconds. Full native
debug passes 2,360 tests (67.65 seconds), and web debug passes 2,272 (178.91
seconds), including WebGL/WebGPU. The final preflight's release/request/cleanup
assertions and all nine ordinary cases pass again; Node syntax and whitespace
checks pass. No installer or workflow behavior changed.

Final requested Build `38772910822`, at ba52064b, completed successfully and
published `v0.4.0-nightly.3` at 17:21:26 UTC on 2026-10-15. All native/web,
MinGW, sanitizer and Android suites, all three packages and their installed
smoke/C++/incremental checks, Google Play checks, JetBrains verification and
Docs deployment passed. Windows native passed all 2,292 CTests, including the
installer fixture in 22.83 seconds. Marketplace was skipped as intended for a
nightly. The preceding run finished with only that installer fixture failing;
its other suites and all package integrations passed too.

Verified the published tag names exactly ba52064b, the release is a nondraft
prerelease with all five nonempty uploaded assets, and the public release JSON
reports the same assets. The public SHA256SUMS contains all three platform
packages, and the deployed docs homepage responds HTTP 200. Both Windows
(nine cases) and Linux/POSIX (eight cases) installer fixtures pass locally.
These final notes are unbuilt text; no additional workflow dispatch is needed.

## Managed hosting deployment workflow (2026-10-15)

Moved (2026-10-21) with the service to `Tide-Engine/tide-services`, where the
workflow now checks out this repo's `dev` for the relay (`notes/repository.md`).
The record below is its history here.

At the owner's request, added `.github/workflows/hosting.yml` (Hosting), separate
from Build and Docs. It runs only through `workflow_dispatch`: every ref runs
the service's `npm ci`/`npm test` on Ubuntu 24.04 with Node 24.16.0; only `dev`
validates `hosting/fly.toml` and deploys `tide-hosting` with the remote builder
and `--ha=false`. The owner confirmed the existing `FLY_API_TOKEN` repository
secret is shared across apps. It is supplied to Fly validation and deployment;
provider credentials remain in Fly. Global hosting concurrency serializes runs
without cancelling an active deployment. No connection to engine publication,
automatic push trigger or allocation-enabling step was added.

The service tests cover real HTTP/relay/WebRTC, ledger/auth, WASM isolation and
recovery. Two installed-toolchain/browser cases require staged tools and skip in
this lightweight deployment workflow. The operator README describes invocation,
coverage and the existing first-deployment setup boundary. Validation:
actionlint 1.7.12 passes for the new workflow, the Fly configuration validates,
and the production Docker image builds. Service tests pass on Windows and in
the Linux service image with Node 24.16.0: 29 passed, two staged-tool integration
cases skipped as documented. Dependency installation reports no vulnerabilities;
the whitespace diff passes. No workflow was dispatched or service deployed.

The owner's first Hosting run, 38754705649 on 364983c5, passed dependency
installation and all ordinary service tests but failed `flyctl config validate`
with "no access token available". Local validation had inherited CLI login,
hiding that this command also requires authentication. Supply the same
`FLY_API_TOKEN` to that step, keeping it outside dependency installation/tests.
The owner explicitly requested a fresh run after the fix; follow its completed
result rather than retrying the old commit's unchanged workflow.

Run 38754895170 on 1ccb7430 then passed validation but deployment was rejected
as unauthorized. Fly's token list showed the repository credential was scoped
to `tide-website`. Created one-year deploy credentials for `tide-website` and
`tide-hosting`, combined their FlyV1 macaroon bundles using Fly's supported
comma-separated header format, and updated the existing repository
`FLY_API_TOKEN` secret. Both app API reads succeed and an unrelated app returns
403. No organization-wide token was created, no prior token was revoked, and
the website workflow keeps the same secret name. The combined value is held
outside the repository in the private user-only hosting-setup directory; no
token values were printed. These credentials expire in October 2027. Retried
the corrected run after this credential update.

Attempt 2 of 38754895170 passed all steps and deployed 1ccb7430. Fly reports one
started shared-cpu-1x/512 MiB machine in `fra`, one encrypted 1 GiB `hosting_data`
volume and a passing health check. All four OAuth/Stripe secrets now show
Deployed. An HTTPS request through `tide-hosting.fly.dev` with the configured
console Host header returns `{"ok":true}`. The console custom domain still does
not resolve here; no app certificates are configured yet. This is the first
service deployment, not completion of paid-launch/provider/restore checks.
Allocation enablement and rates remain unchanged. The checkout@v4 Node 20
deprecation annotation does not fail the run; other workflows use that action
too and it was not changed as part of this fix.

## Stable upgrade fixture failure (2026-10-14)

Build run 38577620235 builds release commit 8637bd0f as 0.3.0 on the stable
channel. All five failed suites in attempt 1 failed only `tide_cli_upgrade`;
All five repeated that single failure in attempt 2. Packages, web
suites, Docs and JetBrains verification passed. The test's local release list
hardcoded `prerelease: true` and nightly output, so the stable CLI correctly
reported no stable package. This does not query published releases and is
unrelated to the age of the previous stable release.

The fixture now follows the built CLI's default channel; implementation and
local reproduction are in notes/cli-and-packages.md, Upgrade fixture channels.
An old-run retry still checks out 8637bd0f. The owner approved merging the
locally tested fix f2f41897 into `release` and starting a fresh stable Build.
Run 38584459544 checks release merge 184985b0. No suite or publication gate
was removed. All native platform and MinGW suites passed 2,263 tests each,
including `tide_cli_upgrade`; sanitizers passed 2,261. All web suites, Android,
three package jobs, Docs and JetBrains build/verification passed too.

The Release job published stable v0.3.0 at 15:17:36 UTC with the three platform
packages, JetBrains ZIP and SHA256SUMS, and committed VERSION on `release`
(c38b8c03). The public `/releases/v0.3.0.json` endpoint confirms all five assets
and `prerelease: false`.

The overall run completed with failure only in the subsequent JetBrains
Marketplace job (115935242705). Its plugin build/tests passed; upload of
`io.github.blenminer.tide` was rejected with "Cannot find plugin" and the
requirement for a first manual upload. JetBrains' publishing documentation
requires that initial upload before Gradle publishing. The Tide rename commit
7e4a88c9 explicitly called for a new Marketplace listing; the previous stable
run 36837849506 skipped the Marketplace job. The current public plugin-ID lookup
returns 404. These observations establish a Marketplace setup/access blocker,
not a compiler failure; do not claim the overall workflow passed or rename the
plugin ID to work around it. The published engine release remains available.
The v0.3.0 `tide-jetbrains.zip` release asset is ready for initial manual upload
under the intended publisher account; account/listing setup remains with the
owner. No additional workflow or failed-job retry was dispatched.

## Docs LLVM alignment (2026-10-14)

The Docs workflow now installs the LLVM major selected by cmake/LLVM.cmake
through the same apt.llvm.org setup as the main Linux Build job and exports
LLVM_ROOT. Its former Ubuntu Clang 18.1.3 built a playground compiler importing
env.saveSetjmp, which the WASI browser host cannot provide (run 38555232799).
Version parsing, repository selection and environment setup match Build;
installation has Bash pipefail and prints clang's version. The staging ABI
guard, reproducer and local full-suite validation are in notes/docs-site.md.
No triggers, suites, deployment/publishing gates or retry policy changed.
The owner explicitly requested a new workflow run and following it to completion.
Docs run 38557265795 on 7333e64c passed the full job with Clang 23.1.2,
including playground staging/browser checks, and deployed successfully to Fly.

## Optional Windows prefab registration (2026-10-13)

With TIDE_ASSOCIATE_PREFABS=1, install.ps1 registers this user's Tide.Prefab
ProgID, quoted tide-editor.exe open command and .tprefab/OpenWithProgids entry,
after a fresh install or successful installer-driven upgrade. It checks that
the native editor exists, leaves extension defaults and UserChoice untouched,
and does not claim an association on Linux/macOS. Windows owns the default
selection; the user can choose Tide in Open With. This follows Microsoft's
[Open With registration](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box).
Direct tide upgrade leaves the stable installed path's registry entries alone.
The native editor's GUI subsystem avoids an additional Explorer-launched console.

The installer harness intercepts every registry write, checks the quoted command
and candidate entry, and rejects extension-default changes. Its optional case
uses only dummy binaries and never writes this user's registry. All nine Windows
cases pass; existing download/checksum/upgrade failure checks remain. The Install
guide documents the opt-in and removal of just Tide's registered keys/values.
No installation or file association was changed on the owner's machine, and no
CI workflow was dispatched. Remaining-platform validation follows the normal
manually requested release workflow.

Build run 38367850012 (2026-10-13, `dev` at b1b07989) failed three ways, fixed the same day:

- **Every Package job:** `tools/tide-editor` installs `tide-editor` in the `tide` component, and the Package step built neither tree's `tide_editor`, so `cmake --install` found no file (the native tree's on Linux and macOS, the MinGW tree's on Windows). The step now builds `tide_editor` in both. Not tried locally: the package preset needs the LLVM libraries.
- **Docs:** `docs/tests/admin-http.test.mjs` refuses a catalog game's name anywhere in the public search index, which includes `docs/spec.md`. Two spec entries named the prototype they were built for. A docs change is checked with that test after `npm run build`, not the build alone.
- **`tide_platform_gpu_time_webgpu` on Windows web-release:** its second round gave a supported timer 60 frames to hand back a sample, which software WebGPU ran through in 0.3 seconds. Each round now ends at the first sample, and fails only after 3,600 frames without one (`platform/tests/gpu_time.c`).

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Packaging and releases

- Users install with `install.ps1` or `install.sh` into `%LOCALAPPDATA%\Tide` or `~/.tide`, with `bin` on `PATH`, and `tide upgrade` replaces the installation from the releases the site lists (below). It checks each download against the release's `SHA256SUMS`, and renames running programs aside instead of overwriting them. Once a day, tide says when a newer version is out. tide and the installers pick a channel's highest version by semantic versioning (`compiler/cli/release.c`), never the last one published, of those with this platform's package (some old nightlies went out without macOS's, back when its failures didn't hold releases back); the site's list has the latest stable release too, which nightly ones push out of GitHub's. `tide upgrade` only goes forward, except to an exact `--version` or when switching channels. Builds made from this repo are versioned `<VERSION>-dev` and never look for updates.
- **Anyone installs and upgrades from the site, with no account, and nothing they're sent names the repository or its owner** (the owner, 2026-10-07: releases stay on GitHub, free, in the private repository, "publicly downloadable while keeping my identity safe"; open to anyone, which replaced the whitelist of GitHub accounts and the tokens tide and the installers sent). What's built, and what code relies on:
  - `docs/releases.js`, in the site's server (`docs/server.js`), answers `/releases.json` (the versions out), `/releases/<tag>.json` (one, for a version the list no longer has) and `/releases/<tag>/<file>` (a redirect to the download). It asks GitHub with a token that only reads the repository (the Fly secret `TIDE_RELEASES_TOKEN`; `TIDE_RELEASES_REPO` in `docs/fly.toml`), and without one answers 503.
  - What it sends is its own JSON, in the shape tide always read (`tag_name`, `prerelease`, each asset's `name`, `size` and `browser_download_url`, which is the site's own address), never GitHub's, which has the owner's login, the repository's URLs and the release notes.
  - A download is GitHub's own link to the asset (the API's 302 for `Accept: application/octet-stream`), passed on: five minutes long, on `release-assets.githubusercontent.com`, with the repository's number and no name (looked at, 2026-10-07: the number resolves for nobody without access). So the bytes are GitHub's to send. The site keeps a link 3 minutes and the list 1, so a burst of upgrades costs GitHub a request or two, and it gives what it last had while GitHub is away.
  - A repository that's renamed or moved answers under its old name with a 301 to where it is now, on its API (`/repositories/<number>/...`). The list's requests follow redirects and went on working; a download's doesn't (`redirect: 'manual'`, to hand the file's link on), so every download answered 502 when the repository moved to `Tide-Engine/tide-engine` (2026-10-07) with `TIDE_RELEASES_REPO` still the old name. `link` follows such a move now, three at the most and only to GitHub's API, as the token goes nowhere else. The name in `docs/fly.toml` is still what a move has to change.
  - `tide upgrade` reads `https://tide-engine.dev/releases.json` (`TIDE_RELEASES` in `compiler/cli/upgrade.c`), sends no credentials and names no repository. `TIDE_RELEASES_URL` still points it at a list in a folder, for tests and for the installers.
  - The installers are the site's too (`/install.sh`, `/install.ps1`: the repo's files, copied into `docs/static/` as the site builds, `installers` in `docs/vite.config.ts`). Over a tide that's there they download and check the package themselves and hand it to that tide's updater as local files, whichever version it is: a tide from before this looks for releases at GitHub, where it finds none without access.
  - A tide installed before this can't upgrade itself any more unless it has access to the repository: running the installer again moves it over.
  - The request-access page (`docs/src/routes/request-access`) is still there and gates nothing; nothing links to it.
  - Tests: `docs/tests/releases.test.mjs` (a made-up GitHub: what goes out, what's cached, what's refused, GitHub away), which the Docs workflow runs; `tide_cli_installers` (both installers against a made-up site, on each platform's shell); `tide_cli_upgrade`.
  - Windows installer fixtures isolate module discovery to Windows PowerShell's stock `$PSHOME/Modules` and import its Utility manifest by absolute path (2026-10-10). Merely deleting `PSModulePath` allows user/vendor discovery, and supplying only the stock path still makes Windows PowerShell prepend the vendor directory at startup; the harness resets it before its first module command. Each case also has its own initially absent `PSModuleAnalysisCachePath`. Build run `38080715656` hit the first fixture's 30-second process guard in MinGW, with the next case taking 29.9 seconds before later cases ran quickly. Local probes confirmed the unwanted search paths but did not reproduce that hosted delay. All eight cases passed with isolated analysis caches from PowerShell 7 and Git Bash, including an unrelated inherited module/cache path, in 0.42–0.55 seconds per case. An independent probe with a poisoned vendor Utility module confirmed stock-only discovery and correct SHA256/JSON/file operations. Installer behavior and the 30-second guard are unchanged; process failures now include captured output for diagnosis.
  - Run `38081597158` passed all 2,044 MinGW tests, including the installer fixture in 12.07 seconds, but its native Windows job still hit the guard in the first two cases. The second had printed `Downloading...`, so module discovery alone does not explain the delay. That fixture started beside the rooms, asset and build-service tests; resource contention remains unproven. The harness now explicitly imports stock Management too and records wall/CPU times around module imports, mock requests/logging, the real SHA256 command and the unpack boundary. It verifies that the request/unpack commands resolve to its functions. On failure the assertion includes partial mock calls, available CPUs and the CTest parallel budget, and preserves the private fixture with stdout/stderr logs. Windows `TEMP`/`TMP` are contained in that fixture as well, including downloads left by a killed process. The 30-second guard, real checksum calculation and all installer assertions remain intact. All eight local cases passed in 0.33–0.40 seconds each; an injected 31-second mock stall hit the unchanged guard at 30.02 seconds and retained the last phase, three partial requests, stdout/stderr and both dummy downloads.
  - Run `38083005042` exposed an error in that new TEMP containment check: Node inherited the runner's `RUNNER~1` short path while PowerShell expanded its long spelling, so string comparison rejected the same private directory before installer execution. Containment now uses a marker stored inside the fixture's scratch directory, read through `GetTempPath`; its value identifies the expected fixture without comparing path spellings. A local NTFS 8.3 path reproduced the previous failure, and all eight cases passed with that same path after the fix, as they did with an ordinary long path. A deliberately different TEMP still fails and reports both actual and expected paths. This repairs the diagnostic fixture; the original hosted cold timeout remains unproven.
  - Final Build run `38085091225` (`0100b415`) passed the installer fixture on native Windows in 13.99 seconds and MinGW in 14.13 seconds, with the same 30-second guard and installer assertions. These passing results do not establish the cause of the original hosted cold timeout.
  - The cold timeout came back on 2026-10-14: Build `38623397915` (both of the first two cases, native MinGW), and `38637433618` (the first case on Windows release and on MinGW). The phase timings showed where it goes.
    - In one case, 8 s went to the first `ConvertTo-Json | Add-Content` and 13.6 s to the first `ConvertFrom-Json`, while the process used 0.03 s of CPU in all. Another case stalled in its first `Import-Module`.
    - So the time goes to Windows PowerShell's first loads of its assemblies on a fresh runner (modules, `System.Web.Extensions` for JSON, file and hash cmdlets), waiting on the disk and the machine's scanner, not on the installer. Every later case ran in about a second.
    - The fix: one warm-up process (a `before` hook in `installers_test.mjs`) runs the same module imports, a JSON round trip, `Add-Content`/`Get-Content` and `Get-FileHash` first. It has a 300-second guard of its own against a real hang, its failure fails the file, and it prints how long it took. The cases keep their 30-second guard and assertions, which then measure the installer.
    - Locally it takes 0.3 s and all nine cases pass in about 0.35 s each. The hosted cold cost should show in the printed warm-up time.
  - The site's built-in admin analytics records successful GET package redirects by version, platform and channel (`createReleases`' optional `onDownload` callback), including cached links. HEAD, checksums, metadata and errors are excluded. The callback runs independently of the response and cannot block a download; counts measure requests, not completed installs. The site's public release JSON and the CLI's contract stay the same (`notes/docs-site.md`, One private analytics dashboard).
- An upgrade never loses what's installed, and one that stopped says so (`compiler/cli/install.c`):
  - The package's `VERSION` is the installation's version, and an installation is whole when that's its tide's own (`tide_installation_whole`). `cmake/package.cmake` takes it from the native tree, whose tide it is: the other trees install one too, and aren't all configured with the version.
  - `VERSION` goes before anything of the installation changes and comes back last, and tide and tidels go just before it, after everything they build with. So an upgrade that stopped leaves the old tide, which still sees a newer version, or an installation without its `VERSION`. tide builds nothing with one that isn't whole (`build_setup`), `tide version` says so, and `tide upgrade` installs the version again even when it's the newest already. The installers run `tide upgrade` on an installation that's there, so running one again finishes it too.
  - Nothing goes before what replaces it is in its place: a folder moves to `.upgrade/old`, and back when the new one can't take its place.
  - Windows moves no folder while a program has anything in it open (an editor that shows an engine header watches the header's folder, a terminal is in one), so what's in such a folder is replaced one by one, and what the package has none of there goes. Only a file a program has open without sharing stops an upgrade, which names it and stops there. No retries, and nothing timed.
  - `tide_cli_tests` holds folders and files open as editors and compilers do (`compiler/cli/tests/install_test.c`, those cases on Windows only), and `tide_cli_upgrade` (`compiler/cli/tests/upgrade_test.cmake`) runs `tide upgrade` against releases in a folder (`TIDE_RELEASES_URL`), where the tide that upgrades replaces its own program.
- An upgrade that went well says one line, the version it had and the one it has now, and nothing else (agreed): not which editors it updated, nor where the release's notes are. In a terminal, a bar each for the download and the unpacking is drawn over itself above that line (`run_step` in `compiler/cli/upgrade.c`), so nothing waits unseen:
  - curl and tar run in the background, and what they say waits in a log until the bar is done with its line. One curl gets the checksums and then the package, so the download's bar, which follows the package's file as it grows, ends when both are there.
  - The unpacking's bar follows the bytes of the files unpacked so far, against what the package unpacks to: a zip's directory says (`unzip_size`), and a gzip's last four bytes, the size of its tar.
  - `tide editors --update`, which the new tide runs, says nothing unless an editor refuses the update, and that waits for the upgrade's line. While editors take it, a terminal says so on the line the upgrade's own then takes.
  - `tide_cli_upgrade` checks the line is all there is, where there's no terminal.
- `.github/workflows/build.yml` is the release workflow, and it runs only when asked to (`workflow_dispatch`, with no inputs: Run workflow on GitHub, or `gh workflow run build.yml --ref <branch>`); pushes and pull requests start nothing (the owner, 2026-10-08: "lets restore it to the proper release workflow except i want to trigger it manually"). Every run does the same: it builds each platform's package (Windows, Linux, macOS) and tries it as a user would (a game built natively, for the web and for Android, a game's C++, incremental builds; on Linux also the build service's page and the checks for Google Play), and runs every suite: native and web on all three, the MinGW target on Windows, Android on the emulator on Linux (KVM), whose image comes through the runner's SDK manager, and the suite once more on Linux under AddressSanitizer and UndefinedBehaviorSanitizer (the `sanitize` preset: see Language, no Tide code can corrupt memory). That job leaves two tests to the others: the relay's, which is Node's, and rooms with the runner's browsers, which nobody has run under sanitizers yet (without browsers it passes). The branch it runs on says what's published once all of that passed: `dev` a nightly pre-release (`0.3.0-nightly.48`), `release` a stable release, with semantic-release committing the new `VERSION` there and the JetBrains plugin going to Marketplace, and any other ref nothing, which is how a branch is checked. Windows and Linux build with the LLVM tide's built-in clang comes from (`TIDE_LLVM_VERSION`): LLVM's own release on Windows, and LLVM's apt packages of that major version on Linux. macOS builds native code with Apple's clang and web code with Homebrew's LLVM. Every platform's Node is 24 (`actions/setup-node`; see Building for why the runners' own won't do on Windows). A failure on any platform fails the run and publishes nothing, so every release has every platform's package and passed every suite; nothing in CI may swallow a failure (no `continue-on-error`). Native tests that need a GPU (label `gpu`) are skipped in CI: no runner can open their window.
  - The workflow has no modes, on purpose. In one day (2026-10-07, which began with no hosted minutes and the jobs on the owner's own machines) it had grown an `action` input (`check`, `nightly`, `stable`), a `scope` of the Windows web suite alone, nightlies that ran no suite, and a macOS with no suites; the owner had the workflow from before that day restored, with the manual trigger. A way to run less of it is the owner's to ask for.
  - A run on `release` uses that branch's own copy of the workflow, which is this one once `dev` is merged there for a stable release.
  - Every run also builds the docs site, and a run on `dev` deploys it: the `Docs` job, which calls `docs.yml` (`notes/docs-site.md`).
- Windows web CI uses Chrome for Testing's Headless Shell 155.0.8059.39, cached by its exact version and checked before configuring `TIDE_BROWSER`. Its offscreen compositor keeps ANGLE's WARP device for WebGPU without a native window or DXGI window swap chain. Unified Chrome failed creating that swap chain on the runner (`DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`), lost its GPU context and disabled compositing, which made canvas pixel snapshots transparent. The workflow records the Windows session ID. Failed served tests include Chrome's Windows subprocess log. The WebGPU flags and pixel assertions stay the same.
- The Linux package job frees emulator disk headroom (2026-10-12) by removing
  only the hosted image's unused .NET and Haskell SDKs (`/usr/share/dotnet`
  and `/usr/local/.ghcup`) before Android setup. It logs free space on the
  AVD's filesystem before and after. Android's SDK/NDK, Java, LLVM, caches,
  emulator partition size, tests and publication gates are unchanged.
  Run `38249496505` compiled Android successfully but its emulator exited
  before boot: 7,052.48 MB available versus 7,372.80 MB required for the
  6 GiB userdata partition. Run `38218458868` passed all 2,173 Android tests
  earlier that day with the same workflow and Ubuntu image
  `20261004.327.1`; restored caches were nearly the same size. The logs do
  not establish what consumed the additional space on the failing runner.
  Validation: actionlint 1.7.12 passes with only its existing unsupported
  `queue` key excluded. Parsed workflow comparison confirms the scoped cleanup
  is the only behavior added. The exact Bash block passes syntax and stubbed
  execution checks for its fixed deletion targets, before/after reports and
  failure propagation; no local SDK was deleted. Full Windows debug suites
  passed 2,250 native tests (349.26 s) and 2,218 web tests (379.77 s), including
  WebGL/WebGPU, with Node 24.16.0. `git diff --check` passes. Hosted disk
  reclamation and emulator boot still need a newly requested Build run;
  retrying the old run uses its old workflow. No CI run was dispatched.
- Web CTest parallelism follows Node's `os.availableParallelism()` (2026-10-10),
  overriding the matrix's four-job default for that step. GitHub's standard
  private-repository Windows/Linux runners have two CPUs and macOS has three.
  Browser fixtures registered with `tide_add_web_test` reserve the host's CPU
  count through `PROCESSORS` (`notes/testing.md`); matching the scheduler budget
  keeps software-rendered pages from competing with each other or WASI tests.
  Other suites keep their existing parallelism. No suites, assertions, browser
  flags, timeouts or publishing gates changed.
- Windows tool setup verifies the hosted image's Ninja and zstd instead of
  asking Chocolatey to install them again (2026-10-10). In run `38057969812`,
  the Windows package and MinGW jobs stopped before building because the
  Chocolatey `zstandard` metadata request timed out after 100 seconds. That
  exact image already had Ninja 1.13.2 and zstd 1.5.7; LLVM cache restoration
  had already run PATH-visible zstd successfully. Chocolatey installs ccache
  only when it is missing, and setup verifies all three tools. Missing tools
  and failed installations still stop the job; LLVM extraction is unchanged.
  Local Git Bash validation of the exact setup block covers ccache already
  present, a successful missing-ccache installation and an installation
  failure propagating its exit code. Chocolatey is isolated behind a stub;
  the real Ninja/zstd version commands run. Actionlint and whitespace checks
  pass, excluding only actionlint's existing unsupported `queue` key.
  The original `c4e5b0fd` run recovered with one failed-jobs-only retry:
  every required check passed and `v0.3.0-nightly.117` published all five
  assets. Successful jobs and their artifacts were reused. This preventive
  setup change was validated locally and landed after that release; the
  hosted retry used the original setup, and no new full workflow was started.
- The JetBrains build job disables `setup-gradle` caching (2026-10-10).
  Run `38049029745` exhausted the hosted runner's disk during Gradle setup:
  its check annotation reports `No space left on device` writing the runner's
  own diagnostic log, while `Build and verify` never started. The dependency
  and transform caches accessed at that time held 2.91 GB and 2.69 GB of
  compressed data, before extraction. `setup-gradle` v4 applies its
  [`gradle-home-cache-excludes` after restoring the entries](https://github.com/gradle/actions/blob/v4/sources/src/caching/gradle-user-home-cache.ts),
  so exclusions alone cannot prevent the existing cache from exhausting disk
  first. The job uses the supported
  [`cache-disabled` input](https://github.com/gradle/actions/blob/v4/docs/setup-gradle.md#disabling-caching)
  and downloads the wrapper and dependencies on demand; plugin compilation and
  verification stay the same. This trades warm-cache build speed for avoiding
  the failing restore. Actionlint passes with only the existing unsupported
  `queue` key excluded, and `git diff --check` passes. Run `38051684843`
  subsequently passed the cold build and verification in 6 minutes 10 seconds.
  Run `38057969812` failed resolving twelve plugin dependencies before
  compilation, with the same pinned IntelliJ plugin 2.19.0 and Gradle 9.8.
  Those artifacts were reachable again afterward, and an unchanged local
  Gradle/JDK 21 `help` run with an empty isolated Gradle home resolved them
  and passed in 1 minute 58 seconds. This was an upstream resolution failure;
  no plugin dependencies or repositories were changed. The owner authorized
  a failed-jobs-only retry of that run, reusing successful suites and artifacts.
  JetBrains passed unchanged on that retry in 5 minutes 33 seconds.
- Publishing uses the default `GITHUB_TOKEN` unless repository secret `TIDE_PUBLISH_TOKEN` is configured. The optional token belongs to `Tide-Engine`, selects this repository, and needs Contents and Workflows write permission: GitHub requires workflow authorization when the selected commit's workflows differ from the default branch's. Version checks that difference before building packages when the optional token is absent; Release checks again because the default branch can move during a build. This publishing token is separate from the docs site's read-only `TIDE_RELEASES_TOKEN`. Nightly retries fetch existing tag and notes refs, reject a tag for another commit, and push both refs atomically so a rejected tag leaves no new notes behind.
- **All CI jobs use GitHub-hosted runners** (the owner, 2026-10-09: "move the workflows back to pure github for now, not blacksmith"). Windows' package and suites run on `windows-2025`, Linux's on `ubuntu-24.04`, and macOS's (Apple Silicon) on `macos-latest`. The supporting jobs and Docs use `ubuntu-24.04` too. Both workflows use `actions/checkout@v4`, install their tools and restore their caches on fresh checkouts. Manual triggers, suites, publishing gates and `TIDE_HIDDEN` stay as before. The former self-hosted runners' notes were removed on 2026-10-21 (git history, `bf647de4`); `.github/runner/setup-linux.sh` remains for the Linux cross build's xwin setup.
  - Validation: both workflows parse as YAML, a comparison against their previous versions confirms only runner labels, checkout actions and the runner comment changed, and `git diff --check` passes. `actionlint` 1.7.12 passes with only its unsupported existing `queue` key excluded; `queue: single` is supported by [GitHub's concurrency documentation](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency). The hosted environments still need a manually requested workflow run; none was dispatched for this change.
- Manual Build runs on `dev` and `release` finish the current run and keep only the newest pending request; a newer manual request replaces the older pending one. The Build workflow says `queue: single` explicitly in each ref's concurrency group, with `cancel-in-progress: false`. Nightlies are previews published when requested, with no nightly for every intermediate commit (the owner, 2026-10-07).
- **Windows' package can also be built on Linux** (built 2026-10-07 for the former home-server runner). CI builds it on Windows, and the workflow has no step for the cross build any more: it's done by hand, with the presets below. The cross-build support remains:
  - Its `tide.exe` and `tidels.exe` are for Windows' own target (`x86_64-pc-windows-msvc`), as on Windows, since tide links LLVM's Windows libraries and so Microsoft's C++ runtime: the `windows-package` preset, which is `package` with `cmake/windows-msvc-toolchain.cmake`. clang takes Microsoft's headers and libraries from a folder xwin made (`-Xmicrosoft-windows-sys-root`, `TIDE_XWIN` or `~/xwin`), laid out as a Windows machine has them, so they come after clang's own headers, as system headers: given with `-isystem`, Microsoft's `vadefs.h` came before clang's and `va_arg` was a warning. They're Microsoft's, under Visual Studio's license, which `.github/runner/setup-linux.sh` accepts when told to (`TIDE_ACCEPT_MICROSOFT_LICENSE`): never in the repo or the package, whose programs have the C runtime linked in, as they always had.
  - CMake's own setup for that target wants LLVM's tools named: `llvm-rc`, `llvm-mt`, `lld-link`, and `llvm-ar` (with clang's GNU-style driver CMake writes `ar`'s arguments, which `llvm-lib` doesn't take). Resources are preprocessed without the C flags, so the toolchain gives their tool Microsoft's folders itself (`CMAKE_RC_FLAGS_INIT`): libxml2's has one.
  - `cmake/LLVM.cmake` takes the release of the system tide is built for (`WIN32`, `APPLE`), where it took the build machine's, and a cross build runs a native tidec as web builds do (`CMAKE_CROSSCOMPILING` in the top `CMakeLists.txt`). `mingw-release` configures anywhere (off Windows `cmake/mingw-toolchain.cmake` says it's a cross build; llvm-mingw's files were the Linux archive's already), and `cmake/package.cmake` tells Windows' package by its name, with `-DNATIVE=build/windows-package`.
  - When `windows-x64` is built on Linux, it uses MinGW's tree (`mingw-release`), then `windows-package`, then the web's and Android's trees, and `cmake -DNAME=tide-windows-x64 -DNATIVE=build/windows-package -P cmake/package.cmake`. Such a package can only be tried on Windows.
  - That cross-built package was checked once by hand (2026-10-07): the package the server made has the 2,417 files of that day's nightly, with programs within a twentieth of a percent of their sizes, and on Windows its tide built the demo natively and for the web. CI tries each Windows package on Windows.
- **A nightly waits for every suite, as a stable release does** (the owner, 2026-10-08, with the workflow's restoring, above). For a day they didn't (2026-10-07: "dev/nightly is meant to move fast; we can introduce them back later, right now its too early for always running it", said while every push started a run and the suites ran on a home server): nightlies went out from the three packages alone, and one could go out with a suite failing somewhere this machine's sessions didn't run it. A run is asked for now, and Windows' and Linux's packages and suites together took about four minutes on Blacksmith's runners (2026-10-08, with the compiler's cache).
  - `gh workflow run build.yml --ref dev` is a nightly, `gh workflow run build.yml --ref release` a stable release, and the same on any other branch checks it and publishes nothing.
  - macOS's suites (native and web) are back with it. They ran on no machine from 2026-10-07 until the first run after this, so what landed in between (audio's CoreAudio, Box3D's resident state) was first run on a Mac then.
- **A nightly is published by the workflow itself, not by semantic-release** (2026-10-07): semantic-release publishes nothing for a commit that isn't its branch's newest any more ("The local branch dev is behind the remote one"), and with several sessions pushing, `dev`'s rarely is by the time its packages are built: runs passed every job and released nothing. `.github/next-nightly.mjs` says the version in the Version job (the highest tag's, one further: `0.3.0-nightly.7` after `.6`, `0.4.0-nightly.1` after a stable `0.3.0`), and that nothing is published for a commit with no `feat`, `fix` or `perf` since the last tag behind it, or that's itself behind a tag. The Release job tags the commit, adds the note semantic-release keeps for each of its tags (`refs/notes/semantic-release-<tag>`, `{"channels":["dev"]}`), and makes the release with `gh`, which the runner has. A stable release (`release`) is still semantic-release's, start to end. The workflow's `concurrency` group is what keeps two runs from taking one number.
  - Nightlies use commit-based release notes again (2026-10-10, future releases
    only). Switching the publisher to `gh --generate-notes` had replaced the
    Features/Bug Fixes lists with GitHub's merged-PR notes; direct commits to
    `dev` left only a comparison link. `.github/nightly-notes.mjs` calls
    `@semantic-release/release-notes-generator` directly with the stable
    release's options from `.releaserc.json`, without semantic-release's
    branch-tip check or publishing steps. Its dependencies are pinned in
    `.github/package-lock.json`; `npm ci --prefix .github --legacy-peer-deps`
    omits the unused semantic-release peer.
  - The notes cover the highest lower-version tag reachable from the built
    commit through that exact commit, including merged work and full commit
    bodies. An existing target tag on a retry never becomes its own baseline,
    and later branch-tip commits or higher-version tags cannot enter the
    notes. `gh release create --notes-file` uses the result; the existing-release
    branch still only verifies its assets and never edits published notes.
    The Version job tests temporary Git histories before any packages build:
    snapshot and baseline selection, merges, retry tags, scoped changes and
    links, breaking headers and footers, revert pairs and a first release.
    Local Windows and WSL tests, a preview of the `.116` to `.117` history,
    actionlint (with the existing `queue` exception) and whitespace checks
    validate this change; no Actions run was started for it.
