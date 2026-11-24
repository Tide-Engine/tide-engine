# iOS

Tide on iOS is a game inside Unity's player only (owner, 2026-11-19): Unity owns the window, the drawing (Metal) and
the audio device, and Tide is the simulation, tide/embed.h's host, files and networking, linked into the player.
There is no standalone iOS Tide app, no window backend and no Metal backend of Tide's own. The owner has an iPhone
and no Mac (2026-11-19: "i dont have a mac i only have an iphone", "feel free to use github actions"), so everything
that needs a Mac runs on CI's macOS runners. The Unity side (the package, `tide unity --ios`, the path to a phone)
is in `notes/unity.md`, iOS players.

## The build (2026-11-19; the agent's picks, Provisional)

- **`cmake/ios-toolchain.cmake`**, presets `ios-simulator` (arm64, the simulator on an Apple-silicon Mac,
  RelWithDebInfo) and `ios-device` (arm64, `iphoneos`; builds only: its programs aren't signed). `CMAKE_SYSTEM_NAME`
  is CMake's own `iOS`, the SDK is found by name (`TIDE_IOS_SDK`), and the compiler is **Xcode's clang**
  (`xcrun --find clang`), not the LLVM the other Mac jobs use: another clang has no iOS runtime to link programs
  with, and Xcode's is what compiles the same sources in a Unity player's Xcode project, so its results are the ones
  to pin. Deployment target 15.0 (`CMAKE_OSX_DEPLOYMENT_TARGET`). Programs are plain ones, not app bundles
  (`CMAKE_MACOSX_BUNDLE OFF`).
- **`TIDE_IOS`** (CMakeLists.txt) builds the engine, the compiler's and the engine's tests (games' C comes from a
  native tidec, `cmake/HostTidec.cmake`, as web and Android builds') and the platform layer, and stops there: no
  tools, demo, bench or examples, which open windows or are desktop hosts' (the packages are configured, for their
  physics tests). Wherever a CMake file says
  "desktop" as `NOT TIDE_WEB AND NOT TIDE_ANDROID`, it now also says `AND NOT TIDE_IOS`.
- **The platform layer with no window of its own** (`platform/embedded/window.c`, `audio.c`; ownership:
  `notes/runtime-ownership.md`, top): every `src/window.h` function as a build inside another program has it (no
  window opens, nothing is held, no pack beside a program), and no audio device. `src/gpu_vk.c` is left out: what
  the game draws is written down by `gpu_record.c` for the other program. Everything else of `platform/src` and
  `platform/src/rtc` compiles for iOS as it is, through its `__APPLE__` branches: files, UDP, the WebRTC stack with
  Secure Transport's TLS, threads, the embed. Frameworks: Security and CoreFoundation. Nothing in `platform/embedded`
  is iOS's: a console's build would take the same two files.
- **Tests run in a booted simulator**: `CMAKE_CROSSCOMPILING_EMULATOR` is `cmake/run_ios.sh`, which starts a
  program with `xcrun simctl spawn`. The simulator gives a program its own environment and working directory, so
  the script hands on a test's `TIDE_` variables as `SIMCTL_CHILD_<name>` and starts the program through
  `tide_ios_run` (`tests/ios/ios_run.c`: `chdir`, then `execv`). The simulator shares the Mac's files, so paths are
  the same inside it. `TIDE_IOS_SIMULATOR` names the device (`booted` by default).
- **Which tests** (`cmake/ios_tests.cmake`; target `tide_ios_tests`, and `build/ios-simulator/ios_tests.txt` is
  ctest's `-R`): `tide_tests` whole (with `tests/test_crossplatform.c`), the platform layer's windowless ones
  (`platform/tests/ios.cmake`, each a copy of the test of its name in `platform/CMakeLists.txt`: migrate, jobs,
  embed, travel, embed_shards, font, audio, io, rtc, rtc_fuzz, udp), and 26 e2e fixtures that pin a simulation's
  bits or run a match (crossplatform, crossplatform_text, random, random_local_order, random_net, rolls, math,
  double, float_bits, vector_equality, edge_cases, transforms, net, predict, delta, handover, skinned, baked_poses,
  retargeting, storage, shards, visibility_repairs, tasks, parallel, sort, maps). Not every e2e fixture: Apple's
  clang takes minutes over some games' generated C (`notes/releases-and-ci.md`, why the Mac jobs use LLVM).
- **CI**: Build's `Test (macos-arm64, ios-simulator, 1)` (`check.yml`, Test (iOS simulator);
  `.github/ios-simulator.sh` boots the newest iPhone the image has). It builds `tide_ios_tests`, runs them in the
  simulator, then builds `tide` and `tide_platform` for `ios-device`.
- **The physics packages' tests too** (`packages/` is in an iOS build for them): `tide_box2d_stack`, `_course`,
  `_complete`, `_rewind` and `tide_box3d_stack`, `_course`, `_complete`, `_rewind`, `_joints`, `_mesh`, whose
  pinned hashes are the other platforms': a Unity iOS game with physics compiles that C with the same compiler.
- **Checked** (2026-11-19, scratch branch): Xcode 26.6, AppleClang 21.0.0, an iPhone 17 Pro simulator. Run
  44513368734: 38 of 38 at the first run (115 s of tests, the job about 10 minutes), every pinned bit as on the
  other platforms, with no source change to the engine or the platform layer; the device libraries built. Run
  44519312231, with the physics: 48 of 48.
- **Not built, and why:** the rest of the packages' tests and of the e2e fixtures;
  `tide_platform_rooms` (a relay and browsers on the Mac against a program in the simulator: nothing rules it out,
  it wasn't tried); anything on a real phone's processor, which only a phone runs.
