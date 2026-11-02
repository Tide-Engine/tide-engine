#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void material_drawn(const float expected)
{
    unsigned found = 0;
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *run = &draw.runs[i];
        if (!run->shader || strcmp(run->shader->name, "MaterialTint on Corner")) continue;
        TIDE_CHECK(run->count == 1 && tide_shader_instance_size(run->shader) >= sizeof(float));
        float captured;
        memcpy(&captured, draw.instances + run->first, sizeof captured);
        TIDE_CHECK(fabsf(captured - expected) < 0.000001f);
        found++;
    }
    TIDE_CHECK(found == 1);
}

static void frame(const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640.0f, 360.0f), NULL);
    tide_frame(&now, previous, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// Each tick moves x by 4, y by 1, dir.z by 8 and pair.b by 2; on the second,
// `since` jumps to 50 alone and the Hops entity to x = 1000 whole.
TIDE_TEST(view_reads_some_lanes_see_what_a_whole_read_sees)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now);

    tide_world_copy(&before, &now);
    tide_world_tick(&now);
    const uint64_t now_hash = tide_world_hash(&now);
    const uint64_t before_hash = tide_world_hash(&before);

    frame(&before, 0.25f);
    const Seen *s = &local.Seen;
    TIDE_CHECK(s->x == 5.0f && s->wholeX == 5.0f);
    TIDE_CHECK(s->dirZ == 10.0f && s->wholeDirZ == 10.0f);
    TIDE_CHECK(s->pairB == 2.5f && s->wholePairB == 2.5f);
    TIDE_CHECK(s->helperY == 2.5f && s->wholeY == 1.25f); // Through a helper handed it whole
    TIDE_CHECK(s->since == 50.0f && s->wholeSince == 50.0f); // Jumped alone: as it is
    TIDE_CHECK(s->radius == 3.0f); // [Snap]: as it is
    TIDE_CHECK(s->queryRows == 1 && s->queryX == 5.0f && s->queryPairB == 2.5f);
    TIDE_CHECK(s->hopX == 1000.0f); // Jumped whole: as it is
    TIDE_CHECK(s->localX == 5.0f && s->marks == 1 && s->spotX == 0.5f);
    TIDE_CHECK(local.Counts.kept == 7 && local.Counts.counted == 1); // The list as the call began, not with what it added
    // Eight parameters and one more for its frag: at (3, 4) then (4, 6) and (5, 8), a quarter of the way
    TIDE_CHECK(local.Figured.drawn == 1 && local.Figured.moving && local.Figured.atX == 4.25f && local.Figured.atY == 6.5f);
    TIDE_CHECK(fabsf(local.MaterialSeen.brightness - 0.45f) < 0.000001f && local.MaterialSeen.marks == 1);
    material_drawn(0.45f);
    TIDE_CHECK(tide_world_hash(&now) == now_hash && tide_world_hash(&before) == before_hash);

    frame(&before, 0.5f);
    TIDE_CHECK(s->x == 6.0f && s->wholeX == 6.0f && s->queryX == 6.0f && s->localX == 6.0f);
    TIDE_CHECK(s->marks == 2 && s->spotX == 1.0f); // The entity's local fields, kept from the frame before
    TIDE_CHECK(local.Counts.kept == 7 && local.Counts.counted == 2);
    TIDE_CHECK(fabsf(local.MaterialSeen.brightness - 0.5f) < 0.000001f && local.MaterialSeen.marks == 2);
    material_drawn(0.5f);
    TIDE_CHECK(tide_world_hash(&now) == now_hash && tide_world_hash(&before) == before_hash);

    frame(NULL, 1.0f); // No tick before: the latest as it is
    TIDE_CHECK(s->x == 8.0f && s->wholeX == 8.0f && s->queryX == 8.0f && s->pairB == 4.0f && s->helperY == 4.0f);
    TIDE_CHECK(fabsf(local.MaterialSeen.brightness - 0.6f) < 0.000001f && local.MaterialSeen.marks == 3);
    material_drawn(0.6f);
    TIDE_CHECK(tide_world_hash(&now) == now_hash && tide_world_hash(&before) == before_hash);

    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&now);
    tide_draw_free(&draw);
}
