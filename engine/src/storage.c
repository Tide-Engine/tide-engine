// The server's store (tide/storage.h): a tick's requests beside its world, what
// loads and listings find as the messages later ticks take, and the line that
// carries a server's requests out off its own thread.

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

// Room for one more request: where it goes.
static tide_storage_request *request_room(tide_storage *s)
{
    if (s->count == s->room) {
        const uint32_t room = s->room ? s->room * 2u : 16u;
        s->items = tide_realloc(s->items, (size_t)s->room * sizeof *s->items, (size_t)room * sizeof *s->items);
        s->room = room;
    }
    return &s->items[s->count++];
}

static const char *kind_done(const uint32_t kind)
{
    return kind == TIDE_STORAGE_SAVE ? "saved" : kind == TIDE_STORAGE_LOAD ? "loaded" : kind == TIDE_STORAGE_LIST ? "listed"
                                                                                                                 : "deleted";
}

void tide_storage_ask(const void *world, const uint32_t kind, const tide_str key, const tide_list bytes, const tide_str after,
                      const uint32_t to, const tide_entity target)
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
    const bool list = kind == TIDE_STORAGE_LIST;
    const bool answered = kind == TIDE_STORAGE_LOAD || list;
    // No such kind, and a save or a delete a task waits for, are no request (a frame that says so isn't one
    // generated code made: what it asks again is a load or a listing, never a save over a key)
    if ((!answered && kind != TIDE_STORAGE_SAVE && kind != TIDE_STORAGE_DELETE) || (!answered && to == TIDE_STORAGE_TO_TASK)) return;
    // A key is 1 to 256 bytes; a listing's prefix can be none, every key (tide_storage_takes, which awaited ones fail by)
    if (key.bytes < (list ? 0 : 1) || (uint32_t)key.bytes > TIDE_STORAGE_KEY_MOST || (key.bytes && !key.ptr)) {
        if (key.bytes <= 0) {
            fprintf(stderr, "tide: Storage: a key is 1 to %u bytes, and this one is empty: nothing was %s; name what's "
                            "kept, like a chunk's dimension and place as numbers\n",
                    TIDE_STORAGE_KEY_MOST, kind_done(kind));
            return;
        }
        fprintf(stderr, "tide: Storage: a %s is %s %u bytes, and this one is %d (\"%.*s%s\"): nothing was %s; use a "
                        "shorter one, like a chunk's dimension and place as numbers\n",
                list ? "prefix" : "key", list ? "at most" : "1 to", TIDE_STORAGE_KEY_MOST, (int)key.bytes,
                key.bytes > 48 ? 48 : (int)key.bytes, key.ptr ? key.ptr : "", key.bytes > 48 ? "..." : "", kind_done(kind));
        return;
    }
    const int32_t after_size = list ? after.bytes : 0;
    if (after_size < 0 || (uint32_t)after_size > TIDE_STORAGE_KEY_MOST || (after_size && !after.ptr)) {
        fprintf(stderr, "tide: Storage.List: the key a listing goes on after is at most %u bytes, as a key is, and this one "
                        "is %d: nothing was listed; pass the last key of the answer before\n",
                TIDE_STORAGE_KEY_MOST, (int)after_size);
        return;
    }
    const int32_t count = kind == TIDE_STORAGE_SAVE ? tide_list_count(bytes) : 0;
    if (count < 0 || (uint32_t)count > TIDE_STORAGE_VALUE_MOST) {
        fprintf(stderr, "tide: Storage.Save: a value is at most %u bytes, and \"%.*s\" was given %d: nothing was saved; "
                        "save it in parts under keys of their own\n",
                TIDE_STORAGE_VALUE_MOST, (int)key.bytes, key.ptr, (int)count);
        return;
    }
    const uint32_t whom = answered && to == TIDE_STORAGE_TO_TASK ? TIDE_STORAGE_TO_TASK : TIDE_STORAGE_TO_EVENT;
    if (answered) { // Asked once however often a tick asks
        for (uint32_t i = 0; i < s->count; i++) {
            const tide_storage_request *r = &s->items[i];
            if (r->kind == kind && r->to == whom && r->key_size == (uint32_t)key.bytes && r->data_size == (uint32_t)after_size
                && tide_entity_equal(r->target, target) && (!r->key_size || memcmp(s->bytes + r->key, key.ptr, r->key_size) == 0)
                && (!r->data_size || memcmp(s->bytes + r->data, after.ptr, r->data_size) == 0)) return;
        }
    }
    tide_storage_request r = {.kind = kind, .key_size = (uint32_t)key.bytes, .to = whom, .target = answered ? target : (tide_entity){0}};
    r.key = room_for(s, r.key_size);
    if (r.key_size) memcpy(s->bytes + r.key, key.ptr, r.key_size);
    if (list) {
        r.data_size = (uint32_t)after_size;
        r.data = room_for(s, r.data_size);
        if (r.data_size) memcpy(s->bytes + r.data, after.ptr, r.data_size);
    } else {
        r.data_size = (uint32_t)count;
        r.data = room_for(s, r.data_size);
        // Each byte from the list, wherever its chunks are
        for (int32_t i = 0; i < count; i++) s->bytes[r.data + (uint32_t)i] = *(const uint8_t *)tide_list_at(bytes, i, 1);
    }
    *request_room(s) = r;
}

