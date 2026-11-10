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
    bool fetched_assets; // An asset is a URL's: asked about at each build, and not among asset_paths
    // The files and folders its assets name (asset_files.h), for whoever
    // watches them: malloc'd, each path malloc'd, for the caller to free.
    char **asset_paths;
    int asset_count;
} game_info;

typedef struct codegen_options {
    const char *name;        // Base name of the generated files.
    const char *out_dir;
    bool line_directives;    // Map generated code back to .tide lines for debuggers.
    // With line_directives: write the plumbing, all but the game's own code, as <name>_plumbing.c, for a debug build
    // to compile optimized while the game's code (<name>.c) stays as it was written (plumbing_split.inc).
    bool split_plumbing;
    bool split_code; // Stable generated C partitions, listed in <name>_parts.txt.
    size_t split_code_min_bytes; // Smaller files stay whole (CLI only; tidec splits explicitly).
    int split_code_parts;          // The most parts of the code: a power of two up to 16 (0: 4).
    size_t split_code_part_bytes;  // Fewer parts where they'd have less code than this each (0: always the most).
    bool visualizers;        // Also write <name>.natvis and <name>_lldb.py: how debuggers show the game's values.
    bool layout;             // Describe the data layout as tide_game_layout (tide/layout.h), for hot reloading.
    bool embed;              // ...and tide_game_embed (tide/embed_game.h), for another program to run it (tide/embed.h): implies layout.
    bool strip_unused_assets; // Closed release games only: exclude assets never referenced by checked Tide code/defaults/shaders.
    bool strip_names;        // Omit diagnostic/source shader names; keep a private <name>.names map beside generated C.
    bool no_loop_indexes;    // Every loop gone through plainly, none through an index (notes/indexed-loops.md): for comparisons.
    bool server; // Preserve the client compatibility identity; emit server build metadata.
    bool strip_server_assets; // Explicit closed-world promise for the simulation asset closure.
    sb *externs;             // If set, gets the C function of each extern function, a line each (for tide's checks).
    game_info *info;         // If set, gets what tide needs of the game's settings.
    bool shaders;            // Instead of the schedule, print the game's shaders (tidec --shaders).
    bool outline;            // ...or what it declares, as JSON (tidec --outline: print_outline).
    sb *outline_to;          // If set, gets that JSON too as the game generates, once it's checked.
    const char *spirv_dir;   // ...or write each one's SPIR-V into this folder, as <n>.spv (tidec --spirv).
    const char *hlsl_dir;    // ...or its HLSL, as <n>.hlsl (tidec --hlsl: hlsl.h).
    const char *kernels_out; // ...or its kernels as a C header of the renderer's (tidec --kernels: write_kernels).
    // If set, called with each of the game's shaders, its name and WGSL, once it's checked (tide unity's HLSL).
    void (*shader_out)(void *user, const char *name, const char *wgsl);
    void *shader_user;
    const char *game_folder; // Where the game's .tide/ is, for its asset hashes and fetched URLs; NULL where nothing says
    const char *pack;        // Where to write the pack of the game's assets (tide/pack.h), or NULL
    const char *pack_c;      // Where to write the pack as a C array (tide_game_pack), for hosts that embed it, or NULL
    // If set, called as the pack is built with each asset declaration in it (after pruning): its qualified name, what
    // it is and how its images are kept ("Texture 4096x4096 BC1 [Raw]"), and the bytes of the pack it added. A
    // block two declarations share is counted with the first (tidec --asset-sizes, tide build --asset-sizes).
    void (*asset_size_out)(void *user, const char *name, const char *what, uint64_t bytes);
    void *asset_size_user;
    const char *const *defines; // The symbols the build defines for every file's #if (`--define FAST`, DEBUG), or NULL
    int define_count;
    const uint8_t *external_identity; // Optional SHA-256 of game C/headers/libraries, independent of target.
    const char *scene; // Explicit startup prefab or scene, overriding settings.startScene and Main.
    const char *editor_prefab; // Internal native-editor build: qualified prefab to instantiate without gameplay startup.
    bool editor_local;
    const char *editor_source, *editor_text; // Unsaved source overlay, retaining the original path and asset scope.
    // The game's types alone: its systems, views, handlers, functions, frags and verts are left out before checking,
    // so code that doesn't build yet doesn't keep its types from building (tide unity --declarations)
    bool declarations_only;
    // Build code (build_code.h): how its program is compiled, which a build that has none can't run. The C compiler,
    // as a command (NULL-terminated: {"clang"}, or {"tide", "cc"}), with flags for every compile and link (a target,
    // a sysroot) and for the link alone (its libraries), each NULL-terminated or NULL; and where the engine's
    // headers and sources are, which the program is built with
    const char *const *build_cc;
    const char *const *build_flags;
    const char *const *build_libs;
    const char *build_include;
    const char *build_engine;
    bool build_program; // Instead of the game, the program that runs its build code, its main and all (build_code.c)
} codegen_options;

// Writes <out_dir>/<name>.h and <out_dir>/<name>.c (and <name>_plumbing.c). Returns false on I/O errors.
bool codegen(program *prog, const codegen_options *opts);
