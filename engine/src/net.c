#include "tide/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

// See tide/net.h.

// ---------------------------------------------------------------------------
// Addresses

bool tide_address_equal(const tide_address a, const tide_address b)
{
    return a.kind == b.kind && a.host == b.host && a.port == b.port;
}

static bool parse_number(const char **p, const uint32_t max, uint32_t *out)
{
    uint32_t v = 0;
    const char *s = *p;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > max) return false;
        s++;
    }
    *p = s;
    *out = v;
    return true;
}

bool tide_address_parse(const char *text, const uint16_t default_port, tide_address *out)
{
    const char *p = text;
    uint32_t host = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t part;
        if (!parse_number(&p, 255, &part)) return false;
        host = host << 8 | part;
        if (i < 3 && *p++ != '.') return false;
    }
    uint32_t port = default_port;
    if (*p == ':') {
        p++;
        if (!parse_number(&p, 65535, &port)) return false;
    }
    if (*p != '\0') return false;
    *out = (tide_address){TIDE_ADDRESS_IPV4, host, (uint16_t)port};
    return true;
}

void tide_address_format(const tide_address a, char *out, const size_t size)
{
    if (a.kind == TIDE_ADDRESS_IPV4) {
        snprintf(out, size, "%u.%u.%u.%u:%u", (unsigned)(a.host >> 24), (unsigned)(a.host >> 16 & 255u),
                 (unsigned)(a.host >> 8 & 255u), (unsigned)(a.host & 255u), (unsigned)a.port);
    } else if (a.kind == TIDE_ADDRESS_LOOPBACK) {
        snprintf(out, size, "loopback %u", (unsigned)a.host);
    } else if (a.kind == TIDE_ADDRESS_ROOM && a.host & TIDE_ROOM_CODE_BIT) {
        char code[TIDE_ROOM_CODE_LENGTH + 1];
        for (int i = 0; i < TIDE_ROOM_CODE_LENGTH; i++) {
            code[i] = TIDE_ROOM_CODE_LETTERS[a.host >> (5 * (TIDE_ROOM_CODE_LENGTH - 1 - i)) & 31u];
        }
        code[TIDE_ROOM_CODE_LENGTH] = '\0';
        snprintf(out, size, "room %s, player %u", code, (unsigned)a.port);
    } else if (a.kind == TIDE_ADDRESS_ROOM) {
        snprintf(out, size, "room player %u", (unsigned)a.port);
    } else if (a.kind == TIDE_ADDRESS_NETWORK) {
        snprintf(out, size, "network %u:%u", (unsigned)a.host, (unsigned)a.port);
    } else {
        snprintf(out, size, "nowhere");
    }
}

// ---------------------------------------------------------------------------
// Loopback

#define LOOPBACK_ENDPOINTS 1024u // Enough for a match of the most players a game takes, and the one that takes it over

typedef struct datagram {
    uint32_t to;
    tide_address from;
    double at;      // When it arrives
    uint64_t order; // Sent before the ones with higher numbers
    uint32_t size;
    uint8_t *data;
} datagram;

typedef struct endpoint {
    tide_loopback *net;
    uint32_t number;
    bool open;
    double arrived; // When the datagram it last gave arrived
} endpoint;

struct tide_loopback {
    double now;
    tide_net_conditions conditions;
    uint64_t random;
    datagram *queue;
    uint32_t count;
    uint32_t capacity;
    uint64_t sent;
    endpoint endpoints[LOOPBACK_ENDPOINTS];
};

