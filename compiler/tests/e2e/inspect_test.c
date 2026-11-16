#include <math.h>
#include <stddef.h>
#include <string.h>

#include "game.h"
#include "tide/inspect.h"
#include "tide_test.h"

// inspect.tide, a dev build: its inspector's packets of the match and of this machine's local world; a field of the
// match changed through the server, which the match's Changed handler hears, and refused when it isn't one an editor
// may change or a value it can't hold; one of the local world's changed here.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_packet match_packet, local_packet;
static uint32_t sequence;

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    tide_world_tick(&world);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    sequence = 0;
}

static void stop(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
}

// The commands this machine sent, to the server, and a tick for it to apply them
static void deliver(void)
{
    uint32_t size = 0;
    for (const uint8_t *data; (data = tide_local_command(&local, 0, &size));) {
        uint8_t bytes[TIDE_COMMAND_BYTES];
        TIDE_REQUIRE(size <= sizeof bytes);
        memcpy(bytes, data, size);
        TIDE_CHECK(tide_game_api.command(&world, tide_player_from_index(0), 0, ++sequence, bytes, size));
        tide_local_command_sent(&local, 0);
    }
    tide_world_tick(&world);
}

static int32_t type_named(const tide_inspect_api *api, const char *name)
{
    for (uint32_t i = 0; i < api->layout->type_count; i++) {
        if (strcmp(api->layout->types[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

// The packet's section of `kind` for `type` (-1: the first of its kind)
static bool part_of(const tide_packet *p, const uint32_t kind, const int32_t type, tide_packet_part *out)
{
    uint32_t at = 0;
    while (tide_packet_next(p->bytes, p->size, &at, out)) {
        if (out->kind == kind && (type < 0 || out->type == (uint32_t)type)) return true;
    }
    return false;
}

TIDE_TEST(inspect_packets_show_the_match_and_the_local_world)
{
    start();
    const tide_inspect_api *api = tide_game_inspect();
    TIDE_REQUIRE(api != NULL && api->layout != NULL && api->layout->type_count > 0);
    api->view(&world, &local, &match_packet, &local_packet);
    const int32_t health = type_named(api, "Health"), health_local = type_named(api, "Health local");
    TIDE_REQUIRE(health >= 0 && health_local >= 0);
    tide_packet_part part;
    TIDE_REQUIRE(part_of(&match_packet, TIDE_PACKET_COMPONENT, health, &part));
    TIDE_REQUIRE(part.count == 2 && part.stride == sizeof(Health));
    bool ann = false, bo = false;
    for (uint32_t i = 0; i < part.count; i++) {
        Health h;
        memcpy(&h, part.values + (size_t)i * part.stride, sizeof h);
        uint32_t length = 0;
        const uint8_t *text = tide_packet_text_of(match_packet.bytes, match_packet.size, h.name.at, &length);
        TIDE_REQUIRE(text != NULL);
        ann |= length == 3 && memcmp(text, "Ann", 3) == 0 && h.value == 10.0f;
        bo |= length == 2 && memcmp(text, "Bo", 2) == 0 && h.value == 4.0f;
        TIDE_CHECK(part.rows[i] < tide_packet_entities(match_packet.bytes, match_packet.size));
    }
    TIDE_CHECK(ann && bo);
    TIDE_REQUIRE(part_of(&match_packet, TIDE_PACKET_COMPONENT_LOCAL, health_local, &part));
    TIDE_CHECK(part.count == 2);
    TIDE_REQUIRE(part_of(&match_packet, TIDE_PACKET_SINGLETON, type_named(api, "Score"), &part));
    // The local world: its spark, and its Panel
    TIDE_CHECK(tide_packet_entities(local_packet.bytes, local_packet.size) == 1);
    TIDE_REQUIRE(part_of(&local_packet, TIDE_PACKET_COMPONENT, -1, &part));
    TIDE_CHECK(strcmp(api->layout->types[part.type].name, "Spark") == 0 && part.count == 1 && part.rows[0] == 0);
    float size;
    memcpy(&size, part.values, sizeof size);
    TIDE_CHECK(size == 2.0f);
    TIDE_REQUIRE(part_of(&local_packet, TIDE_PACKET_SINGLETON, type_named(api, "Panel"), &part));
    Panel panel;
    memcpy(&panel, part.values, sizeof panel);
    TIDE_CHECK(panel.width == 200.0f && panel.made);
    // Outside a match, the match's is empty
    api->view(NULL, &local, &match_packet, &local_packet);
    TIDE_CHECK(tide_packet_entities(match_packet.bytes, match_packet.size) == 0);
    stop();
}

// Each Health in the match by its value, which the setup gave each
static tide_entity health_of(const float value)
{
    const tide_inspect_api *api = tide_game_inspect();
    api->view(&world, &local, &match_packet, &local_packet);
    tide_packet_part part;
    if (!part_of(&match_packet, TIDE_PACKET_COMPONENT, type_named(api, "Health"), &part)) return (tide_entity){0, 0};
    for (uint32_t i = 0; i < part.count; i++) {
        Health h;
        memcpy(&h, part.values + (size_t)i * part.stride, sizeof h);
        if (h.value == value) return tide_packet_entity(match_packet.bytes, match_packet.size, part.rows[i], NULL);
    }
    return (tide_entity){0, 0};
}

// An entity of the match with no Health: the scene's
static tide_entity without_health(void)
{
    const tide_inspect_api *api = tide_game_inspect();
    api->view(&world, &local, &match_packet, &local_packet);
    tide_packet_part part;
    if (!part_of(&match_packet, TIDE_PACKET_COMPONENT, type_named(api, "Health"), &part)) return (tide_entity){0, 0};
    for (uint32_t e = 0; e < tide_packet_entities(match_packet.bytes, match_packet.size); e++) {
        bool has = false;
        for (uint32_t i = 0; i < part.count; i++) has |= part.rows[i] == e;
        if (!has) return tide_packet_entity(match_packet.bytes, match_packet.size, e, NULL);
    }
    return (tide_entity){0, 0};
}

TIDE_TEST(inspect_changes_the_match_through_the_server)
{
    start();
    const tide_inspect_api *api = tide_game_inspect();
    const int32_t health = type_named(api, "Health"), score = type_named(api, "Score");
    const tide_entity bo = health_of(4.0f), ann = health_of(10.0f), scene = without_health();
    TIDE_REQUIRE(!tide_entity_is_null(bo) && !tide_entity_is_null(ann) && !tide_entity_is_null(scene));

    // A number: nothing until the server applies it, then the match has it, and its Changed handler heard it
    const float seven = 7.5f;
    api->set(&local, bo, health, offsetof(Health, value), &seven, sizeof seven);
    TIDE_CHECK(tide_get_Health(&world, bo).value == 4.0f);
    deliver();
    TIDE_CHECK(tide_get_Health(&world, bo).value == 7.5f);
    TIDE_CHECK(world.Score.heard == 1 && world.Score.last == 7.5f);
    TIDE_CHECK(tide_get_Health(&world, ann).value == 10.0f);

    // An enum's member, a bool, a field of a struct field, a vector, a Color, a singleton's double
    const uint8_t fled = Mood_Fled, no = 0;
    const int32_t lives = 9;
    const tide_float3 speed = {1, 2, 3};
    const tide_color tint = {0.25f, 0.5f, 0.75f, 1};
    const double total = 12.5;
    api->set(&local, bo, health, offsetof(Health, mood), &fled, 1);
    api->set(&local, bo, health, offsetof(Health, alive), &no, 1);
    api->set(&local, bo, health, offsetof(Health, stats) + offsetof(Stats, lives), &lives, 4);
    api->set(&local, bo, health, offsetof(Health, stats) + offsetof(Stats, speed), &speed, sizeof speed);
    api->set(&local, bo, health, offsetof(Health, tint), &tint, sizeof tint);
    api->set(&local, (tide_entity){0, 0}, score, offsetof(Score, total), &total, sizeof total);
    deliver();
    Health h = tide_get_Health(&world, bo);
    TIDE_CHECK(h.mood == Mood_Fled && !h.alive && h.stats.lives == 9 && h.stats.speed.y == 2.0f && h.tint.b == 0.75f);
    TIDE_CHECK(world.Score.total == 12.5);

    // Refused: a member no enum has, a bool that's neither, NaN, part of a field, text (a handle), a size that isn't the
    // field's, a singleton's field with an entity, a component the entity hasn't
    const uint8_t five = 5, two = 2;
    const float nan = NAN;
    const uint64_t handle = 1;
    const int32_t points = 5;
    api->set(&local, bo, health, offsetof(Health, mood), &five, 1);
    api->set(&local, bo, health, offsetof(Health, alive), &two, 1);
    api->set(&local, bo, health, offsetof(Health, value), &nan, sizeof nan);
    api->set(&local, bo, health, offsetof(Health, stats) + offsetof(Stats, speed) + 4, &seven, sizeof seven);
    api->set(&local, bo, health, offsetof(Health, name), &handle, sizeof handle);
    api->set(&local, bo, health, offsetof(Health, value), &handle, 2);
    api->set(&local, bo, score, offsetof(Score, points), &points, sizeof points);
    api->set(&local, scene, health, offsetof(Health, value), &seven, sizeof seven);
    deliver();
    const Health after = tide_get_Health(&world, bo);
    TIDE_CHECK(memcmp(&after, &h, sizeof h) == 0);
    TIDE_CHECK(world.Score.points == 0 && world.Score.heard == 1);
    stop();
}

TIDE_TEST(inspect_changes_the_local_world_here)
{
    start();
    const tide_inspect_api *api = tide_game_inspect();
    const float wide = 320, shown = 0.5f;
    const uint8_t open = 1, three = 3;
    const int32_t panel = type_named(api, "Panel"), health_local = type_named(api, "Health local");
    TIDE_CHECK(api->set_local(&local, (tide_entity){0, 0}, panel, offsetof(Panel, width), &wide, sizeof wide));
    TIDE_CHECK(api->set_local(&local, (tide_entity){0, 0}, panel, offsetof(Panel, open), &open, 1));
    TIDE_CHECK(!api->set_local(&local, (tide_entity){0, 0}, panel, offsetof(Panel, open), &three, 1));
    TIDE_CHECK(local.Panel.width == 320.0f && local.Panel.open);
    // A match entity's local fields
    const tide_entity ann = health_of(10.0f);
    TIDE_CHECK(api->set_local(&local, ann, health_local, offsetof(Health_local, shown), &shown, sizeof shown));
    TIDE_CHECK(tide_get_Health_local(&local, ann).shown == 0.5f);
    // A local entity's component, by the type its packet names
    api->view(&world, &local, &match_packet, &local_packet);
    tide_packet_part part;
    TIDE_REQUIRE(part_of(&local_packet, TIDE_PACKET_COMPONENT, -1, &part));
    const tide_entity spark = tide_packet_entity(local_packet.bytes, local_packet.size, part.rows[0], NULL);
    const float big = 6;
    TIDE_CHECK(api->set_local(&local, spark, (int32_t)part.type, 0, &big, sizeof big));
    api->view(&world, &local, &match_packet, &local_packet);
    TIDE_REQUIRE(part_of(&local_packet, TIDE_PACKET_COMPONENT, -1, &part));
    float size;
    memcpy(&size, part.values, sizeof size);
    TIDE_CHECK(size == 6.0f);
    // ...never the match's from here
    TIDE_CHECK(!api->set_local(&local, ann, type_named(api, "Health"), offsetof(Health, value), &big, sizeof big));
    uint32_t left = 0;
    TIDE_CHECK(tide_local_command(&local, 0, &left) == NULL);
    stop();
}
