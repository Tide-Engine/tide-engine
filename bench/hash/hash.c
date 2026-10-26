// Page hashes timed: the engine's (tide_hash_wide) beside candidate hashes, on
// the shapes a world's hash reads (8 KiB lane pages, 16 KiB pages, worlds of
// many in cache and out of it, small runs), natively and as WebAssembly under
// Node. The engine's is "stripes". Every candidate is integer arithmetic only,
// so a SIMD way of one gives the bits its plain C gives on every machine, which
// every run checks before anything else.
//
//   bench_hash [--rounds n] [--only name] [--quick] [--quality]
//
// --quality runs the sanity checks of what a hash tells apart (avalanche,
// one- and two-bit changes, zero runs, counters) instead of timing.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide/time.h"

#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#endif

#define START 0x9E3779B97F4A7C15ull

// ---------------------------------------------------------------------------
// tide_hash's step and end, which net.c keeps to itself

static inline uint64_t add_word(uint64_t h, const uint64_t word)
{
    h ^= word * 0xBF58476D1CE4E5B9ull;
    return (h << 27 | h >> 37) * 0x94D049BB133111EBull;
}

static inline uint64_t mix(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

static inline uint64_t read64(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

// ---------------------------------------------------------------------------
// The engine's, as it is

// Four of tide_hash's chains side by side,
// chain k taking the words at 8k, 32 + 8k and on, what's left after the last
// round a word a chain, the last short and zeros past its bytes, then the size
// and the chains one chain's words.
static uint64_t hash_chains4(const void *data, const size_t size)
{
    const uint8_t *p = data;
    uint64_t chain[4];
    for (uint64_t k = 0; k < 4u; k++) chain[k] = START + k * 0xBF58476D1CE4E5B9ull;
    size_t i = 0;
    for (; i + 32u <= size; i += 32u) {
        chain[0] = add_word(chain[0], read64(p + i));
        chain[1] = add_word(chain[1], read64(p + i + 8u));
        chain[2] = add_word(chain[2], read64(p + i + 16u));
        chain[3] = add_word(chain[3], read64(p + i + 24u));
    }
    uint32_t k = 0;
    for (; i + 8u <= size; i += 8u, k++) chain[k] = add_word(chain[k], read64(p + i));
    if (i < size) {
        uint64_t tail = 0;
        for (size_t at = 0; i + at < size; at++) tail |= (uint64_t)p[i + at] << (8u * at);
        chain[k] = add_word(chain[k], tail);
    }
    uint64_t h = add_word(START, (uint64_t)size);
    for (k = 0; k < 4u; k++) h = add_word(h, chain[k]);
    return mix(h);
}

// The engine's own, as it is (the stripes below, on the machine's widest
// vectors), and on the vectors every CPU of the platform has
static uint64_t hash_engine(const void *data, const size_t size)
{
    return tide_hash_wide(data, size);
}

static uint64_t hash_engine_baseline(const void *data, const size_t size)
{
    return tide_hash_wide_baseline(data, size);
}

// Eight chains instead of four: 64 bytes a round

static uint64_t hash_chains8(const void *data, const size_t size)
{
    const uint8_t *p = data;
    uint64_t chain[8];
    for (uint64_t k = 0; k < 8u; k++) chain[k] = START + k * 0xBF58476D1CE4E5B9ull;
    size_t i = 0;
    for (; i + 64u <= size; i += 64u) {
        for (uint32_t k = 0; k < 8u; k++) chain[k] = add_word(chain[k], read64(p + i + 8u * k));
    }
    uint32_t k = 0;
    for (; i + 8u <= size; i += 8u, k++) chain[k] = add_word(chain[k], read64(p + i));
    if (i < size) {
        uint64_t tail = 0;
        for (size_t at = 0; i + at < size; at++) tail |= (uint64_t)p[i + at] << (8u * at);
        chain[k] = add_word(chain[k], tail);
    }
    uint64_t h = add_word(START, (uint64_t)size);
    for (k = 0; k < 8u; k++) h = add_word(h, chain[k]);
    return mix(h);
}

// ---------------------------------------------------------------------------
// wyhash (Wang Yi's, final version 4, as bench/versus/rollback has it): 48
// bytes a round in three chains of a 64 by 64 to 128-bit multiply each. With
// clang's 128-bit integers, and with four 32-bit multiplies in their place,
// which is what wasm32 has.

#ifdef __SIZEOF_INT128__
__extension__ typedef unsigned __int128 wy_u128;
#endif

static inline void wy_mum128(uint64_t *a, uint64_t *b)
{
#ifdef __SIZEOF_INT128__
    const wy_u128 r = (wy_u128)*a * *b;
    *a = (uint64_t)r;
    *b = (uint64_t)(r >> 64);
#else
    (void)a;
    (void)b;
    abort();
#endif
}

static inline void wy_mum32(uint64_t *a, uint64_t *b)
{
    const uint64_t ha = *a >> 32, hb = *b >> 32, la = (uint32_t)*a, lb = (uint32_t)*b;
    const uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb, t = rl + (rm0 << 32);
    uint64_t c = t < rl;
    const uint64_t lo = t + (rm1 << 32);
    c += lo < t;
    *a = lo;
    *b = rh + (rm0 >> 32) + (rm1 >> 32) + c;
}

static inline uint64_t wy_r4(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static const uint64_t wy_p[4] = {0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull, 0x4b33a62ed433d4a3ull, 0x4d5a2da51de1aa47ull};

#define WY_HASH(name, mum)                                                                                   \
    static inline uint64_t name##_mix(uint64_t a, uint64_t b)                                               \
    {                                                                                                        \
        mum(&a, &b);                                                                                         \
        return a ^ b;                                                                                        \
    }                                                                                                        \
    static uint64_t name(const void *key, const size_t len)                                                 \
    {                                                                                                        \
        const uint8_t *p = key;                                                                              \
        uint64_t seed = 0;                                                                                   \
        seed ^= name##_mix(seed ^ wy_p[0], wy_p[1]);                                                         \
        uint64_t a, b;                                                                                       \
        if (len <= 16) {                                                                                     \
            if (len >= 4) {                                                                                  \
                a = (wy_r4(p) << 32) | wy_r4(p + ((len >> 3) << 2));                                         \
                b = (wy_r4(p + len - 4) << 32) | wy_r4(p + len - 4 - ((len >> 3) << 2));                     \
            } else if (len > 0) {                                                                            \
                a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];                      \
                b = 0;                                                                                       \
            } else {                                                                                         \
                a = b = 0;                                                                                   \
            }                                                                                                \
        } else {                                                                                             \
            size_t i = len;                                                                                  \
            if (i > 48) {                                                                                    \
                uint64_t see1 = seed, see2 = seed;                                                           \
                do {                                                                                         \
                    seed = name##_mix(read64(p) ^ wy_p[1], read64(p + 8) ^ seed);                            \
                    see1 = name##_mix(read64(p + 16) ^ wy_p[2], read64(p + 24) ^ see1);                      \
                    see2 = name##_mix(read64(p + 32) ^ wy_p[3], read64(p + 40) ^ see2);                      \
                    p += 48;                                                                                 \
                    i -= 48;                                                                                 \
                } while (i > 48);                                                                            \
                seed ^= see1 ^ see2;                                                                         \
            }                                                                                                \
            while (i > 16) {                                                                                 \
                seed = name##_mix(read64(p) ^ wy_p[1], read64(p + 8) ^ seed);                                \
                i -= 16;                                                                                     \
                p += 16;                                                                                     \
            }                                                                                                \
            a = read64(p + i - 16);                                                                          \
            b = read64(p + i - 8);                                                                           \
        }                                                                                                    \
        a ^= wy_p[1];                                                                                        \
        b ^= seed;                                                                                           \
        mum(&a, &b);                                                                                         \
        return name##_mix(a ^ wy_p[0] ^ len, b ^ wy_p[1]);                                                   \
    }

WY_HASH(hash_wy128, wy_mum128)
WY_HASH(hash_wy32, wy_mum32)

// ---------------------------------------------------------------------------
// Stripes, XXH3's accumulation with our own keys and end: the run, zeros
// after it to a whole number of stripes of 64 bytes, goes a stripe at a time
// into eight accumulators of 64 bits, one a word. For word j of a stripe,
// d, and its key word k: accumulator j adds the product of d ^ k's low and
// high 32 bits, and accumulator j ^ 1 adds d itself (a product of zero would
// lose the word otherwise). Stripe n of a block of 16 takes key words n to
// n + 7, and after the block each accumulator a becomes
// ((a ^ a >> 47) ^ key[16 + j]) * 0x9E3779B1. Then the size and the eight
// accumulators are tide_hash's chain's words, and its end is tide_hash's.
// Multiplies of 32 by 32 bits to 64 are what SSE2, NEON and WebAssembly's
// SIMD all have. This is the engine's page hash; its plain definition here is
// what "engine" is checked against.

static const uint64_t stripe_key[24] = {
    0xF1D471BF300D2ACAull, 0xB98D6EEDD4DBCB43ull, 0xE41EE98106E4C3FCull, 0x9C8A5940DDCAF100ull,
    0x11B96D3559621C0Full, 0x2F6492F64C4825C7ull, 0x4F68DFF234BD3F70ull, 0x5117AE9AB1A42248ull,
    0xAD0C1209D1D3DB34ull, 0xAC457600ABAB1F16ull, 0x4B606D258A0FA763ull, 0x043784BAA9E95F59ull,
    0x0AE390D1EF5642B1ull, 0x9D695D2FAB53C13Bull, 0x532E68DCA3E90347ull, 0xF8E6E506A1500E40ull,
    0x47CDFC62D58F7BF2ull, 0x296BDD3EE2449306ull, 0x1F8513F3B10D4892ull, 0xDB64DF13CE0E619Full,
    0xABEF04B0B493CF32ull, 0x5ECC859D5BDACECEull, 0x8D192B91AAD61BCDull, 0xC15E687D2404F4AAull,
};

static const uint64_t stripe_start[8] = {
    0xAD7D5C3025A25336ull, 0x2F00DA7797F2515Aull, 0x1934E91F08D7208Cull, 0xF312F6ED745EF31Aull,
    0x0662C9D801DEFBCEull, 0xA776B25C6399975Aull, 0x7821036870D8D6C0ull, 0x0373291AABC48B14ull,
};

#define STRIPE 64u
#define BLOCK 16u // Stripes a block
#define SCRAMBLE 0x9E3779B1u

static uint64_t stripe_end(const uint64_t acc[8], const size_t size)
{
    uint64_t h = add_word(START, (uint64_t)size);
    for (uint32_t j = 0; j < 8u; j++) h = add_word(h, acc[j]);
    return mix(h);
}

// Plain C, a word at a time, with clang's vectorizer kept off: the
// definition, and what a machine with no SIMD would run.
static void stripe_plain(uint64_t acc[8], const uint8_t *p, const uint64_t *key)
{
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t j = 0; j < 8u; j++) {
        const uint64_t d = read64(p + 8u * j);
        const uint64_t dk = d ^ key[j];
        acc[j ^ 1u] += d;
        acc[j] += (dk & 0xFFFFFFFFu) * (dk >> 32);
    }
}

static void scramble_plain(uint64_t acc[8])
{
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t j = 0; j < 8u; j++) {
        uint64_t a = acc[j];
        a ^= a >> 47;
        a ^= stripe_key[16u + j];
        acc[j] = a * SCRAMBLE;
    }
}

