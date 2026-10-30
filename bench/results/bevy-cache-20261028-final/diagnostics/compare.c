#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "wide.h"

static tide_world world;

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const int counts[] = {0, 1, 65, 2049, 4099};
    const int frozen[] = {0, 50, 95, 99, 100};
    for (size_t c = 0; c < sizeof counts / sizeof *counts; c++) {
        for (size_t f = 0; f < sizeof frozen / sizeof *frozen; f++) {
            tide_world_init(&world, 1.0f / 60.0f);
            world.Yard.count = counts[c];
            world.Yard.tagged = true;
            world.Yard.frozen = frozen[f];
            for (int tick = 1; tick <= 17; tick++) {
                tide_world_tick(&world);
                if (tick != 1 && tick != 3 && tick != 17) continue;
                const uint32_t size = tide_world_pack(&world, NULL, 0);
                uint8_t *bytes = malloc(size);
                if (!bytes || tide_world_pack(&world, bytes, size) != size) return 2;
                char path[1024];
                const int length = snprintf(path, sizeof path, "%s/%d-%d-%d.pack", argv[1], counts[c], frozen[f], tick);
                if (length < 0 || (size_t)length >= sizeof path) return 3;
                FILE *out = fopen(path, "wb");
                if (!out || fwrite(bytes, 1, size, out) != size || fclose(out) != 0) return 4;
                free(bytes);
                printf("{\"count\":%d,\"frozen\":%d,\"tick\":%d,\"world_size\":%zu,\"pack_size\":%" PRIu32 ",\"hash\":\"%016" PRIx64 "\"}\n",
                       counts[c], frozen[f], tick, sizeof world, size, tide_world_hash(&world));
            }
            tide_world_free(&world);
        }
    }
    return 0;
}
