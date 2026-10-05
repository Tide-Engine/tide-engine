# The schedule

Every system declares what it reads and writes, so the compiler knows, before the game runs, which systems can run at the same time and which have to wait. That plan is the **schedule**.

The tick follows it on every core: a system starts as soon as what it waits for is done, and big systems split their entities across threads too. Running in parallel never changes results, so every machine still gets the same match. A small tick, where waking the other threads would cost more than they'd save, runs on one thread. On the web, ticks run on threads when the page is cross-origin isolated (see [The tide command](../tools/cli.md#threads-on-the-web)), and on one thread elsewhere.

## Reading it

`tide schedule` prints it. This is the one of the [demo](https://github.com/BlenMiner/tide-engine/blob/dev/demo/demo.tide) in the engine's repo, where balls bounce around an arena:

```
demo: 5 systems each tick, 4 stages deep; 5 can run alongside others.
A system starts once everything it runs after is done.

stage 1  Steer  (per entity)
         writes Body, Aim
         alongside Expire
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
stage 1  Expire  (per entity)
         writes Lifetime
         alongside Steer, Fire, Move, Bounce

Views, every frame in this order (they only read, so nothing waits):
         DrawArena
         DrawBalls
         DrawPlayers
         Options

Event handlers, at the end of the tick, as their events are sent:
         Spawned: Setup
```

`Expire` only touches `Lifetime`, so it can run alongside everything else. The others all work on `Body`, so they wait for each other, in the order they're written.

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
- **Entities that can't overlap.** Two systems that write the same component don't conflict if they can never touch the same entity, which the compiler proves from the archetypes: `with Player` against `with Enemy`, when nothing is both. A component a system reaches through its `World` or a read-only query is any entity's, so this doesn't hold for it: `world.Get<Health>(e)` and `query (Health health) targets` wait for every earlier system that writes `Health`.
- **Structural changes and events.** `Add`, `Remove`, `Destroy` and `Send` are recorded and applied at the end of the tick, in order. Event handlers run then too, so they aren't part of the tick's schedule.
- **Input and `Time`**, which systems only read.
- **C.** Calls to C are trusted: they make no system wait, and a system that calls C splits across threads like any other. What C does when it's called from several threads at once is the game's to get right.

Some things are the whole match's, so systems wait for each other over them whatever entities they run for:

- **Text, lists and grids.** Two systems that change the match's text, lists or grids conflict, since they share its heap. Reading them alongside is fine.
- **Spawns.** Entities get their IDs in order. A system that runs on one thread gives a new entity its ID as it spawns, so it waits for the systems before it that spawn. A system that splits its entities across threads doesn't wait: see below.
- **Tasks.** A system that starts [tasks](./tasks.md) runs their code until they first wait, which can spawn and change text, and the match keeps its tasks in the order they start. So it runs on one thread, and waits for the systems before it that start tasks, spawn or change text. Tasks that go on later do so after the tick's changes, one at a time, in the order they started: `tide schedule` says so at its end.

## Keeping it wide

- Take a component as `with` when a system only needs it to pick entities.
- Don't declare `mut` for what a system only reads. Unused access is a warning, and the editors offer a quick fix for it.
- Splitting a big component into the parts different systems use lets them run side by side.
- Reach other entities through the `World` only where the answer is needed this tick. An event sent to the other entity makes no system wait.

## Splitting across threads

The other kind of parallelism is a system splitting its entities across threads: each thread takes a chunk of them, up to 1,024. It doesn't change results either, and needs nothing from you, but a system only splits when nothing it does has to happen in order across its entities. One that changes text, lists or grids, or changes a singleton, runs on one thread, alongside the others. So does one that calls its `World`'s `Set`, which may change any entity, or that reads through its `World` or a query a component it also changes: it then sees the entities it already ran for as it left them, the same on every machine. Everything a system records (spawns, adds, removes, destroys, events) is applied in the order one thread would have recorded it.

A system that splits can spawn. Its `Spawn` gives a temporary handle, which works like any other: store it in the entity's components, `Add` to it, `Send` to it or put it in an event. Once the system is done, its new entities get their IDs, in the order one thread would have given them, and every handle it kept (in the components it changes, the changes it recorded and its events) becomes the real one, before any system that waits for it starts. Only while the system runs does the difference show: text shows a temporary handle as `Entity(new)`, and C can tell with `tide_entity_is_temporary` (see [Calling C](../c/functions.md)).

## Parallel loops

The third kind is inside a system: a `parallel` loop goes through a grid's cells, or blocks of them, all at once (see [Grids](../language/grids.md)). Its steps spread across the threads that are free, even while other systems run, and the system goes on once they're all done. Each step reads the grid as the loop found it and changes only its own cell or block, so the result is the same as on one thread. A foreach over a grid whose steps only touch their own cell runs the same way. The system itself waits for others as any system that writes the grid's component or singleton would, and the schedule shows its loops: `Fall  (once, 1 parallel loop)`.

## In CMake builds

In a CMake build of the engine's repo, `<game>_schedule` is a build target that prints it, and `tidec --schedule` prints it for any files.
