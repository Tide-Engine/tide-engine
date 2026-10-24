---
description: "Add and update Tide packages from git or local folders, pin commits and share game code and C libraries through tide.packages."
---

# Using and writing packages

A package is Tide code, and C, that games share: a folder of `.tide` files, like a game's, without a `Main`. A game lists the packages it uses in a file named `tide.packages`, next to its own files, and `tide` builds them with it. Packages come from git repositories, at a commit, from folders on your machine, or with `tide` itself.

For the list of bundled packages, their API documentation and how to find docs for other packages, start with the [Packages overview](./index.md).

## Using one

`tide add` puts a package in the game in the current folder:

```
tide add github.com/someone/tide-physics
```

It finds the newest commit of the repository's default branch, downloads it, and writes a line for it in `tide.packages`, starting the file if there's none:

```
github.com/someone/tide-physics b01aac4d779e32c9c415283b65e40c86d691e7ab
```

The package's code is in its namespace, which is its name, so the game names what it declares with it, or imports it with `using` (see [Namespaces and files](../language/namespaces.md)):

```csharp
using Physics;

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Body { velocity = float3(0, 1, 0) });
}
```

`tide add` takes the repository however you have it: `https://github.com/someone/tide-physics.git`, `git@github.com:someone/tide-physics.git`, or as `tide.packages` writes it. Packages come from GitHub, GitLab, Codeberg and Bitbucket, and need no git on your machine: `tide` downloads each commit as its host's archive of it.

### Every build, the same code

A line always has its commit, and the game builds with that commit's code, whoever builds it and wherever: your machine, someone else's, a build server, the web build and the desktop one. They have to: a match's players simulate it together, so each of them has to run the same code. A branch or a tag can move, so a line never builds with "the newest" of anything.

`tide` keeps each commit it downloads, once for every game on your machine, in `%LOCALAPPDATA%\Tide\packages` on Windows and `~/.tide/packages` elsewhere (or where the `TIDE_PACKAGES` environment variable says). A game whose packages aren't there yet, like one you just cloned, downloads them the first time `tide run` or `tide build` builds it. `tide fetch` downloads them without building, for a build that then needs no network.

## Updating

`tide update` moves packages to the newest commit of what they follow, and says what changed:

```
tide update            # every package from git
tide update physics    # one, by its name, its repository's name, or its source
```

```
tide: Physics b01aac4 -> 7d3e9f1 (main)
```

A line follows its repository's default branch, or what comes after `@` in it:

| Line | `tide update` moves it to |
|---|---|
| `github.com/someone/tide-physics b01aac4...` | The newest commit of the default branch |
| `github.com/someone/tide-physics@dev b01aac4...` | The newest commit of the `dev` branch |
| `github.com/someone/tide-physics@v1 b01aac4...` | The newest `v1.x.y` tag |
| `github.com/someone/tide-physics@v1.2 b01aac4...` | The newest `v1.2.x` tag |

A version, like `v1` or `1.2`, follows tags, leaving out pre-releases like `v1.3.0-rc.1`; anything else is a branch. `tide add` takes the same: `tide add github.com/someone/tide-physics@v1`.

A line can leave its commit out, as when you write one by hand: `tide update` puts the newest one in. `tide update` only changes the commits in `tide.packages`, so your comments and the order of its lines stay, and what changed shows in git like any other change.

## The packages a package needs

A package can use other packages, which its own `tide.packages` lists. The game's `tide.packages` lists every package the game builds with, those its packages need included, so that one file says everything the game builds with. `tide add` and `tide update` add the lines of what's missing, and say so:

```
tide: added github.com/someone/tide-math 9f2c1e0..., which Physics needs
```

If a package's metadata or dependencies are invalid, the command reports the problem and fails.

When the game and one of its packages list another package at different commits, the game's line is the one it builds with.

## Packages in folders

A line can be a folder instead, relative to the game's, for a package you're writing, or one a few games of yours share:

```
../shared
github.com/someone/tide-physics b01aac4d779e32c9c415283b65e40c86d691e7ab
```

A folder's files build as they are, with no commit, and `tide run` reloads the game when they change, as it does for the game's own. `tide add ../shared` adds one and preserves the complete folder path in `tide.packages`.

