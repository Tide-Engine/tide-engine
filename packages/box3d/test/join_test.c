// A bounded host with no window. The writer saves a world and the hash it
// reaches ten ticks later; each reader starts in another process, with none
// of Box3D's state and without having called a system before unpacking it.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BOX3D_JOIN_HEADER
#define BOX3D_JOIN_HEADER "game.h"
#endif
#include BOX3D_JOIN_HEADER
#include "tide/net.h"

static tide_world world;

static void tick(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static bool write_state(const char *path)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tick(20);
    const uint64_t hash = tide_world_hash(&world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    if (!bytes) return false;
    if (tide_world_pack(&world, bytes, size) != size) {
        free(bytes);
        return false;
    }
    uint32_t delta_size = 0;
    uint8_t *delta = tide_world_pack_delta(&world, NULL, NULL, 0, &delta_size);
    if (!delta) {
        free(bytes);
        return false;
    }
    tick(10);
    tide_writer out = {.grows = true};
    tide_write_u64(&out, hash);
    tide_write_u64(&out, tide_world_hash(&world));
    tide_write_u32(&out, size);
    tide_write_u32(&out, delta_size);
    tide_write_bytes(&out, bytes, size);
    tide_write_bytes(&out, delta, delta_size);
    free(bytes);
    free(delta);
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

static bool read_state(const char *path, const bool delta)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? ftell(file) : -1;
    ok = ok && length >= 24 && (uint64_t)length <= UINT32_MAX && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)length) : NULL;
    ok = ok && bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        free(bytes);
        return false;
    }
    tide_reader in = {bytes, (uint32_t)length, 0, false};
    const uint64_t hash = tide_read_u64(&in);
    const uint64_t after = tide_read_u64(&in);
    const uint32_t size = tide_read_u32(&in);
    const uint32_t delta_size = tide_read_u32(&in);
    const uint8_t *whole = tide_read_bytes(&in, size);
    const uint8_t *parts = tide_read_bytes(&in, delta_size);
    ok = !in.failed && in.at == in.size;
    // The process has called no Box3D function before this: registration
    // belongs to library loading, not to its first simulation tick.
    ok = ok && (delta ? tide_world_unpack_delta(&world, NULL, parts, delta_size) : tide_world_unpack(&world, whole, size));
    ok = ok && world.Box3D_Physics.state.pages && tide_world_hash(&world) == hash;
    if (ok) {
        tick(10);
        ok = tide_world_hash(&world) == after;
    }
    tide_world_free(&world);
    free(bytes);
    return ok;
}

int main(const int argc, char **argv)
{
    if (argc != 3) return 2;
    bool ok;
    if (strcmp(argv[1], "write") == 0) ok = write_state(argv[2]);
    else if (strcmp(argv[1], "read") == 0) ok = read_state(argv[2], false);
    else if (strcmp(argv[1], "delta") == 0) ok = read_state(argv[2], true);
    else return 2;
    printf("%s: Box3D %s in a fresh process\n", ok ? "ok" : "FAIL", argv[1]);
    return ok ? 0 : 1;
}
