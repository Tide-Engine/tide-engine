#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// Every tick's projection hash, over 150 ticks, hashed together. Built with
// TIDE_CLIENT_MEMO_OFF (tidec_e2e_visibility_hash_memo_off) nothing is kept
// from one hash to the next, and the hashes must be these too.
#define HASHES UINT64_C(0x80b9f7750afd02b9)

static uint64_t hash_of(const tide_world *w)
{
    return tide_game_api.client_hash(w, tide_player_from_index(0));
}

TIDE_TEST(visibility_hash_memo_follows_every_change)
{
    tide_world server = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    TIDE_CHECK(server.Catalog.filled);
    uint64_t hashes = TIDE_HASH_START, last = hash_of(&server);
    for (int t = 1; t <= 150; t++) {
        tide_world before = {0};
        tide_world_copy(&before, &server); // As a session keeps the tick before
        tide_world_tick(&server);
        (void)tide_world_hash(&server); // As the server hashes its world first
        const uint64_t hash = hash_of(&server);
        TIDE_CHECK(hash != last);
        TIDE_CHECK(hash_of(&server) == hash);
        TIDE_CHECK(hash_of(&before) == last); // The tick before, sharing what didn't change
        // Loaded into a world of its own, its heap laid out anew: the same hash
        uint32_t size;
        uint8_t *bytes = tide_game_api.client_state(&server, tide_player_from_index(0), &size);
        tide_world client = {0};
        TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
        // ...with what it kept of the last world loaded where it is, and on odd ticks
        // again with nothing kept: what's kept is for the eight worlds last hashed
        TIDE_CHECK(hash_of(&client) == hash);
        if (t % 2) {
            static tide_world others[8];
            for (int k = 0; k < 8; k++) (void)hash_of(&others[k]);
            TIDE_CHECK(hash_of(&client) == hash);
        }
        tide_world_free(&client);
        free(bytes);
        tide_world_free(&before);
        hashes = tide_hash_more(hashes, &hash, sizeof hash);
        last = hash;
    }
    hashes = tide_hash_end(hashes);
    if (hashes != HASHES) printf("hashes 0x%016" PRIx64 "\n", hashes);
    TIDE_CHECK(hashes == HASHES);
    tide_world_free(&server);
}

// Values changed where they are, deep in a singleton, between two hashes, with
// and without a snapshot sharing their pages: each change shows, and undoing it
// gives the hash back
TIDE_TEST(visibility_hash_memo_sees_changes_in_place)
{
    tide_world server = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    tide_world_tick(&server);
    const uint64_t at_first = hash_of(&server);
    for (int shared = 0; shared < 2; shared++) {
        tide_world snapshot = {0};
        if (shared) tide_world_copy(&snapshot, &server);
        tide_text_use(&server.heap, NULL);
        // A byte of a list in chunks
        uint8_t *b = tide_list_at_mut(server.Catalog.bytes, 23456, 1);
        TIDE_CHECK(b != NULL);
        *b ^= 1u;
        const uint64_t byte_changed = hash_of(&server);
        TIDE_CHECK(byte_changed != at_first);
        tide_text_use(&server.heap, NULL);
        b = tide_list_at_mut(server.Catalog.bytes, 23456, 1);
        *b ^= 1u;
        TIDE_CHECK(hash_of(&server) == at_first);
        // A number in a list in a list's element
        tide_text_use(&server.heap, NULL);
        const Site *site = tide_list_at(server.Catalog.sites, 77, sizeof(Site));
        int32_t *value = tide_list_at_mut(site->values, 3, sizeof(int32_t));
        TIDE_CHECK(value != NULL);
        *value += 1;
        const uint64_t value_changed = hash_of(&server);
        TIDE_CHECK(value_changed != at_first && value_changed != byte_changed);
        tide_text_use(&server.heap, NULL);
        site = tide_list_at(server.Catalog.sites, 77, sizeof(Site));
        value = tide_list_at_mut(site->values, 3, sizeof(int32_t));
        *value -= 1;
        TIDE_CHECK(hash_of(&server) == at_first);
        // A point of a frame of the ring
        tide_text_use(&server.heap, NULL);
        const Snap *frame = tide_list_at(server.Rewind.frames, 17, sizeof(Snap));
        Point *point = tide_list_at_mut(frame->points, 250, sizeof(Point));
        TIDE_CHECK(point != NULL);
        point->at.z = 0.5f;
        TIDE_CHECK(hash_of(&server) != at_first);
        tide_text_use(&server.heap, NULL);
        frame = tide_list_at(server.Rewind.frames, 17, sizeof(Snap));
        point = tide_list_at_mut(frame->points, 250, sizeof(Point));
        point->at.z = 0.0f;
        TIDE_CHECK(hash_of(&server) == at_first);
        if (shared) {
            TIDE_CHECK(hash_of(&snapshot) == at_first);
            tide_world_free(&snapshot);
        }
    }
    tide_world_free(&server);
}
