#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/io.h"
#include "tide/list.h"
#include "tide/text.h"

// The server's store: what Tide's `Storage.Save`, `Storage.Load`,
// `Storage.Delete` and `Storage.List` ask of the machine that runs the match,
// in match code. A world too big to keep in memory keeps what nobody is near
// there: a chunk of blocks saved as its last player leaves and loaded as the
// next one comes.
//
// The store is no part of the match. A tick's requests leave it beside the
// world (a tide_storage, which only the server's own run of the tick has: a
// player's machine predicting the same tick asks nothing), and what a load or
// a listing finds comes back as input to a later tick, a message the server
// gives it as it gives a command (TIDE_STORED_MESSAGE, tide_server_desc.io),
// so the tick that takes it is the same on every machine and in a replay.

typedef enum tide_storage_kind {
    TIDE_STORAGE_SAVE = 1,
    TIDE_STORAGE_LOAD = 2,
    TIDE_STORAGE_DELETE = 3,
    TIDE_STORAGE_LIST = 4,
} tide_storage_kind;

// Who a load's or a listing's answer is for. TIDE_STORAGE_TO_EVENT: its event
// (Loaded, Listed), sent to the request's `target`, or to the world where that's
// the null entity. TIDE_STORAGE_TO_TASK: a match task that waits for it, and
// `target` is 16 bytes of the game's own naming the operation it waits on,
// given back as they were (generated code's: the operation's identity, the
// identity clock and a cause); the engine sends no event for it. Only a load
// or a listing is a task's.
enum { TIDE_STORAGE_TO_EVENT = 0, TIDE_STORAGE_TO_TASK = 1 };

// The most bytes a key is, and a listing's prefix and the key it goes on
// after. A longer one is refused, and stderr says so.
#define TIDE_STORAGE_KEY_MOST 256u

// Whether the store takes a load of `key`, or a listing of the prefix `key`
// after `after`: a key of 1 to TIDE_STORAGE_KEY_MOST bytes, a prefix and an
// `after` of at most that many. tide_storage_ask refuses the rest (stderr
// says why), and generated code gives an awaited load or listing it would
// refuse its failure at once, on every machine, as nothing will answer it.
static inline bool tide_storage_takes(const uint32_t kind, const tide_str key, const tide_str after)
{
    const bool list = kind == TIDE_STORAGE_LIST;
    if (key.bytes < (list ? 0 : 1) || (uint32_t)key.bytes > TIDE_STORAGE_KEY_MOST || (key.bytes && !key.ptr)) return false;
    return !list || (after.bytes >= 0 && (uint32_t)after.bytes <= TIDE_STORAGE_KEY_MOST && (!after.bytes || after.ptr));
}
// The most bytes a value saved or loaded is: a load's answer is a message a tick takes (TIDE_SHARD_MESSAGE_LIMIT).
#define TIDE_STORAGE_VALUE_MOST (32u << 20)
// The most keys one listing's answer has: 1,024 keys of 256 bytes at the most
// are 264 KiB with their sizes, well within TIDE_STORAGE_VALUE_MOST. Past them
// its `more` is true, and the game asks again after the last.
#define TIDE_STORAGE_LIST_MOST 1024u

