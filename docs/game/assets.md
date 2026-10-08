---
description: "Declare images, textures, cubemaps, text and bytes as Tide assets, then load files or fetch URLs locally while the game runs."
---

# Assets

An asset is a file a game is built with: an image, a text, some bytes. The language names it, the compiler checks it, and the build packs what's named and nothing else. The second half of this page covers what a machine loads while the game runs instead: a file of the player's, or a URL, which is local to that machine until the match is told about it.

## Declaring an asset

`asset` declares a file's contents under a name, at the top of a file like a constant. The type is what the file is read as:

```csharp
asset Grid2<Color> hero = "art/hero.png";   // A PNG, decoded as the game is built: cells code reads
asset Texture tiles = "art/tiles.png";      // A PNG that's only drawn
asset string rules = "rules.txt";           // UTF-8 text
asset List<byte> table = "data/table.bin";  // Any file, as its bytes
```

Code reads `hero` wherever it reads a `Grid2<Color>`: a system indexes its cells, a view draws it as a texture with `Draw.Mesh`, and a frag samples it by name, `hero.Sample(uv)`, as it samples a singleton's grid. The four types are the ones there are for now: `string`, `List<byte>`, `Grid2<Color>` and `Texture`.

::: tide-example assets A sprite from a file
<<< @/examples/assets.tide
:::

An asset is read-only. Nothing assigns it, takes it `mut`, or clears it: it's the file as the game was built, the same on every machine. To change pixels, keep a `Grid2<Color>` field of your own and copy cells in.

Match code reads assets like any value, since every machine has the same pack; a `Texture` is the one that's local code's alone (below). What a machine loads as the game runs is another matter, further down.

### A field's default

A field's default can name an asset, as it names a constant:

```csharp
asset Texture logo = "art/logo.png";
asset string motto = "motto.txt";
asset List<byte> table = "data/table.bin";

struct Sprite
{
    Texture image = logo;
    Color tint = Color.white;
}

component Sign
{
    string text = motto;         // The match's own copy of the text
    int size = table.count;      // An expression can read one
    local Texture face = logo;   // This machine's, as every Texture is
    local Sprite art;            // Its image starts as the logo too
}
```

