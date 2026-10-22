---
description: "Find Tide's bundled physics, multiplayer menu, Steam and Opus voice chat packages, and locate docs for packages from git or local folders."
---

# Packages

Packages add shared Tide code and C libraries to a game. Tide bundles six optional packages: two physics integrations, two multiplayer menus, a Steam integration and Opus voice chat. Each has its own page here, with setup, API examples and current limits.

## Bundled packages

The name in the first column is what you pass to `tide add` or write in `tide.packages`. The namespace is what you use in Tide code.

| Package | Namespace | What it provides | Documentation |
|---|---|---|---|
| `box2d` | `Box2D` | Deterministic 2D rigid bodies, shapes, collisions and queries | [Box2D](./box2d.md) |
| `box3d` | `Box3D` | Deterministic 3D rigid bodies, shapes, collisions and queries | [Box3D](./box3d.md) |
| `rooms` | `Rooms` | Play, Host and Join menus, room-code sharing and volume settings | [Rooms](./rooms.md) |
| `lobby` | `Lobbies` | Pre-match lobbies, ready flags, browsing and quick match, with a menu | [Lobby](./lobby.md) |
| `steam` | `Steam` | Steam identity, lobbies, matchmaking and match transport for `lobby` | [Steam](./steam.md) |
| `voice` | `Voice` | Optional Opus voice chat through sessions, push-to-talk and receive volume | [Voice chat](./voice.md) |

Choose `rooms` for a simple Play/Host/Join flow, or `lobby` when players gather before a match and browse or find games. `steam` extends `lobby` for desktop Steam builds and requires the game's own Steamworks library; that library is not bundled with Tide.

To add one, run this in the game's folder:

```sh
tide add box2d
```

That adds `box2d` to `tide.packages`. Its code uses the namespace `Box2D`, so a file can say `using Box2D;` or name a type in full, such as `Box2D.Body`. `tide add` also adds any packages it needs.

Bundled packages compile with games that list them. A game that leaves a package out compiles and ships none of it. Their versions follow your Tide installation: `tide upgrade` updates them, while `tide update` updates packages from git. See [Using and writing packages](./using-and-writing.md) for dependency files, pinned commits, local folders, conditional builds and making a package your own.

## What is built into the engine?

The [ECS](../game/entities.md), [drawing](../game/views.md), [audio](../game/audio.md), [GUI](../game/gui.md), [math](../language/math.md) and [match networking](../multiplayer/networking.md) are engine features. You use them without adding a package.

In particular, [`Session.Start`, `Open` and `Join`, room codes and host migration](../multiplayer/matches.md) belong to the engine. The `rooms` package supplies a menu over those features. The `lobby` package adds pre-match groups and matchmaking. Rigid-body physics comes from `box2d` or `box3d`.

## Finding a package's docs

For a bundled package, use its page in this section. Its installed source folder also contains a `README.md`. Run `tide version` to find the installation folder, then look in `src/packages/<name>/`.

For a package from git, start with the source listed in your game's `tide.packages`: open that repository and read its README, documentation and examples. A source with `//` names a package subfolder within the repository. The file pins a commit, so use documentation from that commit when the repository's latest docs describe a different API. For a local package, start with the folder named by its dependency line.

Editors provide completion and go to definition for packages in the game, so you can inspect the exact declarations you build with. Run `tide fetch` first if a git package has not been downloaded. Package documentation is supplied by its author; Tide does not currently have a package documentation registry or a command that opens a package's docs.

## Writing documentation for your package

When [sharing a package](./using-and-writing.md#writing-a-package), put a README in its folder that names the package and namespace, shows how to add it and gives a minimal usage example. Link any fuller API docs and examples there, and describe dependencies, required C libraries, supported targets and current limits. Keep the docs with the code so users of a pinned commit can find the matching instructions.
