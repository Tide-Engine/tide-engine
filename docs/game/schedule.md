---
description: "Read Tide's system schedule and see how the compiler uses data dependencies to run ECS systems across threads without changing results."
---

# The schedule

Every system declares what it reads and writes, so the compiler knows, before the game runs, which systems can run at the same time and which have to wait. That plan is the **schedule**.

The tick follows it on every core: a system starts as soon as what it waits for is done, and big systems split their entities across threads too. Running in parallel never changes results, so every machine still gets the same match. A small tick, where waking the other threads would cost more than they'd save, runs on one thread: under about half a millisecond of work, by the time its systems took in the ticks before. On the web, ticks run on threads when the page is cross-origin isolated (see [The tide command](../tools/cli.md#threads-on-the-web)), and on one thread elsewhere.

## Reading it

`tide schedule` prints systems grouped by stage, keeping their tick order within each stage. This example is from a demo where balls bounce around an arena:

```
demo: 5 systems each tick, 4 stages deep; 5 can run alongside others.
A system starts once everything it runs after is done.

stage 1  Steer  (per entity)
         writes Body, Aim
         alongside Expire
stage 1  Expire  (per entity)
         writes Lifetime
         alongside Steer, Fire, Move, Bounce

stage 2  Fire  (per entity)
         reads Body, Aim
         after Steer: it writes Body and Aim, which this reads
         alongside Expire

stage 3  Move  (per entity)
         reads Time
         writes Body
         after Steer: both write Body (and through Fire)
         after Fire: it reads Body, which this writes
         alongside Expire

stage 4  Bounce  (per entity)
         reads Arena
         writes Body
         after Steer: both write Body (and through Fire)
         after Fire: it reads Body, which this writes (and through Move)
         after Move: both write Body
         alongside Expire

Views, every frame in this order (they only read, so nothing waits):
         DrawArena
         DrawBalls
         DrawPlayers
         Options

Event handlers, at the end of the tick, as their events are sent:
         Spawned: Setup
```

`Steer` and `Expire` are both stage 1: neither waits for another system. `Expire` only touches `Lifetime`, so it can run alongside everything else. The others all work on `Body`, so they wait for each other, in the order they're written. Stages show dependency depth; `Fire` can start when `Steer` finishes, even if `Expire` is still running.

In the editors, each system shows its stage and why it waits right above it, like `stage 2 · after Move: both write Body`, and on hover.

Each event's handlers are listed in the order they run. A game with [local handlers](./events.md#local-handlers) of the match's events has those listed after the match's, apart, since they run on each machine at the start of a frame and not in the tick. The ones that wait for the server say `(verified)`:

```
Event handlers, at the end of the tick, as their events are sent:
         Spawned: Setup
         Hit: TakeHit

Local handlers of the match's events, at the start of a frame, for what the ticks since the last one sent:
         Destroyed: Mourn (verified)
         Hit: Count (verified), Flash
         Glanced: Spark
```

### Through the World

A system's parameters say what it reads and writes of the entities it runs for. What it reaches through a [`World`](./systems.md#other-entities-the-world) parameter is any entity's, and its signature doesn't name those components, so the schedule does, on lines of their own:

```
stage 1  Heal  (per entity)
         reads Healer
         reads any entity's Armor, through World
         writes any entity's Health, through World
         alongside Aim, Chase

stage 2  Drain  (per entity)
         writes Health
         after Heal: both write Health
```

The editors show it above the system too, `stage 1 · World: reads Armor; writes Health`, and when you hover the system or its `World` parameter.

A system that passes its `World` to a function reaches what the function does, and it's listed the same way: the schedule doesn't tell the two apart.