- The field gets the asset's value where the default is given: as an entity is spawned, as a match or this machine's local state starts, and for an entity's [local fields](./local-state.md) nothing wrote yet.
- Text, bytes and a glob's list or map are copied into the field, which is the field's own from then on: changing `text` changes no file's.
- A `Texture`, a [`Clip`](./audio.md#clips) and a `Cubemap` name their asset, so they cost nothing to give, and under `tide run` a saved file shows through them.
- A `Grid2<Color>` field can't start as an asset's grid, since a grid is never copied: read the asset's by its name, or give it to a `Texture` field, `Texture face = hero;`.
- One of a glob's, like `run[0]` or `icons["art/icons/star.png"]`, isn't a default yet: a view assigns it.
- Only a field's default takes one. A `const`, a setting, an attribute's bound, an asset's path and a parameter's default are constants alone, as the compiler works those out while it builds.

## Images to draw with

Most of a game's images are only ever drawn: sprites, tile sheets, backgrounds, icons. Those are `Texture`s:

```csharp
asset Texture hero = "art/hero.png";
asset List<Texture> run = "art/run/*.png";
asset Map<string, Texture> icons = "art/icons/*.png";

local singleton Player
{
    int frame;
    Texture badge;   // Whichever image it's given
}

view Show(mut Player player)
{
    Draw.Mesh(corners, faces, hero);
    Draw.Mesh(corners, faces, run[player.frame % run.count], Filter.Point);
    player.badge = icons["art/icons/star.png"];
    Draw.Mesh(small, faces, player.badge);
}
```

- A `Texture` is a value (see [A Texture is a value](./meshes-and-textures.md#a-texture-is-a-value)): a draw's texture in 2D and 3D, the source a [frag's `Texture` parameter](./shaders.md#textures) is given, a [`Draw.Blit`](./render-targets.md#post-processing)'s source, something a field or a local holds and a function takes. A frag samples one by its name too: `hero.Sample(uv)`.
- It's a byte a channel, as the GPU keeps it: a quarter of what the same image takes as a `Grid2<Color>`, and nothing copies it on its way to the GPU.
- No code reads it. It has no cells and no size to ask. An image whose pixels or size the game needs is a `Grid2<Color>`, which draws as a `Texture` too, and one file can be declared both ways.
- It's this machine's, like everything that's drawn: views, local handlers and frags use it, and match code can't name one. The match reads a `Grid2<Color>`.
- What a field keeps of `hero` or of `run[2]` is that image as the files have it now: under `tide run`, a saved picture shows through the copies a game kept, as it does through `hero` itself.
- How it's sampled can be said once, on its declaration: `[Filter(Filter.Point), Wrap(Wrap.Repeat)]` before the asset (see [Saying it once, on the texture](./meshes-and-textures.md#saying-it-once-on-the-texture)). The `Texture` carries it: what a field keeps of `hero` is sampled as `hero` is.

## Cubemaps

A sky or a room's surroundings is six images, the faces of a cube, which a frag reads by a direction (see [Skybox and reflections](./shaders.md#skybox-and-reflections)). That's a `Cubemap`, and its path is a glob that matches the six files:

```csharp
asset Cubemap sky = "art/sky_??.png";
```

- **Each file is named for its face** by how its name ends before the extension: `px` and `nx` are the faces `+x` and `-x` point at, `py` and `ny` the ones above and below, `pz` and `nz` the ones ahead and behind. `sky_px.png`, `px.png` and `dusk-PX.png` are all the first.
- **Each is a face as it's seen from the cube's middle,** the way a picture is: `+y` is up for the four around, with `+x` to the right of `+z`. The one above has `-z` at its top, and the one below `+z`.
- Each is a PNG, a square, and all six are one size. A file that's none of the six, a face that's missing or there twice, and a size that differs are errors at the path, which name the file.
- A file is in the pack once, as any is: a face that's also an `asset Texture` shares its pixels.
- It's a value, like a `Texture`: a field or a local holds one, a function takes one, and a frag samples one by its name, `sky.Sample(direction)`. It's this machine's, so match code can't name one.
- No list or map of cubes yet, and no `[Filter]` on one: a cube's filter is said where it's sampled.

## Paths

A path is relative to the `.tide` file that names it, whatever that file's namespace: there's no assets folder and no root. A package's files name files beside its own code the same way. Forward slashes, on every platform.

A path is a constant: a string literal, or a `const string`, so a game that wants one path twice names it once:

```csharp
const string TILES = "art/tiles.png";
asset Grid2<Color> tiles = TILES;
```

### Globs

A glob names every file it matches, in order of their paths (byte by byte): `*` matches within a name, `**` across folders, and `?` one character. The type says what you get:

```csharp
asset List<Grid2<Color>> run = "art/run/*.png";              // Their contents, in order: run[0], run[1], ...
asset Map<string, Grid2<Color>> tiles = "art/tiles/**/*.png"; // Each path as written from this file, with its contents
asset List<string> notes = "notes/*.txt";
asset List<Texture> frames = "art/walk/*.png";              // Images to draw with, in order
```

A map's keys are the paths as you'd have written them, `"art/tiles/grass.png"`, so a game finds a file by name: `tiles["art/tiles/grass.png"]`, `tiles.Has(name)`. A glob that matches nothing is an error, and so is a glob for a type that holds one file.

A list or a map can name one file, and is then a list or a map of that one:

```csharp
asset List<Clip> shots = "sfx/shot.wav";             // shots.count is 1, and shots[0] is the file
asset Map<string, Clip> named = "sfx/shot.wav";      // named["sfx/shot.wav"]
```

That's for a path that changes while the game is made. Start with the one file there is, widen the path to `"sfx/shot*.wav"` or `"sfx/*.wav"` when there are more, or narrow it again, and the type and the code that reads it stay as they are. A map's key for the one file is its path as written, which is the key a glob gives it. A URL in a list or a map is the same: one entry, keyed by the URL.

A frag samples an asset by its own name: it can't index a list of them (`run[1].Sample(uv)` is an error), so declare the one it samples as an asset of its own, or for a `Texture`, give the draw that one as the frag's source: `Draw.Mesh(corners, faces, frames[1], Shade)`.

### URLs

A path can be an `http://` or `https://` URL. The compiler fetches it as the game is built and packs what it got, so it's a file kept elsewhere and nothing else: nothing is fetched when the game runs, and the game plays offline.

```csharp
asset string credits = "https://example.com/credits.txt";
```

What was fetched is kept in the game's `.tide/`, with the server's ETag. Each build asks the server whether the file changed, and builds from what it has when the server can't be reached, so an offline build works once the file was fetched once. A URL that can't be fetched and was never cached is an error.

## What's checked

Anything wrong that can be known as the game is built is an error at the declaration, in the editor as you type and in `tide build` alike:

- A file that isn't there. The error says where a file with the same bytes is now, if the file was moved (the compiler keeps a hash of every named file in the game's `.tide/`), or else lists the files of that name, with a quick fix for each in the editor.
- A glob that matches nothing.
- Bytes that aren't what the type says: a `Grid2<Color>` or a `Texture` whose file isn't a PNG that decodes, a `string` whose file isn't UTF-8.
- A type an asset can't be, a path that isn't a constant, a `local` or `virtual` asset, a write to one.

The editor keeps assets in step with their files: renaming a file or a folder rewrites the paths that name it, and a path's completion offers the files it can lead to. See [Editors](../tools/editors.md).

## The pack

`tide build` packs the closure of the game's `asset` declarations, and nothing else in the folder. A file is in the pack once however many declarations name it, and they all read the same value: a `Grid2<Color>` of the pack lives in a read-only region every world can name, outside every world's heap, so no world copies, hashes or sends it. A file declared as a `Grid2<Color>` and as a `Texture` is in it once as each, and so is a text file with characters past ASCII that's declared as a `string` and as a `List<byte>`.

Where the pack is: beside the program as `<name>.pack` on desktop, inside the self-contained page on the web, and in the APK on Android. Under `tide run` there's no build step for it: tide watches the files assets name, and a saved image is in the running game at once, with no rebuild, as it's data; a hot reload keeps it. A C host of its own (`tide_add_game` with `HOST`) gets the pack as a C array, `tide_game_pack`, to hand `tide_assets_load`.

## Loading at runtime

Two things bring bytes in while a game runs, both local, both failing with `IOError`:

- `File.ReadAllBytes(path)` reads a file of this machine's (see [Files and directories](./files.md)).
- `await Web.Fetch(url)` fetches an `http://` or `https://` URL, in a task, so frames go on meanwhile.

Bytes that every player should see go into the match through a [command](./commands.md), and the match keeps them as its own state, which the netcode sends like any other, to those in the match and to whoever joins.

## Decoding a PNG

`grid.Decode(bytes)` reads a PNG into a `Grid2<Color>` field of a component, singleton or scene, sizing the grid as the image is and writing every pixel. It fails with `IOError.InvalidData` for bytes that aren't a PNG, or are wrong anywhere, and then the grid is as it was.

It's pure, so it runs in match code as well as local code: a handler that decodes gives every machine the same pixels, bit for bit, because the engine's own reader does the decoding, with no system library in the way. The grid then draws as a texture like any `Grid2<Color>` (see [Meshes and textures](./meshes-and-textures.md#textures)).

A player's own picture, decoded for this machine alone:

```csharp
local singleton Picker
{
    string path;
    bool load;
    Grid2<Color> preview;
    string message;
}

view Pick(mut Picker picker)
{
    if (!picker.load) return;
    picker.load = false;
    var bytes = File.ReadAllBytes(picker.path) ?? [];
    if (picker.preview.Decode(bytes) is IOError error) picker.message = "Not a PNG: " + error;
    else picker.message = "Loaded " + picker.preview.size.x + " by " + picker.preview.size.y;
}
```

In the match, the handler of a command decodes, and every machine gets the same pixels:

```csharp
singleton Arena
{
    Grid2<Color> background;
    PlayerID host;
}

command SetBackground { List<byte> png; }

command(SetBackground value, PlayerID sender) Apply(mut Arena arena)
{
    if (sender != arena.host) return;
    arena.background.Decode(value.png)!;
}
```

A command's `List<byte>` goes to the server and on to every player, one that doesn't fit a datagram in chunks, so a few megabytes is fine; see [Commands](./commands.md).

Every kind of PNG decodes: gray, color, with a palette, with or without alpha, 1 to 16 bits a sample, interlaced or not. A clear black pixel is a cell that's zero, which takes no memory. A picture past 16384 pixels a side is `InvalidData`. Decoding runs where it's called, on the frame's or the tick's thread: a 4K picture takes a frame, so decode once, when the picture arrives, not every frame.

## Fetching a URL

`await Web.Fetch(url)` gives the body as a `List<byte>`. It's local only, like `File`: views, local handlers, local tasks and the functions they call, never the match, input sampling, the GPU, a sort comparison or a parallel loop. The task waits while this machine fetches, and the bytes arrive in a frame to come.

| Result | When |
| --- | --- |
| `IOError.NotFound` | The server answered 404. |
| `IOError.InvalidPath` | The URL doesn't start with `http://` or `https://`. |
| `IOError.Failed` | Anything else: no connection, a certificate the system doesn't trust, another status, a body over 64 MiB, more than 5 redirects, or on the web a server whose CORS refuses the page. |

```csharp
local singleton Download { string status; Grid2<Color> preview; }

async void Fetch(string url, mut Download download)
{
    download.status = "fetching";
    if (await Web.Fetch(url) is IOError error)
    {
        download.status = "failed: " + error;
        return;
    }
    download.status = "fetched";
}
```

Big media is better fetched by each machine than sent through the match: what goes through the match goes player to server to everyone, so a host uploads it once per player. Keep a URL in the match and let each machine fetch and decode.