// splitmix64: a number from 0 to 1.
static double chance(tide_loopback *net)
{
    uint64_t z = (net->random += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (double)(z >> 11) * (1.0 / 9007199254740992.0);
}

tide_loopback *tide_loopback_create(const uint64_t seed)
{
    tide_loopback *net = calloc(1, sizeof *net);
    if (!net) return NULL;
    net->random = seed;
    for (uint32_t i = 0; i < LOOPBACK_ENDPOINTS; i++) net->endpoints[i] = (endpoint){net, i + 1u, false, 0.0};
    return net;
}

void tide_loopback_destroy(tide_loopback *net)
{
    if (!net) return;
    for (uint32_t i = 0; i < net->count; i++) free(net->queue[i].data);
    free(net->queue);
    free(net);
}

void tide_loopback_set_time(tide_loopback *net, const double now)
{
    net->now = now;
}

void tide_loopback_set_conditions(tide_loopback *net, const tide_net_conditions conditions)
{
    net->conditions = conditions;
}

tide_address tide_loopback_address(const uint32_t number)
{
    return (tide_address){TIDE_ADDRESS_LOOPBACK, number, 0};
}

static void remove_at(tide_loopback *net, const uint32_t i)
{
    free(net->queue[i].data);
    net->queue[i] = net->queue[--net->count];
}

static void loopback_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const endpoint *from = self;
    tide_loopback *net = from->net;
    if (to.kind != TIDE_ADDRESS_LOOPBACK || to.host < 1 || to.host > LOOPBACK_ENDPOINTS) return;
    if (!net->endpoints[to.host - 1u].open) return; // Nobody there
    if (chance(net) < net->conditions.loss) return;
    if (net->count == net->capacity) {
        const uint32_t capacity = net->capacity ? net->capacity * 2u : 64u;
        datagram *queue = realloc(net->queue, capacity * sizeof *queue);
        if (!queue) return;
        net->queue = queue;
        net->capacity = capacity;
    }
    uint8_t *copy = malloc(size ? size : 1u);
    if (!copy) return;
    memcpy(copy, data, size);
    const double delay = net->conditions.latency + net->conditions.jitter * chance(net);
    net->queue[net->count++] = (datagram){to.host, tide_loopback_address(from->number), net->now + delay, net->sent++, size, copy};
}

static uint32_t loopback_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    endpoint *e = self;
    tide_loopback *net = e->net;
    for (;;) {
        int32_t next = -1;
        for (uint32_t i = 0; i < net->count; i++) {
            const datagram *d = &net->queue[i];
            if (d->to != e->number || d->at > net->now) continue;
            const datagram *best = next >= 0 ? &net->queue[next] : NULL;
            if (!best || d->at < best->at || (d->at == best->at && d->order < best->order)) next = (int32_t)i;
        }
        if (next < 0) return 0;
        const datagram d = net->queue[next];
        if (d.size > capacity || d.size == 0) {
            remove_at(net, (uint32_t)next);
            continue;
        }
        memcpy(data, d.data, d.size);
        *from = d.from;
        e->arrived = d.at;
        remove_at(net, (uint32_t)next);
        return d.size;
    }
}

// The network's time is its clock, and a datagram arrived when its time came,
// however long after that its endpoint was read.
static double loopback_clock(void *self)
{
    const endpoint *e = self;
    return e->net->now;
}

static double loopback_arrived(void *self)
{
    const endpoint *e = self;
    return e->arrived;
}

static void loopback_close(void *self)
{
    endpoint *e = self;
    tide_loopback *net = e->net;
    e->open = false;
    for (uint32_t i = 0; i < net->count;) {
        if (net->queue[i].to == e->number) remove_at(net, i);
        else i++;
    }
}

tide_transport tide_loopback_endpoint(tide_loopback *net, const uint32_t number)
{
    if (number < 1 || number > LOOPBACK_ENDPOINTS) return (tide_transport){0};
    endpoint *e = &net->endpoints[number - 1u];
    e->open = true;
    return (tide_transport){e, loopback_send, loopback_receive, loopback_close, NULL, loopback_clock, loopback_arrived};
}

// ---------------------------------------------------------------------------
// Bytes