void tide_storage_take(tide_storage *to, tide_storage *from)
{
    for (uint32_t i = 0; i < from->count; i++) {
        const tide_storage_request *r = &from->items[i];
        tide_storage_request moved = *r;
        moved.key = room_for(to, r->key_size);
        if (r->key_size) memcpy(to->bytes + moved.key, from->bytes + r->key, r->key_size);
        moved.data = room_for(to, r->data_size);
        if (r->data_size) memcpy(to->bytes + moved.data, from->bytes + r->data, r->data_size);
        *request_room(to) = moved;
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

// ---------------------------------------------------------------------------
// Answers: u8 TIDE_STORED_MESSAGE, u8 kind, u8 to, the target (two u64), u64 its number, u8 how it came out, a
// varint and the key (a listing's prefix); then a load's value to the end, or a listing's varint and key it went on
// after, u8 more, a varint of its keys' count and each key's varint size and bytes to the end.

uint8_t *tide_stored_message(const tide_stored *answer, uint32_t *out_size)
{
    tide_writer w = {.grows = true};
    const bool list = answer->kind == TIDE_STORAGE_LIST;
    tide_write_u8(&w, TIDE_STORED_MESSAGE);
    tide_write_u8(&w, answer->kind);
    tide_write_u8(&w, answer->to);
    tide_write_u64(&w, answer->target.birth);
    tide_write_u64(&w, answer->target.lineage);
    tide_write_u64(&w, answer->number);
    tide_write_u8(&w, answer->outcome);
    tide_write_varint(&w, answer->key_size);
    if (answer->key_size) tide_write_bytes(&w, answer->key, answer->key_size);
    if (list) {
        tide_write_varint(&w, answer->after_size);
        if (answer->after_size) tide_write_bytes(&w, answer->after, answer->after_size);
        tide_write_u8(&w, answer->more ? 1u : 0u);
        tide_write_varint(&w, answer->outcome == TIDE_STORED_FAILED ? 0u : answer->count);
        if (answer->outcome != TIDE_STORED_FAILED && answer->data_size) tide_write_bytes(&w, answer->data, answer->data_size);
    } else if (answer->outcome == TIDE_STORED_FOUND && answer->data_size) {
        tide_write_bytes(&w, answer->data, answer->data_size);
    }
    if (w.overflow) tide_out_of_memory();
    *out_size = w.size;
    return w.data;
}

// A key's bytes, `n` of them at most `most`: text, as every key a Tide string made is.
static const char *read_text(tide_reader *r, const uint32_t least, uint32_t *size)
{
    const uint32_t n = tide_read_varint(r);
    if (r->failed || n < least || n > TIDE_STORAGE_KEY_MOST) return NULL;
    const uint8_t *text = tide_read_bytes(r, n);
    if (!text || r->failed || !tide_utf8_valid((const char *)text, (int32_t)n)) return NULL;
    *size = n;
    return (const char *)text;
}

bool tide_stored_read(const uint8_t *message, const uint32_t size, tide_stored *out)
{
    tide_reader r = {message, size, 0, false};
    tide_stored s = {0};
    if (!message || tide_read_u8(&r) != TIDE_STORED_MESSAGE) return false;
    s.kind = tide_read_u8(&r);
    s.to = tide_read_u8(&r);
    s.target.birth = tide_read_u64(&r);
    s.target.lineage = tide_read_u64(&r);
    s.number = tide_read_u64(&r);
    s.outcome = tide_read_u8(&r);
    const bool list = s.kind == TIDE_STORAGE_LIST;
    if (r.failed || (s.kind != TIDE_STORAGE_LOAD && !list) || s.to > TIDE_STORAGE_TO_TASK || s.outcome > TIDE_STORED_FAILED
        || (list && s.outcome == TIDE_STORED_MISSING)) return false;
    s.key = read_text(&r, list ? 0u : 1u, &s.key_size);
    if (!s.key) return false;
    if (!list) {
        s.data = message + r.at;
        s.data_size = size - r.at;
        if ((s.outcome != TIDE_STORED_FOUND && s.data_size) || s.data_size > TIDE_STORAGE_VALUE_MOST) return false; // Nothing found holds nothing
        *out = s;
        return true;
    }
    s.after = read_text(&r, 0u, &s.after_size);
    const uint8_t more = tide_read_u8(&r);
    s.count = tide_read_varint(&r);
    if (!s.after || r.failed || more > 1u || s.count > TIDE_STORAGE_LIST_MOST || (s.outcome == TIDE_STORED_FAILED && (s.count || more)))
        return false;
    s.more = more != 0;
    s.data = message + r.at;
    s.data_size = size - r.at;
    // Every key whole, and nothing after the last
    for (uint32_t i = 0; i < s.count; i++) {
        uint32_t n = 0;
        if (!read_text(&r, 1u, &n)) return false;
    }
    if (r.at != size) return false;
    *out = s;
    return true;
}

bool tide_stored_next_key(const tide_stored *answer, uint32_t *at, const char **key, uint32_t *size)
{
    if (answer->kind != TIDE_STORAGE_LIST || *at >= answer->data_size) return false;
    tide_reader r = {answer->data, answer->data_size, *at, false};
    const char *text = read_text(&r, 1u, size);
    if (!text) return false;
    *key = text;
    *at = r.at;
    return true;
}

tide_str tide_stored_text(const char *bytes, const uint32_t size)
{
    return (tide_str){size ? bytes : "", (int32_t)size, size ? tide_utf8_chars(bytes, (int32_t)size) : 0};
}

tide_list tide_stored_bytes(const tide_stored *answer)
{
    tide_list bytes = {0};
    if (answer->kind != TIDE_STORAGE_LOAD || answer->outcome != TIDE_STORED_FOUND || !answer->data_size) return bytes;
    tide_block *block = tide_scratch_block(answer->data_size, "loaded bytes", &bytes.at);
    block->a = block->b = answer->data_size;
    memcpy(block + 1, answer->data, answer->data_size);
    return bytes;
}

tide_list tide_stored_keys(const tide_stored *answer)
{
    tide_list keys = {0};
    const char *key;
    uint32_t size, at = 0;
    while (tide_stored_next_key(answer, &at, &key, &size)) {
        *(tide_text *)tide_list_add(&keys, sizeof(tide_text), TIDE_IN_SCRATCH) = tide_text_temp(tide_stored_text(key, size));
    }
    return keys;
}

// ---------------------------------------------------------------------------
// Keys' files

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

static int hex_digit(const char ch)
{
    return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
}

uint32_t tide_storage_key(const char *path, const uint32_t path_size, char *out, const uint32_t capacity)
{
    if (capacity < TIDE_STORAGE_KEY_MOST || path_size < 6u + 1u + 4u || path_size > TIDE_STORAGE_PATH_MOST
        || memcmp(path, "saved/", 6) != 0 || memcmp(path + path_size - 4u, ".bin", 4) != 0) return 0;
    uint32_t n = 0;
    for (uint32_t i = 6; i < path_size - 4u; i++) {
        const char ch = path[i];
        char byte = ch;
        if (ch == '%') { // Two hex digits in capitals, as tide_storage_path writes them: "%%" is the hashed keys' folder
            const int high = i + 2u < path_size - 4u ? hex_digit(path[i + 1u]) : -1;
            const int low = high >= 0 ? hex_digit(path[i + 2u]) : -1;
            if (low < 0) return 0;
            byte = (char)(high << 4 | low);
            i += 2u;
        } else if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '/')) {
            return 0;
        }
        if (n == TIDE_STORAGE_KEY_MOST) return 0;
        out[n++] = byte;
    }
    if (!n || !tide_utf8_valid(out, (int32_t)n)) return 0;
    // Only the very path the key is kept at: another spelling of it (a '.' where the key's file has %2E, a hex digit
    // in small letters, a '/' that's no folder's, a part too long that the key's file is named by its hash for) isn't
    char again[TIDE_STORAGE_PATH_MOST + 1u];
    if (tide_storage_path(out, n, again, sizeof again) != path_size || memcmp(again, path, path_size) != 0) return 0;
    return n;
}

