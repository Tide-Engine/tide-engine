# The runners on the owner's machine

While the account has no hosted minutes, the workflows run on two GitHub
Actions runners on one Windows machine: its own (label `tide-windows`) and one
in its WSL's Ubuntu (label `tide-linux`). Nothing builds for macOS meanwhile.

## Windows

- GitHub's runner, unpacked in `D:\Tools\tide-runner` and registered to the
  repository with the label `tide-windows` (`config.cmd --unattended --url
  https://github.com/BlenMiner/tide-engine --token <registration token>
  --name tide-windows --labels tide-windows`).
- It runs as the user, not as a service, so it has the user's tools: Git's
  bash, CMake, Ninja, Node, zstd, LLVM in `C:\Program Files\LLVM` (the major
  version `cmake/LLVM.cmake` pins, which a job checks), Chrome, and Android's
  SDK and NDK where Android Studio puts them.
- `start-runners.ps1`, copied beside the runner, starts both runners hidden; a
  shortcut to it in the user's Startup folder starts them at logon. It puts
  Git's bash first on `PATH` (System32's `bash.exe` is WSL's) and sets
  `TIDE_HIDDEN`, so no job shows a window.

## Linux (WSL)

- GitHub's runner, unpacked in `~/tide-runner` and registered with the label
  `tide-linux`. `~/tide-runner/start.sh` runs it, which `start-runners.ps1`
  calls through `wsl.exe`.
- `setup-linux.sh` installs, once, what a hosted runner comes with: LLVM's
  packages, Chrome, the X11 headers, Java, Android's SDK with its emulator, and
  the user's place in the `kvm` group. It needs the sudo password. Run it again
  when `cmake/LLVM.cmake` moves to another major version.

## What differs from a hosted runner

- The checkout and its `build/` stay from one job to the next
  (`clean: false`), so jobs build what changed, with no compiler cache and no
  cache steps. A build tree that went wrong is deleted by hand, in
  `_work/tide-engine/tide-engine/build`.
- One runner an OS runs one job at a time, and both workflows share a checkout.
- Going back to hosted runners: the `runs-on` labels and the macOS rows of the
  matrix in `build.yml`, and `runs-on` in `docs.yml`. The steps for hosted
  runners are still there.
