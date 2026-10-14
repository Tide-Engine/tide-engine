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

TIDE_TEST(json_all_checks_pass)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Results(&world, (tide_entity){1, 1}));
    const Results r = tide_get_Results(&world, (tide_entity){1, 1});
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    TIDE_CHECK(r.checks == 90);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // Every run cleared what it made
    tide_world_free(&world);
}

// What the match read is the world's own, in its heap: the bits a float was
// read as, the text and the lists, through a copy, the world as bytes, and a
// tick more on each. The hash is every platform's.
TIDE_TEST(json_reads_the_same_bits_everywhere)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Kept kept = tide_get_Kept(&world, (tide_entity){1, 1});
    static const uint32_t floats[] = {
        0x00000000u, 0x80000000u, 0x3F800000u, 0x3DCCCCCDu, 0x40490FDBu, 0x4B800000u, 0x00000001u,
        0x00000000u, 0x00000001u, 0x7F7FFFFFu, 0x00800000u, 0x007FFFFFu, 0x7F7FFFFFu, 0x42C80000u,
        0x00000000u, 0x3DFCD6EAu, 0x00000001u, 0x5A000001u, 0x3F800001u, 0x3F800002u, 0x3F800002u,
    };
    TIDE_REQUIRE(tide_list_read_count(&world.heap, kept.floats) == (int32_t)(sizeof floats / sizeof floats[0]));
    for (int32_t i = 0; i < (int32_t)(sizeof floats / sizeof floats[0]); i++) {
        uint32_t bits;
        memcpy(&bits, tide_list_read(&world.heap, kept.floats, i, sizeof(float)), sizeof bits);
        if (bits != floats[i]) printf("    float %d is 0x%08" PRIX32 ", expected 0x%08" PRIX32 "\n", (int)i, bits, floats[i]);
        TIDE_CHECK(bits == floats[i]);
    }
    TIDE_CHECK(is(tide_text_read(&world.heap, kept.numbers),
                  "{\"floats\":[0,-0,1,0.1,3.1415927,16777216,1E-45,0,1E-45,3.4028235E+38,1.1754944E-38,1.1754942E-38,"
                  "3.4028235E+38,100,0,0.12345679,1E-45,9007200000000000,1.0000001,1.0000002,1.0000002],"
                  "\"longs\":[9223372036854775807,-9223372036854775808,1000000000000000000,12],\"bytes\":[0,255]}"));
    TIDE_CHECK(is(tide_text_read(&world.heap, kept.info.name), "Lobby \"one\""));
    TIDE_CHECK(tide_list_read_count(&world.heap, kept.pieces) == 2 && tide_list_read_count(&world.heap, kept.bytes) == 10);

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
    if (hash != JSON_HASH) printf("    world: hash is %" PRIu64 "ull, expected %" PRIu64 "ull\n", hash, (uint64_t)JSON_HASH);
    TIDE_CHECK(hash == JSON_HASH);
    tide_world_free(&restored);
    tide_world_free(&copy);
    tide_world_free(&world);
}
