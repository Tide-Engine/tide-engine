#include <string.h>
#include <stdlib.h>

#include "tide/draw.h"
#include "tide/mesh.h"
#include "tide/text.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_draw_list list;

TIDE_TEST(draw_records_in_order)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, TIDE_COLOR_BLACK);
    tide_draw_circle(&list, tide_f2(1.0f, 2.0f), 3.0f, TIDE_COLOR_RED);
    tide_draw_text(&list, "one", tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    tide_draw_text(&list, "two", tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(list.count == 4);
    TIDE_CHECK(list.commands[0].kind == TIDE_DRAW_CLEAR);
    TIDE_CHECK(list.commands[1].kind == TIDE_DRAW_PLAIN && list.commands[1].plain.count == 1);
    TIDE_CHECK(list.plains[0].kind == TIDE_DRAW_CIRCLE && list.plains[0].b.x == 3.0f);
    TIDE_CHECK(strcmp(list.text + list.commands[2].text, "one") == 0);
    TIDE_CHECK(strcmp(list.text + list.commands[3].text, "two") == 0);

    tide_draw_reset(&list);
    TIDE_CHECK(list.count == 0 && list.text_used == 0);
}

// It grows as it needs: nothing is dropped, however much a frame draws.
TIDE_TEST(draw_list_grows)
{
    tide_draw_list grown = {0};
    for (uint32_t i = 0; i < 100000u; i++) {
        tide_draw_rect(&grown, tide_f2((float)i, 0.0f), tide_f2(1.0f, 1.0f), TIDE_COLOR_WHITE);
    }
    static char big[100000];
    memset(big, 'a', sizeof big - 1);
    tide_draw_text(&grown, big, tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    tide_draw_text(&grown, "end", tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    // The rects are one run of plain shapes
    TIDE_REQUIRE(grown.count == 3u && grown.plain_count == 100000u && grown.commands[0].plain.count == 100000u);
    TIDE_CHECK(grown.plains[99999].a.x == 99999.0f);
    TIDE_CHECK(strlen(grown.text + grown.commands[1].text) == sizeof big - 1);
    TIDE_CHECK(strcmp(grown.text + grown.commands[2].text, "end") == 0);

    // Appended, as the GUI's are over the world's, text and all
    tide_draw_list world = {0};
    tide_draw_text(&world, "world", tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    tide_draw_append(&world, &grown);
    TIDE_REQUIRE(world.count == 4u && world.plain_count == 100000u);
    TIDE_CHECK(strcmp(world.text + world.commands[0].text, "world") == 0);
    TIDE_CHECK(world.plains[99999].a.x == 99999.0f);
    TIDE_CHECK(strcmp(world.text + world.commands[3].text, "end") == 0);
    tide_draw_free(&world);
    tide_draw_free(&grown);
    TIDE_CHECK(grown.count == 0 && grown.commands == NULL);
}

// ---------------------------------------------------------------------------
// Meshes

TIDE_TEST(draw_targets_preserve_recording_state_and_append_sources)
{
    tide_draw_list d = {0}, appended = {0};
    tide_draw_to(&d, 17, 8, 6);
    tide_draw_set_clip(&d, (tide_rect){0, 0, 4, 3});
    const uint32_t first_count = d.count;
    tide_draw_to(&d, 17, 8, 6);
    TIDE_CHECK(d.count == first_count);
    tide_draw_to(&d, 18, 4, 3);
    TIDE_CHECK(!d.place.clip);
    tide_draw_to(&d, 17, 8, 6);
    TIDE_CHECK(d.place.clip);
    const uint32_t color = tide_draw_target_texture(&d, 17, 8, 6);
    TIDE_REQUIRE(color && d.textures[color - 1u].space == TIDE_PIXELS_TARGET);
    TIDE_CHECK(d.textures[color - 1u].pixels == NULL && d.textures[color - 1u].capacity == 0);
    static const tide_shader shader = {.dims = 2, .vertex_size = sizeof(tide_vertex), .texture_count = 1};
    tide_draw_to(&d, 18, 4, 3);
    tide_draw_blit(&d, &shader, TIDE_BLEND_REPLACE, NULL, NULL, &color, 0);
    tide_draw_blit(&d, &shader, TIDE_BLEND_REPLACE, NULL, NULL, &color, 0);
    TIDE_REQUIRE(d.run_count == 2 && d.runs[0].count == 1 && d.runs[1].count == 1);
    tide_draw_to(&appended, 19, 2, 2);
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.run_count == 2 && appended.target_count == 3);
    const tide_draw_run *run = &appended.runs[0];
    TIDE_REQUIRE(run->textures[0]);
    const tide_draw_texture *copied = &appended.textures[run->textures[0] - 1u];
    TIDE_CHECK(copied->space == TIDE_PIXELS_TARGET && copied->id == 17 && copied->pixels == NULL);
    TIDE_CHECK(appended.commands[1].kind == TIDE_DRAW_TARGET && appended.commands[1].target == 0);
    tide_draw_reset(&d);
    TIDE_CHECK(d.target_count == 0 && d.target == 0 && !d.place.clip);
    tide_draw_free(&appended);
    tide_draw_free(&d);
}

TIDE_TEST(draw_owned_target_aliases_capture_resize_and_disposal)
{
    tide_draw_list d = {0}, appended = {0};
    const tide_render_target target = tide_render_target_make(8, 6), alias = target;
    const tide_texture_ref color = tide_render_target_color(target);
    tide_draw_render_target(&d, target);
    tide_draw_clear(&d, TIDE_COLOR_RED);
    const uint32_t before = tide_draw_texture_source(&d, color);
    tide_render_target_resize(alias, 8, 6);
    tide_draw_render_target(&d, target);
    const uint32_t resized = tide_draw_texture_source(&d, color);
    TIDE_REQUIRE(d.target_count == 2 && before && resized && before != resized);
    TIDE_CHECK(d.targets[0].version == 1 && d.targets[1].version == 2);
    TIDE_CHECK(d.textures[before - 1u].version == 1 && d.textures[resized - 1u].version == 2);
    tide_render_target_resize(target, 16, 12);
    const uint32_t larger = tide_draw_texture_source(&d, color);
    TIDE_CHECK(d.textures[larger - 1u].target.width == 16.0f && d.textures[larger - 1u].target.height == 12.0f);
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.target_count == 2);
    TIDE_CHECK(appended.targets[0].version == 1 && appended.targets[1].version == 2);
    tide_render_target_dispose(alias);
    tide_render_target_dispose(target);
    TIDE_CHECK(!tide_render_target_live(target.id));
    tide_draw_render_target(&d, target);
    TIDE_REQUIRE(d.target && d.target <= d.target_count);
    TIDE_CHECK(d.targets[d.target - 1u].id == target.id && !tide_target_size_valid(d.targets[d.target - 1u].size));
    tide_draw_render_target(&d, (tide_render_target){0});
    TIDE_CHECK(d.target && d.targets[d.target - 1u].id == TIDE_RENDER_INVALID);
    tide_draw_render_target(&d, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    TIDE_CHECK(d.target == 0);
    tide_draw_to(&d, 33, 0, 4);
    TIDE_CHECK(d.target && d.targets[d.target - 1u].id == 33);
    const uint32_t prior_count = d.count;
    tide_draw_forget(&d);
    TIDE_CHECK(d.commands[prior_count - 1u].kind == TIDE_DRAW_TARGET
               && d.commands[prior_count - 1u].target == UINT32_MAX);
    TIDE_CHECK(d.commands[d.count - 1u].kind == TIDE_DRAW_TARGET && d.commands[d.count - 1u].target == 0);
    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_render_targets_free();
}

// A target's state as another build of the game would have made it: on the
// boundary that leaves its id the bits what it holds goes in.
static tide_render_target_state *foreign_state(void)
{
    void *memory = calloc(1, sizeof(tide_render_target_state) + (size_t)TIDE_RENDER_BITS);
    if (!memory) return NULL;
    tide_render_target_state *s = (tide_render_target_state *)(((uintptr_t)memory + (uintptr_t)TIDE_RENDER_BITS)
                                                               & ~(uintptr_t)TIDE_RENDER_BITS);
    s->memory = memory;
    return s;
}

TIDE_TEST(draw_target_reload_adopts_tombstones_and_recreates_opaque_aliases)
{
    // Another library's registry is a different variable pointing to shared
    // heap state. Retained disposed aliases must move before its owner unloads.
    tide_render_target_state *foreign = foreign_state(), *disposed = foreign_state();
    TIDE_REQUIRE(foreign && disposed);
    void *foreign_memory = foreign->memory, *disposed_memory = disposed->memory;
    *foreign = (tide_render_target_state){.next = disposed, .owner = &foreign, .memory = foreign_memory,
                                         .size = {.width = 12, .height = 9}, .version = 2, .alive = true, .holds = 1};
    *disposed = (tide_render_target_state){.owner = &foreign, .memory = disposed_memory, .version = 1, .holds = 1};
    const tide_render_target live_ref = {.id = TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)foreign | 1u};
    const tide_render_target dead_ref = {.id = TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)disposed | 1u};
    tide_render_target_adopt(dead_ref);
    tide_render_target_adopt(live_ref);
    tide_render_target_adopt(live_ref);
    TIDE_CHECK(foreign == NULL && tide_render_target_live(live_ref.id) && !tide_render_target_live(dead_ref.id));

    // Tokens here are deliberately not pointers; recreation cannot dereference
    // the previous program's address space and must retain shared identity.
    const tide_render_target packed = {.id = TIDE_RENDER_MANAGED | (UINT64_C(123) << 16) | 1u,
                                       .size = {.width = 24, .height = 18, .scale = 2}};
    const tide_render_target first = tide_render_target_recreate(packed), second = tide_render_target_recreate(packed);
    TIDE_CHECK(first.id == second.id && first.size.width == 24.0f && first.size.height == 18.0f && first.size.scale == 2.0f);
    TIDE_CHECK(tide_render_target_recreate((tide_render_target){.id = packed.id}).id == 0);
    tide_render_targets_recreated();
    const tide_render_target another_restore = tide_render_target_recreate(packed);
    TIDE_CHECK(another_restore.id != first.id);
    // One that followed the window follows it again
    const tide_render_target following = {.id = TIDE_RENDER_MANAGED | (UINT64_C(124) << 16) | 1u, .size = {.window = 0.5f}};
    const tide_render_target restored = tide_render_target_recreate(following);
    TIDE_CHECK(restored.id && restored.id != another_restore.id && restored.size.window == 0.5f);
    TIDE_CHECK(tide_render_target_live(restored.id));
    tide_render_targets_recreated();
    tide_render_targets_free();
}

// A target's size is in logical pixels, at the display's scale or one of its
// own, and one made for the screen follows the window: the list records which.
TIDE_TEST(draw_targets_record_their_scale_and_whether_they_follow_the_window)
{
    tide_draw_list d = {0}, appended = {0};
    const tide_render_target card = tide_render_target_make(200.5f, 300.0f);
    const tide_render_target exact = tide_render_target_make_scaled(320.0f, 180.0f, 1.0f);
    const tide_render_target screen = tide_render_target_window(), half = tide_render_target_screen(0.5f);
    TIDE_CHECK(card.size.width == 200.5f && card.size.height == 300.0f && card.size.scale == 0.0f && card.size.window == 0.0f);
    TIDE_CHECK(exact.size.scale == 1.0f && screen.size.window == 1.0f && half.size.window == 0.5f);
    TIDE_CHECK(tide_render_target_live(card.id) && tide_render_target_live(exact.id));
    TIDE_CHECK(tide_render_target_live(screen.id) && tide_render_target_live(half.id));

    tide_draw_render_target(&d, exact);
    TIDE_REQUIRE(d.target == 1 && d.targets[0].size.scale == 1.0f && d.targets[0].size.width == 320.0f);
    tide_draw_render_target(&d, half);
    TIDE_REQUIRE(d.target == 2 && d.targets[1].size.window == 0.5f);
    const uint32_t color = tide_draw_texture_source(&d, tide_render_target_color(screen));
    TIDE_REQUIRE(color && d.textures[color - 1u].space == TIDE_PIXELS_TARGET);
    TIDE_CHECK(d.textures[color - 1u].target.window == 1.0f && d.textures[color - 1u].id == screen.id);
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.target_count == 2);
    TIDE_CHECK(appended.targets[0].size.scale == 1.0f && appended.targets[1].size.window == 0.5f);

    // In logical pixels: a fixed size is its own, and the window's is the window's, times the factor
    const tide_float2 window = tide_f2(1280.0f, 720.0f);
    const tide_float2 fixed = tide_target_size_logical(exact.size, window);
    const tide_float2 followed = tide_target_size_logical(half.size, window);
    TIDE_CHECK(fixed.x == 320.0f && fixed.y == 180.0f && followed.x == 640.0f && followed.y == 360.0f);

    // A window's target keeps the window's size: Resize is refused, and it isn't made again
    tide_render_target_resize(half, 64.0f, 64.0f);
    const tide_render_target after = tide_render_target_resolve(half);
    TIDE_CHECK(after.size.window == 0.5f && tide_render_target_version(half.id) == 1);
    // A fixed one keeps its scale through a Resize
    tide_render_target_resize(exact, 64.0f, 32.0f);
    const tide_render_target resized = tide_render_target_resolve(exact);
    TIDE_CHECK(resized.size.width == 64.0f && resized.size.height == 32.0f && resized.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_version(exact.id) == 2);

    // Sizes there's no image of: nothing, less than nothing, and not a number
    const float nothing = 0.0f;
    TIDE_CHECK(!tide_render_target_live(tide_render_target_make(0.0f, 4.0f).id));
    TIDE_CHECK(!tide_render_target_live(tide_render_target_make(4.0f, nothing / nothing).id));
    TIDE_CHECK(!tide_render_target_live(tide_render_target_make_scaled(4.0f, 4.0f, 0.0f).id));
    TIDE_CHECK(!tide_render_target_live(tide_render_target_make_scaled(4.0f, 4.0f, -1.0f).id));
    TIDE_CHECK(!tide_render_target_live(tide_render_target_screen(0.0f).id));
    TIDE_CHECK(!tide_render_target_live(tide_render_target_screen(-2.0f).id));
    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_render_targets_free();
}

