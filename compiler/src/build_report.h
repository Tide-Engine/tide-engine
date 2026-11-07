#pragma once

#include <stdbool.h>
#include <stdint.h>

// What a build did and made, for `tide build --report <file>`: where its time went, what its pack holds and how
// big each part is, and what the build left out. tide keeps one while a build runs (build_report_begin); the
// compiler and tide add to it as they go, and nothing is recorded while none is kept. Its text is malloc'd, so it
// outlives the compiler's arena. See notes/cli-and-packages.md, Build reports.

typedef struct build_report_time {
    char *name;
    double ms;
} build_report_time;

typedef struct build_report_asset {
    char *name;       // Its qualified name
    char *type;       // Its type, as code writes it
    char **files;     // The files it was read from: absolute paths, or what a URL was saved as
    int file_count;
    double import_ms; // Reading and decoding its files as the game was checked (0 for what build code made)
    // What it added to the pack: content an asset before it put there already is shared, not counted again
    uint64_t pack_bytes;
    const char *left_out; // NULL when it's in the pack; else why it isn't (a string literal)
} build_report_asset;

typedef struct build_report_compile {
    char *source;
    double ms;
    bool engine;
} build_report_compile;

typedef struct build_report_file {
    char *path;
    int64_t bytes;
} build_report_file;

typedef struct build_report {
    double started, stage_started;
    char *stage;                        // The step running now, ended by the next one (build_report_stage)
    build_report_time *stages;          // tide's steps, in order
    int stage_count;
    build_report_time *phases;          // The compiler's, within "compiling Tide code and assets", by name
    int phase_count;
    build_report_asset *assets;
    int asset_count;
    uint64_t pack_size;                 // The pack's whole size: its assets, and its table of names
    build_report_compile *compiles;     // The C and C++ compiled, in the order they finished
    int compile_count;
    int reused;                         // ...and how many objects were already up to date
    build_report_file *outputs;
    int output_count;
    char **cuts;                        // What the build's mode left out or kept, a line each
    int cut_count;
    double total_ms;
} build_report;

// The report being kept, or NULL.
build_report *build_report_current(void);
void build_report_begin(void);
// Ends the step running now and starts `name` (NULL to end the last).
void build_report_stage(const char *name);
void build_report_phase(const char *name, double ms);
void build_report_compiled(const char *source, double ms, bool engine);
void build_report_output(const char *path, int64_t bytes);
void build_report_cut(const char *line);
// The pack the build wrote, replacing what an earlier one in the same build said (a server packs twice).
void build_report_assets_clear(void);
build_report_asset *build_report_add_asset(const char *name, const char *type);
void build_report_asset_file(build_report_asset *a, const char *path);
typedef struct build_report_about {
    const char *version; // tide's
    const char *game;    // Its name
    const char *folder;  // Its folder, absolute, which a table's paths are from
    const char *mode;    // debug, release or distribution
    const char *target;
    bool succeeded;
} build_report_about;

// Ends the report: `json` is its text as JSON, or else as a table for reading. malloc'd.
char *build_report_end(const build_report_about *about, bool json);
double build_report_clock_ms(void);
