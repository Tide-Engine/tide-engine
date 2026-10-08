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

Text can show numbers, bools, enums (their member's name, or their number when [no member has it](./basics.md#from-a-number)), vectors and quaternions (`(1, 0.5)`), `Color`, `Rect`, entities (`Entity(3:1)`, or `Entity(new)` for one a system splitting its entities across threads just spawned, whose ID comes once it's done) and players (`PlayerID(0)`). Floats are written with the fewest digits that read back as the same float, exactly the same on every platform.

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
- `Contains`, `StartsWith`, `EndsWith` and `IndexOf`, which gives -1 when it's not there.
- `Substring(start)` and `Substring(start, length)`.
- `ToUpper` and `ToLower`, for ASCII letters for now.
- `Trim` and `Replace(from, to)`.
- `==` and `!=` compare text byte by byte. There's no `<` for text.

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
- For elements that `==` compares (numbers, bools, enums, text, entities and players): `Contains(item)`, `IndexOf(item)` (-1 if it's not there) and `Remove(item)`, which returns whether it found one.

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

`T[N]` is a fixed number of values side by side in whatever holds them, with nothing in the heap: `int[32] cells;`, of up to 4,096 elements (an [audio declaration's state](../game/audio.md#echoes-and-reverbs) holds far longer ones, for an echo's delay line). An array is part of its component's bytes, like its other fields, so a system's loop over rows runs with SIMD, and there's no list to look up. The length is a constant: a number, or a `const` and arithmetic on it, up to 4096 elements with every level of `int[4][8]` counted together.

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

Elements are plain values: numbers, bools, enums, vectors, quaternions, matrices, `Color`, `Rect`, `Entity`, `PlayerID`, structs of those, and arrays. Not text, lists, maps or grids, which need the heap. An array goes where a plain value goes, inputs included: a fixed number of values a player sends, which `[Clamp]`, `[Min]` and `[Max]` bound element by element.

`a[i]` reads an element, or zero past the end; `a[i] = v` and `a[i] += v` write one, and do nothing past the end. `a.count` is the length, `foreach` goes through the elements (each a copy), `a = b` copies the whole array, and `a = [1, 2, 3]` sets it from a literal of exactly `count` values. Text shows one as it shows a list: `[1, 2, 3]`.

## Changing an element where it is

A list's element, a map's value or an array's element that's a struct is changed where it is when the field written is a plain value: `rings[i].head = 0`, `rings[i].cells[j] = v`, `trail.points[i].x += 1`. A missing map key is added when its value is written this way. A field that holds text or a collection, and anything in a list or a map inside the struct element, is still taken out, changed and put back (`mut var r = rings[i]; r.names.Add("x"); rings[i] = r;`), and so is a grid's cell. Reading `rings[i].head` or `rows[i][j]` copies nothing either way.

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

Keys are `byte`, `int`, `long`, enums, `Entity`, `PlayerID` or `string`: values with a hash and `==`. Floats aren't keys, since two that look equal can differ in a bit; key by an int instead. Values are what a list's elements can be.

### Members

- `count`.
- A map: `scores[key]` to read, which gives the value type's zero when the key isn't there, and `scores[key] = value` or `scores[key] += x` to write. `Has(key)`, `Get(key)` (the value, or nothing, as a `V?`), `TryGet(key, out value)` (whether the key is there, and its value), `Remove(key)` (whether it was there) and `Clear()`.
- A set: `Add(key)` (whether it was new), `Has(key)`, `Remove(key)` and `Clear()`.
- `foreach (var e in scores)` goes through a map's entries in the order their keys were added, each with `key` and `value`; `foreach (var k in tagged)` goes through a set's keys the same way. The order is the same on every machine.

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

A world keeps the text, lists and maps in its components in its **heap**, which is part of the world. So a snapshot has them with everything else. The heap grows as it needs, up to 1 GiB, and one text in it is 128 MiB at most.

Text and lists that code makes along the way, joining text, say, live in a scratch area that's cleared once the system, view or handler is done, and are only copied into the world when they're stored in it. The scratch area grows as it needs too, up to 1 GiB on each thread, and a list stops growing at 1 GiB of elements. A heap or a scratch area that's full ends the program, as running out of memory does.

An input can't hold text or dynamic collections: what players send every tick is plain data, including vectors, structs and fixed-length arrays (see [Input](../game/input.md)).

Systems that change the match's text or collections wait for each other, since they share one heap.