static uint64_t hash_stripes_plain(const void *data, const size_t size)
{
    const uint8_t *p = data;
    uint64_t acc[8];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n < whole; n++) {
        stripe_plain(acc, p + n * STRIPE, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_plain(acc);
    }
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_plain(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_plain(acc);
    }
    return stripe_end(acc, size);
}

// The same in clang's vectors of two 64-bit lanes, which are SSE2's, NEON's
// and WebAssembly's: a product of a lane's low and high halves is pmuludq on
// x86 and umull on ARM.
typedef uint64_t u64x2 __attribute__((vector_size(16)));

static inline u64x2 load2(const void *p)
{
    u64x2 v;
    memcpy(&v, p, 16);
    return v;
}

static inline void stripe_vec(u64x2 acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 4u; v++) {
        const u64x2 d = load2(p + 16u * v);
        const u64x2 dk = d ^ load2(key + 2u * v);
        acc[v] += __builtin_shufflevector(d, d, 1, 0) + (dk & 0xFFFFFFFFu) * (dk >> 32);
    }
}

static inline void scramble_vec(u64x2 acc[4])
{
    for (uint32_t v = 0; v < 4u; v++) {
        u64x2 a = acc[v];
        a ^= a >> 47;
        a ^= load2(stripe_key + 16u + 2u * v);
        acc[v] = (a & 0xFFFFFFFFu) * SCRAMBLE + (((a >> 32) * SCRAMBLE) << 32);
    }
}

