#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "tide/text.h"
#include "tide_test.h"

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

static tide_str f(const float v, const int32_t format)
{
    return tide_str_add_float(TIDE_STR_EMPTY, v, format);
}

static tide_str i(const int32_t v, const int32_t format)
{
    return tide_str_add_int(TIDE_STR_EMPTY, v, format);
}

TIDE_TEST(text_floats_shortest)
{
    const uint64_t mark = tide_scratch_mark();
    TIDE_CHECK(is(f(0.0f, 0), "0"));
    TIDE_CHECK(is(f(-0.0f, 0), "0"));
    TIDE_CHECK(is(f(1.0f, 0), "1"));
    TIDE_CHECK(is(f(0.1f, 0), "0.1"));
    TIDE_CHECK(is(f(0.3f, 0), "0.3"));
    TIDE_CHECK(is(f(-2.5f, 0), "-2.5"));
    TIDE_CHECK(is(f(100.0f, 0), "100"));
    TIDE_CHECK(is(f(3.14159265f, 0), "3.1415927"));
    TIDE_CHECK(is(f(123456789.0f, 0), "123456790"));
    TIDE_CHECK(is(f(16777216.0f, 0), "16777216"));
    TIDE_CHECK(is(f(1e-6f, 0), "0.000001"));
    TIDE_CHECK(is(f(1e-7f, 0), "1E-7"));
    TIDE_CHECK(is(f(1e20f, 0), "100000000000000000000"));
    TIDE_CHECK(is(f(1e21f, 0), "1E+21"));
    TIDE_CHECK(is(f(FLT_MAX, 0), "3.4028235E+38"));
    TIDE_CHECK(is(f(FLT_MIN, 0), "1.1754944E-38"));
    TIDE_CHECK(is(f(1.4e-45f, 0), "1E-45"));
    TIDE_CHECK(is(f(0.2f + 0.1f, 0), "0.3"));
    tide_scratch_reset(mark);
}

TIDE_TEST(text_floats_read_back)
{
    // Every text reads back as the same float: a sweep over exponents and mantissas.
    const uint64_t mark = tide_scratch_mark();
    for (uint32_t bits = 1u; bits < 0x7F800000u; bits += 0x00F0F0F1u) {
        float v;
        memcpy(&v, &bits, sizeof v);
        const tide_str s = f(v, 0);
        char text[64];
        memcpy(text, s.ptr, (size_t)s.bytes);
        text[s.bytes] = '\0';
        const float back = strtof(text, NULL);
        TIDE_CHECK(memcmp(&back, &v, sizeof v) == 0);
        tide_scratch_reset(mark);
    }
}

TIDE_TEST(text_floats_fixed)
{
    const uint64_t mark = tide_scratch_mark();
    const int32_t f2 = TIDE_FORMAT('F', 2);
    TIDE_CHECK(is(f(1.5f, f2), "1.50"));
    TIDE_CHECK(is(f(2.675f, f2), "2.67")); // 2.67499995... as a float
    TIDE_CHECK(is(f(0.125f, f2), "0.13"));  // An exact half goes away from zero
    TIDE_CHECK(is(f(-0.125f, f2), "-0.13"));
    TIDE_CHECK(is(f(-0.001f, f2), "0.00"));
    TIDE_CHECK(is(f(1234.5f, TIDE_FORMAT('F', 0)), "1235"));
    TIDE_CHECK(is(f(0.5f, TIDE_FORMAT('F', 3)), "0.500"));
    TIDE_CHECK(is(f(1e10f, TIDE_FORMAT('F', 1)), "10000000000.0"));
    TIDE_CHECK(is(f(1.0f / 3.0f, TIDE_FORMAT('F', 9)), "0.333333343"));
    tide_scratch_reset(mark);
}

