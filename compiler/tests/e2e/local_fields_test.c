#include <string.h>

#include "local_fields_same.h"

// Local fields of the match's components and singletons: this machine's,
// outside the match. One machine here, with its worlds made by hand: the one
// its views draw, and the last one the server confirmed where a test has one.
// local_fields_net_test.c has machines on a network.

static tide_world world;
static tide_world verified;
static tide_world base;
static tide_local local;
static tide_local other;
static tide_draw_list draw;
static tide_gui gui;

// The scene is slot 0; then the boxes, by their sides
#define BOX4 ((tide_entity){1, 1}) // With a Label
#define BOX3 ((tide_entity){2, 1}) // With a Glow
#define BOX5 ((tide_entity){3, 1}) // With both

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
}

static void frame_on(tide_local *l, const tide_world *w, const tide_world *confirmed)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, confirmed, NULL, 1.0f, l, &draw, &gui);
    tide_text_use(NULL, &l->heap); // For the lists the checks read
}

static void frame(void)
{
    frame_on(&local, &world, NULL);
}

static bool text_is(const tide_local *l, const tide_text text, const char *want)
{
    const tide_str s = tide_text_read(&l->heap, text);
    return s.bytes == (int32_t)strlen(want) && memcmp(s.ptr, want, strlen(want)) == 0;
}

TIDE_TEST(local_fields_bake_once_and_draw)
{
    start();
    TIDE_CHECK(!tide_has_Box_local(&local, BOX4));
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).age == 0.5f); // Nothing written: the defaults
    frame();
    const Box_local four = tide_get_Box_local(&local, BOX4);
    const Box_local three = tide_get_Box_local(&local, BOX3);
    TIDE_CHECK(tide_has_Box_local(&local, BOX4) && tide_has_Box_local(&local, BOX3) && tide_has_Box_local(&local, BOX5));
    TIDE_CHECK(four.bakes == 1 && tide_list_count(four.verts) == 4 && tide_list_count(four.faces) == 4);
    TIDE_CHECK(three.bakes == 1 && tide_list_count(three.verts) == 3);
    TIDE_CHECK(four.age == 1.5f);
    TIDE_CHECK(local.Stats.corners == 12); // What functions read through the component
    // Each box drew its own mesh, from its local fields
    uint32_t meshes = 0;
    for (uint32_t i = 0; i < draw.count; i++) meshes += draw.commands[i].kind == TIDE_DRAW_MESH_3D;
    TIDE_CHECK(meshes == 3);

    frame();
    frame();
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).bakes == 1); // Baked once
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).age == 3.5f); // Written in place, frame after frame
    TIDE_CHECK(local.Stats.corners == 36);
    TIDE_CHECK(local.tide_fields_Box.rows.count == 3);
}