// ---------------------------------------------------------------------------
// Listing keys

// What the provider lists of one folder: each entry's name, after its last '/'.
typedef struct names {
    char **items;
    uint32_t count, room;
} names;

static void name_entry(void *user, const char *path)
{
    names *n = user;
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const size_t size = strlen(name);
    if (!size || size > TIDE_STORAGE_PATH_MOST) return; // No key's
    if (n->count == n->room) {
        const uint32_t room = n->room ? n->room * 2u : 64u;
        n->items = tide_realloc(n->items, (size_t)n->room * sizeof *n->items, (size_t)room * sizeof *n->items);
        n->room = room;
    }
    char *copy = tide_alloc(size + 1u);
    memcpy(copy, name, size + 1u);
    n->items[n->count++] = copy;
}

static void free_names(names *n)
{
    for (uint32_t i = 0; i < n->count; i++) free(n->items[i]);
    free(n->items);
    memset(n, 0, sizeof *n);
}

// Keys in the order a listing has them: by their bytes, a key before those it starts.
static int compare_keys(const char *a, const uint32_t an, const char *b, const uint32_t bn)
{
    const int c = memcmp(a, b, an < bn ? an : bn);
    return c ? c : (an > bn) - (an < bn);
}

// A listing as it walks: the smallest keys past `after` with the prefix found so far, one more than an answer has
// (which says there are more), kept as a heap with the greatest on top.
#define HELD (TIDE_STORAGE_LIST_MOST + 1u)