TIDE_TEST(text_ints)
{
    const uint64_t mark = tide_scratch_mark();
    TIDE_CHECK(is(i(0, 0), "0"));
    TIDE_CHECK(is(i(-42, 0), "-42"));
    TIDE_CHECK(is(i(INT32_MIN, 0), "-2147483648"));
    TIDE_CHECK(is(i(42, TIDE_FORMAT('D', 5)), "00042"));
    TIDE_CHECK(is(i(-42, TIDE_FORMAT('D', 5)), "-00042"));
    TIDE_CHECK(is(i(255, TIDE_FORMAT('X', 0)), "FF"));
    TIDE_CHECK(is(i(255, TIDE_FORMAT('x', 4)), "00ff"));
    TIDE_CHECK(is(i(-1, TIDE_FORMAT('X', 0)), "FFFFFFFF"));
    TIDE_CHECK(is(i(5, TIDE_FORMAT('F', 2)), "5.00"));
    tide_scratch_reset(mark);
}

TIDE_TEST(text_joining)
{
    const uint64_t mark = tide_scratch_mark();
    tide_str x = tide_str_add(TIDE_STR_EMPTY, tide_str_from_cstr("ab"));
    const tide_str y = x; // Grows in place below: y keeps its own length
    x = tide_str_add_cstr(x, "c");
    x = tide_str_add_int(x, 7, 0);
    TIDE_CHECK(is(x, "abc7") && x.chars == 4);
    TIDE_CHECK(is(y, "ab"));
    TIDE_CHECK(strcmp(tide_str_c(y), "ab") == 0); // A copy: the byte after it isn't a NUL anymore
    TIDE_CHECK(strcmp(tide_str_c(x), "abc7") == 0);
    TIDE_CHECK(is(tide_str_add_bool(TIDE_STR_EMPTY, false), "false"));
    TIDE_CHECK(is(tide_str_add_f2(TIDE_STR_EMPTY, tide_f2(1.0f, 0.5f), 0), "(1, 0.5)"));
    TIDE_CHECK(is(tide_str_add_color(TIDE_STR_EMPTY, TIDE_COLOR_RED, 0), "RGBA(1, 0, 0, 1)"));
    TIDE_CHECK(is(tide_str_add_entity(TIDE_STR_EMPTY, (tide_entity){3, 1}, false), "Entity(3:1)"));
    TIDE_CHECK(is(tide_str_add_entity(TIDE_STR_EMPTY, (tide_entity){0, TIDE_ENTITY_TEMPORARY | 5u}, false), "Entity(new)"));
    TIDE_CHECK(is(tide_str_add_player(TIDE_STR_EMPTY, tide_player_from_index(0)), "PlayerID(0)"));
    tide_scratch_reset(mark);
}

TIDE_TEST(text_characters)
{
    const uint64_t mark = tide_scratch_mark();
    const tide_str word = tide_str_from_cstr("h\xC3\xA9llo");
    TIDE_CHECK(word.chars == 5 && word.bytes == 6);
    TIDE_CHECK(is(tide_str_substring(word, 1, 2), "\xC3\xA9l"));
    TIDE_CHECK(is(tide_str_substring(word, 3, 99), "lo"));
    TIDE_CHECK(is(tide_str_substring(word, -5, 1), "h"));
    TIDE_CHECK(is(tide_str_substring_from(word, 9), ""));
    TIDE_CHECK(tide_str_index_of(word, tide_str_from_cstr("l")) == 2);
    TIDE_CHECK(tide_str_index_of(word, tide_str_from_cstr("z")) == -1);
    TIDE_CHECK(tide_str_contains(word, tide_str_from_cstr("\xC3\xA9")));
    TIDE_CHECK(tide_str_starts_with(word, tide_str_from_cstr("h")));
    TIDE_CHECK(tide_str_ends_with(word, tide_str_from_cstr("lo")));
    TIDE_CHECK(is(tide_str_to_upper(word), "H\xC3\xA9LLO"));
    TIDE_CHECK(is(tide_str_trim(tide_str_from_cstr("  hi \n")), "hi"));
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("a-b-c"), tide_str_from_cstr("-"), tide_str_from_cstr("+=")), "a+=b+=c"));
    TIDE_CHECK(tide_str_eq(tide_str_from_cstr("cat"), tide_str_add_cstr(TIDE_STR_EMPTY, "cat")));
    tide_scratch_reset(mark);
}

