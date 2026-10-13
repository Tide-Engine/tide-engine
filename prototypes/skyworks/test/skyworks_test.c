#include "fixture.h"
#include "tide/net.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static bool craft(tide_world *w, int recipe, uint32_t sequence)
{
    uint8_t data[32] = {0};
    tide_bits b = {data, sizeof data, 0, false};
    tide_bits_put(&b, TIDE_COMMAND_ID_Craft, 16);
    tide_bits_put(&b, 0, 32); tide_bits_put(&b, 0, 32);
    tide_bits_put_varint(&b, tide_zigzag(recipe));
    return tide_game_api.command(w, tide_player_from_index(0), 0, sequence, data, tide_bits_end(&b));
}
static void crafting(void)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f / 30.0f);
    CHECK(craft(&w, 1, 1)); ticks(&w, 1);
    CHECK(w.Pilot.built.at[Kind_Drill] == 0);
    w.Pilot.bag.at[1] = 2; w.Pilot.bag.at[2] = 3; w.Pilot.bag.at[4] = 4;
    CHECK(craft(&w, 13, 2)); ticks(&w, 1);
    CHECK(w.Pilot.bag.at[5] == 1 && w.Pilot.bag.at[4] == 2);
    CHECK(craft(&w, 1, 3)); ticks(&w, 1);
    CHECK(w.Pilot.built.at[1] == 1);
    CHECK(w.Pilot.bag.at[1] == 0 && w.Pilot.bag.at[2] == 0 && w.Pilot.bag.at[4] == 0 && w.Pilot.bag.at[5] == 0);
    CHECK(!craft(&w, 1, 3)); // duplicate delivery does not spend again
    CHECK(craft(&w, -100, 4)); ticks(&w, 1);
    CHECK(w.Pilot.built.at[1] == 1);
    CHECK(craft(&w, 100, 5)); ticks(&w, 1);
    CHECK(w.Pilot.built.at[1] == 1);
    w.Pilot.bag.at[1] = 1; w.Pilot.bag.at[3] = 1;
    CHECK(craft(&w, 12, 6)); ticks(&w, 1);
    CHECK(w.Pilot.bag.at[4] == 1 && w.Pilot.bag.at[1] == 0 && w.Pilot.bag.at[3] == 0);
    tide_world_free(&w);
}
static void production(void)
{
    tide_world w = {0}, snapshot = {0}, restored = {0};
    factory_fixture(&w);
    ticks(&w, 1800);
    CHECK(w.Island.produced >= 12);
    CHECK(machine_at(&w, 17, 19).item == 4);
    CHECK(machine_at(&w, 17, 19).count > 0 && machine_at(&w, 17, 19).count <= 24);
    int total = 0;
    for (int z = 15; z <= 19; z++) { Machine m = machine_at(&w, 17, z); total += m.count + m.input; }
    int remaining = 0;
    memcpy(&remaining, tide_grid_read(&w.heap, w.Island.deposits, 17, 15, 0, &tide_shape_Grid2_int), sizeof remaining);
    CHECK(total + remaining == 80);
    tide_world_copy(&snapshot, &w);
    const uint64_t hash = tide_world_hash(&w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size); CHECK(bytes != NULL);
    CHECK(tide_world_pack(&w, bytes, size) == size);
    CHECK(tide_world_unpack(&restored, bytes, size)); free(bytes);
    CHECK(tide_world_hash(&restored) == hash);
    ticks(&w, 120); ticks(&snapshot, 120); ticks(&restored, 120);
    CHECK(tide_world_hash(&w) == tide_world_hash(&snapshot));
    CHECK(tide_world_hash(&w) == tide_world_hash(&restored));
    CHECK(tide_world_hash(&w) == UINT64_C(0xcfbcdd0138c94509));
    printf("factory hash %016" PRIx64 "\n", tide_world_hash(&w));
    tide_world_free(&w); tide_world_free(&snapshot); tide_world_free(&restored);
}
static void single_hop_and_backpressure(void)
{
    tide_world w = {0}; tide_world_init(&w, 1.0f / 30.0f);
    install(&w, 10, 10, Kind_Tube, 0); install(&w, 11, 10, Kind_Tube, 0); install(&w, 12, 10, Kind_Chest, 0);
    Machine m = machine_at(&w, 10, 10); m.item = 4; m.count = 2; set_machine(&w, 10, 10, m);
    ticks(&w, 15);
    CHECK(machine_at(&w, 11, 10).count == 1 && machine_at(&w, 12, 10).count == 0);
    ticks(&w, 15);
    CHECK(machine_at(&w, 12, 10).count == 1);
    m = machine_at(&w, 12, 10); m.count = 24; set_machine(&w, 12, 10, m);
    ticks(&w, 60);
    CHECK(machine_at(&w, 11, 10).count == 1 && machine_at(&w, 12, 10).count == 24);
    m.item = 7; m.count = 1; set_machine(&w, 12, 10, m);
    ticks(&w, 15); CHECK(machine_at(&w, 11, 10).count == 1 && machine_at(&w, 12, 10).item == 7);
    tide_world_free(&w);
}
static void circuits_and_water(void)
{
    tide_world w = {0}; factory_fixture(&w);
    install(&w, 18, 14, Kind_Switch, 0); install(&w, 18, 15, Kind_Wire, 0);
    Machine sw = machine_at(&w, 18, 14); sw.enabled = false; set_machine(&w, 18, 14, sw);
    ticks(&w, 180); CHECK(machine_at(&w, 17, 15).count == 0 && w.Island.produced == 0);
    sw.enabled = true; set_machine(&w, 18, 14, sw);
    ticks(&w, 180); CHECK(machine_at(&w, 18, 15).signal && w.Island.produced > 0);
    install(&w, 16, 35, Kind_Pump, 0); install(&w, 17, 35, Kind_Tube, 0); install(&w, 18, 35, Kind_Tank, 0);
    ticks(&w, 300);
    CHECK(machine_at(&w, 18, 35).item == 7 && machine_at(&w, 18, 35).count > 0);
    install(&w, 18, 34, Kind_Sensor, 1); install(&w, 17, 34, Kind_Wire, 0);
    ticks(&w, 15); CHECK(machine_at(&w, 18, 34).signal && machine_at(&w, 17, 34).signal);
    tide_world_free(&w);
}
static void movement_and_mining(void)
{
    tide_world w = {0}; tide_world_init(&w, 1.0f / 30.0f);
    tide_world_set_server_input(&w, (Controls){.move = {0, 1}}); ticks(&w, 6);
    CHECK(w.Pilot.position.z > 12.3f && w.Pilot.position.y > 3.9f);
    tide_world_set_server_input(&w, (Controls){.jump = true}); ticks(&w, 5);
    CHECK(w.Pilot.position.y > 4.5f);
    tide_world_set_server_input(&w, (Controls){0}); ticks(&w, 30);
    CHECK(w.Pilot.position.y > 3.9f && w.Pilot.position.y < 4.1f);
    w.Pilot.position = (tide_float3){17.5f, 4.01f, 13.5f}; w.Pilot.pitch = 0.95f;
    tide_world_set_server_input(&w, (Controls){.mine = true}); ticks(&w, 64);
    CHECK(w.Pilot.bag.at[3] >= 2);
    tide_world_set_server_input(&w, (Controls){0}); ticks(&w, 1);
    CHECK(w.Pilot.progress == 0);
    w.Pilot.position.y = -20; ticks(&w, 1);
    CHECK(w.Pilot.position.y >= 4 && w.Pilot.bag.at[3] >= 2);
    tide_world_set_server_input(&w, (Controls){.look = {1000, -1000}}); ticks(&w, 1);
    CHECK(w.Pilot.pitch <= 1.45f && w.Pilot.pitch >= -1.45f);
    tide_world_free(&w);
}
static void aim(tide_world *w, int x, int y, int z, bool west)
{
    w->Pilot.position = (tide_float3){(float)x + 0.5f - (west ? 2 : 0), 4.01f, (float)z + 0.5f - (west ? 0 : 2)};
    w->Pilot.yaw = west ? TIDE_PI_F / 2 : 0;
    w->Pilot.pitch = tide_atan2_f(5.61f - ((float)y + 0.5f), 2.4f);
    w->Pilot.velocity = 0;
    tide_world_set_server_input(w, (Controls){0}); ticks(w, 1);
}
static void mine(tide_world *w, int x, int y, int z)
{
    aim(w, x, y, z, x >= 30 || (y == 3 && z == 15));
    tide_world_set_server_input(w, (Controls){.mine = true}); ticks(w, 24);
    tide_world_set_server_input(w, (Controls){0}); ticks(w, 1);
}
static Block block_at(tide_world *w, int x, int y, int z)
{
    const Block *p = tide_grid_read(&w->heap, w->Island.blocks, x, y, z, &tide_shape_Grid3_Block);
    return p ? *p : Block_Air;
}
static void place(tide_world *w, int x, int z, Kind kind, int direction)
{
    aim(w, x, 3, z, direction == 1);
    tide_world_set_server_input(w, (Controls){.place = true, .selected = kind, .direction = direction}); ticks(w, 1);
    tide_world_set_server_input(w, (Controls){0}); ticks(w, 9);
    if (machine_at(w, x, z).kind != kind) fprintf(stderr, "place %d,%d kind%d ready%d target %d,%d,%d\n", x, z, kind, w->Pilot.built.at[kind], w->Pilot.target.x, w->Pilot.target.y, w->Pilot.target.z);
    CHECK(machine_at(w, x, z).kind == kind);
}
static void first_island_journey(void)
{
    tide_world w = {0}; tide_world_init(&w, 1.0f / 30.0f);
    // Harvest real world resources through the player's ray and mining input.
    for (int y = 4; y <= 6; y++) { mine(&w, 12, y, 14); mine(&w, 20, y, 19); mine(&w, 14, y, 20); }
    CHECK(w.Pilot.bag.at[1] == 27);
    for (int x = 13; x <= 20; x++) mine(&w, x, 3, 13);
    for (int x = 10; x <= 12; x++) mine(&w, x, 3, 16);
    CHECK(w.Pilot.bag.at[2] == 11);
    for (int i = 0; i < 6; i++) mine(&w, 18, 3, 15);
    CHECK(w.Pilot.bag.at[3] == 6);
    uint32_t sequence = 0;
    for (int i = 0; i < 6; i++) { CHECK(craft(&w, 12, ++sequence)); ticks(&w, 1); }
    const int recipes[] = {13, 1, 2, 3, 2, 4};
    for (unsigned i = 0; i < sizeof recipes / sizeof *recipes; i++) { CHECK(craft(&w, recipes[i], ++sequence)); ticks(&w, 1); }
    place(&w, 17, 15, Kind_Drill, 1); place(&w, 17, 16, Kind_Tube, 1);
    place(&w, 17, 17, Kind_Kiln, 1); place(&w, 17, 18, Kind_Tube, 1); place(&w, 17, 19, Kind_Chest, 1);
    ticks(&w, 1800); CHECK(w.Island.produced >= 12);
    aim(&w, 17, 4, 19, true);
    tide_world_set_server_input(&w, (Controls){.use = true}); ticks(&w, 1);
    CHECK(w.Pilot.bag.at[4] >= 17 && machine_at(&w, 17, 19).count == 0);
    // Build the east bridge with the actual plank placement affordance.
    for (int x = 24; x <= 30; x++)
    {
        w.Pilot.position = (tide_float3){(float)x - 0.5f, 4.01f, 16.5f};
        w.Pilot.yaw = TIDE_PI_F / 2; w.Pilot.pitch = 1.4f;
        tide_world_set_server_input(&w, (Controls){.place = true, .selected = Kind_None}); ticks(&w, 1);
        tide_world_set_server_input(&w, (Controls){0}); ticks(&w, 9);
        CHECK(block_at(&w, x, 3, 16) == Block_Plank);
    }
    CHECK(w.Pilot.planks == 9);
    mine(&w, 33, 4, 14); mine(&w, 34, 4, 14); CHECK(w.Pilot.bag.at[6] == 4);
    const int water_recipes[] = {13, 5, 2, 6};
    for (unsigned i = 0; i < sizeof water_recipes / sizeof *water_recipes; i++) { CHECK(craft(&w, water_recipes[i], ++sequence)); ticks(&w, 1); }
    place(&w, 16, 35, Kind_Pump, 0); place(&w, 17, 35, Kind_Tube, 0); place(&w, 18, 35, Kind_Tank, 0);
    ticks(&w, 300); aim(&w, 18, 4, 35, false);
    tide_world_set_server_input(&w, (Controls){.use = true}); ticks(&w, 1); CHECK(w.Pilot.bag.at[7] >= 8);
    tide_world_set_server_input(&w, (Controls){0}); ticks(&w, 1);
    CHECK(craft(&w, 10, ++sequence)); ticks(&w, 1); CHECK(w.Pilot.built.at[10] == 1);
    place(&w, 19, 17, Kind_Beacon, 0); CHECK(w.Island.beacon);
    mine(&w, 19, 4, 17); CHECK(!w.Island.beacon && w.Pilot.built.at[10] == 1);
    tide_world_free(&w);
}
int main(void)
{
    crafting(); production(); single_hop_and_backpressure(); circuits_and_water(); movement_and_mining();
    first_island_journey();
    printf("SKYWORKS: %d checks passed\n", checks);
    return 0;
}
