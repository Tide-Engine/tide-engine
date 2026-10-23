#include "game.h"
#include "tide/hosting.h"
#include "tide/platform.h"
#include "tide/time.h"

#include <stdio.h>
#include <string.h>

// A player of a sharded world on a dedicated server (tide_serve --shards 2,
// platform/tests/server_pack.mjs): let in at its front, match 0, by UDP or
// through the server's room, it follows its avatar into the left region's
// match, then walks it right across the border into the right region's, each
// a match it's let into over the one connection it joined by.

static double now;
static double walk_until;

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){.move = {now < walk_until ? 1.0f : 0.0f, 0.0f}};
}

// The avatar in the world this machine plays in, if that world owns it
static bool avatar(const tide_session *s, Avatar *found)
{
    const tide_world *w = tide_session_world(s);
    int owned = 0;
    for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
        if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
            index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
            continue;
        }
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (tide_entity_is_null(e) || !tide_has_Avatar(w, e) || tide_has_tide_ghost(w, e)) continue;
        *found = tide_get_Avatar(w, e);
        owned++;
    }
    return owned == 1;
}

int main(const int argc, char **argv)
{
    if (argc != 2) return 2;
    const char *address = argv[1];
    const bool room = strlen(address) == TIDE_ROOM_CODE_LENGTH;
    tide_transport transport;
    tide_address server;
    if (!(room ? tide_platform_room_join(address, &transport, &server)
               : tide_platform_resolve(address, 7777, &server) && tide_platform_udp_open(0, &transport))) return 2;
    now = (double)tide_time_now_ns() * 1e-9;
    tide_hosting_client_desc d = {.transport = transport, .server = server, .nonce = tide_platform_entropy(), .secure = room};
    memcpy(d.simulation, tide_game_api.simulation, 32);
    tide_hosting_client *admission = tide_hosting_client_create(&d, now);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample,
                                                               .entropy = tide_platform_entropy});
    if (!admission || !s) return 2;
    const double deadline = now + (room ? 20.0 : 12.0);
    bool joined = false, ok = false;
    Avatar a = {0};
    double frame = 0.0;
    while (now < deadline) {
        now = (double)tide_time_now_ns() * 1e-9;
        // A frame at a time, as a host updates: every update sends, and a
        // hosted world's router allows each admission so many a second
        if (now - frame < 1.0 / 60.0) {
            tide_yield();
            continue;
        }
        frame = now;
        tide_hosting_client_update(admission, now);
        if (tide_hosting_client_result(admission) > TIDE_HOSTING_ACCEPTED) {
            fprintf(stderr, "sharded server client: not let in (%d)\n", (int)tide_hosting_client_result(admission));
            break;
        }
        if (!joined && tide_hosting_client_result(admission) == TIDE_HOSTING_ACCEPTED) {
            tide_session_join(s, tide_hosting_client_transport(admission), tide_hosting_client_address(admission), now);
            joined = true;
        }
        tide_session_update(s, now);
        // Once it plays where its avatar is, in the left region, it walks
        // right for three seconds: 24 units, from x = -12 to 12
        if (walk_until == 0.0 && avatar(s, &a) && a.position.x < 0.0f) walk_until = now + 3.0;
        if (walk_until != 0.0 && now > walk_until + 1.0) {
            const tide_session_status status = tide_session_status_of(s);
            ok = avatar(s, &a) && a.position.x > 10.0f && a.position.x < 14.0f
              && status.client.state == TIDE_SESSION_CONNECTED && status.client.resyncs == 0;
            break;
        }
        tide_yield();
    }
    fprintf(stderr, "sharded server client%s: %s (avatar at x = %.2f)\n", room ? " by room" : "", ok ? "ok" : "FAILED",
            (double)a.position.x);
    tide_session_destroy(s);
    tide_hosting_client_destroy(admission);
    return ok ? 0 : 1;
}
