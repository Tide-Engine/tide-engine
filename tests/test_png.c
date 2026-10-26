// The PNG reader (tide/png.h), as the pack's builder and grid.Decode read with
// it: against the platform layer's writer, and against PNGs made here by hand,
// of every color type, bit depth and filter, interlaced too, with every
// corruption a reader has to refuse.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h" // The writer, platform/src/png.h
#include "tide/png.h"
#include "tide_test.h"

// ---------------------------------------------------------------------------
// A PNG made by hand: its rows as the format wants them (a filter byte then
// the samples), in stored deflate blocks, so the reader's chunk, filter and
// pixel code is what's tested.

typedef struct bytes {
    uint8_t *data;
    size_t size, room;
} bytes;

static void put(bytes *b, const void *p, const size_t n)
{
    if (b->size + n > b->room) {
        size_t room = b->room ? b->room : 1024u;
        while (room < b->size + n) room *= 2u;
        b->data = realloc(b->data, room);
        b->room = room;
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

static void put32(bytes *b, const uint32_t v)
{
    put(b, (const uint8_t[4]){(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}, 4);
}

static uint32_t crc32(const uint8_t *data, const size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = crc & 1u ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

static void chunk(bytes *b, const char *type, const uint8_t *data, const size_t size)
{
    put32(b, (uint32_t)size);
    const size_t from = b->size;
    put(b, type, 4);
    if (size) put(b, data, size);
    put32(b, crc32(b->data + from, 4u + size));
}

static bytes zlib_stored(const uint8_t *raw, const size_t size)
{
    bytes z = {0};
    put(&z, (const uint8_t[2]){0x78, 0x01}, 2);
    size_t at = 0;
    do {
        const size_t left = size - at;
        const uint32_t n = left < 65535u ? (uint32_t)left : 65535u;
        const uint8_t head = at + n == size;
        put(&z, &head, 1);
        put(&z, (const uint8_t[4]){(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)~n, (uint8_t)(~n >> 8)}, 4);
        put(&z, raw + at, n);
        at += n;
    } while (at < size);
    put32(&z, adler32(raw, size));
    return z;
}

typedef struct made {
    uint32_t width, height;
    uint8_t depth, color, interlaced;
    const uint8_t *palette; // 3 bytes an entry
    uint32_t palette_count;
    const uint8_t *trns;
    uint32_t trns_size;
} made;

static bytes make_png(const made *m, const uint8_t *raw, const size_t raw_size, const size_t idat)
{
    bytes png = {0};
    put(&png, (const uint8_t[8]){0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}, 8);
    bytes h = {0};
    put32(&h, m->width);
    put32(&h, m->height);
    put(&h, (const uint8_t[5]){m->depth, m->color, 0, 0, m->interlaced}, 5);
    chunk(&png, "IHDR", h.data, h.size);
    free(h.data);
    chunk(&png, "gAMA", (const uint8_t[4]){0, 0, 0xB1, 0x8F}, 4); // An ancillary chunk, skipped
    if (m->palette) chunk(&png, "PLTE", m->palette, (size_t)m->palette_count * 3u);
    if (m->trns) chunk(&png, "tRNS", m->trns, m->trns_size);
    bytes z = zlib_stored(raw, raw_size);
    for (size_t at = 0; at < z.size;) {
        const size_t n = z.size - at < idat ? z.size - at : idat;
        chunk(&png, "IDAT", z.data + at, n);
        at += n;
    }
    free(z.data);
    chunk(&png, "IEND", NULL, 0);
    return png;
}

static bool near(const float a, const float b)
{
    return a - b < 1e-6f && b - a < 1e-6f;
}

static bool same_color(const tide_color a, const tide_color b)
{
    return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.a, b.a);
}

// ---------------------------------------------------------------------------

TIDE_TEST(png_reads_what_the_writer_wrote)
{
    enum { W = 37, H = 23 };
    static uint8_t rgba[W * H * 4];
    uint32_t x = 7;
    for (int i = 0; i < W * H * 4; i++) {
        x = x * 1103515245u + 12345u;
        rgba[i] = (uint8_t)(x >> 24);
        if (i % 4 == 3) rgba[i] = 255;
    }
    memcpy(rgba + 5 * W * 4, rgba + 2 * W * 4, W * 4);
    memset(rgba + 10 * W * 4, 0x42, W * 4 * 3);
    size_t size;
    uint8_t *png = tide_png_encode(rgba, W, H, &size);
    TIDE_REQUIRE(png != NULL);
    tide_png_image image;
    TIDE_REQUIRE(tide_png_decode(png, size, &image, NULL));
    TIDE_CHECK(image.width == (uint32_t)W && image.height == (uint32_t)H);
    for (int i = 0; i < W * H; i++) {
        const tide_color want = {(float)rgba[4 * i] / 255.0f, (float)rgba[4 * i + 1] / 255.0f, (float)rgba[4 * i + 2] / 255.0f, 1.0f};
        if (!same_color(image.pixels[i], want)) {
            TIDE_CHECK(same_color(image.pixels[i], want));
            break;
        }
    }
    free(image.pixels);
    free(png);
}

// A dynamic-Huffman stream, which the writer never makes: zlib's (Node's) of
// 3000 bytes made by the generator below, at level 9.
static const char dynamic_hex[] =
    "2d560b909665196540c0191895bc20a14044248d403916a01b0408bab3b080826208cbb978036f01a1c444208528ca2548142460bcc0a8b84a121390b8ca1881895c04d259726445ae0d1630cbb2643ccfc7ecb7f3effedffbbddf73ce73ce795e80625c9516c4b82cc0af8a7ed6a6da8aabe511ae3f4c52d6f8629d41491a17dfdd005f2c830dbdc0f1ff9d4de005b6644f9037c20d2ce32fa6fdeddd1f6af13cd1b6ef320078b069089e115fc2934e7a97096ae5473be12df2fd8648b242f43d51a1e5e92fcaebbc0ca20f598e62464176d6f6a5c85a9fc7e2ca4e6a289035b10d48d4f196a310ab05d7b5516e6ecf428b7f12fa92a45bbabf4ba362c124f461d020bb9b61c4f5db1ac4cddfc82fd844f0b0a9a3b14ca2b0c888cf9a28b484b6f297e82b7a52d4aee008516d1b37106e94ac68811f72bcd4aa282da39b052beee2d85c101eb076ce6412d726e0e636689424dbf4d040de4f1bbb70a9fe1078e3b5c7e28fefb4e6f4de51e470528769ff8ad67f199b1ca0baf1280072bfac4065badee801e8c16046f04937b0193df24f9c1cffbb1d4d4bb7472fa421f085e67de21a76fd1c2871a90eccbaf7d1a8118254b067ee270402beddf8535b1ba30237e59b450c72340bcbb1f58b25b2e6c93a1a6fc1eb24b08fed442b74dadb7b455c0f3f8e553f3f6baf3d156d6627fb74d0c73952eb4d410e15ef254f94c13efd89556bc4dbfc06385957191a198db94ee8ec7f984ff4b2dcb8f149d0e651f2d8453af5300b4dc177ae15022beccb201ee789e6506994e60b5ee1b5b4a1b9012088a619c2ee2fff54bdccd7a5a7d1986dac5ba89e15970c826e721346896f867c78f0fdd077784a0ded21d02a947e57d0279f5a03389c8aed6267bf5b6eae827aee9dc220c72a8f464bf83a5facced6d432f9a4c0dfdf165ae3cca8de65584eb85e7eb2f6e054a3b74a4ccf56089b94fa07b205739cde129a5266277c4f535c68b386eebc2eee5d3352b756a3b0855d197c5baa92dde4c4185903ddcdb3604fd4567b0cbf4a071ec62ba936b7b4fa8618467fcc111c0b7f66713b435361d6d45ef2213d079fa23569015d1b9a6790aa7d63430103c25212ae0849164f08b7ad08ee43a6f6da414ac781dd494e3c24d49a5c226f4a7ffd4df3d366f4cb2a6b7ef7b9903a1196cdf0512abdc2e9c700f716ef8dc60c02dcea8a10e079fb8adcc8ee110651ff3e3f4ca3e568b869c8a57113a1d9186a5aa465a6c876d0db203709c0843bd08b220422904c57b91f8dd3482b070d424907a09cf2fc91c4957f5d968603750afe359038c04411f62ae00853b895d66e6676058fe18cb1b12053c474ff738cd5dcfd8c07cf87bf3057a39555c309e3427c80b7b59e2cfe822363a95e83b534828aa0dbe30721d17a8bcd1e14da13fc65c49dd97d354b2e7ed745b83e1b3dd0bf82cbcfbe1a94ed0955d546243219edff88b026456b7b07e9e6309f8aa9637d9ea5bec499ce28fc8f8b86aa43f5f9f5d67c7bc923b1fa8feee3012c3a96f7e6fe7d8785dda1b43003305f660b597ba5aa04c419494a7855a84cb796df145a515b38157866e08b1971211e4acfa4c7aa5806797223ee7166b5ccdda1b5c47401d155d72d624baf63028cd869975a0a8fe0bc60c285c458e310a0ba7856d6bef6eea281b40643d9d485e67d4537415fc202feb442d7913fb8662be8895ba2e978de9db2d6234ac42c317b8459bd6737bf717a2a4d95bc47b6fbf16cd8ec00657e90824a1cbc34ca71bb1cd99c25eb656c10351ce7a4bf8d9bb5276340abc495c4d2703dddb1ba6f01031f39d4d991cbb558e692646857dfc7688cb1d608bdc691079869e48ac8818033aa8af4919df18629074d5d1d64f5c9f04578c8e4e58f11c40778358f1146ab4cc96c41d714ca30c09716d305619c5d818fd321d7db673c22d7de1c987e28ed29b4d2f1a8e54f5d5043eabdb7f705ead993747591441e1fe7843ea2af64a69bb0c02afa73d68beb50f0faa468f45321d015398243e1478ea42c366b7c24088b734e3ca019c59cf70ef405bd3a1f58194308c2712f93b1d8db8b0430dce6a1f87c431fa7c101f51c98bac5e2716ffac7e73cb5dff086705ce6df3d12572847b80bdb3be790947cbfe501bc9f4562fc4e3d4b0bcb68a6465f9b827e29e77043d2d356b82d73343dc03c0299fe3acce35b733ee50ff18df6ab3ab68815d18b770ee589702dcbe9e7438f77840a26009e659df05ae64ef819e7b25ae74d8b3f878ee20c958624a75e6fcc01eb8a48c37a7358c6878a93a9cdba58e750f1ff324cb04176857c397bc83ef6ad800fae4fd2aa79873209733b45745c161c10ba116cf4363332b18bdf27757c6171da3b18ce1fa22da49ae2b5e0ef2915e788a7adb3f811bbc09531513d1bd6f4eea0ef92de99579ca1e3c60b7b42c3c328bf9f154278b4e84a361615e6454a41c1ef41de1c3344f60a7a68520ee106e289147b5a55436df436d5f5aaf5678c160b4357ff07";

typedef struct sink {
    uint8_t *data;
    size_t size;
} sink;

static bool take_inflated(void *user, const uint8_t *bytes, const size_t count)
{
    sink *s = user;
    s->data = realloc(s->data, s->size + count);
    memcpy(s->data + s->size, bytes, count);
    s->size += count;
    return true;
}

TIDE_TEST(png_inflates_dynamic_codes)
{
    const size_t n = strlen(dynamic_hex) / 2u;
    uint8_t *deflated = malloc(n);
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(dynamic_hex + 2 * i, "%2x", &v);
        deflated[i] = (uint8_t)v;
    }
    uint8_t want[3000];
    uint32_t x = 12345;
    for (int i = 0; i < 3000; i++) {
        x = x * 1103515245u + 12345u;
        const uint32_t r = (x >> 16) & 0xFFFFu;
        want[i] = r % 7 == 0 ? 97 : r % 5 == 0 ? 98 : r % 3 == 0 ? 99 : r % 2 == 0 ? 100 : (uint8_t)(r >> 8);
    }
    sink out = {0};
    size_t consumed;
    TIDE_REQUIRE(tide_inflate(deflated, n, take_inflated, &out, &consumed));
    TIDE_CHECK(out.size == 3000 && memcmp(out.data, want, 3000) == 0 && consumed == n);
    sink cut = {0};
    TIDE_CHECK(!tide_inflate(deflated, n / 2, take_inflated, &cut, &consumed));
    free(out.data);
    free(cut.data);
    free(deflated);
}

// A stored block after a block of codes whose end came early in the bytes
// the bit buffer had taken: zlib's flush (Z_SYNC_FLUSH) after 22 'H's, which
// is a block of codes, an empty stored block and a last empty fixed block. The
// stored block's lengths are in a byte the code's lookup had already taken.
TIDE_TEST(png_inflates_a_stored_block_after_codes)
{
    static const uint8_t stream[22] = {0x04, 0xC1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x3D, 0xFF,
                                       0x45, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00};
    sink out = {0};
    size_t consumed = 0;
    TIDE_CHECK(tide_inflate(stream, sizeof stream, take_inflated, &out, &consumed));
    bool all_h = out.size == 22;
    for (size_t i = 0; i < out.size; i++) all_h = all_h && out.data[i] == 'H';
    TIDE_CHECK(all_h && consumed == sizeof stream);
    free(out.data);

    // A picture written that way: 5 by 3 of 8-bit gray, its rows flushed
    static const uint8_t z[31] = {0x78, 0x01, 0x04, 0xC1, 0x31, 0x01, 0x00, 0x00, 0x00, 0xC2, 0xA0, 0x7D, 0xF4, 0x6F, 0x65, 0x2C,
                                  0x21, 0x46, 0xD8, 0x02, 0x07, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x1A, 0x61, 0x02, 0xAD};
    static const uint8_t gray[15] = {7, 7, 200, 7, 7, 7, 7, 7, 200, 200, 7, 7, 7, 7, 7};
    bytes png = {0};
    put(&png, (const uint8_t[8]){0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}, 8);
    chunk(&png, "IHDR", (const uint8_t[13]){0, 0, 0, 5, 0, 0, 0, 3, 8, 0, 0, 0, 0}, 13);
    chunk(&png, "IDAT", z, sizeof z);
    chunk(&png, "IEND", NULL, 0);
    tide_png_image image;
    TIDE_REQUIRE(tide_png_decode(png.data, png.size, &image, NULL));
    bool same = image.width == 5 && image.height == 3;
    for (int i = 0; i < 15 && same; i++) {
        const float g = (float)gray[i] / 255.0f;
        same = same_color(image.pixels[i], (tide_color){g, g, g, 1.0f});
    }
    TIDE_CHECK(same);
    free(image.pixels);
    free(png.data);
}

// Bits into bytes, lowest first, as deflate has them.
typedef struct bits {
    uint8_t data[64];
    uint32_t count;
} bits;

static void put_bits(bits *b, const uint32_t v, const uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, b->count++) b->data[b->count / 8u] |= (uint8_t)(((v >> i) & 1u) << (b->count % 8u));
}

TIDE_TEST(png_refuses_hostile_streams)
{
    sink out = {0};
    size_t consumed;
    // A stored block whose length's complement is wrong, and one longer than the input
    TIDE_CHECK(!tide_inflate((const uint8_t[6]){0x01, 0x01, 0x00, 0x00, 0x00, 'x'}, 6, take_inflated, &out, &consumed));
    TIDE_CHECK(!tide_inflate((const uint8_t[6]){0x01, 0x05, 0x00, 0xFA, 0xFF, 'x'}, 6, take_inflated, &out, &consumed));
    // A fixed block's first symbol a match, reaching before the stream's start
    TIDE_CHECK(!tide_inflate((const uint8_t[3]){0x03, 0x02, 0x00}, 3, take_inflated, &out, &consumed));
    // Code lengths' codes that are over-subscribed: 19 codes of 1 bit
    bits over = {0};
    put_bits(&over, 1, 1);
    put_bits(&over, 2, 2);
    put_bits(&over, 0, 10);
    put_bits(&over, 15, 4);
    for (int i = 0; i < 19; i++) put_bits(&over, 1, 3);
    TIDE_CHECK(!tide_inflate(over.data, (over.count + 7u) / 8u + 8u, take_inflated, &out, &consumed));
    // And incomplete: one code of 2 bits
    bits under = {0};
    put_bits(&under, 1, 1);
    put_bits(&under, 2, 2);
    put_bits(&under, 0, 10);
    put_bits(&under, 0, 4);
    put_bits(&under, 2, 3);
    TIDE_CHECK(!tide_inflate(under.data, (under.count + 7u) / 8u + 8u, take_inflated, &out, &consumed));
    TIDE_CHECK(out.size == 0);
    free(out.data);

    // A palette's index past its entries, and a tRNS longer than the palette
    static const uint8_t palette[2 * 3] = {255, 0, 0, 0, 255, 0};
    const uint8_t raw[2 * (3 + 1)] = {0, 0, 1, 0, 0, 1, 5, 1};
    const made past = {3, 2, 8, 3, 0, palette, 2, NULL, 0};
    bytes p = make_png(&past, raw, sizeof raw, 1000);
    tide_png_image image;
    TIDE_CHECK(!tide_png_decode(p.data, p.size, &image, NULL));
    free(p.data);
    const uint8_t fine[2 * (3 + 1)] = {0, 0, 1, 0, 0, 1, 1, 1};
    const made longer = {3, 2, 8, 3, 0, palette, 2, (const uint8_t[3]){0, 128, 255}, 3};
    p = make_png(&longer, fine, sizeof fine, 1000);
    TIDE_CHECK(!tide_png_decode(p.data, p.size, &image, NULL));
    free(p.data);
}

TIDE_TEST(png_reads_every_color_type_and_depth)
{
    static const uint8_t palette[16 * 3] = {255, 0,   0,   0,   255, 0,   0,  0,  255, 10,  20, 30, 40, 50, 60, 70,
                                             80,  90,  100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230,
                                             240, 250, 1,   2,   3,   4,   5,   6,   7,   8,   9,   11, 12, 13, 14, 15};
    static const uint8_t trns_palette[3] = {0, 128, 255};
    const struct {
        uint8_t color, depth;
    } cases[] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1}, {3, 2}, {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};
    enum { W = 5, H = 3 };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const uint8_t color = cases[k].color, depth = cases[k].depth;
        const uint32_t channels = color == 0 || color == 3 ? 1u : color == 2 ? 3u : color == 4 ? 2u : 4u;
        const uint32_t most = (1u << depth) - 1u;
        const uint32_t entries = depth < 4 ? 1u << depth : 16u; // A palette has no more entries than its depth can name
        const size_t row = ((size_t)W * depth * channels + 7u) / 8u;
        uint8_t raw[(row + 1) * H];
        memset(raw, 0, sizeof raw);
        tide_color want[W * H];
        for (int y = 0; y < H; y++) {
            uint8_t *r = raw + (size_t)y * (row + 1u) + 1u;
            for (int x = 0; x < W; x++) {
                uint32_t s[4];
                for (uint32_t c = 0; c < channels; c++) s[c] = ((uint32_t)(x * 7 + y * 3 + c * 5) * 2654435761u >> 7) & most;
                if (color == 3) s[0] %= entries;
                for (uint32_t c = 0; c < channels; c++) {
                    const size_t i = (size_t)x * channels + c;
                    if (depth == 16) {
                        r[2 * i] = (uint8_t)(s[c] >> 8);
                        r[2 * i + 1] = (uint8_t)s[c];
                    } else if (depth == 8) {
                        r[i] = (uint8_t)s[c];
                    } else {
                        const uint32_t per = 8u / depth;
                        r[i / per] |= (uint8_t)(s[c] << (8u - depth * (1u + i % per)));
                    }
                }
                tide_color *w = &want[y * W + x];
                const float m = (float)most;
                switch (color) {
                case 0: *w = (tide_color){(float)s[0] / m, (float)s[0] / m, (float)s[0] / m, 1.0f}; break;
                case 2: *w = (tide_color){(float)s[0] / m, (float)s[1] / m, (float)s[2] / m, 1.0f}; break;
                case 3:
                    *w = (tide_color){(float)palette[3 * s[0]] / 255.0f, (float)palette[3 * s[0] + 1] / 255.0f,
                                      (float)palette[3 * s[0] + 2] / 255.0f, s[0] < 3 && s[0] < entries ? (float)trns_palette[s[0]] / 255.0f : 1.0f};
                    break;
                case 4: *w = (tide_color){(float)s[0] / m, (float)s[0] / m, (float)s[0] / m, (float)s[1] / m}; break;
                default: *w = (tide_color){(float)s[0] / m, (float)s[1] / m, (float)s[2] / m, (float)s[3] / m}; break;
                }
            }
        }
        const made m = {W, H, depth, color, 0, color == 3 ? palette : NULL, entries, color == 3 ? trns_palette : NULL,
                        entries < 3 ? entries : 3};
        bytes png = make_png(&m, raw, sizeof raw, 7);
        tide_png_image image;
        const bool ok = tide_png_decode(png.data, png.size, &image, NULL);
        if (!ok) printf("    color type %d at %d bits didn't decode\n", color, depth);
        TIDE_CHECK(ok);
        if (!ok) {
            free(png.data);
            continue;
        }
        TIDE_CHECK(image.width == (uint32_t)W && image.height == (uint32_t)H);
        bool same = true;
        for (int i = 0; i < W * H; i++) same = same && same_color(image.pixels[i], want[i]);
        TIDE_CHECK(same);
        free(image.pixels);
        free(png.data);
    }
}

