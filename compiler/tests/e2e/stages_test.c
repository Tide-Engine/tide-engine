#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(stages_systems_follow_phases_offsets_and_declaration_dependencies)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Scheduling_Trace.systems == 123456789);
    tide_world_free(&world);
}

TIDE_TEST(stages_handlers_are_ordered_separately_for_each_event)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Scheduling_Trace.handlers == 12345);
    TIDE_CHECK(world.Scheduling_Trace.otherHandlers == 1);
    tide_world_free(&world);
}

TIDE_TEST(stages_after_a_view_phase_follows_every_entity_and_offset)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Scheduling_FrameTrace.order == 12234);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Scheduling_FrameTrace.order == 12234);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}
