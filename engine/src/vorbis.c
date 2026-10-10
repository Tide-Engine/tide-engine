// Ogg Vorbis, read by the engine's own code (tide/vorbis.h): Ogg's pages from
// RFC 3533, and the Vorbis I specification's headers, codebooks, floor 1,
// the three residue formats, channel coupling, the inverse MDCT and the
// window overlap. Nothing of anyone else's.

#include "tide/vorbis.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

#define LEAF 0x80000000u       // A tree node that's an entry
#define FLOOR_VALUES_MOST 65u  // A floor's X list: 2 + 31 partitions of up to 8 each, by the format
#define BLOCK_MOST 8192u
#define BLOCK_LEAST 64u
#define CODEBOOK_FLOATS_MOST (1u << 24) // The vectors of one codebook, which a corrupt file could ask gigabytes of
#define CLASSWORDS_MOST 64u

// ---------------------------------------------------------------------------
// A sine of our own, in double, for the windows and the transforms' twiddles:
// the engine's math is floats, and a file has to decode the same everywhere.

static const double PI = 3.14159265358979323846264338327950;

static double vsin(double x)
{
    // To [-pi, pi], then to a quadrant, then Taylor's series to the 17th power
    x -= (double)(long long)(x / (2.0 * PI)) * 2.0 * PI;
    if (x > PI) x -= 2.0 * PI;
    if (x < -PI) x += 2.0 * PI;
    double sign = 1.0;
    if (x < 0) {
        x = -x;
        sign = -1.0;
    }
    if (x > PI / 2.0) x = PI - x;
    const double x2 = x * x;
    double term = x, sum = x;
    for (int k = 1; k <= 8; k++) {
        term *= -x2 / (double)((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sign * sum;
}

static double vcos(const double x) { return vsin(x + PI / 2.0); }

// ---------------------------------------------------------------------------
// Bits, least significant first, as Vorbis packs them

typedef struct bits {
    const uint8_t *p;
    uint64_t size; // In bits
    uint64_t at;
    bool eop; // Read past the end: what's read from there is zeros
} bits;

static bits bits_over(const uint8_t *p, const uint32_t bytes) { return (bits){.p = p, .size = (uint64_t)bytes * 8u}; }

static uint32_t read_bits(bits *b, const uint32_t n)
{
    if (n == 0) return 0;
    if (b->at + n > b->size) {
        b->eop = true;
        b->at = b->size;
        return 0;
    }
    const uint64_t byte = b->at >> 3;
    const uint32_t off = (uint32_t)(b->at & 7u);
    uint64_t v = 0;
    for (uint32_t i = 0, got = 0; got < off + n; i++, got += 8) v |= (uint64_t)b->p[byte + i] << (8u * i);
    v >>= off;
    b->at += n;
    return n == 32 ? (uint32_t)v : (uint32_t)(v & ((UINT64_C(1) << n) - 1u));
}

static uint32_t ilog(uint32_t v)
{
    uint32_t r = 0;
    while (v) {
        r++;
        v >>= 1;
    }
    return r;
}

static float float32_unpack(const uint32_t x)
{
    const uint32_t mantissa = x & 0x1fffffu;
    const bool negative = (x & 0x80000000u) != 0;
    int32_t exponent = (int32_t)((x & 0x7fe00000u) >> 21) - 788;
    double v = (double)mantissa;
    while (exponent > 0) {
        v *= 2.0;
        exponent--;
    }
    while (exponent < 0) {
        v *= 0.5;
        exponent++;
    }
    return (float)(negative ? -v : v);
}

// ---------------------------------------------------------------------------
// Setup: codebooks, floors, residues, mappings and modes

typedef struct codebook {
    uint32_t dimensions, entries;
    uint32_t *nodes; // Pairs: a child's node, LEAF | entry, or 0 for a branch no codeword takes
    uint32_t node_count, node_capacity;
    uint32_t lookup_type; // 0: scalar only
    float *vectors;       // entries * dimensions, for lookup types 1 and 2
} codebook;

typedef struct floor1 {
    uint8_t partitions;
    uint8_t partition_class[31];
    uint8_t class_dims[16], class_subclasses[16];
    int16_t class_masterbook[16];
    int16_t subclass_books[16][8];
    uint8_t multiplier; // 1 to 4
    uint8_t rangebits;
    uint8_t values;
    uint16_t X[FLOOR_VALUES_MOST];
    uint8_t sorted[FLOOR_VALUES_MOST];           // The X list's order
    uint8_t low[FLOOR_VALUES_MOST], high[FLOOR_VALUES_MOST]; // Each point's neighbors among those before it
} floor1;

typedef struct residue {
    uint16_t type;
    uint32_t begin, end, partition_size;
    uint8_t classifications;
    uint8_t classbook;
    int16_t books[64][8]; // -1 where the cascade leaves a pass out
} residue;

typedef struct mapping {
    uint8_t submaps;
    uint16_t coupling_steps;
    uint8_t magnitude[256], angle[256];
    uint8_t mux[2];
    uint8_t submap_floor[16], submap_residue[16];
} mapping;

typedef struct mode {
    bool blockflag;
    uint8_t mapping;
} mode;

// The tables one block size's transform and window take
typedef struct transform {
    uint32_t n;
    float *t1re, *t1im; // n/4: e^(i pi m / M)
    float *t2re, *t2im; // n/4: e^(i pi (p + 1/4) / M)
    float *wre, *wim;   // n/8: e^(2 pi i k / L), the FFT's
    uint16_t *rev;      // n/4: the FFT's bit reversal
    float *slope;       // n/2: the window's rising half
} transform;

struct tide_vorbis {
    uint32_t channels, rate;
    uint32_t bs0, bs1;
    uint64_t frames; // The last page's granule position
    codebook *books;
    uint32_t book_count;
    floor1 *floors;
    uint32_t floor_count;
    residue *residues;
    uint32_t residue_count;
    mapping *mappings;
    uint32_t mapping_count;
    mode *modes;
    uint32_t mode_count;
    transform transforms[2]; // bs0's and bs1's (the second unused when they're one)
    uint8_t *packets;        // The audio packets, one after another
    uint32_t *offsets;       // packet_count + 1
    uint32_t packet_count;
    uint32_t classwords_most; // The widest classbook's dimensions
};

static bool fail(const char **why, const char *text)
{
    if (why) *why = text;
    return false;
}

// Adds a tree node, giving its index (its two children zero)
static uint32_t add_node(codebook *c)
{
    if (c->node_count == c->node_capacity) {
        const uint32_t grown = c->node_capacity ? c->node_capacity * 2u : 64u;
        c->nodes = tide_realloc(c->nodes, c->node_capacity * 2u * sizeof *c->nodes, grown * 2u * sizeof *c->nodes);
        memset(c->nodes + c->node_capacity * 2u, 0, (grown - c->node_capacity) * 2u * sizeof *c->nodes);
        c->node_capacity = grown;
    }
    return c->node_count++;
}

// Puts an entry's codeword, the top `length` bits of `code`, in the tree
static bool insert_codeword(codebook *c, const uint32_t code, const uint32_t length, const uint32_t entry)
{
    uint32_t node = 0;
    for (uint32_t bit = 0; bit < length; bit++) {
        const uint32_t side = (code >> (31u - bit)) & 1u;
        uint32_t *slot = &c->nodes[2u * node + side];
        if (bit + 1u == length) {
            if (*slot) return false; // Two codewords, one a prefix of the other
            *slot = LEAF | entry;
            return true;
        }
        if (*slot & LEAF) return false;
        if (!*slot) {
            const uint32_t made = add_node(c);
            slot = &c->nodes[2u * node + side]; // The array may have moved
            *slot = made;
        }
        node = *slot;
    }
    return false;
}

static uint32_t lookup1_values(const uint32_t entries, const uint32_t dimensions)
{
    uint32_t r = 0;
    for (;;) {
        // (r + 1) ^ dimensions <= entries?
        uint64_t p = 1;
        bool over = false;
        for (uint32_t i = 0; i < dimensions; i++) {
            p *= (uint64_t)(r + 1u);
            if (p > entries) {
                over = true;
                break;
            }
        }
        if (over) return r;
        r++;
    }
}

static bool read_codebook(bits *b, codebook *c, const char **why)
{
    if (read_bits(b, 24) != 0x564342u) return fail(why, "a codebook's sync pattern is wrong");
    c->dimensions = read_bits(b, 16);
    c->entries = read_bits(b, 24);
    if (c->entries == 0) return fail(why, "a codebook has no entries");
    uint8_t *lengths = tide_alloc_zeroed(c->entries, 1);
    bool ok = true;
    if (read_bits(b, 1) == 0) {
        const bool sparse = read_bits(b, 1) != 0;
        for (uint32_t i = 0; i < c->entries; i++) {
            if (sparse && read_bits(b, 1) == 0) continue;
            lengths[i] = (uint8_t)(read_bits(b, 5) + 1u);
        }
    } else {
        uint32_t length = read_bits(b, 5) + 1u;
        uint32_t at = 0;
        while (at < c->entries) {
            const uint32_t count = read_bits(b, ilog(c->entries - at));
            if (at + count > c->entries || length > 32u) {
                ok = false;
                break;
            }
            for (uint32_t i = 0; i < count; i++) lengths[at + i] = (uint8_t)length;
            at += count;
            length++;
        }
    }
    if (!ok || b->eop) {
        free(lengths);
        return fail(why, "a codebook's lengths are cut short");
    }
    // Codewords: each entry takes the lowest code of its length not yet taken,
    // the specification's assignment, which decoders must follow exactly
    uint32_t available[33] = {0};
    bool first = true;
    add_node(c); // The root
    for (uint32_t i = 0; i < c->entries; i++) {
        const uint32_t length = lengths[i];
        if (!length) continue;
        uint32_t code;
        if (first) {
            code = 0;
            for (uint32_t k = 1; k <= length; k++) available[k] = 1u << (32u - k);
            first = false;
        } else {
            uint32_t y = length;
            while (y > 0 && !available[y]) y--;
            if (y == 0) {
                free(lengths);
                return fail(why, "a codebook has more codewords than its lengths allow");
            }
            code = available[y];
            available[y] = 0;
            for (uint32_t k = length; k > y; k--) available[k] = code + (1u << (32u - k));
        }
        if (!insert_codeword(c, code, length, i)) {
            free(lengths);
            return fail(why, "a codebook's codewords aren't a prefix code");
        }
    }
    free(lengths);
    c->lookup_type = read_bits(b, 4);
    if (c->lookup_type == 0) return true;
    if (c->lookup_type > 2) return fail(why, "a codebook's lookup type isn't one");
    const float minimum = float32_unpack(read_bits(b, 32));
    const float delta = float32_unpack(read_bits(b, 32));
    const uint32_t value_bits = read_bits(b, 4) + 1u;
    const bool sequence = read_bits(b, 1) != 0;
    const uint64_t floats = (uint64_t)c->entries * c->dimensions;
    if (c->dimensions == 0 || floats > CODEBOOK_FLOATS_MOST) return fail(why, "a codebook's vectors are too big");
    const uint32_t lookup_values = c->lookup_type == 1 ? lookup1_values(c->entries, c->dimensions) : (uint32_t)floats;
    uint16_t *multiplicands = tide_alloc(lookup_values * sizeof *multiplicands + 1u);
    for (uint32_t i = 0; i < lookup_values; i++) multiplicands[i] = (uint16_t)read_bits(b, value_bits);
    if (b->eop) {
        free(multiplicands);
        return fail(why, "a codebook's vectors are cut short");
    }
    c->vectors = tide_alloc((size_t)floats * sizeof *c->vectors);
    for (uint32_t e = 0; e < c->entries; e++) {
        float last = 0.0f;
        float *v = c->vectors + (size_t)e * c->dimensions;
        if (c->lookup_type == 1) {
            uint32_t divisor = 1;
            for (uint32_t d = 0; d < c->dimensions; d++) {
                const uint32_t offset = (e / divisor) % lookup_values;
                v[d] = (float)multiplicands[offset] * delta + minimum + last;
                if (sequence) last = v[d];
                divisor *= lookup_values;
            }
        } else {
            for (uint32_t d = 0; d < c->dimensions; d++) {
                v[d] = (float)multiplicands[(size_t)e * c->dimensions + d] * delta + minimum + last;
                if (sequence) last = v[d];
            }
        }
    }
    free(multiplicands);
    return true;
}

static void free_codebook(codebook *c)
{
    free(c->nodes);
    free(c->vectors);
}

// One entry's number, read bit by bit down the tree: -1 at the packet's end,
// or down a branch no codeword takes (a corrupt packet), which is the end too
static int32_t decode_scalar(bits *b, const codebook *c)
{
    uint32_t node = 0;
    for (;;) {
        if (b->at >= b->size) {
            b->eop = true;
            return -1;
        }
        const uint32_t bit = (b->p[b->at >> 3] >> (b->at & 7u)) & 1u;
        b->at++;
        const uint32_t next = c->nodes[2u * node + bit];
        if (!next) {
            b->eop = true;
            return -1;
        }
        if (next & LEAF) return (int32_t)(next & ~LEAF);
        node = next;
    }
}

static bool read_floor(bits *b, floor1 *f, const uint32_t book_count, const char **why)
{
    const uint32_t type = read_bits(b, 16);
    if (type == 0) return fail(why, "a floor of type 0, which this decoder doesn't read (no encoder writes one)");
    if (type != 1) return fail(why, "a floor's type isn't one");
    f->partitions = (uint8_t)read_bits(b, 5);
    uint32_t classes = 0;
    for (uint32_t i = 0; i < f->partitions; i++) {
        f->partition_class[i] = (uint8_t)read_bits(b, 4);
        if (f->partition_class[i] + 1u > classes) classes = f->partition_class[i] + 1u;
    }
    for (uint32_t c = 0; c < classes; c++) {
        f->class_dims[c] = (uint8_t)(read_bits(b, 3) + 1u);
        f->class_subclasses[c] = (uint8_t)read_bits(b, 2);
        f->class_masterbook[c] = -1;
        if (f->class_subclasses[c]) {
            f->class_masterbook[c] = (int16_t)read_bits(b, 8);
            if (f->class_masterbook[c] >= (int32_t)book_count) return fail(why, "a floor names a codebook the file hasn't got");
        }
        for (uint32_t s = 0; s < (1u << f->class_subclasses[c]); s++) {
            f->subclass_books[c][s] = (int16_t)((int32_t)read_bits(b, 8) - 1);
            if (f->subclass_books[c][s] >= (int32_t)book_count) return fail(why, "a floor names a codebook the file hasn't got");
        }
    }
    f->multiplier = (uint8_t)(read_bits(b, 2) + 1u);
    f->rangebits = (uint8_t)read_bits(b, 4);
    f->X[0] = 0;
    f->X[1] = (uint16_t)(1u << f->rangebits);
    uint32_t values = 2;
    for (uint32_t i = 0; i < f->partitions; i++) {
        const uint32_t c = f->partition_class[i];
        for (uint32_t j = 0; j < f->class_dims[c]; j++) {
            if (values >= FLOOR_VALUES_MOST) return fail(why, "a floor has too many points");
            f->X[values++] = (uint16_t)read_bits(b, f->rangebits);
        }
    }
    f->values = (uint8_t)values;
    if (b->eop) return fail(why, "a floor is cut short");
    // The order of the points, and each one's neighbors among the points before it
    for (uint32_t i = 0; i < values; i++) f->sorted[i] = (uint8_t)i;
    for (uint32_t i = 1; i < values; i++) {
        for (uint32_t j = i; j > 0 && f->X[f->sorted[j - 1]] > f->X[f->sorted[j]]; j--) {
            const uint8_t t = f->sorted[j];
            f->sorted[j] = f->sorted[j - 1];
            f->sorted[j - 1] = t;
        }
    }
    for (uint32_t i = 1; i < values; i++) {
        if (f->X[f->sorted[i]] == f->X[f->sorted[i - 1]]) return fail(why, "a floor has two points at one place");
    }
    for (uint32_t i = 2; i < values; i++) {
        uint32_t low = 0, high = 1;
        for (uint32_t n = 0; n < i; n++) {
            if (f->X[n] < f->X[i] && f->X[n] > f->X[low]) low = n;
            if (f->X[n] > f->X[i] && f->X[n] < f->X[high]) high = n;
        }
        f->low[i] = (uint8_t)low;
        f->high[i] = (uint8_t)high;
    }
    return true;
}

static bool read_residue(bits *b, residue *r, const codebook *books, const uint32_t book_count, const char **why)
{
    r->type = (uint16_t)read_bits(b, 16);
    if (r->type > 2) return fail(why, "a residue's type isn't one");
    r->begin = read_bits(b, 24);
    r->end = read_bits(b, 24);
    r->partition_size = read_bits(b, 24) + 1u;
    r->classifications = (uint8_t)(read_bits(b, 6) + 1u);
    r->classbook = (uint8_t)read_bits(b, 8);
    if (r->classbook >= book_count) return fail(why, "a residue names a codebook the file hasn't got");
    if (books[r->classbook].dimensions == 0 || books[r->classbook].dimensions > CLASSWORDS_MOST) {
        return fail(why, "a residue's classbook has too many dimensions");
    }
    uint8_t cascade[64];
    for (uint32_t i = 0; i < r->classifications; i++) {
        const uint32_t low = read_bits(b, 3);
        const uint32_t high = read_bits(b, 1) ? read_bits(b, 5) : 0;
        cascade[i] = (uint8_t)((high << 3) | low);
    }
    for (uint32_t i = 0; i < r->classifications; i++) {
        for (uint32_t pass = 0; pass < 8; pass++) {
            r->books[i][pass] = -1;
            if (!(cascade[i] & (1u << pass))) continue;
            const uint32_t book = read_bits(b, 8);
            if (book >= book_count) return fail(why, "a residue names a codebook the file hasn't got");
            if (books[book].lookup_type == 0) return fail(why, "a residue decodes vectors with a codebook that has none");
            r->books[i][pass] = (int16_t)book;
        }
    }
    if (b->eop) return fail(why, "a residue is cut short");
    return true;
}

static bool read_mapping(bits *b, mapping *m, const uint32_t channels, const uint32_t floor_count, const uint32_t residue_count,
                         const char **why)
{
    if (read_bits(b, 16) != 0) return fail(why, "a mapping's type isn't 0");
    m->submaps = read_bits(b, 1) ? (uint8_t)(read_bits(b, 4) + 1u) : 1;
    m->coupling_steps = 0;
    if (read_bits(b, 1)) {
        m->coupling_steps = (uint16_t)(read_bits(b, 8) + 1u);
        for (uint32_t i = 0; i < m->coupling_steps; i++) {
            m->magnitude[i] = (uint8_t)read_bits(b, ilog(channels - 1u));
            m->angle[i] = (uint8_t)read_bits(b, ilog(channels - 1u));
            if (m->magnitude[i] == m->angle[i] || m->magnitude[i] >= channels || m->angle[i] >= channels) {
                return fail(why, "a mapping couples a channel with itself, or one the file hasn't got");
            }
        }
    }
    if (read_bits(b, 2) != 0) return fail(why, "a mapping's reserved bits aren't 0");
    for (uint32_t c = 0; c < channels; c++) {
        m->mux[c] = 0;
        if (m->submaps > 1) {
            m->mux[c] = (uint8_t)read_bits(b, 4);
            if (m->mux[c] >= m->submaps) return fail(why, "a mapping puts a channel in a submap it hasn't got");
        }
    }
    for (uint32_t s = 0; s < m->submaps; s++) {
        read_bits(b, 8); // The time configuration, unused
        m->submap_floor[s] = (uint8_t)read_bits(b, 8);
        m->submap_residue[s] = (uint8_t)read_bits(b, 8);
        if (m->submap_floor[s] >= floor_count || m->submap_residue[s] >= residue_count) {
            return fail(why, "a mapping names a floor or a residue the file hasn't got");
        }
    }
    if (b->eop) return fail(why, "a mapping is cut short");
    return true;
}

// ---------------------------------------------------------------------------
// The transforms' tables

static void make_transform(transform *t, const uint32_t n)
{
    const uint32_t M = n / 2, L = M / 2;
    t->n = n;
    t->t1re = tide_alloc(L * sizeof(float));
    t->t1im = tide_alloc(L * sizeof(float));
    t->t2re = tide_alloc(L * sizeof(float));
    t->t2im = tide_alloc(L * sizeof(float));
    t->wre = tide_alloc((L / 2) * sizeof(float));
    t->wim = tide_alloc((L / 2) * sizeof(float));
    t->rev = tide_alloc(L * sizeof(uint16_t));
    t->slope = tide_alloc(M * sizeof(float));
    for (uint32_t m = 0; m < L; m++) {
        t->t1re[m] = (float)vcos(PI * (double)m / (double)M);
        t->t1im[m] = (float)vsin(PI * (double)m / (double)M);
        t->t2re[m] = (float)vcos(PI * ((double)m + 0.25) / (double)M);
        t->t2im[m] = (float)vsin(PI * ((double)m + 0.25) / (double)M);
    }
    for (uint32_t k = 0; k < L / 2; k++) {
        t->wre[k] = (float)vcos(2.0 * PI * (double)k / (double)L);
        t->wim[k] = (float)vsin(2.0 * PI * (double)k / (double)L);
    }
    const uint32_t bits_of = ilog(L) - 1u;
    for (uint32_t i = 0; i < L; i++) {
        uint32_t r = 0;
        for (uint32_t bit = 0; bit < bits_of; bit++) r |= ((i >> bit) & 1u) << (bits_of - 1u - bit);
        t->rev[i] = (uint16_t)r;
    }
    for (uint32_t j = 0; j < M; j++) {
        const double s = vsin(((double)j + 0.5) / (double)M * PI / 2.0);
        t->slope[j] = (float)vsin(PI / 2.0 * s * s);
    }
}

static void free_transform(transform *t)
{
    free(t->t1re);
    free(t->t1im);
    free(t->t2re);
    free(t->t2im);
    free(t->wre);
    free(t->wim);
    free(t->rev);
    free(t->slope);
}

// The inverse MDCT of M = n/2 coefficients into n samples: a DCT-IV through
// an L = n/4 point complex FFT, then the DCT-IV's halves laid out and signed as
// the MDCT's symmetries have them. `cre`, `cim` are L floats of scratch, `u` M.
static void imdct(const transform *t, const float *x, float *y, float *cre, float *cim, float *u)
{
    const uint32_t n = t->n, M = n / 2, L = M / 2;
    for (uint32_t m = 0; m < L; m++) {
        const float a = x[2u * m], b = x[M - 1u - 2u * m];
        cre[t->rev[m]] = a * t->t1re[m] + b * t->t1im[m];
        cim[t->rev[m]] = a * t->t1im[m] - b * t->t1re[m];
    }
    for (uint32_t len = 2; len <= L; len <<= 1) {
        const uint32_t half = len / 2, step = L / len;
        for (uint32_t i = 0; i < L; i += len) {
            for (uint32_t j = 0; j < half; j++) {
                const float wr = t->wre[j * step], wi = t->wim[j * step];
                const uint32_t p = i + j, q = i + j + half;
                const float tr = cre[q] * wr - cim[q] * wi;
                const float ti = cre[q] * wi + cim[q] * wr;
                cre[q] = cre[p] - tr;
                cim[q] = cim[p] - ti;
                cre[p] += tr;
                cim[p] += ti;
            }
        }
    }
    for (uint32_t p = 0; p < L; p++) {
        u[2u * p] = cre[p] * t->t2re[p] - cim[p] * t->t2im[p];
        u[M - 1u - 2u * p] = cre[p] * t->t2im[p] + cim[p] * t->t2re[p];
    }
    const uint32_t h = M / 2;
    for (uint32_t i = 0; i < h; i++) y[i] = u[i + h];
    for (uint32_t i = h; i < 3u * h; i++) y[i] = -u[3u * h - 1u - i];
    for (uint32_t i = 3u * h; i < n; i++) y[i] = -u[i - 3u * h];
}

// ---------------------------------------------------------------------------
// Ogg: the pages of the first Vorbis stream, and the packets in them

// Ogg's CRC-32 (the polynomial 0x04c11db7, direct, from 0, nothing after),
// going on from `crc` over `size` bytes
static uint32_t ogg_crc(uint32_t crc, const uint8_t *p, const uint32_t size)
{
    // Immutable: several clips may open on the preparation pool at once.
    static const uint32_t table[256] = {
        0x00000000u, 0x04c11db7u, 0x09823b6eu, 0x0d4326d9u, 0x130476dcu, 0x17c56b6bu, 0x1a864db2u, 0x1e475005u,
        0x2608edb8u, 0x22c9f00fu, 0x2f8ad6d6u, 0x2b4bcb61u, 0x350c9b64u, 0x31cd86d3u, 0x3c8ea00au, 0x384fbdbdu,
        0x4c11db70u, 0x48d0c6c7u, 0x4593e01eu, 0x4152fda9u, 0x5f15adacu, 0x5bd4b01bu, 0x569796c2u, 0x52568b75u,
        0x6a1936c8u, 0x6ed82b7fu, 0x639b0da6u, 0x675a1011u, 0x791d4014u, 0x7ddc5da3u, 0x709f7b7au, 0x745e66cdu,
        0x9823b6e0u, 0x9ce2ab57u, 0x91a18d8eu, 0x95609039u, 0x8b27c03cu, 0x8fe6dd8bu, 0x82a5fb52u, 0x8664e6e5u,
        0xbe2b5b58u, 0xbaea46efu, 0xb7a96036u, 0xb3687d81u, 0xad2f2d84u, 0xa9ee3033u, 0xa4ad16eau, 0xa06c0b5du,
        0xd4326d90u, 0xd0f37027u, 0xddb056feu, 0xd9714b49u, 0xc7361b4cu, 0xc3f706fbu, 0xceb42022u, 0xca753d95u,
        0xf23a8028u, 0xf6fb9d9fu, 0xfbb8bb46u, 0xff79a6f1u, 0xe13ef6f4u, 0xe5ffeb43u, 0xe8bccd9au, 0xec7dd02du,
        0x34867077u, 0x30476dc0u, 0x3d044b19u, 0x39c556aeu, 0x278206abu, 0x23431b1cu, 0x2e003dc5u, 0x2ac12072u,
        0x128e9dcfu, 0x164f8078u, 0x1b0ca6a1u, 0x1fcdbb16u, 0x018aeb13u, 0x054bf6a4u, 0x0808d07du, 0x0cc9cdcau,
        0x7897ab07u, 0x7c56b6b0u, 0x71159069u, 0x75d48ddeu, 0x6b93dddbu, 0x6f52c06cu, 0x6211e6b5u, 0x66d0fb02u,
        0x5e9f46bfu, 0x5a5e5b08u, 0x571d7dd1u, 0x53dc6066u, 0x4d9b3063u, 0x495a2dd4u, 0x44190b0du, 0x40d816bau,
        0xaca5c697u, 0xa864db20u, 0xa527fdf9u, 0xa1e6e04eu, 0xbfa1b04bu, 0xbb60adfcu, 0xb6238b25u, 0xb2e29692u,
        0x8aad2b2fu, 0x8e6c3698u, 0x832f1041u, 0x87ee0df6u, 0x99a95df3u, 0x9d684044u, 0x902b669du, 0x94ea7b2au,
        0xe0b41de7u, 0xe4750050u, 0xe9362689u, 0xedf73b3eu, 0xf3b06b3bu, 0xf771768cu, 0xfa325055u, 0xfef34de2u,
        0xc6bcf05fu, 0xc27dede8u, 0xcf3ecb31u, 0xcbffd686u, 0xd5b88683u, 0xd1799b34u, 0xdc3abdedu, 0xd8fba05au,
        0x690ce0eeu, 0x6dcdfd59u, 0x608edb80u, 0x644fc637u, 0x7a089632u, 0x7ec98b85u, 0x738aad5cu, 0x774bb0ebu,
        0x4f040d56u, 0x4bc510e1u, 0x46863638u, 0x42472b8fu, 0x5c007b8au, 0x58c1663du, 0x558240e4u, 0x51435d53u,
        0x251d3b9eu, 0x21dc2629u, 0x2c9f00f0u, 0x285e1d47u, 0x36194d42u, 0x32d850f5u, 0x3f9b762cu, 0x3b5a6b9bu,
        0x0315d626u, 0x07d4cb91u, 0x0a97ed48u, 0x0e56f0ffu, 0x1011a0fau, 0x14d0bd4du, 0x19939b94u, 0x1d528623u,
        0xf12f560eu, 0xf5ee4bb9u, 0xf8ad6d60u, 0xfc6c70d7u, 0xe22b20d2u, 0xe6ea3d65u, 0xeba91bbcu, 0xef68060bu,
        0xd727bbb6u, 0xd3e6a601u, 0xdea580d8u, 0xda649d6fu, 0xc423cd6au, 0xc0e2d0ddu, 0xcda1f604u, 0xc960ebb3u,
        0xbd3e8d7eu, 0xb9ff90c9u, 0xb4bcb610u, 0xb07daba7u, 0xae3afba2u, 0xaafbe615u, 0xa7b8c0ccu, 0xa379dd7bu,
        0x9b3660c6u, 0x9ff77d71u, 0x92b45ba8u, 0x9675461fu, 0x8832161au, 0x8cf30badu, 0x81b02d74u, 0x857130c3u,
        0x5d8a9099u, 0x594b8d2eu, 0x5408abf7u, 0x50c9b640u, 0x4e8ee645u, 0x4a4ffbf2u, 0x470cdd2bu, 0x43cdc09cu,
        0x7b827d21u, 0x7f436096u, 0x7200464fu, 0x76c15bf8u, 0x68860bfdu, 0x6c47164au, 0x61043093u, 0x65c52d24u,
        0x119b4be9u, 0x155a565eu, 0x18197087u, 0x1cd86d30u, 0x029f3d35u, 0x065e2082u, 0x0b1d065bu, 0x0fdc1becu,
        0x3793a651u, 0x3352bbe6u, 0x3e119d3fu, 0x3ad08088u, 0x2497d08du, 0x2056cd3au, 0x2d15ebe3u, 0x29d4f654u,
        0xc5a92679u, 0xc1683bceu, 0xcc2b1d17u, 0xc8ea00a0u, 0xd6ad50a5u, 0xd26c4d12u, 0xdf2f6bcbu, 0xdbee767cu,
        0xe3a1cbc1u, 0xe760d676u, 0xea23f0afu, 0xeee2ed18u, 0xf0a5bd1du, 0xf464a0aau, 0xf9278673u, 0xfde69bc4u,
        0x89b8fd09u, 0x8d79e0beu, 0x803ac667u, 0x84fbdbd0u, 0x9abc8bd5u, 0x9e7d9662u, 0x933eb0bbu, 0x97ffad0cu,
        0xafb010b1u, 0xab710d06u, 0xa6322bdfu, 0xa2f33668u, 0xbcb4666du, 0xb8757bdau, 0xb5365d03u, 0xb1f740b4u,
    };
    for (uint32_t i = 0; i < size; i++) crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 0xffu];
    return crc;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

typedef struct packets {
    uint8_t *data;
    uint32_t size, capacity;
    uint32_t *offsets; // count + 1
    uint32_t count, offsets_capacity;
    uint32_t partial; // Bytes of a packet that goes on into the next page
} packets;

static void packets_append(packets *ps, const uint8_t *bytes, const uint32_t size)
{
    if (ps->size + size > ps->capacity) {
        uint32_t grown = ps->capacity ? ps->capacity : 4096u;
        while (ps->size + size > grown) grown *= 2u;
        ps->data = tide_realloc(ps->data, ps->capacity, grown);
        ps->capacity = grown;
    }
    memcpy(ps->data + ps->size, bytes, size);
    ps->size += size;
    ps->partial += size;
}

static void packets_end(packets *ps)
{
    if (ps->count + 2u > ps->offsets_capacity) {
        const uint32_t grown = ps->offsets_capacity ? ps->offsets_capacity * 2u : 64u;
        ps->offsets = tide_realloc(ps->offsets, ps->offsets_capacity * sizeof *ps->offsets, grown * sizeof *ps->offsets);
        ps->offsets_capacity = grown;
    }
    if (ps->count == 0) ps->offsets[0] = 0;
    ps->offsets[++ps->count] = ps->size;
    ps->partial = 0;
}

// Reads every page, keeping the packets of the first stream that begins with a
// Vorbis identification header, and the last granule position of its pages.
static bool read_pages(const uint8_t *bytes, const size_t size, packets *ps, uint64_t *granule, const char **why)
{
    if (size > UINT32_MAX) return fail(why, "the file is too big");
    uint32_t at = 0;
    bool found = false;
    uint32_t serial = 0;
    bool ended = false;
    *granule = 0;
    while (at + 27u <= size && !ended) {
        const uint8_t *h = bytes + at;
        if (memcmp(h, "OggS", 4) != 0) return fail(why, "a page doesn't start with OggS");
        if (h[4] != 0) return fail(why, "a page's version isn't 0");
        const uint32_t segments = h[26];
        if (at + 27u + segments > size) return fail(why, "a page is cut short");
        uint32_t body = 0;
        for (uint32_t i = 0; i < segments; i++) body += h[27u + i];
        const uint32_t page_size = 27u + segments + body;
        if (at + page_size > size) return fail(why, "a page is cut short");
        uint8_t header[27 + 255];
        memcpy(header, h, 27u + segments);
        memset(header + 22, 0, 4);
        const uint8_t *p = h + 27u + segments;
        const uint32_t crc = ogg_crc(ogg_crc(0, header, 27u + segments), p, body);
        if (crc != le32(h + 22)) return fail(why, "a page's checksum is wrong");
        const uint32_t page_serial = le32(h + 14);
        const uint8_t type = h[5];
        if (!found) {
            if ((type & 0x02u) && segments && body >= 7u && p[0] == 1 && memcmp(p + 1, "vorbis", 6) == 0) {
                found = true;
                serial = page_serial;
            } else {
                at += page_size;
                continue;
            }
        } else if (page_serial != serial) {
            at += page_size;
            continue;
        }
        bool completed = false;
        uint32_t off = 0;
        bool skipping = (type & 0x01u) && ps->partial == 0; // Continues a packet this stream lost: drop the rest of it
        for (uint32_t i = 0; i < segments; i++) {
            const uint32_t lacing = h[27u + i];
            if (skipping) {
                if (lacing < 255u) skipping = false;
            } else {
                packets_append(ps, p + off, lacing);
                if (lacing < 255u) {
                    packets_end(ps);
                    completed = true;
                }
            }
            off += lacing;
        }
        const uint64_t g = le64(h + 6);
        if (completed && g != UINT64_MAX) *granule = g;
        if (type & 0x04u) ended = true;
        at += page_size;
    }
    if (!found) return fail(why, "there's no Vorbis stream in it");
    if (ps->count < 3u) return fail(why, "the file ends before its three headers");
    return true;
}

// ---------------------------------------------------------------------------
// Opening: the headers

static bool read_identification(tide_vorbis *f, const uint8_t *p, const uint32_t size, const char **why)
{
    if (size < 30u || p[0] != 1 || memcmp(p + 1, "vorbis", 6) != 0) return fail(why, "the identification header isn't one");
    if (le32(p + 7) != 0) return fail(why, "the Vorbis version isn't 0");
    f->channels = p[11];
    f->rate = le32(p + 12);
    const uint8_t sizes = p[28];
    f->bs0 = 1u << (sizes & 15u);
    f->bs1 = 1u << (sizes >> 4);
    if (f->channels == 0) return fail(why, "the file has no channels");
    if (f->channels > 2) return fail(why, "the file has more than two channels");
    if (f->rate == 0) return fail(why, "the file's rate is 0");
    if (f->bs0 < BLOCK_LEAST || f->bs1 > BLOCK_MOST || f->bs0 > f->bs1) return fail(why, "the file's block sizes aren't ones");
    if (!(p[29] & 1u)) return fail(why, "the identification header's framing bit isn't set");
    return true;
}

static bool read_setup(tide_vorbis *f, const uint8_t *p, const uint32_t size, const char **why)
{
    if (size < 7u || p[0] != 5 || memcmp(p + 1, "vorbis", 6) != 0) return fail(why, "the setup header isn't one");
    bits b = bits_over(p + 7, size - 7u);
    f->book_count = read_bits(&b, 8) + 1u;
    f->books = tide_alloc_zeroed(f->book_count, sizeof *f->books);
    for (uint32_t i = 0; i < f->book_count; i++) {
        if (!read_codebook(&b, &f->books[i], why)) return false;
        if (f->books[i].dimensions > f->classwords_most) f->classwords_most = f->books[i].dimensions;
    }
    const uint32_t times = read_bits(&b, 6) + 1u;
    for (uint32_t i = 0; i < times; i++) {
        if (read_bits(&b, 16) != 0) return fail(why, "a time domain transform isn't 0");
    }
    f->floor_count = read_bits(&b, 6) + 1u;
    f->floors = tide_alloc_zeroed(f->floor_count, sizeof *f->floors);
    for (uint32_t i = 0; i < f->floor_count; i++) {
        if (!read_floor(&b, &f->floors[i], f->book_count, why)) return false;
    }
    f->residue_count = read_bits(&b, 6) + 1u;
    f->residues = tide_alloc_zeroed(f->residue_count, sizeof *f->residues);
    for (uint32_t i = 0; i < f->residue_count; i++) {
        if (!read_residue(&b, &f->residues[i], f->books, f->book_count, why)) return false;
    }
    f->mapping_count = read_bits(&b, 6) + 1u;
    f->mappings = tide_alloc_zeroed(f->mapping_count, sizeof *f->mappings);
    for (uint32_t i = 0; i < f->mapping_count; i++) {
        if (!read_mapping(&b, &f->mappings[i], f->channels, f->floor_count, f->residue_count, why)) return false;
    }
    f->mode_count = read_bits(&b, 6) + 1u;
    f->modes = tide_alloc_zeroed(f->mode_count, sizeof *f->modes);
    for (uint32_t i = 0; i < f->mode_count; i++) {
        f->modes[i].blockflag = read_bits(&b, 1) != 0;
        if (read_bits(&b, 16) != 0 || read_bits(&b, 16) != 0) return fail(why, "a mode's window or transform type isn't 0");
        f->modes[i].mapping = (uint8_t)read_bits(&b, 8);
        if (f->modes[i].mapping >= f->mapping_count) return fail(why, "a mode names a mapping the file hasn't got");
    }
    if (read_bits(&b, 1) != 1 || b.eop) return fail(why, "the setup header's framing bit isn't set");
    if (f->classwords_most > CLASSWORDS_MOST) f->classwords_most = CLASSWORDS_MOST;
    return true;
}

tide_vorbis *tide_vorbis_open(const uint8_t *bytes, const size_t size, const char **why)
{
    if (why) *why = "";
    packets ps = {0};
    uint64_t granule = 0;
    if (!read_pages(bytes, size, &ps, &granule, why)) {
        free(ps.data);
        free(ps.offsets);
        return NULL;
    }
    tide_vorbis *f = tide_alloc_zeroed(1, sizeof *f);
    f->frames = granule;
    bool ok = read_identification(f, ps.data + ps.offsets[0], ps.offsets[1] - ps.offsets[0], why);
    if (ok) {
        const uint8_t *c = ps.data + ps.offsets[1];
        if (ps.offsets[2] - ps.offsets[1] < 7u || c[0] != 3 || memcmp(c + 1, "vorbis", 6) != 0) {
            ok = fail(why, "the comment header isn't one");
        }
    }
    if (ok) ok = read_setup(f, ps.data + ps.offsets[2], ps.offsets[3] - ps.offsets[2], why);
    if (!ok) {
        free(ps.data);
        free(ps.offsets);
        tide_vorbis_close(f);
        return NULL;
    }
    // The audio packets: the three headers are dropped from the front
    const uint32_t audio_from = ps.offsets[3];
    f->packet_count = ps.count - 3u;
    f->offsets = tide_alloc((f->packet_count + 1u) * sizeof *f->offsets);
    for (uint32_t i = 0; i <= f->packet_count; i++) f->offsets[i] = ps.offsets[3u + i] - audio_from;
    f->packets = tide_alloc(ps.size - audio_from + 1u);
    memcpy(f->packets, ps.data + audio_from, ps.size - audio_from);
    free(ps.data);
    free(ps.offsets);
    make_transform(&f->transforms[0], f->bs0);
    if (f->bs1 != f->bs0) make_transform(&f->transforms[1], f->bs1);
    return f;
}

void tide_vorbis_close(tide_vorbis *f)
{
    if (!f) return;
    for (uint32_t i = 0; i < f->book_count; i++) free_codebook(&f->books[i]);
    free(f->books);
    free(f->floors);
    free(f->residues);
    free(f->mappings);
    free(f->modes);
    free_transform(&f->transforms[0]);
    free_transform(&f->transforms[1]);
    free(f->packets);
    free(f->offsets);
    free(f);
}

uint32_t tide_vorbis_channels(const tide_vorbis *f) { return f ? f->channels : 0; }
uint32_t tide_vorbis_rate(const tide_vorbis *f) { return f ? f->rate : 0; }
uint64_t tide_vorbis_frames(const tide_vorbis *f) { return f ? f->frames : 0; }

// ---------------------------------------------------------------------------
// Reading: a block at a time

struct tide_vorbis_reader {
    const tide_vorbis *f;
    uint32_t packet;
    bool primed;             // A block decoded, in `prev`, for the next to overlap with
    uint32_t prev_n, prev_right_start, prev_right_n;
    uint64_t produced;
    float *prev;             // channels * bs1: the last block, windowed
    float *out;              // channels * bs1 / 2 frames, interleaved
    uint32_t out_at, out_count;
    float *resid;            // channels * bs1 / 2
    float *inter;            // channels * bs1 / 2: a type 2 residue's one vector
    float *flr;              // bs1 / 2: a floor's curve
    int32_t *fy;             // bs1 / 2
    float *cre, *cim, *u;    // The transform's scratch: bs1 / 4, bs1 / 4, bs1 / 2
    float *pcm;              // bs1: one channel's block
    uint8_t *classes;        // channels * (bs1 / 2 + classwords_most)
    int32_t Y[2][FLOOR_VALUES_MOST];
    bool has_floor[2];
    bool no_residue[2];
};

size_t tide_vorbis_reader_size(const tide_vorbis *f)
{
    if (!f) return 0;
    const size_t ch = f->channels, n = f->bs1;
    return sizeof(tide_vorbis_reader) + sizeof(float) * (ch * n + ch * n / 2 + ch * n / 2 + ch * n / 2 + n / 2 + n / 4 + n / 4 + n / 2 + n) +
           sizeof(int32_t) * (n / 2) + ch * (n / 2 + f->classwords_most);
}

tide_vorbis_reader *tide_vorbis_reader_open(const tide_vorbis *f)
{
    if (!f) return NULL;
    const uint32_t ch = f->channels, n = f->bs1;
    tide_vorbis_reader *r = tide_alloc_zeroed(1, sizeof *r);
    r->f = f;
    r->prev = tide_alloc_zeroed((size_t)ch * n, sizeof(float));
    r->out = tide_alloc_zeroed((size_t)ch * n / 2, sizeof(float));
    r->resid = tide_alloc_zeroed((size_t)ch * n / 2, sizeof(float));
    r->inter = tide_alloc_zeroed((size_t)ch * n / 2, sizeof(float));
    r->flr = tide_alloc_zeroed(n / 2, sizeof(float));
    r->fy = tide_alloc_zeroed(n / 2, sizeof(int32_t));
    r->cre = tide_alloc_zeroed(n / 4, sizeof(float));
    r->cim = tide_alloc_zeroed(n / 4, sizeof(float));
    r->u = tide_alloc_zeroed(n / 2, sizeof(float));
    r->pcm = tide_alloc_zeroed(n, sizeof(float));
    r->classes = tide_alloc_zeroed((size_t)ch * (n / 2 + f->classwords_most), 1);
    return r;
}

void tide_vorbis_reader_close(tide_vorbis_reader *r)
{
    if (!r) return;
    free(r->prev);
    free(r->out);
    free(r->resid);
    free(r->inter);
    free(r->flr);
    free(r->fy);
    free(r->cre);
    free(r->cim);
    free(r->u);
    free(r->pcm);
    free(r->classes);
    free(r);
}

void tide_vorbis_rewind(tide_vorbis_reader *r)
{
    if (!r) return;
    r->packet = 0;
    r->primed = false;
    r->produced = 0;
    r->out_at = r->out_count = 0;
}

// Floor 1's values from the packet: false where the channel has no floor
// (its nonzero bit is 0), or the packet ended (b->eop says which)
static bool floor_decode(bits *b, const tide_vorbis *file, const floor1 *f, int32_t *Y)
{
    if (read_bits(b, 1) == 0) return false;
    static const uint32_t ranges[4] = {256, 128, 86, 64};
    const uint32_t range = ranges[f->multiplier - 1u];
    Y[0] = (int32_t)read_bits(b, ilog(range - 1u));
    Y[1] = (int32_t)read_bits(b, ilog(range - 1u));
    if (b->eop) return false;
    uint32_t offset = 2;
    for (uint32_t i = 0; i < f->partitions; i++) {
        const uint32_t c = f->partition_class[i];
        const uint32_t cdim = f->class_dims[c];
        const uint32_t cbits = f->class_subclasses[c];
        const uint32_t csub = (1u << cbits) - 1u;
        uint32_t cval = 0;
        if (cbits) {
            const int32_t v = decode_scalar(b, &file->books[f->class_masterbook[c]]);
            if (v < 0) return false;
            cval = (uint32_t)v;
        }
        for (uint32_t j = 0; j < cdim; j++) {
            const int32_t book = f->subclass_books[c][cval & csub];
            cval >>= cbits;
            if (book >= 0) {
                const int32_t v = decode_scalar(b, &file->books[book]);
                if (v < 0) return false;
                Y[offset + j] = v;
            } else {
                Y[offset + j] = 0;
            }
        }
        offset += cdim;
    }
    return true;
}

static int32_t render_point(const int32_t x0, const int32_t y0, const int32_t x1, const int32_t y1, const int32_t X)
{
    const int32_t dy = y1 - y0, adx = x1 - x0;
    const int32_t ady = dy < 0 ? -dy : dy;
    const int32_t err = ady * (X - x0);
    const int32_t off = err / adx;
    return dy < 0 ? y0 - off : y0 + off;
}

static void render_line(const int32_t x0, const int32_t y0, const int32_t x1, const int32_t y1, int32_t *v, const uint32_t n)
{
    const int32_t dy = y1 - y0, adx = x1 - x0;
    int32_t ady = dy < 0 ? -dy : dy;
    const int32_t base = dy / adx;
    const int32_t sy = dy < 0 ? base - 1 : base + 1;
    ady -= (base < 0 ? -base : base) * adx;
    int32_t y = y0, err = 0;
    if ((uint32_t)x0 < n) v[x0] = y;
    for (int32_t x = x0 + 1; x < x1; x++) {
        err += ady;
        if (err >= adx) {
            err -= adx;
            y += sy;
        } else {
            y += base;
        }
        if ((uint32_t)x < n) v[x] = y;
    }
}

// The floor's curve from its values, as `n2` (n/2) amplitudes
static void floor_curve(const floor1 *f, const int32_t *Y, const uint32_t n2, int32_t *fy, float *out)
{
    static float inverse_db[256];
    if (inverse_db[255] == 0.0f) {
        // The specification's table: 1 at 255, each step down a 1.0649863th
        double v = 1.0;
        for (int i = 255; i >= 0; i--) {
            inverse_db[i] = (float)v;
            v /= 1.0649863;
        }
    }
    static const int32_t ranges[4] = {256, 128, 86, 64};
    const int32_t range = ranges[f->multiplier - 1u];
    bool step2[FLOOR_VALUES_MOST];
    int32_t final[FLOOR_VALUES_MOST];
    step2[0] = step2[1] = true;
    final[0] = Y[0];
    final[1] = Y[1];
    for (uint32_t i = 2; i < f->values; i++) {
        const uint32_t lo = f->low[i], hi = f->high[i];
        const int32_t predicted = render_point(f->X[lo], final[lo], f->X[hi], final[hi], f->X[i]);
        const int32_t val = Y[i];
        const int32_t highroom = range - predicted, lowroom = predicted;
        const int32_t room = (highroom < lowroom ? highroom : lowroom) * 2;
        if (val) {
            step2[lo] = step2[hi] = true;
            step2[i] = true;
            if (val >= room) {
                final[i] = highroom > lowroom ? val - lowroom + predicted : predicted - val + highroom - 1;
            } else {
                final[i] = (val & 1) ? predicted - (val + 1) / 2 : predicted + val / 2;
            }
        } else {
            step2[i] = false;
            final[i] = predicted;
        }
        if (final[i] < 0) final[i] = 0;
        if (final[i] >= range) final[i] = range - 1;
    }
    int32_t hx = 0, lx = 0;
    int32_t ly = final[0] * f->multiplier, hy = ly;
    for (uint32_t s = 1; s < f->values; s++) {
        const uint32_t i = f->sorted[s];
        if (!step2[i]) continue;
        hy = final[i] * f->multiplier;
        hx = f->X[i];
        render_line(lx, ly, hx, hy, fy, n2);
        lx = hx;
        ly = hy;
    }
    if ((uint32_t)hx < n2) render_line(hx, hy, (int32_t)n2, hy, fy, n2);
    for (uint32_t i = 0; i < n2; i++) {
        int32_t v = fy[i];
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out[i] = inverse_db[v];
    }
}

// A residue's vectors into `v` (ch vectors of n2 floats each; for type 2 the
// one vector of ch * n2), those not marked do-not-decode. False at the packet's end.
static bool residue_decode(bits *b, const tide_vorbis *file, const residue *r, const uint32_t ch, const bool *dnd, float **v,
                           const uint32_t n2, uint8_t *classes, const uint32_t class_stride)
{
    const uint32_t actual = r->type == 2 ? n2 * ch : n2;
    const uint32_t limit_begin = r->begin < actual ? r->begin : actual;
    const uint32_t limit_end = r->end < actual ? r->end : actual;
    if (limit_end <= limit_begin) return true;
    const codebook *classbook = &file->books[r->classbook];
    const uint32_t classwords = classbook->dimensions;
    const uint32_t partitions = (limit_end - limit_begin) / r->partition_size;
    if (partitions == 0) return true;
    const uint32_t vectors = r->type == 2 ? 1 : ch;
    for (uint32_t pass = 0; pass < 8; pass++) {
        uint32_t partition = 0;
        while (partition < partitions) {
            if (pass == 0) {
                for (uint32_t j = 0; j < vectors; j++) {
                    if (dnd[j]) continue;
                    int32_t temp = decode_scalar(b, classbook);
                    if (temp < 0) return false;
                    for (uint32_t i = classwords; i-- > 0;) {
                        classes[j * class_stride + i + partition] = (uint8_t)((uint32_t)temp % r->classifications);
                        temp = (int32_t)((uint32_t)temp / r->classifications);
                    }
                }
            }
            for (uint32_t i = 0; i < classwords && partition < partitions; i++, partition++) {
                for (uint32_t j = 0; j < vectors; j++) {
                    if (dnd[j]) continue;
                    const uint32_t vqclass = classes[j * class_stride + partition];
                    const int32_t book = r->books[vqclass][pass];
                    if (book < 0) continue;
                    const codebook *c = &file->books[book];
                    float *out = v[j] + limit_begin + partition * r->partition_size;
                    const uint32_t dim = c->dimensions;
                    if (r->type == 0) {
                        const uint32_t step = r->partition_size / dim;
                        for (uint32_t s = 0; s < step; s++) {
                            const int32_t e = decode_scalar(b, c);
                            if (e < 0) return false;
                            const float *vec = c->vectors + (size_t)e * dim;
                            for (uint32_t d = 0; d < dim; d++) out[s + d * step] += vec[d];
                        }
                    } else {
                        for (uint32_t s = 0; s < r->partition_size;) {
                            const int32_t e = decode_scalar(b, c);
                            if (e < 0) return false;
                            const float *vec = c->vectors + (size_t)e * dim;
                            for (uint32_t d = 0; d < dim && s < r->partition_size; d++, s++) out[s] += vec[d];
                        }
                    }
                }
            }
        }
    }
    return true;
}

static const transform *transform_for(const tide_vorbis *f, const uint32_t n)
{
    return n == f->bs0 ? &f->transforms[0] : &f->transforms[1];
}

// Decodes the next audio packet into `out`: false when there are no more
static bool decode_block(tide_vorbis_reader *r)
{
    const tide_vorbis *f = r->f;
    const uint32_t ch = f->channels;
    for (;;) {
        if (r->packet >= f->packet_count) return false;
        const uint32_t from = f->offsets[r->packet], size = f->offsets[r->packet + 1u] - from;
        r->packet++;
        bits b = bits_over(f->packets + from, size);
        if (read_bits(&b, 1) != 0) continue; // Not an audio packet: ignored
        const uint32_t mode_index = read_bits(&b, ilog(f->mode_count - 1u));
        if (mode_index >= f->mode_count || b.eop) continue;
        const mode *md = &f->modes[mode_index];
        const uint32_t n = md->blockflag ? f->bs1 : f->bs0;
        const uint32_t n2 = n / 2;
        bool prev_long = true, next_long = true;
        if (md->blockflag) {
            prev_long = read_bits(&b, 1) != 0;
            next_long = read_bits(&b, 1) != 0;
        }
        uint32_t left_start = 0, left_n = n2, right_start = n2, right_n = n2;
        if (md->blockflag) {
            if (!prev_long) {
                left_start = n / 4 - f->bs0 / 4;
                left_n = f->bs0 / 2;
            }
            if (!next_long) {
                right_start = n * 3 / 4 - f->bs0 / 4;
                right_n = f->bs0 / 2;
            }
        }
        const uint32_t left_end = left_start + left_n;
        const mapping *mp = &f->mappings[md->mapping];
        // Floors
        bool silent = false;
        for (uint32_t c = 0; c < ch; c++) {
            const floor1 *fl = &f->floors[mp->submap_floor[mp->mux[c]]];
            r->has_floor[c] = floor_decode(&b, f, fl, r->Y[c]);
            if (b.eop) {
                silent = true; // The specification: a packet that ends in its floors is all zeros
                break;
            }
        }
        memset(r->resid, 0, (size_t)ch * n2 * sizeof(float));
        if (!silent) {
            for (uint32_t c = 0; c < ch; c++) r->no_residue[c] = !r->has_floor[c];
            for (uint32_t s = 0; s < mp->coupling_steps; s++) {
                if (!r->no_residue[mp->magnitude[s]] || !r->no_residue[mp->angle[s]]) {
                    r->no_residue[mp->magnitude[s]] = r->no_residue[mp->angle[s]] = false;
                }
            }
            const uint32_t class_stride = n2 + f->classwords_most;
            for (uint32_t s = 0; s < mp->submaps; s++) {
                float *v[2];
                bool dnd[2];
                uint32_t in_submap = 0;
                for (uint32_t c = 0; c < ch; c++) {
                    if (mp->mux[c] != s) continue;
                    v[in_submap] = r->resid + (size_t)c * n2;
                    dnd[in_submap] = r->no_residue[c];
                    in_submap++;
                }
                if (!in_submap) continue;
                const residue *rs = &f->residues[mp->submap_residue[s]];
                if (rs->type == 2) {
                    bool all = true;
                    for (uint32_t j = 0; j < in_submap; j++) all = all && dnd[j];
                    if (all) continue;
                    memset(r->inter, 0, (size_t)in_submap * n2 * sizeof(float));
                    float *one[1] = {r->inter};
                    const bool none = false;
                    const bool more = residue_decode(&b, f, rs, in_submap, &none, one, n2, r->classes, class_stride);
                    for (uint32_t i = 0; i < n2; i++) {
                        for (uint32_t j = 0; j < in_submap; j++) v[j][i] = r->inter[i * in_submap + j];
                    }
                    if (!more) break; // What's decoded stands, the rest zeros
                } else {
                    if (!residue_decode(&b, f, rs, in_submap, dnd, v, n2, r->classes, class_stride)) break;
                }
            }
            // Inverse coupling
            for (uint32_t s = mp->coupling_steps; s-- > 0;) {
                float *mag = r->resid + (size_t)mp->magnitude[s] * n2;
                float *ang = r->resid + (size_t)mp->angle[s] * n2;
                for (uint32_t i = 0; i < n2; i++) {
                    const float M = mag[i], A = ang[i];
                    float nm, na;
                    if (M > 0.0f) {
                        if (A > 0.0f) {
                            nm = M;
                            na = M - A;
                        } else {
                            na = M;
                            nm = M + A;
                        }
                    } else {
                        if (A > 0.0f) {
                            nm = M;
                            na = M + A;
                        } else {
                            na = M;
                            nm = M - A;
                        }
                    }
                    mag[i] = nm;
                    ang[i] = na;
                }
            }
        }
        // Each channel: the floor's curve over the residue, the transform, the window
        const transform *t = transform_for(f, n);
        for (uint32_t c = 0; c < ch; c++) {
            float *spec = r->resid + (size_t)c * n2;
            float *pcm = r->pcm;
            if (!silent && r->has_floor[c]) {
                const floor1 *fl = &f->floors[mp->submap_floor[mp->mux[c]]];
                floor_curve(fl, r->Y[c], n2, r->fy, r->flr);
                for (uint32_t i = 0; i < n2; i++) spec[i] *= r->flr[i];
                imdct(t, spec, pcm, r->cre, r->cim, r->u);
            } else {
                memset(pcm, 0, n * sizeof(float));
            }
            const float *left_slope = transform_for(f, left_n * 2u)->slope;
            const float *right_slope = transform_for(f, right_n * 2u)->slope;
            for (uint32_t i = 0; i < left_start; i++) pcm[i] = 0.0f;
            for (uint32_t i = 0; i < left_n; i++) pcm[left_start + i] *= left_slope[i];
            for (uint32_t i = 0; i < right_n; i++) pcm[right_start + i] *= right_slope[right_n - 1u - i];
            for (uint32_t i = right_start + right_n; i < n; i++) pcm[i] = 0.0f;
            // Overlap with the last block, and what's finished between the two centers
            float *prev = r->prev + (size_t)c * f->bs1;
            if (r->primed) {
                const uint32_t pn = r->prev_n;
                uint32_t k = 0;
                for (uint32_t i = pn / 2; i < r->prev_right_start; i++) r->out[(size_t)k++ * ch + c] = prev[i];
                const uint32_t overlap = r->prev_right_n < left_n ? r->prev_right_n : left_n;
                for (uint32_t i = 0; i < overlap; i++) r->out[(size_t)k++ * ch + c] = prev[r->prev_right_start + i] + pcm[left_start + i];
                for (uint32_t i = left_end; i < n2; i++) r->out[(size_t)k++ * ch + c] = pcm[i];
                r->out_count = k;
            }
            memcpy(prev, pcm, n * sizeof(float));
        }
        const bool had = r->primed;
        r->primed = true;
        r->prev_n = n;
        r->prev_right_start = right_start;
        r->prev_right_n = right_n;
        r->out_at = 0;
        if (!had) continue; // The first block only primes
        // Never past what the file says it holds
        if (r->produced + r->out_count > f->frames) r->out_count = (uint32_t)(f->frames - r->produced);
        if (r->out_count == 0) continue;
        return true;
    }
}

uint32_t tide_vorbis_read(tide_vorbis_reader *r, float *out, const uint32_t frames)
{
    if (!r) return 0;
    const uint32_t ch = r->f->channels;
    uint32_t got = 0;
    while (got < frames) {
        if (r->out_at == r->out_count) {
            if (r->produced >= r->f->frames) break;
            if (!decode_block(r)) break;
        }
        uint32_t n = r->out_count - r->out_at;
        if (n > frames - got) n = frames - got;
        memcpy(out + (size_t)got * ch, r->out + (size_t)r->out_at * ch, (size_t)n * ch * sizeof(float));
        r->out_at += n;
        got += n;
        r->produced += n;
    }
    return got;
}

void tide_vorbis_imdct_check(const uint32_t n, const float *x, float *y)
{
    transform t = {0};
    make_transform(&t, n);
    float *cre = tide_alloc(n / 4 * sizeof(float));
    float *cim = tide_alloc(n / 4 * sizeof(float));
    float *u = tide_alloc(n / 2 * sizeof(float));
    imdct(&t, x, y, cre, cim, u);
    free(cre);
    free(cim);
    free(u);
    free_transform(&t);
}

bool tide_vorbis_decode(const uint8_t *bytes, const size_t size, tide_sound *out)
{
    *out = (tide_sound){0};
    tide_vorbis *f = tide_vorbis_open(bytes, size, NULL);
    if (!f) return false;
    tide_vorbis_reader *r = tide_vorbis_reader_open(f);
    const uint32_t ch = f->channels;
    uint32_t capacity = f->frames < UINT32_MAX / 4u ? (uint32_t)f->frames + 1u : 1u;
    float *samples = tide_alloc((size_t)capacity * ch * sizeof(float));
    uint32_t frames = 0;
    for (;;) {
        if (frames == capacity) {
            samples = tide_realloc(samples, (size_t)capacity * ch * sizeof(float), (size_t)capacity * 2u * ch * sizeof(float));
            capacity *= 2u;
        }
        const uint32_t got = tide_vorbis_read(r, samples + (size_t)frames * ch, capacity - frames);
        if (!got) break;
        frames += got;
    }
    *out = (tide_sound){.samples = samples, .frames = frames, .channels = ch, .rate = f->rate};
    tide_vorbis_reader_close(r);
    tide_vorbis_close(f);
    return true;
}
