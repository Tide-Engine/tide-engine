// Cover the new per-place state as well as the bench's physical mechanics.
// Each result is replayed from a snapshot and round-tripped through packed
// bytes; the combined hash is compared between native and WASI runs.
static void campaign_replay(void)
{
    uint64_t combined = UINT64_C(1469598103934665603);
    for (int id = RECEPTION; id < PLACES; id++) {
        tide_world world = {0}, saved = {0}, packed = {0};
        start(&world);
        choose(&world, 0, id);
        input(&world, 0, (PilotInput){.move = {.3f, .6f}, .look = {.015f, 0}});
        ticks(&world, 20);
        tide_world_copy(&saved, &world);
        ticks(&world, 90);
        const uint64_t expected = tide_world_hash(&world);
        tide_world_copy(&world, &saved);
        ticks(&world, 90);
        CHECK(tide_world_hash(&world) == expected);
        const uint32_t size = tide_world_pack(&world, NULL, 0);
        uint8_t *bytes = malloc(size);
        CHECK(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
        CHECK(tide_world_unpack(&packed, bytes, size) && tide_world_hash(&packed) == expected);
        combined = (combined ^ expected) * UINT64_C(1099511628211);
        free(bytes);
        tide_world_free(&packed);
        tide_world_free(&saved);
        tide_world_free(&world);
    }
    printf("FOLDLINE campaign hash: %llu\n", (unsigned long long)combined);
}