// A writer that grows: room for `size` more bytes, doubling what it has.
static void grow(tide_writer *w, const uint32_t size)
{
    const uint64_t needed = (uint64_t)w->size + size;
    if (needed > UINT32_MAX) return;
    uint64_t capacity = w->capacity ? w->capacity : 256u;
    while (capacity < needed) capacity *= 2u;
    if (capacity > UINT32_MAX) capacity = UINT32_MAX;
    uint8_t *data = realloc(w->data, (size_t)capacity);
    if (!data) return;
    w->data = data;
    w->capacity = (uint32_t)capacity;
}

static uint8_t *room(tide_writer *w, const uint32_t size)
{
    if (w->grows && !w->overflow && w->capacity - w->size < size) grow(w, size);
    if (w->overflow || w->capacity - w->size < size) {
        w->overflow = true;
        return NULL;
    }
    uint8_t *at = w->data + w->size;
    w->size += size;
    return at;
}

void tide_write_u8(tide_writer *w, const uint8_t v)
{
    uint8_t *at = room(w, 1);
    if (at) at[0] = v;
}

void tide_write_u16(tide_writer *w, const uint16_t v)
{
    uint8_t *at = room(w, 2);
    if (!at) return;
    at[0] = (uint8_t)v;
    at[1] = (uint8_t)(v >> 8);
}

void tide_write_u32(tide_writer *w, const uint32_t v)
{
    uint8_t *at = room(w, 4);
    if (!at) return;
    for (int i = 0; i < 4; i++) at[i] = (uint8_t)(v >> (8 * i));
}

void tide_write_u64(tide_writer *w, const uint64_t v)
{
    uint8_t *at = room(w, 8);
    if (!at) return;
    for (int i = 0; i < 8; i++) at[i] = (uint8_t)(v >> (8 * i));
}

void tide_write_bytes(tide_writer *w, const void *data, const uint32_t size)
{
    uint8_t *at = room(w, size);
    if (at && size) memcpy(at, data, size);
}

void tide_write_varint(tide_writer *w, uint32_t v)
{
    uint8_t bytes[5];
    uint32_t n = 0;
    while (v >= 0x80u) {
        bytes[n++] = (uint8_t)(v | 0x80u);
        v >>= 7;
    }
    bytes[n++] = (uint8_t)v;
    tide_write_bytes(w, bytes, n);
}

uint8_t *tide_write_space(tide_writer *w, const uint32_t size)
{
    return room(w, size);
}

static const uint8_t *take(tide_reader *r, const uint32_t size)
{
    if (r->failed || r->size - r->at < size) {
        r->failed = true;
        return NULL;
    }
    const uint8_t *at = r->data + r->at;
    r->at += size;
    return at;
}

uint8_t tide_read_u8(tide_reader *r)
{
    const uint8_t *at = take(r, 1);
    return at ? at[0] : 0;
}

uint16_t tide_read_u16(tide_reader *r)
{
    const uint8_t *at = take(r, 2);
    return at ? (uint16_t)(at[0] | at[1] << 8) : 0;
}

uint32_t tide_read_u32(tide_reader *r)
{
    const uint8_t *at = take(r, 4);
    if (!at) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)at[i] << (8 * i);
    return v;
}

uint64_t tide_read_u64(tide_reader *r)
{
    const uint8_t *at = take(r, 8);
    if (!at) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)at[i] << (8 * i);
    return v;
}

void tide_write_varint64(tide_writer *w, uint64_t v)
{
    uint8_t bytes[10];
    uint32_t n = 0;
    while (v >= 0x80u) {
        bytes[n++] = (uint8_t)(v | 0x80u);
        v >>= 7;
    }
    bytes[n++] = (uint8_t)v;
    tide_write_bytes(w, bytes, n);
}

uint64_t tide_read_varint64(tide_reader *r)
{
    uint64_t v = 0;
    for (uint32_t shift = 0; shift < 70; shift += 7) {
        const uint8_t byte = tide_read_u8(r);
        if (shift == 63 && byte > 0x01u) break; // More than 64 bits
        v |= (uint64_t)(byte & 0x7Fu) << shift;
        if (!(byte & 0x80u)) return v;
    }
    r->failed = true;
    return 0;
}

