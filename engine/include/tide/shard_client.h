#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/session.h"
#include "tide/shard.h"

// A player's machine in a sharded match (tide/shards.h): a client of each
// shard its route says it follows (tide_shard_game.route), all as the same
// player, its input going to each. One of them is its home, the shard that
// owns most of its player's entities: the world it plays in and views draw.
// Following the shards near its player's entities, it moves its home to one
// of them as its entities do, with nothing to download.

typedef struct tide_shard_client tide_shard_client;

typedef struct tide_shard_client_desc {
    // How each of its clients is made. Its transport and server are the first
    // shard's, the root's: the world's front door, which gives this machine
    // its player and cookie.
    tide_client_desc client;
    // Where another shard's server is, to follow it: false while it can't be
    // reached yet (asked again each update). `match` is what its host knows
    // it by. NULL: only the first shard is ever followed.
    bool (*reach)(void *user, uint32_t shard, uint64_t match, tide_transport *transport, tide_address *server);
    void *user;
} tide_shard_client_desc;

tide_shard_client *tide_shard_client_create(const tide_shard_client_desc *desc, double now);
void tide_shard_client_destroy(tide_shard_client *c);
void tide_shard_client_update(tide_shard_client *c, double now);

// The client of its home shard, and which shard that is.
tide_client *tide_shard_client_home(const tide_shard_client *c);
uint32_t tide_shard_client_home_shard(const tide_shard_client *c);
// The client of a shard it follows, or NULL.
tide_client *tide_shard_client_session(const tide_shard_client *c, uint32_t shard);
// How many times its home moved to another shard.
uint32_t tide_shard_client_moves(const tide_shard_client *c);
