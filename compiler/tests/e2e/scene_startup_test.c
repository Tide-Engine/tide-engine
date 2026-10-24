#include "game.h"
#include "tide_test.h"

TIDE_TEST(scene_startup_authored_root_and_normal_lifecycle)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f / 30.0f);
    TIDE_CHECK(w.Time.tickRate == 30);
    TIDE_CHECK(tide_world_entity_count(&w) == 2);
    TIDE_CHECK(w.Probe.order == 12 && w.Probe.ready == 1);
    const tide_entity root = w.Probe.root;
    TIDE_CHECK(tide_get_Number(&w, root).value == 1);
    const tide_entity child = tide_get_Root(&w, root).child;
    TIDE_CHECK(tide_get_Number(&w, child).value == 2);
    tide_devices devices = {0};
    tide_button_set(&devices.keyboard.space, true);
    const tide_input input = tide_input_sample(&devices, NULL);
    tide_world_set_server_input(&w, input);
    tide_world_tick(&w);
    TIDE_CHECK(w.Probe.steps == 1 && w.Probe.input == 1);
    TIDE_CHECK(tide_get_Number(&w, root).value == 2);
    TIDE_CHECK(tide_get_Number(&w, child).value == 3);
    tide_world_free(&w);
}

TIDE_TEST(scene_startup_unmarked_views_and_session_default)
{
    tide_world w = {0}, restarted = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&w, 1.0f / 30.0f);
    tide_local_init(&local);
    local.FrameProbe.request = 1;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.FrameProbe.frames == 1 && local.FrameProbe.entities == 2);
    TIDE_CHECK(draw.count > 0);
    TIDE_REQUIRE(draw.place.camera_2d > 0);
    TIDE_CHECK(draw.cameras[draw.place.camera_2d - 1].center.x == 4);
    TIDE_CHECK(draw.cameras[draw.place.camera_2d - 1].center.y == 5);
    tide_session_request request = {0};
    tide_start start = {0};
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START && start.scene >= 0);
    tide_world_start(&restarted, 1.0f / 30.0f, &start);
    TIDE_CHECK(tide_world_entity_count(&restarted) == 1);
    local.FrameProbe.request = 2;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START && start.scene == -1);
    tide_world_start(&restarted, 1.0f / 30.0f, &start);
    TIDE_CHECK(tide_world_entity_count(&restarted) == 2);
    TIDE_CHECK(restarted.Probe.order == 12 && restarted.Probe.ready == 1);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&restarted);
    tide_world_free(&w);
}

TIDE_TEST(scene_startup_empty_scene_fallback_reloads_authored_tree)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f / 30.0f);
    const tide_entity old = w.Probe.root;
    w.Probe.unload = true;
    tide_world_tick(&w);
    TIDE_CHECK(!tide_entity_alive(&w.entities, old));
    TIDE_CHECK(tide_world_entity_count(&w) == 2);
    TIDE_CHECK(w.Probe.ready == 2 && w.Probe.order == 1212);
    TIDE_CHECK(!tide_world_ended(&w));
    tide_world_free(&w);
}
