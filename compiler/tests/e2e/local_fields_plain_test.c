#include "local_fields_same.h"

static tide_world world;
static tide_world base;

// The game without local fields: the same world as with them
TIDE_TEST(local_fields_plain_match_is_the_same)
{
    same_match_check(&world, &base, NULL);
    tide_world_free(&world);
    tide_world_free(&base);
}
