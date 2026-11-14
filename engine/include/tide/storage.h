#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/io.h"
#include "tide/list.h"
#include "tide/text.h"

// The server's store: what Tide's `Storage.Save`, `Storage.Load` and
// `Storage.Delete` ask of the machine that runs the match, in match code. A
// world too big to keep in memory keeps what nobody is near there: a chunk of
// blocks saved as its last player leaves and loaded as the next one comes.
//
// The store is no part of the match. A tick's requests leave it beside the
// world (a tide_storage, which only the server's own run of the tick has: a
// player's machine predicting the same tick asks nothing), and what a load
// finds comes back as input to a later tick, a message the server gives it as
// it gives a command (TIDE_STORED_MESSAGE, tide_server_desc.io), so the tick
// that takes it is the same on every machine and in a replay.

typedef enum tide_storage_kind {
    TIDE_STORAGE_SAVE = 1,
    TIDE_STORAGE_LOAD = 2,
    TIDE_STORAGE_DELETE = 3,
} tide_storage_kind;

// The most bytes a key is. A longer one is refused, and stderr says so.
#define TIDE_STORAGE_KEY_MOST 256u
// The most bytes a value saved or loaded is: a load's answer is a message a tick takes (TIDE_SHARD_MESSAGE_LIMIT).
#define TIDE_STORAGE_VALUE_MOST (32u << 20)

typedef struct tide_storage_request {
    uint32_t kind;      // tide_storage_kind
    uint32_t key;       // Where its key starts in `bytes`
    uint32_t key_size;
    uint32_t data;      // A save's value
    uint32_t data_size;
    tide_entity target; // A load's: the entity its answer is sent to, or the null one for the world
} tide_storage_request;

// What one tick asked, in the order it asked.
typedef struct tide_storage tide_storage;
struct tide_storage {
    tide_storage_request *items;
    uint32_t count, room;
    uint8_t *bytes;
    uint32_t size, capacity;
};

// A server's, around its own run of a tick on `world`: what the tick asks
// goes into `out`. Unbound, a world's requests go nowhere.
void tide_storage_bind(const void *world, tide_storage *out);
void tide_storage_unbind(const void *world);

// Generated code's: a request of the world's store. Nothing unless `world` is
// bound. `bytes` is a save's List<byte>; a load asked twice in a tick with one
// key and target is asked once.
void tide_storage_ask(const void *world, uint32_t kind, tide_str key, tide_list bytes, tide_entity target);

void tide_storage_clear(tide_storage *s);
// Moves `from`'s requests to the end of `to`'s, in order, leaving `from` empty.
void tide_storage_take(tide_storage *to, tide_storage *from);
void tide_storage_free(tide_storage *s);

// What a load found, as the message a tick takes: its first byte is this, which no shard message's is.
#define TIDE_STORED_MESSAGE 0xF0u

// How a load came out: the key held bytes, held nothing, or couldn't be read (the provider gave another error than
// "not found", or the value was bigger than a value can be), which the game is told apart from nothing.
enum { TIDE_STORED_MISSING = 0, TIDE_STORED_FOUND = 1, TIDE_STORED_FAILED = 2 };

// The message for a load's answer (to free()): the target, a number that tells
// it from every other answer the same tick takes, how it came out, the key and
// what it held.
uint8_t *tide_stored_message(tide_entity target, uint64_t number, const char *key, uint32_t key_size, const uint8_t *data,
                             uint32_t size, uint8_t outcome, uint32_t *out_size);
// ...and reading one: false for bytes that aren't one. `key` and `data` point into the message.
bool tide_stored_read(const uint8_t *message, uint32_t size, tide_entity *target, uint64_t *number, const char **key,
                      uint32_t *key_size, const uint8_t **data, uint32_t *data_size, uint8_t *outcome);

// Where a key is kept under a provider's persistent root: "saved/", then the
// key with every byte but small letters, digits, '-', '_' and (in its last
// part, after its first byte) '.' written %XX, '/' kept between parts that
// aren't empty, then ".bin". A part whose name before its first '.' is a
// Windows device's (con, nul, com1...) has its first byte written %XX. So
// folders have no '.' (none ends in one, none is another key's `x.bin`), any
// key is a file of its own on a disk that tells capitals from small letters or
// one that doesn't, and none reaches outside the folder. A key with a part
// longer than TIDE_STORAGE_PART_MOST so written, more than a file system's
// name takes, is "saved/%%/" and its SHA-256 in hex instead: no other key's
// path has a folder "%%". The path's length, or 0 for a key that isn't one or
// a `capacity` less than TIDE_STORAGE_PATH_MOST + 1.
#define TIDE_STORAGE_PART_MOST 200u
#define TIDE_STORAGE_PATH_MOST (6u + TIDE_STORAGE_KEY_MOST * 3u + 4u)
uint32_t tide_storage_path(const char *key, uint32_t key_size, char *out, uint32_t capacity);

// Carries out a tick's requests, in order, against a host's files: saves
// (each into `<path>.new`, moved over the key's file once written whole) and
// deletes at once, and each load's answer through `answer` (a
// tide_stored_message, which it takes to free; `number` counts up from
// *next). What it saved and deleted is flushed on the provider's thread,
// nobody waiting. With no provider nothing is kept: saves go nowhere and every
// load finds nothing. The store is left empty.
void tide_storage_serve(tide_storage *store, const tide_io *io, uint64_t *next,
                        void (*answer)(void *user, uint8_t *message, uint32_t size), void *user);