uint32_t tide_read_varint(tide_reader *r)
{
    uint32_t v = 0;
    for (uint32_t shift = 0; shift < 35; shift += 7) {
        const uint8_t byte = tide_read_u8(r);
        if (shift == 28 && byte > 0x0Fu) break; // More than 32 bits
        v |= (uint32_t)(byte & 0x7Fu) << shift;
        if (!(byte & 0x80u)) return v;
    }
    r->failed = true;
    return 0;
}

const uint8_t *tide_read_bytes(tide_reader *r, const uint32_t size)
{
    return take(r, size);
}

// ---------------------------------------------------------------------------
// Bits: the first bit is the lowest of the first byte.

void tide_bits_put(tide_bits *b, const uint32_t value, const uint32_t count)
{
    if (b->overflow) return;
    if (b->bit + count > b->capacity * 8u) {
        b->overflow = true;
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t at = b->bit + i;
        const uint8_t mask = (uint8_t)(1u << (at % 8u));
        if (value >> i & 1u) b->data[at / 8u] |= mask;
        else b->data[at / 8u] &= (uint8_t)~mask;
    }
    b->bit += count;
}

uint32_t tide_bits_get(tide_bits *b, const uint32_t count)
{
    if (b->overflow || b->bit + count > b->capacity * 8u) {
        b->overflow = true;
        return 0;
    }
    uint32_t v = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t at = b->bit + i;
        v |= (uint32_t)(b->data[at / 8u] >> (at % 8u) & 1u) << i;
    }
    b->bit += count;
    return v;
}

uint32_t tide_bits_end(tide_bits *b)
{
    if (b->overflow) return 0;
    if (b->bit % 8u) b->data[b->bit / 8u] &= (uint8_t)((1u << (b->bit % 8u)) - 1u);
    return (b->bit + 7u) / 8u;
}

void tide_bits_put_f32(tide_bits *b, const float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    tide_bits_put(b, bits, 32);
}

