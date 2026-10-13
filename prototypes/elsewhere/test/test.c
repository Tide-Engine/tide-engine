#include "elsewhere_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "Elsewhere: %s, line %d\n", #value, __LINE__); exit(1); } } while (0)

static tide_player_id id(int seat) { return tide_player_from_index(seat); }
static Gardener person(const tide_world *w, int seat) { return tide_get_Gardener(w, w->Garden.people.at[seat]); }
static void input(tide_world *w, int seat, Wander value) { tide_world_set_input(w, id(seat), value); }
static void ticks(tide_world *w, int count) { for (int i = 0; i < count; i++) tide_world_tick(w); }
static void start(tide_world *w, int players)
{
    tide_world_init(w, 1.0f / 60.0f);
    for (int i = 0; i < players; i++) tide_world_player_joined(w, id(i));
    ticks(w, 2);
    CHECK(w->Garden.joined == players);
    for (int i = 0; i < players; i++) {
        Gardener p = person(w, i); p.yaw = 0;
        CHECK(tide_set_Gardener(w, w->Garden.people.at[i], p));
    }
}
static void at(tide_world *w, int seat, int realm, float x, float z)
{
    Gardener p = person(w, seat);
    p.realm = realm; p.position = (tide_float3){x, 0, z}; p.yaw = 0; p.sitting = false;
    CHECK(tide_set_Gardener(w, w->Garden.people.at[seat], p));
}

// Actual movement input, in a chart with yaw 0. A wrong doorway mapping,
// an unreachable route or an unexpected obstruction fails within this bound.
static void walk_to(tide_world *w, int seat, int realm, float x, float z)
{
    for (int step = 0; step < 1600; step++) {
        const Gardener p = person(w, seat);
        CHECK(p.realm == realm);
        const float dx = x - p.position.x, dz = z - p.position.z;
        const float length = sqrtf(dx * dx + dz * dz);
        if (length < 0.07f) { input(w, seat, (Wander){0}); return; }
        const float amount = fminf(length / (3.1f / 60), 1);
        input(w, seat, (Wander){.move = {dx / length * amount, dz / length * amount}});
        tide_world_tick(w);
    }
    CHECK(false);
}
static void use(tide_world *w, int seat, int count)
{
    input(w, seat, (Wander){0}); ticks(w, 1);
    input(w, seat, (Wander){.use = true}); ticks(w, count);
    input(w, seat, (Wander){0}); ticks(w, 1);
}
static void cross(tide_world *w, int seat, float direction, int destination)
{
    const int before = person(w, seat).realm;
    input(w, seat, (Wander){.move = {0, direction}});
    for (int i = 0; i < 90 && person(w, seat).realm == before; i++) tide_world_tick(w);
    CHECK(person(w, seat).realm == destination);
    input(w, seat, (Wander){0}); ticks(w, 40);
}

static void solo_journey(void)
{
    tide_world w = {0}; start(&w, 1);
    for (int realm = 1; realm <= 3; realm++) {
        const float door_x = (realm - 2) * 7.0f;
        const float door_z = realm == 2 ? 10.0f : 6.0f;
        // Cross at x beside the central tree, then approach the proper window.
        walk_to(&w, 0, 0, -5, -6);
        walk_to(&w, 0, 0, -5, 0);
        if (realm == 2) { walk_to(&w, 0, 0, -5, 8); walk_to(&w, 0, 0, 0, 8); }
        else { walk_to(&w, 0, 0, door_x, 0); walk_to(&w, 0, 0, door_x, door_z - 0.3f); }
        cross(&w, 0, 1, realm);
        walk_to(&w, 0, realm, -5, -7);
        walk_to(&w, 0, realm, -5, -0.8f);
        use(&w, 0, 1); CHECK(w.Garden.source.at[realm - 1] > 23);
        walk_to(&w, 0, realm, -3, 0);
        walk_to(&w, 0, realm, -3, 3);
        walk_to(&w, 0, realm, -1.8f, 3.2f);
        use(&w, 0, 215); CHECK(w.Garden.growth.at[realm - 1] == 1);
        use(&w, 0, 1); CHECK(person(&w, 0).seed == realm);
        walk_to(&w, 0, realm, -3, 1);
        walk_to(&w, 0, realm, 0, 1);
        walk_to(&w, 0, realm, 0, -9.7f);
        cross(&w, 0, -1, 0);
        // Return windows put the carrier at the matching hub window.
        if (realm == 2) { walk_to(&w, 0, 0, -4, 8); walk_to(&w, 0, 0, -4, -1); }
        else { walk_to(&w, 0, 0, door_x, -1); }
        walk_to(&w, 0, 0, (realm - 2) * 3.0f, -1);
        use(&w, 0, 1);
        CHECK(w.Garden.planted.at[realm - 1]); CHECK(person(&w, 0).seed == 0);
        CHECK(w.Garden.flowers == realm);
    }
    ticks(&w, 600); CHECK(w.Garden.finale == 1);
    walk_to(&w, 0, 0, 6, -5); use(&w, 0, 1); CHECK(person(&w, 0).sitting);
    input(&w, 0, (Wander){.move = {1, 0}}); ticks(&w, 1); CHECK(!person(&w, 0).sitting);
    printf("Solo journey hash: %llu\n", (unsigned long long)tide_world_hash(&w));
    tide_world_free(&w);
}

