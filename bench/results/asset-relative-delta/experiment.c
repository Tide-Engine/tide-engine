#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tide/delta.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/time.h"

// Isolated value codec, not a production world/asset registry implementation.
#define LIMIT (16u * 1024u * 1024u)
#define CHUNK 4096u
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed at %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { MODE_FULL, MODE_REFERENCE, MODE_PATCH };
typedef struct { uint8_t *data; uint32_t size; } blob;
typedef struct { blob bytes; uint8_t id[32]; } asset;
static bool timing;
static bool trace_failure;
static uint32_t rejected;

static void register_asset(asset *a, blob bytes)
{
    a->bytes = bytes;
    tide_sha256_of(bytes.data, bytes.size, a->id);
}

static blob envelope(uint8_t mode, const asset *a, blob body)
{
    tide_writer w = {.grows = true};
    tide_write_bytes(&w, "AP01", 4u);
    tide_write_u8(&w, mode);
    if (mode != MODE_FULL) tide_write_bytes(&w, a->id, 32u);
    tide_write_bytes(&w, body.data, body.size);
    CHECK(!w.overflow);
    return (blob){w.data, w.size};
}

static blob encode_value(blob value, const asset *a, bool patch, uint64_t hash)
{
    tide_delta_writer d;
    tide_delta_begin(&d, patch, NULL, 0u, hash);
    tide_delta_number(&d, value.size);
    for (uint32_t at = 0; at < value.size; ) {
        const uint32_t size = value.size - at < CHUNK ? value.size - at : CHUNK;
        const uint32_t base_size = patch && at < a->bytes.size
            ? (a->bytes.size - at < CHUNK ? a->bytes.size - at : CHUNK) : 0u;
        const uint8_t *base = base_size ? a->bytes.data + at : NULL;
        tide_delta_region(&d, value.data + at, size, base, base_size, false, 1u);
        at += size;
    }
    tide_delta_close(&d);
    blob body;
    body.data = tide_delta_end(&d, &body.size);
    const blob out = envelope(patch ? MODE_PATCH : MODE_FULL, a, body);
    free(body.data);
    return out;
}

static blob encode_full(blob value)
{
    return encode_value(value, NULL, false, tide_hash(value.data, value.size));
}

// known_reference is provenance from storage; it is never inferred by scanning
// every list during reads/ticks. Changed owned values are compared only here.
static blob encode_best(blob value, const asset *a, bool known_reference)
{
    if (a && known_reference) return envelope(MODE_REFERENCE, a, (blob){0});
    const uint64_t hash = tide_hash(value.data, value.size);
    blob full = encode_value(value, NULL, false, hash);
    if (!a) return full;
    blob patch = encode_value(value, a, true, hash);
    if (patch.size < full.size) { free(full.data); return patch; }
    free(patch.data);
    return full;
}

// Receiver has a registered exact content version, but no previous match state
// or edit history. Failure leaves its caller's output untouched.
static bool decode(blob wire, const asset *available, blob *out)
{
    tide_reader r = {wire.data, wire.size, 0u, false};
    const uint8_t *magic = tide_read_bytes(&r, 4u);
    if (!magic || memcmp(magic, "AP01", 4u)) return false;
    const uint8_t mode = tide_read_u8(&r);
    if (r.failed || mode > MODE_PATCH) return false;
    const asset *a = NULL;
    if (mode != MODE_FULL) {
        const uint8_t *id = tide_read_bytes(&r, 32u);
        if (!id || !available || memcmp(id, available->id, 32u)) return false;
        a = available;
    }
    blob made = {0};
    if (mode == MODE_REFERENCE) {
        if (r.at != r.size || a->bytes.size > LIMIT) return false;
        made.size = a->bytes.size;
        made.data = malloc(made.size ? made.size : 1u);
        CHECK(made.data);
        if (made.size) memcpy(made.data, a->bytes.data, made.size);
        *out = made;
        return true;
    }
    tide_delta_reader d;
    uint64_t expected;
    if (!tide_delta_open(&d, r.data + r.at, r.size - r.at, mode == MODE_PATCH, &expected)
        || d.xor != (mode == MODE_PATCH)) return false;
    made.size = tide_delta_get_number(&d);
    if (d.bytes.failed || made.size > LIMIT) return false;
    // tide_delta_get's output contract is zeroed storage, including the tail
    // beyond the baseline when the value has grown.
    made.data = calloc(made.size ? made.size : 1u, 1u);
    CHECK(made.data);
    bool ok = true;
    for (uint32_t at = 0; at < made.size && ok; ) {
        const uint32_t size = made.size - at < CHUNK ? made.size - at : CHUNK;
        const uint32_t base_size = a && at < a->bytes.size
            ? (a->bytes.size - at < CHUNK ? a->bytes.size - at : CHUNK) : 0u;
        ok = tide_delta_get(&d, made.data + at, size, base_size ? a->bytes.data + at : NULL, base_size);
        at += size;
    }
    ok = ok && tide_delta_closed(&d) && d.bytes.at == d.bytes.size
        && tide_hash(made.data, made.size) == expected;
    if (!ok) {
        if (trace_failure) fprintf(stderr, "decode mode=%u size=%u consumed=%u/%u failed=%d expected=%llu actual=%llu\n", (unsigned)mode, made.size, d.bytes.at, d.bytes.size, d.bytes.failed, (unsigned long long)expected, (unsigned long long)tide_hash(made.data, made.size));
        free(made.data); return false;
    }
    *out = made;
    return true;
}