## Packages that come with tide

The [bundled packages](./index.md#bundled-packages) are `render3d`, `box2d`, `box3d`, `newton`, `rooms`, `lobby`, `steam` and `voice`. Their API docs are collected in this section. [Render3D](./render3d.md) supplies opaque mesh/material rendering with package-owned lighting, shadows and HDR presentation. [Newton](./newton.md) is experimental. Use a checkout or installation containing the packages you need, and check their current scope. A line for a bundled package is its name alone:

```
box2d
github.com/someone/tide-physics b01aac4d779e32c9c415283b65e40c86d691e7ab
```

`tide add box2d` writes that line, and the lines of what the package needs. A game that doesn't list one builds none of it: its code compiles and links only with the games that name it.

A name is lower-case letters and digits, and it's never a folder: a folder of that name beside the game is still written as a path, `./box2d`. The line has no commit and follows no branch, since the package is the version your `tide` came with: `tide upgrade` updates both, and `tide update` leaves the line alone. So two machines build the same code of it when they have the same version of `tide`. A `tide 0.3` line in the game's `tide.packages` says the oldest one the game builds with.

A name no package has is an error that lists the ones there are. They're folders of the installation's `src/packages` folder, each named as its line is, where the editors' go to definition takes you.

## Packages for some builds only

`#if` lines say which builds a package is in, by the build's [symbols](../language/namespaces.md#symbols-of-your-own): `DEBUG`, which every build but a `--release` one has, and what the command line names with `--define`.

```
rooms
#if STEAM
github.com/someone/tide-steam 9c1d7be0a4f25d6c8e3b17f04a92d5c6e8f0a1b2
#else
../our-lobby
#endif
#if DEBUG
../cheats
#endif
```

`tide build --release --define STEAM` builds with `rooms` and the Steam package; `tide run` builds with `rooms`, the lobby in the folder and the cheats. A package a build leaves out is no part of it: none of its code, C or libraries compiles or links, and its `PACKAGE_` symbol isn't defined, so the game's own code can follow with `#if PACKAGE_STEAM`.

Conditions are written as in code: symbols with `!`, `&&`, `||`, `==` and `!=`, parentheses, `true` and `false`, and versions of `tide` like `TIDE_0_4_OR_NEWER`, with `#elif` and `#else` branches, and `#if`s inside one another. Any other line that starts with `#` is still a comment. A `package` or `tide` line is the same in every build, so it goes outside them, and a condition can't read a `PACKAGE_` symbol, as those come from this file.

`tide add`, `tide update`, `tide fetch` and `tide eject` see every line, whichever build it's for: `tide update` moves a package no matter which branch lists it, and `tide fetch` downloads them all. Editors read the file as `tide run` does, with `DEBUG` and nothing a command line would define.

## Making a package your own

A package's code isn't the game's to edit: one from git is a commit, and one that comes with `tide` is replaced by the next upgrade. `tide eject` copies a package into the game and builds with the copy:

```sh
tide eject rooms
```

```
./packages/rooms
```

The package is a folder of the game's from there, in `packages/` under its own folder's name, and its line is that folder, with any comment after it kept. It's the same package still, with its namespace and what it needs, so nothing else in the game changes; `tide update` and `tide upgrade` no longer move it. Before ejecting, see whether the package marks what you want to change as `virtual`: an [override](../language/overrides.md) changes that much and keeps the package's updates.

## Writing a package

A package is a folder whose `tide.packages` starts with its name, which is its namespace:

```
package Physics
tide 0.3
github.com/someone/tide-math 9f2c1e07d4b8a3f6e5c2d1b0a9f8e7d6c5b4a392
```

- Every file of the package puts what it declares in its namespace, `namespace Physics;`, or in one inside it, like `namespace Physics.Joints;`.
- What's the game's own stays the game's: a package can't declare `Main`, the input or settings. Its systems can take `Devices`, which the game's input sends with its own fields.
- `tide 0.3` is the oldest `tide` it builds with. With an older one, a game that uses it says so, and to run `tide upgrade`.
- Its other lines are the packages it needs, as a game lists them. One from git only needs packages from git and packages that come with `tide`, since a folder on your machine isn't on anyone else's.
- Its C goes in its folder, as a game's does (see [Calling C](../c/functions.md)), and compiles with the game.
- A package's systems run before the game's: packages' files compile first, each package after those it needs, and the game's files last (see [Systems](../game/systems.md#order)).
- A package can replace code the game marked `virtual`, with an `override`, and mark its own for the game to replace (see [Virtual and override](../language/overrides.md)).

To try a package as you write it, make a game in a folder of its own inside it, whose `tide.packages` lists the package's folder:

```
physics/
  tide.packages      package Physics
  body.tide
  example/
    tide.packages    ..
    main.tide        scene Main { } ...
```

A folder with a `tide.packages` of its own is a game or a package, so `example/` isn't part of the package, and the package isn't part of a game that holds it in a folder: a game uses it by listing it. Put a package's repository online, and `tide add` takes it. A repository can hold several packages, each in a folder of it, which a line names after `//`:

```
github.com/someone/tide-packages//physics b01aac4d779e32c9c415283b65e40c86d691e7ab
```

### Replacing a game's code

A package adds to a game, and it can also change it: an expansion, a variant of the rules, a mod. The game marks what can be replaced with `virtual`, and the package replaces it with an `override` of the same name and parameters, which runs in its place:

```csharp
// The game
namespace Combat;

virtual float Damage(float base, Weapon weapon)
{
    return base + weapon.power;
}
```

```csharp
// The package
namespace Hardcore;

override float Combat.Damage(float base, Combat.Weapon weapon)
{
    return (base + weapon.power) * 2;
}
```

Every call of `Damage` in the game now calls the package's. Systems, views, handlers, methods, frags and verts are replaced the same way, and a game replaces what a package marked: see [Virtual and override](../language/overrides.md).

### Code for other versions of tide

Tide still changes from version to version, so a package can have code for the versions it supports, which `#if` picks between (see [Namespaces and files](../language/namespaces.md#code-for-other-versions-of-tide)):

```csharp
#if TIDE_0_4_OR_NEWER
    // Code that uses what tide 0.4 added
#else
    // The same, as tide 0.3 writes it
#endif
```

### Unity components

A package can say which of Unity's own components are which of its own, for games made in Unity: a `unity.json` in its folder, which `tide unity` reads for each game that lists the package. Box3D's makes a `Rigidbody` its `Body` and a `BoxCollider` its `Box`. Only components that are one to one belong there, field for field:

```json
{
  "unity": {
    "BoxCollider": {
      "as": "Box3D.Box",
      "fields": {
        "size": "m_Size * absScale",
        "offset": "m_Center * scale",
        "sensor": "m_IsTrigger"
      },
      "needs": "Box3D.Body { type = Box3D.BodyType.Static }",
      "notUnder": ["Rigidbody", "a collider under a Rigidbody is a shape of that body, which is Box3D.Part, not one to one"]
    }
  }
}
```

- Each key of `unity` is a Unity component, as Unity's scene files name it. `as` is the package's component it is.
- `fields` gives each field of that component from Unity's: its serialized name (`m_IsTrigger`), or `{"from": "m_IsKinematic", "values": {"0": "...", "1": "..."}}` for one whose values become others. A size can be by the GameObject's scale, as Unity's are: `* scale` and `* absScale` (each axis, with its sign or without), `* largestScale`, `* largestScaleXY`, `* largestScaleXZ`, and `* scaleX`, `* scaleY` and `* scaleZ`.
- `when`, `["m_Direction", "1", "why"]`, says the only value of a field with which it's one to one. With another the component is left out, and a warning says why.
- `needs` is a component the entity has with it when nothing else gives it one of that type: a collider with no `Rigidbody` is on a static body.
- `notUnder` names a Unity component that, on a GameObject above it, makes it no one to one.
- `source` is lines of Tide the game has in a Unity project alone: Box3D's starts a body where its entity's Transform is, and has the Transform follow the body.

A component of the package's own on the GameObject always wins over a Unity one's translation.

## Editors

The editors' language server analyzes a game with its packages, so completion, errors and go to definition reach into them. A package's file you open is analyzed with the open game that uses it, or else on its own, with what it needs. Packages from git it doesn't have yet are left out until `tide` downloads them.
