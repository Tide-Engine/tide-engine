#pragma once

#include "ast.h"

// Built-in functions and constants reached through a type, `Math`, `Draw2D`, `Draw3D`, `Draw`,
// `GUI`, `GUILayout`, `Screen`, `Cursor` or `Frame`: Math.Dot(a, b),
// quaternion.AxisAngle(axis, angle), Draw2D.Circle(...), GUILayout.Button(text),
// Color.red, Screen.width, Cursor.Lock(), Frame.dt.

// Is `name` something that can own static members: Math, Draw2D, Draw3D, Draw (resource operations), GUI, GUILayout,
// Screen, Cursor, Frame or a built-in type?
bool builtin_owner(str name);

// The two frame drawing APIs: Draw2D and Draw3D.
bool builtin_draw_owner(str name);

// The program's Anchor enum, which GUILayout.Area and Draw2D.Text take, its
// Shape struct, which Draw2D.Shape does, its Blend enum, which Draw.Blend
// does, and its Font enum, a text's weight. Call before resolving calls.
void builtins_use(const decl *anchor, const decl *shape, const decl *blend, const decl *font);
void builtins_use_io(const decl *bytes, const decl *paths);
// Its GUITheme and GUIControl structs, which GUI.SetTheme and GUI.Theme take
// and GUI.Control gives, the same way.
void builtins_use_gui(const decl *theme, const decl *control);

// The types Draw2D.Mesh and Draw3D.Mesh take, as the program has them: List<Vertex>,
// List<Vertex3>, List<int>, Grid2<Color> (NULL when the program has no such
// grid, so nothing is one), Filter, and List<float4x4> for Draw3D.Instanced.
// Call before resolving a draw call.
void builtins_use_mesh(const decl *vertices, const decl *vertices3, const decl *indices, const decl *pixels,
                       const decl *filter, const decl *transforms);

// The list type owner.name takes as its argument `index`, when every version
// of it that has one takes the same: what `[a, b, c]` written there is. When
// drawing APIs take different lists, like Draw2D.Mesh's Vertex and Draw3D.Mesh's Vertex3,
// it's the one whose elements `list`'s first one names, as in
// `[Vertex3 { ... }, ...]`.
bool builtin_list_param(str owner, str name, int index, const expr *list, type *out);

// Resolves owner.name(args) for a call whose arguments are already checked.
// Fills e->call, e->c_callee, e->c_pack and e->arg_want, and for the GUI and
// the draw settings e->arg_mut, e->arg_out and e->gui. Reports errors and
// returns TY_ERROR on failure. An argument written `out` goes to the parameter
// in its place, `out` or not: the caller says what's wrong with it.
type resolve_builtin_call(str owner, expr *e);

// Whether a version of owner.name has an `out` parameter, like
// GUILayout.TextField(label, value, out entered): its calls' arguments can say
// `out`.
bool builtin_takes_out(str owner, str name);

// The name of parameter `index` of the function a call was resolved to.
str builtin_param_name(const expr *e, int index);

// Resolves owner.member, such as quaternion.identity. Fills e->c_constant.
type resolve_builtin_member(str owner, expr *e);

// The owner of a built-in function named `name` ("Math" for Sin), or NULL.
const char *builtin_function_owner(str name);

// ---------------------------------------------------------------------------
// For editors (the language server)

typedef struct builtin_member {
    const char *name;
    bool is_function;
    const char *detail;  // "Draw2D.Circle(float2 center, float radius, Color color)", or "Math.PI: float"; with
                         // " (+1 version)" after a function's first version when it has others
    const char *doc;     // One line, or NULL; then a function's other versions, each as code with what it says of its own
    const char *snippet; // "Circle(${1:center}, ...)" for functions, NULL for constants
} builtin_member;

// Calls `visit` for every static member of `owner`, overloads once.
void builtin_list_members(str owner, void (*visit)(void *user, const builtin_member *m), void *user);

// Appends every signature of owner.name, one per line, then its doc. False if unknown.
bool builtin_describe(str owner, str name, sb *out);

// Calls `visit` with each signature of owner.name, like
// "Draw2D.Circle(float2 center, float radius, Color color)", and its doc (or NULL).
void builtin_signatures(str owner, str name, void (*visit)(void *user, const char *label, const char *doc), void *user);

// ---------------------------------------------------------------------------
// Settings: the engine's, which a game sets in `settings { ... }`

typedef struct setting {
    const char *name;
    type_kind kind;        // What it's set to: TY_INT, TY_STRING, TY_BOOL, or TY_ENUM for one of the engine's enums
    const char *otherwise; // What it is when the game doesn't set it, in words: "60"
    const char *doc;
} setting;

// The setting named `name`, or NULL.
const setting *setting_named(str name);

// Every setting, and how many there are.
const setting *settings_list(int *count);
