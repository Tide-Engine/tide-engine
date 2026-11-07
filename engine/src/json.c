#include "tide/json.h"

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Numbers out of text: a number's digits and its power of ten, read once,
// then made an int, float or double by exact integer arithmetic. No strtod: the
// same text is the same bits on every platform.

// Digits kept. Binary64 midpoint decimals need at most 768 significant digits
// (binary32 at most 112); beyond this bound only a nonzero tail matters.
#define DIGITS 800

typedef struct number {
    bool negative;
    bool more;
    int32_t count;
    int64_t exponent; // The value is the digits, as an integer, times ten to this
    uint8_t digits[DIGITS];
} number;

static bool is_digit(const uint8_t c)
{
    return c >= '0' && c <= '9';
}

static void take_digit(number *n, const uint8_t d, const bool fraction)
{
    if (n->count == 0 && d == 0) {
        if (fraction) n->exponent--;
        return;
    }
    if (n->count < DIGITS) {
        n->digits[n->count++] = d;
        if (fraction) n->exponent--;
        return;
    }
    n->more |= d != 0;
    if (!fraction) n->exponent++;
}

// The number at `p`, and where it ends, or NULL when there's none. `strict`
// is JSON's: digits before a point and after one, and no zero before another
// digit. Otherwise `5.` and `.5` are numbers too, and zeros go before any.
static const uint8_t *scan_number(const uint8_t *p, const uint8_t *end, const bool strict, number *n)
{
    n->negative = false;
    n->more = false;
    n->count = 0;
    n->exponent = 0;
    if (p < end && *p == '-') {
        n->negative = true;
        p++;
    }
    const uint8_t *whole = p;
    while (p < end && is_digit(*p)) take_digit(n, (uint8_t)(*p++ - '0'), false);
    const ptrdiff_t whole_digits = p - whole;
    if (strict && (whole_digits == 0 || (whole_digits > 1 && *whole == '0'))) return NULL;
    ptrdiff_t fraction_digits = 0;
    if (p < end && *p == '.') {
        p++;
        const uint8_t *fraction = p;
        while (p < end && is_digit(*p)) take_digit(n, (uint8_t)(*p++ - '0'), true);
        fraction_digits = p - fraction;
        if (strict && fraction_digits == 0) return NULL;
    }
    if (whole_digits + fraction_digits == 0) return NULL;
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool down = false;
        if (p < end && (*p == '+' || *p == '-')) down = *p++ == '-';
        const uint8_t *digits = p;
        int64_t e = 0;
        for (; p < end && is_digit(*p); p++) {
            if (e < 100000000) e = e * 10 + (*p - '0'); // Past this it's zero or too big whatever follows
        }
        if (p == digits) return NULL;
        n->exponent += down ? -e : e;
    }
    return p;
}

// `n` as an integer from `least` to `most`: false for one that isn't whole,
// or is outside them.
static bool number_int(const number *n, const int64_t least, const int64_t most, int64_t *out)
{
    if (n->more) return false; // More than 128 digits that count: no long, or not whole
    int32_t count = n->count;
    int64_t exponent = n->exponent;
    while (count > 0 && n->digits[count - 1] == 0) { // 3.0, 300e-2
        count--;
        exponent++;
    }
    int64_t v = 0;
    if (count > 0) {
        if (exponent < 0 || count + exponent > 19) return false;
        uint64_t u = 0; // 19 digits at the most, which 64 bits hold
        for (int32_t i = 0; i < count; i++) u = u * 10u + n->digits[i];
        for (int64_t e = 0; e < exponent; e++) u *= 10u;
        const uint64_t limit = (uint64_t)INT64_MAX + (n->negative ? 1u : 0u);
        if (u > limit) return false;
        v = n->negative ? (u == limit ? INT64_MIN : -(int64_t)u) : (int64_t)u;
    }
    if (v < least || v > most) return false;
    *out = v;
    return true;
}

// Bounded integers for the largest retained binary64 decimal and its power
// of ten. 4096 bits cover 801 retained digits with the accepted exponent range.
#define WIDE_WORDS 128

typedef struct wide {
    int n;                    // Words in use; the top one is nonzero
    uint32_t w[WIDE_WORDS]; // Least significant first
} wide;

static void wide_set(wide *b, const uint32_t v)
{
    b->w[0] = v;
    b->n = v != 0;
}

