// Includes freshly generated real bench/versus/wide/wide.tide.
#include "wide.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

int main(int argc, char **argv)
{
    CHECK(!getenv("TIDE_PACKED_MODE"));
    CHECK(!getenv("TIDE_PACKED_REPORT"));
    CHECK(argc == 6);
    const int count = atoi(argv[1]), frozen = atoi(argv[2]), mode = atoi(argv[3]);
    const int snapshot = atoi(argv[4]), churn = atoi(argv[5]);
    CHECK(count > 0 && count <= 1000000 && frozen >= 0 && frozen <= 100 && mode >= 0 && mode <= 2);
    tide_world w = {0}, kept = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    w.Yard.count = count; w.Yard.frozen = frozen; w.Yard.tagged = true;
    tide_packed_rows_mode = 0;
    tide_world_tick(&w);
    tide_packed_rows_mode = mode;
    uint64_t digest = 0;
    uint64_t hashes[40];
    double samples[30];
    const tide_columns *columns = &tide_columns_arch1_Crate;
    for (uint32_t i = 0; i < 40; ++i) {
        const uint64_t begin = tide_time_now_ns();
        if (snapshot) tide_world_copy(&kept, &w);
        if (churn) {
            for (uint32_t j = 0; j < 16; ++j) {
                const uint32_t row = (i * 257u + j * 997u) % w.arch1_Crate.count;
                uint8_t *present = tide_table_cell(&w.arch1_Crate, columns, row, columns->count - 2u);
                *present = !*present;
            }
        }
        tide_world_tick(&w);
        CHECK(!w.arch0_Body.mapping && !w.arch1_Crate.mapping);
        const uint64_t hash = tide_world_hash(&w);
        hashes[i] = hash;
        const uint64_t end = tide_time_now_ns();
        digest = digest * UINT64_C(6364136223846793005) + hash;
        if (i >= 10) samples[i - 10u] = (double)(end - begin) / 1000000.0;
    }
    if (mode == 2 && frozen > 0 && frozen < 100) CHECK(tide_packed_rows_passes > 0);
    printf("{\"count\":%d,\"frozen\":%d,\"mode\":%d,\"snapshot\":%d,\"churn\":%d,\"passes\":%llu,\"rows\":%llu,\"digest\":\"%llu\",\"final\":\"%llu\",\"samples_ms\":[",
           count, frozen, mode, snapshot, churn, (unsigned long long)tide_packed_rows_passes, (unsigned long long)tide_packed_rows_count,
           (unsigned long long)digest, (unsigned long long)tide_world_hash(&w));
    for (uint32_t i = 0; i < 30; ++i) printf("%s%.6f", i ? "," : "", samples[i]);
    printf("],\"hashes\":[");
    for (uint32_t i = 0; i < 40; ++i) printf("%s\"%llu\"", i ? "," : "", (unsigned long long)hashes[i]);
    puts("]}");
    tide_world_free(&w); tide_world_free(&kept);
    return 0;
}
