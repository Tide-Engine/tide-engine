// A client ticks its projection as the server ticks the match (projected.tide).
// The projection leaves Box3D's state out: its Resident, and the [Native]
// handles and bookkeeping in Physics and the bodies. The client's tick makes
// Box3D's world again from the Bodies, so its probes find what the server's
// find, and its projection after the tick is the server's.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "projected.h"
#include "tide_test.h"

// bridge.c
void b3t_forget(void);

#define DT (1.0f / 60.0f)

static tide_player_id player(void) { return tide_player_from_index(0); }

static int hits(const tide_world *w)
{
    int n = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_entity_alive(&w->entities, e) || !tide_has_Probe(w, e)) continue;
        n += tide_get_Probe(w, e).distance >= 0;
    }
    return n;
}

TIDE_TEST(box3d_a_client_ticks_its_projection_as_the_server_ticks_the_match)
{
    b3t_forget();
    tide_world server = {0};
    tide_world_init(&server, DT);
    for (int i = 0; i < 3; i++) tide_world_tick(&server);
    TIDE_CHECK(server.Box3D_Physics.bodies >= 3 && server.Box3D_Physics.stamp != 0);

    int found = 0, made = 0;
    for (int t = 0; t < 60; t++) {
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&server, player(), &size);
        tide_world client = {0};
        TIDE_CHECK(bytes && tide_game_api.load_client_state(&client, bytes, size));
        // Box3D's own state stays on the server
        TIDE_CHECK(client.Box3D_Physics.bodies == 0 && client.Box3D_Physics.stamp == 0);
        TIDE_CHECK(tide_map_count(client.Box3D_Physics.owners) == 0);
        const int32_t before = server.Box3D_Physics.bodies;
        tide_world_tick(&client);
        tide_world_tick(&server);
        made += server.Box3D_Physics.bodies > before;
        TIDE_CHECK(client.Box3D_Physics.bodies > 0); // Made again, on the client
        found += hits(&server) == 2;
        const uint64_t want = tide_game_api.client_hash(&server, player());
        const uint64_t got = tide_game_api.client_hash(&client, player());
        if (want != got) printf("tick %d: client 0x%016llx, server 0x%016llx (hits %d and %d)\n", t,
                                (unsigned long long)got, (unsigned long long)want, hits(&client), hits(&server));
        TIDE_CHECK(want == got);
        uint32_t a = 0, b = 0;
        uint8_t *mine = tide_game_api.client_state(&client, player(), &a);
        uint8_t *theirs = tide_game_api.client_state(&server, player(), &b);
        TIDE_CHECK(a == b && memcmp(mine, theirs, a) == 0);
        free(mine);
        free(theirs);
        free(bytes);
        tide_world_free(&client);
    }
    TIDE_CHECK(found > 40); // The probes hit something on most ticks
    TIDE_CHECK(made >= 5);  // ...and bodies came on the way
    tide_world_free(&server);
    b3t_forget();
}
