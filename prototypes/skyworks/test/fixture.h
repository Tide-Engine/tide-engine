#pragma once
#include "skyworks.h"
#include <assert.h>
#include <string.h>

static inline Machine machine_at(const tide_world *w, int x, int z)
{
    Machine m = {0};
    const void *p = tide_grid_read(&w->heap, w->Island.machines, x, z, 0, &tide_shape_Grid2_Machine);
    if (p) memcpy(&m, p, sizeof m);
    return m;
}
static void set_machine(tide_world *w, int x, int z, Machine m)
{
    tide_text_use(&w->heap, NULL);
    memcpy(tide_grid_poke(&w->Island.machines, x, z, 0, &tide_shape_Grid2_Machine, true, TIDE_IN_MATCH), &m, sizeof m);
}
static void install(tide_world *w, int x, int z, Kind kind, int direction)
{
    set_machine(w, x, z, (Machine){.kind = kind, .direction = direction, .enabled = true});
    const tide_int2 p = {x, z};
    memcpy(tide_list_add(&w->Island.sites, sizeof p, TIDE_IN_MATCH), &p, sizeof p);
    const Block b = Block_Machine;
    memcpy(tide_grid_poke(&w->Island.blocks, x, 4, z, &tide_shape_Grid3_Block, true, TIDE_IN_MATCH), &b, sizeof b);
    w->Island.revision++;
}
static void factory_fixture(tide_world *w)
{
    tide_world_init(w, 1.0f / 30.0f);
    install(w, 17, 15, Kind_Drill, 1);
    install(w, 17, 16, Kind_Tube, 1);
    install(w, 17, 17, Kind_Kiln, 1);
    install(w, 17, 18, Kind_Tube, 1);
    install(w, 17, 19, Kind_Chest, 1);
}
static void ticks(tide_world *w, int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(w);
}
