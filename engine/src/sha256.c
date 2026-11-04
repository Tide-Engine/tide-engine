// getauxval, which says whether an Arm CPU has the SHA-2 instructions
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#include "tide/sha256.h"

#include <string.h>

// From FIPS 180-4. Where the CPU has instructions for it (x86's SHA extensions,
// Armv8's SHA-2), blocks go through them: the same digest, many times faster,
// which a pack of gigabytes needs. The portable code is everywhere else.

#if (defined(__x86_64__) || defined(__i386__)) && defined(__clang__)
#define SHA_X86 1
#include <cpuid.h>
#include <immintrin.h>
#elif defined(__aarch64__) && defined(__clang__) && (defined(__APPLE__) || defined(__linux__) || defined(__ARM_FEATURE_SHA2))
#define SHA_ARM 1
#include <arm_neon.h>
#if defined(__linux__) && !defined(__ARM_FEATURE_SHA2)
#include <sys/auxv.h>
#ifndef HWCAP_SHA2
#define HWCAP_SHA2 (1u << 6)
#endif
#endif
#endif

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(const uint32_t x, const int n)
{
    return x >> n | x << (32 - n);
}

static uint32_t get32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void put32be(uint8_t *p, const uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void portable_blocks(uint32_t h[8], const uint8_t *p, size_t blocks)
{
    for (; blocks; blocks--, p += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = get32be(p + 4 * i);
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += k;
    }
}

#ifdef SHA_X86
// Intel's SHA extensions keep the state as ABEF and CDGH, and take two rounds
// an instruction.
__attribute__((target("sha,sse4.1,ssse3"))) static void hardware_blocks(uint32_t h[8], const uint8_t *p, size_t blocks)
{
    const __m128i swap = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL); // Each word big-endian
    __m128i cdab = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)(const void *)&h[0]), 0xB1);
    __m128i efgh = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)(const void *)&h[4]), 0x1B);
    __m128i abef = _mm_alignr_epi8(cdab, efgh, 8);
    __m128i cdgh = _mm_blend_epi16(efgh, cdab, 0xF0);
    for (; blocks; blocks--, p += 64) {
        const __m128i abef_was = abef, cdgh_was = cdgh;
        __m128i m[4]; // Message words 4i to 4i + 3 in m[i % 4]
        for (int i = 0; i < 4; i++) m[i] = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 16 * i)), swap);
        for (int i = 0; i < 16; i++) {
            __m128i k = _mm_add_epi32(m[i & 3], _mm_loadu_si128((const __m128i *)(const void *)&K256[4 * i]));
            cdgh = _mm_sha256rnds2_epu32(cdgh, abef, k);
            k = _mm_shuffle_epi32(k, 0x0E);
            abef = _mm_sha256rnds2_epu32(abef, cdgh, k);
            if (i < 12) { // Words 4i + 16 on: W[t-16] + s0(W[t-15]) + W[t-7] + s1(W[t-2])
                __m128i t = _mm_sha256msg1_epu32(m[i & 3], m[(i + 1) & 3]);
                t = _mm_add_epi32(t, _mm_alignr_epi8(m[(i + 3) & 3], m[(i + 2) & 3], 4));
                m[i & 3] = _mm_sha256msg2_epu32(t, m[(i + 3) & 3]);
            }
        }
        abef = _mm_add_epi32(abef, abef_was);
        cdgh = _mm_add_epi32(cdgh, cdgh_was);
    }
    const __m128i feba = _mm_shuffle_epi32(abef, 0x1B);
    const __m128i dchg = _mm_shuffle_epi32(cdgh, 0xB1);
    _mm_storeu_si128((__m128i *)(void *)&h[0], _mm_blend_epi16(feba, dchg, 0xF0)); // DCBA: a in the lowest word
    _mm_storeu_si128((__m128i *)(void *)&h[4], _mm_alignr_epi8(dchg, feba, 8));    // HGFE
}

static bool hardware_found(void)
{
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
    const bool ssse3 = (c >> 9) & 1u, sse41 = (c >> 19) & 1u;
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
    return ssse3 && sse41 && ((b >> 29) & 1u);
}
#endif