static void expect_reject(blob wire, const asset *a)
{
    uint8_t guard[] = {7u, 9u, 13u};
    blob out = {guard, 3u};
    CHECK(!decode(wire, a, &out));
    CHECK(out.data == guard && out.size == 3u && guard[0] == 7u && guard[1] == 9u && guard[2] == 13u);
    rejected++;
}

static void roundtrip(blob wire, const asset *a, blob expected)
{
    blob out = {0};
    trace_failure = true;
    CHECK(decode(wire, a, &out));
    trace_failure = false;
    CHECK(out.size == expected.size && (!out.size || memcmp(out.data, expected.data, out.size) == 0));
    // The receiver owns ordinary mutable bytes. An edit cannot change its asset.
    if (out.size && a && a->bytes.size) {
        const uint8_t before = a->bytes.data[0];
        out.data[0] ^= 0xffu;
        CHECK(a->bytes.data[0] == before);
    }
    free(out.data);
}

static void malformed(blob wire, const asset *a)
{
    // Exact-sized prefixes: exhaustive for small frames; sampled for large
    // ones to keep this finite rather than repeatedly decoding gigabytes.
    const uint32_t cuts = wire.size < 128u ? wire.size : 128u;
    for (uint32_t i = 0; i < cuts; i++) {
        const uint32_t size = wire.size < 128u ? i : (uint32_t)((uint64_t)wire.size * i / cuts);
        uint8_t *copy = malloc(size ? size : 1u);
        CHECK(copy);
        if (size) memcpy(copy, wire.data, size);
        expect_reject((blob){copy, size}, a);
        free(copy);
    }
    uint8_t *copy = malloc((size_t)wire.size + 1u);
    CHECK(copy);
    memcpy(copy, wire.data, wire.size);
    copy[wire.size] = 0u;
    expect_reject((blob){copy, wire.size + 1u}, a);
    copy[4] = 255u;
    expect_reject((blob){copy, wire.size}, a);
    free(copy);
    if (wire.data[4] != MODE_FULL) {
        expect_reject(wire, NULL);
        asset wrong = *a;
        wrong.id[0] ^= 1u;
        expect_reject(wire, &wrong);
    }
    if (wire.data[4] != MODE_REFERENCE) {
        copy = malloc(wire.size); CHECK(copy);
        memcpy(copy, wire.data, wire.size);
        // Both delta forms put their result hash after the one-byte flags.
        copy[(wire.data[4] == MODE_FULL ? 5u : 37u) + 1u] ^= 1u;
        expect_reject((blob){copy,wire.size}, a);
        free(copy);
    }
}

static void print_sha(const uint8_t id[32])
{
    for (uint32_t i = 0; i < 32u; i++) printf("%02x", (unsigned)id[i]);
}

