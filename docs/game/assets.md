# Assets

An asset is a file a game is built with: an image, a text, some bytes. This page covers what a machine loads while the game runs: a file of the player's, or a URL, which is local to that machine until the match is told about it.

## Loading at runtime

Two things bring bytes in while a game runs, both local, both failing with `IOError`:

- `File.ReadAllBytes(path)` reads a file of this machine's (see [Files and directories](./files.md)).
- `await Web.Fetch(url)` fetches an `http://` or `https://` URL, in a task, so frames go on meanwhile.

Bytes that every player should see go into the match through a [command](./commands.md), and the match keeps them as its own state, which the netcode sends like any other, to those in the match and to whoever joins.

## Decoding a PNG

`grid.Decode(bytes)` reads a PNG into a `Grid2<Color>` field of a component, singleton or scene, sizing the grid as the image is and writing every pixel. It fails with `IOError.InvalidData` for bytes that aren't a PNG, or are wrong anywhere, and then the grid is as it was.

It's pure, so it runs in match code as well as local code: a handler that decodes gives every machine the same pixels, bit for bit, because the engine's own reader does the decoding, with no system library in the way. The grid then draws as a texture like any `Grid2<Color>` (see [Views and drawing](./views.md)).

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

::: info
A command carrying a `List<byte>` is agreed but not built yet (see the spec's Commands): until it is, what a match handler decodes is bytes the match already holds.
:::

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