static uint64_t hash_stripes_vec(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x2 acc[4];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        for (uint32_t s = 0; s < BLOCK; s++) stripe_vec(acc, p + (n + s) * STRIPE, stripe_key + s);
        scramble_vec(acc);
    }
    for (; n < whole; n++) stripe_vec(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_vec(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_vec(acc);
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}

// With the next kilobyte asked for while this one is hashed: hardware
// prefetchers stop at each 4 KiB of memory, and a page is several. A
// prefetch is a hint, which changes no bits; WebAssembly has none.
static uint64_t hash_stripes_prefetch(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x2 acc[4];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        if (n + 2u * BLOCK <= whole) {
            for (uint32_t s = 0; s < BLOCK; s++) {
                __builtin_prefetch(p + (n + BLOCK + s) * STRIPE);
                stripe_vec(acc, p + (n + s) * STRIPE, stripe_key + s);
            }
        } else {
            for (uint32_t s = 0; s < BLOCK; s++) stripe_vec(acc, p + (n + s) * STRIPE, stripe_key + s);
        }
        scramble_vec(acc);
    }
    for (; n < whole; n++) stripe_vec(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_vec(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_vec(acc);
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}

// Two stripes a turn of the loop, with the prefetch
static uint64_t hash_stripes_x2(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x2 acc[4];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        const bool more = n + 2u * BLOCK <= whole;
        for (uint32_t s = 0; s < BLOCK; s += 2u) {
            if (more) __builtin_prefetch(p + (n + BLOCK + s) * STRIPE);
            stripe_vec(acc, p + (n + s) * STRIPE, stripe_key + s);
            if (more) __builtin_prefetch(p + (n + BLOCK + s + 1u) * STRIPE);
            stripe_vec(acc, p + (n + s + 1u) * STRIPE, stripe_key + s + 1u);
        }
        scramble_vec(acc);
    }
    for (; n < whole; n++) stripe_vec(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_vec(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_vec(acc);
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}

// The four chains with the same prefetch
static uint64_t hash_chains4_prefetch(const void *data, const size_t size)
{
    const uint8_t *p = data;
    uint64_t chain[4];
    for (uint64_t k = 0; k < 4u; k++) chain[k] = START + k * 0xBF58476D1CE4E5B9ull;
    size_t i = 0;
    for (; i + 32u <= size; i += 32u) {
        if (i + 1024u + 32u <= size && i % 64u == 0) __builtin_prefetch(p + i + 1024u);
        chain[0] = add_word(chain[0], read64(p + i));
        chain[1] = add_word(chain[1], read64(p + i + 8u));
        chain[2] = add_word(chain[2], read64(p + i + 16u));
        chain[3] = add_word(chain[3], read64(p + i + 24u));
    }
    uint32_t k = 0;
    for (; i + 8u <= size; i += 8u, k++) chain[k] = add_word(chain[k], read64(p + i));
    if (i < size) {
        uint64_t tail = 0;
        for (size_t at = 0; i + at < size; at++) tail |= (uint64_t)p[i + at] << (8u * at);
        chain[k] = add_word(chain[k], tail);
    }
    uint64_t h = add_word(START, (uint64_t)size);
    for (k = 0; k < 4u; k++) h = add_word(h, chain[k]);
    return mix(h);
}

#if defined(__x86_64__) || defined(_M_X64)
// AVX2's 256-bit vectors: a stripe in two, for machines that have it (checked
// as it starts).
#define HASH_AVX2 1
typedef uint64_t u64x4 __attribute__((vector_size(32)));

__attribute__((target("avx2"))) static uint64_t hash_stripes_avx2(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x4 acc[2];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    uint8_t last[STRIPE] = {0};
    for (;;) {
        const uint8_t *at;
        uint32_t s = (uint32_t)(n % BLOCK);
        if (n < whole) at = p + n * STRIPE;
        else if (n == whole && size % STRIPE) {
            memcpy(last, p + n * STRIPE, size % STRIPE);
            at = last;
        } else break;
        for (uint32_t v = 0; v < 2u; v++) {
            u64x4 d, k;
            memcpy(&d, at + 32u * v, 32);
            memcpy(&k, stripe_key + s + 4u * v, 32);
            const u64x4 dk = d ^ k;
            acc[v] += __builtin_shufflevector(d, d, 1, 0, 3, 2) + (dk & 0xFFFFFFFFu) * (dk >> 32);
        }
        if (s == BLOCK - 1u) {
            for (uint32_t v = 0; v < 2u; v++) {
                u64x4 a = acc[v], k;
                memcpy(&k, stripe_key + 16u + 4u * v, 32);
                a ^= a >> 47;
                a ^= k;
                acc[v] = (a & 0xFFFFFFFFu) * SCRAMBLE + (((a >> 32) * SCRAMBLE) << 32);
            }
        }
        n++;
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}

// The same a block at a time, with the next block asked for meanwhile
__attribute__((target("avx2"))) static inline void stripe_avx2(u64x4 acc[2], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 2u; v++) {
        u64x4 d, k;
        memcpy(&d, p + 32u * v, 32);
        memcpy(&k, key + 4u * v, 32);
        const u64x4 dk = d ^ k;
        acc[v] += __builtin_shufflevector(d, d, 1, 0, 3, 2) + (dk & 0xFFFFFFFFu) * (dk >> 32);
    }
}

__attribute__((target("avx2"))) static inline void scramble_avx2(u64x4 acc[2])
{
    for (uint32_t v = 0; v < 2u; v++) {
        u64x4 a = acc[v], k;
        memcpy(&k, stripe_key + 16u + 4u * v, 32);
        a ^= a >> 47;
        a ^= k;
        acc[v] = (a & 0xFFFFFFFFu) * SCRAMBLE + (((a >> 32) * SCRAMBLE) << 32);
    }
}

__attribute__((target("avx2"))) static uint64_t hash_stripes_avx2_prefetch(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x4 acc[2];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        if (n + 2u * BLOCK <= whole) {
            for (uint32_t s = 0; s < BLOCK; s++) {
                __builtin_prefetch(p + (n + BLOCK + s) * STRIPE);
                stripe_avx2(acc, p + (n + s) * STRIPE, stripe_key + s);
            }
        } else {
            for (uint32_t s = 0; s < BLOCK; s++) stripe_avx2(acc, p + (n + s) * STRIPE, stripe_key + s);
        }
        scramble_avx2(acc);
    }
    for (; n < whole; n++) stripe_avx2(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_avx2(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_avx2(acc);
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}

// AVX's registers saved by the system (OSXSAVE, then XCR0's SSE and AVX
// bits), and AVX2 itself
static bool has_avx2(void)
{
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d) || !(c & (1u << 27)) || !(c & (1u << 28))) return false;
    unsigned lo, hi;
    __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    (void)hi;
    if ((lo & 6u) != 6u) return false;
    return __get_cpuid_count(7, 0, &a, &b, &c, &d) && (b & (1u << 5)) != 0;
}
#endif

#ifdef __wasm_simd128__
// WebAssembly's own: two vectors' halves gathered into one vector of low
// halves and one of high (a shuffle each), then extmul_low and extmul_high
// multiply them, four products for two shuffles, where i64x2.mul is three
// multiplies a vector under V8.
static inline void stripe_wasm(v128_t acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t v = 0; v < 4u; v += 2u) {
        const v128_t x = wasm_v128_load(p + 16u * v), y = wasm_v128_load(p + 16u * v + 16u);
        const v128_t dx = wasm_v128_xor(x, wasm_v128_load(key + 2u * v));
        const v128_t dy = wasm_v128_xor(y, wasm_v128_load(key + 2u * v + 2u));
        const v128_t lo = wasm_i32x4_shuffle(dx, dy, 0, 2, 4, 6), hi = wasm_i32x4_shuffle(dx, dy, 1, 3, 5, 7);
        acc[v] = wasm_i64x2_add(acc[v], wasm_i64x2_add(wasm_i64x2_shuffle(x, x, 1, 0), wasm_u64x2_extmul_low_u32x4(lo, hi)));
        acc[v + 1u] = wasm_i64x2_add(acc[v + 1u], wasm_i64x2_add(wasm_i64x2_shuffle(y, y, 1, 0), wasm_u64x2_extmul_high_u32x4(lo, hi)));
    }
}

// With i64x2.mul of the masked low halves and the high halves
static inline void stripe_wasm_mul(v128_t acc[4], const uint8_t *p, const uint64_t *key)
{
    const v128_t low = wasm_u64x2_splat(0xFFFFFFFFu);
    for (uint32_t v = 0; v < 4u; v++) {
        const v128_t d = wasm_v128_load(p + 16u * v);
        const v128_t dk = wasm_v128_xor(d, wasm_v128_load(key + 2u * v));
        const v128_t product = wasm_i64x2_mul(wasm_v128_and(dk, low), wasm_u64x2_shr(dk, 32));
        acc[v] = wasm_i64x2_add(acc[v], wasm_i64x2_add(wasm_i64x2_shuffle(d, d, 1, 0), product));
    }
}

static inline void scramble_wasm(v128_t acc[4])
{
    const v128_t by = wasm_i32x4_splat((int32_t)SCRAMBLE);
    for (uint32_t v = 0; v < 4u; v++) {
        v128_t a = acc[v];
        a = wasm_v128_xor(a, wasm_u64x2_shr(a, 47));
        a = wasm_v128_xor(a, wasm_v128_load(stripe_key + 16u + 2u * v));
        // a * SCRAMBLE as the products of its low and its high halves, the high one moved up
        const v128_t halves = wasm_i32x4_shuffle(a, a, 0, 2, 1, 3);
        const v128_t lo = wasm_u64x2_extmul_low_u32x4(halves, by), hi = wasm_u64x2_extmul_high_u32x4(halves, by);
        acc[v] = wasm_i64x2_add(lo, wasm_i64x2_shl(hi, 32));
    }
}

#define STRIPES_WASM(name, stripe)                                                         \
    static uint64_t name(const void *data, const size_t size)                             \
    {                                                                                      \
        const uint8_t *p = data;                                                           \
        v128_t acc[4];                                                                     \
        memcpy(acc, stripe_start, sizeof acc);                                             \
        const size_t whole = size / STRIPE;                                                \
        size_t n = 0;                                                                      \
        for (; n + BLOCK <= whole; n += BLOCK) {                                           \
            for (uint32_t s = 0; s < BLOCK; s++) stripe(acc, p + (n + s) * STRIPE, stripe_key + s); \
            scramble_wasm(acc);                                                            \
        }                                                                                  \
        for (; n < whole; n++) stripe(acc, p + n * STRIPE, stripe_key + n % BLOCK);        \
        if (size % STRIPE) {                                                               \
            uint8_t last[STRIPE] = {0};                                                    \
            memcpy(last, p + n * STRIPE, size % STRIPE);                                   \
            stripe(acc, last, stripe_key + n % BLOCK);                                     \
            if (n % BLOCK == BLOCK - 1u) scramble_wasm(acc);                               \
        }                                                                                  \
        uint64_t out[8];                                                                   \
        memcpy(out, acc, sizeof out);                                                      \
        return stripe_end(out, size);                                                      \
    }

STRIPES_WASM(hash_stripes_wasm, stripe_wasm)
STRIPES_WASM(hash_stripes_wasm_mul, stripe_wasm_mul)

// Two stripes a turn of the loop
static uint64_t hash_stripes_wasm_x2(const void *data, const size_t size)
{
    const uint8_t *p = data;
    v128_t acc[4];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        for (uint32_t s = 0; s < BLOCK; s += 2u) {
            stripe_wasm(acc, p + (n + s) * STRIPE, stripe_key + s);
            stripe_wasm(acc, p + (n + s + 1u) * STRIPE, stripe_key + s + 1u);
        }
        scramble_wasm(acc);
    }
    for (; n < whole; n++) stripe_wasm(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        stripe_wasm(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_wasm(acc);
    }
    uint64_t out[8];
    memcpy(out, acc, sizeof out);
    return stripe_end(out, size);
}
#endif

// ---------------------------------------------------------------------------
// Pairs: the stripes' accumulation with each product taken across two words,
// so WebAssembly's extmul and NEON's umull take two vectors as they come,
// with no gathering of halves first. In each 32 bytes of a stripe, words w0
// to w3 and dw = w ^ their key words:
//   acc0 += w1 + lo(dw0) * lo(dw2)     acc1 += w3 + hi(dw0) * hi(dw2)
//   acc2 += w0 + lo(dw1) * lo(dw3)     acc3 += w2 + hi(dw1) * hi(dw3)
// so a word's own sum is never in an accumulator its product is in. Keys,
// blocks, scramble and end are the stripes'.

static void pair_plain(uint64_t acc[8], const uint8_t *p, const uint64_t *key)
{
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t g = 0; g < 2u; g++) {
        uint64_t w[4], dw[4];
        for (uint32_t k = 0; k < 4u; k++) {
            w[k] = read64(p + 32u * g + 8u * k);
            dw[k] = w[k] ^ key[4u * g + k];
        }
        uint64_t *a = acc + 4u * g;
        a[0] += w[1] + (dw[0] & 0xFFFFFFFFu) * (dw[2] & 0xFFFFFFFFu);
        a[1] += w[3] + (dw[0] >> 32) * (dw[2] >> 32);
        a[2] += w[0] + (dw[1] & 0xFFFFFFFFu) * (dw[3] & 0xFFFFFFFFu);
        a[3] += w[2] + (dw[1] >> 32) * (dw[3] >> 32);
    }
}