float tide_bits_get_f32(tide_bits *b)
{
    const uint32_t bits = tide_bits_get(b, 32);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

uint32_t tide_f32_bits(const float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    return bits;
}

void tide_bits_put_changed(tide_bits *b, const uint32_t now, const uint32_t was)
{
    tide_bits_put_bool(b, now != was);
    if (now != was) tide_bits_put(b, now, 32);
}

void tide_bits_put_varint(tide_bits *b, uint32_t v)
{
    while (v >= 0x80u) {
        tide_bits_put(b, v & 0x7Fu, 7);
        tide_bits_put_bool(b, true);
        v >>= 7;
    }
    tide_bits_put(b, v, 7);
    tide_bits_put_bool(b, false);
}

uint32_t tide_bits_get_varint(tide_bits *b)
{
    uint32_t v = 0;
    for (uint32_t shift = 0; shift < 35; shift += 7) {
        v |= tide_bits_get(b, 7) << shift; // Past 32 bits, the rest falls off
        if (!tide_bits_get_bool(b)) break;
    }
    return v;
}

void tide_bits_put_changed_varint(tide_bits *b, const uint32_t now, const uint32_t was)
{
    tide_bits_put_bool(b, now != was);
    if (now != was) tide_bits_put_varint(b, now);
}

void tide_bits_put_changed_difference(tide_bits *b, const uint32_t now, const uint32_t was)
{
    tide_bits_put_bool(b, now != was);
    if (now != was) tide_bits_put_varint(b, tide_zigzag((int32_t)(now - was)));
}

uint32_t tide_bits_get_difference(tide_bits *b, const uint32_t was)
{
    return was + (uint32_t)tide_unzigzag(tide_bits_get_varint(b));
}

void tide_bits_put_varint64(tide_bits *b, uint64_t v)
{
    while (v >= 0x80u) {
        tide_bits_put(b, (uint32_t)(v & 0x7Fu), 7);
        tide_bits_put_bool(b, true);
        v >>= 7;
    }
    tide_bits_put(b, (uint32_t)v, 7);
    tide_bits_put_bool(b, false);
}

uint64_t tide_bits_get_varint64(tide_bits *b)
{
    uint64_t v = 0;
    for (uint32_t shift = 0; shift < 70; shift += 7) {
        v |= (uint64_t)tide_bits_get(b, 7) << shift; // Past 64 bits, the rest falls off
        if (!tide_bits_get_bool(b)) break;
    }
    return v;
}

void tide_bits_put_changed_difference64(tide_bits *b, const uint64_t now, const uint64_t was)
{
    tide_bits_put_bool(b, now != was);
    if (now != was) tide_bits_put_varint64(b, tide_zigzag64((int64_t)(now - was)));
}

uint64_t tide_bits_get_difference64(tide_bits *b, const uint64_t was)
{
    return was + (uint64_t)tide_unzigzag64(tide_bits_get_varint64(b));
}

void tide_bits_put_changed_xor(tide_bits *b, const uint32_t now, const uint32_t was)
{
    const uint32_t x = now ^ was;
    tide_bits_put_bool(b, x != 0);
    if (!x) return;
    const uint32_t lead = (uint32_t)__builtin_clz(x);
    const uint32_t trail = (uint32_t)__builtin_ctz(x);
    const uint32_t len = 32u - lead - trail;
    const bool window = 10u + len < 32u;
    tide_bits_put_bool(b, window);
    if (!window) {
        tide_bits_put(b, now, 32);
        return;
    }
    tide_bits_put(b, lead, 5);
    tide_bits_put(b, len - 1u, 5);
    tide_bits_put(b, x >> trail, len);
}

uint32_t tide_bits_get_xor(tide_bits *b, const uint32_t was)
{
    if (!tide_bits_get_bool(b)) return tide_bits_get(b, 32);
    const uint32_t lead = tide_bits_get(b, 5);
    const uint32_t len = tide_bits_get(b, 5) + 1u;
    if (lead + len > 32u) { // Bits past either end: not what any input packs to
        b->overflow = true;
        return was;
    }
    return was ^ (tide_bits_get(b, len) << (32u - lead - len));
}

float tide_f32_from_bits(const uint32_t bits)
{
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

// ---------------------------------------------------------------------------
// Hash: 8 bytes at a time, read little-endian, as every platform Tide
// supports stores them.

static uint64_t mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

static uint64_t add_word(uint64_t h, const uint64_t word)
{
    h ^= word * 0xBF58476D1CE4E5B9ull;
    return (h << 27 | h >> 37) * 0x94D049BB133111EBull;
}

uint64_t tide_hash_more(uint64_t h, const void *data, const size_t size)
{
    const uint8_t *p = data;
    h = add_word(h, (uint64_t)size);
    size_t i = 0;
    for (; i + 8u <= size; i += 8u) {
        uint64_t word;
        memcpy(&word, p + i, 8);
        h = add_word(h, word);
    }
    if (i < size) {
        uint64_t tail = 0;
        for (size_t k = 0; i + k < size; k++) tail |= (uint64_t)p[i + k] << (8u * k);
        h = add_word(h, tail);
    }
    return h;
}

uint64_t tide_hash_end(const uint64_t h)
{
    return mix(h);
}

uint64_t tide_hash(const void *data, const size_t size)
{
    return tide_hash_end(tide_hash_more(TIDE_HASH_START, data, size));
}

// ---------------------------------------------------------------------------
// A page's hash: XXH3's accumulation, with keys and an end of our own. The run,
// with zeros after it to a whole number of stripes of 64 bytes, goes a stripe
// at a time into eight accumulators of 64 bits, one a word: for word j of a
// stripe, d, and its key word k, accumulator j adds the product of the low
// and high 32 bits of d ^ k, and accumulator j ^ 1 adds d itself (a product of
// zero would lose the word otherwise, and a word's sum is never where its
// product is). Stripe n of each block of 16 takes key words n to n + 7, and
// after a block each accumulator a becomes ((a ^ a >> 47) ^ key[16 + j]) times
// WIDE_SCRAMBLE. Then the size and the eight accumulators are tide_hash's
// chain's words, and its end is tide_hash's. All of it is integer arithmetic,
// so a way that takes vectors gives the bits the plain words give, everywhere;
// 32 by 32 to 64-bit multiplies are what SSE2, NEON and WebAssembly's SIMD all
// have, where a 64 by 64 to 128-bit one is several multiplies on wasm32.
//
// The keys and the accumulators' starting values are splitmix64's numbers
// from 0x7469646568617368 ("tidehash"), the first 24 and the next 8.

static const uint64_t wide_key[24] = {
    0xF1D471BF300D2ACAull, 0xB98D6EEDD4DBCB43ull, 0xE41EE98106E4C3FCull, 0x9C8A5940DDCAF100ull,
    0x11B96D3559621C0Full, 0x2F6492F64C4825C7ull, 0x4F68DFF234BD3F70ull, 0x5117AE9AB1A42248ull,
    0xAD0C1209D1D3DB34ull, 0xAC457600ABAB1F16ull, 0x4B606D258A0FA763ull, 0x043784BAA9E95F59ull,
    0x0AE390D1EF5642B1ull, 0x9D695D2FAB53C13Bull, 0x532E68DCA3E90347ull, 0xF8E6E506A1500E40ull,
    0x47CDFC62D58F7BF2ull, 0x296BDD3EE2449306ull, 0x1F8513F3B10D4892ull, 0xDB64DF13CE0E619Full,
    0xABEF04B0B493CF32ull, 0x5ECC859D5BDACECEull, 0x8D192B91AAD61BCDull, 0xC15E687D2404F4AAull,
};

static const uint64_t wide_start[8] = {
    0xAD7D5C3025A25336ull, 0x2F00DA7797F2515Aull, 0x1934E91F08D7208Cull, 0xF312F6ED745EF31Aull,
    0x0662C9D801DEFBCEull, 0xA776B25C6399975Aull, 0x7821036870D8D6C0ull, 0x0373291AABC48B14ull,
};

#define WIDE_STRIPE 64u
#define WIDE_BLOCK 16u // Stripes a block
#define WIDE_SCRAMBLE 0x9E3779B1u

static uint64_t wide_end(const uint64_t acc[8], const size_t size)
{
    uint64_t h = add_word(TIDE_HASH_START, (uint64_t)size);
    for (uint32_t j = 0; j < 8u; j++) h = add_word(h, acc[j]);
    return mix(h);
}

// Each way goes through whole blocks with the next block asked for meanwhile
// (a hint, which changes no bits: hardware prefetchers stop at each 4 KiB of
// memory, and a page is more), then the stripes left, then the last one short,
// copied into zeros.
#define WIDE_RUN(acc, stripe, scramble, prefetch)                                                  \
    do {                                                                                            \
        const uint8_t *p = data;                                                                    \
        const size_t whole = size / WIDE_STRIPE;                                                    \
        size_t n = 0;                                                                               \
        for (; n + WIDE_BLOCK <= whole; n += WIDE_BLOCK) {                                          \
            const bool more = n + 2u * WIDE_BLOCK <= whole;                                         \
            for (uint32_t s = 0; s < WIDE_BLOCK; s += 2u) {                                         \
                if (prefetch && more) __builtin_prefetch(p + (n + WIDE_BLOCK + s) * WIDE_STRIPE);   \
                stripe(acc, p + (n + s) * WIDE_STRIPE, wide_key + s);                               \
                if (prefetch && more) __builtin_prefetch(p + (n + WIDE_BLOCK + s + 1u) * WIDE_STRIPE); \
                stripe(acc, p + (n + s + 1u) * WIDE_STRIPE, wide_key + s + 1u);                     \
            }                                                                                       \
            scramble(acc);                                                                          \
        }                                                                                           \
        for (; n < whole; n++) stripe(acc, p + n * WIDE_STRIPE, wide_key + n % WIDE_BLOCK);          \
        if (size % WIDE_STRIPE) {                                                                   \
            uint8_t last[WIDE_STRIPE] = {0};                                                        \
            memcpy(last, p + n * WIDE_STRIPE, size % WIDE_STRIPE);                                  \
            stripe(acc, last, wide_key + n % WIDE_BLOCK);                                           \
            if (n % WIDE_BLOCK == WIDE_BLOCK - 1u) scramble(acc);                                   \
        }                                                                                           \
    } while (0)

#ifdef __wasm_simd128__
// WebAssembly's own: two vectors' low halves gathered into one vector and
// their high halves into another, then extmul_low and extmul_high multiply
// them, four products for two shuffles, where i64x2.mul is three multiplies a
// vector under V8.
static inline void wide_stripe_wasm(v128_t acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 4u; v += 2u) {
        const v128_t x = wasm_v128_load(p + 16u * v), y = wasm_v128_load(p + 16u * v + 16u);
        const v128_t dx = wasm_v128_xor(x, wasm_v128_load(key + 2u * v));
        const v128_t dy = wasm_v128_xor(y, wasm_v128_load(key + 2u * v + 2u));
        const v128_t lo = wasm_i32x4_shuffle(dx, dy, 0, 2, 4, 6), hi = wasm_i32x4_shuffle(dx, dy, 1, 3, 5, 7);
        acc[v] = wasm_i64x2_add(acc[v], wasm_i64x2_add(wasm_i64x2_shuffle(x, x, 1, 0), wasm_u64x2_extmul_low_u32x4(lo, hi)));
        acc[v + 1u] =
            wasm_i64x2_add(acc[v + 1u], wasm_i64x2_add(wasm_i64x2_shuffle(y, y, 1, 0), wasm_u64x2_extmul_high_u32x4(lo, hi)));
    }
}

static inline void wide_scramble_wasm(v128_t acc[4])
{
    const v128_t by = wasm_i32x4_splat((int32_t)WIDE_SCRAMBLE);
    for (uint32_t v = 0; v < 4u; v++) {
        v128_t a = acc[v];
        a = wasm_v128_xor(a, wasm_u64x2_shr(a, 47));
        a = wasm_v128_xor(a, wasm_v128_load(wide_key + 16u + 2u * v));
        // a * WIDE_SCRAMBLE as the products of its low and its high halves, the high one moved up
        const v128_t halves = wasm_i32x4_shuffle(a, a, 0, 2, 1, 3);
        acc[v] = wasm_i64x2_add(wasm_u64x2_extmul_low_u32x4(halves, by), wasm_i64x2_shl(wasm_u64x2_extmul_high_u32x4(halves, by), 32));
    }
}

uint64_t tide_hash_wide_baseline(const void *data, const size_t size)
{
    v128_t acc[4];
    memcpy(acc, wide_start, sizeof acc);
    WIDE_RUN(acc, wide_stripe_wasm, wide_scramble_wasm, false);
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return wide_end(out, size);
}
#else
// In clang's vectors of two words, which are SSE2's, NEON's and (where the
// build has no SIMD) plain words: the product of a word's halves is pmuludq
// on x86 and umlal on ARM.
typedef uint64_t wide_v2 __attribute__((vector_size(16)));

static inline wide_v2 wide_load2(const void *p)
{
    wide_v2 v;
    memcpy(&v, p, 16);
    return v;
}

static inline void wide_stripe2(wide_v2 acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 4u; v++) {
        const wide_v2 d = wide_load2(p + 16u * v);
        const wide_v2 dk = d ^ wide_load2(key + 2u * v);
        acc[v] += __builtin_shufflevector(d, d, 1, 0) + (dk & 0xFFFFFFFFu) * (dk >> 32);
    }
}

