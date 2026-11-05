#include "game.h"
#include "tide_test.h"
#include "tide/pack.h"
#include "tide/preparation.h"
#ifdef TIDE_WORKER_THREADS
#include "tide/platform.h"
#include "tide/time.h"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(scene_preparation_parallel_helpers_read_packed_constants)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world one = {0}, many = {0};
    tide_world_init(&one, 0.1f);
    tide_game_api.start(&many, 0.1f, NULL, 0);
    const tide_jobs *jobs = NULL;
#ifdef TIDE_WORKER_THREADS
    jobs = tide_platform_jobs();
#endif
    tide_preparation *state = NULL;
    while (!tide_preparation_update(&state, &tide_game_api, &many, jobs, NULL)) {
#ifdef TIDE_WORKER_THREADS
        tide_yield();
#endif
    }
    TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
    TIDE_CHECK(tide_list_count(many.State.values) == 8192);
    for (int32_t i = 0; i < 8192; i++) {
        const int32_t coefficients[] = {17, 23, 31};
        int32_t expected = 0;
        for (int32_t j = 0; j < 64; j++) expected += coefficients[(i + j) % 3];
        const int32_t *value = tide_list_read(&many.heap, many.State.values, i, sizeof *value);
        TIDE_CHECK(value && *value == expected);
    }
    tide_world_free(&one);
    tide_world_free(&many);
}