static uint64_t hash_pairs_plain(const void *data, const size_t size)
{
    const uint8_t *p = data;
    uint64_t acc[8];
    memcpy(acc, stripe_start, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n < whole; n++) {
        pair_plain(acc, p + n * STRIPE, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_plain(acc);
    }
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        pair_plain(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) scramble_plain(acc);
    }
    return stripe_end(acc, size);
}

// SSE2's way round: pmuludq multiplies the low halves of two vectors' words,
// so vector 2g holds accumulators 4g and 4g + 2 and vector 2g + 1 holds
// 4g + 1 and 4g + 3. Its starting values and scramble keys in that order.
static const uint64_t pair_start_sse[8] = {
    0xAD7D5C3025A25336ull, 0x1934E91F08D7208Cull, 0x2F00DA7797F2515Aull, 0xF312F6ED745EF31Aull,
    0x0662C9D801DEFBCEull, 0x7821036870D8D6C0ull, 0xA776B25C6399975Aull, 0x0373291AABC48B14ull,
};
static const uint64_t pair_scramble_sse[8] = {
    0x47CDFC62D58F7BF2ull, 0x1F8513F3B10D4892ull, 0x296BDD3EE2449306ull, 0xDB64DF13CE0E619Full,
    0xABEF04B0B493CF32ull, 0x8D192B91AAD61BCDull, 0x5ECC859D5BDACECEull, 0xC15E687D2404F4AAull,
};