// What a target holds is said when it's made, and its id says it from then on:
// to a list, to a renderer, and to another build that makes the target again.
// Its depth is a texture of a kind of its own, beside its color's.
TIDE_TEST(draw_targets_say_what_they_hold_and_lend_their_depth)
{
    tide_draw_list d = {0}, appended = {0};
    const tide_render_target scene = tide_render_target_make(64.0f, 48.0f);
    const tide_render_target shadow = tide_render_target_make_as(64.0f, 48.0f, TIDE_TARGET_DEPTH);
    const tide_render_target exact = tide_render_target_make_scaled_as(256.0f, 256.0f, 1.0f, TIDE_TARGET_DEPTH);
    const tide_render_target screen = tide_render_target_window_as(TIDE_TARGET_DEPTH);
    const tide_render_target half = tide_render_target_screen_as(0.5f, TIDE_TARGET_DEPTH);
    TIDE_CHECK(tide_render_target_format(scene.id) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_format(shadow.id) == TIDE_TARGET_DEPTH && tide_render_target_format(exact.id) == TIDE_TARGET_DEPTH);
    TIDE_CHECK(tide_render_target_format(screen.id) == TIDE_TARGET_DEPTH && tide_render_target_format(half.id) == TIDE_TARGET_DEPTH);
    TIDE_CHECK(exact.size.scale == 1.0f && screen.size.window == 1.0f && half.size.window == 0.5f);
    // The same four with color, by name and by the ones there always were
    TIDE_CHECK(tide_render_target_format(tide_render_target_make_as(4.0f, 4.0f, TIDE_TARGET_RGBA8).id) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_format(tide_render_target_window().id) == TIDE_TARGET_RGBA8);
    // The window, a target that's none, a borrowed one, and a format there's none of, hold color
    TIDE_CHECK(tide_render_target_format(TIDE_RENDER_WINDOW) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_format(TIDE_RENDER_INVALID) == TIDE_TARGET_RGBA8 && tide_render_target_format(0) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_format(1001) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(tide_render_target_format(tide_render_target_make_as(4.0f, 4.0f, (tide_target_format)9).id) == TIDE_TARGET_RGBA8);

    // Its state is found whatever it holds: it's live, resized and disposed like any other
    TIDE_CHECK(tide_render_target_live(shadow.id) && tide_render_target_version(shadow.id) == 1);
    tide_render_target_resize(shadow, 32.0f, 24.0f);
    const tide_render_target resized = tide_render_target_resolve(shadow);
    TIDE_CHECK(resized.id == shadow.id && resized.size.width == 32.0f && tide_render_target_version(shadow.id) == 2);
    TIDE_CHECK(tide_render_target_format(resized.id) == TIDE_TARGET_DEPTH);

    // A target's depth and its color are two textures of the list's, each found again
    const tide_depth_ref depth = tide_render_target_depth(scene);
    TIDE_CHECK(depth.target.id == scene.id && depth.target.size.width == 64.0f);
    const uint32_t color_slot = tide_draw_texture_source(&d, tide_render_target_color(scene));
    const uint32_t depth_slot = tide_draw_depth_source(&d, depth);
    TIDE_REQUIRE(color_slot && depth_slot && color_slot != depth_slot && d.texture_count == 2);
    TIDE_CHECK(d.textures[color_slot - 1u].space == TIDE_PIXELS_TARGET && d.textures[depth_slot - 1u].space == TIDE_PIXELS_TARGET_DEPTH);
    TIDE_CHECK(d.textures[depth_slot - 1u].id == scene.id && d.textures[depth_slot - 1u].pixels == NULL);
    TIDE_CHECK(d.textures[depth_slot - 1u].target.width == 64.0f && d.textures[depth_slot - 1u].target.height == 48.0f);
    TIDE_CHECK(tide_draw_depth_source(&d, depth) == depth_slot && tide_draw_target_depth(&d, scene.id, 64.0f, 48.0f) == depth_slot);
    TIDE_CHECK(tide_draw_texture_source(&d, tide_render_target_color(scene)) == color_slot && d.texture_count == 2);
    // The window has none to lend, and a depth that's no target's is no texture
    TIDE_CHECK(tide_render_target_depth((tide_render_target){.id = TIDE_RENDER_WINDOW}).target.id == TIDE_RENDER_INVALID);
    TIDE_CHECK(tide_draw_depth_source(&d, (tide_depth_ref){0}) == 0 && d.texture_count == 2);
    // A resized target's depth is its new image's
    tide_render_target_resize(scene, 16.0f, 16.0f);
    const uint32_t smaller = tide_draw_depth_source(&d, depth);
    TIDE_REQUIRE(smaller && smaller != depth_slot);
    TIDE_CHECK(d.textures[smaller - 1u].target.width == 16.0f && d.textures[smaller - 1u].version == 2);

    // A blit doesn't read the depth of what it draws into, any more than its color
    static const tide_shader reads_depth = {.name = "depth read", .dims = 2, .vertex_size = sizeof(tide_vertex), .texture_count = 1,
                                            .texture_reads = {TIDE_SHADER_READ_DEPTH}};
    tide_draw_render_target(&d, scene);
    const uint32_t commands = d.count;
    tide_draw_blit(&d, &reads_depth, TIDE_BLEND_REPLACE, NULL, NULL, &smaller, 0);
    TIDE_CHECK(d.count == commands);
    tide_draw_render_target(&d, shadow);
    tide_draw_blit(&d, &reads_depth, TIDE_BLEND_REPLACE, NULL, NULL, &smaller, 0);
    TIDE_REQUIRE(d.count == commands + 2u && d.commands[d.count - 1u].kind == TIDE_DRAW_BLIT);
    TIDE_CHECK(d.runs[d.commands[d.count - 1u].shaded.run].textures[0] == smaller);

    // Another list has each as what it was
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.run_count == 1 && appended.target_count == 2);
    const uint32_t carried = appended.runs[0].textures[0];
    TIDE_REQUIRE(carried && carried <= appended.texture_count);
    TIDE_CHECK(appended.textures[carried - 1u].space == TIDE_PIXELS_TARGET_DEPTH && appended.textures[carried - 1u].id == scene.id);
    TIDE_CHECK(tide_render_target_format(appended.targets[1].id) == TIDE_TARGET_DEPTH);

    // Another build makes a target again from its id and its size alone, where the id is no address
    // it can read: what it held comes with it, and every alias is the one new target
    const tide_render_target packed = {.id = TIDE_RENDER_MANAGED | (UINT64_C(0x123) << 16),
                                       .size = {.width = 24.0f, .height = 18.0f, .scale = 2.0f}};
    const tide_render_target colored = {.id = TIDE_RENDER_MANAGED | (UINT64_C(0x124) << 16) | 1u, .size = {.window = 0.5f}};
    const tide_render_target first = tide_render_target_recreate(packed), second = tide_render_target_recreate(packed);
    const tide_render_target third = tide_render_target_recreate(colored);
    TIDE_CHECK(first.id == second.id && first.size.scale == 2.0f && tide_render_target_live(first.id));
    TIDE_CHECK(tide_render_target_format(first.id) == TIDE_TARGET_DEPTH && tide_render_target_format(third.id) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(third.id != first.id && third.size.window == 0.5f);
    tide_render_targets_recreated();

    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_render_targets_free();
}

// A target holds up to four color images, each of a format of its own, and
// its id says them all. A texture's reference names one of them, which the
// list keeps apart from the others and another build finds again.
TIDE_TEST(draw_targets_hold_several_colors_and_lend_each)
{
    tide_draw_list d = {0}, appended = {0};
    const tide_target_format three[] = {TIDE_TARGET_RGBA8, TIDE_TARGET_RGBA16F, TIDE_TARGET_RGBA16F};
    const tide_render_target scene = tide_render_target_make_of(64.0f, 48.0f, three, 3);
    tide_target_formats held = tide_render_target_formats(scene.id);
    TIDE_REQUIRE(held.count == 3 && tide_render_target_colors(scene.id) == 3);
    TIDE_CHECK(held.color[0] == TIDE_TARGET_RGBA8 && held.color[1] == TIDE_TARGET_RGBA16F && held.color[2] == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(tide_render_target_format(scene.id) == TIDE_TARGET_RGBA8 && tide_render_target_live(scene.id));
    TIDE_CHECK(scene.size.width == 64.0f && scene.size.height == 48.0f && scene.size.scale == 0.0f);
    // One float image is a target made with that format, by either function
    const tide_render_target bright = tide_render_target_make_as(8.0f, 8.0f, TIDE_TARGET_RGBA16F);
    held = tide_render_target_formats(bright.id);
    TIDE_CHECK(held.count == 1 && held.color[0] == TIDE_TARGET_RGBA16F && tide_render_target_format(bright.id) == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(tide_render_target_formats(tide_render_target_make_of(8.0f, 8.0f, &three[1], 1).id).color[0] == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(tide_render_target_colors(tide_render_target_make_of(8.0f, 8.0f, (tide_target_format[]){TIDE_TARGET_DEPTH}, 1).id) == 0);
    // The other three ways to make one hold several too
    const tide_render_target exact = tide_render_target_make_scaled_of(320.0f, 180.0f, 1.0f, three, 2);
    const tide_render_target screen = tide_render_target_window_of(three, 3), half = tide_render_target_screen_of(0.5f, three, 2);
    TIDE_CHECK(tide_render_target_colors(exact.id) == 2 && exact.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_colors(screen.id) == 3 && screen.size.window == 1.0f);
    TIDE_CHECK(tide_render_target_colors(half.id) == 2 && half.size.window == 0.5f);
    // Four at the most, each a color's: among several, depth alone is a color image, and so is a format there's none of
    const tide_target_format many[] = {TIDE_TARGET_RGBA16F, TIDE_TARGET_DEPTH, (tide_target_format)9, TIDE_TARGET_RGBA16F,
                                       TIDE_TARGET_RGBA16F, TIDE_TARGET_RGBA16F};
    held = tide_render_target_formats(tide_render_target_make_of(4.0f, 4.0f, many, 6).id);
    TIDE_REQUIRE(held.count == TIDE_TARGET_COLORS);
    TIDE_CHECK(held.color[0] == TIDE_TARGET_RGBA16F && held.color[1] == TIDE_TARGET_RGBA8 && held.color[2] == TIDE_TARGET_RGBA8
               && held.color[3] == TIDE_TARGET_RGBA16F);
    // The window, a borrowed target and a target that's none hold one color image
    TIDE_CHECK(tide_render_target_colors(TIDE_RENDER_WINDOW) == 1 && tide_render_target_colors(1001) == 1);
    TIDE_CHECK(tide_render_target_colors(TIDE_RENDER_INVALID) == 1 && tide_render_target_image(1001 | TIDE_RENDER_IMAGE) == 0);

    // Each color image is a texture of its own: the first is the target's `color`
    const tide_texture_ref first = tide_render_target_color(scene), again = tide_render_target_color_at(scene, 0);
    const tide_texture_ref second = tide_render_target_color_at(scene, 1), third = tide_render_target_color_at(scene, 2);
    TIDE_CHECK(first.target.id == scene.id && again.target.id == scene.id);
    TIDE_CHECK(second.target.id != scene.id && tide_render_target_whole(second.target.id) == scene.id);
    TIDE_CHECK(tide_render_target_image(first.target.id) == 0 && tide_render_target_image(second.target.id) == 1
               && tide_render_target_image(third.target.id) == 2);
    TIDE_CHECK(tide_render_target_live(second.target.id) && tide_render_target_version(third.target.id) == 1);
    TIDE_CHECK(tide_render_target_colors(third.target.id) == 3 && second.target.size.width == 64.0f);
    // One past what any target holds is no texture's; one this target hasn't got is named, for the renderer to refuse
    TIDE_CHECK(tide_render_target_color_at(scene, 4).target.id == TIDE_RENDER_INVALID);
    TIDE_CHECK(tide_render_target_color_at(scene, -1).target.id == TIDE_RENDER_INVALID);
    TIDE_CHECK(tide_render_target_image(tide_render_target_color_at(scene, 3).target.id) == 3);
    TIDE_CHECK(tide_render_target_color_at((tide_render_target){.id = TIDE_RENDER_WINDOW}, 1).target.id == TIDE_RENDER_INVALID);
    TIDE_CHECK(tide_render_target_color_at((tide_render_target){.id = 1001, .size = {.width = 4, .height = 4}}, 1).target.id
               == TIDE_RENDER_INVALID);
    // Its depth is the target's, whichever image a reference names
    TIDE_CHECK(tide_render_target_depth(second.target).target.id == scene.id);

    const uint32_t slots[3] = {tide_draw_texture_source(&d, first), tide_draw_texture_source(&d, second),
                               tide_draw_texture_source(&d, third)};
    TIDE_REQUIRE(slots[0] && slots[1] && slots[2] && slots[0] != slots[1] && slots[1] != slots[2] && d.texture_count == 3);
    TIDE_CHECK(d.textures[slots[1] - 1u].space == TIDE_PIXELS_TARGET && d.textures[slots[1] - 1u].id == second.target.id);
    TIDE_CHECK(tide_draw_texture_source(&d, second) == slots[1] && d.texture_count == 3);
    // A resized target's images are its new ones, each still its own
    tide_render_target_resize(scene, 32.0f, 32.0f);
    const uint32_t smaller = tide_draw_texture_source(&d, second);
    TIDE_REQUIRE(smaller && smaller != slots[1]);
    TIDE_CHECK(d.textures[smaller - 1u].version == 2 && d.textures[smaller - 1u].target.width == 32.0f);
    TIDE_CHECK(tide_render_target_colors(scene.id) == 3);

    // A blit into a target samples none of its color images, the second no more than the first
    static const tide_shader reads = {.name = "two outputs", .dims = 2, .vertex_size = sizeof(tide_vertex), .texture_count = 1,
                                      .outputs = 2};
    TIDE_CHECK(tide_shader_outputs(&reads) == 2 && tide_shader_outputs(&(tide_shader){0}) == 1
               && tide_shader_outputs(&(tide_shader){.outputs = 9}) == TIDE_TARGET_COLORS);
    tide_draw_render_target(&d, scene);
    const uint32_t commands = d.count;
    tide_draw_blit(&d, &reads, TIDE_BLEND_REPLACE, NULL, NULL, &smaller, 0);
    TIDE_CHECK(d.count == commands);
    tide_draw_render_target(&d, bright);
    tide_draw_blit(&d, &reads, TIDE_BLEND_REPLACE, NULL, NULL, &smaller, 0);
    TIDE_REQUIRE(d.count == commands + 2u && d.commands[d.count - 1u].kind == TIDE_DRAW_BLIT);

    // Another list has the image the run named
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.run_count == 1);
    const uint32_t carried = appended.runs[0].textures[0];
    TIDE_REQUIRE(carried && carried <= appended.texture_count);
    TIDE_CHECK(appended.textures[carried - 1u].id == second.target.id && appended.textures[carried - 1u].version == 2);

    // Another build makes the target again from its id alone, with every image it had, and a
    // texture's reference to one of them names the same image of the new target
    const uint64_t token = TIDE_RENDER_MANAGED | (UINT64_C(0x321) << 16) | (scene.id & TIDE_RENDER_FORMAT);
    const tide_render_target packed = {.id = token, .size = {.width = 24.0f, .height = 18.0f}};
    const tide_render_target packed_second = {.id = token | (UINT64_C(1) << TIDE_RENDER_IMAGE_SHIFT), .size = packed.size};
    const tide_texture_ref rebound = tide_render_target_color(tide_render_target_recreate(packed_second));
    const tide_render_target remade = tide_render_target_recreate(packed);
    held = tide_render_target_formats(remade.id);
    TIDE_REQUIRE(tide_render_target_live(remade.id) && held.count == 3);
    TIDE_CHECK(held.color[0] == TIDE_TARGET_RGBA8 && held.color[1] == TIDE_TARGET_RGBA16F && held.color[2] == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(tide_render_target_image(remade.id) == 0 && tide_render_target_image(rebound.target.id) == 1);
    TIDE_CHECK(tide_render_target_whole(rebound.target.id) == remade.id);
    tide_render_targets_recreated();

    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_render_targets_free();
}

TIDE_TEST(draw_targets_say_their_samples_and_keep_them_through_a_reload)
{
    tide_draw_list d = {0};
    const tide_target_format two[] = {TIDE_TARGET_RGBA8, TIDE_TARGET_RGBA16F};

    // A target's samples are its id's to say, beside what it holds: 1 for one that says none
    const tide_render_target plain = tide_render_target_make(64.0f, 48.0f);
    const tide_render_target smooth = tide_render_target_make_sampled(64.0f, 48.0f, NULL, 0, 4);
    TIDE_REQUIRE(tide_render_target_live(plain.id) && tide_render_target_live(smooth.id));
    TIDE_CHECK(tide_render_target_samples(plain.id) == 1 && tide_render_target_samples(smooth.id) == 4);
    TIDE_CHECK(tide_render_target_colors(smooth.id) == 1 && tide_render_target_format(smooth.id) == TIDE_TARGET_RGBA8);
    TIDE_CHECK(smooth.size.width == 64.0f && smooth.size.height == 48.0f && smooth.size.scale == 0.0f);
    TIDE_CHECK(sizeof(tide_render_target) == 24); // As ever: no field of its own for them
    // Each count there is, with each way to make a target, and what it holds beside
    const tide_render_target one = tide_render_target_make_sampled(8.0f, 8.0f, two, 2, 1);
    const tide_render_target pair = tide_render_target_make_scaled_sampled(320.0f, 180.0f, 1.0f, two, 2, 2);
    const tide_render_target screen = tide_render_target_window_sampled(&two[1], 1, 8);
    const tide_render_target half = tide_render_target_screen_sampled(0.5f, two, 2, 4);
    TIDE_CHECK(tide_render_target_samples(one.id) == 1 && tide_render_target_colors(one.id) == 2);
    TIDE_CHECK(tide_render_target_samples(pair.id) == 2 && tide_render_target_colors(pair.id) == 2 && pair.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_samples(screen.id) == 8 && tide_render_target_format(screen.id) == TIDE_TARGET_RGBA16F
               && screen.size.window == 1.0f);
    TIDE_CHECK(tide_render_target_samples(half.id) == 4 && half.size.window == 0.5f);
    tide_target_formats held = tide_render_target_formats(half.id);
    TIDE_CHECK(held.count == 2 && held.color[0] == TIDE_TARGET_RGBA8 && held.color[1] == TIDE_TARGET_RGBA16F);
    // A count there's none of is the one under it, and a target of depth alone has one: nothing reads its samples
    TIDE_CHECK(tide_render_target_samples(tide_render_target_make_sampled(8.0f, 8.0f, NULL, 0, 3).id) == 2);
    TIDE_CHECK(tide_render_target_samples(tide_render_target_make_sampled(8.0f, 8.0f, NULL, 0, 7).id) == 4);
    TIDE_CHECK(tide_render_target_samples(tide_render_target_make_sampled(8.0f, 8.0f, NULL, 0, 64).id) == 8);
    TIDE_CHECK(tide_render_target_samples(tide_render_target_make_sampled(8.0f, 8.0f, NULL, 0, 0).id) == 1);
    TIDE_CHECK(tide_render_target_samples(tide_render_target_make_sampled(8.0f, 8.0f, NULL, 0, -4).id) == 1);
    const tide_render_target shadow = tide_render_target_make_sampled(8.0f, 8.0f, (tide_target_format[]){TIDE_TARGET_DEPTH}, 1, 4);
    TIDE_CHECK(tide_render_target_samples(shadow.id) == 1 && tide_render_target_colors(shadow.id) == 0);
    // The window and a borrowed target have one
    TIDE_CHECK(tide_render_target_samples(TIDE_RENDER_WINDOW) == 1 && tide_render_target_samples(1001) == 1);
    TIDE_CHECK(tide_render_target_samples(TIDE_RENDER_INVALID) == 1 && tide_render_target_samples(1001 | TIDE_RENDER_SAMPLES) == 1);

    // Its images' references, and its depth's, say whose they are, samples and all: the target's own id
    const tide_texture_ref first = tide_render_target_color(half), second = tide_render_target_color_at(half, 1);
    TIDE_CHECK(first.target.id == half.id && tide_render_target_whole(second.target.id) == half.id);
    TIDE_CHECK(tide_render_target_image(second.target.id) == 1 && tide_render_target_samples(second.target.id) == 4);
    TIDE_CHECK(tide_render_target_colors(second.target.id) == 2 && tide_render_target_depth(half).target.id == half.id);
    TIDE_CHECK(tide_render_target_state_of(second.target.id) == tide_render_target_state_of(half.id));
    // A list draws into it and reads it by that id
    tide_draw_render_target(&d, smooth);
    TIDE_REQUIRE(d.target_count == 1 && d.targets[0].id == smooth.id);
    const uint32_t color = tide_draw_texture_source(&d, tide_render_target_color(smooth));
    const uint32_t depth = tide_draw_depth_source(&d, tide_render_target_depth(smooth));
    TIDE_REQUIRE(color && depth && color != depth);
    TIDE_CHECK(d.textures[color - 1u].id == smooth.id && d.textures[depth - 1u].space == TIDE_PIXELS_TARGET_DEPTH);
    // Resizing keeps them
    tide_render_target_resize(smooth, 32.0f, 32.0f);
    TIDE_CHECK(tide_render_target_samples(smooth.id) == 4 && tide_render_target_version(smooth.id) == 2);

    // Another build makes the target again from its id alone, with the samples it had, and a
    // texture's reference to one of its images names the same image of the new target
    const uint64_t token = TIDE_RENDER_MANAGED | (UINT64_C(0x654) << 16) | (half.id & (TIDE_RENDER_FORMAT | TIDE_RENDER_SAMPLES));
    const tide_render_target packed = {.id = token, .size = {.window = 0.5f}};
    const tide_render_target packed_second = {.id = token | (UINT64_C(1) << TIDE_RENDER_IMAGE_SHIFT), .size = packed.size};
    const tide_texture_ref rebound = tide_render_target_color(tide_render_target_recreate(packed_second));
    const tide_render_target remade = tide_render_target_recreate(packed);
    TIDE_REQUIRE(tide_render_target_live(remade.id) && remade.id != half.id);
    held = tide_render_target_formats(remade.id);
    TIDE_CHECK(tide_render_target_samples(remade.id) == 4 && held.count == 2 && held.color[1] == TIDE_TARGET_RGBA16F);
    TIDE_CHECK(remade.size.window == 0.5f && tide_render_target_image(remade.id) == 0);
    TIDE_CHECK(tide_render_target_whole(rebound.target.id) == remade.id && tide_render_target_image(rebound.target.id) == 1);
    // One with none comes back with none
    const tide_render_target bare = tide_render_target_recreate(
        (tide_render_target){.id = TIDE_RENDER_MANAGED | (UINT64_C(0x655) << 16) | 1u, .size = {.width = 4.0f, .height = 4.0f}});
    TIDE_CHECK(tide_render_target_samples(bare.id) == 1 && tide_render_target_colors(bare.id) == 1);
    tide_render_targets_recreated();

    // A state another build's library made is found through an id with samples in it
    tide_render_target_state *foreign = foreign_state();
    TIDE_REQUIRE(foreign);
    void *foreign_memory = foreign->memory;
    const uint16_t holds = (uint16_t)(1u | 2u << TIDE_RENDER_SAMPLES_SHIFT);
    *foreign = (tide_render_target_state){.owner = &foreign, .memory = foreign_memory, .size = {.width = 12, .height = 9},
                                         .version = 3, .alive = true, .holds = holds};
    const tide_render_target adopted = {.id = TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)foreign | holds};
    TIDE_CHECK(tide_render_target_samples(adopted.id) == 4 && tide_render_target_version(adopted.id) == 3);
    tide_render_target_adopt(adopted);
    TIDE_CHECK(foreign == NULL && tide_render_target_live(adopted.id) && tide_render_target_resolve(adopted).size.width == 12.0f);

    tide_draw_free(&d);
    tide_render_targets_free();
}

static const tide_vertex quad[4] = {
    {{0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{8.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{8.0f, 8.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{0.0f, 8.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};
static const uint32_t quad_indices[6] = {0, 1, 2, 0, 2, 3};

TIDE_TEST(draw_meshes_keep_their_triangles)
{
    tide_draw_list d = {0};
    tide_draw_rect(&d, tide_f2(0.0f, 0.0f), tide_f2(1.0f, 1.0f), TIDE_COLOR_WHITE);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 2 && d.vertex_count == 4 && d.index_count == 6);
    TIDE_CHECK(d.commands[1].kind == TIDE_DRAW_MESH);
    TIDE_CHECK(d.commands[1].mesh.first == 0 && d.commands[1].mesh.count == 6 && d.commands[1].mesh.texture == 0);
    TIDE_CHECK(d.vertices[2].uv.x == 1.0f && d.vertices[2].color.b == 1.0f);

    // Another right after it is more of the same command, its indices places
    // in the list's vertices
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 2 && d.vertex_count == 8 && d.index_count == 12);
    TIDE_CHECK(d.commands[1].mesh.count == 12);
    TIDE_CHECK(d.indices[6] == 4 && d.indices[8] == 6 && d.indices[11] == 7);

    // Something between them, or another filter, starts another
    tide_draw_set_clip(&d, (tide_rect){1.0f, 2.0f, 3.0f, 4.0f});
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_POINT);
    TIDE_REQUIRE(d.count == 5);
    TIDE_REQUIRE(d.commands[2].kind == TIDE_DRAW_CLIP && d.commands[2].clip == 1 && d.clip_count == 1);
    TIDE_CHECK(d.clips[0].rect.y == 2.0f && d.clips[0].rect.width == 3.0f);
    TIDE_CHECK(d.commands[3].mesh.first == 12 && d.commands[3].mesh.count == 6);
    TIDE_CHECK(d.commands[4].mesh.first == 18 && d.commands[4].mesh.filter == TIDE_FILTER_POINT);

    // A triangle with an index past the vertices is left out, and so is what's
    // left over after the last whole one
    static const uint32_t off[8] = {0, 1, 2, 0, 4, 1, 3, 2};
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, off, 8, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 1 && d.index_count == 3);
    TIDE_CHECK(d.indices[0] == 0 && d.indices[2] == 2);
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, off + 3, 3, NULL, TIDE_FILTER_BILINEAR); // Nothing of it is left
    TIDE_CHECK(d.count == 0 && d.index_count == 0);

    // Vertices several meshes share go in once
    tide_draw_reset(&d);
    tide_draw_rect(&d, tide_f2(0.0f, 0.0f), tide_f2(1.0f, 1.0f), TIDE_COLOR_WHITE);
    tide_draw_vertices(&d, quad, 4);
    const uint32_t base = tide_draw_vertices(&d, quad, 4);
    TIDE_CHECK(base == 4);
    tide_draw_triangles(&d, base, quad_indices, 3, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 1.0f, 1.0f});
    tide_draw_triangles(&d, base, quad_indices + 3, 3, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 4 && d.vertex_count == 8 && d.index_count == 6);
    TIDE_CHECK(d.indices[0] == 4 && d.indices[5] == 7);
    TIDE_CHECK(d.commands[3].mesh.first == 3 && d.commands[3].mesh.count == 3);
    tide_draw_free(&d);
    TIDE_CHECK(d.vertices == NULL && d.indices == NULL && d.vertex_count == 0);
}

// A mesh of one quad, one color, is kept as the quad (tide_draw_quad): its
// corners are those it was given, to the bit, and quads one after another
// with the same image are one command; anything else is corners and triangles.
TIDE_TEST(draw_meshes_of_one_quad_are_kept_as_quads)
{
    tide_draw_list d = {0};
    tide_vertex sprite[4];
    for (uint32_t i = 0; i < 4u; i++) sprite[i] = (tide_vertex){quad[i].position, quad[i].uv, {0.25f, 0.5f, 0.75f, 1.0f}};
    sprite[2].position.x = -0.0f; // Told apart by its bits from 0, as the renderer tells them
    sprite[1].position.x = -0.0f;
    tide_draw_mesh(&d, sprite, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh(&d, sprite, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 1 && d.quad_count == 2 && d.vertex_count == 0 && d.index_count == 0);
    TIDE_CHECK(d.commands[0].kind == TIDE_DRAW_QUADS && d.commands[0].mesh.first == 0 && d.commands[0].mesh.count == 2);
    for (uint32_t k = 0; k < 6u; k++) { // p q r p r s
        const tide_vertex v = tide_draw_quad_corner(&d.quads[1], k), want = sprite[quad_indices[k]];
        TIDE_CHECK(memcmp(&v, &want, sizeof v) == 0);
    }

    // Through the same calls Tide's lists take, and after other meshes: a run of quads of its own
    static const uint32_t other_way[6] = {2, 1, 0, 3, 2, 0};
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR); // Four colors: corners and triangles
    tide_draw_mesh_with(&d, sprite, 4, quad_indices, 6, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_mesh(&d, sprite, 4, other_way, 6, NULL, TIDE_FILTER_BILINEAR); // Its triangles share other corners
    TIDE_REQUIRE(d.count == 4 && d.quad_count == 3 && d.vertex_count == 8 && d.index_count == 12);
    TIDE_CHECK(d.commands[1].kind == TIDE_DRAW_MESH && d.commands[2].kind == TIDE_DRAW_QUADS
               && d.commands[2].mesh.first == 2 && d.commands[3].kind == TIDE_DRAW_MESH);

    // Tinted, it's corners and triangles, as a tint is worked into each corner
    tide_draw_reset(&d);
    tide_draw_set_tint(&d, (tide_color){0.5f, 0.5f, 0.5f, 1.0f});
    tide_draw_mesh(&d, sprite, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.quad_count == 0 && d.vertex_count == 4);

    // Another list's quads are this one's, with this one's textures
    static const uint8_t pixels[4] = {9, 8, 7, 255};
    tide_draw_list from = {0};
    tide_draw_reset(&d);
    tide_draw_rect(&d, tide_f2(0.0f, 0.0f), tide_f2(1.0f, 1.0f), TIDE_COLOR_WHITE);
    tide_draw_mesh(&from, sprite, 4, quad_indices, 6, &(tide_texture){pixels, 1, 1, 5}, TIDE_FILTER_POINT);
    tide_draw_mesh(&from, sprite, 4, quad_indices, 6, &(tide_texture){pixels, 1, 1, 5}, TIDE_FILTER_POINT);
    TIDE_REQUIRE(from.count == 1 && from.quad_count == 2);
    tide_draw_append(&d, &from);
    TIDE_REQUIRE(d.quad_count == 2 && d.texture_count == 1);
    const tide_draw_command *appended = &d.commands[d.count - 1u];
    TIDE_CHECK(appended->kind == TIDE_DRAW_QUADS && appended->mesh.count == 2 && appended->mesh.texture == 1
               && appended->mesh.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(memcmp(&d.quads[1], &from.quads[1], sizeof d.quads[1]) == 0);
    tide_draw_free(&from);
    tide_draw_free(&d);
    TIDE_CHECK(d.quads == NULL && d.quad_count == 0);
}

TIDE_TEST(draw_textures_are_copied_when_they_change)
{
    tide_draw_list d = {0};
    uint8_t pixels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    tide_texture texture = {pixels, 2, 2, 1};
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_REQUIRE(d.count == 1 && d.texture_count == 1);
    TIDE_CHECK(d.commands[0].mesh.texture == 1);
    const tide_draw_texture *t = &d.textures[0];
    TIDE_CHECK(t->width == 2 && t->height == 2 && t->version == 1 && t->space == TIDE_PIXELS_MEMORY);
    TIDE_CHECK(t->pixels != pixels && memcmp(t->pixels, pixels, sizeof pixels) == 0);

    // The same pixels at the same version aren't copied again, in this frame
    // or the next
    pixels[0] = 7;
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_CHECK(d.texture_count == 1 && d.textures[0].pixels[0] == 255);
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_CHECK(d.texture_count == 1 && d.textures[0].pixels[0] == 255);

    // Another version is, and so is another size
    texture.version = tide_texture_version(pixels, 2, 2);
    TIDE_CHECK(texture.version != 1 && texture.version == tide_texture_version(pixels, 2, 2));
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_CHECK(d.texture_count == 1 && d.textures[0].pixels[0] == 7);
    pixels[0] = 9;
    texture.height = 1;
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_CHECK(d.texture_count == 1 && d.textures[0].height == 1 && d.textures[0].pixels[0] == 9);

    // Other pixels are another texture, and no pixels are none
    static const uint8_t white[4] = {255, 255, 255, 255};
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &(tide_texture){white, 1, 1, 0}, TIDE_FILTER_BILINEAR);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &(tide_texture){NULL, 1, 1, 0}, TIDE_FILTER_BILINEAR);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &(tide_texture){white, 0, 1, 0}, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.texture_count == 2);
    TIDE_CHECK(d.commands[d.count - 2].mesh.texture == 2);
    TIDE_CHECK(d.commands[d.count - 1].mesh.texture == 0);

    // Forgotten, as when the game's code was swapped, it's copied again though
    // its version is the same
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_REQUIRE(d.texture_count == 2 && d.textures[0].pixels[0] == 9 && d.textures[0].epoch == 0);
    pixels[0] = 11;
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_CHECK(d.textures[0].pixels[0] == 9);
    tide_draw_forget(&d);
    TIDE_CHECK(d.texture_count == 0 && d.commands[0].mesh.texture == 0);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    TIDE_REQUIRE(d.texture_count == 1);
    TIDE_CHECK(d.textures[0].pixels[0] == 11 && d.textures[0].epoch == 1);
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &texture, TIDE_FILTER_POINT);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &(tide_texture){white, 1, 1, 0}, TIDE_FILTER_BILINEAR);

    // A texture no command of a frame drew with is let go when the next starts
    tide_draw_reset(&d);
    tide_draw_mesh(&d, quad, 4, quad_indices, 6, &(tide_texture){white, 1, 1, 0}, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.texture_count == 2);
    tide_draw_reset(&d);
    TIDE_REQUIRE(d.texture_count == 1);
    TIDE_CHECK(d.textures[0].id == (uint64_t)(uintptr_t)white);
    tide_draw_reset(&d);
    TIDE_CHECK(d.texture_count == 0);
    tide_draw_free(&d);
}

