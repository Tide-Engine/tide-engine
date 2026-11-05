#include "compile.h"
#include "tide_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void diagnostic(void *user, diag_severity severity, loc at, const char *message)
{
    (void)at;
    (void)message;
    if (severity == DIAG_ERROR) (*(int *)user)++;
}

static char *generated(void)
{
    FILE *file = fopen(RECOMPILE_WORK "/game.c", "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    if (size < 0) { fclose(file); return NULL; }
    char *text = malloc((size_t)size + 1);
    if (!text) { fclose(file); return NULL; }
    const size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) { free(text); return NULL; }
    text[read] = '\0';
    return text;
}

TIDE_TEST(recompile_keeps_lane_analysis_inside_each_program)
{
    // Both helper memo tables must forget their arena storage. Alternate the
    // fields the same named helpers read/write, with a failed save between them.
    const char *bodies[] = {
        "void Move(mut Body b) { b.x = Math.Min(b.x + 1, 4); } "
        "float Read(Body b) { return b.x; } ",
        "void Move(mut Body b) { b.y = Math.Min(b.y + 2, 8); } "
        "float Read(Body b) { return b.y; } ",
        "void Move(mut Body b) { b.missing = 1; } "
        "float Read(Body b) { return b.missing; } "
    };
    char *expected[2] = {NULL, NULL};
    for (int pass = 0; pass < 18; pass++) {
        arena_reset();
        const int variant = pass % 3;
        sb source = {0};
        sb_put(&source, "component Body { float x; float y; float[16] spare; } ");
        sb_put(&source, bodies[variant]);
        sb_put(&source, "system Tick(mut Body b) { Move(b); } "
            "view Paint(Body b) { Draw.Text($\"{Read(b)}\", float2(0), 16, Color.white); } "
            "scene Main; event(Spawned) Setup(with Main) { Spawn(Body {}); }");
        const compile_input input = {.path = "recompile.tide", .text = source.data};
        const codegen_options options = {.name = "game", .out_dir = RECOMPILE_WORK, .layout = true};
        int errors = 0;
        diag_set_sink(diagnostic, &errors);
        const bool ok = compile_inputs(&input, 1, &options, NULL);
        diag_set_sink(NULL, NULL);
        TIDE_REQUIRE(ok == (variant != 2));
        TIDE_CHECK((errors != 0) == (variant == 2));
        char *text = generated();
        TIDE_REQUIRE(text);
        if (variant == 2) {
            TIDE_CHECK(strcmp(text, expected[1]) == 0); // A failed save keeps the last C.
        } else if (!expected[variant]) {
            expected[variant] = text;
            text = NULL;
        } else {
            TIDE_CHECK(strcmp(text, expected[variant]) == 0);
        }
        free(text);
    }
    TIDE_CHECK(strcmp(expected[0], expected[1]) != 0);
    free(expected[0]);
    free(expected[1]);
    arena_reset();
}

static unsigned progress_seen;

static void progress(const char *stage, const char *detail, const int completed, const int total)
{
    TIDE_CHECK(completed >= 0 && total >= completed);
    if (strcmp(stage, "Parsing Tide") == 0 && detail && strstr(detail, "progress.tide")) {
        progress_seen |= 1;
        TIDE_CHECK(total == 1);
        if (completed == 1) progress_seen |= 32;
    }
    if (strcmp(stage, "Checking Tide") == 0) progress_seen |= 2;
    if (strcmp(stage, "Importing asset") == 0 && detail && strstr(detail, "progress.txt")) {
        progress_seen |= 4;
        TIDE_CHECK(total == 1 || total == 2);
        if (completed == total) progress_seen |= 64;
    }
    if (strcmp(stage, "Importing asset") == 0 && total == 2 && completed == 2) progress_seen |= 128;
    if (strcmp(stage, "Packing asset") == 0 && detail && strcmp(detail, "rules") == 0) {
        progress_seen |= 8;
        TIDE_CHECK(total == 2);
    }
    if (strcmp(stage, "Packing assets") == 0 && total == 2 && completed == 2) progress_seen |= 256;
    if (strcmp(stage, "Generating C") == 0) progress_seen |= 16;
}

TIDE_TEST(compile_progress_identifies_sources_and_assets)
{
    FILE *file = fopen(RECOMPILE_WORK "/progress.txt", "wb");
    TIDE_REQUIRE(file);
    fputs("rules", file);
    fclose(file);
    file = fopen(RECOMPILE_WORK "/progress-other.txt", "wb");
    TIDE_REQUIRE(file);
    fputs("more rules", file);
    fclose(file);
    const compile_input input = {.path = RECOMPILE_WORK "/progress.tide",
        .text = "asset string rules = \"progress.txt\"; asset List<string> allRules = \"progress*.txt\"; scene Main;"};
    const codegen_options options = {.name = "game", .out_dir = RECOMPILE_WORK,
        .pack = RECOMPILE_WORK "/progress.pack"};
    progress_seen = 0;
    compile_set_progress(progress);
    const bool ok = compile_inputs(&input, 1, &options, NULL);
    compile_set_progress(NULL);
    TIDE_CHECK(ok);
    TIDE_CHECK(progress_seen == 511);
    arena_reset();
}
