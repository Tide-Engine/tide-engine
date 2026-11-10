# Android and MinGW builds

Unity's Android players (2026-11-10): `tide unity --android` links the game with the embed's host as
`libtide_game.so` for each CPU, through `link_android` with no activity entry and no `exit` wrap, the pack inside
the library. Built and packaged by Unity, not yet run on a device. `notes/unity.md`, Android players.

Vulkan only (2026-11-05): OpenGL ES 3 and EGL went with the other GL backends
(`notes/rendering.md`, GL backends removed). `activity.c` links neither `EGL`
nor `GLESv3` (nor does `tide build --android`), and the intent's `tide.backend`
extra is gone. A device without Vulkan 1.3 and what the renderer needs gets a
screen saying so (Claude's pick, Provisional): `say_needs_vulkan` draws the
message with the CPU straight into the activity's window (`ANativeWindow_lock`,
RGBA 8888, the regular Tuffy weight from the font atlas, word-wrapped), again
for each window the activity is given, until the player leaves (back, which is
Escape, or the activity going); then the window doesn't open and the app ends.
The emulator with `-gpu swiftshader_indirect` (CI's package job's) has Vulkan
1.2, so apps show that screen there; it posts a frame, which is what CI's
Google Play check (`am start -W`, then the activity resumed) needs. The
renderer's apps (`tide_platform_draw_app`, `_targets_app`, `_shaders_app`)
need a Vulkan 1.3 device: the emulator booted with `-gpu lavapipe` (CI's
Android job's) or `-gpu host`, or a phone. Since 2026-11-05 they're CTests
(An app's end, below).

Tried on this machine's emulator (x86_64, API 36), the android-debug build:
- `-gpu swiftshader_indirect`: `demo.apk` logged "Vulkan: no device has Vulkan
  1.3, the newest being 1.2" and the message, `am start -W` said `Status: ok`,
  the activity stayed resumed and its screen showed the message (captured with
  `adb exec-out screencap`). Back ended the app.
- `-gpu host`: `demo.apk` drew with Vulkan, and `tide_platform_draw_app` passed
  its checks (3,377 `ok`s, no `FAIL`) on Vulkan.
- Both apps' exits ended with `FORTIFY: pthread_mutex_lock called on a
  destroyed mutex` and a SIGABRT in Android's own `hwuiTask0` thread as
  `exit()` ran. Fixed the same day: An app's end, below.
- `apk.c` no longer declares the OpenGL ES 3 `uses-feature`; it declares
  Vulkan 1.3's (Google Play's filter, below).

## An app's end (2026-11-05, the owner's task; Claude's picks, Provisional)

- **Root cause of the abort at every app's exit:** the program's thread called
  `exit()` (`run_program` after `main`, `platform.c`'s `exit(code)` at a
  game's end, `exit(1)` where the window can't open) while the process's
  threads of Android's own still ran. `exit()` runs `__cxa_finalize(NULL)`:
  every library's static destructors, libhwui's and ART's too. HWUI's worker
  threads (`hwuiTask0`, `hwuiTask1`: its common pool, in every app with a
  window) then locked a mutex those destructors had destroyed, and bionic's
  FORTIFY aborted (SIGABRT); ART's `mutex.cc` said "destroying mutex with
  owner or contenders" too. It's a race: sometimes `_exit(0)` got there first,
  and Zygote said "exited cleanly (0)" (and `dumpsys activity exit-info`
  EXIT_SELF, status 0) though the crash buffer had the abort; sometimes the
  signal did (134). No tombstone: debuggerd's dump failed as the process went.
  Nothing to do with the message screen or `ANativeWindow_lock`: the draw app
  ended the same way. Android's own app processes never end through `exit()`.
- **Fix:** an app links with `-Wl,--wrap=exit` (`tide_android_app` in
  `platform/CMakeLists.txt`, and `tide build --android`'s link in
  `compiler/cli/build.c`), so every `exit()` in the game's library (the
  platform layer, the game's C and C++, packages linked into it) is
  `__wrap_exit` in `activity.c` (hidden, not exported):
  1. `__cxa_finalize(&__dso_handle)`: the game library's own exit handlers
     (bionic's `atexit` registers with the caller's DSO handle) and C++
     destructors, as `exit()` would run them; none of the system's.
  2. `app.running = false` (so a callback taking the window or input queue
     away doesn't wait for the program), `ANativeActivity_finish`, then a wait
     for `onDestroy` (which now broadcasts `app.taken`): the activity goes
     before the process. No wait on the activity's own thread, which couldn't,
     or with no activity.
  3. `end_output`: `fflush(NULL)`, `/dev/null` over stdout and stderr so the
     log pipe's writing ends close, and a join of the log thread (joinable
     now), so the program's last lines reach the log.
  4. `_exit(code)`. A second thread calling `exit()` meanwhile waits (`pause`)
     for the first to end the process.
  - `run_program` is `exit(main(...))` now; `tide_window_close` still
    finishes the activity, as before.
  - Not covered: `exit()` called from another shared library a game ships
    (only the game's own library is linked with the wrap), and aborts and
    signals (crashes, as anywhere).
- **The check, `run_android.mjs --app <package> [--back] [--vulkan] <apk>
  [arguments...]`:** installs the APK (replacing one signed with another
  checkout's key: on `INSTALL_FAILED_UPDATE_INCOMPATIBLE` it uninstalls it
  first), notes the device's clock (`date +%s.%N`, for `logcat -T`), starts
  the activity (`am start -W`, the arguments as `tide.args`), presses back
  once it drew with `--back`, then reads the log from then on: the pid from
  the activity manager's "Start proc <pid>:<package>/", the end from Zygote's
  "Process <pid> exited cleanly (N)" or "exited due to signal S" (128 + S).
  It prints the process's `tide` lines (its output), and fails the run, with
  the lines, when any line of that pid is fatal (`F`, as libc's FORTIFY and
  "Fatal signal" are), whatever the code: Zygote's word alone missed the
  abort above. It uninstalls the app at the end. `--vulkan` stops and fails
  the app at once, saying how to boot the emulator, when it logs that it
  needs Vulkan 1.3, rather than waiting on the screen until the test's
  timeout (Vulkan in CI, below, for why not the device's features). Its limit
  is the test's timeout. The
  log is read once the process ended, so a run printing more than the log's
  ring holds would lose lines (the draw app's 3,400 fit on the emulator).
  `tide_android_runner` tests it with adb mocked: a clean end with its output
  and the uninstall, `--back`, the exit code, a fatal line failing a code of
  0, another pid's crash left alone, a signal's 134, the reinstall, and
  `--vulkan` stopping an app that says it needs 1.3 (another pid's saying so
  left alone), and waiting for one without it.
- **Tests** (`tide_android_app_test(<target> [BACK] [GPU])` in
  `platform/CMakeLists.txt`, RUN_SERIAL as an app takes the screen, 600 s):
  `tide_platform_app_exit` (`platform/tests/app_exit.c`, BACK: opens the
  window, closes it and returns 0, on any device; on one without Vulkan 1.3
  after the screen), and, labelled `gpu` (`--vulkan`),
  `tide_platform_draw_app`, `_targets_app` and `_shaders_app`, until then
  started by hand.
- **Checked on this machine's emulator (x86_64, API 36, android-debug):**
  - `-gpu host`: the unfixed draw app aborted at exit as above (reproduced);
    fixed, `tide_platform_app_exit` and `tide_platform_draw_app` (3,399
    `ok`s) ended cleanly with 0. The exit app linked without the wrap, on
    purpose: the runner failed it with the FORTIFY lines and "Fatal signal 6
    (SIGABRT) ... (hwuiTask1)", code 134.
  - `-gpu lavapipe`: the exit app, the draw app (3,380 `ok`s, its lost-device
    pass included), the targets app ("target sampling passed") and the
    shaders app (2,081 `ok`s) ended cleanly with 0. Before they were rebuilt
    with the fix, the targets and shaders apps' runs failed on the crash at
    their end. The whole suite: Vulkan in CI, below.
  - `-gpu swiftshader_indirect`: Vulkan in CI, below.
- `llvm-nm -D` of a linked app shows no `exit` imported, only `_exit` and
  `__cxa_finalize`.

## Google Play's filter: Vulkan 1.3 (2026-11-05)

- The manifest (`manifest_nodes` in `compiler/cli/apk.c`, so both the APK's
  binary XML and the bundle's protocol buffers) has
  `<uses-feature android:name="android.hardware.vulkan.version" android:version="0x403000" android:required="true"/>`.
  Android's documentation (the `uses-feature` element's page, Vulkan hardware
  features) gives the form, with `0x400003` for Vulkan 1.0, and
  `PackageManager.FEATURE_VULKAN_HARDWARE_VERSION` says the version is the
  highest `VkPhysicalDeviceProperties::apiVersion`: `major << 22 | minor <<
  12`, so 1.3 is 0x403000. The emulator on `-gpu host` and on `-gpu lavapipe`
  says `android.hardware.vulkan.version=4206592` (0x403000) in `pm list
  features`. Play leaves out a device without the feature or with a lower
  version. Android itself installs the APK anywhere (the page: "informational
  only"), so a sideload still shows the screen.
- `android:version`'s resource ID, 0x01010519, is in neither AOSP's
  `public-final.xml` nor `public-staging.xml` (checked), and this machine has
  no SDK platform: it was read from the emulator's
  `/system/framework/framework-res.apk` (Android 16), whose `resources.arsc`
  gives the IDs `apk.c` already had for `name`, `required`, `minSdkVersion`
  and `enableOnBackInvokedCallback` the same.
- `android.hardware.vulkan.level` isn't declared: level 0 is any hardware
  Vulkan, which a `vulkan.version` already implies (the version is of the
  devices at the device's level), and level 1's optional features aren't
  Tide's requirements (`notes/rendering.md`, Vulkan: a device without those
  is passed over, saying why, and the screen says so).
- Checked: bundletool 1.18.3 (CI's pin) `validate`s a bundle `tide_apk` made,
  and `dump manifest` reads the feature back (`android:version="0x00403000"`).
  `build-apks --connected-device` and `install-apks` installed it, and it
  started and stayed resumed, on `-gpu lavapipe`. CI's Google Play step now
  also checks the bundle's manifest says 0x00403000 (`bundletool dump
  manifest --xpath`). `tide_cli_tests`' `apk_manifest_is_binary_xml` checks
  the attribute's ID in the resource map, the feature's name and the hex
  value; `apk_bundle_is_signed_as_a_jar` the name in the bundle's manifest.
  On `-gpu swiftshader_indirect` too, bundletool installed it, it started and
  stayed resumed, showing the screen. The emulator can't show Play's filter:
  its `android.hardware.vulkan.version` is 0x403000 whatever its Vulkan is
  (below).

## Vulkan in CI: the emulator's lavapipe (2026-11-05)

- The emulator (37.2.12 here, the SDK's current one) ships two software
  Vulkans for the host side of its gfxstream: SwiftShader
  (`lib64/vulkan/vk_swiftshader`, the device says 1.2.0, "LLVM 10.0.0", no
  dynamic rendering) and Mesa's lavapipe (`libvulkan_lvp`, ICD api_version
  1.4.318, llvmpipe "LLVM 21.1.2, 256 bits", with its own `libLLVM`).
  `-gpu lavapipe` ("Lavapipe software renderer for Vulkan and auto-select
  software backend for GLES", `emulator -help-gpu`) picks lavapipe
  (`vulkan_mode_selected:lavapipe`, GLES on SwiftShader), with no GPU and
  headless (`-no-window`). The device then has Vulkan 1.3 as Tide needs it:
  everything Tide asks of a device was there, the draw, targets and shaders
  apps passed (An app's end, above).
- On it, with `-j 4`, the whole android-debug suite passed, 2,927 tests in
  508 s (2026-11-05), the four app tests among them (draw 12 s, exit 2 s,
  targets 9 s, shaders 31 s). The build was warning-free.
- Wired: CI's Android job boots with `-gpu lavapipe` (`check.yml`, Start the
  Android emulator: `inputs.preset == 'android'`), so its `ctest --preset
  android-debug` runs the `gpu` apps; the package job keeps
  `-gpu swiftshader_indirect`, so its Google Play check still shows the
  screen saying Vulkan 1.3 is needed. **Not yet run on a hosted runner:** the
  Linux emulator package (`emulator-linux_x64-16428233.zip`, 37.2.12) has the
  same mode in `-help-gpu` and `lib64/vulkan/libvulkan_lvp.so` with its own
  `libLLVM.so` and `lvp_icd.json` (1.4.318), checked by unpacking it under
  WSL, but WSL here has no `/dev/kvm` to boot it. The next Build run is the
  check; if lavapipe fails there, the job's gpu tests fail at once (below)
  and say how.
- The emulator's `pm list features` gives
  `android.hardware.vulkan.version=4206592` (1.3) and `vulkan.level=1` on
  SwiftShader too, whose device is 1.2: the system image's features are
  fixed, not the host renderer's. So `run_android.mjs --vulkan` goes by the
  app's own word ("needs Vulkan 1.3" in its `tide` lines, from
  `say_needs_vulkan`): it stops the app (`am force-stop`) and fails within a
  second, saying to boot with `-gpu lavapipe` or `-gpu host` or leave out
  `gpu`. On SwiftShader that's what the three `gpu` apps did, while
  `tide_platform_app_exit` passed through the screen and back; the demo,
  through the screen and back, ended with 1 (`platform.c`'s `exit(1)` where
  the window can't open) and no crash.
- Not tried: `-gpu swangle_indirect` (ANGLE on SwiftShader for GLES, the same
  SwiftShader Vulkan, so 1.2), `-gpu guest`, a newer system image. lavapipe
  made them unnecessary.

CLI modes (2026-10-30): --release is optimized with symbols and debuggable APKs,
using a default app ID if needed, and links with --gc-sections. --distribution
owns symbol stripping, shipping exports, appId requirement and AAB creation. The CLI builds warning-free under Windows ASan;
native/web artifact/C++ coverage passes. Android packaging/device execution
for this split remains CI coverage; details: cli-and-packages.md.

Shipping export policy (2026-10-27): Release/MinSizeRel CMake apps and standalone
CLI --distribution builds use platform/android/game.exports, exposing only
ANativeActivity_onCreate from libgame.so. That file installs as
lib/android/game.exports for both ABI builds. Other game-supplied .so files keep
their own exports. Debug/profiling and host/embed APIs are unchanged. No activity
code, JNI callback, package signature format or runtime function changed.
Cross-link and actual packaged-library validation: cli-and-packages.md.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Building

### MinGW builds

- On Windows, the `mingw-release` preset builds everything for the MinGW-w64 target (`x86_64-w64-windows-gnu`, with the UCRT), which tide builds Windows games for (see Packaging and releases): `cmake --workflow --preset mingw-release`. Everyday Windows builds keep Visual Studio's target.
- Its headers, C runtime and compiler runtime come from llvm-mingw, downloaded once into `build/llvm-mingw-<version>`, pinned (`cmake/mingw-toolchain.cmake`). clang is the usual one, linking with lld.

### Android builds

- The `android-debug` and `android-release` presets build everything for Android on x86_64, the emulator's, and `android-arm64` for phones and tablets: `cmake --workflow --preset android-debug`. They're clang with Android's NDK (`cmake/android-toolchain.cmake`), whose C library, headers and compiler runtime Google's license keeps out of this repo and the package: the build finds the NDK installed (ANDROID_NDK_HOME or ANDROID_NDK_ROOT, or the newest in the SDK: ANDROID_HOME, ANDROID_SDK_ROOT, or where Android Studio puts it). Programs run on Android 10 (API 29) and up, the first with thread-locals of its own, and with 16 KiB pages.
- Tests run on a device or emulator through adb (`cmake/run_android.mjs`): each program goes to a folder of its own under `/data/local/tmp`, so they run at once, with the `TIDE_` variables the test sets. A test that shares a file between processes names it with `--fixture <filename>` before the program: the wrapper copies it from the host working directory when present, and back after a successful run, before removing the device folder. Box3D's join test uses this for its snapshot; `tide_android_runner` tests the transfers and failure cleanup against mocked adb, without a device. The emulator runs x86_64 on x86_64 machines; arm64 needs a device (or an arm64 machine). Games generate their C with a native tidec, as web builds do.
- A game that opens a window (no `HOST`, or `HOST` with `WINDOW`) is an app: a shared library in `<target>.apk` (`tide_android_app`), which Android's own NativeActivity loads, so apps have no Java. The platform layer's window there is the activity's (`platform/android/activity.c`): the activity's callbacks run on Android's main thread, and the program's `main` on a thread of its own, which takes what changed (its window, its input queue) through its looper; a callback that takes something away waits until the program let it go. It draws with Vulkan where the device has what the renderer needs (see Vulkan on Android, below; until 2026-11-05 else with OpenGL ES 3 through EGL, and now a screen saying what's needed), in logical pixels (the window's over dpi / 160). Keys are by position, the back button is Escape (as in Unity), a mouse is the mouse, and fingers are touches. Its stdout and stderr go to the system's log (`adb logcat -s tide`).
- The renderer's pixel checks run as an app there too, `tide_platform_draw_app.apk` (`platform/tests/draw.c`, which keeps to the screen's top left corner whatever its size): its test (label `gpu`, since 2026-11-05: An app's end, above) installs and starts it (by hand: `adb shell am start -n dev.tide.tide_platform_draw_app/android.app.NativeActivity`), and it prints them to the log. `tide_platform_targets_app.apk` and `tide_platform_shaders_app.apk` are the targets' and the shaders' checks the same way (`targets.c`, and `shaders.c` with `shaders.tide`, whose frames are a window of their own size through `tide_platform_read_pixels_as`, whatever the screen's): OpenGL ES 3 compiled what tidec wrote there (until 2026-11-05), which is how the loop above was found; now the device's Vulkan takes tidec's SPIR-V.
- **Vulkan on Android (2026-10-14, after the owner's "let's close those gaps"; Claude's picks, Provisional).**
  - `activity.c` opens `libvulkan.so` with `dlopen` rather than linking it, so a device without it can still show what's needed (until 2026-11-05 it ran the app with OpenGL ES). It waits for the activity's first window, opens Vulkan on it with `VK_KHR_android_surface` (`platform/third_party/vulkan/vulkan_android.h`), and (until 2026-11-05) fell back to EGL where `tide_gpu_vk_open` found no device that would do, with the intent's `tide.backend` extra setting `TIDE_BACKEND`; both are gone.
  - Games carry SPIR-V on Android too: `TIDE_SPIRV` is on everywhere but the web, so `gpu_vk.c` and the renderer's generated `shaders_spirv.c` build for Android.
  - When the activity takes its window away (the app goes to the background) or gives one back, `follow_activity` calls `tide_gpu_vk_surface_changed`. What was recorded goes to the GPU, the swapchain and surface go, and a surface is made for the new window. With none, frames draw nothing, as a minimized desktop window's do. The device and everything made on it stay.
  - A screen turned from how it's made reports a rotated `currentTransform`. The swapchain is then made at the window's own size with the identity transform, so the system turns the image as it shows it (`vk.turned`). Vulkan then answers acquires and presents with `VK_SUBOPTIMAL_KHR`, which means just that, so it neither remakes the swapchain nor counts as a lost device. That last part was a bug at first: the image was drawn upright, but the device was made again every frame, at 4–6 fps. With the fix it's 55 fps.
  - Many Android drivers have no `VK_KHR_push_descriptor`, and neither does the emulator's. So a device without it gets descriptor sets: from pools of 256 sets of the frame's own, written and bound for each draw, and reset with the frame's room once its fence says the GPU is done (`frame_set` in `gpu_vk.c`). `TIDE_VULKAN_DESCRIPTOR_SETS` forces this on a device that has push descriptors, and the desktop test `tide_platform_draw_vulkan_sets` runs the draw checks that way under the validation layer.
  - Tested on the emulator (x86_64, API 36), booted with `-gpu host`. With `-gpu swiftshader_indirect` its Vulkan is 1.2 with no dynamic rendering, and Tide fell back to OpenGL ES (now it shows the screen saying it needs Vulkan 1.3). With `-gpu host` it's the host GPU's 1.3, through gfxstream, with no push descriptors.
    - `tide_platform_draw_app` passes all 1,447 checks on Vulkan, and again after losing the device on purpose: 2,850 `ok`s.
    - `tide_platform_shaders_app` passes 1,310 checks, and `tide_platform_targets_app` says "target sampling passed".
    - With OpenGL ES forced (history: removed 2026-11-05), the draw app passed its 1,447.
    - The demo, with Vulkan forced, went to the background and back twice and kept drawing. Turned to landscape, it drew upright at the window's new size, before and after the fix above. The emulator's own screenshots (`adb exec-out screencap`) showed each.
  - Not tested: a phone. A real device's rotation, its drivers, and an arm64 build all need one. There's no validation layer on Android: the same paths run under it on desktop.
  - When Vulkan could not run (history: before 2026-11-05): the default run fell back to OpenGL ES, then waited for a window event that never came, because its loop polled before it checked for the surface it had already made. It now checks first.
- In the background an app has no window: frames come when the session asks and draw nothing (the frame loop's wait for a window nobody sees: `tide_window_unseen`, `tide_window_wait`), until Android freezes the app, as it does to apps in the background. Its match goes on through host migration, when the game has it (see Platforms).
- `tide_apk` (`compiler/cli/apk.c`, built natively like tidec) makes and signs apps with no tool of Google's: the binary manifest (Android's attribute IDs, from its SDK's `public-final.xml`), a resource table (`resources.arsc`) that only names the icon (the game's `icon.png`, or Tide's, `lib/android/icon.png` in the package), the libraries stored on 16 KiB pages, which Android maps from the APK, and APK Signature Scheme v2. Apps are made for Android 16 (API 36), as Google Play requires, and opt out of its predictive back (`enableOnBackInvokedCallback`), which would keep the back button from the game.
- Given a `.aab`, it makes an App Bundle for Google Play instead: the same files as bundletool lays them out, with the manifest and resource table as aapt2's protocol buffers (`Resources.proto`) and a `BundleConfig.pb` that keeps libraries stored on 16 KiB pages, signed as a JAR (`META-INF/TIDE.SF` and its PKCS #7 signature). `tide build --android --distribution` makes one beside the APK.
- Play takes upload keys of RSA only, so apps are signed with RSA of 2048 bits, PKCS #1 v1.5 (`compiler/cli/rsa.c`: keys, primes and signatures, from RFC 8017), and the key is a PEM file of a PKCS #8 key and its self-signed certificate (`compiler/cli/sign.c`), which OpenSSL and Java read. `tide_cli_tests` checks apps as Android does before installing one, bundles as Java checks a JAR, and RSA against a key and signature of OpenSSL's. Every build of this repo signs its apps with `build/android-debug.pem`, made once, so a new build installs over the last: `adb install -r build/android-debug/bench/sand.apk`.
