---
description: "Define Tide value structs, methods and functions, with named arguments, defaults, overloads, out parameters, actions and operators."
---

# Structs and functions

## Structs

A struct is a value type for fields and locals: plain data, copied when it's assigned, with no references.

```csharp
struct Range
{
    float lo;
    float hi = 1;
}

struct Stats
{
    float health = 100;
    Range damage;                   // lo = 0, hi = 1: Range's defaults
    Range armor = Range { hi = 5 };
}

component Unit
{
    Stats stats;
}
```

- A value is written like a component's: `Stats { health = 50 }`. Fields left out take their defaults.
- A field that may be missing is a `T?`, like `int? target;`. It starts with nothing (see [Errors](./errors.md#fields-that-may-be-missing)).
- Components, singletons, inputs and other structs can hold structs, and so can locals. System parameters stay components, singletons, the input and `Devices`.
- A struct can't contain itself, even through other structs.

A struct's fields change through whatever holds it, so the signature still says what a system writes: `unit.stats.health -= 5` needs `mut Unit unit`. A local copy changes if it's `mut var`.

## Methods

Structs, components and singletons can have methods. Their fields are in scope by name, and so are their other methods. A method only reads the fields, unless it's `mut`:

```csharp
struct Stats
{
    float health = 100;

    bool IsDead() { return health <= 0; }

    mut void Hurt(float amount)
    {
        health -= amount;
        if (IsDead()) health = 0;
    }
}

system Burn(mut Unit unit)
{
    unit.stats.Hurt(1);
}
```

Calling a `mut` method changes what it's called on, so it needs write access, like an assignment: `unit.stats.Hurt(1)` needs `mut Unit unit`. A read-only method can't call a `mut` one.

Singleton methods use their explicit receiver and have no entity `this`, implicit world access or local sidecar fields. Inputs have only `Sample`, `Sanitize` and `Predict`.

A method marked `virtual` can be replaced by another package, and so can a function: see [Virtual and override](overrides.md).

## Functions

A function is code that other code calls. It has no keyword, just a return type:

```csharp
float Heal(mut Stats stats, float amount)
{
    stats.health = Math.Min(stats.health + amount, 100);
    return stats.health;
}

system Regenerate(mut Unit unit)
{
    Heal(unit.stats, 0.5);
}
```

- A plain value parameter is a read-only copy. A `mut` parameter is the caller's variable itself, which the function changes. The caller must be able to write it, so `Heal(unit.stats, 0.5)` needs `mut Unit unit`.
- Parameters are built-in types, structs, components, singletons and event/command payloads, and an argument's type matches exactly. Return values are built-in types, structs, components and event/command payloads; singleton returns remain unsupported.
- Synchronous functions and methods can borrow the caller's [input](../game/input.md#passing-input-to-helpers) with an ordinary parameter, `bool WantsJump(Controls controls)`. Pass the input parameter directly. Its current and previous tick stay together, so `controls.jump.down` and `.up` retain their meaning through helper calls. The input is read-only, has no default and cannot be stored or returned; tasks take individual field values instead.
- Synchronous functions and methods can also borrow a read-only `query`, as described below.
- A function returns a value on every path, unless it returns `void`. The check follows conditions known while compiling, including named bool constants and comparisons of constant integers: `if (true) return 1;` covers its only path. A loop whose condition is always true and has no reachable `break` can't end without returning.
- A function or method can fail, with `fails` after its parameters, and its callers handle the error where they call (see [Errors](./errors.md)).
- A parameter can have a default value, a call can pass arguments by name, and functions can share a name: see [Default values, names and overloads](#default-values-names-and-overloads).
- A parameter can be `out`, which gives the caller's variable its value: see [Out parameters](#out-parameters).
- A function shares its name with nothing else in its namespace but other functions, and a method never with a field.

Functions work from their parameters, constants and the functions they call; methods also see their own fields. To read or update another entity's component, take the caller's [`World`](../game/systems.md#other-entities-the-world) explicitly: reads use `World`, and `Set` needs `mut World`. What the helper reaches counts toward the caller's access, and a view can't call a helper that writes the match. A function can draw and use the GUI, and then only views, and other functions like it, can call it. A function can also send events, spawn and change entities, and start tasks, as the code that calls it could: see [Sending and spawning](#sending-and-spawning). [Async functions](../game/tasks.md#async-functions) run as tasks and have their world's entity operations.

A helper can `Set<T>` beside either a read-only or mutable component parameter. A read-only parameter keeps its captured value; setting the current entity replaces its active `mut T` parameter immediately. This follows through synchronous helper chains, including when the mutable parameter is passed by reference to another helper. Ordinary value parameters and explicit copies keep their own values. Later world reads see the Set immediately, while subsequent edits to the mutable parameter retain normal write-back timing.

Resource asset collections can be borrowed by synchronous CPU functions and methods with ordinary read-only parameters: `List<Clip>`, `List<Texture>`, `List<Mesh<V>>`, and their `Map<string, T>` equivalents.

```csharp
asset List<Clip> hits = "sfx/hit_*.wav";

Clip Pick(List<Clip> clips, int index)
{
    return clips[index];
}

view HitSound() { Audio.Play(Pick(hits, 0)); }
```

Pass the asset directly, or forward a borrowed parameter through further helpers. Count, indexing, `foreach` and read-only collection methods work as usual; passing copies no collection. Elements keep their ordinary value rules, so a helper can return a selected clip, texture or mesh. The collection borrow itself cannot be stored, returned, optional, mutated or retained by an async declaration, and has no default. Externs, operators, audio declarations and GPU code take no such borrow. Texture and clip collections remain local code's; mesh geometry retains its existing match-read rules.

Large read-only values can use borrowed storage in generated CPU helpers when the compiler proves it safe, avoiding repeated stack copies. They retain value semantics: overlapping mutable arguments, later argument effects and C calls still preserve the captured value. This needs no annotation or reference syntax.

Singleton arguments are explicit values, like component arguments. The function gets no singleton lookup or access to a world. A match system or handler can pass state it can write to a `mut` parameter; a view can pass local state it can write, while its match state stays read-only. The input's `Sample` can only read local state, including through helpers. These restrictions follow calls through other helpers, and their reads and writes count toward the caller's access and scheduling.

```csharp
singleton Round
{
    int number;
}

void NextRound(mut Round round)
{
    round.number += 1;
}

system AdvanceRound(mut Round round)
{
    NextRound(round);
}
```

Methods take explicit singleton parameters and receivers by the same value and `mut` rules.

A match component or singleton can also have [local fields](../game/local-state.md). An ordinary function can read or write those when the caller passes its actual bound parameter or a permitted query binding, and can pass that binding through another helper. A plain copy contains only match fields and has no local fields to pass on. Match code can't call a helper that uses local fields; local code can't call one that writes match fields. A helper can't do both through the same component parameter. Method receivers don't receive local fields; a method borrowing a query can reach that query's permitted local fields. The existing restrictions on copying or passing grids and rendering resources still apply.

A drawing function can also [name a frag or vert](../game/shaders.md#drawing-from-a-helper). Each component or singleton the shader takes must match exactly one explicit input parameter of the function by type. The shader reads that passed value and, where needed, its forwarded local fields. The helper uses its caller's frame and follows ordinary drawing capabilities.

Engine-owned singleton instances, such as `Time` and `Session`, stay read-only when passed to helpers. Passing `Session` grants no session operations: a function makes [session calls](../multiplayer/matches.md) as the code that calls it could, with a `Session` or without. Ordinary helpers also can't read `session.room` or `session.invite`, or format a whole `Session` as text: pass the room code or the invite as a separate `string`.

A function can also be written in C, declared with `extern` and no body: see [Calling C](../c/functions.md).

### A value nobody takes

A call written as a statement, whose function gives a value and does nothing else, is a warning: the value is lost, and the call with it. It's usually a method that gives a changed copy, taken for one that changes its value:

```csharp
request.SetHeader("Accept", "text/plain");            // Warning: 'SetHeader' gives a value that nothing takes
request = request.SetHeader("Accept", "text/plain");  // Keeps it
_ = request.SetHeader("Accept", "text/plain");        // Drops it, on purpose
```

`_ = value;` works anything out and drops it. A call that does something besides giving its value (it sends, spawns, draws, calls C, or changes a `mut` argument or its own value) is a statement as it always was, and so is one that can fail, which has a warning of its own.

## Borrowing a query

A helper uses the same `query (...)` parameter syntax as a system or handler. The caller explicitly passes the selection to inspect:

```csharp
float TotalMass(query (Body body) bodies)
{
    mut var total = 0.0;
    foreach (var row in bodies) total += row.body.mass;
    return total;
}

system Weigh(mut Stats stats, query (Body candidate, with Enemy) enemies)
{
    stats.enemyMass = TotalMass(enemies);
}
```

`TotalMass` visits the enemies the caller selected. The helper declares the component data each row must provide; names can differ, and the caller can supply extra bindings and filters. Every requested component type needs one unambiguous data binding in the source query. A `with Body` filter alone does not provide Body data. Filters written in a helper's signature are requirements the caller's query must already guarantee, and do not select additional entities.

The source keeps its order and timing. Match queries read the current match, a view passes its blended or verified rows, and local handlers pass their recorded rows. A helper can forward the query to another helper. Passing it allocates no collection and makes no snapshot. Container fields read through a borrowed recorded query are included in its journal capture; unrelated unread containers are omitted. To borrow local entities, use local bindings such as `query (local Spark spark) sparks`.

The query is valid for the call. The query parameter itself has no `mut`, `out` or default. In local code, a nested binding such as `query (mut Fighter fighter) fighters` can write attached local fields while its match data stays read-only. A mutable helper binding requires a mutable source binding; a read-only helper can borrow either. Local fields always use current local state, including when the source has verified or recorded match values. Match code cannot call a helper that reaches local fields.

Queries and whole rows cannot be stored or returned, and whole rows cannot be passed as values. A component binding passed directly to an ordinary helper retains its local-field access; an explicit component copy has match data only. Keep query iteration and forwarding outside callback blocks and parallel steps. Async functions, externs, operators, audio declarations and shaders take no borrowed queries.

The helper gains the row access its parameter declares. Its other capabilities still come from its caller; explicit `World` access remains separate. Reads and writes preserve the existing scheduling and evaluation-order rules.

## Sending and spawning

A function or a method can `Send`, `Spawn`, `Add`, `Remove` and `Destroy`, written as a system writes them. It does it as the code that calls it: what a system could do itself, a function it calls can do for it.

```csharp
event Sank { int2 at; }
event Hit { int damage; }

component Crate { int from; }

// Says what happened as it happens
bool Step(mut Board board)
{
    board.at += int2(1, 0);
    if (board.at.x < board.width) return true;
    Send(Sank { at = board.at });
    return false;
}

Entity Drop(int from)
{
    return Spawn(Crate { from = from });
}

void Hurt(Entity target, int damage)
{
    target.Send(Hit { damage = damage });
}

system Play(mut Kitchen kitchen)
{
    if (!Step(kitchen.board)) kitchen.last = Drop(kitchen.round);
}
```

Nothing is wrong with such a function by itself. A call is an error only from a place that couldn't do the same thing itself, and the error names the line in the function that does it:

| The function | Who can call it |
| --- | --- |
| Sends a match event, or spawns or changes the match's entities | Match systems, event handlers, command handlers and the match's tasks |
| Sends a local event or a [command](../game/commands.md), or spawns or changes local entities | Views, local handlers and this machine's tasks |
| Does neither | Anything, as before |

```csharp
view Hud(Kitchen kitchen)
{
    Drop(1); // Error: a view can't change the match, and 'Drop' spawns entities
}
```

- **It follows calls.** A function that calls one that sends counts as sending too, through however many.
- **Which world** is told by what's written: a match event or a local one, an `Entity` or a `LocalEntity`, the match's components or local ones. A `Spawn` with no components is the match's.
- **A function has no `this`.** It takes the entity as a parameter, `Entity target`, and writes `target.Send(...)`. A component's method has `this`, the entity it's called for.
- **What it spawns joins the scene of the entity its caller runs for**, as the caller's own spawns do.
- **Its calls keep their order.** What a function sends is recorded where it's called, among what the caller sends itself, and `Total(First(), Second())` runs `First` first.
- **What was sent stays sent.** A function that tries a move and takes it back should send once the move is certain.
- **Threads change nothing.** A system that splits its entities across threads calls such functions as it sends and spawns itself (see [The schedule](../game/schedule.md#splitting-across-threads)).

Some code never sends or spawns, whoever calls it: a `Sort`'s comparison and a `parallel` loop's step, the input's `Sample`, `Sanitize` and `Predict`, frags and verts, a struct's operators and a component's `Interpolate`. A call there of a function that does is the error.

### Starting a task

A function or a method starts a [task](../game/tasks.md) the same way, by calling an async function as a statement, and the same rule holds: it starts it as the code that calls it could.

```csharp
singleton Round
{
    int left;
}

async void Countdown(mut Round round, int from)
{
    for (var i = from; i > 0; i--)
    {
        round.left = i;
        await Wait.Seconds(1);
    }
    round.left = 0;
}

// Any system or handler of the match can call this
void Restart(mut Round round)
{
    Countdown(round, 3); // Starts a task; Restart doesn't wait for it
}

system Rounds(mut Round round, Controls controls)
{
    if (controls.restart && round.left == 0) Restart(round);
}
```

- **Which world** is the task's own: a function that starts a task that changes the match is the match's to call, and one whose task uses local state is a view's, a local handler's or a local task's. A call from the other side is the error, and it names the task and the line that starts it. A task that does neither starts in the world of whatever calls the function, so one function serves both.
- **Whom the task belongs to** is the entity the calling code runs for, through however many functions: it ends when that entity is destroyed, and what it spawns joins that entity's scene.
- **A task's singleton is the caller's own.** A task gets its singletons again each time it goes on, so `Restart` hands on the `Round` it was handed, and every call of `Restart` passes a parameter of its own, never a copy. A component isn't handed on this way: start a task that takes one from the system, view or handler that has it.
- **Its calls keep their order,** and a system that calls such a function starts its tasks in order, on one thread, as one that starts them itself.

The code that never sends never starts a task either.

## Default values, names and overloads

A parameter can have a default value, a call can pass an argument by its parameter's name, and functions can share a name.

```csharp
float Damage(float base, float multiplier = 1, bool crit = false)
{
    return crit ? base * multiplier * 2 : base * multiplier;
}

float Damage(float base, Weapon weapon)
{
    return base + weapon.power;
}

system Strike(mut Unit unit, Weapon weapon)
{
    unit.health -= Damage(10);              // multiplier 1, no crit
    unit.health -= Damage(10, 2);
    unit.health -= Damage(10, crit: true);  // By name, past multiplier
    unit.health -= Damage(10, weapon);      // The other Damage
}
```

**Default values.** A parameter with `= value` can be left out of a call, which then passes the value.

- The parameters a call can leave out are the last ones: after one with a default, each has one.
- A default is a constant, like a [field's default](../game/entities.md): a literal, a constant, an enum's member, a constructor like `float2(0, 1)`, a struct's value, `Math` functions and operators on them, or `null` for a `T?`.
- Only plain values take one. A `mut` parameter is the caller's variable, and a component or singleton is the caller's own, so a call always passes those.

**Named arguments.** A call can pass an argument by its parameter's name, `crit: true`.

- Arguments without a name come first, in the parameters' order. After a named one, each is named, in any order: `Damage(crit: true, base: 5)`.
- They're worked out in the order the call writes them (see [Order](#order)).
- The engine's functions take names where their parameters have them: `Draw.Circle(center: p, radius: 2, color: Color.red)`, `Session.Connect(address: host, port: 7001)`. Constructors, `Spawn`, `Send` and the like take their arguments in order.

**Overloads.** Functions can share a name, and so can a type's methods, when their parameters' types differ. Names, `mut` and defaults don't tell two apart.

- A call picks by its arguments. An argument's own type wins over one it converts to: `Damage(10)` is `Damage(int)` if there is one, and `Damage(float)` otherwise.
- When two fit equally, the one the call wrote every parameter of wins over one that fills defaults in.
- A call that fits none, or two just as well, is an error that lists them. Write the argument as the type meant, like `float(x)`, or name a parameter only one has.

Systems, views and handlers can share a name too. Nothing calls them, so where one is named, it's written with its parameters' types: see [Order of systems](../game/systems.md#order).

## Out parameters

A function returns one value. An `out` parameter gives another, through a variable of the caller's. It's for one pattern above all: return whether there's something, and give it, or the default.

```csharp
bool TryHalf(int value, out int half)
{
    if (value % 2 != 0)
    {
        half = default;
        return false;
    }
    half = value / 2;
    return true;
}

system Halve(mut Pile pile, mut Tally tally)
{
    if (TryHalf(pile.size, out var half))
    {
        pile.size = half;
    }
    tally.last = half;                  // Still there after the if

    TryHalf(pile.size, out pile.next);  // Into a field
    TryHalf(pile.size, out _);          // Not wanted
}
```

**In the function**, an `out` parameter starts with no value: what the caller's variable held isn't passed in.

- The code assigns it before it reads it, and on every path that returns. A path that has nothing to give assigns `default`.
- An `if` assigns it when both sides do, or when the only side a constant condition can reach does. The right of `&&` and `||` only runs sometimes, and a loop may run no rounds, so what only they assign doesn't count after them unless its constant condition guarantees that path runs.
- Passing it on as another call's `out` argument assigns it: `return scores.TryGet(key, out score);`.
- A struct can be assigned a field at a time: it has a value once each of its fields does, and a field is read once it's assigned. `pair.a = 1; pair.b = 2;` fills a `Pair`, and reading `pair.b` between the two is an error. A vector or a color is assigned whole: `v.x = 1` before `v = ...` reads a `v` that has no value yet.
- In a function that [fails](./errors.md), `fail` doesn't have to assign it. After a call that failed, the caller's variable is what the function assigned before it failed, or its type's default.

**At the call**, the argument says `out`, in one of four ways:

| Argument | What it does |
| --- | --- |
| `out var half` | Declares a local of the parameter's type, right there |
| `out int half` | The same, with its type written |
| `out pile.next` | Assigns a variable the code has: a `mut` local, or a field of something it takes as `mut` |
| `out _` | Leaves it out |

- A local declared this way is read-only, like a `var`. `out mut var half` declares one the code changes later.
- It belongs to the block the call's statement is in. Declared in an `if`'s condition, it's still there in the `else` and after the `if`. Declared in a `while`'s or a `for`'s condition, it's the loop's own.
- Later elements and arguments can read what an earlier nested call made, including a call inside a list argument. Their order is the order written, even when arguments are named or the outer function has overloads.
- It's only read where its call has run. In `if (ready && TryHalf(8, out var half))`, `half` has a value inside the `if`, and reading it after the `if` is an error, since `ready` may have been false.
- The type matches exactly, as for `mut`, and by name the name comes first: `TryHalf(value: 8, half: out var h)`.
- A list's element and a swizzle of several components (`v.xy`) are copies, so neither can be an `out` argument: declare a variable, and put it in.
- An `out` argument is a write. A system needs `mut` on the component or singleton the variable is in, and a view can't make the match's.

Functions, methods and [C functions](../c/functions.md#values-across) have `out` parameters. Systems, views and handlers don't, as the engine calls them and nothing would take what they make. [Frags and verts](../game/shaders.md) have them for what the GPU takes: a frag's color, `out Color color`, and a struct a vert [writes for its frag](../game/shaders.md#from-a-vert-to-its-frag). The rules for the code are the same there: written before it's read, and on every path that ends. Async functions don't either: their call returns before the task has made anything, so they return a struct instead. An `out` parameter has no default, and `out` doesn't tell functions that share a name apart.

Maps have one built in, [`TryGet(key, out value)`](./text-and-lists.md#members-2), and so does the [World](../game/systems.md#other-entities-the-world): `world.TryGet<Health>(target, out var health)`. The GUI's fields that are typed into have one too, for [whether the player pressed Enter in them](../game/gui.md#when-a-field-is-entered): `GUILayout.TextField("Room", lobby.code, out var entered)`.

## Order

A `mut` parameter, an `out` parameter and a `mut` method change something of the caller's, so it matters when they run. Tide runs an expression left to right, on every platform:

```csharp
int Next(mut Counter counter)
{
    counter.n += 1;
    return counter.n;
}

system Count(mut Counter counter, mut Log log)
{
    log.both = Pack(Next(counter), Next(counter));  // The first Next runs first
    log.before = Pack(counter.n, Next(counter));    // counter.n as it was before Next
    log.after = Pack(Next(counter), counter.n);     // ...and as Next left it
    counter.n += Next(counter);                     // Adds to what counter.n was
}
```

- Arguments, operands, a vector's parts and a value's fields (`Pair { a = Next(counter), b = Next(counter) }`) all go in the order they're written, through any functions in between.
- Arguments passed by name go in the order written too, whatever order the parameters are in: in `Pack(ones: Next(counter), tens: Next(counter))`, `ones` gets the first `Next`.
- A value is read where it's written. A read-only argument is a copy made then, a list too.
- A method reads the value it's called on as it runs, once its arguments are worked out: `stats.Plus(stats.Bump())` sees what `Bump` left. An element is read once its index is worked out.
- A read-only method's receiver follows the same copy rules as a read-only argument: if a `mut` or `out` argument changes its lists or maps during the call, the method still reads its original copy.
- The right of `&&` and `||` and the sides of `?:` still only run when they would.
- A list's, map's or set's change that gives a value (`items.Remove(x)`, `seen.Add(key)`) counts as a call that changes it.

## Actions

A function's last parameter can be an `Action`: code the caller writes in braces after the call. The function runs it by calling it, as many times as it likes, including none.

```csharp
// A container of your own: shows its content only while open.
void Foldout(string title, mut bool open, Action content)
{
    GUILayout.Toggle(title, open);
    if (open) content();
}

view Options(mut Menu menu, mut Settings settings)
{
    Foldout("Audio", menu.audioOpen)
    {
        GUILayout.Slider("Volume", settings.volume, 0, 1);
    }
}
```

The block runs as if it were written at the call. It sees the caller's locals and parameters, and what it reads and writes counts toward the caller's signature. `return` in it ends the caller, and `break` and `continue` act on the caller's loop.

A function that takes a block is inlined where it's called, so blocks cost nothing and need no closures. That also means it can't call itself, and a block can only be run, never stored.

This is how the GUI's containers work: `GUILayout.Horizontal()`, `GUILayout.Area()` and the others are functions with a block, like `Foldout`.

### Several calls, one block

Calls that take a block can be listed with commas before one block, which they share. It means the same as each inside the one before, without the nesting:

```csharp
view Options(mut Menu menu, mut Settings settings, Session session)
{
    GUILayout.Area(Anchor.MiddleCenter),
    GUI.Disabled(session.state == SessionState.Connecting),
    Foldout("Audio", menu.audioOpen)
    {
        GUILayout.Slider("Volume", settings.volume, 0, 1);
    }
}
```

Anything that takes a block can be in the list: the GUI's containers, [draw settings](../game/draw-settings.md) like `Draw.Blend(Blend.Add)`, and functions of your own. The first call is set up first and finished last. A call that takes no block doesn't belong in one, and the compiler says so; and since it's the comma that makes a list, a call that's only missing its `;` is still reported as that.

## Operators

A struct can declare operators, each a function named `operator` and its symbol. They compile to plain function calls.

```csharp
struct Money
{
    int cents;

    Money operator +(Money a, Money b) { return Money { cents = a.cents + b.cents }; }
    Money operator *(Money a, int times) { return Money { cents = a.cents * times }; }
    bool operator ==(Money a, Money b) { return a.cents == b.cents; }
    bool operator !=(Money a, Money b) { return !(a == b); }
}
```

- The operators are `+ - * / % & | ^ << >>`, the comparisons `== != < <= > >=`, and `-`, `!` and `~` with one parameter.
- At least one parameter is the struct itself. An operator sees only its parameters, not fields.
- `==` and `!=` come in pairs, and so do `<` and `>`, and `<=` and `>=`: a struct that declares one declares the other.
- A compound assignment uses its operator: `total += price` uses `+`.
- Several operators can share a symbol with different types, like `Money * int` and `int * Money`.

Structs are only compared with `==` if they declare it.