TIDE_TEST(text_chars_by_index_and_in_turn)
{
    const tide_str plain = tide_str_from_cstr("abc");
    TIDE_CHECK(tide_str_char(plain, 0) == 'a' && tide_str_char(plain, 2) == 'c');
    TIDE_CHECK(tide_str_char(plain, 3) == 0 && tide_str_char(plain, -1) == 0 && tide_str_char(plain, INT32_MAX) == 0
               && tide_str_char(plain, INT32_MIN) == 0 && tide_str_char(TIDE_STR_EMPTY, 0) == 0);

    const tide_str wide = tide_str_from_cstr("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z");
    static const int32_t codes[] = {'a', 0xE9, 0x20AC, 0x1F600, 'z'};
    TIDE_REQUIRE(wide.chars == 5 && wide.bytes == 11);
    int32_t at = 0;
    for (int32_t k = 0; k < 5; k++) {
        TIDE_CHECK(tide_str_char(wide, k) == codes[k]);
        TIDE_CHECK(tide_str_next(wide, &at) == codes[k]);
    }
    TIDE_CHECK(at == wide.bytes && tide_str_next(wide, &at) == 0 && at == wide.bytes);
    TIDE_CHECK(tide_str_char(wide, 5) == 0 && tide_str_char(wide, -1) == 0);

    char bytes[3 * 200 + 1];
    int32_t expected[200];
    int32_t n = 0;
    for (int32_t k = 0; k < 200; k++) {
        if (k % 5 == 2) {
            bytes[n++] = (char)0xC3;
            bytes[n++] = (char)(0x80 + k % 0x40);
            expected[k] = 0xC0 + k % 0x40;
        } else if (k % 7 == 3) {
            bytes[n++] = (char)0xE2;
            bytes[n++] = (char)0x82;
            bytes[n++] = (char)(0x80 + k % 0x40);
            expected[k] = 0x2080 + k % 0x40;
        } else {
            bytes[n++] = (char)('a' + k % 26);
            expected[k] = 'a' + k % 26;
        }
    }
    bytes[n] = '\0';
    const tide_str mixed = {bytes, n, 200};
    at = 0;
    for (int32_t k = 0; k < 200; k++) {
        TIDE_CHECK(tide_str_char(mixed, k) == expected[k]);
        TIDE_CHECK(tide_str_next(mixed, &at) == expected[k]);
    }
    TIDE_CHECK(tide_str_char(mixed, 200) == 0);

    const tide_str piece = tide_str_substring(wide, 1, 2);
    TIDE_CHECK(piece.ptr == wide.ptr + 1 && piece.bytes == 5 && piece.chars == 2);
    TIDE_CHECK(tide_str_char(piece, 0) == 0xE9 && tide_str_char(piece, 1) == 0x20AC && tide_str_char(piece, 2) == 0);
    const tide_str letters = tide_str_substring(plain, 0, 2);
    TIDE_CHECK(letters.ptr == plain.ptr && tide_str_char(letters, 1) == 'b' && tide_str_char(letters, 2) == 0);
    at = 2;
    TIDE_CHECK(tide_str_next(letters, &at) == 0);
}

TIDE_TEST(text_chars_of_bytes_that_are_none)
{
    static const struct {
        const char *bytes;
        int32_t count;
    } wrong[] = {
        {"\xFF", 1},             // Starts nothing
        {"\xC3", 1},             // Cut short
        {"\xE2\x82", 2},         // Cut short
        {"\xC0\x80", 2},         // Zero, the long way
        {"\xE0\x80\xA9", 3},     // An overlong three
        {"\xED\xA0\x80", 3},     // U+D800
        {"\xF4\x90\x80\x80", 4}, // Past U+10FFFF
        {"a\x80", 2},            // A byte that continues nothing after one that needs none
    };
    for (size_t k = 0; k < sizeof wrong / sizeof wrong[0]; k++) {
        const tide_str text = {wrong[k].bytes, wrong[k].count, tide_utf8_chars(wrong[k].bytes, wrong[k].count)};
        TIDE_REQUIRE(text.chars == 1);
        int32_t at = 0;
        TIDE_CHECK(tide_str_char(text, 0) == 0xFFFD);
        TIDE_CHECK(tide_str_next(text, &at) == 0xFFFD && at == text.bytes);
        TIDE_CHECK(tide_str_char(text, 1) == 0);
    }
    const tide_str stray = {"\x80\x80z", 3, 1};
    int32_t at = 0;
    TIDE_CHECK(tide_str_char(stray, 0) == 'z' && tide_str_next(stray, &at) == 'z' && at == 3);
    const tide_str lying = {"ab", 2, 9};
    at = 0;
    TIDE_CHECK(tide_str_char(lying, 5) == 0 && tide_str_next(lying, &at) == 'a' && tide_str_next(lying, &at) == 'b'
               && tide_str_next(lying, &at) == 0 && at == 2);
}

