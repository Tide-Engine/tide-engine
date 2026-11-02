---
description: "Write Tide functions that work for many types: type parameters, inferred or written type arguments, and has tests for the fields a type has."
---

# Generic functions

A generic function works for any type it's given. Its type parameters go in `<>` after its name, and its code uses them as types:

```csharp
T Max<T>(T a, T b)
{
    return a > b ? a : b;
}
```

A call says what each type parameter is, in `<>`, or leaves it to the arguments:

```csharp
int bigger = Max(3, 7);           // T is int, from the arguments
float half = Max<float>(2, 1.5);  // T written: 2 converts to a float
```

The arguments have to agree. `Max(1, 2.5)` is an error, because one argument makes `T` an `int` and the other a `float`. Write `Max<float>(1, 2.5)` and both convert. A type parameter that no argument mentions is written in `<>`:

```csharp
U Parse<U>(string text) { ... }

var score = Parse<int>("12");
```

A parameter's type can hold a type parameter, and the argument's type fills it in: `List<T>`, `Map<K, V>`, `Set<T>`, `T[4]`, `T?`.

```csharp
int CountAll<K, V>(Map<K, List<V>> buckets)
{
    mut int total = 0;
    foreach (var entry in buckets) total += entry.value.count;
    return total;
}
```

A type argument is a value type, one a list can hold: numbers, vectors, text, enums, structs, lists, maps and sets. Components, singletons, events and grids aren't.

## One function for each type

Each set of type arguments is a function of its own, checked and compiled once however many calls use it. `Max<int>` and `Max<float>` are two functions, each as fast as one written for its type. Calls the compiler can inline still inline.

The type parameter can be anything when the generic is declared. What the code does with it is checked for each type it's used with. Calling `Max` with a struct that has no `>` is an error at `a > b`, with a note at the call that used that struct:

```
error: operator '>' can't be used with Pair and Pair
    T Max<T>(T a, T b) { return a > b ? a : b; }
                                  ^
  = note: declare it in Pair, like 'bool operator >(Pair a, Pair b)'
  = note: in 'Max<Pair>', which this call instantiates
```

A generic that no code calls isn't checked yet, only read.

## Writing the fields a type has: has

`T has float3 normal` is `true` when type `T` has a field named `normal` of type `float3`, and `false` otherwise. It's worked out as the program is compiled. An `if` whose condition is made of `has` tests keeps only the side that runs, so that side can use a field other types don't have:

```csharp
struct Corner
{
    float3 position;
    float3 normal;
}

V MakeVertex<V>(float3 position, float3 normal)
{
    mut var v = V { };
    v.position = position;
    if (V has float3 normal) v.normal = normal;
    return v;
}

var a = MakeVertex<Corner>(p, n);    // Sets its normal
var b = MakeVertex<Vertex3>(p, n);   // Vertex3 has no normal: that line isn't compiled for it
```

`has` tests can be combined with `!`, `&&` and `||`, and an `else` takes the other case: `if (V has float2 position) ... else ...`. The [Geometry package](../packages/geometry.md) is written this way, so its functions fill in any vertex struct a game declares.

## Sharing a name

A generic function can share its name with ordinary functions, and with other generics that take different parameters. When an ordinary function takes the arguments as well as a generic does, the ordinary function wins, as in C#:

```csharp
string Describe(int value) { return "a number"; }
string Describe<T>(T value) { return "something else"; }

Describe(1);    // "a number"
Describe(1.5);  // "something else"
```

## What isn't there yet

- Generic structs (`struct Pair<T>`) and methods with type parameters of their own.
- A generic function as a `Func` value. Call it in a lambda instead: `(int a, int b) => Max(a, b)`.
- Async generic functions and generic workers.
- Constraints such as C#'s `where T : ...`.
- Editor features specific to generics, such as hovering over a type parameter to see what it's bound to.
