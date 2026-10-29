#include "game.h"
#include "tide_test.h"
#include <stdlib.h>
#include <string.h>

extern int tide_packed_rows_mode;
extern uint64_t tide_packed_rows_passes, tide_packed_rows_count;
static uint32_t recorded;
void Record(int32_t id) { recorded = recorded * 31u + (uint32_t)id; }

TIDE_TEST(packed_rows_restore_order_before_other_consumers)
{
    tide_world plain = {0}, packed = {0}, kept = {0}, restored = {0};
    tide_world_init(&plain, 1.0f / 64.0f);
    tide_world_copy(&packed, &plain);
    tide_packed_rows_passes = tide_packed_rows_count = 0;
    for (int tick = 0; tick < 12; tick++) {
        tide_world_copy(&kept, &packed);
        const uint64_t kept_hash = tide_world_hash(&kept);
        recorded = 0;
        tide_packed_rows_mode = 0;
        tide_world_tick(&plain);
        const uint32_t before = recorded;
        recorded = 0;
        tide_packed_rows_mode = 2;
        tide_world_tick(&packed);
        TIDE_CHECK(recorded == before);
        TIDE_CHECK(tide_world_hash(&plain) == tide_world_hash(&packed));
        TIDE_CHECK(tide_world_hash(&kept) == kept_hash);
        TIDE_CHECK(plain.Observed.first == 0 && packed.Observed.first == 0);
        const size_t size = tide_world_pack(&plain, NULL, 0);
        void *a = malloc(size), *b = malloc(size);
        TIDE_CHECK(a && b);
        TIDE_CHECK(tide_world_pack(&plain, a, size) == size);
        TIDE_CHECK(tide_world_pack(&packed, b, size) == size);
        TIDE_CHECK(memcmp(a, b, size) == 0);
        TIDE_CHECK(tide_world_unpack(&restored, b, size));
        TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&packed));
        free(a); free(b);
    }
    TIDE_CHECK(tide_packed_rows_passes == 12u);
    TIDE_CHECK(tide_packed_rows_count == 12u * 4103u);
    tide_world_free(&plain); tide_world_free(&packed);
    tide_world_free(&kept); tide_world_free(&restored);
    tide_packed_rows_mode = 0;
}
