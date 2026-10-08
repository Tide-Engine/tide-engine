#pragma once

#include "ast.h"

// Helpers for Tide's built-in types.

// Looks up a built-in type by name: bool, int, float, float3, quaternion, ...
bool builtin_type_named(str name, type *out);

// What a built-in type that takes types accepts for each: the one table the
// parser (what's in the <>, and what a syntax error says of it) and the
// checker (what may go there, and where one can be inside another:
// resolve_generic in check.c) read. List<T>, Map<K, V>, Set<K>, Grid2<T> and
// Grid3<T> take theirs in <>; `T?` and `fails E` take theirs around them.
typedef enum generic_arg {
    GENERIC_ELEMENT, // A list's element or a map's value: any value, containers among them, but a grid (which is never copied)
    GENERIC_KEY,     // A map's or set's key: a value with a hash and ==, of a fixed size (no containers, no text in structs)
    GENERIC_CELL,    // A grid's cell: a plain value, side by side with the others in its chunk (no containers, no text)
    GENERIC_VALUE,   // What a T? holds: any value a function can return
    GENERIC_ERROR,   // What a function fails with: a value, never a T?
} generic_arg;

typedef struct generic_form {
    const char *name;
    int arity;
    generic_arg args[2];
    const char *hints[2]; // What each type in the <> is, for a syntax error
    const char *comma;    // ...what's between them
    const char *close;    // ...and what closes it
} generic_form;

// The form a name takes types in <> with, or NULL: List, Map, Set, Grid2, Grid3.
const generic_form *generic_form_named(str name);

// The forms around a type: T? and `fails E`.
extern const generic_form generic_optional;
extern const generic_form generic_fails;

// Offers every built-in type name to a "did you mean" suggestion.
void suggest_builtin_types(suggestion *s);

// Number of components: 1 for byte, int and float, 2-4 for vectors, 0 for anything else.
int type_dim(type t);

// float or a float vector / byte, int or an int vector.
bool type_is_float_based(type t);
bool type_is_int_based(type t);

// byte, int or float (dim 1) and int/float vectors (dim 2-4). Not long, which has no vectors.
bool type_is_numeric(type t);

// byte, int or long.
bool type_is_integer(type t);

// 2, 3 or 4 for float2x2, float3x3, float4x4; 0 otherwise.
int matrix_dim(type t);

// A type's bytes and alignment in generated C. Generated types have no padding
// the compiler adds: tidec writes it out as members, so a struct's layout is
// its fields' in order, each on its own alignment, and the checker knows a
// value's size as the C compiler will have it. Every built-in type is 4-byte
// aligned with a size that's a multiple of 4, except bool and byte; long is
// 8-byte aligned.
typedef struct layout {
    int size;
    int align;
} layout;

layout type_layout(type t);

// The layout of a declaration's fields together: a struct's, a component's.
// With `pad`, room for an int a field and one more, it's also given the
// padding before each field (pad[i] before field i) and after the last.
layout decl_layout_padded(const decl *d, int *pad);
layout decl_layout(const decl *d);

// Whether views see values of type `t` blended between the last two ticks:
// floats, and what's made of them. A struct blends if any of its fields do.
bool type_blends(type t);

// The int/float scalar or vector with `dim` components.
type vector_type(bool is_float, int dim);
type matrix_type(int dim);

// Can a value of type `from` be stored where `to` is expected? Exact matches,
// plus byte -> int/long/float, int -> long/float and intN -> floatN widening. Errors are always accepted so
// they don't cascade.
bool type_assignable(type to, type from);
// A grid that's a texture where one is wanted: a Grid2<Color>, or a Grid2<float> (one channel).
bool type_is_texture_grid(type t);

// Suffix of the tide/math.h functions for a type: f3, i2, q, f4x4, ...
const char *type_suffix(type t);

// Name for error messages.
const char *type_name(type t);

// C type for generated code.
const char *type_c_name(type t);
