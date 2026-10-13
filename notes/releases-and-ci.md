# Releases, installing and CI

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

Build run 38367850012 (2026-10-13, `dev` at b08fc1bc) failed three ways, fixed the same day:

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
  - Final Build run `38085091225` (`ea509d62`) passed the installer fixture on native Windows in 13.99 seconds and MinGW in 14.13 seconds, with the same 30-second guard and installer assertions. These passing results do not establish the cause of the original hosted cold timeout.
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
  The original `6a8c9e24` run recovered with one failed-jobs-only retry:
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
- **All CI jobs use GitHub-hosted runners** (the owner, 2026-10-09: "move the workflows back to pure github for now, not blacksmith"). Windows' package and suites run on `windows-2025`, Linux's on `ubuntu-24.04`, and macOS's (Apple Silicon) on `macos-latest`. The supporting jobs and Docs use `ubuntu-24.04` too. Both workflows use `actions/checkout@v4`, install their tools and restore their caches on fresh checkouts. Manual triggers, suites, publishing gates and `TIDE_HIDDEN` stay as before. `.github/runner/README.md` records the former self-hosted runners, which neither workflow selects.
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
