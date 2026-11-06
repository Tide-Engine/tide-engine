#include <stdio.h>
#include <stdlib.h>
#include "game.h"
int main(void) {
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 1.0f / 60.0f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = tide_game_api.client_state(&server, tide_player_from_index(0), &size);
    if (!tide_game_api.load_client_state(&client, bytes, size)) return 2;
    free(bytes);
    printf("before: free server %u client %u next server %u client %u hash equal %d\n", server.entities.free_head, client.entities.free_head, server.entities.next_unused, client.entities.next_unused, tide_game_api.client_hash(&server, tide_player_from_index(0)) == tide_game_api.client_hash(&client, tide_player_from_index(0)));
    tide_world_tick(&server);
    tide_world_tick(&client);
    printf("after: entity server %u:%u client %u:%u hash equal %d\n", server.State.object.index, server.State.object.generation, client.State.object.index, client.State.object.generation, tide_game_api.client_hash(&server, tide_player_from_index(0)) == tide_game_api.client_hash(&client, tide_player_from_index(0)));
    tide_world_free(&client);
    tide_world_free(&server);
    return 0;
}
