// SUNDAY SUPPER's story, played through with no window: two travellers from
// the dead car to the barn, doing what the script waits for at each step.
// Where a player would walk, the test puts them there; what they press goes
// through the input, as a player's does. Each beat checks the story moved on.
#include "sunday_supper_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "SUNDAY SUPPER check failed: %s (%s:%d), story step %d\n", #value, __FILE__, __LINE__, world.Story.step); \
    exit(1); } } while (0)

static tide_world world;
static Hands held[2];

static const float F1 = 0.6f, F2 = 3.5f, HX = 28.0f, HZ = 34.0f;
static tide_float3 house(float u, float y, float v) { return (tide_float3){HX + u, y, HZ + v}; }

static tide_entity walker_of(int seat) { return seat == 0 ? world.Party.sam : world.Party.jo; }

static void tick(int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2; seat++) {
            tide_world_set_input(&world, tide_player_from_index(seat), held[seat]);
            if (seat == 0) tide_world_set_server_input(&world, held[seat]);
        }
        tide_world_tick(&world);
    }
}

// Ticks until the story's at least at a step whose objective or line says so, or fails after `most`.
static bool wait_for(bool (*done)(void), int most)
{
    for (int i = 0; i < most; i++) {
        if (done()) return true;
        tick(1);
    }
    return done();
}

static void put(int seat, tide_float3 feet, float yaw, float pitch)
{
    const tide_entity e = walker_of(seat);
    Walker w = tide_get_Walker(&world, e);
    w.feet = feet;
    w.yaw = yaw;
    w.pitch = pitch;
    w.velocity = (tide_float3){0};
    w.sitting = -1;
    w.lying = -1;
    w.jumped = true;
    CHECK(tide_set_Walker(&world, e, w));
}

// Stand a traveller where they can see a spot, `back` metres away along `yaw`, and look at it.
static void face(int seat, tide_float3 spot, float yaw, float back, float floor)
{
    const tide_float3 feet = {spot.x - sinf(yaw) * back, floor, spot.z - cosf(yaw) * back};
    const float eye = floor + 1.6f;
    const float pitch = atan2f(spot.y - eye, back);
    put(seat, feet, yaw, pitch);
    tick(2);
}

static void press_use(int seat)
{
    held[seat].use = true;
    tick(1);
    held[seat].use = false;
    tick(1);
}

static void pick(int seat, int which)
{
    if (which == 1) held[seat].pick1 = true;
    if (which == 2) held[seat].pick2 = true;
    tick(1);
    held[seat].pick1 = held[seat].pick2 = false;
    tick(1);
}

static bool choosing(void) { return tide_text_view(world.Story.options).bytes > 0 && world.Story.choice == 0; }
static bool seated_wait(void) { return (world.Story.enabled & (1LL << Spot_SeatSam)) != 0; }
static bool upstairs_wait(void) { return (world.Story.enabled & (1LL << Spot_Drawer)) != 0; }
static bool beds_wait(void) { return (world.Story.enabled & (1LL << Spot_BedSam)) != 0; }
static bool morning(void) { return world.Story.daylight > 0.6f && !world.Story.frozen && world.Story.fade < 0.1f; }
static bool sacks_wait(void) { return (world.Story.enabled & (1LL << Spot_Sack0)) != 0; }
static bool tarp_wait(void) { return (world.Story.enabled & (1LL << Spot_Tarp)) != 0; }
static bool knock_wait(void) { return (world.Story.enabled & (1LL << Spot_FrontDoor)) != 0; }
static bool ended(void) { return world.Story.ended; }
static bool objective_set(void) { return tide_text_view(world.Story.objective).bytes > 0; }

static int aimed(int seat) { return tide_get_Walker(&world, walker_of(seat)).aimed; }

