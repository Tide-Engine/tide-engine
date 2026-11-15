#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

TIDE_TEST(defines_are_the_files_the_builds_and_the_packages)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Picked.own == 1);
    TIDE_CHECK(world.Picked.fastHere == 0); // #undef, in that file alone
    TIDE_CHECK(world.Picked.fastThere == 1);
    TIDE_CHECK(world.Picked.localThere == 0); // A file's #define is its own
    TIDE_CHECK(world.Picked.fromBuild == 1);
    TIDE_CHECK(world.Picked.unknown == 0);
    TIDE_CHECK(world.Picked.debugging == DEFINES_DEBUG);
    TIDE_CHECK(world.Picked.withExtra == 7);
    TIDE_CHECK(world.Picked.withBox2D == 0);
    tide_world_free(&world);
}

// defined(NAME) and define(NAME, default): constants of what the build's
// --define NAME=value, its --define NAME, or the file's #define says.
TIDE_TEST(defines_are_read_by_code_with_their_values)
{
    tide_world_init(&world, 1.0f);
    const Picked *p = &world.Picked;
    TIDE_CHECK(p->prepass && !p->saysFast && p->saysLocal && p->saysWide && p->saysSide && p->saysNew);
    TIDE_CHECK(p->sideCount == 50 && p->unset == -12 && p->big == INT64_MIN && p->tiny == 255);
    TIDE_CHECK(p->scale == -2.5f && p->distance == 0.1);
    TIDE_CHECK(is(tide_text_read(&world.heap, p->title), "Two \"words\", a \\ and a tab:\t.") && is(tide_text_read(&world.heap, p->empty), ""));
    TIDE_CHECK(p->quick && !p->slow && p->army == Army_Huge);
    TIDE_CHECK(p->sizes.at[0] == 50 && p->sizes.at[1] == 2);
    TIDE_CHECK(!p->localInExtra && p->sideInExtra == 50);
    tide_world_tick(&world);
    TIDE_CHECK(world.Picked.inCode == 100);
    tide_world_free(&world);
}
