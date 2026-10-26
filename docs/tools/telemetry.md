---
description: "What anonymous usage data the tide command sends, every field of it, where it goes, how long it's kept, and how to turn it off."
---

# Usage data

The `tide` command sends a small anonymous record of each command it runs, so we can see which commands and platforms people use, which versions are still in use, how long builds take and where things fail. It never sends your code, your paths, your project's or files' names, error messages, or who or where you are.

It's on unless you turn it off, and the first run that would send something says so, on one line, and sends nothing itself:

```
tide: sends anonymous usage data (no code, paths or names); see https://tide-engine.dev/tools/telemetry, or turn it off with `tide telemetry off`
```

## Turning it off

Any of these turns it off:

- `tide telemetry off`, for this installation until `tide telemetry on`. It also deletes anything waiting to be sent.
- The environment variable `TIDE_TELEMETRY=0`, wherever it's set.
- `DO_NOT_TRACK` set to anything but nothing or `0`, the convention other tools follow too.

`tide telemetry status` says whether anything is sent, and if not why not, what an event holds, where it goes, this installation's ID and where its settings are.

Nothing is ever sent from:

- a development build of `tide`, one built from the engine's repository, whose version ends in `-dev` (the rule that also keeps it from looking for updates)
- a run with `TIDE_HIDDEN` set, as automated runs and the engine's own tests have
- an installation where `tide` can't keep its settings, such as one it can't write to

## What's sent

After a command ends, `tide` writes one event, then a separate background `tide` sends it. No command waits for the network, and offline is fine: events wait, the newest 100 at most, and go with a later command. If the site refuses them, they're dropped rather than sent again.

An event is JSON with these fields, and only these:

| Field | What it holds |
|---|---|
| `id` | The installation's random ID: 32 hexadecimal digits |
| `command` | The command, from a fixed list (below) |
| `target` | What `build`, `run` or `unity` built for: `native`, `web` or `android`; `none` for other commands |
| `version` | `tide`'s version, like `0.4.0` or `0.4.0-nightly.3` |
| `os` | The operating system: `windows`, `linux`, `macos` or `other` |
| `arch` | The CPU: `x64`, `arm64` or `other` |
| `ci` | Whether a CI service runs it: `true` or `false` |
| `ms` | How long it took, in milliseconds, at most a day |
| `outcome` | Whether it worked, its exit code being 0: `ok` or `failed` |
| `error` | Only for a failure, its kind, from a fixed list (below) |
| `files` | How many `.tide` files the game has of its own: `0`, `1`, `2-5`, `6-20`, `21-100` or `101+` |
| `packages` | How many packages it lists: `0`, `1`, `2-5` or `6+` |

- `id` is 128 random bits from the system's cryptographic random source, made on the first run that would send. It names an installation, not a person or a machine, and is what lets two events be counted as one install.
- `command` is one of `add`, `build`, `deploy`, `editors`, `eject`, `fetch`, `help`, `ide`, `login`, `logout`, `open`, `publish`, `run`, `schedule`, `shaders`, `unity`, `update`, `upgrade` and `version`, or `other` for anything else.
- `ci` is `true` when one of the variables CI services set is: `CI`, `GITHUB_ACTIONS`, `GITLAB_CI`, `BUILDKITE`, `CIRCLECI`, `TRAVIS`, `JENKINS_URL`, `TF_BUILD`, `TEAMCITY_VERSION`, `BITBUCKET_BUILD_NUMBER`, `APPVEYOR`, `CODEBUILD_BUILD_ID`, `DRONE`, `BUILD_ID` or `CI_NAME`.
- `error` is one of `usage` (the command line), `setup` (no compiler or engine files), `packages`, `compile` (the game's Tide code), `c` (a C or C++ file), `link`, `network`, `game` (the game a run started exited with an error) and `other`.
- `files` and `packages` are there only for commands that read a game.

Every value comes from a fixed list or is a number, so nothing you type, name or write can be in one. An event looks like this:

```json
{"id":"3f9c2a7e5b1d48c0a6e4f2b9d7c1e305","command":"build","target":"web","version":"0.4.0","os":"windows","arch":"x64","ci":false,"ms":2412,"outcome":"ok","files":"2-5","packages":"1"}
```

Not sent: what you build or run, the arguments you give, paths, names of projects, folders or files, code, compiler output or error messages, your user or machine name, your locale or time zone. Your IP address reaches the server, as every request's does; it's used only to limit how many requests one address can make in a minute, kept in memory for that minute, and never stored.

`tide telemetry` itself records nothing, and neither do the runs `tide` starts for its own work: its built-in compiler, the scene editor's previews, the editor update after an upgrade and the background sender.

## Where it goes

Events are sent with `curl` over HTTPS to `https://tide-engine.dev/telemetry`, the engine's own website, and kept in a database on its server. There's no analytics company or other third party in between. The time an event arrived is kept to the hour, and events are deleted after 13 months.

We look at them as totals: installations active each day and week, commands a day, the versions in use, operating systems, how often each command fails and of which kind, and how long builds take. The site's [privacy page](/privacy) covers the rest of what the website and the multiplayer relay see.

## Where its settings are

In the `telemetry` folder of the installation (`%LOCALAPPDATA%\Tide\telemetry` on Windows and `~/.tide/telemetry` elsewhere, where the installers put `tide`), beside the channel `tide upgrade` follows:

- `settings`: whether it's on, whether the notice was shown, and the installation's ID
- `queue`: events not sent yet, one a line

Deleting the folder gives the installation a new ID, and shows the notice again.