// The match's struct has no local fields, and frames that write them leave the
// world as it was.
TIDE_TEST(local_fields_are_outside_the_match)
{
    TIDE_CHECK(sizeof(Box) == sizeof(tide_float3) + sizeof(int32_t));
    TIDE_CHECK(sizeof(Label) == sizeof(int32_t) && sizeof(Arena) == sizeof(float));
    start();
    tide_world_tick(&world);
    const uint64_t hash = tide_world_hash(&world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *before = malloc(size);
    uint8_t *after = malloc(size);
    tide_world_pack(&world, before, size);
    local.Stats.pulsing = true;
    for (int i = 0; i < 3; i++) frame();
    TIDE_CHECK(tide_world_pack(&world, NULL, 0) == size);
    tide_world_pack(&world, after, size);
    TIDE_CHECK(memcmp(before, after, size) == 0 && tide_world_hash(&world) == hash);
    free(before);
    free(after);
}

static void frames_between(const tide_world *w)
{
    local.Stats.pulsing = true;
    for (int i = 0; i < 2; i++) frame_on(&local, w, NULL);
}

// The same world as the game without local fields has: its hash, its bytes,
// what a joining player downloads and a delta (local_fields_same.h)
TIDE_TEST(local_fields_match_is_the_same)
{
    tide_local_init(&local);
    same_match_check(&world, &base, frames_between);
    TIDE_CHECK(local.tide_fields_Box.rows.count == 3 && local.Stats.forgotten == 1);
}

// Entities nothing was written for take no memory, and read as the defaults.
TIDE_TEST(local_fields_take_memory_once_written)
{
    start();
    frame();
    TIDE_CHECK(!tide_has_Glow_local(&local, BOX3) && !tide_has_Glow_local(&local, BOX5));
    TIDE_CHECK(local.tide_fields_Glow.rows.count == 0);
    TIDE_CHECK(local.Stats.fresh == 202); // Two Glows and two Labels, all as they start

    // A view that writes some of them: those have a row from then on
    local.Stats.pulsing = true;
    frame();
    TIDE_CHECK(tide_has_Glow_local(&local, BOX3) && !tide_has_Glow_local(&local, BOX5));
    TIDE_CHECK(tide_get_Glow_local(&local, BOX3).pulse == 0.25f && tide_get_Glow_local(&local, BOX3).lit == 1);
    TIDE_CHECK(tide_get_Glow_local(&local, BOX5).pulse == 0.0f);
    frame();
    TIDE_CHECK(tide_get_Glow_local(&local, BOX3).pulse == 0.5f);
    TIDE_CHECK(local.tide_fields_Glow.rows.count == 1);
    TIDE_CHECK(local.Stats.fresh == 405); // The frame it was written in still read the defaults; the next, only the other's
}

// Defaults the heap holds: an entity's own once a view that writes them runs
// for it.
TIDE_TEST(local_fields_defaults_can_be_text_and_lists)
{
    start();
    frame();
    Label_local one = tide_get_Label_local(&local, BOX4);
    TIDE_CHECK(tide_has_Label_local(&local, BOX4) && tide_has_Label_local(&local, BOX5));
    TIDE_CHECK(text_is(&local, one.text, "new") && tide_list_count(one.marks) == 2);
    TIDE_CHECK(one.text.at != tide_get_Label_local(&local, BOX5).text.at); // Each its own
    local.Stats.pulsing = true;
    frame();
    one = tide_get_Label_local(&local, BOX4);
    const Label_local two = tide_get_Label_local(&local, BOX5);
    TIDE_CHECK(text_is(&local, one.text, "label 1") && text_is(&local, two.text, "label 2"));
    TIDE_CHECK(tide_list_count(one.marks) == 3 && tide_list_count(two.marks) == 3);
    // A local field holds a LocalEntity: the spark each spawned
    TIDE_CHECK(tide_local_entity_count(&local) == 2);
    TIDE_CHECK(tide_get_Spark(&local, one.spark).of == 1 && tide_get_Spark(&local, two.spark).of == 2);
}

TIDE_TEST(local_fields_of_a_singleton)
{
    start();
    frame();
    frame();
    TIDE_CHECK(local.Arena_local.painted == 2);
    const tide_color *cell = tide_grid_read(&local.heap, local.Arena_local.floor, 1, 2, 0, &tide_shape_Grid2_Color);
    TIDE_REQUIRE(cell != NULL);
    TIDE_CHECK(cell->r == 8.0f && cell->a == 1.0f); // Painted from the match's Arena.size, in the local state's heap
    TIDE_CHECK(world.Arena.size == 8.0f);
    // Loops that run at once, a sort and a map, on local fields
    const tide_list heights = local.Arena_local.heights;
    TIDE_REQUIRE(tide_list_count(heights) == 4);
    TIDE_CHECK(*(const float *)tide_list_at(heights, 0, sizeof(float)) == 3.0f);
    TIDE_CHECK(*(const float *)tide_list_at(heights, 3, sizeof(float)) == 6.0f);
    TIDE_CHECK(tide_map_count(local.Arena_local.tally) == 2);
    cell = tide_grid_read(&local.heap, local.Arena_local.floor, 3, 2, 0, &tide_shape_Grid2_Color);
    TIDE_REQUIRE(cell != NULL);
    TIDE_CHECK(cell->g == 2.0f && cell->a == 1.0f);

    // The match goes: they're their defaults again, after their handler ran
    frame_on(&local, NULL, NULL);
    TIDE_CHECK(local.Stats.arenas == 2);
    TIDE_CHECK(local.Arena_local.painted == 0);
    TIDE_CHECK(!tide_grid_read(&local.heap, local.Arena_local.floor, 1, 2, 0, &tide_shape_Grid2_Color));
    TIDE_CHECK(local.tide_fields_Box.rows.count == 0 && local.Stats.forgotten == 3); // And every entity's
    frame_on(&local, NULL, NULL);
    TIDE_CHECK(local.Stats.arenas == 2); // Once
    frame();
    TIDE_CHECK(local.Arena_local.painted == 1);
}

// An entity's local fields go with it, after local handlers of Forgotten ran
// with them, and an entity that takes its slot starts from the defaults.
TIDE_TEST(local_fields_go_with_the_entity)
{
    start();
    frame();
    world.Rules.drop = 3;
    tide_world_tick(&world);
    world.Rules.drop = 0;
    TIDE_REQUIRE(!tide_has_Box(&world, BOX3));
    frame();
    TIDE_CHECK(!tide_has_Box_local(&local, BOX3) && tide_has_Box_local(&local, BOX4));
    TIDE_CHECK(local.Stats.forgotten == 1 && local.Stats.lastBakes == 103); // Its local fields, still there to read
    TIDE_CHECK(tide_entity_equal(local.Stats.last, BOX3));

    world.Rules.spawn = 6;
    tide_world_tick(&world);
    const tide_entity next = {2, 3}; // In the slot the one that went had
    TIDE_REQUIRE(tide_has_Box(&world, next));
    frame();
    const Box_local six = tide_get_Box_local(&local, next);
    TIDE_CHECK(six.bakes == 1 && tide_list_count(six.verts) == 6 && six.age == 1.5f);
    TIDE_CHECK(local.Stats.forgotten == 1);
}

// Losing the component is losing its local fields; the entity's others stay.
TIDE_TEST(local_fields_go_with_the_component)
{
    start();
    local.Stats.pulsing = true;
    frame();
    TIDE_REQUIRE(tide_has_Glow_local(&local, BOX3));
    world.Rules.shed = true;
    tide_world_tick(&world);
    TIDE_REQUIRE(!tide_has_Glow(&world, BOX3) && tide_has_Box(&world, BOX3));
    frame();
    TIDE_CHECK(!tide_has_Glow_local(&local, BOX3));
    TIDE_CHECK(tide_has_Box_local(&local, BOX3) && tide_get_Box_local(&local, BOX3).bakes == 1);
}

// They go when the entity is in neither the world views draw nor the last one
// the server confirmed: a destroy the client predicted wrong takes nothing,
// and a spawn it predicted that never happened leaves nothing.
TIDE_TEST(local_fields_outlast_a_wrong_prediction)
{
    start();
    frame();
    tide_world_copy(&verified, &world);

    // The client predicts the box destroyed; the server hasn't said
    world.Rules.drop = 3;
    tide_world_tick(&world);
    world.Rules.drop = 0;
    TIDE_REQUIRE(!tide_has_Box(&world, BOX3) && tide_has_Box(&verified, BOX3));
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_has_Box_local(&local, BOX3) && local.Stats.forgotten == 0);
    const float age = tide_get_Box_local(&local, BOX3).age;

    // The server says it wasn't: the box is back, with what was kept for it
    tide_world_tick(&verified);
    tide_world_copy(&world, &verified);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_get_Box_local(&local, BOX3).bakes == 1); // Not baked again
    TIDE_CHECK(tide_get_Box_local(&local, BOX3).age == age + 1.0f);

    // A destroy the server confirms: gone once it's in neither
    world.Rules.drop = 3;
    tide_world_tick(&world);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_has_Box_local(&local, BOX3));
    verified.Rules.drop = 3;
    tide_world_tick(&verified);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(!tide_has_Box_local(&local, BOX3) && local.Stats.forgotten == 1);
    world.Rules.drop = verified.Rules.drop = 0;

    // A spawn the client predicted, which the server never had
    world.Rules.spawn = 7;
    tide_world_tick(&world);
    const tide_entity predicted = {2, 3};
    TIDE_REQUIRE(tide_has_Box(&world, predicted) && !tide_has_Box(&verified, predicted));
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_has_Box_local(&local, predicted) && tide_list_count(tide_get_Box_local(&local, predicted).verts) == 7);
    tide_world_tick(&verified);
    tide_world_copy(&world, &verified);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(!tide_has_Box_local(&local, predicted) && local.Stats.forgotten == 2);
    TIDE_CHECK(local.Stats.lastBakes == 107);
    tide_world_free(&verified);
}