TIDE_TEST(text_chars_joined_and_searched)
{
    const uint64_t mark = tide_scratch_mark();
    TIDE_CHECK(tide_char_of('a') == 'a' && tide_char_of(0x10FFFF) == 0x10FFFF && tide_char_of(0xD7FF) == 0xD7FF && tide_char_of(0xE000) == 0xE000);
    TIDE_CHECK(tide_char_of(-1) == 0 && tide_char_of(0x110000) == 0 && tide_char_of(0xD800) == 0 && tide_char_of(0xDFFF) == 0
               && tide_char_of(INT32_MIN) == 0 && tide_char_of(INT32_MAX) == 0);

    tide_str s = tide_str_add_char(TIDE_STR_EMPTY, 'h');
    s = tide_str_add_char(s, 0xE9);
    s = tide_str_add_char(s, 0);
    s = tide_str_add_char(s, -5);
    s = tide_str_add_char(s, 0xD800);
    s = tide_str_add_char(s, 0x1F600);
    TIDE_CHECK(is(s, "h\xC3\xA9\xF0\x9F\x98\x80") && s.chars == 3);
    TIDE_CHECK(is(tide_str_add_char_quoted(TIDE_STR_EMPTY, 'a'), "'a'"));
    TIDE_CHECK(is(tide_str_add_char_quoted(TIDE_STR_EMPTY, 0), "'\\0'"));
    TIDE_CHECK(is(tide_str_add_char_quoted(TIDE_STR_EMPTY, 0x20AC), "'\xE2\x82\xAC'"));

    const tide_str word = tide_str_from_cstr("h\xC3\xA9llo");
    TIDE_CHECK(tide_str_index_of_char(word, 'l') == 2 && tide_str_index_of_char(word, 0xE9) == 1 && tide_str_index_of_char(word, 'z') == -1);
    TIDE_CHECK(tide_str_index_of_char(word, 0) == -1 && tide_str_index_of_char(word, -1) == -1 && tide_str_index_of_char(word, 0x110000) == -1);
    TIDE_CHECK(tide_str_contains_char(word, 'o') && !tide_str_contains_char(word, 'x') && !tide_str_contains_char(word, 0)
               && !tide_str_contains_char(TIDE_STR_EMPTY, 'a'));
    TIDE_CHECK(tide_str_starts_with_char(word, 'h') && !tide_str_starts_with_char(word, 0xE9) && !tide_str_starts_with_char(TIDE_STR_EMPTY, 'h'));
    TIDE_CHECK(tide_str_ends_with_char(word, 'o') && !tide_str_ends_with_char(word, 'l') && !tide_str_ends_with_char(word, 0));
    TIDE_CHECK(is(tide_str_replace_char(word, 'l', 'L'), "h\xC3\xA9LLo"));
    TIDE_CHECK(is(tide_str_replace_char(word, 0xE9, 'e'), "hello"));
    TIDE_CHECK(is(tide_str_replace_char(word, 'l', 0), "h\xC3\xA9o"));
    TIDE_CHECK(is(tide_str_replace_char(word, 'z', 'y'), "h\xC3\xA9llo") && is(tide_str_replace_char(word, -1, 'y'), "h\xC3\xA9llo"));
    tide_scratch_reset(mark);
}

static int32_t plain_find(const tide_str text, const tide_str needle)
{
    for (int32_t at = 0; at <= text.bytes - needle.bytes; at++) {
        if (memcmp(text.ptr + at, needle.ptr, (size_t)needle.bytes) == 0) return at;
    }
    return -1;
}

static void check_search(const tide_str text, const tide_str needle)
{
    const int32_t expected = plain_find(text, needle);
    TIDE_CHECK(tide_str_contains(text, needle) == (expected >= 0));
    TIDE_CHECK(tide_str_index_of(text, needle) == (expected < 0 ? -1 : tide_utf8_chars(text.ptr, expected)));
}

