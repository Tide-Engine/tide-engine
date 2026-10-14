// What hosted.tide's view hands over each frame of a run under the standard
// host. By the tenth frame the match the game hosted at its first is open on
// the game's own network: the host asked the game for the network its C
// registered, opened the match on it and on nothing else, and Session.room
// is what that network says. The run fails otherwise.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void HostedCheck(const bool server, const bool open, const char *room, const int32_t frame)
{
    if (frame != 10) return;
    if (!server || !open || strncmp(room, "127.0.0.1:", 10) != 0 || !room[10]) {
        fprintf(stderr, "the standard host didn't open the match on the game's network: server %d, open %d, room '%s'\n",
                (int)server, (int)open, room);
        exit(1);
    }
    fprintf(stderr, "the standard host opened the match on the game's network\n");
}