static void report(const char *family, const char *name, blob value, const asset *a, bool known_reference)
{
    uint8_t before[32], after[32], full_sha[32], best_sha[32];
    tide_sha256_of(a->bytes.data, a->bytes.size, before);
    blob full = encode_full(value);
    blob patch = encode_value(value, a, true, tide_hash(value.data, value.size));
    blob best = encode_best(value, a, known_reference);
    CHECK(best.size <= full.size || known_reference);
    // Tiny, highly compressible values may be smaller in full form even than
    // the 37-byte reference. Keep the reference control visible, without
    // claiming it is universally the smallest possible representation.
    roundtrip(full, NULL, value);
    roundtrip(patch, a, value);
    roundtrip(best, a, value);
    malformed(full, a);
    malformed(patch, a);
    malformed(best, a);
    tide_sha256_of(a->bytes.data, a->bytes.size, after);
    CHECK(memcmp(before, after, 32u) == 0);
    tide_sha256_of(full.data, full.size, full_sha);
    tide_sha256_of(best.data, best.size, best_sha);
    double times[4] = {0};
    if (timing) for (uint32_t i = 0; i < 180u; i++) {
        // Alternate both encode and decode orders; correctness is checked
        // outside their timed intervals. Registration/hash lookup is untimed.
        for (uint32_t k = 0; k < 2u; k++) {
            const bool chosen = ((i + k) & 1u) != 0;
            const uint64_t start = tide_time_now_ns();
            blob encoded = chosen ? encode_best(value, a, known_reference) : encode_full(value);
            const uint64_t end = tide_time_now_ns();
            if (i >= 30u) times[chosen ? 1 : 0] += (double)(end - start);
            CHECK(encoded.size == (chosen ? best.size : full.size));
            free(encoded.data);
        }
        for (uint32_t k = 0; k < 2u; k++) {
            const bool chosen = ((i + k) & 1u) != 0;
            blob out = {0};
            const uint64_t start = tide_time_now_ns();
            const bool ok = decode(chosen ? best : full, chosen ? a : NULL, &out);
            const uint64_t end = tide_time_now_ns();
            if (i >= 30u) times[chosen ? 3 : 2] += (double)(end - start);
            CHECK(ok && out.size == value.size && (!out.size || memcmp(out.data, value.data, out.size) == 0));
            free(out.data);
        }
    }
    printf("{\"family\":\"%s\",\"case\":\"%s\",\"asset_bytes\":%u,\"value_bytes\":%u,\"full_bytes\":%u,\"patch_bytes\":%u,\"chosen_bytes\":%u,\"mode\":%u,\"hash\":\"%llu\",\"asset_sha\":\"",
        family, name, a->bytes.size, value.size, full.size, patch.size, best.size, best.data[4], (unsigned long long)tide_hash(value.data, value.size));
    print_sha(a->id);
    printf("\",\"full_sha\":\""); print_sha(full_sha);
    printf("\",\"chosen_sha\":\""); print_sha(best_sha);
    printf("\",\"timing\":%s", timing ? "true" : "false");
    if (timing) printf(",\"full_encode_ms\":%.6f,\"chosen_encode_ms\":%.6f,\"full_decode_ms\":%.6f,\"chosen_decode_ms\":%.6f",
        times[0] / 150000000.0, times[1] / 150000000.0, times[2] / 150000000.0, times[3] / 150000000.0);
    puts("}");
    free(full.data); free(patch.data); free(best.data);
}

static uint32_t random_word(uint32_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}

static void cases(const char *family, asset a)
{
    const uint32_t n = a.bytes.size;
    uint8_t *bytes = malloc((size_t)n + 257u);
    CHECK(bytes && n > 4096u);
    memcpy(bytes, a.bytes.data, n);
    report(family, "reference", (blob){bytes,n}, &a, true);
    report(family, "owned-unchanged", (blob){bytes,n}, &a, false);
    bytes[n / 2u] ^= 0x55u;
    report(family, "one-byte", (blob){bytes,n}, &a, false);
    memcpy(bytes, a.bytes.data, n);
    for (uint32_t i = 0; i < 64u; i++) bytes[(uint32_t)((uint64_t)i * n / 64u)] ^= 0x55u;
    report(family, "sparse-64", (blob){bytes,n}, &a, false);
    memcpy(bytes, a.bytes.data, n);
    for (uint32_t i = n / 2u; i < n / 2u + 4096u && i < n; i++) bytes[i] ^= 0xa5u;
    report(family, "xor-block-4k", (blob){bytes,n}, &a, false);
    memcpy(bytes, a.bytes.data, n);
    uint32_t block_state = 0x876543u;
    for (uint32_t i = n / 2u; i < n / 2u + 4096u && i < n; i++) bytes[i] = (uint8_t)random_word(&block_state);
    report(family, "block-4k", (blob){bytes,n}, &a, false);
    uint32_t state = 0x654321u;
    for (uint32_t i = 0; i < n; i++) bytes[i] = (uint8_t)random_word(&state);
    report(family, "dense", (blob){bytes,n}, &a, false);
    memset(bytes, 0, n);
    report(family, "all-zero", (blob){bytes,n}, &a, false);
    memcpy(bytes, a.bytes.data, n);
    memset(bytes + n, 0x35, 257u);
    report(family, "append-257", (blob){bytes,n + 257u}, &a, false);
    report(family, "truncate-257", (blob){bytes,n - 257u}, &a, false);
    memmove(bytes + n / 2u, bytes + n / 2u + 257u, n - n / 2u - 257u);
    report(family, "delete-middle-257", (blob){bytes,n - 257u}, &a, false);
    report(family, "empty", (blob){bytes,0u}, &a, false);
    free(bytes);
}