// A replay can put another entity on a handle: a spawn the client predicted,
// where the server had another in that tick. A row remembers what its entity
// was born as, so the other one starts from the defaults, and the same spawn
// again keeps what it had.
TIDE_TEST(local_fields_tell_entities_on_one_handle_apart)
{
    start();
    frame();
    tide_world_copy(&verified, &world);

    // The client predicts a box of 7 sides
    world.Rules.spawn = 7;
    tide_world_tick(&world);
    const tide_entity handle = {4, 1};
    TIDE_REQUIRE(tide_get_Box(&world, handle).sides == 7);
    const uint32_t seven = tide_entity_born(&world.entities, handle);
    TIDE_CHECK(seven != 0u && tide_entity_born(&verified.entities, handle) == 0u);
    frame_on(&local, &world, &verified);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_list_count(tide_get_Box_local(&local, handle).verts) == 7 && tide_get_Box_local(&local, handle).age == 2.5f);

    // The server had one of 9 there: the same handle, and another entity
    verified.Rules.spawn = 9;
    tide_world_tick(&verified);
    tide_world_copy(&world, &verified);
    TIDE_REQUIRE(tide_get_Box(&world, handle).sides == 9);
    TIDE_CHECK(tide_entity_born(&world.entities, handle) != seven);
    frame_on(&local, &world, &verified);
    const Box_local nine = tide_get_Box_local(&local, handle);
    TIDE_CHECK(local.Stats.forgotten == 1 && local.Stats.lastBakes == 107); // The 7's went, and Forgotten heard
    TIDE_CHECK(tide_entity_equal(local.Stats.last, handle));
    TIDE_CHECK(nine.bakes == 1 && tide_list_count(nine.verts) == 9 && nine.age == 1.5f); // From the defaults
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).bakes == 1 && tide_get_Box_local(&local, BOX4).age == 4.5f); // The others' stay

    // A spawn the server has as the client predicted it is the same entity
    world.Rules.spawn = 6;
    tide_world_tick(&world);
    const tide_entity next = {5, 1};
    TIDE_REQUIRE(tide_get_Box(&world, next).sides == 6);
    const uint32_t six = tide_entity_born(&world.entities, next);
    frame_on(&local, &world, &verified);
    verified.Rules.spawn = 6;
    tide_world_tick(&verified);
    tide_world_copy(&world, &verified);
    TIDE_CHECK(tide_entity_born(&world.entities, next) == six && six != 0u);
    frame_on(&local, &world, &verified);
    TIDE_CHECK(tide_get_Box_local(&local, next).bakes == 1 && tide_get_Box_local(&local, next).age == 2.5f);
    TIDE_CHECK(local.Stats.forgotten == 1);

    // The same values a tick later are another spawn
    tide_world_copy(&base, &world);
    world.Rules.spawn = 8;
    tide_world_tick(&world);
    const uint32_t eight = tide_entity_born(&world.entities, (tide_entity){6, 1});
    tide_world_tick(&base);
    base.Rules.spawn = 8;
    tide_world_tick(&base);
    TIDE_REQUIRE(tide_get_Box(&base, (tide_entity){6, 1}).sides == 8);
    TIDE_CHECK(eight != 0u && tide_entity_born(&base.entities, (tide_entity){6, 1}) != eight);

    // A world that came as bytes says nothing of what its entities were born
    // as: there they're known by their handles, and what's kept stays
    const uint32_t size = tide_world_pack(&verified, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&verified, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&base, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&base) == tide_world_hash(&verified));
    TIDE_CHECK(tide_entity_born(&base.entities, next) == 0u && tide_entity_born(&base.entities, handle) == 0u);
    tide_world_tick(&base);
    frame_on(&local, &base, &base);
    TIDE_CHECK(local.Stats.forgotten == 1 && local.tide_fields_Box.rows.count == 5);
    TIDE_CHECK(tide_get_Box_local(&local, next).bakes == 1 && tide_get_Box_local(&local, handle).bakes == 1);
    tide_world_free(&verified);
}