static void answer(int seat, int which)
{
    CHECK(wait_for(choosing, 60 * 90));
    pick(seat, which);
    CHECK(!choosing());
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    held[0].ready = held[1].ready = true;
    tick(2);
    CHECK(world.Party.joined == 2);

    // Friday: the road. Looking under the hood says something; then up the road to the lane.
    tick(60 * 8);
    face(0, (tide_float3){1.4f, 1.0f, -110 + 2.1f}, 3.14159f, 1.2f, 0);
    CHECK(aimed(0) == Spot_CarHood);
    press_use(0);
    CHECK(tide_text_view(world.Story.remarkSam).bytes > 0);
    put(0, (tide_float3){3, 0, 36}, 0, 0);
    put(1, (tide_float3){2, 0, 35}, 0, 0);
    CHECK(wait_for(knock_wait, 60 * 30));

    // The porch: a knock, and Ruth at the door
    face(1, house(-0.1f, F1 + 1.0f, 5.85f), 3.14159f / 2, 1.1f, F1);
    CHECK(aimed(1) == Spot_FrontDoor);
    press_use(1);
    answer(1, 2);
    // Inside, both of them, once Ruth asks
    CHECK(wait_for(objective_set, 60 * 30));
    put(0, house(1.2f, F1, 6.0f), 3.14159f / 2, 0);
    put(1, house(1.6f, F1, 6.2f), 3.14159f / 2, 0);
    answer(0, 1);
    CHECK(wait_for(seated_wait, 60 * 60));

    // Supper: both sit
    face(0, house(1.85f, F1 + 0.6f, 7.35f), 0, 0.9f, F1);
    CHECK(aimed(0) == Spot_SeatSam);
    press_use(0);
    face(1, house(3.05f, F1 + 0.6f, 7.35f), 0, 0.5f, F1);
    CHECK(aimed(1) == Spot_SeatJo);
    press_use(1);
    CHECK(tide_get_Walker(&world, walker_of(0)).sitting == 0 && tide_get_Walker(&world, walker_of(1)).sitting == 1);
    answer(1, 1);

    // Upstairs with Ruth: into the guest room, which she locks
    CHECK(wait_for(objective_set, 60 * 120));
    put(0, house(2.0f, F2, 2.6f), 0, 0);
    put(1, house(2.4f, F2, 3.0f), 0, 0);
    CHECK(wait_for(upstairs_wait, 60 * 60));
    CHECK(world.Farm.doors.at[2].locked);
    // The door won't open
    face(0, house(2.15f, F2 + 1.0f, 4.5f), 0, 1.0f, F2);
    CHECK(aimed(0) == Spot_GuestDoor);
    press_use(0);
    CHECK(world.Farm.doors.at[2].goal < 0.5f);

    // The night, and to bed
    CHECK(wait_for(beds_wait, 60 * 200));
    face(0, house(1.7f, F2 + 0.55f, 2.75f), 0, 0.9f, F2);
    CHECK(aimed(0) == Spot_BedSam);
    press_use(0);
    face(1, house(2.95f, F2 + 0.55f, 1.7f), 3.14159f, 0.9f, F2);
    put(1, house(2.95f, F2, 3.0f), 3.14159f, atan2f(0.55f - 1.6f, 1.3f));
    tick(2);
    CHECK(aimed(1) == Spot_BedJo);
    press_use(1);
    CHECK(wait_for(morning, 60 * 60));

    // Saturday: breakfast, and the barn
    put(0, house(3.0f, F1, 7.2f), 0, 0);
    put(1, house(2.6f, F1, 7.4f), 0, 0);
    answer(0, 1);
    CHECK(wait_for(objective_set, 60 * 60));
    put(0, (tide_float3){47.5f, 0, 37.0f}, 3.14159f / 2, 0);
    put(1, (tide_float3){47.5f, 0, 38.0f}, 3.14159f / 2, 0);
    CHECK(wait_for(sacks_wait, 60 * 30));
    for (int sack = 0; sack < 3; sack++) {
        const tide_float3 at = world.Story.sacks.at[sack];
        face(0, (tide_float3){at.x, 0.35f, at.z}, -3.14159f / 2, 1.0f, 0);
        CHECK(aimed(0) == Spot_Sack0 + sack);
        press_use(0);
        face(1, (tide_float3){at.x, 0.35f, at.z}, 0, 1.0f, 0);
        CHECK(aimed(1) == Spot_Sack0 + sack);
        press_use(1);
        CHECK(world.Story.holders.at[sack] == 3);
        // Carry it to the bin together, side by side
        put(0, (tide_float3){53.4f, 0, 36.7f}, 3.14159f / 2, 0);
        put(1, (tide_float3){53.4f, 0, 37.7f}, 3.14159f / 2, 0);
        tick(60);
        CHECK(world.Story.binned.at[sack]);
    }
    CHECK(wait_for(tarp_wait, 60 * 60));
    face(0, (tide_float3){51, 1.5f, 40.4f}, 3.14159f / 2, 2.9f, 0);
    CHECK(aimed(0) == Spot_Tarp);
    press_use(0);
    CHECK(!world.Story.pulled);
    face(1, (tide_float3){51, 1.5f, 40.4f}, -3.14159f / 2, 2.9f, 0);
    CHECK(aimed(1) == Spot_Tarp);
    press_use(1);
    CHECK(world.Story.pulled);
    CHECK(wait_for(ended, 60 * 60));

    // The match is the same from a snapshot, and through its packed bytes
    tide_world saved = {0}, packed = {0};
    tide_world_copy(&saved, &world);
    tick(120);
    const uint64_t expected = tide_world_hash(&world);
    tide_world_copy(&world, &saved);
    tick(120);
    CHECK(tide_world_hash(&world) == expected);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    CHECK(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    CHECK(tide_world_unpack(&packed, bytes, size) && tide_world_hash(&packed) == expected);
    free(bytes);
    tide_world_free(&packed);
    tide_world_free(&saved);
    tide_world_free(&world);
    puts("SUNDAY SUPPER simulation checks passed");
    return 0;
}
