#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "game.h"
int main(void) {
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 1.0f / 60.0f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = tide_game_api.client_state(&server, tide_player_from_index(0), &size);
    if (!tide_game_api.load_client_state(&client, bytes, size)) {
        free(bytes);
        tide_world_free(&server);
        return 2;
    }
    free(bytes);
    const bool before = tide_game_api.client_hash(&server, tide_player_from_index(0))
        == tide_game_api.client_hash(&client, tide_player_from_index(0));
    printf("before: free server %u client %u next server %u client %u hash equal %d\n", server.entities.free_head,
        client.entities.free_head, server.entities.next_unused, client.entities.next_unused, before);
    tide_world_tick(&server);
    tide_world_tick(&client);
    const bool after = tide_game_api.client_hash(&server, tide_player_from_index(0))
        == tide_game_api.client_hash(&client, tide_player_from_index(0));
    const bool same = tide_entity_equal(server.State.object, client.State.object)
        && !tide_entity_is_null(server.State.object);
    printf("after: entity server %016" PRIx64 ":%016" PRIx64 " client %016" PRIx64 ":%016" PRIx64 " hash equal %d\n",
        server.State.object.birth, server.State.object.lineage, client.State.object.birth, client.State.object.lineage, after);
    tide_world_free(&client);
    tide_world_free(&server);
    return before && after && same ? 0 : 1;
}
