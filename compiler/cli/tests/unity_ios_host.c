// A program standing in for Unity's iOS player (unity_ios_test.mjs): the game tide unity --ios wrote as sources,
// linked in, run through tide/embed.h with nothing held, saying the match's tick and its world's hash as it goes.
// The same lines on every system and compiler: a build that says other ones simulates otherwise.
//
//   unity_ios_host <pack, or -> <folder for its saved data>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/embed.h"

const tide_host_game *tide_host_library(void); // The generated library.c's

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "unity_ios_host <pack, or -> <folder for its saved data>\n");
        return 2;
    }
    uint8_t *pack = NULL;
    size_t pack_size = 0;
    if (strcmp(argv[1], "-") != 0) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) {
            perror(argv[1]);
            return 2;
        }
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        pack = malloc(size > 0 ? (size_t)size : 1u);
        if (!pack || size < 0 || fread(pack, 1, (size_t)size, f) != (size_t)size) {
            fprintf(stderr, "can't read %s\n", argv[1]);
            return 2;
        }
        pack_size = (size_t)size;
        fclose(f);
    }
    if (tide_embed_abi() != TIDE_EMBED_ABI) {
        fprintf(stderr, "the embed says ABI %u, its header %u\n", (unsigned)tide_embed_abi(), (unsigned)TIDE_EMBED_ABI);
        return 1;
    }
    char *arguments[] = {"unity_ios_host", "--seed", "7", "--data-dir", argv[2], NULL};
    tide_embed *e = tide_embed_create(&(tide_embed_desc){.game = tide_host_library(), .pack = pack, .pack_size = pack_size,
                                                         .argc = 5, .argv = arguments, .game_name = "unity_ios_host"});
    if (!e) {
        uint32_t said = 0;
        const uint8_t *log = tide_embed_log(NULL, &said);
        fprintf(stderr, "tide_embed_create gave no embed (%u bytes said)\n", (unsigned)said);
        if (log && said > 8) fprintf(stderr, "%.*s\n", (int)(said - 8u), (const char *)log + 8);
        return 1;
    }
    tide_embed_travel_keep(e, 1.0f); // So the shown world's hash can be asked for
    const tide_embed_input nothing = {0};
    uint32_t last = 0;
    for (int frame = 1; frame <= 240; frame++) {
        tide_embed_frame(e, 1.0f / 60.0f, &nothing, 640.0f, 480.0f, 1.0f);
        if (frame % 60 != 0) continue;
        tide_embed_status status;
        tide_embed_status_of(e, &status);
        // The world's hash is a shown tick's: the match held for a frame that takes no time, then let go on
        tide_embed_travel travel;
        const bool paused = tide_embed_pause(e, true);
        tide_embed_frame(e, 0.0f, &nothing, 640.0f, 480.0f, 1.0f);
        tide_embed_travel_of(e, &travel);
        if (!paused || !travel.hash) {
            fprintf(stderr, "the match wouldn't pause to say its world's hash: %s\n", travel.why);
            return 1;
        }
        tide_embed_pause(e, false);
        uint32_t view = 0;
        tide_embed_view(e, &view);
        printf("frame %d: tick %u, world %016llx, a view packet of %s\n", frame, (unsigned)travel.shown,
               (unsigned long long)travel.hash, view ? "some bytes" : "none");
        last = status.predicted_tick;
    }
    tide_embed_destroy(e);
    free(pack);
    if (!last) {
        fprintf(stderr, "the match ran no tick\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
