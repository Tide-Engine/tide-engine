#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_local local;
static tide_gui gui;
static tide_draw_list draw;
static tide_devices devices;

static void frame(void)
{
    tide_draw_reset(&draw);
    draw.screen_scale = tide_f2(2, 2);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_frame(NULL, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void start(void)
{
    tide_draw_free(&gui.list);
    tide_draw_free(&draw);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_init(&local);
    frame();
    frame();
}

static const tide_draw_command *text(const char *value, uint32_t *index)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(draw.text + c->text, value) == 0) {
            if (index) *index = i;
            return c;
        }
    }
    return NULL;
}

TIDE_TEST(gui_custom_content_joins_layout_in_order_and_helpers_share_its_list)
{
    start();
    uint32_t world_at, item_at, stock_at;
    const tide_draw_command *world = text("world", &world_at);
    const tide_draw_command *item = text("item 0", &item_at);
    const tide_draw_command *stock = text("stock", &stock_at);
    TIDE_REQUIRE(world && item && stock);
    TIDE_CHECK(world_at < item_at && item_at < stock_at);
    TIDE_CHECK(item->a.x > 250 && item->a.x < 450);
    TIDE_CHECK(item->a.y > 180 && item->a.y < 400);
    TIDE_CHECK(local.Menu.frames == 2); // Layout never replays game code
    TIDE_CHECK(tide_test_color_is(stock->color, 0.93f, 0.93f, 0.95f, 1));
    TIDE_CHECK(tide_test_color_is(world->color, 1, 1, 1, 1));
}

TIDE_TEST(gui_custom_keyed_controls_keep_focus_when_rows_reorder_and_disappear)
{
    start();
    TIDE_REQUIRE(gui.nav_last_count == 5);
    const uint32_t selected = gui.nav_last[3]; // Key 2 moves from the last row to the first
    gui.focus = selected;
    local.Menu.reverse = true;
    local.Menu.omitFirst = true;
    frame();
    TIDE_CHECK(gui.focus == selected);
    TIDE_REQUIRE(gui.nav_last_count == 4);
    TIDE_CHECK(gui.nav_last[1] == selected);
    tide_button_set(&devices.keyboard.enter, true);
    frame();
    TIDE_CHECK(local.Menu.picked == 2);
}

TIDE_TEST(gui_custom_pointer_capture_follows_a_reordered_key)
{
    start();
    const tide_draw_command *item = text("item 2", NULL);
    TIDE_REQUIRE(item);
    devices.mouse.position = tide_f2(item->a.x + 4, 600 - item->a.y - 4);
    tide_button_set(&devices.mouse.left, true);
    frame();
    const uint32_t active = gui.active;
    TIDE_REQUIRE(active != 0);
    local.Menu.reverse = true;
    local.Menu.omitFirst = true;
    frame();
    TIDE_CHECK(gui.active == active);
    item = text("item 2", NULL);
    TIDE_REQUIRE(item);
    devices.mouse.position = tide_f2(item->a.x + 4, 600 - item->a.y - 4);
    tide_button_set(&devices.mouse.left, false);
    frame();
    TIDE_CHECK(local.Menu.picked == 2 && gui.active == 0);
}

TIDE_TEST(gui_custom_early_return_restores_both_draw_lists_and_identity_scopes)
{
    start();
    local.Menu.leave = true;
    frame();
    TIDE_CHECK(gui.depth == 0 && gui.key == 0);
    TIDE_CHECK(draw.saved_count == 0 && gui.list.saved_count == 0);
    TIDE_CHECK(!draw.place.tinted && !gui.list.place.tinted);
    TIDE_CHECK(text("world", NULL) == NULL);
    local.Menu.leave = false;
    frame();
    TIDE_CHECK(text("stock", NULL) != NULL && text("world", NULL) != NULL);
}

TIDE_TEST(gui_custom_shader_content_tracks_anchored_placement_and_host_scale)
{
    start();
    local.Menu.shader = true;
    frame(); // The area's height changes, so this frame must reposition its shader too
    const tide_draw_command *label = text("shader", NULL);
    TIDE_REQUIRE(label && gui.list.run_count == 1);
    const tide_draw_run *run = &gui.list.runs[0];
    TIDE_REQUIRE(run->shader && run->shader->dims == 2 && run->moved);
    TIDE_CHECK(run->transform.offset.x == label->a.x && run->transform.offset.y == label->a.y);
    TIDE_CHECK(gui.list.screen_scale.x == 2 && gui.list.screen_scale.y == 2);
    TIDE_CHECK(tide_test_color_is(run->tint, 1, 1, 1, 1));
}