typedef struct tide_storage_request {
    uint32_t kind;      // tide_storage_kind
    uint32_t key;       // Where its key (a listing's prefix) starts in `bytes`
    uint32_t key_size;
    uint32_t data;      // A save's value, a listing's key to go on after
    uint32_t data_size;
    uint32_t to;        // A load's or a listing's: TIDE_STORAGE_TO_EVENT or TIDE_STORAGE_TO_TASK
    tide_entity target; // ...and whom it's for: the entity (null: the world), or a waiting task's operation
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
// bound. `bytes` is a save's List<byte>; `after` a listing's key to go on
// after (0 bytes: from the first), `key` its prefix (0 bytes: every key); `to`
// and `target` whom a load's or a listing's answer is for. A load or a listing
// asked twice in a tick with one key (and `after`) and one target is asked
// once. No such kind, or a save or delete for a task, is no request.
void tide_storage_ask(const void *world, uint32_t kind, tide_str key, tide_list bytes, tide_str after, uint32_t to,
                      tide_entity target);

void tide_storage_clear(tide_storage *s);
// Moves `from`'s requests to the end of `to`'s, in order, leaving `from` empty.
void tide_storage_take(tide_storage *to, tide_storage *from);
void tide_storage_free(tide_storage *s);

// What a load or a listing found, as the message a tick takes: its first byte is this, which no shard message's is.
#define TIDE_STORED_MESSAGE 0xF0u

// How a load came out: the key held bytes, held nothing, or couldn't be read (the provider gave another error than
// "not found", or the value was bigger than a value can be), which the game is told apart from nothing. A
// listing's is FOUND, whatever it found, or FAILED where the provider couldn't list what's kept.
enum { TIDE_STORED_MISSING = 0, TIDE_STORED_FOUND = 1, TIDE_STORED_FAILED = 2 };

// An answer, as tide_stored_read reads it: pointers into the message.
typedef struct tide_stored {
    uint8_t kind;        // TIDE_STORAGE_LOAD or TIDE_STORAGE_LIST
    uint8_t to;          // TIDE_STORAGE_TO_EVENT or TIDE_STORAGE_TO_TASK
    uint8_t outcome;     // TIDE_STORED_*
    bool more;           // A listing's: keys past its last one were left out
    tide_entity target;  // Whom it's for (tide_storage_request.target)
    uint64_t number;     // Tells it from every other answer the same tick takes
    const char *key;     // A load's key, a listing's prefix (a listing's can be 0 bytes)
    uint32_t key_size;
    const char *after;   // A listing's: the key it went on after (0 bytes: from the first)
    uint32_t after_size;
    const uint8_t *data; // A load's value (FOUND only); a listing's keys, each a varint of its size and its bytes
    uint32_t data_size;
    uint32_t count;      // A listing's keys, at most TIDE_STORAGE_LIST_MOST
} tide_stored;

// The message for an answer (to free()): `answer`'s fields, where `data` is
// a load's value or a listing's keys as tide_stored has them.
uint8_t *tide_stored_message(const tide_stored *answer, uint32_t *out_size);
// ...and reading one: false for bytes that aren't one (sizes past the limits,
// keys that aren't UTF-8, or bytes past its end). `out` points into it.
bool tide_stored_read(const uint8_t *message, uint32_t size, tide_stored *out);
// A listing's keys one by one, from *at (0 for the first): false past the last.
bool tide_stored_next_key(const tide_stored *answer, uint32_t *at, const char **key, uint32_t *size);
// For generated code: a load's value as a List<byte> in the scratch area...
tide_list tide_stored_bytes(const tide_stored *answer);
// ...a listing's keys as a List<string> in the scratch area, in order (text
// made with tide_text_temp, in whatever heap the caller has set to use)...
tide_list tide_stored_keys(const tide_stored *answer);
// ...and a key's bytes as text: its characters counted.
tide_str tide_stored_text(const char *bytes, uint32_t size);

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
// path has a folder "%%", and the key itself is kept beside it, in the same
// name with ".key" for ".bin", so a listing finds it. The path's length, or 0
// for a key that isn't one or a `capacity` less than TIDE_STORAGE_PATH_MOST + 1.
#define TIDE_STORAGE_PART_MOST 200u
#define TIDE_STORAGE_PATH_MOST (6u + TIDE_STORAGE_KEY_MOST * 3u + 4u)
uint32_t tide_storage_path(const char *key, uint32_t key_size, char *out, uint32_t capacity);
// The other way: the key kept at `path` (as tide_storage_path wrote it), into
// `out`, which has room for TIDE_STORAGE_KEY_MOST bytes. Its size, or 0 for a
// path that's no key's: one tide_storage_path wouldn't write for any key (a
// stray file, a ".new" a save left as it stopped, another spelling of a key's
// name), a key that isn't UTF-8, and one kept by its SHA-256, which the path
// alone doesn't say.
uint32_t tide_storage_key(const char *path, uint32_t path_size, char *out, uint32_t capacity);

// Carries out a tick's requests, in order, against a host's files, on this
// thread: saves (each into `<path>.new`, moved over the key's file once
// written whole) and deletes, and each load's and listing's answer through
// `answer` (a tide_stored_message, which it takes to free; `number` counts up
// from *next). A listing has the keys that start with its prefix, past its
// `after`, sorted by their bytes, TIDE_STORAGE_LIST_MOST at the most: the
// files under the folder the prefix's whole parts make, and those kept by
// their SHA-256 whose key file says what they are. What it saved and deleted
// is flushed on the provider's thread, nobody waiting. With no provider nothing
// is kept: saves go nowhere, every load finds nothing and every listing no
// keys. The store is left empty.
void tide_storage_serve(tide_storage *store, const tide_io *io, uint64_t *next,
                        void (*answer)(void *user, uint8_t *message, uint32_t size), void *user);

// A server's requests on their way: each tick's handed over once it has ended
// (tide_storage_send), carried out on the provider's own thread where it has
// one (tide_io.post) and on this one at once where it hasn't, and their
// answers taken once they're ready, in the order they were asked
// (tide_storage_receive). Only the thread that sends touches the line; the
// provider's thread touches only the batch it carries out.
typedef struct tide_storage_batch tide_storage_batch;
typedef struct tide_storage_line {
    tide_storage_batch *first, *last; // On their way, oldest first
    uint64_t next;                    // The number the next answer has
} tide_storage_line;

// Hands `store`'s requests over, after everything handed over before, leaving
// it empty. With `answers` false nobody will take what loads and listings
// find, which aren't carried out: a server that stops still saves and deletes.
void tide_storage_send(tide_storage_line *line, tide_storage *store, const tide_io *io, bool answers);
// The answers that are ready, in order, each through `answer` (a message, which
// it takes to free), up to the first batch still being carried out: how many.
uint32_t tide_storage_receive(tide_storage_line *line, void (*answer)(void *user, uint8_t *message, uint32_t size),
                              void *user);
// The answers on their way are nobody's (a rewind): dropped as they're ready.
// What their batches save and delete is still done.
void tide_storage_forget(tide_storage_line *line);
// Returns once everything handed over is carried out (tide_io.settle for each
// batch still on its way: not what others posted after it), and frees it,
// answers and all: a server that stops loses nothing it saved.
void tide_storage_finish(tide_storage_line *line);
// Batches handed over and not yet received.
uint32_t tide_storage_waiting(const tide_storage_line *line);
