---
description: "Send player actions to a Tide multiplayer match with commands, validate their payloads and control delivery, ticks and ordering channels."
---

# Commands

A command is an action a player sends into the match: submit a name, choose a team, equip an item, or press a ready button. Declare its payload like an event:

```csharp
command Rename
{
    string name;
}
```

## Sending

Views send commands with `entity.Send(...)`, or `Send(...)` for an action on the whole match. The text field and other UI state stay local until the player submits them:

```csharp
component Name { string value; }
local singleton RenameUI { string draft; }

view RenameMenu(Name name, mut RenameUI ui, with ownership)
{
    GUILayout.Label(name.value);
    GUILayout.TextField("Name", ui.draft);
    if (GUILayout.Button("Rename"))
        this.Send(Rename { name = ui.draft });
}
```

The engine copies the payload when it is sent, so editing the field afterwards does not change the queued command. Sending is available while connected to a match. Single-player uses the same queue and handler as multiplayer.

## Handling

Declare a handler with `command`, like an event handler. Its first parentheses hold the payload and the sender; the second describe the components and singletons it uses:

```csharp
command(Rename rename, PlayerID sender) RenamePlayer(
    Owner owner, mut Name name)
{
    if (sender != owner.player) return;
    if (rename.name.length == 0 || rename.name.length > 32) return;

    name.value = rename.name;
}
```

The engine supplies `sender` from the player's connection. It is not a field that the player sends. `Owner` identifies the target's owner; targeting an entity does not grant permission to change it.

Below are two machines in one match, each with a badge its player owns. Click the name's field in a frame, type one, and press **Rename my badge**: the command goes to the server, which picks its tick, and both machines run the handler. **Rename theirs** sends the same command for the other player's badge. Nothing stops a machine from sending it, so the handler looks at who did, refuses, and counts.

::: tide-match commands A command, and a handler that says no

<<< @/examples/commands.tide

:::

A handler is void. Guard clauses end processing, and writes take effect normally, just as in a system or event handler. There is no acceptance call, return status, or transaction that undoes earlier changes. Each command can have at most one handler. The handler can send ordinary events when other parts of the game should react.

Components and `this` refer to the target entity, as in an event handler. A handler taking no components runs once for the command. The compiler requires an entity target if the handler needs one. A target that no longer exists, or no longer matches the handler, has no effect.

Synchronous command handlers also take read-only `query` parameters, using the same [query rules as match events](./events.md#querying-other-entities). A query selects a separate set of entities, so a handler taking only queries and singletons requires no entity target. It reads the current match at the command's turn in the tick-end queue; recorded events and structural changes remain deferred. Check the authenticated sender's permissions before acting on queried entities, as with any command target.

## Delivery and ticks

Commands are queued independently of input. Multiple submissions between ticks remain separate commands, and a late command is not lost because its original input tick passed. The engine resends pending commands and identifies duplicates by a per-player sequence.

The server chooses the tick and order. Handlers run at the end of that tick with the existing event and structural-change queue. Each handler sees the changes made by the commands handled before it. Full-world games send recorded commands with tick history so every machine replays that order. With [state visibility](./visibility.md), command handlers execute only on the authoritative server; clients receive their permitted state consequences rather than raw command targets or payloads.

Sequence state belongs to the world: snapshots, state hashes and host migration include it. A received command occupies one entry in the authoritative timeline, but its handler can run again during rollback. External side effects must account for replay, as in any simulation code that calls C.

## Ordering: channels

A player's commands are handled in the order they were sent, each after every earlier one: the server holds a command back until the one before it has arrived. That's right for most actions, and wrong for a big one: a chat line sent after a file would wait until the file had been uploaded.

A command's ordering is its type's. Without an attribute, a command is in the **default channel**, in order with every other command of that channel. `[Channel(...)]` with an enum member of the game's puts it in a channel of its own: in order among that channel's commands, and in no order against the others, so a chat line never waits behind an upload. `[Unordered]` gives a command no order at all: the server handles it in whichever tick it arrives, before or after commands sent earlier, still exactly once.

```csharp
enum Channels { Uploads }

command Say { string text; }                 // The default channel

[Channel(Channels.Uploads)]
command SetBackground { List<byte> png; }    // Its own: a Say never waits for one

[Unordered]
command Ping { int at; }                     // Whenever it arrives, once
```

Whatever the ordering, the server picks the tick and every handler runs in the tick's end queue, in the server's order: a channel only decides what the server holds back waiting for an earlier command. Commands can share a channel, and a channel's name can come from any enum. Each channel has a sequence of its own in the world, and an unordered command has an id of its own, which the world keeps a window of the last 64 of, per player, to drop a resend. The 32 pending commands per player are per channel, so a full channel holds back only its own.

## Big commands

A command holds a `List<byte>` too, so a player's file goes into the match as it is: an image's bytes, a text's, any file's, for a handler to keep in a component or decode. Such a command is 4 MiB at most. One that doesn't fit a datagram goes as a numbered transfer in chunks, the way worlds go, resent until acknowledged and paced under the input traffic (16 KB an update, about a megabyte a second), so a transfer never delays a tick. It lands in a later tick, as any command may.

In full-world games, the server distributes a big command to every player before scheduling it, so no player's tick waits for its download; later joiners receive the resulting world. Visibility-enabled games keep that upload server-side and replicate only permitted state consequences. A few megabytes is fine; larger public media can be a URL fetched by each machine. Put a big command in a channel of its own, or make it unordered, so nothing waits behind it.

## Untrusted payloads

The engine checks payload structure and size, UTF-8, numeric values and declared bounds before match code sees the fields. A command holds numbers, bools, enums, vectors, entity or player IDs, strings, `List<byte>`, and structs of those; it does not hold other lists, maps, grids, components or device records.

The initial limits are 1024 packed bytes per command (4 MiB for one with a `List<byte>`), 32 pending commands per player in each channel, and 16 commands scheduled per authoritative tick. The packed limit includes the command type, target and string bytes. Incoming queues are bounded before allocation. A full session queue leaves local submissions of that channel waiting for capacity. Sending a payload larger than the packed limit, or filling a channel's local submission queue, is a programming error that stops the program with a diagnostic.

Starting or leaving a match clears its pending submissions. Host migration preserves them. A hot reload that changes the game's data layout clears pending packed commands, because the new build can give their types a different wire layout.

Those checks establish well-formed data. The handler checks permissions, allowed choices, affordability, cooldowns and any other rules of the game. A valid name, item ID or target is not permission to act on it.

Use [input](./input.md) for state sampled each tick, commands for submitted actions, and [events](./events.md) for notifications produced inside the simulation.
