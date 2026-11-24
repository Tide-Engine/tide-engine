---
description: "Declare player actions in a Tide multiplayer match as commands, call them like functions, check who sent them and control delivery, ticks and ordering channels."
---

# Commands

A command is an action a player asks of the match: submit a name, choose a team, equip an item, or press a ready button. It is one declaration, with the code the match runs for it, and it's called like a function.

## Declaring

```csharp
component Name { string value; }

command(string name) Rename(Owner owner, mut Name mine, PlayerID sender)
{
    if (sender != owner.player) return;
    if (name.length == 0 || name.length > 32) return;

    mine.value = name;
}
```

The first parentheses hold what a call passes: `name` here. Those values are copied and cross the network. The second hold what the match gives the code, as an [event handler's](./events.md) parameters do: the components of the entity the command is for, singletons, queries and filters, and `PlayerID sender`, the player who called it. By convention, `sender` goes after the components and before the filters.

The code reads what it was called with by name, and can't change it. A value can't share its name with a parameter of the second parentheses, so the `Name` component above is `mine`.

Each value is a type and a name. It can have a default, and the attributes a field takes:

```csharp
command(int item, [Clamp(1, 99)] int count = 1) Buy(mut Purse purse, with ownership)
{
    purse.coins -= Price(item) * count;
}
```

## Calling

Views, local handlers, this machine's tasks and the functions they call can call a command. The text field and other UI state stay local until the player submits them:

```csharp
local singleton RenameUI { string draft; }

view RenameMenu(Name mine, mut RenameUI ui, with ownership)
{
    GUILayout.Label(mine.value);
    GUILayout.TextField("Name", ui.draft);
    if (GUILayout.Button("Rename")) Rename(ui.draft);
}
```

A call is written as a function's: arguments by place, then by name, and the declared default for each value left out. `Buy(3)`, `Buy(3, count: 2)` and `Buy(item: 3, count: 2)` all call the command above. Leaving out a value with no default is an error. A command in a namespace is called as a function there is: `Shop.Buy(3)`.

The engine copies the values when the command is called, so editing the field afterwards does not change the queued command. Local code can call commands while connected to a match. Single-player uses the same queue and the same code as multiplayer.

### Which entity

`entity.Rename("Ada")` calls the command for that entity. A call that names none, in code that runs for an entity of the match, is for that entity: `Rename(ui.draft)` above is for the entity `RenameMenu` runs for, and `this.Rename(ui.draft)` says the same. A view, system or handler runs for an entity when it takes components or an entity filter.

```csharp
view Board(Devices devices, query (Body body, with Crate) crates)
{
    var at = Draw.ScreenToWorld(Screen.Flip(devices.pointer.position));
    foreach (var crate in crates)
    {
        if (devices.pointer.press.down && Math.Distance(at, crate.body.position) < 0.5) crate.entity.Kick();
    }
}
```

In the command's code, components and `this` refer to that entity, as in an event handler. A command that takes components, or [filters by ownership](#only-the-sender-s-own-entities), needs an entity: a call with none, in code that runs for no entity, is an error. A command with neither runs once for each call, whatever entity the call names. An entity that no longer exists, or no longer matches the command's parameters and filters, has no effect.

## The sender

The engine supplies `sender` from the player's connection. It is never something a call passes. `Owner` identifies the entity's owner; calling a command for an entity does not grant permission to change it.

### Only the sender's own entities

Most commands for an entity begin by checking that the sender owns it. `with ownership` says that in the declaration, and the guard and the `Owner` parameter go:

```csharp
command(string name) Rename(mut Name mine, with ownership)
{
    if (name.length == 0 || name.length > 32) return;
    mine.value = name;
}
```

The code runs only when the entity the command is for has an `Owner` whose `player` is the command's sender. A call for any other entity is dropped, as the guard dropped it: it counts as handled, a resend of it is refused, and nothing changes. `without ownership` is the other side, any entity the sender doesn't own, another player's or nobody's, for something like an attack:

```csharp
command(int amount) Steal(mut Purse purse, PlayerID sender, without ownership)
{
    purse.coins -= amount;
}
```

It's the word a [view](./views-and-the-match.md) filters with, where it means this machine's player; in a command, the player is the one who called it. A command that filters this way can leave `PlayerID sender` out, or take it to read it. One that does neither is an error: a command names the player who sent it, so it can check what that player may do. Systems and event handlers can't filter by ownership: nothing there is a player's, so write `with Owner` for entities any player owns.

Below are two machines in one match, each with a badge its player owns. Click the name's field in a frame, type one, and press **Rename my badge**: the call goes to the server, which picks its tick, and both machines run the command. **Rename theirs** calls the same command for the other player's badge. Nothing stops a machine from calling it, so the command looks at who did, refuses, and counts.

::: tide-match commands A command that says no

<<< @/examples/commands.tide

:::

## What the code does

A command returns nothing. Guard clauses end it, and writes take effect normally, just as in a system or event handler. There is no acceptance call, return status, or transaction that undoes earlier changes. Each command has one body of code. It can send ordinary events when other parts of the game should react.

A command that isn't `async` also takes read-only `query` parameters, using the same [query rules as match events](./events.md#querying-other-entities). A query selects a separate set of entities, so a command taking only queries and singletons needs no entity. It reads the current match at the command's turn in the tick-end queue; recorded events and structural changes remain deferred. Check the sender's permissions before acting on queried entities, as with the command's own entity.

## From systems and bots

Match code can call a command too: a system, or one of the match's event handlers. Nothing is sent. The call is queued as an event's `Send` is, and the command's code runs at the end of the tick with the tick's other commands and events, in the order they were queued.

```csharp
command(int slot) PickCard(mut Hand hand, with ownership)
{
    if (slot < 0 || slot > 2) return;
    hand.picked = slot;
}

// A bot picks with the command a player's button calls
system BotPicks(Bot bot, Hand hand)
{
    if (hand.picked < 0) PickCard(bot.favorite);
}
```

The match is the authority, so its call differs from a player's in two ways:

- Its `sender` is no player: `default`. Code that compares `sender` with a player refuses it as it would any other player's call.
- `with ownership` and `without ownership` both pass, for an entity with no `Owner` too. The filter checks what a player may do, and no player is asking.

A match's call has none of the wire's limits below: it isn't packed, counted against a player's pending commands or given a sequence. It is part of the tick that made it.

A function that calls a command belongs to local code. Calling such a function from a system is an error, which says to call the command from the system itself.

## Delivery and ticks

Commands called by local code are queued independently of input. Multiple calls between ticks remain separate commands, and a late command is not lost because its original input tick passed. The engine resends pending commands and identifies duplicates by a per-player sequence.

The server chooses the tick and order. Commands run at the end of that tick with the existing event and structural-change queue. Each sees the changes made by the commands that ran before it. Full-world games send recorded commands with tick history so every machine replays that order. With [state visibility](./visibility.md), commands run only on the authoritative server; clients receive their permitted state consequences rather than the commands' entities or values.

Sequence state belongs to the world: snapshots, state hashes and host migration include it. A received command occupies one entry in the authoritative timeline, but its code can run again during rollback. External side effects must account for replay, as in any simulation code that calls C.

## Ordering: channels

A player's commands run in the order they were called, each after every earlier one: the server holds a command back until the one before it has arrived. That's right for most actions, and wrong for a big one: a chat line called after a file would wait until the file had been uploaded.

A command's ordering is its own. Without an attribute, a command is in the **default channel**, in order with every other command of that channel. `[Channel(...)]` with an enum member of the game's puts it in a channel of its own: in order among that channel's commands, and in no order against the others, so a chat line never waits behind an upload. `[Unordered]` gives a command no order at all: the server runs it in whichever tick it arrives, before or after commands called earlier, still exactly once. Both go before the declaration:

```csharp
enum Channels { Uploads }

// The default channel
command(string text) Say(mut Chat chat, PlayerID sender) { chat.last = text; }

// Its own: a Say never waits for one
[Channel(Channels.Uploads)]
command(List<byte> png) SetBackground(mut Wall wall, PlayerID sender) { wall.png = png; }

// Whenever it arrives, once
[Unordered]
command(int at) Ping(mut Lag lag, PlayerID sender) { lag.last = at; }
```

Whatever the ordering, the server picks the tick and every command runs in the tick's end queue, in the server's order: a channel only decides what the server holds back waiting for an earlier command. Commands can share a channel, and a channel's name can come from any enum. Each channel has a sequence of its own in the world, and an unordered command has an id of its own, which the world keeps a window of the last 64 of, per player, to drop a resend. The 32 pending commands per player are per channel, so a full channel holds back only its own.

## Big commands

A command takes a `List<byte>` too, so a player's file goes into the match as it is: an image's bytes, a text's, any file's, for the command to keep in a component or decode. Such a command is 4 MiB at most. One that doesn't fit a datagram goes as a numbered transfer in chunks, the way worlds go, resent until acknowledged and paced under the input traffic (16 KB an update, about a megabyte a second), so a transfer never delays a tick. It lands in a later tick, as any command may.

A command takes lists of other values as well: numbers, bools, enums, vectors, entity or player IDs, and structs of those, as a value or in a struct the command takes.

```csharp
struct Slot { [Clamp(0, 9)] int level; float weight = 1; }

// Each element of `attributes` is bounded
command([Clamp(0, 20)] List<int> attributes, List<Slot> slots) Enlist(mut Recruit recruit, with ownership)
{
    recruit.attributes = attributes;
    recruit.slots = slots;
}
```

Each element goes on the wire as a value of its type would, after the list's count, and is checked the same way: a float that isn't finite becomes the element type's default, an enum value no member has becomes its default, and `[Clamp]`, `[Min]` and `[Max]` bound each element, on the list or on a struct's fields. A command with a list follows the same rules as one with a `List<byte>`: 4 MiB at most, sent in chunks when it doesn't fit a datagram. A list of text or of other lists can't be passed: pass text as string values or a fixed `string[N]`, or its bytes as a `List<byte>`. Commands can't take maps, sets or grids; a map's keys and values can go as two lists.

In full-world games, the server distributes a big command to every player before scheduling it, so no player's tick waits for its download; later joiners receive the resulting world. Visibility-enabled games keep that upload server-side and replicate only permitted state consequences. A few megabytes is fine; larger public media can be a URL fetched by each machine. Put a big command in a channel of its own, or make it unordered, so nothing waits behind it.

## Untrusted payloads

What a player's call passes arrives from that player's machine. The engine checks its structure and size, UTF-8, numeric values and declared bounds before the command's code reads the values. A command takes numbers, bools, enums, vectors, entity or player IDs, strings, `List<byte>`, lists of those but strings, and structs of those; it does not take lists of text or lists, maps, sets, grids, components or device records.

The initial limits are 1024 packed bytes per command (4 MiB for one with a list), 32 pending commands per player in each channel, and 16 commands scheduled per authoritative tick. The packed limit includes the command's id, entity and string bytes. Incoming queues are bounded before allocation. A full session queue leaves local calls of that channel waiting for capacity. Calling with values larger than the packed limit, or filling a channel's local queue, is a programming error that stops the program with a diagnostic.

Starting or leaving a match clears its pending commands. Host migration preserves them. A hot reload that changes the game's data layout clears pending packed commands, because the new build can give them a different wire layout.

Those checks establish well-formed data. The command's code checks permissions, allowed choices, affordability, cooldowns and any other rules of the game. A valid name, item ID or entity is not permission to act on it.

Use [input](./input.md) for state sampled each tick, commands for submitted actions, and [events](./events.md) for notifications produced inside the simulation.