// b = b * m + add
static void wide_mul_add(wide *b, const uint32_t m, const uint32_t add)
{
    uint64_t carry = add;
    for (int i = 0; i < b->n; i++) {
        const uint64_t t = (uint64_t)b->w[i] * m + carry;
        b->w[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry && b->n < WIDE_WORDS) b->w[b->n++] = (uint32_t)carry;
}

static int wide_bits(const wide *b)
{
    if (b->n == 0) return 0;
    return 32 * b->n - __builtin_clz(b->w[b->n - 1]);
}

static void wide_shl(wide *b, const int bits)
{
    if (b->n == 0 || bits <= 0) return;
    const int words = bits / 32;
    const int rest = bits % 32;
    int n = b->n + words + 1;
    if (n > WIDE_WORDS) n = WIDE_WORDS;
    for (int i = n - 1; i >= 0; i--) {
        const int from = i - words;
        const uint32_t high = from >= 0 && from < b->n ? b->w[from] : 0u;
        const uint32_t low = from - 1 >= 0 && from - 1 < b->n ? b->w[from - 1] : 0u;
        b->w[i] = rest ? (high << rest) | (low >> (32 - rest)) : high;
    }
    b->n = n;
    while (b->n > 0 && b->w[b->n - 1] == 0) b->n--;
}

static int wide_cmp(const wide *a, const wide *b)
{
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--) {
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    }
    return 0;
}

// a -= b, where a >= b.
static void wide_sub(wide *a, const wide *b)
{
    uint32_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        const uint64_t t = (uint64_t)a->w[i] - (i < b->n ? b->w[i] : 0u) - borrow;
        a->w[i] = (uint32_t)t;
        borrow = (uint32_t)(t >> 63);
    }
    while (a->n > 0 && a->w[a->n - 1] == 0) a->n--;
}

// `n` as the float nearest it, a tie going to the even one, as IEEE 754
// rounds: false for a number past the floats, which would be an infinity.
// The number is a fraction of two integers, num / den, which long division
// takes a float's 24 bits from, and one more to round by; whether anything is
// left then says which side of halfway it is.
static bool number_float(const number *n, float *out)
{
    uint32_t bits = 0;
    const int64_t magnitude = n->count + n->exponent; // The number is under ten to this, and at least a tenth of that
    if (n->count > 0 && magnitude > 39) return false;  // Past 3.4e38, the biggest float
    if (n->count > 0 && magnitude >= -46) {            // Under 1e-46 it's nearest to zero: the smallest float is 1.4e-45
        wide num, den;
        int64_t exponent = n->exponent;
        wide_set(&num, 0);
        for (int32_t i = 0; i < n->count; i++) wide_mul_add(&num, 10u, n->digits[i]);
        if (n->more) { // What wasn't kept, as one more digit: it's never exactly halfway then
            wide_mul_add(&num, 10u, 1u);
            exponent--;
        }
        wide_set(&den, 1u);
        for (int64_t e = 0; e < exponent; e++) wide_mul_add(&num, 10u, 0u);
        for (int64_t e = exponent; e < 0; e++) wide_mul_add(&den, 10u, 0u);
        // num / den as x * 2^k, x from 1 up to 2
        int k = wide_bits(&num) - wide_bits(&den);
        if (k >= 0) wide_shl(&den, k);
        else wide_shl(&num, -k);
        if (wide_cmp(&num, &den) < 0) {
            wide_shl(&num, 1);
            k--;
        }
        // The float is m * 2^q: 24 bits of m, or fewer for one under the smallest normal float
        int q = k - 23 > -149 ? k - 23 : -149;
        const int take = k - q + 1;
        if (take >= 0) {
            uint32_t m = 0;
            bool round = false;
            for (int i = 0; i <= take; i++) {
                const bool bit = wide_cmp(&num, &den) >= 0;
                if (bit) wide_sub(&num, &den);
                if (i < take) m = m << 1 | (bit ? 1u : 0u);
                else round = bit;
                wide_shl(&num, 1);
            }
            if (round && (num.n != 0 || (m & 1u))) m++;
            if (m == 1u << 24) {
                m >>= 1;
                q++;
            }
            if (m >= 1u << 23) {
                const int biased = q + 150;
                if (biased >= 255) return false;
                bits = (uint32_t)biased << 23 | (m & 0x7FFFFFu);
            } else {
                bits = m; // Under the smallest normal float
            }
        }
    }
    if (n->negative) bits |= 0x80000000u;
    memcpy(out, &bits, sizeof bits);
    return true;
}