static inline void wide_scramble2(wide_v2 acc[4])
{
    for (uint32_t v = 0; v < 4u; v++) {
        wide_v2 a = acc[v];
        a ^= a >> 47;
        a ^= wide_load2(wide_key + 16u + 2u * v);
        acc[v] = (a & 0xFFFFFFFFu) * WIDE_SCRAMBLE + (((a >> 32) * WIDE_SCRAMBLE) << 32);
    }
}

uint64_t tide_hash_wide_baseline(const void *data, const size_t size)
{
    wide_v2 acc[4];
    memcpy(acc, wide_start, sizeof acc);
    WIDE_RUN(acc, wide_stripe2, wide_scramble2, true);
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return wide_end(out, size);
}
#endif

#if defined(__x86_64__) || defined(_M_X64)
// AVX2's vectors of four words, a stripe in two, where the CPU has them:
// twice SSE2's speed on a page in cache.
typedef uint64_t wide_v4 __attribute__((vector_size(32)));

__attribute__((target("avx2"))) static inline void wide_stripe4(wide_v4 acc[2], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 2u; v++) {
        wide_v4 d, k;
        memcpy(&d, p + 32u * v, 32);
        memcpy(&k, key + 4u * v, 32);
        const wide_v4 dk = d ^ k;
        acc[v] += __builtin_shufflevector(d, d, 1, 0, 3, 2) + (dk & 0xFFFFFFFFu) * (dk >> 32);
    }
}

