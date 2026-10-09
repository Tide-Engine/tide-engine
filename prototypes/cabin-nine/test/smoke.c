#include <stdbool.h>
#include <stdint.h>

#include "cabin.h"
#include "tide/net.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)

static tide_world world;

static tide_player_id player(const int seat)
{
    return tide_player_from_index(seat);
}

static void tick(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static tide_entity crew(const int seat)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Crew(&world, entity) && tide_get_Crew(&world, entity).seat == seat) return entity;
    }
    return (tide_entity){0};
}

static tide_entity passenger(const int id)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Passenger(&world, entity) && tide_get_Passenger(&world, entity).id == id) return entity;
    }
    return (tide_entity){0};
}

static int passengers(void)
{
    int count = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++)
        if (tide_has_Passenger(&world, tide_entity_in_slot(&world.entities, i))) count++;
    return count;
}

static void start(const int people)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int i = 0; i < people; i++) tide_world_player_joined(&world, player(i));
    tick(2);
}

static void at(const int seat, const float z)
{
    const tide_entity entity = crew(seat);
    Crew value = tide_get_Crew(&world, entity);
    value.position = tide_f3(0, 0, z);
    TIDE_CHECK(tide_set_Crew(&world, entity, value));
}

// Exercise the actual command wire and deferred match handler, including
// authenticated senders; this does not bypass the game's guard clauses.
static void send_command(const uint32_t id, const int sender, const int fields, const int first, const int second)
{
    uint8_t bytes[TIDE_COMMAND_BYTES] = {0};
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    if (fields > 0) tide_bits_put_varint(&bits, tide_zigzag(first));
    if (fields > 1) tide_bits_put_varint(&bits, tide_zigzag(second));
    const uint32_t size = tide_bits_end(&bits);
    const uint32_t sequence = tide_game_api.command_sequence(&world, player(sender), 0) + 1;
    TIDE_REQUIRE(tide_game_api.check_command(bytes, size, NULL));
    TIDE_REQUIRE(tide_game_api.command(&world, player(sender), 0, sequence, bytes, size));
    tide_world_tick(&world);
}

static void plain(const uint32_t id, const int sender)
{
    send_command(id, sender, 0, 0, 0);
}

static void interact(const int sender, const int target, const int action)
{
    send_command(TIDE_COMMAND_ID_Interact, sender, 2, target, action);
}

static void ready(void)
{
    plain(TIDE_COMMAND_ID_Ready, 0);
    plain(TIDE_COMMAND_ID_Ready, 1);
    tick(1);
}

static void boarding(void)
{
    start(2);
    ready();
    for (int id = 0; id < 8; id++) {
        at(0, tide_get_Passenger(&world, passenger(id)).position.z);
        interact(0, id, 0);
    }
    tick(500);
}

static void records(void)
{
    at(0, 0.8f);
    send_command(TIDE_COMMAND_ID_ReadEvidence, 0, 1, 0, 0);
    at(1, 12.8f);
    send_command(TIDE_COMMAND_ID_ReadEvidence, 1, 1, 1, 0);
}

static void nominate(const int id)
{
    at(0, tide_get_Passenger(&world, passenger(id)).position.z);
    interact(0, id, 0);
    interact(0, id, 1);
    send_command(TIDE_COMMAND_ID_Propose, 0, 1, id, 0);
    interact(0, id, 2);
}

TIDE_TEST(cabin_two_human_ready_gate_and_departure)
{
    start(1);
    plain(TIDE_COMMAND_ID_Ready, 0);
    tick(120);
    TIDE_CHECK(world.Flight.phase == 0 && world.Flight.elapsed == 0);
    TIDE_CHECK(passengers() == 8);
    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_CHECK(world.Flight.phase == 0);
    plain(TIDE_COMMAND_ID_Ready, 1);
    tick(1);
    TIDE_CHECK(world.Flight.phase == 1);
    TIDE_CHECK(tide_get_Crew(&world, crew(0)).ready && tide_get_Crew(&world, crew(1)).ready);
    tide_world_free(&world);
}

TIDE_TEST(cabin_rejects_distant_invalid_and_unjoined_interactions)
{
    start(2);
    ready();
    at(0, 0.5f);
    interact(0, 7, 0);
    interact(0, -1, 0);
    interact(0, 500, 2);
    interact(0, 0, 99);
    TIDE_CHECK(world.Flight.served == 0);
    at(0, tide_get_Passenger(&world, passenger(0)).position.z);
    interact(0, 0, 0);
    TIDE_CHECK(world.Flight.served == 1);
    tide_world_player_left(&world, player(1));
    tick(1);
    interact(1, 0, 0);
    TIDE_CHECK(world.Flight.served == 1 && world.Roster.ready == 0);
    tide_world_free(&world);
}

TIDE_TEST(cabin_records_and_two_independent_confirmations)
{
    boarding();
    TIDE_REQUIRE(world.Flight.phase == 3 && passengers() == 9);
    at(0, 0.8f);
    send_command(TIDE_COMMAND_ID_ReadEvidence, 0, 1, 0, 0);
    at(0, 12.8f);
    send_command(TIDE_COMMAND_ID_ReadEvidence, 0, 1, 1, 0);
    TIDE_CHECK(world.Flight.clues == 3 && world.Flight.readers == 1);
    nominate(8);
    TIDE_CHECK(world.Flight.suspect == -1);
    records();
    // Free the compartment occupied by the previous attempted nomination.
    at(0, 14.5f);
    interact(0, 8, 2);
    nominate(8);
    TIDE_CHECK(world.Flight.suspect == 8);
    TIDE_CHECK(tide_get_Passenger(&world, passenger(8)).held);
    TIDE_CHECK(tide_get_Passenger(&world, passenger(8)).position.z == 14.5f);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    TIDE_CHECK(world.Flight.votes == 1 && world.Flight.resolved == 0);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.phase == 4 && world.Flight.resolved == 1);
    tide_world_free(&world);
}