static blob read_file(const char *path)
{
    FILE *f = fopen(path, "rb"); CHECK(f);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    const long size = ftell(f); CHECK(size >= 0 && (uint64_t)size <= LIMIT);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    blob bytes = {malloc(size ? (size_t)size : 1u), (uint32_t)size}; CHECK(bytes.data);
    CHECK(fread(bytes.data, 1u, bytes.size, f) == bytes.size && fclose(f) == 0);
    return bytes;
}

int main(int argc, char **argv)
{
    const char *pack_path = NULL, *asset_name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--timing")) timing = true;
        else if (!strcmp(argv[i], "--pack") && i + 2 < argc) {
            pack_path = argv[++i]; asset_name = argv[++i];
        } else return 2;
    }
    blob bytes = {malloc(1048576u), 1048576u}; CHECK(bytes.data);
    uint32_t state = 0x123456u;
    for (uint32_t i = 0; i < bytes.size; i++) bytes.data[i] = (uint8_t)random_word(&state);
    asset a; register_asset(&a, bytes);
    cases("noise-1m", a);
    for (uint32_t i = 0; i < bytes.size; i++) bytes.data[i] = (uint8_t)(i * 17u);
    register_asset(&a, bytes);
    cases("pattern-1m", a);
    asset tiny; register_asset(&tiny, (blob){bytes.data,16u});
    report("tiny-16", "reference", tiny.bytes, &tiny, true);
    report("tiny-16", "owned-unchanged", tiny.bytes, &tiny, false);
    // Ordinary owned data without recorded provenance takes exactly the
    // original full encoding; there is no search through known assets.
    blob unknown = encode_best(bytes, NULL, false), ordinary = encode_full(bytes);
    CHECK(unknown.size == ordinary.size && !memcmp(unknown.data, ordinary.data, ordinary.size));
    roundtrip(unknown, NULL, bytes); free(unknown.data); free(ordinary.data);
    free(bytes.data);
    if (pack_path) {
        blob pack = read_file(pack_path);
        CHECK(tide_pack_load(pack.data, pack.size));
        const tide_pack_entry *e = tide_pack_find(asset_name, TIDE_PACK_BYTES);
        tide_list value = {0};
        if (e) value.at = tide_pack_handle(e);
        else {
            e = tide_pack_find(asset_name, TIDE_PACK_LIST);
            CHECK(e && e->extra == TIDE_PACK_BYTES);
            const tide_list outer = {tide_pack_handle(e)};
            CHECK(tide_list_count(outer) > 0);
            memcpy(&value, tide_list_at(outer, 0, sizeof value), sizeof value);
        }
        const int32_t count = tide_list_count(value);
        CHECK(count > 4096 && (uint32_t)count < LIMIT - 257u && tide_pack_holds(value.at));
        bytes = (blob){malloc((size_t)count), (uint32_t)count}; CHECK(bytes.data);
        for (int32_t i = 0; i < count; i++) bytes.data[i] = *(const uint8_t *)tide_list_at(value, i, 1u);
        register_asset(&a, bytes);
        cases("private-animation", a);
        free(bytes.data); free(pack.data); tide_pack_unload();
    }
    // A hostile logical size must fail before allocating the claimed value.
    tide_delta_writer d; tide_delta_begin(&d, false, NULL, 0u, 0u);
    tide_delta_number(&d, UINT32_MAX); tide_delta_close(&d);
    blob body; body.data = tide_delta_end(&d, &body.size);
    blob bomb = envelope(MODE_FULL, NULL, body);
    expect_reject(bomb, NULL); free(body.data); free(bomb.data);
    printf("{\"checks\":\"passed\",\"rejected\":%u,\"unknown_origin\":\"full\",\"timing\":%s}\n", rejected, timing ? "true" : "false");
    return 0;
}
