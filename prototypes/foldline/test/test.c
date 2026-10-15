#include "helpers.h"
#include "core.c"
#include "prop_collision.c"
#include "cube_presentation.c"
#include "place_reception.c"
#include "place_archive.c"
#include "place_mailroom.c"
#include "place_exchange.c"
#include "place_atrium.c"
#include "place_crawlspace.c"
#include "place_weighbridge.c"
#include "place_long_throw.c"
#include "place_vignettes.c"
#include "place_showroom.c"
#include "place_red_stair.c"
#include "campaign.c"
#include "layout_export.c"

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--export-layout") == 0) {
        if (argc != 3) {
            fputs("Usage: foldline_test --export-layout PATH\n", stderr);
            return 1;
        }
        return export_layout(argv[2]);
    }
    int ran = 0;
#define RUN(test) do { if (argc < 2 || strstr(#test, argv[1])) { test(); ran++; } } while (0)
    RUN(ownership_and_movement);
    RUN(folds_are_shot_and_crossed);
    RUN(fold_preview_matches_firing);
    RUN(cube_teleports_without_interpolating);
    RUN(prop_collision_tests);
    RUN(ends_placed_at_building);
    RUN(sizes);
    RUN(carrying_and_throwing);
    RUN(plates_by_mass);
    RUN(lift_rides);
    RUN(restart);
    RUN(pads_throw);
    RUN(lift_stops);
    RUN(replay_and_pack_are_identical);
    RUN(place_reception_tests);
    RUN(place_archive_tests);
    RUN(place_mailroom_tests);
    RUN(place_exchange_tests);
    RUN(place_atrium_tests);
    RUN(place_crawlspace_tests);
    RUN(place_weighbridge_tests);
    RUN(place_long_throw_tests);
    RUN(place_vignettes_tests);
    RUN(place_showroom_tests);
    RUN(place_red_stair_tests);
    RUN(place_ending_tests);
    RUN(campaign_replay);
    CHECK(ran > 0);
#undef RUN
    puts("FOLDLINE simulation checks passed");
    return 0;
}
