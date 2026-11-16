#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A dev build's tweaks (`tweak float KickUp = 0.00012;`), which tweaks.json gives other values than the code does:
// what a host needs to read the file again while the game runs, to show them and to write them back. tidec makes the
// table (tide_game_tweaks, NULL for a build without dev or with no tweaks); the values are a struct of the game's,
// TideTweakValues, of which each tweak says where its value is.

typedef enum tide_tweak_kind {
    TIDE_TWEAK_BOOL,   // bool
    TIDE_TWEAK_INT,    // int32_t: int, int2, int3, int4
    TIDE_TWEAK_LONG,   // int64_t
    TIDE_TWEAK_BYTE,   // uint8_t
    TIDE_TWEAK_FLOAT,  // float: float, float2, float3, float4, Color (r, g, b, a)
    TIDE_TWEAK_DOUBLE, // double: double and its vectors
    TIDE_TWEAK_ENUM,   // int32_t, one of `members`, by its place
} tide_tweak_kind;

typedef struct tide_tweak {
    const char *name;              // Its qualified name, as tweaks.json names it: "KickUp", "Combat.Pace"
    uint32_t kind;                 // tide_tweak_kind
    uint32_t count;                // Its components: 1, or a vector's 2 to 4
    uint32_t offset;               // Where in the values it is
    uint32_t size;                 // A component's bytes: an enum's are 1, 2 or 4
    bool color;                    // A Color: four floats
    const char *const *members;    // An enum's members' names, their values, and how many
    const int32_t *member_values;
    uint32_t member_count;
} tide_tweak;

typedef struct tide_tweaks_api {
    const tide_tweak *tweaks;
    uint32_t count;
    uint32_t size;      // sizeof the values
    const void *(*code)(void); // The values the code gives, which tweaks.json holds what differs from
    const char *path;   // The tweaks.json the build read or would have, absolute; "" where there's no file to read
    // This machine's copy of the values (local state's TideTweaksHere), which local code reads
    void *(*here)(void *local);
    // Sends this machine's copy to the match, whose code reads it once the server applied it (TideSetTweaks)
    void (*send)(void *local);
} tide_tweaks_api;

// tweaks.json's text as values: the code's, and each the file gives in its place. False, with the values as the code
// gives them, when the text isn't an object of values; a name no tweak has, or a value of the wrong shape, is skipped.
bool tide_tweaks_read(const tide_tweaks_api *api, void *values, const char *text, size_t length);

// The values as tweaks.json's text: those that differ from the code's, one a line. Writes up to `capacity` bytes and
// a NUL, and returns the length the whole text has (more than fits says it was cut short).
size_t tide_tweaks_write(const tide_tweaks_api *api, const void *values, char *out, size_t capacity);
