# Tide for Unity

Runs a Tide game's simulation and networking inside Unity. Tide simulates,
predicts and rolls back, every machine bit for bit the same; Unity draws, plays
audio and is where you author.

Early: the owner is testing it. What's built and what isn't is in the Tide
repository's `notes/unity.md`.

## Install

1. Install tide (tide-engine.dev). The package finds it on `PATH` or where the
   installer puts it; Project Settings > Tide can name another.
2. In Unity, Window > Package Manager > + > Add package from git URL:

   ```
   https://github.com/BlenMiner/tide-engine.git?path=/unity/com.tide.unity
   ```

3. Put the game's `.tide` files (and its C, if any) in `Assets/Tide`, or the
   folder Project Settings > Tide names. Keep other plugins' C sources out of
   that folder: tide compiles every `.c` file in it.

While the editor is open, `tide unity` builds the game into `Library/Tide`
whenever its files change; its lines are in the Console. Nothing it builds goes
in `Assets` or source control.

## Run

Add a GameObject with a `TideRunner`. In Play mode it runs the game: Main's
match on this machine, or what its arguments say (`--host`, `--join <code>`,
`--connect <address>`). A new build of the game is swapped into the running
match.

Each entity is shown by the first view rule that matches it: a Tide prefab's
name, or a component the entity has, and the Unity prefab to show it with. An
entity with Tide's `Transform` is placed by it, under its parent's GameObject.
`TideEntityView` on that GameObject reads the entity's components as Tide's
views see them this frame: `view.Get<Health>()`, with `Health` a struct marked
`[TideType("Health")]` laid out as the game's.

## Requirements

Unity 6000.0 or newer, and the tide version this package was made with.