// Every machine has them, whenever it learned of the entity, each from the
// defaults: nothing of one machine's reaches another.
TIDE_TEST(local_fields_start_at_their_defaults_on_every_machine)
{
    start();
    for (int i = 0; i < 4; i++) frame();
    tide_local_init(&other); // A machine that joined later, with the same world
    TIDE_CHECK(!tide_has_Box_local(&other, BOX4));
    frame_on(&other, &world, NULL);
    TIDE_CHECK(tide_get_Box_local(&other, BOX4).bakes == 1 && tide_get_Box_local(&other, BOX4).age == 1.5f);
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).age == 4.5f);
    TIDE_CHECK(other.Arena_local.painted == 1 && local.Arena_local.painted == 4);
    tide_local_free(&other);
}

// A match that's over takes what was kept for it, whatever the next frame sees:
// another match's entities have the same handles.
TIDE_TEST(local_fields_go_when_the_match_is_over)
{
    start();
    for (int i = 0; i < 3; i++) frame();
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, NULL);
    tide_world_init(&world, 1.0f / 60.0f); // The next match, by the frame after
    frame();
    TIDE_CHECK(local.Stats.forgotten == 3 && local.Stats.arenas == 3);
    TIDE_CHECK(tide_get_Box_local(&local, BOX4).bakes == 1 && tide_get_Box_local(&local, BOX4).age == 1.5f); // The new one's
    TIDE_CHECK(local.Arena_local.painted == 1);
}

