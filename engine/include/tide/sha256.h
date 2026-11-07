#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// SHA-256 (FIPS 180-4), for what needs a hash nobody can work back from:
// the digests of players' cookies that sessions share for host migration
// (tide/session.h), WebRTC's crypto (platform/src/rtc), and packs' names and
// contents. Through the CPU's SHA instructions where it has them (x86's SHA
// extensions, Armv8's SHA-2): the same digests, several times faster.

typedef struct tide_sha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
} tide_sha256;

void tide_sha256_init(tide_sha256 *s);
void tide_sha256_add(tide_sha256 *s, const void *data, size_t size);
void tide_sha256_end(tide_sha256 *s, uint8_t out[32]);
void tide_sha256_of(const void *data, size_t size, uint8_t out[32]);

// Whether blocks go through the CPU's instructions on this machine.
bool tide_sha256_hardware(void);
// For tests: true makes every hash after it use the portable code, false
// goes back to the CPU's instructions where there are some.
void tide_sha256_portable(bool on);
