# The former runners on the owner's machines

The workflows now use GitHub-hosted runners: `windows-latest`, `ubuntu-latest`
and `macos-latest`. Normal pushes and pull requests build the three packages
with their existing package checks; full test suites still run only by hand
or for stable releases, on Windows and Linux. Docs runs on `ubuntu-latest`.

The setup below records the two self-hosted runners used while the account
had no hosted minutes. Neither workflow currently selects them.

## Windows (the PC)

- GitHub's runner, unpacked in `D:\Tools\tide-runner` and registered to the
  repository with the label `tide-windows` (`config.cmd --unattended --url
  <the repository> --token <registration token> --name tide-windows --labels
  tide-windows`).
- It runs as the user, not as a service, so it has the user's tools: Git's
  bash, CMake, Ninja, Node, zstd, LLVM in `C:\Program Files\LLVM` (the major
  version `cmake/LLVM.cmake` pins, which a job checks), Chrome, and Android's
  SDK and NDK where Android Studio puts them.
- A push runs nothing here: this runner is for a full run's Windows test
  suites, which are started by hand.
- `start-runner.ps1`, copied beside the runner, starts it hidden; a shortcut
  to it in the user's Startup folder starts it at logon. It puts Git's bash
  first on `PATH` (System32's `bash.exe` is WSL's) and sets `TIDE_HIDDEN`, so
  no job shows a window.

## Linux (the home server)

- Debian, reached as `ssh purrmachine@home`. GitHub's runner is in
  `~/tide-runner`, registered with the label `tide-linux-home`, and runs as a
  systemd service (`sudo ./svc.sh install purrmachine`, `start`, `status`), so
  it's there after a restart. Its `.env` has `TIDE_HIDDEN` and `ANDROID_HOME`.
- `setup-linux.sh` installs, once, what a hosted runner comes with: LLVM's
  packages, Chrome, the X11 headers, Java, Android's SDK with its emulator, and
  the user's place in the `kvm` group. It works on Ubuntu too. Run it again
  when `cmake/LLVM.cmake` moves to another major version.
- This runner builds Windows' package too, which takes Microsoft's headers and
  libraries in `~/xwin`: the same script fetches them with xwin when
  `TIDE_ACCEPT_MICROSOFT_LICENSE` is set, which accepts Visual Studio's
  license for them.
- The runner needs .NET's own libraries first: `sudo
  ./bin/installdependencies.sh` in its folder.
- It's a small machine (4 cores) that runs other things. Each package builds
  there in a few minutes; a full run's test suites take it far longer than
  they took the PC.

## What differs from a hosted runner

- The checkout and its `build/` stay from one job to the next
  (`clean: false`), so jobs build what changed, with no compiler cache and no
  cache steps. A build tree that went wrong is deleted by hand, in
  `_work/tide-engine/tide-engine/build`.
- One runner an OS runs one job at a time, and both workflows share a checkout.
- The hosted workflows install their tools and restore caches on fresh
  checkouts. Their test selection is the Version job's Checks step in
  `build.yml`; macOS has a package row only.
