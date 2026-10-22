// Canonical delta files transfer between native and WASM as well as between
// fresh processes. No model cache or native registration has to run first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "robot.h"
#include "tide/net.h"

static tide_world world;

static void run(int count)
{
    for (int i = 0; i < count; ++i) tide_world_tick(&world);
}

static bool write_state(const char *path)
{
    tide_world_init(&world, 1.0f / 120.0f);
    run(40);
    const uint64_t before = tide_world_hash(&world);
    uint32_t size = 0;
    uint8_t *bytes = tide_world_pack_delta(&world, NULL, NULL, 0, &size);
    if (!bytes) return false;
    run(20);
    tide_writer out = {.grows = true};
    tide_write_u64(&out, before);
    tide_write_u64(&out, tide_world_hash(&world));
    tide_write_u32(&out, size);
    tide_write_bytes(&out, bytes, size);
    free(bytes);
    FILE *file = fopen(path, "wb");
    bool ok = file && !out.overflow;
    if (file) {
        ok = ok && fwrite(out.data, 1, out.size, file) == out.size;
        ok = fclose(file) == 0 && ok;
    }
    free(out.data);
    tide_world_free(&world);
    return ok;
}

static bool read_state(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? ftell(file) : -1;
    ok = ok && length >= 20 && (uint64_t)length <= UINT32_MAX && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)length) : NULL;
    ok = ok && bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (!ok) { free(bytes); return false; }
    tide_reader in = {bytes, (uint32_t)length, 0, false};
    const uint64_t before = tide_read_u64(&in);
    const uint64_t after = tide_read_u64(&in);
    const uint32_t size = tide_read_u32(&in);
    const uint8_t *data = tide_read_bytes(&in, size);
    ok = !in.failed && in.at == in.size && tide_world_unpack_delta(&world, NULL, data, size);
    ok = ok && tide_world_hash(&world) == before;
    if (ok) {
        run(20);
        ok = world.Newton_Physics.status == Newton_Status_Ok && tide_world_hash(&world) == after;
    }
    tide_world_free(&world);
    free(bytes);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const bool ok = strcmp(argv[1], "write") == 0 ? write_state(argv[2]) :
                    strcmp(argv[1], "read") == 0 && read_state(argv[2]);
    printf("%s: Newton fresh-process %s\n", ok ? "ok" : "FAIL", argv[1]);
    return ok ? 0 : 1;
}