TIDE_TEST(png_reads_a_transparent_color)
{
    const uint8_t gray[2 * (3 + 1)] = {0, 10, 20, 30, 0, 20, 20, 5};
    const uint8_t key[2] = {0, 20};
    const made m = {3, 2, 8, 0, 0, NULL, 0, key, 2};
    bytes png = make_png(&m, gray, sizeof gray, 1000);
    tide_png_image image;
    TIDE_REQUIRE(tide_png_decode(png.data, png.size, &image, NULL));
    TIDE_CHECK(near(image.pixels[0].a, 1.0f) && near(image.pixels[1].a, 0.0f) && near(image.pixels[3].a, 0.0f) && near(image.pixels[4].a, 0.0f));
    TIDE_CHECK(near(image.pixels[5].a, 1.0f) && near(image.pixels[5].r, 5.0f / 255.0f));
    free(image.pixels);
    free(png.data);

    const uint8_t rgb16[1 + 2 * 6] = {0, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBD};
    const uint8_t key16[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    const made m16 = {2, 1, 16, 2, 0, NULL, 0, key16, 6};
    png = make_png(&m16, rgb16, sizeof rgb16, 1000);
    TIDE_REQUIRE(tide_png_decode(png.data, png.size, &image, NULL));
    TIDE_CHECK(near(image.pixels[0].a, 0.0f) && near(image.pixels[1].a, 1.0f) && near(image.pixels[1].r, (float)0x1234 / 65535.0f));
    free(image.pixels);
    free(png.data);
}

static void filter_row(uint8_t *out, const uint8_t *row, const uint8_t *above, const size_t n, const uint32_t bpp, const uint8_t f)
{
    for (size_t i = 0; i < n; i++) {
        const int left = i >= bpp ? row[i - bpp] : 0, up = above ? above[i] : 0, corner = above && i >= bpp ? above[i - bpp] : 0;
        int predict = 0;
        if (f == 1) predict = left;
        else if (f == 2) predict = up;
        else if (f == 3) predict = (left + up) / 2;
        else if (f == 4) {
            const int p = left + up - corner;
            const int pa = abs(p - left), pb = abs(p - up), pc = abs(p - corner);
            predict = pa <= pb && pa <= pc ? left : pb <= pc ? up : corner;
        }
        out[i] = (uint8_t)(row[i] - predict);
    }
}

TIDE_TEST(png_undoes_every_filter)
{
    enum { W = 9, H = 10, BPP = 4 };
    uint8_t picture[H][W * BPP];
    uint32_t x = 99;
    for (int y = 0; y < H; y++) {
        for (int i = 0; i < W * BPP; i++) {
            x = x * 1103515245u + 12345u;
            picture[y][i] = (uint8_t)(x >> 23);
        }
    }
    uint8_t raw[H * (W * BPP + 1)];
    for (int y = 0; y < H; y++) {
        const uint8_t f = (uint8_t)(y % 5);
        raw[y * (W * BPP + 1)] = f;
        filter_row(raw + y * (W * BPP + 1) + 1, picture[y], y ? picture[y - 1] : NULL, W * BPP, BPP, f);
    }
    const made m = {W, H, 8, 6, 0, NULL, 0, NULL, 0};
    bytes png = make_png(&m, raw, sizeof raw, 1000);
    tide_png_image image;
    TIDE_REQUIRE(tide_png_decode(png.data, png.size, &image, NULL));
    bool same = true;
    for (int y = 0; y < H; y++) {
        for (int i = 0; i < W; i++) {
            const tide_color want = {(float)picture[y][4 * i] / 255.0f, (float)picture[y][4 * i + 1] / 255.0f,
                                     (float)picture[y][4 * i + 2] / 255.0f, (float)picture[y][4 * i + 3] / 255.0f};
            same = same && same_color(image.pixels[y * W + i], want);
        }
    }
    TIDE_CHECK(same);
    free(image.pixels);
    free(png.data);
}

TIDE_TEST(png_reads_interlaced_pictures)
{
    static const uint8_t adam7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    const int sizes[][2] = {{13, 7}, {1, 1}, {2, 3}, {9, 9}, {16, 1}};
    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        const int W = sizes[k][0], H = sizes[k][1];
        uint8_t *picture = malloc((size_t)W * H * 3u);
        for (int i = 0; i < W * H * 3; i++) picture[i] = (uint8_t)(i * 37 + 11);
        bytes raw = {0};
        for (int p = 0; p < 7; p++) {
            const int across = W > adam7[p][0] ? (W - adam7[p][0] + adam7[p][2] - 1) / adam7[p][2] : 0;
            const int down = H > adam7[p][1] ? (H - adam7[p][1] + adam7[p][3] - 1) / adam7[p][3] : 0;
            if (!across || !down) continue;
            uint8_t *rows = malloc((size_t)down * across * 3u);
            for (int y = 0; y < down; y++) {
                for (int x = 0; x < across; x++) {
                    const int px = adam7[p][0] + x * adam7[p][2], py = adam7[p][1] + y * adam7[p][3];
                    memcpy(rows + ((size_t)y * across + x) * 3u, picture + ((size_t)py * W + px) * 3u, 3);
                }
            }
            for (int y = 0; y < down; y++) {
                const uint8_t f = (uint8_t)((y + p) % 5);
                uint8_t *filtered = malloc((size_t)across * 3u);
                filter_row(filtered, rows + (size_t)y * across * 3u, y ? rows + (size_t)(y - 1) * across * 3u : NULL, (size_t)across * 3u, 3, f);
                put(&raw, &f, 1);
                put(&raw, filtered, (size_t)across * 3u);
                free(filtered);
            }
            free(rows);
        }
        const made m = {(uint32_t)W, (uint32_t)H, 8, 2, 1, NULL, 0, NULL, 0};
        bytes png = make_png(&m, raw.data, raw.size, 5);
        tide_png_image image;
        const bool ok = tide_png_decode(png.data, png.size, &image, NULL);
        TIDE_CHECK(ok);
        if (ok) {
            bool same = true;
            for (int i = 0; i < W * H; i++) {
                const tide_color want = {(float)picture[3 * i] / 255.0f, (float)picture[3 * i + 1] / 255.0f, (float)picture[3 * i + 2] / 255.0f, 1.0f};
                same = same && same_color(image.pixels[i], want);
            }
            TIDE_CHECK(same);
            free(image.pixels);
        }
        free(png.data);
        free(raw.data);
        free(picture);
    }
}

