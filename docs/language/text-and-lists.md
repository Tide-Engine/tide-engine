---
description: "Work with Tide strings, lists, arrays, maps and sets as values, including text formatting, sorting and parallel element loops."
---

# Text and lists

Text and collections are values: assigning one copies it, and changing a copy never changes the original. Text clamps positions to its bounds. A list or array read outside its bounds gives zero, and a write there does nothing.

## Text

`string` is text, in UTF-8. Literals are written in double quotes, with the escapes `\"`, `\\` and `\n`.

```csharp
component Nameplate
{
    string name = "Player";
}

system Greet(mut Nameplate plate, Stats stats)
{
    plate.name = $"{plate.name.Trim()} ({stats.wins} wins)";
}
```

### Joining and formatting

- `+` joins text with anything it can show: `"score " + score`, `1 + "st"`.
- `$"score {score}"` puts values in text. After a value, a colon and a format say how it's written: `{x:F2}` for two decimals, `{n:D3}` for at least three digits (`007`), `{n:X}` for hex. `{{` and `}}` are braces, and `?:` in a value goes in parentheses: `{(won ? 1 : 0)}`.

Text can show numbers, bools, enums (their member's name, or their number when [no member has it](./basics.md#from-a-number)), vectors and quaternions (`(1, 0.5)`), `Color`, `Rect`, entities (`Entity(3:1)`, or `Entity(new)` for one a system splitting its entities across threads just spawned, whose ID comes once it's done) and players (`PlayerID(0)`). Floats and doubles are written with the fewest digits that read back as the same value, exactly the same on every platform. Doubles also accept fixed decimal formats through `F17`, such as `$"{value:F12}"`.

It shows whole values too, which is handy for debugging. Structs, components, singletons, events and inputs show their name and every field, and lists show their elements. Text inside them is in quotes, so empty text still shows:

```csharp
GUILayout.Label($"{session}");
// Session { state = Connected, player = PlayerID(0), ping = 0, server = true, open = false, room = "" }
GUILayout.Label($"{scores}");
// [3, 1, 2]
```

Matrices can't be shown yet, so neither can a value that holds one.

### Members

