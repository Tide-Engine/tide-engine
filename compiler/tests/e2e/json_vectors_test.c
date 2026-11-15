#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

TIDE_TEST(json_vectors_all_checks_pass)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Results(&world, tide_entity_in_slot(&world.entities, 1)));
    const Results r = tide_get_Results(&world, tide_entity_in_slot(&world.entities, 1));
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    if (r.checks != JSON_VECTORS_CHECKS) printf("    %d checks, expected %d\n", (int)r.checks, JSON_VECTORS_CHECKS);
    TIDE_CHECK(r.checks == JSON_VECTORS_CHECKS);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // Every run cleared what it made
    tide_world_free(&world);
}

// What the match read is the world's own: the bits each number was read as,
// in the C value, the text written, and the world through a copy, as bytes
// and a tick more. The hash is every platform's.
TIDE_TEST(json_vectors_read_the_same_bits_everywhere)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Kept kept = tide_get_Kept(&world, tide_entity_in_slot(&world.entities, 1));
    const Pose *p = &kept.pose;
    uint32_t bits;
    memcpy(&bits, &p->at.x, sizeof bits);
    TIDE_CHECK(bits == 0x3DCCCCCDu); // 0.1
    memcpy(&bits, &p->at.y, sizeof bits);
    TIDE_CHECK(bits == 0x80000000u); // -0
    memcpy(&bits, &p->turn.value.y, sizeof bits);
    TIDE_CHECK(bits == 0x3F3504F3u); // 0.70710677
    uint64_t wide;
    memcpy(&wide, &p->far.z, sizeof wide);
    TIDE_CHECK(wide == 0x3FD3333333333334ull); // 0.30000000000000004
    TIDE_CHECK(p->cell.x == INT32_MIN && p->cell.y == INT32_MAX && p->tint.a == 1.0f);
    TIDE_CHECK(is(tide_text_read(&world.heap, kept.edges), "{\"2147483647\":1,\"-2147483648\":2}"));
    TIDE_CHECK(is(tide_text_read(&world.heap, kept.posed),
                  "{\"flat\":[1.5,-2],\"at\":[0.1,-0,300],\"wide\":[1,2,3,4],\"cell\":[-2147483648,2147483647],\"block\":[3,4,5],"
                  "\"box\":[1,2,3,4],\"far2\":[0.1,2],\"far\":[1E+300,-1E-300,0.30000000000000004],\"far4\":[1,2,3,4],"
                  "\"turn\":[0,0.70710677,0,0.70710677],\"tint\":[0.25,0.5,0.75,1],\"aim\":[0,1,0],\"path\":[[1,2,3],[4,5,6]],"
                  "\"marks\":{\"a\":[1,2]},\"corners\":[[1,2],[3,4]]}"));

    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    tide_world_tick(&world);
    tide_world_tick(&restored);
    tide_world_tick(&copy);
    const uint64_t hash = tide_world_hash(&world);
    TIDE_CHECK(tide_world_hash(&restored) == hash && tide_world_hash(&copy) == hash);
    if (hash != JSON_VECTORS_HASH) printf("    world: hash is %" PRIu64 "ull, expected %" PRIu64 "ull\n", hash, (uint64_t)JSON_VECTORS_HASH);
    TIDE_CHECK(hash == JSON_VECTORS_HASH);
    tide_world_free(&restored);
    tide_world_free(&copy);
    tide_world_free(&world);
}