static inline void pair_vec(u64x2 acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t g = 0; g < 2u; g++) {
        const u64x2 x = load2(p + 32u * g), y = load2(p + 32u * g + 16u);
        const u64x2 dx = x ^ load2(key + 4u * g), dy = y ^ load2(key + 4u * g + 2u);
        acc[2u * g] += __builtin_shufflevector(x, x, 1, 0) + (dx & 0xFFFFFFFFu) * (dy & 0xFFFFFFFFu);
        acc[2u * g + 1u] += __builtin_shufflevector(y, y, 1, 0) + (dx >> 32) * (dy >> 32);
    }
}

static inline void pair_scramble_vec(u64x2 acc[4])
{
    for (uint32_t v = 0; v < 4u; v++) {
        u64x2 a = acc[v];
        a ^= a >> 47;
        a ^= load2(pair_scramble_sse + 2u * v);
        acc[v] = (a & 0xFFFFFFFFu) * SCRAMBLE + (((a >> 32) * SCRAMBLE) << 32);
    }
}

static uint64_t hash_pairs_vec(const void *data, const size_t size)
{
    const uint8_t *p = data;
    u64x2 acc[4];
    memcpy(acc, pair_start_sse, sizeof acc);
    const size_t whole = size / STRIPE;
    size_t n = 0;
    for (; n + BLOCK <= whole; n += BLOCK) {
        for (uint32_t s = 0; s < BLOCK; s++) pair_vec(acc, p + (n + s) * STRIPE, stripe_key + s);
        pair_scramble_vec(acc);
    }
    for (; n < whole; n++) pair_vec(acc, p + n * STRIPE, stripe_key + n % BLOCK);
    if (size % STRIPE) {
        uint8_t last[STRIPE] = {0};
        memcpy(last, p + n * STRIPE, size % STRIPE);
        pair_vec(acc, last, stripe_key + n % BLOCK);
        if (n % BLOCK == BLOCK - 1u) pair_scramble_vec(acc);
    }
    uint64_t v[8], out[8];
    memcpy(v, acc, sizeof v);
    for (uint32_t g = 0; g < 2u; g++) {
        out[4u * g] = v[4u * g];
        out[4u * g + 2u] = v[4u * g + 1u];
        out[4u * g + 1u] = v[4u * g + 2u];
        out[4u * g + 3u] = v[4u * g + 3u];
    }
    return stripe_end(out, size);
}

#ifdef __wasm_simd128__
// WebAssembly's way round is the accumulators' own order: extmul_low gives
// accumulators 4g and 4g + 1, extmul_high 4g + 2 and 4g + 3
static inline void pair_wasm(v128_t acc[4], const uint8_t *p, const uint64_t *key)
{
    for (uint32_t g = 0; g < 2u; g++) {
        const v128_t x = wasm_v128_load(p + 32u * g), y = wasm_v128_load(p + 32u * g + 16u);
        const v128_t dx = wasm_v128_xor(x, wasm_v128_load(key + 4u * g));
        const v128_t dy = wasm_v128_xor(y, wasm_v128_load(key + 4u * g + 2u));
        acc[2u * g] = wasm_i64x2_add(acc[2u * g], wasm_i64x2_add(wasm_i64x2_shuffle(x, y, 1, 3), wasm_u64x2_extmul_low_u32x4(dx, dy)));
        acc[2u * g + 1u] =
            wasm_i64x2_add(acc[2u * g + 1u], wasm_i64x2_add(wasm_i64x2_shuffle(x, y, 0, 2), wasm_u64x2_extmul_high_u32x4(dx, dy)));
    }
}

