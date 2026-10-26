#pragma once

#include "json.h"

// A rename of one of the game's declarations reaching the C# of the Unity project its game is in: the project's own
// scripts (in Assets, and in its embedded and local packages, never Packages/com.tide.game, which tide writes) use
// the C# twins tide unity wrote of the game's types (unity_names.h), so renaming a component, a field, an event, a
// command, a singleton, a struct, an enum or an enum's member renames their references there too.
//
// References are found conservatively, with no C# compiler: a type or a namespace by its name where the file's usings
// or namespace make it the game's (Tide.Game...), and not where a type, a variable or a member of the project's C# has
// its name and the place could be either; a member where what it's a member of is known to be the game's type: a
// variable, parameter, field or lambda parameter declared as it, a pattern's variable, a generic call naming it
// (GetComponent<Health>()), `new`, an object initializer, a cast, `as`, an enum member after its enum, a field,
// property or method of the project's own classes (partial ones across files, static ones by the class's name) whose
// type is it, and on through fields of the game's types, lists and indexers. Text in strings and comments is never
// touched, but for a doc comment's `cref`; code in an interpolated string's holes is code.

typedef enum csharp_what {
    CSHARP_TYPE,   // A type or an enum: `type` renamed
    CSHARP_FIELD,  // One of `type`'s fields (its local ones too), `name`
    CSHARP_MEMBER, // One of the enum `type`'s members, `name`
    CSHARP_NAMESPACE, // The namespace `type` (its whole dotted name): every type in it moves with it
} csharp_what;

// Appends the edits of the project's C# for renaming `type` (its Tide name, qualified) or its field or member `name`
// to `new_name` (a Tide name) to `out` as WorkspaceEdit "changes" entries, a file's URI and its edits each, counting
// the files in *files_written. Nothing where the project's Packages/com.tide.game has no schema.json, or the rename
// changes no C# name.
void csharp_renames(const char *project, csharp_what what, const char *type, const char *name, const char *new_name, jbuf *out,
                    int *files_written);