TIDE_TEST(draw_texture_slots_survive_growth_and_frame_pruning)
{
    tide_draw_list d = {0};
    enum { images = 1024 };
    const uint64_t prefix = UINT64_C(0x1234567800000000);
    uint8_t *pixels;
    for (uint32_t i = 0; i < images; i++) {
        for (uint32_t space = TIDE_PIXELS_MEMORY; space <= TIDE_PIXELS_GRID; space++) {
            const uint32_t slot = tide_draw_texture_slot(&d, space, prefix + i, 7, 1, 1, &pixels);
            TIDE_REQUIRE(slot == i * 2u + space + 1u && pixels != NULL);
            memset(pixels, (int)(i & 255u), 4);
        }
    }
    TIDE_CHECK(d.texture_count == images * 2u);
    // Growth and repeated reads leave places and copied pixels as they were.
    for (uint32_t i = images; i-- > 0;) {
        for (uint32_t space = TIDE_PIXELS_MEMORY; space <= TIDE_PIXELS_GRID; space++) {
            const uint32_t slot = tide_draw_texture_slot(&d, space, prefix + i, 7, 1, 1, &pixels);
            TIDE_REQUIRE(slot == i * 2u + space + 1u && pixels == NULL);
            TIDE_CHECK(d.textures[slot - 1u].pixels[0] == (uint8_t)i);
        }
    }
    // An ordinary image's versions, sizes and formats all reuse its place.
    const uint32_t changed = tide_draw_texture_slot_as(&d, TIDE_PIXELS_MEMORY, prefix + 1u, 8, 2, 1,
                                                       TIDE_TEXELS_INTS, &pixels);
    TIDE_REQUIRE(changed == 3u && pixels != NULL);
    memset(pixels, 42, 32);
    TIDE_CHECK(tide_draw_texture_slot_as(&d, TIDE_PIXELS_MEMORY, prefix + 1u, 8, 2, 1,
                                         TIDE_TEXELS_INTS, &pixels) == changed && pixels == NULL);
    tide_draw_reset(&d);
    for (uint32_t i = 1; i < images; i += 3u) {
        const uint32_t slot = tide_draw_texture_slot(&d, TIDE_PIXELS_MEMORY, prefix + i, 9, 1, 1, &pixels);
        TIDE_REQUIRE(slot == i * 2u + 1u && pixels != NULL);
        memset(pixels, 43, 4);
    }
    // Dropped entries disappear, and the surviving entries are compacted in array order.
    tide_draw_reset(&d);
    TIDE_REQUIRE(d.texture_count == images / 3u);
    for (uint32_t i = images / 3u; i-- > 0;) {
        const uint32_t slot = tide_draw_texture_slot(&d, TIDE_PIXELS_MEMORY, prefix + i * 3u + 1u, 9, 1, 1, &pixels);
        TIDE_REQUIRE(slot == i + 1u && pixels == NULL);
        TIDE_CHECK(d.textures[slot - 1u].pixels[0] == 43);
    }
    const uint32_t restored = tide_draw_texture_slot(&d, TIDE_PIXELS_GRID, prefix + 1u, 7, 1, 1, &pixels);
    TIDE_REQUIRE(restored == images / 3u + 1u && pixels != NULL);
    tide_draw_forget(&d);
    TIDE_CHECK(d.texture_count == 0);
    TIDE_CHECK(tide_draw_texture_slot(&d, TIDE_PIXELS_MEMORY, prefix + 1u, 9, 1, 1, &pixels) == 1u && pixels != NULL);
    tide_draw_free(&d);
    TIDE_CHECK(d.textures == NULL && d.texture_index == NULL);
}

