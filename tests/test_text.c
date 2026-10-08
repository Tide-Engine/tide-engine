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
    const uint32_t mark = tide_scratch_mark();
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
    TIDE_CHECK(is(f(0.2f + 0.1f, 0), "0.3")); // In float, it rounds to the same as 0.3
    tide_scratch_reset(mark);
}

TIDE_TEST(text_floats_read_back)
{
    // Every text reads back as the same float: a sweep over exponents and mantissas.
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
    const tide_str word = tide_str_from_cstr("h\xC3\xA9llo"); // héllo
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
    // Expand every binary pattern into runs, to exercise long-pattern splits
    // and shifts against all haystacks of eight runs, including overlapping matches.
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
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("aaaaa"), tide_str_from_cstr("aa"), tide_str_from_cstr("b")), "bba"));
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("aaaa"), tide_str_from_cstr("aa"), TIDE_STR_EMPTY), ""));
    TIDE_CHECK(is(tide_str_replace(tide_str_from_cstr("unchanged"), TIDE_STR_EMPTY, tide_str_from_cstr("x")), "unchanged"));
    const tide_str unicode = tide_str_from_cstr("h\xC3\xA9-h\xC3\xA9");
    const tide_str replaced = tide_str_replace(unicode, tide_str_from_cstr("h\xC3\xA9"), tide_str_from_cstr("\xE2\x98\x83"));
    TIDE_CHECK(is(replaced, "\xE2\x98\x83-\xE2\x98\x83") && replaced.chars == 3);
    tide_scratch_reset(mark);
}