static bool number_double(const number *n, double *out)
{
    uint64_t bits = 0;
    const int64_t magnitude = n->count + n->exponent; // The number is under ten to this, and at least a tenth of that
    if (n->count > 0 && magnitude > 309) return false;  // Past the largest finite binary64 value
    if (n->count > 0 && magnitude >= -324) {            // The smallest double is about 4.94e-324
        wide num, den;
        int64_t exponent = n->exponent;
        wide_set(&num, 0);
        for (int32_t i = 0; i < n->count; i++) wide_mul_add(&num, 10u, n->digits[i]);
        if (n->more) { // What wasn't kept, as one more digit: it's never exactly halfway then
            wide_mul_add(&num, 10u, 1u);
            exponent--;
        }
        wide_set(&den, 1u);
        for (int64_t e = 0; e < exponent; e++) wide_mul_add(&num, 10u, 0u);
        for (int64_t e = exponent; e < 0; e++) wide_mul_add(&den, 10u, 0u);
        // num / den as x * 2^k, x from 1 up to 2
        int k = wide_bits(&num) - wide_bits(&den);
        if (k >= 0) wide_shl(&den, k);
        else wide_shl(&num, -k);
        if (wide_cmp(&num, &den) < 0) {
            wide_shl(&num, 1);
            k--;
        }
        // The double is m * 2^q: 53 bits of m, or fewer for one under the smallest normal double
        int q = k - 52 > -1074 ? k - 52 : -1074;
        const int take = k - q + 1;
        if (take >= 0) {
            uint64_t m = 0;
            bool round = false;
            for (int i = 0; i <= take; i++) {
                const bool bit = wide_cmp(&num, &den) >= 0;
                if (bit) wide_sub(&num, &den);
                if (i < take) m = m << 1 | (bit ? 1u : 0u);
                else round = bit;
                wide_shl(&num, 1);
            }
            if (round && (num.n != 0 || (m & 1u))) m++;
            if (m == UINT64_C(1) << 53) {
                m >>= 1;
                q++;
            }
            if (m >= UINT64_C(1) << 52) {
                const int biased = q + 1075;
                if (biased >= 2047) return false;
                bits = (uint64_t)biased << 52 | (m & UINT64_C(0x000FFFFFFFFFFFFF));
            } else {
                bits = m; // Under the smallest normal double
            }
        }
    }
    if (n->negative) bits |= UINT64_C(0x8000000000000000);
    memcpy(out, &bits, sizeof bits);
    return true;
}

bool tide_str_parse_float(const tide_str text, float *value)
{
    *value = 0.0f;
    if (text.bytes <= 0) return false;
    const uint8_t *p = (const uint8_t *)text.ptr;
    number n;
    float v;
    if (scan_number(p, p + text.bytes, false, &n) != p + text.bytes || !number_float(&n, &v)) return false;
    *value = v;
    return true;
}

bool tide_str_parse_double(const tide_str text, double *value)
{
    *value = 0.0;
    if (text.bytes <= 0) return false;
    const uint8_t *p = (const uint8_t *)text.ptr;
    number n;
    double v;
    if (scan_number(p, p + text.bytes, false, &n) != p + text.bytes || !number_double(&n, &v)) return false;
    *value = v;
    return true;
}

bool tide_str_parse_long(const tide_str text, int64_t *value)
{
    *value = 0;
    const uint8_t *p = (const uint8_t *)text.ptr;
    const uint8_t *end = p + (text.bytes > 0 ? text.bytes : 0);
    const bool negative = p < end && *p == '-';
    if (negative) p++;
    if (p == end) return false;
    const uint64_t limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
    uint64_t u = 0;
    for (; p < end; p++) {
        if (!is_digit(*p)) return false;
        const uint32_t d = (uint32_t)(*p - '0');
        if (u > (limit - d) / 10u) return false;
        u = u * 10u + d;
    }
    *value = negative ? (u == limit ? INT64_MIN : -(int64_t)u) : (int64_t)u;
    return true;
}

bool tide_str_parse_int(const tide_str text, int32_t *value)
{
    int64_t v;
    *value = 0;
    if (!tide_str_parse_long(text, &v) || v < INT32_MIN || v > INT32_MAX) return false;
    *value = (int32_t)v;
    return true;
}