static void coop_and_leave(void)
{
    tide_world w = {0}; start(&w, 2);
    at(&w, 0, 2, -5, -0.8f); at(&w, 1, 2, -1.8f, 3.2f);
    input(&w, 1, (Wander){.use = true}); ticks(&w, 240);
    CHECK(w.Garden.growth.at[1] == 0); // Tending alone before the source wakes does nothing.
    input(&w, 0, (Wander){.use = true}); ticks(&w, 216);
    CHECK(w.Garden.growth.at[1] == 1);
    use(&w, 1, 1); CHECK(person(&w, 1).seed == 2);
    CHECK(person(&w, 0).seed == 0);
    // The source cannot claim a seed a partner already took.
    at(&w, 0, 2, -1.8f, 3.2f); use(&w, 0, 1); CHECK(person(&w, 0).seed == 0);
    tide_world_player_left(&w, id(1)); ticks(&w, 2);
    CHECK(w.Garden.joined == 1); CHECK(!w.Garden.harvested.at[1]);
    use(&w, 0, 1); CHECK(person(&w, 0).seed == 2);
    tide_world_player_joined(&w, id(1)); ticks(&w, 2); CHECK(w.Garden.joined == 2);
    CHECK(person(&w, 1).seed == 0);
    tide_world_free(&w);
}

static void doorway_edges(void)
{
    tide_world w = {0}; start(&w, 2);
    // The entire visible lower opening is traversable, including both edges.
    for (int realm = 1; realm <= 3; realm++) for (int side = -1; side <= 1; side += 2) {
        const float x = (realm - 2) * 7.0f, z = realm == 2 ? 10.0f : 6.0f;
        at(&w, 0, 0, x + side * 1.4f, z - 0.02f);
        input(&w, 0, (Wander){.move = {0, 1}}); ticks(&w, 1);
        CHECK(person(&w, 0).realm == realm);
        CHECK(fabsf(person(&w, 0).position.x - side * 1.4f) < 0.00001f);
        // Reversing immediately must return home, without a cooldown hole.
        input(&w, 0, (Wander){.move = {0, -1}}); ticks(&w, 1);
        CHECK(person(&w, 0).realm == 0);
    }
    // Starting exactly on a plane and stepping through still crosses.
    at(&w, 0, 0, -7, 6);
    input(&w, 0, (Wander){.move = {0, 1}}); ticks(&w, 1);
    CHECK(person(&w, 0).realm == 1);
    at(&w, 0, 1, 0, -10);
    input(&w, 0, (Wander){.move = {0, -1}}); ticks(&w, 1);
    CHECK(person(&w, 0).realm == 0);
    // Test the intersection with the plane, rather than the tick's endpoint.
    at(&w, 0, 0, -7 + 1.4f, 5.99f);
    input(&w, 0, (Wander){.move = {1, 1}}); ticks(&w, 1);
    CHECK(person(&w, 0).realm == 1);
    at(&w, 0, 0, -7 + 1.44f, 5.99f);
    input(&w, 0, (Wander){.move = {-1, 1}}); ticks(&w, 1);
    CHECK(person(&w, 0).realm == 0); // Intersection is outside, endpoint inside.
    at(&w, 0, 0, -7, 6);
    input(&w, 0, (Wander){0}); ticks(&w, 2);
    CHECK(person(&w, 0).realm == 0); // Resting on the plane does not oscillate.
    // Both owners can cross and immediately reverse independently.
    at(&w, 0, 0, -7, 5.98f); at(&w, 1, 0, -7, 5.98f);
    for (int seat = 0; seat < 2; seat++) input(&w, seat, (Wander){.move = {0, 1}});
    ticks(&w, 1);
    CHECK(person(&w, 0).realm == 1 && person(&w, 1).realm == 1);
    for (int seat = 0; seat < 2; seat++) input(&w, seat, (Wander){.move = {0, -1}});
    ticks(&w, 1);
    CHECK(person(&w, 0).realm == 0 && person(&w, 1).realm == 0);
    tide_world_free(&w);
}

