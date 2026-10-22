#pragma once

#include "ast.h"

// What tide needs of the game's settings to build it, beyond its C.
typedef struct game_info {
    char title[256];  // The title setting, or "" without one
    char app_id[256]; // appId, likewise
    char version[64]; // version, likewise
    bool bold;        // Its C names the bold weight's file (it says Font.Bold), which is in the platform layer's library
    bool microphone;  // It says Clip.Microphone: its Android app's manifest asks to record audio
    bool assets_affect_match; // Gameplay assets contribute to game identity; edits require a build, not only a new pack
    char simulation[65]; // Generated compatibility identity, as lowercase SHA-256.
    // The files and folders its assets name (asset_files.h), for whoever
    // watches them: malloc'd, each path malloc'd, for the caller to free.
    char **asset_paths;
    int asset_count;
} game_info;

typedef struct codegen_options {
    const char *name;        // Base name of the generated files.
    const char *out_dir;
    bool line_directives;    // Map generated code back to .tide lines for debuggers.
    bool layout;             // Describe the data layout as tide_game_layout (tide/layout.h), for hot reloading.
    bool embed;              // ...and tide_game_embed (tide/embed_game.h), for another program to run it (tide/embed.h): implies layout.
    bool strip_unused_assets; // Closed release games only: exclude assets never referenced by checked Tide code/defaults/shaders.
    bool server; // Preserve the client compatibility identity; emit server build metadata.
    bool strip_server_assets; // Explicit closed-world promise for the simulation asset closure.
    sb *externs;             // If set, gets the C function of each extern function, a line each (for tide's checks).
    game_info *info;         // If set, gets what tide needs of the game's settings.
    bool shaders;            // Instead of the schedule, print the game's shaders (tidec --shaders).
    bool outline;            // ...or what it declares, as JSON (tidec --outline: print_outline).
    sb *outline_to;          // If set, gets that JSON too as the game generates, once it's checked.
    const char *spirv_dir;   // ...or write each one's SPIR-V into this folder, as <n>.spv (tidec --spirv).
    const char *hlsl_dir;    // ...or its HLSL, as <n>.hlsl (tidec --hlsl: hlsl.h).
    // If set, called with each of the game's shaders, its name and WGSL, once it's checked (tide unity's HLSL).
    void (*shader_out)(void *user, const char *name, const char *wgsl);
    void *shader_user;
    const char *game_folder; // Where the game's .tide/ is, for its asset hashes and fetched URLs; NULL where nothing says
    const char *pack;        // Where to write the pack of the game's assets (tide/pack.h), or NULL
    const char *pack_c;      // Where to write the pack as a C array (tide_game_pack), for hosts that embed it, or NULL
    const char *const *defines; // The symbols the build defines for every file's #if (`--define FAST`, DEBUG), or NULL
    int define_count;
    const uint8_t *external_identity; // Optional SHA-256 of game C/headers/libraries, independent of target.
    const char *editor_prefab; // Internal native-editor build: qualified prefab to instantiate without gameplay startup.
    bool editor_local;
    const char *editor_source, *editor_text; // Unsaved source overlay, retaining the original path and asset scope.
} codegen_options;

// Writes <out_dir>/<name>.h and <out_dir>/<name>.c. Returns false on I/O errors.
bool codegen(program *prog, const codegen_options *opts);