TIDE_TEST(draw_target_texture_versions_survive_growth_pruning_and_append)
{
    tide_draw_list d = {0}, other = {0};
    const tide_render_target target = tide_render_target_make(8, 6);
    const tide_texture_ref color = tide_render_target_color(target);
    const tide_depth_ref depth = tide_render_target_depth(target);
    const uint32_t first = tide_draw_texture_source(&d, color);
    const uint32_t first_depth = tide_draw_depth_source(&d, depth);
    tide_render_target_resize(target, 8, 6);
    const uint32_t second = tide_draw_texture_source(&d, color);
    tide_render_target_resize(target, 16, 12);
    const uint32_t third = tide_draw_texture_source(&d, color);
    const tide_cubemap_ref cube = tide_cubemap_make(4);
    const uint32_t cube_slot = tide_draw_cube_source(&d, cube);
    TIDE_REQUIRE(first == 1u && first_depth == 2u && second == 3u && third == 4u && cube_slot == 5u);
    for (uint32_t i = 1; i <= 256; i++) {
        TIDE_REQUIRE(tide_draw_target_texture(&d, i, 1, 1) == i + 5u);
    }
    TIDE_CHECK(tide_draw_texture_source(&d, color) == third && tide_draw_cube_source(&d, cube) == cube_slot);
    TIDE_CHECK(d.textures[first - 1u].version == 1 && d.textures[second - 1u].version == 2
               && d.textures[third - 1u].version == 3);
    // Replaying earlier versions through append finds each one's original place.
    static const tide_shader reads = {.dims = 2, .vertex_size = sizeof(tide_vertex), .texture_count = 1};
    const uint32_t slots[] = {first, first_depth, second, third, cube_slot};
    for (uint32_t i = 0; i < 5; i++) tide_draw_blit(&d, &reads, TIDE_BLEND_REPLACE, NULL, NULL, &slots[i], 0);
    tide_draw_append(&other, &d);
    TIDE_REQUIRE(other.run_count == 5 && other.texture_count == 5);
    for (uint32_t i = 0; i < 5; i++) {
        const uint32_t carried = other.runs[i].textures[0];
        TIDE_REQUIRE(carried == i + 1u);
        TIDE_CHECK(other.textures[carried - 1u].space == d.textures[slots[i] - 1u].space
                   && other.textures[carried - 1u].version == d.textures[slots[i] - 1u].version);
    }
    tide_draw_append(&other, &d);
    TIDE_CHECK(other.texture_count == 5);
    tide_draw_reset(&d);
    TIDE_CHECK(tide_draw_texture_source(&d, color) == third && tide_draw_cube_source(&d, cube) == cube_slot);
    tide_draw_reset(&d);
    TIDE_REQUIRE(d.texture_count == 2);
    TIDE_CHECK(tide_draw_texture_source(&d, color) == 1u && tide_draw_cube_source(&d, cube) == 2u);
    TIDE_CHECK(tide_draw_depth_source(&d, depth) == 3u);
    tide_draw_forget(&d);
    TIDE_CHECK(tide_draw_texture_source(&d, color) == 1u && tide_draw_cube_source(&d, cube) == 2u);
    tide_draw_free(&d);
    tide_draw_free(&other);
    tide_render_targets_free();
}