TIDE_TEST(png_refuses_what_is_wrong)
{
    const uint8_t raw[2 * (3 + 1)] = {0, 1, 2, 3, 0, 4, 5, 6};
    const made m = {3, 2, 8, 0, 0, NULL, 0, NULL, 0};
    bytes png = make_png(&m, raw, sizeof raw, 1000);
    tide_png_image image;
    TIDE_REQUIRE(tide_png_decode(png.data, png.size, &image, NULL));
    free(image.pixels);

    TIDE_CHECK(!tide_png_decode(png.data, 0, &image, NULL));
    TIDE_CHECK(!tide_png_decode((const uint8_t *)"not a png at all, not a png at all, not", 40, &image, NULL));
    for (size_t cut = 1; cut < png.size; cut += 3) TIDE_CHECK(!tide_png_decode(png.data, cut, &image, NULL));

    bytes bad = {0};
    put(&bad, png.data, png.size);
    bad.data[8 + 8 + 13] ^= 1; // A bit of the header's CRC
    TIDE_CHECK(!tide_png_decode(bad.data, bad.size, &image, NULL));
    memcpy(bad.data, png.data, png.size);
    bad.data[bad.size - 1 - 4 - 4 - 4 - 1] ^= 1; // The zlib checksum's last byte, in the IDAT before IEND... its CRC then fails first
    TIDE_CHECK(!tide_png_decode(bad.data, bad.size, &image, NULL));
    free(bad.data);

    uint8_t filtered[sizeof raw];
    memcpy(filtered, raw, sizeof raw);
    filtered[0] = 7;
    bytes f = make_png(&m, filtered, sizeof filtered, 1000);
    TIDE_CHECK(!tide_png_decode(f.data, f.size, &image, NULL));
    free(f.data);

    bytes s = make_png(&m, raw, sizeof raw - 2, 1000);
    TIDE_CHECK(!tide_png_decode(s.data, s.size, &image, NULL));
    free(s.data);

    const made p = {3, 2, 8, 3, 0, NULL, 0, NULL, 0};
    bytes np = make_png(&p, raw, sizeof raw, 1000);
    TIDE_CHECK(!tide_png_decode(np.data, np.size, &image, NULL));
    free(np.data);

    const made d = {3, 2, 4, 2, 0, NULL, 0, NULL, 0};
    bytes bd = make_png(&d, raw, sizeof raw, 1000);
    TIDE_CHECK(!tide_png_decode(bd.data, bd.size, &image, NULL));
    free(bd.data);

    const made z = {0, 2, 8, 0, 0, NULL, 0, NULL, 0};
    bytes bz = make_png(&z, raw, sizeof raw, 1000);
    TIDE_CHECK(!tide_png_decode(bz.data, bz.size, &image, NULL));
    free(bz.data);
    free(png.data);
}

