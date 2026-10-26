#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "json.h"

// The names a game's types have in C#, as tide unity writes them into a Unity project's Packages/com.tide.game
// (compiler/cli/unity_code.c) from the game's schema: shared with tidels, whose renames reach the project's C# by
// them, so both name every type, field and enum member alike.
//
// - A type is in `Tide.Game`, its Tide namespace's parts after it (`Combat.Health` is `Tide.Game.Combat.Health`), its
//   name an identifier C# takes (what C# can't hold as `_`, `?` as `Optional`, a C# keyword with `@`). A local part
//   (`Ball local`) is its component's name with `Local` after. Two of one C# name in one namespace: the later one in
//   the schema has `_` after, as many times as it takes.
// - A field's name is the first of its identifier, with `_` after, that its type's names haven't taken: the type's own
//   name, and for a class (a component, singleton, event or command) or a struct of Unity code's (`members`), the
//   members of those (Value, TideName, ReadLive, Pack, From, Tick, Verified, Target, TargetEntity). A class has its
//   local part's fields after its own, named among them (`local_ids`).
// - An enum's member is its identifier; a second of one identifier is left out.

typedef struct unity_cs_type {
    const char *name; // The schema's: "Ball", "Combat.Health", "Ball local"
    const char *kind; // ...and its kind: "component", "singleton", "event", "command", "struct", "local"
    char *bare;       // Its name without " local"
    char *ns;         // Its C# namespace after Tide.Game, NULL for none ("Combat")
    char *cs;         // Its C# name
    // Which of the package's classes it is (TideComponent, TideSingleton, TideEvent, TideCommand), or NULL; a struct
    // Unity code has as one of its own (`live`); an authored match component (`component`)
    const char *base;
    bool live, component, scene;
    bool members;          // Its fields keep clear of a class's or a C# struct's members: base or live
    int field_count;
    const char **fields;   // Its fields' Tide names, in the schema's order
    char **ids;            // ...their C# names
    int local;             // Its local part's type, or -1
    char **local_ids;      // ...whose fields its class names after its own (that part's field_count of them), or NULL
} unity_cs_type;

typedef struct unity_cs_enum {
    const char *name;
    char *ns, *cs;
    int member_count;
    const char **members; // Tide's names
    char **ids;           // C#'s, NULL for one left out as a second of its identifier
} unity_cs_enum;

typedef struct unity_cs_names {
    unity_cs_type *types;
    int type_count;
    unity_cs_enum *enums;
    int enum_count;
} unity_cs_names;

// Reads the types' and enums' Tide names from a schema (its "types" and "enums"), pointing into its tree, and names
// them (unity_cs_assign). False if it has no types.
bool unity_cs_read(const json *schema, unity_cs_names *out);

// Names every type, field and member again from their Tide names (`name`, `fields`, `members`), which a caller may
// have changed: what a rename makes of them.
void unity_cs_assign(unity_cs_names *n);

void unity_cs_free(unity_cs_names *n);

// A name C# takes, as unity_code.c writes it. malloc'd.
char *unity_cs_identifier(const char *name, size_t n);

// The C# namespace of a dotted Tide one, each part an identifier; NULL for none. malloc'd.
char *unity_cs_namespace(const char *name, size_t n);

// A type's or enum's C# name after `Tide.Game.` ("Combat.Health"). malloc'd.
char *unity_cs_full(const char *ns, const char *cs);
