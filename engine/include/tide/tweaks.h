#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A dev build's tweaks (`tweak float KickUp = 0.00012;`), which .tweaks files give other values than the code does:
// what a host needs to take the values tide run works out again as a file changes, to show them and to write one
// back. tidec makes the table (tide_game_tweaks(), NULL for a build without dev or with no tweaks); the values are a
// struct of the game's, TideTweakValues, of which each tweak says where its value is.

typedef enum tide_tweak_kind {
    TIDE_TWEAK_BOOL,   // bool
    TIDE_TWEAK_INT,    // int32_t: int, int2, int3, int4
    TIDE_TWEAK_LONG,   // int64_t
    TIDE_TWEAK_BYTE,   // uint8_t
    TIDE_TWEAK_FLOAT,  // float: float, float2, float3, float4, Color (r, g, b, a)
    TIDE_TWEAK_DOUBLE, // double: double and its vectors
    TIDE_TWEAK_ENUM,   // An enum's value, of `size` bytes: one of `members`
} tide_tweak_kind;

typedef struct tide_tweak {
    const char *name;              // Its qualified name, as a .tweaks file names it: "KickUp", "Gun.kick"
    uint32_t kind;                 // tide_tweak_kind
    uint32_t count;                // Its components: 1, or a vector's 2 to 4
    uint32_t offset;               // Where in the values it is
    uint32_t size;                 // A component's bytes: an enum's are 1, 2 or 4
    bool color;                    // A Color: four floats
    const char *const *members;    // An enum's members' names, their values, and how many
    const int32_t *member_values;
    uint32_t member_count;
    const char *type;              // Its type as a value names it there: "float3", "Color", "Combat.Pace"
    const char *file;              // The .tweaks file its value is written to, absolute
    bool plain;                    // Its value is written plainly there, so it can be written over: not worked out
} tide_tweak;

// What tidec writes the values it works out to, before them: so a host takes only values of its build's layout
#define TIDE_TWEAKS_MAGIC "TIDETWK1"

typedef struct tide_tweaks_api {
    const tide_tweak *tweaks;
    uint32_t count;
    uint32_t size;      // sizeof the values
    uint64_t layout;    // Which values these are: the tweaks' names, types and places, hashed
    // The file tidec writes the values it worked out to (absolute), which tide run makes again as a .tweaks file
    // changes, without building the game's C; "" where there's none to read
    const char *values;
    // This machine's copy of the values (local state's TideTweaksHere), which local code reads
    void *(*here)(void *local);
    // Sends this machine's copy to the match, whose code reads it once the server applied it (TideSetTweaks)
    void (*send)(void *local);
} tide_tweaks_api;

// The values file's bytes (TIDE_TWEAKS_MAGIC, the layout, the size, then the values) into `values`: false, with
// nothing changed, when they're another build's or cut short.
bool tide_tweaks_load(const tide_tweaks_api *api, void *values, const void *bytes, size_t length);

// One tweak's value as a .tweaks file writes it: `0.25`, `true`, `float3(1, 0, 2)`, `Color(1, 0.5, 0, 1)`,
// `Combat.Pace.Fast`. Writes up to `capacity` bytes and a NUL; returns the whole text's length.
size_t tide_tweaks_format(const tide_tweak *t, const void *values, char *out, size_t capacity);

// Writes one tweak's value into its file: over the value on the line that sets it, keeping the rest of the line and
// of the file as it is, or as a line of its own at the end (the file made if it isn't there). False when it can't.
bool tide_tweaks_save(const tide_tweak *t, const void *values);

// The same for a value already written as text: `name = value;` in `file`. What tide run --web does for the menu of
// a page, which has no files of this machine to write.
bool tide_tweaks_write(const char *file, const char *name, const char *value);