// Appended to another list, as the GUI's are, meshes keep their triangles and
// their textures.
TIDE_TEST(draw_meshes_append)
{
    static const uint8_t pixel[4] = {1, 2, 3, 4};
    tide_draw_list world = {0}, over = {0};
    tide_draw_mesh(&world, quad, 4, quad_indices, 6, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_set_clip(&world, (tide_rect){0.0f, 0.0f, 4.0f, 4.0f});
    TIDE_CHECK(world.place.clip);
    tide_draw_vertices(&over, quad, 2); // Before its mesh's, so its indices don't start at 0
    tide_draw_mesh(&over, quad, 4, quad_indices, 6, &(tide_texture){pixel, 1, 1, 5}, TIDE_FILTER_POINT);
    tide_draw_unset_clip(&over);
    tide_draw_append(&world, &over);
    // The clip nothing drew through records nothing, and isn't the appended list's
    TIDE_REQUIRE(world.count == 2 && world.vertex_count == 10 && world.index_count == 12 && world.texture_count == 1);
    TIDE_CHECK(!world.place.clip);
    const tide_draw_command *c = &world.commands[1];
    TIDE_CHECK(c->kind == TIDE_DRAW_MESH && c->mesh.first == 6 && c->mesh.count == 6 && c->mesh.texture == 1);
    TIDE_CHECK(c->mesh.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(world.indices[6] == 6 && world.indices[11] == 9);
    static const uint8_t copied[4] = {0, 0, 0, 4}; // The list's copy: each color times its alpha, 4 of 255
    TIDE_CHECK(world.textures[0].version == 5 && memcmp(world.textures[0].pixels, copied, 4) == 0);
    tide_draw_free(&world);
    tide_draw_free(&over);
}

// Tide's Draw.Mesh: lists, and a grid of colors as the texture, which is only
// copied when its cells changed.
TIDE_TEST(draw_grids_are_textures)
{
    static const tide_grid_shape colors = {sizeof(tide_color), 2u, {5u, 5u, 0u}}; // As tidec makes Grid2<Color>'s
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    tide_grid grid = {0};
    tide_grid_set(&grid, tide_grid_new(2, 40, 33, 0), &colors, TIDE_IN_LOCAL);
    const tide_color red = {1.0f, 0.0f, 0.0f, 1.0f}, half = {0.5f, 2.0f, -1.0f, 0.25f};
    *(tide_color *)tide_grid_poke(&grid, 0, 0, 0, &colors, true, TIDE_IN_LOCAL) = red;
    *(tide_color *)tide_grid_poke(&grid, 39, 32, 0, &colors, true, TIDE_IN_LOCAL) = half;

    static const int32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const tide_list vertices = tide_list_from(quad, 4, sizeof(tide_vertex));
    const tide_list triangles = tide_list_from(indices, 6, sizeof(int32_t));
    tide_draw_list d = {0};
    tide_draw_mesh_lists(&d, vertices, triangles);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_REQUIRE(d.count == 2 && d.vertex_count == 8 && d.index_count == 12 && d.texture_count == 1);
    TIDE_CHECK(d.commands[0].mesh.texture == 0 && d.commands[0].mesh.filter == TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.commands[1].mesh.texture == 1 && d.commands[1].mesh.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(d.indices[6] == 4 && d.indices[11] == 7);
    const tide_draw_texture *t = &d.textures[0];
    TIDE_REQUIRE(t->width == 40 && t->height == 33 && t->space == TIDE_PIXELS_GRID);
    static const uint8_t first[4] = {255, 0, 0, 255}, last[4] = {32, 64, 0, 64}, none[4] = {0, 0, 0, 0};
    TIDE_CHECK(memcmp(t->pixels, first, 4) == 0);
    // Each color times its alpha, as a texture has them, with what's past 0 and 1 kept there first
    TIDE_CHECK(memcmp(t->pixels + (32 * 40 + 39) * 4, last, 4) == 0);
    TIDE_CHECK(memcmp(t->pixels + 4, none, 4) == 0);          // A cell nothing set
    TIDE_CHECK(memcmp(t->pixels + (10 * 40 + 35) * 4, none, 4) == 0); // ...and one where the grid has no chunk

    // Cells that stayed the same aren't copied again
    d.textures[0].pixels[4] = 77;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_CHECK(d.texture_count == 1 && d.textures[0].pixels[4] == 77);

    // One that changed is, even changed through a cache that had its chunk
    // from before the list looked
    tide_grid_cache cache = {0};
    *(tide_color *)tide_grid_cached_poke(&cache, &grid, 1, 0, 0, &colors, true, TIDE_IN_LOCAL) = red;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_CHECK(memcmp(d.textures[0].pixels + 4, first, 4) == 0);
    *(tide_color *)tide_grid_cached_poke(&cache, &grid, 2, 0, 0, &colors, true, TIDE_IN_LOCAL) = red;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_CHECK(memcmp(d.textures[0].pixels + 8, first, 4) == 0);

    // A cleared grid is clear pixels, and one with an open axis is no texture
    tide_grid_clear(&grid, &colors, TIDE_IN_LOCAL);
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_REQUIRE(d.texture_count == 1);
    TIDE_CHECK(memcmp(d.textures[0].pixels, none, 4) == 0);
    tide_grid_set(&grid, tide_grid_new(2, 40, 0, 0), &colors, TIDE_IN_LOCAL);
    *(tide_color *)tide_grid_poke(&grid, 0, 0, 0, &colors, true, TIDE_IN_LOCAL) = red;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_CHECK(d.count == 1 && d.commands[0].mesh.texture == 0);
    tide_draw_mesh_texture(&d, vertices, triangles, tide_texture_of_grid((tide_grid){0}, &colors, 1u));
    TIDE_CHECK(d.commands[0].mesh.count == 12 && d.commands[0].mesh.texture == 0);

    tide_draw_free(&d);
    tide_grid_release(&grid, &colors, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// What a renderer that has the texture `t` as it was at `had` is told changed:
// how many areas, -1 where the list doesn't know, and the first of them.
static int areas_since(const tide_draw_texture *t, const uint64_t had, tide_draw_area *first)
{
    const tide_draw_area *areas;
    uint32_t count;
    if (!tide_draw_texture_since(t, had, &areas, &count)) return -1;
    if (count && first) *first = areas[0];
    return (int)count;
}

static bool is_area(const tide_draw_area a, const int32_t x, const int32_t y, const int32_t width, const int32_t height)
{
    return a.x == x && a.y == y && a.width == width && a.height == height;
}

typedef struct shown {
    float place[3];
    int32_t kind;
} shown;

// A `shown` as a shader is sent it: its floats by their bits, then its int.
static void shown_words(const void *element, int32_t *words)
{
    memcpy(words, element, sizeof(shown));
}

// ...and only its kind, as a shader that reads no more of it is.
static void shown_kind(const void *element, int32_t *words)
{
    words[0] = ((const shown *)element)->kind;
}

// A grid's texture and a list's are megabytes of which a frame changes a
// little, so the list says what did: the pixels that differ from the ones it
// had as the frame started, for a renderer that has those to send the GPU
// only them.
TIDE_TEST(draw_textures_say_what_changed)
{
    static const tide_grid_shape colors = {sizeof(tide_color), 2u, {5u, 5u, 0u}};
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    tide_grid grid = {0};
    tide_grid_set(&grid, tide_grid_new(2, 100, 70, 0), &colors, TIDE_IN_LOCAL); // Four chunks across, three down
    const tide_color red = {1.0f, 0.0f, 0.0f, 1.0f}, blue = {0.0f, 0.0f, 1.0f, 1.0f};
    for (int32_t y = 0; y < 70; y++) {
        for (int32_t x = 0; x < 100; x++) *(tide_color *)tide_grid_poke(&grid, x, y, 0, &colors, true, TIDE_IN_LOCAL) = red;
    }
    static const int32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const tide_list vertices = tide_list_from(quad, 4, sizeof(tide_vertex));
    const tide_list triangles = tide_list_from(indices, 6, sizeof(int32_t));
    const tide_texture_ref texture = tide_texture_of_grid(grid, &colors, 1u);
    static const uint8_t reds[4] = {255, 0, 0, 255}, blues[4] = {0, 0, 255, 255}, none[4] = {0, 0, 0, 0};

    // New, nothing says what changed: a renderer takes all of it
    tide_draw_list d = {0};
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    TIDE_REQUIRE(d.texture_count == 1 && d.textures[0].width == 100 && d.textures[0].height == 70);
    const uint64_t first = d.textures[0].version;
    TIDE_CHECK(areas_since(&d.textures[0], first, NULL) == -1 && areas_since(&d.textures[0], 0, NULL) == -1);

    // A cell changed: that pixel, for a renderer that has the frame before, and all of it for any other
    *(tide_color *)tide_grid_poke(&grid, 40, 35, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    tide_draw_reset(&d);
    d.textures[0].pixels[0] = 77; // What isn't copied again stays as it is
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    const uint64_t second = d.textures[0].version;
    tide_draw_area area = {0};
    TIDE_CHECK(second != first && areas_since(&d.textures[0], first, &area) == 1 && is_area(area, 40, 35, 1, 1));
    TIDE_CHECK(areas_since(&d.textures[0], second, NULL) == -1 && areas_since(&d.textures[0], 5, NULL) == -1);
    TIDE_CHECK(memcmp(d.textures[0].pixels + (35 * 100 + 40) * 4, blues, 4) == 0 && d.textures[0].pixels[0] == 77);
    d.textures[0].pixels[0] = 255;

    // Cells of two chunks, in one frame: each its own area. A frame that draws it twice, with more changed
    // between, says all of it since the frame started
    *(tide_color *)tide_grid_poke(&grid, 1, 2, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    *(tide_color *)tide_grid_poke(&grid, 3, 2, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    *(tide_color *)tide_grid_poke(&grid, 99, 69, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    const tide_draw_area *areas;
    uint32_t count = 0;
    TIDE_REQUIRE(tide_draw_texture_since(&d.textures[0], second, &areas, &count) && count == 2);
    TIDE_CHECK(is_area(areas[0], 1, 2, 3, 1) && is_area(areas[1], 99, 69, 1, 1)); // From the first cell that differs to the last
    *(tide_color *)tide_grid_poke(&grid, 50, 3, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    TIDE_REQUIRE(tide_draw_texture_since(&d.textures[0], second, &areas, &count) && count == 3);
    TIDE_CHECK(is_area(areas[2], 50, 3, 1, 1));

    // A frame that changes nothing says nothing, and the next one counts from it
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    const uint64_t third = d.textures[0].version;
    *(tide_color *)tide_grid_poke(&grid, 0, 0, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    TIDE_CHECK(areas_since(&d.textures[0], third, &area) == 1 && is_area(area, 0, 0, 1, 1));
    TIDE_CHECK(areas_since(&d.textures[0], second, NULL) == -1);

    // A list added to another hands on what it knows, for the pixels the other has
    tide_draw_list over = {0};
    tide_draw_append(&over, &d);
    TIDE_REQUIRE(over.texture_count == 1 && memcmp(over.textures[0].pixels, d.textures[0].pixels, 100 * 70 * 4) == 0);
    const uint64_t fourth = d.textures[0].version;
    *(tide_color *)tide_grid_poke(&grid, 70, 40, 0, &colors, true, TIDE_IN_LOCAL) = blue;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    tide_draw_reset(&over);
    over.textures[0].pixels[4] = 77;
    tide_draw_append(&over, &d);
    TIDE_CHECK(areas_since(&over.textures[0], fourth, &area) == 1 && is_area(area, 70, 40, 1, 1));
    TIDE_CHECK(memcmp(over.textures[0].pixels + (40 * 100 + 70) * 4, blues, 4) == 0 && over.textures[0].pixels[4] == 77);
    tide_draw_free(&over);

    // A grid that's cleared has no chunk left: zeros where each was
    const uint64_t fifth = d.textures[0].version;
    tide_grid_clear(&grid, &colors, TIDE_IN_LOCAL);
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    TIDE_REQUIRE(tide_draw_texture_since(&d.textures[0], fifth, &areas, &count) && count >= 1);
    bool clear = true;
    for (int i = 0; i < 100 * 70; i++) clear &= memcmp(d.textures[0].pixels + i * 4, none, 4) == 0;
    TIDE_CHECK(clear);
    *(tide_color *)tide_grid_poke(&grid, 99, 0, 0, &colors, true, TIDE_IN_LOCAL) = red;
    const uint64_t sixth = d.textures[0].version;
    tide_draw_reset(&d);
    tide_draw_mesh_texture(&d, vertices, triangles, texture);
    TIDE_CHECK(areas_since(&d.textures[0], sixth, &area) == 1 && is_area(area, 99, 0, 1, 1));
    TIDE_CHECK(memcmp(d.textures[0].pixels + 99 * 4, reds, 4) == 0);

    // A list's elements as numbers, a pixel each here, in rows of 1024: 5000 of them, in chunks of a page
    tide_list items = {0};
    for (int32_t i = 0; i < 5000; i++) {
        *(shown *)tide_list_add(&items, sizeof(shown), TIDE_IN_LOCAL) = (shown){{(float)i, 1.0f, 2.0f}, i};
    }
    int32_t held = 0;
    tide_draw_reset(&d);
    uint32_t numbers = tide_draw_list_data(&d, items, sizeof(shown), 4, 0, shown_words, &held);
    TIDE_REQUIRE(numbers != 0 && held == 5000);
    const tide_draw_texture *t = &d.textures[numbers - 1u];
    TIDE_REQUIRE(t->width == 1024 && t->height == 5 && t->format == TIDE_TEXELS_INTS);
    const int32_t *ints = (const int32_t *)(const void *)t->pixels;
    TIDE_CHECK(ints[4999 * 4 + 3] == 4999 && areas_since(t, t->version, NULL) == -1);
    uint64_t was = t->version;

    // An element changed: its pixel, wherever its chunk is
    ((shown *)tide_list_at_mut(items, 2000, sizeof(shown)))->kind = -7;
    tide_draw_reset(&d);
    numbers = tide_draw_list_data(&d, items, sizeof(shown), 4, 0, shown_words, &held); // Its place, among the frame's
    TIDE_REQUIRE(numbers != 0);
    t = &d.textures[numbers - 1u];
    ints = (const int32_t *)(const void *)t->pixels;
    TIDE_CHECK(areas_since(t, was, &area) == 1 && is_area(area, 2000 - 1024, 1, 1, 1) && ints[2000 * 4 + 3] == -7);
    was = t->version;

    // Elements of two chunks, and a run of them across two rows
    ((shown *)tide_list_at_mut(items, 5, sizeof(shown)))->kind = -1;
    for (int32_t i = 3070; i < 3075; i++) ((shown *)tide_list_at_mut(items, i, sizeof(shown)))->kind = -2;
    tide_draw_reset(&d);
    numbers = tide_draw_list_data(&d, items, sizeof(shown), 4, 0, shown_words, &held);
    TIDE_REQUIRE(numbers != 0);
    t = &d.textures[numbers - 1u];
    TIDE_REQUIRE(tide_draw_texture_since(t, was, &areas, &count) && count == 2);
    TIDE_CHECK(is_area(areas[0], 5, 0, 1, 1) && is_area(areas[1], 0, 2, 1024, 2));
    was = t->version;

    // A shorter list: zeros where its last elements were
    tide_list_resize(&items, 4998, sizeof(shown), TIDE_IN_LOCAL);
    tide_draw_reset(&d);
    numbers = tide_draw_list_data(&d, items, sizeof(shown), 4, 0, shown_words, &held);
    TIDE_REQUIRE(numbers != 0);
    t = &d.textures[numbers - 1u];
    ints = (const int32_t *)(const void *)t->pixels;
    TIDE_CHECK(held == 4998 && areas_since(t, was, &area) == 1 && is_area(area, 4998 - 4096, 4, 2, 1));
    TIDE_CHECK(ints[4998 * 4] == 0 && ints[4999 * 4 + 3] == 0 && ints[4997 * 4 + 3] == 4997);

    // The same list as other numbers of it is another texture, with nothing known of it yet
    const uint32_t kinds = tide_draw_list_data(&d, items, sizeof(shown), 1, 9, shown_kind, &held);
    TIDE_REQUIRE(kinds != 0 && kinds != numbers && held == 4998);
    t = &d.textures[kinds - 1u];
    TIDE_CHECK(t->width == 1024 && t->height == 2 && ((const int32_t *)(const void *)t->pixels)[2000] == -7);
    // ...and with none of its numbers read, it's only counted
    TIDE_CHECK(tide_draw_list_data(&d, items, sizeof(shown), 0, 9, NULL, &held) == 0 && held == 4998);

    tide_draw_free(&d);
    tide_list_release(&items, TIDE_IN_LOCAL);
    tide_grid_release(&grid, &colors, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

static void int_words(const void *element, int32_t *words)
{
    memcpy(words, element, sizeof(int32_t));
}

// A kept texture against the same thing packed from nothing: the same size
// and the same pixels. And for a renderer that has `before`, the pixels the
// texture had at `had` (NULL: it has none): the areas the list says changed
// are all that differs.
static bool as_made_anew(const tide_draw_texture *kept, const tide_draw_texture *anew, uint8_t *before, const uint64_t had)
{
    if (!kept || !anew) return !kept && !anew;
    const size_t pixel = tide_texels_bytes(kept->format), row = (size_t)kept->width * pixel;
    if (kept->width != anew->width || kept->height != anew->height || kept->format != anew->format) return false;
    if (memcmp(kept->pixels, anew->pixels, row * (size_t)kept->height) != 0) return false;
    const tide_draw_area *areas;
    uint32_t count;
    if (!before || !tide_draw_texture_since(kept, had, &areas, &count)) return true;
    for (uint32_t i = 0; i < count; i++) {
        for (int32_t y = areas[i].y; y < areas[i].y + areas[i].height; y++) {
            const size_t at = (size_t)y * row + (size_t)areas[i].x * pixel;
            memcpy(before + at, kept->pixels + at, (size_t)areas[i].width * pixel);
        }
    }
    return memcmp(before, kept->pixels, row * (size_t)kept->height) == 0;
}

// Whatever a game does to a grid or a list between two frames, the texture
// the list keeps and changes in place is the one it would make from nothing,
// and what it says changed is all that did.
TIDE_TEST(draw_textures_kept_are_the_textures_made_anew)
{
    static const tide_grid_shape colors = {sizeof(tide_color), 2u, {5u, 5u, 0u}};
    static const tide_grid_shape ints = {sizeof(int32_t), 3u, {4u, 4u, 2u}};
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    tide_grid paint = {0}, blocks = {0};
    tide_grid_set(&paint, tide_grid_new(2, 90, 70, 0), &colors, TIDE_IN_LOCAL);
    tide_grid_set(&blocks, tide_grid_new(3, 40, 0, 9), &ints, TIDE_IN_LOCAL); // No size along y: its chunks say where it is
    tide_list items = {0};
    for (int32_t i = 0; i < 3000; i++) *(shown *)tide_list_add(&items, sizeof(shown), TIDE_IN_LOCAL) = (shown){{1.0f, 2.0f, 3.0f}, i};

    tide_draw_list kept = {0};
    uint32_t seed = 12345u;
    uint8_t *before[3] = {NULL, NULL, NULL};
    size_t room[3] = {0, 0, 0};
    uint64_t had[3] = {0, 0, 0};
    bool has[3] = {false, false, false};
    int wrong = 0, parts = 0;
    for (int step = 0; step < 240 && !wrong; step++) {
        // A few changes, of which some change nothing, and now and then a lot
        const int changes = step % 7 == 0 ? 0 : step % 31 == 0 ? 400 : 1 + (int)(seed >> 28);
        for (int c = 0; c < changes; c++) {
            seed = seed * 1664525u + 1013904223u;
            const uint32_t pick = seed >> 8;
            const int32_t value = pick % 5u == 0 ? 0 : (int32_t)(pick % 251u);
            switch (pick % 3u) {
            case 0:
                *(tide_color *)tide_grid_poke(&paint, (int32_t)(pick / 3u % 90u), (int32_t)(pick / 270u % 70u), 0, &colors, true,
                                              TIDE_IN_LOCAL) = (tide_color){(float)value / 251.0f, 0.5f, 0.25f, 1.0f};
                break;
            case 1:
                *(int32_t *)tide_grid_poke(&blocks, (int32_t)(pick / 3u % 40u), (int32_t)(pick / 120u % 100u) - 40,
                                           (int32_t)(pick / 12000u % 9u), &ints, true, TIDE_IN_LOCAL) = value;
                break;
            default:
                ((shown *)tide_list_at_mut(items, (int32_t)(pick / 3u % (uint32_t)tide_list_count(items)), sizeof(shown)))->kind = value;
                break;
            }
        }
        if (step % 53 == 52) tide_grid_clear(&blocks, &ints, TIDE_IN_LOCAL);
        if (step % 61 == 60) tide_grid_clear(&paint, &colors, TIDE_IN_LOCAL);
        if (step % 17 == 16) tide_list_resize(&items, 2000 + (int32_t)(seed >> 22), sizeof(shown), TIDE_IN_LOCAL);

        tide_draw_reset(&kept);
        tide_draw_list anew = {0};
        int32_t info[TIDE_GRID3_DATA_INTS], count;
        const uint32_t now[3] = {tide_draw_grid_texture(&kept, paint, &colors),
                                 tide_draw_grid_data(&kept, blocks, &ints, 1, 0, int_words, info),
                                 tide_draw_list_data(&kept, items, sizeof(shown), 4, 0, shown_words, &count)};
        const uint32_t made[3] = {tide_draw_grid_texture(&anew, paint, &colors),
                                  tide_draw_grid_data(&anew, blocks, &ints, 1, 0, int_words, info),
                                  tide_draw_list_data(&anew, items, sizeof(shown), 4, 0, shown_words, &count)};
        for (int k = 0; k < 3; k++) {
            const tide_draw_texture *t = now[k] ? &kept.textures[now[k] - 1u] : NULL;
            const tide_draw_texture *other = made[k] ? &anew.textures[made[k] - 1u] : NULL;
            const size_t bytes = t ? (size_t)t->width * (size_t)t->height * tide_texels_bytes(t->format) : 0;
            // What a renderer that drew the last frame has only stands for this texture while it's the same size
            const bool same = t && has[k] && bytes == room[k];
            const tide_draw_area *areas;
            uint32_t area_count;
            parts += same && tide_draw_texture_since(t, had[k], &areas, &area_count);
            if (!as_made_anew(t, other, same ? before[k] : NULL, had[k])) wrong = 1 + step * 3 + k;
            has[k] = t != NULL;
            if (!t) continue;
            before[k] = tide_realloc(before[k], room[k], bytes);
            memcpy(before[k], t->pixels, bytes);
            room[k] = bytes;
            had[k] = t->version;
        }
        tide_draw_free(&anew);
    }
    TIDE_CHECK(wrong == 0);
    TIDE_CHECK(parts > 300); // Most frames, for most of the three, only the areas that changed were all it took

    for (int k = 0; k < 3; k++) free(before[k]);
    tide_draw_free(&kept);
    tide_list_release(&items, TIDE_IN_LOCAL);
    tide_grid_release(&paint, &colors, TIDE_IN_LOCAL);
    tide_grid_release(&blocks, &ints, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// ---------------------------------------------------------------------------
// 3D

// Where a world position lands through the list's 3D camera `i`, from -1 to 1
// across the screen each way, and its depth.
static tide_float3 through(const tide_draw_list *d, const uint32_t i, const tide_float3 p)
{
    const tide_float4 clip = tide_mul_f4x4_f4(d->cameras_3d[i].matrix, tide_f4(p.x, p.y, p.z, 1.0f));
    return tide_f3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
}

static bool about(const float a, const float b)
{
    return a - b < 1e-4f && b - a < 1e-4f;
}

// The camera looks along its rotation's +z, with +x to the right of the
// screen and +y up, as Unity's do, from 0.3 units in front of it on.
TIDE_TEST(draw_cameras_3d)
{
    tide_draw_list d = {0};
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    // The list has it, and records nothing until something draws through it
    TIDE_REQUIRE(d.count == 0 && d.camera_3d_count == 1 && d.place.camera_3d == 1 && d.cameras_3d[0].fit == 1);
    tide_float3 p = through(&d, 0, tide_f3(0.0f, 0.0f, 0.0f));
    TIDE_CHECK(about(p.x, 0.0f) && about(p.y, 0.0f) && p.z > 0.0f && p.z < 1.0f);
    p = through(&d, 0, tide_f3(1.0f, 2.0f, 0.0f));
    TIDE_CHECK(about(p.x, 0.2f) && about(p.y, 0.4f));
    TIDE_CHECK(about(through(&d, 0, tide_f3(0.0f, 0.0f, -4.7f)).z, -1.0f)); // The near side
    TIDE_CHECK(through(&d, 0, tide_f3(0.0f, 0.0f, 1e5f)).z < 1.0f);         // ...and no far one
    TIDE_CHECK(through(&d, 0, tide_f3(0.0f, 0.0f, 10.0f)).z > p.z);         // Farther is deeper

    // Turned a quarter around y, it looks along +x, its right along -z
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), TIDE_PI_F * 0.5f), 90.0f);
    p = through(&d, 1, tide_f3(5.0f, 0.0f, -1.0f));
    TIDE_CHECK(about(p.x, 0.2f) && about(p.y, 0.0f) && p.z > 0.0f);

    // Any projection, from where the camera is
    const tide_float4x4 at = tide_trs_f4x4(tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), tide_f3_splat(1.0f));
    tide_draw_set_camera_matrices(&d, at, tide_perspectivefov_f4x4(TIDE_PI_F * 0.5f, 2.0f, 0.3f, 100.0f));
    TIDE_REQUIRE(d.camera_3d_count == 3 && d.cameras_3d[2].fit == 0);
    p = through(&d, 2, tide_f3(1.0f, 2.0f, 0.0f));
    TIDE_CHECK(about(p.x, 0.1f) && about(p.y, 0.4f) && p.z > -1.0f && p.z < 1.0f);
    TIDE_CHECK(about(through(&d, 2, tide_f3(0.0f, 0.0f, 95.0f)).z, 1.0f));
    tide_draw_set_camera_matrices(&d, at, tide_ortho_f4x4(10.0f, 4.0f, 1.0f, 11.0f));
    p = through(&d, 3, tide_f3(5.0f, -2.0f, 1.0f));
    TIDE_CHECK(about(p.x, 1.0f) && about(p.y, -1.0f) && about(p.z, 0.0f));
    tide_draw_free(&d);
}

static bool same_place(const tide_float3 a, const tide_float3 b, const float within)
{
    return tide_abs_f(a.x - b.x) < within && tide_abs_f(a.y - b.y) < within && tide_abs_f(a.z - b.z) < within;
}

// A point through the destination's world-to-clip matrix, as a screen
// position: where tide_draw_world_to_screen_3d has to put it.
static tide_float3 by_matrix(const tide_draw_list *d, const tide_float3 p, const tide_float2 window, const tide_float2 size)
{
    const tide_float4 clip = tide_mul_f4x4_f4(tide_draw_world_to_clip(d, window), tide_f4(p.x, p.y, p.z, 1.0f));
    return tide_f3((clip.x / clip.w * 0.5f + 0.5f) * size.x, (0.5f - clip.y / clip.w * 0.5f) * size.y,
                   clip.z / clip.w * 0.5f + 0.5f);
}

// The same camera given back as its parts: the matrix tide_draw_set_camera_matrices makes of them.
static bool parts_make_it(tide_draw_list *d, const tide_float2 window)
{
    const tide_float4x4 whole = tide_draw_world_to_clip(d, window);
    const uint32_t mark = tide_draw_push_camera_matrices(d, tide_draw_camera_transform(d, window),
                                                         tide_draw_camera_projection(d, window));
    const tide_float4x4 again = tide_draw_world_to_clip(d, window);
    tide_draw_pop(d, mark);
    const float *a = &whole.c0.x, *b = &again.c0.x;
    for (int i = 0; i < 16; i++) {
        if (tide_abs_f(a[i] - b[i]) > 1e-4f * (1.0f + tide_abs_f(a[i]))) return false;
    }
    return true;
}

// A destination's cameras are read back as matrices: the 3D one from the
// world to clip space, fitted to the destination's shape as a renderer fits
// it, with the parts it's made of, and the 2D one from the world to the screen.
TIDE_TEST(draw_cameras_are_read_as_matrices)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 600.0f), small = tide_f2(64.0f, 16.0f);
    const tide_float3 points[3] = {{0.5f, 1.0f, 4.0f}, {-2.0f, 0.25f, 9.0f}, {3.0f, -1.5f, 2.0f}};

    // Each frame's first camera, then one that's set, then one of any projection: the window's shape is in each
    for (int camera = 0; camera < 3; camera++) {
        if (camera == 1) tide_draw_set_camera_3d(&d, tide_f3(1.0f, 2.0f, -6.0f), tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), 0.3f), 70.0f);
        if (camera == 2) {
            tide_draw_set_camera_matrices(&d, tide_trs_f4x4(tide_f3(0.0f, 1.0f, -8.0f), tide_identity_q(), tide_f3_splat(1.0f)),
                                          tide_ortho_f4x4(16.0f, 12.0f, 0.5f, 40.0f));
        }
        for (int i = 0; i < 3; i++) {
            TIDE_CHECK(same_place(by_matrix(&d, points[i], window, window), tide_draw_world_to_screen_3d(&d, points[i], window), 1e-3f));
        }
        TIDE_CHECK(parts_make_it(&d, window));
    }
    // A camera of any projection is given back as it was given, bit for bit
    const tide_float4x4 sees = tide_ortho_f4x4(16.0f, 12.0f, 0.5f, 40.0f), given = tide_draw_camera_projection(&d, window);
    TIDE_CHECK(memcmp(&sees, &given, sizeof sees) == 0);
    TIDE_CHECK(tide_draw_camera_transform(&d, window).c3.z == -8.0f);
    // ...and nothing was recorded for the asking
    TIDE_CHECK(d.count == 0);

    // A fitted camera's matrix is the square one with x divided by the destination's shape
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    const tide_float4x4 square = tide_draw_camera_3d_matrix(tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    const tide_float4x4 wide = tide_draw_world_to_clip(&d, window);
    TIDE_CHECK(wide.c0.x == square.c0.x * (600.0f / 800.0f) && wide.c1.y == square.c1.y && wide.c3.z == square.c3.z);
    const tide_float4x4 fitted = tide_draw_camera_fit(square, 800.0f, 600.0f);
    TIDE_CHECK(memcmp(&wide, &fitted, sizeof wide) == 0);
    TIDE_CHECK(about(tide_draw_camera_projection(&d, window).c0.x, 0.75f) && about(tide_draw_camera_projection(&d, window).c1.y, 1.0f));
    // A transform is no part of a camera
    tide_draw_set_transform_3d(&d, tide_translate_f4x4(tide_f3(5.0f, 0.0f, 0.0f)));
    const tide_float4x4 moved = tide_draw_world_to_clip(&d, window);
    TIDE_CHECK(memcmp(&wide, &moved, sizeof wide) == 0);

    // A target has its own, fitted to its own shape, whatever the window's
    tide_draw_to(&d, 17, small.x, small.y);
    for (int i = 0; i < 3; i++) {
        TIDE_CHECK(same_place(by_matrix(&d, points[i], window, small), tide_draw_world_to_screen_3d(&d, points[i], window), 1e-3f));
    }
    TIDE_CHECK(about(tide_draw_camera_projection(&d, window).c0.x, 0.25f * tide_draw_first_camera_3d().projection.c0.x));
    tide_draw_set_camera_matrices(&d, tide_identity_f4x4(), sees);
    const tide_float4x4 target_sees = tide_draw_camera_projection(&d, window);
    TIDE_CHECK(memcmp(&sees, &target_sees, sizeof sees) == 0);
    TIDE_CHECK(parts_make_it(&d, window));
    tide_draw_to(&d, 0, 0, 0);
    const tide_float4x4 back = tide_draw_world_to_clip(&d, window);
    TIDE_CHECK(memcmp(&wide, &back, sizeof wide) == 0);
    // No size, no matrix
    TIDE_CHECK(tide_draw_world_to_clip(&d, tide_f2(0.0f, 600.0f)).c0.x == 0.0f
               && tide_draw_world_to_clip(&d, tide_f2(0.0f, 600.0f)).c3.w == 0.0f);

    // The 2D camera: the same place as tide_draw_world_to_screen gives
    const tide_float2 spots[3] = {{2.0f, 3.0f}, {-40.0f, 7.5f}, {0.0f, 0.0f}};
    for (int camera = 0; camera < 3; camera++) {
        if (camera == 1) tide_draw_set_camera(&d, tide_f2(2.0f, 3.0f), 5.0f);
        if (camera == 2) tide_draw_set_screen(&d);
        const tide_float3x3 m = tide_draw_world_to_screen_matrix(&d, window);
        for (int i = 0; i < 3; i++) {
            const tide_float3 by = tide_mul_f3x3_f3(m, tide_f3(spots[i].x, spots[i].y, 1.0f));
            const tide_float2 to = tide_draw_world_to_screen(&d, spots[i], window);
            TIDE_CHECK(tide_abs_f(by.x - to.x) < 1e-3f && tide_abs_f(by.y - to.y) < 1e-3f && by.z == 1.0f);
        }
        const tide_float3x3 same = tide_identity_f3x3();
        TIDE_CHECK(camera != 2 || memcmp(&m, &same, sizeof m) == 0);
    }
    TIDE_CHECK(tide_draw_world_to_screen_matrix(&d, tide_f2(0.0f, 0.0f)).c2.z == 0.0f);
    tide_draw_free(&d);
}

static const tide_vertex3 cube_corners[4] = {
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    {{0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};
static const uint32_t cube_indices[9] = {0, 1, 2, 0, 2, 3, 0, 1, 9}; // The last triangle's past the corners

// A mesh is copied once, and its instances drawn one after another are one
// command.
TIDE_TEST(draw_meshes_3d_are_kept_and_instanced)
{
    tide_draw_list d = {0};
    tide_mesh mesh = {cube_corners, 4, cube_indices, 9, 1};
    for (int i = 0; i < 3; i++) {
        tide_draw_mesh_3d(&d, &mesh, tide_translate_f4x4(tide_f3((float)i, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
    }
    static const uint8_t pixel[4] = {1, 2, 3, 4};
    const tide_texture texture = {pixel, 1, 1, 7};
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), &texture, TIDE_FILTER_POINT); // Another texture: a run of its own
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), &texture, TIDE_FILTER_POINT); // After a camera too
    TIDE_REQUIRE(d.count == 4 && d.mesh_count == 1 && d.texture_count == 1);
    const tide_draw_command *c = &d.commands[0];
    TIDE_CHECK(c->kind == TIDE_DRAW_MESH_3D && c->mesh3.mesh == 1 && c->mesh3.count == 3 && c->mesh3.texture == 0);
    TIDE_CHECK(d.matrices[c->mesh3.first + 2].c3.x == 2.0f);
    TIDE_CHECK(d.commands[1].mesh3.count == 1 && d.commands[1].mesh3.texture == 1);
    TIDE_CHECK(d.commands[1].mesh3.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(d.commands[3].kind == TIDE_DRAW_MESH_3D && d.commands[3].mesh3.mesh == 1);
    const tide_draw_mesh_data *m = &d.meshes[0];
    TIDE_REQUIRE(m->space == TIDE_MESH_MEMORY && m->vertex_count == 4 && m->index_count == 6);
    TIDE_CHECK(m->vertices[3].position.z == 1.0f && m->indices[5] == 3);

    // The same version isn't copied again, in this frame or the next
    d.meshes[0].vertices[0].position.x = 77.0f;
    tide_draw_reset(&d);
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.mesh_count == 1 && d.meshes[0].vertices[0].position.x == 77.0f);

    // Another version takes another place, and what the frame drew before
    // stays as it was
    mesh.version = 2;
    mesh.index_count = 3;
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 2 && d.mesh_count == 2);
    TIDE_CHECK(d.commands[1].mesh3.mesh == 2 && d.meshes[1].index_count == 3 && d.meshes[0].index_count == 6);

    // A frame that doesn't draw one lets it go, and its place goes to the next
    tide_draw_reset(&d);
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    tide_draw_reset(&d);
    TIDE_CHECK(d.meshes[0].space == TIDE_MESH_NONE && d.meshes[0].vertices == NULL && d.meshes[1].space == TIDE_MESH_MEMORY);
    mesh.version = 3;
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.mesh_count == 2 && d.commands[0].mesh3.mesh == 1 && d.meshes[0].version == 3);

    // A mesh with no triangles draws nothing
    const tide_mesh none = {cube_corners, 4, cube_indices + 6, 3, 1};
    tide_draw_mesh_3d(&d, &none, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 1);
    TIDE_CHECK(tide_mesh_version(&mesh) == tide_mesh_version(&mesh) && tide_mesh_version(&mesh) != tide_mesh_version(&none));
    tide_draw_free(&d);
}

// Many meshes are each found again, frame after frame, without a copy.
TIDE_TEST(draw_meshes_3d_many)
{
    enum { MESHES = 3000 };
    static tide_vertex3 corners[MESHES][3];
    static const uint32_t triangle[3] = {0, 1, 2};
    tide_draw_list d = {0};
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&d);
        for (int i = 0; i < MESHES; i++) {
            corners[i][1].position.x = (float)i;
            const tide_mesh mesh = {corners[i], 3, triangle, 3, (uint64_t)i};
            tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
        }
        TIDE_CHECK(d.mesh_count == MESHES && d.count == MESHES);
    }
    bool same = true;
    for (int i = 0; i < MESHES; i++) {
        same = same && d.meshes[d.commands[i].mesh3.mesh - 1u].vertices[1].position.x == (float)i;
    }
    TIDE_CHECK(same);
    tide_draw_free(&d);
}

// Tide's lists are meshes the list keeps: one that doesn't change isn't
// copied again, one that does is, and lists made along the way, which take
// the same places in the scratch area again, are told apart by what's in them.
TIDE_TEST(draw_lists_are_meshes_3d)
{
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    tide_list vertices = {0}, big = {0};
    for (int i = 0; i < 4; i++) {
        *(tide_vertex3 *)tide_list_add(&vertices, sizeof(tide_vertex3), TIDE_IN_LOCAL) = cube_corners[i];
    }
    for (int i = 0; i < 2000; i++) { // In chunks
        tide_vertex3 *v = tide_list_add(&big, sizeof(tide_vertex3), TIDE_IN_LOCAL);
        v->position.x = (float)i;
    }
    static const int32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const tide_float4x4 at = tide_identity_f4x4();
    tide_draw_list d = {0};
    tide_draw_mesh3_lists(&d, vertices, tide_list_from(indices, 6, sizeof(int32_t)), at);
    tide_draw_mesh3_lists(&d, big, tide_list_from(indices, 6, sizeof(int32_t)), at);
    TIDE_REQUIRE(d.count == 2 && d.mesh_count == 2);
    TIDE_CHECK(d.meshes[0].space == TIDE_MESH_LISTS && d.meshes[0].index_count == 6 && d.meshes[1].vertex_count == 2000);
    TIDE_CHECK(d.meshes[1].vertices[1999].position.x == 1999.0f);
    d.meshes[0].vertices[0].position.x = 77.0f;
    d.meshes[1].vertices[0].position.x = 77.0f;

    for (int frame = 0; frame < 2; frame++) {
        tide_draw_reset(&d);
        tide_scratch_reset(mark);
        tide_draw_mesh3_lists(&d, vertices, tide_list_from(indices, 6, sizeof(int32_t)), at);
        tide_draw_mesh3_lists(&d, big, tide_list_from(indices, 6, sizeof(int32_t)), at);
        TIDE_CHECK(d.mesh_count == 2 && d.meshes[0].vertices[0].position.x == 77.0f);
        TIDE_CHECK(d.meshes[1].vertices[0].position.x == 77.0f);
    }

    // Changed, through the list's chunk
    ((tide_vertex3 *)tide_list_at_mut(big, 1500, sizeof(tide_vertex3)))->position.y = 5.0f;
    tide_draw_reset(&d);
    tide_scratch_reset(mark);
    tide_draw_mesh3_lists(&d, big, tide_list_from(indices, 6, sizeof(int32_t)), at);
    TIDE_REQUIRE(d.count == 1);
    const tide_draw_mesh_data *m = &d.meshes[d.commands[0].mesh3.mesh - 1u];
    TIDE_CHECK(m->vertices[0].position.x == 0.0f && m->vertices[1500].position.y == 5.0f);

    // Two lists made along the way, at the same place in the scratch area
    tide_draw_reset(&d);
    tide_scratch_reset(mark);
    tide_draw_mesh3_lists(&d, tide_list_from(cube_corners, 3, sizeof(tide_vertex3)),
                          tide_list_from(indices, 3, sizeof(int32_t)), at);
    tide_scratch_reset(mark);
    tide_draw_mesh3_lists(&d, tide_list_from(cube_corners + 1, 3, sizeof(tide_vertex3)),
                          tide_list_from(indices, 3, sizeof(int32_t)), at);
    TIDE_REQUIRE(d.count == 2);
    TIDE_CHECK(d.commands[0].mesh3.mesh != d.commands[1].mesh3.mesh);
    TIDE_CHECK(d.meshes[d.commands[0].mesh3.mesh - 1u].vertices[0].position.x == 0.0f);
    TIDE_CHECK(d.meshes[d.commands[1].mesh3.mesh - 1u].vertices[0].position.x == 1.0f);

    // ...and the same indices made again at other places, as a view does for
    // each entity, are one mesh's instances
    tide_draw_reset(&d);
    for (int i = 0; i < 3; i++) tide_draw_mesh3_lists(&d, vertices, tide_list_from(indices, 6, sizeof(int32_t)), at);
    TIDE_CHECK(d.count == 1 && d.commands[0].mesh3.count == 3);

    tide_draw_free(&d);
    tide_list_release(&vertices, TIDE_IN_LOCAL);
    tide_list_release(&big, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// A list that forgot its meshes copies them again, and its commands draw
// nothing of what it forgot; appended, a list's 3D commands come along.
TIDE_TEST(draw_meshes_3d_forget_and_append)
{
    const tide_mesh mesh = {cube_corners, 4, cube_indices, 6, 1};
    tide_draw_list d = {0}, world = {0};
    tide_draw_set_camera_3d(&d, tide_f3(1.0f, 2.0f, 3.0f), tide_identity_q(), 60.0f);
    tide_draw_mesh_3d(&d, &mesh, tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh_3d(&d, &mesh, tide_translate_f4x4(tide_f3(2.0f, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);

    tide_draw_clear(&world, TIDE_COLOR_BLACK);
    tide_draw_append(&world, &d);
    TIDE_REQUIRE(world.count == 3 && world.mesh_count == 1 && world.matrix_count == 3);
    TIDE_CHECK(world.commands[1].kind == TIDE_DRAW_CAMERA_3D && world.commands[1].camera.fit == 1);
    TIDE_CHECK(memcmp(&world.matrices[world.commands[1].camera.matrix], &d.matrices[d.commands[0].camera.matrix],
                      sizeof(tide_float4x4)) == 0);
    TIDE_CHECK(world.commands[2].mesh3.count == 2 && world.matrices[world.commands[2].mesh3.first + 1].c3.x == 2.0f);
    TIDE_CHECK(world.meshes[0].index_count == 6);

    tide_draw_forget(&d);
    TIDE_CHECK(d.mesh_count == 0 && d.commands[1].mesh3.mesh == 0);
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.mesh_count == 1 && d.meshes[0].epoch == d.epoch && d.meshes[0].index_count == 6);
    tide_draw_free(&world);
    tide_draw_free(&d);
}

// Explicit instance lists may span heap pages, and join adjacent ordinary
// mesh draws without losing their order or sharing the caller's values.
TIDE_TEST(draw_instance_lists_are_copied_and_batched)
{
    static const tide_grid_shape colors = {sizeof(tide_color), 2u, {5u, 5u, 0u}};
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const uint32_t mark = tide_scratch_mark();
    const tide_list vertices = tide_list_from(cube_corners, 4, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(cube_indices, 6, sizeof(uint32_t));
    tide_grid grid = {0};
    tide_grid_set(&grid, tide_grid_new(2, 1, 1, 0), &colors, TIDE_IN_LOCAL);
    tide_draw_list d = {0}, appended = {0};

    tide_draw_mesh3_instances_lists(&d, vertices, indices, (tide_list){0});
    tide_draw_mesh3_instances_texture(&d, vertices, indices, (tide_list){0}, tide_texture_of_grid(grid, &colors, 1u));
    TIDE_CHECK(d.count == 0 && d.mesh_count == 0 && d.texture_count == 0 && d.matrix_count == 0);

    enum { COUNT = 513 };
    tide_list transforms = {0};
    for (int i = 0; i < COUNT; i++) {
        *(tide_float4x4 *)tide_list_add(&transforms, sizeof(tide_float4x4), TIDE_IN_LOCAL) =
            tide_translate_f4x4(tide_f3((float)i, 2.0f, 3.0f));
    }
    TIDE_CHECK(tide_block_at(transforms.at)->b & TIDE_LIST_CHUNKED);
    tide_draw_mesh3_instances_lists(&d, vertices, indices, transforms);
    TIDE_REQUIRE(d.count == 1 && d.mesh_count == 1 && d.matrix_count == COUNT);
    TIDE_CHECK(d.commands[0].mesh3.count == COUNT && d.matrices[0].c3.x == 0.0f);
    TIDE_CHECK(d.matrices[COUNT / 2].c3.x == (float)(COUNT / 2) && d.matrices[COUNT - 1].c3.x == (float)(COUNT - 1));

    ((tide_float4x4 *)tide_list_at_mut(transforms, 0, sizeof(tide_float4x4)))->c3.x = -1.0f;
    TIDE_CHECK(d.matrices[0].c3.x == 0.0f);
    tide_draw_mesh3_lists(&d, vertices, indices, tide_translate_f4x4(tide_f3((float)COUNT, 0.0f, 0.0f)));
    tide_draw_mesh3_instances_lists(&d, vertices, indices, transforms);
    TIDE_REQUIRE(d.count == 1 && d.mesh_count == 1 && d.commands[0].mesh3.count == 2 * COUNT + 1);
    TIDE_CHECK(d.matrices[COUNT].c3.x == (float)COUNT && d.matrices[COUNT + 1].c3.x == -1.0f);
    TIDE_CHECK(d.matrices[2 * COUNT].c3.x == (float)(COUNT - 1));

    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 60.0f);
    tide_draw_mesh3_instances_texture(&d, vertices, indices, transforms, tide_texture_of_grid(grid, &colors, 1u));
    tide_draw_mesh3_instances_lists(&d, vertices, indices, transforms);
    TIDE_REQUIRE(d.count == 4 && d.mesh_count == 1 && d.texture_count == 1);
    TIDE_CHECK(d.commands[2].mesh3.count == COUNT && d.commands[2].mesh3.texture == 1);
    TIDE_CHECK(d.commands[2].mesh3.filter == TIDE_FILTER_POINT);
    TIDE_CHECK(d.commands[3].mesh3.count == COUNT && d.commands[3].mesh3.texture == 0);
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.count == d.count && appended.matrix_count == d.matrix_count && appended.mesh_count == 1);
    TIDE_CHECK(memcmp(appended.matrices, d.matrices, d.matrix_count * sizeof(tide_float4x4)) == 0);

    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_list_release(&transforms, TIDE_IN_LOCAL);
    tide_grid_release(&grid, &colors, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// A packed instance run keeps every value, copies its uniforms once, and
// merges with ordinary shader draws only while the shared inputs match.
TIDE_TEST(draw_shaded_instances_are_copied_and_batched)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list vertices = tide_list_from(cube_corners, 4, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(cube_indices, 6, sizeof(uint32_t));
    static const tide_shader shader = {
        .vertex_size = sizeof(tide_vertex3), .instance_floats = 1, .instance_ints = 1,
        .uniform_floats = 1, .texture_count = 1,
    };
    struct payload { float f[4]; int32_t i[4]; } instances[3] = {
        {{1.0f, 2.0f, 3.0f, 4.0f}, {5, 6, 7, 8}},
        {{9.0f, 10.0f, 11.0f, 12.0f}, {13, 14, 15, 16}},
        {{17.0f, 18.0f, 19.0f, 20.0f}, {21, 22, 23, 24}},
    };
    float uniforms[4] = {7.0f, 8.0f, 9.0f, 10.0f};
    const float original_uniforms[4] = {7.0f, 8.0f, 9.0f, 10.0f};
    tide_draw_list d = {0}, appended = {0};
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, NULL, 0, NULL, NULL, 0);
    TIDE_CHECK(d.count == 0 && d.mesh_count == 0 && d.instance_used == 0 && d.uniform_used == 0);
    uint8_t *pixels;
    const uint32_t texture = tide_draw_texture_slot(&d, TIDE_PIXELS_MEMORY, 1, 1, 1, 1, &pixels);
    memset(pixels, 255, 4);
    const uint32_t textures[TIDE_SHADER_TEXTURES] = {texture};
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, instances, 3, uniforms, textures, 0);
    TIDE_REQUIRE(d.count == 1 && d.mesh_count == 1 && d.run_count == 1 && d.runs[0].count == 3);
    TIDE_CHECK(d.instance_used == sizeof instances && d.uniform_used == sizeof uniforms);
    TIDE_CHECK(memcmp(d.instances, instances, sizeof instances) == 0);
    instances[1].f[0] = 99.0f;
    uniforms[0] = 100.0f;
    TIDE_CHECK(((const struct payload *)(const void *)d.instances)[1].f[0] == 9.0f);
    TIDE_CHECK(memcmp(d.uniforms, original_uniforms, sizeof uniforms) == 0);

    tide_draw_mesh_shaded(&d, &shader, vertices, indices, &instances[0], original_uniforms, textures, 0);
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, instances + 1, 2, original_uniforms, textures, 0);
    TIDE_REQUIRE(d.count == 1 && d.mesh_count == 1 && d.runs[0].count == 6);
    TIDE_CHECK(d.uniform_used == sizeof uniforms && d.instance_used == 2 * sizeof instances);
    TIDE_CHECK(memcmp(d.instances + 3 * sizeof instances[0], instances, sizeof instances) == 0);

    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, instances, 3, uniforms, textures, 0);
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, instances, 3, uniforms, textures, 1);
    tide_draw_clear(&d, TIDE_COLOR_BLACK);
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, instances, 3, uniforms, textures, 1);
    TIDE_REQUIRE(d.count == 5 && d.run_count == 4 && d.mesh_count == 1);
    TIDE_CHECK(d.runs[1].count == 3 && d.runs[2].count == 3 && d.runs[3].count == 3);
    TIDE_CHECK(d.uniform_used == 4 * sizeof uniforms);
    tide_draw_append(&appended, &d);
    TIDE_REQUIRE(appended.count == d.count && appended.run_count == d.run_count && appended.mesh_count == 1);
    TIDE_CHECK(appended.runs[0].count == 6 && appended.instance_used == d.instance_used);
    TIDE_CHECK(memcmp(appended.instances, d.instances, d.instance_used) == 0);
    TIDE_CHECK(memcmp(appended.uniforms, d.uniforms, d.uniform_used) == 0);

    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_scratch_reset(mark);
}

// Even when a shader reads no instance data, an explicit count still draws
// that many copies, while empty or invalid meshes draw none.
TIDE_TEST(draw_shaded_instances_without_payload)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list vertices = tide_list_from(cube_corners, 4, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(cube_indices, 6, sizeof(uint32_t));
    static const tide_shader shader = {.vertex_size = sizeof(tide_vertex3)};
    tide_draw_list d = {0};
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, NULL, 5, NULL, NULL, 0);
    tide_draw_mesh_shaded(&d, &shader, vertices, indices, NULL, NULL, NULL, 0);
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, indices, NULL, 7, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 1 && d.run_count == 1 && d.runs[0].count == 13);
    TIDE_CHECK(d.instance_used == 0 && d.uniform_used == 0);
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, (tide_list){0}, NULL, 3, NULL, NULL, 0);
    tide_draw_mesh_shaded_instances(&d, &shader, (tide_list){0}, indices, NULL, 3, NULL, NULL, 0);
    const tide_list invalid = tide_list_from(cube_indices + 6, 3, sizeof(uint32_t));
    tide_draw_mesh_shaded_instances(&d, &shader, vertices, invalid, NULL, 3, NULL, NULL, 0);
    TIDE_CHECK(d.count == 1 && d.runs[0].count == 13);
    tide_draw_free(&d);
    tide_scratch_reset(mark);
}

// Equivalent shader descriptors share their generated id, so automatic and
// explicit draws can join. C descriptors without an id still use identity.
TIDE_TEST(draw_shaded_instances_merge_by_shader_id)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list vertices = tide_list_from(cube_corners, 4, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(cube_indices, 6, sizeof(uint32_t));
    const tide_shader first = {.id = 7, .vertex_size = sizeof(tide_vertex3)};
    tide_shader same = first, different = first, unnamed = first;
    different.id = 8;
    unnamed.id = 0;
    const tide_shader another_unnamed = unnamed;
    tide_draw_list d = {0};
    tide_draw_mesh_shaded_instances(&d, &first, vertices, indices, NULL, 2, NULL, NULL, 0);
    tide_draw_mesh_shaded_instances(&d, &same, vertices, indices, NULL, 3, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 1 && d.run_count == 1 && d.runs[0].count == 5);
    tide_draw_mesh_shaded_instances(&d, &different, vertices, indices, NULL, 2, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 2 && d.runs[1].count == 2);
    tide_draw_mesh_shaded(&d, &unnamed, vertices, indices, NULL, NULL, NULL, 0);
    tide_draw_mesh_shaded(&d, &unnamed, vertices, indices, NULL, NULL, NULL, 0);
    tide_draw_mesh_shaded(&d, &another_unnamed, vertices, indices, NULL, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 4 && d.run_count == 4);
    TIDE_CHECK(d.runs[2].count == 2 && d.runs[3].count == 1);

    // Forgotten runs have no shader descriptor left to compare.
    tide_draw_forget(&d);
    tide_draw_mesh_shaded(&d, &first, vertices, indices, NULL, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 5 && d.run_count == 5 && d.runs[4].count == 1);
    tide_draw_free(&d);
    tide_scratch_reset(mark);
}
