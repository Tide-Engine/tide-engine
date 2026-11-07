// SHA-256 (tide/sha256.h): FIPS 180-4's examples, and the CPU's instructions
// giving the portable code's digests for every length and way of feeding them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/sha256.h"
#include "tide_test.h"

static void hex_of(const uint8_t digest[32], char out[65])
{
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static bool digest_is(const void *data, const size_t size, const char *expected)
{
    uint8_t digest[32];
    char hex[65];
    tide_sha256_of(data, size, digest);
    hex_of(digest, hex);
    return strcmp(hex, expected) == 0;
}

TIDE_TEST(sha256_gives_the_standards_digests_both_ways)
{
    for (int portable = 0; portable < 2; portable++) {
        tide_sha256_portable(portable != 0);
        TIDE_CHECK(digest_is("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        TIDE_CHECK(digest_is("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        TIDE_CHECK(digest_is(two, strlen(two), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
        uint8_t *million = malloc(1000000);
        TIDE_REQUIRE(million);
        memset(million, 'a', 1000000);
        TIDE_CHECK(digest_is(million, 1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
        free(million);
    }
    tide_sha256_portable(false);
}

TIDE_TEST(sha256_hardware_matches_the_portable_code_at_every_length_and_split)
{
    enum { MOST = 4100 };
    uint8_t *bytes = malloc(MOST);
    TIDE_REQUIRE(bytes);
    uint32_t x = 0x9E3779B9u;
    for (int i = 0; i < MOST; i++) {
        x = x * 1664525u + 1013904223u;
        bytes[i] = (uint8_t)(x >> 24);
    }
    printf("sha256: the CPU's instructions are %s\n", tide_sha256_hardware() ? "used" : "absent: both are the portable code");
    for (size_t size = 0; size < MOST; size += size < 260 ? 1u : 61u) {
        uint8_t portable[32], hardware[32], pieces[32];
        tide_sha256_portable(true);
        tide_sha256_of(bytes, size, portable);
        tide_sha256_portable(false);
        tide_sha256_of(bytes, size, hardware);
        tide_sha256 s; // In pieces of 1 to 97 bytes, which begin and end mid-block
        tide_sha256_init(&s);
        for (size_t at = 0; at < size;) {
            size_t n = (at * 7u + 3u) % 97u + 1u;
            if (n > size - at) n = size - at;
            tide_sha256_add(&s, bytes + at, n);
            at += n;
        }
        tide_sha256_end(&s, pieces);
        TIDE_CHECK(memcmp(portable, hardware, 32) == 0);
        TIDE_CHECK(memcmp(portable, pieces, 32) == 0);
    }
    free(bytes);
}
