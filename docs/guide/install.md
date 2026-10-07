---
description: "Install the tide command on Windows, Linux or macOS, with built-in clang for native and web games, and learn how to update it."
---

# Install

Tide comes as one command, `tide`, which builds and runs games. It has its C compiler built in (clang), for native and web games alike.

## Windows

In PowerShell:

```powershell
Invoke-RestMethod https://tide-engine.dev/install.ps1 | Invoke-Expression
```

Windows needs nothing else: `tide` brings the headers and libraries games build with.

## Linux and macOS

```sh
curl -fsSL https://tide-engine.dev/install.sh | sh
```

The installer uses `curl`, `tar` and `python3`. Native games also need the system's own headers and libraries:

- **Linux** (x64): your distribution's C development files, which come with gcc: `sudo apt install build-essential` on Debian and Ubuntu, `sudo dnf install gcc` on Fedora. Playing in [rooms](../multiplayer/matches.md#rooms) needs OpenSSL's `libssl` too, which nearly every distribution has, and so do the players of your game.
- **macOS** (Apple Silicon): Apple's command-line tools, `xcode-select --install`. Install them before tide: they bring the `python3` the installer uses.

## What the installer does

It installs `tide` for your user, in `%LOCALAPPDATA%\Tide` on Windows or `~/.tide` elsewhere, and puts its `bin` folder on your `PATH`: the user's `PATH` on Windows, and elsewhere a line in your shell's profiles (`~/.zshrc`, `~/.bashrc`, `~/.profile` and the like, or fish's `conf.d`). It needs no admin rights. Open a new terminal afterwards so the `PATH` change applies, then check it worked:

```sh
tide version
```

This shows tide's version and channel, where it's installed, and which compilers it found.

The installer also adds Tide to the editors it finds (see [Editors](../tools/editors.md)). If you run it again when `tide` is already there, it upgrades it instead, on the channel it's on unless you set `TIDE_CHANNEL`, and adds Tide to editors installed since.

## Nightly versions

Stable versions come out now and then. Nightly versions are previews published when ready. For a nightly version, set `TIDE_CHANNEL` before running the installer:

::: code-group

```powershell [Windows]
$env:TIDE_CHANNEL = 'nightly'
Invoke-RestMethod https://tide-engine.dev/install.ps1 | Invoke-Expression
```

```sh [Linux and macOS]
curl -fsSL https://tide-engine.dev/install.sh | TIDE_CHANNEL=nightly sh
```

:::

::: tip
This site follows development, so it describes the nightly version. Some of what it shows may not be in the stable version yet.
:::

## Stay up to date

```sh
tide upgrade            # the newest version of your channel
tide upgrade --nightly  # switch to nightly versions
tide upgrade --stable   # back to stable ones
```

`tide` also tells you, at most once a day, when a new version is out. Upgrades check every download against the release's checksums.

`tide upgrade` only ever moves forward. Switching channels, or `tide upgrade --version <v>`, installs what you ask for, even an older version.

An upgrade replaces tide while your editor and your games keep running. When it can't replace a file, because another program is reading it, it stops and says which one. Until it's finished, `tide` builds no games and says the installation isn't whole: close what had the file open and run `tide upgrade` again, which installs the version again even when it's the newest already.

## Uninstall

Delete the folder tide is in (`tide version` says where), and the line that puts its `bin` folder on your `PATH`: on Windows, in **Edit environment variables for your account**; elsewhere, the lines after `# tide` in your shell's profiles, or fish's `conf.d/tide.fish`. Remove the Tide extension from your editors as you would any other.