// ---------------------------------------------------------------------------
// UTF-8

static bool continues(const uint8_t c)
{
    return (c & 0xC0u) == 0x80u;
}

// How many bytes the character at `p` has, or 0 when what's there is no
// character's UTF-8: a byte that starts none, too few after it, a longer form
// than the shortest, half of a UTF-16 pair, or past U+10FFFF.
static int utf8_length(const uint8_t *p, const uint8_t *end)
{
    const uint32_t lead = p[0];
    if (lead < 0x80u) return 1;
    const ptrdiff_t left = end - p;
    if (lead >= 0xC2u && lead < 0xE0u) return left >= 2 && continues(p[1]) ? 2 : 0;
    if (lead >= 0xE0u && lead < 0xF0u) {
        if (left < 3 || !continues(p[1]) || !continues(p[2])) return 0;
        if ((lead == 0xE0u && p[1] < 0xA0u) || (lead == 0xEDu && p[1] >= 0xA0u)) return 0;
        return 3;
    }
    if (lead >= 0xF0u && lead < 0xF5u) {
        if (left < 4 || !continues(p[1]) || !continues(p[2]) || !continues(p[3])) return 0;
        if ((lead == 0xF0u && p[1] < 0x90u) || (lead == 0xF4u && p[1] >= 0x90u)) return 0;
        return 4;
    }
    return 0;
}

