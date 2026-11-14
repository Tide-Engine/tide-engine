// The server's store (tide/storage.h): a tick's requests beside its world, and
// what loads find as the messages later ticks take.

#include "tide/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide/page.h"
#include "tide/sha256.h"

// The worlds servers are ticking now, each with where its requests go: a
// process has a few at the most (a server, or a shard each), so a list under a
// lock, which a tick only takes when its code asks something of the store.
#define BOUND_MOST 64u

static struct {
    const void *world;
    tide_storage *out;
} bound[BOUND_MOST];
static uint32_t bound_count, bound_lock;

static void lock(void)
{
    while (__atomic_exchange_n(&bound_lock, 1u, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(void)
{
    __atomic_store_n(&bound_lock, 0u, __ATOMIC_RELEASE);
}

void tide_storage_bind(const void *world, tide_storage *out)
{
    lock();
    for (uint32_t i = 0; i < bound_count; i++) {
        if (bound[i].world == world) {
            bound[i].out = out;
            unlock();
            return;
        }
    }
    if (bound_count < BOUND_MOST) {
        bound[bound_count].world = world;
        bound[bound_count].out = out;
        bound_count++;
    }
    unlock();
}

void tide_storage_unbind(const void *world)
{
    lock();
    for (uint32_t i = 0; i < bound_count; i++) {
        if (bound[i].world != world) continue;
        bound[i] = bound[--bound_count];
        break;
    }
    unlock();
}

// Room for `size` more bytes after what's kept: where they start.
static uint32_t room_for(tide_storage *s, const uint32_t size)
{
    if (size > UINT32_MAX - s->size) tide_out_of_memory();
    if (s->size + size > s->capacity) {
        uint32_t capacity = s->capacity ? s->capacity : 1024u;
        while (capacity < s->size + size) capacity = capacity > UINT32_MAX / 2u ? UINT32_MAX : capacity * 2u;
        s->bytes = tide_realloc(s->bytes, s->capacity, capacity);
        s->capacity = capacity;
    }
    const uint32_t at = s->size;
    s->size += size;
    return at;
}

void tide_storage_ask(const void *world, const uint32_t kind, const tide_str key, const tide_list bytes,
                      const tide_entity target)
{
    lock();
    tide_storage *s = NULL;
    for (uint32_t i = 0; i < bound_count && !s; i++) {
        if (bound[i].world == world) s = bound[i].out;
    }
    unlock();
    // The world's own tick is one thread's at a time for this (code that asks keeps its order, as code that
    // calls C does), so what it asked is changed without the lock
    if (!s) return;
    if (key.bytes <= 0 || (uint32_t)key.bytes > TIDE_STORAGE_KEY_MOST) {
        fprintf(stderr, "tide: Storage: a key is 1 to %u bytes, and this one is %d (\"%.*s%s\"): nothing was %s; use a "
                        "shorter key, like a chunk's dimension and place as numbers\n",
                TIDE_STORAGE_KEY_MOST, (int)key.bytes, key.bytes > 48 ? 48 : (int)(key.bytes > 0 ? key.bytes : 0),
                key.ptr ? key.ptr : "", key.bytes > 48 ? "..." : "",
                kind == TIDE_STORAGE_SAVE ? "saved" : kind == TIDE_STORAGE_LOAD ? "loaded" : "deleted");
        return;
    }
    const int32_t count = kind == TIDE_STORAGE_SAVE ? tide_list_count(bytes) : 0;
    if (count < 0 || (uint32_t)count > TIDE_STORAGE_VALUE_MOST) {
        fprintf(stderr, "tide: Storage.Save: a value is at most %u bytes, and \"%.*s\" was given %d: nothing was saved; "
                        "save it in parts under keys of their own\n",
                TIDE_STORAGE_VALUE_MOST, (int)key.bytes, key.ptr, (int)count);
        return;
    }
    if (kind == TIDE_STORAGE_LOAD) { // Asked once however often a tick asks
        for (uint32_t i = 0; i < s->count; i++) {
            const tide_storage_request *r = &s->items[i];
            if (r->kind == TIDE_STORAGE_LOAD && r->key_size == (uint32_t)key.bytes && tide_entity_equal(r->target, target)
                && memcmp(s->bytes + r->key, key.ptr, r->key_size) == 0) return;
        }
    }
    if (s->count == s->room) {
        const uint32_t room = s->room ? s->room * 2u : 16u;
        s->items = tide_realloc(s->items, (size_t)s->room * sizeof *s->items, (size_t)room * sizeof *s->items);
        s->room = room;
    }
    tide_storage_request r = {.kind = kind, .key_size = (uint32_t)key.bytes, .data_size = (uint32_t)count, .target = target};
    r.key = room_for(s, r.key_size);
    memcpy(s->bytes + r.key, key.ptr, r.key_size);
    r.data = room_for(s, r.data_size);
    // Each byte from the list, wherever its chunks are
    for (int32_t i = 0; i < count; i++) s->bytes[r.data + (uint32_t)i] = *(const uint8_t *)tide_list_at(bytes, i, 1);
    s->items[s->count++] = r;
}

void tide_storage_take(tide_storage *to, tide_storage *from)
{
    for (uint32_t i = 0; i < from->count; i++) {
        const tide_storage_request *r = &from->items[i];
        if (to->count == to->room) {
            const uint32_t room = to->room ? to->room * 2u : 16u;
            to->items = tide_realloc(to->items, (size_t)to->room * sizeof *to->items, (size_t)room * sizeof *to->items);
            to->room = room;
        }
        tide_storage_request moved = *r;
        moved.key = room_for(to, r->key_size);
        memcpy(to->bytes + moved.key, from->bytes + r->key, r->key_size);
        moved.data = room_for(to, r->data_size);
        if (r->data_size) memcpy(to->bytes + moved.data, from->bytes + r->data, r->data_size);
        to->items[to->count++] = moved;
    }
    tide_storage_clear(from);
}

void tide_storage_clear(tide_storage *s)
{
    s->count = 0;
    s->size = 0;
}

void tide_storage_free(tide_storage *s)
{
    free(s->items);
    free(s->bytes);
    memset(s, 0, sizeof *s);
}

uint8_t *tide_stored_message(const tide_entity target, const uint64_t number, const char *key, const uint32_t key_size,
                             const uint8_t *data, const uint32_t size, const uint8_t outcome, uint32_t *out_size)
{
    tide_writer w = {.grows = true};
    tide_write_u8(&w, TIDE_STORED_MESSAGE);
    tide_write_u64(&w, target.birth);
    tide_write_u64(&w, target.lineage);
    tide_write_u64(&w, number);
    tide_write_u8(&w, outcome);
    tide_write_varint(&w, key_size);
    tide_write_bytes(&w, key, key_size);
    tide_write_bytes(&w, data, outcome == TIDE_STORED_FOUND ? size : 0u);
    if (w.overflow) tide_out_of_memory();
    *out_size = w.size;
    return w.data;
}

bool tide_stored_read(const uint8_t *message, const uint32_t size, tide_entity *target, uint64_t *number, const char **key,
                      uint32_t *key_size, const uint8_t **data, uint32_t *data_size, uint8_t *outcome)
{
    tide_reader r = {message, size, 0, false};
    if (tide_read_u8(&r) != TIDE_STORED_MESSAGE) return false;
    target->birth = tide_read_u64(&r);
    target->lineage = tide_read_u64(&r);
    *number = tide_read_u64(&r);
    const uint8_t how = tide_read_u8(&r);
    const uint32_t n = tide_read_varint(&r);
    if (r.failed || how > TIDE_STORED_FAILED || !n || n > TIDE_STORAGE_KEY_MOST) return false;
    const uint8_t *text = tide_read_bytes(&r, n);
    if (!text || r.failed) return false;
    *key = (const char *)text;
    *key_size = n;
    *outcome = how;
    *data = message + r.at;
    *data_size = size - r.at;
    return how == TIDE_STORED_FOUND || r.at == size; // Nothing found holds nothing
}

// Whether `n` bytes of a key, a part's name up to its first '.', name a device on Windows (in any case, with any
// extension): con, prn, aux, nul, com0 to com9, lpt0 to lpt9
static bool device_name(const char *name, const uint32_t n)
{
    char low[4];
    if (n != 3u && n != 4u) return false;
    for (uint32_t i = 0; i < n; i++) low[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] - 'A' + 'a' : name[i]);
    if (n == 3u) return memcmp(low, "con", 3) == 0 || memcmp(low, "prn", 3) == 0 || memcmp(low, "aux", 3) == 0 || memcmp(low, "nul", 3) == 0;
    return (memcmp(low, "com", 3) == 0 || memcmp(low, "lpt", 3) == 0) && low[3] >= '0' && low[3] <= '9';
}

uint32_t tide_storage_path(const char *key, const uint32_t key_size, char *out, const uint32_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!key_size || key_size > TIDE_STORAGE_KEY_MOST || capacity < TIDE_STORAGE_PATH_MOST + 1u) return 0;
    uint32_t at = 0;
    memcpy(out, "saved/", 6);
    at = 6;
    bool long_part = false;
    for (uint32_t from = 0; from <= key_size;) {
        uint32_t to = from;
        while (to < key_size && key[to] != '/') to++;
        // A folder's name keeps no '.': it can't end in one (which Windows drops) or be another key's file, `x.bin`
        const bool last = to == key_size;
        uint32_t stem = from;
        while (stem < to && key[stem] != '.') stem++;
        const bool device = device_name(key + from, stem - from);
        const uint32_t began = at;
        for (uint32_t i = from; i < to; i++) {
            const unsigned char ch = (unsigned char)key[i];
            const bool safe = ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'
                               || (ch == '.' && last && i > from)) && !(device && i == from);
            if (safe) out[at++] = (char)ch;
            else {
                out[at++] = '%';
                out[at++] = hex[ch >> 4];
                out[at++] = hex[ch & 15u];
            }
        }
        long_part |= at - began > TIDE_STORAGE_PART_MOST;
        if (to < key_size) {
            // A '/' between two parts is a folder's; one that starts, ends or doubles is written out
            const bool between = to > from && to + 1u < key_size && key[to + 1u] != '/';
            if (between) out[at++] = '/';
            else {
                out[at++] = '%';
                out[at++] = '2';
                out[at++] = 'F';
            }
        }
        from = to + 1u;
    }
    if (long_part) {
        // A name too long for a file system's: the key's SHA-256 in a folder no other key's name can be, as a '%'
        // is never written but before two hex digits
        uint8_t digest[32];
        tide_sha256 sha;
        tide_sha256_init(&sha);
        tide_sha256_add(&sha, key, key_size);
        tide_sha256_end(&sha, digest);
        memcpy(out, "saved/%%/", 9);
        at = 9;
        for (uint32_t i = 0; i < 32u; i++) {
            out[at++] = "0123456789abcdef"[digest[i] >> 4];
            out[at++] = "0123456789abcdef"[digest[i] & 15u];
        }
    }
    memcpy(out + at, ".bin", 5);
    return at + 4u;
}