TIDE_TEST(cabin_wrong_decisions_release_and_cannot_repeat_penalties)
{
    boarding();
    records();
    nominate(0);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.trust == 2 && world.Flight.phase == 3);
    TIDE_CHECK(!tide_get_Passenger(&world, passenger(0)).held);
    TIDE_CHECK(world.Flight.wrongTargets == 1);
    at(0, tide_get_Passenger(&world, passenger(0)).position.z);
    send_command(TIDE_COMMAND_ID_Propose, 0, 1, 0, 0);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.trust == 2 && world.Flight.suspect == -1);
    tide_world_free(&world);
}

TIDE_TEST(cabin_disconnect_freezes_progress_and_requires_new_signatures)
{
    boarding();
    records();
    nominate(8);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    tide_world_player_left(&world, player(1));
    tick(1);
    const float elapsed = world.Flight.elapsed;
    const float phase_seconds = world.Flight.phaseSeconds;
    tick(180);
    TIDE_CHECK(world.Flight.elapsed == elapsed && world.Flight.phaseSeconds == phase_seconds);
    TIDE_CHECK(world.Flight.votes == 0 && world.Flight.readers == 0);
    TIDE_CHECK(!tide_get_Crew(&world, crew(0)).ready);
    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_CHECK(tide_get_Crew(&world, crew(0)).seat == 0 && tide_get_Crew(&world, crew(1)).seat == 1);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.resolved == 0);
    ready();
    records();
    plain(TIDE_COMMAND_ID_Confirm, 0);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.resolved == 1);
    tide_world_free(&world);
}

TIDE_TEST(cabin_four_investigations_reach_ending_and_restart)
{
    boarding();
    for (int investigation = 0; investigation < 4; investigation++) {
        TIDE_REQUIRE(world.Flight.phase == 3 && world.Flight.caseIndex == investigation);
        records();
        nominate(8);
        plain(TIDE_COMMAND_ID_Confirm, 0);
        plain(TIDE_COMMAND_ID_Confirm, 1);
        TIDE_CHECK(world.Flight.resolved == investigation + 1);
        if (investigation < 3) {
            TIDE_CHECK(world.Flight.phase == 4);
            plain(TIDE_COMMAND_ID_NextCase, 0);
            TIDE_CHECK(passengers() == 8 && world.Flight.phase == 2);
            tick(500);
            TIDE_CHECK(passengers() == 9 && world.Flight.clues == 0 && world.Flight.readers == 0);
        }
    }
    TIDE_CHECK(world.Flight.phase == 5 && world.Flight.outcome == 1 && world.Flight.trust == 3);
    plain(TIDE_COMMAND_ID_Restart, 0);
    TIDE_CHECK(world.Flight.phase == 0 && world.Flight.resolved == 0 && world.Flight.elapsed == 0);
    TIDE_CHECK(world.Flight.trust == 3 && world.Flight.served == 0 && passengers() == 8);
    TIDE_CHECK(world.Roster.ready == 0);
    ready();
    TIDE_CHECK(world.Flight.phase == 1 && passengers() == 8);
    tide_world_free(&world);
}

TIDE_TEST(cabin_controls_stay_in_walkable_aisle)
{
    start(2);
    tide_world_set_input(&world, player(0), (Controls){.move = {100, 100}, .look = {100, 100}});
    tick(120);
    const Crew value = tide_get_Crew(&world, crew(0));
    TIDE_CHECK(value.position.x >= -0.5f && value.position.x <= 0.5f);
    TIDE_CHECK(value.position.z >= 0.5f && value.position.z <= 15.0f && value.position.y == 0);
    TIDE_CHECK(value.pitch >= -1.2f && value.pitch <= 1.2f);
    tide_world_free(&world);
}

TIDE_TEST(cabin_damaged_and_failed_flight_reports)
{
    boarding();
    records();
    nominate(0);
    plain(TIDE_COMMAND_ID_Confirm, 0);
    plain(TIDE_COMMAND_ID_Confirm, 1);
    TIDE_CHECK(world.Flight.trust == 2);
    for (int investigation = 0; investigation < 4; investigation++) {
        records();
        nominate(8);
        plain(TIDE_COMMAND_ID_Confirm, 0);
        plain(TIDE_COMMAND_ID_Confirm, 1);
        if (investigation < 3) {
            plain(TIDE_COMMAND_ID_NextCase, 1);
            tick(500);
        }
    }
    TIDE_CHECK(world.Flight.phase == 5 && world.Flight.outcome == 2 && world.Flight.resolved == 4);
    boarding();
    records();
    for (int innocent = 0; innocent < 3; innocent++) {
        nominate(innocent);
        plain(TIDE_COMMAND_ID_Confirm, 0);
        plain(TIDE_COMMAND_ID_Confirm, 1);
        TIDE_CHECK(!tide_get_Passenger(&world, passenger(innocent)).held);
    }
    TIDE_CHECK(world.Flight.phase == 5 && world.Flight.outcome == 3 && world.Flight.trust == 0);
    TIDE_CHECK(world.Flight.resolved == 0);
    plain(TIDE_COMMAND_ID_Restart, 1);
    TIDE_CHECK(world.Flight.phase == 0 && world.Flight.trust == 3 && passengers() == 8);
    tide_world_free(&world);
}
