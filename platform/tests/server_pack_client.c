#include "game.h"
#include "tide/hosting.h"
#include "tide/pack.h"
#include "tide/platform.h"
#include "tide/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t tide_game_pack[];
extern const size_t tide_game_pack_size;

int main(const int argc, char **argv)
{
    if (argc != 2 || !tide_assets_load(tide_game_pack, tide_game_pack_size)) return 2;
    tide_transport transport;
    tide_address server;
    const bool room = strlen(argv[1]) == TIDE_ROOM_CODE_LENGTH;
    if (!(room ? tide_platform_room_join(argv[1], &transport, &server)
               : tide_platform_resolve(argv[1], 7777, &server) && tide_platform_udp_open(0, &transport))) return 2;
    double now = (double)tide_time_now_ns() * 1e-9;
    const tide_hosting_client_desc d = {.transport = transport, .server = server,
        .nonce = tide_platform_entropy(), .secure = room};
    tide_hosting_client *admission = tide_hosting_client_create(&d, now);
    if (!admission) return 2;
    tide_client *client = NULL;
    const double deadline = now + 4;
    bool ok = false;
    while (now < deadline) {
        now = (double)tide_time_now_ns() * 1e-9;
        tide_hosting_client_update(admission, now);
        if (tide_hosting_client_result(admission) > TIDE_HOSTING_ACCEPTED) break;
        if (!client && tide_hosting_client_result(admission) == TIDE_HOSTING_ACCEPTED) {
            const tide_client_desc cd = {.game = &tide_game_api,
                .transport = tide_hosting_client_transport(admission), .server = tide_hosting_client_address(admission)};
            client = tide_client_create(&cd, now);
        }
        if (client) {
            tide_client_update(client, now);
            const tide_client_status status = tide_client_status_of(client);
            if (status.state == TIDE_SESSION_CONNECTED && status.verified_tick >= 20) {
                const tide_world *w = tide_client_verified_world(client);
                const tide_str text = tide_text_read(&w->heap, w->State.source);
                ok = status.resyncs == 0 && text.bytes && w->State.value == text.chars;
                break;
            }
        }
        tide_yield();
    }
    fprintf(stderr, "full-pack client / reduced-pack server: %s\n", ok ? "ok" : "FAILED");
    tide_client_destroy(client);
    tide_hosting_client_destroy(admission);
    tide_pack_unload();
    return ok ? 0 : 1;
}