// The local state as bytes, as hot reloading carries it where a new build is
// a new program: local fields go with it.
TIDE_TEST(local_fields_are_in_the_local_states_bytes)
{
    start();
    local.Stats.pulsing = true;
    for (int i = 0; i < 2; i++) frame();
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_local_pack(&local, bytes, size);
    TIDE_REQUIRE(tide_local_unpack(&other, bytes, size));
    free(bytes);
    tide_text_use(NULL, &other.heap);
    const Box_local five = tide_get_Box_local(&other, BOX5);
    TIDE_CHECK(five.bakes == 1 && five.age == 2.5f && tide_list_count(five.verts) == 5);
    TIDE_CHECK(tide_has_Glow_local(&other, BOX3) && !tide_has_Glow_local(&other, BOX5));
    TIDE_CHECK(text_is(&other, tide_get_Label_local(&other, BOX4).text, "label 1"));
    TIDE_CHECK(other.Arena_local.painted == 2);
    // And it goes on from there
    frame_on(&other, &world, NULL);
    TIDE_CHECK(tide_get_Box_local(&other, BOX5).bakes == 1 && tide_get_Box_local(&other, BOX5).age == 3.5f);
    tide_local_free(&other);
}

// C reads and sets them through the local state.
TIDE_TEST(local_fields_from_c)
{
    start();
    Glow_local glow = tide_get_Glow_local(&local, BOX5);
    glow.pulse = 9.0f;
    tide_set_Glow_local(&local, BOX5, glow);
    TIDE_CHECK(tide_has_Glow_local(&local, BOX5) && tide_get_Glow_local(&local, BOX5).pulse == 9.0f);
    frame();
    TIDE_CHECK(tide_get_Glow_local(&local, BOX5).pulse == 9.0f);
    TIDE_CHECK(local.Stats.fresh == 201); // The view read what C set

    // Set for an entity the match hasn't got: gone by the frame after a tick
    tide_set_Glow_local(&local, (tide_entity){40, 1}, glow);
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(!tide_has_Glow_local(&local, (tide_entity){40, 1}) && tide_has_Glow_local(&local, BOX5));
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&base);
}