STRIPES_WASM(hash_pairs_wasm, pair_wasm)
#endif

// ---------------------------------------------------------------------------
// The candidates

typedef struct candidate {
    const char *name;
    uint64_t (*hash)(const void *data, size_t size);
    // The plain definition its bits have to be, or NULL
    uint64_t (*same_as)(const void *data, size_t size);
    // Set aside for what --quality found: reported, not a failure
    bool weak;
} candidate;

static const candidate candidates[] = {
    {"engine", hash_engine, hash_stripes_plain, false},
    {"engine_baseline", hash_engine_baseline, hash_stripes_plain, false},
    {"chains4", hash_chains4, NULL, false},
    {"chains8", hash_chains8, NULL, false},
#ifdef __SIZEOF_INT128__
    {"wyhash", hash_wy128, NULL, false},
#endif
    {"wyhash32", hash_wy32,
#ifdef __SIZEOF_INT128__
     hash_wy128,
#else
     NULL,
#endif
     false},
    {"chains4_prefetch", hash_chains4_prefetch, hash_chains4, false},
    {"stripes_plain", hash_stripes_plain, NULL, false},
    {"stripes", hash_stripes_vec, hash_stripes_plain, false},
    {"stripes_prefetch", hash_stripes_prefetch, hash_stripes_plain, false},
    {"stripes_x2", hash_stripes_x2, hash_stripes_plain, false},
#ifdef HASH_AVX2
    {"stripes_avx2", hash_stripes_avx2, hash_stripes_plain, false},
    {"stripes_avx2_prefetch", hash_stripes_avx2_prefetch, hash_stripes_plain, false},
#endif
#ifdef __wasm_simd128__
    {"stripes_wasm", hash_stripes_wasm, hash_stripes_plain, false},
    {"stripes_wasm_mul", hash_stripes_wasm_mul, hash_stripes_plain, false},
    {"stripes_wasm_x2", hash_stripes_wasm_x2, hash_stripes_plain, false},
#endif
    {"pairs_plain", hash_pairs_plain, NULL, true},
    {"pairs", hash_pairs_vec, hash_pairs_plain, true},
#ifdef __wasm_simd128__
    {"pairs_wasm", hash_pairs_wasm, hash_pairs_plain, true},
#endif
};
#define CANDIDATES (sizeof candidates / sizeof candidates[0])

// Whether a run leaves a candidate out: --only names another, or this
// machine can't run it
static bool skipped(const candidate *c, const char *only)
{
#ifdef HASH_AVX2
    if ((c->hash == hash_stripes_avx2 || c->hash == hash_stripes_avx2_prefetch) && !has_avx2()) return true;
#endif
    return only && strcmp(only, c->name) != 0;
}

// ---------------------------------------------------------------------------
// Bytes to hash: a generator's, the same everywhere

static uint64_t dice_state = 0x243F6A8885A308D3ull;

