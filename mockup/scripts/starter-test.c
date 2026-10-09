#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"

static void require(const bool value, const char *message)
{
    if (value) return;
    fprintf(stderr, "Moonwake: %s\n", message);
    exit(1);
}

static tide_entity courier_entity(const tide_world *world)
{
    return TIDE_ENTITY_AT(world, arch1_Owner_Courier, 0);
}

static Courier courier(const tide_world *world)
{
    return tide_get_Courier(world, courier_entity(world));
}

static void tick(tide_world *world, const Controls input)
{
    tide_world_set_input(world, tide_player_from_index(0), input);
    tide_world_tick(world);
}

int main(void)
{
    tide_local local = {0};
    tide_local_init(&local);
    local.Canvas.half = tide_f2(195, 175);
    tide_devices devices = {0};
    tide_button_set(&devices.keyboard.d, true);
    require(tide_input_sample(&devices, &local).move.x == 1, "keyboard input is sampled");
    devices = (tide_devices){0};
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_BEGAN, 1, tide_f2(58, 56));
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_MOVED, 1, tide_f2(103, 56));
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_BEGAN, 2, tide_f2(333, 56));
    const Controls touch = tide_input_sample(&devices, &local);
    require(touch.move.x == 1 && touch.dash, "mobile multitouch can move and dash together");
    devices = (tide_devices){0};
    tide_touch_event(&devices.touchscreen, TIDE_TOUCH_BEGAN, 3, tide_f2(350, 330));
    require(tide_input_sample(&devices, &local).restart, "mobile replant control is sampled");
    tide_local_free(&local);

    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tick(&world, (Controls){0});

    require(courier(&world).position.y == -215.0f, "courier starts in the garden");
    for (int i = 0; i < 30; i++) tick(&world, (Controls){.move = {1, 0}});
    require(courier(&world).position.x > 80, "movement responds to input");
    const float before_dash = courier(&world).position.x;
    tick(&world, (Controls){.move = {1, 0}, .dash = true});
    require(courier(&world).position.x - before_dash > 8, "dash accelerates immediately");

    // A saved world plus identical inputs reproduces identical bits.
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    require(bytes != NULL && size > 0, "world can be serialized");
    require(tide_world_pack(&world, bytes, size) == size, "world serialization is complete");
    tide_world replay = {0};
    require(tide_world_unpack(&replay, bytes, size), "saved world can be restored");
    free(bytes);
    require(tide_world_hash(&world) == tide_world_hash(&replay), "saved world preserves its hash");
    for (int i = 0; i < 120; i++) {
        const Controls input = {.move = {i < 60 ? 1 : -1, 1}, .dash = i % 50 == 0};
        tick(&world, input);
        tick(&replay, input);
        require(tide_world_hash(&world) == tide_world_hash(&replay), "input replay stays deterministic");
    }
    tide_world_free(&replay);
    tick(&world, (Controls){.restart = true});
    require(world.Garden.collected == 0, "replant resets the score");
    require(courier(&world).position.x == 0, "replant resets the courier");
    tick(&world, (Controls){0});

    const tide_float2 seeds[] = {
        {-100, -210}, {-265, -190}, {-375, -70}, {-320, 90}, {-185, 205}, {0, 245},
        {185, 205}, {320, 90}, {375, -70}, {265, -190}, {100, -210}, {0, -75},
    };
    for (int i = 0; i < 12; i++) {
        Courier value = courier(&world);
        value.position = seeds[i];
        value.velocity = tide_f2(0, 0);
        require(tide_set_Courier(&world, courier_entity(&world), value), "courier is accessible");
        tick(&world, (Controls){0});
        require(world.Garden.collected == i + 1, "each reachable moonseed is collected once");
        tick(&world, (Controls){0});
        require(world.Garden.collected == i + 1, "standing on a collected moonseed never counts twice");
    }
    const float finished_time = world.Garden.elapsed;
    for (int i = 0; i < 20; i++) tick(&world, (Controls){0});
    require(world.Garden.elapsed == finished_time && world.Garden.bloom > 0, "completion freezes the timer and wakes the shrine");
    tick(&world, (Controls){.restart = true});
    require(world.Garden.collected == 0 && world.Garden.bloom == 0, "completed garden can restart");
    Courier value = courier(&world);
    value.position = seeds[0];
    require(tide_set_Courier(&world, courier_entity(&world), value), "courier remains after restart");
    tick(&world, (Controls){0});
    require(world.Garden.collected == 1, "restart restores the collected moonseeds");
    printf("Moonwake replay hash: %" PRIu64 "\n", tide_world_hash(&world));
    tide_world_free(&world);
    puts("Moonwake: movement, dash, collection, completion, restart, and saved-world replay passed.");
    return 0;
}
