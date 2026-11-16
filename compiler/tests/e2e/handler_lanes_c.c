#include "game.h"

// What handler_lanes.tide's OnNative hands its component to: C the compiler can't see into, so every lane of the
// component could have been written. An odd `by` changes q, an even one past 0 changes r, and 0 nothing.
void c_handler_lanes_touch(Plain *plain, const int32_t by)
{
    if (by & 1) plain->q += by;
    else if (by) plain->r += 1.0f;
}

// The world being ticked, which the test says: OnSly's C Sets the entity's component in it, a lane of it changed,
// while the handler that called holds its own copy of the row.
tide_world *handler_lanes_world;

void c_handler_lanes_sly(const Plain plain, const tide_entity entity) // By value, as its Tide declaration says
{
    Plain other = plain;
    other.s += 1000;
    if (handler_lanes_world) tide_set_Plain(handler_lanes_world, entity, other);
}
