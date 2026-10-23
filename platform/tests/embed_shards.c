// A sharded game inside another program (tide/embed.h): platform/tests/embed_shards.tide,
// split in two along x, its view watching the whole world.

#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/embed_game.h"
#include "tide/run.h"
#include "tide_test.h"

static char program[] = "embed", shards[] = "--shards", two[] = "2", width[] = "--shard-width", twenty[] = "20";

// The packet's Rock section: each rock's id, by how many times it's there
static uint32_t rocks(const uint8_t *packet, const uint32_t size, int seen[6])
{
    memset(seen, 0, 6 * sizeof *seen);
    uint32_t entities, sections;
    memcpy(&entities, packet + 36, sizeof entities);
    memcpy(&sections, packet + 40, sizeof sections);
    uint32_t at = TIDE_PACKET_HEADER + entities * TIDE_PACKET_ENTITY;
    uint32_t found = 0;
    for (uint32_t s = 0; s < sections && at + TIDE_PACKET_SECTION <= size; s++) {
        at = (at + 7u) & ~7u;
        uint32_t head[6];
        memcpy(head, packet + at, sizeof head);
        const uint32_t body = at + TIDE_PACKET_SECTION;
        if (head[0] == TIDE_PACKET_COMPONENT && head[3] == sizeof(Rock)) {
            const uint32_t values = body + ((head[2] * 4u + 7u) & ~7u);
            for (uint32_t i = 0; i < head[2] && values + (i + 1u) * sizeof(Rock) <= size; i++) {
                Rock rock;
                memcpy(&rock, packet + values + i * sizeof(Rock), sizeof rock);
                if (rock.id >= 0 && rock.id < 6) seen[rock.id]++;
                found++;
            }
        }
        at = body + head[4];
    }
    return found;
}

TIDE_TEST(embed_draws_every_shard_it_watches)
{
    char *argv[] = {program, shards, two, width, twenty};
    tide_embed *e = tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = 5, .argv = argv});
    TIDE_REQUIRE(e != NULL);
    for (int i = 0; i < 240; i++) tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
    uint32_t size = 0;
    const uint8_t *packet = tide_embed_view(e, &size);
    TIDE_REQUIRE(packet != NULL && size >= TIDE_PACKET_HEADER);
    int seen[6];
    TIDE_CHECK(rocks(packet, size, seen) == 6); // Every rock, from whichever region has it...
    for (int id = 0; id < 6; id++) TIDE_CHECK(seen[id] == 1); // ...once
    tide_embed_destroy(e);
}