typedef struct listing {
    const tide_io *io;
    const char *prefix;
    uint32_t prefix_size;
    const char *after;
    uint32_t after_size;
    char (*keys)[TIDE_STORAGE_KEY_MOST]; // HELD slots
    uint16_t sizes[HELD];
    uint16_t heap[HELD];                 // Slots, the greatest key's first
    uint32_t count;
    // A folder the provider couldn't list: the first error, and the least of what such folders' keys start with,
    // which fails the listing where its keys could be among the answer's (worth_walking, once the walk is done)
    tide_io_error error;
    char failed_at[TIDE_STORAGE_KEY_MOST];
    uint32_t failed_size;
    char path[TIDE_STORAGE_PATH_MOST + 16u]; // The folder being walked, and its entries' paths after it
} listing;

static int compare_slots(const listing *l, const uint16_t a, const uint16_t b)
{
    return compare_keys(l->keys[a], l->sizes[a], l->keys[b], l->sizes[b]);
}

static void sift_down(listing *l, uint32_t at, const uint32_t count)
{
    for (;;) {
        const uint32_t left = 2u * at + 1u, right = left + 1u;
        uint32_t most = at;
        if (left < count && compare_slots(l, l->heap[left], l->heap[most]) > 0) most = left;
        if (right < count && compare_slots(l, l->heap[right], l->heap[most]) > 0) most = right;
        if (most == at) return;
        const uint16_t swap = l->heap[at];
        l->heap[at] = l->heap[most];
        l->heap[most] = swap;
        at = most;
    }
}

// A key found: kept where it's one of the smallest past `after` with the prefix.
static void offer(listing *l, const char *key, const uint32_t size)
{
    if (size < l->prefix_size || memcmp(key, l->prefix, l->prefix_size) != 0) return;
    if (l->after_size && compare_keys(key, size, l->after, l->after_size) <= 0) return;
    uint16_t slot;
    if (l->count < HELD) {
        slot = (uint16_t)l->count;
        memcpy(l->keys[slot], key, size);
        l->sizes[slot] = (uint16_t)size;
        uint32_t at = l->count++;
        l->heap[at] = slot;
        while (at && compare_slots(l, l->heap[(at - 1u) / 2u], l->heap[at]) < 0) { // Up while greater than its parent
            const uint32_t up = (at - 1u) / 2u;
            const uint16_t swap = l->heap[up];
            l->heap[up] = l->heap[at];
            l->heap[at] = swap;
            at = up;
        }
        return;
    }
    slot = l->heap[0];
    if (compare_keys(key, size, l->keys[slot], l->sizes[slot]) >= 0) return; // Past every key it has
    memcpy(l->keys[slot], key, size);
    l->sizes[slot] = (uint16_t)size;
    sift_down(l, 0, l->count);
}

// Whether a folder whose keys all start with `start` (`n` bytes: its key's parts and a '/') can hold one the listing
// keeps: one with the prefix, past `after`, and below the greatest it has once it has as many as it keeps.
static bool worth_walking(const listing *l, const char *start, const uint32_t n)
{
    const uint32_t common = n < l->prefix_size ? n : l->prefix_size;
    if (memcmp(start, l->prefix, common) != 0) return false;
    if (l->after_size) { // Every key here is before `after` where `start` is, and `after` doesn't start with it
        const uint32_t k = n < l->after_size ? n : l->after_size;
        const int c = memcmp(start, l->after, k);
        if (c < 0) return false;
    }
    if (l->count == HELD) { // Every key here is past the greatest kept where `start` is past it, or is it or starts it
        const uint16_t top = l->heap[0];
        const uint32_t k = n < l->sizes[top] ? n : l->sizes[top];
        const int c = memcmp(start, l->keys[top], k);
        if (c > 0 || (c == 0 && l->sizes[top] <= n)) return false;
    }
    return true;
}

