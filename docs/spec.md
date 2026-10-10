---
description: "The Tide language reference: syntax, types, ECS components and systems, deterministic simulation, multiplayer, drawing and runtime APIs."
---

# The Tide language

The language Tide games and the engine's built-in systems are written in. It transpiles to C (see AGENTS.md). This file is the source of truth for the language: only decisions the owner has approved go in **Decided**. Proposals and open questions go in **Open**.

## Decided

### Files and tools

- Source files use the `.tide` extension.
- The compiler (transpiler) is `tidec`.
- A game is one or more `.tide` files: by default, every `.tide` file in the game's folder and its subfolders, and those of the packages it lists (see Packages). Every declaration is visible from every file of the game; there are no imports between files.
- A game needs no C: the engine runs it. A custom C host is optional, for tests or special hosts. A game can call C of its own, from `.c` files, `.cpp` files and libraries in its folder (see C functions).

### Namespaces

- A file can put its declarations in a namespace, and namespaces are how large games keep names apart.
- Code outside the namespace names its declarations with it (`Combat.Health`), or imports it with `using Combat;`.

### Order of systems

- Systems run in one deterministic order, the same on every platform. By default it follows the files, sorted by path, then the order of declarations in each file.
- Attributes change the order of a system relative to others.

### Style

- Tide is a language of its own, and isn't described as C#-like. C# inspired its syntax, and a rule below that follows C# says so.
- Types, systems and methods use PascalCase: `Transform`, `MovePlayer`, `Spawn(...)`.
- Fields, parameters and locals use camelCase: `trs.position`, not `trs.Position`.
- Public properties use camelCase too, even static ones: `Color.red`, `quaternion.identity`. True constants use FULL_CASE: `Math.PI`, `Math.TAU`.
- Attributes (`[...]`) are only for metadata, such as when a system runs or the bounds of an input field, not for what code does. The engine may enforce what an attribute declares, as it clamps an input field to its `[Clamp]`.
- Braces go on lines of their own, as in C#, and so does an `else` after a block. A block that fits on one line can stay there (`scene Main { }`, `if (dead) { return; }`), and so do literals (`Body { position = p }`). The language server's formatter lays code out this way, indenting as the editor is set to (four spaces by default).

### Declarations

- `component` declares a component.
- `singleton` declares world-wide state (what other ECSs call a resource). There is exactly one instance **per world**, not per process.
- `struct` declares a value type for fields and locals (see Structs).
- `system` declares a system.
- `event` declares an event, and `event(...)` code that runs when one is sent (see Events).
- `enum` declares an enum (see Enums and switch).
- `scene` declares a scene (see Scenes).
- `local` in front of a declaration makes it belong to the machine instead of the match, and in front of a field of a match component or singleton, that field alone (see Local state).
- A function, `ReturnType Name(parameters) { ... }` with no keyword, declares code that other code calls (see Functions).
- `extern` declares a function written in C (see C functions).
- `virtual` in front of a declaration with code says another package can replace it, and `override` in front of one replaces it (see Mods).
- `const` declares a constant (see Constants).
- `settings` sets the engine's settings for the game (see Settings).

### Field defaults

- Fields of components, singletons, inputs and structs can declare a default value: `int value = 100;`.
- A default must be a constant expression: literals, constants, constructors of built-in types, struct values of constants (`Range { hi = 5 }`), `Math` functions, built-in constants like `quaternion.identity`, and operators, as in `float angle = Math.Radians(45);`. It can't read fields, singletons or `Time`.
  - Provisional: it can name an asset too, as it names a constant, `local Texture face = logo;` (see Assets, A field's default).
- Singletons start with their defaults when the world is created, before `Main` runs.
- Components get their defaults whenever a value is created without setting that field: `Spawn(Health)`, `e.Add(Health)`, and fields left out of `Health { max = 200 }`.
- Fields without a default start at zero. `Entity` fields always start as the null entity and can't have another default.

```csharp
singleton Physics
{
    int collisionCount = 69;
    float3 gravity = float3(0, -9.81, 0);
}

component Health
{
    int value = 100;
    int max = 2 * 2 * 25;
}
```

### Default values

- `default` is the default value of the type where it goes, as in C#: `Draw.Circle(default, 5, Color.yellow)` draws at `float2(0, 0)`.
- It's the value a field of that type starts at when it declares no default: zero for numbers, vectors, matrices, quaternions, colors and rects, `false`, empty text, an empty list, the null entity, no player, an enum's 0, and for a struct or component, its fields' defaults, as `Stats { }` makes them.
- Its type comes from where it goes: a typed local (`float2 center = default;`), an assignment, an argument, a `return`, a field in a value (`Range { lo = default }`), a field's default, a list's element, and the other side of `==`, `!=` or `?:` (`target == default`).

### Structs

- A struct is plain data, copied when it's assigned, with no references. The world stays plain data, so copying it is still a snapshot.
- Structs are the types of fields (of components, singletons, inputs and other structs) and of locals. System parameters stay components, singletons, the input and `Devices`.
- A value is written like a component's: `Stats { armor = 2 }`. Fields left out take their default, and a struct field without one takes its struct's defaults.
- A struct's fields change through whatever holds it: `unit.stats.health -= 5` needs `mut Unit unit`, so a system's signature still says what it writes. A local copy changes with `mut var`.
- A struct can't contain itself, even through other structs: it would be infinitely big.
- Namespaces apply as to every declaration: another namespace names it `Combat.Stats`.
- `[Clamp]`, `[Min]` and `[Max]` on a struct's field are enforced where untrusted data enters the simulation: in an input that holds the struct, before `Sanitize`, as on the input's own fields. Elsewhere they only describe the field.
- `==` compares structs only if the struct declares it (see Operators).
- Structs have methods and operators (see Methods and Operators).

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

system Hurt(mut Unit unit)
{
    unit.stats.health -= unit.stats.damage.hi;
}
```

### Methods

- Structs and components can have methods. Their fields are in scope by name, and so are their other methods.
- A method only reads the fields, unless it's `mut`. Calling a `mut` method changes what it's called on, so it needs write access, like an assignment: `unit.stats.Hurt(5)` needs `mut Unit unit`, and the system's signature still says what it writes. A read-only method can't call a `mut` one.
- A method returns a value with `return value;`, on every path, unless it returns `void`.
- A method sees its fields, its parameters and `Math`. It can't draw: views do. It sends events, spawns and changes entities as a function does, for the code that calls it (see Functions).
- A component's method has `this`: the entity whose component it's called on. A method that uses it, itself or through another of its methods, can only be called on a component the code runs for, a parameter of a system, view or handler: a copy belongs to no entity. A struct's methods and `Interpolate` have no `this`.
- Methods can share a name when their parameters differ, as functions can (see Default values, named arguments and overloads). A method can't share one with a field.
- Singletons and inputs have no methods (an input has `Sample` and `Sanitize`): keep the data and its methods in a struct, and the struct in them.
- Parameters work as in functions: they can have default values, and calls can name them.

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

### Functions

- A function is code that other code calls: systems, views, the input's `Sample` and `Sanitize`, methods and other functions. The engine never runs one by itself.
- A parameter is a copy, read-only. A `mut` parameter is the caller's variable itself, which the function changes: the caller passes something it can write, and its signature still shows the write, so `Heal(unit.stats, 5)` needs `mut Unit unit`. The argument's type matches exactly.
- Parameters are built-in types, structs, components and singletons. Return values are built-in types, structs and components; singleton return values aren't supported.
- Singleton parameters are explicit values, with the same read-only copy and `mut` caller-variable rules as components, in functions and methods. They bind no singleton from the world and grant no world access: a function reaches other entities through a `World` parameter, which its caller passes its own (see The World). Existing restrictions on copying or passing grids and rendering resources still apply; singletons still have no methods of their own.
- Caller access remains decisive, through helper chains: match systems and handlers can pass match state they can write; views can read match state and change local state; the input's `Sample` only reads local state. Helper reads and writes count toward the caller's access and scheduling. For a match component or singleton with local fields, only an actual bound parameter supplies them to an ordinary function, not a plain copy (see Local state). Match code can't call a helper that uses local fields, and local code can't call one that writes match fields. Methods don't receive local fields.
- Engine-owned singleton instances stay read-only through calls. A helper can read stored fields of `Time` or `Session`, but passing `Session` grants no session operations. Ordinary helpers can't read `session.room` or format a whole `Session` as text; pass the room code as a separate `string`.
- Functions follow the rules of methods: they see their parameters and `Math`, and return a value on every path unless they return `void`.
- **A function sends events, spawns and changes entities as the code that calls it could** (the owner, 2026-10-08, of `Send`, `Spawn` and `Destroy` in a function: "it should inherit their ability; if you say `Send` it should mean that what calls us can `Send` otherwise is a compiler error ONLY when its called from a place it can't"). `Send`, `entity.Send`, `Spawn`, `Add`, `Remove` and `Destroy` are written in a function or a method as in a system, with no parameter for it. The function is never wrong by itself: a call of it is an error where the calling code couldn't do the same thing itself, and that follows calls through other functions, as their reads and writes do. What was picked to build it is under Provisional, Functions and blocks.
- A function can draw and use the GUI. Then only views, and other functions like it, can call it, as with `Draw`.
- Namespaces apply: another namespace calls it `Combat.Heal(...)`. A function shares its name with nothing else in its namespace but other functions, whose parameters tell them apart (see Default values, named arguments and overloads).
- A parameter can have a default value, which a call can leave out, and a call can pass arguments by name: see the same section.
- A parameter can be `out`: the call gives the caller's variable its value, as in C# (see Out parameters).
- A function's last parameter can be an `Action`: code the caller writes in braces after the call, `Section("Audio") { ... }`. A function takes at most one, always last, and it's always written after the call, never inside the parentheses.
- The function runs the block by calling it, `content();`, as many times as it chooses, including none. The block runs as if it were written at the call: it sees the caller's locals and parameters, and what it reads and writes counts toward the caller's signature.
- A function that takes an `Action` is inlined where it's called, so blocks cost nothing and need no closures. It can't call itself, directly or through other functions, and a block can only be run, not stored.

```csharp
// A container of your own: runs its content only while open.
void Foldout(string title, mut bool open, Action content)
{
    GUILayout.Toggle(title, open);
    if (open) content();
}

view Options(mut Main menu, mut Settings settings)
{
    Foldout("Audio", menu.audioOpen)
    {
        GUILayout.Slider("Volume", settings.volume, 0, 1);
    }
}
```

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

### Calls that take a block

- Some calls take a block, written in braces after them: the GUI's containers (`GUILayout.Horizontal() { ... }`), draw settings (`Draw.Blend(Blend.Add) { ... }`, see Views and drawing) and a game's functions that take an `Action` (above).
- Calls that take a block can be listed with commas before one block, which they share, so they don't nest three deep:

```csharp
Draw.Blend(Blend.Add),
Draw.Tint(Fade(Color.white, shown)),
Draw.Clip(panel)
{
    ...
}

GUILayout.Area(Anchor.MiddleCenter),
GUI.Disabled(busy)
{
    ...
}
```

- A list means what nesting its calls in that order does: each call's block is the call after it, and the last one's is the block. The first is set up first and put back last.
- It's a rule of the language, not of `Draw`: the GUI's containers, the draw settings and a game's own functions with an `Action` go in one list, each beside the others.
- A call in the list that takes no block is an error at that call, which says so.
- The comma is what makes a list. With none, a call followed by another is still a missing `;`.
- The formatter puts a call a line, each starting where the first does, and the block's braces on lines of their own.

### Default values, named arguments and overloads

**Following C# here is agreed.** So is how a shared name is written where nothing calls it: with its parameters' types, and it has to be one declaration's. The points marked Provisional are picks the owner hasn't confirmed.

- **Default values.** A function's or method's parameter can have one: `float Damage(float base, float multiplier = 1, bool crit = false)`. A call can leave such a parameter out, and it gets the default.
  - They're the last parameters, as in C#: after one with a default, each has one. An `Action` still comes last: it isn't an argument.
  - Provisional: a default is a constant expression, as a field's is (see Field defaults), and `null` for a `T?`. It can't read another parameter.
  - Provisional: only plain values take one. A `mut` parameter is the caller's variable, a component or a singleton is the caller's own, and the devices, a `DrawList` and rendering resources are passed: none of them can be left out. An `Entity`'s only default is `default`, the null entity. Operators and `Interpolate` take none.
  - Parameters of systems, views, handlers, frags and verts take none: the engine supplies them, and nothing calls them.
  - An `extern` function's parameters take them too: C is passed the default like any argument.
- **Named arguments.** A call can pass an argument by its parameter's name, `Damage(10, crit: true)`, which lets it leave out the ones before. Struct values keep `=` (`Body { position = p }`), as in C#.
  - Arguments without a name come first, in the parameters' order. After a named one, each is named, in any order: `Damage(crit: true, base: 5)`.
  - Each parameter is given once, and every parameter without a default is given.
  - Arguments are worked out in the order written, whatever order the parameters are in (see Evaluation order).
  - Provisional: `default` as an argument stays the type's default, as in C#, not the parameter's.
  - The engine's functions take names too, where their parameters have names: `Draw.Circle(center: p, radius: 2, color: Color.red)`, `Session.Connect(address: host, port: 7001)`, `Debug.Assert(ok, message: "lost")`. What a call leaves out of them is at the end. Constructors (`float3(...)`), `Spawn`, `Send`, `Add`, `Remove`, a list's and a map's methods, the `World`'s methods, the component-wise `Math` functions and `Draw.Mesh` and its kin take their arguments in order.
- **Overloads.** Functions can share a name, and so can a type's methods, when their parameters differ: `Damage(float base, float multiplier = 1)` and `Damage(float base, Weapon weapon)`.
  - What tells them apart is their parameters' types, in order. Names, `mut` and defaults don't: two declarations that differ only in those are an error.
  - A call picks by its arguments, as in C#. Each one that could take them is a candidate. One is better than another when no argument goes to it worse and at least one goes better: an argument's own type is better than one it converts to, and of two types it converts to, the one that converts to the other (an `int` over a `float`, for a `byte`). When that leaves two the same, the one the call wrote every parameter of is better than one that fills defaults in. A call that fits none, or fits two equally, is an error that lists them.
  - `default`, `null`, `[a, b]` and a grid's size take their type from the parameter, so they don't pick: the other arguments do.
  - Provisional: `extern` functions that share a name each say which C function they are, with `[NativeName]`, since C has one function of a name.
  - A function still shares its name with nothing else in its namespace (a type, a system, a constant), and a method with no field.
- **Systems, views, handlers, frags and verts** can share a name the same way. Nothing calls them, so the name is written with its parameters where it's used:
  - `[After(Move(Position, Velocity, with Player))]`: the parameters' types in order, with the filters (`with`, `without`, `with ownership`, and a query as `query(Body, with Fighter)`) and a view's `verified` (`Bar(verified Health)`), and neither names nor `mut`. A handler's trigger isn't among them: it's written before the name.
  - A mesh's vert and frag are named the same way: `Draw.Mesh(vertices, indices, Tint(Vertex, Color))`.
  - A name alone is fine while only one declaration has it, as it always was. When several share it, the name alone is an error that lists them as they're written, and so is a parameter list that isn't exactly one's. A name written with parameters is checked against them even when it's the only one.
  - Tools show a shared name with its parameters: `tidec --schedule`, the profiler's rows, the language server.
- Provisional: in generated C, a declaration that shares its name goes on with its parameters' types (`tide_function_Damage_float_Weapon`). One alone keeps the name it had, so code that shares no names generates the C it always did.

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
    unit.health -= Damage(10, crit: true);  // By name, past multiplier
    unit.health -= Damage(10, weapon);      // The other one
}
```

```csharp
system Move(mut Position position, Velocity velocity, with Player) { ... }
system Move(mut Position position, Velocity velocity, with Enemy) { ... }

[After(Move(Position, Velocity, with Player)), Before(Move(Position, Velocity, with Enemy))]
system Aim(mut Turret turret, Position position) { ... }
```

### Out parameters

**Following C# here is agreed**, and so is the pattern it's for: a function that returns whether it has something, and gives it through `out`, or the default. The points marked Provisional are picks the owner hasn't confirmed.

- **Declaring.** `out` before a parameter's type makes the call give the caller's variable its value: `bool TryHalf(int value, out int half)`. Functions, methods and `extern` functions have them. The word is a name like any other everywhere else, as `in` is.
  - What the engine calls itself takes none, since nothing would take what's made: systems, views, and event and command handlers. Neither do operators, `Interpolate` or, as in C#, async functions, whose call returns before the task has made anything: they return a struct instead.
  - A frag and a vert have them for what the GPU takes: a frag's color, and the structs a vert writes for its frag (see Views and drawing). What's said below of the code in the function holds there, with the GPU in the caller's place.
  - Provisional: an `out` parameter is of any type a parameter can be, but the devices and an `Action`.
  - It has no default, and `mut out` is an error that says to drop `mut`. A parameter after one with a default needs a default too, so `out` parameters come before those.
- **In the function.** An `out` parameter starts unassigned: what the caller's variable held isn't passed in. The code assigns it before it reads it, and on every path that returns. This is C#'s definite assignment:
  - A path is what the code can run through: an `if` assigns it when both sides do, `&&`, `||`, `?:` and `??` only run their right sides sometimes, a `while (true)` only ends at its breaks, and where a `return` or a `break` leaves, what follows isn't reached. A condition that's `true` or `false` as written counts as that.
  - A loop may run no rounds, so what its body assigns isn't assigned after it. Provisional: the same goes for a block written after a call (see Calls that take a block), which its call runs when it does.
  - Passing it on as another call's `out` argument assigns it, once that call has run.
  - Provisional: a struct is assigned whole, or a field at a time, as in C#: it has a value once each of its fields does, on every path, and a field is read once it's assigned. `pair.a = 1; pair.b = 2;` fills a `Pair`, a field can be another call's `out` argument (`Half(seed, out pair.a)`), and reading `pair.b`, or all of `pair`, before `pair.b` is assigned is an error that names the field. That's a struct's own fields: a field of a struct in it (`pair.inner.x = 1`) is a part of `pair.inner`, which is assigned whole first, and so is a component of a vector or a color (`v.x = 1` before `v = ...` reads a `v` with no value). It was whole assignment only until a vert's struct for its frag was built, whose agreed example fills it a field at a time: one rule for both.
  - `default` is how a path with nothing to give fills it: `health = default;`.
  - **Provisional: `fail` owes it nothing**, and neither does a `try` that passes an error on. C# has nothing to follow here. After a call that failed, each of its `out` variables is what the function assigned before it failed, or its type's default, never what the variable held before: nothing the code wrote is undone, and nothing is left unset.
- **At the call.** The argument for an `out` parameter says `out`, and no other argument does:
  - `out health` assigns a variable the code has: a `mut` local, a field of a component or singleton it takes as `mut`, its own `mut` or `out` parameter. The rules are a `mut` argument's: its type is the parameter's exactly, and a copy can't be one, which a list's element is, and a swizzle of several components (`v.xy`). (A swizzle passed to a `mut` parameter used to compile, and change a copy: it's the same error now.)
  - `out var health` and `out Health health` declare a local there, of the parameter's type. Provisional: it's read-only, as a `var` is, and `out mut var health` declares one the code changes later.
  - `out _` leaves what the call makes out. Provisional: as in C#, `_` is that only while nothing in scope is named `_`; `out var _` and `out int _` are too.
  - By name, the name comes first: `TryHalf(value: 8, half: out var h)`.
  - **Provisional: the word is written at every call, for an `extern` function's `out List<T>` too**, which took none before `out` was every function's (see C functions). `mut` and `in` still take none: `out` does because the argument can declare.
- **Scope**, as in C#: a local an `out` argument declares belongs to the block its statement is in, from the call on. Declared in an `if`'s condition, it's in scope in both branches and after the `if`. Declared in a `while`'s or a `for`'s condition, it's the loop's own. A statement that's a branch or a loop's body with no braces is a block of its own.
  - It's only read where the call that makes it has run, by the same rules as above: in `if (ready && TryHalf(8, out var half))`, `half` has a value inside the `if` and not after it.
  - One that nothing reads is a warning, like any local (see Mutability), whose note says to write `out _`.
- **Everything else treats an `out` argument as a write**, as a `mut` one: a system needs `mut` on the component or singleton the variable is in, a view can't make the match's, a parallel loop's step and a sort's comparison only make their own variables, and the call is one that writes, which keeps its turn among what's read beside it (see Evaluation order).
- **Overloads.** `out` doesn't tell functions that share a name apart, as `mut` doesn't: their parameters' types do. A call picks among them as ever, where an `out` argument only goes to an `out` parameter, and `out var` takes its type from the one picked.
- **`extern` functions.** `out T` is a `T *` in C, as `mut T` is: the difference is Tide's, which passes nothing in and takes it that C writes. `out List<T>` stays the list C makes through its writer. C can't make text, so `out string` is an error there (see C functions).
- **Code the GPU runs.** A function a frag or vert calls can have `out` parameters: `inout` in GLSL and a pointer in WGSL, as `mut` ones are. Provisional: a local an `out` argument declares can't be declared on a counting loop's own line there.
- **The engine's own.** `Map<K, V>` has `TryGet(key, out value)`, C#'s `TryGetValue` (see Maps and sets), the `World` has `TryGet<T>(entity, out value)` (see The World), and the GUI's `TextField`, `IntField` and `FloatField` each have a version with `out bool entered` (see GUI).
- In generated C an `out` parameter is a pointer, as a `mut` one is, and code that has none generates the C it always did.

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
        pile.size = half;               // Declared in the condition: the call ran here
    }
    tally.last = half;                  // ...and it's the block's, so it's still there after the if

    TryHalf(pile.size, out pile.next);  // A field of a component taken as mut
    TryHalf(pile.size, out _);          // Not wanted

    if (tally.scores.TryGet(pile.owner, out var score)) tally.total += score;
}
```

A function that looks an entity's component up and gives it through `out` takes the `World` its caller passes it (see The World):

```csharp
bool TryFind(Entity target, World world, out Health health)
{
    if (!world.Has<Health>(target))
    {
        health = default;
        return false;
    }
    health = world.Get<Health>(target);
    return true;
}

system Heal(Healer healer, World world)
{
    if (TryFind(healer.patient, world, out var health))
    {
        // health is the patient's
    }
}
```

`world.TryGet<Health>(healer.patient, out var health)` is that function, built in.

### Operators

- A struct can declare operators, in C#'s form: `Money operator +(Money a, Money b) { ... }`. They compile to plain function calls.
- The operators are `+ - * / % & | ^ << >>`, the comparisons `== != < <= > >=`, which return `bool`, and `-` (negation), `!` and `~` with one parameter.
- At least one parameter is the struct itself. Parameters are copies, and an operator has no fields in scope: it only sees its parameters.
- As in C#, `==` and `!=` come in pairs, and so do `<` and `>`, and `<=` and `>=`.
- A compound assignment uses its operator: `total += price` uses `+`.
- Several operators with the same symbol can take different types, like `Money * int` and `int * Money`. The one whose parameters match the operands exactly wins over one that needs `int` to `float`; if two match as well, it's an error.

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

### Mutability

- Everything is read-only by default. Mutability is opt-in with `mut`.
- This includes local variables. `var` infers the type, like C++ `auto`, and is read-only.

```csharp
var speed = player.speed * 2;    // read-only, type inferred
mut var total = 0.0;             // mutable, type inferred
float limit = 10;                // read-only, explicit type
mut float scale = 1;             // mutable, explicit type
```

- A local that's declared and never read is a warning, `local 'fwd' is never used`: a value worked out and then not used is a mistake more often than not, as when code computes `fwd` and then moves by `move`. Assigned but never read counts as never read.
- The warning's note says what to do: remove the local or use it. When its value is a call that does something (a `Spawn`, a call to C, a call that sends or changes state), the note says to keep the call, as a statement of its own. For a local an `out` argument declares (see Out parameters), it says to write `out _`.
- Parameters are apart: systems, views and handlers warn about theirs that are never used (see System parameters), and a plain function's or a method's stay quiet.

### System parameters

A system's component parameters describe the entities it runs for. Each parameter's modifier says how the system relates to that component or singleton:

| Modifier  | Meaning                                                                 |
|-----------|-------------------------------------------------------------------------|
| none      | Read access                                                             |
| `mut`     | Write access                                                            |
| `with`    | Entity must have this component. No data access, so no data dependency. |
| `without` | Entity must not have this component.                                    |

```csharp
system ClampPlayer(mut Transform trs, with Player)
{
    if (trs.position.y < 0)
        trs.position.y = 0;
}
```

- A view has one filter more, by who owns the entity: `with ownership` and `without ownership` (see Views and drawing). In match code they're errors.
- In code that runs for an entity, `this` is that entity: `if (health.value <= 0) this.Destroy();`. It's an `Entity`, or a `LocalEntity` in code that runs for local entities. In a frag, `this` is its pixel, and in a vert its vertex as the engine would place it (see Views and drawing). `this` is a keyword everywhere, and it can't be assigned.
- A system, view or handler has `this` when it runs once per entity: it takes a component or filters by one (`with Player`). A component's method has its entity too (see Methods), a frag has its pixel, and a vert its vertex. Other code has none, including ordinary functions; the error says why.
- The entity isn't a parameter: an `Entity` or `LocalEntity` parameter is an error that points to `this`.

### Read-only entity queries

A system, a view or a local handler can take another set of entities to inspect, using the same component binding and filter syntax inside a `query` parameter:

```csharp
system BulletHits(Body shot, Bullet bullet, query (Body body, with Fighter) fighters)
{
    foreach (var fighter in fighters)
    {
        // fighter.body is read-only Body data; fighter.entity is its Entity.
    }
}
```

- The outer parameters still select the entities the code runs for. `fighters` selects a separate set: its `Body` and `Fighter` requirements do not filter the bullet entity. A system with only query parameters runs once per tick, and a view with only query parameters once per frame.
- The nested list accepts named read-only component bindings and `with`/`without` component filters. Bindings follow ordinary parameter naming rules, with `entity` reserved for the row handle. It accepts no `mut`, singleton, input, `World` or nested query. A system's is of the match's components; a view's and a local handler's are all the match's or all local (below).
- `foreach (var fighter in fighters)` yields a read-only row. Its `entity` member is the matching entity's handle; each named binding exposes that entity's component value. Code can copy a component value or pass it to an ordinary function. As with other component copies, a method that uses `this` cannot be called on it. Code cannot write through a row, store a query or whole row, or pass either to a function. Queries are parameters of systems, views and local handlers, iterated in that code's ordinary control flow, outside parallel loops and callback blocks. The match's event handlers, command handlers and tasks take none yet.
- Every foreach traverses matching entities in normal deterministic ECS order, checking sparse component presence and filters per row. An empty set performs no iterations. Queries promise neither Entity-ID order nor distance or hit sorting; game code calculates intersections and chooses the earliest hit itself.
- In a system, component bindings read the current world, with the same semantics as `World.Get`. Their reads participate in access analysis, system ordering, the schedule and editor hints as reads of any entity's component. Filters access no component data. If a system also writes a component it queries, it follows the existing `World` reads-own-writes rule: it runs on one thread and sees rows changed earlier in that run.
- `Send`, `Spawn`, `Add`, `Remove` and `Destroy` keep their deferred behavior. Recording one through a row's entity does not change query membership or component data during the code's run.
- **Views take `query` parameters as systems do**, read-only, with the same syntax and the same filters:

```csharp
// A fighter's shadow falls on the highest platform under it.
view DrawFighter(Body body, Fighter fighter, query (Platform box) platforms)
{
    foreach (var entry in platforms) { ... entry.box.center ... }
}
```

  - **Rows are what a view sees**: blended between ticks like the view's own parameters, over the entities of the world it draws. An entity that wasn't there last tick is as it is, as for a view's own entity.
  - **`verified` inside a query** follows the rule for views (see Views and drawing): a binding marked `verified` comes from the world the server has confirmed. `query (verified Health health) confirmed` loops over the confirmed world's entities; a query that mixes marked and unmarked bindings takes the entities that are in both, the same entity and not just the same handle. The filters `with verified X` and `without verified X` work in a query too.
  - **A query is all local or all the match's**, as a view is: a view may loop over local entities too, like its own sparks.
- **Local handlers take queries too** (`local event(Hit hit) Show(..., query (Body body, with Fighter) fighters)`): they're local code that reads the match as views do. Their rows are as the handler's own match parameters are: from the tick that sent the event.

### The World

- `World world` is a built-in parameter, like `Devices`: any entity's components, by its handle. It reads another entity's component in the same tick, where `Send` would wait for the tick's end. A signature has one at most, and it filters nothing.
- `world.Get<Health>(target)` is a copy of that entity's `Health` as it is, or its zero value when it has none. `world.Has<Health>(target)` tells whether it has one.
- Provisional: `world.TryGet<Health>(target, out var health)` is both in one, as C#'s `TryGetValue`: whether the entity has one, and through `out` (see Out parameters), a copy of it, or the component's `default` when it has none. A component with a grid in it can't be copied, so it has no `TryGet`.
- `world.Set<Health>(target, h)` puts one back, and needs `mut World`. Nothing happens when the entity has none. The component `Get` gives is a copy, as a list's element is: change it and put it back.

```csharp
system Attack(Attacker a, World world)
{
    var h = world.Get<Health>(a.target);
    if (world.Has<Health>(a.target)) { ... }
}

system Heal(Healer healer, mut World world)
{
    mut var h = world.Get<Health>(healer.target);
    h.value += 1;
    world.Set<Health>(healer.target, h);
}
```

- What a system reaches through its `World` is access to every entity's component, which the schedule takes from the calls in its body: `Get<T>` and `Has<T>` read every entity's `T`, and `Set<T>` writes every entity's `T` and keeps the system on one thread. A system that reads through its `World` a component it also changes through a parameter runs on one thread too, and reads the rows it changed earlier in the same run.
- `Set<T>` of a component the system also takes as a parameter is an error: this entity's goes through the parameter, which would put it back as it was.
  - The exception (agreed 2026-10-05): where the parameter is only read, `world.Set<T>(this, value)` sets this entity's own. Nothing puts a read parameter back, and the parameter stays as the code came to the entity: `world.Get<T>(this)` has what was set. It's for code that goes through many entities and writes a few of them, which reads them all for what reading costs; taken as `mut`, a component is copied and compared for every entity once a snapshot shares the world's pages. Another entity's `T` is still an error there: the entities the code hasn't come to yet would be read changed or not by whether a snapshot shares their pages.
- The signature doesn't list those components, so tooling does: `tidec --schedule` says what each system reads and writes through its `World`, and so do the language server's hover and the line above each system.
- A `World` parameter that's never used is a warning, and so is a `mut World` that never calls `Set`.
- Views take `World` to read the match, never to change it: `Set` of the match's component in a view is an error.
- **Functions and methods take the World too** (agreed 2026-10-05: it's how the work is carried out, not what the language means), as their caller's, passed on: `bool TryFind(Entity target, World world, out Health health)`, called as `TryFind(healer.patient, world, out var health)`. A function has no World of its own: the argument is the `World` parameter of the system, view or handler that calls it, or of the function in between.
  - What a function reaches through it is what each of its callers reaches, through however many functions it's passed. So the schedule counts it as the system's own, on the same lines (`reads any entity's Health, through World`), a system that sets through a function runs on one thread, and one that reads through a function a component it also changes reads the rows it changed earlier in the same run.
  - `Set` needs `mut World` there as anywhere, and a caller passes a `mut World` to it: a read-only one is an error that says to declare it `mut`.
  - The rules that depend on who calls are said at the call: the match can't reach a local component, a local handler's World is this machine's, a view changes nothing of the match, a parallel loop's step and a sort's comparison set nothing, and a system that takes a component as a parameter can't call a function that sets that component through the World.
  - Provisional: a function takes one World at most, with no default, and it's neither `out` nor a value to keep or return. Operators and `extern` functions take none, and neither do async functions yet: an async handler takes it itself.
  - Provisional: `world.Set<T>(this, value)` beside a parameter that only reads `T` stays the system's own to write: a function has no `this`, so it can't say the entity is the one its caller runs for.
  - In generated C the function is handed the worlds it reaches, where its World parameter is written: the match's, read-only unless it sets, and this machine's for local components.
- `World` is built in: a game can't declare a type of that name outside a namespace.
- `lookup` parameters (`lookup Health health`, read as `health[e]`) are gone. Writing one is an error that says what to write with `World` instead.

### Entities

- `Spawn(...)`, `entity.Add(...)` and `entity.Remove(...)` take plain component lists. `with` and `without` only appear in queries.
- Component values are written without `new`. A bare type name means all fields get default values.
- An entity has at most one component of each type. `Add` on a component the entity already has replaces its value. `Remove` of a component the entity doesn't have does nothing.
- When an entity needs several of something, use a list inside one component, one entity per item pointing back at its owner, or separate component types.
- Structural changes (`Spawn`, `Add`, `Remove`) are deferred. They're applied together at the next fixed point in the tick, in a deterministic order. The entity handle `Spawn` returns is usable immediately, for example to store in a component. The entity's data becomes readable once the changes are applied.

```csharp
var e = Spawn(Transform { scale = float3(1, 1, 1) }, Player);
e.Add(Stunned { duration = 2 });
e.Remove(Stunned);
```

### Entry point

- The scene named `Main` is where the program starts (see Scenes). There is exactly one.
- A local `Main` starts the program in it, usually a menu. A match `Main` starts a single-player match with it right away, the same as a menu starting one.
- `Main` sets up the first scene, not the game loop: the engine runs the tick.

```csharp
scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Transform { scale = float3(1, 1, 1) }, Player { speed = float3(0, 0, 5) });
}
```

### Evaluation order

- Expressions evaluate left to right, like C#: operands, arguments and field initializers run in source order, the same on every platform and in every build. `Spawn(Pair { a = Spawn(Thing), b = Spawn(Thing) })` spawns `a`'s Thing, then `b`'s, then the Pair.
- The order shows wherever an expression has side effects: `Spawn` and `Scene.Load` (entity IDs), a widget or a call that draws, a call to C (what C keeps), and a call that writes, which changes what it's given as `mut`, makes what it's given as `out`, or changes the value a `mut` method is called on (a `Random`'s methods are such calls: see Random numbers). So do the functions, methods and operators that make such calls. `Pack(Next(counter), Next(counter))` runs the first `Next` first.
- A value is read in its turn too: in `Pack(counter.n, Next(counter))`, `counter.n` is read before `Next` changes it, and in `Pack(Next(counter), counter.n)`, after.
- `cond ? a : b` runs the condition first, then only the side it picks, as in C#.

### Archetypes

- There are no archetype declarations. The compiler derives every archetype from the code: the component set at each spawn site, plus every combination reachable through adding and removing components.
- Reachable means by the code as written: `this.Add(X)` in a system only reaches the entities its parameters, `with` and `without` admit; `e.Add(X)` on an `e` that a `Spawn` or `Scene.Load` in the same code made (a local that isn't `mut`) reaches that one; `Add` or `Remove` on an entity read from a field or a parameter may reach any entity. The changes recorded for an entity in one tick apply in any order, so every combination of the components those sites add and remove is reachable from it.

## Provisional: implemented in v0, awaiting approval

tidec v0 needed answers to these to work end to end. They're implemented, but the owner hasn't approved them yet, so any of them can change.

### Types and values

- Built-in scalar types are `bool`, `byte` (unsigned 8-bit), `int` (32-bit), `long` (64-bit), `float` (32-bit), `char` (one character of text: see Text), `Entity` and `LocalEntity` (see Local state). There is no `double`. Vector types are under Vector math, `Color` under Views and drawing, `string` under Text and `List<T>` under Lists.
- `byte` holds 0 to 255 in one byte, aligned to one byte, including in structs, components, input, `List<byte>` and grid cells. `byte` widens implicitly to `int`, `long` and `float`. Arithmetic and bitwise operators promote a byte to `int`; unary `-` and `~` do too. `byte(x)` keeps an integer's low 8 bits; for a float it first uses `int(x)` (truncated, saturating, NaN to 0), then keeps those bits. An `int` constant from 0 to 255 fits where a byte is expected, including in a list literal; other integers require `byte(x)`. Compound assignments and `++`/`--` narrow their integer result back to byte, wrapping. Input sends a byte as 8 bits; deltas send whether it changed and its 8 bits when it did. Hot reload carries byte fields over to `int`, `long` or `float` fields of the same name.
- `1.5` is a `float`; the `f` suffix is optional. The zero before the point can be left out: `.5` and `.5f` are `0.5`. An `int` converts to `float` implicitly, never the other way.
- A decimal literal with the lowercase suffix `deg` is a `float` in radians: `180deg`, `.5deg`, `1e2deg` and `1_8_0deg`. The number is first read as a 32-bit float, then converted at compile time with the same bits as `Math.Radians` of that float. Unary minus works normally (`-90deg`). The suffix touches the number, cannot combine with `f` or `L`, and is not accepted on hex or binary literals. It introduces no new type or implicit conversion; `quaternion.Euler(0, 180deg, 0)` receives radians.
- `long` is a 64-bit integer, for counters, sums and hashes that outgrow an `int`: tick counts over months, scores, money, IDs from outside. An `int` converts to `long` implicitly, and `int` and `long` mix in arithmetic, comparisons and bitwise operators, giving a `long`. Nothing else converts by itself: `float(x)` makes a `long` a `float` (rounded to the nearest), `long(x)` a `float` a `long` (truncated toward zero, saturating, NaN to 0) and `int(x)` a `long` an `int` (its low 32 bits, as C#'s unchecked cast). A `long` with a `float` is an error that says which to write. There are no `long` vectors, and shift counts stay `int`s. `long` arithmetic wraps and divides by zero to 0 like `int`'s; shifts use the count's low 6 bits. A `long` literal has an `L` suffix (`5L`), and a literal past the `int` range is a `long` by itself: decimal up to 9223372036854775807, hex and binary up to 64 bits, read as the bit pattern (`0xFFFFFFFF` is the `int` -1; `0xFFFFFFFFL` is 4294967295). `Math.Abs`, `Sign`, `Min`, `Max`, `Clamp` and `Hash` take longs; `switch` works on them; text shows them with the same formats as ints (`X` up to `X16`). A `long` field is 8 bytes, aligned to 8, in components, inputs (sent as a 64-bit varint, and as a delta by how much it changed), lists and grid cells. On hot reload an `int` field carries over to a `long` of the same name, and a `long` to a `float`.
- In `cond ? a : b`, the condition is a `bool`, and the two sides have the same type or one converts to the other's, as in C#: `ready ? 1 : 0.5` is a `float`. It binds looser than every binary operator and groups to the right.
- Integer arithmetic wraps on overflow. Integer division and modulo by zero give 0, so no input can crash the simulation.
- Bitwise operators `& | ^ ~ << >>` work on `int` and `long`, with compound forms `&= |= ^= <<= >>=`. As in C#, shift counts use their low 5 bits (`1 << 33` is `2`; an `int` shift) or 6 (a `long` shift) and `>>` keeps the sign. Operator precedence follows C#.
- Integer literals can be decimal (`255`), hex (`0xFF`) or binary (`0b1010`). Decimal goes up to 2147483647 as an `int`, and beyond that is a `long`. Hex and binary can use all 32 bits and are read as the int's bit pattern: `0xFFFFFFFF` is `-1` and `0x80000000` is the lowest int; past 32 bits they're longs the same way.
- `_` separates digits in any number, as in C#: `1_000_000`, `0b1111_0000`, `0x_FF_FF`. It's allowed between digits and right after `0x` or `0b`, but not at the start or end or next to `.`.
- Local variables need an initial value.
- The warning for a local that's never read (see Mutability), in its details:
  - It's about the locals statements declare: `var`, a type, or `mut` in front of a name, in a `for`'s first part too (`for (var i = 0; ...)`, where the for's own condition reading it counts).
  - The names code needs whether it uses them or not are exempt, as in C#: a `foreach`'s and a `parallel` loop's variable (a loop that only counts), the name after `is`, and a `Sort`'s two elements.
  - No name opts out, `_` included: a value that isn't needed isn't given a name, and a call that's there for what it does is a statement.
  - A read is the local's name anywhere but where an assignment writes: `total = 5`, `total += hits`, `total++`, `stats.health = 3` and `seen[i] = 1` don't read `total`, `stats` or `seen`. A read on the right of its own assignment counts (`total = total + 1`), and so does passing it, as a `mut` argument too, and calling a method on it.
  - A call does something when it's a `Spawn`, `Scene.Load` or the like, a widget (`GUILayout.Button`), an `await`, a `try`, a list's or a map's change (`items.Remove(x)`), or a function or method that calls C, draws, is async, takes a `mut` or an `out` parameter or is a `mut` method. Any other call only gives a value, a function's too.
  - A local that's assigned later gets a note of its own: remove it and what assigns it, or use it.
  - Errors hide uses, so a local whose scope has an error in it waits: the warning comes once the code checks.
- `default` of a struct or component is its fields' defaults, not all zeros as in C#, where `default` skips field initializers: a Tide value never exists without them.
- As in C#, `default` is only compared: `default + 1` is an error. So is `var x = default;`, which says no type, and a place where nothing else does, like `if (default)`.
- In a built-in call, the other arguments pick the version, and `default` takes its parameter's type: `Math.Max(default, 1.5)` is a float. A call whose versions take different types there is an error, like `Math.Mul(q, default)` (a quaternion or a float3), and so is a `Math` function whose arguments are all `default`. Constructors like `float3(...)` don't take it.

### Systems

- A system with no component parameters runs once per tick. Otherwise it runs once per matching entity.
- A parameter that's never used, or declared `mut` and never written, is a warning: it makes other systems wait for nothing. A component that's only there to filter entities belongs in `with`.
- **Where the World goes.** Systems, views and event handlers take `World`, async handlers too: their tasks keep it across their waits. Ordinary functions and methods can take the caller's `World` explicitly, and pass it through further helpers (see The World). Async functions and extern functions take none. Outside an argument to a `World` parameter it is only ever called: it can't be stored or compared, and `world.Get<Health>(e).value = 5` is an error, since `Get` gives a copy.
- **The World in local code.** A local component is reached the same way, with a `LocalEntity`: `world.Get<Spark>(e)`. A view or a local handler with `mut World` can `Set` one, as it can change local state. Match code's `World` only reaches the match's components, and a local handler's only local ones for now. A view that reads the match through its `World` only runs in a match, as one that takes a match component does, and what `Get` gives it is the component as of the latest tick, not blended between ticks.
- **A type in `<>`.** Only the `World`'s methods take one. A `<` after a method's name starts a type when a name, `>` and the call's `(` follow, as in C#: `a.b < c.d > (e)` would compare a `bool`, so no comparison reads that way.
- **`Set` and text.** `Set` of a component that holds text, lists or maps copies them into the world as the entity's own, as assigning the component does. A system that does it changes the match's heap, like any that writes text.
- **`Set` where code runs at once.** In a parallel loop's step and in a `Sort`'s block, `Get` and `Has` work and `Set` is an error, as writing anything outside the step is.
- **A game's own `World`.** A type named `World` in a namespace is what a plain `World` means in that namespace's code, and in files that `using` it, as with any name: the built-in one is hidden there. Elsewhere it's written with its namespace, `Game.World`, beside the built-in `World`.
- `return;` ends the system for the current entity.
- Locals can't reuse the name of another local or parameter in scope.
- Names starting with `tide_` are reserved for generated code.

### Entities

- `e.Destroy()` destroys an entity. It's deferred like other structural changes.
- The "fixed point" where structural changes apply is the end of each tick (and the end of `Main`). Changes apply in the order they were recorded.
- `Add`, `Remove` and `Destroy` on an entity that was already destroyed do nothing.
- In a system that splits its entities across threads, `Spawn` returns a temporary handle: the entity gets its ID once the system is done, in the order one thread would have given them, and the handles the system kept in the components it changes, the changes it recorded and its events become the real one, before any system that waits for it starts. So such systems don't wait for each other to spawn. Until then, text shows the handle as `Entity(new)`, and C can tell with `tide_entity_is_temporary`.
- `Spawn` can't appear on the right side of `&&` or `||`, or in a side of `?:`. Those parts only run sometimes, while spawns run first, in order (see Evaluation order). Spawn into a local before the condition, or use `if`/`else`.
- **Sparse components.** A component marked `[Sparse]` is kept as a presence bit and its value in every archetype it can reach, rather than making an archetype of its own with each combination: `Add` and `Remove` of it move no entity. A component with no fields (a tag) that any `Add` or `Remove` names is sparse by itself; a tag only set at `Spawn` isn't, so filtering on it costs nothing per entity. Systems, views and handlers that take or filter on a sparse component skip the entities that haven't got it, row by row. Two systems can't meet the same entity when one needs a sparse component the other excludes, but their entities are rows of the same archetype, kept side by side: where one of them writes a component the other reads or writes, they wait for each other all the same. Only archetypes keep two systems apart. Toggled status like `Stunned`, `Burning` or `Selected` wants `[Sparse]`; a component most entities of a kind have, or that's set once, doesn't.

### Built-ins

- `Time` is a built-in singleton with `float dt` (the fixed tick length), `int tick` (starts at 0), `float t` (the seconds since the match started) and `int tickRate` (the ticks in a second). Systems can read it but not write it.
  - `t` is `tick * dt`, worked out from the tick each time and never added to, so it doesn't drift. Views see it blended between ticks like any float of the match, so it moves every frame, in the frags and verts they draw through too.
  - A float holds about 7 digits, so `t` gets coarser as the match goes on: after a day its steps are about 8 milliseconds. What has to be exact counts in `tick`.
  - `tickRate` is 1 / `dt` to the nearest whole number: the `tickRate` setting, or the rate the machine that runs the match gave it.
  - The match's seed is read as `Random.seed` (see Random numbers). Generated C keeps it in `Time` underneath; Tide code can't name `time.seed`.
- `Debug.Log(text)` prints a line for whoever runs the game: the terminal under `tide run`, the browser's console on the web, the system's log on Android. `Debug.Assert(condition)` and `Debug.Assert(condition, text)` stop the program when the condition is false, naming the file, the line and the source line, and the text. Both go anywhere, and neither changes the match, so it stays the same on every machine with or without them. A tick a client runs again after a rollback logs again: a log of match state is best prefixed with `Time.tick`.

### Syntax

- Statements: blocks, `if`/`else`, `switch`, loops (see Loops), `break;`, `continue;`, `return;`, `fail error;` (see Errors), local declarations, assignments (`= += -= *= /= %= <<= >>= &= |= ^=`), `i++` and `i--`, and calls: of `Spawn`, `Add`, `Remove`, `Destroy`, `Send`, the `World`'s `Set`, the `Draw` and GUI functions, lists' methods, and methods and functions, with a block after the ones that take one (several of them can be listed with commas before one block: see Calls that take a block), and with `!` after or `try` before the ones that can fail.

### Evaluation order

- A compound assignment reads its target before its value is worked out, as in C#: `counter.n += Next(counter)` adds to what `counter.n` was. An index in the target (`items[Next(counter)] += 5`) runs once, before the value.
- A method reads the value it's called on as it runs, once its arguments are worked out, as C# reads a struct variable: `stats.Plus(stats.Bump())` sees what `Bump` left. An element is read once its index is worked out: `items[Push(items, 7) - 1]` is the element `Push` added.
- A read-only argument is a copy made in its turn, a list's too: `Total(items, Push(items, 4))` totals the list as it was. A `mut` method given its own value's list (`bag.Absorb(bag.items)`) gets a copy as well.
- A read-only method's receiver follows the same copy rule: when a `mut` or `out` argument can change its lists or maps during the call, the method reads its own copy, made after its arguments are worked out.
- A list's, a map's and a set's changes that give a value (`items.Remove(x)`, `seen.Add(k)`) are calls that write, and so are widgets that change what they're given (`GUILayout.Toggle(title, open)`), C functions with a `mut` or an `out` parameter, and a map's `TryGet`.
- `++i` and `i++` as values are writes like those: `i + ++i` reads `i` before it changes, `++i + i` after, and `items[i++] + items[i++]` reads two elements in a row.
- A variable an `out` argument makes is read in its turn like any other: in `Count(tally, out var made) * 10 + made`, `made` is what the call gave, and in `seen * 100 + Count(tally, out seen)`, `seen` is read before the call makes it anew.
- A call whose value is read several times runs once: `float4(Corner(counter), 0, 1)` and `Corner(counter).yx` each call `Corner` once.
- Arguments passed by name are worked out in the order the call writes them, not the parameters': in `Pack(ones: Next(counter), tens: Next(counter))`, `ones` gets the first `Next`. A default the call leaves out is a constant, so it has no turn.

### Functions and blocks

- A call with a block after it is a statement, and the block's braces go on lines of their own, like any other's. A function that takes an Action returns nothing, since its call is a statement: it changes what its caller passes as `mut` instead.
- Only functions take an Action, not methods, and an Action is never `mut`. An Action can't be stored in a field or a local.
- In the block, `return` ends the caller, as if the block were written there, and `break` ends the caller's switch, even if the function runs the block inside a switch of its own. `return` in the function's own code ends the function.
- A function that takes an Action is copied into each call in generated C, with its locals renamed, so its names never hide the caller's in the block.
- A `mut string` parameter is the caller's text, a local's or a field's, which the function changes.
- **Functions that send, spawn and change entities**, beyond what's decided (Functions): built 2026-10-08, each point a pick awaiting the owner.
  - **Which world** a function's `Send`, `Spawn`, `Add`, `Remove` or `Destroy` is in is told by what's written, as an async function's is: the event's own, an `Entity` or a `LocalEntity`, the components'. A command is this machine's to send. What's written has to agree with itself, which is the one error in the function: a `Spawn`'s, an `Add`'s and a `Remove`'s components are all of the entity's world, and an event goes to an entity of its own. A `Spawn` with no components is the match's.
  - **Who can call it:** what's the match's (a match event, the match's entities), match systems, event handlers, command handlers and the match's tasks; what's this machine's (a local event, a command, local entities), views, local handlers and this machine's tasks. An async function that calls one is of that world (see Tasks). A function that does both can be written, and nothing can call it.
  - **What never does, whoever calls it:** a `Sort`'s comparison and a parallel loop's step, the input's `Sample` and `Sanitize`, frags and verts, and an `audio` declaration: a call there of a function that does is the error. A struct's operators and a component's `Interpolate` are refused where the `Send` is written, or the call of what sends: an operator is handed its operands and nothing else, in a default's value too, and the engine calls `Interpolate` as views blend.
  - **Methods do as functions do**, though they still can't draw. A component's method reaches its entity as `this`; a function has none and takes an `Entity`.
  - **A function's spawn joins the scene of the entity its caller runs for**, through however many functions, when that entity is of the spawn's world, as the caller's own would.
  - **It's the caller's in the schedule too:** a system that calls a function that spawns hands out entity IDs (it waits for the spawning systems before it, or settles its handles when it splits), and one whose function records text changes its world's heap, as if the `Spawn` or the `Send` were written in the system. Sending makes nothing wait, there as anywhere. A foreach over a grid whose step calls one goes in order.
  - **Its calls keep their order:** what a function records goes where it's called among what the caller records itself, and a call that gives a value runs in its turn, like one that writes (see Evaluation order).
  - `Add` and `Remove` in a function are on an entity read from a parameter, so they may reach any entity (see Archetypes): a helper makes more combinations than `this.Add` in the system does.
  - Not part of it: `Snap`, `Scene.Load` and `Scene.Unload`, session calls and starting tasks stay the system's, view's or handler's own to write.
  - In generated C the function is handed the world it records in, as one that takes a `World` is, and the scene its spawns join. One that does none of this generates the C it did.
- Operators and their precedence follow C#. Comments are `//` and `/* */`.
- Source files are UTF-8, and a byte order mark at the start is skipped. Names are ASCII: other characters only go in comments and text.

### Limits

- 256 components and 1024 archetypes.
- A world's heap, which keeps its text, lists, maps, sets and grids, holds 1 GiB, in blocks of 128 MiB at most: a text it keeps is one block. The scratch area, where code makes text and lists along the way, holds 1 GiB on each thread. Past any of these the program ends, as it does when it runs out of memory. A list stops growing at 1 GiB of elements.
- Everything else grows as it needs, with no limit but memory: entities, entities per archetype, and structural changes and events per tick.

### Namespaces

- `namespace Game.Combat;` at the top of a file puts everything in the file in that namespace. A file has at most one such line; files without one are in the global namespace. Namespaces can be dotted.
- `namespace Combat { ... }` puts the declarations between its braces in that namespace (agreed 2026-10-07, to separate concerns within one file; the rules under it are Provisional). It goes wherever a declaration does, so a file can have several, and one inside another.
  - A block's name is inside the namespace it's written in: in a file of `namespace Game;`, `namespace Combat { }` is `Game.Combat`, and a block inside it named `Rules` is `Game.Combat.Rules`. A block's name can be dotted too.
  - A namespace is a set of names, not a place: the same one can be a block twice in a file, a block in several files, and a file's own namespace in another. What any of them declares is one namespace's.
  - What's inside a block is declared as anywhere: its order among the file's declarations is where it's written, so a block changes no system's order.
  - Provisional: `using` stays at the top of the file, and is every block's of the file. A `using` inside a block is an error.
  - Provisional: the line form comes before any declaration, a block being one: a file says its own namespace first, and blocks after it are inside that.
  - Provisional: a block takes no attributes, `local`, `virtual` or `override`: they go before the declarations inside it.
  - Provisional: a qualified name is still exact, from the global namespace: inside `Combat`, the block `Rules`' function is `Combat.Rules.Cap`, not `Rules.Cap`. (C# also reads a qualified name's first part from the namespaces around the code. Not built.)
  - Provisional: a package's blocks are in its namespace or one inside it, as its files are: a package file with no namespace line can have everything in `namespace Package { ... }`.
  - Where a file is half typed, editors and the errors after a first one tell where a block's declarations start by their column: the first line's after the `{`, which the formatter makes one step in from the block's `namespace`.
- `using Physics;` at the top of a file lets it name Physics' declarations without the prefix. `namespace Name;` and `using` come before any declaration.
- A plain name is looked up in the namespace of the code that writes it (its block's, or its file's), then the namespaces around it out to the global namespace, then the `using` namespaces, as in C# (agreed 2026-10-05). So what a game declares itself is what its code means, whatever a namespace it imports also has by that name: a game with a `Body` of its own that says `using Physics;` means its own by `Body`, and writes `Physics.Body` for the other. If two `using` namespaces both have a name and nothing nearer does, it's ambiguous: write the namespace.
- Qualified names work wherever a type is named: parameters (`mut Combat.Health health`), `with` and `without`, component values (`Combat.Health { value = 10 }`), `Spawn`, `Add` and `Remove`.
- The same name can be declared in different namespaces. Built-in names can't start a namespace (`namespace Math;` is an error).
- In generated C, namespaced declarations are prefixed with their namespace: `Combat.Health` is `Combat_Health`, read with `tide_get_Combat_Health`.
- There's exactly one `Main` in a game, in any file or namespace. `namespace` and `using` are only keywords where a declaration can start: anywhere else they're names like any other.

```csharp
// combat.tide
namespace Combat;

component Health { int value = 100; }

// main.tide
using Combat;

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Health, Items.Health { value = 3 });
}
```

```csharp
// One file, two namespaces, and the global one around them
namespace Combat
{
    component Health { int value = 100; }

    namespace Rules
    {
        int Cap(int value) { return Math.Min(value, 100); }
    }

    system Heal(mut Health health)
    {
        health.value = Combat.Rules.Cap(health.value + 1);
    }
}

namespace Items
{
    component Bag { int coins; }
}

scene Main { }

event(Spawned) Setup(with Main)
{
    Spawn(Combat.Health, Items.Bag);
}
```

### Order of systems

- Files compile in order of their paths, compared byte by byte. Systems run in that order, then in the order they're declared within a file. Views, which draw once per frame, follow the same rules among themselves; later views draw on top.
- `[Before(X)]` makes a system run before `X`, and `[After(X)]` after it. They take any number of systems (`[After(Gravity, Collisions)]`), with qualified names when needed (`[After(Physics.Gravity)]`).
- Where several systems share a name, it's written with the parameters of the one meant, as types: `[After(Move(Position, Velocity, with Player))]`. A shared name alone is an error that lists them (see Default values, named arguments and overloads).
- An enum can define named phases: `[Stage(TickStages.Simulation)]` assigns a declaration to a phase, and `[Stage(TickStages.Simulation, 10)]` gives it an offset within that phase. The offset is an `int` known while compiling, like an enum value: an integer literal, an integer constant, or integer operations on them. It defaults to 0 and can be negative. Each declaration has at most one `Stage`.
- Phases follow the enum members' declaration order, regardless of their numeric values. Lower offsets run before higher ones within a phase; offsets never move work into another phase. Equal offsets keep the default order unless declaration constraints change it.
- `Before` and `After` can name enum members too, mixed with declaration names: `[After(TickStages.Simulation, Gravity)]`. A phase reference means before all of that phase or after all of it, including every offset. Empty phases keep their place in the order. Different enums define independent phase orders; explicit declaration constraints can connect them.
- Named phases apply independently to systems, views, and the handlers of each event. Systems in a later phase wait for all earlier-phase systems, even if they share no data. Systems at the same offset can still run together when their data allows it. Declarations with no phase keep the default order wherever their constraints allow.
- Otherwise the default order holds: of the systems whose constraints are met, the earliest in the default order runs next.
- Constraints that form a loop are an error naming the loop. Systems and views are ordered separately, and `Main` isn't ordered: it runs once, before everything.
- The language server shows a system's place in the order when hovering it.
- Systems that share no data they write can run at the same time; the others wait, in this order. Above each system, the language server shows its stage, what it reaches through its `World` and why it waits ("stage 2 · World: reads Health", "after Move: both write Body", each wait a link to its system), and `tidec --schedule` prints the whole plan.

```csharp
[After(Physics.Gravity)]
system Move(mut Body body) { ... }
```

```csharp
enum RenderStages { Prepare, Objects, Effects, UI }

[Stage(RenderStages.Prepare)]
view ClearScene() { Draw.Clear(Color.black); }

[Stage(RenderStages.Objects, -10)]
view Background() { ... }

[Stage(RenderStages.Objects)]
view Objects(Body body) { ... }

[After(RenderStages.Objects), Before(RenderStages.UI)]
view Effects() { ... }
```

## Vector math

Follows Unity.Mathematics, with Tide's naming. Everything in this section is decided.

- Types: `float2`, `float3`, `float4`, `int2`, `int3`, `int4`, `quaternion`, `float2x2`, `float3x3` and `float4x4`. They're lowercase built-in value types, like `float`.
- Math functions live on `Math` and follow the PascalCase method convention: `Math.Dot(a, b)`, `Math.Normalize(v)`, `Math.Mul(q, r)`. This overrides Unity.Mathematics' lowercase `math.dot`.
- Swizzles read any combination of components: `v.xz`, `v.zyx`, `v.xxyy`.
- Angles are in radians. A decimal degree literal (`180deg`) converts to an ordinary float in radians at compile time. `Math.Radians(degrees)` and `Math.Degrees(radians)` convert values.
- Math is forgiving with bad values, which can come from other players' input. `Math.Clamp` always returns a value in range: NaN gives the lower bound. `Math.Min` and `Math.Max` with one NaN argument return the other. `Math.Normalize` never gives NaN for a vector with no direction (next). Everywhere else, results match Unity.Mathematics.
- `Math.Normalize` gives zero for a vector too short to normalize, and the identity for a quaternion too short, where dividing by the length would give NaN. There's one `Normalize`, and it's the safe one: Unity.Mathematics splits it into `normalize` and `normalizesafe`, and here Tide's own rules win. NaN in the match is a determinism hazard (its bit patterns aren't portable), and normalizing a move that's zero while no key is held is the first thing every game writes.
  - Too short is a length squared at or under the smallest normal float (about 1.18e-38): a length of about 1e-19 or less, zero included. A vector with NaN in it gives zero too. Any longer vector gives exactly `v * (1 / Sqrt(Dot(v, v)))`, as it always did; that's zero for one so long its length squared overflows (past about 1.8e19), and still NaN in an infinite component (zero times infinity).
  - So vectors shorter than about 1e-19 give zero, where they gave a unit vector while `Normalize` wasn't safe.
  - It gives zero wherever it runs: in the match, in views, and in code compiled for the GPU, whose shader languages leave their own `normalize` of a zero vector undefined, so Tide's `Normalize` there is never just theirs.
  - `Math.NormalizeSafe` is gone: writing it is an error that points to `Math.Normalize`.

```csharp
quaternion spin = quaternion.AxisAngle(float3(0, 1, 0), input.turn * time.dt);
trs.rotation = Math.Mul(trs.rotation, spin);
float3 forward = Math.Rotate(trs.rotation, float3(0, 0, 1));
trs.position += Math.Normalize(forward) * 5 * time.dt;
float2 flat = trs.position.xz;
```

**Constructors**

- Vectors take any mix of scalars and vectors adding up to the right size (`float4(v.xy, 0, 1)`), or one scalar for every component (`float3(1)`).
- `float3(int3)` and `int3(float3)` convert between int and float vectors. `int(x)` and `float(x)` convert scalars. Float to int truncates toward zero, saturates at the int range, and turns NaN into 0, so no input is undefined.
- `quaternion(x, y, z, w)`, `quaternion(float4)` and `quaternion(float3x3)`.
- Matrices take one column vector per column (`float3x3(c0, c1, c2)`) or all numbers row by row (`float2x2(1, 2, 3, 4)`, where `1, 2` is the first row). Also `float3x3(quaternion)` and `float4x4(float3x3 rotation, float3 translation)`.

**Members and constants**

- Vectors have `x`, `y`, `z`, `w`. Swizzles can also be assigned (`v.xz = float2(1, 2)`), as long as no component repeats.
- `quaternion` has `value`, a `float4` with (x, y, z) as the vector part. Matrices are stored column by column and have columns `c0` to `c3`.
- `Math.PI`, `Math.TAU`, `Math.E`, `quaternion.identity`, `float2x2.identity`, `float3x3.identity`, `float4x4.identity`.
- `quaternion.AxisAngle(axis, angle)`, `quaternion.Euler(radians)` from a `float3` or `quaternion.Euler(x, y, z)` from three angles in radians (equivalent to `quaternion.Euler(float3(x, y, z))`; Z first, then X, then Y, Unity's default order), `quaternion.LookRotation(forward, up)`, `float4x4.TRS(translation, rotation, scale)`, `float4x4.Translate(translation)`.
- Projections, as Unity.Mathematics': `float4x4.PerspectiveFov(verticalFov, aspect, near, far)`, with the field of view in radians and `aspect` width over height, and `float4x4.Ortho(width, height, near, far)`. They take a view looking down -z, as OpenGL's and Unity's, to clip space, where z goes from -1 at `near` to 1 at `far`.

**Operators**

- `+ - * /` are component-wise on vectors, and `%` too on int vectors. A scalar widens to the vector's size, and int widens to float, so `v * 2 + 1` works.
- An int vector converts to a float vector of the same size implicitly, like `int` to `float`.
- Matrices support `+` and `-` with the same type, and `*` and `/` by a number. There's no `*` between matrices, or between a matrix and a vector; that's `Math.Mul`.
- Quaternions have no operators: combine rotations with `Math.Mul` and rotate vectors with `Math.Rotate`.
- Comparisons and `==` work on scalars only.

**Functions**

- Component-wise, on numbers, vectors and colors: `Abs`, `Sign`, `Min`, `Max`, `Clamp` (ints too); `Floor`, `Ceil`, `Round` (ties to even), `Trunc`, `Frac`, `Sqrt`, `Rsqrt`, `Saturate`, `Radians`, `Degrees`, `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Exp2`, `Log`, `Log2`, `Log10`, `Pow`, `Step`, `Lerp`, `Unlerp`, `SmoothStep`. A `Color` goes through them channel by channel, alpha too, with colors and with numbers (a number is that number in every channel), and gives a `Color`: `Math.Lerp(a, b, 0.5)`. A color and a vector in one call are an error. (That colors are taken is agreed, 2026-10-08: "Color is essentially a float4 anyways"; that it's every component-wise function, and that vectors don't mix in, are Provisional.)
- Vectors: `Dot`, `Cross`, `Length`, `LengthSq`, `Distance`, `DistanceSq`, `Normalize` (zero for a vector too short to normalize, never NaN), `Reflect`, `Csum`, `Cmin`, `Cmax`.
- Quaternions: `Mul` (two rotations, or a rotation and a `float3`, which it rotates), `Rotate`, `Inverse`, `Conjugate`, `Normalize` (the identity for a quaternion too short to normalize, like a field nothing set), `Dot`, `Slerp`, `Nlerp`, `Forward`, `Up`, `Right`, `Angle`.
- Matrices: `Mul` (two matrices, or a matrix and a vector of its size), `Transpose`, `Inverse`, `Determinant`, and for `float4x4`, `Transform` (a point) and `Rotate` (a direction).
- Everything is deterministic (see AGENTS.md). The transcendental functions are Tide's own, accurate to about 1 ulp but not correctly rounded.
- `Math.Hash(value)` on an `int`, `int2`, `int3` or `int4` gives random numbers that keep no state: the same int for the same value on every machine, like `Math.Hash(int3(x, y, seed)) % 6`. Code the GPU runs gets the same numbers (GPU code, under Views and drawing).
- Noise: `Math.Perlin(point[, seed])`, `Math.Simplex(point[, seed])` and `Math.Cellular(point[, seed])`, of a `float2` or a `float3`, which keep no state and give the same bits on every machine, so noise can be in the match. The owner asked for noise functions in `Math` that work on the GPU too (2026-10-07); which functions, their names and the rules below are picks.

### Provisional

Implemented, awaiting approval:

- Noise (2026-10-07; `engine/src/noise.c`, and the same lines in GLSL and WGSL in `compiler/src/shader.c`):
  - Three kinds, in 2D and 3D: `Perlin` (gradient noise, Perlin's improved: a quintic fade, 8 unit gradients in 2D and the 12 edge directions of a cube in 3D), `Simplex` (Gustavson's, with the same gradients) and `Cellular` (Worley's: one feature point in each unit cell, placed by the cell's hash, and the distances to the nearest and the second nearest, Euclidean). Named by their kinds (Unity's are `noise.cnoise`, `snoise` and `cellular`), as explicit beats a plain `Noise` that would mean one of them. No 1D, no 4D, no periodic noise, no `float2` of derivatives, and no fractal sum: octaves are a line of the game's.
  - Each gradient or feature point is `Math.Hash` of the cell and the seed (`int3` in 2D, `int4` in 3D), never a permutation table: nothing repeats, the same cell is the same everywhere, and the GPU gets exactly the cells the CPU does. What's between them is floats, with the GPU's rounding.
  - A seed is an `int` a call can leave out, 0 then: `Math.Perlin(p)` is `Math.Perlin(p, 0)`. Another seed is another field of noise.
  - `Perlin` is from -1 to 1 exactly (its gradients keep it within `sqrt(n / 4)`, which the scale takes to 1; a GPU's rounding may go a millionth past), and 0 at every whole number. `Simplex` is scaled (99.2 in 2D, 32.5 in 3D) so that the greatest value found by a search is a hair under 1: about -1 to 1. `Cellular` is `float2(nearest, second)`, from 0 to about 1.4 in 2D and 1.3 in 3D, Unity's shape (`noise.cellular` gives F1 and F2 too).
  - The cell is the point's floor, as an int saturated past what an int holds, so a point past 2^31 is in the last cell, with no error. A NaN point gives NaN.
  - `tide_perlin_f2(p, seed)` and the five beside it in `tide/math.h` are C's.
- `Math.Hash` gives an int from 0 to 2147483647, so `%` never goes negative. It's xxHash32 of the value's bytes (little-endian, seed 0) without the top bit: what Unity's `math.hash` gives for a block of memory. Unity's `math.hash` on vectors is a cheaper mix whose low bits follow the input's, so `& 3` of it makes stripes; Tide's differs from it on purpose.

## Random numbers

The owner asked for these on 2026-10-05: random numbers whose state is ECS data ("we likely want it to be a component") and a seed for the match that's "random every time and new joiners get". On 2026-10-08 the owner found that inconvenient ("i need to either make my own singleton or hack together something local with the struct"), and that `Time.seed` "has nothing to do with time". Of a built-in singleton like `Time`, and of calls on the type that keep no state, the owner picked the calls.

### Decided

- **`Random` rolls with no generator of the game's**: `Random.NextInt(6)`, `Random.NextFloat2Direction()` and the rest, called on the type, in a system with no parameter for it.
- **Rolling writes nothing**, so it costs the schedule nothing: a system that rolls waits for no other system over it, and still splits its entities across threads. A singleton that every rolling system would write was turned down for that.

### Provisional

Implemented, awaiting approval:

- **What a roll on the type is made from.** Each run of the match's code that rolls (a system for an entity, a system that runs once, a handler's run) has a generator of its own, a local of that run: made from the match's seed, the tick, the entity it runs for (none for code that runs once) and which system or handler it is, by its name. Its rolls step that generator, in the order they run. So a run's rolls are a sequence, two systems roll differently for one entity in one tick, each entity rolls for itself, a tick later it's all other numbers, and a tick run again rolls the same.
  - A handler's generator also takes which of the tick's runs of rolling handlers it is, counted from the tick's start, so one event sent twice to one entity in one tick rolls differently each time. Handlers run one at a time in the same order on every machine.
  - A system's rolls start over each tick. A sequence that goes on across ticks, or from a seed the game picks, is a `Random` value (below).
  - Adding a roll to a system changes its later rolls in that run, and nobody else's. Renaming a system changes its rolls.
- **The methods are the value's**, by the same names, with the same ranges.
- **Functions, methods and operators roll too**, with nothing passed: one that rolls, itself or through what it calls, is handed its caller's generator.
- **Local code** (views, local handlers, and the functions they call) rolls with a generator of this machine's local state, one for all of it, which is no part of the match. It takes in each frame's length as the host gives it, so no two runs with a window roll alike, and a run with no window, whose frames are made up, repeats.
- **It rolls wherever a roll can mean something** (the owner, 2026-10-08, of a first version that refused several of these: "if it could we should do the extra work to get it usable there"). How each is built is a pick:
  - **A parallel loop's steps**, and a foreach over a grid whose steps run at once: the code around the loop gives the loop a generator (two rolls of its own), and each step makes its own from that and its place, a grid's cell (a block's first) or a list's index. So a loop's numbers don't depend on threads, and a step's rolls are a sequence within the step.
  - **A sort's comparison** steps the generator of the code around it, in the order the sort compares, which is the same on every machine.
  - **A task** keeps a generator in its frame, so its rolls go on across its waits, and are the world's as the task is. It's made where the task starts, from the generator of the code that starts it; a task it awaits gets one from its own the same way; and a handler that's a task gets one as a handler does, from the tick, its entity and its turn.
  - **The input's `Sample`** rolls with this machine's generator, as local code does. A host that gives `tide_input_sample` no local state gets a generator of the program's.
  - **An `audio` declaration** keeps a generator after its own state, on the next 8 bytes of what its voice keeps for it (so its own state is 8 bytes less at the most), seeded the first time the voice runs by how many voices rolled before it. One thread renders, so a run with no window repeats.
  - **Frags and verts, on the GPU**, and the functions, methods and operators they call. These GPUs have no 64-bit integers, so it isn't the CPU's generator there:
    - **A generator each time the code runs**: a frag's for each pixel, a vert's for each vertex, made as it starts and gone when it ends. Its rolls step it in the order they run, statement by statement. Two rolls in one expression come in whichever order that GPU works the expression out, as its shader language leaves that open: where the order matters, a roll gets a statement of its own. (Each of `Random`'s own vectors is rolled x first everywhere.) A `?:` with a roll in it works both of its sides out, whichever it picks, on every GPU, so both rolls are made: WGSL has no other way to pick, and a pixel's rolls stay the same numbers everywhere.
    - **What it's made from**: for a frag, the pixel's place in the destination's own pixels (the window's or a render target's, from the top left, whatever the display's scale) and which instance of its draw the pixel is of; for a vert, which vertex of the mesh and which instance; and for both, a number of the frame's and which frag or vert is rolling, by its name. So no two pixels of a frame roll alike, two frags that draw over one pixel roll apart, and a function rolls with the generator of whichever stage runs it.
    - **The frame's number is one roll of this machine's local generator**, taken as the frame starts, before its views, and sent with every draw through a shader that rolls, among its uniforms. It's the same for every draw of a frame, so draws that roll still join into one draw call. So GPU rolls are other numbers every frame, the same every run where frames are made up (a run with no window) and no two runs alike with a window, as local code's rolls are. What has to hold still from frame to frame is `Math.Hash` and the noise functions.
    - **An instance is its place in its draw.** Instances of one draw roll apart. Two draws that don't join each start at their first instance, so where they cover one pixel through one frag, they roll the same there.
    - **Not the CPU's numbers.** These GPUs have no 64-bit integers, so the generator there has 32 bits of state: a linear congruential step and PCG's RXS-M-XS output. What's made of its 32 bits is the CPU's formulas: ints scaled down by multiply and shift (the high half of the product worked out in halves), floats 24 bits over 2^24, ranges that exclude their end, an empty range giving its start. The generator and the whole-number rolls are integer arithmetic, so a pixel rolls the same on every GPU for the same frame's number; what goes through sines, cosines and square roots is that GPU's rounding.
    - **Every method the type has.**
    - **`Random.seed` is read there too**: the seed of the match the frame draws, 0 with none, sent with the draw as another of its uniforms. So a frag draws noise from the match's seed, as the match computed it.
    - **A `Random` value stays the CPU's**: a field, a local or a parameter of that type, its methods and `Random(seed)` are errors in code the GPU runs, which say to roll on the type. Its state is 64 bits.
    - **A Sample that's `Filter.Trilinear` can't read at a place made of a roll**: the frag works that place out a first time as it starts, to ask how fast it changes, and a roll made twice is two numbers. An error that says to sample with `Filter.Bilinear`.
    - **A frag that rolls carries one more value from the mesh's corners** (which instance), of the 15 a draw has.
- **Where it never rolls**, each an error that says why: the input's `Sanitize` (the same repair on every machine, from the input alone), a type's `Interpolate` (the engine calls it itself, to blend the same two values the same way every frame), and constants and defaults, an operator in one included (no code runs).
- **`Random.seed`** is the match's seed, where it was `Time.seed`, which is an error that names it, with the rename as a quick fix. It's read with no parameter wherever code rolls, but in an `audio` declaration, whose thread has no match. A frag or a vert reads the seed of the match its frame draws, 0 with none, which its draw sends with it (above). The match's code reads its world's; local code (views, local handlers, local tasks, `Sample`) reads what its local state kept of the match the last frame drew, 0 with none; a function or method is handed it.
- **`Random` is also a built-in value, a struct with one field, `long state`.** It's a generator's whole state: 8 bytes, aligned to 8. It goes wherever a struct goes: a field of a component, a singleton, a struct, an event or local state, a local, a parameter, a list's element.
- **A value, not a built-in component.** A component's state can only be an entity's, and a game also needs a generator for the match (in a singleton) and for effects (in local state), so the value has to exist either way; a built-in component would be one more name for one of its homes. With the value, a game's own component is that home, and it says which systems share a sequence: `component Wanderer { Random random; }` and `system Wander(mut Wanderer wanderer)`. The built-in component would read `Spawn(Enemy { }, Random { })` and `system Wander(mut Random random, mut Body body)`, and every system that rolls for an entity would then write the same component, so none of them would run side by side.
- **Methods**, following Unity.Mathematics' `Random` with Tide's naming: `NextInt()`, `NextInt(max)`, `NextInt(min, max)`, `NextFloat()`, `NextFloat(max)`, `NextFloat(min, max)`, `NextBool()`, `NextFloat2()`, `NextFloat2(min, max)`, `NextFloat3()`, `NextFloat3(min, max)`, `NextFloat2Direction()`, `NextFloat3Direction()` and `NextQuaternionRotation()`. Ranges include their start and exclude their end.
  - Where Tide's differ from Unity's: `NextInt()` is from 0 to 2147483647, as `Math.Hash` is, where Unity's can be negative; a range with nothing in it gives its start (`NextInt(0)` is 0, `NextFloat(2, 1)` is 2); `NextFloat(min, max)` never gives `max`, where Unity's can by rounding; and any state works, where Unity's refuses 0.
  - Each call steps the generator once for each number it gives, whatever its arguments.
- **Each method changes the generator**, so it's called on one the code can change, by the rules of a struct's `mut` methods: a `mut` parameter's field, a `mut var`, a field of `this` in a `mut` method. Through a read-only parameter it's an error that offers `mut`. A list's element is a copy: take it out, roll, put it back. So a system that rolls says `mut` for the component or singleton the generator is in, and the schedule follows from that as for any write: no new rule. A roll is a call that writes, so it keeps its turn in an expression (see Evaluation order): in `float2(random.NextFloat(), random.NextFloat())`, x is rolled first.
- **`Random(seed)`** makes a generator for a seed, an `int` or a `long`. It's a constant expression when its seed is, so it's a field's default: `Random fixed = Random(42);`. Seeds are mixed, so neighbours (1, 2, 3) give unrelated sequences, and no seed gives state 0. `Random { state = n }` sets the state as it is.
- **The generator** is PCG32 (PCG-XSH-RR 64/32), which gives the 32 bits of the state it stepped to. Ints in a range are those 32 bits scaled down (multiply and shift), floats 24 bits of them over 2^24, and the directions go through `Math.Sin` and `Math.Cos`. Everything is integer arithmetic or exact float operations, so it's the same on every platform (`tide/random.h`).
- **A state of 0 is a generator nothing seeded.** It rolls like any other, but the same numbers as every other one at 0.
- **The engine seeds what the game left at 0**, in components and singletons, where they're fields or in a struct that's a field (not in lists, maps or grids):
  - A component's, as its entity spawns, and as the component is added: from the match's seed, the tick, the entity's handle and which field it is. The seeding happens as the spawn or the add applies, at the end of the tick, when a handle a split system gave is final. So a tick run again after a rollback seeds the same, twins spawned in one tick differ, and a component added twice rolls differently each time.
  - A match singleton's, as the match starts, from the match's seed and which field it is.
  - In the local world there's no seed: a local component's is seeded from its entity's handle, a local singleton's from which field it is, and a `local Random` field of a match component from the match entity's handle. They're the same every run.
  - A generator code sets to 0 later isn't seeded again.
- **The match's seed** (`Random.seed`) is an `int` from 0 to 2147483647. The server picks it as it starts a match, from the system's random number generator, which its host gives it (`tide_session_desc.entropy`; the standard host passes `tide_platform_entropy`, but in a run with no window: `--capture`, `--frames`, `--script`). `--seed <n>` on the standard host's command line (`tide run --seed 7`) picks the seed instead, from 0 to 2147483647: every match that machine starts in the run has it (`tide_session_desc.seed`), with a window or without, on every platform. The owner asked for the flag on 2026-10-05; that it's every match of the run, and for runs with a window too, is how it was built. It's kept in the world: a joiner gets it with the world, snapshots, the hash, host migration and hot reload carry it, and clients never send it. A player who starts the match itself to have something to build the server's world from (`tide_game.pure_start`) is told the seed with how the match started. Where no host gives entropy (`tide_world_init`, tests, benchmarks, the standard host's runs with no window) it's 0, so a run repeats exactly.
- **A game that uses none of it is what it was.** The world only has the seed in a game that reads it, rolls with `Random` in the match's code, or has a `Random` in the match's components or singletons, and only such a game's header has `tide_world_start_seeded(w, dt, start, seed)` and includes `tide/random.h`. Other games' worlds, hashes, headers and traffic don't change. A game that only rolls in local state has no seed in its match either.
- **Hot reload**: a `Random` field is a struct with a `long`, so its state carries over by name like any field. One a reload adds starts at 0 on the entities that are there already, like any new field at its default: they roll alike until they're spawned again.
- **Every player can read the seed** until scenes hide state from players (see Scenes): it's in the world, which every machine has whole.

### Open

- Whether the engine should also offer the built-in component the owner leaned to, beside the value (`mut Random random` in a system, for games that want one sequence an entity).
- On the GPU: a way to hold a frag's rolls still across frames (today that's `Math.Hash` of `this.position`); rolls in one expression in a set order on OpenGL, which would take hoisting them into statements as the CPU's code does; a per-draw number, so two draws that don't join roll apart where they overlap, at the cost of joining; and a `Random` value there, which would need a 32-bit generator of its own type.
- Whether local code's rolls should repeat in a run with a window too, on request.
- A seed the game picks for a match, like a world's seed a player types: `Session.Start(scene, seed)`. Today the game keeps it in a field of its own and seeds its generators with `Random(seed)`.
- What seeds a generator a hot reload adds to entities that exist.
- Vectors of ints (`NextInt2`...), a `long`, and picking from a list or shuffling one.

## Constants

### Decided

- `const` declares a constant at the top level of a file: `const int STARTING_LIVES = 3;`. The type is written out, as a field's is, and the value is a constant expression, by the rules of field defaults, which can name other constants.
- Constants are named in FULL_CASE, like `Math.PI`.
- A file's namespace is its constants' too: `Combat.CRIT_MULTIPLIER` from outside it, or `CRIT_MULTIPLIER` with `using Combat;`.
- Constants are part of the build, so every machine has the same values. Any code reads them (match code, local code, views and `Sample`), and reading one makes no system wait, as reading `Math.PI` doesn't.
- They go wherever a constant does: field defaults, the bounds of `[Clamp]`, `[Min]` and `[Max]`, settings, and other constants.
- Changing one under `tide run` reloads the game and keeps the match where it is: constants aren't part of its data layout.
- A game's own values are constants. `settings` only holds the engine's (see Settings).

```csharp
const int MAX_HEALTH = 100;
const float REGEN_PER_SECOND = MAX_HEALTH / 20.0;

component Health
{
    float value = MAX_HEALTH;
}

system Regenerate(mut Health health, Time time)
{
    health.value = Math.Min(health.value + REGEN_PER_SECOND * time.dt, MAX_HEALTH);
}
```

### Provisional

Implemented, awaiting approval:

- `const` is a keyword only at the start of a top-level declaration. One inside code, or inside a type, is an error that points to the top of the file; a local is read-only already.
- A constant is a number, vector, matrix, quaternion, `bool`, `Color`, `Rect`, `PlayerID`, text, struct or enum: not a list, nor a struct that holds one, yet, and not an entity, which could only ever be the null one.
- `case` labels and enum members' values can use int constants and operators on them (`case MAX_LEVEL + 1:`). The compiler works them out as they would be at run time: wrapping on overflow, and 0 for a division by zero.
- Constants can name each other in any order, across files, but not in a circle.
- A local, parameter or field in scope hides a constant of the same name, and a constant's name can't be another declaration's in its namespace.
- In generated C, a constant is its value, written wherever it's read: it has no C name, and nothing of it is in the header.

## Events

### Decided

- An event is something that happened, sent from one part of the game to the rest: a hit, a pickup, a round ending. `event Hit { ... }` declares one, with fields like a struct's, which carry its context.
- `event(Hit hit) TakeHit(...)` declares a handler: code that runs when a `Hit` is sent. The same keyword declares both, and `event(` starts a handler. The first parentheses hold the trigger, exactly one event. The second list takes the same parameters as a system, except query parameters, which only a local handler takes (see Read-only entity queries).
- An event with no fields needs no name in the trigger: `event(Spawned) Arm(...)`.
- `entity.Send(Hit { ... })` sends an event to an entity. `Send(RoundOver { ... })` sends it to the whole world.
- A handler's components and `this` come from the entity the event was sent to, as a system's come from the entity it runs for. If that entity doesn't match the handler's parameters, the handler doesn't run for it.
- A handler that takes components needs that entity, so every `Send` of its event must name one. The compiler checks every `Send`: `Send(Hit { ... })` is an error when a `Hit` handler needs an entity, and says to write `entity.Send(...)`.
- A handler that takes no components runs once per event, whether it was sent to an entity or not, and has no `this`.
- The sender isn't recorded. When handlers need it, it goes in a field: the entity that sends is often not the one that matters, like a bomb sending a `Hit` for whoever threw it.
- Events are handled at the end of the tick, with structural changes, in the order they were all recorded. A `Send` before a `Destroy` of the same entity is handled while the entity still exists. An event sent to an entity that's gone by its turn is dropped, as `Add` on a destroyed entity does nothing.
- Handlers can send events and change entities in turn. Those are handled next, until nothing is left, so everything settles within the tick. Systems later in the same tick don't see an event's effects yet, as with `Spawn`.
- The handlers of one event run in declaration order, and `[Before]` and `[After]` order them as they do systems.
- Handlers can't draw. Drawing happens every frame, in views.
- Events waiting to be handled are world state: a snapshot holds them, and re-simulating a tick sends and handles them again the same way.
- **Built-in events** are declared like any other, and cost nothing where no handler takes them:
  - `Spawned` and `Destroyed` are sent to an entity when it's spawned or destroyed. `Destroyed` handlers run while its components can still be read.
  - `PlayerJoined` and `PlayerLeft` are sent to the world when a player joins or leaves, with the player's `PlayerID` in `player`. The server picks the tick, so every machine handles them on the same one.
  - `Forgotten` is a local one: sent on this machine as an entity's local fields go, for local handlers to let go of what they hold (see Local state).

```csharp
event Hit
{
    Entity attacker;
    int damage;
}

system Explode(Bomb bomb)
{
    if (bomb.timer > 0) return;
    bomb.target.Send(Hit { attacker = bomb.owner, damage = 50 });
    this.Destroy();
}

// Health and this come from the entity the Hit was sent to.
event(Hit hit) TakeHit(mut Health health)
{
    health.value -= hit.damage;
    if (health.value <= 0) this.Destroy();
}

// Takes nothing from the entity, so it runs once per Hit.
event(Hit hit) CountHits(mut Stats stats)
{
    stats.hits += 1;
}

// Spawned has no fields, so it needs no name.
event(Spawned) Arm(with Player)
{
    Spawn(Weapon { owner = this });
}
```

- **Local handlers** hear the match's events on each machine, to show what happened: `local event(Hit hit) ShowHit(Body body, mut Fighter fighter, mut Fx fx)`. It's `local` and the same `event(...)`, with no keyword of its own.
  - It takes the event; the components of the entity the event was sent to, to read, where `mut` reaches only their local fields, as in a view; the match's singletons, to read; local singletons, which it changes; and queries, whose rows are the match as the tick that sent the event left it (see Read-only entity queries). One that takes no components runs once per event, as a match handler does.
  - It runs at the start of the frame, before the views, for each tick this machine simulated since the last frame, in order. It changes local state and can't draw, like every handler.
  - It sees what it takes of the match as the event's turn left it, even when the entity is gone by the frame: that's what makes `Destroyed` say where something ended. Only what handlers take is kept.
  - The engine's own events have them too: `Spawned`, `Destroyed`, `PlayerJoined` and `PlayerLeft`.
  - **A machine hears `Spawned` once for every entity of the match it comes to have** (agreed 2026-10-07): in the tick that spawns it, or, for an entity that's in a world the machine is sent (the match as it started, the match a player joins, a world sent again), when that world arrives. So a `local event(Spawned)` handler is where a machine sets up what it keeps for an entity, like its local fields, however it learned of the entity.
  - **Once, under rollback.** Each tick remembers the events it delivered locally. When a tick is run again, the same event (the same tick, event, entity and fields) stays silent; one only the corrected tick has is heard then, late; one the correction removed isn't taken back.
  - **Events only local code handles.** The match can send an event no match handler takes, purely for what's shown, like `Impact { float2 at; float2 normal; }`. It costs the match nothing: the world's bytes, hash, snapshots and what's sent are what they'd be without it, and a machine with nobody to show it to does no work for it.
  - **`verified`** before the event, `local event(verified Died died) ShowDeath(...)`, hears it once the server has confirmed the tick that sent it: late by the prediction's lead, never wrong, never taken back. What the handler takes of the match comes from that confirmed tick, as the event's turn left it, so nothing else is marked. A predicted handler and a verified one of the same event can both exist. See Views and drawing for `verified` in views.

```csharp
// The match sends Hit to the fighter, and handles it; each machine shows it.
local event(Hit hit) ShowHit(Body body, mut Fighter fighter, mut Fx fx)
{
    fighter.flash = 1;   // A local field
    Burst(fx, body.position, hit.impulse);
}

// Where a shot ended: its components as they were when it went
local event(Destroyed) ShotEnded(Body body, Bullet bullet, mut Fx fx) { ... }

// Nothing in the match handles Impact: it's sent for whoever shows it
event Impact
{
    float2 at;
    float2 normal;
}

local event(Impact impact) Spark(mut Fx fx) { ... }

// The spark as it's predicted, the death once it's certain
local event(verified Died died) ShowDeath(Body body, mut Fx fx) { ... }
```

### Provisional

- **Local handlers of the match's events**, beyond what's decided:
  - **The same event** is one of the same tick, event and fields, sent to the same entity. An entity is the same by what it was born as (see Local state, an entity after a rollback), where a machine knows that of both: a handle a replay gave to another entity is another entity's, so an event to it is heard, and the same entity on another handle isn't heard twice. Otherwise it's the same by its handle. Two alike in one tick are two: each is heard once.
  - `Spawned` and `Destroyed` come from both worlds. A local handler of one hears the match's when it takes something of the match (a component, a filter or a singleton) or is `verified`; taking only local state, it's the local world's, as before. One that takes a local component and something of the match is an error.
  - What it takes of the match is a copy from the event's turn: after the match's handlers of that event ran, and for `Destroyed`, before the entity goes. `Time` is the tick the event was sent in. An event sent to an entity that's gone by its turn isn't heard, as it isn't handled.
  - **Text, lists, maps, sets and grids of the match aren't heard yet:** naming one, in the event or in a component or singleton the handler takes, is an error that says so, a copy of the value has them empty, and they don't count towards two events being the same. Local state has them as ever.
  - It can't take the `World` (it's the match as it is now, not as the event left it), `Devices`, the input, or `with ownership` (it takes `Owner owner` and `Session session`, and compares). It can't be `async` yet: it starts a task with what the task needs.
  - Local fields are the entity's, whether or not it's still there: what a handler writes for an entity that's gone goes with the rest at that frame (below), `Forgotten` first.
  - A frame runs them first: then the local fields of entities that are gone go, then the views run. What they spawn, change and send locally is handled at the end of the frame, like a view's. They send commands as views do.
  - An event's local handlers run in declaration order, and `[Before]` and `[After]` order them among themselves, apart from the match's handlers of the event. An event has 64 at most.
  - A machine hears what's sent from the tick it joins in: its own `PlayerJoined` and every later one. Ticks a world sent again skips (a client that went wrong) say nothing. A match that changes hands goes on from the new host's last confirmed tick, so the ticks after it are heard again on a machine that had heard them. Nothing is made up for what happened before a machine had the world, but for `Spawned` (Decided, above), in these details:
    - An entity that arrives is heard when its world does, at the start of the next frame, before anything a later tick sent. The handler sees the entity's components and the match's singletons as that world has them, not as the spawn left them, `Time` is that world's tick, and a query's rows are that world's. Both kinds of handler run, as the world is the server's.
    - Among the entities of one world, the order is the world's own storage's, not the order they were spawned in.
    - An entity arrives as it is: one that came to a component by an `Add` is heard by a handler that takes that component, which a tick's `Spawned` never is.
    - Which entities a machine had is told by their handles. For the first world of a match, none, but for a match that changed hands or a web build that was reloaded, whose last world's entities aren't new. For a world sent again: for the handlers that hear ticks as this machine runs them, those of its own world at that tick; for `verified` handlers, those of the last world the server had confirmed. A predicted spawn that took the handle the server gave another entity hides that entity's arrival.
    - Local fields a machine kept under an entity's handle are still there when a handler hears it arrive: a world that came as bytes doesn't say what its entities were born as, so it can't tell another entity on the same handle.
    - `Destroyed` isn't sent for what a world sent again no longer has, and no other event is made up.
  - A handler of an event nothing sends is a warning, like a match handler's, and `tidec --schedule` lists each event's local handlers after the match's.
- `event` is only a keyword at the start of a declaration, like `input`.
- A trigger's name is optional for any event: a handler that doesn't read the event leaves it out.
- `Spawned` handlers run as the spawn is applied, and `Destroyed` handlers just before the entity goes, both in the queue's order. A spawn's `Spawned` handlers run before the next change in the queue.
- Structural changes and events share one queue per tick, which grows as it needs. What handlers record goes on its end. A chain of events that never ends (handlers that set each other off) stops the program once it's 100,000 deep in one tick, each event sent or entity spawned by a handler of the one before, with a message naming the event. `TIDE_MAX_CHAIN` sets the depth for a game that needs deeper chains.
- Events can be locals (`var h = hit;`) and can be sent on (`other.Send(hit)`), but they can't be fields, function parameters or system parameters.
- `[Before]` and `[After]` only order handlers of the same event. Handlers and systems are ordered separately.
- Games can't send the built-in events. The host sends `PlayerJoined` and `PlayerLeft` with `tide_world_player_joined` and `tide_world_player_left`: they're handled at the end of the next tick, before anything that tick sends. `tide/run.h` has player 0 join before the first tick.
- A handler of an event nothing sends is a warning, and so is declared access a handler doesn't use, as for systems.
- `tidec --schedule` lists each event's handlers in the order they run.

### Open

- Events for a component being added or removed.
- Handling a world event for every matching entity, such as resetting every player on `RoundOver`. It waits for queries in the match's event handlers, command handlers and tasks, which only systems, views and local handlers take so far.
- Local handlers: text and lists of the match in what they hear; the `World` as an event left it; async ones; reading `verified` components in a handler that hears a tick as it's predicted; an event's local handler that follows the same entity when a replay gives it another handle on a machine that doesn't know what it was born as.

## Commands

### Decided

- `command Name { ... }` declares a typed player action entering the match. Commands hold plain scalar values, vectors, enums, entity and player IDs, strings, `List<byte>` (a file's bytes, for user content: see Assets), and structs of those. Other lists, grids, components and device records are not command payloads.
- Local views and handlers send commands with `entity.Send(value)` or `Send(value)`, while connected to a match. The compiler distinguishes commands from events by their declaration. An entity target is required when the command handler takes components.
- Each command has one void handler: `command(Rename rename, PlayerID sender) RenamePlayer(Owner owner, mut Name name)`. The first parentheses contain the read-only payload and connection-derived sender. The second take the same match state as event handlers. Components and `this` come from the target.
- Guard clauses return normally. Match writes take effect normally. There is no acceptance operation, boolean result or transaction. The handler checks permissions and game rules, and can send events after acting.
- Sending copies the packed payload into a bounded local queue. Commands are independent of input ticks; repeated packets are identified by a per-player sequence. The server selects their tick and order, and handlers run in the existing deferred queue at tick end. Snapshots, hashes and migration include processed sequences, and replay executes recorded commands again in the same order.
- Initial limits are 1024 packed bytes per command, 32 pending per player and 16 commands scheduled per authoritative tick. Wire structure, UTF-8, finite values, enum values and declared bounds are checked before the match reads the payload. The handler remains responsible for authorization and game-specific rules.
- A command's ordering is its type's, per player (agreed 2026-10-06). Without an attribute a command is in the default channel: handled in the order sent, after every earlier command of that channel. `[Channel(Channels.Uploads)]`, with an enum of the game's, puts it in a channel of its own, ordered among that channel's commands and against nothing else, so a chat line never waits behind an upload. `[Unordered]` gives it no order at all: the server handles it in whichever tick it arrives, before or after commands sent earlier, still exactly once. Whatever the ordering, the server picks the tick and every handler runs in the tick's end queue in the server's order, so ordering only decides what the server holds back waiting for an earlier command.
- A command holds `List<byte>` too, so a player's file goes to the match as it is. One that doesn't fit a datagram goes as a numbered transfer in chunks, the way worlds go, paced under the input traffic so a transfer never delays a tick. It lands in a later tick, as any command may; a command was never tied to the tick it was sent in. What goes through the match goes player to server to everyone, so a host sends it once per player: a few megabytes is fine, and big media is a URL in the match, fetched by each machine (see Assets).

```csharp
enum Channels { Uploads }

command Rename { string name; }

[Channel(Channels.Uploads)]
command SetBackground { List<byte> png; }

[Unordered]
command Ping { int at; }


command(Rename rename, PlayerID sender) RenamePlayer(
    Owner owner, mut Name name)
{
    if (sender != owner.player) return;
    if (rename.name.length == 0 || rename.name.length > 32) return;
    name.value = rename.name;
}
```

### Provisional

Built 2026-10-06; the picks below await the owner.

- Sequence state is per player per channel, in the world as today's is: a channel's sequence counts that channel's commands alone, from 1. An unordered command has an id of its own, from 1, in place of a sequence, and the world keeps a window of the last 64 ids applied per player (the highest, and a bit for each of the 63 below it) to drop resends: an id older than the window is taken as applied. The 32 pending per player is per channel, the unordered commands counting as a channel of their own; 16 scheduled per tick stays one limit for all.
- A channel is an enum member of the game's, named by `[Channel(Enum.Member)]` on a `command` declaration; commands can share one, members of any enum can be channels, and a game has 63 channels of its own at most beside the default. `[Unordered]` takes nothing. Both on one command, either on anything but a command, and a name that isn't an enum member are errors. Whatever the ordering, each handler runs where it always did: in the tick's end queue, in the server's order.
- A command holds `List<byte>` as a field, or in a struct it holds, and no other list: `List<int>` stays an error. One with no `List<byte>` is still 1024 packed bytes at most. One with a `List<byte>` is 4 MiB at most (`TIDE_COMMAND_LIMIT`), its bytes on the wire whole, after a count. Over 1024 bytes it goes in chunks of 1000, 16 chunks an update each way (about a megabyte a second at 60 updates a second), each sent again once a round trip and a half has passed without it being acknowledged, as a world's chunks are. A machine sends one big command at a time, in the order they were sent; the server takes one at a time from each player.
- A big command lands in a tick only once every player in the match has its bytes: the server sends them down to each player first, paced the same way under the frames, and the tick's frame names the transfer rather than carrying the bytes, so no player's tick waits for one. A player being sent the world, or catching up with one, doesn't hold it up: that player gets the transfer with the ticks it lacks. A player who joins with a world from a later tick needs nothing of it.
- The local state's outbox (what views sent and the session hasn't taken yet) is 32 a channel too, and a channel that's full at the session holds back only its own: the host hands each channel's commands over in turn.

## Enums and switch

### Decided

- Enums and `switch` follow C#'s syntax.

```csharp
enum Phase
{
    Warmup,
    Playing = 5,
    Over,
}

system Advance(mut Match match)
{
    switch (match.phase)
    {
        case Phase.Warmup:
            match.phase = Phase.Playing;
            break;
        case Phase.Playing:
        case Phase.Over:
            break;
    }
}
```

### Provisional

- `enum Name { A, B = 5, C }` declares an enum. A member without a value is one more than the one before it, and the first is 0. A value is an int known while compiling: a literal, an int constant, or operators on them, worked out as they would be at run time. A comma after the last member is fine. `enum` is only a keyword at the start of a declaration.
- An enum is stored as an int, or as what it says after its name, as in C#: `enum Voxel : byte { ... }` takes one byte (0 to 255) and `: ushort` two (0 to 65535), for grids' cells and other data there's a lot of. A member out of that range is an error that gives the range. Anything else after the colon is an error that lists the three. Only the size changes: a value's number is still `int(...)`, and the value with a number `Voxel(...)`, which keeps the number's low bits (below).
- Members are always written with their enum: `Phase.Playing`, or `Game.Phase.Playing` from another namespace. In generated C, `Phase.Playing` is `Phase_Playing`, a constant of the type `Phase`: an `int32_t`, or a `uint8_t` or `uint16_t` for `: byte` and `: ushort`.
- Enums are values, like structs: fields, locals, inputs, and functions' parameters and return values can hold them. A field without a default starts at 0, even if no member has that value, as in C#.
- `==` and `!=` compare two values of the same enum.
- An enum and its number convert both ways, and neither way by itself: `int(phase)` (or `long(phase)`) is a value's number, and `Phase(5)` the value with that number, as `float3(i)` mirrors `int3(f)`. The enum's name is all there is to it: no keyword, and no attribute. An enum in a namespace converts by its qualified name too, `Combat.Stance(2)`.
- `Phase(...)` takes one int, or a byte, which is an int wherever one goes. A float or a long converts with `int(...)` first, and so does another enum's value: each is an error that says so. A mismatch the other way (`Phase p = 1;`, `int n = phase;`) names the conversion to write.
- As C#'s cast does, `Phase(n)` keeps a number none of the members has: the value is that number, and `int(...)` gives it back. Nothing checks it, so the conversion costs nothing. The rest of the language takes such a value as it takes a field that started at 0 with no member there: it equals no member, a `switch` on it runs its `default` (or no section), text shows its number, an input or a command that carries one from another machine gets the field's default instead (below, and Commands), and a hot reload that changes the layout carries enums by their member's name, so it goes back to the field's default. Code that needs a member bounds the number first (`Card(hash % CARDS)`), or handles the rest in a `default`.
- An enum stored as a byte or a ushort keeps the number's low 8 or 16 bits, as `byte(x)` keeps an int's low 8: `Voxel(300)` is `Voxel(44)`, and `Voxel(-1)` is `Voxel(255)`.
- With a constant number it's a constant: `const Card FIRST = Card(0);`, a field's default, a `case` label.
- It works in a frag or vert too, where an enum is an int whatever it's stored as: the number itself, cut to its low bits for a byte's or a ushort's enum, so the GPU and the CPU agree.
- An enum in an input that isn't one of its members, which only a bad client could send, becomes the field's default before `Sanitize`, like a NaN float.
- `switch` works on ints, longs, chars and enums. A case is an int known while compiling (a literal, an int constant, or operators on them), a char known the same way (see Text), or one of the enum's members, maybe through a constant of the enum. Labels in a row share a section, `default` handles the rest, and each value appears once.
- Every section ends with `break;` or `return;` on every path, so none runs into the next, as in C#. Outside a switch, `break` ends a loop (see Loops); anywhere else it's an error.
- Each section has its own scope for locals.
- A function returns on every path when a switch with a `default` returns in every section.
- `switch`, `case`, `default` and `break` are keywords.

### Open

- Ordering enums (`<`).
- A warning when a switch on an enum has no `default` and misses a member.

## Loops

### Decided

- `for`, `foreach` and `while`, with `break` and `continue`, as in C#.

### Provisional

- `for (var i = 0; i < n; i++) { ... }`: its start, condition and step are each optional. The variable it declares changes in its step, and is read-only in its body unless it's declared `mut var`.
- `foreach (var item in list) { ... }` goes through a list's elements in order; `foreach (Type item in list)` names their type. Each element is a copy, read-only. The list's `count` is read each round, so elements added along the way are reached too. A foreach over a grid goes through its cells' places, and `parallel` through them at once (see Grids); `parallel` goes through a list's elements at once too, by index (see Lists). A foreach over text goes through its characters (see Text).
- `i++`, `i--`, `++i` and `--i` are statements, on ints and floats, the same as `i += 1` and `i -= 1`.
- They're expressions too, as in C# (the owner, 2026-10-08, of `(++some.count % 2 == 0) ? logo : test`, which was a syntax error: "i feel like this should be possible?"; the rules are Claude's picks): `++i` and `--i` are the variable's value once it changed, `i++` and `i--` what it held before. They go on what an assignment's target can be (a local, a field, a list's or an array's element, a map's value), of an int, a long, a byte or a float, and are checked as that assignment is: `mut` where it's needed, a view never the match. Each is a write in its turn (see Evaluation order): `i + ++i` adds what `i` was to what it is then, and `Pack(i++, i++)` gets the two values in order. On the right of `&&` and `||` and on the sides of `?:` one only runs when its part does. In a `parallel` loop's step one changes what an assignment there can: the step's own variables, its own cell, its own element. In code the GPU runs (which has no `++` as a value in WGSL, and no promised order between operands in GLSL) each runs before its statement, into a variable, so its value is the CPU's; for that to hold, the statement names the variable nowhere else, and the `++` isn't on a side of `?:` or the right of `&&` or `||`, nor inside an expression on a loop's own line (a for's or a while's): each an error that says what to write. Not built: `x = y = 0`, as assignments are still statements.
- `break` ends the innermost loop or switch; `continue` goes on to the innermost loop's next round, from inside a switch too. In a block after a call, both are the caller's: they end or continue the caller's loop, even if the function runs the block inside a loop of its own.
- `Spawn`, `Scene.Load` and widgets can't be in a loop's condition or a for's step, which run again and again; they go in its body.
- `while (true)` and `for (;;)` with no `break` of their own never end, so nothing needs to follow them: a function can end with one, and so can a switch's section.
- `in` is only a keyword in a foreach or a parallel loop, and `by` and `offset` only in a parallel loop; `while`, `for`, `foreach`, `parallel` and `continue` are keywords everywhere.
- There's no `do ... while` yet.

## Text

### Decided

- `string` is text, as a value: assigning copies it, and nothing is ever shared.
- A string's `length` counts characters (Unicode code points), not bytes.
- Components, singletons, structs and events can hold text. A world keeps it in its heap, part of the world, so snapshots have it too. The heap grows as it needs, up to 1 GiB (see Limits).
- Nothing about text fails: past the end of a string, positions are clamped. Text that code makes can be as long as the scratch area it lives in has room for: the area grows as it needs, up to 1 GiB a thread (see Limits).

### Provisional

- Text is UTF-8. Literals can hold any UTF-8 character, and `\"`, `\\` and `\n`.
- `$"score {score}"` puts values in text. After a value, a colon and a format, as in C#: `{x:F2}` for two decimals, `{n:D3}` for at least three digits (`007`), `{n:X}` for hex. Floats, vectors, quaternions, colors and rects take F, up to F9; ints, longs and int vectors take D (up to D32), X (up to X8, or X16 for a long) and F. The letter can be lowercase. `{{` and `}}` are braces, and `?:` in a value goes in parentheses: `{(won ? 1 : 0)}`.
- Text can show numbers, bools, enums (their member's name, or their number when no member has it), vectors and quaternions (`(1, 0.5)`), `Color` (`RGBA(1, 0, 0, 1)`), `Rect`, entities (`Entity(3:1)`, or `Entity(new)` for a temporary handle: see Entities) and players (`PlayerID(0)`). Floats are written with the fewest digits that read back as the same float, plainly from 1e-7 to 1e21 and with an exponent beyond (`1.5E+21`), the same on every platform: computed exactly, never with the platform's printf.
- Text shows values with fields (structs, components, singletons, events and inputs) as C# shows records: `Stats { hp = 3, speed = 1.5 }`, `Nothing { }`, nested ones inside. Lists show as `[1, 2, 3]`. Text in them is in quotes, `name = "Bob"`, so `""` shows; it isn't escaped. Scenes show their own fields, not the engine's. `Session` shows its `room` last. The device records (`Devices` and the rest) don't show, nor does anything holding a matrix, which text can't show yet; the error names what's in the way.
- `+` joins text with anything it can show: `"score " + score`, `1 + "st"`. `==` and `!=` compare text byte by byte. There's no `<` for text.
- `length`, and the methods `Contains`, `StartsWith`, `EndsWith`, `IndexOf` (-1 if it's not there), `Substring(start)` and `Substring(start, length)`, `ToUpper` and `ToLower` (ASCII letters only, for now), `Trim` and `Replace(from, to)`.
- Text that code makes, joining and formatting, lives in a scratch area that's cleared once the system, view or handler that made it is done, for each entity. It's only ever copied into a world's heap.
- Heap text never changes once it's written: changing a field writes new text. What's released only goes back once the code running is done, so a copy of a field made before it changed still reads the old text, with no copying.
- An input can't hold text: what players send each tick is numbers, bools, chars and enums.
- **`char` is one character of text** (the owner, 2026-10-08, of three answers to what `text[i]` gives, a one-character `string`, the code point as an `int`, or a `char` type: "C; its the non-lazy approach". That there's a `char`, and that `text[i]` gives one, is theirs; each rule below is Claude's pick, C#'s where it has one):
  - **What it is.** A Unicode code point, as a text's `length` counts them (where C#'s is a UTF-16 unit, half of some characters), in four bytes: a plain value, which goes where an int does (fields of components, singletons, structs, events and commands, arrays, lists, a grid's cells, an input's fields, locals, parameters, constants, what a function returns, a C function's parameters as an `int32_t`). Its zero, `'\0'`, is no character, and its `default`.
  - **Literals.** `'a'`: one character between single quotes, any of UTF-8's, or one of the escapes `\'`, `\"`, `\\`, `\n`, `\r`, `\t` and `\0`. The last three aren't text's (which has `\"`, `\\` and `\n`): they're what text read from a file is compared with. None between the quotes, or more than one, is an error that says which of a char and text to write.
  - **A text's characters.** `text[i]` is the one at `i`, counted as `length` counts, and `'\0'` before the start and past the end, as nothing about text fails. (`Substring(-1, 1)`, whose start is clamped, gives the first character; an index names one place, so there's nothing to clamp it to.) It only reads: `name[0] = 'x'`, `name[0]++` and a `mut` or `out` argument of one are errors that say to make the new text of its pieces. `foreach (var c in text)` goes through them in order, of the text as the loop found it, whatever its variable becomes meanwhile (a list's foreach reads its count each round; text is a value that never changes in place). `foreach (char c in text)` names the type.
  - **Comparing.** `==`, `!=`, `<`, `<=`, `>` and `>=`, by code point. Never with text: `c == "a"` is an error that says to write `'a'`.
  - **To an operator, a char is its code point, an int** (C#'s rule): `c - '0'`, `c + 1`, `c == 65`, `'a' * 2`, `-c`. What comes of one is an int, never a char. A char converts to an `int`, a `long` or a `float` by itself where one goes (a variable, a field, an argument, an element, an index, `Math`'s functions), and `int(c)`, `long(c)` and `float(c)` say so. The other way is written: `char(n)` is the character with code point `n`, an int, and `'\0'` for a number none has (under 0, past `0x10FFFF`, or from `0xD800` to `0xDFFF`, the halves of UTF-16's pairs), so a char is always a character or none. No `++`, `--` or `+=` on one: `c = char(c + 1)`. Vector constructors take none.
  - **`switch`** works on a char, its cases chars: literals, char constants, `char(n)` of a constant. An int's or a long's switch takes chars as cases too, as their code points; a char's takes no int (`case char(65):`).
  - **Keys.** A map's and a set's key can be a char.
  - **In text.** `+` joins one as its character, and `$"{c}"` shows it; `'\0'` adds nothing, so what's read past a text's end joins as nothing. It takes no format (`{int(c):X4}` shows its number). Inside a value, text shows it in its quotes, as it shows text in its own: `Glyph { mark = 'a' }`, `['a', 'b']`, and `'\0'` for the one that's none.
  - **Text's members take one**: `Contains(c)`, `StartsWith(c)`, `EndsWith(c)`, `IndexOf(c)` and `Replace(from, to)`, where replacing with `'\0'` takes the character out. A call with text picks the text one.
  - **An input and a command carry one in 21 bits.** A number no character has, which only another machine could send, becomes the field's default before `Sanitize`, as an enum that's none of its members does.
  - **On the GPU** a char is an int, its code point: fields, arrays, lists, a map's keys, literals, `switch`, comparisons and `char(n)` work there. Text itself stays the CPU's.
  - **Bytes that are no character's.** Text from C or a file can hold bytes that aren't UTF-8. A character is a byte that starts one and the bytes that continue it, which is how `length` counts, and one whose bytes are no character's (too few or too many, a longer form than the shortest, half of a UTF-16 pair) reads as U+FFFD, the replacement character, the same on every machine.
  - **What it costs.** `foreach` reads each character once, on any text. `text[i]` is as quick for text of a byte a character, which a text's two counts (its bytes and its characters) say; for any other it counts its way from the start, so a `for` over a long one with `text[i]` is slow, and `foreach` is the loop to write. `Substring` gives a piece where its text is, with nothing copied.
- `GUILayout.TextField(label, mut text)` and `GUI.TextField(rect, label, mut text)`: a field the player types into, which changes the text as they type. Enter or Escape stop typing, and Backspace takes off the last character. With `out bool entered` after the text, it also says whether Enter ended the typing (see GUI).
- Systems that change the match's text or lists wait for each other, as their heap is one: `tidec --schedule` says "both change text or lists".

### Open

- What a char is, as members: `IsDigit`, `IsLetter` and the like, and its case. For now `c >= '0' && c <= '9'`, and `ToUpper` and `ToLower` are text's.
- `++`, `--` and `+=` on a char, and `\r`, `\t` and `\0` in text's literals, which a char's have.
- Text with a caret that moves, and selection, in text fields.
- Case for letters past ASCII.

## Generics

### Decided

- Generics are a proper part of Tide, in three steps (the owner, 2026-10-06: "we clearly need generics and I don't like making them just an engine thing that you can't touch"): a type is read as a tree, containers nest by one rule (both built: see Lists, and `AGENTS.md`, Language), and games declare generic structs and functions of their own, below.

### Open

A proposal for the third step (Claude, 2026-10-06), for the owner to approve before it's built. Each point is a pick unless it says otherwise.

- **Declaring.** A struct, a function or a method takes type parameters in `<>` after its name, as C#'s do:

```csharp
struct Pair<T>
{
    T a;
    T b;
}

T Max<T>(T a, T b)
{
    return a > b ? a : b;
}

struct Stack<T>
{
    List<T> items;

    mut void Push(T item) { items.Add(item); }

    T? Pop()
    {
        if (items.count == 0) return null;
        var top = items[items.count - 1];
        items.RemoveAt(items.count - 1);
        return top;
    }
}
```

  A method of a generic struct uses its struct's parameters, and can have parameters of its own, `U Map<U>(...)`. Not components, singletons, events, commands, inputs, scenes, systems, views or handlers: what the engine stores by name and schedules is concrete (a `component Slot<T>` would be an archetype for each instantiation, and a system over it a system for each: left for when a game asks). Not enums. A type parameter is a name; it shadows nothing, so a `T` that's also a type of the game's is an error at the declaration.
- **Using.** `Pair<int> p;`, `Stack<Entity> enemies;`, `Max<float>(a, b)`, or `Max(a, b)` with the type arguments inferred (below). A type argument is what a list's element can be, the same rule in the same table (`GENERIC_ELEMENT`): built-in types, text, enums, structs (generic ones instantiated, `Pair<Pair<int>>`), lists, maps and sets. Not grids, ECS data, `Action`, `Devices`, `World`, nor a `T?`. A generic struct holds what a struct holds, so no grid.
- **Monomorphization.** Each instantiation is a declaration of its own, as `List<int>` is one today: `Pair<int>` and `Pair<float>` are two structs in generated C, each checked once with its arguments bound, and two instantiations with the same arguments are one declaration, found by them. Its C name is the generic's with the arguments' C names after it, `Pair_int`, `Pair_List_Entity`, `tide_function_Max_float` (a name that reads in a debugger, over a number like `tide_list3`), and tools show it as written, `Pair<int>`, `Max<float>`: the schedule, the profiler, hovers. The game's layout has an instantiation as a struct of its name, so hot reloading carries a world over field by field as for any struct, and renaming a type parameter changes nothing.
- **No constraints in v1.** A generic body uses a `T` for anything, and what the `T` doesn't support is found at the instantiation: `Max<Pair<int>>` is an error at `a > b` in `Max`, with a note at the instantiation that led there ("instantiated as `Max<Pair<int>>` here", through a chain of them where one generic instantiates another), as Tide's errors say what and where. What doesn't depend on `T` (names, syntax, the shape of calls, what a `T` is passed to as a plain value) is checked once with the parameters unbound, so an error that's the generic's own is found with no instantiation at all. Constraints (`where T : ...`) can come later, as a check at the declaration; Tide has nothing to constrain with yet but the built-in rules (`==`, `<`, hashable), which would be the first.
- **Inference**, as C#'s: a call with no `<>` takes each type argument from the arguments whose parameters mention it, `T` against the argument's type and `List<T>` or `Pair<T, U>` against its parts; two arguments that give the same `T` have to give it exactly (`Max(1, 2.5)` is an error that says to write `Max<float>(1, 2.5)`: no widening picks between `int` and `float`); a `T` no argument gives is an error that says to write the `<>`; the return type takes no part. Defaults and named arguments go as for any call. A generic and a non-generic function can share a name: the non-generic wins where it fits, as in C#, and two generics that differ in how many type parameters they take are two overloads. A generic can't be `virtual` or overridden in v1, which keeps `override` matching by name and parameters alone.
- **The GPU.** A generic function a frag or a vert reaches is instantiated like any function and written per instantiation, with its C name; a generic struct as a vertex or an instance struct is instantiated the same way. What the GPU refuses of a type (text, lists, entities) it refuses of a type argument, where it's used.
- **Text** shows an instantiation's value with its written name: `Pair<int> { a = 1, b = 2 }`.
- **Editors** (tidels): hover on a generic shows its declaration with `T`, on an instantiation the arguments bound; go to definition goes to the generic from any instantiation; rename renames the generic and every instantiation; completion offers `<` after a generic's name with its parameters as placeholders, as `Map<$1, $2>` is offered; the formatter keeps `Pair<int>` tight as it keeps `List<int>`; the grammar colors a type parameter as a type.
- **Recursion.** `Pair<Pair<int>>` is fine, and `struct Node<T> { List<Node<T>> kids; }` too, as lists nest. `struct Node<T> { Node<T> next; }` isn't, as no struct holds itself. An instantiation that instantiates forever (`Grow<T>` calling `Grow<Pair<T>>`) stops at 8 levels, with an error that shows the chain.
- **Tests and docs** that come with it: `compiler/tests/e2e/generics` (structs, functions, methods, inference, nested instantiations, instantiations that are one declaration, hot reloading's layout, the GPU), `compiler/tests/errors/generic_*.tide` one per diagnostic, `compiler/tests/shaders/generics`, `lsp_generics`, the grammar's probe, and `docs/language/generics.md` with a live example.

## Arrays

### Decided

- `T[N]` is an array: `N` values of `T`, side by side, in the bytes of whatever holds it. `int[32] cells;` (the owner, 2026-10-06: fixed-length arrays, to avoid a list's indirection; `int[32]` over `Array<int, 32>`).

### Provisional

Built 2026-10-06 on that; each point beyond it is a pick, awaiting the owner.

- An array is a value, copied with its struct, and needs no heap: a component's array is lanes of the component, like its other fields, so a system's loop over rows runs with SIMD, and snapshots, hashes, deltas, the world as bytes and hot reloading see component data and nothing new. Hot reloading carries one over by its type's name, `int[32]`: another length is another type, and the field starts over.
- `N` is a constant: a literal, or a constant and operators on them (`int[CAP] cells`), from 1 to 4096; `int[4][8]` is 4 arrays of 8 (`a[i]` is an `int[8]`, `a.count` is 4), and all the lengths together stay under 4096 too. An `audio` declaration's state alone holds a longer one, a delay line, which nothing copies (Audio, A long array).
- Elements are plain values: numbers, bools, chars, enums, vectors, matrices, quaternions, `Color`, `Rect`, `Entity`, `LocalEntity`, `PlayerID`, structs of those, and arrays. Not text, lists, maps, sets or grids, which need the heap, nor ECS data or rendering resources.
- An array goes where a plain value goes: fields of components, singletons, structs, events and scenes, locals, parameters and what functions return (copies), a list's elements and a map's values (`List<int[32]>`), a grid's cells. And an input's fields, which a bounded list could never be: `[Clamp]`, `[Min]` and `[Max]` on one apply to every element, and NaN and infinite floats in one are repaired element by element. Not a map's or a set's key, and not a constant yet.
- `a[i]` reads an element, or the element type's zero past the end; `a[i] = v` and `a[i] += v` write one, and past the end do nothing, as a list's do. `a.count` is `N`. `foreach (var x in a)` goes through the elements, each a copy. `a = b` copies the whole array, and `a = [1, 2, 3]` sets it from a literal of exactly `N` values (`default` is all zeros). An element that's an array is changed where it is, `a[i][j] = v`, and so is an array that's an element of a list or a map's value, `rows[i][j] = v`, `buckets[key][j] = v`, through any number of indexes, and a plain field of an element that's a struct, `a[i].x = 1`, `rings[b].cells[j] = v` (see Lists, Changing an element where it is).
- Text shows an array as it shows a list: `[1, 2, 3]`.
- Views don't blend arrays between ticks: an array is seen as it is at the latest tick, as `[Snap]` fields are.
- On the GPU an array is a value too (Views and drawing, Lists and arrays on the GPU). Not sorted, searched or compared with `==`.

### Open

- Blending arrays of floats in views.
- Constants that are arrays.

## Lists

### Decided

- `List<T>` is a list of values, as a value: assigning or passing one copies it, and changing a copy never changes the original.
- A world keeps its lists in its heap, like text.
- Nothing about lists fails: past the end, a read gives the element type's zero and a write does nothing. A world's lists grow with its heap, and a list that code makes grows with the scratch area, each as far as it goes; a list stops growing at 1 GiB of elements (see Limits).

### Provisional

- A list starts empty. `[a, b, c]` is a list where one goes, from what it goes into: `List<int> scores = [1, 2, 3];`, `scores = [];`, a field's default, an argument. `var x = [1, 2]` is an error, as it doesn't say the type.
- `count`, `items[i]` to read, `items[i] = x` and `items[i] += x` to write, and the methods `Add(item)`, `Insert(index, item)` (the index clamped), `RemoveAt(index)`, `Clear()`, and for elements that `==` compares (numbers, bools, chars, enums, text, entities and players) `Contains(item)`, `IndexOf(item)` (-1 if it's not there) and `Remove(item)`, which returns whether it found one.
- Provisional, awaiting the owner: `Resize(count)` makes the list `count` elements long in one call, as Unity's `NativeList.Resize` does. New elements are the element type's zero: zero bytes, empty text. With fewer, the rest go, and the text they held with them. A count under 0 is 0, and one past what a list holds (see Limits) leaves the list as it is. It changes the list as `Add` and `Clear` do, with their rules: something that can change, not in a parallel loop's step or a `Sort`'s comparison, and not on the GPU. Picked beyond that: its name and its one `int` argument; no `Capacity` or `Reserve` beside it; a list in a world gives back the room it no longer needs, where `Clear` keeps its room for what's added next.
- An element is a copy, as with C#'s List of structs, when it's taken out: a local, a foreach's variable, an argument. A `mut` parameter or a mut method can't change an element in place. But a plain field of an element that's a struct is changed where it is (below, Changing an element where it is).
- Changing a list needs it to be something that can change: a `mut` component or singleton, or a `mut` local.
- Elements are values: built-in types, text, enums and structs, not ECS data (keep an `Entity` instead).
- Components, singletons, structs and events can hold lists; inputs can't. A struct that holds a list, a map or text is a value like any other: a field of a component, a singleton, an event or another struct, a local, a parameter. Copying it copies what it holds.
- A list holds lists, maps and sets as elements, and structs that hold them, at any depth: `List<List<int>>`, `List<Bag>` where `Bag` has a `List<int>`, `Map<int, List<Entity>>`, `Map<string, List<List<int>>>`. Not grids, which are never copied (but in an asset, which is never written: see Assets). A key is still a plain value (see Maps and sets), and so is a grid's cell (see Grids). One rule says what each built-in type takes, the same in the parser and the checker (`generic_forms` in `compiler/src/types.c`): a list's element and a map's value take any value but a grid; a key takes a value with a hash and `==`, of a fixed size; a cell takes a plain value; a `T?` takes any value a function can return; `fails E` takes a value that isn't a `T?`. (Provisional, built 2026-10-06 as the second of the three steps generics take: the owner decided the steps, and that containers nest, 2026-10-06; the picks here await them.)
- An element that's a container is changed where it is: `rows[i].Add(x)`, `rows[i][j] = v`, `rows[i][j] += v`, `buckets[key].Add(e)`, through any number of indexes. `buckets[key].Add(e)` adds the key when it isn't there, as `buckets[key] = ...` does. Past the end of the list it's in, nothing happens, as any write past the end. Not through a struct element's field (`bags[i].parts.Add(x)`): the struct is a copy, taken out and put back, as before. An element isn't sorted in place either: a copy is sorted and put back. (Picked: a slot in the list it's in, where C#'s `rows[i].Add(x)` works because its lists are references.)
- Changing an element where it is (Provisional, built 2026-10-06 with arrays, so that `rings[b].cells[j] = v` costs what the write costs): a field of a struct that's a list's element, a map's value or an array's element is written where it is when what's written holds nothing of the heap (`rows[i].x = 1`, `rows[i].count++`, `rings[b].cells[j] = v`), through fields and arrays' indexes after the first container's. Generated code finds the element's slot and writes into it; past the end nothing happens, and a map's value is added when the key isn't there. A field that holds text or a list, anything in a list or a map inside the element, and a grid's cell are still copies, taken out and put back; a `mut` method on an element still is too. A field read through an element, `rows[i].x`, goes through a pointer at the element, never a copy of it.
- Whatever takes a whole element out of a list copies it, its lists too, so no copy ever reaches what it was copied from: a `foreach`'s variable, a local assigned from an element, an argument (unless a `mut` argument of the same call could change it, as for any list), a map's `Get` or `TryGet`. Reading `rows[i][j]` or `rows[i].count` copies nothing. A `foreach` over a `List<List<int>>` copies each row as it comes to it: a loop that only counts them wants `rows[i].count` in a `for`.
- Text shows them as it shows a list, inside out: `[[1, 2], [3]]`, `[{"a": 1}]`, `[{1, 2}]`.
- Not to C: an extern function's list holds plain values, and a list of lists is refused as a list of text is. Not in a parallel loop's list, whose elements are plain, as text is refused there. Not in an input or a command, which hold no lists but a command's `List<byte>`.
- A snapshot, a hash, a delta and the world as bytes see nothing new: an inner list is a block in the same heap, with its own pages, which they already cover block by block. Hot reloading carries the field over by its type's name (`List<List<int>>`) with the heap, as it carries any list.
- Taking a whole list out of a field or variable copies it; its elements, count, methods and `foreach` don't. A read-only list argument is passed without a copy unless a `mut` argument of the same call could change it.
- Lists in code (not in a world) live in the scratch area, like text.
- `parallel (var i in items) { ... }` goes through a list's elements at once, on threads, as it does a grid's cells (see Grids): its places are the elements' indices, so `i` is an `int`, and the step reads the element as `items[i]`. Each step reads the list as the loop found it and changes only its own element, `items[i]`, so the result is the same on any number of threads. `parallel (var i in items by 2 offset o)` gives each step a block of elements from `i` up to the block's size, which it changes as `items[i + 1]` and the like; blocks start at the offset plus any multiple of their size, and one that would go past the list's end is left out. The step's rules are a grid's: it changes nothing else (not the list's count either: no `Add`, `RemoveAt`, `Resize` or `Clear`), can't end the loop, wait, spawn or send, and so on.
- A parallel loop goes through a list the world keeps, a component's or a singleton's, not a variable's, and one whose elements hold no text: steps run on threads, where text can't be made.
- `items.Sort(a, b) { return a.x < b.x; }` sorts a list, stable, by a comparison written after the call: its two names are the elements it compares, read-only copies of the element type, and the block says whether the first goes before the second, as `return a.x < b.x;`, on every path. Elements the comparison doesn't order keep their order. It runs again and again as the list sorts, in an order nothing promises, so it changes nothing outside it (not the list, not a field, not an outer variable), and can't spawn, send, wait, draw, use the GUI, change scenes or the session, or call C; it reads what it likes: fields, the code's parameters and variables. `break`, `continue` and the comparison's `return` are its own, not the code's around it. It sorts in systems, views, handlers and plain functions, and not yet in methods, async code, functions that take an Action, blocks written after a call, or the input's code. Merge sort, so the same comparisons in the same order on every machine.

### Open

- Searching with a condition, and sorting by a key.
- Changing a list inside a struct element where it is (`bags[i].parts.Add(x)`), and sorting an inner list in place.
- Blending arrays of floats in views (fixed-size arrays in components are implemented; see Arrays).

## Maps and sets

### Decided

- `Map<K, V>` holds values by key, and `Set<K>` keys each once, as values: assigning or passing one copies it, and changing a copy never changes the original.
- A world keeps its maps and sets in its heap, like lists and text (see Lists).
- Nothing about them fails: a key that isn't there reads as the value type's zero, and removing it does nothing. They grow with the heap, as far as it goes (see Limits).
- Going through a map or a set gives its keys in the order they were added, the same on every machine.

### Provisional

- A map or set starts empty. `[a, b]` is a set where one goes, from what it goes into: `Set<int> ids = [1, 2];`, a field's default, an argument. There's no map literal yet: add entries one by one.
- Keys are `byte`, `int`, `long`, `char`, enums, `Entity`, `LocalEntity`, `PlayerID` or `string`: values with a hash and `==`. Floats aren't keys (key by an int instead), nor are structs (key by one of their fields).
- Values are what a list's elements can be: built-in types, text, enums, structs, lists, maps and sets, and structs with any of them in them (see Lists), not ECS data and not grids. A value that's a container is changed where it is, `buckets[key].Add(e)`, which adds the key when it isn't there; a value read is a copy, as a list's element is (see Lists).
- A map: `count`, `scores[key]` to read (the value type's zero when the key isn't there), `scores[key] = value` and `scores[key] += x` to write (adding the key, or replacing its value), and the methods `Has(key)`, `Get(key)` (the value, or nothing, as a `V?`), `TryGet(key, out value)` (whether the key is there, and its value, or the value type's `default`: C#'s `TryGetValue`, see Out parameters), `Remove(key)` (whether it was there) and `Clear()`. A value read is a copy, as a list's element is, when it's taken out; a plain field of a struct value is changed where it is, `scores[k].x = 1`, which adds the key when it isn't there (see Lists, Changing an element where it is).
- A set: `count`, and the methods `Add(key)` (whether it was new), `Has(key)`, `Remove(key)` and `Clear()`. A set has no values to index.
- `foreach (var e in scores)` goes through a map's entries in the order their keys were added, each a copy with `key` and `value` (an `Entry<K, V>`, named by `var`); `foreach (var k in ids)` goes through a set's keys. Entries added while going through are reached too; removed ones aren't.
- Text shows maps as `{1: "a", 2: "b"}` and sets as `{1, 2}`, the keys in their order.
- Changing a map or a set needs something that can change: a `mut` component or singleton, or a `mut` local. Systems that change the match's maps wait for each other, as they do for text and lists: they share its heap.
- Components, singletons, structs and events can hold maps and sets, and so can lists and other maps, as values (see Lists); inputs can't, nor grids' cells. They don't go to C.
- How they're kept (`tide/map.h`): a map is two lists in the heap. Its entries, each a live flag, the key and the value, in the order they were added, a removed one left dead in its place until more than half are dead, when adding a key packs the live ones together; and an index, a power of two of slots by the key's hash, kept under half full. So an entry's bytes depend on nothing but what's in it, and the same adds and removes give the same bytes on every machine.

### Open

- Map literals, `[1: "a", 2: "b"]`.
- Struct keys, and keys by several values.

## Grids

### Decided

- `Grid2<T>` and `Grid3<T>` hold cells at `int2` or `int3` positions. They're fields of components, singletons and scenes, so a world can have many: a dimension per scene, a canvas per player, a grid per ship.
- A grid keeps its cells in chunks, and a chunk only exists once a cell in it is set to something other than zero: everywhere else reads as zero, so an open grid costs what's in it, not the space it spans. A world and its snapshots share chunks until one of them changes one, and hashes cover each chunk on its own.
- A grid's size is given when it's made, `Grid2(1024, 1024)`. An axis given 0, or left out, is open: any int, negative ones too. Past its size, reads give zero and writes do nothing.
- `parallel (var at in cells) { ... }` goes through every cell of a grid at once, on threads. Each step reads the grid as the loop found it and changes only its own cell, so the result is the same on any number of threads, and the order the steps run in never shows. Code after the loop runs once every step is done.
- `parallel (var at in cells by 2 offset o) { ... }` goes through blocks instead: each step has the block of cells from `at` to `at + 1` on each axis (`by int2(2, 1)` sizes each axis), changes only those, and reads any cell. Blocks start at the offset, an int or an `int2`/`int3` given each time, and never overlap, so cells can move within a block: sand falls and slides in 2 by 2 blocks whose offset goes 0, 1, 0, 1, so each cell is in another block the tick after. A block that would go past a grid's size is left out, so along a sized edge, cells are only in the blocks of some offsets.
- `foreach (var at in cells) { ... }` goes through a grid's cells in order, in place, as C#'s would: rows from the first, each from its lowest x, and each step sees what the ones before it changed. When nothing a step does depends on the others (it changes only its own cell, and nothing outside the loop), its steps run at once instead, with the same result.
- `for` loops stay in order.
- There are no chunk systems: a system takes the grid's component or singleton like any other, and goes through the cells with a loop.

```csharp
singleton Field
{
    Grid2<int> cells = Grid2(512, 512);
}

// Ones fall a cell a tick, in blocks of a cell and the one above it, from
// even rows one tick and odd ones the next
system Fall(mut Field field, Time time)
{
    parallel (var at in field.cells by int2(1, 2) offset int2(0, time.tick % 2))
    {
        if (field.cells[at + int2(0, 1)] != 1 || field.cells[at] != 0) continue;
        field.cells[at] = 1;
        field.cells[at + int2(0, 1)] = 0;
    }
}
```

### Provisional

Implemented on the owner's go-ahead, to be revisited once games use them:

- Cells are plain values: numbers, bools, chars, enums, vectors, quaternions, matrices, `Color`, `Rect`, `Entity`, `PlayerID`, and structs of those. Not text, lists or grids, which say to keep them elsewhere and a number for them in the cell.
- `cells[x, y]` is `cells[int2(x, y)]`, and `cells[x, y, z]` is `cells[int3(x, y, z)]`. `cells.size` is the grid's size, 0 on open axes. `cells.Clear()` sets every cell back to zero and keeps the size; `cells = Grid2(...)` gives the field a new, empty grid.
- A cell is a copy, like a list's element: `cells[p].heat = 1` is an error that says to take it out, change it and put it back. `cells[p] += 1` works on numbers. Setting a cell to zero where there's no chunk makes none.
- A grid is never copied, since every chunk would be: a local can't hold one, nor a component or singleton that has one, and a grid field is only assigned a new grid. Functions can't take or return grids yet. `Grid2(...)` takes its cell type from the field it goes in; anywhere else it's an error that says so.
- A chunk is the engine's: 4096 cells (64 by 64, or 16 by 16 by 16 in 3D), or for cells bigger than four bytes, as many as fit in a page (16 KiB), a power of two along each axis, the first axes the most.
- A loop over a grid with a size on every axis goes through every cell within it. An open axis goes on forever, so there a loop goes through the cells of the chunks the grid has, where something was set: in blocks, every block that takes in part of one. A foreach over an open grid goes through them in the same order, rows from the first, skipping what has no chunk.
- A parallel loop's step changes only its own cell or block, `at` plus constants (`at + int2(1, 0)`, `int2(at.x + 1, at.y)`), and the variables it declares. Anything else it changes is an error that says why: a variable outside the loop (to add up, use a `for` loop or a foreach that goes in order), another cell, the whole grid (`Clear`), through a `mut` argument too. It can't `break` out of the loop (`continue` ends the step), return, wait, start tasks, spawn, send, add, remove or destroy, load scenes, draw, use the GUI, or hold another parallel loop. It can call functions, and C.
- A block is 1 to 64 cells along each axis, a size known while compiling: a constant int, or `int2`/`int3`. The offset can be any int: blocks start at it plus any multiple of their size, so with blocks of 2, an offset of 2 gives the same blocks as 0.
- An open axis ends where the ints do. A block that would go past the last int is left out, as one past a grid's size is, so no block's cells wrap around; a step that reads past that edge, `cells[at + int2(1, 0)]` at the last int, reads the cell its own arithmetic wraps to, as an int wraps anywhere else. A foreach reaches the cells at both ends. (Picked while testing that no loop can go wrong there: see Open, at the end.)
- A parallel loop goes in a system, a view or an event handler that isn't async: not in functions, methods, async code or an input's `Sample`. A foreach there whose steps only touch their own cell runs at once; anywhere else, it goes in order.
- A foreach over a grid can't wait inside it: it goes through the cells as they are.
- Each step writes into a copy of its chunk, made from the chunk as it was, and the loop's end puts the copies into the grid in order, chunk by chunk, so where the steps ran never shows. The schedule says how many parallel loops each system has: `(once, 2 parallel loops)`. A tick with one runs on threads, and the loop's steps spread across them when there are enough to be worth it.
- Hosts read a world's cells with `tide_grid_read(&w->heap, w->Field.cells, x, y, 0, &tide_shape_Grid2_int)`: the generated header names each grid type's shape.
- Text can't show a grid.

### Open

- Generating chunks as they're first needed, keeping the regions around players loaded, and sending each player only theirs: an endless voxel world.
- Functions that take grids; parallel steps that spawn and send.
- Skipping what's settled: a parallel loop that only runs where something near changed (what `[Sleeps]` did for chunk systems).
- `for` loops that run at once when nothing a step does depends on the others.
- Resizing a grid; drawing one.

## Errors

### Decided

- Errors are values, never exceptions. An error is any type: usually an enum, a struct when it needs to carry more.
- A function says it can fail with `fails`, and `fail` ends it with an error, as `return` does with a value. A function with no value works the same: `void Open() fails OpenError`.
- The combined type, a value or an error, is never written: `var` holds it.
- At the call: `var score = ParseScore(text) ?? 0;` falls back to a value, `if (ParseScore(text) is int score) { ... }` runs only on success, `if (ParseScore(text) is ParseError why) { ... }` only on failure, and `var score = try ParseScore(text);` passes the error on to the caller.
- `try` only works inside a function that `fails` with the same error type. In a system it's an error, since systems have no caller: handling happens at the call.
- `T?` is for lookups where absence isn't an error: a value or nothing, unwrapped the same way (`??`, `is`).
- Postfix `!`, as in C#, after the expression (`ParseScore(t)!`), means: on failure, carry on with the type's default value (0 for int), never crash. It also silences the warning for ignoring an error.
- Using a failable call's value without unwrapping it is a compile error whose message says how to unwrap it (`??`, `is`, `!`, `try`). Calling a failable function as a statement and ignoring its error is a warning, which `!` silences.
- Forgiving defaults stay as they are for indexing, arithmetic and text. Asynchronous failures stay events (`Disconnected`).

```csharp
enum ParseError
{
    Empty,
    NotANumber,
}

int ParseScore(string text) fails ParseError
{
    if (text == "") fail ParseError.Empty;
    mut var score = 0;
    for (var i = 0; i < text.length; i++)
    {
        var digit = "0123456789".IndexOf(text.Substring(i, 1));
        if (digit < 0) fail ParseError.NotANumber;
        score = score * 10 + digit;
    }
    return score;
}

int Doubled(string text) fails ParseError
{
    var score = try ParseScore(text);
    return score * 2;
}

int? Find(List<int> items, int wanted)
{
    for (var i = 0; i < items.count; i++)
    {
        if (items[i] == wanted) return i;
    }
    return null;
}

system Score(mut Board board)
{
    board.points = ParseScore(board.typed) ?? 0;
    if (Doubled(board.typed) is ParseError.NotANumber) board.mistakes += 1;
    if (Find(board.picks, 3) is int at) board.last = at;
}
```

### Provisional

- `fail`, `try`, `is` and `null` are keywords everywhere; `fails` is one only right after a function's parameters.
- Methods can fail too. Operators, `Interpolate`, extern functions and functions that take an Action can't. Systems, views, handlers and the input's `Sample` and `Sanitize` can't fail, and can't `try`.
- An error's type is one a function can return. It can't be the type the function returns, which `is` couldn't tell apart, nor a `T?`, and a function that fails can't return a `T?` too.
- `try` binds like a unary operator, as C#'s `await` does: `try Parse(a) + 1` adds 1 to the value. It works on a variable that holds a result too, and as a statement: `try Open();`. It keeps its place in the evaluation order: what's before it in its statement runs first, and when it passes an error on, nothing after it runs. In a block written after a call, `try` and `fail` leave the function the block is written in, as `return` does. Leaving a function this way closes the GUI containers it opened.
- `??` binds and groups as in C#: looser than `||`, tighter than `?:`, to the right. Its right side only runs when the left fails or is nothing, so, like the right side of `&&` and `||`, it can't spawn, load scenes or draw widgets. The right side takes the value's type (`?? []`, `?? default`), an int widens to a float (`ParseScore(t) ?? 0.5` is a float), and it can be another failable call or `T?`, which is then what the whole gives: `a ?? b ?? 0`.
- `is` is relational, as in C#. After it goes the value's type (true when it succeeds, or a `T?` has one), the error's type (true when it fails), or one of an error enum's members, `is ParseError.Empty` (true when it fails with that one). Tide has no other type tests: `is` on a plain value is an error.
- A name after `is`'s type declares a read-only local. It goes in the condition of an `if`, `while` or `for`, alone or joined with `&&`, and is in scope in the rest of the condition and where it's true: the `if`'s body (not its `else`), or the loop's body and a for's step. Elsewhere `is` takes no name.
- `!` gives the type's default as `default` does: a struct's field defaults. A `T?` takes `!` too. `F()!;` and `try F();` are statements, for functions with no value.
- `var` holds a failable call's result or a `T?` as it is, and a `mut var` can take another of the same type. A result can't be passed, returned (that's `try`), shown in text or compared; it's unwrapped first.
- `T?` goes on functions' and methods' return types and parameters, and on locals: `int? best = null;`. Not on fields, in lists or inputs, or on system parameters yet. `null` and `default` are nothing, and a value converts to a `T?` by itself, as in C# (an int to a `float?` too). `x == null` and `x != null` tell whether it's nothing. C functions can't take or return one.
- The warning for ignoring an error is on a statement that calls a failable function; `try` and `!` handle it.
- Editors show a failable call's result as `int fails ParseError`, and offer `fail` and `try` in functions that fail.
- In generated C, a failable call's result and a `T?` are a small struct, `tide_result<n>`: the value, the error and whether it succeeded, plain data with no padding the compiler adds. A zeroed one is nothing. Results never go in the world, so snapshots never hold one.

### Open

- Built-in calls that fail, like `Session.Open()` when it can't take players: today Session calls are requests the host acts on after the frame, so their failures come later, as events.
- `is not`, as in C#'s `if (ParseScore(t) is not int score) return;`, which keeps the name in scope after the `if`.
- `T?` in fields, lists and inputs, and `??=`.
- `switch` on an error, and functions that take an Action failing.

## Local files

- `File.Exists(path)` and `Directory.Exists(path)` return bool. `File.Create(path)`, `WriteAllText(path, text)`, `AppendAllText(path, text)`, `WriteAllBytes(path, List<byte>)`, `Copy(source, destination[, overwrite])`, `Move(source, destination[, overwrite])` and `Delete(path)` return void and fail with `IOError`. `File.ReadAllText(path)` returns string; `File.ReadAllBytes(path)` returns `List<byte>`, both failing with `IOError`.
- `Directory.CreateDirectory(path)` creates parents and succeeds if the directory already exists. `Directory.GetFiles(path[, recursive])` and `GetDirectories(path[, recursive])` return `List<string>` of full paths sorted by UTF-8 bytes. `Directory.Move(source, destination)` refuses an existing destination. `Directory.Delete(path[, recursive])` requires recursion for nonempty directories. All fail with `IOError`; recursion and file overwriting default to false. Native recursive traversal does not follow directory links.
- `IOError` is a built-in enum: `None`, `NotFound`, `AlreadyExists`, `AccessDenied`, `InvalidPath`, `NotDirectory`, `DirectoryNotEmpty`, `OutOfSpace`, `StorageUnavailable`, `InvalidData`, `Failed`. Missing file deletion succeeds. Text is strict UTF-8; an empty file is successful, while invalid UTF-8 fails. File creation truncates contents, writes replace contents, and neither creates parent directories.
- `Application.title` is the game's `title` setting, or the program's name without one: text the same in every build of the game, which match code may read too. `Application.persistentDataPath` and `Application.temporaryCachePath` are read-only runtime strings. The standard host selects save locations using the stable `appId` setting, or its game name as the development fallback. Relative paths resolve under the persistent root. Native absolute paths follow OS permissions; web paths stay in this game's virtual persistent and temporary roots.
- File/directory calls and application paths are local only, including helpers that reach them. They cannot run in match code, input sampling, GPU code, sort comparisons or parallel steps. Pure `Path.Combine(a, b)`, `Path.GetFileName(path)`, `Path.GetDirectoryName(path)` and `Path.GetExtension(path)` work on text, recognize both path separators and can run in match code.
- `await Storage.Flush()` is a local async operation returning void and failing with `IOError`. It confirms that changes preceding the call have committed, without blocking the frame loop. Browser files are preloaded from IndexedDB before the game starts, and mutations commit in the background. Files and pending commits belong to the page across hot reloads; failures retain dirty changes for a later flush to retry. Unavailable persistence is an error, while temporary files remain usable. Browser saves are scoped to the origin/profile and may be cleared or evicted. Flush during play, never rely on page shutdown. Native flushes affected OS files and directories.
- `await Web.Fetch(url)` is the other local async operation, giving a `List<byte>` and failing with `IOError` (see Assets, Provisional, Fetching): local only, as the file operations are, and awaited like `Storage.Flush`.

## Assets

### Decided

Agreed 2026-10-06. An asset is a file a game is built with: an image, a text, some bytes. The language names it, the compiler checks it, and the build packs what's named and nothing else.

- `asset` declares a file's contents under a name, at the top level like a constant, resolving to an ordinary type: `asset Grid2<Color> hero = "art/hero.png";`, `asset string rules = "rules.txt";`, `asset List<byte> table = "data/table.bin";`. Code reads `hero` wherever it reads a value of that type. It's read-only, and never assigned.
- The path is relative to the `.tide` file that names it. There's no assets folder and no root: files are wherever the game keeps them, a package's beside its own code, and the path says where from here.
- A path can be a URL. tidec fetches it and packs what it was at build time, so it's a file kept elsewhere and nothing else: match code reads it, nothing is fetched when the game runs, and the game plays offline.
- A path can be a glob, which resolves to every file it matches, in order of their paths: `asset List<Grid2<Color>> run = "art/run/*.png";` gives the contents, and `asset Map<string, Grid2<Color>> run = "art/run/*.png";` the path of each with its contents, for a game that needs the names. A single file resolves to its contents alone: its path is on the line that declares it, and a `const string` can be that path where a game wants it twice.
- A path that names one file is valid where the type holds many (agreed 2026-10-08, of `asset List<Clip> shot = "sfx/shot.wav";` and `asset Map<string, Clip> shot = "sfx/shot.wav";`: "should still be valid, i know its only one entry but this allows to change the scope of the search without changing the signature"): a list of that one file, or a map of it. So a declaration's path goes from one file to a glob, and back, with its type and every line that reads it as they are. The other way round stays an error: a glob where the type holds one file.
- Anything wrong that can be known at compile time is an error at the declaration: a file that isn't there, a glob that matches nothing, a URL that can't be fetched, bytes that aren't what the type says (an image that doesn't decode, text that isn't UTF-8). The editor says the same as the game is written, with no build.
- A game ships the closure of its declarations: `tide build` packs the files `asset` names, and nothing else in the folder. A file in the pack is there once however many declarations name it or files have its bytes, and all of them are the same value.
- Match code reads packed assets, since every machine has them. What a machine loads as the game runs (a file of the player's, a URL) is local: it reaches the match as match data, through a command, as a game's own state, which the netcode sends like any other, to those in the match and to whoever joins.
- A user's content enters the match as the file's bytes, in the format it came in: a `Grid2<Color>` field decodes a PNG with `grid.Decode(bytes)`, with the engine's own decoder, so it's the same pixels on every machine.
- No GUIDs and no meta files: the engine owns none of a game's files and writes into none. References are in text, in the source, and all of them are constant, so moving a file is a refactor: the language server rewrites the references when a file is moved in the editor, and a file moved outside it is found again by its bytes, which the error names with a quick fix.
- **A model is a `Mesh<V>`** (agreed 2026-10-08. The owner, of the Tag sample: every 3D game of the repo was boxes, balls and cylinders made by loops, as a game couldn't use a model an artist made or a tool generated. Of the proposal's four points they picked: glTF and OBJ; `Mesh<V>` over the game's vertex struct, filled by its fields' names, its lists read by any code; both ways of drawing it; a file's parts as one mesh. FBX support was requested 2026-10-10). `asset Mesh<Corner> tree = "art/tree.glb";` reads a glTF (`.glb`, `.gltf`), FBX (`.fbx`, binary or ASCII) or Wavefront OBJ as one mesh, whose vertices are the struct in its `<>`:
  - The file fills the struct's fields by their names, `position`, `normal`, `uv`, `color` and the like, and a field of any other name keeps its default. `position`, `uv` and `color` already meant that on a vertex, so `Mesh<Vertex3>` draws with no frag.
  - `tree.vertices` and `tree.indices` are plain read-only lists, which any code reads, match code too, as every machine has the same pack.
  - `Draw.Mesh(tree, transform, ...)` and `Draw.Instanced(tree, instances, ...)` draw it, the mesh where its two lists go, and `Draw.Mesh(tree.vertices, tree.indices, ...)` is the same draw.
  - A file with several meshes or materials is one mesh: its nodes' transforms worked in, and each material's base color multiplied into its vertices' `color`. A game that needs parts apart exports them as files, and names them with a glob.
  - Static meshes: what's built, and each rule of it, are picks: see Mesh assets, under Provisional.
- **An image that's only drawn is a `Texture`** (agreed 2026-10-07, of `asset Texture hero = "art/hero.png";`: "ah yes lets add `Texture` there"). `Grid2<Color>` stays what an image is where code reads its cells, and it draws too; a `Texture` is the image as the GPU has it, which no code reads. Reading a `Texture`'s pixels back from the GPU isn't part of it. What's built, and each rule of it, are picks: see Texture assets, under Provisional.
- **PSD images are imported without Photoshop** (agreed 2026-10-08): an image asset reads the saved composite, the visible layers combined into one picture, through the same `Texture`, `Grid2<Color>` and cubemap declarations as PNGs. The build needs no external application. What's read, and its limits, are picks: see PSD images, under Provisional.

```csharp
asset Grid2<Color> hero = "art/hero.png";
asset Map<string, Grid2<Color>> tiles = "art/tiles/*.png";
asset string credits = "https://example.com/credits.txt";

view Hero(Body body)
{
    Draw.Mesh(Quad(body.position), QuadIndices, hero);
}

command SetBackground { List<byte> png; }

command(SetBackground value, PlayerID sender) Apply(mut Arena arena)
{
    if (sender != arena.host) return;
    arena.background.Decode(value.png)!;
}
```

### Provisional

Picks made to build it, awaiting the owner.

- **The types** an asset resolves to: `string` (UTF-8), `List<byte>` (any file) and `Grid2<Color>` (PNG, supported PSD, JPEG, TGA or BMP, decoded at build time into pixels), each alone, as a `List<T>` of a glob's matches (or of the one file a path names), or as a `Map<string, T>` of their paths. The grid is sized as the image is, and read-only: a system takes it as it takes any read-only value, and a frag or vert samples it by name, as it samples a singleton's grid. Textures and cubemap faces accept the same images; audio and model assets are described below. Fonts remain open.
- **Globs:** `*` matches within a name, `**` across folders, and `?` a character; matches are sorted by the UTF-8 bytes of their paths, relative to the declaring file; a map's keys are those paths. Matching nothing is an error.
- **One file for a list or a map** (picks made as it was built, 2026-10-08; that it's valid is decided, above): the map's key is the path as the declaration writes it (`shot["sfx/shot.wav"]`), which is the key a glob gives that file, so a lookup by key goes on working when the path becomes `"sfx/*.wav"`. A URL is taken the same way, a list or a map of the one file it names, keyed by the URL: the owner spoke of files, and a rule with an exception for URLs would be the odd one. A `Cubemap` is six files, so its path stays a glob, and `[Raw]` stays for one `Texture` or one `Grid2<Color>`, as a frag reads those by name.
- **URLs** are `http://` and `https://`. tidec keeps what it fetched in `.tide/` by URL with the server's ETag, asks whether it changed at each build and at each `tide run`'s start, and builds from the cache when the server can't be reached, so offline builds and reloads work; a run picks up a changed URL when it's started again. Pinning a URL to a hash is open, below.
- **Where the pack is:** in a file beside the program on desktop (`<game>.pack`), inside the self-contained page on the web, and in the APK's assets on Android. Under `tide run` there's no pack: the game reads the source files, and tide watches them, so a saved image is in the running game at once, with no rebuild, as it's data. A web build can keep the pack beside the page rather than inside it, so the page stays small and the pack is fetched from wherever it's hosted; the option's name is open.
- **Sharing** is by SHA-256 of the bytes, among declarations that read the file as the same thing. A `Grid2<Color>` of the pack lives in a read-only region every world can name, outside every heap, so no world copies, hashes or sends it, and it's the same memory to every reference. A file read as two things is in the pack as each where they're kept differently: an image as a `Grid2<Color>` and as a `Texture`, and a text with a character of several bytes as a `string` and as a `List<byte>` (text says how many characters it is, which isn't its bytes then; text that's all ASCII and its bytes are in the pack once).
- **Decoding:** `grid.Decode(bytes)` reads a PNG into a `Grid2<Color>` field of a component, singleton or scene, sizing the grid as the image is, and fails with `IOError.InvalidData`. It's pure, so it runs in match code, and it runs where it's called, which in a tick is on every machine. **Fetching:** `await Web.Fetch(url)` is a local async operation giving `List<byte>` and failing with `IOError`, like `File.ReadAllBytes`, which is the other way bytes come in at runtime; so loading a player's image is `grid.Decode(File.ReadAllBytes(path))`, and there's no asset handle type beside the plain ones. On the web it's `fetch`, so the server's CORS applies; on desktop it's the HTTPS the relay's connection uses; on Android, Java's.
  - Both are built (2026-10-06), with these picks. `Decode` reads every kind of PNG the specification has (gray, color, a palette, with and without alpha, 1 to 16 bits a sample, interlaced or not) with the engine's own reader (`tide/png.h`); a sample becomes a channel by one division, so the pixels are the same bits on every machine. A side past 16384 pixels is `InvalidData` before anything is made for it. A failed decode leaves the grid as it was. A pixel that's clear black is a cell that's zero, which takes no chunk. Rows go into the grid as they're decoded, so nothing the picture's size is made beside it; a picture too big for the world's heap ends the program as running out of memory does. `Web.Fetch` is a GET that follows up to 5 redirects; `NotFound` is a 404 and every other status outside 200 to 299 is `Failed`, as is a body past 64 MiB (read no further) and a certificate the system doesn't trust; nothing times out, as a browser's `fetch` doesn't. On Android it's the same client as desktop's over the TLS the relay's connection uses there (Java's `SSLEngine`), not `HttpsURLConnection`. A fetch still going when its task ends (a reload, the program's end) is forgotten, and stops.
- **Finding a moved file:** tide keeps the SHA-256 of every file an `asset` named in `.tide/`; when one is missing, the error says where a file with the same bytes is now in the game's folder (or packages), with a quick fix to the new path, and else lists files of the same name. The language server takes the editor's rename of a file or folder (`workspace/willRenameFiles`) and rewrites every reference to it.
- **An `asset` is a declaration** of its file's namespace, named like a constant, usable in every kind of code (match, local, GPU, C through a `List<byte>` or a grid as any value is passed), and never `mut`, assigned, or a parameter's default. A path has to be a constant expression; anything else is an error that says why: the dependency tree is static.

Picks made as the first slice was built (2026-10-06), awaiting the owner too:

- **`List<byte>` alone is one file's bytes**, never a glob of files; a glob of files as bytes is `List<List<byte>>` or `Map<string, List<byte>>`. These are the only places a list holds a list or a grid: an asset's type can, nothing else can, and a local can't hold one of them either (a grid is still never copied). `Map<K, ...>` with a key that isn't `string` is an error that says the keys are paths.
- **A frag or vert samples an asset by its bare name** (`hero.Sample(uv)`), never an element of a list of assets (`run[1].Sample(uv)` is an error that says to declare the one it samples): the GPU has no list to index.
- **A path's `\` is `/`**, and `>>` closes two generic types written inside each other (`List<Grid2<Color>>`), as it does in C#.
- **Under `tide run` the pack still exists**, in the run's own folder, written by tide (whose front end resolves the files) with each build and again when a file an asset names changes, and loaded by the host between frames: so a saved image is in the running game within a moment, with no compile or link, which is what "no pack" meant; a hot reload hands the new library the pack the host has. On the web tide serves it beside the program and the page fetches it. A C host of its own (`tide_add_game` with `HOST`) gets the pack as C (`tide_game_pack`) and loads it itself (`tide_assets_load`).
- **A pack from another build** of the game: a name it hasn't got, or has as another type, reads as empty, and the program says so once on stderr. A program whose pack is missing says so too and runs with every asset empty.
- **URLs** are fetched by tidec once a process (so once a build, and once as `tide run` starts), with curl, into `<game>/.tide/assets/urls/`, each under the SHA-256 of its URL with its ETag beside it; a `tidec` run with no game folder keeps them in a temporary folder. A URL with a character tide can't pass to curl (a quote, a backslash, a dollar sign, a percent) is an error.
- **What the pack holds of a grid** is the chunks that have a color in them: a clear black cell takes nothing, as in a world's grid a cell nothing wrote takes nothing. A texture drawn from an asset is told apart by the file's hash, so the draw list copies it once.
- **Where the pack is at runtime:** the top 512 MiB of the scratch area's offsets, so an asset's handle is a scratch handle that every read path reads already, and a thread maps the region into its slots the first time it runs after a load; the scratch area has 512 MiB a thread while a pack is loaded, where it had 1 GiB.
- **The editor and the build say the same things** about a path: the checker reports a missing file (naming a file of the same bytes, or files of that name), a glob with no match, bytes that aren't the type's, and tidels attaches its fixes to those diagnostics.

Picks made as defaults that name assets were built (2026-10-07), awaiting the owner, who asked for the first of these to compile (`local Texture test = logo;` in a component: "I feel like this should be possible?"):

- **A field's default can name an asset**, where it could only be a constant expression: `local Texture face = logo;`, `string text = rules;`, a struct's `Texture image = logo;`. An asset is as good as a constant there: read-only, known as the game is built, the same on every machine.
  - **It counts as a constant anywhere in the default's expression**, by the rule for constants and no other: a struct's value (`Sprite { image = logo }`), a member (`table.count`), an operator, a `Math` function, a name through its namespace (`Art.logo`).
  - **The value is the pack's, read where the default is given**, not while compiling: as a spawn or an `Add` leaves the field out, as a match's or the local state's singletons start, for the local fields of an entity nothing wrote yet, and for a field a hot reload adds. Text, bytes and a glob's list or map are copied, into the world the field is in; a `Texture`, a `Clip` and a `Cubemap` are names of their declarations (above), so nothing is copied and a file saved under `tide run` shows through them.
  - **What a field can't be, a default doesn't make it**: a `Texture` field in the match is the error it was, and a `Grid2<Color>` field can't start as an asset's grid, as a grid is never copied (an error that says to read the asset by its name). A grid asset is a `Texture` field's default, by the conversion there is.
  - **One of a glob's isn't a default**: `frames[0]` and `icons["star.png"]` are no constant expressions, as indexing isn't one.
  - **Nothing else that takes a constant takes an asset**: a `const`'s value, a setting, a bound, an enum member's value, an array's length, an asset's path, a parameter's default. Those are worked out as the game is built, or name what the build reads, and an asset's value is the pack's. Through a namespace or a member (`Art.rules`, `rules.length`) an asset used to get past that check with no error, and an asset whose path was one had no file; it's an error that says a field's default is where one goes.
  - **A host loads the pack before it makes a world or the local state** (the standard host does; a hot reload hands a new build the pack before it carries anything over): a default given with no pack loaded reads the asset as empty, as any read does.

Picks made as `Texture` assets were built (2026-10-07), awaiting the owner. That an asset can be a `Texture` is decided, above:

- **Texture assets.** `asset Texture hero = "art/hero.png";` is a PNG, supported PSD, JPEG, TGA or BMP, decoded as the game is built into pixels of a byte a channel (each channel the nearest of 0 to 255), which is what the GPU's copy has: a quarter of what the same image takes as a `Grid2<Color>`. A glob's are `asset List<Texture> frames = "art/run/*.png";` and `asset Map<string, Texture> icons = "art/icons/*.png";`, the one place a list or a map holds a `Texture`; PSDs use `.psd` in their paths the same way.
- **Cubemap assets.** `asset Cubemap sky = "art/sky_??.png";` is six supported image files a glob matches, named for their faces, as a cube a frag reads by a direction: see Cubemaps, under Views and drawing's Provisional, for the file convention and each rule.
  - **It's a `Texture` like any other**, a value: a draw's texture (`Draw.Mesh(vertices, indices, hero)`, in 3D, instanced, into a target), the source a frag's `Texture` parameter is given, a `Draw.Blit`'s source, copied into a `Texture` field or a local, passed to a function. A frag or a vert samples one by its bare name, `hero.Sample(uv)`, as it samples a `Grid2<Color>` asset, and never one of a list's (`frames[1].Sample(uv)` is the same error): give the draw that one as the frag's `Texture` source.
  - **No code reads it**: it has no cells, and no size to ask (a `Texture` has neither). A game that needs an image's size or its pixels declares the file as a `Grid2<Color>` too, or instead; the two are values of their own, and the pack has both.
  - **It's local code's.** A `Texture` is this machine's, so match code can't name one (the error rendering resources always gave), where it reads a `Grid2<Color>` asset freely.
  - **A `Texture` names its declaration's image, not its pixels.** What a field keeps of `hero`, of `frames[2]` (the declaration and that place) or of `icons["coin"]` (the declaration and that key) is the file as the loaded pack has it: under `tide run`, a saved image shows through the copies a game kept as it does through the asset's own name. A copy whose declaration, place or key the pack no longer has draws as the default `Texture` does: no texture.
  - **Nothing copies its pixels**: the draw list draws them where the pack has them, so an image costs its 4 bytes a pixel in the pack and the GPU's copy. A `Grid2<Color>` asset costs 16 a pixel in the pack, 4 in the draw list and the GPU's. Two names of one file are one image, in the pack and on the GPU.
  - **A pack from another build**: a `Texture` it has no image for is the default one, as the other types read empty.
  - **Not there**: a `Texture`'s size, reading its pixels (which would be a read back from the GPU for a target's color, and isn't offered for any `Texture`), WebP/TIFF, animated images, SVG, HDR/EXR image assets, and compressed GPU formats.

Picks made as PSD images were built (2026-10-08), awaiting the owner. Reading a PSD's saved composite without Photoshop is decided, above:

- **PSD images.** `asset Texture hero = "art/hero.psd";` and `asset Grid2<Color> hero = "art/hero.psd";`, alone, as a list or map, and a `Cubemap`'s faces. The file is an ordinary PSD (version 1), RGB with 8 or 16 bits per channel, at most 16384 pixels per side, whose image data is uncompressed, RLE, ZIP or ZIP with prediction. The compiler's own reader decodes its saved composite, never its layers; saving with Photoshop's Maximize Compatibility includes that picture. A missing or damaged composite, PSB, other color modes (CMYK among them), 32-bit samples and unsupported compression are errors at the path. Color profiles aren't applied; sRGB art keeps matching display colors. Each sample becomes a float channel of a `Grid2<Color>` or the nearest byte of a `Texture`, and only the decoded pixels go in the pack, as with PNG. No Photoshop installation is needed at build time or runtime, and no PSD decoder is added to a game. `grid.Decode(bytes)` continues to read PNG only.

Picks made as `Mesh` assets were built (2026-10-08), awaiting the owner. That a model is a `Mesh<V>`, filled by field names, drawn both ways and one mesh a file, is decided, above:

- **Mesh assets.** `asset Mesh<V> name = "path";`, alone or as a glob's `List<Mesh<V>>` or `Map<string, Mesh<V>>` (one file where the type holds many is a list or a map of that one, as for any asset). `V` is a struct that can be a 3D mesh's vertex: a `float3 position`, and fields of floats, ints, their vectors and colors, 12 at the most.
  - **The fields a file fills, and their types:** `position` (`float3`), `normal` (`float3`), `uv` and `uv2` (`float2`), `color` (`Color`) and `tangent` (`float4`, its `w` the side the bitangent is on, or `float3` for the direction alone). One of those names with another type is an error at the declaration. Names follow the built-in vertex and Unity's vertex attributes.
  - **What a file hasn't got:** a `normal` is each triangle's own, the way its front faces, for vertices that triangle doesn't share (what glTF says a mesh with no normals is drawn as); `uv`, `uv2` and `tangent` are zeros; `color` is white. No tangents are worked out.
  - **A field of any other name is given its default as the pack is loaded**, by the game's own code, so it's exactly what a vertex made in code starts as, whatever expression the default is.
  - **Axes.** glTF is right-handed, `+y` up, its fronts counterclockwise; Tide follows Unity, left-handed with fronts clockwise. A file's `x` is negated (positions, normals, tangents, whose `w` turns too) and its triangles wound the other way, but under a node that mirrors, which has turned them already. So `+y` stays up, what faces `+z` in the file faces `+z` here, as Unity's glTF importer has it, and nothing is mirrored. An OBJ says nothing of its axes: it's read the same way, as tools write it.
  - **A uv is (0, 0) at a texture's top left**, as glTF's is and Tide's; an OBJ's counts from the bottom, so its second number is turned over.
  - **Colors.** glTF's vertex colors and base color factors are light, and a `Color` is what the screen shows: each channel goes through the sRGB curve, kept from 0 to 1, alpha as it is. An OBJ's colors after its positions, which have no standard, are taken as written.
  - **From a glTF:** the scene it opens with (or its first; with none, every node that's nobody's child; with no nodes, its meshes as they are), each node's matrix or translation, rotation and scale, every primitive of triangles, strips or fans (points and lines are left out), accessors of every component type, normalized or not (`KHR_mesh_quantization`), with a stride and sparse; a material's `baseColorFactor`. Buffers are the `.glb`'s own, a data URI's, or files beside a `.gltf` by the paths it writes, which `tide run` watches with the rest. Nothing else is read: textures, the rest of a material, skins (the mesh is as it stands), animation, morph targets, cameras, lights, extras. Geometry compressed with Draco or meshopt is an error that says to export it without, and so is glTF 1.0.
  - **From an OBJ:** `v`, `vt`, `vn` and `f`, places counted from the end (`-1`), faces of any number of corners as fans, and a color after a position (`v x y z r g b`). A vertex of the mesh is each combination of position, uv and normal a face uses. Its `.mtl`, groups, lines and smoothing are passed over.
  - **From an FBX:** polygon meshes triangulated during import, node transforms, normals, tangents, two uv sets, vertex colors and material base colors. The source's axes and units become right-handed Y-up meters before the existing Tide coordinate conversion. Geometry and inheritance transforms use unnamed helper nodes when needed. A static mesh takes the rest geometry and ignores animation and deformation. Textures and other material properties stay outside the import. The compiler's ufbx reader is build-time code only; generated C and the runtime use the existing mesh representation.
  - **The same bits whichever machine builds.** The pack's vertices are what match code reads, so everything that makes them is the basic float operations in a fixed order: a decimal number becomes a float by a reader of tidec's own (the C library's isn't promised to round the same everywhere), a node's matrices and the normals through them are plain arithmetic, and the sRGB curve is the engine's `pow`.
  - **Limits:** 16,777,216 vertices and 33,554,432 triangles a mesh.
  - **Where a `Mesh<V>` can be:** an asset's type, and what passes one on within a frame: a local, a loop's variable, a function's read-only parameter (no `mut`, `out` or default) and what it returns. Not a field, a constant, an `is` pattern, or an element of a list or a map that's no asset's; not an `extern` function's (C takes the two lists), an operator's or an `audio` declaration's; and nothing of a task's (an `async` function's parameter, local or result), which would keep it while it waits.
  - **Why nothing keeps one:** a `Mesh` value is two handles into the loaded pack, and a pack is loaded again between frames (`tide run` does when a file is saved). What a game keeps is which mesh: a place in a list or a key of a map.
  - **Read-only through whatever holds it:** `mesh.vertices` and `mesh.indices` of a `mut` local are the pack's too. A copy of a list is the code's own: `mut var mine = tree.vertices;`.
  - **A draw's Mesh is worked out once**, though it stands for two arguments: `Draw.Mesh(models[next++], ...)` steps once.
  - **In the pack** a mesh is two lists' blocks, its vertices laid out as the struct is, shared by what's in the file and how the struct lays it out: two declarations of one file and one struct are one mesh, in the pack and on the GPU; the same file as two structs is two. An entry says its vertices' layout as a number, and a pack another build made, of a struct that's changed since, hasn't got the mesh: it reads as empty, and the program says so once. The draw list tells a pack's mesh by what its file had, with no vertex read, so a pack loaded again with the file as it was draws the mesh the GPU has.
  - **Not there:** the textures inside a model file (declare each texture separately) and a material beyond its color; a file's parts or materials apart; skins, animation and morph targets; `Mesh<V>` as the type of a mesh a game makes, in a field; a mesh read from bytes while the game runs, as `grid.Decode` reads a picture; a 2D mesh from a file; tangents worked out; and a frag or a vert reading `mesh.vertices` as a list, which is an error that says to copy what it needs into a singleton's list, and is a gap by the rule that GPU code refuses only what a GPU can't do.

- **JPEG, TGA and BMP images** (built 2026-10-10): build-time `Texture`, `Grid2<Color>`, their lists/maps and cubemap faces import these alongside PNG/PSD through the existing types. Maximum dimensions remain 16384 per side; new decoder/output allocations and source size are bounded to 512 MiB. Only pixels enter the pack, and runtime `grid.Decode` remains PNG-only. Profiles and JPEG EXIF orientation are not applied. See [Assets](./game/assets.md#image-formats) for supported variants and source-format limits.

### Open

- Fonts as assets, with their type. Audio's is `Clip` (see Audio), and a model's `Mesh<V>` (above), of which what's listed under Not there is open: the textures inside a file first, as most models that tools hand over have one.
- A pack kept beside a web page rather than inside it, fetched from wherever it's hosted (the option's name, and whether a page waits for it).
- Load order, streaming and lazy loading within the pack, and what a web page fetches first.
- Decoding off the frame's thread for what a machine loads at runtime (a 4K PNG takes a frame).
- Pinning a URL to a hash of its contents, so a build is reproducible when the server changes the file.
- Big media through the match: it goes player to server to everyone, so a host's upload is the file once per player. The docs say the number; a game with big files keeps a URL in the match and each machine fetches. Whether the engine should do more than say so.

## Skinned meshes and animation

### Decided

Agreed 2026-10-08 and implemented the same day. Static `Mesh<V>` retains its flattening behavior; `SkinnedMesh<V>` preserves skeletons and animations. Implementation choices beyond the agreed design are recorded under Provisional.

- **A skinned model is a `SkinnedMesh<V>`.** It provides geometry over the game's vertex struct, its skeleton and its animations. A mesh is shared independently of playback: several characters can use the same model with different poses.
- **A pose is separate from the mesh.** Animation produces a pose; game code can read and edit its bones, and drawing uses it to deform the mesh. Both match systems and views can sample and use poses, including for sockets, hitboxes and root motion. Sampling and blending are deterministic CPU operations using Tide's math. GPU deformation is for drawing and never supplies match results.
- **Drawing has its own call, `Draw.SkinnedMesh`.** A pose is required by this dedicated call. The implemented argument list and pose operations are below.
- **Contracts are optional.** `SkinnedMesh<V>` needs no empty contract. `SkinnedMesh<V, C>` declares required members through a contract `C`. A contract specifies a required subset of the model's content: extra clips and bones remain available for dynamic discovery and lookup. A known asset is checked during compilation; a runtime-loaded value with a contract is checked before that value is successfully made available.
- **A known asset provides checked members without a written contract.** For a single asset declared without a contract, the compiler reads its animation and bone names and exposes them as members, such as `hero.animations.idle` and `hero.skeleton.bones.head`. A missing member is a compile error. An `asset` URL is known this way too, since it is fetched during compilation as other asset URLs are. A contracted value guarantees its declared requirements.
- **Dynamic lookup remains available in every case.** `hero.animations.Find(name)` returns `Animation?`; bones can also be looked up dynamically, with an optional typed reference as the result. A runtime-loaded model with no contract uses lookup and enumeration. A model with inferred or declared members supports those operations as well. A clip obtained through a checked member and one found dynamically are ordinary animation values used by the same sampling and blending operations.
- **A bone member produces a typed reference.** The reference identifies the bone within its skeleton and lets a pose reach that bone without looking up text at every edit. Name lookup resolves such a reference. The implemented type is `Bone`, used as `pose[head]`, below. Skeleton compatibility is distinct from the presence of names: two models both having an animation named `run` does not establish compatible skeletons.
- **Inference retains known members.** `var character = hero;` keeps a known asset's inferred members. A general parameter written as `SkinnedMesh<V>` promises the common dynamic API; a helper requiring checked members declares a contract or receives the selected `Animation` or `Bone` directly. A declared contract keeps its guarantees when the model passes through helpers and collections.
- **Names have explicit rules.** Inferred identifiers lowercase the first ASCII letter and otherwise preserve the source name: `Idle` becomes `idle`. Invalid identifiers, keywords, duplicate names and normalization collisions have no checked dot member. `[Name("original name")]` on a contract field supplies an exact source alias. `Find` uses the exact original spelling, without normalization.
- **The editor understands these members.** Completion and hover expose inferred and contracted animations and bones, and member diagnostics agree with the compiler. Reading model metadata must keep editing responsive; ordinary completion must not wait for a full geometry import or a URL download. Cache and refresh details are below.
- **Runtime fetching follows the existing local/match boundary.** A URL fetched while playing is local, unlike an `asset` URL. Content that affects match bones or root motion must enter through synchronized match data, as other runtime files do; a system does not fetch its own copy independently.

There are three forms of member access:

| Model | Checked members | Dynamic access |
|---|---|---|
| Compile-time asset, no explicit contract | Inferred from the file | `Find` and enumeration |
| Runtime-loaded model, no contract | None inferred from its unknown contents | `Find` and enumeration |
| Model with an explicit contract | The declared requirements, validated before use | `Find` and enumeration, including extra content |

These examples use source names `Idle`, `Run` and `Head`, exposed as `idle`, `run` and `head`.

```csharp
asset SkinnedMesh<Corner> hero = "art/hero.glb";

// No written contract is needed for the known asset.
var idle = hero.animations.idle;
var head = hero.skeleton.bones.head;

var character = hero;
var run = character.animations.run;

// Lookup is also available for names chosen while playing.
Animation? emote = hero.animations.Find(emoteName);
```

### Provisional

The following API and failure behavior are implemented. These detailed choices remain provisional where the owner's agreed points above do not settle them. A contract is an ordinary struct whose required fields are `Animation` or `Bone`, without defaults or local fields:

```csharp
struct Locomotion
{
    Animation idle;
    Animation run;
    [Name("Right Hand")] Bone hand;
}

asset SkinnedMesh<Corner, Locomotion> hero = "art/hero.glb";
```

`Locomotion` requires animations whose normalized identifiers are `idle` and `run`, and a bone whose exact source name is `Right Hand`. Extra content remains available through dynamic lookup and enumeration. Checked contract catalogs expose the required fields; they do not additionally infer other dot members. `count`, `Find` and `At` are reserved catalog operation names. A duplicate exact source name makes `Find` return null and cannot satisfy an exact contract alias. A `Name` alias cannot be empty, including on a contract used only by a helper parameter. Unnamed entries are accessible through `At`; `Find("")` returns null.

The CPU operations are:

| Value | Operation | Result |
|---|---|---|
| `model` | `.vertices`, `.indices` | Read-only geometry lists |
| `model` | `.animations`, `.skeleton` | Immutable catalogs and hierarchy |
| `model.animations` | `.count`, `.At(int index)`, `.Find(string name)` | `int`, `Animation?`, `Animation?` |
| `skeleton.bones` | `.count`, `.At(int index)`, `.Find(string name)` | `int`, `Bone?`, `Bone?` |
| `skeleton` | `.Find(string name)`, `.RestPose()` | `Bone?`, `Pose` |
| `animation` | `.name`, `.duration` | Original name as `string`, seconds as `float` |
| `animation` | `.Sample(float seconds[, Playback playback])` | A new `Pose` |
| Mutable `pose` | `.Sample(Animation animation, float seconds[, Playback playback])` | Reuses the pose's transforms |
| Mutable `pose` | `.Blend(Pose other, float weight)` | Blends toward `other` in place |
| `pose` | `[Bone bone]` | Parent-local `BoneTransform` |
| `pose` | `.Model(Bone bone)` | Accumulated model-space `float4x4` |

`BoneTransform` has `float3 position`, `quaternion rotation` and `float3 scale`; its default rotation is identity and default scale is one. `Playback.Clamp` is the default; `Playback.Loop` wraps positive and negative times over the clip duration, with an exact endpoint wrapping to zero. Unanimated properties begin from authored rest transforms on every sample. Blending clamps weight to zero through one and interpolates local transforms with shortest-path quaternion interpolation. A bone includes its skeleton identity; an invalid or incompatible bone reads the identity transform, and its writes do nothing. Incompatible in-place sample or blend leaves the destination unchanged; an incompatible pose records no skinned draw.

Pose use:

```csharp
mut var pose = hero.animations.idle.Sample(seconds, Playback.Loop);
var running = hero.animations.run.Sample(runSeconds, Playback.Loop);
pose.Blend(running, moving);

var head = hero.skeleton.bones.head;
pose[head].rotation = Math.Mul(pose[head].rotation, lookRotation);

Draw.SkinnedMesh(hero, transform, pose);
var weaponTransform = Math.Mul(transform, pose.Model(rightHand));
Draw.Mesh(sword, weaponTransform);
```

- **Import and runtime loading.** Assets accept glTF 2.0 `.gltf` and `.glb`, and binary or ASCII `.fbx`. Node hierarchy and authored local transforms are preserved, including animated non-joint parents; the skeleton contains all source nodes and a synthetic identity root. Several skins keep their own inverse-bind palettes; glTF's supplied consecutive joint/weight sets are preserved and positive weights are normalized in a fixed order. glTF animation channels support STEP, LINEAR and CUBICSPLINE. FBX imports linear or rigid skin weights and converts declared axes and units to Y-up meters. Animation stacks become clips shifted to zero with their authored duration; nonlinear transform curves are resampled at 60 samples per second into LINEAR channels, retaining source keys within the clip and stepped changes. Geometry and inheritance transforms can add unnamed helper nodes. Skinned FBX rejects dual-quaternion skinning, several skin deformers on one mesh and external vertex caches. Morph targets are rejected. Vertex fields follow the static mesh's naming/default rules. `SkinnedMesh<V>.Decode(List<byte> bytes)` and the contracted form return a model `fails IOError`, accepting self-contained GLB bytes and returning `IOError.InvalidData` for malformed data, external resources or unmet contracts. Fetching remains separate. Model bytes, animation values and mutable poses use ordinary list/struct ownership and can be stored in match or local fields.
- **Type boundaries.** `var` retains a single known asset's catalog. Explicit plain generic types expose dynamic catalogs; contracted helper parameters and list/map elements expose their common contract. Inferred or contracted model conversions are checked against the destination requirements. Collection asset imports provide their common type rather than per-item inferred dot members; selection does not invent a union of unrelated catalogs.
- **Drawing and shaders.** `Draw.SkinnedMesh(mesh, transform, pose[, texture][, vert][, frag][, values])` has the same form on a render target. Skinning precedes the user `vert`, providing mesh-local positions, normals and tangents before the draw transform and camera. Inverse-bind offsets apply to deformation, not to `pose.Model(bone)`, which is suitable for sockets. The renderer supports nonuniform scale and tangent handedness. Pose lists use ordinary copy/snapshot rules and have no automatic view interpolation; a view can sample from its chosen playback time. Sampling does not move entities or send animation events.
- **Responsive editor metadata.** A dedicated cached reader extracts catalogs and hierarchy from `.gltf` JSON or a `.glb` JSON chunk without reading geometry, sample buffers, side files or the BIN chunk. FBX inspection reads its source while skipping geometry, animation curves and embedded content. Watched changes invalidate metadata even with unchanged size/time; source and contract edits reuse unchanged metadata. URLs use an existing local cache without fetching, retaining the original URL's source extension for format detection. Invalid and unavailable imports are diagnosed separately from missing checked members, and contract completion remains available. Cold inspection is synchronous: 16 MiB of glTF JSON, or 64 MiB of FBX source with 128 MiB each for temporary and result parser allocations. No asynchronous indexing protocol is built. Full builds perform authoritative import validation. Details and measurements are in `notes/editors.md`.
- **Imported match content.** Canonical skeleton, animation and model data and the vertex layout participate in the generated game's compatibility identity. Loading a pack checks each skinned declaration's expected content and shape before replacing the current pack; changed content requires its matching game build. A rejected replacement leaves the old pack available. This is separate from a Pose/Bone skeleton fingerprint, which checks structural pose compatibility rather than peer admission.

### Open

- **Playback and interpolation.** Transitions, masks, additive layers, automatic `Play`/`Update` state, long-running clock precision and a convenience rule for view interpolation. A wrapped clock must not interpolate through the wrong part of a clip.
- **Root motion and events.** An explicit root-motion delta over a time interval, including loop crossings, for a system to apply through its own movement rules; removal of extracted motion from the displayed pose. Root selection, translation/rotation/scale behavior and animation markers or event queries remain open. Sampling must not implicitly move entities or send events.
- **Further model APIs.** Separate animation files and their skeleton binding, retargeting, hierarchy-path lookup, pre-skin vertex access and an instanced call with an independent pose per instance. `Draw.SkinnedInstanced` is a suggested name.
- **Further match-content policy.** Admission of arbitrary runtime content and convenience rules for replacing a rig while retaining older rollback history. Imported content already participates in build compatibility; runtime content reaches the match through synchronized state.
- **Further behavior.** IK, morph-target animation and an automatic animation controller are outside the current implementation. Background metadata indexing and explicit editor URL refresh remain open.

## Tasks

### Decided

- `async` and `await`, as in C#. `async` before a function lets it wait with `await`, and pick up where it stopped. There's no `Task<T>`: the function's type is its value's, `async int Doubled(int x)`, and `await Doubled(3)` gives it.
- A call of an async function that isn't awaited starts a task, which goes on by itself.
- Tasks run in the match as well as in local code. A match task is part of the match: snapshots, hashes and rollback hold it, players who join get it, and it carries on when the match changes hands. Match code only waits for what's the same on every machine (ticks, time, events); a service's answer reaches the match through input.
- A task ends when whatever started it does.

```csharp
singleton Round
{
    int count;
}

event RoundStarted { }

async event(RoundStarted) Countdown(mut Round round)
{
    for (var i = 3; i > 0; i--)
    {
        round.count = i;
        await Wait.Seconds(1);
    }
    round.count = 0;
}

async int Doubled(int x)
{
    await Wait.Ticks(1);
    return x * 2;
}

system Start(Round round)
{
    if (round.count == 0) Doubled(2); // Starts a task; its value is dropped
}
```

### Provisional

Implemented, awaiting approval:

- `async` goes before a function or an event handler, before or after `local` if it has one (`local async event(...)`); before anything else it's an error that says where it goes. Methods can't be async yet, nor extern functions or functions that take an Action. `await` is a keyword, and binds like `try`: `await Doubled(3) + 1` adds 1 to the value. A `!` after an awaited call is the awaited value's: `await Fetch(name)!`.
- An async call is awaited, from async code (async functions and handlers), or a statement of its own, which starts a task; using its value without `await` is an error that says which to write. A started task's value is dropped; one that can fail is warned about as a call that can fail is, and `Load(name)!;` starts it without the warning. Only systems, views, handlers and async code start tasks: a plain function or method can't, as a task belongs to a world.
- `await` waits for an async call, or for `Wait.Ticks(n)` (the match's ticks; match code only), `Wait.Frames(n)` (this machine's frames; local code only) or `Wait.Seconds(s)`: in the match, the nearest whole number of ticks, at least one; in local code, this machine's time, which hosts give each frame (`tide_local_frame_time`) and a task counts down frame by frame, within a tenth of a millisecond. A wait of 0 or less doesn't wait. `Wait` only goes after `await`. `await` isn't allowed in a block written after a call.
- Async code is a state machine, as in C#: a call of an async function runs until it first waits, inside the code that calls it. An awaited call's frame is part of the caller's, so a task is one frame however deep it awaits, and an async function can't await itself (it can start itself again, as a task of its own).
- A task keeps its parameters and locals while it waits, copied, with their text and lists in its world's heap. An async function's component and singleton parameters aren't kept: it gets them again each time it goes on, as they are then, from the entity it belongs to, so the caller passes its own (a parameter), and a task whose entity lost one ends. Its `mut` parameters are only components and singletons; it can't keep the caller's other variables, and it has no `out` parameters: its call returns before it has made anything, so it returns a struct instead.
- An async handler is a task per event: its event is copied into the task, and its components are the entity's, got again after each wait.
- A plain function an async function calls counts too: one that sends a match event makes it match code, and one that sends a local event or a command makes it local (see Functions).
- An async function's world comes from what it does: changing the match (spawning, `mut` match components and singletons, sending match events, entity changes, `Snap`, `Wait.Ticks`) makes it match code, and local state (local components and singletons, `Session` calls, `Wait.Frames`, `Frame.dt`) makes it local. What it awaits or starts decides it too. One that does neither runs in whichever world starts it, and tidec makes it for each. Doing both is an error, as is starting or awaiting one from the other world. Tasks can't draw or read this frame's `Devices`.
- A task belongs to the entity the code that started it runs for, or with a task, to that task's entity, and ends when it's destroyed; code that runs for no entity starts tasks that belong to their world. A local task can belong to a match entity (a view of the match's entities started it), and ends with it, or when there's no match.
- Tasks go on at the end of the tick, after its changes and events, one after another in the order they started, those whose time has come; what they change applies after them, and their handlers can start more. Local tasks do the same at the end of the frame. A world keeps its waiting tasks in a table for each async function and handler, a row each: when it started, when it looks again, its owner and its frame.
- A system that starts tasks runs on one thread, and waits for the systems before it that start tasks, spawn or change text, as the code its tasks run until they first wait can do those. `--schedule` names it.
- Under `tide run`, a waiting task carries over to a build where its function's code, the code of what it awaits, and its frame's layout are the same; otherwise it's dropped, and tide says how many were.

### Open

- Awaiting an event: `await Hit` on an entity, which a task would wake for in the event's turn.
- Async C functions, which C finishes later (a service's answer), for local code.
- Holding a running call to await later, like C#'s `Task.WhenAll`: `var` could hold it without its type being written, as with errors.
- Cancelling a task from code, through a handle.
- Methods and functions that take an Action being async, and `await` inside a block written after a call.
- Local handlers of match events (see Events), which a local task awaiting a match event would need too.
- Whether what's written before an `await` in its statement is read before the wait, as in C#: `score.value + await Bonus()` reads `score.value` once the task goes on today, unless a call that writes comes after it in the statement (see Evaluation order).

## Input

### Decided

- One `input` declaration per game describes one player's input for one tick. It's what the network sends.
- Input is one value per player per tick. The engine writes it, and the simulation can only read it.
- `Owner` is a built-in component that ties an entity to a player. It's a normal component: it can be read, written, added and removed. Writing it hands control over. A view runs for the entities this machine's player owns with `with ownership` (see Views and drawing).
- Players are identified by a built-in `PlayerID` type, not an `int`. Like `Entity`, it's opaque, comparable with `==`, and has a null value. The simulation only sees `PlayerID`s and never connections, so a player who reconnects and gets their `PlayerID` back keeps everything they owned. `PlayerID(0)` names a player by index, for local play and tests.
- **Sampling is Tide code:** the input's `Sample(Devices devices)` method. The engine calls it on the client once per tick. Fields start at their defaults, and `Sample` assigns them by name, without `mut`. It runs outside the simulation: it reads this machine's `Devices`, and the local singletons it takes, but not the match.
- **Device access always takes a parameter**, `Devices devices`. Views and the input's `Sample` receive this machine's devices; match code receives the devices of the player who owns the entity, or the server's, as with an input parameter. The input sends what match code reads of them, and nothing else, and the engine repairs them, since it knows their ranges.
- The input stays, for what's worked out on the player's machine: an aim direction from the mouse, or anything that depends on local state. The match can't read positions on the screen (the mouse's, a touch's, the pointer's), which are in this machine's window; `Sample` works out what the match needs from them.
- Singletons stay parameters, in `Sample` too: `Sample(Settings settings)`.
- **Input carries whether buttons are held, not whether they just went down,** because a missing remote input is guessed by repeating the last one. On a device, `.pressed` means down at any point since the last sample, so a quick tap between ticks is never lost. That's between the ticks of a match this machine is in: with no match, or before this machine is in it, nothing is kept for a sample, so a match's first tick reads what's held as it starts, not what was pressed in a menu before it, nor how far the mouse moved there. In the simulation, `bool` input fields get `.down` and `.up`, computed against the previous tick, inside structs too (`input.aim.fire.down`).
- **A guess lets go after a while** (the owner asked for the limit, 2026-10-05: at a ping of 500 ms, corrections were wild; and agreed to what follows, 2026-10-06). A machine predicts other players' inputs, and the server's, by their last ones for 0.15 seconds past the last tick the server confirmed to it. In the ticks it predicts past that, each of those inputs is back at the input's defaults, as if its player had let go. So a guess that was wrong moves things by what a player does in 0.15 seconds at most, however far ahead a slow connection has a machine run.
  - Fields are let go unless they say otherwise. `[Absolute]` on a field says its default isn't rest, as it says where something is or which (an aim, a pointer's cell, a weapon chosen): a guess keeps it for as long as it goes on. A field that should have had it and doesn't shows at its default on machines that run further ahead than that.
  - `[Absolute]` takes no arguments and goes on the input's own fields, never a struct's, a component's or a singleton's. The name is the owner's, over `[Hold]`: it says what the field is, where `[Hold] bool fire` would read as a button that's held.
  - The 0.15 seconds are the engine's, with no setting for now. Hosts written in C have `tide_client_desc.guess_seconds`.
- An input parameter in a system gives the input of the player who owns the entity, and so does a `Devices` parameter. Entities without an `Owner`, or whose owner isn't a known player, get the **server's input**, and so do systems that run once per tick. This is how the server controls what no player owns. An input parameter doesn't filter entities: add `with Owner` to only run on owned ones.
- An input can have a `Sanitize()` method. Every input passes through it before the simulation reads it, including input from other players, so systems can rely on what it guarantees without checking again. It assigns the input's fields by name, like `Sample`, and reads nothing else.
- Input fields can declare bounds: `[Clamp(lo, hi)]`, `[Min(x)]` and `[Max(x)]`, and so can the fields of structs an input holds. The engine applies them to every input before `Sanitize`, so `Sanitize` only handles what they can't express. Bounds are constants; a number bounds every component of a vector. They go on input and struct fields only, for now: on a component's or singleton's field, they're an error.
- **Input is an attack point,** so the engine is forgiving with it. Before `Sanitize` runs, NaN and infinite floats become the field's default. Nothing a client sends can put NaN in the simulation, and `Sanitize` only deals with values that are merely out of range.
- `Devices` has a keyboard, a mouse, a gamepad, a touchscreen and the pointer; pen, joysticks and sensors come later. Every button has `.pressed` (held), `.down` (went down since the last sample) and `.up` (went up), named as in Unity: the Input System's `isPressed`, and the old `GetKeyDown` and `GetKeyUp`.
  - **Keyboard:** every key by physical position, named after the US layout (`keys.w`, `keys.space`, `keys.leftShift`, `keys.digit1`, `keys.upArrow`, `keys.f1`). WASD works on AZERTY. `text` is what was typed since the last frame, a `string`, which follows the keyboard's layout as keys don't. It's a frame's: views read it, and the functions they call. `Sample` and the match can't, and the input never sends it.
  - **Mouse:** `position`, `delta` and `scroll` (`float2`), and buttons `left`, `right` and `middle`. While its cursor is locked (see Views and drawing), `position` stays and `delta` is the mouse's own movement.
  - **Gamepad:** `connected`; `leftStick` and `rightStick` (`float2`); `leftTrigger` and `rightTrigger` (`float`, 0 to 1); face buttons by position (`buttonSouth`, `buttonEast`, `buttonWest`, `buttonNorth`); `dpad.up` and the other directions; `leftShoulder`, `rightShoulder`, `start` and `select`.
  - **Touchscreen:** `connected`, `primaryTouch`, and `touches`, a slot for each of 10 fingers, as Unity's. A `Touch` has `press` (a button: `.down` when the finger touched, Unity's Began, and `.up` when it lifted, Ended), `id` (Unity's `touchId`: the same while the finger touches, a new one for each touch), and `position`, `delta` and `startPosition` (`float2`). A finger keeps its slot while it touches, and one that touches while ten do is left out. `primaryTouch` is the finger that touched while no other was the primary one, until it lifts. A finger that touches and lifts between two samples reads as held for one, so no tap is lost. A finger is never the mouse, as in Unity's Input System.
  - **Pointer:** `position`, `delta` and `press`: the mouse, with its left button, or the primary touch, whichever was used last, as Unity's `Pointer.current`. Code that reads it works with a mouse and a finger alike.
  - **Axes follow Unity:** `y` is positive up for sticks, the mouse, touches and the pointer. Their positions are in window pixels from the bottom left; in a view, `Screen.Flip` gives them from the top left, as the screen and the GUI count (see GUI). `scroll.y` is positive when scrolling away from the user.

```csharp
input PlayerInput
{
    float2 move;
    bool jump;

    Sample(Devices devices)
    {
        var keys = devices.keyboard;
        if (keys.d.pressed) move.x += 1;
        if (keys.a.pressed) move.x -= 1;
        move += devices.gamepad.leftStick;
        jump = keys.space.pressed || devices.gamepad.buttonSouth.pressed;
    }
}

system Jump(PlayerInput input, mut Velocity velocity)
{
    if (input.jump.down)
        velocity.value.y = 5;
}

// The same without an input: the player's devices, as they read them.
system Hop(Devices devices, mut Velocity velocity)
{
    if (devices.keyboard.space.down || devices.gamepad.buttonSouth.down)
        velocity.value.y = 5;
}
```

```csharp
input PlayerInput
{
    [Clamp(-1, 1)] float2 move;
    [Min(0), Max(3)] int gear = 1;
    bool boost;

    Sample(Devices devices) { move = devices.gamepad.leftStick; }

    // After the bounds: what attributes can't say.
    Sanitize()
    {
        if (gear == 0) boost = false;
    }
}
```

### Provisional

- `input` is only a keyword at the start of a declaration, so it can still name parameters and locals.
- A player, or the server, whose input isn't set for a tick keeps their last one. Until then, inputs are the defaults.
- The input's defaults go through the same steps as any input: NaN repair, bounds, then `Sanitize`.
- The world keeps an input for each player a match can take: the game's `maxPlayers` setting, 16 without one (see Settings). A `PlayerID` past that is no one's: its input is the server's.
- A system can have one input parameter.
- Extra device members beyond the list above: mouse `back` and `forward`, gamepad `leftStickButton` and `rightStickButton`, and the full key list in `engine/include/tide/devices.h`.
- The types inside `Devices` are `Keyboard`, `Mouse`, `Gamepad`, `Dpad`, `Touchscreen`, `Touch`, `Pointer` and `Button`. Functions take them and `Devices` as parameters, read-only, passed without a copy: `float2 Steer(Gamepad pad)`.
- `touches` is a fixed-size device array, read-only, shown by tools as `Touch[10]`: `touches[i]` (an empty touch past the last, as forgiving as a list), `touches.count` (10: every slot, touching or not) and `foreach`, which goes through every slot. Ordinary fixed-size value arrays are implemented separately (see Arrays); device records remain read-only.
- `Sample` takes `Devices` and local singletons, read-only: `Sample(Devices devices, Settings settings)`. Either can be omitted when unused. Hosts pass the local state to `tide_input_sample`.
- `Devices` in views is this frame's: `.down` and `.up` since the last frame, and the `delta` of the mouse, touches and the pointer, and the mouse's `scroll`, too. What the GUI is using is hidden from views, as from `Sample`, and so is what another view claimed (see GUI). Helpers take the devices or a part of them explicitly; reading `keyboard.text` still requires a frame.
- `keyboard.text` is the characters typed since the last frame, with Shift, dead keys and a phone's keyboard applied, and what's pasted too, but for newlines and tabs. Backspace, Enter and the arrows are keys, not characters. It holds up to 32 characters a frame: the platform keeps the rest for the frames after, up to 1024, so a long paste comes over a few frames, as it does into the GUI's fields. It's empty while the GUI has the keyboard (a widget with the focus, a field being typed into, a modal). A function that reads it through a `Keyboard` or `Devices` parameter needs the frame, so `Sample` and systems can't call it. It can't be written, like the rest of the devices.
- A `Devices` parameter goes in `Sample`, views, systems and match event handlers, at most one per declaration. Functions and methods take the devices or their parts as explicit parameters. Bare `Devices` is a type, never a value. Local event handlers and tasks can't take devices.
- What the input sends of the devices comes from the whole program: each value read through a `Devices` parameter, in systems and handlers and in the functions and methods they call, and every value of a part used whole, like `var pad = devices.gamepad;`, but its positions in the window, which count once they're read. A `Touch` read through a function's parameter or a copy (a `foreach` over `touches`, `touches[i]` at an index that isn't a number) is sent for every place it could be: the primary touch, every slot, or both. A game without an `input` declaration gets one that only sends the devices.
- Buttons are sent as held (`.pressed`), like bool input fields: `.down` and `.up` in match code are against last tick's input, so a guessed input that repeats the last one doesn't press them again. A button let go and pressed again between two ticks is one press.
- Repairs: sticks -1 to 1 on each axis, with NaN 0; triggers 0 to 1; the `delta` and `scroll` of the mouse, and the `delta` of touches and the pointer, finite, or 0.
- On the server's machine, its own player's input is the server's too, so entities without an owner read it. A server with no player of its own keeps the defaults.
- What a guess is once it lets go (see Decided), in its details:
  - The 0.15 seconds are to the nearest tick. Past them, each guessed input is the input's defaults through the steps any input takes (repair, bounds, `Sanitize`), and the tick before is taken to have had the same, so no `.down` or `.up` is true for it, of a `bool` field or of a device's button.
  - The devices match code reads are let go with the rest: nothing of them is `[Absolute]`.
  - `[Absolute]` goes on a field of any type.
  - Only predicted ticks are guessed. The rule above stands for what the match is: a slot whose input isn't set for a tick keeps its last one, on the server and in every tick it confirms.
  - This machine's own input is never guessed, nor the server's on the machine that runs it.
  - Not picked yet: a limit of its own for a field, and anything between keeping and letting go, like an axis that fades.
### Open

- Pairing devices with players, for local multiplayer.
- Compact input types (bytes, quantized floats) to save bandwidth.
- The server's input computed from the game's state (AI) rather than from devices.

## Views and drawing

### Decided

- Drawing is immediate mode: code records drawing commands every frame.
- Views never change the match. They can change local state (see Local state).
- Views draw at whatever frame rate the game renders, not the tick rate, and see the match blended between its last two ticks, so motion is smooth even at 20 ticks per second. They draw up to a tick late for it.
- Floats are blended by default: every float, vector, quaternion and color a view reads of the match, `Time.t` among them. Ints, bools, enums, entities and text are as they are at the latest tick. `[Snap]` on a field keeps it out of blending, for angles that wrap and values that jump:

```csharp
component Body
{
    float2 position;        // Blended
    [Snap] float heading;   // Wraps from 360 to 0: as it is
    int lives;              // Ints are as they are
}
```

- Something that jumps, like a respawn, a portal or a camera cut, says so from match code: `entity.Snap()` or `singleton.Snap()`. For the tick it happens in, views draw it as it is instead of sliding from where it was.

#### Provisional correction smoothing

**Provisional.** `[Position]` and `[Rotation]` describe how a match field is presented when prediction is corrected. The names and contract may change as games test them.

```csharp
component Body
{
    [Position] float2 position;
    [Rotation] float heading; // Radians, for 2D
    float2 velocity;          // Ordinary tick interpolation
}

component Pose
{
    [Position] float3 position;
    [Rotation] quaternion rotation;
}
```

- `[Position]` applies to a float or floating-point vector. Tick interpolation stays linear. A correction accumulates the additive difference needed to preserve the displayed value, and the presentation removes that error over time.
- `[Rotation]` applies to a float in radians or a quaternion. A float interpolates and corrects through the shortest wrapped angular difference; a quaternion uses the shortest relative rotation. Its correction error decays toward zero angle or the identity rotation.
- Error is local to presentation and decays exponentially with frame time, with a provisional half-life of 75 ms. Further corrections accumulate with the error still present. Ordinary motion follows the corrected simulation; it is not low-passed along with the correction.
- Untagged fields keep ordinary tick interpolation. An ordinary struct can contain annotated built-in fields, including through nested ordinary structs. Correction smoothing through a type with its own `Interpolate` remains deferred; no companion custom-type method is agreed.
- `entity.Snap()` and `singleton.Snap()` remain immediate and clear the accumulated correction error. The retained errors change neither simulation values nor collisions, the simulation hash or serialized match state. No new correction-rate setting is agreed.
- The tags take no arguments and cannot be combined with each other or `[Snap]` on the same field. They describe match component and singleton fields, or ordinary struct fields held by them, not local state, inputs or events.
- A new match, a hot reload or a replacement world with no comparable presentation history clears the error. Host migration retains the frozen presentation while reconnecting, then clears the error when the new network timeline begins.

#### Other view rules

- A view filters its entities by whether this machine's player owns them, with `with ownership` and `without ownership`, beside `with` and `without`:

```csharp
view RenderGame(Player player, with ownership)
{
    Draw.SetCamera(player.pos, player.rot, 75);
}

view Nameplates(Player player, without ownership) { ... }
```

  - `with ownership`: the view runs only for entities that have the built-in `Owner` component and whose `Owner.player` equals `Session.player`, this machine's player.
  - `without ownership`: every other entity the view's other parameters and filters admit: entities with no `Owner`, and entities another player owns.
  - They're filters: they create no data dependency, and the view needs no `Owner` or `Session` parameter for them. It can still take either, and `with Owner` still means "has any owner": `with Owner, without ownership` is the entities whose `Owner` isn't this machine's player: another player's, or no player's. A view that takes both `with ownership` and `without ownership`, or `with ownership` and `without Owner`, is an error, as no entity is both.
  - A view with no component parameter runs once per entity through them, as through `with Player`: `view Hud(with ownership)`.
  - Only views have them. In match code (systems, the match's event handlers, tasks) they're errors, since the match has no "this machine": the message points to `with Owner`, the entities that have any owner, or `without Owner`. A local handler doesn't filter by them either: one of a match event takes `Owner owner` and `Session session` and compares them, and local entities have no owner.
  - Outside a match, a view with either doesn't run, like any view that reads the match.
  - `ownership` is a keyword only right after `with` or `without`, where it's never a component's name. Anywhere else it's a name like any other: a field, a local, a parameter or a declaration can be called `ownership`, and a qualified name starts with it as with any namespace (`with ownership.Flag`).
- **`verified`** before a parameter's type means "from the world the server has confirmed": one word with one meaning everywhere, a modifier like `mut`, and a keyword only there. A client's views draw the world as it predicts it, which can be wrong for a moment; what must not be wrong, like a health bar, a score or a death, is read `verified`.

```csharp
// Movement as predicted, the health bar only what's confirmed.
view DrawFighter(Body body, verified Health health) { ... }

// The same component twice: what's predicted, and what's confirmed so far.
view HealthBar(Health health, verified Health confirmed) { ... }

// A filter, and a singleton.
view Corpse(Body body, with verified Dead) { ... }
view Board(verified Duel duel) { ... }
```

  - A view runs for an entity when each parameter exists in the world it names. A view with components of both worlds needs the entity in both, so it skips one whose spawn isn't confirmed yet; a game that wants it drawn sooner splits the view in two. A view whose components are all `verified` runs for the confirmed world's entities.
  - "The same entity" in both worlds is the same entity, not only the same handle: a rollback can give a handle to another entity (see Local state, an entity after a rollback).
  - Filters take it too: `with verified Dead`, `without verified Dead`. So do singletons: `verified Duel duel`.
  - A `verified` value isn't blended: it's as the last confirmed tick has it.
  - In match code it's an error: the match is the same on every machine and has no "confirmed". Systems, match handlers, tasks and their queries say so, and point to views and local handlers.
  - With a predicted `Body`, an entity is gone from a view the moment the prediction destroys it, whatever a `verified` parameter beside it says. To hold a death until it's confirmed, a game makes the death a state of a living entity (a field or a tag), takes everything `verified` in that view, or shows it from `local event(verified Destroyed)`.
  - A local handler takes it on its event: `local event(verified Died died)` (see Events).
- Meshes are the general shape: `Draw.Mesh(vertices, indices)` draws triangles with a position, a texture coordinate and a color at each corner, three indices each, in order with other drawing calls. `Draw` also supplies cube, sphere, cylinder and plane helpers.
- `Texture` is a local, read-only image reference. `RenderTarget` is a local drawing destination whose `.color` is a `Texture`, whose other color images are `.colors[i]` where it holds several, and whose `.depth` a frag reads too (below). Target receiver methods (`scene.Clear`, `scene.SetCamera`, `scene.Mesh` and the other drawing methods) make the destination explicit: a target has every method `Draw` has, and nothing else. Ordinary `Draw` calls draw into `Draw.window`; the window has no sampleable `.color`. Each target has every setting of its own, shared across views, and switching passes preserves color and depth. A clear resets both within its clip.
- A render target's size is in logical pixels, like the window's, and its image is made at the display's scale: `RenderTarget(width, height)` creates one in local code, of a positive width and height. Drawing into a target is then the same as drawing into the window: `target.Screen()` is in logical pixels, text is fitted to the image's real pixels, shapes' edges are smooth over one of them, and conversions (`WorldToScreen` and the others), clips and a frag's `this.position` are in logical pixels.
  - A third argument is its pixels per unit, the display's scale when left out: `RenderTarget(320, 180, 1)` is exactly 320 by 180 pixels whatever the display, for pixel art and anything that must not depend on the display.
  - `RenderTarget.Window()` is a target that's always the window's size, and `RenderTarget.Window(factor)` one that's always that part of it: `RenderTarget.Window(0.5)` is half as wide and half as high. The engine makes it again when the window's size or the display's scale changes, and the game never calls `Resize` on it. What was drawn in it is lost then, which doesn't matter for a target a game draws again every frame.
  - When the display's scale changes (the window moves to another monitor, a browser zooms), targets at the display's scale are made again and lose their contents, as with a lost device. Targets with pixels per unit of their own aren't touched.
  - Nothing reads the display's scale: there's no `Screen.scale`.
  - A target's color and depth persist across frames otherwise. Copies, including `.color` references, share the image. `target.Resize(width, height)` changes the size of a target that isn't the window's to size, and discards its contents, even at the same size. `target.Dispose()` invalidates all aliases and releases its attachments; repeated disposal is harmless. A default target is invalid. These operations cannot run in field defaults or the match. Invalid destinations and disposed sources skip drawing with a diagnostic, including already-recorded uses of an image disposed in the frame. `Draw.window` cannot be resized or disposed. Hot reload retains managed sizes, pixels per unit, whether a target follows the window, and shared aliases, adopting their state natively and recreating their images on the web; borrowed references are cleared and rendered contents are discarded. Device loss discards GPU contents.
- **`Texture` is the one name a draw and a `Sample` take** (agreed 2026-10-07, item 3 of the reshaping of the drawing surface; built the same day). A `Grid2<Color>` converts to one implicitly where a draw or a `Sample` wants one, and so does a `Grid2<float>`, a texture of one channel (a heightmap, a lookup table). A `Texture` value carries its filter and its wrap, so `art.sprite = tiles` draws as `tiles` declared: the `[Filter]` and `[Wrap]` attributes are the sampling a `Texture` is born with, and a draw's or a `Sample`'s arguments an override, which leaves two places to say it where there were three. No fourth CPU image type. What was picked to build it is under Provisional, Textures.
- A grid stays the CPU's pixels: its cells can be local or match state, the ones that changed are uploaded again when they change (those cells, not the grid: see What's sent, under Lists and arrays on the GPU), and the texture it converts to is the grid, not a copy. Rendering into a target never writes a grid's cells.
- `Draw.Blit(source, destination[, blend], frag[, values])` has explicit source and destination; `values` is a struct the frag reads, this blit's own (see Provisional, A draw's values). It runs a frag taking `Vertex` and one read-only `Texture` source parameter over a full destination quad, replacing color and alpha (of as many of the destination's color images as the frag has outputs, where it holds several), preserving depth, and temporarily ignoring camera and clip (and the other draw settings, below, where it also takes a blend of its own). The source is a `Texture`, which a grid of colors or of floats is there. Sampling the destination's own image is an error. GUI and stats stay on the window. Target storage is recreated after resize, hot reload and device loss; its previous rendered contents are lost.
- **A target says what it holds when it's made** (agreed 2026-10-07), and never changes it. Every target has depth, which its 3D meshes read and write, and its format says what color image is beside it, if any: the color there always was, or none, for a target of depth alone. `RenderTarget(1024, 1024, 1, Format.Depth)` is one (the enum, its members' names and where the format goes are picks: see Provisional).
  - A target of depth alone is drawn into like any other: its `Clear`, its cameras, its clip and its `Mesh` are a target's. A color has nowhere to go there, so only what writes depth leaves anything: a clear, which sets it back to the far limit, and a 3D mesh that writes depth, with a frag or without. A mesh's frag still runs there, as it may leave pixels out (`this.Discard()`); what it writes to its color is dropped.
  - A draw with no frag keeps the engine's default look, and never means "depth only": what a draw writes is said, never implied. Here it's said by where it's drawn.
  - There's no setting for whether a draw writes color: a target of depth alone covers it.
- **A target holds several color images, a format for each** (agreed 2026-10-07): the formats are written after its size where it's made, one for each color image, four at the most, `RenderTarget(1024, 1024, Format.Rgba8, Format.Rgba16f)` and `RenderTarget.Window(Format.Rgba8, Format.Rgba16f)`. With none it holds one color image, as ever, and `Format.Depth`, by itself, none.
  - Each is read as a `Texture`: `target.colors[i]`, counted from 0 in the order of the formats, and `target.color` is the first.
  - A frag's outputs go to them in the order they're written (see Code the GPU runs, below). An output with no image in its place is dropped, as a color is in a target of depth alone, and an image past a frag's outputs is left as it was.
- **A color image can be floats** (agreed 2026-10-07): `Format.Rgba16f`, which keeps any number where `Format.Rgba8` keeps each channel from 0 to 1. It's what light that adds up past white wants, and numbers that aren't colors (a normal, a place).
  - It's used wherever a GPU draws into floats. OpenGL 3.3 and WebGPU do; OpenGL ES 3 and WebGL 2, the fallback, do where they have it, which is nearly everywhere, and that's the one thing the OpenGL backend asks of a GPU beyond what every one of them has.
  - On a machine that can't, a float image is 4 bytes a pixel, and the engine says so once: the game runs, with what's drawn there kept from 0 to 1.
- **A frag reads a target's depth,** as it reads its color: `target.depth` beside `target.color` (agreed 2026-10-07). Every target has it, with color or without, and `Draw.window` has none to read.
  - Read plainly, it's a number: how far the nearest thing drawn there is, from 0 at the camera's near plane to 1 at its far limit, as `this.depth` is, and 1 where nothing was drawn since a clear.
  - Read the second way, it's compared with a number of the frag's, which gives how much of what's stored there is further, from 0 to 1, filtered by the GPU: what a shadow's edge wants.
  - It's the same way up as the target's color, with the same `uv`, and the same number on every backend (to the GPU's own rounding, as everything a frag computes is).

```csharp
local singleton Lamp
{
    RenderTarget map;      // What's nearest the lamp: depth alone
    DepthTexture shadow;   // ...as frags read it
    float4x4 toLamp;       // From the world to the lamp's clip space
}

frag Lit(BoxVertex vertex, out Color color, Lamp lamp)
{
    float4 seen = Math.Mul(lamp.toLamp, float4(this.world, 1));
    float2 uv = float2(seen.x * 0.5 + 0.5, 0.5 - seen.y * 0.5);
    // 1 where nothing is nearer the lamp than this pixel, 0 in shadow, and between the two along a shadow's edge
    float lit = lamp.shadow.Compare(uv, seen.z * 0.5 + 0.5 - 0.01);
    color = Color(lit, lit, lit);
}

view Casters(Box box, Models models, mut Lamp lamp)
{
    // No frag, and the engine's own look: in a target of depth alone, its depth is what's kept
    lamp.map.Mesh(models.crate, models.faces, float4x4.Translate(box.pos));
}
```

- Filtering is how a draw reads the pixels, not part of them: `Draw.Mesh(vertices, indices, texture, Filter.Point)`. Left out, it's `Filter.Bilinear`, as Unity's default. `Filter.Trilinear` is the third (Provisional: see Mipmaps).
- **What a `uv` past 0 and 1 reads is chosen where a texture is sampled, as the filter is** (agreed 2026-10-07): it's how a draw reads the pixels too, so one texture is read one way in one draw and another way in the next, or both ways in one frag. How it's written, and each rule of it, are picks: see `Wrap`, under Provisional.
- **A texture's declaration can say how it's sampled, for wherever nothing else does** (agreed 2026-10-07, of `[Filter(Filter.Point), Wrap(Wrap.Repeat)]` before an `asset Texture` and an `asset Grid2<Color>`: "also add those attributes for both"). It's a default: a draw or a `Sample` that says its filter or its wrap still has what it says. It's the sampling the `Texture` made there is born with, which the value carries (agreed the same day, above). Where else the attributes go is a pick: see Declared sampling, under Provisional.
- **Code the GPU runs is written in Tide: `frag` and `vert`.** They're declaration kinds, like `view`: a `frag` runs once for each pixel a mesh covers, and a `vert` once for each of its vertices. Their signature says what they read, and the engine supplies it. Neither returns a value. A `Draw.Mesh`, or a target's `Mesh`, names them after its other arguments, the vert first, and both are optional.

```csharp
struct BoxVertex
{
    float3 position;
    float3 normal;
    float2 uv;
}

// Once per vertex: changes it in place, as a system changes a component
vert Wave(mut BoxVertex vertex, Time time)
{
    vertex.position.y += Math.Sin(time.t * 2 + vertex.position.x) * 0.1;
}

// Once per pixel: the triangle's three vertices blended at that pixel, and the pixel's color, which it writes
frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, Models models)
{
    float light = Math.Max(Math.Dot(Math.Normalize(vertex.normal), -sun.direction), 0.1);
    color = models.wood.Sample(vertex.uv) * box.tint * light;
}

view DrawBox(Box box, Models models)
{
    Draw.Mesh(models.crate, models.faces, float4x4.Translate(box.pos), Wave, Shade);
}
```

  - The first parameter is the vertex: a struct of the game's, or the built-in `Vertex` or `Vertex3`, which is what the mesh's list holds. A mesh draw that names a frag or vert whose vertex isn't its list's is an error. A vert takes it `mut` and changes it in place; the engine then applies the transform and the camera, unless the vert puts its vertex itself (below). A frag gets it blended across the triangle, and can't change it.
  - **A frag's outputs are `out` parameters, its color among them** (agreed 2026-10-07, the owner's idea): `out Color color` is the pixel's color. It starts with no value, as any `out` parameter does (see Out parameters): the frag writes it before it reads it, and on every path that ends, so a frag that leaves a path without writing its color is an error. A frag takes up to four, each a `Color`, for a target that holds as many color images: they go to them in the order they're written, the pixel's color first, and each is owed on every path that ends, as the first is (agreed 2026-10-07). An output of another type isn't there yet. What the frag wrote there is blended over what's behind it by its alpha, as meshes are: the frag writes the color itself, and the engine does what its blend needs of it (see Alpha and color space) (or as `Draw.Blend` says, times `Draw.Tint`: see Draw settings).
    - `mut Color color`, which is what a frag's color was, and which started as `Color.white`, is an error that names `out`, with the change as a quick fix. A frag that leaned on the white writes it: the editor offers `color = Color.white;` where a path writes nothing, and where the color is read first.
    - A vert's vertex stays `mut`: it's handed a vertex and changes it. `out` there is an error that says so.
  - **A vert hands its frag a struct of its own** (agreed 2026-10-07): an `out` parameter of the vert's, a struct, which the GPU blends across the triangle and the frag it's drawn with reads as a read-only parameter, so what a vert works out needs no field in every vertex of the mesh. Verts and frags stay declarations of their own, paired where a draw names them, and a frag that reads a struct the draw's vert doesn't write, or in a draw with no vert, is an error at the draw.

```csharp
struct FromLamp { float3 place; }

vert Place(mut Corner corner, out FromLamp lamp, Sun sun)
{
    lamp.place = corner.position - sun.at;
}

frag Look(Corner corner, FromLamp lamp, out Color color)
{
    color = Color.white * Math.Saturate(1 - Math.Length(lamp.place) * 0.1);
}
```

  - **A vert can put its vertex itself** (agreed 2026-10-08). The gap was found writing a sample that moves each corner onto the nearest pixel of a small frame, as a console of the nineties drew: a vert only changed its vertex in the mesh's space, so the sample went to the frame and back by hand, through two matrices a view kept and the inverse of the draw's transform at every vertex. Of three ways offered (a vert that says where its vertex lands, a draw setting that snaps, or the workaround documented), the owner took the first as recommended: "i trust your recommendation". A vert takes where its vertex goes as an `out float4`, in clip space, and `this.clip` is where the engine would have put it. With none, the engine places the vertex, as ever. Each rule of it is a pick: see Where a vert puts its vertex, under Provisional.

```csharp
// Each corner onto the nearest pixel of a frame of `lens.size` pixels
vert Snap(mut Corner corner, out float4 clip, Lens lens)
{
    float4 at = this.clip;
    float2 half = lens.size * 0.5;
    clip = float4(Math.Round(at.xy / at.w * half) / half * at.w, at.z, at.w);
}
```

  - The other parameters are bound as a view's are: components are those of the entity the view that draws runs for, and it's an error at the `Draw.Mesh` when that view doesn't guarantee its entity has one; singletons, the match's or local ones, and `Time`. None is `mut` or `out`. One read-only `Texture` parameter per stage may receive an explicit draw source; both stages receive the same source when both take it.
  - There are no individual plain value arguments to bind: what a frag or vert reads lives in a component, a singleton (local ones included), the ordinary instance struct supplied by `Draw.Instanced`, or the struct a draw gives after its vert and frag (agreed 2026-10-07: "a per-draw parameter struct for `Mesh` and `Blit`, bound as an `Instanced` draw's struct is, so a blur's direction isn't a singleton field written between two blits"; the rest is under Provisional, A draw's values).
  - A grid in what they take is a texture: `models.wood.Sample(uv)` on a `Grid2<Color>`.
  - With no vert, the vertices are used as they are. With no frag, a pixel is its vertex's `color` times the texture's at its `uv`, where the vertex struct has them: white, and untextured, where it doesn't. So a struct with only a `position` draws with no code of the game's, at 12 bytes a vertex.
  - 2D meshes take them too: a vertex struct whose `position` is a `float2` is drawn through the 2D camera or the screen, in order with the other 2D calls.
  - They call ordinary Tide functions, which then have to stay within what the GPU runs.
  - What the GPU computes differs between machines, so it never reaches the match: frags and verts are local code, as views are, and what `Math` makes of floats there is the GPU's own rounding, not Tide's deterministic functions. Only its integers, `Math.Hash`, are the same there as in the match.
- Draw settings (below) say how what's drawn comes out and where it lands: how it blends, what tints it, what moves it, the clip, the camera, and the screen's pixels in the camera's place.
- **The mouse's cursor is locked by asking, frame by frame.** `Cursor.Lock()` asks for the cursor's lock, for a camera that looks around. A view, or a function it calls, calls it every frame it wants the mouse, as with `GUI.ShowKeyboard()`: the lock is held while some view asked in the frame that just ended. When no view asks (a pause menu is up, the match ended), the cursor comes back on its own. There's no `Unlock`.
- `Cursor.locked` is whether the lock holds right now, a `bool`. It's what the platform says, not what was asked, since a platform may refuse the lock or drop it.
- While the cursor is locked, it's hidden, `devices.mouse.position` and the pointer's stay where they were, `devices.mouse.delta` keeps coming with no screen's edge to stop at (the mouse's own movement, without acceleration, where the system gives it), and the GUI sees no pointer: nothing hovers, and a click presses no widget.
- The match never calls `Cursor.Lock()` or reads `Cursor.locked`: the cursor is this machine's.
- Platforms differ, and games read `Cursor.locked` rather than assume. The usual pattern shows the pause menu when the lock goes away:
  - **The web** only gives the lock when it's asked for from a click or a key press, so the page asks at the next one while the game wants it, and at once when the game starts wanting it, which works shortly after one (a Resume button's click). The browser lets the cursor go itself when the player presses Escape, which a page can't prevent and may never see the key of, and when the page loses the focus or is hidden. For a moment after Escape it refuses the lock. Nothing asks again on its own: the player's next click or key press does.
  - **Desktop** windows hold the cursor while they have the focus. Alt+Tab lets it go, `Cursor.locked` is false meanwhile, and the lock returns with the focus if the game still asks. Escape lets it go too, as on the web, though the game still gets the key: while the game goes on asking, the player's next click on the window or press of another key locks it again, and Escape never does. Once a frame goes by without the game asking, that's over: its next ask locks at once.
  - **Android** never locks it, for now: `Cursor.locked` stays false.

```csharp
input PlayerInput
{
    float2 look;

    Sample(Devices devices)
    {
        if (Cursor.locked) look = devices.mouse.delta * 0.003;
    }
}

local singleton Menu
{
    bool paused;
    bool looking; // The window held the cursor last frame
}

view Look(Devices devices, Viewer viewer, mut Menu menu)
{
    Draw.SetCamera(viewer.position, viewer.rotation, 75);
    if (devices.keyboard.escape.down) menu.paused = true;
    if (!menu.paused) Cursor.Lock();
    // The lock went away on its own, as it does at Escape: the pause menu
    if (menu.looking && !Cursor.locked) menu.paused = true;
    menu.looking = Cursor.locked;
}
```

- A struct or component can say how it blends with an `Interpolate` override: `T Interpolate(T from, T to, float t)`, declared in it like an operator, with no value of its own. It replaces the default blend for that type wherever views see it:

```csharp
struct Angle
{
    float degrees;

    Angle Interpolate(Angle from, Angle to, float t)
    {
        mut var d = to.degrees - from.degrees;
        if (d > 180) d -= 360;
        if (d < -180) d += 360;
        return Angle { degrees = from.degrees + d * t };
    }
}

component Body
{
    float2 position; // Blended as usual
    Angle heading;   // Blended the short way round
}

event(Died dead) Respawn(mut Body body, Arena arena)
{
    body.position = arena.start;
    this.Snap(); // No sliding from where it died
}
```

#### Draw settings

- A setting changes how everything drawn after it comes out. Each has two forms, told apart by name. The plain name wraps a block, and puts back what it found when the block ends. The `Set` name stays for the draws that follow, in this view and the views after it, until it's set again or the frame ends:

```csharp
Draw.Blend(Blend.Add) { ... }      // Around some draws
Draw.SetBlend(Blend.Add);          // For the views that follow, as a camera is set for a frame
```

- The settings, all with the same two forms:
  - `Blend` and `SetBlend`: how colors go over what's there. `Blend.Alpha` (the default: by their alpha), `Blend.Add`, `Blend.Multiply` and `Blend.Replace`.
  - `Tint` and `SetTint`: fades or colors everything inside, multiplying its color and its alpha.
  - `Depth` and `SetDepth`: what 3D meshes do with depth: whether they're hidden behind what's nearer, and whether they hide what's drawn behind them. Only 3D meshes have depth: around 2D draws it changes nothing, and that's no error. Its modes' names and what each means aren't decided yet (see Provisional).
  - `Cull` and `SetCull`: which side of a 3D mesh's triangles isn't drawn. Each frame starts with the back of a triangle not drawn, as game engines have it. Only 3D meshes' triangles have a back: around 2D draws it changes nothing. Which side is the front, and the sides' names, aren't decided yet (see Provisional).
  - `Transform` and `SetTransform`: moves, turns and scales everything inside.
  - `Clip` and `SetClip`: only what's inside a rectangle draws.
  - `Camera` and `SetCamera`: the camera the draws go through.
  - `Screen` and `SetScreen`: the draws are in the screen's pixels, from the top left with `y` down, as the GUI's are. `Draw.Screen() { ... }` lets a view of the world draw a label in pixels, and leaves the camera as it was for the views after it. What views draw on the screen is under the GUI's widgets.
  - `Stencil` and `SetStencil`: what draws do with stencil, a whole number from 0 to 255 for each pixel, beside its depth. A draw compares the stored number with a number of its own, by a test it names, and isn't drawn where that fails; and it says what becomes of the stored number where it is drawn. It's there as it is on a GPU, and with none of a GPU's other stencil state: no separate state for each side of a triangle, nothing for what a failed depth test does, no bit masks. How it's written, its tests' and operations' names, whose it is and each rule beyond these aren't decided yet (see Provisional).
- A form written as the other is a compile error, whose message names the other: `Draw.Blend(x);` with no block points to `Draw.SetBlend`, and `Draw.SetBlend(x) { }` to `Draw.Blend`. The spellings from before settings had two forms are errors the same way: `Draw.Camera(...);`, `Draw.Camera(...);`, `Draw.Clip(rect);`, `Draw.Clip();` and `Draw.Screen();`, and each on a render target. The language server offers the rename as a quick fix.
- Every frame starts from the defaults. The GUI and the stats overlay are never drawn with a game's settings.
- A block that's left early (`return`, `try`, `fail`, `break`, `continue`) still puts back what it found, as a GUI container is still closed.
- Settings can be listed with commas before one block, with each other and with any other call that takes one (see Calls that take a block).

```csharp
view Sparks(Spark spark, Hud hud)
{
    Draw.Blend(Blend.Add),
    Draw.Tint(Color(1, 1, 1, spark.life))
    {
        Draw.Circle(spark.position, spark.radius, spark.color);
    }
    Draw.Screen()
    {
        Draw.Text(spark.name, Draw.WorldToScreen(spark.position), 14, Color.white);
    }
}
```

#### What the GPU side gains (agreed, built in part)

Agreed 2026-10-07. The aim is a complete set of raw pieces, so a game can build any technique itself (shadow maps were what started it), with no lighting or shadows of the engine's own. Nearly every technique draws into an image and reads that image in a later frag, so the pieces are what an image holds, what a draw's state is, what a frag reads and what it writes.

- **Built, where the rest of this list isn't yet:** a target says what it holds when it's made, a target of depth alone among them, and a frag reads a target's depth, plainly or compared with a number. Their rules are with the render targets' above (Views and drawing), and what was picked to build them is under Provisional (Target formats and depth).
- How `uv` wraps was agreed with these, and is built: see Decided above, and `Wrap` under Provisional.
- **A frag has more outputs than one color,** each an `out` parameter, into a target made to hold as many. Built: its rules are with the render targets' and with Code the GPU runs, above, and what was picked to build it is under Provisional (Several colors and float images).
- **A target's samples** (multisampling, agreed 2026-10-07 as item 8 of the drawing surface's reshaping: "MSAA on render targets, a `samples` argument", as 3D mesh edges have no antialiasing). Built: what was picked to build it is under Provisional (A target's samples).
- **Float color formats** were agreed as wanted with these (light past white, and positions and normals for deferred shading), and what the OpenGL fallback does about them was decided as they were built: see A color image can be floats, above.
- Built since: a frag's color as `out Color color` and a vert's struct for its frag, which are in the rules above (Code the GPU runs), which side of a triangle draws, which is among the Draw settings, above (`Draw.Cull`), and stencil, which is among them too (`Draw.Stencil`). Their picks are under Provisional.
- Built since: mipmaps, as a filter, `Filter.Trilinear`, with the level's derivatives asked as a frag starts so that code still samples in any branch. What was picked to build it is under Provisional (Mipmaps).
- Built since: cube maps, as a type of their own, `Cubemap`: an asset's six files, or six faces a view draws into, read by a direction. It was item 9 of the drawing surface's reshaping, the last. What was picked to build it is under Provisional (Cubemaps).
- Left for later: 3D textures, a depth bias as state (a frag subtracts one where it compares), and reading a target's pixels back into Tide.
- Open: a `shader Name { ... }` block that groups a vert and a frag under one name, maybe with the two unnamed inside. It would only add a name for a pair that's checked already, so it can come once naming two at a draw gets tedious.

### Provisional

- Alpha and color space (the owner picked both 2026-10-07, from a proposal with pictures: "go with your recommendations, we will polish if we find out it wasnt ideal after all"; built the same day). Decided: colors are premultiplied under the hood, with everything a game writes staying a color as it is; and colors are the display's values, blended as such, with nothing sRGB-aware. The rules below are picks made to build it, to confirm:
  - **A game never multiplies.** A `Color` is its color and how much of it there is, in a draw, a tint, a vertex, a grid's cell, a PNG, C's pixels and what a frag writes. The engine works the alpha into the color on the way to the GPU: in a texture's bytes (an asset `Texture`'s as the pack is built, a grid's and C's pixels as they're copied for the GPU), and in every pixel a shader writes, after its tint.
  - **`Blend.Alpha`** is the color, and what's there less the color's alpha; the alpha that's there becomes `a + d * (1 - a)`. **`Blend.Add`** adds the color, and raises the alpha that's there by its own of what was left to 1, so it never passes 1, in a float image either (an image whose alpha grew past 1 would read back darker than what was added). **`Blend.Multiply`** is as it was. **`Blend.Replace`** puts the color and its alpha in place of what's there, as an image keeps them: it isn't a way to write numbers with an alpha other than 1.
  - **`Sample` gives the color itself:** what the texture holds, blended between its pixels, divided by its alpha. A pixel with no alpha is `Color(0, 0, 0, 0)`. So a frag reads and writes colors as they are, and what's blended between an opaque pixel and a clear one is the opaque one's color with less alpha. A depth and a `Grid2<float>` are read as they were.
  - **A target holds each color times its alpha.** Drawn from (`target.color` in a draw), it goes over what's there as what was drawn into it would have. Onto the window, which has no alpha, a color replaced with an alpha under 1 shows darker by that much.
  - **What's lost:** the color of a pixel whose alpha is 0, and, in an image of bytes, the finer steps of a color that's nearly clear. An image whose alpha isn't how clear it is says so with `[Raw]` (below).
  - **`[Raw]` marks a texture as numbers** (built 2026-10-07, the owner having approved an attribute for it with the rest; its name and each rule here are picks). Before an asset that's one `Texture` or one `Grid2<Color>`, or on a field that's a `Grid2<Color>`: its pixels are kept as the file or the cells have them, with no alpha worked in, and a `Sample` gives them as they are, blended between pixels as numbers. **It's read by its name in a frag or a vert, and goes nowhere else:** drawing with it, giving it to a draw for a frag's `Texture` parameter, a blit's source, a `Texture` field, local, parameter or return are each an error. That's what keeps it out of everything that blends, with nothing for a `Texture` value to carry: whether a sample divides is known as the game is compiled. So it isn't on a glob's list or map (one of those has no name of its own to sample by), on a `Grid2<float>` (no alpha), a `RenderTarget` or a `Texture` field. The same file declared both ways is two images. Code reads a `[Raw]` grid's cells as any grid's.
  - **Not built:** a `[Raw]` texture as a `Texture` value (it would have to carry the mark to a frag handed it); C's own pixels as numbers. An sRGB `Format`, a texture marked as sRGB, and `Math` helpers between light and the display's values: a game that works in light converts in its own frags (see the users' page, Colors).
- What a frag and a vert write, where the agreed rules leave a choice (built 2026-10-07, to confirm):
  - **By type.** A vert's struct and the frag's parameter are matched by type, as a system finds a component: `out FromLamp lamp` in the vert, `FromLamp lamp` in the frag, whatever each names it. So a frag and a vert each take a struct type once.
  - **A vert can write several structs,** each its own `out` parameter, and a frag take any of them: nothing limits it to one but the 15 values a GPU carries from a mesh's corners to its pixels, which the draw checks. Only the fields a frag reads are carried, each one value: a field no frag reads costs nothing, and neither does a struct the draw's frag doesn't take, or a draw with no frag.
  - **What the struct holds** is what blends across a triangle: floats, their vectors and colors. A struct with an int, a bool, an enum, a matrix, a quaternion or a struct in it is an error where the vert takes it as `out`. A vertex's ints can't be read by a frag for the same reason, but a vertex can have them, as a vert reads them; a struct only a frag reads has no use for one. Passing an int unblended (the value of one of the triangle's corners) was left out: which corner differs between GPUs.
  - **The vert writes all of it:** each field, on every path that ends, a field at a time or the whole struct at once, before reading it (see Out parameters). `lamp = default;` fills it where there's nothing to give.
  - **A path that ends in `this.Discard()` owes the frag's color nothing,** as `fail` owes an `out` parameter nothing: no pixel is drawn.
  - **A frag's struct is the vert's or the instances'** (or, since, the draw's values: below). A frag's read-only struct was an element of the list `Instanced` draws; it's now that, or what the draw's vert writes, by its type, and a frag can take one of each. A draw whose vert writes a struct of the type its instances are is an error at the draw, as a frag couldn't tell them apart. A vert's read-only struct is an instance's or the draw's values.
- Where a vert puts its vertex (built 2026-10-08; that a vert can say where its vertex lands is decided, above, and the owner took the form recommended; every rule here is Claude's pick, to confirm):
  - **`out float4` is the place, by its type,** as a frag's `out Color` is its color: one a vert, whatever it's called, anywhere after the vertex. A second is an error. `mut float4` is an error that names `out`, with the change as a quick fix. An `out` of another plain type is the error it was (a vert hands its frag a struct), which for a float vector also names this.
  - **It replaces the engine's placing.** What the vert leaves in it is where the vertex is drawn, as it is: neither the draw's transform nor the camera is applied to it. A vert with none is placed by the engine.
  - **It starts with no value, and the vert writes it on every path that ends,** as a frag writes its color (see Out parameters): whole first, then a component at a time if it likes (`clip = this.clip; clip.z = clip.w;`). The editor's fix for a path that writes nothing is `clip = this.clip;`, which leaves the vertex where the engine would have put it.
  - **A vert's `this` is its vertex as the engine would place it,** as a frag's is its pixel. It has one field, `this.clip`, a `float4`: the camera times the draw's transform times the vertex's `position` as the vert has it at that line, so a vert that changes its vertex and then reads it gets the changed place. For a 2D mesh it's through the 2D camera or the screen, with `Draw.Transform` in it. It's read-only: assigning it, or a part of it, is an error that says to take `out float4`. `this` there has no methods, and none of a pixel's fields, each an error that says what it has; like a frag's, it can't be kept or passed whole, and a function a vert calls has none.
  - **A vert that only reads `this.clip`** (to hand its frag a depth, say) is placed by the engine.
  - **Clip space is `WorldToClip()`'s** (see The cameras as matrices, below): OpenGL's on every backend. `x / w` and `y / w` from -1 to 1 across the destination, y up, and `z / w` from -1 at the near plane to 1 at the far limit; tidec writes the conversion for WebGPU, as it does for the engine's own placing.
  - **A frag's `this.world` is still the vertex's own place in the world,** the draw's transform of its `position`, wherever its vert put it in the frame: light and shadows worked out from it don't move with the vertex. `this.position` and `this.depth` are where the pixel was drawn.
  - **Nothing more is sent,** as the camera and the transform are in the shader already, and a game with no such vert generates the C and the shaders it did.
  - **Argued:** writing it through `this` (`this.clip.xy = ...`) is shorter and is what one tries first. It was left an error: what a frag and a vert write is `out`, which their signatures then say; `this`'s fields are read-only everywhere else; and a field that's both the engine's place and the vert's own needs a rule for a vertex changed after it was written. A named type in the place of `float4` would say more, at the cost of a type for one value.
  - **Not there:** the place in the destination's pixels, as a frag's `this.position` and `this.depth` (it would make the snap `Math.Round` of a position, and needs the destination's size sent with the draw; a vertex behind the camera has no pixel); `this.world` in a vert, which is `Math.Transform(instance.transform, vertex.position)`; a setting that snaps every draw, which was the second way offered; a vert that drops its vertex.
- A draw's values, where the agreed sentence leaves a choice (built 2026-10-07, card 4 of the drawing surface's reshaping, to confirm):
  - **Where it's written: last, after the vert and the frag.** `Draw.Mesh(vertices, indices[, transform][, texture[, filter[, wrap]]][, vert][, frag][, values])`, `Draw.Instanced(..., instances[, ...][, vert][, frag][, values])`, a target's `Mesh` and `Instanced`, and `Draw.Blit(source, destination[, blend], frag[, values])`. After the names, as the owner's example has it, because the values are those names' arguments: an `Instanced` draw's list is before the texture because it's the mesh's (how many, and where), and the values are nothing to a draw that names no vert and no frag. A struct written before the names, or with no name at all, is an error that says where it goes.
  - **Any expression of a struct's type**: a literal, a local, a field, a call's result. Not `default`, and not a component's or a singleton's value.
  - **One struct a draw**, found by its type, as an instance's is: the vert takes it, the frag takes it, or both, each as a read-only parameter of that type, whatever they call it.
  - **It's sent once for the draw**, in the uniform block with what the code reads of singletons, and only the fields the vert and the frag read (each top-level field whole, as a component's). Unread fields can have types the GPU can't use. They count in the 3984 bytes a draw's uniforms hold.
  - **Draws join as they did**: the same mesh, shader and textures in a row with the same values (the bytes that are sent) are one run, so two draws that differ only in a field nothing reads are one, and other values start another. A Blit is always a draw of its own.
  - **Three sources, told apart by type, and never two of one type.** A frag's read-only struct is what the draw's vert writes, an element of the list an `Instanced` draw is given, or the draw's values; a vert's is one of the last two. A draw whose values are of the type its vert writes, or of the type its instances are, is an error at the draw. A frag or vert that takes a struct none of the three supplies is an error at the draw whose note names all three. So is a draw that gives a struct neither its vert nor its frag takes: the value would go nowhere.
  - **A vert takes any number of read-only structs** now, where it took one (the instances'): which of them a draw supplies is checked at the draw, as a frag's are. The declaration's own error ("a vert takes one instance struct") is gone.
  - **No texture in it.** A `Texture`, a `Grid2<Color>` or a `DepthTexture` field of a draw's values can't be sampled, as an instance's can't: an error that says to keep it in a component or a singleton. A draw's image is its `Texture` argument.
  - **Not there:** values for a draw with no vert and no frag (nothing would read them); several structs a draw; a value that isn't a struct (`Draw.Blit(..., Blur, float2(1, 0))`), which would bind by position where everything else binds by type.
  - **In the shaders,** a frag's color still starts as white where the GPU's compiler sees it, so the text tidec writes is what it was: the checker is what knows it's written first.
  - `mut` on a vert's struct is an error that names `out`, with the quick fix, and so is `mut out` in either.
- One `Draw` (built 2026-10-07; that there's one `Draw` with every method a render target has and nothing else, that the 2D and 3D forms are overloads told apart by their arguments, that the window is the same object a target is, that every setting belongs to the destination it's called on, blend and tint too, that depth and cull are for 3D meshes alone, and that the old spellings are errors with the rename as a quick fix are decided, above; what follows was picked to build it, to confirm):
  - **A mesh's dimension is its vertices'.** A `List<Vertex>`, or of a struct with a `float2 position`, is a 2D mesh's, drawn where its vertices are through the 2D camera; a `List<Vertex3>`, or of a struct with a `float3 position`, is a 3D mesh's, which takes its transform after its indices. A 3D mesh with no transform, and a 2D mesh with one, are errors that say so. An empty list literal, `[]`, says nothing: it's a 2D mesh's, unless a transform follows its indices.
  - **`Draw.SetCamera()` puts both cameras back**, the 2D and the 3D, and `Draw.SetTransform()` both transforms, as a target's always did: with one name for both, nothing says which. One alone goes back by setting it: `Draw.SetCamera(float2(0, 0), 0)` is a pixel a unit again, and `Draw.SetTransform(float4x4.identity)` no 3D transform.
  - **Every `Draw` call selects the window**, as a target's methods select the target: `Draw.Blend` and `Draw.Tint` too, which selected no destination while they were global. That's the one change to the C generated for a game that was only respelled: those two calls (and their `Set` forms) are made on the window's list.
  - **What the window has that a target hasn't** is `Blit`, which names the target it goes into (`target.Blit` is an error that says so), and the members `window` and `list`. A target has everything else of `Draw`'s, and nothing else.
  - **Depth and cull around 2D draws alone change nothing, and are no error** (decided): the setting is recorded and reaches no command. Only 3D meshes have depth or a back.
  - **`Draw2D` and `Draw3D` stay reserved names**, which a game can't declare: that's what lets `Draw2D.Circle(...)` be the error "Draw2D.Circle is 'Draw.Circle' now", with the quick fix on the owner's name, and `Draw2D.list` the same for `Draw.list`.
  - **Editors list every version of an overloaded function** on hover, with each one's doc once: `Draw.SetCamera` shows its four forms.
- The cameras as matrices (built 2026-10-07, card 5 of the reshaping of the drawing surface. That a view reads the matrices of the camera a destination has, the window's and a target's, so a shadow map's frag isn't handed a matrix the game built by hand, is the owner's; the shape, the names and each rule are Claude's picks, not yet confirmed):
  - **Four functions of `Draw`, and so of a render target**, with no arguments, beside the conversions they belong with: `Draw.WorldToClip()`, `Draw.CameraTransform()` and `Draw.CameraProjection()`, each a `float4x4`, for the 3D camera, and `Draw.WorldToScreenMatrix()`, a `float3x3`, for the 2D one. Functions, and not a built-in struct read as a member (`Draw.camera3D`): a target has every function `Draw` has and none of its members, so a function is on both with no rule of its own, and a struct of three matrices would be worked out whole for the one that's read.
  - **They read the destination's camera as it's set when they're called**, in this frame: what a `SetCamera` left, or the block they're in, or the camera each frame starts with. Nothing is recorded for the asking.
  - **`WorldToClip()` is the matrix the renderer multiplies a 3D mesh's positions by**: the camera's transform undone, z turned around, the projection, and for a camera set with a field of view, the fitting to the destination's shape (x divided by its width over its height). Clip space is OpenGL's on every backend: `z / w` from -1 at the near plane to 1 at the far limit, so a depth as a target stores it is `z / w * 0.5 + 0.5`. The fitting is one function the renderer and these share.
  - **`CameraTransform()` and `CameraProjection()` are the arguments of `Camera(transform, projection)`**: given back to it, they set the same camera. A camera set that way gives them back bit for bit. One set with a position, a rotation and a field of view gives its position and its rotation, normalized, as a matrix with no scale, and the engine's projection (from 0.3 units on, with no far side), fitted to the destination's shape as `WorldToClip()` is.
  - **`WorldToScreenMatrix()`** does to `(x, y, 1)` what `WorldToScreen(float2)` does: into logical pixels from the top left, y down, so it has the destination's size in it. The identity inside `Screen`. A `float3x3`, as `Draw.Transform` takes one.
  - **A transform is no part of them**, as it's none of the conversions.
  - **A destination with no size** (a target that was disposed) has zeros for `WorldToClip()`, `CameraProjection()` and `WorldToScreenMatrix()`, as the conversions give zeros there. A camera that's singular or not finite is given as it is: it's what the renderer would draw through.
  - **Local code only, as the conversions are**, and not in code the GPU runs: a view works a matrix out and keeps it where its frag reads it.
- One word for the window, and formats by what they store (built 2026-10-07; that the target which follows the window is `RenderTarget.Window()` and `RenderTarget.Window(factor)`, that drawing in the screen's pixels is `Draw.Screen() { ... }` and `Draw.SetScreen()`, that `Screen` is the window's measurements alone (`Screen.width`, `Screen.height`, `Screen.Flip`), that `Draw.window` stays, that depth alone is `Format.Depth`, and that formats are named by what they store are decided; what follows was picked to build it, to confirm):
  - **The formats are `Format.Rgba8`, `Format.Depth` and `Format.Rgba16f`.** A color format's name is its channels, the bits of each, and `f` where they're floats: four channels of a byte each, and four floats of 16 bits. So the ones to come have their names already (`R8`, `Rg8`, `R16f`, `Rgba32f`), with no word to find for each. `Depth` says no bits: what a target's depth has is the GPU's (24 bits and 8 of stencil today), and nothing reads it by its size.
  - **No `Format.Color` beside `Format.Rgba8`.** One name for one thing: an alias would be a second spelling of the same number in every example, and the question of which to write. What it would have been friendlier for is the first target someone makes, and that one says no format at all: `RenderTarget(width, height)` holds one `Rgba8` image.
  - **The members' numbers are what they were** (`Rgba8` 0, `Depth` 1, `Rgba16f` 2): they're in a target's id, so a target made by a build from before is the same format to a build from after.
  - **The old spellings are errors that name the new one**, with the rename as the editors' quick fix: `RenderTarget.Screen`, and `Format.Color`, `Format.DepthOnly` and `Format.Float`; and `Draw.Pixels` and `Draw.SetPixels` (a target's own too), the names this card gave the screen's pixel space for an afternoon. They went back to `Screen` and `SetScreen` the same day (the owner having left the review's follow-ups to Claude): with the target named `Window`, `Screen` means one thing, the screen's pixel space, as `Screen.width` and `Screen.Flip` do; and `Pixels` sat beside a frag's `Pixel` parameter as two names that read as one.
  - **A target's own pixel space is `target.Screen()` and `target.SetScreen()`**, as every setting of `Draw`'s is a target's method: the target's logical pixels, from its top left.
  - **In C the formats are `TIDE_TARGET_RGBA8`, `TIDE_TARGET_DEPTH` and `TIDE_TARGET_RGBA16F`** (`tide/draw.h`), renamed with them. The functions keep their names (`tide_render_target_window`, `tide_render_target_screen` for a part of it, `tide_draw_set_screen`), so a game that was only respelled generates the C it did, but for the enum's members in a game that names `Format`.
- Draw settings, where the decided rules above leave a choice (to confirm):
  - **Where each lives.** Every setting belongs to the destination it's called on (decided, with one `Draw`): `Draw.Blend`, `Draw.Tint`, `Draw.Transform`, `Draw.Clip`, `Draw.Camera`, `Draw.Screen`, `Draw.Depth`, `Draw.Cull` and `Draw.Stencil` are the window's, and a render target has each as a method, in both forms (`target.Blend(Blend.Add) { ... }`, `target.Clip(rect) { ... }`, `target.SetCamera(...)`), its own for the frame. So a blend or a tint set for the window doesn't reach a target's draws, nor a target's the window's, as a clip or a transform never did. The 2D and 3D forms are told apart by their arguments, on the window as on a target.
  - **Putting a `Set` back.** A `Set` with no arguments sets what each frame starts with: `Draw.SetBlend()` is `Blend.Alpha`, `Draw.SetTint()` no tint, `Draw.SetStencil()` no stencil test, with nothing stored, `Draw.SetDepth()` is `Depth.ReadWrite`, `Draw.SetCull()` is `Cull.Back`, `Draw.SetClip()` no clip, `Draw.SetTransform()` no transform, 2D or 3D, and `Draw.SetCamera()` both first cameras: the 2D one with the origin at the middle of the screen, a world unit a pixel (which is also how to leave the screen's pixels), and the first 3D one. A target's are the same, each the target's own. `Draw.NoClip()` is gone: it's `Draw.SetClip()`.
  - **Nesting.** A block's setting is inside the one it found, where that means something: a clip in a clip is what of its rect is inside the outer one, a tint multiplies the tint there is, and a transform is inside the transform there is. A blend, a depth, a cull, a stencil, a camera and the screen have nothing to be inside, and replace what's there. A `Set` sets outright, in a block too: `Draw.SetClip(rect)` inside a `Draw.Clip` block isn't inside its clip, and the block still puts back what it found when it ends.
  - **`Blend`** is a built-in enum: `Alpha`, `Add`, `Multiply`, `Replace`. A game can't declare the name outside a namespace. In each, a color's alpha is how much of it there is: `Add` adds the color times its alpha, as light adds, and `Multiply` multiplies what's there by the color, by less as its alpha goes to 0, so what's clear (a shape's smooth edge, the gaps in text) changes nothing. `Replace` writes the color and its alpha as they are. In a render target, `Add` also adds its alpha to the one there, and `Multiply` leaves that one alone.
  - **`Tint`** takes a `Color`. It multiplies shapes' and text's colors, a mesh's corners', a 3D mesh's, and what a `frag` wrote, alpha included.
  - **`Depth`** (to confirm: the enum's name and its members', and each rule here; that it's a draw setting for 3D meshes alone is decided). `Draw.Depth(mode) { ... }` and `Draw.SetDepth(mode);` say what the 3D meshes drawn inside, or after, do with depth. `Depth` is a built-in enum with three members, and a game can't declare the name outside a namespace:
    - `Depth.ReadWrite`: a mesh is hidden behind what's nearer, and hides what's drawn behind it later, whatever the order. It's what each frame starts with, what `Draw.SetDepth()` puts back, and what 3D meshes always did.
    - `Depth.Read`: a mesh is hidden behind what's nearer and already drawn, and writes no depth, so it hides nothing: for what's see-through (glass, smoke, a shadow or a mark lying on a surface). Meshes drawn so go over each other in the order they're drawn, as 2D calls do, and are drawn after what's solid.
    - `Depth.None`: a mesh neither reads nor writes depth: it draws over whatever is there, and hides nothing drawn after it.
    - There's no mode that writes depth without reading it.
    - **It's a destination's own**, as every setting is: `Draw.Depth` and `Draw.SetDepth` are the window's, and a render target has `target.Depth(mode) { ... }` and `target.SetDepth(mode);` for its meshes, each target's its own for the frame. So `Draw.Depth(Depth.Read) { target.Mesh(...); }` leaves the target's mesh as the target has it, and `target.SetDepth()` puts the target's back. Each target has its own depth, as before, which a mesh drawn into it reads and writes as its setting says.
    - **It's for 3D meshes of every kind**: `Draw.Mesh`, the primitives, a mesh through a `frag` or a `vert` whose vertex has a `float3 position`, and `Draw.Instanced`, and a target's.
    - **The 2D calls have no depth**, whatever is set: shapes, text, 2D meshes, and 2D meshes through a `frag` neither read nor write it, and draw over what's there in order. Nothing gives a 2D call a depth; that would come with layers (see Open). `Draw.Depth` around 2D draws alone changes nothing, and isn't an error.
    - **`Draw.Clear`, and a target's `Clear`,** clear depth whatever is set, within the clip. **`Draw.Blit`** leaves depth as it is, whatever is set.
    - A block's depth replaces the one it found, and puts it back when it ends.
    - Instances drawn in a row with the same depth are still one draw, with the setting around each or around them all; another depth is another draw.
    - A `Depth` is a value like any enum's: a field, a local, a parameter. One made of a number that's none of the three (`Depth(40)`) is `Depth.ReadWrite`.
    - In generated C the enum is `Depth`, with `Depth_ReadWrite`, `Depth_Read` and `Depth_None`, only in a game whose code names it: the others' headers, and so their layout's hash, are what they were.
  - **`Cull`** (to confirm: which side is the front, the enum's name and its members', and each rule here; that it's a setting for 3D meshes alone whose default leaves the back out, and that nothing in 2D culls, are decided). `Draw.Cull(side) { ... }` and `Draw.SetCull(side);` say which side of the triangles of the 3D meshes drawn inside, or after, isn't drawn. `Cull` is a built-in enum with three members, and a game can't declare the name outside a namespace:
    - `Cull.Back`: a triangle seen from the back isn't drawn. It's what each frame starts with, and what `Draw.SetCull()` puts back.
    - `Cull.Front`: a triangle seen from the front isn't: for the inside of a shell.
    - `Cull.None`: both sides are drawn: for what's flat, or see-through.
    - **The front of a triangle is the side its corners go clockwise on,** in the order its indices list them, as Unity's. Seen by a camera that looks along `+z`, with `+x` to its right and `+y` up, a triangle whose corners go clockwise on the screen shows its front.
    - **It's the mesh's own, whatever it's drawn through.** A transform that mirrors a mesh (its determinant is under 0) turns its triangles round on the screen, and its front is still its front: the mesh's or the instance's own transform, `Draw.Transform`'s, and a camera that shows the world mirrored. A camera given as matrices is one when its `transform` mirrors, or its `projection` isn't the way round OpenGL's are (the identity's looks back along `z`), and not both.
    - Instances that are mirrored and ones that aren't, drawn in a row, are draws apart: with `Cull.None` they're one. Instances drawn in a row with the same cull are still one draw, with the setting around each or around them all.
    - **A transform that flattens a mesh** (its determinant is 0) leaves it no front: the triangles of both of its sides land on one plane, wound opposite ways, and each is still drawn or not by which way it's wound there. A game draws such a mesh inside `Cull.None`.
    - **A `vert` that turns triangles round itself** (it mirrors its mesh, or turns it inside out) is the game's to draw for: the engine goes by the transforms it's given. Such a mesh is drawn inside `Cull.Front`.
    - **It's a destination's own**, as depth is: `Draw.Cull` and `Draw.SetCull` are the window's, and a render target has `target.Cull(side) { ... }` and `target.SetCull(side);`. A triangle's front is the same side in a target as in the window.
    - **It's for 3D meshes of every kind**: `Draw.Mesh`, the primitives, a mesh through a `frag` or a `vert` whose vertex has a `float3 position`, and `Draw.Instanced`, and a target's.
    - **The primitives are solid**: a cube's, a sphere's and a cylinder's fronts are outside, and a plane's is its top, its local `+y`. A plane seen from below isn't drawn.
    - **Nothing in 2D has a back**, whatever is set: shapes, text, 2D meshes, 2D meshes through a `frag`, `Draw.Clear` and `Draw.Blit` draw whichever way their triangles are wound. `Draw.Cull` around 2D draws alone changes nothing, and isn't an error.
    - A `Cull` is a value like any enum's. One made of a number that's none of the three (`Cull(40)`) is `Cull.Back`.
    - In generated C the enum is `Cull`, with `Cull_Back`, `Cull_Front` and `Cull_None`, only in a game whose code names it, as `Depth` is.
  - **Stencil** (built 2026-10-07; to confirm: how it's written, the two enums' names and their members', whose it is, and each rule here. That it's there as on a GPU, that it's a draw setting with both forms, and that it has none of a GPU's other stencil state are decided). `Draw.Stencil(test, value) { ... }`, `Draw.Stencil(test, value, operation) { ... }` and `Draw.SetStencil(...)` with the same arguments say what the draws inside, or after, do with stencil:
    - **`StencilTest`** is a built-in enum of the test a draw passes to be drawn at a pixel: `Always` (no test: what each frame starts with), `Never`, `Less`, `Equal`, `LessEqual`, `Greater`, `NotEqual`, `GreaterEqual`. **The draw's `value` is on the left and the stored number on the right,** as a GPU has it and as Unity's `Comp` does: `StencilTest.Less` draws where `value` is less than the stored number.
    - **`StencilOp`** is a built-in enum of what becomes of the stored number at each pixel the draw is drawn at: `Keep` (what a call that names none has), `Replace` (the draw's `value`), `Increment` (one more, staying at 255), `Decrement` (one less, staying at 0), `Zero` and `Invert` (255 less it: each of its 8 bits the other way). The two that count don't wrap round; a GPU's wrapping ones are left out.
    - A game can't declare either name outside a namespace.
    - **`value` is an `int`,** any expression: under 0 it's 0, and over 255 it's 255. The test and the operation are values like any enum's: a field, `inside ? StencilTest.Equal : StencilTest.NotEqual`. One made of a number that's no member's is its first (`Always`, `Keep`). The arguments take names (`test:`, `value:`, `operation:`).
    - **It's a destination's own,** as every setting is: `Draw.Stencil` and `Draw.SetStencil` are the window's, and a render target has `target.Stencil(...) { ... }` and `target.SetStencil(...)`. `Draw.Stencil(...) { target.Rect(...); }` leaves the target's rect as the target has it. Shapes, text, 2D meshes and 3D meshes all take it.
    - **What each frame starts with:** no test, and nothing stored (`StencilTest.Always` with `StencilOp.Keep`), which is what `Draw.SetStencil()` puts back. The window's stored numbers are all 0 as a frame starts. A render target keeps its own from frame to frame, as it keeps its color and its depth, and a new one's are 0.
    - **A draw is drawn, and stores, at the pixels it touches, whatever its color.** A clear color (`Color.clear`) stores and leaves the colors there as they are, under every blend but `Blend.Replace`: that's how a mask is laid down with nothing painted, as there's no setting for whether a draw writes color. What a draw touches: all of a rect, a circle and a smooth shape out to the pixels their edges cover any part of, text where its glyphs are and not the rest of their boxes, all of a mesh's triangles whatever its texture's or its corners' alpha, and for a mesh through a frag, the pixels the frag doesn't discard.
    - **Stencil has no part pixels:** a pixel is in a mask or isn't. So what's drawn where a mask is has a stepped edge where the mask's is round. Where a draw only tests (`StencilOp.Keep`), its own edges are as smooth as ever.
    - **Where a draw isn't drawn, nothing is stored:** where its test fails, and for a 3D mesh, where it's hidden behind what's nearer.
    - **It's for everything that draws:** shapes, text, 2D meshes, 3D meshes and the primitives, with a frag or a vert or without, and `Instanced`.
    - **`Draw.Clear`, and a target's `Clear`,** set the stored numbers back to 0, within the clip, whatever is set, and nothing tests them: a clear under `StencilTest.Never` still clears. **`Draw.Blit`** neither tests nor stores, whatever is set.
    - A block's stencil replaces the one it found, and puts it back when it ends.
    - Draws in a row under the same stencil are still one draw, with the setting around each or around them all; another stencil is another draw. A test and a number nothing reads are no stencil at all: `Draw.Stencil(StencilTest.Always, 9)` is none, and costs nothing.
    - **A target of depth alone has stencil too.** What stores it is drawn there, shapes, text and 2D meshes among it, though their colors go nowhere; what only tests it and writes no depth still leaves nothing.
    - **The same pixels on every backend.**
    - In generated C the enums are `StencilTest` and `StencilOp`, with `StencilTest_Always`, `StencilOp_Keep` and the rest, only in a game whose code names them, as `Depth` is: the others' headers, and so their layout's hash, are what they were.
    - Not there: reading a pixel's stencil in a frag or back into Tide; clearing stencil alone (a draw under `StencilOp.Zero` does); the counting operations that wrap; and the GPU state that's decided against (above).
  - **`Wrap`** (to confirm: the enum's name and its members', and each rule here; that how `uv` wraps is chosen where a texture is sampled, as the filter is, is decided). `Wrap` is a built-in enum of what a `uv` past 0 and 1 reads, and a game can't declare the name outside a namespace, for a type or for a function:
    - `Wrap.Clamp`: the cells at the texture's edge. It's what a draw or a `Sample` that says no wrap has, and what every one did.
    - `Wrap.Repeat`: the texture again, so 1.25 reads what 0.25 does, and -0.25 what 0.75 does: tiles, and what scrolls.
    - `Wrap.Mirror`: the texture again, the other way round each time, so 1.25 reads what 0.75 does and 2.25 what 0.25 does: no seam where its edges don't match.
    - **It's an argument after the filter, wherever the filter is one**: `Draw.Mesh(vertices, indices, texture, Filter.Point, Wrap.Repeat)`, `Draw.Mesh(vertices, indices, transform, texture, filter, wrap)`, `Draw.Instanced(vertices, indices, instances, texture, filter, wrap)`, a render target's `Mesh` and `Instanced`, and in a frag or a vert `source.Sample(uv, Filter.Point, Wrap.Repeat)`.
    - **A wrap is written with its filter**: what a call leaves out is at its end, so `Draw.Mesh(vertices, indices, texture, Wrap.Repeat)` and `Sample(uv, Wrap.Repeat)` are errors that say to write the filter before it, and that a texture which only wraps another way says so where it's declared (`[Wrap(Wrap.Repeat)]`). `Sample` takes its arguments by name too (`uv:`, `filter:`, `wrap:`), with none left out before one that's given.
    - **One wrap for both axes.** A second is an error.
    - **In a mesh draw it's a value, as the filter is**: a field, a local, `tiled ? Wrap.Repeat : Wrap.Clamp`. One made of a number that's none of the three (`Wrap(7)`) is `Wrap.Clamp`. **In `Sample` it's written there, as the filter is** (`Wrap.Repeat`, never a variable): the GPU is told how a texture is read before any pixel runs.
    - **A draw's filter and wrap are its texture's for that draw**, whichever way the mesh is drawn: the engine's own vertices, a vertex struct of the game's with no frag, and a frag or a vert that takes the texture as its `Texture` parameter, whose `Sample`s that say nothing then read it so (until 2026-10-07 a draw through a frag took neither, an error).
    - **Each way a texture is read in a frag or a vert is one of its draw's 8 textures**: one sampled with another filter or another wrap counts again, and the same way again doesn't. For a `Texture` parameter or field, a `Sample` that says nothing and one that says the default are two ways, as the texture may say otherwise.
    - **It's for every texture**: a `Grid2<Color>`, an asset's, a `Texture`, a render target's color, of any size (not only powers of two). A grid with no size still samples white.
    - **Blended** (`Filter.Bilinear`), a cell at the texture's edge is blended with what the wrap says is past it: with `Wrap.Repeat` the cell at the opposite edge, and with `Wrap.Clamp` and `Wrap.Mirror` itself.
    - **The same pixels on every backend.**
    - Meshes drawn in a row with the same texture, filter and wrap are still one draw; another wrap is another draw.
    - Text and shapes sample nothing a game gives them, and have no wrap. `Draw.Blit` takes none: its frag samples its source as the source says, and as the frag says where it does.
    - In generated C the enum is `Wrap`, with `Wrap_Clamp`, `Wrap_Repeat` and `Wrap_Mirror`, only in a game whose code names it: the others' headers, and so their layout's hash, are what they were.
    - Not there: a wrap for each axis, a color for what's past the edges (a border), and a texture that mirrors once and then clamps.
  - **Declared sampling** (to confirm: each rule here; that a texture's declaration can say its filter and its wrap, with attributes, for a `Texture` asset and a `Grid2<Color>` asset, is decided). `[Filter(Filter.Point)]` and `[Wrap(Wrap.Repeat)]`, each alone or both in one pair of brackets, say how the texture made there is sampled wherever a draw or a `Sample` leaves that out:
    - **Where they go**: before an asset that's a `Texture` or a `Grid2<Color>`, alone or a glob's list or map of them, whose every image they're for; before a field of a component or a singleton that's a `Grid2<Color>` or a `Grid2<float>` (not asked for: a grid that's drawn is a texture declared in a field, and it would have been the one texture that couldn't say); and before a `RenderTarget` field, for the target's color images, `target.color` and `target.colors[i]` (not asked for either, by the same reasoning: it's the third place a texture is made). Anywhere else is an error that says where they go: another declaration, an asset that's text or bytes, a field that's none of those, the same one twice.
    - **Not on a `Texture` field**, which is an error of its own: such a field holds whichever texture it's given, which says how it's sampled itself.
    - **Each takes one member of its enum, written there**: `[Filter(Filter.Point)]`, `[Wrap(Wrap.Mirror)]`. Not a variable, as it's a declaration's.
    - **A call that says still decides, each of the two apart**: `Draw.Mesh(vertices, indices, tiles, Filter.Bilinear)` is blended, with the declaration's wrap. Without an attribute a texture is sampled as it always was: `Filter.Bilinear`, `Wrap.Clamp`.
    - **It's what the `Texture` made there is born with, and the value carries it** (decided, above; until 2026-10-07 it was the declaration's say only where the declaration was named, and a `Texture` in a field had none). So it holds by the declaration's name (`tiles`), as one of its list or map (`frames[2]`, `icons["coin"]`), as the variable of a `foreach` over it, as a grid's field (`art.paint`) and as a target's color (`art.small.color`), and just as much in a `Texture` field or local given one of those, a function's `Texture` parameter, and what a function returns: `art.kept = tiles;` and then `Draw.Mesh(vertices, indices, art.kept)` draws as `Draw.Mesh(vertices, indices, tiles)` does. In a mesh draw's texture, in 2D, in 3D and instanced, into the window and into a target; in a frag's or vert's `Sample`; and for the source a draw or a `Draw.Blit` gives a frag's `Texture` parameter.
    - **`[Filter(Filter.Bilinear)]` and `[Wrap(Wrap.Clamp)]` say what a texture with neither has**: there's no "said to be blended" that a later default could differ from.
    - Mipmaps are said the same way since (`Filter.Trilinear`: see Mipmaps, below). Not there: anisotropy.
  - **Textures** (built 2026-10-07; that `Texture` is the one name, that grids of colors and of floats convert, that the value carries its sampling and that the attributes are what it's born with are decided, above; each rule here was picked to build it, to confirm):
    - **Where a grid converts**: wherever a value of one type goes where another is wanted, as an `int` goes where a `float` is: a draw's texture (`Mesh`, `Instanced`, a target's own), a `Draw.Blit`'s source, an assignment to a `Texture` field or local, a `Texture` parameter's argument, a function's `return`. A `Grid2<Color>` and a `Grid2<float>`, and no other grid: a `Grid2<int>` or a `Grid3` where a texture goes is an error that says which are. It's local code's alone with no rule of its own, as only local code has anywhere a `Texture` goes.
    - **The texture is the grid, not a copy**: a small value that names where the grid is, as the grid's own draws always did underneath, so it draws what the grid holds when it's drawn, and it's how a grid's pixels reach a function, which can't take the grid. Nothing ties their lifetimes: a `Texture` kept past its grid (its entity destroyed, the match another) names whatever is there, and draws nothing or anything. It never reads outside the world's own memory: everything the draw list reads through one is checked to be within the heap's pages first, as a list's index is checked where it's used. No error says a texture outlived its grid.
    - **A `Grid2<float>` reads as `(r, 0, 0, 1)`**: the cell's number as red, green and blue 0, alpha 1. That's what every GPU gives for a texture of one channel with nothing asked, so a frag's text doesn't depend on what kind of texture it's given; `(r, r, r, 1)` would need a swizzle on the texture, which WebGL 2 and WebGPU have none of, or a shader for each kind. The numbers aren't clamped to 0 to 1. Drawn with no frag it's the vertex's color times that, so red.
    - **On the GPU it's a float of 16 bits a cell** (IEEE 754's half): the one float format every backend samples blended between pixels with nothing asked of the device (OpenGL 3.3, OpenGL ES 3, WebGL 2, WebGPU). One of 32 bits is only blended with an extension on WebGL 2 (`OES_texture_float_linear`) and a feature on WebGPU (`float32-filterable`) that a device may lack, and on WebGPU a pipeline's layout says whether a texture can be blended, so a frag's pipeline would depend on the texture it was given. So no device does anything else, at the cost of precision: about three digits, up to 65504 either way; a bigger number is that, an infinity too, and NaN is 0, so no filter makes a NaN of a neighbour. The grid keeps its 32 bits for code.
    - **A grid is no texture past 16384 cells a side**, which is more than a GPU's largest texture on most devices. (It also keeps a `Texture` that outlived its grid from asking for memory by a size that's no grid's.)
    - **Sampling is two things, the filter and the wrap, each the texture's own until a draw or a `Sample` says it.** A texture with nothing said is blended and clamped. There's nothing in Tide that changes a `Texture` value's sampling but giving it to a draw with a filter and a wrap.
    - **In a frag or a vert**, a `Sample` of a grid's field or of an asset by its name that leaves its filter or its wrap out has its declaration's, known as the game is compiled. One of the frag's own `Texture` parameter, or of a `Texture` field, has whatever texture it's given, which isn't: the engine says it with each draw, so the same frag draws one texture repeated and the next clamped, as two runs of one shader. A shader's text, and the pipeline made of it, are the same whatever its textures say.
    - **A draw gives a grid to a frag's `Texture` parameter**, as it gives any texture. A draw that gives a texture to a frag with no `Texture` parameter is the error it was for a `Texture`, for a grid too (it used to say the frag samples the grids it takes itself).
    - **One textured `Mesh` for each dimension**, where there were two (a grid's and a `Texture`'s). Editors show its filter and its wrap as `Filter filter = the texture's`.
    - **Hot reloading keeps a `Texture` of the pack's or of a grid's**, with its sampling, as it keeps one of a target's color. (One of the pack's in a field was dropped by a reload before.) After a reload that changed the game's data layout, a grid's texture may name another place, and draws as one that outlived its grid does.
    - In generated C a game with no texture is what it was. One with textures differs in its `.c` alone, never its header, so no game's layout hash moved: a textured draw converts its grid at the argument and calls the one function, a shader's function takes one `Texture` and works its run's sampling out of what it's given, and a reload carries sampling over.
    - Not there: a `Texture`'s size or pixels; a way to make a `Texture` sampled otherwise outside a draw; grids of other cells (`int`, `byte`, `float2`) as textures, and `Grid3`; a `Grid2<float>` asset; a float of 32 bits where a device can blend one.
  - **Mipmaps** (built 2026-10-07, card 6 of the reshaping; that the drawing side gets mipmaps, and that the card designs how a mipmapped sample is written where code samples in a branch, are agreed; every rule here was picked to build it, to confirm):
    - **A filter, `Filter.Trilinear`, the enum's third member**: the texture is blended between its pixels and between its smaller images (each half the last one's size, down to one pixel), by how much smaller than itself it's drawn. Drawn its own size or bigger it's `Filter.Bilinear`. It's said wherever a filter is: a mesh draw's argument, a `Sample`'s, a declaration's `[Filter(Filter.Trilinear)]`, and what a `Texture` carries from there.
    - **`Filter.Bilinear` is as it was**, where Unity's reads a texture's mipmaps when it has them. Argued both ways: mipmapped by default is what most 3D wants, and one word less. But here a texture has no import setting that says whether it has mipmaps: the filter is the only place it's said, so "Bilinear reads them when they're there" would mean every texture has them, which is a third more GPU memory for each, levels made again for every grid a game repaints (Sand's, Sumo's floor), and other pixels than today's for everything drawn smaller than it is, in every game and every pinned picture. A new member moves no pixel and costs nothing where it isn't named.
    - **Nothing declares a texture's mipmaps, and nothing makes them but being read**: a texture's smaller images are made the first time a draw reads it with `Filter.Trilinear`, and again when its pixels changed since (a grid or C's pixels by their version, a pack's image never, a render target's color when a pass has drawn into the target since). A texture no draw reads so has none. So they're a third more GPU memory for the textures read so, and the work of making them: OpenGL's `glGenerateMipmap`, and on WebGPU, which has none, a pass for each level that draws it from the one before.
    - **A pixel of a smaller image is the mean of the four it's made of**, which is exact for sizes that halve evenly. An image whose side is odd loses its last row or column a level, as GPUs' own generation does.
    - **In a frag, the GPU picks the level from how fast the `uv` changes from one pixel to the next, and tidec asks that as the frag starts.** A GPU only answers that outside branches and loops (what it says inside one is undefined in GLSL, and an error in WGSL), and Tide's code samples anywhere. Three ways were weighed:
      - *Let the GPU pick where the Sample is, and only where that's allowed* (`texture()`, `textureSample`), falling back to the texture's own pixels elsewhere. It costs nothing, but tidec would need the shader languages' own analysis of which control flow counts, a Sample would read differently for being moved into an `if`, and a mistake there is an error from a shader compiler, which nobody is to see.
      - *Work the level out by hand as the frag starts* (`textureLod` with a level computed from the uv's derivatives and the texture's size). It needs the texture's size in the shader and gives up what a GPU does with the two derivatives apart.
      - *Ask for the derivatives as the frag starts, and hand them to the read* (`textureGrad`, `textureSampleGrad`), which is allowed anywhere and is what the GPU would have used. **Picked.** For each such Sample the frag's function begins with the uv worked out a first time, and its `dFdx` and `dFdy`; the Sample, wherever it is, reads with those. It costs working that uv out twice (once as the frag starts, on pixels a branch would have skipped too) and two derivatives a Sample.
    - **So the `uv` of a Trilinear Sample has to be one the frag can work out as it starts**: made of the frag's parameters, the pixel's fields through `this`, constants, `Math`, constructors, operators, functions and methods of the game's that take everything by value, a texture read with another filter, and locals declared with `var` that are never assigned again and are made of the same. One made of a `mut` local or a loop's counter isn't. Where the Sample's filter is known to be Trilinear (written there, or its grid's or asset's declaration's), that's an error at the `uv`, whose note says what it can be made of and that `Filter.Bilinear` there is the other way out. Where the filter is the texture's to say (a frag's `Texture` parameter, a `Texture` field), it can't be an error, as the texture is only known when the draw runs: the Sample reads the texture's own pixels, as `Filter.Bilinear`, and tidec warns there, in a game that names `Filter.Trilinear` at all (2026-10-07, after the review: a downgrade nothing says comes back as a shimmer nobody can place). The note says to make the uv one the frag can work out, or to write `Filter.Bilinear` at the Sample, which is what's read anyway.
    - **A vert reads the texture's own pixels**, whatever the filter: it has no pixels to compare. It's no error, as a Texture that says Trilinear is given to verts too.
    - **A mesh with no frag** (the engine's own shader, or the one tidec writes for a vert alone) lets the GPU pick, as its one read is before any branch.
    - **How a texture is read is still no part of a shader's text, with one exception**: whether a Sample *can* read between smaller images is, as it's written with the derivatives then. A Sample that says Trilinear is; one that says another filter isn't; and one that leaves it to the Texture it's given is in a game whose code names `Filter.Trilinear` anywhere, and isn't in a game that never does. So a game that names no new filter has the shaders, the generated C and the header it had, byte for byte (the enum's third member is in generated C only for a game that names it), and one frag still draws a texture that says Trilinear and one that says Point with one shader, in two runs.
    - **A `Filter` made of the number 2** is `Filter.Trilinear` for a mesh draw. In a game that never names the member, a frag's Sample that takes its filter from such a Texture reads its own pixels.
    - **A `Grid2<float>`** has smaller images wherever the device draws into floats (they're made by drawing): everywhere but an OpenGL ES 3 or a WebGL 2 without the float extension, where it's read as `Filter.Bilinear`, with nothing said.
    - **A target's color** is read between its smaller images as any texture, made of what was last drawn into it: a blur can read a big target small in one step. A target's depth has none.
    - In C, `tide_filter` has `TIDE_FILTER_TRILINEAR`, a texture reference's sampling a bit for it, and a shader run's `sampling` a bit a texture (`TIDE_SAMPLING_MIPS`).
    - Not there: anisotropic filtering (a sampler setting on each backend, left out until something wants it); choosing the level, or a bias; the nearest smaller image without blending between two; a way to sample Trilinear at a `uv` a loop makes (the derivatives of one `uv`, used for others near it); smaller images an asset brings itself; saying a texture never changes, so that its smaller images are made as the game is built.
  - **Cubemaps** (built 2026-10-07, card 9 of the reshaping, the last; that the drawing side gets cube maps, for skyboxes and reflections, is agreed; every rule here was picked to build it, to confirm):
    - **A type of its own, `Cubemap`,** and not a `Texture` with a cube inside. Argued both ways: one type fewer, and a `Texture` already is several kinds of thing (C's pixels, a pack's image, a grid, a target's color). But every place a `Texture` goes would have to refuse one: a mesh's texture, a blit's source and a `Sample(uv)` are all read by a `float2`, and a cube is read by a `float3`, through another kind of sampler in both shader languages, so the compiler has to know which it is where it's read, as it does for a `DepthTexture`. A type says it once.
    - **`cube.Sample(direction)`** gives a `Color`: what the cube has towards `direction` from its middle, a `float3` of any length, in the axes the faces are named by. A zero direction reads nothing in particular.
    - **`cube.Sample(direction, filter)`**: the filter is written there, a member of `Filter`, as a texture's is. Left out, it's `Filter.Bilinear`. A cube has no wrap: one face goes on in the next, and a read at an edge is blended across it. `[Filter]` and `[Wrap]` on a `Cubemap` (an asset, a field) are errors, and a `Cubemap` value carries no sampling, where a `Texture` carries its own: the one thing there'd be to carry is the filter, and saying it at the `Sample` keeps it known as the game is compiled.
    - **`Filter.Trilinear`** reads between a cube's smaller images, by how fast the direction changes from one pixel to the next, with the rule a texture's has (Mipmaps, above): in a frag, of a direction the frag can work out as it starts, which is an error otherwise; a vert reads the cube's own pixels.
    - **A frag or a vert reads one where it's an asset by its name, or a field of a component or a singleton it takes,** as it reads a `DepthTexture`. Not a parameter of the frag's own, a local, or a field of a struct a draw supplies: each is an error that says where it goes. A function they call takes one as a parameter, and samples the cube its call names, or an asset's by its name (Code the GPU runs, A function samples the textures it takes).
    - **Its faces are six, in the order `+x`, `-x`, `+y`, `-y`, `+z`, `-z`,** each as it's seen from the cube's middle, the way a picture is, rows from the top: `+y` is up for the four around, with `+x` to the right of `+z` (the world is left-handed, as the cameras are); the one above has `-z` at its top and the one below `+z`. That's the layout OpenGL, WebGPU and Direct3D have, so six faces from another tool go in as they are.
    - **An asset's is `asset Cubemap sky = "art/sky_??.png";`:** a glob that matches exactly the six files, each a supported PNG, PSD, JPEG, TGA or BMP image. Argued: six named arguments, or six `asset Texture`s put together in code, say the faces outright; but an asset's path is one constant text, and a glob over files named by a convention is what a list of textures already is. **Each file is named for its face by how its name ends before its extension: `px`, `nx`, `py`, `ny`, `pz`, `nz`,** in any case. Picked over `+x` and `-x`, which are awkward in file names, shells and URLs, and over `right`, `left`, `top`, which don't say which way is which; it's the convention the tools that write cube maps mostly use. A file that ends in none of them, a face that's missing or there twice, a face that isn't square and faces of different sizes are errors at the path, which name the file. One file, or a URL, is an error that says to write a glob.
    - **A list or a map of cubes isn't there:** `List<Cubemap>` and `Map<string, Cubemap>` are errors, as the glob is already the cube's own.
    - **In the pack, a cube is six images, each there once** (shared by its file's hash, with a `Texture` of the same file too), and a block that names them in order. A `Cubemap` of the pack's is a name, as a `Texture` of the pack's is: what a field keeps of `sky` is the cube as the files are now.
    - **`Cubemap(size)` makes a `Cubemap` that's drawn into** (a constructor, as `RenderTarget(width, height)` is; it was `RenderTarget.Cube(size)` for an afternoon, and a static of one type that makes another read oddly: `RenderTarget.Cube` is an error that names the constructor), and `Cubemap(size, format)` one whose faces hold `format`: `Format.Rgba8`, when left out, or `Format.Rgba16f`. `Format.Depth` is an error. It's made in local code, as a target is. Its name is the card's; that it gives a `Cubemap`, and not a `RenderTarget` with six of everything, is a pick: a cube is no destination by itself, and every method a target has would have to refuse it.
    - **Its size is in pixels,** whatever the display, where a target's is in logical pixels at the display's scale: a cube's faces aren't laid over the window. `cube.Resize(size)` gives it another and empty images, and `cube.Dispose()` lets it go for every copy. Neither is for an asset's cube, which is an error where it's known and a line on the game's output otherwise.
    - **`cube.faces[i]` is a `RenderTarget`,** with an int that's any expression: the face `i`, from 0, in the faces' order. `cube.faces` is only read one at a time, as `target.colors` is, and can't be assigned. A constant index past 5 is an error; one that's only known as the game runs, and a face of a cube that isn't drawn into (an asset's, or one never made), is no destination, and draws into it are passed by.
    - **A face is a destination like any other:** its own camera, clip, transform, blend, tint, depth, cull and stencil, every draw a target has, and depth and stencil of its own, kept from frame to frame. Six faces are six sets of depth. Its color and its depth aren't handed out (`face.color` and `face.depth` name no image, and a draw that samples one is passed by): the cube is what's read. `face.Resize` and `face.Dispose` change nothing, having said so: they're the cube's.
    - **What a face holds is what a camera at the cube's middle sees along the face's axis with a field of view of 90,** with the up the faces' layout says. The engine has no call that draws all six, and none that gives the six rotations: a game writes them (six `quaternion.LookRotation`s), as it works out a shadow map's matrix.
    - **A draw into a face that reads that face's cube is passed by,** with a line that says so, as a draw into a target that reads the target is, and `Draw.Blit` refuses it as it's recorded.
    - **A cube that was never set reads as black,** opaque. One that was disposed is passed by, with a line.
    - **A cube that's drawn into starts black,** and is black again after `Resize`, after a hot reload and after a lost device, as a target is.
    - **Its smaller images** are made when a draw reads it with `Filter.Trilinear` and a pass has drawn into any of its faces since, each face's from that face alone: at the smallest of them a face is one pixel, which a read off its middle blends with the faces beside it.
    - **`Format.Rgba16f` on a GPU that can't draw into floats** is 4 bytes a pixel, with the line a target's float image has.
    - **A game with no `Cubemap` generates the C it did,** and no shader's text moved.
    - In C: `tide_cubemap_ref`, `tide_cubemap_make`, `tide_cubemap_make_as`, `tide_cubemap_face`, `tide_cubemap_resize`, `tide_cubemap_dispose`, `tide_draw_cube_source`, and `TIDE_SHADER_READ_CUBE` for how a shader reads that texture; in the pack, `TIDE_PACK_CUBEMAP`, `tide_pack_cubemap_of` and `tide_pack_cubemap_find`.
    - Not there: choosing the level a cube is read at, or a roughness, which a blurred reflection wants (mipmaps have no chosen level yet); a cube of depth alone, for a lamp that shines every way; a face's depth as a `DepthTexture`, or its color as a `Texture`; several color images or samples on a cube; a cube as a draw's own texture or a frag's own parameter; a cube made of six `Texture`s or grids as the game runs; image formats other than PNG, PSD, JPEG, TGA and BMP, and the layouts that put six faces in one file (a cross, a strip, a panorama); float files; lists, maps and arrays of cubes; drawing all six faces in one pass; `[Filter]` on a cube.
  - **`Draw.Transform`** takes `(offset)`, `(offset, rotation)`, `(offset, rotation, scale)` with a `float` or a `float2` scale, or a `float3x3` matrix. `offset` is a `float2` in the units the calls are in, `rotation` is in radians from `+x` towards `+y`, as shapes' are, and the draws are scaled, then turned, then moved, all about (0, 0). The matrix's first two columns are where (1, 0) and (0, 1) go, and its third where (0, 0) does. **`Draw.Transform`** takes a `float4x4`, which each mesh's own transform goes through on its way to the world.
  - A transform is no part of the conversions: `Draw.WorldToScreen` and the others are the camera's alone. `Draw.MeasureText` is in the units the transform takes, so inside one, text measures as it's drawn.
  - **Shapes under a transform** stay shapes. Moved, turned, mirrored and scaled as much each way, they're exactly that, and rects that are only moved and scaled still fill whole pixels, so tiles under one leave no seams. A transform that stretches (more one way than the other) or skews gives the nearest shape: a circle or an ellipse is the ellipse it becomes, a rect's sides follow its own axes, and what has no direction (a corner's radius, an outline's width, how far an edge fades, a ring's radius) is scaled by the transform's mean. A `WireRect` or `WireCircle` that's turned or stretched keeps an outline a pixel wide. Meshes go through any transform as they are, corner by corner.
  - **Text under a transform**: moved, and scaled as much each way, it's text of that size, fitted to the display's pixels as any text (so as a scale changes, it steps from one whole size to the next, as it does under a camera that zooms). Turned, stretched or mirrored, it has no pixels to fit: it's laid out as it would be upright, at the size the transform's mean scale makes it, and drawn through the transform, softer than upright text. Either way its anchor's place is its `position` through the transform, and its box is against it as it is upright, then turned with it.
  - **A clip under a transform** is the upright rect around what its rect becomes: clips don't turn.
  - **`frag` and `vert`**: a mesh drawn through them blends as the setting says, its frag's color is multiplied by the tint, and a 2D one's vertices go through the transform after its vert, before the camera. A 3D one's `instance.transform` is its own through `Draw.Transform`, and `instance.Normal` goes by both.
  - A draw's singletons' fields are 3984 bytes at most, where they were 4000: the tint is sent with them, and with it 4000 would make a block of uniforms that some OpenGL ES drivers don't link (Android's emulator takes 255 float4s in a vertex shader's block, and not 256).
  - **`Draw.Clear`, and a target's `Clear`,** fill what the clip leaves with their color as it is: no blend, tint, stencil or transform changes a clear.
  - **`Draw.Blit`** is its destination's, whatever is set: no blend, tint, depth, cull, stencil, transform, camera or clip changes it. It takes a blend of its own before its frag, `Draw.Blit(source, destination, Blend.Add, frag)`, and replaces what's there without one. So a pass over the screen doesn't change with what an earlier view left set.
  - A setting that's set to what it was, or set and put back with nothing drawn, costs nothing: what's drawn on either side of it is still drawn at once. A tint and a 2D transform are worked into shapes and text as they're recorded, so shapes with a tint or a transform each are still one draw call. A mesh through a `frag` gets its tint and its 2D transform with its draw, so instances with different ones are different draws.
  - In C (`tide/draw.h`), `tide_draw_set_*` are the `Set` forms (`tide_draw_set_camera`, `tide_draw_set_clip`, `tide_draw_set_screen`, `tide_draw_set_blend`, `tide_draw_set_depth`, `tide_draw_set_cull`, `tide_draw_set_stencil`...), `tide_draw_unset_*` the ones with no arguments, and `tide_draw_push_*` with `tide_draw_pop(list, mark)` the blocks. The former `tide_draw_camera`, `tide_draw_camera_3d`, `tide_draw_camera_matrices`, `tide_draw_clip`, `tide_draw_no_clip` and `tide_draw_screen` were renamed to match.
- **Target formats and depth** (built 2026-10-07; that a target says what it holds when it's made, that one can hold depth alone, and that a frag reads a target's depth plainly or compared are decided, above; everything here is a pick, to confirm):
  - **`Format`** is a built-in enum, with two members when this was built: `Format.Rgba8`, a color image beside the depth, which is what a target holds when nothing is said and the name for what targets always were, and `Format.Depth`. (They were `Format.Color` and `Format.DepthOnly` until the formats were named by what they store: see One word for the window, and formats by what they store, above.) (`Format.Rgba16f` joined them with several colors: see Several colors and float images, below.) A game can't declare the name outside a namespace. It's a value like any enum's: a variable or a function's parameter says it too.
  - **It's the last argument where a target is made:** `RenderTarget(width, height, format)`, `RenderTarget(width, height, pixelsPerUnit, format)`, `RenderTarget.Window(format)` and `RenderTarget.Window(factor, format)`. Anywhere else among them is an error that says it comes last. `Resize` keeps it, and so does a hot reload, with the target's size and scale. (That a target's formats are after its size, one for each color image, is decided since.)
  - **What draws into depth alone.** A clear, a 3D mesh under `Depth.ReadWrite` (`target.Mesh`, the primitives, `target.Instanced`, through a frag or not), and whatever stores stencil (see Stencil, above). Shapes, text, 2D meshes, a `Draw.Blit` into it, and 3D meshes under `Depth.Read` or `Depth.None` draw nothing there otherwise, and say nothing: it's what the target is for. Its `Clear` takes a color like any target's, which goes nowhere.
  - **`target.depth` is a `DepthTexture`,** a type of its own, as the code the GPU runs differs for it and the compiler has to know which it's writing. It's a local reference like `Texture`: kept only in local state (a local component's or singleton's field, a match one's `local` field, a struct of those, a local), copied freely, with no constructor. A default one is no target's, and reads as the far limit: `Sample` gives 1, and `Compare` 1.
  - **A frag or a vert reads one where it's a field of a component or a singleton it takes,** `lamp.shadow.Compare(uv, depth)`, as it reads a `Grid2<Color>` or a `Texture` field: a view puts a target's depth there, `lamp.shadow = lamp.map.depth;`. It isn't a parameter of the frag's own, and no draw supplies one as its texture (`Draw.Mesh`'s, or `Draw.Blit`'s source): those are a color's. Reading it through the target, `lamp.map.depth.Sample(uv)`, is an error, as a frag doesn't read a `RenderTarget`. A function they call takes one as a parameter, `float Lit(DepthTexture shadow, float3 at)`, and reads the depth its call names, `Lit(lamp.shadow, at)`, both ways (Code the GPU runs, A function samples the textures it takes).
  - **`depth.Sample(uv)`** gives a `float`, the nearest pixel's. It takes no `Filter`: a depth blended between two pixels is neither's.
  - **`depth.Compare(uv, value)`** gives a `float` from 0 to 1. At each of the pixels around `uv` it's 1 where what's stored isn't nearer than `value`, the test a mesh passes to be drawn there, so a pixel compared with its own depth passes; what that gives at each is blended by how near `uv` is to it. `value` is kept between 0 and 1 first: past the far limit is the far limit. A frag takes its own bias off `value`; there's none as state.
  - `uv` is a color's: (0, 0) at the target's top left corner, (1, 1) at its bottom right, and past them, the pixels at the edge: neither read takes a `Wrap`.
  - **A draw into a target that reads that target's depth is passed by,** with a line that says so, when it's drawn: what a frag reads comes through a field, which the compiler can't follow to a target. It's what a draw that samples its own destination's color through a field gets, where `Draw.Blit(target.color, target, frag)` is refused as it's written. So is a draw that samples the `color` of a target of depth alone, which has none.
  - A depth is 24 bits, with the 8 bits of stencil beside it, in every target and in the window.
  - Each way a frag reads a depth is one of the draw's 8 textures: a depth it both samples and compares counts twice.
  - **Not there:** a `DepthTexture` as a draw's own source or a frag's own parameter; a comparison other than "isn't nearer"; writing a pixel's depth from a frag; reading a depth back into Tide; and skipping a frag that can't discard in a target of depth alone, which a backend may do some day and changes no picture.
  - **Where a point of the world is in a target's image is the game's to work out,** for a frag that looks one up (a shadow map does): through the target's camera as a matrix, `target.WorldToClip()` (see The cameras as matrices, above: the projection, after z turned around, after the camera's transform undone, which a game spelled out itself until that was built); then `uv` is `(x * 0.5 + 0.5, 0.5 - y * 0.5)` of that point and its depth `z * 0.5 + 0.5`, each over `w` under a perspective.
- **Several colors and float images** (built 2026-10-07; a format for each color image after a target's size, four at the most, `target.colors[i]`, a frag's outputs to them by their order with the rest of either dropped or left, and floats where a GPU draws into them with bytes and a line where one doesn't, are decided, above; everything here is a pick, to confirm):
  - **`Format.Rgba16f`** is the enum's third member, and the one float format there is: four floats of 16 bits a pixel, each any number to three digits or so (up to about 65,000, in steps of a thousandth near 1). It's the one every backend that has float images draws into, blends and reads blended between pixels, so a `Format.Rgba16f` image behaves as a `Format.Rgba8` one does but for what it keeps. It takes twice the memory. Floats of 32 bits, and images of one or two channels, are Open.
  - **Where the formats are written:** after the size and, where it's said, the pixels per unit, `RenderTarget(width, height, pixelsPerUnit, Format.Rgba8, Format.Rgba16f)`, and after a screen's factor. A fifth is an error, and so is `Format.Depth` written among several: every target has depth, so it's said alone. A format is a value like any enum's, so one that's only known as it runs, and isn't a color's among several, is a `Format.Rgba8` image, with a line that says so.
  - **`target.colors` is only read one at a time,** `target.colors[1]`, with an int that's any expression. It's no list: `target.colors` by itself, its `count`, and a loop over it are errors that say to read one. A literal index past 3 is an error; any other image a target hasn't got is found out when a draw samples it, which is passed by with a line that says so, as a draw that samples the color of a target of depth alone is. `Draw.window` has none, like its `color`.
  - **`target.color`, `target.colors[i]` and `target.depth` can't be assigned:** they're the target's own images. (The first and the last compiled before, to C that didn't.)
  - **What the engine draws itself has one color,** which goes into a target's first color image and leaves the others: shapes, text, 2D and 3D meshes drawn with no frag, and so the GUI's widgets, were they drawn there.
  - **A clear fills every color image** with its color, and sets depth back. Its color is a color, each channel from 0 to 1, whatever the image holds: a float image can't be cleared to more than white, or to a color of its own.
  - **A frag's outputs are matched by their order alone,** wherever they are among its parameters and whatever they're called, so `out Color albedo, Sun sun, out Color normal` writes the first image and the second.
  - **Each output is finished as the pixel's color is:** times the draw's tint (`Draw.Tint`), then blended over what's in its own image by its own alpha, as the draw's blend says. There's one blend for a draw, not one for each output, so an output that's data is written with an alpha of 1 (`Color(x, y, z)` has one): an image keeps each color times its alpha, under `Blend.Replace` too.
  - **A frag of several outputs draws anywhere:** into the window or a target of one color image, where its first is the pixel's and the others go nowhere, and into a target of depth alone, where none does. Nothing warns: one frag serves a pass that fills three images and a pass that casts a shadow.
  - **`Draw.Blit`** replaces as many of its destination's color images as its frag has outputs, and leaves the others. Its source is one `Texture`, as ever: a frag that reads several images of a target takes the others through `Texture` fields of a singleton.
  - **A draw into a target samples none of that target's color images,** the second no more than the first, whichever of them it writes: it's passed by, with the line a draw that samples its own color gets.
  - **On a GPU that can't draw into floats** (the decided fallback), the line is the engine's, on the game's output, once for a run. Nothing lets a game ask: see Open.
  - **Not there:** an output of another type than a `Color`; reading how many color images a target has; a `Format` for each output's blend or for a clear; a target's color image as another target's (each target owns its images).
- **A target's samples** (built 2026-10-07; that render targets have multisampling, said with a `samples` argument, is decided, above; everything here is a pick, to confirm):
  - **It's a named argument, the last, where a target is made:** `RenderTarget(width, height, samples: 4)`, `RenderTarget(width, height, pixelsPerUnit, Format.Rgba8, Format.Rgba16f, samples: 4)`, `RenderTarget.Window(samples: 4)`, `RenderTarget.Window(0.5, Format.Rgba16f, samples: 4)`. By name because a target's other arguments are told apart by their types and their order (numbers, then formats), and a bare `4` after them would read as nothing: after `RenderTarget(64, 64` it would be the pixels per unit. It's the one argument of a target's that's named, and it's only written with its name: any other name is an error, and so is `samples:` before another argument.
  - **1, 2, 4 or 8.** 1 is a target that says nothing. A number that's known as the game is compiled and is none of those is an error; one that's only known as it runs is the one of those under it (5 is 4, 100 is 8, 0 is 1), with a line that says so.
  - **A device with fewer draws it with what it has,** and the engine says so once, on the game's output: the most of 8, 4 and 2 that's no more than asked and that the device has for what the target holds. WebGPU has 1 and 4 and nothing else, so there 2 is 4 (more than asked, where the other choice was no smoothing at all) and 8 is 4. Nothing lets a game ask what it got.
  - **What it's for:** each of the target's pixels keeps that many colors, depths and stencils while it's drawn into, and a triangle's edge covers some of them, so the edges of 3D meshes, and of 2D ones, are smooth. A frag still runs once for a pixel a triangle covers, not once a sample. The smooth shapes and text are smooth by themselves, and are as they were.
  - **Its color images are what its samples come to,** one color a pixel, each time drawing into it ends (a pass): `target.color` and `target.colors[i]` are textures like any target's, sampled, blitted and blended between pixels the same way, of any format. What's drawn into it is kept from frame to frame, sample for sample, until it's cleared, as any target's is.
  - **Its depth isn't read.** A GPU keeps several depths a pixel there, and none of the backends resolves them into one texture a frag could sample (OpenGL could, by picking a sample; WebGPU can't without a shader of the engine's own that reads each sample and writes a depth). So `target.depth` of a multisampled target is refused, over a resolve that costs a pass on every frame for every such target whether its depth is read or not: a draw that reads it is passed by, with a line that says so, when it's drawn, as a draw that samples an image a target hasn't got is (a `DepthTexture` comes through a field, which the compiler can't follow to a target). It's by what the target asked for, whatever the device gave it, so the same draws are passed by on every machine. A game that wants both draws its depth into a target with no samples.
  - **A target of depth alone has none:** `Format.Depth` with `samples` above 1 is an error where both are written, as nothing could be read of it, and where either is only known as it runs the target has one sample, with a line that says so.
  - **The window has none.** A game that wants an antialiased frame draws it into `RenderTarget.Window(samples: 4)` and brings it to the window with `Draw.Blit`. The window's own framebuffer would take a setting that's the game's and three ways to ask for it that can each be refused (GLFW's hint before the window is made, the WebGL context's `antialias`, which a browser may ignore, and an EGL config on Android), a canvas on WebGPU has no samples of its own at all, and the GUI, the stats and the smooth shapes drawn there gain nothing from it.
  - **It takes that many times the memory:** `samples` colors and depths a pixel, and one more color a pixel for each image they come to. A 4-sample target of the window's size at 1920 by 1080 is about 75 MB where one with none is 17.
  - **Everything else is as for any target:** every draw goes into it, the engine's own, a frag's, a clear, a blit and stencil among them; `Resize` keeps its samples; a hot reload makes it again with them.
  - **Not there:** asking how many samples a target has, or a device's most; a frag that runs for each sample, or reads one; alpha to coverage; a resolve other than the GPU's own (the mean of a pixel's samples, which is wrong for light past white in a `Format.Rgba16f` image: a bright edge stays jagged until tone mapping is done before the resolve); samples for the window.
- **`verified` in views**, beyond what's decided:
  - Any `verified` component or filter asks for the entity in the confirmed world: `without verified Dead` is "confirmed, and not `Dead` there", so an entity whose spawn isn't confirmed yet is skipped by it too.
  - The same entity is told by what each was born as, where this machine knows it of both, and by its handle otherwise (an entity that came with a world the server sent).
  - `mut` goes with it: `mut verified Health health` reads the confirmed `Health` and writes the entity's local fields, which are the entity's whichever world a parameter reads. A view that takes a component both ways reaches its local fields through the one that isn't `verified`; through the other it's an error that says so.
  - A view of the confirmed world's entities draws them as they are there: nothing it reads of them is blended, corrections aren't smoothed, and `this` is the confirmed entity. Its `World` is still the world it draws.
  - A `verified` value is whole: its text, lists, maps, sets and grids are the confirmed world's, read like any other's, through members, methods, loops, copies of the value and the functions it's passed to. Nothing of them is copied to read them, but the elements of a list or map whose elements hold text, which a view gets a copy of each time it runs. They're to read: like everything a view takes of the match, a change goes to a copy, in a local or in local state.
  - `verified World world` isn't built: an error that points to `verified` component parameters.
  - A frag or vert can't say `verified`: it reads what its view is given. The mesh's components are the view's own as it draws them (the ones that aren't `verified`, where it takes one both ways), and the confirmed ones in a view of the confirmed world's entities.
  - A function's parameter can't say it either: the view takes it `verified` and passes it on.
  - With one world and no server, as on a C host that ticks a world itself, that world is the confirmed one too.
  - Outside a match, a view with a `verified` parameter doesn't run, like any view that reads the match.
  - `verified` is a word of its own before a parameter's type where a type and a name follow, after `with` or `without` where a name follows, and before an event in a handler's `event(...)`. Anywhere else it's a name like any other.
- **Queries in views and local handlers**, beyond what's decided (see Read-only entity queries):
  - **Rows are read-only, and have the match's fields only.** `mut` on a binding is an error, in a view as in a system, and a row doesn't reach an entity's local fields, to read or to write: those are a view's that runs for the entity, through its own parameter. Naming one through a row is an error that says so.
  - `entry.entity` is an `Entity` for the match's entities, the confirmed world's handle in a query whose bindings and filters are all `verified`, and a `LocalEntity` for a local query's.
  - **Outside a match, a view with a query over the match doesn't run**, like any view that reads the match: a view only runs when everything it reads exists. A query that names no component is the match's in a view. A game that wants the rest of the view to go on outside a match keeps the query in a view of its own.
  - A view's rows are made once a frame for the view, when it first runs for an entity, so a view no entity matches makes none, and a view that runs for each of its entities doesn't blend a query's again for each. Correction smoothing (`[Position]`, `[Rotation]`) applies to them as to the view's own parameters. Text and lists of the world the view draws are read through a row as through a parameter.
  - As for a view's own parameters: any `verified` binding or filter asks for the entity in the confirmed world; a `verified` binding isn't blended, and its text, lists, maps, sets and grids are the confirmed world's, read from its heap; the same component can be bound both ways in one query, `query (Health health, verified Health sure) both`; with one world and no server, that world is the confirmed one too.
  - **A local query** reads the local world as it is, with what views earlier in the frame wrote. A view that changes a local component it also queries sees the rows it changed earlier in that run, as a system does, and its own entity's as it was before the view ran for it.
  - `ownership` isn't a query filter, in a view either: an error, as in a system.
  - **A local handler's rows are its event's tick's once that tick is done**: after all its events and changes, not as each event's turn left things, where the handler's own components are. So an entity the tick destroyed is in no row, the one the event was sent to included, and one it spawned is in them. They aren't blended. The tick records them once, whichever of its events have a handler that reads them, and only when it sent one.
  - Under rollback, a handler that hears a tick as it's predicted gets the rows of the run that first sent its event, and one of a `verified` event the confirmed run's.
  - Nothing in a local handler's query says `verified`: its event does, for all of it. Text, lists, maps, sets and grids of the match aren't in its rows (an error to name, and empty in a copy), as for what the handler takes itself.
  - A query of this machine's entities in a local handler is the local world as it is when the handler runs. A handler of a local event, `Forgotten` among them, takes only those. A query over the match counts as something of the match's for `Spawned` and `Destroyed`: a local handler of one that takes it hears the match's.
- `frag` and `vert` declare code the GPU runs for a mesh's pixels and vertices (above, and GPU code below).
- `view` declares a view. It looks like a system and takes the same parameters, but it runs once per rendered frame instead of once per tick. Its `mut` parameters, `Spawn`, `Add`, `Remove`, `Destroy` and `Send` are local (see Local state): a `mut` parameter is a local component or singleton, or one of the match's that has local fields, which are all the view writes of it. Input parameters are errors in a view.
- Views run in declaration order, after all the systems of the frame's ticks. Within a view, entities run in the same order as in systems.
- `Draw` functions can only be called from views and the functions they call (see Functions). Calling them from systems needs to tell predicted ticks from verified or replayed ones, which comes with multiplayer.
- `view` is only a keyword at the start of a declaration, like `input`.
- Positions and sizes are in world units with `y` up. The camera maps them to the screen.
- The Draw functions:
  - `Draw.Clear(color)`: fills the screen.
  - `Draw.SetCamera(center, size)`, or `Draw.Camera(center, size) { ... }` around a block (see Draw settings): the camera for the Draw calls. `center` is the world position at the middle of the screen, and `size` is half the visible height, like Unity's orthographic size. Each frame starts black, with the camera at the origin and one world unit per pixel.
  - `Draw.Circle(center, radius, color)` and `Draw.WireCircle(center, radius, color)`.
  - `Draw.Rect(center, size, color)` and `Draw.WireRect(center, size, color)`.
  - `Draw.Line(from, to, color)`.
  - The shapes with smooth edges (below): `Draw.Line(from, to, width, color)`, `Draw.WireRect(center, size, width, color)`, `Draw.WireCircle(center, radius, width, color)`, `Draw.Rect(center, size, rotation, color)`, `Draw.RoundRect(center, size, radius[, rotation], color)`, `Draw.WireRoundRect(center, size, radius, width, color)`, `Draw.Ellipse(center, radii[, rotation], color)`, `Draw.WireEllipse(center, radii, width, color)`, `Draw.Arc(center, radius, start, sweep, width, color)` and `Draw.Shape(shape)`.
  - `Draw.Text(text, position, size, color)`: `position` is the top left corner and `size` the font size, the height of a line of it. A newline in `text` starts another line under it.
  - `Draw.Text(text, position, size, color, anchor)`: `position` is the place of the text's box that the `Anchor` names, rather than always its top left corner. `Anchor` is the built-in enum `GUILayout.Area` takes, the nine places of Unity's `TextAnchor`, `UpperLeft` to `LowerRight`. The box is the one `Draw.MeasureText` gives: as wide as the text's widest line and as tall as its lines. Upper is towards the top of the screen whether the calls are in the world, where `y` goes up, or in the screen's pixels, where it goes down. Each line of a text of several is against the anchor's side: lines start at a left anchor, end at a right one, and are centered on one between. Without the argument nothing changes: `Anchor.UpperLeft` is what text has without one.
  - `Draw.Text(text, position, size, color, font)` and `Draw.Text(text, position, size, color, anchor, font)`: the weight the text is drawn in, the built-in enum `Font`: `Font.Regular`, which text has without one, or `Font.Bold`, the bold weight of the engine's font, a font file of its own. A game only carries that file when its code says `Font.Bold`. The weight is an argument, as a mesh's `Filter` is, since it's about one thing drawn. `target.Text` takes the same arguments for a render target. The GUI's widgets, the stats overlay and the debug views are in the regular weight.
  - What the agreed design of anchors and weights left open, picked by the implementation and not yet confirmed:
    - The regular weight has a name, `Font.Regular`, so a `Font` can be a value: a field, a local, a parameter, the result of a function. `Font` is a name of the engine's, as `Anchor` and `Filter` are, so a game can't declare its own.
    - A game carries the bold weight's file when its code names `Font.Bold` anywhere, or makes a `Font` of a number (`Font(1)`), whether or not that code ever runs. In a game that does neither, every `Font` is the regular weight.
    - A code point the bold weight has no glyph for is drawn in the regular weight, and measured as it's drawn. The bold file covers Latin, Greek and Cyrillic, with fewer letters of each than the regular one.
    - Bold is fitted to pixels by its own measures, as the regular weight is by its: its stems are a whole number of pixels thick. Under 13 of the display's pixels that's one pixel in both weights, where bold only differs in its letters' shapes and widths.
    - An `Anchor` made of a number that's none of the nine (`Anchor(40)`) is `Anchor.UpperLeft`.
    - Measuring takes no anchor: a text is as big at every one.
  - `Draw.MeasureText(text, size)` and `Draw.MeasureText(text, size, font)` draw nothing, and return a `float2`: how big `Draw.Text` would draw `text` at `size` where the call is, in the weight `font` (`Font.Regular` without one), with the width of its widest line in `x` and the height of its lines in `y`. Both are positive and in the units the Draw calls are in: the screen's pixels on the screen, and world units under the 2D camera. It's the text's box, which an anchor names a place of: without one, the text reaches `x` to the right of its `position` and `y` below it on the screen. One line is `size` high, and each newline adds the distance from one line to the next; an empty text is still one line high. A `size` that isn't above 0 gives zero, as nothing is drawn. Text is fitted to the display's pixels, so the answer is the one for this display, this camera and this frame, to the pixel: it's measured where it's drawn rather than kept. `target.MeasureText(text, size)` and `target.MeasureText(text, size, font)` measure for a render target, through its own camera: in the units its calls are in, as the text is fitted to its image's own pixels. The name, and that it returns one `float2` rather than a width alone, are the implementation's picks, not yet confirmed.
  - `Draw.Mesh(vertices, indices)`, `Draw.Mesh(vertices, indices, texture)` and `Draw.Mesh(vertices, indices, texture, filter)`: a `List<Vertex>`, a `List<int>`, a `Grid2<Color>` and a `Filter`.
  - The other settings (see Draw settings): `Draw.Blend(mode)`, `Draw.Tint(color)`, `Draw.Transform(...)`, `Draw.Transform(transform)`, `Draw.Depth(mode)`, `Draw.Clip(rect)` and `Draw.Screen()`, each around a block, and each with its `Set` form.
  - In 3D: `Draw.SetCamera(position, rotation, fieldOfView)` and `Draw.SetCamera(transform, projection)`, or `Draw.Camera` with either around a block, and `Draw.Mesh(vertices, indices, transform)`, `Draw.Mesh(vertices, indices, transform, texture)` and `Draw.Mesh(vertices, indices, transform, texture, filter)`: a `List<Vertex3>`, a `List<int>`, a `float4x4`, a `Grid2<Color>` and a `Filter`.
  - Both forms of `Draw.Mesh` take a list of a vertex struct of the game's instead, and after their other arguments, a vert and a frag by name (GPU code, below). The struct's `position` says which: a `float2` is a 2D mesh's, drawn where its vertices are, and a `float3` a 3D mesh's, which takes a transform after its indices.
  - `Draw.Clear(color)` clears the window and its depth; clipping is controlled by `Draw.Clip` and `Draw.SetClip`.
  - `Draw.Cube(center, size[, rotation], color)`, `Draw.Sphere(center, radius, color)`, `Draw.Cylinder(center, radius, height[, rotation], color)` and `Draw.Plane(center, size[, rotation], color)` draw solid unlit meshes. Sizes and heights are full extents. A cylinder's axis is local `y`; a plane spans local `xz`. Nonpositive dimensions draw nothing. Geometry is cached by shape and color, and consecutive matching instances draw together.
  - `Draw.WorldToScreen(float2)` and `Draw.ScreenToWorld(float2)` convert with the currently selected 2D camera; on the screen (`Draw.Screen`, `Draw.SetScreen`) they are identity mappings. `Draw.WorldToScreen(float3)` and `Draw.ScreenToWorld(float3)` use the currently selected 3D camera, including custom projections. Screen `x` and `y` are top-left logical pixels, with `y` down; 3D `z` is normalized depth, 0 at the near plane and 1 at the far limit. Depth 1 for the standard infinite perspective camera has no finite world position.
  - `Draw.ScreenRayOrigin(float2)` and `Draw.ScreenRayDirection(float2)` give a picking ray from the near plane, with normalized world direction, for perspective and orthographic cameras. Device positions count from the bottom left, so they go through `Screen.Flip` first (see GUI, Provisional): `Draw.ScreenToWorld(Screen.Flip(devices.pointer.position))`. Conversion helpers use the current frame's viewport and return zero for a viewport with no size, a singular camera matrix or an undefined projection.
  - One `Draw` has every drawing function, 2D and 3D, with `Blit`, `window` and `list`, which hands the frame's draw list to an extern function; a render target has the same functions as its methods, but for `Blit`, which names its destination, and the two members. `Draw2D` and `Draw3D`, the owners there were until 2026-10-07, are errors that name `Draw`, with the rename as the editors' quick fix (decided).
  - `Draw.Instanced(vertices, indices, instances[, texture[, filter]][, vert][, frag])` draws one mesh for each item in an instance list, without entities for its items, 2D or 3D by its vertices. Meshes, textures and shaders follow `Mesh`'s forms. See GPU code below for how a list item is supplied to a shader.
- A render target's size (above), in what the agreed design leaves open. These are the implementation's picks, not yet confirmed:
  - Sizes are numbers, not only ints, as `Screen.width` and `Screen.height` are floats: `RenderTarget(Screen.width / 4, 90.5)`. `Resize` takes them the same way.
  - A target's image has the whole number of pixels nearest its size times its scale, one at least, each way. It's then stretched over the target's size by under half a pixel, as a window's pixels are on a display with a scale that isn't whole: the size stays what was asked for, so conversions and what's drawn at the target's edges land where the size says. A `RenderTarget.Window(factor)` target has the window's own pixels times the factor, rounded the same way, so `RenderTarget.Window()` has the window's pixels exactly.
  - A width, a height, pixels per unit or a factor that isn't above 0, or isn't a number, makes an invalid target, as a default one is, with a diagnostic: drawing with it is skipped.
  - `Resize` on a target made with `RenderTarget.Window` does nothing but a diagnostic: its size is the window's to say. It can't be known when the code is checked, as a target is a value. `Resize` takes no pixels per unit: a target keeps the ones it was made with, and one that wants others is disposed of and made again.
  - No image is bigger than the GPU's largest texture a side. A target that would be, after its scale, keeps its shape with fewer pixels, the most that fit, with a diagnostic when its image is made: what's drawn into it lands where it would, less sharp.
  - A target at the display's scale takes the scale of what its frame is drawn into: the window's or, in a run with no window, the run's `--scale`. A host's C that reads pixels back from a frame of its own (`tide_platform_read_pixels`) draws it at a scale of 1, whatever the display's.
  - A target that follows the window has no pixels per unit of its own: `RenderTarget.Window(factor)` is at the display's scale. A pass that wants the same number of pixels on every display, whatever the window's size, has no form yet.
- Later Draw calls draw over earlier ones, but for 3D meshes among themselves (below), unless `Draw.Depth` says otherwise.
- `Rect`, `WireRect` and the `Line` with no width fill whole pixels, so rects side by side leave no seam and a line is a pixel wide wherever it is. `Circle` and `WireCircle` have a smooth edge, the outline a pixel wide. The other shapes are the smooth ones: a pixel is in one by how far it is from the shape's edge, which is smooth over one pixel of the screen at any size, turned any way.
  - Their lengths (sizes, radii, widths) are in the units the calls are in, as positions are: the world's under a camera, pixels on the screen. A size, radius or width that isn't above 0 draws nothing.
  - Angles are in radians, from `+x` towards `+y`: counterclockwise under a camera, where `y` goes up, and clockwise on the screen, where it goes down. A `rotation` turns a shape about its center.
  - A `radius` rounds a rect's corners, no more than half its smaller side. An ellipse's `radii` are half its width and half its height. An outline (`width`) is inside the shape's edge, no wider than half the shape's smaller side. A thick line's ends are round, and reach half its width past where it ends.
  - `Arc` is part of a ring: of the circle outline `width` thick inside `radius`, what's from the angle `start` on for `sweep`, the other way round for a sweep under 0, cut straight across at both ends. A width of the radius is a slice of a disc. A sweep of a whole turn or more is the whole ring.
  - `Shape` is a built-in struct, and `Draw.Shape(shape)` the call the others but `Arc` are short for: `float2 center`, `float2 size`, `float rotation`, `float radius`, `bool ellipse` (the ellipse that fits `size`, whatever `radius` says), `Color color = Color.white`, `Color color2 = Color.none`, `float outline`, `Color outlineColor = Color.white` and `float soft`. A game can't declare the name outside a namespace.
  - A shape's fill goes from `color` at its top to `color2` at its bottom: its own top and bottom, as it is before `rotation` turns it, the top being the upper one on the screen in the world's units and in pixels alike. `Color.none` for `color2` is one color. The colors in between are blended with their alpha in them, so a fill to a clear color fades and doesn't darken.
  - `outline` is an outline that wide in `outlineColor`, inside the edge, over the fill.
  - `soft` is how far the edge fades. Above 0, outwards: the shape is whole up to its edge, and its color goes on past it, gone `soft` further out (a glow, a shadow). Under 0, inwards: its color is at its edge and gone that far in, with the middle clear. The fade is squared, so it ends smoothly.
  - `Color.none` is a color constant with an alpha under 0, `Color(0, 0, 0, -1)`: exactly that value is none, and drawn with, it shows nothing.
  - The engine's own shaders draw every shape. They take no vert and no frag.
  - Shapes of any kinds drawn one after another are one draw call.
- `Vertex` is a built-in struct: `float2 position`, `float2 uv` and `Color color = Color.white`, so a corner that leaves its color out takes the texture's as it is. `Filter` is a built-in enum, `Point` and `Bilinear`, with the values of Unity's `FilterMode`. A game can't declare either name outside a namespace.
- Triangles draw whichever way round their corners go. A triangle with an index past the vertices is left out, and so are the one or two indices after the last whole triangle. Colors blend over what's behind them by their alpha. A color is written as it is, never times its alpha (see Alpha and color space).
- `uv` (0, 0) is the outer corner of the grid's cell (0, 0), and (1, 1) the outer corner of its last cell. Past them, it reads the cells at the edge.
- The GPU's copy of a texture is a byte a channel: a cell's floats are clamped to 0 to 1, and NaN is 0. Where the grid has no chunk, it's clear. A grid with an open axis has no size, so it's no texture, and the mesh draws with its colors alone. How big a texture can be is the GPU's limit, at least 2048 by 2048.
- The engine tells whether a grid's cells changed from the hashes it keeps of the grid's chunks, without reading the cells: a frame that changed nothing costs a hash per chunk. A texture no call of a frame draws with is let go, and comes back when one does.
- Views read the match's grids as they are at the latest tick: cells aren't blended.
- `[a, b, c]` where a built-in function takes a list is that list, as it is for a function's argument: `Draw.Mesh(corners, [0, 1, 2])`.
- A clip's `Rect` goes from (x, y) to (x + width, y + height) in the units the calls are in: the world's under a camera, with `y` up, and pixels on the screen. It's where it lands on the screen then, to whole pixels, and stays there when the camera changes. `Draw.Clear` fills the clip. Each frame starts with none, and a view's clip never clips the GUI.
- `Draw.SetCamera` after `Draw.SetScreen()` goes back to the world.
- 3D meshes go through a camera of their own, which the 2D one and the screen leave as it is, and the other calls go through theirs whatever the 3D camera is.
- GPU code (`frag`, `vert` and the functions they call):
  - **`color` starts as `Color.white`** (to confirm). It isn't the pixel already on the screen, which a GPU doesn't let a frag read, though `mut` elsewhere means reading what's there and changing it. A frag that never writes it draws white.
  - `frag` and `vert` are keywords only at the start of a declaration, like `view`. They share their names with functions: a frag and a function can't have the same name. Nothing calls one, and its name is no value: `Draw.Mesh`, `Draw.Instanced`, a target's `Mesh` and `Instanced`, and `Draw.Blit` name it. `local` before one is an error, as it's local already.
  - `Draw.Mesh(vertices, indices[, texture[, filter]][, vert][, frag])` draws a 2D mesh, and `Draw.Mesh(vertices, indices, transform[, texture[, filter]][, vert][, frag])` a 3D one. The vertices are a list of a struct with a `position`: a `float2` makes the mesh 2D and a `float3` 3D. Its other fields are floats, ints, their vectors and colors, 12 fields at most, and the GPU reads them from the list as they are. A 3D mesh takes its transform, and a 2D one takes none. Without a frag, the texture and filter sample the vertex's `float2 uv`. A frag or vert with a read-only `Texture` parameter receives an explicit Texture argument; a frag without that parameter samples only its component/singleton fields and rejects an explicit source. With no frag, a field named `color` has to be a `Color`. A `List<Vertex>` or a `List<Vertex3>` with neither a vert nor a frag is drawn as it was before.
  - `Draw.Instanced(vertices, indices, instances[, texture[, filter]][, vert][, frag])` replaces `Draw.Mesh`'s single 3D transform with a list. A `List<T>` of an ordinary struct supplies each item to the vert or frag's read-only `T` parameter after its vertex (and a frag's color); either stage can take it, or both. A `List<float4x4>` supplies transforms for a 3D mesh. The instance parameter's type is the list's element type; a struct parameter of another type is the draw's values (above), or an error at the draw. `Mesh` supplies no instance parameter. Other component and singleton parameters bind as before.
  - A 3D instance struct's field named `transform`, when present, is a `float4x4`, applied by the engine and available as `Instance.transform`; without one, the transform is identity. A 2D instance list holds ordinary structs, whose fields a vert can use to place the vertices. Their `transform` field is ordinary data, not applied by the engine, and `Instance.transform` is identity. Only fields the shaders read go to the GPU, plus a 3D transform; unread fields can have types the GPU cannot use. Reading a field or passing the instance whole follows the same rules as component reads. An empty instance list records nothing. Items draw in list order, in one instanced run, which adjacent compatible mesh draws can join.
  - A draw that names a vert or a frag is in the view itself, not in a function the view calls: the components they read are the view's entity's. A function draws a mesh of any vertex struct without them.
  - The view needn't take what its frags and verts read: `with Box` is enough for a frag that reads `Box`, and a singleton needs no mention. The engine hands them over as it does the view's own parameters, blended between ticks the same way, and a view whose frag reads the match doesn't run outside a match. What the view does take is sent as the view has it when it draws.
  - Only the fields the code reads are sent: a component's with each instance, and a singleton's once for the draw. Reading a parameter whole, as a value passed on, sends all of its fields.
  - A `local` field of the match's component or singleton (see Local state) is read like any other field of it: what this machine keeps for the entity being drawn, or the field's default where nothing was written.
  - A frag or vert takes no `World`: the GPU is sent the components of the entity being drawn, and the view copies what it needs of others into a component of it or a singleton.
  - **`this` is the pixel a frag runs for** (agreed 2026-10-07), without a parameter: `this.position`, a `float2` in the screen's logical pixels from the top left, as `Draw.Screen`'s; `this.depth`, a `float` from 0 at the camera's near side to 1; and `this.world`, a `float3`, where on the mesh it is in the world: the vertex's position after the vert, through the draw's transform, blended. A 2D mesh's is its position in the units it's drawn in, with `z` 0. These fields are read-only, and the whole pixel context can't be copied, stored or passed: `this` is accessed through its fields and `Discard()`. A vert has no pixel yet: its `this` is its vertex as the engine would place it, with `this.clip` alone (Where a vert puts its vertex, above). Ordinary functions a frag or a vert calls have no implicit `this`: pass the coordinates they need as ordinary values.
  - **`Instance instance`** (to confirm) is a parameter a frag or a vert can take: `instance.transform` is the `float4x4` the draw was given, and the identity for a 2D mesh.
  - **`instance.Normal(normal)`** (agreed 2026-10-08, of three ways offered: a member of `Instance`, over a `Math` function of any matrix and a normal matrix the engine would supply beside the transform) gives a normal of the mesh, a `float3` of any length, as it is in the world, in the vert or in the frag: `float3 normal = instance.Normal(vertex.normal);`. It's right whatever the draw's transform scales the mesh by. `Math.Rotate(instance.transform, normal)` turns a direction, which is the same thing only for a mesh scaled as much every way: on a ball drawn as an egg, or a cylinder as a flat disc, it leans normals the wrong way. Provisional, Claude's picks:
    - The result is of length 1, and zero for a normal of no length, as `Math.Normalize`'s is.
    - A mesh that a scale under 0 turns inside out has its normals pointing out of it.
    - A mesh flattened to nothing along an axis, whose transform has no inverse, has that axis for its normals, each to the side it leaned.
    - A 2D mesh has no transform: its normal comes back as it is, of length 1.
    - It goes by the draw's transform (through `Draw.Transform`, as `instance.transform` is), and nothing else: a vert that moves its vertices itself works out its own normals.
    - Nothing is sent for it. The GPU works it out from the transform that's sent already (the cross products of its columns, once a vertex, and no inverse), so it costs an instance no bytes, and a pixel less than `Math.Rotate` does. A frag that calls it counts three more of the values a draw's pixels read from the mesh's corners.
    - `Instance` has no other method, and a call of another name is an error that names this one.
  - `Instance` is built in only as a frag's or vert's parameter type. A game that declares a type of its own named `Instance` or `Pixel` gets its own there. An old built-in `Pixel pixel` parameter is an error that says to remove the parameter and use `this`.
  - The camera isn't given: a game keeps where its camera is in its own state, which a frag takes like any singleton.
  - **`this.Discard()`** leaves the pixel undrawn, with no color, depth or stencil written, and ends the frag there. It's a statement of its own, in the frag itself. A discarded path owes its `out` parameters nothing.
  - `source.Sample(uv[, filter[, wrap]])` reads a `Texture`, `Grid2<Color>` or `Grid2<float>` field of a component/singleton parameter, an asset by its name, or a direct read-only `Texture` parameter. `Filter.Bilinear` blends between pixels, `Filter.Point` reads the nearest and `Filter.Trilinear` blends between the texture's smaller images too (see Mipmaps, above, for what its `uv` can be); one that's left out is the texture's own (see Textures, above). A filter and a wrap that are said are written there: the GPU is told how the texture is read before any pixel runs. UVs go from (0, 0) at the top-left outer corner to (1, 1) past the bottom-right pixel, and what's past them is the wrap's to say: the pixels at the edge without one (see `Wrap`, above). A grid with no size samples white. Verts sample too.
  - What the GPU runs: bools, ints, floats, vectors, matrices, colors, enums, and structs of those; locals; `if`; `?:` (between structs, matrices and arrays too, since 2026-10-08, where it's a function of the engine's own for the type in the shader: both sides are worked out, as WGSL's `select` works out a number's); `for` loops that count (a variable of the loop's own, compared with a bound, that only its step changes), with `break` and `continue`; `return;`; the game's functions, methods and operators; `Math`, and the math types with what builds them (next); constructors, constants and `default`. `Random`'s rolls on the type and `Random.seed` (see Random numbers: a generator of the pixel's or the vertex's own, with 32 bits of state). No `Random` value: its state is 64 bits, and a frag or vert that makes one, takes one or rolls with one is an error that says to roll on the type.
  - **Everything in `Math` is on the GPU unless it makes no sense there**, and so are the types it works on and what builds them: vectors, quaternions and matrices, every `Math` function on them (`Math.Hash`, the noise, `Math.Perlin`, `Simplex` and `Cellular`, whose cells are the CPU's, the quaternions' `Mul`, `Rotate`, `Inverse`, `Conjugate`, `Normalize`, `Dot`, `Slerp`, `Nlerp`, `Forward`, `Up`, `Right` and `Angle`, the matrices' `Inverse` and `Determinant`), `quaternion.AxisAngle`, `Euler` and `LookRotation`, `float4x4.TRS`, `Translate`, `PerspectiveFov` and `Ortho`, and the constructors between quaternions and matrices. What's left out is left out for a reason, which its error says with what to write instead. There's one: `Math.Hash` of a `long`, as these GPUs have no 64-bit integers, so no `long` gets there. `Math.Hash(int2(int(x), int(x >> 32)))` is the same number as `Math.Hash(x)`, from halves the CPU split.
    - `Math.Hash` gives exactly the CPU's number there: it's integer math, which wraps the same in every shader language.
    - What works on floats follows the CPU's formulas (`tide/math.h`'s, in its order, so the same handedness and argument order) with the GPU's rounding, which differs between GPUs and from the CPU, most for big arguments: the sine of a number in the thousands is no number to build on.
    - It's forgiving as on the CPU. `Math.Normalize` gives zero for a vector too short to normalize, and the identity for a quaternion. `Math.Min` and `Math.Max` with one NaN give the other. `Math.Clamp` and `Math.Saturate` always give a value in range: its lower bound for NaN, and for bounds the wrong way around. A shader's compiler may reckon without NaN, so these tell one by its bits.
    - It gives a number wherever the CPU does, which the shader languages' own functions leave open: `Math.Pow` of a number under 0 to a whole power, and of anything to the power of 0, which is 1; `Math.Atan2(0, 0)`, which is 0; `Math.SmoothStep` with its edges the wrong way around, or at the same place; `Math.Inverse` of a matrix, which one of the languages has no function for. Where the CPU gives no number (NaN or an infinity: `Sqrt` and `Log` of a number under 0, `Asin` and `Acos` outside -1 to 1, `Pow` of a number under 0 to a power that isn't whole, the inverse of a matrix that has none), the GPU gives whatever it gives.
  - `switch` on an int, a char or an enum, as each shader language has its own.
  - **Lists and arrays on the GPU** (the owner, 2026-10-08: "sending collections to GPU side; and in general trying to make anything that is possible on there too to close the gap", and of how a list travels, that a fixed size "is the annoying part"). That lists get there, with no fixed size, and on every backend there is, is the owner's; each rule here is Claude's pick, Provisional.
    - A list that's a field of a component, a singleton or a draw's values, or a `List<byte>` asset by its name, is read in a frag or a vert: `items[i]`, `items.count` and `foreach (var item in items)`. An element past its ends is zeros. Its elements are what the GPU has values of, bytes and arrays among them; not `long`, text, entities or containers.
    - A grid that's such a field, or an asset by its name, is read a cell at a time: `cells[x, y]`, `cells[at]` and `cells.size` for a `Grid2`, `cells[x, y, z]`, `cells[at]` for an `int3` and a `size` of three for a `Grid3`; the cell as the grid has it, of any type the GPU has values of; zeros outside it and where nothing was set. It's another of the draw's textures than the same grid sampled.
    - A grid with no size on an axis is read the same way (2026-10-08, to close what was a silent difference: the GPU read zeros where the CPU read the cells): cells at any place an int says, before 0 too, and `size` 0 on that axis, as on the CPU.
    - What's sent of a grid is the box its chunks take up, within its size, as one image of numbers; a `Grid3`'s layers are side by side in it. A box whose image would be more than 16,384 pixels a side or 16,777,216 in all isn't sent: the code reads zeros, and a line on stderr says so once. Within 2,048 pixels a side, which every backend has, are 256 by 256 by 256 cells of one number each and 128 by 128 by 128 colors.
    - `byte` is there (the owner, 2026-10-08: "`List<byte>` why?"): an int from 0 to 255, which `byte(x)` and a compound assignment keep to its low 8 bits, as on the CPU.
    - `while`, and a `for` of any shape, are there (the owner, 2026-10-08: "`while` can run forever but so can a stupid for loop"). Nothing bounds a loop.
    - The owner's rule for all of it (2026-10-08): "i want GPU code to be as close to non GPU code". What's refused there is refused because a GPU can't, and its error says why.
    - It's read-only.
    - **A list or a grid has other names there: a local, and a function's or a method's parameter** (built 2026-10-08, from the rule above; every rule is Claude's pick, Provisional). Which list or grid a name stands for is known as the shader is written, never as it runs: neither GLSL ES 3 nor WGSL hands a texture to a function on every backend, so a function that takes one is written once for each list or grid it's called with, and takes nothing for it.
      - `var items = lights.items;` names a field of what the frag or vert takes, an asset, another such local or a parameter. It isn't `mut`, and it isn't a list that's picked or made as the code runs (`flag ? a : b`, a function's result, a literal): an error that says a list there is one the code was given, by a name.
      - `float Sum(List<float> numbers)` is the function the CPU's code calls too. On the GPU its argument is such a name, and the parameter isn't `mut` or `out`. A function passes its own on.
      - `int Cell(Grid2<int> cells, int2 at)`: a function or a method takes a grid, to read `cells[x, y]` and `cells.size`. Such a function, and one that calls it, is the GPU's alone: called from a system, a view or a handler it's an error that says to read the cells through the grid's component or singleton, and no C is written for it. Its grid parameter takes no `mut`, `out` or default, and an operator, an extern, an async function and one that takes an Action take none. A grid's local is in a frag, a vert or a function that takes a grid; anywhere else it's the error it was.
      - An asset's list or grid is read by its name in a function a frag or vert calls, too.
      - A function still returns no list and no grid, and a struct that holds one isn't a value there.
    - **A function samples the textures it takes** (built 2026-10-08. The owner asked that a function only GPU code calls take a `DepthTexture` and call `Sample` and `Compare` on it, and the same for a `Texture`, with a call from the CPU an error that says so, after Tag's and Sumo's frags each carried a copy of one shadow lookup; a `Cubemap` and a grid by the same rule, and every other rule here, are Claude's picks, Provisional).
      - `float Lit(DepthTexture shadow, float3 at)`, `Color Tile(Texture image, float2 uv)`, `Color Above(Cubemap sky, float3 way)`: a function or a method reads a texture parameter as a frag reads a field, `Sample` with a filter and a wrap or without, a depth's `Compare`, a cube's `Sample(direction)`. A `Grid2<Color>` or `Grid2<float>` parameter is sampled too, and goes where a `Texture` parameter is.
      - The call names the texture: a field of a component or a singleton the frag or vert takes, its own `Texture`, an asset, or in a function, a parameter of its own, which it passes on. One that's picked or made as the code runs (`flag ? a : b`, a function's result) is an error that says a texture there is one the frag or vert was given, by a name. Which texture it is is known as the shader is written, so the function is written once for each texture it's called with and takes nothing for it, as for a list.
      - What a `Sample` there leaves out is the texture's own, as in a frag: its declaration's for a grid's field or an asset, and for a `Texture` field or the frag's own `Texture`, whatever the texture it holds says. Each way each texture is read is one of the draw's 8.
      - A function samples its own parameter, or an asset by its name. A texture in a singleton or a component the function takes is an error that says to take the texture itself: a struct with a texture in it is no value on the GPU.
      - **Such a function, and one that calls it, is the GPU's alone**: no C is written for it, and a call from a system, a view or a handler is an error that says so ("'Lit' reads a render target's depth, which only code the GPU runs does"), with where it reads it. A function that takes a texture and samples none is still everyone's: the CPU's hands it on (to C, say), and the GPU's is written with nothing for it. An operator, an extern, an async function, an `audio` declaration and a function that takes an Action sample nothing, as they take no grid.
      - A texture parameter that's sampled takes no `mut` or `out`; none is returned, kept in a local or in a struct.
      - **A function's `Sample` reads the texture's own pixels, never between its smaller images.** A frag asks how fast a `uv` changes as it starts, of a `uv` it can work out there, and a function's `uv` is its call's. So in what a frag reaches, a `Sample` in a function that says `Filter.Trilinear`, or leaves its filter to a texture whose declaration says it, is an error, and one that leaves it to a `Texture` that may say it, in a game that names `Filter.Trilinear`, is a warning, said once: what a frag's own `Sample` gets for a `uv` it can't work out as it starts. A vert has no pixels to compare, there or in a function. Argued against reading blended without a word: a texture that shimmers is found late.
      - A `[Raw]` texture is read by its name in a function, as in a frag, and isn't passed to one as a `Texture`.
      - A grid parameter is one of the draw's textures of numbers whether the function reads its cells or not, as it was, and another where it's sampled: a function that only samples takes a `Texture`.
      - Not there: a texture's local (`var shadow = lamp.shadow;`), and `Filter.Trilinear` through a function, which would take working the `uv` out at the frag's start from the call's arguments.
    - No size is declared, and none is fixed: a list is sent as it is that frame, up to 2,097,152 pixels of four numbers each (an element of one or two numbers shares a pixel with the next; a bigger one takes whole pixels), and elements past that aren't sent.
    - Each list a draw reads counts as one of its 8 textures, and its count is among the draw's values. A list that didn't change isn't sent again.
    - **What's sent is what's read, and what changed** (built 2026-10-08; the owner: "only sending data that we actually use vs the entire thing, or upload only what changes"; each rule is Claude's pick, Provisional).
      - Of an element (a list's, a grid's cell, a map's value), the parts that some frag or vert of the game reads are sent, and no others. A part is a struct's field, a struct's own fields each apart, and anything else whole (a vector, a matrix, an array). It's one layout for the game, the parts any of its code reads, so every draw that reads the list shares one texture.
      - A part is read by a chain of fields (`lights[i].notch.deep`), through a `foreach`'s variable, and through a local that isn't `mut` and holds an element or a part of one (`var light = lights[i];`, `var notch = light.notch;`), by what the code then reads of the local. Anything else that takes an element reads all of it: an argument, a result, an operator, a `mut` local, `TryGet`'s `out` value. A function that takes a list reads of each list it's called with what it reads of its parameter.
      - A map's key is always sent. A list that's only counted, and a grid only asked its size, send nothing.
      - What isn't sent is zeros in the shader, which nothing reads, so no code can tell.
      - Of a list or a grid that changed, the elements and cells that changed are sent, to the element and the cell, and the rest stay as the GPU has them. A list's count that shrinks sends zeros over what's gone. A grid that's a texture (sampled) is sent the same way, the pixels of the cells that changed.
      - A list made along the way (a draw's values' `[a, b]`) is by what's in it, as before: another content is another texture, sent whole.
    - A component's list is its entity's, so each such entity is a draw of its own. An instance struct's list that the code reads is an error at the draw.
    - A fixed-length array is a value on the GPU as a struct is: a field that's sent with what else the code reads (its elements' numbers, among a draw's 3,984 bytes or an instance's 16 values), a local, a parameter, a result, an array literal. `a[i]` past its ends is zeros, `a[i] = v` there does nothing, `a.count`, `foreach`. An array of arrays is a value the same way (2026-10-08, by the owner's rule that GPU code refuses only what a GPU can't do; how is Claude's pick): `float[4][8]`, each index checked by itself, so a read past either end is zeros and a write there does nothing, and `rows[i]` is a row like any array. A `for`'s step that writes an element runs after each round, a `continue`'s too. None in what a vert hands its frag or in a vertex. Not there, as on the CPU, where the checker refuses them: `==` between arrays, and an array's element as a `mut` argument.
    - **Maps and sets** (built 2026-10-08, asked for as read-only maps and sets on the GPU for a field of a component, a singleton or a draw's values; each rule is Claude's pick, Provisional). A `Map<K, V>` or a `Set<K>` that's such a field is read in a frag or a vert as it's read anywhere: `map[key]` (zeros for a key that isn't there), `Has(key)`, `map.TryGet(key, out value)` (the value type's default for a key that isn't there), `count`, and `foreach`, a map's entries with `key` and `value` and a set's keys, in the order the keys were added, without the removed ones.
      - Keys there are `int`, `byte`, `char` and enums: the keys the CPU has that the GPU has values of. Text, `long`, `Entity` and `PlayerID` keys are an error where the map is read, which says which and why.
      - Values are what a list's elements can be.
      - A local names one and a function takes one as a list's does, by the same rules: no `mut`, none made, returned or picked as the code runs.
      - Read-only. `Add`, `Remove`, `Clear` and assigning `map[key]` are errors, and so is `Get`, whose `V?` the GPU has none of.
      - `TryGet`'s `out` argument is a whole local there (`out var value`, or a variable), not a field of one: a pick, as WGSL hands a function a pointer to a whole variable.
      - It's one of the draw's 8 textures, and two of its values (how many entries, and how many slots its index has). What's sent is the live entries, in order, and an index made for the GPU: a lookup hashes the key with `Math.Hash`, which is the same number on the CPU and the GPU, and probes. Not the CPU's own index, whose hash is of 64 bits, which these GPUs can't compute; the answers are the same, as keys are compared by their bits on both.
      - A map that didn't change isn't sent again. One that changed is made again whole, as a key that's added or removed moves what's after it, and the rows of its texture that came out otherwise are sent.
    - Not built: a list, a map or a grid as a function's result, a `mut` local, or one picked as the code runs; `Sample` of a grid or a texture through a local; a glob's list of assets, and an asset's text; a grid's local in a function that takes no grid; a map's `Get`, and `TryGet` into a field; `foreach` over a grid's cells (on the CPU a grid with an open axis goes through the chunks it has, which the GPU isn't told); storage buffers where a backend has them, which wait for Vulkan.
  - Never there: making or changing a list, a map or a set, maps and sets keyed by text, longs, entities or players, text, entities and players, `long`, `Rect`, spawning, sending and the other changes to the world, `Draw`, the GUI, `Screen`, `Devices`, `Cursor`, `Frame`, `Debug`, C, tasks, errors and `T?`, recursion, and grids used any other way than sampled. A frag's `this` is its pixel; verts and ordinary functions have no `this`. Each unavailable feature is an error where it's written, which says what to do instead, and for code in a function, which frag or vert reaches it. Bytes and longs should be converted to an `int` or `float` in local code before being supplied to a shader.
  - A function a frag or vert calls takes `mut` and `out` parameters as anywhere, and a call that changes a variable that isn't a whole one (a field, a swizzle) is a statement of its own there.
  - A frag can't read an int field of its vertex, as ints don't blend across a triangle. A vert can.
  - What a GPU takes, on every backend, is checked at the draw, which says what the draw reads and what to do about it: 16 values for each vertex (each field of the vertex struct, 4 for a 3D mesh's transform, and one for every 4 floats or 4 ints the code reads of components and an explicit instance struct); 15 values from the mesh's corners to its pixels (each field of the vertex the frag reads, the pixel's place in the world, and one for every 4 floats or 4 ints the frag reads of components and an explicit instance struct); 3984 bytes of singletons' fields for a draw; and 8 texture slots, an image sampled with both filters counting twice.
  - Instances of one mesh drawn in a row through the same vert and frag, with the same singletons' values and the same grids, are one draw.
  - A function both the CPU and the GPU run compiles for both.
  - `tide shaders` (`tidec --shaders`) prints each shader, as GLSL and as WGSL. No game sees a shader compiler's error: what tidec writes always compiles.
- Colors: `+`, `-` and `*` between two colors work channel by channel, alpha too, and a color times a number, or divided by one, scales every channel.
- `Draw.SetCamera(position, rotation, fieldOfView)` (and `Draw.Camera` with the same, around a block) is a perspective camera: at `position`, looking along `rotation`'s `+z` with its `+x` to the right of the screen and `+y` up, as Unity's, and seeing `fieldOfView` degrees up and down (clamped to 0.00001 to 179, as Unity does), as much across as the screen's shape gives. It sees from 0.3 units in front of it on, as Unity's cameras, and has no far side, which costs depth precision next to nothing. Each frame starts with one at the origin, looking along `+z`, with 60 degrees.
- `Draw.SetCamera(transform, projection)` is any camera: `transform` places it in the world, looking along its `+z`, and `projection` takes what it sees, looking down `-z` (OpenGL's and Unity's view space), to clip space, the screen's shape included. `Screen.width / Screen.height` is the shape.
- `Vertex3` is a built-in struct: `float3 position`, `float2 uv` and `Color color = Color.white`. A game can't declare the name outside a namespace.
- 3D meshes are the only calls that have depth. Unless `Draw.Depth` says otherwise (see Draw settings), they read and write it: they hide each other by how far they are, whatever their order, and a see-through one blends with what's behind it and writes depth like any other. Every other call draws over what's there, in order. `Draw.Clear` clears depth too, within the clip. Textures and filters are as a mesh's, and there's no lighting. Unlike a 2D mesh's, a triangle is drawn from its front alone, unless `Draw.Cull` says otherwise (see Draw settings).
- A 3D mesh is its two lists. The engine keeps a copy of each it draws, on the GPU too, and only copies it again when it changed: a world's list tells from the hashes its heap keeps of its pages (a list in chunks, of each chunk's), so drawing one that didn't change costs a hash for each of its pages; a list made along the way is read. A mesh no call of a frame draws is let go. A mesh that changed is another copy, so what a frame drew before it changed stays as it was.
- Instances of one 3D mesh drawn in a row, with the same texture and filter and nothing between them, are one draw: a view that runs for each entity and draws one mesh draws them all at once. Lists made along the way are the same mesh when they hold the same.
- `Color` is a built-in value type with `r`, `g`, `b` and `a`, floats from 0 to 1, as in Unity. Its constructor takes any mix of numeric scalars and vectors totaling 3 or 4 components, in order: `Color(r, g, b)`, `Color(uv.xy, 0)` and `Color(rgb)` have alpha 1; `Color(r, g, b, a)`, `Color(rgb, a)` and `Color(rgba)` supply it. Ints and int vectors widen to floats, and each argument is evaluated once, left to right. The constants are `Color.white`, `black`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray` and `clear`, with Unity's values and names, and `Color.none`, Tide's own: no color, where one can be left out (see the smooth shapes, above). Components and singletons can hold colors, and colors add, subtract, multiply and scale (below).
- Text is written in double quotes, with the escapes `\"`, `\\` and `\n`. Its type is `string` (see Text).
- Blending: a view's match components and singletons are copies, their fields that blend set between last tick's value and this tick's, as far as this moment is between the two ticks. Their local fields aren't part of the copy: a view reads and writes those where they are (see Local state). Vectors, matrices, colors and rects blend component by component, quaternions the short way round (normalized), and structs field by field. An entity that wasn't there last tick is drawn as it is. Local state isn't blended: it's this machine's, as it is.
- `[Snap]` goes on floats, vectors, quaternions, colors and rects of components, singletons and structs; on anything else it's an error that says why.
- `entity.Snap()` is recorded and applied at the end of the tick, like `Destroy`, so it never makes systems wait. Each entity counts its snaps, and views only blend it between two ticks with the same count. `singleton.Snap()` needs the singleton as a `mut` parameter. Local state is never blended, so snapping it is an error, and so is `component.Snap()`, which says to snap the entity.
- `Interpolate` isn't for code to call. Singletons have no methods, so a singleton blends its own way through a struct in it that has an `Interpolate`. A `[Snap]` field of such a type is still drawn as it is.
- Hosts pass the two ticks and how far between them this moment is to `tide_frame`, with the last world the server confirmed; sessions work those out (`tide_session_view`).
- The cursor's lock:
  - **`Cursor.locked` is read in the input's `Sample` too** (proposed, not yet agreed), so a game looks only while the lock holds without a frame's delay: `if (Cursor.locked) look = devices.mouse.delta * 0.003;`. `Sample` reads it of the devices it samples, so it can't call `Cursor.Lock()`, and `Sanitize` can't read it, as it can't read `Devices`. A function `Sample` calls can't either: it's passed what it needs.
  - Where neither goes, the error says where it does: the match is told that a view calls `Cursor.Lock()` and that `Sample` reads `Cursor.locked`; a task, a method and a local handler, that views ask every frame. `Cursor.Unlock()` is told that the cursor comes back once no view asks.
  - A function that uses `Cursor` needs the frame, like one that draws: views and the functions they call can call it.
  - While a modal is up (`GUILayout.Modal`), the lock isn't asked for, whatever views ask: its widgets need the pointer. It's asked for again once the modal closes.
  - A finger isn't the mouse: it presses the widgets it touches while the mouse's cursor is locked.
  - A mouse button still held as the cursor comes back (the browser took the lock at Escape while the player fired) was pressed for the game: it presses no widget, and letting it go on one is no click.
  - `Cursor` is built into the language, as `Screen` and `GUI` are: a game can't declare the name, in a namespace or outside one.
  - On the web, Escape never asks for the lock again, nor does a held key repeating. The page asks for the mouse's own movement (`unadjustedMovement`) and, where the system has none, for the usual kind. A lock the browser gives after the game stopped wanting it is let go at once. On desktop, the mouse's own movement is GLFW's raw motion, where the system supports it.
  - While it's locked, the position that stays is where the cursor was when it was locked, and the cursor comes back there.
  - Hosts pass the frame's ask on with `tide_platform_cursor_lock(tide_gui_cursor_lock(gui))`, after `tide_gui_end`, and `tide_platform_poll` says whether the window holds the cursor: `tide_devices.mouse.locked`, which `Cursor.locked` reads, in views of the frame's devices and in `Sample` of the ones it samples.

```csharp
view DrawBalls(Body body, Ball ball)
{
    Draw.Circle(body.position, body.radius, ball.color);
}

view DrawHud(Arena arena)
{
    Draw.SetCamera(float2(0, 0), arena.halfSize.y);
    Draw.Text("fire: space", float2(-arena.halfSize.x, arena.halfSize.y), 18, Color.gray);
}
```

### Open

- Drawing from systems, with the prediction stage (verified, predicted, replayed) visible to the code.
- **Blending confirmed state.** A `verified` value is as the last confirmed tick has it, so it steps at the tick rate. Showing other players from confirmed state, blended between confirmed ticks rather than predicted ahead (a competitive shooter wants their positions not extrapolated), needs that, and may belong at another level than a parameter's modifier.
- `verified World`: the confirmed world's other entities, by their handles.
- Queries in local code: `mut` on a binding, to write a row's local fields, and reading them through a row; `with ownership` in a query; a local handler's rows as each event's turn left things, where they're the tick's once it's done.
- Views reading input, for example to draw where the local player aims before the tick runs.
- GPU code: which side of a triangle a pixel is on; the camera; a frag's output of another type than a `Color`; code that computes rather than draws; `while` and `switch` there; SPIR-V, with Vulkan. What was agreed of the GPU side, built since, is in What the GPU side gains.
- Target formats beyond the three: floats of 32 bits, which keep a place in a big world where 16 bits don't (WebGPU neither blends them nor reads them blended without features a device may lack, and OpenGL ES 3 needs more extensions for the same, so they'd be read at the nearest pixel and replace what's there, everywhere); images of one or two channels, which take less memory for a mask, a speed or a shadow's two moments; and a byte format that isn't a color's.
- A blend for each of a frag's outputs (OpenGL ES 3 and WebGL 2 have one for a draw); a clear's color for each of a target's images, and past 1 for a float one; and whether a game can ask if `Format.Rgba16f` images are floats on this machine, to draw another way where they aren't, which would be the first thing a game reads of the machine it runs on.
- A `DepthTexture` as a draw's own source, for a pass over the screen that reads a scene's depth with no singleton between.
- The cameras' matrices (see Provisional, The cameras as matrices): a camera's matrix in code the GPU runs with nothing handed over (`pixel` or a built-in for the draw's own camera); the inverses, from clip space back to the world; whether a 2D camera should also give a `float4x4` to clip space, for a 2D shader's own arithmetic.
- 3D: meshes in the grid's chunks, for voxels.
- The cursor: hiding it without locking it, keeping it in the window without hiding it, and images for it; its lock on Android.
- Layers, and a sprite in one call (a textured rectangle is a mesh of four corners for now).
- Textures: a pixel of a byte a channel, a quarter of a `Color`'s memory; sending only the part that changed. Pixels from image files are assets (see Assets).

## GUI

### Decided

- The GUI is immediate mode, called from views, and drawn over the world. It follows Unity's IMGUI.
- `GUILayout` lays widgets out automatically: they stack top to bottom, and `GUILayout.Horizontal()` puts them side by side. `GUI` has the same widgets, each at an explicit `Rect`, which comes first, as in Unity: `GUI.Button(rect, "Quit")`.
- Containers take a block, and they're ordinary functions with an `Action` parameter (see Functions): `GUILayout.Horizontal() { ... }`, `GUILayout.Vertical() { ... }` and `GUILayout.Area(...) { ... }`. Calls always have parentheses.
- Widgets edit values through `mut` parameters, and return whether the value changed: `GUILayout.Toggle("Fullscreen", settings.fullscreen)`. A button returns whether it was pressed.
- **Entered fields** (agreed 2026-10-06): a view can tell that the player pressed Enter in a field they typed into. `TextField`, `IntField` and `FloatField` each have a second version, in `GUILayout` and in `GUI`, with an `out bool entered` parameter (see Out parameters): `GUILayout.TextField("Code", lobby.code, out var entered)`. It's true in the frame Enter ends the typing in that field. What the field returns stays whether the value changed, and without the argument the field is the one there always was. The details are picks, under Provisional.
- Widgets have no IDs to write. They're told apart by where they're called from and the entity the view runs for.
- The engine builds nothing a game couldn't build itself: containers are functions with an `Action`, and widgets are made of pieces games can use too.
- Gamepad and keyboard navigation are built in: focus moves between widgets, the south button presses, the east button goes back.
- Whatever the GUI is using, such as a click on a button or typing in a field, is hidden from the input's `Sample`.
- Typing into a field uses the characters the player types, which follow their keyboard layout, not keys by position.
- `Screen.width` and `Screen.height` are the window's size.
- The widgets: `Label`, `Button`, `Toggle`, `Slider`, `IntSlider`, `TextField`, `IntField`, `FloatField`, `Float2Field`, `Float3Field`, `Float4Field`, `ColorField` and `Space`, and the containers `Horizontal`, `Vertical`, `Area` and `Modal`.
- `GUILayout.Modal(anchor, mut bool open) { ... }` is a panel over the whole screen while `open` is true, like a pause menu. While it's up, it has the focus, the widgets outside it don't work, the game and views get nothing from the devices, and back (Escape or the east button) closes it.
- **Controls:** a view's own widgets are controls. `GUI.Control(rect)` is a widget the view draws itself, however it likes: the GUI handles its input, and draws nothing for it, so showing that it's hovered or has the focus is the view's. It returns a small built-in value that says whether the rect is hovered, held, focused and clicked.
  - It takes the pointer for its rect by itself, as the GUI's own widgets do: a press on it never reaches the input's `Sample` or the other views, from the frame it lands in.
  - It joins Tab and Shift+Tab, arrow, d-pad and left stick navigation alongside the built-in widgets, in the order they're called. Enter, Space and the gamepad's south button click it.
  - `GUI.Disabled(...) { }` and modals stop it like any widget.
  - What a view draws is under the GUI's own widgets, and under a modal's dim, since the GUI draws last.
- **Themes:** the GUI stays bare: one look, and a simple theme for it. `GUITheme` is a built-in struct of the colors the GUI's widgets use and their corners' radius, and nothing else: no styles for single widgets, no text sizes, no fonts. Its defaults are the look the widgets have without one, so a theme names only what it changes, and a game that sets none looks as it always did.
  - `GUI.Theme(theme) { ... }` wraps a block: its widgets have the theme, and the look it found is back when the block ends. `GUI.SetTheme(theme)` stays for the widgets that follow, in this view and the ones after it, until it's set again or the frame ends. Every frame starts from the defaults. It's the rule for every setting of a frame with two forms, `Draw`'s too: the plain name around a block, and `Set` for what follows.
  - Round corners are `Draw`'s round rects.
  - The stats overlay and the debug views always keep their own look.
- **Claims:** a view with widgets that read the devices themselves, like a C library's, says what they're using: `GUI.ClaimPointer()` and `GUI.ClaimKeyboard()`. What's claimed is hidden from the input's `Sample` and from the other views, as what the GUI uses is, and the view that claimed it goes on reading it.
- A claim lasts one frame. The view makes it again every frame its widget uses the device, as immediate-mode code draws every frame, so a view that stops running leaves nothing claimed.
- `GUI.ShowKeyboard()` shows a phone's keyboard, as typing into a field does, each frame it's called. It's apart from the keyboard's claim, so a claim for a widget's shortcuts doesn't bring the keyboard up.

```csharp
local singleton Settings
{
    bool open;
    bool fullscreen;
    float volume = 1;
}

view Options(Devices devices, mut Settings settings)
{
    if (GUI.Button(Rect(Screen.width - 210, 10, 200, 40), "Options")) settings.open = true;
    if (devices.keyboard.escape.down || devices.gamepad.start.down) settings.open = true;

    GUILayout.Modal(Anchor.MiddleCenter, settings.open)
    {
        GUILayout.Toggle("Fullscreen", settings.fullscreen);
        GUILayout.Slider("Volume", settings.volume, 0, 1);
    }
}

local singleton Toolbox
{
    bool dragging;
    bool renaming;
    string name;
}

local singleton Hand
{
    int picked = -1;
}

local singleton Lobby
{
    string code;
}

// A card the view draws itself: the GUI says what the player does with it
view Cards(mut Hand hand)
{
    Draw.SetScreen();
    for (var slot = 0; slot < 3; slot++)
    {
        var rect = Rect(40 + slot * 220, 200, 200, 280);
        var card = GUI.Control(rect);
        var center = float2(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
        Draw.RoundRect(center, float2(rect.width, rect.height), 18, card.focused || card.hovered ? Color.white : Color.gray);
        if (card.clicked) hand.picked = slot;
    }
}

// The GUI's own widgets, with a look that names what it changes. Enter in the
// field joins, as the button beside it does
view Join(mut Lobby lobby)
{
    mut var entered = false;
    GUI.Theme(GUITheme { field = Color(0.1, 0.12, 0.2), accent = Color.cyan, radius = 10 })
    {
        GUI.TextField(Rect(40, 500, 240, 36), "", lobby.code, out entered);
    }
    if ((GUI.Button(Rect(290, 500, 90, 36), "Join") || entered) && lobby.code != "") Session.Join(lobby.code);
}

// A library's widget, which reads the devices itself, in the window's lower left corner
view Tools(Devices devices, mut Toolbox box)
{
    var pointer = devices.pointer;
    var over = pointer.position.x < 200 && pointer.position.y < 200;
    if (over && pointer.press.down) box.dragging = true;
    if (!pointer.press.pressed) box.dragging = false;
    if (over || box.dragging) GUI.ClaimPointer();

    if (box.renaming)
    {
        GUI.ClaimKeyboard();
        GUI.ShowKeyboard();
        box.name += devices.keyboard.text;
        if (devices.keyboard.enter.down) box.renaming = false;
    }
}
```

### Provisional

Implemented, awaiting approval:

- Positions and sizes are in pixels, as on a web page, so widgets keep their size when the window changes size. On a display scaled to 150%, a pixel is the display's logical one, 1.5 real pixels wide, as CSS pixels are.
- `Rect(x, y, width, height)` is a built-in value type measured from the top left corner, with `y` down, as in Unity's GUI, with `x`, `y`, `width` and `height`. World drawing and the mouse have `y` up. `GUI`'s rects are on the screen, inside an area or not.
- `Screen.Flip(position)` gives a `float2` position counted from the screen's other edge: `float2(position.x, Screen.height - position.y)`. The devices count pixels from the bottom left with `y` up (see Input), and the screen, the GUI, `Draw.Screen()` and the conversion helpers from the top left with `y` down: `Screen.Flip(devices.pointer.position)` is the pointer on the screen. It's its own inverse, so it converts either way. It's the window's whatever target is drawn into, as the devices are, and readable where `Screen.width` is: views and the functions they call, not the match, the input's code or the GPU. The name is a pick, awaiting the owner.
- `GUILayout.Area(anchor)` places a panel sized to its content at one of nine anchors, the built-in enum `Anchor`, named as Unity's `TextAnchor` (`UpperLeft` to `LowerRight`), 12 pixels from the screen's edges. `GUILayout.Area(rect)` places it at a rect. An anchored area is placed with its size from the frame before, and moves at the end of the frame if the size changed. One too big for the screen starts at its top left margin.
- `GUILayout` widgets outside any area stack from the screen's top left, across every view. In a vertical container, buttons, toggles, sliders and fields stretch to the widest widget's width. Labelled widgets put their label in a column at least 130 wide, so a column of them lines up.
- Widgets shrink when there isn't room: an anchored area is at most as wide as the screen less its margins, and a vertical container's widgets are at most as wide as it is. A row that's too wide shrinks each of its widgets by as much as it can give, with the frame before's widths. Fields and sliders shrink down to a small minimum, a labelled widget's label column first, down to the label; buttons give up half their padding; labels and toggles don't shrink.
- The widgets, with `GUI`'s taking a `Rect rect` first (`TextField` too, see Text):
  - `Label(string text)` and `Button(string text) -> bool`, which returns whether it was pressed.
  - `Toggle(string text, mut bool value)`, `Slider(string label, mut float value, float min, float max)`, `IntSlider(string label, mut int value, int min, int max)`, `IntField(string label, mut int value)`, `FloatField`, `Float2Field`, `Float3Field`, `Float4Field` and `ColorField` (a swatch, and fields for r, g, b and a, from 0 to 1). Each returns whether it changed its value.
  - `GUILayout.Space(float size)`, and the containers `GUILayout.Vertical()`, `GUILayout.Horizontal()` and `GUILayout.Area(...)`.
- A widget's `mut` argument is the variable itself, so its type matches exactly: `Slider` takes a `float` variable, not an `int`.
- Widget IDs come from where each call is in the program, mixed with the entity a view runs for, and for a function that draws, with where it's called from. Calls from one place with the same entity count up, in the order they run.
- Widgets draw as they're called, so, like `Spawn`, they can't be on the right of `&&` or `||` or in a side of `?:`. A statement with several runs them left to right.
- Navigation: Tab and Shift+Tab move the focus between widgets, in the order they were drawn. The arrows, d-pad and left stick move it once a widget has it, and only start moving it while the game isn't reading the devices, so a HUD's button never takes the d-pad from the player. Enter, Space and the south button press; Escape and the east button let go. With the focus, left and right step a slider or number field.
- Down and right go on to the next widget and up and left go back, in the order the widgets are called, whatever their places on the screen: three cards side by side are reached with left and right. A slider and a number field keep left and right, to step their value, and the press that brought the focus to one doesn't step it too. From a button, a toggle, a text field or a control, left and right move the focus.
- `GUI.Navigate()` says a menu is up: the arrows, the d-pad and the left stick can start moving the focus though the match reads the devices, as in a menu over a match that goes on behind it. A view, or a function it calls, calls it every frame the menu is up, as with `GUI.ShowKeyboard()`. Without it, only Tab starts the focus there. Once a widget has the focus, the keyboard and the gamepad are hidden from the input's `Sample`, as ever.
- Typing: clicking a number field, or pressing Enter on it, starts typing into it with its value selected, so the first character replaces it; typing a number into a focused field starts too. Enter or leaving the field keeps a valid number; Escape keeps the old value.
- **Entered fields**, beyond what's agreed (see Decided). Picks, awaiting the owner:
  - The versions: `TextField(string label, mut string value, out bool entered)`, `IntField(string label, mut int value, out bool entered)` and `FloatField(string label, mut float value, out bool entered)`, with `GUI`'s taking the `Rect` first. The parameter is named `entered` and comes last. `Float2Field`, `Float3Field`, `Float4Field`, `ColorField`, the sliders and `Toggle` have no such version.
  - `entered` is false in every frame but the one Enter ends the typing in: while the player types; when Enter starts the typing in a field that has the focus; when Escape stops it; when the focus leaves by Tab or by a click elsewhere, which keeps a number field's valid number without entering it; and when the field is in a `GUI.Disabled` block, or under a modal that isn't its own. Every call assigns it, so it's never what the variable held.
  - A number field is entered whether or not its number changed, or was a number at all: `entered` says the key, and what the field returns says the value.
  - The numpad's Enter is Enter. A gamepad's south button enters a number field, since it ends its typing as Enter does (it always did); it doesn't end a text field's typing, so it enters none.
  - What's typed in the frame Enter comes in is part of what's entered. Characters and Enter can arrive in one frame (a slow frame, a paste and a quick Enter, a script), and the characters used to be dropped there. It's so for every field that's typed into, with `out entered` or without.
  - The argument is an `out` argument like any other (see Out parameters): the word is written; `out var entered` and `out bool entered` declare a local, the block's, read where the call ran; `out` before a `mut` local or a field of local state assigns it; `out _` leaves it out; it's a write, so a view can't make the match's; and the call keeps its turn among what its statement reads. A call that leaves the word out, or writes it before another argument, is told so as a function's call is, with the editors' quick fix, where the engine's other functions say that no version takes the arguments.
  - Which version a call means goes by how many arguments it has. By name, `entered: out var done`.
  - A phone's keyboard: its Enter, whatever the key is labelled, is the GUI's Enter on the web (a key that names itself and says no position, or a line break typed into the page's field) and on Android (the system's key event), so it enters a field there too. That's by the code and a web test with made-up events: no phone was tried.
  - A widget still can't be on the right of `||`, so the button comes first: `if (GUILayout.Button("Join") || entered)`.
- `mut` before an argument is an error that says to remove it, where it was "expected an expression": the word is the parameter's, and only `out` is written at a call.
- Widgets follow the pointer: the mouse, or a finger. A finger that lifted is nowhere, so nothing stays hovered where it was.
- Hidden from the input's `Sample` and from views' `Devices`: the keyboard and gamepad while a widget has the focus, and the mouse's buttons and scroll, the pointer's press and the primary touch while the pointer is over a widget or an area, or pressing a widget. While a modal is up, everything is, the movement of the mouse and the pointer too. A key or a gamepad's button that went down while the keyboard and the gamepad were the GUI's stays hidden until it's let go, its release too, whatever became of the widget: the key that clicked a button isn't the game's while it's still down, and one the player held before the GUI took the keyboard is the game's again as soon as the GUI lets go.
- The pointer is over a widget or an area where it is now, against where the last frame drew them: the game samples, and views read, before the frame's GUI sees the devices. So a press is the GUI's from the frame it lands in, with nothing before it: a finger that touches a button, or a click the mouse made as it came. And a click made as the mouse left a widget is the game's. A widget takes the pointer a frame after it first appears, so a press on it in that frame reaches the game. A frame remembers the places of up to 1024 widgets and areas; past those, the pointer is the GUI's a frame after the GUI saw it there.
- What a claim hides: `GUI.ClaimPointer()` the mouse's buttons and scroll, the pointer's press and the primary touch, which is what the pointer over a widget hides; `GUI.ClaimKeyboard()` the keys and what's typed, and not the gamepad (the GUI's focus hides both, as both move it). Where the pointer is stays everyone's.
- Claims are statements of views and the functions they call, like widgets; a function's claim is the view's that called it. A view that runs once per entity is one view.
- A claim stands from the end of its frame to the end of the next: it hides from the samples taken after it, and from the next frame's views. So a widget claims the pointer while the pointer is over it, before any press, and a click on it never reaches the game. With no rect, the engine doesn't know where a view's own widgets are, as it does the GUI's, so a press with nothing before it (a finger that touches, a widget that appears under the pointer) reaches the game and the other views for the frame it lands in.
- `GUI.ClaimPointer(Rect rect)` says where a widget of the view's own is, in the GUI's pixels, every frame it's there. While the pointer is on it, the pointer is the view's, as it's the GUI's on the GUI's own widgets: by where the pointer is now against where the last frame's claims were, so from the frame a press lands in, a finger's too. Off the rect it claims nothing: a widget that's dragged claims the pointer with no rect meanwhile. Up to 256 rects in one frame; past those, a rect claims the pointer while the pointer is on it, which hides a frame late.
- The GUI's own use comes first: a view never reads what the GUI is using, whatever it claimed, and the GUI's widgets work under a claim, as they're drawn on top. While a view has the keyboard, Tab and the arrows don't start moving the GUI's focus.
- Views that claim the same device all go on reading it: which of their widgets is on top is theirs to know. Up to 16 views claim in one frame and go on reading; past that, a claim still hides.
- A modal is an anchored area over the screen, dimmed. The one drawn last is on top, and only its widgets work. Its first widget takes the focus the frame after it comes up, and back doesn't close it on the frame it came up, so the press that opened it doesn't. `GUI` has no modal at a rect yet.
- The drawing: a dark panel behind each area and the engine's default font, in the theme's colors.
- `GUI.Control(Rect rect) -> GUIControl`. `GUIControl` is a built-in struct of four bools, named as a `Devices` button's are:
  - `hovered`: the pointer is on it, with nothing of the GUI's over it.
  - `pressed`: the pointer holds it, from a press on it until it lets go, wherever it goes meanwhile, as `devices.mouse.left.pressed` is true while the button is held. A button looks down while it's `pressed` and `hovered`, and a handle follows the pointer while it's `pressed`. It's false in the frame the pointer lets go, and Enter held on it doesn't count.
  - `focused`: the keyboard and the gamepad are on it.
  - `clicked`: a press on it let go on it, or Enter, Space or the south button went down while it had the focus. For one frame.
- A click with the pointer doesn't give a control the focus, as it doesn't a button. In a Disabled block or outside the modal that's up, nothing of it is true, the focus skips it, and the pointer on it is still the GUI's.
- While a control has the focus, the view that called it goes on reading the keyboard and the gamepad, but for what moves the focus and clicks: the arrows, Tab, Enter, Space and Escape, and the d-pad, the left stick and the south and east buttons. The input's `Sample` and the other views read none of it, as with any focused widget. So a menu's own shortcuts (a card's number) work while one of its cards has the focus, and a control can be a text field: its view reads `devices.keyboard.text`. A view that runs once per entity is one view. Under a modal, no view reads anything.
- Controls made in a loop come from one call, so they're told apart by their turn, as the GUI's own widgets in a loop are: the first one made, the second, and so on. A loop that makes the same ones every frame, in the same order, is all that takes. One that leaves an earlier one out gives its turn to the next: what was hovered, held or focused there goes on with the control that's now in its place.
- `GUILayout` has no control: `GUI.Control` takes its rect. A laid out one would hand back a rect inside an area, and what a view draws there is under the area's panel, which the GUI draws last; and an anchored area finds its place when it closes, after the control was drawn. It waits until views can draw among the GUI's widgets.
- `GUITheme`'s fields, all `Color` but the last: `text` (text, and a slider's thumb), `panel` (an area's background), `control` (a button, a toggle's box, a slider's rail, a color field's frame), `hot` (those with the pointer on them), `pressed` (a button held down), `field` (a field to type into), `fieldHot` (one with the pointer on it), `accent` (the focus's outline, a toggle's check, a slider's filled part) and `float radius` (the corners', in pixels; 0, its default, is square). A modal's dim isn't in it, nor how much a disabled widget fades.
- A part is no rounder than half its smaller side, so a slider's rail and a toggle's box end as a pill and a circle under a big radius. A part drawn around another (the focus's outline) or inside one (a toggle's check) is rounder or less round by how far it is from it. With a radius of 0 the widgets are the plain rects they always were.
- An area's panel has the theme it opened with, whatever its block sets. `GUI.SetTheme` inside a `GUI.Theme` block ends with the block, which puts back what it found. `GUI.Theme` lays nothing out, like `GUI.Disabled`. Blocks nest up to 32 deep with containers; past that, a `GUI.Theme` changes nothing.
- `GUI.Theme(theme);` with no block is an error that names `GUI.SetTheme`, and `GUI.SetTheme(theme) { ... }` one that names `GUI.Theme`. Several settings in one block, with commas, are `Draw`'s to bring.
- `GUI.Disabled(bool disabled) { ... }` grays out the widgets in its block while `disabled` is true, as Unity's `GUI.enabled = false` and `EditorGUI.DisabledScope` do: they're drawn at half opacity, and can't be hovered, pressed, focused or typed into, so Tab skips them. A widget disabled while it's pressed lets go, and a field disabled while it's typed into keeps its old value. The block lays nothing out: its widgets go on in the container around it, `GUI`'s and `GUILayout`'s alike. The mouse on a disabled widget is still the GUI's, hidden from the input's `Sample`. Inside a disabled block, another stays disabled whatever its own `disabled` is. An area's panel doesn't fade, only its widgets.
- **Clipboard:** `Clipboard.Copy(string text)` puts text on this machine's clipboard, like a "Copy" button next to a room's code. It's a statement of local code, as `Session`'s calls are (views and local handlers, not functions yet, `Sample` or match code), and the host does it after the frame; the frame's last one wins. Up to 255 bytes of UTF-8 for now, cut where a character starts. A browser takes it only shortly after a click or a key, as when a button is pressed. Pasting needs no call: Ctrl+V (Cmd+V on macOS) types what's on the clipboard into the text field that has the focus, but for newlines and tabs, on desktop and the web. Reading the clipboard from code isn't offered, since a browser only gives it to a paste.

### Open

- A field for any enum. A game couldn't write one itself until there are generics.
- Styles and themes.
- More than one block per function, like Swift's labelled trailing closures.
- The pieces widgets are made of, so a game can build its own like the built-in ones: a control's ID, which the compiler derives from the call as for the built-in widgets, whether it's hovered, pressed or focused, and drawing in the GUI's layer, over its panels (`Draw.Screen()` draws in its pixels, under it).
- Scrolling, clipping, and keys that repeat while held.

## Local state

### Decided

- The world holds a match: the state every machine simulates the same way. It only exists while a match is on. Single-player is a match too, on a server the machine runs itself and reaches through a loopback transport, the same path as any other connection.
- Local state belongs to one machine and lives outside every world: menus, settings, connection status, animation timers, particles. It's never sent, rolled back or hashed.
- `local` in front of a declaration makes it local: `local singleton`, `local component`, `local scene` and `local event`. Everything else belongs to the match. Structs and functions belong to neither, and both sides use them.
- A local singleton exists once per machine, for the whole program. Local components make local entities.
- Views run every frame, in a match or not. They read the match, and read and write local state: `mut` local parameters, and `Spawn`, `Add`, `Remove` and `Destroy` of local entities. A view only runs when everything it reads exists, so a view of match components or singletons doesn't run outside a match.
- Local structural changes and local events are handled at the end of the frame, as the match's are at the end of the tick.
- The GUI is immediate mode, drawn from views (see GUI).
- **Local fields.** `local` before a field of a match component or singleton makes that field this machine's: it lives outside the match, starts at its default on every machine, and is never sent, hashed, snapshotted or rolled back. It's for what a machine keeps for the match's entities to show them: a baked mesh, an animation timer, a smoothed position, a C library's handle. The match's layout, world hash, deltas and what a joining player downloads are exactly what they'd be without the field.
  - Match code can't see local fields: reading or writing one in a system, a match handler or a task is an error, and so is naming one in a `Spawn` or in a component value. The errors say why, and where the code belongs: a view.
  - In a view, `mut Box box` allows writing the component's local fields only. Writing a match field through it stays an error ("a view can't change the match..."), `mut` on a match component with no local fields stays one too, and `mut` that writes no local field gets the usual warning.
  - Nothing attaches or removes them: every entity with `Box` has its local fields, on every machine, however that machine learned of the entity (it ran the spawn, joined later, was sent the world again, took over as host, hot reloaded). They take memory only once written, and reading ones never written gives the defaults.
  - An entity's local fields go when the entity is in neither the world views draw (the predicted one) nor the verified world. So a destroy the client predicted wrong, which the server undoes, keeps them, and a spawn it predicted that never happened leaves none. Nothing about it is timed.
  - Singletons have them too, like a texture painted from the match's state.
  - A view's match fields are blended copies, as ever; local fields aren't blended, and are read and written in place.
- **The compiler enforces the boundary,** so no mistake can reach a running game:
  - Match code (systems, match event handlers, match scenes) can't read or write anything local, and can't read this machine's `Devices`: it takes a `Devices` parameter, the owner's, which the input sends. The input's `Sample` reads this machine's, and writes only the input.
  - Local code (views, local event handlers, local scenes) can read the match but never change it: no `mut` on match components or singletons, and no spawning, changing or sending match things. Views read it as it's drawn, or `verified`; a local handler hears its events (see Events).
  - The only way from local code into the match is input. Starting, joining and leaving a match are session calls, which never touch a running match.
  - Errors say where to go instead: "a view can't change the match: put it in the input and handle it in a system."

```csharp
local component Spark
{
    float2 position;
    int framesLeft = 30;
}

// Reads the match and leaves a trail of local sparks behind every ball.
view Trail(Body body, with Ball)
{
    Spawn(Spark { position = body.position });
}

view DrawSparks(mut Spark spark)
{
    Draw.Circle(spark.position, 0.1, Color.yellow);
    spark.framesLeft -= 1;
    if (spark.framesLeft <= 0) this.Destroy();
}

// Error: match code can't read local state.
system Count(Spark spark) { }
```

```csharp
component Box
{
    float3 pos = float3(0, 0, 5);
    local List<Vertex3> verts;
    local List<int> faces;
}

// Bakes each box's mesh once, on this machine, and draws from it
view DrawBox(mut Box box)
{
    if (box.verts.count == 0) Bake(box);
    Draw.Mesh(box.verts, box.faces, float4x4.Translate(box.pos));
}

void Bake(mut Box box)
{
    box.verts = [Vertex3 { position = float3(0, 0, 0) }, Vertex3 { position = float3(0, 1, 0) }, Vertex3 { position = float3(1, 1, 0) }];
    box.faces = [0, 1, 2];
}

// Error: 'verts' is a local field of 'Box': it's this machine's, and the match can't see it.
system Shrink(mut Box box) { box.verts.Clear(); }
```

### Provisional

- `local` is only a keyword at the start of a declaration, and before a field's type. It goes before components, singletons, events and event handlers; before any other declaration it's an error that says why (structs and functions belong to neither side, views are always local, systems run the match). Anywhere else it's a name like any other, a field's too.
- **`LocalEntity`** is an entity of the local world. `Spawn` in local code returns one, and `this` is one in a view of local components, where in a view of match components it's an `Entity`. Local code can hold and read an `Entity` (the unit a player selected, say) but never change one. The match's declarations can't hold a `LocalEntity`, and neither can structs, which both sides share.
- A view runs for the entities of one world: its components are all local or all the match's.
- Local handlers handle local events, and `Spawned` and `Destroyed` of local entities, with local state only; the match's events, with what they take of the match as the event left it (see Events); and `Forgotten`, with the match component or singleton whose local fields are going (below).
- The host keeps the local state and calls `tide_local_init(local)` once, then `tide_frame(w, previous, alpha, verified, local, draw, gui)` every frame. `tide_frame` runs the local handlers of what the match sent since the last frame (which the host handed it with `tide_local_events`), lets go of the local fields of entities that are gone, runs the views, then applies their local changes and events. Outside a match, `w` is NULL, and views that read the match don't run.
- The local world has its own entity table and queue, which grow as they need, as the match's do.
- **`Frame.dt`** (the name is a pick, to confirm: `Time` is the match's, so Unity's `Time.deltaTime` can't be it) is the seconds since this machine's previous frame started, a `float`, read-only: what moves things that aren't the match's (particles, menus, a camera's shake) at the same speed whatever the display's rate.
  - It's this machine's, and has nothing to do with the match: it's there in a local scene and while connecting, where no view that takes `Time` runs, and in a match it's the same value, whatever the tick rate and however many ticks the frame ran.
  - Local code reads it: views, local handlers, local tasks (async functions that read it are local, as ones that use local state are), and the functions they call, in a sort's comparison and a parallel loop's steps too. A local task's `Wait.Seconds` counts the same seconds down, and one that waits a frame at a time, `await Wait.Frames(1)`, moves on by it each frame.
  - The match can't read it, as it's another on every machine: a system or a match handler that does is an error that says to take `Time time` and read `time.dt`, a tick's length, and a function that reads it is local code, which the match can't call. The input's `Sample` and `Sanitize` can't either, as they run once per tick: a view keeps what `Sample` needs in local state. Nor can methods, which are handed nothing, or a frag or vert.
  - It's the length the host measured, as it is: a frame after the window was dragged or the page was hidden is that long, and a game clamps it where a big step would show (`Math.Min(Frame.dt, 0.05)`). It's never negative or NaN, and it's 0 until the host has given a frame's length: in a handler that runs as the local state is made, like `Spawned` of a local `Main`.
  - There's no clock of seconds beside it. A float of seconds loses precision as it grows (after four and a half hours its steps are 2 milliseconds, about an eighth of a frame at 60 a second), which shows in whatever is animated by it. A game adds `Frame.dt` up into a field of its own, and wraps it where what it drives repeats. Asking `Frame` for one, as a member or a function by a clock's name in any case (`Frame.time`, `Frame.t`, `Frame.elapsed`, `Frame.seconds`, `Frame.Now()`), is an error that says so and what to write: that field, or `time.t` of `Time time` for the match's seconds, which is all it says in a system or a match handler.
  - `Frame` is built into the language, as `Screen` and `Cursor` are: a game can't declare the name, in a namespace or outside one.
  - Hosts give it before each `tide_frame`: `tide_local_frame_time(local, seconds)`, which `tide/run.h` does.

```csharp
struct Spark
{
    float2 position;
    float2 velocity;
    float age;
    float life = 0.4;
}

local singleton Sparks
{
    List<Spark> list;
}

view DrawSparks(mut Sparks sparks)
{
    mut var i = 0;
    while (i < sparks.list.count)
    {
        mut var spark = sparks.list[i];
        spark.age += Frame.dt;
        if (spark.age >= spark.life)
        {
            sparks.list.RemoveAt(i);
            continue;
        }
        spark.position += spark.velocity * Frame.dt;
        sparks.list[i] = spark;
        Draw.Circle(spark.position, 0.1, Color.yellow);
        i++;
    }
}
```

Local fields, beyond what's decided:

- **What they hold** is what a local component's fields can: numbers, vectors, enums, structs, text, lists, maps, sets, grids, `Entity` and `LocalEntity`, each with a default like any field's. `[Snap]` on one is an error, as local state is never blended. A local field shares its name with no other field or method of its component.
- **Where `local` can't go.** On a field of a struct (structs belong to neither side: make the field that holds the struct local), of the input (all of it is sent), of an event (it's sent whole), or of a local component or singleton (all of it is local already): each is an error that says so, and the last has a quick fix that removes the word.
- **They're reached through a parameter**, `box.verts`, where local code has the component or singleton itself:
  - a view's, read through `Box box` and written through `mut Box box`;
  - an ordinary function's, which gets them with the component or singleton: `void Bake(mut Box box)` writes them, `int Corners(Box box)` reads them, and the caller passes its own bound parameter, `Bake(box)`. The parameter can go through helper chains with its local fields. Such a function is local code: only views, local handlers and functions like it call it, and calling it from match code is an error that says where it uses a local field;
  - a local handler's of one of the match's events, for the entity the event was sent to (see Events);
  - a handler of `Forgotten`'s (below).
- **A component's or singleton's value is the match's part.** A copy (`var b = box;`), a value written out (`Box { ... }`), and what `world.Get<Box>(entity)` or `default` gives have no local fields, and `b.verts` is an error that says to read them through the parameter. So is naming one in a value, `Box { verts = ... }`, and passing a copy to a function that uses them. Methods work on values, so they have none either, including through explicit parameters; an ordinary function that takes a bound component or singleton parameter does.
- **One side at a time.** A function that changes a component's or singleton's match fields through a parameter and uses its local fields through the same one is an error: no code could call it. This follows helper chains too. A view can't pass a match component or singleton to a function that changes its match fields.
- Tasks can't use local fields yet, in the match or in local code.
- **Memory.** An entity's local fields take memory from the first frame a view leaves them other than their defaults; a view that takes them as `mut` and changes nothing takes none. Where a default needs the heap (text, a list with elements, a grid with a size), a view that takes them as `mut` makes the entity's own as it first runs for it. Their text, lists and grids are in the local state's heap; a game whose only such fields are local has no heap in the match.
- **When they go.** At the start of a frame, before the views: an entity's, once it's in neither world, or has the component in neither; a singleton's, once there's no match, back to their defaults; and all of them once the match this machine was in is over (it left, was dropped, or the match ended), whatever match the next frame sees, as another match's entities have the same handles. Entities only come and go with ticks, so a frame looks when either world's tick changed since the last one did.
- **An entity after a rollback.** A rollback can give a handle to another entity: a client predicts a spawn, the server had another in that tick, and after the replay the handle is the other one's. Local fields are kept for the entity, not the handle:
  - Each entity is born as a number: a hash of the tick it was spawned in and what it was spawned with (which components, its scene, and each component's value). An entity is in a world, for when local fields go, when the world has its handle born as the same.
  - So a replay that spawns something else on a handle takes what was kept there, `Forgotten` first, and the other entity starts from the defaults. The same spawn again (the server had the tick as the client predicted it) keeps them.
  - A spawn that comes out with other values, like a position a wrong guess moved, is another entity. Text and lists among the values count by where the match keeps them, so a replay that changed the match's text before the spawn can make it another too. Either way the cost is the local fields starting over.
  - The number is this machine's, beside the world and never in it: the world's hash, its bytes, its deltas and what's sent are the same with it or without, a game with neither local fields nor correction-tagged fields on components keeps none, and it costs 4 bytes an entity where there are. An entity that came with a world the server sent (a player who joins, or one whose world the server put right) has none, and is known by its handle alone; so are local fields C set for an entity that had none.
- **`Forgotten`** is how a local handler hears of it, to let go of what the fields hold, like a C library's handle: a built-in local event, sent to an entity as its local fields of a component go, before they do.

```csharp
component Model
{
    int kind;
    local int handle; // A C library's
}

local event(Forgotten) FreeModel(Model model, mut Loaded loaded)
{
    ReleaseModel(model.handle);
    loaded.count -= 1;
}
```

  - The handler takes the one match component or singleton whose local fields are going, and local singletons. `this` is the entity's handle, an `Entity` that's no longer alive.
  - The entity is gone from the match, so only its local fields are there to read: a match field, or the component as a whole, is an error. What the handler needs of the match goes in a local field while the entity is there.
  - It runs for entities that had something written, as the others have nothing to let go of, and for a singleton once, as the match goes. It isn't async, and the program ending sends none.
  - Games can't send it, and a handler of it is a local handler: `local event(Forgotten) ...`.
- **In C**, the generated `Box` struct, `tide_get_Box` and the rest stay the match's part. The local fields are a struct of their own, `Box_local`, which the local state keeps: `tide_has_Box_local(local, entity)` says whether anything was written for an entity, `tide_get_Box_local(local, entity)` gives a copy (the defaults when nothing was, with text, lists and grids empty), and `tide_set_Box_local(local, entity, value)` sets them, as their bytes. A singleton's are `local->Arena_local`.
- **Hosts** give the frame the last world the server confirmed: `tide_frame(w, previous, alpha, verified, local, draw, gui)`, where `tide_session_view` gives it with the other two (`tide_view_worlds.verified`). NULL is for a host with one world: `w` is all there is, and it's what `verified` parameters read too.
- **Hosts hand local handlers what the match sent**, before each frame: `tide_local_events(local, tide_session_events(session))`, which `tide/run.h` does. The session records what its client's ticks send, each event once however often its tick ran, and again for `verified` handlers once the server confirmed the tick. A host that ticks a world itself gives the tick a log, `tide_world_tick_events(w, jobs, &events)`, and hands that over the same way: with no server, what it ticks is all there is, so a tick's events are heard by both kinds of handler at the next frame. `tide_world_tick(w)` records nothing, and its world comes out the same. See `tide/events.h`.
- **Hot reloading** carries local fields over by name with the rest of the local state: an entity's as its component's, field by field, and a singleton's as a local singleton's. A component that's gone, or has no local fields left, takes them with it. What a session recorded of the match's events for local handlers isn't carried over: a swap comes between two frames, when the last frame's were heard, and the ticks a client had run and not had confirmed tell `verified` handlers nothing once the new build confirms them as they were. They still don't tell the others twice.

### Open

- Saving local state, such as settings, between runs.
- **An `Entity` local code holds, after a rollback.** A rollback can give a handle to another entity (see local fields, above), and an `Entity` in local state, like a selected unit, then names the other one: nothing tells local code yet. Local fields are told apart, but only follow what this machine spawned: after the server sends a world again, its entities are known by their handles. And a predicted entity that the server's order gave another handle is another entity to local fields, which start over there rather than move with it.
- Local state for a component the game didn't declare, like a package's: a declaration of its own that says which component it's for, on the storage local fields have.
- Local fields in tasks, and in a component's methods; another entity's, through the `World`.

## Scenes

### Decided

- A scene is a group of entities that load and unload together: a menu, a level, an arena. Several scenes can be loaded at once, and several copies of the same one.
- `scene Arena { ... }` declares one. A loaded scene is an entity like any other: the declaration is its component, and its fields are the scene's state. A system that takes `Arena` runs once per loaded arena.
- `local scene` declares a local one (see Local state). A scene only holds entities of its own side.
- `Scene.Load(Arena { size = 30 })` loads a scene into the current world and returns its entity. `Scene.Unload(scene)` unloads it, destroying every entity it owns. `Spawn` of a scene is an error that says to use `Scene.Load`.
- Loading and unloading happen at the end of the tick, like `Spawn` and `Destroy`. `Spawned` handlers set a scene up, and its entities get `Destroyed` when it unloads, both within that tick (see Events).
- **Ownership:** a spawn joins the scene of the entity the code runs for. In a handler, that's the entity the event was sent to. Code that isn't running for an entity, like a system that runs once per tick, spawns into no scene, and those entities live until they're destroyed.
- A scene is never owned: it lives until it's unloaded, whoever loaded it.
- `Main` is the fallback: when the last scene of `Main`'s world unloads, `Main` loads again, set up by its `Spawned` handlers as at the start. A match `Main` comes back in the match, a local one in the local state.
- When `Main` is local, a match can't load it, so a match whose last scene unloads ends. Every machine in it goes offline, back to its local `Main`, with `Disconnected` and the reason `Ended` (see Sessions).
- Loaded scenes share their world: the same systems, singletons and `Time`. Scenes that share nothing are separate worlds, which never communicate. Only the server creates worlds, so match code can't (see Open).
- **Visibility:** scenes are public by default, seen by every player in the world. `Scene.Load(Hand { ... }, SceneVisibility.Private)` loads a private one, which only the server and the players given it see: `Scene.AddPlayer(scene, player)` and `Scene.RemovePlayer(scene, player)`. Membership is match state, so the server decides it, and a player who's added receives the scene's state.
- A private scene with no players exists only on the server, which is where secrets like RNG seeds go. Code that reads a private scene only predicts correctly on machines that see it, and the server corrects the others.
- A client never loads match scenes on its own: it has the ones the server has it in.
- A system named `Main` is an ordinary system. Without a `scene Main`, the missing entry point's error says so.

```csharp
scene Arena
{
    int size = 20;
}

scene Hand
{
    PlayerID player;
}

// Sets up each arena. The floor joins that arena.
event(Spawned) SetupArena(Arena arena)
{
    Spawn(Floor { size = arena.size });
}

// Deals each player a hand only they can see.
event(PlayerJoined joined) DealIn()
{
    var hand = Scene.Load(Hand { player = joined.player }, SceneVisibility.Private);
    Scene.AddPlayer(hand, joined.player);
}

system Collapse(Arena arena)
{
    if (arena.size <= 0) Scene.Unload(this);
}
```

### Provisional

- **Current visibility implementation:** `SceneVisibility.Private`, `Scene.AddPlayer` and `Scene.RemovePlayer` record the scene's visibility and membership in match state. Selective state delivery is not built yet: every player still receives the whole world, including every private scene. The visibility rules under Decided describe the intended behavior, not a way to protect secrets in a current build.
- `scene` and `local scene` are only keywords at the start of a declaration. A scene's component can have methods, and `Add` can give its entity other components, like any entity.
- `Spawn`, `Add` and `Remove` of a scene's component are errors that point to `Scene.Load` and `Scene.Unload`. `Destroy` on a scene's entity unloads it, and `Scene.Unload` of an entity that isn't a scene does nothing.
- `Scene.Load` makes its entity right away, like `Spawn`, so it follows the same evaluation order and can't be inside `?:` or on the right of `&&` and `||`.
- An entity spawned into a scene that's been unloaded by the spawn's turn is never made, like the loot an enemy drops while its arena unloads.
- Entities unload in archetype order, each archetype's from its last row, and the scene's own entity last. Each gets its `Destroyed` handlers.
- `SceneVisibility` is a built-in enum, `Public` and `Private`. A local scene has no visibility. Local code can't change who sees a scene.
- In generated C, a scene's component holds its visibility and players too, as `tide_visibility` and `tide_players`, one bit per player: 32 to a word, with `tide_players1` and on for games that take more. Code can't name them.
- The engine loads `Main` itself: `tide_world_init` loads a match `Main`, and `tide_local_init` a local one, with `TIDE_MAIN_IS_LOCAL` defined. `tide/run.h` starts without a match when `Main` is local.
- The `Main` scene is the world's first entity.
- The fallback waits for the end of the tick (the frame, in local state), once every change and handler has applied, so unloading one scene and loading another in the same tick never brings `Main` back. Entities in no scene don't count. `Main` comes back at most once a tick: one that unloads itself as it loads leaves the world without a scene until the next.
- A match that ends runs no more ticks after the one that left it without a scene. The server tells every player then, and anyone who tries to join. Only the match ends: local state carries on as it was, so whatever local scenes were loaded stay loaded.

### Open

- The server's own code: creating worlds from scenes, and moving players between them.
- Entity references that say what they point to, so the compiler can check `Scene.Unload` on an entity read from a field. It can already check `this` in a system taking the scene's component, as in `Collapse`.
- The details of private scenes: what players outside one see of it, and how an added player catches up.

## Sessions

### Decided

- Single-player and multiplayer are the same: every match runs on a server, and this machine's player connects to it, over a loopback transport when the server is on this machine. The engine assumes nothing about what a game does with it, like pausing; games build that from inputs and state.
- Local code decides which match this machine is in: `Session.Start(scene)` starts one on this machine, `Session.Join(code)` joins the match in a room by its code, `Session.Connect(address, port)` joins another machine's by its address, and `Session.Leave()` leaves. `Start` names the scene the match starts in, with its values like `Scene.Load`'s: `Session.Start(Arena { size = 30 })`; `Session.Start()` names none, and starts in `Main` with its defaults, which is for code that can't name the game's scenes (a package's menu), and an error where `Main` is local. `Join` and `Connect` get whatever the server runs.
- There's one kind of match. Whether others can join it is a switch on it, not another way to start one: a match starts closed, `Session.Open()` lets others join the match this machine runs, and `Session.Close()` stops letting them, at any time. Single-player is a match nobody else was let into.
- The machine that runs a match sends players out of it with `Session.Kick(player, message)`, or every other machine's with `Session.KickAll(message)`. The message is text, so a game can say anything, and kicked players get it with their `Disconnected`. A kick isn't a ban.
- The machine that runs a match says what it means by leaving it: `Session.Leave()` leaves, and with host migration the match goes on without this machine; `Session.End()` ends the match for everyone, who go offline with `Ended`. A crash, or closing the window, is a `Leave`.
- **Host migration** is a game's choice, off by default: `settings { hostMigration = true; }`. When the machine running a room's match leaves or stops answering, another player's machine takes the match over from the last tick it verified, and the other players join it again as the same players. The old host's player leaves the match (`PlayerLeft`), and from then on, entities without an owner read the new host's input.
  - Only room matches change hands. A match joined by address (`Session.Connect`) has no relay to meet at again, so it ends, as it does without host migration.
  - A new host only has what its machine could see: private scenes it wasn't in are lost, whole. A game that uses host migration shouldn't keep secrets.
  - No player ever gets another's cookie. With host migration on, every player gets a hash of each player's cookie, so a new host can tell who's coming back and nobody can pass for someone else.
  - A host that leaves says so, and the match changes hands at once. One that stops answering is only noticed after the time-out, so players wait a few seconds first.
- Players find each other's matches in rooms, by a code. The hosting machine picks its room's code itself, so local code has it at once, with nothing to wait for: `Session.room`.
- Local code sees where this machine stands through a built-in local singleton, `Session` (taken as a parameter like any singleton), and the built-in local events `Connected` and `Disconnected`.
- Clients have no input delay: their own input applies at once, and they run ahead of the server so it arrives in time. Only other players' inputs are ever guessed. A client that can't afford to run its predicted ticks again delays its input instead, a tick at a time, until it can (see Settings, `prediction`); a game can ask for delay in place of prediction altogether.
- Local code tells what's predicted from what the server has confirmed with one word, `verified`: before a view's parameter or filter it reads the confirmed world (see Views and drawing), and before a local handler's event it hears the event once its tick is confirmed (see Events). Without it, a view draws the world as this machine predicts it, and a local handler hears each tick as this machine first ran it.
- The desktop transport is our own thin layer on UDP. The web's is WebRTC data channels that neither order nor resend, so browsers host matches as well as join them. A relay we host introduces players to each other; their packets go straight between them whenever their networks allow it (see AGENTS.md, Networking).

```csharp
local scene Main { }

scene Arena
{
    int size = 20;
}

view Menu(Session session)
{
    if (session.state != SessionState.Offline) return;
    GUILayout.Area(Anchor.MiddleCenter)
    {
        if (GUILayout.Button("Play")) Session.Start(Arena);
        if (GUILayout.Button("Host"))
        {
            Session.Start(Arena { size = 40 });
            Session.Open();
        }
        if (GUILayout.Button("Join")) Session.Join("K7QF2M");
        if (GUILayout.Button("Connect")) Session.Connect("192.168.1.5");
    }
}

local event(Disconnected gone) BackToMenu()
{
    // gone.reason says why: Left, TimedOut, Refused, ServerLeft, Failed, Ended or Kicked,
    // and gone.message what a kick said.
}
```

### Provisional

Implemented, awaiting approval:

- `Session` has `state` (`SessionState.Offline`, `Connecting` or `Connected`), `player` (this machine's `PlayerID`, once connected), `ping` (the network's round trip to the server, in milliseconds: from when this machine sent to when the answer arrived, less what the server held it, so no wait for a frame is in it), `server` (whether this machine runs it), `open` (whether others can join it, which only the server's machine knows) and `room` (the code of the room the match is in, or `""`, as it is while the match is closed). It's read-only. A single-player match is `Connected` too, with `server` true and `open` false.
- `Connected` is sent once the match's world has arrived and this machine plays in it; `Disconnected { DisconnectReason reason; string message; }` when it leaves: `Left` (it called `Leave`, or started another match), `TimedOut` (the server stopped answering, or never did), `Refused` (another build of the game, no room left, or a closed match), `ServerLeft` (the server's machine left, which ended the match), `Failed` (it couldn't start: no network, a port in use, an address that isn't one, a room nobody has) `Ended` (the match's last scene unloaded, and `Main` is local; see Scenes) or `Kicked` (the server's machine sent it away). `message` is the kick's, and `""` for the other reasons.
- `Session.Kick(player)` and `Session.KickAll()` send no message. A kicked player leaves the match at once, as if they had left: `PlayerLeft` follows, and they can join again, as the same player, while the match is open. A goodbye can be lost: a kicked player who didn't hear it is told again whenever they're in touch, while they still send to the server and when they join it again, before they're let in (taking a match over with host migration included). Once they've heard, they can join again. Kicks only change a match this machine runs: on a client, or offline, they do nothing, and this machine's own player can't be kicked (it calls `Leave`). A message is up to 255 bytes of UTF-8, cut where a character starts. A frame takes up to 16 kicks; a `Start`, `Join`, `Connect` or `Leave` after them drops them, as it does Opens. A machine kicked from a match it joined with `--join` or `--connect` doesn't join it again by itself.
- `Session.Open(port)` takes players in a room, and on `port` too, 7777 without one, except on the web, which has no ports. A closed match turns away anyone who isn't in it, coming back or not; the players in it stay. Opened again, it takes players in the same room and on the same port. `Open` and `Close` only change a match this machine runs: on a client, or offline, they do nothing. When it can take players neither in a room nor on the port, the match stays closed. Opens and Closes before a `Start`, `Join`, `Connect` or `Leave` in the same frame were for the match it ends, so they're dropped. `Session.Play` and `Session.Host` are errors that point to `Start` and `Open`.
- `Session.Connect(address, port)` takes `"192.168.1.5"`, `"192.168.1.5:7777"` or a name like `"localhost"`, and the port is 7777 without one.
- A room's code is 6 letters and digits, without look-alikes (no `0`, `O`, `1` or `I`), like `K7QF2M`: about a billion codes. Case and spaces don't matter when joining. `Session.Join` with text that can't be a code is an error, which points to `Connect` for an address.
- A code another room already has is refused by the relay, and the host picks another at once, before anyone could have read it: `Session.room` changes. It's `""` while the relay can't be reached; the host keeps trying, and the room opens again under the same code if it's still free. Players already in keep playing: only joining needs the relay.
- Joining a room that doesn't exist, or whose host can't be reached, ends with `Failed` at once. A room's host gets 15 seconds to answer for the first time, rather than 5, since WebRTC can take a while to find a way through routers.
- Desktop and web players meet in the same rooms: desktop games speak WebRTC too, with an implementation of our own (no third-party code, so games carry no license terms for it). Everyone reaches the relay over `wss://`: desktop games with their system's TLS (on Linux, OpenSSL's libssl, which they load if it's there).
- Under `tide run --web`, a reload goes on with the match closed: the room closes, and the other players drop out.
- Session calls are statements, in views and local handlers. Functions can't make them yet, nor can match code, which runs the same on every machine, nor `Sample`.
- A match can't start in a scene that holds text or lists yet.
- Starting a match leaves the one this machine is in first, which sends `Disconnected` with `Left` before the new one's `Connected`.
- The server's player joins before the match's first tick, as `PlayerJoined` handled at the end of it; the server only starts ticking then.
- When `Main` is the match's, `tide/run.h` starts it at once, and opens it with `--host [port]`, or joins another with `--join code` and `--connect address` on the command line (and `tide run --host`, `--join` and `--connect`).
- A match takes up to the game's `maxPlayers` players, 16 without the setting and 256 at most (see Settings). One more is refused, as from a full match.
- A client predicts at most a second ahead of the last tick the server confirmed, however many ticks that is at the match's tick rate; beyond it, it waits for the server. The server keeps four seconds of ticks to send again; a player further behind gets the whole world again. A player that was sent the world has every tick since kept for it until it catches up, however long the world took to arrive, up to 32 seconds: one that can't catch up in that long gets the world again.
- Joining takes a round trip and a half: a hello gets a welcome, and the world comes once the player writes back, so that a hello from an address that isn't the player's costs the server a datagram and never puts a player in the match. One that never writes back is forgotten in two seconds. Players who join in the same tick are sent one packing of the world.
- **Coming back:** joining a server gives this machine a cookie, and joining the same server again (the same room, or the same address) presents it, so the player gets their `PlayerID` back, and with it whatever the game kept for them. If the server still has them connected (their old connection went quiet), the new one takes over with no events at all; if they'd left, `PlayerJoined` comes again with the same `PlayerID`. A server keeps a slot for a player who left until it has no slot that was never used; then it gives away the one away longest, and that player's cookie stops working.
- The cookie lives as long as the program: it doesn't survive a restart yet, and it's not safe against someone on the network guessing it.
- **Host migration:** the first player's machine to reach the room takes the match over: the relay pings the room's host for 3 seconds, and if it doesn't answer, or it left, gives the room to the first player there and introduces the others to it. A host that answered keeps the room, and the players who came join it again.
- While a match changes hands, local code sees no `Connected` or `Disconnected`: `Session.state` is `Connecting`, `Session.player` stays the same, and views see the last world this machine had. On the new host, `Session.server` becomes true.
- The new host runs the match at its tick rate, and goes on from the last tick it verified. The last host's player gets `PlayerLeft` at the first tick. A player who doesn't come back within 20 seconds gets `PlayerLeft` too; one who comes back later is the same player, with `PlayerJoined` again.
- A closed match changes hands closed: its players come back, but no one new joins. The room keeps its code, and its key (128 random bits) only goes to the match's players, so no one else can take the room over.
- A host that's still running but doesn't answer the relay in time, like one stopped at a breakpoint, loses the room to its players. When it carries on, its match fails on its own machine (`Failed`).
- `Session.End()` on a client does nothing. A match that ends, by `Session.End()` or by its last scene unloading, ends at the relay too: for five minutes, players who come back to its room to take it over are told it ended, and go offline with `Ended`, whatever goodbye they missed. The room's code stays taken that long.

### Open

- Keeping the cookie across restarts, and making it unguessable.
- Servers with no window and no player of their own.
- Keeping a room open across reloads under `tide run --web`.
- Lobbies, and finding matches without a code.
- Showing other players from confirmed state, blended between confirmed ticks (see Views and drawing, Open).

## Settings

### Decided

- `settings { ... }` sets the engine's settings for the game. Each is set without a type: `tickRate = 30;`. The game's own values are constants (see Constants).
- `settings` is a keyword only at the top level of a file, so it still works as a name everywhere else.
- Every setting has a default, so a block only lists what it changes.
- A game can have several blocks, in any of its files, and together they're one list of what it sets: each sets what the others don't. A setting that two of them set is an error, as one set twice in a block is. A second block is no error by itself: what clashes is two that set the same thing.
- Values are constant expressions, and can name constants.
- Settings are part of the build, so every machine in a match has the same ones.
- The editor completes the settings' names and shows each one's default and what it does. A name that isn't a setting is an error that says which one was meant.
- The first settings are `title` (the window's), `tickRate` (ticks per second, 60 by default), `maxPlayers` (how many players a match takes, 16 by default), `hostMigration` (see Sessions), `prediction` (how clients keep up with the match, see below), `appId` (the app's ID on phones), `version` (the version people see), `stats` (the stats overlay, see below) and `debugViews` (the engine's debug views, like the profiler, see below). The window's size and the default port can come later.
- `prediction` is one of the built-in enum `Prediction`: `Rollback` (the default) or `Delay`. With `Rollback`, a client runs ahead of the server with no input delay, guessing other players' inputs, and runs ticks again when a guess was wrong; when what its rollbacks cost is more than a frame can afford (one rollback about a frame, and all of them a budget an update on the whole), it delays its input a tick at a time, predicting less, until it fits, and takes the delay back once there's been room to spare for a moment. With `Delay`, clients never predict: their inputs are delayed so every player's reach the server before its tick, and ticks run once each, as the server confirms them, as many ticks behind as that takes. A game that would rather every machine ran each tick once, like one whose ticks are expensive, or whose views can't show a rollback well, picks `Delay`.

```csharp
settings
{
    title = "Asteroids";
    tickRate = 30;
    hostMigration = true;
}
```

### Provisional

Implemented, awaiting approval:

- `hostMigration` is true or false, or a constant that is.
- `debugViews` is true or false, or a constant that is, false by default: the engine's debug views are on in the game. The standard host then shows the profiler at F7, and hides it at F7 again: where a frame's time goes (input, the match's ticks and its netcode, each view, drawing) and a tick's (each system, then its changes and events), as averages over half a second. `--debug-views` on the game's command line (`debug-views` in a web page's address), `tide run --debug-views` and `tide_run_desc.debug_views` turn them on whatever the setting says. Nothing is measured while the profiler is hidden. In generated C it's `tide_game_api.debug_views`, read each frame, so under `tide run` a reload changes it. The names (`debugViews`, `--debug-views`), the key and what the profiler shows are to confirm.
- `maxPlayers` is an int from 1 to 256, known while compiling: how many players a match takes. The world keeps an input for each, and the server's after them, so it's part of the game's data layout: builds with different `maxPlayers` are different games, which can't share a match, and under `tide run` a reload that changes it starts the match over. Every player's input goes to every other each tick, so a match's traffic grows with the square of its players: the limit is what the game is built for, not a number to raise for the sake of it. In generated C it's `TIDE_GAME_PLAYERS` and `tide_game_api.max_players`.
- `prediction` is `Prediction.Rollback` or `Prediction.Delay`, or a constant that is. It's the client's: the server runs the match the same either way, and players of both kinds share a match. In generated C it's `tide_game_api.prediction`, a `tide_prediction`.
- The input delay (`tide_client_status.delay_ticks`, which `tide run --stats` shows) is how many ticks before a tick runs its input is sampled. A client samples inputs as its clock says, as far ahead as it ever predicted, and runs ticks that many short of them. Predicting, it starts at none, and adds a tick when the ticks it would run again cost more than its budget (`tide_client_desc.resim_budget`, 4 ms a frame unless the host says, measured with the host's clock; without one, as in tests and benchmarks, there's no delay), one change per round trip. Not predicting, it starts at the lead and round trip its inputs go ahead by, plus half a round trip for the frame to come back and two ticks for it to be late by; it adds a tick when its clock reached a tick whose frame hadn't come, and takes one back once frames have come three ticks early for five seconds. Either way, a frame whose tick the clock hasn't reached waits for the clock, so ticks run at the match's pace however early their frames come, and views blend up to the clock less the delay.
- `tickRate` is an int from 1 to 1000, known while compiling. The machine that runs a match decides its rate (its desc's, or its game's), and the players who join tick at it. A new `tickRate` under `tide run` takes effect when a match starts.
- `title` is text written out, or a constant that is. `--title` and CMake's `TITLE` go over it, and without any of them, the window has the game's name: its folder's under `tide`, its target's under CMake. Under `tide run`, a new title shows the next time the game runs. On Android it's the app's name under its icon.
- `appId` is text written out, or a constant that is: what Android and iOS, and their app stores, know the app by, like `com.studio.game`, the same for every version. It's two parts or more between dots, of letters and digits, each starting with a letter: what Android and iOS both take. Without it, apps are `dev.tide.<the game's name>`, which is fine to test with; an app made to ship (`tide build --android --release`) needs one.
- `stats` is true or false, or a constant that is. With it, the window shows the stats in its bottom right corner with no option asking for them, as `tide run --stats` and CMake's `STATS` do. It's part of the build like every setting, so what `tide build` makes shows them too: a game takes it out before players get it, unless the frame rate is theirs to see. Under `tide run`, setting it or taking it out shows at the next reload. In generated C it's `tide_game_api.stats`, which the standard host reads beside its own `tide_run_desc.stats`.
- The stats are a drawer (`tide/stats.h`), which the standard host draws over the game:
  - Closed, it's one line: the frame rate, and in a match another machine runs, the ping (`60 fps   ping 34 ms`). A click or a tap on it, or F8, opens it: the rest is listed above that line, which stays where it is, so a click in the same place closes it again. The list is two columns, a label on the left and its value on the right, in groups: what a frame's work takes on its thread (`cpu`: everything but the wait for the display, with `render`, the part of it that's rendering, listed under it, and `gpu` beside them, with the frame's draw calls in its label, or on a line of their own where there's no GPU timer); the match's (the ping of a match this machine runs, with the input delay in its label when there is one; the bandwidth, up and down on one line; the entity count); the machine's (the threads ticks run on, the memory the program has (as the system counts it; on the web, its WebAssembly memory), what draws); and the three systems that took longest on average per measured run over the last half second, each keeping its row while it stays among them. Open, the panel keeps the widest it has been, so its edge doesn't jump. Outside a match it has the lines that aren't a match's, and on a screen too low for all of it, the lines there's room for, from its top.
  - It starts closed when the game's setting, `tide build --stats` or CMake's `STATS` asks for the stats, and open under `tide run --stats` (`tide_run_desc.stats_open`). Open or closed is the host's own, whatever build of the game runs, so a reload keeps it.
  - A press that lands on it is the drawer's until it's let go, wherever it goes meanwhile: the mouse's buttons, and any finger. The game's input never samples it and its views and GUI never read it, from the frame it lands in. A press that began elsewhere stays the game's when it crosses the drawer, and a mouse whose cursor is locked is on nothing. The game reads F8 like any key.
  - Closed, the host measures nothing for it: the frame rate is frames counted, and the ping is the session's. A frame's work, the systems' times and the memory are measured while the list is open. Timing averages update every half second; rendering's CPU cost is part of the CPU total and has its own `render` line, indented under `cpu`. Desktop OpenGL and WebGPU devices with timestamp queries show asynchronous `gpu` drawing time, including render targets and excluding the drawer and display wait (WebGPU sums pass durations). Unsupported backends show no GPU line. CPU and GPU overlap, so their times aren't added. Closing or reloading discards old timings.
  - What's picked, to confirm: that it's a drawer at all, and closed by default; the key, F8, beside the profiler's F7; what the one line says, and that the ping is in it only when another machine runs the match; that `tide run --stats` alone starts it open; that it shows outside a match too, where the stats showed nothing before; that a game can't ask where it is, and leaves the corner free by its own numbers (about 80 by 40 pixels, 190 wide with a ping); the two columns, the groups, `render` under `cpu`, a system keeping its row, and the panel's width that only grows while it's open (2026-10-07, for the owner's ask that it read better and shift less; the draw calls in the GPU line's label, the bandwidth on one line and no tick line were their own asks); and the names in C (`tide/stats.h`, `tide_run_desc.stats_open`, `tide_platform_draw_over`).
- `version` is text written out, or a constant that is: the version people see, like `1.2.0`, one to three numbers between dots, which Android and iOS both take (iOS's version has to be). Without it, it's `1.0`. The number app stores order updates by is each build's own: on Android, the minutes since 2020.
- A setting that's set twice, a name that isn't a setting, and a type written before one are errors. The first is at the second place the setting is set, in the same block or in another, and says where the other one is, with its file when that's another. Set twice to the same value, it's still an error. The last two say that a game's own values are constants.
- In generated C, the settings are in `tide_game_api` (`tick_rate` and `title`), in the `.c` rather than the header, so they don't change the game's hash.

### Open

- Changing settings when the game starts, from the command line or a file: the server would send its settings to the players who join.
- The web page's title: it doesn't show the `title` setting yet.

## C functions

### Decided

- Games can call C libraries. `extern` declares a function written in C, with no body: `extern float Noise(float x);`. Calls cost what a call between C functions does.
- Any code that can call a function can call an extern one, match code and local code alike. The language doesn't mark or check what C does: its determinism, the state it keeps and its thread safety are the game's to get right, and the compiler takes a C call as touching nothing it tracks, so C never makes systems wait for each other, and a system that calls C splits its entities across threads like any other. An `Entity` a system that splits its entities passes C may be one it just spawned, whose ID comes once the system is done: `tide_entity_is_temporary` (`tide/entity.h`) tells, and a handle C keeps stays temporary.
- The C function's name is the extern's own name as written, or the one `[NativeName("...")]` gives, so the Tide name can follow Tide's style: `[NativeName("stb_perlin_noise3")] extern float Noise(...);`. A namespace doesn't change the C name.
- A game's C is in its folder, with nothing to set up: every `.c` file there compiles with the game, with the engine's determinism flags, and every prebuilt library there (`.a`, `.lib`, `.so`, `.dll`, `.dylib`) links with it when it was built for the platform being built for. tide tells which platform a library is for from its contents, not its name or folder, so one folder holds every platform's libraries. C for one platform only uses `#ifdef`, as any C does.
- A game's C++ files (`.cpp`) compile with it too, as C++ with no runtime: tide brings no C++ standard library, exceptions or RTTI. What Tide calls is `extern "C"`. C++ that needs the runtime comes as a prebuilt library behind a C API: a shared one, which has its runtime inside, or a static one built to need none, which is the only kind the web has.
- On Android, a game's prebuilt libraries are shared ones (`.so`). An ELF static library has nothing that says whether it's Linux's or Android's (clang makes the same objects for both), and tide tells libraries by their contents, so an ELF one is Linux's: no folder or name says otherwise.
- Writing `external` gets an error that points to `extern`.
- C takes pointers without Tide having pointer arithmetic: the parameter says how a value is passed, and the call takes its address. `mut T` and `out T` are `T *`, `in T` is `const T *`, a `List<T>` is a pointer to its elements (`T *` with `mut`), with the count passed separately, and a `string` is a zero-terminated UTF-8 copy. Each is only valid during the call. A `const char *` that C returns is copied into text.
- C calls keep the order of evaluation: calls to C, and to functions that call it, run left to right like the rest of Tide, whatever order C would pick. One with a `mut` or an `out` parameter is a call that writes (see Evaluation order).
- A game's C draws by taking the frame's draw list: `extern void DrawUI(DrawList list);`, called as `DrawUI(Draw.list)` from a view or a function it calls. C draws into it with `tide/draw.h`, as much as it likes, in order with the view's own Draw calls. So libraries that hand over triangles each frame (Dear ImGui, Nuklear, Spine) need no way to return lists to Tide.

```csharp
[NativeName("stb_perlin_noise3")]
extern float Noise(float x, float y, float z, int xWrap, int yWrap, int zWrap);

component Ground
{
    float2 position;
    float height;
}

system Shape(mut Ground ground)
{
    ground.height = Noise(ground.position.x, ground.position.y, 0, 0, 0, 0);
}
```

### Provisional

Implemented, awaiting approval:

- Extern functions take and return numbers, `bool`, `char` (an `int32_t` in C, its code point), vectors, matrices, quaternions, `Color`, `Rect`, `Entity`, `PlayerID`, enums (`int32_t` in C, or `uint8_t` and `uint16_t` for `: byte` and `: ushort`), and structs and components of those, by value. A struct is a C struct with the same fields in the same order: Tide's types have no padding the compiler adds, so the layouts match. The vector types are `tide/math.h`'s (`tide_float3` and the like), which C files can include.
- A `mut` parameter is a pointer to the caller's variable (`float *`, `Stats *`), which C can change, as `mut` works for Tide functions.
- An `out` parameter that isn't a list is the same pointer (see Out parameters): `extern bool TryHalf(int value, out int half);` is `bool TryHalf(int value, int *half)`. The difference is Tide's: the call says `out`, can declare the variable there, and passes in nothing C should read. C is trusted to write it: a variable C leaves alone keeps what it held, which for one declared at the call is its type's default. `out string` is an error, as C can't make Tide's text: C returns text.
- An `in` parameter points at the caller's variable or field when it's one of the parameter's type, and otherwise at a copy made for the call, like a computed value or one that converts (`int3` to `in float3`). `in` is only for extern functions: Tide functions' parameters are read-only already. It doesn't go on text or lists, which go by address anyway.
- A list's elements are plain data (no text or lists in them). C gets NULL for an empty list. With `mut`, C can change the elements, but not how many there are. C can't return a list.
- Provisional, awaiting the owner: `out List<T>` is a list C makes, as C#'s `out`: `extern int WriteImage(out List<byte> image);`, called as `physics.size = WriteImage(out physics.image);`. C decides its elements and how many there are, and what the list held isn't passed, so nothing of it is copied for C.
  - C gets a `tide_list_out *` (`tide/list.h`). `tide_list_out_room(out, count)` gives memory for `count` elements side by side, for C to fill, with whatever was there in it. Asking again gives memory for the new count and the earlier one is no longer good. `tide_list_out_count(out, count)` says how many of them, from the first, the list has once C returns: at most the room last asked for. Never called, the list is empty.
  - Once C returns, the list is exactly those elements: more than it had, fewer, or none. A world's list is written chunk by chunk and only where its bytes differ from what it had, so chunks that come out the same stay shared with the world's snapshots and aren't hashed again; the chunks past the new count go back to the heap.
  - C that knows which elements changed says only those, and the list keeps the rest. `tide_list_out_keep(out, count)` says the list keeps what it holds and is `count` long once C returns: the elements past that go, and the ones it gains are zeros. `tide_list_out_part(out, first, elements, count)` then says the `count` elements from `first` on are the ones at `elements`. Nothing of what the list keeps is written, compared or hashed: only the parts are, each with the elements it goes over. A part's elements are read once C has returned, so they're in memory that's still good then (C's own, not the function's variables). Parts are written in the order they're given, and what's past the list's count is left out. Of keeping and asking for room, what C said last stands.
  - Only an `extern` function's `out List<T>` is made this way, of the elements a `mut List<T>` goes to C with; a Tide function's `out List<T>` is a list it assigns, like any `out` parameter. The call says `out`, as every call with an `out` parameter does: `WriteImage(physics.image)`, as it was written before `out` was every function's, is an error that says so.
  - To everything but C it's an `out` parameter like any other: the argument is a list the code can change (a `mut` component's or singleton's field, a `mut` local, a function's own `mut` list), the call is one that writes (see Evaluation order), and a system that makes one of the match's lists this way waits as one that changes it does.
  - Picked beyond that: the room is NULL for no elements, and for more than a list holds (see Limits); a count said before the room is asked for again is forgotten with that room; `mut out` is an error that says to drop `mut`; the memory is the engine's, kept between calls, which C neither frees nor keeps.
- C gets text as it is when a zero follows it, and a copy in the scratch area otherwise. Text C returns is copied into the scratch area, as C may reuse its memory; NULL is empty text. A `mut string` can't go to C.
- An `Action` and the devices can't be passed to C, nor structs that hold text or lists.
- `DrawList` is only an extern function's parameter, a `tide_draw_list *` in C, neither `mut` nor `in`. `Draw.list` is only an argument for one: a local, a field or a Tide function's parameter can't hold it. It needs the frame, as drawing does, so it's an error in systems, tasks and a parallel loop's steps.
- C draws meshes with `tide_draw_mesh`, or `tide_draw_vertices` and `tide_draw_triangles` for vertices that several batches share, and clips with `tide_draw_set_clip` and `tide_draw_unset_clip`, or `tide_draw_push_clip` and `tide_draw_pop` around some of them. `tide_vertex` is `Vertex`.
- Shaders are tidec's to write: generated code draws through them with `tide_draw_mesh_shaded` (`tide/mesh.h`) and a `tide_shader` (`tide/draw.h`), and C has no frag or vert of its own yet.
- C draws 3D meshes with `tide_draw_mesh_3d`, through `tide_draw_set_camera_3d` and `tide_draw_set_camera_matrices`. `tide_vertex3` is `Vertex3`. A `tide_mesh` is its vertices and indices where C keeps them, and a `version`, as a texture's: the draw list copies them when it hasn't got that version, and C says they changed by changing it (`tide_mesh_version` hashes them).
- C's textures are pixels it keeps: `tide_texture` is their address, `width`, `height` and a `version`, 4 bytes a pixel (red, green, blue, alpha), rows from the first. The draw list copies them when their version or size isn't what it last copied, so C can free or change them once the call returns, and says they changed by changing the version. C holds no handle, and nothing is created or freed.
- At a hot reload the draw list forgets the textures it copied, as the new build's C may name other pixels at the same address and version.
- Calls are put in order the way spawns and GUI calls are: what has to go first runs before its statement, and before a loop's condition each round. An `&&` or `||` whose right side calls C, or a `?:` whose sides do, runs as `if` statements then, so each part still only runs when it would, with its own calls in order. So do struct operators that call C.
- `extern` declarations go at the top level of a file, not in structs. `local` doesn't apply to them.
- The C name must be a C identifier, not a C keyword. `[NativeName]` can't name the engine's functions (`tide_...`), and two externs can't name the same C function.
- tidec declares each extern function itself in the generated C, from its Tide signature, rather than including the library's header, whose names could clash with the game's. If the signature doesn't match C's, the game is wrong the way C would be.
- Libraries: static libraries and Windows import libraries link in; `.so` and `.dylib` files link and are copied next to the game, which finds them there; a `.dll` is copied next to the game, and links through its import library (`.lib`). Windows games build for MinGW, so a static library built with Microsoft's compiler may need its C runtime and fail to link; rebuild it with clang or MinGW. A library tide can't read (LLVM bitcode, text) is left out, saying so.
- On Android, the game's shared libraries (`.so`) for each of the app's CPUs link and go in the app with the game, each under the name it was linked with (its soname, or else its file's name), which has to be `lib<name>.so`. Android's libraries and Linux's are both ELF: a shared library is Android's when it has the note Android's NDK puts in what it links (`.note.android.ident`). tide warns about a library whose segments are aligned for pages smaller than 16 KiB, which may not load on phones that have those.
- On the web, only C files and WebAssembly libraries define functions. A web build fails when a function has no definition there (an extern function, or one the game's C calls), rather than when the page calls it: a web program may only leave undefined what the platform layer does, which the page defines.
- `tide run` builds again when a C or C++ file, header or library changes. C code is part of the game's library, which each build replaces, so the state C keeps starts over at each reload.
- tide's own CMake (`tide_add_game`) compiles the `.c` and `.cpp` files in the game's folder but the host's, or those listed after `SOURCES`. It doesn't pick up libraries.
- C++ files are `.cpp`, `.cc` and `.cxx`. They build as C++20 with `-nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics` on every platform: the standard library's headers aren't found even where the system has them, so the same code builds everywhere. C's headers are there. `.hpp`, `.hh`, `.hxx` and `.inl` files count as headers, like `.h`: a change to one builds the folder's files again.
- tide defines nothing of the runtime's: code that uses `new`, `delete` or a pure virtual function defines `operator new`, `operator delete` and `__cxa_pure_virtual` itself. A global's constructor runs before the game does (at each reload under `tide run`). A function's static with a constructor is made the first time through, with no lock.
- A build that fails for want of the runtime says so after the compiler's or the linker's errors, with what to write instead: a standard header that isn't there (`<cstdint>` gets C's `<stdint.h>`), `throw`, `try`, `typeid`, `dynamic_cast`, `new`, `delete`, `__cxa_pure_virtual`, and a prebuilt library built to need the runtime.

### Systems written in C

Provisional. The owner approved the direction (2026-10-05: a system whose body is C, "in a way that lets others use this too for their own situations"); the syntax, the semantics and the C API below are picks that await them.

An extern function is called for one entity at a time, with a copy of what it reads. C that wants every entity's data (a physics library, pathfinding, animation, a loop written with SIMD) is handed the components as the arrays the match keeps them in.

```csharp
[NativeName("game_move")]
extern system Move(mut Position position, Velocity velocity, Time time, without Frozen);
```

```c
void game_move(tide_rows *rows, const Time *time);
```

- **The declaration.** `extern system Name(parameters);`: a system's declaration with `extern` before it and no body. It takes the attributes a system takes (`[Stage]`, `[Before]`, `[After]`) and `[NativeName]`, as an extern function does: its C function has its own name as written, or the one `[NativeName]` gives, whatever its namespace. Extern systems that share a name each say theirs, and a C function is one extern function or one extern system, declared once. A body is an error that says to end it with `;`. `extern view` and `extern event(...) Name(...)` are errors that say only a system can be written in C.
- **To the rest of the language it's the system its declaration says.** It runs for the entities its parameters pick, it's ordered and scheduled as a system, and what it reads and writes is what it declares: a component or singleton it takes `mut` is written, and the rest is read. Nothing warns about access it doesn't use, as its code can't be read. `tide schedule` shows it as `Move  (per entity, in C)`, and the editors' line above it says `in C`.
- **Parameters.** Components, to read or `mut`; `with` and `without` filters; singletons, to read or `mut` (the engine's stay read-only). Each of these is an error that says it isn't there yet for a system written in C, and what to write instead:
  - a sparse component, or a filter by one: which rows have it is told row by row, and C is handed whole runs;
  - the `World`, a `query`, `Devices`, the input: a system written in Tide takes them, and can call C;
  - an `Entity` parameter: C reads each row's entity from its rows;
  - a component with no fields as a parameter: it's a filter, `with`.

  What no system takes is the error it always was: `with ownership`, `verified`, `out`, a default, `local`, `async`.
- **What C is called with.** One call for each run of rows: a chunk of an archetype the filters admit, or a piece of one when the tick shares a chunk out over threads, in the order a system written in Tide would go through them on one thread. A run has at least one row. The function takes the run, a `tide_rows *`, then the singletons the declaration takes, in its order, each by address (`const` for one it only reads), and returns nothing. Components are reached through the run, numbered from 0 in the order of the system's parameters; filters and singletons don't count.
- A system with no component and no filter runs once a tick, as in Tide: C is called once, with a run of no rows and its singletons.
- **The C API**, `tide/rows.h`:
  - `tide_rows_count(rows)`, and `tide_rows_entities(rows)`, each row's entity.
  - `tide_rows_floats(rows, component, offset)` and `tide_rows_ints(rows, component, offset)`: the array of a 4-byte field (a float, an int, an int enum, half a long or an entity), a value a row, by where the field starts in the C struct that has the component's fields in order (`offsetof`). `tide_rows_floats_mut` and `tide_rows_ints_mut` are the same to write, for a `mut` component, with what's there in them.
  - `tide_rows_get(rows, component, row, &value)` and `tide_rows_set(rows, component, row, &value)`: one row's whole component, for the fields that aren't 4 bytes, for a component whose size isn't a multiple of 4 (kept 2 bytes or 1 at a time), and for C that works a struct at a time.
- **Nothing is copied to read**: an array is the match's own memory. **Writing is on demand, a field at a time**: asking for an array to write is what makes that field's page the match's own, a copy when a snapshot shares it, and a page that comes out as it was is the snapshot's again once C returns, with its hash. `tide_rows_set` writes the 4 bytes that differ and asks for no other. So C that writes nothing costs nothing, and C that writes 5 fields of 40 touches 5 pages.
- An array to write is the field from then on: C reads it through that pointer too. One of the same field asked for to read before it may be the memory the snapshot kept.
- A call that can't be answered stops the program, saying which function it was, in which system, and what to write instead: a component number the system doesn't have, an offset that isn't a 4-byte field's, a component not in fields of 4 bytes, an array to write or a `tide_rows_set` of a component that isn't `mut`, a row past the run.
- **Text, lists, maps, sets and grids.** A component or a singleton that holds some can be taken; those fields are handles into the match's heap, which C leaves as they are. C has no way to read or change them from here yet.
- **C can't spawn, destroy, add, remove or send** from a system: there's no call for it. A system written in Tide does, from what C left in a component or singleton.
- **Threads.** It splits its entities across threads by the rule a system written in Tide does: its runs go to several threads at once unless it takes a singleton as `mut`. C is trusted, so what it does on several threads is the game's to get right: each run reaches its own rows, and a system that keeps something from one run to the next, or needs them in order, takes a `mut` singleton.
- **How much work it is.** Its code can't be read, so where the engine reckons whether a tick is worth threads, a row counts as 256 operations: what a few calls would, as C is written for what's more work than a line of Tide. No attribute says otherwise yet.
- tidec declares the C function in the generated `.c`, from the declaration, as it does an extern function's. A web build fails at its link when nothing defines it.

Picked beyond what was asked for:

- A system that runs once a tick is allowed, and gets a run of no rows rather than no run, so every system written in C has one signature.
- Components and singletons that hold text or lists are allowed (an extern function refuses such a struct), as C can't be handed a singleton any other way and a physics library's is likely to hold lists. Their handles are C's to leave alone.
- A component with no fields as a parameter is an error rather than a component C can't ask anything of.
- `tide_rows_get` and `tide_rows_set` number rows from 0 within the run, and a row past it stops the program.
- The C function's prototype in generated C has no parameter names, as a game's could be anything.
- `extern` is a word before `system`, a name anywhere else, as it is before a function.

### Provisional: resident state

A C library whose state is big and lives between calls (a physics world) has, until now, been a guest in the match: all of its state written into a list of the match's after every tick, and the library's own world made again from that list on every rollback (see the Box3D package). Resident state is the other way round, for libraries that come with tide (the owner, 2026-10-07: "for ours only is fine to start with"): the library's memory is the world's, snapshots of it are copies of the pages that changed, a rollback is copying them back, and nothing is serialized except to send or to reload. The owner asked for the direction ("how do we make it a resident? how do we allow externals to take part?"); the rest is Claude's picks.

- `Resident` is a built-in type for one field of a match singleton: `singleton Physics { Resident state; ... }`. Tide code can't read or write it, copy it out, or pass it except to an extern function's `in Resident` or `mut Resident` parameter, or put it in a struct, a component, a list, an event, an input, local state or the GPU. It's for the package's C alone, which gets it with the singleton. Only one such field per singleton, and eight at most in a game. A singleton that holds one can't be copied into a local or assigned whole. A Tide function or method can borrow it to read or change it, but can't take the same singleton both by value and `mut`: changing it could release pages the read copy still names. An extern function's borrowed `in Resident` and `mut Resident` parameters can be used together.
- **What it holds.** The pages of the library's arena as they were when the tick ended (a snapshot of that machine's own, never sent and never hashed), and three numbers the C side writes: a `kind` (which library's), a `stamp` (which state), and a `digest`. The world's hash covers the stamp and the digest, and nothing else of it. So what the match agrees on is the digest, and the engine's hash check finds a divergence through it within a tick, as it would through the pages of the library's state as a list.
- **The arena.** `tide/resident.h`: the engine keeps one arena for each kind in the process, at a fixed address for as long as the process runs (reserved whole, committed as it's used; 64 MiB unless the library says otherwise, past which the program stops as it does when a world runs out of memory). The library allocates everything it owns from it (`tide_arena_alloc`, `tide_arena_free`, which can be called from the tick's threads at once) and says what it may have written (`tide_arena_touch`): the arena keeps a byte a page for that. The arena's bytes are position-dependent (they hold pointers) and never the same twice (threads allocate in whatever order they get there): they're this process's own, and nothing of them crosses a machine. A compatible reloaded build takes the arena over at the same address and relinks what named the old build.
- **Commit and restore.** When the library's tick is done, `tide_arena_commit(arena, resident, stamp, digest)` copies every touched page into the resident (pages the world owns, shared with its snapshots and copied on write like every other page; a page that wasn't touched stays the one the world had) and clears the touches. Before a tick of a world whose resident pages aren't the arena's, `tide_arena_restore(arena, resident)` copies into the arena every page that differs from what the arena holds, by identity (the arena remembers which page it last loaded or committed at each place): the pages of the ticks between, and nothing else. A world's copy shares the pages; its free releases them.
- **Canonical bytes** are written by the library, through the kind's functions (`tide_resident_register(&kind)`, which the library calls as it loads; a world names its kind by the name's hash, so a library loaded again finds it): `pack` writes what a machine that has none of this state needs to make it (the library's own serialization, as the image is today), and `unpack` makes the arena's state from them and commits it. The engine calls them where a world is packed, unpacked, sent as a delta or carried across a reload, and only there: a joining player, a resync, a host migration, a hot reload. Nothing else ever serializes. A delta carries them whole (not against a base) for now.
- **Hot reloading.** A native reload with the same layout keeps the arena at its address: the registry rides in the local state the host keeps (`tide_local.tide_residents`), and the engine detaches that registry before the local state is cleared or freed. A build that takes over relinks callbacks, scratch and pointers into the previous build's constants (`relink`). A reload that changes the layout carries the resident through canonical bytes by the field's name, into the new build's arena. The web always migrates through canonical bytes, into its new linear memory.
- **One arena a kind**, so one live state: a world at another stamp is restored before it's stepped, and a question asked of a world at another stamp (a view's query of the confirmed world) restores that state, answers, and the next tick restores its own. Several arenas a kind, for a server and a client at different ticks in one process, are an open question: a page's bytes belong to the arena they were taken from.
- **What it costs, against the state as a list:** a tick pays for copying the pages it touched, not for writing its state out and comparing it; a rollback pays for copying the pages that differ, not for making a world again and writing it out whole. The library's own step is what it was.
- **Not built, and open:** a resident in a component or in local state; several arenas a kind; deltas against a base; a game's own libraries (which would need position-independent pages, or a schema to relocate them); what the digest covers beyond what the library says.

### Open

- Systems written in C: spawning, sending and structural changes from C; sparse components, with which rows have them as an array; the `World` and queries; text and lists; views and event handlers; and the game saying how much work a row is.
- Objects C owns (`ma_engine *`): a handle type only local state can hold, since pointers differ between machines.
- Keeping the state of C libraries across hot reloads, by building them apart from the game's library.
- Reading declarations from C headers, with tide's built-in clang.

## Packages

### Decided

- Games share code as packages: Tide code (and C) from a git repository, at a commit, or from a folder on this machine.
- A game lists its packages in a file of its own, `tide.packages`. A package from git is always pinned to a commit there, so every build of a game builds the same code; there's no lock file, and nothing is generated.
- A package declares nothing global: everything it declares is in its namespace.
- `tide update` moves packages to the newest commit of what they follow: a branch, or the newest tag of a version.
- Packages come from git hosts. Code adapts to the version of tide it builds with (see Conditional compilation).

```
# tide.packages
github.com/someone/tide-physics@v1 b01aac4d779e32c9c415283b65e40c86d691e7ab
../shared
```

### Provisional

Implemented, awaiting approval:

- `tide.packages` has a line each, and `#` at the start of a line or after a space starts a comment. A package line is a source, then its commit: `github.com/owner/repo 0123...` (40 hex digits, or 64), with `@ref` after the source for what `tide update` follows (the default branch without one) and `//folder` for a package in a folder of the repository. A line that starts with `.`, `/` or a drive (`../shared`, `D:/shared`) is a folder, relative to the file's, and has no commit. A line without its commit is an error that says `tide update` pins it.
- A package is a folder whose `tide.packages` says `package Name`: its name and namespace, dotted or not. Its files are in that namespace or one inside it, and it can't declare `Main`, the input or settings, which are the game's. `tide 0.3` says the oldest tide it builds with; a game can say it too. Its other lines are the packages it needs; a package from git can't need a folder.
- The game's `tide.packages` lists every package it builds with, those its packages need included: each one's lines must be in the game's, as the same source (any commit) or the same folder, and the game's line is what's built. `tide add` and `tide update` add what's missing. Two packages of the same name are an error.
- Files compile with each package's first, in an order where each package comes after those it needs (by name where that leaves a choice), each package's by path, then the game's. That's the default order of systems.
- A subfolder with a `tide.packages` of its own is a game or a package of its own, and isn't part of the folder's game or package: a package's example game lives inside it, listing `..`.
- A version-looking ref (`v1`, `v1.2`, `1.2.3`) follows the newest tag whose numbers start with it, leaving out pre-releases; any other ref is a branch, or else a tag, of that name.
- Packages from git download as their host's archive of the commit (GitHub, GitLab, Codeberg and Bitbucket), with curl and tar, into a cache shared by every game: `$TIDE_PACKAGES`, or `%LOCALAPPDATA%/Tide/packages` on Windows and `~/.tide/packages` elsewhere, at `<host>/<owner>/<repo>/<commit>`. A commit's files go there one by one, and a file named `.tide-fetched` last, which says they're all there: a folder without it is downloaded again. `tide run` and `tide build` download what's missing. Branches and tags come from git's own list of them (`info/refs`), which every host serves.
- `tide add <source>` takes URLs as people copy them (`https://...git`, `git@host:owner/repo`) and writes the source as `tide.packages` does. `tide update [names]` matches a package by its name, its repository's or folder's name, or its source, and only changes commits, leaving the rest of the file as it is.
- A package's systems that match no entity, and its handlers of events nothing sends, aren't warned about: a game may use only part of a package.
- The language server analyzes a game with its packages, and a package's file with an open game that uses it, or else the package on its own, which needs no `Main`.

Packages that come with tide: a package in tide's installation, which a game opts into with one line, and which costs a game that doesn't list it nothing. How the line is written and where the package is looked up are picks, awaiting the owner:

- Provisional: the line is a name alone, lower-case letters and digits after a letter: `box2d`. Nothing else in the file is written that way: a git source has a `/`, a folder starts with `.`, `/` or a drive (so a folder of that name beside the game is `./box2d`), and `package` and `tide` lines take a second word.
- Provisional: it takes no commit and no `@ref`, each an error that says why: the package is the version this tide came with. So what a game builds of it depends on the tide that builds it, where a git line's commit doesn't; `tide 0.3` in the game's file says the oldest.
- Provisional: the package is the folder `src/packages/<name>` of tide's installation (the folder tide's `bin` is in), beside `src/engine`: both are sources that compile with the game. This repo's are in `packages/`, which a build installs there. The installation's own `packages` folder is where packages from git are kept, and holds none of these, so `tide upgrade` replaces them with the rest of the installation and leaves that folder alone.
- Provisional: it's otherwise a package like any other. Its `tide.packages` says `package Name` and lists what it needs, which the game's file lists too: another that comes with tide by its name, or one from git. It can't need a folder, as a package from git can't. `tide run` reloads the game when its files change, as it does for a folder's.
- Provisional: a name no package has is an error that lists the names there are, with the closest when one is close. `tide add <name>` writes the line, in lower case however the name was typed, with the lines of what the package needs; `tide update` leaves the line alone and says nothing about it, and `tide update <name>` says `tide upgrade` is what updates it.

### Open

- A registry of names, so a line doesn't need its host.
- Packages from private repositories, and hosts other than those four.
- Symbols for the packages a game has, so code can add to another package only when it's there (`#if PHYSICS`).

## Mods

**Decided** is what the owner agreed (2026-10-05); the design proposed for the rest is under **Open**. Of it all, two parts are built (**Provisional**, below): `virtual` and `override`, as the language has them for any package, and the service's first slice, for web games, with `tide publish`. What a mod may be, games that load builds, and the sandbox aren't.

**Open again** (the owner, 2026-10-06: "its no longer decided"): who builds a game with its mods. That a service we run does was agreed, and built as a first slice, which is on hold. It isn't decided any more, and neither is what rested on it, each marked below: where a player's build comes from, who signs it and how it's revoked, what the `mods` setting lets a game run, and a closed game's source being the service's alone. What's being weighed instead, with nothing of it agreed: a game loads a build of itself for its platform, as `tide run`'s host loads one, and that build is compiled on the player's own machine from the mods' source where the platform allows a compiler (the developer opting in to ship it), or made elsewhere and fetched where it doesn't. What stands: a mod is a package and its code is game code, every machine of a match runs the same build, `virtual` and `override`, a mod being Tide alone, the prompt being the game's, memory safety, and the sandbox on desktop. And one thing agreed since: mods are the same on every platform (below).

### Decided

- A mod is a package that someone other than a game's developer adds to it. It builds as packages do: its files compile with the game's, as one program, so a mod's code is game code, with the same language, storage, schedule and speed. There's no scripting layer, nothing is interpreted, and games carry no runtime for mods.
- (Open again, see above.) Mods are built by a service we run, beside the relay and hosting, not on players' machines: it has the game's source and the mods', builds the game with them for each platform, and signs what it built. A game runs only builds signed for it.
- Every machine of a match runs the same build, as ever, its local code included: mods never differ between the players of a match, not even ones that only change views, since a view can show a player what gives them an advantage.
- (Open again, see above.) A player who joins a match with other mods gets its build from the service, never from the match's host. The game is told first, with the mods' names and where they're from, so that it can ask its player ("this match runs these mods"), who then knows that code from outside the game is about to be downloaded. It joins once local code says yes, and nothing is fetched before that.
- That prompt is the game's own, in its look and its language. The engine draws none and enforces none: it can't tell whether a game asked, one of its own wouldn't fit the game, and it isn't what keeps a player safe. A game whose code never says yes joins no such match, so none skips the prompt by accident. For mods a developer listed, whether to ask is the game's choice, as the developer approved them. For mods from anyone, telling the player is a condition of the service, which turns such mods off for a game that doesn't.
- A build's mods are a set: the order they're listed in changes nothing, since what would depend on it is an error.
- A mod is Tide alone: no `extern`, no C or C++ files, no libraries.
- `virtual` before a declaration says it can be replaced, as in C#, and `override` before another replaces it. Both are always written: nothing is replaced by having the same name, and nothing can be replaced that its developer didn't mark. It happens while compiling: what was replaced isn't in the build, and an override costs what the code it replaced would.
- An override matches by name and by parameters, both: the parameters' types, a system's filters among them, as overloads are told apart. Two systems can have the same parameters and filters under different names, and several declarations can share a name, so neither alone says which is meant.
- An override takes the place of what it replaces: its stage, its `[Before]` and `[After]`, and every reference to its name. Nothing calls the code that was replaced, for now (see Open).
- Two mods that replace the same declaration are an error that names both.

```csharp
// The game
virtual system Move(mut Position position, Velocity velocity, Time time)
{
    position.value += velocity.value * time.dt;
}

// A mod
override system Move(mut Position position, Velocity velocity, Time time)
{
    position.value += velocity.value * time.dt * 2;
}
```

- A mod's code sees files as a web page's does: files of its own, and nothing else of the machine.
- A mod's code calls nothing that reaches outside the game: `Session.Connect` and `Clipboard.Copy` are the first two. Which calls those are is part of the sandbox's work.
- (Open again, see above.) `mods` is a setting, false by default. With it, what `tide build` makes can also run the builds the service signed for the game. A game without it has none of this.
- (Open again, see above.) A closed game's source is seen by the service alone. Its modders have its declarations, each without its body.
- (Open again, see above.) Builds are signed with a key per game, and a build can be revoked: a game stops running one the service says no longer stands.
- On desktop, a build with mods runs inside the system's sandbox, as a second layer under the language's own rules. On the web, the browser's is that layer. A system that can't sandbox a build runs no mods, and the game says why: making the sandbox work wherever games ship is our job, so that's the exception.
- That no Tide code can corrupt memory is the language's promise to every game, and mods rest on it: it isn't a rule for mods.
- Mods are the same on every platform Tide targets, phones and consoles included (the owner, 2026-10-06, replacing what was agreed before, that a platform whose store forbids code downloaded after installing has the mods its developer built in and no others). The engine holds no platform back for what a store allows: whether a store or a system lets a game load code that came after it was installed is the game's developer's to know, and so is mods being less useful somewhere for it.

### Provisional

`virtual` and `override`, implemented for every package, a mod's or not, awaiting approval:

- **Where the words go.** Where `local` and `async` do, in any order with them, before a declaration with code: a system, a view, a handler (an event's, a command's, a local one), a function, a method, a frag or a vert. They're words of their own only there, and names like any other everywhere else, a function's type too (`virtual Make()` is a function that returns a `virtual`).
  - On data (a component, singleton, struct, event, command, enum, scene or the input), a constant, the settings or an `extern` function, each is an error that says what the words are for, and why this isn't it. Editors offer to remove the word.
  - Operators, `Interpolate` and the input's `Sample` and `Sanitize` aren't replaced either: an error on each.
  - `virtual override` together is an error: a declaration is replaced once, so nothing could replace an override.
- **The name an override writes.** It's the name of what it replaces, looked up as any reference is from the override's file: with its namespace (`override system Combat.Move(...)`), or alone in that namespace, in one inside it, or after `using Combat;`. Two `using` namespaces that both have it are ambiguous, as for any name.
  - A method's override is written outside its type, since it's in another package, with the type before the method's name: `override bool Stats.IsDead()`, or `override bool Combat.Stats.IsDead()`. The type's fields and methods are in scope, as in the type's body. Where a type and a namespace have the same name, it's the type.
  - An override's code reads names as its own file does: its namespace and its `using` lines.
  - It declares no name of its own: nothing in its own namespace is named after it, and it clashes with nothing there.
- **What it has to match.** An override that names nothing is an error that says what there is: the name in another namespace, with how to write it, a declaration of another kind under that name, or the closest name. One whose parameters are no declaration's lists the declarations the name has, as a wrong reference to a shared name does. One that matches a declaration that isn't `virtual` says so, and that marking it is its developer's choice; editors offer to mark it where it's the game's own.
  - Beyond its parameters' types, an override is what it replaces: a view's is a view, a handler's handles the same event (a command's the same command), `local` and `verified` on its event as the other has them, and `async` as the other is. A function's or a method's returns the same type, `fails` with the same error, and has `out` where the other has it. Each is an error that says what the replaced declaration has, with the override's start as it should be written.
  - It can differ in its parameters' names, in `mut` (a parameter's, and a method's own), and in a function's default values. Calls are checked against the override, since it's what's built: its defaults, its names for arguments passed by name, and its `mut`.
- **Where it runs.** It writes no attribute of its own: `[Stage]`, `[Before]` or `[After]` on an override is an error that says it keeps the replaced declaration's. Those are read as the replaced declaration's file wrote them. Its place in the default order is the replaced declaration's, among that one's file's declarations.
- **Another package.** An override is in another package than what it replaces, the game counting as one: a package replaces the game's code or another package's, and a game a package's. One in the same package is an error that says to change the declaration itself. A package can mark its own declarations `virtual`.
  - `tide` and the language server say which package each file is in. `tidec` by itself is told with `--package <Name>` before a package's files, on its command line or as a line of a list of files, which is how this repo's tests build games with packages (`tide_add_game(... PACKAGES <Name> <files>...)`).
- **What was replaced.** It's still checked for errors, as the code it is: its names, types, what it may write, what it returns and assigns. It isn't checked for what depends on the code that calls it or that it calls (a match system that calls a function that draws, say), as those rules are worked out over the program that's built, which it isn't in.
  - Nothing of it is built or counted: its spawns, Adds and Removes make no archetype, what it sends counts as sent by nobody, its calls make nothing wait, and it's in no schedule. It warns about nothing.
- **Parameters nothing uses.** A virtual declaration's parameters are what its overrides get, and an override's are those of what it replaces, so neither is warned about a parameter it never uses: a virtual system with an empty body is a hook. `mut` is each one's own, and one that's never written is still warned about.
- **The same declaration again, without `override`.** It's the error it always was (`is already declared with these parameters`). Where the first one is `virtual` and in another package, a note says `override` is what replaces it, and editors offer the word.
- **Names in tools and generated C.** An override in place goes by the replaced declaration's name, with its namespace: `Combat.Move` in `tide schedule`, the profiler and messages, `tide_system_Combat_Move` in generated C. A game that uses neither word generates the C it did, byte for byte.
- **Editors.** Hover says what an override replaces and what replaced a declaration, and so does a line above each. Go to definition on an override's name, or on `override`, goes to what it replaces; on a use of the name, to what's built under it. Go to implementation on a virtual declaration goes to its override. Find usages and rename treat an override and what it replaces as one name. After `override`, completion lists the virtual declarations of other packages that nothing replaces yet, each written out as the file would write it.

The service's first slice. Implemented, and on hold (the owner, 2026-10-06: it may be too complicated, from the service's side): the code is `builds/` in the repository, and nothing of it is deployed. It ran for a day at `builds.tide-engine.dev`. A developer publishes a version of a game's source; whoever has the game's key asks for a build of that version with a set of mods; the service builds it as a web page in a machine made for that build, signs it, and serves it at a link anyone can open. It was agreed for this slice that a game is an id and a key, with no accounts; that a build is named by a SHA-256 of the game's id, the version, its mods sorted, and tide's version; that builds are signed with a key per game, ECDSA on P-256, as 64 bytes; and that a build can be revoked. The picks:

- Provisional: `tide publish [folder]`, the name proposed below. It sends what a build reads from the game's folder: its `.tide` files, its C and C++ with their headers, its libraries and its `tide.packages`, with the packages in folders inside the game's. Packages from git aren't sent, as their commits name them, and a package in a folder outside the game's is an error. It doesn't compile the game first: a version that doesn't build is found when a build is asked for.
- Provisional: the game's key is in the environment, as `TIDE_GAME_KEY`, and nowhere else: no file. A key is the game's id, a dot and a secret, so one variable says which game too. An id is lower-case letters, digits, dots and dashes, as it's part of addresses. `TIDE_BUILDS_URL` names another service than ours.
- Provisional: a version is named by the SHA-256 of what was sent, and `tide` writes that archive itself (a tar, with no times, owners or modes, its files in one order) rather than with the system's `tar`, so the same files are the same version from any machine. `tide publish` says one line, with the version.
- Provisional: asking for a build is an HTTP call, not a command: `POST /games/<id>/builds` with `{"version": ..., "mods": [...]}` and the key as `Authorization: Bearer`. `GET /games/<id>/builds/<build>` says how it went, with what `tide` said, and `DELETE` there revokes it. The page is at `/b/<build>/`.
- Provisional: a mod's line is a package from git with its commit, as `tide.packages` writes it, from the hosts `tide` downloads from. What `tide update` follows (`@v1`) is no part of a build's name, a repository listed twice is an error, and a build takes 32 at most. Folders and the packages that come with tide aren't mods. Nothing checks yet that a mod is Tide alone.
- Provisional: the mods' lines go at the end of the game's `tide.packages`, and the lines of what they need after them, as `tide add` adds them. A mod the game already lists, or two packages of one name, is the error a build of that file gives.
- Provisional: `tide fetch [folder]`, a new command: it downloads the packages from git that the game lists and the machine doesn't have, each at its commit, adds the lines of what they need, and builds nothing. The service fetches with it, then builds with no network.
- Provisional: what's signed is a manifest, a line of JSON: `{"build", "target", "sha256", "size"}`, the SHA-256 being the page's. It's at `/b/<build>/manifest.json`, its signature (r then s) at `/b/<build>/manifest.sig`, and the game's public key, 65 bytes uncompressed, at `/games/<id>/key`. Nothing checks them yet: a game that loads builds will.
- Provisional: a build that failed is built again when it's asked for again. A revoked one never is: what it built is deleted, and its link answers 410.
- Provisional: a page is fetched again each time it's opened (`Cache-Control: no-cache`, with an `ETag`), so a revoked build is gone from caches too.
- Provisional: the limits. A version is 64 MB at most, and unpacks to 4096 files of 256 MB; a build has 10 minutes, 1 GB of memory and a page of 64 MB; its packages unpack to 512 MB; a game keeps 512 MB of versions and builds; 4 builds run at once.
- Provisional: whoever asks for a build gets all that `tide` said of it. That's the game's own developer for now, as asking takes the game's key; a modder of a closed game will get what it says of the mod's own files.

### Open

Proposed, awaiting the owner. None of it is built, but for the service's first slice (Provisional, above).

The language:

- A mod sees every declaration of the game's, as a package does (see Open, Access control). It declares what a package can, less `extern`. The packages it needs are mods too, or come with tide, or are ones the game lists itself: only those last two can have C.

What a mod's code reaches:

- Files. In a mod's code, `File`, `Directory` and the `Application` paths are that mod's: a persistent folder and a temporary one of its own, inside the game's, and the files its package came with, to read. A path that leaves them fails with `IOError.AccessDenied`. It goes by where the call is written, which the compiler knows. In a build with mods, the game's own code is kept to the game's folders too, by the sandbox, so a function of the game's that a mod calls reaches no further.
- Sessions. A mod's matches are in rooms: besides `Session.Connect`, a port for `Session.Open` isn't a mod's to give.
- `Clipboard.Copy` is on the list because text a player pastes elsewhere is a way out of the game.
- The rest is as it is for any code: devices (the game's window's alone), drawing, the GUI, `Debug`.
- What no rule stops: a mod can hang the game or end it, use its memory, draw anything in its window (a box that asks for a password included), and send what a player does in the game to the match's other players. Revoking builds, and the developer's say over who publishes, answer those.

The game:

- With the `mods` setting, a game runs another build of itself as a library on desktop and a program on the web, loaded as `tide run`'s host loads one, with nothing watched or carried over. It has the game's public key in it.
- A build is named by what it's made from: the game's published version, its mods, each at its commit, and tide's version. The mods are sorted first, so the same set listed in another order is the same build. The platform isn't part of the name: a build's generated C is the same everywhere, so its players share matches across platforms as they do now.
- Local code asks for a build by its mods, written as `tide.packages` writes packages. The game gets it, checks it, and starts it in the running one's place: the match and the local state start over. The command line takes the same. The names are to pick.
- A room says which build its match runs, and a server on an address says it as it refuses a hello. That's what a game about to join is told from. Once local code says yes, it gets the build, starts it, and joins. A join nothing said yes to ends with a reason of its own.
- Host migration and hosted servers need nothing new: every machine has the build, and the service builds a hosted server's with the rest.

The service:

- A developer publishes each version's source to it (`tide publish`, a name to pick): the Tide, the C, the libraries and the packages' commits. It stays private unless the developer says otherwise.
- Modders of an open game have its source. A closed one's declarations are what `tide` and the language server check a mod against. Only the service can build such a mod: `tide run` sends the mod's files as they are and runs what comes back. What a build says of the game is then its declarations, never its code.
- Who publishes mods for a game is the developer's choice, at the service, changed without shipping the game again: anyone, or the mods the developer lists.
- Builds run in machines made for one build, with no network while they compile (packages are fetched before, by commit) and limits on time, memory and what they write.
- A build is made once for each platform, the first time one is asked for, and kept by its name.
- Signatures are ECDSA on P-256 over a SHA-256 of the build's name, its platform and its file. A game checks one with the code it has for WebRTC (`rtc_p256_verify`), a page with the browser's own, every time a build is loaded, not only when it's fetched. The service keeps each game's key.
- Revoking is how a build that turned out harmful stops running on machines that already have it. So before a game loads a build it has, it asks the service whether the build is still allowed. It keeps the answer for a while, a day as proposed, and asks again after that: within that time the build runs with no network, and a build revoked meanwhile may still run. A shorter time stops a bad build sooner, and keeps modded play from starting sooner when a player is offline or the service is down. A revoked build is deleted. The developer revokes, and so do we.
- It's billed to the game: builds, what's kept and what's downloaded.

The sandbox:

- A build with mods runs in a process of its own, which the game starts sandboxed: an AppContainer and a job object on Windows, Landlock and seccomp on Linux, a sandbox profile on macOS. It has its window, the GPU, gamepads, the network, the game's folders, and its builds to read. It has no other file, no other process, and starts no program.
- The network stays open, as rooms need it. So code that got past the language's rules could still reach the network, with nothing of the machine's to send but the game's own files.
- WebAssembly is the sandbox where the platform runs it itself: browsers. On desktop it would mean a runtime in every game, ours to write or someone else's, and mod code slower than the game's, which the first decision rules out.
- Phones: the engine loads a build there as anywhere (see Decided: mods are the same on every platform). Apple's and Google's stores forbid native code downloaded after installing, as far as we know, and iOS itself loads only code that's signed, which is the developer's to deal with. Their own web views may run WebAssembly, so a modded game could also be its web build there.

The order to do the work in, proposed:

1. `virtual` and `override`, across packages. They're useful with no service: expansions, variants, and mods a developer builds in. Built (see Provisional).
2. Memory safety as a tested promise (see Open, at the end).
3. tidec's rules for mods: Tide alone, and files of their own.
4. Games that load signed builds, and the service, with listed mods only.
5. The sandbox, then mods from anyone.

Questions:

- Whether a game can also open every declaration at once, without a `virtual` on each.
- `base()` in an override, to call what it replaced: maybe later.
- Whether scenes and singletons' defaults can be replaced, which is how a mod would change `Main`.
- What else a mod's code can't call, beyond `Session.Connect` and `Clipboard.Copy`.
- How long a build runs offline before the service is asked again.
- Whether a modded game on a phone is its web build in the system's web view.

## Audio

Built: clips, one-shots, `Sound`, buses, `audio` effects and sources, device backends and the microphone. The original design below is followed by the per-card implementation picks, which describe the current API. [Audio](./game/audio.md) has runnable examples.

### Decided

- **Audio is presentation, never the match.** Like drawing, it reads the world and writes nothing to it: no sound is in a snapshot, a hash, a delta or a world sent to a joiner. Everything here is local code's and the platform layer's, and the simulation never includes an audio header.
- **One-shot sounds come from local handlers of match events.** `local event(Hit hit) HitSound(...)` is heard once under rollback: a sound plays late when a correction brings its event in, and never twice. A `verified` handler plays a sound only once the server confirmed the event.
- **Continuous sounds come from views**, which run every frame and see blended positions: a view says what should be sounding this frame, as it says what should be drawn.
- **Two ways to play**, both wanted: a quick sound that's fired and forgotten (`Audio.Play`), and a sound source a game keeps track of to change its pitch, volume and position as it plays (a `Sound` value in a local field).
- **Procedural audio**: a game modifies sound with code of its own, as Unity's `OnAudioFilterRead` lets it, and can make sound from nothing (a synth) or from a stream (a microphone). It's a declaration of its own, `audio`, compiled to C that runs on the audio thread, with the same shape as code the GPU runs: its parameters say what it reads.
- **Formats:** WAV, Ogg Vorbis, MP3 and native FLAC as build-time Clip sources. Runtime playback uses the engine's WAV and Vorbis decoders; MP3/FLAC are converted into float32 PCM WAV during the build, without adding their decoders to games.
- **Small and fast**: the pack stays as small as the author's files, and what plays is fast to reach at runtime. The pack keeps the file's bytes as they are; short clips are decoded to PCM at load, long ones streamed.
- **Headless runs have audio**: a run with no window mixes as a device would, and what played can be captured beside the frame's picture.
- **The block a filter sees is wrapped**, not a bare `float[512]` a game has to remember the size of: a built-in `Samples` that holds the array and says its size.

Original design sketch. The implemented API below differs; see [Audio](./game/audio.md) for current code.

```csharp
struct Knobs { float cutoff; }
struct LowpassState { float last; }

audio Lowpass(mut Samples samples, mut LowpassState state, Knobs knobs)
{
    for (var i = 0; i < samples.count; i++)
    {
        state.last += (samples[i] - state.last) * knobs.cutoff;
        samples[i] = state.last;
    }
}

local singleton Engine
{
    Sound hum;
}

[Stage(Layers.World)]
view Engines(Body body, Throttle throttle, mut Engine engine)
{
    engine.hum.clip = engineLoop;
    engine.hum.position = body.position;
    engine.hum.pitch = 0.8 + throttle.amount * 0.6;
    engine.hum.effect = Lowpass;
    engine.hum.knobs = Knobs { cutoff = 0.2 + throttle.amount * 0.5 };
    engine.hum.playing = true;
}

local event(Hit hit) HitSound(Body body)
{
    Audio.Play(hitClip, body.position, 0.8);
}
```

### Provisional

The following is the initial proposal, awaiting the owner. Later per-card picks below describe what is built and supersede it where they differ.

- **`Audio.Play(clip[, volume[, pitch]])` and `Audio.Play(clip, position[, volume[, pitch]])`** start a one-shot the mixer then owns: nothing comes back, and nothing can stop it. A `float2` position places it under the 2D listener, a `float3` under the 3D one. It's a call that does something, so it keeps its turn in evaluation order and is refused in match code, as drawing is.
- **`Sound` is a built-in struct**, a value kept in a local field: `clip` (a `Clip`), `position` (a `float3`; a 2D sound is one with `z` 0, under the 2D listener where no 3D camera is set), `volume`, `pitch`, `loop`, `bus`, `playing`, `effect` (an `audio` declaration, or none) and `knobs` (the struct that declaration reads). A view writes it each frame. The mixer keeps a voice for each `Sound` a frame names, told apart as GUI widgets are (by the field and the entity), and fades a voice out over a few milliseconds when a frame stops naming it, so a destroyed entity's sound doesn't hang. `playing = false` stops it the same way; writing `clip` to another clip starts over. A `Sound` has no methods: the fields are the API.
- **The listener** is a frame's setting, with both forms like the draw settings: `Audio.Listener(position, rotation) { ... }` and `Audio.SetListener(...)`, defaulting to the 3D camera, or to the 2D camera's center where none is set. Volume falls off with distance by one curve with a game-set reference distance (`Audio.SetFalloff(near, far)`), and pans by the listener's right.
- **Buses are a game's enum**, `[Bus] enum Bus { Master, Music, Sfx, Voice }`: `Audio.SetVolume(Bus.Music, 0.5)` holds for the frames after until it's set again, and a bus can have an `effect` too (`Audio.SetEffect(Bus.Master, Compress, knobs)`). Every voice goes through its bus and then `Master`. No graph, no sends, no reverb zones in v1.
- **`audio` declarations** run on the audio thread, once a block, for the voice or bus they're set on. Their parameters: the block, `mut Samples`; at most one `mut` struct, the voice's own state, which persists between blocks and nothing else reads or writes (plain fields only, as a frag's); and read-only structs, the knobs, which the frame writes into the `Sound` (or the bus) and the mixer hands the thread with its next block, so the thread reads a copy, never the local world. What's refused is what the GPU refuses, and for the same reason: the `World`, text, lists, maps, grids, allocation, `Debug.Log`, events, tasks, anything that reaches outside; `Math` all works; the functions it calls are checked the same way (`refuse` in the shader writer's checking pass is most of the rule). A declaration given as a `Sound`'s `effect` filters that voice; one set on a bus filters the mix; one given to a `Sound` whose clip is `Clip.none` is a **source**: its block starts as silence and what it leaves is what plays (a synth). One `audio` per voice or bus in v1; chains are functions it calls.
- **`Samples`** is a built-in struct holding one `float[512]`: `samples[i]` (a float, interleaved left then right, with an array's bounds rule: past the end reads zero and writes nothing), `count` (512), `frames` (256), `channels` (2), `rate` (48000), each a constant, and `Frame(i)` as a `float2` with `SetFrame(i, value)`. In generated C it's the array, so a filter's loop vectorizes. If the block ever changes size, every filter still compiles.
- **The mixer is stereo at 48 kHz in blocks of 256 frames**, in floats, on the system's audio thread. A clip is resampled to 48 kHz at load and a mono clip plays as a stereo voice; a device that wants another rate or block size is adapted to behind the mixer, which is a backend's job. Pitch is linear resampling. The frame hands the thread its commands (voices named, knobs, bus volumes, one-shots) through a lock-free queue, double-buffered by frame: the frame never blocks on audio and the thread never waits for a frame.
- **The microphone is a source**: `Clip.Microphone` as a `Sound`'s clip gives the voice the capture device's frames, which its `effect` can filter and its bus plays or mutes (`volume` 0 to only process). Capture starts when a frame first names such a `Sound` and stops when none does. On phones and the web the system asks the player; until they say yes the source is silence. Nothing of it reaches the network in v1: voice chat is a package later, over the data channel.
- **Assets:** `asset Clip shot = "sfx/shot.wav";`, alone or as a glob's `List<Clip>` or `Map<string, Clip>`. Source files accept mono/stereo PCM 8/16/24/32-bit or float32/64 WAV, Ogg Vorbis, MPEG Layer III MP3 and native FLAC with a declared sample count. WAV/Vorbis bytes are retained; MP3/FLAC are validated and converted to float32 PCM WAV at build time, with source rate and channel count preserved. The latter imports are limited to 128 MiB source, 256 MiB decoded PCM and 30 minutes. At load, WAV and Vorbis up to ten seconds are decoded once on the pool's threads at their source rate; longer Vorbis streams during playback. Converted MP3/FLAC is always fully decoded, so Ogg Vorbis is the compact streaming choice for long music. Unsupported formats, codecs, channels and damaged data give a diagnostic at the path. A `Clip` names its declaration's sound; saved files refresh retained values under `tide run`. `Clip.none` plays nothing. No `Clip.length` property is built; clips are local code's.
- **Headless:** a run with no window mixes exactly the blocks a device would ask for at the frame loop's made-up 60th of a second (at 48 kHz a frame is 800 frames of audio, 3.125 blocks of 256: the mixer runs as many whole blocks as the time reached), and `--capture-audio <file.wav>` writes the run's whole mix as 16-bit stereo PCM beside `--capture`'s PNG. Nothing in it is a device's or a clock's, and the mixer is the engine's own float code, so a headless run's audio is the same bytes on every machine, which its pictures can't be. A script line, `300 capture-audio hit.wav`, writes what played since the last such line. Tests compare the mixer's output exactly, on every platform, with no device.
- **Backends**, one mixer against a small `platform/src/audio.h` (a device that asks for stereo frames at a rate, and a capture device that gives them): WASAPI on Windows, CoreAudio on macOS, ALSA loaded at run time on Linux (as OpenGL and X11 are, so games link nothing), AAudio on Android, and on the web an `AudioWorklet` fed from a ring buffer the program fills in shared memory, in `tide.js` as the GPU's objects are. A machine with no device (CI, a server) runs the null backend: the mixer still runs, so `Clip.length` and captures work. Phones and browsers start audio only after a player's gesture, and a phone in the background loses its device: the activity and the page handle both, and nothing in the API says "failed". **Hot reload:** voices live in the platform layer, which outlives the game's library on desktop, so sounds keep playing across a reload; on the web they go with the page's state, as the stats drawer does. An `audio` declaration whose code changed is swapped at the next block.
- **Stats:** the drawer's open list gains one line, voices playing and the mixer's time a block, measured only while it's open.
- **The order to build it in:** the mixer, the null backend and `--capture-audio` with tests; WAV assets and `Audio.Play` from a local handler; `Sound` fields and the listener; buses; `audio` declarations with state and knobs; backends, desktop first, the web second, Android last; Vorbis; the microphone.

Picks made as the mixer was built (2026-10-07, the first card: `tide/audio.h`, `platform/src/mixer.c`, `--capture-audio`; nothing of it is in Tide yet), awaiting the owner too:

- **A frame's audio is a list, as its drawing is** (`tide_audio_frame`): one-shots, the voices named this frame by an id, the listener, a falloff and each bus's volume and effect, with every effect's knobs copied in. The host hands it to the platform once a frame, after the frame drew, and the mixer takes it at its next block: the newest frame wins for what's state (voices, listener, buses), and one-shots go through a queue of their own so none is lost when two frames come between two blocks. The mixer allocates nothing and waits for nothing on its thread.
- **Every frame starts from the defaults**, as draw settings do: a frame that says no listener has it at the origin, its right +x, and one that says no falloff has a spatial sound at its volume within 1 unit and silent past 50. A listener is a position and a rotation; a spatial sound pans by where it is against the listener's right (equal power: a sound in front is heard at 0.707 each side), and falls off as `near / distance`, scaled to reach 0 at `far`. A sound that isn't spatial plays at its volume on both channels.
- **A voice named again goes on; one not named fades out over a block** (256 frames, 5 ms) and is gone, as does one whose `playing` goes false, and `playing` back to true starts its clip over; another clip starts over too. A voice whose clip ended, and doesn't loop, stays silent while it's named. A one-shot can't be stopped and ends with its clip. Volume and pan changes ramp over a block, so a value written each frame doesn't click; a voice's first block starts at its gains, with no ramp in.
- **Limits** (a game's own sounds past them are dropped, counted for the stats): 256 voices and one-shots at once, a new one past that taking the quietest's place; 1,024 one-shots queued between two blocks; 16 buses; an effect's state of a megabyte, 4 KiB of it in the voice itself and anything bigger in memory the mixer makes for the voice while it plays (it was 4 KiB in all until 2026-10-08), and its knobs of 256 bytes (1 KiB until `Effect` values held them: an `audio` declaration's structs are checked against both as the game compiles, below).
- **Effects run on the audio thread in order**: a voice's on its block after its clip is read (so a source with no clip starts from silence), before its volume and pan; a bus's on the bus's mix before the bus's volume; then every bus into the master, and the master out. A voice's effect state starts over when its effect's id changes. A frame's `forget` (a reload) stops every voice at once, with no fade, and drops every effect.
- **A clip is read at its own rate**, mono or stereo, through linear interpolation, at `pitch` times its speed: resampling at load is an optimization for later, not a rule. The cursor is a double, so a long clip doesn't drift.
- **Headless runs** render 800 frames of audio after each frame (48,000 a second over 60), in whole blocks with the remainder carried, so `--capture-audio` is the run's whole mix and a script's `capture-audio` line what played since the last. A sound a frame plays starts at the first whole block not yet rendered when the frame ran, up to a block (5 ms) later than the frame's own start. The WAV is 16-bit stereo PCM, each sample clamped and rounded to the nearest, ties away from zero. `tide_platform_audio` on a window with a device does nothing yet: the backends are a later card.

Picks made as `Clip` assets and `Audio.Play` were built (2026-10-07, the second card), awaiting the owner too:

- **`Clip` is a type beside `Texture`, this machine's.** An asset's (`asset Clip shot = "sfx/shot.wav";`, and a glob's `List<Clip>` or `Map<string, Clip>`, the one place a list or a map holds one), a value in local fields, locals and parameters, and never the match's: the error is the one rendering resources give ("rendering resources belong to this machine, so the match can't use them"), as the rule is the same, though the words name rendering. `Clip.none` is the default. A `Clip` names its declaration's sound, as a `Texture` names its image, so a copy follows `tide run`'s packs, and a name the loaded pack hasn't got plays nothing. No way to make a `Clip` of bytes at runtime yet, and nothing to read of one (no `length`).
- **The pack keeps a WAV as the file is**, decoded by the engine the first time it's played (a memcpy's worth of work for PCM) and kept by what's in the file, so two declarations of one file are one decode, a file edited under `tide run` another beside it, and a voice that plays the old samples keeps them until the game's library is unloaded (`tide_clips_free`, after the host had the mixer forget). Decoding on the pool's threads at load came with Vorbis (the seventh card, below).
- **What a WAV may be:** PCM of 8, 16, 24 or 32 bits or floats of 32 or 64, plain or `WAVE_FORMAT_EXTENSIBLE`, one or two channels, any rate; a sample becomes a float by one division, so a file is the same bits everywhere. The build refuses anything else at the path ("isn't a WAV that decodes").
- **`Audio.Play(clip[, volume[, pitch]])`, `Audio.Play(clip, position[, volume[, pitch]])`** with a `float2` (z 0) or a `float3`, each default 1: three overloads, as `Draw`'s are. Who may call: views, local handlers and the functions they call, which are handed the local state as functions that read `Frame.dt` are (the frame's audio list is in it: `tide_local_audio`, which the host gives before each frame); the match, the input's `Sample`, tasks, methods and GPU code can't, each with its own message, and a function that plays called from the match is the error `Frame.dt`'s is. So `Audio` is an owner like `Draw`, with `Draw`'s rule widened to local handlers, where `Draw` stays a view's.
- **A host that gives no audio list** (a C host of its own) records nothing, and nothing breaks.
- **A hot reload stops every sound**: the old build's clips are its own, so the host has the mixer forget before it unloads the library (`tide_platform_audio_forget`). A pack loaded again (a saved file under `tide run`) stops nothing.

Picks made as `Sound` fields and the listener were built (2026-10-07, the third card), awaiting the owner too:

- **`Sound` is a built-in struct**, `{ Clip clip; float3 position; bool spatial; float volume = 1; float pitch = 1; bool loop; bool playing; }`, in generated C only for a game that names it, as `Depth` is. It's a resource of this machine's, as it holds a `Clip`: local singletons, local components, a match component's `local` fields and structs in those, and never the match's or a list's. `bus`, `effect` and `knobs` join it with their cards.
- **The frame names every `Sound` that plays, itself** (`tide_local_sounds`, at the end of `tide_frame`, after the views and the local commands they queued, so an entity spawned or destroyed in a frame is counted that frame): a view only writes the fields. A voice is named by its field's path (hashed) and, for a row's, the entity's bits, so the same field on the same entity is the same voice frame after frame, and another entity's another voice. A `Sound` that isn't `playing` isn't named, so the mixer fades it out as it fades one whose field is gone; `playing` false then true, or another clip, starts over (the mixer's rule). Nothing of a `Sound`'s state is in the field but what the game wrote: where the clip is up to is the mixer's.
- **The listener and the falloff are `Set` forms alone for now**: `Audio.SetListener(position, rotation)`, `Audio.SetListener(center)` (2D: z 0, facing +z) and `Audio.SetFalloff(near, far)`, each a frame's setting that holds from where it's set to the frame's end, and every frame starts from the defaults. The block forms (`Audio.Listener(...) { }`), and the listener defaulting to the frame's camera, aren't built: the default is the origin.

Picks made as buses and `audio` declarations were built (2026-10-07, the fourth and fifth cards), awaiting the owner too:

- **`[Bus]` on an enum of the game's** names its buses, the first the master, 16 at most, one such enum a game; `Sound.bus` is of that enum (an `int` in a game with none, 0 to 15), and `Audio.SetVolume(bus, volume)` and `Audio.SetEffect(bus, effect)` take it. No block forms yet.
- **An `audio` declaration is a function the audio thread runs** (`audio Name(...)`, a declaration word like `frag`): its first parameter `mut Samples samples`, then at most one `mut` struct of plain fields (its state) and at most one read-only struct of plain fields (its knobs), in either order. `Samples` is a built-in struct of one field, `float[512] values`, so `samples.values[i]` and `samples.values.count` are the block (the wrapper the owner asked for, over a bare `float[512]`): no `Frame(i)`, `frames`, `channels` or `rate` yet. Its body is checked as a function's, and then refused if it, or a function it calls, draws, plays, reaches files, reads `Frame.dt`, reaches the world, calls C, is a task, or makes text, lists, maps, sets or reads grids: the audio thread has no scratch memory and no world. A `Random` is allowed, as plain arithmetic.
- **An effect is a value**, `Effect`, made by calling the declaration with its knobs: `Lowpass(Knobs { cutoff = 0.2 })`, or `Square()` with none. It's a built-in struct of the declaration's id (its name hashed), the knobs' size and 256 bytes of them, which `Sound.effect` keeps and `Audio.SetEffect(bus, effect)` takes; so a game's knobs are 256 bytes at most, and an `Effect` in local state survives a reload as a name (the new build's table of effects resolves it). No way to say "no effect" but `default`. In generated C each declaration has a wrapper of the mixer's signature and an entry in a table by id, which the frame's walk resolves as it names a `Sound`; the Effect's knobs bytes go into the frame's list by copy.
- **A state and its knobs have a size, and a struct past it is an error** (2026-10-08). An `audio` declaration's state struct is 1,048,576 bytes at the most (a megabyte: 2.7 seconds of stereo), and its knobs struct 256, what an `Effect` holds. A struct's bytes are what the generated C has for it, which tidec knows: its fields in order with their padding, a `float` or an `int` 4, a `long` 8, a `bool` or a `byte` 1, an array its elements'. A bigger one is an error at the parameter ("an audio declaration's state is 1048576 bytes at the most, and 'Tail' is 1048580"), with a note that says to make it smaller. A declaration that rolls with `Random`, itself or through a function it calls, keeps its generator after its own state, on the next 8 bytes (Random numbers, above), so its state is 8 bytes less at the most, and the error says so, with where it rolls.
- **A state can be big, for an echo or a reverb** (the owner, 2026-10-08: "i do want echo or reverb to be a future feature so maybe we prepare now", and of the two questions that left: that a long delay line is written as a longer array in the state, and that it's built now. The megabyte, the 4,096 bytes a voice keeps itself and the rules below are Claude's picks). Until then a state was 4,096 bytes, kept in every one of the mixer's 256 voices and 16 buses, which is 1,024 floats, about 10 ms of stereo: no delay line to speak of. What's built:
  - **The mixer makes a big state's memory** (one of more than 4,096 bytes) for a voice or a bus while frames play it, on the frame's thread, and frees it there once the audio thread is done with it: the audio thread allocates nothing, as before. So only the sounds that play through such an effect pay for it. A state of up to 4,096 bytes is in its voice, as it always was.
  - **A frame that doesn't play the voice lets its big state go**, and the next that does starts the effect over, from zeros: a `Sound` whose `playing` goes false, or whose field is gone, or that's given another effect. That's what the game sees of any effect's state. Underneath, a small state's lifetime is its voice's in the mixer, which fades out over a block, and a big one's is by the frames as they were submitted, so where two frames come between two blocks of audio and the first doesn't play the voice, a big state starts over though the mixer never saw the voice stop. A bus's effect is said every frame (`Audio.SetEffect`), and a frame that doesn't say it lets its state go the same way.
  - **A one-shot has no big state**: C that gives one an effect with one plays it without the effect. `Audio.Play` takes no effect in Tide.
  - **An effect that says more state than the most there is plays nothing of its own** (`tide_audio_voice` records no effect for it): for C, as tidec refuses such a declaration.
- **A long array: more than 4,096 elements, in an audio declaration's state alone** (2026-10-08, the owner's pick over a buffer type of its own; the rules are Claude's). `float[96000] line;` is a field of a struct, and such a struct, or one that holds it through structs and arrays, is only ever an `audio` declaration's state. Up to 1,048,576 elements, all of an array's lengths together, as many as a state has bytes. Everywhere else an array holds 4,096 at the most, as it did, with the error it had.
  - **Nothing copies it**, as a copy would be hundreds of kilobytes on the audio thread's stack. A value with a long array in it is read and written where it is: `tail.line[i]`, `tail.line.count`, a field of it, an element of an array of such structs and that element's fields, or the state passed on to a function or a method as a `mut` parameter. Anything else is an error where it's written, which says what to write: a local of it (`var copy = tail;`), an assignment of one (`pair.a = pair.b;`, `tail = default;`, a struct's literal), a `foreach` over a long array (which goes through a copy of its array: go through it by index), `==`, text made of one, an argument a parameter takes by value.
  - **Where such a type can't be written:** a field of anything but a struct (a component's, a singleton's, an event's, the input's), what a list, a map, a set, a grid or a `T?` holds, a local, a constant, a parameter that isn't `mut` (an `out` one, an operator's, a frag's or a vert's, an `extern` function's, an async function's, an audio declaration's knobs), and what a function returns. Such a struct has no methods, as `this` is a copy: a function takes it `mut`.
  - **An element of an array of such structs isn't passed on as `mut`** (`Sum(hall.sides[0])`), by the rule arrays' elements already had: the function takes what holds the array, and which element.
  - **A struct's field defaults count for nothing in a state**, long or not: a state starts as zeros.
  - Not built: a way to say how long a state's array is as the game runs; a `Sound`'s share sent to another bus (what lets one reverb on a bus serve many sounds, each at its own amount); a state a game reads back from a view.
- **The named gating** that keeps other games' C as it was covers `Sound`, `Samples` and `Effect` (the structs, their blend helpers, the layout) and the arrays only their fields made (`float[512]`, `byte[256]`: their types and helpers), which are numbered after the game's own arrays so those keep their names.

Picks made as the backends were built (2026-10-07, the sixth card), awaiting the owner too:

- **One pull for every device** (`platform/src/audio_out.c`): the mixer's stereo at 48 kHz, read out at the device's rate by linear interpolation between two of the mixer's frames (so a device at 48 kHz hears each frame a frame late, and other rates a little blurred: a proper resampler is for later), a mono device given the average and more channels the two and silence. Each backend fills its device from it on the device's thread: WASAPI (shared mode, event driven, at the device's mix format, floats or 16-bit integers), CoreAudio (the default output unit, given stereo floats at 48 kHz to convert itself), ALSA (`libasound.so.2` at run time, `default`, stereo floats at 48 kHz with ALSA's own resampling, 50 ms, a thread of ours writing a block at a time), AAudio (floats, a data callback). The device opens with the window and closes with it; a run with no window opens none.
- **The web's device is the page's** (`platform/web/audio.c`, `tide.js`): an AudioContext and an AudioWorklet of our own, which plays stereo chunks the main thread posts it; the page asks the program to render (`tide_web_audio_render`, up to 4096 frames) from its frame and from a 20 ms timer, enough to keep about 90 ms queued by the context's clock, as the program's mixer has no thread of its own in a page. So the web is about 90 ms behind the frame, where a desktop is a device buffer (10 ms) behind. The context resumes at the page's first pointer, key or touch, which browsers want; a page starved of chunks (one that was away) starts again from where the clock is, with nothing to catch up. Nothing is shared with the worklet, isolated page or not, as a copy a chunk is small.
- **A forget waits for the device's thread** (`tide_mixer_forget`, `tide_mixer_rendered_elsewhere`): a reload stops every voice once that thread has taken the forget at its next block, or after half a second, for a device that stalled.
- **Not run here**: CoreAudio (no Mac, and CI has none for now), AAudio (built under the NDK, no device), ALSA on a machine with a device (WSL has none). WASAPI is run by `tide_platform_audio_device` on this machine, and the web's by the page tests, which open the context in headless Chrome without a gesture, suspended.

Picks made as Ogg Vorbis was built (2026-10-07, the seventh card), awaiting the owner too:

- **The decoder is `tide/vorbis.h`** (`engine/src/vorbis.c`), from the Vorbis I specification and RFC 3533, in floats, with a sine of its own for its windows and twiddles (the engine's math is floats, and tidec compiles the decoder for the build's check without the math library): the same samples on every machine. It's held against ffmpeg's decodes of three files its encoder wrote (`tests/audio`), to within 3e-5 of a sample. The whole file is read as pages first (every checksum checked) and its packets copied out, so the bytes it was opened from can go; headers are parsed at open and nothing of the audio is decoded then, which is what the build's check does to a file.
- **Floor type 0 is refused** ("a floor of type 0, which this decoder doesn't read"): no encoder has written one in twenty years, and untested code is worse than a clear refusal. Files of more than two channels are refused as WAVs are: the mixer plays mono and stereo.
- **What a file holds is its last page's granule position**, and the reader never gives more: a file whose encoder padded nothing after its last block ends a part of a block short of it (ffmpeg's own encoder does: its decoder then gives 128 frames fewer than the granule, and for another file 160 more; the reader gives the granule). A packet cut short or corrupt decodes as the specification has it (silence for one that ends in its floors, what was decoded for one that ends in its residues), and the file goes on.
- **Ten seconds is the line between decoding whole and streaming** (`tide_clips_whole_seconds`, a variable so tests move it), by the granule and the rate: under it a clip is decoded whole as a WAV is, over it the `tide_clip` carries the opened file (`stream`) and no samples, and a voice that plays it holds a `tide_vorbis_reader` (made at its first block, on the audio thread, about `tide_vorbis_reader_size` bytes: 300 KB for stereo at the biggest block size), reading two frames at a time through the same interpolation a decoded clip's samples go through, so pitch and the clip's own rate work alike; a loop rewinds the reader at the end. A WAV is never streamed: its decode is a copy.
- **Clips are decoded as the pack loads** (`tide_clips_prepare`, which the standard host calls after `tide_assets_load` and after each pack `tide run` writes, with the platform's pool), each file once, on the pool's threads where there's more than one clip to do; `tide_clip_of` still decodes a clip nothing prepared, at its first play. `tide_pack_clips_each` is how the engine lists a pack's clips.
- **What a reader costs** is Vorbis's: the three reference files (1.3 seconds of audio) decode in 2 ms in a release build (6 in a debug one), about six hundred times real time a core, so a game's library of effects is ready in milliseconds, behind the web's wait for its GPU device. A streamed voice's block (256 frames) is a fraction of a Vorbis block, so the audio thread decodes a Vorbis block every few of its own.
- **Seeking a streamed clip isn't built**: a `Sound` whose `time` would move it can't yet, and a stream starts over when the frame starts its Sound over. Nor are a Vorbis clip's bytes at runtime, chained Ogg streams (the first logical stream is read, and the rest ignored), or an Ogg that starts at another granule than 0.

Picks made as the microphone was built (2026-10-07, the eighth card), awaiting the owner too:

- **`Clip.Microphone` and `Clip.none` are members of `Clip`** (the built-in members' table, so editors list and explain them). `Clip.none` had been documented and never built; it's `default`. The microphone's clip names nothing of the pack's: a `tide_clip` that says `microphone`, mono at the mixer's rate, with no samples.
- **The frame says it** (`tide_audio_frame.microphone`, set by a voice whose clip is the microphone and is playing), **and the platform opens the device while frames do** (`tide_platform_audio`), closing it when none does, and trying again every second while it couldn't be opened: a player still being asked, a device that isn't there yet. Nothing asks until a game names such a `Sound` playing, so a game that never does never prompts anyone.
- **What's captured goes through one path** (`platform/src/audio_in.c`): the device's frames, whatever their channels (averaged to one) and rate (linear interpolation to the mixer's, each device frame emitted as it came and one frame late), into the mixer's ring (`tide_mixer_capture`, 16384 frames). The renderer takes a block from the ring as each block begins, once it holds four blocks (21 ms: a device delivers in bursts), plays silence and waits for a prime again when it runs dry, and skips to the newest frames, a prime back, when it's more than eight blocks behind (a device that ran on while nothing rendered). Every microphone voice plays that block, in both channels, whatever its pitch: the device sets the pace. A forget empties the ring.
- **A one-shot of the microphone records nothing**: `Audio.Play(Clip.Microphone)` is silent, as a one-shot has no end a device would give it.
- **The backends:** WASAPI's default capture endpoint on a thread of ours (floats or 16-bit integers, a silent packet as zeros); an `AudioQueue` input of mono floats at 48 kHz on macOS; ALSA's capture stream, mono floats at 48 kHz, read on a thread of ours; AAudio's input stream on Android, after `RECORD_AUDIO`, which the app's manifest names only for a game that says `Clip.Microphone` (`game_info.microphone`, from tidec's `uses_microphone`) and the app asks for (`requestPermissions`, through JNI) the first time it wants the device, then polls as the platform tries again; `getUserMedia` on the web, into the context the output plays in, through a worklet that posts what it hears to the page and the page to the program (`tide_web_audio_captured`), silence until the player allows it and the context runs.
- **A run with no window plays a file into it**: `--microphone <file>` (`tide_headless_desc.microphone`, a WAV or Ogg Vorbis, read before anything runs and refused with exit code 2 when it can't be), looped, a 60th of a second of it a frame through the same path a device's frames take, so a test hears it where a player would be heard; without it the microphone is silent.
- **Capture validation:** `tide_platform_audio_device` opens and closes this machine's WASAPI capture (nothing checks what it hears: a muted one hears silence). A headless Chrome check with its fake microphone ran `getUserMedia`, the input worklet and the WebAssembly capture bridge through the resampler and mixer: nonzero samples reached a muted voice's effect, output stayed silent, and closing capture ended the stream's tracks. Six Node checks cover delayed permission and worklet answers after close/reopen, queued messages, sample delivery and the input processor. **Not run here:** CoreAudio, ALSA with a device, AAudio, and the browser's physical microphone or permission prompts. **Not built:** choosing a device, stereo capture, echo cancellation or gain control as choices (the web's `getUserMedia` has the browser's defaults), Android's permission result as a callback (the platform polls), a macOS bundle with its usage text, voice chat.

### Open

- Effects a game can't write itself: a reverb, which needs more state than a struct of plain fields (a `float[N]` delay line fits once arrays are in `audio` state structs, which is the first thing to try), and sends to a shared reverb bus.
- A chain of `audio` declarations on one voice (`List<Effect>`), where v1 has one.
- 3D audio beyond panning and distance: HRTF, occlusion, doppler (`pitch` by hand does the last).
- Streaming a clip from a URL or a file at runtime (`Web.Fetch` gives the bytes; a `Clip` made of bytes is the missing piece), and recording to a file.
- Voice chat as a package over the data channel, with the microphone as its source.
- Sample-accurate scheduling (a sound that starts at a tick's exact time, music synced to the match's beat): `Audio.Play` starts at the next block.
- Whether the simulation should ever hear audio (a rhythm game's match reading the music's position): today it reads `Time.t` and the view keeps the music to it.

## Conditional compilation

### Decided

- Code can adapt to the version of tide it builds with, as C# code adapts to Unity's with `#if UNITY_2022_3_OR_NEWER`: tide gives the tools, and packages adapt to the versions they support.

```csharp
#if TIDE_0_4_OR_NEWER
const int SLOTS = 8;
#else
const int SLOTS = 4;
#endif
```

### Provisional

Implemented, awaiting approval:

- C#'s directives: `#if`, `#elif`, `#else` and `#endif`, each on a line of its own (spaces before it are fine, and a `//` comment after it), nesting. Conditions take symbols, `true`, `false`, `!`, `&&`, `||`, `==`, `!=` and parentheses.
- Tide's versions are symbols: `TIDE_0_4_OR_NEWER` is true in tide 0.4 and after, `TIDE_0_4_2_OR_NEWER` in 0.4.2 and after. Only a version's numbers count: a pre-release (0.4.0-nightly.3) is the version it comes before. Any other symbol that starts with `TIDE_` is an error, so a misspelled version doesn't quietly leave code out.
- Lines a directive leaves out aren't read, only looked through for the directives that end them, as in C#: they can hold anything, like code with syntax a newer tide reads. A directive inside a `/* */` comment in code that's read is part of the comment.
- Editors show the lines left out as comments, fold each branch, and the formatter leaves directives and those lines as they are.

Symbols beyond tide's versions (the owner, 2026-10-07: `#if` with no `#define` isn't much, symbols from the command line, `#if DEBUG`, and knowing whether a package is installed; everything below is Claude's pick of how):

- A symbol is defined or not, with no value, as in C#. One that nothing defines is false, and no error: `#if STEAM` has to compile in the build that doesn't define it.
- `#define NAME` and `#undef NAME` are C#'s: for the file they're in, and before its first token, as they're for all of it; after code they're an error. `#undef` takes away a build's symbol too, in that file. A line in a branch that's left out isn't read.
- `--define NAME` on `tide run`, `build`, `schedule` and `shaders` (and tidec, and a line of its `@list`; `DEFINES` in `tide_add_game`) defines a symbol for every file of the game and its packages. No `-D`, no `NAME=value`, no setting or `tide.packages` line for it.
- `DEBUG` is defined in every build but `tide`'s `--release` (in this repo's CMake builds: in the Debug configuration). Nothing else is predefined: no `RELEASE`, no symbols for `--web` or `--android`, and none for platforms, as a game is one code for all of them.
- `PACKAGE_<NAME>` is defined for each package the game builds with: its name (the `package Name` of its `tide.packages`, its namespace) in capitals, anything that's no letter or digit as `_`. No versions of packages.
- `TIDE_` and `PACKAGE_` names, `true` and `false` can't be defined, by a file or a command line.
- **The build's symbols that conditions read are part of the game's identity.** Each one a condition reads (not a file's own, not tide's versions) is listed in the generated header with whether it's defined, so it's in the game's hash: a debug and a release build of a game that says `#if DEBUG`, or builds with other `--define`s that the game reads, are different games and join none of each other's matches, where they'd otherwise connect and correct each other every tick. A game that reads none has the header it had. The alternative that was weighed: refusing those symbols in match code, which a lexer can't tell.
- Editors read `DEBUG` as defined and know the game's packages; what a command line defines they don't, and read as not defined.

## Open

- How entities authored as data (levels, prefabs) feed into archetype derivation.
- Archetype growth past what reachability trims: an `Add` or `Remove` on an entity read from a field or a parameter still reaches every archetype of its world, so a game that toggles many components on arbitrary entities multiplies combinations. Sparse components (a presence bit in the row instead of an archetype of their own) are the way out; see Entities.
- How modules, such as the engine's built-in systems, initialize when there's a single `Main`.
- Access control: whether a namespace can keep declarations to itself.
- Memory safety as a promise: that no Tide code, however wrong, can corrupt memory or reach outside its world. Mods rest on it (see Mods), and so does a player who takes a world from a host they don't know. Where it stands (2026-10-05):
  - **Tested, for what Tide alone can say.** `compiler/tests/safety/` is games that try each way in: an index anywhere in a list, a grid, a map or text, sizes nothing can have, handles to entities that are gone or never were, ints and floats at their limits, loops whose places are at the ints' edge, a sort whose comparison lies, events and tasks in their thousands, and drawing and widgets given NaN, infinities and indices past what they index. Each ends in a defined way, with what it gave pinned, under AddressSanitizer and UndefinedBehaviorSanitizer on every push (the `sanitize` preset). What they found is fixed, and what's left is below.
  - **What a stack that runs out does is open, and the owner's.** Tide has no limit on how deep calls go, and generated code doesn't check. Today a function that calls itself with no end stops the program as the system stops one whose stack ran out: a crash, with no message of the engine's, or, where the C compiler turned the recursion into a loop, a program that never ends. It's a crash and not something worse on Windows (which checks big frames), on Linux and Android (games are compiled so that a function with locals bigger than a page touches each page), and on the web's own thread (which stops at memory's edge). macOS wasn't checked. It's not known to be one on the web's other threads, whose stacks are memory like any other with nothing beside them to stop at: a deep enough call there writes over what's next to its stack. The choices:
    1. Leave it: a crash where the platform gives one. Nothing to build, and the web's threads stay as they are.
    2. A stop with a message, as an endless chain of events has: generated code checks how much stack is left where a function can call itself (a comparison a call, in those functions alone), and stops naming the function. The same on every platform, the web's threads included, and the only one of the three that closes them.
    3. A limit on depth instead (a count, say 10,000 calls deep): simpler to say, but a count isn't a size, so big locals still run out before it, and it would need 2's check as well.
  - **Bytes from another machine** aren't tested as hostile: a world, a delta or an input a server or a player made up. Sessions check what they read (sizes, counts, a world's hash), and a fuzzer covers the WebRTC stack, but nothing yet feeds a session's own messages broken at random.
  - **A game's C is trusted**, as ever: the promise is about Tide code.
  - **Still to do: programs made at random.** The tests are the ways in someone thought of. A generator of valid Tide programs, run the same way, is what finds the rest.
