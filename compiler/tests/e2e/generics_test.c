#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static bool said(const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&world.heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

static void start(void)
{
    tide_world_free(&world);
    world = (tide_world){0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
}

TIDE_TEST(generics_infer_or_take_type_arguments)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->bigInt == 7);
    TIDE_CHECK(r->bigFloat == 2.0f); // Max<float>(2, 1.5): the int converts to the float written in <>
    TIDE_CHECK(r->converted == 4.0f); // Convert<int, Holder>: U only from <>
    TIDE_CHECK(r->bucketTotal == 3);  // K and V from Map<K, List<V>>
    TIDE_CHECK(r->depth == 5);        // Recursion through its own instantiation
    TIDE_CHECK(r->emptyIsNull);
    TIDE_CHECK(r->first == 9);
}

TIDE_TEST(generics_plain_function_wins_over_generic)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(said(r->plainPick, "plain"));
    TIDE_CHECK(said(r->genericPick, "generic"));
}

TIDE_TEST(generics_has_writes_only_the_fields_a_type_has)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->cornerCount == 2);
    TIDE_CHECK(r->dotCount == 2);
    const Corner *c = tide_list_at(r->corners, 1, sizeof(Corner));
    TIDE_CHECK(c->position.x == 2 && c->position.y == 4 && c->position.z == 6);
    TIDE_CHECK(c->normal.y == 1);
    const Dot *d = tide_list_at(r->dots, 0, sizeof(Dot));
    TIDE_CHECK(d->position.x == 4 && d->position.y == 5); // A float2 position takes x and y
    TIDE_CHECK(d->tag == 7);                              // Its other field keeps its default
}