// The folders a path is in, made where they aren't: each of its parts but the last
static void make_folders(const tide_io *io, char *path)
{
    for (char *slash = strchr(path, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        if (!io->exists(io->user, path, true)) (void)io->create(io->user, path, true);
        *slash = '/';
    }
}

// A save: into a file beside the key's, which takes its place once it's whole, so a save that fails or stops part
// way leaves the value there was. (`<key>.bin.new` is no key's file: every key's ends in ".bin")
static tide_io_error save(const tide_io *io, const char *path, const uint8_t *data, const uint32_t size)
{
    if (!io->move) return io->write(io->user, path, data, size, false);
    char fresh[TIDE_STORAGE_PATH_MOST + 8u];
    snprintf(fresh, sizeof fresh, "%s.new", path);
    tide_io_error error = io->write(io->user, fresh, data, size, false);
    if (!error) error = io->move(io->user, fresh, path, true, false);
    if (error && io->remove) (void)io->remove(io->user, fresh, false, false);
    return error;
}

void tide_storage_serve(tide_storage *store, const tide_io *io, uint64_t *next,
                        void (*answer)(void *user, uint8_t *message, uint32_t size), void *user)
{
    char path[TIDE_STORAGE_PATH_MOST + 1u];
    bool changed = false;
    for (uint32_t i = 0; i < store->count; i++) {
        const tide_storage_request *r = &store->items[i];
        const char *key = (const char *)store->bytes + r->key;
        const bool kept = io && tide_storage_path(key, r->key_size, path, sizeof path) != 0;
        if (r->kind == TIDE_STORAGE_SAVE) {
            if (!kept || !io->write) continue;
            if (io->exists && io->create) make_folders(io, path);
            const tide_io_error error = save(io, path, store->bytes + r->data, r->data_size);
            changed = true;
            if (error) {
                fprintf(stderr, "tide: Storage.Save: \"%.*s\" couldn't be written to %s (error %d): it isn't saved, and "
                                "what was saved under it before is as it was; check the folder can be written and the disk "
                                "has room\n", (int)r->key_size, key, path, (int)error);
            }
        } else if (r->kind == TIDE_STORAGE_DELETE) {
            if (kept && io->remove) {
                const tide_io_error error = io->remove(io->user, path, false, false);
                changed = true;
                if (error && error != TIDE_IO_NOT_FOUND) {
                    fprintf(stderr, "tide: Storage.Delete: \"%.*s\" couldn't be deleted from %s (error %d): it's still there\n",
                            (int)r->key_size, key, path, (int)error);
                }
            }
        } else if (r->kind == TIDE_STORAGE_LOAD) {
            uint8_t *data = NULL;
            uint32_t size = 0;
            // Nothing there is "not found"; anything else that went wrong is "failed", which a game can tell apart
            // from a key never saved, rather than make it afresh and save over what's there
            const tide_io_error error = !kept || !io->read ? TIDE_IO_NOT_FOUND : io->read(io->user, path, &data, &size);
            uint8_t outcome = !error ? TIDE_STORED_FOUND : error == TIDE_IO_NOT_FOUND ? TIDE_STORED_MISSING : TIDE_STORED_FAILED;
            if (outcome == TIDE_STORED_FAILED) {
                fprintf(stderr, "tide: Storage.Load: \"%.*s\" couldn't be read from %s (error %d): its Loaded says it failed\n",
                        (int)r->key_size, key, path, (int)error);
            }
            if (outcome == TIDE_STORED_FOUND && size > TIDE_STORAGE_VALUE_MOST) {
                fprintf(stderr, "tide: Storage.Load: \"%.*s\" holds %u bytes, more than a value's %u: its Loaded says it failed\n",
                        (int)r->key_size, key, size, TIDE_STORAGE_VALUE_MOST);
                outcome = TIDE_STORED_FAILED;
            }
            uint32_t message_size = 0;
            uint8_t *message = tide_stored_message(r->target, (*next)++, key, r->key_size, data, size, outcome, &message_size);
            free(data);
            answer(user, message, message_size);
        }
    }
    // What the tick changed goes to the disk on the provider's own thread (a flush nobody waits for), so the files a
    // server has changed never pile up waiting for one
    if (changed && io && io->flush_begin) {
        const uint32_t ticket = io->flush_begin(io->user);
        if (ticket && io->flush_forget) io->flush_forget(io->user, ticket);
    }
    tide_storage_clear(store);
}
