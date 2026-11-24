---
description: "Write Tide functions, structs and methods that work for many types: type parameters, inferred or written type arguments, where rules, and has tests for the fields a type has."
---

# Generics

A generic works for any type it's given. Functions, structs and a struct's methods can be generic. Their type parameters go in `<>` after their name, and their code uses them as types:

```csharp
T Max<T>(T a, T b)
{
    return a > b ? a : b;
}

struct Pair<T>
{
    T first;
    T second;
}
```

## Generic functions

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

A parameter's type can hold a type parameter, and the argument's type fills it in: `List<T>`, `Map<K, V>`, `Set<T>`, `T[4]`, `T?`, `Mesh<V>`, and a generic struct, `Pair<T>`.

```csharp
int CountAll<K, V>(Map<K, List<V>> buckets)
{
    mut int total = 0;
    foreach (var entry in buckets) total += entry.value.count;
    return total;
}

T Larger<T>(Pair<T> pair) { return pair.first > pair.second ? pair.first : pair.second; }

int best = Larger(Pair<int> { first = 8, second = 3 });   // T is int, from Pair<int>
```

A type argument is a value type, one a list can hold: numbers, vectors, text, enums, structs, lists, maps and sets. Components, singletons, events and grids aren't.

## Generic structs

A generic struct is written with its types wherever it's used: as a field, a local, a parameter, or a value.

```csharp
struct Stack<T>
{
    List<T> items;

    int Count() { return items.count; }
    mut void Push(T item) { items.Add(item); }
    mut T? Pop()
    {
        if (items.count == 0) return null;
        var top = items[items.count - 1];
        items.RemoveAt(items.count - 1);
        return top;
    }
}

component Inventory
{
    Stack<Item> slots;
}

mut var path = Stack<int2> { };
path.Push(int2(3, 4));
```

`Stack<Item>` and `Stack<int2>` are two structs, each laid out for what it holds. A generic struct can hold lists of itself, `struct Node<T> { T value; List<Node<T>> kids; }`, but not itself directly, as no struct can. A component, singleton, event or input can't be generic: what the engine stores and schedules by name is one type. A component holds a generic struct instead, as `Inventory` does.

As text, a value is written with its types: `$"{pair}"` is `Pair<int> { first = 1, second = 2 }`.

## Generic methods

A struct's method can have type parameters of its own, beside its struct's:

```csharp
struct Pair<T>
{
    T first;
    T second;

    Tagged<T, U> Tag<U>(U tag) { return Tagged<T, U> { value = first, tag = tag }; }
}

var tagged = pair.Tag("best");   // U is string
```

A plain struct's methods can be generic too. Only a struct's can: a component's methods are written as a function that takes it.

## where

`where` after a generic's parameters, before its body, says what a type parameter has to be. The call or type that doesn't meet it is the error, rather than code inside the generic:

```csharp
T Max<T>(T a, T b) where T : comparable
{
    return a > b ? a : b;
}

struct Index<K, V> where K : hashable
{
    Map<K, V> values;
}
```

| Rule | What it means | What meets it |
|---|---|---|
| `equatable` | `==` and `!=` take it | numbers, bools, enums, vectors, colors, text, `Entity`, `PlayerID`, and structs with their own `operator ==` |
| `comparable` | `<`, `<=`, `>` and `>=` take it | numbers, enums, and structs with their own `operator <` |
| `hashable` | it can be a map's key or a set's element | `int`, `long`, `byte`, `char`, enums, `Entity`, `LocalEntity`, `PlayerID` and text |

Several rules go in one `where`, `where T : comparable, hashable`, and each parameter has its own: `where K : hashable where V : equatable`.

## One instantiation for each set of types

Each set of type arguments is a function or a struct of its own, checked and compiled once however much code uses it. `Max<int>` and `Max<float>` are two functions, each as fast as one written for its type. Calls the compiler can inline still inline.

What a generic does with its type parameters is checked for each type it's used with. Calling `Max` with a struct that has no `>` (and no `where` to say it must) is an error at `a > b`, with a note at the call that used that struct:

```
error: operator '>' can't be used with Point and Point
    T Max<T>(T a, T b) { return a > b ? a : b; }
                                  ^
  = note: declare it in Point, like 'bool operator >(Point a, Point b)'
  = note: in 'Max<Point>', which this call instantiates
```

A generic that nothing uses is checked too, with its type parameters unknown: a misspelled name, a wrong type or a missing function is said there, so a package's generics are checked before a game calls them. What depends on the type parameters, like `a > b` or a field of `V`, waits for a type to check it with.

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

## A generic as a Func

A generic function can be a `Func`'s value: the `Func`'s types say which instantiation it is.

```csharp
Func<int, int, int> pick = Max;        // Max<int>
Func<float, float, float> pickF = Max; // Max<float>
```

## On the GPU

Frags and verts can call generic functions, and a generic struct can be a mesh's vertex, `List<Corner<float>>`. Each instantiation is written for the GPU as it is for the CPU.

## In editors

Hovering over a generic shows its type parameters and its `where`; over an instantiation, `Pair<int>` or a call of `Max`, it shows the types bound, `int Max<int>(int a, int b)`; over a type parameter, what it's of, its rules and the types the game instantiates it with. Renaming a generic renames it wherever an instantiation is written, and renaming a type parameter renames it through its generic's code. Completion writes a generic struct with its types to fill in, `Pair<T>`.

## What isn't there

- Inferring a type from a lambda's result: `pair.Map((int x) => x * 2)` writes `Map<int>`.
- Async generic functions and generic workers.
- Generic components, singletons, events and inputs.
