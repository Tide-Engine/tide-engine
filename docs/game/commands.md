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

## Delivery and ticks

Commands are queued independently of input. Multiple submissions between ticks remain separate commands, and a late command is not lost because its original input tick passed. The engine resends pending commands and identifies duplicates by a per-player sequence.

The server chooses the tick and order. Handlers run at the end of that tick with the existing event and structural-change queue. Each handler sees the changes made by the commands handled before it. The server sends the recorded commands with its tick history, so every machine replays that order.

Sequence state belongs to the world: snapshots, state hashes and host migration include it. A received command occupies one entry in the authoritative timeline, but its handler can run again during rollback. External side effects must account for replay, as in any simulation code that calls C.

## Untrusted payloads

The engine checks payload structure and size, UTF-8, numeric values and declared bounds before match code sees the fields. A command holds numbers, bools, enums, vectors, entity or player IDs, strings, and structs of those; it does not hold lists, grids, components or device records.

The initial limits are 1024 packed bytes per command, 32 pending commands per player, and 16 commands scheduled per authoritative tick. The packed limit includes the command type, target and string bytes. Incoming queues are bounded before allocation. A full session queue leaves local submissions waiting for capacity. Sending a payload larger than the packed limit, or filling the local submission queue, is a programming error that stops the program with a diagnostic.

Starting or leaving a match clears its pending submissions. Host migration preserves them. A hot reload that changes the game's data layout clears pending packed commands, because the new build can give their types a different wire layout.

Those checks establish well-formed data. The handler checks permissions, allowed choices, affordability, cooldowns and any other rules of the game. A valid name, item ID or target is not permission to act on it.

Use [input](./input.md) for state sampled each tick, commands for submitted actions, and [events](./events.md) for notifications produced inside the simulation.