- `length` counts characters (Unicode code points), not bytes.
- `text[i]` is the character at `i`, a [`char`](#characters), counting from 0.
- `Contains`, `StartsWith`, `EndsWith` and `IndexOf`, which gives -1 when it's not there. Each takes text or a char: `name.Contains("ob")`, `name.IndexOf('b')`.
- `Substring(start)` and `Substring(start, length)`.
- `ToUpper` and `ToLower`, for ASCII letters for now.
- `Trim` and `Replace(from, to)`, of text or of chars.
- `Split(separator)`, the pieces between each separator and the next, and `ToBytes()`, the text as UTF-8 (see [Bytes, pieces and numbers](#bytes-pieces-and-numbers)).
- `==` and `!=` compare text byte by byte. There's no `<` for text.

### Bytes, pieces and numbers

What comes from outside the game is bytes, and what a player types is text: these turn one into the other, and text into the values in it. They're the same on every machine, so the match can use them.

```csharp
bool ReadScore(List<byte> file, out string name, out int score)
{
    name = "";
    score = 0;
    var line = string.FromBytes(file) ?? "";      // "Ada;1200"
    var parts = line.Trim().Split(';');
    if (parts.count != 2) return false;
    name = parts[0];
    return int.TryParse(parts[1], out score);
}
```

| Call | What it gives |
| --- | --- |
| `text.ToBytes()` | A `List<byte>`: the text's UTF-8 |
| `string.FromBytes(bytes)` | A `string?`: the text those bytes are, or nothing when they aren't UTF-8 |
| `text.Split(separator)` | A `List<string>`: the pieces between each separator and the next. The separator is text or a char |
| `int.TryParse(text, out value)` | Whether the whole text is an int, which goes to `value` |
| `long.TryParse(text, out value)` | The same, for a long |
| `float.TryParse(text, out value)` | Whether the whole text is a number, which goes to `value` as the nearest float |
| `double.TryParse(text, out value)` | The same, rounded directly to the nearest double |

- **`Split` keeps empty pieces**, so joining the pieces with the separator gives the text back: `"a,,b".Split(",")` is `["a", "", "b"]`, and `"".Split(",")` is one empty piece. A separator that isn't in the text, or an empty one, gives the whole text as one piece.
- **`TryParse` takes a number and nothing else.** An int is digits, with `-` before them for a negative one. A float can have a point and an exponent too: `-12.5`, `.5`, `1e-3`. Spaces, a `+`, hex and words like `NaN` make it false, and so does a number its type doesn't hold. `Trim` the text first when it's what a player typed. When it's false, `value` is 0.
- **A float read from text is the nearest one**, worked out exactly, the same bits on every platform. The same holds for doubles at their full precision. What `$"{x}"` shows reads back as the original value.
- **`FromBytes` gives a [`string?`](./errors.md)**, since bytes from a file or a server can be anything: `?? ""` for a fallback, or `is string text` to run only when they're text.

For JSON, there's [`Json.Read` and `Json.Write`](./json.md), which read and write a struct whole.

### Characters

`char` is one character of text, written in single quotes: `'a'`. `text[i]` reads a text's character at `i`, and `foreach` goes through them in order. That's what reading text a piece at a time is made of: a level drawn in characters, a line to cut at a space, a number typed by a player.

```csharp
enum Tile { Grass, Water, Rock }

singleton Board
{
    Tile[64] tiles;
    int2 size;
}

// A level's plan: a character a tile, with '/' between its rows
void LayOut(mut Board board, string plan)
{
    mut var x = 0;
    mut var y = 0;
    foreach (var mark in plan)
    {
        if (mark == '/')
        {
            x = 0;
            y += 1;
            continue;
        }
        switch (mark)
        {
            case '~':
                board.tiles[y * 8 + x] = Tile.Water;
                break;
            case '^':
                board.tiles[y * 8 + x] = Tile.Rock;
                break;
            default:
                break;
        }
        board.size = int2(Math.Max(board.size.x, x + 1), y + 1);
        x += 1;
    }
}

// A whole number at the start of some text, or -1
int Leading(string text)
{
    mut var number = -1;
    foreach (var c in text)
    {
        if (c < '0' || c > '9') break;
        number = Math.Max(number, 0) * 10 + (c - '0');
    }
    return number;
}
```

- A literal is one character, any of Unicode's (`'é'`, `'€'`), or one of the escapes `\'`, `\"`, `\\`, `\n`, `\r`, `\t` and `\0`.
- `text[i]` counts characters the way `length` does. Before the start and past the end it gives `'\0'`, which is no character: text shows it as nothing, so `"a" + name[99]` is `"a"`. It's a char's zero, and its `default`.
- Text only changes whole, so `name[0] = 'x'` is an error. Make the new text of its pieces: `name.Substring(0, i) + c + name.Substring(i + 1)`.
- A `foreach` goes through the text as it was when the loop started, whatever its variable becomes on the way.
- Chars compare with `==`, `!=`, `<`, `<=`, `>` and `>=`, in Unicode's order: `c >= 'a' && c <= 'z'` is a small letter.
- A char goes where an int goes, as its number in Unicode (its code point): `int code = c;`, `var digit = c - '0';`. What an operator gives of one is an int, never a char: `char(n)` makes an int the character, so `char(c + 1)` is the one after `c`. A number no character has (under 0, past `0x10FFFF`, or from `0xD800` to `0xDFFF`) gives `'\0'`.
- `switch` works on a char, with chars as its cases, and a char is a map's or a set's key: `Map<char, int> counts;`.
- Text joins a char as its character: `"a" + 'b'` is `"ab"`, and `$"{c}"` shows it. Inside a value it's in its quotes, like text: `Glyph { mark = 'a' }`, `['a', 'b']`.
- A char is a plain value of four bytes, so it goes where an int does: components, structs, arrays (`char[16]`), lists, a grid's cells, inputs, commands and events.

To go through a text, use `foreach`: it reads each character once. `text[i]` is as quick where every character is one byte (English letters, digits, punctuation); in text with accents, other alphabets or emoji it has to count its way to `i` each time, so a `for` over a long one gets slow.

## Lists

`List<T>` is a list of values. It starts empty, and `[a, b, c]` makes one where the type is known from where it goes:

```csharp
component Inventory
{
    List<int> items = [1, 2, 3];
}

system Collect(mut Inventory inventory, Pickup pickup)
{
    if (!inventory.items.Contains(pickup.item)) inventory.items.Add(pickup.item);
}
```

`var x = [1, 2]` is an error, since it doesn't say the type.

### Members

- `count`, `items[i]` to read, and `items[i] = x` or `items[i] += x` to write.
- `Add(item)`, `Insert(index, item)`, `RemoveAt(index)` and `Clear()`.
- `Resize(count)` makes the list `count` elements long in one go. New elements are the element type's zero, empty for text. With fewer, the rest go. A count under 0 is 0.
- For elements that `==` compares (numbers, bools, chars, enums, text, entities and players): `Contains(item)`, `IndexOf(item)` (-1 if it's not there) and `Remove(item)`, which returns whether it found one.

Past the end, a read gives the element type's zero and a write does nothing.

### Elements are copies

Taking a whole element into a local, an argument or a `foreach` variable gives a copy. Changing that copy never changes the list:

```csharp
mut var slot = inventory.slots[i];
slot.count += 1;
inventory.slots[i] = slot;
```

Plain fields of struct elements can also be [changed where they are](#changing-an-element-where-it-is): `inventory.slots[i].count += 1`. A `mut` method or a `mut` argument can't change an element this way; use a mutable local and put it back. A struct element's text or collection fields also need that copy-and-replace form.

Changing a list needs something that can change: a `mut` component or singleton, or a `mut` local.

Elements are built-in types, text, enums and structs. ECS data doesn't go in lists: keep an `Entity` instead.

### Lists in structs

A struct can hold a list, a map or text, and it's still a value like any other: a field of a component or a singleton, a local, a parameter. Copying the struct copies what it holds.

```csharp
enum Card { Vitality, RapidFire, Scatter }

struct Build
{
    float maxHealth = 100;
    List<Card> cards; // The upgrades picked so far, in order
}

component Fighter
{
    Build build;
    float health = 100;
}

system Upgrade(mut Fighter fighter, Draft draft)
{
    if (!draft.picked) return;
    fighter.build.cards.Add(draft.card);
    if (draft.card == Card.Vitality) fighter.build.maxHealth += 35;
}
```

## Arrays

`T[N]` is a fixed number of values side by side in whatever holds them: `int[32] cells;`, `string[3] tabs;`, of up to 4,096 elements (an [audio declaration's state](../game/audio.md#echoes-and-reverbs) holds far longer ones, for an echo's delay line). An array is part of its component's bytes, like its other fields, so a system's loop over rows runs with SIMD, and there's no list to look up. The length is a constant: a number, or a `const` and arithmetic on it, up to 4096 elements with every level of `int[4][8]` counted together.

```csharp
const int CAP = 32;

struct Ring
{
    int[CAP] cells;
    int head;
}

component Trail
{
    float3[16] points;
    int next;
}

system Record(mut Trail trail, Body body)
{
    trail.points[trail.next] = body.position;
    trail.next = (trail.next + 1) % trail.points.count;
}
```

Elements are any value but a grid: numbers, bools, chars, enums, vectors, quaternions, matrices, `Color`, `Rect`, `Entity`, `PlayerID`, text, lists, maps, sets, structs of any of those, and arrays.

```csharp
local singleton Menu
{
    string[3] tabs = ["Video", "Audio", "Profile"];
    List<string>[2] logs;   // Two lists, each its own
    int picked;
}
```

An array of plain values is its holder's bytes and nothing more, so a system's loop over it runs with SIMD and an input can hold one: a fixed number of values a player sends, which `[Clamp]`, `[Min]` and `[Max]` bound element by element. An array of text or lists keeps each element's text or list where a field's would be, its holder's own, so copying the array copies them and nothing is shared. Inputs and code on the GPU take no text or lists, in arrays or anywhere.

`a[i]` reads an element, or zero past the end; `a[i] = v` and `a[i] += v` write one, and do nothing past the end. `a.count` is the length, `foreach` goes through the elements (each a copy), `a = b` copies the whole array, and `a = [1, 2, 3]` sets it from a literal of exactly `count` values. Text shows one as it shows a list: `[1, 2, 3]`.

Lists, arrays and map values also hold `Clip`, `Texture`, `DepthTexture`, `Cubemap`, `RenderTarget`, `Sound`, and structs holding them. These collections belong in local state, just like their elements. Copies and helper parameters follow the ordinary collection rules:

```csharp
struct Layer { Clip clip; float volume; }
local singleton Mixer
{
    List<Layer> layers;
    Clip[2] accents;
}
```

Playing `Sound` elements are collected automatically each frame. Their voices follow list/array positions; map voices follow insertion order. Rendering references inside collections are rebound on reload as ordinary resource fields are. Grids, ECS declarations and borrowed contexts still cannot be collection elements; mesh values store stable asset IDs, so mesh collections can persist without copying geometry.

## Changing an element where it is

A list's element, a map's value or an array's element that's a struct is changed where it is when the field written is a plain value: `rings[i].head = 0`, `rings[i].cells[j] = v`, `trail.points[i].x += 1`. A missing map key is added when its value is written this way. Through arrays alone, text and lists are written where they are too, as an array is its holder's own bytes: `menu.tabs[1] = "Sound"`, `shelf.bags[0].label = "Tools"`, `log.lines[2] += "!"`, `menu.logs[0].Add("x")`. Text or a collection reached through a list's element or a map's value, and anything in a list or a map inside the struct element, is still taken out, changed and put back (`mut var r = rings[i]; r.names.Add("x"); rings[i] = r;`), and so is a grid's cell. Reading `rings[i].head` or `rows[i][j]` copies nothing either way.

## Lists inside lists

A list's elements can be lists, maps or sets, and structs that hold them, as deep as you like: `List<List<int>>`, `List<Build>` with the `Build` above, `Map<int, List<Entity>>`. A map's values can too. What can't go inside another container is a grid, which is never copied, and a key or a grid's cell is still a plain value.

An element that's a container is changed where it is, through any number of indexes:

```csharp
component Item { }

singleton Table
{
    List<List<int>> rows;
    Map<int, List<Entity>> buckets;
}

system Fill(mut Table table, with Item)
{
    table.rows.Add([1, 2]);   // A new row
    table.rows[0].Add(3);     // Into the row where it is
    table.rows[0][1] = 20;
    table.rows[0][2] += 1;
    table.buckets[7].Add(this); // This Item's entity; adds the key 7 if it isn't there
}
```

Past the end of the list it's in, such a write does nothing, as any write past the end does. A list inside a struct element is changed by taking the struct out and putting it back (`mut var b = bags[i]; b.parts.Add(x); bags[i] = b;`), and an inner list is sorted the same way.

Whatever takes a whole element out copies it, its lists too: a `foreach`'s variable, a local, a map's `Get`. Changing the copy changes nothing in the list it came from. Reading `rows[i][j]` or `rows[i].count` copies nothing, so a loop that only counts or reads is best written with indexes:

```csharp
int Cells(Table table)
{
    mut int total = 0;
    for (var i = 0; i < table.rows.count; i++) total += table.rows[i].count;
    return total;
}
```

Text shows them inside out, `[[1, 20, 4], [3]]`. Nested collections don't go to C or into a `parallel` loop, whose list elements must be plain values. Inputs hold no lists; a [command](../game/commands.md#big-commands) can hold `List<byte>` for bytes, but no other list type.

Rows that all have one length need no second list: `cells[row * width + column]`, or a [grid](./grids.md).

### Sorting

`Sort` sorts a list by a comparison written after the call. Its two names are the elements it compares, and the block says whether the first goes before the second:

```csharp
system Rank(mut Scoreboard board)
{
    board.players.Sort(a, b) { return a.score > b.score; }
}
```

The sort is stable: players with the same score keep the order they were in. The comparison can read anything around it, like a variable or a field, but it changes nothing: it runs again and again as the list sorts, in an order nothing promises, so it can't spawn, send, wait, draw or write outside itself either, and tidec says so. It returns a `bool` on every path.

```csharp
var center = arena.center;
enemies.Sort(a, b)
{
    var da = Math.Distance(a.position, center);
    var db = Math.Distance(b.position, center);
    if (da != db) return da < db;
    return a.id < b.id;
}
```

Sorting works in systems, views, handlers and plain functions; not yet in methods or async code.

The bars below are a `List<int>`, and each button changes it with one of its members. The line at the top is the list itself, as text shows it.

::: tide-example lists A list, changed and sorted

<<< @/examples/lists.tide

:::

### Going through every element at once

A `parallel` loop goes through a list's elements at once, on threads, as it goes through a grid's cells (see [Grids](./grids.md#going-through-every-cell)). Its places are the elements' indices:

```csharp
struct Body
{
    float3 position;
    float3 velocity;
    float mass;
}

singleton Space
{
    List<Body> bodies;
}

// Every body is pulled by every other, from where they all were
system Gravity(mut Space space, Time time)
{
    var n = space.bodies.count;
    parallel (var i in space.bodies)
    {
        mut var body = space.bodies[i];
        mut var pull = float3(0);
        for (var j = 0; j < n; j++)
        {
            var other = space.bodies[j];
            var d = other.position - body.position;
            var r2 = Math.Dot(d, d) + 0.01;
            pull += d * (other.mass / (r2 * Math.Sqrt(r2)));
        }
        body.velocity += pull * time.dt;
        space.bodies[i] = body;
    }
}
```

Each step reads the list as the loop found it and changes only its own element, `space.bodies[i]`, so the result is exactly the same on one thread as on many. `by 2 offset o` gives each step a block of elements, from `i` up to `i + 1`, which it changes as `items[i]` and `items[i + 1]`: a block that would go past the end is left out. The rules are a grid's parallel loop's: a step changes nothing else, not the list's count either, and tidec says what to write instead.

The list is one the world keeps, a component's or a singleton's, and its elements hold no text: the steps run on threads, where text can't be made.

## Maps and sets

`Map<K, V>` holds values by key, and `Set<K>` holds keys, each once. Both start empty, and `[a, b]` makes a set where one goes:

Use `[]` for an empty map wherever its type is known: `Map<string, Stats> scores = [];`, `scores = [];`, or `return [];` from a function returning that map type. `default` also gives an empty map. Add nonempty entries with `scores[key] = value`.

```csharp
component Scoreboard
{
    Map<PlayerID, int> scores;
    Map<string, Stats> byName;
    Set<Entity> tagged = [];
}

system Score(mut Scoreboard board, Hit hit)
{
    board.scores[hit.by] += 10;            // Adds the key, or changes its value
    if (board.tagged.Add(hit.target))      // Whether it was new
        board.byName["last"] = Stats { wins = board.scores[hit.by] };
}
```

Keys are `byte`, `int`, `long`, `char`, enums, `Entity`, `PlayerID` or `string`: values with a hash and `==`. Floats and doubles aren't keys, since two that look equal can differ in a bit; key by an int instead. Values are what a list's elements can be.

### Members

- `count`.
- A map: `scores[key]` to read, which gives the value type's zero when the key isn't there, and `scores[key] = value` or `scores[key] += x` to write. `Has(key)`, `Get(key)` (the value, or nothing, as a `V?`), `TryGet(key, out value)` (whether the key is there, and its value), `Remove(key)` (whether it was there) and `Clear()`.
- A set: `Add(key)` (whether it was new), `Has(key)`, `Remove(key)` and `Clear()`.
- `foreach (var e in scores)` goes through a map's entries in the order their keys were added, each with `key` and `value`; `foreach (var k in tagged)` goes through a set's keys the same way. Entries added during the loop are reached too, and entries removed before their turn are skipped, including when the loop waits between entries. The order is the same on every machine.

```csharp
foreach (var e in board.scores)
    GUILayout.Label($"{e.key}: {e.value}");
GUILayout.Label($"{board.scores}"); // {PlayerID(0): 30, PlayerID(1): 10}
```

`TryGet` looks a key up once and says whether it was there. Its second argument is an [`out` argument](./functions.md#out-parameters), so it can declare the variable the value goes in:

```csharp
if (board.byName.TryGet("last", out var stats))
    GUILayout.Label($"last: {stats.wins}");
```

Where the key isn't there, the variable gets the value type's `default`.

A whole value taken out of a map is a copy, like a list's element: take it out into a mutable local, change it and put it back. A plain field of a struct value can be [changed where it is](#changing-an-element-where-it-is), and a value that's a collection can be changed through its key: `buckets[key].Add(entity)`. A map taken out of a field is a copy too.

Maps and sets go in components, singletons, structs and events. They also nest in lists and as map values, as [lists inside lists](#lists-inside-lists) do. Inputs and grid cells don't hold them. Keys stay the types listed above; a collection or a struct can't be a key.

Choose a name below and add a score. A missing name joins the map; removing and adding it again puts it last in insertion order. Tagging it twice leaves one tag, and `TryGet` shows both whether its score exists and the value it gives.

::: tide-example maps Scores by key, tags only once

<<< @/examples/maps.tide

:::

## Where they live

A world keeps the text, lists and maps in its components in its **heap**, which is part of the world. So a snapshot has them with everything else. The heap grows as it needs, as far as memory goes, and one text in it is 128 MiB at most.

Text and lists that code makes along the way, joining text, say, live in a scratch area that's cleared once the system, view or handler is done, and are only copied into the world when they're stored in it. The scratch area grows as it needs too, on each thread, and a list stops growing at 2,147,483,647 elements. Running out of memory ends the program. On the web, everything shares WebAssembly's 4 GiB.

An input can't hold text or dynamic collections: what players send every tick is plain data, including vectors, structs and fixed-length arrays (see [Input](../game/input.md)).

Systems that change the match's text or collections wait for each other, since they share one heap.