__attribute__((target("avx2"))) static inline void wide_scramble4(wide_v4 acc[2])
{
    for (uint32_t v = 0; v < 2u; v++) {
        wide_v4 a = acc[v], k;
        memcpy(&k, wide_key + 16u + 4u * v, 32);
        a ^= a >> 47;
        a ^= k;
        acc[v] = (a & 0xFFFFFFFFu) * WIDE_SCRAMBLE + (((a >> 32) * WIDE_SCRAMBLE) << 32);
    }
}

__attribute__((target("avx2"))) static uint64_t wide_avx2(const void *data, const size_t size)
{
    wide_v4 acc[2];
    memcpy(acc, wide_start, sizeof acc);
    WIDE_RUN(acc, wide_stripe4, wide_scramble4, true);
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return wide_end(out, size);
}

static void wide_cpuid(const uint32_t leaf, uint32_t r[4])
{
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0u));
}

// AVX2, and the system saving AVX's registers (OSXSAVE, then XCR0's SSE and
// AVX bits). Looked at once; threads that look at the same time find the same.
static bool wide_has_avx2(void)
{
    static int known; // 0 until looked at, then 1 without AVX2 and 2 with it
    int got = __atomic_load_n(&known, __ATOMIC_RELAXED);
    if (!got) {
        uint32_t r[4];
        wide_cpuid(0, r);
        bool avx2 = r[0] >= 7u;
        if (avx2) {
            wide_cpuid(1, r);
            avx2 = (r[2] & (1u << 27 | 1u << 28)) == (1u << 27 | 1u << 28);
        }
        if (avx2) {
            uint32_t low, high;
            __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0u));
            (void)high;
            avx2 = (low & 6u) == 6u;
        }
        if (avx2) {
            wide_cpuid(7, r);
            avx2 = (r[1] & 1u << 5) != 0;
        }
        got = avx2 ? 2 : 1;
        __atomic_store_n(&known, got, __ATOMIC_RELAXED);
    }
    return got == 2;
}

uint64_t tide_hash_wide(const void *data, const size_t size)
{
    return wide_has_avx2() ? wide_avx2(data, size) : tide_hash_wide_baseline(data, size);
}
#else
uint64_t tide_hash_wide(const void *data, const size_t size)
{
    return tide_hash_wide_baseline(data, size);
}
#endif
