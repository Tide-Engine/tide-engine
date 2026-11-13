#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// The loaded world's own hash, pages and heap alike, as the codecs read lists
// one element at a time (tidec before runs, 2026-11-02): reading them in runs
// must leave every block where it was.
#define LOADED_WORLD_HASH UINT64_C(0xfceec78bc1ff1776)

static uint8_t *project(const tide_world *w, uint32_t *size)
{
    return tide_game_api.client_state(w, tide_player_from_index(0), size);
}

TIDE_TEST(visibility_lists_read_back_as_they_were_written)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    TIDE_CHECK(server.Bulk.filled);
    uint32_t size;
    uint8_t *bytes = project(&server, &size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    tide_text_use(&client.heap, NULL);
    TIDE_CHECK(tide_list_count(client.Bulk.big) == 3000 && tide_list_count(client.Bulk.bytes) == 20000);
    const Point *last = tide_list_at(client.Bulk.big, 2999, sizeof(Point));
    TIDE_CHECK(last && last->id == 2999 * 7 && last->at.y == 1499.5f);
    const uint8_t *b = tide_list_at(client.Bulk.bytes, 12345, 1);
    TIDE_CHECK(b && *b == (uint8_t)(12345 * 31));
    const Cell *cell = tide_list_at(client.Bulk.cells, 36, sizeof(Cell));
    TIDE_CHECK(cell && cell->value == 36 * 36 && cell->solid);
    const tide_list *row = tide_list_at(client.Bulk.rows, 8, sizeof(tide_list));
    TIDE_CHECK(row && tide_list_count(*row) == 4800);

    // The same bytes again, and the same hash as the server's
    uint32_t again_size;
    uint8_t *again = project(&client, &again_size);
    TIDE_CHECK(again_size == size && memcmp(again, bytes, size) == 0);
    TIDE_CHECK(tide_game_api.client_hash(&client, tide_player_from_index(0))
               == tide_game_api.client_hash(&server, tide_player_from_index(0)));

    // Where every block went: as reading element by element put them
    const uint64_t loaded = tide_world_hash(&client);
    if (loaded != LOADED_WORLD_HASH) printf("loaded world hash 0x%016" PRIx64 "\n", loaded);
    TIDE_CHECK(loaded == LOADED_WORLD_HASH);

    // Over a loaded world, the same again; cut short anywhere, refused, the world as it was
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_world_hash(&client) == loaded);
    for (uint32_t cut = 0; cut < size; cut += cut + 64u < size ? 61u : 1u) {
        TIDE_CHECK(!tide_game_api.load_client_state(&client, bytes, cut));
    }
    TIDE_CHECK(tide_world_hash(&client) == loaded);
    free(again);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

// A world after a tick, as a projection: the bytes of its tick from a copy
static uint8_t *ticked_projection(const tide_world *w, uint32_t *size)
{
    tide_world copy = {0};
    tide_world_copy(&copy, w);
    tide_world_tick(&copy);
    uint8_t *bytes = project(&copy, size);
    tide_world_free(&copy);
    return bytes;
}

TIDE_TEST(visibility_lists_reload_over_the_last_projection)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    uint32_t had_size;
    uint8_t *had = project(&server, &had_size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, had, had_size));
    TIDE_CHECK(tide_game_api.reload_client_state(&client, had, had_size, had, had_size)); // Nothing changed
    uint32_t same_size;
    uint8_t *same = project(&client, &same_size);
    TIDE_CHECK(same_size == had_size && memcmp(same, had, had_size) == 0);
    free(same);
    uint32_t most_pages = 0, fresh_pages = 0, kept = 0;
    for (int t = 1; t <= 120; t++) {
        tide_world_tick(&server);
        uint32_t size;
        uint8_t *bytes = project(&server, &size);
        if (t % 10 == 0) { // Refused, the world as it was
            const uint64_t before = tide_world_hash(&client);
            TIDE_CHECK(!tide_game_api.reload_client_state(&client, bytes, size - 1u, had, had_size));
            TIDE_CHECK(!tide_game_api.reload_client_state(&client, bytes, size, had, had_size - 1u));
            TIDE_CHECK(!tide_game_api.reload_client_state(&client, bytes, 7u, had, had_size));
            TIDE_CHECK(tide_world_hash(&client) == before);
        }
        tide_world before = {0};
        tide_world_copy(&before, &client);
        TIDE_CHECK(tide_game_api.reload_client_state(&client, bytes, size, had, had_size));
        // Bulk kept, not read again: its elements still on the page it shares with the world before
        tide_text_use(&before.heap, NULL);
        const void *was = tide_list_at(before.Bulk.big, 0, sizeof(Point));
        tide_text_use(&client.heap, NULL);
        if (tide_list_at(client.Bulk.big, 0, sizeof(Point)) == was) kept++;
        tide_world_free(&before);
        // What it loaded is the server's projection, as a fresh load is
        uint32_t again_size;
        uint8_t *again = project(&client, &again_size);
        TIDE_CHECK(again_size == size && memcmp(again, bytes, size) == 0);
        TIDE_CHECK(tide_game_api.client_hash(&client, tide_player_from_index(0))
                   == tide_game_api.client_hash(&server, tide_player_from_index(0)));
        tide_world fresh = {0};
        TIDE_CHECK(tide_game_api.load_client_state(&fresh, bytes, size));
        TIDE_CHECK(tide_world_entity_count(&fresh) == tide_world_entity_count(&client));
        // ...and a tick predicted from it comes out as from a fresh load
        uint32_t a_size, b_size;
        uint8_t *a = ticked_projection(&client, &a_size), *b = ticked_projection(&fresh, &b_size);
        TIDE_CHECK(a_size == b_size && memcmp(a, b, a_size) == 0);
        if (tide_heaps_pages(&client.heap) > most_pages) most_pages = tide_heaps_pages(&client.heap);
        fresh_pages = tide_heaps_pages(&fresh.heap);
        free(a); free(b); free(again);
        tide_world_free(&fresh);
        free(had);
        had = bytes;
        had_size = size;
    }
    // Blocks let go of are used again: the heap stays near a fresh load's
    TIDE_CHECK(most_pages <= fresh_pages * 2u + 4u);
    TIDE_CHECK(kept >= 30); // Ticks that changed none of Bulk
    free(had);
    tide_world_free(&client);
    tide_world_free(&server);
}
