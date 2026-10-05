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
- Components, singletons, inputs and other structs can hold structs, and so can locals. System parameters stay components, singletons, the input and `Devices`.
- A struct can't contain itself, even through other structs.

A struct's fields change through whatever holds it, so the signature still says what a system writes: `unit.stats.health -= 5` needs `mut Unit unit`. A local copy changes if it's `mut var`.

## Methods

Structs and components can have methods. Their fields are in scope by name, and so are their other methods. A method only reads the fields, unless it's `mut`:

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

Singletons and inputs have no methods of their own: keep the data and its methods in a struct, and the struct in them.

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

- A parameter is a read-only copy. A `mut` parameter is the caller's variable itself, which the function changes. The caller must be able to write it, so `Heal(unit.stats, 0.5)` needs `mut Unit unit`.
- Parameters are built-in types, structs, components and singletons, and an argument's type matches exactly. Return values are built-in types, structs and components; singleton return values aren't supported.
- A function returns a value on every path, unless it returns `void`.
- A function or method can fail, with `fails` after its parameters, and its callers handle the error where they call (see [Errors](./errors.md)).
- A parameter can have a default value, a call can pass arguments by name, and functions can share a name: see [Default values, names and overloads](#default-values-names-and-overloads).
- A function shares its name with nothing else in its namespace but other functions, and a method never with a field.

Functions see their parameters and `Math`, and methods their fields too, but nothing else. Neither can spawn or change entities: systems do. A function can draw and use the GUI, and then only views, and other functions like it, can call it.

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

Methods take explicit singleton parameters by the same value and `mut` rules. Singletons still have no methods of their own.

A match component or singleton can also have [local fields](../game/local-state.md). An ordinary function can read or write those when the caller passes its actual bound parameter, and can pass that parameter through another helper. A plain copy contains only match fields and has no local fields to pass on. Match code can't call a helper that uses local fields; local code can't call one that writes match fields. A helper can't do both through the same parameter. Methods don't receive local fields, and the existing restrictions on copying or passing grids and rendering resources still apply.

Engine-owned singleton instances, such as `Time` and `Session`, stay read-only when passed to helpers. Passing `Session` grants no session operations. Ordinary helpers also can't read `session.room` or format a whole `Session` as text: pass the room code as a separate `string`.

A function can also be written in C, declared with `extern` and no body: see [Calling C](../c/functions.md).

## Default values, names and overloads

These work as in C#.

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
- The engine's functions take names where their parameters have them: `Draw2D.Circle(center: p, radius: 2, color: Color.red)`, `Session.Connect(address: host, port: 7001)`. Constructors, `Spawn`, `Send` and the like take their arguments in order.

**Overloads.** Functions can share a name, and so can a type's methods, when their parameters' types differ. Names, `mut` and defaults don't tell two apart.

- A call picks by its arguments. An argument's own type wins over one it converts to: `Damage(10)` is `Damage(int)` if there is one, and `Damage(float)` otherwise.
- When two fit equally, the one the call wrote every parameter of wins over one that fills defaults in.
- A call that fits none, or two just as well, is an error that lists them. Write the argument as the type meant, like `float(x)`, or name a parameter only one has.

Systems, views and handlers can share a name too. Nothing calls them, so where one is named, it's written with its parameters' types: see [Order of systems](../game/systems.md#order).

## Order

A `mut` parameter and a `mut` method change something of the caller's, so it matters when they run. Tide runs an expression left to right, on every platform:

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

## Operators

A struct can declare operators, in C#'s form. They compile to plain function calls.

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
- As in C#, `==` and `!=` come in pairs, and so do `<` and `>`, and `<=` and `>=`.
- A compound assignment uses its operator: `total += price` uses `+`.
- Several operators can share a symbol with different types, like `Money * int` and `int * Money`.

Structs are only compared with `==` if they declare it.