#ifdef SHA_ARM
__attribute__((target("sha2"))) static void hardware_blocks(uint32_t h[8], const uint8_t *p, size_t blocks)
{
    uint32x4_t abcd = vld1q_u32(&h[0]), efgh = vld1q_u32(&h[4]);
    for (; blocks; blocks--, p += 64) {
        const uint32x4_t abcd_was = abcd, efgh_was = efgh;
        uint32x4_t m[4];
        for (int i = 0; i < 4; i++) m[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16 * i)));
        for (int i = 0; i < 16; i++) {
            const uint32x4_t k = vaddq_u32(m[i & 3], vld1q_u32(&K256[4 * i]));
            const uint32x4_t was = abcd;
            if (i < 12) m[i & 3] = vsha256su0q_u32(m[i & 3], m[(i + 1) & 3]);
            abcd = vsha256hq_u32(abcd, efgh, k);
            efgh = vsha256h2q_u32(efgh, was, k);
            if (i < 12) m[i & 3] = vsha256su1q_u32(m[i & 3], m[(i + 2) & 3], m[(i + 3) & 3]);
        }
        abcd = vaddq_u32(abcd, abcd_was);
        efgh = vaddq_u32(efgh, efgh_was);
    }
    vst1q_u32(&h[0], abcd);
    vst1q_u32(&h[4], efgh);
}

static bool hardware_found(void)
{
#if defined(__ARM_FEATURE_SHA2) || defined(__APPLE__)
    return true; // Every Apple Arm CPU has them
#else
    return (getauxval(AT_HWCAP) & HWCAP_SHA2) != 0;
#endif
}
#endif

// 0 not asked yet, 1 the portable code, 2 the CPU's instructions. Asked once;
// threads that ask at once find the same.
static uint32_t chosen;

static uint32_t choice(void)
{
#if defined(SHA_X86) || defined(SHA_ARM)
    uint32_t use = __atomic_load_n(&chosen, __ATOMIC_RELAXED);
    if (!use) {
        use = hardware_found() ? 2u : 1u;
        __atomic_store_n(&chosen, use, __ATOMIC_RELAXED);
    }
    return use;
#else
    return 1u;
#endif
}

static void blocks_of(uint32_t h[8], const uint8_t *p, const size_t blocks)
{
    if (!blocks) return;
#if defined(SHA_X86) || defined(SHA_ARM)
    if (choice() == 2u) {
        hardware_blocks(h, p, blocks);
        return;
    }
#endif
    portable_blocks(h, p, blocks);
}

bool tide_sha256_hardware(void)
{
    return choice() == 2u;
}

void tide_sha256_portable(const bool on)
{
    __atomic_store_n(&chosen, 0u, __ATOMIC_RELAXED);
    if (on) __atomic_store_n(&chosen, 1u, __ATOMIC_RELAXED);
}

void tide_sha256_init(tide_sha256 *s)
{
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, start, sizeof start);
    s->bytes = 0;
}

void tide_sha256_add(tide_sha256 *s, const void *data, size_t size)
{
    const uint8_t *p = data;
    size_t used = (size_t)(s->bytes % 64u);
    s->bytes += size;
    if (used) { // A block begun before
        const size_t n = size < 64 - used ? size : 64 - used;
        memcpy(s->block + used, p, n);
        p += n;
        size -= n;
        used += n;
        if (used < 64) return;
        blocks_of(s->h, s->block, 1);
    }
    // Whole blocks straight from the caller's bytes, then what's left for next time
    blocks_of(s->h, p, size / 64u);
    p += size / 64u * 64u;
    memcpy(s->block, p, size % 64u);
}

void tide_sha256_end(tide_sha256 *s, uint8_t out[32])
{
    const uint64_t bits = s->bytes * 8u;
    static const uint8_t pad[64] = {0x80};
    const size_t used = (size_t)(s->bytes % 64u);
    tide_sha256_add(s, pad, used < 56 ? 56 - used : 120 - used);
    uint8_t length[8];
    for (int i = 0; i < 8; i++) length[i] = (uint8_t)(bits >> (56 - 8 * i));
    tide_sha256_add(s, length, 8);
    for (int i = 0; i < 8; i++) put32be(out + 4 * i, s->h[i]);
}

void tide_sha256_of(const void *data, const size_t size, uint8_t out[32])
{
    tide_sha256 s;
    tide_sha256_init(&s);
    tide_sha256_add(&s, data, size);
    tide_sha256_end(&s, out);
}
