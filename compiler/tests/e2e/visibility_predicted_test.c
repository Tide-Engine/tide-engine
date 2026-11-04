#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A client's run of a tick is checked against the server's projection after
// it (visibility_predicted.tide): the session repairs a client whose run of a
// tick differs, so anything a client can work out for itself must come out as
// the server's projection says.

static tide_player_id player(void) { return tide_player_from_index(0); }

static bool contains(const uint8_t *bytes, const uint32_t size, const void *what, const uint32_t n)
{
    for (uint32_t i = 0; i + n <= size; i++) if (memcmp(bytes + i, what, n) == 0) return true;
    return false;
}

// The live entities with a Secret, and how many of them the client may see
static int secrets(const tide_world *w, int *visible)
{
    int n = 0;
    *visible = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_entity_alive(&w->entities, e) || !tide_has_Secret(w, e)) continue;
        n++;
        *visible += tide_world_visible(w, e, player());
    }
    return n;
}

// Each tick: the server's projection loaded, ticked by the client and by the server
static void client_ticks(const bool shown)
{
    tide_world server = {0};
    tide_world_init(&server, 0.1f);
    server.Mode.shown = shown;
    tide_world_tick(&server);
    for (int t = 0; t < 24; t++) {
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&server, player(), &size);
        tide_world client = {0};
        TIDE_CHECK(bytes && tide_game_api.load_client_state(&client, bytes, size));
        int visible = 0;
        TIDE_CHECK(secrets(&client, &visible) == 0);
        const int32_t shown_before = client.Count.shown;
        tide_world_tick(&client);
        tide_world_tick(&server);
        // The client made its Secret too, out of its own sight
        TIDE_CHECK(secrets(&client, &visible) == (shown ? 0 : 1) && visible == 0);
        TIDE_CHECK(client.Count.shown == server.Count.shown && client.Count.secrets == server.Count.secrets);
        // Its projection is the server's, bytes and hash, but for the number of
        // a group it made: the server's directory numbers them
        uint32_t a = 0, b = 0;
        uint8_t *mine = tide_game_api.client_state(&client, player(), &a);
        uint8_t *theirs = tide_game_api.client_state(&server, player(), &b);
        const bool made = client.Count.shown != shown_before; // A Walker came on in this tick
        if (!made && (a != b || memcmp(mine, theirs, a) != 0)) printf("tick %d: client %u bytes, server %u\n", t, a, b);
        TIDE_CHECK(a == b && (made || memcmp(mine, theirs, a) == 0));
        TIDE_CHECK(made || tide_game_api.client_hash(&client, player()) == tide_game_api.client_hash(&server, player()));
        if (shown) {
            int walkers = 0, seen = 0;
            for (uint32_t i = 0; i < client.entities.next_unused; i++) {
                const tide_entity e = tide_entity_in_slot(&client.entities, i);
                if (!tide_entity_alive(&client.entities, e) || !tide_has_Walker(&client, e)) continue;
                walkers++;
                seen += tide_world_visible(&client, e, player());
            }
            TIDE_CHECK(walkers == seen && walkers == 1 + client.Count.shown);
        }
        free(mine);
        free(theirs);
        free(bytes);
        tide_world_free(&client);
    }
    TIDE_CHECK(shown ? server.Count.shown >= 5 : server.Count.secrets >= 24);
    tide_world_free(&server);
}

TIDE_TEST(visibility_a_client_tick_leaves_out_what_it_spawns_out_of_its_sight)
{
    client_ticks(false);
}

TIDE_TEST(visibility_a_client_tick_shows_what_it_spawns_into_its_groups_as_the_server_does)
{
    client_ticks(true); // With no group number either side
}

TIDE_TEST(visibility_projections_leave_native_fields_out)
{
    tide_world server = {0};
    tide_world_init(&server, 0.1f);
    for (int i = 0; i < 3; i++) tide_world_tick(&server);
    const int64_t state = server.Library.state;
    TIDE_CHECK(state == INT64_C(0x7A11D00D5EC12E8) && tide_list_count(server.Library.made) == 2);

    uint32_t size = 0;
    uint8_t *bytes = tide_game_api.client_state(&server, player(), &size);
    TIDE_CHECK(!contains(bytes, size, &state, sizeof state)); // The library's state isn't sent
    tide_world client = {0};
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(client.Library.state == 0 && tide_list_count(client.Library.made) == 0 && client.Library.setting == 3);
    for (uint32_t i = 0; i < client.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&client.entities, i);
        if (tide_entity_alive(&client.entities, e) && tide_has_Handle(&client, e)) {
            TIDE_CHECK(tide_get_Handle(&client, e).made == 0 && tide_get_Handle(&client, e).kind > 0);
        }
    }
    // The client attaches its handles again, its own: the projections stay alike
    tide_world_tick(&client);
    tide_world_tick(&server);
    TIDE_CHECK(client.Library.state == INT64_C(0x7A11D00D5EC12E8));
    TIDE_CHECK(tide_game_api.client_hash(&client, player()) == tide_game_api.client_hash(&server, player()));
    // ...over a reload of the next projection too
    uint32_t next_size = 0;
    uint8_t *next = tide_game_api.client_state(&server, player(), &next_size);
    tide_world taken = {0};
    TIDE_CHECK(tide_game_api.load_client_state(&taken, bytes, size));
    TIDE_CHECK(tide_game_api.reload_client_state(&taken, next, next_size, bytes, size));
    TIDE_CHECK(taken.Library.state == 0);
    TIDE_CHECK(tide_game_api.client_hash(&taken, player()) == tide_game_api.client_hash(&server, player()));
    free(next);
    free(bytes);
    tide_world_free(&taken);
    tide_world_free(&client);
    tide_world_free(&server);
}