TIDE_TEST(text_search_periods_and_counted_bytes)
{
    char text[64], needle[40];
    for (uint32_t pattern = 0; pattern < 32; pattern++) {
        for (int32_t j = 0; j < 40; j++) needle[j] = (pattern & (1u << (j / 8))) ? 'b' : 'a';
        for (uint32_t bits = 0; bits < 256; bits++) {
            for (int32_t j = 0; j < 64; j++) text[j] = (bits & (1u << (j / 8))) ? 'b' : 'a';
            check_search((tide_str){text, 64, 64}, (tide_str){needle, 40, 40});
        }
    }
    char varied[513], part[129];
    uint32_t seed = 17;
    for (int32_t round = 0; round < 2000; round++) {
        const int32_t length = 1 + round % 513;
        const int32_t count = round % 130;
        for (int32_t j = 0; j < length; j++) {
            seed = seed * 1664525u + 1013904223u;
            varied[j] = (char)(seed >> (round % 2 ? 30 : 24));
        }
        for (int32_t j = 0; j < count; j++) {
            seed = seed * 1664525u + 1013904223u;
            part[j] = (char)(seed >> (round % 2 ? 30 : 24));
        }
        // The buffers need no NUL and can contain zero or high bytes. Some
        // cases have a planted match; the reference still checks the first one.
        if (count && count <= length && round % 3) {
            memcpy(varied + (round % (length - count + 1)), part, (size_t)count);
        }
        check_search((tide_str){varied, length, tide_utf8_chars(varied, length)},
                     (tide_str){part, count, tide_utf8_chars(part, count)});
    }
    check_search(TIDE_STR_EMPTY, TIDE_STR_EMPTY);
    check_search(TIDE_STR_EMPTY, tide_str_from_cstr("missing"));
}

TIDE_TEST(text_search_long_repeated_prefix)
{
    const uint64_t mark = tide_scratch_mark();
    const int32_t bytes = 1024 * 1024, count = 16 * 1024;
    char *text = malloc((size_t)bytes);
    char *needle = malloc((size_t)count);
    TIDE_REQUIRE(text && needle);
    memset(text, 'a', (size_t)bytes);
    memset(needle, 'a', (size_t)count);
    needle[count - 1] = 'b';
    const tide_str value = {text, bytes, bytes}, pattern = {needle, count, count};
    TIDE_CHECK(!tide_str_contains(value, pattern));
    TIDE_CHECK(tide_str_index_of(value, pattern) == -1);
    TIDE_CHECK(tide_str_replace(value, pattern, tide_str_from_cstr("x")).ptr == value.ptr);
    text[bytes - 1] = 'b';
    TIDE_CHECK(tide_str_index_of(value, pattern) == bytes - count);
    const tide_str replaced = tide_str_replace(value, pattern, tide_str_from_cstr("x"));
    TIDE_CHECK(replaced.bytes == bytes - count + 1 && replaced.chars == replaced.bytes);
    TIDE_CHECK(replaced.ptr[replaced.bytes - 1] == 'x');
    TIDE_CHECK(memcmp(replaced.ptr, text, (size_t)(bytes - count)) == 0);
    free(needle);
    free(text);
    tide_scratch_reset(mark);
}

TIDE_TEST(text_replace_keeps_nonoverlapping_matches)
{
    const uint64_t mark = tide_scratch_mark();
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("aaaaa"), tide_str_from_cstr("aa"), tide_str_from_cstr("b")), "bba"));
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("aaaa"), tide_str_from_cstr("aa"), TIDE_STR_EMPTY), ""));
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("unchanged"), TIDE_STR_EMPTY, tide_str_from_cstr("x")), "unchanged"));
    const tide_str unicode = tide_str_from_cstr("h\xC3\xA9-h\xC3\xA9");
    const tide_str replaced = tide_str_replace(unicode, tide_str_from_cstr("h\xC3\xA9"), tide_str_from_cstr("\xE2\x98\x83"));
    TIDE_CHECK(is(replaced, "\xE2\x98\x83-\xE2\x98\x83") && replaced.chars == 3);
    tide_scratch_reset(mark);
}