// Inflated bytes past the last scanline are read for zlib's checksum up to
// TIDE_PNG_MAX_EXTRA_BYTES, and a stream with more is refused: a decode's
// work is its picture's, never what a few bytes of deflate say
TIDE_TEST(png_reads_extra_inflated_bytes_up_to_the_limit)
{
    const made m = {1, 1, 8, 0, 0, NULL, 0, NULL, 0}; // One gray pixel
    const size_t most = TIDE_PNG_MAX_EXTRA_BYTES;
    uint8_t *raw = calloc(2u + most + 1u, 1);
    raw[1] = 0x80; // The scanline: no filter, then the pixel
    tide_png_image image;
    bytes within = make_png(&m, raw, 2u + most, 65535);
    TIDE_CHECK(tide_png_decode(within.data, within.size, &image, NULL));
    TIDE_CHECK(image.width == 1 && image.height == 1 && image.pixels[0].r == 128.0f / 255.0f);
    free(image.pixels);
    bytes past = make_png(&m, raw, 2u + most + 1u, 65535);
    char why[TIDE_PNG_WHY];
    TIDE_CHECK(!tide_png_decode(past.data, past.size, &image, why));
    TIDE_CHECK(strcmp(why, "its compressed data goes on for more than 1 MiB past its last row (TIDE_PNG_MAX_EXTRA_BYTES)") == 0);
    TIDE_CHECK(!tide_png_decode(past.data, 9, &image, why) && why[0] == 0); // Wrong in another way: no limit to name
    free(within.data);
    free(past.data);
    free(raw);
}
