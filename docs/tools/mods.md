# Mods

A mod is a [package](packages.md) that someone other than a game's developer adds to it. Its code compiles with the game's, as one program, so a mod is game code: the same language, the same speed, in the same schedule. Nothing is interpreted, and a game carries no runtime for mods.

Every player of a match runs the same build, so a game with mods is built whole, for each set of mods, by a service we run. It builds the game with its mods, signs what it built, and serves it at a link.

::: warning
This is the first part of mods, and it will change. Builds are web pages only, a game's key comes from us, and only whoever has that key can ask for a build. What's decided and what's still open is in the [spec](../spec.md#mods).
:::

## How it goes

1. The game's developer **publishes** a version of the game's source: `tide publish`.
2. Whoever has the game's key **asks for a build** of that version with a set of mods, each a package from git, pinned to a commit.
3. The service builds it, in a machine made for that one build, and serves the page at a **link**. Anyone who opens the link plays the modded game in a browser, and players of the same link can share its rooms, since they run the same build.

## A mod is a package

A mod is written as any package is (see [Writing a package](packages.md#writing-a-package)): a folder of `.tide` files whose `tide.packages` says `package Name`, in a git repository on GitHub, GitLab, Codeberg or Bitbucket. What it declares is in its namespace, and it names the game's own declarations as the game's code does.

It can add to the game: components on the game's entities, systems in the match's schedule, views that draw with the game's. And it can replace what the game marked `virtual`, with an `override` (see [Virtual and override](../language/overrides.md)): what a mod can replace is the game's developer's choice.

This game is the one the service is tried with. It marks one view `virtual`:

<<< @/../builds/example/game/game.tide

And this is a mod for it. It adds a system and two views, and replaces how the ball is drawn:

<<< @/../builds/example/mod/sparkle.tide

Its `tide.packages` is one line, `package Sparkle`.

To try a mod while writing it, list its folder in a copy of the game's `tide.packages` (`../sparkle`) and `tide run`: that's the build the service makes, on your machine.

## Publishing a game

`tide publish` sends the service a version of the game in the current folder, or in the one named:

```
$ tide publish
Published duel version 5d0c3f...e9
```

- **The game's key** is in the environment, as `TIDE_GAME_KEY`. It names the game too: it's the game's id, a dot and a secret. Keep it as you keep a password: whoever has it publishes the game's source and decides what's built under its name.
- **What's sent** is what a build reads from the game's folder: its `.tide` files, its C and C++ with their headers, its libraries and its `tide.packages`, with the packages in folders inside the game's. Nothing else of the folder is, and nothing `tide` made there (`.tide/`, `build/`). Packages from git aren't sent: their commits name them. A package in a folder outside the game's can't be published with it: list it from git, or move it inside.
- **A version** is named by what was sent, with 64 hex digits: the same files are the same version, from any machine, whenever they're published. Publishing again after a change is another version, and the ones before it stay.
- **The source stays private.** The service keeps it to build from, and gives it to nobody.

## Asking for a build

A build is asked for over HTTP, with the game's key. Its mods are written as `tide.packages` writes a package from git: the repository, `//folder` when the package is in a folder of it, and the commit.

```
curl https://builds.tide-engine.dev/games/duel/builds \
  -H "Authorization: Bearer $TIDE_GAME_KEY" \
  -d '{"version": "5d0c3f...e9", "mods": ["github.com/someone/sparkle 0123456789abcdef0123456789abcdef01234567"]}'
```

```json
{"build": "7be9b4...d0", "game": "duel", "version": "5d0c3f...e9", "mods": ["github.com/someone/sparkle 0123...4567"],
 "tide": "0.3.0-nightly.71", "status": "pending", "asked": "2026-10-05T22:31:02.000Z"}
```

- **A build is named by what it's made from**: the game, the version, its mods and the version of `tide` that builds it. The same mods in any order are the same build, and asking for it again builds nothing: the answer is the build as it stands.
- **The mods' lines are added to the game's `tide.packages`**, with the lines of the packages they need, as [`tide add`](cli.md#add-and-update) adds them. The game is then built as `tide build --web --release` builds it.
- **Its `status`** is `pending` while it builds, which takes under a minute for a small game, then `built` or `failed`. Ask how it went with the key:

```
curl https://builds.tide-engine.dev/games/duel/builds/7be9b4...d0 -H "Authorization: Bearer $TIDE_GAME_KEY"
```

The answer has the status, and `log`, what `tide` said: a mod that doesn't compile fails with the error at its line, as `tide build` says it. A build that failed is built again when it's asked for again. Once it's `built`, the answer has its `url`.

| What's asked | Who can | What it does |
|---|---|---|
| `PUT /games/<id>/versions` | The game's key | Publishes a version: what `tide publish` does |
| `POST /games/<id>/builds` | The game's key | Asks for a build: `{"version", "mods"}` |
| `GET /games/<id>/builds/<build>` | The game's key | How a build went, with what `tide` said |
| `DELETE /games/<id>/builds/<build>` | The game's key | Revokes a build |
| `GET /b/<build>/` | Anyone | The page |
| `GET /b/<build>/manifest.json`, `manifest.sig` | Anyone | What was signed, and its signature |
| `GET /games/<id>/key` | Anyone | The game's public key |

The key goes in `Authorization: Bearer <key>`. What goes wrong is a status with `{"error": "..."}`, which says what to do: 400 for a request that's wrong, 401 without the game's key, 404 for a version or a build there's none of, 410 for a revoked build, 413 past a limit, 429 when too many builds run at once, and 503 when no machine could be had to build on.

## Playing a build

A build's page is at `https://builds.tide-engine.dev/b/<build>/`, for anyone, with no key. It's the game's web build, as `tide build --web` makes it (see [The tide command](cli.md#run-and-build)), and it runs on threads.

Players of one link run one build, so they can be in one match: `?host` after the address opens a room, and `?join=<code>` joins it (see [In a web page's address](cli.md#in-a-web-page-s-address)). A player on another build of the game, with other mods or none, can't join: every machine of a match runs the same code.

## What's signed

Each game has a key pair at the service, and every build is signed with it, so that a game can tell a build made for it from anything else. Nothing checks the signature yet: games that load builds will.

- `manifest.json` is one line of JSON: the build's name, its target (`web`), and the page's SHA-256 and size.
- `manifest.sig` is its signature: ECDSA on P-256 over a SHA-256 of the manifest's bytes, as 64 bytes, `r` then `s`.
- `/games/<id>/key` is the game's public key: 65 bytes, the point uncompressed.

Those are the forms a browser's own crypto takes:

```js
const at = 'https://builds.tide-engine.dev';
const bytes = async path => new Uint8Array(await (await fetch(at + path)).arrayBuffer());
const key = await crypto.subtle.importKey('raw', await bytes('/games/duel/key'), { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
const manifest = await bytes(`/b/${build}/manifest.json`);
const signed = await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, await bytes(`/b/${build}/manifest.sig`), manifest);
```

## Revoking a build

`DELETE` on a build's address, with the game's key, revokes it:

```
curl -X DELETE https://builds.tide-engine.dev/games/duel/builds/7be9b4...d0 -H "Authorization: Bearer $TIDE_GAME_KEY"
```

What it built is deleted, its link answers that it's gone, and it's never built again. Pages are asked for again each time they're opened, so a revoked build is gone from browsers' caches too. We can revoke a build as well, when one turns out to harm its players.

## Limits

| | |
|---|---|
| A version | 64 MB, which unpacks to 4096 files of 256 MB at most |
| A build's mods | 32 |
| A build | 10 minutes, 1 GB of memory, a page of 64 MB, packages of 512 MB |
| What a game keeps | 512 MB, of its versions and its builds: revoking makes room |
| Builds at once | 4, for every game together |

## How a build is made

Each build has a machine of its own, made for it and destroyed after it. `tide` runs none of the game's code or the mods' while it builds: it compiles them. The machine downloads the mods first, each at its commit, and then compiles with no network.

A build is made with one version of `tide`, which is in its name: when the service moves to a newer one, the same version and mods are another build, with another link.

## What isn't there yet

- **Builds for desktop**, and a shipped game that loads a build and joins its matches: a build is a web page at a link.
- **Mods from anyone.** Asking for a build takes the game's key, so a build has the mods its developer chose.
- **Rules for what a mod may be.** A mod will be Tide alone, with files of its own and no way out of the game. Today a mod is any package, C included: build the mods you'd build into your game yourself.
- **Accounts.** A game is an id and a key, which we make.