[Read-only query parameters](./systems.md#another-set-of-entities) also read components of a separate entity set. Those bindings count as reads of any entity's component and appear in the schedule and editor hints, so `query (Body body, with Fighter) fighters` waits for earlier systems that write `Body`, even when their outer entity filters differ. The query's `with` and `without` filters access no component data.

In a big game, the long lists are cut short. Every wait that's a system's own is there, but the ones that go through another system (`and through Fire`) and the systems it runs alongside are named up to 32 each and counted past that: `after 412 more, through those`, `alongside Expire, Fade and 168 more`.

## When systems wait

Two systems **conflict** when one writes a component or singleton the other reads or writes. Conflicting systems keep their order in the tick; `[Before]` and `[After]` order systems too. A system starts as soon as everything it waits for is done, with no barriers between stages: a system's stage is only how deep it is in that chain.

[Named phases](./systems.md#named-phases), assigned with `[Stage(MyStages.Simulation)]`, add explicit ordering requirements to that plan. Later phases wait for earlier ones, and higher offsets wait for lower offsets within a phase. These names are separate from the numbered dependency stages printed above: systems at one phase and offset can share a dependency stage and run together. The schedule explains these waits as requirements of the stage order.

Some things never make systems wait:

- **Filters.** `with` and `without` read nothing.
- **Entities that can't overlap.** Two systems that write the same component don't conflict if they can never touch the same entity, which the compiler proves from the archetypes: `with Player` against `with Enemy`, when nothing is both. A [sparse component](./entities.md#limits) doesn't count for this: entities with it and without it are one archetype, kept side by side, so `with Stunned` and `without Stunned` still wait for each other. A component a system reaches through its `World` or a read-only query is any entity's, so this doesn't hold for it: `world.Get<Health>(e)` and `query (Health health) targets` wait for every earlier system that writes `Health`.
- **Structural changes and events.** `Add`, `Remove`, `Destroy` and `Send` are recorded and applied at the end of the tick, in order. Event handlers run then too, so they aren't part of the tick's schedule.
- **Input and `Time`**, which systems only read. An entity's input is its owner's, though, so a system that reads its entities' input reads their `Owner`: it waits for an earlier system that changes it.
- **C.** Calls to C are trusted: they make no system wait, and a system that calls C splits across threads like any other. What C does when it's called from several threads at once is the game's to get right. A [system written in C](../c/functions.md#systems-written-in-c) is scheduled by what it declares, as its code can't be read, and shows as `(per entity, in C)`.

Some things systems wait for each other over whatever entities they run for:

- **Text, lists, maps, sets and grids.** Each component, singleton and event keeps what its fields hold of these in memory of its own, which one system changes at a time. So two systems that change the same component's text or lists wait for each other even when they run for different entities (`with Player` against `with Enemy`), and two that change different components' or singletons' don't. A `Spawn`, `Add` or `Send` whose values hold text or a list changes that component's or event's. `tide schedule` says whose: `both change the text, lists or grids of Party`. Reading them alongside is fine. What C can write is the exception: a component or singleton your code hands to a C function that may change it shares one such memory with every other one handed to C, so systems that change any of those wait for each other.
- **Spawns and queued effects.** Spawning makes no system wait: a spawn's identity comes from the entity and the place in the code that made it, and each system's entities are registered once it's done, in the tick's order. Structural and event effects keep system order, then the order of the entities that made them.
- **Tasks.** A system that starts [tasks](./tasks.md) runs their code until they first wait, which can spawn and change text, and the match keeps its tasks in the order they start. So it runs on one thread, and waits for the systems before it that start tasks, spawn or change text. Tasks that go on later do so after the tick's changes, one at a time, in the order they started: `tide schedule` says so at its end.

## Keeping it wide

- Take a component as `with` when a system only needs it to pick entities.
- Don't declare `mut` for what a system only reads. Unused access is a warning, and the editors offer a quick fix for it.
- Splitting a big component into the parts different systems use lets them run side by side.
- Reach other entities through the `World` only where the answer is needed this tick. An event sent to the other entity makes no system wait.

## Splitting across threads

The other kind of parallelism is a system splitting its entities across threads: each thread takes a run of them, 2,048 at most. How small the runs get depends on what the entities cost: the engine times each system as the match runs, so a few hundred entities that each take a lot of work are shared out over every core, down to a few dozen a thread, while a few hundred cheap ones stay on one thread, where they're done sooner than another thread would wake. It doesn't change results either, and needs nothing from you, but a system only splits when no entity's turn depends on another's. One that changes text, lists or grids (through a `T?` field too), or changes a singleton, runs on one thread, alongside the others. Writing an element of a fixed array in the entity's own component (`bot.seen[i] = x`) is a write to that component like any other, and splits. So does one that calls its `World`'s `Set`, which may change any entity, or that reads through its `World` or a query a component it also changes: it then sees the entities it already ran for as it left them, the same on every machine. Everything a system records (spawns, adds, removes, destroys, events) is applied in the order one thread would have recorded it: a system that records anything goes through its entities in the order of their identities, and threads take runs of that order.

For a per-entity system that stays on one thread, `tide schedule` says what keeps it there, and where in the code it first does:

```
stage 3  Tally  (per entity)
         reads Body
         writes Score
         on one thread: it changes the singleton Score (game.tide:42)
```

A spawn returns its final logical identity immediately; there is no temporary handle or later identity rewrite. Structural installation remains deferred. Queries executed by match/simulation code traverse ascending logical entity identity across archetypes. Presentation/view queries retain their existing traversal order. Extern systems receive contiguous runs in that same global order, while ordinary C function calls from otherwise independent parallel rows retain the game's existing determinism and thread-safety responsibility.

## Parallel loops

The third kind is inside a system: a `parallel` loop goes through a grid's cells, or blocks of them, all at once (see [Grids](../language/grids.md)). It can also go through a [list's elements by index](../language/text-and-lists.md#going-through-every-element-at-once). Its steps spread across the threads that are free, even while other systems run, and the system goes on once they're all done. Each step reads the collection as the loop found it and changes only its own cell, element or block, so the result is the same as on one thread. A foreach over a grid whose steps only touch their own cell runs the same way. The system itself waits for others as any system that writes the collection's component or singleton would, and the schedule shows its loops: `Fall  (once, 1 parallel loop)`.

`tide schedule` ends with every parallel loop: what one step of it is reckoned to cost, and from how many steps the loop is worth the threads, or what keeps its steps on one:

```
Parallel loops, and when their steps go to the threads:
         Crowd, line 107: a query's rows, a step reckoned at 4843: on the threads from 413 steps a frame
         Weigh, line 72: a list's elements, on one thread: its steps read 'scale', which lives on the thread that made it, as text, lists, maps, sets and grids in variables do
```

The reckoning is of the code as written: a step that returns on its first line counts as much as one that runs it all. A loop under its number runs in order on the thread that reached it, with the same results. The numbers are where a loop starts: as the game runs, the engine times each loop, and one whose steps take longer than their code looks goes to the threads from fewer.

A view that runs for each entity and starts with `if (...) return;` tests of this machine's own state (the entity's [local fields](./views.md), a local singleton's fields), or of the entity's `bool`, `int`, enum and `Entity` fields, has those tests run before anything else of the entity is copied, so a view that goes through a thousand entities to draw six costs little for the rest. `tide schedule` lists them:

```
Views that test an entity before copying anything of it:
         DrawChosen: 2 tests, down to line 90
         PoseChosen, the rows of its loop at line 120: 1 test, down to line 122
```

The second line is a view's query: a view that runs once and goes through its one query in one loop, whose steps start with `if (...) continue;` tests of the same kind, makes no row for an entity they turn away. The loop may be in a function the view hands its query to. The tests may also read fields of the match's that a frame shows exactly as the tick has them: a `bool`, an `int`, an enum or an `Entity`, as in `if (!row.t.laserOn || row.t.holstered) continue;`. A test that calls a function, or reads a field that's drawn between two ticks (a position, an angle), is run by the view's code as usual, after the entity's components are read.

## In CMake builds

In a CMake build of the engine's repo, `<game>_schedule` is a build target that prints it, and `tidec --schedule` prints it for any files.