static void loops_bounds_and_replay(void)
{
    tide_world w = {0}, before = {0}, restored = {0}; start(&w, 2);
    at(&w, 0, 1, 5, 9.7f); cross(&w, 0, 1, 2);
    at(&w, 0, 2, 5, 9.7f); cross(&w, 0, 1, 3);
    at(&w, 0, 3, 5, 9.7f); cross(&w, 0, 1, 1);
    at(&w, 0, 1, 10.4f, 0);
    input(&w, 0, (Wander){.move = {100, 0}, .look = {NAN, INFINITY}}); ticks(&w, 600);
    CHECK(person(&w, 0).position.x <= 10.5f); CHECK(isfinite(person(&w, 0).yaw));
    tide_world_copy(&before, &w);
    input(&w, 0, (Wander){.move = {-0.5f, 0.7f}}); ticks(&w, 40);
    const uint64_t first = tide_world_hash(&w);
    tide_world_copy(&restored, &before);
    input(&restored, 0, (Wander){.move = {-0.5f, 0.7f}}); ticks(&restored, 40);
    CHECK(tide_world_hash(&restored) == first);
    const size_t need = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(need); CHECK(bytes);
    CHECK(tide_world_pack(&w, bytes, need) == need);
    CHECK(tide_world_unpack(&restored, bytes, need)); CHECK(tide_world_hash(&restored) == first);
    free(bytes);
    tide_world_free(&w); tide_world_free(&before); tide_world_free(&restored);
}

static void reset_command(tide_world *w, int sender)
{
    uint8_t payload[10] = {0}; // Wire id, then the command's null entity target.
    payload[0] = (uint8_t)TIDE_COMMAND_ID_BeginAgain;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_BeginAgain >> 8);
    const uint32_t sequence = tide_game_api.command_sequence(w, id(sender), 0) + 1;
    (void)tide_game_api.command(w, id(sender), 0, sequence, payload, sizeof payload);
    ticks(w, 1);
}

static void restart_and_membership(void)
{
    tide_world w = {0}; start(&w, 2);
    w.Garden.flowers = 2; w.Garden.planted.at[0] = true; w.Garden.planted.at[1] = true;
    at(&w, 1, 3, -3, 2);
    Gardener p = person(&w, 1); p.seed = 3; p.sitting = true;
    CHECK(tide_set_Gardener(&w, w.Garden.people.at[1], p));
    tide_world_player_left(&w, id(0)); ticks(&w, 2);
    reset_command(&w, 0); CHECK(w.Garden.flowers == 2); // A departed visitor cannot reset.
    reset_command(&w, 1);
    CHECK(w.Garden.flowers == 0); CHECK(w.Garden.joined == 1);
    CHECK(!w.Garden.planted.at[0] && !w.Garden.planted.at[1]);
    CHECK(person(&w, 1).realm == 0 && person(&w, 1).seed == 0 && !person(&w, 1).sitting);
    CHECK(tide_player_equal(tide_get_Owner(&w, w.Garden.people.at[1]).player, id(1)));
    tide_world_free(&w);
}

int main(void)
{
    doorway_edges(); solo_journey(); coop_and_leave(); loops_bounds_and_replay(); restart_and_membership();
    puts("Elsewhere gameplay checks passed");
    return 0;
}