static tide_io_error list_folder(const listing *l, const bool directories, names *out)
{
    const tide_io_error error = l->io->list(l->io->user, l->path, directories, false, name_entry, out);
    return error == TIDE_IO_NOT_FOUND ? TIDE_IO_NONE : error; // A folder never made holds no keys
}

// A folder whose keys all start with `start` (`n` bytes) that couldn't be listed. Folders that fail are apart (one
// in another isn't walked), so where the least of them can't hold a key the answer has, none can.
static void failed(listing *l, const tide_io_error error, const char *start, const uint32_t n)
{
    if (!l->error || compare_keys(start, n, l->failed_at, l->failed_size) < 0) {
        if (n) memcpy(l->failed_at, start, n);
        l->failed_size = n;
    }
    if (!l->error) l->error = error;
}

// The keys in the folder at l->path (`size` bytes of it), all starting with `start` (`n` bytes, or none to be
// sure), and the folders in it, one by one: not the provider's recursive listing, whose paths say nothing of where
// the folder it was asked for ends.
static void walk(listing *l, const uint32_t size, const char *start, const uint32_t n_start)
{
    names files = {0}, folders = {0};
    l->path[size] = '\0';
    tide_io_error error = list_folder(l, false, &files);
    if (!error) error = list_folder(l, true, &folders);
    if (error) failed(l, error, start, n_start);
    char key[TIDE_STORAGE_KEY_MOST];
    for (uint32_t i = 0; !error && i < files.count; i++) {
        const uint32_t n = (uint32_t)strlen(files.items[i]);
        if (size + 1u + n > TIDE_STORAGE_PATH_MOST || n < 5u || memcmp(files.items[i] + n - 4u, ".bin", 4) != 0) continue;
        l->path[size] = '/';
        memcpy(l->path + size + 1u, files.items[i], n);
        const uint32_t k = tide_storage_key(l->path, size + 1u + n, key, sizeof key); // 0 for a file no key is kept in
        if (k) offer(l, key, k);
    }
    for (uint32_t i = 0; !error && i < folders.count; i++) {
        const uint32_t n = (uint32_t)strlen(folders.items[i]);
        // The folder keys too long for a file's name are kept in is the hashed walk's
        if (size == 5u && n == 2u && memcmp(folders.items[i], "%%", 2) == 0) continue;
        if (size + 1u + n + 6u > TIDE_STORAGE_PATH_MOST) continue;
        // A folder of keys' files has its name as a key's part is written: "<folder>/a.bin" is a key's file then,
        // and what that key is says what every key in it starts with
        l->path[size] = '/';
        memcpy(l->path + size + 1u, folders.items[i], n);
        memcpy(l->path + size + 1u + n, "/a.bin", 6);
        const uint32_t k = tide_storage_key(l->path, size + 1u + n + 6u, key, sizeof key);
        if (!k || !worth_walking(l, key, k - 1u)) continue; // `key` less its "a"
        walk(l, size + 1u + n, key, k - 1u);
    }
    l->path[size] = '\0';
    free_names(&files);
    free_names(&folders);
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// Keys too long for a file's name: each "<SHA-256>.bin" whose "<SHA-256>.key" beside it holds a key of that hash.
// A key file without its value (a save that stopped before its move, a delete that stopped between them) lists
// nothing.
static void walk_hashed(listing *l)
{
    names files = {0};
    memcpy(l->path, "saved/%%", 9);
    tide_io_error error = list_folder(l, false, &files);
    if (error) failed(l, error, "", 0); // What's there can't be told: any key could be
    if (!error && files.count > 1) qsort(files.items, files.count, sizeof *files.items, compare_names);
    char key[TIDE_STORAGE_KEY_MOST], path[TIDE_STORAGE_PATH_MOST + 1u], value[72];
    for (uint32_t i = 0; !error && i < files.count; i++) {
        const char *name = files.items[i];
        if (strlen(name) != 68u || memcmp(name + 64, ".key", 4) != 0) continue;
        memcpy(value, name, 64);
        memcpy(value + 64, ".bin", 5);
        const char *wanted = value;
        if (!bsearch(&wanted, files.items, files.count, sizeof *files.items, compare_names)) continue;
        if (!l->io->read) {
            failed(l, TIDE_IO_FAILED, "", 0);
            break;
        }
        snprintf(l->path, sizeof l->path, "saved/%%%%/%s", name);
        uint8_t *data = NULL;
        uint32_t size = 0;
        const tide_io_error read = l->io->read(l->io->user, l->path, &data, &size);
        if (read && read != TIDE_IO_NOT_FOUND) failed(l, read, "", 0);
        // The key, if it is one this file is named for
        if (!read && size >= 1u && size <= TIDE_STORAGE_KEY_MOST) {
            memcpy(key, data, size);
            snprintf(l->path, sizeof l->path, "saved/%%%%/%s", value);
            const uint32_t n = tide_storage_path(key, size, path, sizeof path);
            if (tide_utf8_valid(key, (int32_t)size) && n == strlen(l->path) && memcmp(path, l->path, n) == 0) offer(l, key, size);
        }
        free(data);
    }
    free_names(&files);
}

// The folder of `prefix`'s whole parts in l->path, where every key that starts with it is kept but those kept by
// their hash: its length, or 0 where one of those parts is too long for a file's name (every such key is hashed).
static uint32_t prefix_folder(listing *l)
{
    // Its last '/' that's a folder's in any key that starts with it: one after a part that isn't empty, with a
    // byte after it that isn't another
    uint32_t last = 0;
    bool found = false;
    for (uint32_t i = 1; i + 1u < l->prefix_size; i++) {
        if (l->prefix[i] == '/' && l->prefix[i - 1u] != '/' && l->prefix[i + 1u] != '/') {
            last = i;
            found = true;
        }
    }
    memcpy(l->path, "saved", 6);
    if (!found) return 5u;
    // Its parts as a key's that goes on after them are written: "<parts>/x"
    char key[TIDE_STORAGE_KEY_MOST + 2u];
    memcpy(key, l->prefix, last);
    memcpy(key + last, "/x", 2);
    char path[TIDE_STORAGE_PATH_MOST + 1u];
    const uint32_t n = tide_storage_path(key, last + 2u, path, sizeof path);
    if (!n || memcmp(path, "saved/%%/", 9) == 0) return 0;
    memcpy(l->path, path, n - 6u); // Less "/x.bin"
    l->path[n - 6u] = '\0';
    return n - 6u;
}

// A listing's answer: the keys it found, sorted, at most TIDE_STORAGE_LIST_MOST of them.
static uint8_t *list_keys(const tide_io *io, const tide_storage_request *r, const uint8_t *bytes, const uint64_t number,
                          uint32_t *message_size)
{
    // An empty prefix or `after` is "": a tick whose only requests are listings of every key from the first has no
    // bytes at all, and NULL + 0 isn't a pointer C has
    tide_stored answer = {.kind = TIDE_STORAGE_LIST, .to = (uint8_t)r->to, .outcome = TIDE_STORED_FOUND, .target = r->target,
                          .number = number, .key = r->key_size ? (const char *)bytes + r->key : "", .key_size = r->key_size,
                          .after = r->data_size ? (const char *)bytes + r->data : "", .after_size = r->data_size};
    if (!io) return tide_stored_message(&answer, message_size); // Nothing kept: no keys
    if (!io->list) {
        fprintf(stderr, "tide: Storage.List: the host's files can't be listed: its Listed says it failed\n");
        answer.outcome = TIDE_STORED_FAILED;
        return tide_stored_message(&answer, message_size);
    }
    listing *l = tide_alloc_zeroed(1, sizeof *l);
    l->keys = tide_alloc((size_t)HELD * sizeof *l->keys);
    l->io = io;
    l->prefix = answer.key;
    l->prefix_size = answer.key_size;
    l->after = answer.after;
    l->after_size = answer.after_size;
    const uint32_t folder = prefix_folder(l);
    if (folder) walk(l, folder, "", 0);
    walk_hashed(l);
    // A folder that couldn't be listed fails it where its keys could be among those it has: not where they'd all be
    // past them, which a listing that goes on finds, whichever order the folders came in
    if (l->error && worth_walking(l, l->failed_at, l->failed_size)) {
        fprintf(stderr, "tide: Storage.List: the keys starting \"%.*s\" couldn't all be listed (error %d): its Listed says "
                        "it failed\n", (int)answer.key_size, answer.key, (int)l->error);
        answer.outcome = TIDE_STORED_FAILED;
    } else {
        // Sorted where they are: the greatest to the end, then the greatest of the rest before it...
        for (uint32_t end = l->count; end > 1u; end--) {
            const uint16_t swap = l->heap[0];
            l->heap[0] = l->heap[end - 1u];
            l->heap[end - 1u] = swap;
            sift_down(l, 0, end - 1u);
        }
        answer.more = l->count > TIDE_STORAGE_LIST_MOST;
        answer.count = answer.more ? TIDE_STORAGE_LIST_MOST : l->count;
    }
    tide_writer keys = {.grows = true};
    for (uint32_t i = 0; i < answer.count; i++) {
        const uint16_t slot = l->heap[i];
        tide_write_varint(&keys, l->sizes[slot]);
        tide_write_bytes(&keys, l->keys[slot], l->sizes[slot]);
    }
    if (keys.overflow) tide_out_of_memory();
    answer.data = keys.data;
    answer.data_size = keys.size;
    uint8_t *message = tide_stored_message(&answer, message_size);
    free(keys.data);
    free(l->keys);
    free(l);
    return message;
}

// ---------------------------------------------------------------------------
// Carrying requests out

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

// Whether a key's file is named by its hash: then the key itself is kept beside it, in "<hash>.key"...
static bool hashed(const char *path)
{
    return memcmp(path, "saved/%%/", 9) == 0;
}

// ...which this writes in `out`, from the ".bin" path of `size` bytes
static void key_file(char *out, const char *path, const uint32_t size)
{
    memcpy(out, path, size - 4u);
    memcpy(out + size - 4u, ".key", 5);
}

static void serve(tide_storage *store, const tide_io *io, uint64_t *next, const bool answers,
                  void (*answer)(void *user, uint8_t *message, uint32_t size), void *user)
{
    char path[TIDE_STORAGE_PATH_MOST + 1u], beside[TIDE_STORAGE_PATH_MOST + 1u];
    bool changed = false;
    for (uint32_t i = 0; i < store->count; i++) {
        const tide_storage_request *r = &store->items[i];
        const char *key = r->key_size ? (const char *)store->bytes + r->key : ""; // A listing's prefix can be empty
        if (r->kind == TIDE_STORAGE_LIST) {
            const uint64_t number = (*next)++;
            if (!answers) continue;
            uint32_t message_size = 0;
            uint8_t *message = list_keys(io, r, store->bytes, number, &message_size);
            answer(user, message, message_size);
            continue;
        }
        const uint32_t path_size = io ? tide_storage_path(key, r->key_size, path, sizeof path) : 0u;
        const bool kept = path_size != 0;
        if (r->kind == TIDE_STORAGE_SAVE) {
            if (!kept || !io->write) continue;
            if (io->exists && io->create) make_folders(io, path);
            tide_io_error error = TIDE_IO_NONE;
            if (hashed(path)) { // Its key beside it first, so whatever has its value has what it is
                key_file(beside, path, path_size);
                error = save(io, beside, (const uint8_t *)key, r->key_size);
            }
            if (!error) error = save(io, path, store->bytes + r->data, r->data_size);
            changed = true;
            if (error) {
                fprintf(stderr, "tide: Storage.Save: \"%.*s\" couldn't be written to %s (error %d): it isn't saved, and "
                                "what was saved under it before is as it was; check the folder can be written and the disk "
                                "has room\n", (int)r->key_size, key, path, (int)error);
            }
        } else if (r->kind == TIDE_STORAGE_DELETE) {
            if (kept && io->remove) {
                tide_io_error error = io->remove(io->user, path, false, false);
                if (hashed(path) && (!error || error == TIDE_IO_NOT_FOUND)) { // Its value first: a key file alone lists nothing
                    key_file(beside, path, path_size);
                    const tide_io_error other = io->remove(io->user, beside, false, false);
                    if (other != TIDE_IO_NOT_FOUND) error = other;
                }
                changed = true;
                if (error && error != TIDE_IO_NOT_FOUND) {
                    fprintf(stderr, "tide: Storage.Delete: \"%.*s\" couldn't be deleted from %s (error %d): it's still there\n",
                            (int)r->key_size, key, path, (int)error);
                }
            }
        } else if (r->kind == TIDE_STORAGE_LOAD) {
            const uint64_t number = (*next)++;
            if (!answers) continue;
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
            const tide_stored found = {.kind = TIDE_STORAGE_LOAD, .to = (uint8_t)r->to, .outcome = outcome, .target = r->target,
                                       .number = number, .key = key, .key_size = r->key_size, .data = data,
                                       .data_size = outcome == TIDE_STORED_FOUND ? size : 0u};
            uint32_t message_size = 0;
            uint8_t *message = tide_stored_message(&found, &message_size);
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

void tide_storage_serve(tide_storage *store, const tide_io *io, uint64_t *next,
                        void (*answer)(void *user, uint8_t *message, uint32_t size), void *user)
{
    serve(store, io, next, true, answer, user);
}

// ---------------------------------------------------------------------------
// A server's line: batches of requests on their way, each a tick's

struct tide_storage_batch {
    tide_storage_batch *next;
    tide_storage store;   // Its requests, its own
    const tide_io *io;
    uint64_t number;      // Its first answer's
    bool answers;         // Loads and listings are carried out
    bool forgotten;       // The sender's alone: its answers are dropped
    uint32_t done;        // Atomic: carried out, by whichever thread did; nothing of it changes after
    uint8_t **messages;   // Its answers, in order
    uint32_t *sizes;
    uint32_t count, room;
};

static void batch_answer(void *user, uint8_t *message, const uint32_t size)
{
    tide_storage_batch *b = user;
    if (b->count == b->room) {
        const uint32_t room = b->room ? b->room * 2u : 16u;
        b->messages = tide_realloc(b->messages, (size_t)b->room * sizeof *b->messages, (size_t)room * sizeof *b->messages);
        b->sizes = tide_realloc(b->sizes, (size_t)b->room * sizeof *b->sizes, (size_t)room * sizeof *b->sizes);
        b->room = room;
    }
    b->messages[b->count] = message;
    b->sizes[b->count++] = size;
}

// On the provider's thread, or the sender's: the batch's requests, then `done`, after which it isn't touched here
static void carry_out(void *context)
{
    tide_storage_batch *b = context;
    uint64_t next = b->number;
    serve(&b->store, b->io, &next, b->answers, batch_answer, b);
    __atomic_store_n(&b->done, 1u, __ATOMIC_RELEASE);
}

static void free_batch(tide_storage_batch *b)
{
    for (uint32_t i = 0; i < b->count; i++) free(b->messages[i]);
    free(b->messages);
    free(b->sizes);
    tide_storage_free(&b->store);
    free(b);
}

void tide_storage_send(tide_storage_line *line, tide_storage *store, const tide_io *io, const bool answers)
{
    if (!store->count) return;
    tide_storage_batch *b = tide_alloc_zeroed(1, sizeof *b);
    b->store = *store;
    *store = (tide_storage){0};
    b->io = io;
    b->answers = answers;
    b->number = line->next;
    for (uint32_t i = 0; i < b->store.count; i++) {
        line->next += b->store.items[i].kind == TIDE_STORAGE_LOAD || b->store.items[i].kind == TIDE_STORAGE_LIST;
    }
    tide_storage_batch *before = line->last;
    if (line->last) line->last->next = b;
    else line->first = b;
    line->last = b;
    // Off this thread where the provider has one of its own, else here and now
    if (!io || !io->post || !io->settle) {
        carry_out(b);
        return;
    }
    if (io->post(io->user, carry_out, b)) return;
    // The provider couldn't queue it (it hadn't the memory, its queue was full): this line's batches before it are
    // carried out first, so it still goes after them (a load after their saves, a save of a key after theirs, never
    // two of the line's writes of one file at once), then it's queued again, or carried out here
    if (before && !__atomic_load_n(&before->done, __ATOMIC_ACQUIRE)) io->settle(io->user, before);
    if (!io->post(io->user, carry_out, b)) carry_out(b);
}

uint32_t tide_storage_receive(tide_storage_line *line, void (*answer)(void *user, uint8_t *message, uint32_t size),
                              void *user)
{
    uint32_t taken = 0;
    while (line->first && __atomic_load_n(&line->first->done, __ATOMIC_ACQUIRE)) {
        tide_memory_sync(); // What another thread made
        tide_storage_batch *b = line->first;
        line->first = b->next;
        if (!line->first) line->last = NULL;
        for (uint32_t i = 0; !b->forgotten && i < b->count; i++) {
            answer(user, b->messages[i], b->sizes[i]);
            b->messages[i] = NULL;
            taken++;
        }
        free_batch(b);
    }
    return taken;
}

void tide_storage_forget(tide_storage_line *line)
{
    for (tide_storage_batch *b = line->first; b; b = b->next) b->forgotten = true;
}

void tide_storage_finish(tide_storage_line *line)
{
    for (tide_storage_batch *b = line->first; b;) {
        // Posted, so it has one: a wait for this batch, not for what other servers posted after it
        if (!__atomic_load_n(&b->done, __ATOMIC_ACQUIRE)) b->io->settle(b->io->user, b);
        while (!__atomic_load_n(&b->done, __ATOMIC_ACQUIRE)) {
        }
        tide_memory_sync();
        tide_storage_batch *next = b->next;
        free_batch(b);
        b = next;
    }
    line->first = line->last = NULL;
}

uint32_t tide_storage_waiting(const tide_storage_line *line)
{
    uint32_t count = 0;
    for (const tide_storage_batch *b = line->first; b; b = b->next) count++;
    return count;
}
