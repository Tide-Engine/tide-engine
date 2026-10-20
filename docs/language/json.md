---
description: "Read and write JSON in Tide as your own structs with Json.Read and Json.Write, the same on every machine, with no dynamic values."
---

# JSON

`Json.Read<T>` reads JSON as a struct of yours, and `Json.Write` writes one. There's no JSON value to walk through and ask what it is: you say the shape as a struct, and the compiler writes the reader and the writer for it.

```csharp
struct LobbyInfo
{
    string id;
    string name;
    int players;
    List<string> tags;
    [Json("max_players")] int maxPlayers = 8;
}

async void Refresh(string url, mut Lobbies lobbies)
{
    var answer = await Web.Request(url).Send() ?? WebResponse { };
    if (Json.Read<LobbyInfo>(answer.body) is LobbyInfo info)
    {
        lobbies.current = info;
        lobbies.shown = $"{info.name}: {info.players}/{info.maxPlayers}";
    }
    else
    {
        lobbies.shown = "The server said something that isn't a lobby";
    }
}
```

| Call | What it gives |
| --- | --- |
| `Json.Read<T>(json)` | A `T?`: the value, or nothing when the JSON isn't one. `json` is a `string` or a `List<byte>` of UTF-8, like a response's body. |
| `Json.Write(value)` | A `string`: the value as JSON, with no spaces. |

`Json.Read` gives a [`T?`](./errors.md), so the code says what happens when the JSON is bad: `??` for a fallback, `is` to run only when it read, `!` for the type's defaults.

## What a struct is in JSON

A struct is an object, and each field is a key: the field's name, or what `[Json("key")]` before it says. `T` is a struct, a `List<T>` or a `Map<string, T>`, and what's in it is what JSON has something for:

| In Tide | In JSON |
| --- | --- |
| `bool` | `true` or `false` |
| `byte`, `int`, `long` | A number that's whole and fits: `3`, `3.0` and `3e2` are, `3.5` isn't |
| `float` | A number, read as the nearest float |
| `double` | A number, read directly as the nearest double |
| `string` | A string |
| `char` | A string of one character, or `""` for `'\0'` |
| An enum | Its member's name, as a string: `"Ranked"` |
| A struct | An object |
| `List<T>` and `T[N]` | An array |
| `Map<string, T>` | An object, a key for each entry |

Anything else is a compile error that names the field: an `Entity`, a vector, a `Set<T>`, a map whose keys aren't text, a grid, a component. Copy what you need of those into a struct of plain fields.

## Reading

Reading is forgiving about what's there and strict about what it is:

- **A key the JSON leaves out keeps its field's default**, and so does a key whose value is `null`. `maxPlayers` above is 8 unless the JSON says otherwise.
- **A `T?` field tells you whether the key was there.** It has nothing when the key is left out (or its default, if it declares one) and when the value is `null`.
- **A key no field has is skipped.** A server can add to what it sends without breaking the game.
- **A key written twice takes the last one.**
- **A value of the wrong kind makes the whole read give nothing**: a string where a number goes, a number that doesn't fit its field (`300` for a `byte`, `1e40` for a `float`), an enum's name no member has, an object where an array goes.
- **Text that isn't JSON gives nothing**: a missing brace, a comma after the last element, a comment, single quotes, bytes that aren't UTF-8.

A fixed array takes the elements it has room for: fewer leave the rest as they were, and more are skipped. An enum also reads from its number, which is how one with no member's name is written.

```csharp
enum Mode { Casual, Ranked }

struct Rules
{
    Mode mode = Mode.Casual;
    float[3] spawn;
    Map<string, int> limits;
}

// {"mode":"Ranked","spawn":[0,2.5],"limits":{"lives":3,"time":120},"motd":"hi"}
var rules = Json.Read<Rules>(text) ?? Rules { };
// rules.mode == Mode.Ranked, rules.spawn[1] == 2.5, rules.limits["time"] == 120
```

The top of the JSON can be an array or an object of anything, too:

```csharp
var lobbies = Json.Read<List<LobbyInfo>>(body) ?? [];
var prices = Json.Read<Map<string, int>>(body) ?? default;
```

## Writing

`Json.Write` writes every field that has a value, in the order the struct declares them, and a map's entries in the order they were added:

```csharp
var text = Json.Write(LobbyInfo { id = "a1", name = "Friday", tags = ["casual"] });
// {"id":"a1","name":"Friday","players":0,"tags":["casual"],"max_players":8}
```

A [`T?` field](./errors.md#fields-that-may-be-missing) with nothing is left out, so one struct can say only what changes:

```csharp
struct LobbyChange
{
    string? name;
    [Json("max_players")] int? maxPlayers;
    bool? open;
}

var text = Json.Write(LobbyChange { open = false });
// {"open":false}
```

Reading works the same way round: a key that's missing or `null` leaves the field with nothing.

What it writes, `Json.Read` reads back as the same value. A float or double is written with the fewest digits that read back as the same value, bit for bit. Double fields retain binary64 precision without first rounding to float. JSON has no number for NaN or an infinity, so those are written as `null`, which reads back as the field's default.

To send it, turn the text into bytes:

```csharp
var answer = await Web.Request(url)
    .SetMethod("POST")
    .SetHeader("Content-Type", "application/json")
    .SetBody(Json.Write(info).ToBytes())
    .Send();
```

## In the match

Reading and writing JSON is the same on every machine: numbers are read and written with exact arithmetic, never the platform's own, so a float read from the same text has the same bits on desktop and on the web. Match code can read JSON a [command](../game/commands.md) brought, and every machine gets the same value from it.

What reaches `Json.Read` can be anything, a server's mistake or someone's attempt to break the game, and it never does more than give nothing. The limits it reads within:

| Limit | What happens past it |
| --- | --- |
| Arrays and objects nested 32 deep | The read gives nothing |
| 64 MiB of text | The read gives nothing |
| 128 MiB of values made by one read | The read gives nothing |
| A field's key of 120 bytes | A compile error at the `[Json]` |

## Without JSON

The pieces `Json` is made of are text's own, for formats that aren't JSON (see [Text and lists](./text-and-lists.md#bytes-pieces-and-numbers)): `text.ToBytes()` and `string.FromBytes(bytes)`, `text.Split(separator)`, and `int.TryParse`, `long.TryParse` and `float.TryParse`.