static uint64_t dice(void)
{
    uint64_t z = (dice_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void fill(uint8_t *p, const size_t size)
{
    for (size_t i = 0; i < size; i += 8u) {
        const uint64_t v = dice();
        memcpy(p + i, &v, size - i < 8u ? size - i : 8u);
    }
}

static void *take(const size_t size)
{
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "out of memory for %zu bytes\n", size);
        exit(2);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Each SIMD way gives its plain definition's bits: at every length to 300
// from several starts, around blocks and pages, of random bytes and zeros.

static int check_same(void)
{
    enum { ROOM = 16384 + 64 };
    uint8_t *p = take(ROOM), *z = calloc(1, ROOM);
    if (!z) return 1;
    fill(p, ROOM);
    int failed = 0;
    static const size_t sizes[] = {960, 1023, 1024, 1025, 1088, 2047, 2048, 4095, 4096, 4097, 8191, 8192, 8193, 16320, 16383, 16384};
    for (size_t c = 0; c < CANDIDATES; c++) {
        if (!candidates[c].same_as || skipped(&candidates[c], NULL)) continue;
        uint32_t wrong = 0;
        for (size_t size = 0; size <= 300u; size++) {
            for (size_t at = 0; at < 16u; at += 5u) {
                wrong += candidates[c].hash(p + at, size) != candidates[c].same_as(p + at, size);
                wrong += candidates[c].hash(z + at, size) != candidates[c].same_as(z + at, size);
            }
        }
        for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            for (size_t at = 0; at < 64u; at += 13u) {
                wrong += candidates[c].hash(p + at, sizes[s]) != candidates[c].same_as(p + at, sizes[s]);
                wrong += candidates[c].hash(z + at, sizes[s]) != candidates[c].same_as(z + at, sizes[s]);
            }
        }
        printf("%-18s %s\n", candidates[c].name, wrong ? "DIFFERS from its plain definition" : "same bits as its plain definition");
        failed |= wrong != 0;
    }
    free(p);
    free(z);
    return failed;
}

// ---------------------------------------------------------------------------
// Timing

static volatile uint64_t sink;

typedef struct shape {
    const char *name;
    size_t page;  // Bytes a page
    size_t pages; // Pages in the world, hashed in a shuffled order each pass
    size_t bytes; // At least this many a pass, going round the world again
} shape;

// A sample is a few milliseconds, so a moment of another program's work on
// the machine spoils a few of many samples rather than all of one
static const shape shapes[] = {
    {"8 KiB page, hot", 8192, 1, 32u << 20},
    {"16 KiB page, hot", 16384, 1, 32u << 20},
    {"512 KiB of 8 KiB pages (L2)", 8192, 64, 32u << 20},
    {"8 MiB of 8 KiB pages (L3)", 8192, 1024, 32u << 20},
    {"8 MiB of 16 KiB pages (L3)", 16384, 512, 32u << 20},
    {"256 MiB of 16 KiB pages (memory)", 16384, 16384, 256u << 20},
    {"64 B, hot", 64, 1, 4u << 20},
    {"1000 B, hot", 1000, 1, 16u << 20},
};
#define SHAPES (sizeof shapes / sizeof shapes[0])

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int run_timing(const uint32_t rounds, const char *only, const bool quick)
{
    size_t most = 0;
    for (size_t s = 0; s < SHAPES; s++) {
        const size_t world = shapes[s].page * shapes[s].pages;
        if (world > most) most = world;
    }
    if (quick) most = 1u << 20;
    uint8_t *world = take(most);
    fill(world, most);
    uint32_t *order = take(sizeof *order * 16384u);
    double *gbs = take(sizeof *gbs * CANDIDATES * SHAPES * rounds);
    for (uint32_t r = 0; r < rounds; r++) {
        for (size_t s = 0; s < SHAPES; s++) {
            const shape *sh = &shapes[s];
            size_t pages = sh->pages, bytes = sh->bytes;
            if (quick) {
                while (pages * sh->page > most) pages /= 2u;
                bytes = 1u << 20;
            }
            for (size_t i = 0; i < pages; i++) order[i] = (uint32_t)i;
            for (size_t i = pages; i > 1u; i--) { // Another order each round, the same for every candidate
                const size_t j = (size_t)(dice() % i);
                const uint32_t t = order[i - 1u];
                order[i - 1u] = order[j];
                order[j] = t;
            }
            const size_t passes = (bytes + pages * sh->page - 1u) / (pages * sh->page);
            for (size_t c = 0; c < CANDIDATES; c++) {
                if (skipped(&candidates[c], only)) continue;
                uint64_t got = 0;
                // Once round the world first, so a world in memory isn't partly in cache from the last
                // candidate's pass alone
                if (sh->pages > 1u) {
                    for (size_t i = 0; i < pages; i++) got ^= candidates[c].hash(world + (size_t)order[i] * sh->page, sh->page);
                }
                const uint64_t began = tide_time_now_ns();
                for (size_t pass = 0; pass < passes; pass++) {
                    for (size_t i = 0; i < pages; i++) got ^= candidates[c].hash(world + (size_t)order[i] * sh->page, sh->page);
                }
                const uint64_t took = tide_time_now_ns() - began;
                sink ^= got;
                gbs[(c * SHAPES + s) * rounds + r] = (double)(passes * pages * sh->page) / (double)(took ? took : 1u);
            }
        }
    }
    // The median of the rounds, and the best: on a machine others use too, the best is the nearest to
    // the hash's own speed
    printf("%-34s", "GB/s, median (best)");
    for (size_t c = 0; c < CANDIDATES; c++) {
        if (!skipped(&candidates[c], only)) printf(" %16s", candidates[c].name);
    }
    printf("\n");
    for (size_t s = 0; s < SHAPES; s++) {
        printf("%-34s", shapes[s].name);
        for (size_t c = 0; c < CANDIDATES; c++) {
            if (skipped(&candidates[c], only)) continue;
            double *at = gbs + (c * SHAPES + s) * rounds;
            qsort(at, rounds, sizeof *at, compare_double);
            printf("    %5.1f (%5.1f)", at[rounds / 2u], at[rounds - 1u]);
        }
        printf("\n");
    }
    // Small pages also as nanoseconds a hash
    for (size_t s = 0; s < SHAPES; s++) {
        if (shapes[s].page > 1000u) continue;
        printf("%-34s", "  ns a hash");
        for (size_t c = 0; c < CANDIDATES; c++) {
            if (skipped(&candidates[c], only)) continue;
            const double *at = gbs + (c * SHAPES + s) * rounds;
            printf("    %5.1f (%5.1f)", (double)shapes[s].page / at[rounds / 2u], (double)shapes[s].page / at[rounds - 1u]);
        }
        printf("   (%s)\n", shapes[s].name);
    }
    free(gbs);
    free(order);
    free(world);
    return 0;
}

// ---------------------------------------------------------------------------
// What each tells apart, roughly: these aren't SMHasher, but each would show
// a hash that's no good for telling worlds apart.

static int compare_u64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

// Equal pairs among n hashes, sorted in place
static size_t equal_pairs(uint64_t *h, const size_t n)
{
    qsort(h, n, sizeof *h, compare_u64);
    size_t same = 0;
    for (size_t i = 1; i < n; i++) same += h[i] == h[i - 1u];
    return same;
}

// Avalanche: flipping an input bit flips each output bit half the time. For
// `keys` random runs of `size` bytes and `bits` input bits spread over them,
// the largest distance of any (input bit, output bit) pair's rate from a half.
static double avalanche(const candidate *c, const size_t size, const uint32_t keys, const uint32_t bits)
{
    uint8_t *p = take(size);
    uint32_t *flips = calloc((size_t)bits * 64u, sizeof *flips);
    if (!flips) exit(2);
    for (uint32_t k = 0; k < keys; k++) {
        fill(p, size);
        const uint64_t h = c->hash(p, size);
        for (uint32_t b = 0; b < bits; b++) {
            const size_t bit = (size_t)((uint64_t)b * (size * 8u) / bits);
            p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            const uint64_t d = h ^ c->hash(p, size);
            p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            for (uint32_t o = 0; o < 64u; o++) flips[b * 64u + o] += (uint32_t)(d >> o & 1u);
        }
    }
    double worst = 0;
    for (size_t i = 0; i < (size_t)bits * 64u; i++) {
        const double bias = (double)flips[i] / keys - 0.5;
        if (bias > worst) worst = bias;
        if (-bias > worst) worst = -bias;
    }
    free(flips);
    free(p);
    return worst;
}

// Every run that differs from a base page in one bit, and every one that
// differs in two bits `apart` bytes apart for each distance in `aparts`
// (the same place in two words, lanes, stripes or blocks), among the first
// `span` bytes: equal pairs among all of them and the base.
static size_t one_and_two_bits(const candidate *c, uint8_t *page, const size_t size, const size_t span,
                               const size_t *aparts, const size_t apart_count, size_t *count)
{
    size_t n = 1u + span * 8u;
    for (size_t a = 0; a < apart_count; a++) n += aparts[a] < span ? (span - aparts[a]) * 8u : 0u;
    uint64_t *h = take(n * sizeof *h);
    size_t at = 0;
    h[at++] = c->hash(page, size);
    for (size_t bit = 0; bit < span * 8u; bit++) {
        page[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        h[at++] = c->hash(page, size);
        page[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
    for (size_t a = 0; a < apart_count; a++) {
        if (aparts[a] >= span) continue;
        for (size_t bit = 0; bit < (span - aparts[a]) * 8u; bit++) {
            const size_t other = bit + aparts[a] * 8u;
            page[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            page[other / 8u] ^= (uint8_t)(1u << (other % 8u));
            h[at++] = c->hash(page, size);
            page[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            page[other / 8u] ^= (uint8_t)(1u << (other % 8u));
        }
    }
    *count = at;
    const size_t same = equal_pairs(h, at);
    free(h);
    return same;
}

// --quick runs each check small, in a test's time even unoptimized: the
// numbers worth reading are a full --quality run's.
static int run_quality(const char *only, const bool quick)
{
    const uint32_t scale = quick ? 400u : 1u;
    int failed = 0;
    for (size_t ci = 0; ci < CANDIDATES; ci++) {
        const candidate *c = &candidates[ci];
        if (skipped(c, only)) continue;
        if (c->same_as) continue; // Its plain definition's bits: checked once, under that name
        printf("%s%s\n", c->name, c->weak ? " (set aside for what these find)" : "");
        // Avalanche at small and page sizes. With k keys a rate's spread is about 0.5 / sqrt(k): the
        // largest of many pairs is several times that
        static const struct { size_t size; uint32_t keys, bits; } av[] = {
            {8, 40000, 64}, {16, 40000, 128}, {64, 20000, 512}, {100, 20000, 800}, {1000, 4000, 512}, {8192, 2000, 512}, {16384, 2000, 512}};
        for (size_t i = 0; i < sizeof av / sizeof av[0]; i++) {
            const uint32_t keys = av[i].keys / scale;
            const double worst = avalanche(c, av[i].size, keys, av[i].bits);
            printf("  avalanche %5zu B: worst bias %.4f (noise about %.4f a pair, %u keys, %u input bits)\n", av[i].size, worst,
                   0.5 / __builtin_sqrt((double)keys), keys, av[i].bits);
        }
        // One and two bits of a page apart, on zeros (worlds are mostly zeros) and random bytes, at
        // distances of a word, the words of a round, a stripe, a block and more
        static const size_t aparts[] = {1, 4, 8, 16, 24, 32, 48, 64, 128, 192, 256, 512, 1024, 2048, 4096, 8192};
        const size_t size = 16384, span = quick ? 64u : 16384u;
        uint8_t *page = calloc(1, size);
        if (!page) exit(2);
        for (int random = 0; random < 2; random++) {
            if (random) fill(page, size);
            size_t count = 0;
            const size_t same = one_and_two_bits(c, page, size, span, aparts, sizeof aparts / sizeof aparts[0], &count);
            printf("  1 and 2 bits of a 16 KiB page of %s: %zu runs, %zu equal pairs\n", random ? "random bytes" : "zeros", count, same);
            failed |= same != 0 && !c->weak;
        }
        // The same place in every pair of stripes, on zeros: the top bit of a word and the bits either
        // side of its middle, which an accumulator's product or sum could let cancel
        {
            memset(page, 0, size);
            const size_t used = quick ? 1024u : 4096u, stripes = used / 64u;
            static const uint32_t places[] = {0, 1, 31, 32, 33, 62, 63};
            const size_t n = 1u + 8u * 7u * (stripes * (stripes - 1u) / 2u);
            uint64_t *h = take(n * sizeof *h);
            size_t at = 0;
            h[at++] = c->hash(page, used);
            for (uint32_t word = 0; word < 8u; word++) {
                for (size_t pl = 0; pl < sizeof places / sizeof places[0]; pl++) {
                    const size_t bit = word * 64u + places[pl];
                    for (size_t s = 0; s < stripes; s++) {
                        for (size_t t = s + 1u; t < stripes; t++) {
                            const size_t x = s * 512u + bit, y = t * 512u + bit;
                            page[x / 8u] ^= (uint8_t)(1u << (x % 8u));
                            page[y / 8u] ^= (uint8_t)(1u << (y % 8u));
                            h[at++] = c->hash(page, used);
                            page[x / 8u] ^= (uint8_t)(1u << (x % 8u));
                            page[y / 8u] ^= (uint8_t)(1u << (y % 8u));
                        }
                    }
                }
            }
            const size_t same = equal_pairs(h, at);
            printf("  the same bit in two stripes of %zu B of zeros: %zu runs, %zu equal pairs\n", used, at, same);
            failed |= same != 0 && !c->weak;
            free(h);
        }
        // Zeros of every length to 16 KiB: only the length tells them apart
        {
            memset(page, 0, size);
            const size_t longest = quick ? 2048u : size;
            uint64_t *h = take((longest + 1u) * sizeof *h);
            for (size_t len = 0; len <= longest; len++) h[len] = c->hash(page, len);
            const size_t same = equal_pairs(h, longest + 1u);
            printf("  zeros of every length to %zu B: %zu equal pairs\n", longest, same);
            failed |= same != 0 && !c->weak;
            free(h);
        }
        // A counter in a page that's otherwise the same: low bits of the hash spread evenly
        {
            fill(page, 8192);
            const uint32_t n = quick ? 1u << 10 : 1u << 20;
            uint64_t *h = take((size_t)n * sizeof *h);
            uint32_t bucket[256] = {0};
            for (uint32_t i = 0; i < n; i++) {
                memcpy(page + 4096, &i, 4);
                h[i] = c->hash(page, 8192);
                bucket[h[i] & 255u]++;
                memcpy(page, &i, 4); // And at the start, the other way round
            }
            double chi = 0;
            const double expect = (double)n / 256.0;
            for (int b = 0; b < 256; b++) chi += ((double)bucket[b] - expect) * ((double)bucket[b] - expect) / expect;
            const size_t same = equal_pairs(h, n);
            printf("  a counter in an 8 KiB page, %u values: %zu equal pairs, chi-squared of the low byte %.1f (255 expected, 330 rare)\n", n,
                   same, chi);
            failed |= same != 0 && !c->weak;
            free(h);
        }
        free(page);
    }
    return failed;
}

int main(int argc, char **argv)
{
    uint32_t rounds = 31;
    const char *only = NULL;
    bool quick = false, quality = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) rounds = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = argv[++i];
        else if (strcmp(argv[i], "--quick") == 0) quick = true;
        else if (strcmp(argv[i], "--quality") == 0) quality = true;
        else {
            fprintf(stderr, "usage: bench_hash [--rounds n] [--only name] [--quick] [--quality]\n");
            return 2;
        }
    }
    if (rounds < 1u) rounds = 1;
    if (check_same()) return 1;
    if (quality) return run_quality(only, quick);
    if (quick) {
        if (run_quality(only, true)) return 1;
        rounds = 1;
    }
    return run_timing(rounds, only, quick);
}