bool tide_utf8_valid(const char *bytes, const int32_t count)
{
    const uint8_t *p = (const uint8_t *)bytes;
    const uint8_t *end = p + (count > 0 ? count : 0);
    while (p < end) {
        const int n = utf8_length(p, end);
        if (!n) return false;
        p += n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reading

static bool failed(tide_json *j)
{
    j->failed = true;
    return false;
}

static void skip_space(tide_json *j)
{
    while (j->at < j->end && (*j->at == ' ' || *j->at == '\t' || *j->at == '\n' || *j->at == '\r')) j->at++;
}

// Counts `bytes` of what the read makes: false, failed, past what one makes.
static bool spend(tide_json *j, const uint32_t bytes)
{
    j->made += bytes;
    return j->made <= TIDE_JSON_ROOM || failed(j);
}

static void begin(tide_json *j, const uint8_t *bytes, const int32_t count)
{
    *j = (tide_json){0};
    if (count < 0 || (uint32_t)count >= TIDE_JSON_LIMIT) {
        j->failed = true;
        return;
    }
    j->at = bytes;
    j->end = bytes + count;
    if (count >= 3 && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu) j->at += 3; // A byte order mark
}

void tide_json_begin(tide_json *j, const tide_str text)
{
    begin(j, (const uint8_t *)text.ptr, text.bytes);
}

void tide_json_begin_bytes(tide_json *j, const tide_list bytes)
{
    const int32_t count = tide_list_count(bytes);
    if (count <= 0 || (uint32_t)count >= TIDE_JSON_LIMIT) {
        begin(j, (const uint8_t *)"", count > 0 ? -1 : 0);
        return;
    }
    // A list in the scratch area or the pack has its bytes side by side; a world's can be in chunks
    const uint8_t *side_by_side = tide_handle_where(bytes.at) == TIDE_IN_SCRATCH ? tide_list_at(bytes, 0, 1) : NULL;
    if (!side_by_side) {
        uint8_t *copy = tide_scratch_memory((size_t)count);
        for (int32_t i = 0; i < count; i++) {
            const uint8_t *b = tide_list_at(bytes, i, 1);
            copy[i] = b ? *b : 0;
        }
        side_by_side = copy;
    }
    begin(j, side_by_side, count);
}

bool tide_json_end(tide_json *j)
{
    if (j->failed) return false;
    skip_space(j);
    return (j->at == j->end && j->depth == 0) || failed(j);
}

static bool word(tide_json *j, const char *word)
{
    if (j->failed) return false;
    skip_space(j);
    const size_t n = strlen(word);
    if ((size_t)(j->end - j->at) < n || memcmp(j->at, word, n) != 0) return false;
    j->at += n;
    return true;
}

bool tide_json_null(tide_json *j)
{
    return word(j, "null");
}

bool tide_json_bool(tide_json *j, bool *v)
{
    if (word(j, "true")) {
        *v = true;
        return true;
    }
    if (word(j, "false")) {
        *v = false;
        return true;
    }
    return failed(j);
}

bool tide_json_number_next(tide_json *j)
{
    if (j->failed) return false;
    skip_space(j);
    return j->at < j->end && (*j->at == '-' || is_digit(*j->at));
}

bool tide_json_int(tide_json *j, const int64_t least, const int64_t most, int64_t *v)
{
    if (j->failed) return false;
    skip_space(j);
    number n;
    const uint8_t *after = scan_number(j->at, j->end, true, &n);
    if (!after || !number_int(&n, least, most, v)) return failed(j);
    j->at = after;
    return true;
}

bool tide_json_float(tide_json *j, float *v)
{
    if (j->failed) return false;
    skip_space(j);
    number n;
    float f;
    const uint8_t *after = scan_number(j->at, j->end, true, &n);
    if (!after || !number_float(&n, &f)) return failed(j);
    *v = f;
    j->at = after;
    return true;
}

bool tide_json_double(tide_json *j, double *v)
{
    if (j->failed) return false;
    skip_space(j);
    number n;
    double f;
    const uint8_t *after = scan_number(j->at, j->end, true, &n);
    if (!after || !number_double(&n, &f)) return failed(j);
    *v = f;
    j->at = after;
    return true;
}

// The quote that closes the string opening at `j->at`, or NULL when the text
// ends first. What's between them ends with no lone backslash.
static const uint8_t *string_close(const tide_json *j)
{
    const uint8_t *p = j->at + 1;
    while (p < j->end && *p != '"') {
        if (*p == '\\') {
            if (j->end - p < 2) return NULL;
            p += 2;
        } else {
            p++;
        }
    }
    return p < j->end ? p : NULL;
}

static bool hex4(const uint8_t *p, const uint8_t *end, uint32_t *out)
{
    if (end - p < 4) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t c = p[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A') + 10u;
        else return false;
        v = v << 4 | d;
    }
    *out = v;
    return true;
}

// A string's characters, from `p` up to its closing quote, as UTF-8 into the
// `room` bytes at `out`: the escapes worked out, a \u pair as its one
// character. `*bytes` is how many bytes they are, whether or not they all
// fit, and `*chars` how many characters. False for what JSON has no string
// of: a control character, an escape there's none of, half a pair, or bytes
// that are no character's UTF-8. What's decoded is never longer than what's
// written.
static bool decode_string(const uint8_t *p, const uint8_t *close, uint8_t *out, const uint32_t room, uint32_t *bytes,
                          uint32_t *chars)
{
    uint32_t n = 0;
    uint32_t count = 0;
    while (p < close) {
        const uint8_t c = *p;
        if (c < 0x20u) return false;
        if (c != '\\') {
            const int length = utf8_length(p, close);
            if (!length) return false;
            if (n + (uint32_t)length <= room) memcpy(out + n, p, (size_t)length);
            n += (uint32_t)length;
            p += length;
            count++;
            continue;
        }
        if (close - p < 2) return false;
        uint32_t code;
        p += 2;
        switch (p[-1]) {
        case '"': code = '"'; break;
        case '\\': code = '\\'; break;
        case '/': code = '/'; break;
        case 'b': code = '\b'; break;
        case 'f': code = '\f'; break;
        case 'n': code = '\n'; break;
        case 'r': code = '\r'; break;
        case 't': code = '\t'; break;
        case 'u':
            if (!hex4(p, close, &code)) return false;
            p += 4;
            if (code >= 0xDC00u && code < 0xE000u) return false; // The second half of a pair, with no first
            if (code >= 0xD800u && code < 0xDC00u) {
                uint32_t low;
                if (close - p < 6 || p[0] != '\\' || p[1] != 'u' || !hex4(p + 2, close, &low) || low < 0xDC00u || low >= 0xE000u) {
                    return false;
                }
                p += 6;
                code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            }
            break;
        default: return false;
        }
        char utf8[4];
        const int length = tide_utf8_encode(code, utf8);
        if (n + (uint32_t)length <= room) memcpy(out + n, utf8, (size_t)length);
        n += (uint32_t)length;
        count++;
    }
    *bytes = n;
    *chars = count;
    return true;
}

static bool string_next(tide_json *j, const uint8_t **close)
{
    if (j->failed) return false;
    skip_space(j);
    if (j->at >= j->end || *j->at != '"') return failed(j);
    *close = string_close(j);
    return *close != NULL || failed(j);
}

bool tide_json_text(tide_json *j, tide_text *v)
{
    const uint8_t *close;
    if (!string_next(j, &close)) return false;
    const uint32_t written = (uint32_t)(close - (j->at + 1));
    *v = (tide_text){0};
    if (written > 0) {
        if (!spend(j, written + (uint32_t)sizeof(tide_block) + 1u)) return false;
        // A block as text in the scratch area has (tide_text_temp): its bytes and characters, the text, a NUL
        tide_handle at;
        tide_block *b = tide_scratch_block(written + 1u, "text", &at);
        uint8_t *out = (uint8_t *)(b + 1);
        uint32_t bytes, chars;
        if (!decode_string(j->at + 1, close, out, written, &bytes, &chars)) return failed(j);
        out[bytes] = 0;
        b->a = bytes;
        b->b = chars;
        v->at = at;
    }
    j->at = close + 1;
    return true;
}

bool tide_json_char(tide_json *j, int32_t *v)
{
    const uint8_t *close;
    if (!string_next(j, &close)) return false;
    uint8_t utf8[4] = {0};
    uint32_t bytes, chars;
    if (!decode_string(j->at + 1, close, utf8, sizeof utf8, &bytes, &chars) || chars > 1) return failed(j);
    uint32_t code = 0;
    if (bytes == 1) code = utf8[0];
    else if (bytes == 2) code = (utf8[0] & 0x1Fu) << 6 | (utf8[1] & 0x3Fu);
    else if (bytes == 3) code = (utf8[0] & 0x0Fu) << 12 | (utf8[1] & 0x3Fu) << 6 | (utf8[2] & 0x3Fu);
    else if (bytes == 4) code = (utf8[0] & 0x07u) << 18 | (utf8[1] & 0x3Fu) << 12 | (utf8[2] & 0x3Fu) << 6 | (utf8[3] & 0x3Fu);
    *v = (int32_t)code;
    j->at = close + 1;
    return true;
}

// The longest key or name told apart from the others: a longer one is none of them.
#define NAME_BYTES 128u

static bool read_name(tide_json *j, const char *const *names, const int32_t count, int32_t *v)
{
    const uint8_t *close;
    if (!string_next(j, &close)) return false;
    uint8_t name[NAME_BYTES];
    uint32_t bytes, chars;
    if (!decode_string(j->at + 1, close, name, sizeof name, &bytes, &chars)) return failed(j);
    j->at = close + 1;
    *v = TIDE_JSON_UNKNOWN;
    for (int32_t i = 0; i < count && bytes <= sizeof name; i++) {
        if (strlen(names[i]) == bytes && memcmp(names[i], name, bytes) == 0) {
            *v = i;
            break;
        }
    }
    return true;
}

bool tide_json_name(tide_json *j, const char *const *names, const int32_t count, int32_t *v)
{
    return read_name(j, names, count, v) && (*v != TIDE_JSON_UNKNOWN || failed(j));
}

static bool open(tide_json *j, const uint8_t bracket)
{
    if (j->failed) return false;
    skip_space(j);
    if (j->at >= j->end || *j->at != bracket || j->depth == TIDE_JSON_DEPTH) return failed(j);
    j->first[j->depth++] = true;
    j->at++;
    return true;
}

// Whether the array or object the reader is in has something next: its first,
// or one after a comma. Reads its closing bracket when it has none.
static bool next_in(tide_json *j, const uint8_t close)
{
    if (j->failed) return false;
    if (j->depth == 0) return failed(j);
    skip_space(j);
    if (j->at < j->end && *j->at == close) {
        j->at++;
        j->depth--;
        return false;
    }
    bool *first = &j->first[j->depth - 1];
    if (*first) {
        *first = false;
        return true;
    }
    if (j->at < j->end && *j->at == ',') {
        j->at++;
        return true;
    }
    return failed(j);
}

static bool colon(tide_json *j)
{
    if (j->failed) return false;
    skip_space(j);
    if (j->at >= j->end || *j->at != ':') return failed(j);
    j->at++;
    return true;
}

bool tide_json_array(tide_json *j)
{
    return open(j, '[');
}

bool tide_json_item(tide_json *j, const uint32_t bytes)
{
    return next_in(j, ']') && spend(j, bytes);
}

bool tide_json_object(tide_json *j)
{
    return open(j, '{');
}

int32_t tide_json_field(tide_json *j, const char *const *names, const int32_t count)
{
    int32_t which;
    if (!next_in(j, '}') || !read_name(j, names, count, &which) || !colon(j)) return TIDE_JSON_DONE;
    return which;
}

bool tide_json_key(tide_json *j, tide_text *key, const uint32_t bytes)
{
    return next_in(j, '}') && tide_json_text(j, key) && colon(j) && spend(j, bytes);
}

bool tide_json_skip(tide_json *j)
{
    if (j->failed) return false;
    skip_space(j);
    if (j->at >= j->end) return failed(j);
    switch (*j->at) {
    case '{':
        if (!open(j, '{')) return false; // No deeper than TIDE_JSON_DEPTH, so neither is this
        while (next_in(j, '}')) {
            int32_t ignored;
            if (!read_name(j, NULL, 0, &ignored) || !colon(j) || !tide_json_skip(j)) return false;
        }
        return !j->failed;
    case '[':
        if (!open(j, '[')) return false;
        while (next_in(j, ']')) {
            if (!tide_json_skip(j)) return false;
        }
        return !j->failed;
    case '"': {
        int32_t ignored;
        return read_name(j, NULL, 0, &ignored);
    }
    case 't': return word(j, "true") || failed(j);
    case 'f': return word(j, "false") || failed(j);
    case 'n': return word(j, "null") || failed(j);
    default: {
        number n;
        const uint8_t *after = scan_number(j->at, j->end, true, &n);
        if (!after) return failed(j);
        j->at = after;
        return true;
    }
    }
}

// ---------------------------------------------------------------------------
// Writing

tide_str tide_json_add_text(tide_str a, const tide_str text)
{
    static const char hex[] = "0123456789abcdef";
    char buf[256];
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)text.ptr;
    const uint8_t *end = p + (text.bytes > 0 ? text.bytes : 0);
    buf[n++] = '"';
    while (p < end) {
        if (n > sizeof buf - 8u) { // Room for the longest of what follows, an escape of six
            a = tide_str_add(a, (tide_str){buf, (int32_t)n, tide_utf8_chars(buf, (int32_t)n)});
            n = 0;
        }
        const uint8_t c = *p;
        if (c == '"' || c == '\\') {
            buf[n++] = '\\';
            buf[n++] = (char)c;
            p++;
        } else if (c < 0x20u) {
            const char *known = c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : c == '\b' ? "\\b" : c == '\f' ? "\\f" : NULL;
            if (known) {
                buf[n++] = known[0];
                buf[n++] = known[1];
            } else {
                memcpy(buf + n, "\\u00", 4);
                buf[n + 4] = hex[c >> 4];
                buf[n + 5] = hex[c & 15u];
                n += 6;
            }
            p++;
        } else {
            const int length = utf8_length(p, end);
            if (length) {
                memcpy(buf + n, p, (size_t)length);
                n += (uint32_t)length;
                p += length;
            } else { // No character's bytes: U+FFFD for the byte and what continues it, as text reads them
                memcpy(buf + n, "\xEF\xBF\xBD", 3);
                n += 3;
                p++;
                while (p < end && continues(*p)) p++;
            }
        }
    }
    buf[n++] = '"';
    return tide_str_add(a, (tide_str){buf, (int32_t)n, tide_utf8_chars(buf, (int32_t)n)});
}

tide_str tide_json_add_char(const tide_str a, const int32_t c)
{
    char utf8[4];
    const int n = c > 0 ? tide_utf8_encode((uint32_t)c, utf8) : 0;
    return tide_json_add_text(a, (tide_str){utf8, n, n > 0});
}

tide_str tide_json_add_float(const tide_str a, const float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u) return tide_str_add_cstr(a, "null"); // NaN and the infinities
    if (bits == 0x80000000u) return tide_str_add_cstr(a, "-0");
    return tide_str_add_float(a, v, 0);
}

tide_str tide_json_add_double(const tide_str a, const double v)
{
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    if ((bits & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)) return tide_str_add_cstr(a, "null");
    if (bits == UINT64_C(0x8000000000000000)) return tide_str_add_cstr(a, "-0");
    return tide_str_add_double(a, v, 0);
}
