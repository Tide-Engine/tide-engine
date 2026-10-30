#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/json.h"
#include "tide_test.h"

typedef struct json_world {
    tide_list bytes;
    tide_heap heap;
} json_world;

static json_world world;

static tide_str text_of(const char *s)
{
    return tide_str_from_cstr(s);
}

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

static uint32_t bits_of(const float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    return bits;
}

// The float `text` reads as, as its bits: 1 (no float's bits that any text here is) when it isn't one.
static uint32_t parsed(const char *text)
{
    float v;
    return tide_str_parse_float(text_of(text), &v) ? bits_of(v) : 1u;
}

#define NOT_A_FLOAT 1u

TIDE_TEST(json_floats_read_as_the_nearest_float)
{
    static const struct {
        const char *text;
        uint32_t bits;
    } cases[] = {
        {"0", 0x00000000u}, {"-0", 0x80000000u}, {"0.0e99", 0x00000000u}, {"1", 0x3F800000u}, {"-1", 0xBF800000u},
        {"0.1", 0x3DCCCCCDu}, {"0.3", 0x3E99999Au}, {"3.1415927", 0x40490FDBu}, {"100", 0x42C80000u}, {"1e2", 0x42C80000u},
        {"1E+2", 0x42C80000u}, {"1000e-1", 0x42C80000u}, {"0.001e5", 0x42C80000u}, {"5.", 0x40A00000u}, {".5", 0x3F000000u},
        {"007", 0x40E00000u}, {"16777216", 0x4B800000u},
        {"16777217", 0x4B800000u}, {"16777219", 0x4B800002u}, {"16777217.000000000000000000000000000000001", 0x4B800001u},
        {"16777216.99999999999999999999999999999999", 0x4B800000u},
        {"1.000000178813934326171875", 0x3F800002u}, {"1.00000017881393432617187499999999999999999999", 0x3F800001u},
        {"1.00000017881393432617187500000000000000000001", 0x3F800002u},
        {"1.00000005960464477539062500000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
         "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001",
         0x3F800001u},
        {"1.000000059604644775390625", 0x3F800000u},
        {"3.4028235e38", 0x7F7FFFFFu}, {"340282346638528859811704183484516925440", 0x7F7FFFFFu},
        {"340282356779733661637539395458142568447", 0x7F7FFFFFu}, {"340282356779733661637539395458142568448", NOT_A_FLOAT},
        {"3.4028236e38", NOT_A_FLOAT}, {"1e39", NOT_A_FLOAT}, {"1e40", NOT_A_FLOAT}, {"-1e999999999999999999999", NOT_A_FLOAT},
        {"0.00000000001e50", NOT_A_FLOAT}, {"100000000000000000000000000000000000000000000000000e-12", 0x7E967699u},
        {"1.17549435e-38", 0x00800000u}, {"1.1754942e-38", 0x007FFFFFu}, {"1.1754943e-38", 0x00800000u},
        {"1.401298464324817e-45", 0x00000001u}, {"1e-45", 0x00000001u}, {"7.006492321624085e-46", 0x00000000u},
        {"7.006492321624086e-46", 0x00000001u}, {"7.1e-46", 0x00000001u}, {"7e-46", 0x00000000u}, {"2.1e-45", 0x00000001u},
        {"2.2e-45", 0x00000002u}, {"1e-46", 0x00000000u}, {"1e-999999999999999999999", 0x00000000u}, {"-1e-50", 0x80000000u},
        {"9007199791611905", 0x5A000001u}, {"9007199791611904", 0x5A000000u}, {"1.00000011920928955078125", 0x3F800001u},
        {"", NOT_A_FLOAT}, {"-", NOT_A_FLOAT}, {".", NOT_A_FLOAT}, {"-.", NOT_A_FLOAT}, {"e5", NOT_A_FLOAT}, {"1e", NOT_A_FLOAT},
        {"1e+", NOT_A_FLOAT}, {"+1", NOT_A_FLOAT}, {" 1", NOT_A_FLOAT}, {"1 ", NOT_A_FLOAT}, {"1f", NOT_A_FLOAT},
        {"0x10", NOT_A_FLOAT}, {"1,5", NOT_A_FLOAT}, {"1.2.3", NOT_A_FLOAT}, {"nan", NOT_A_FLOAT}, {"NaN", NOT_A_FLOAT},
        {"inf", NOT_A_FLOAT}, {"Infinity", NOT_A_FLOAT}, {"--1", NOT_A_FLOAT}, {"1e5.0", NOT_A_FLOAT}, {"1_000", NOT_A_FLOAT},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const uint32_t got = parsed(cases[i].text);
        if (got != cases[i].bits) printf("    %s: 0x%08" PRIX32 ", expected 0x%08" PRIX32 "\n", cases[i].text, got, cases[i].bits);
        TIDE_CHECK(got == cases[i].bits);
    }
    float untouched = 5.0f;
    TIDE_CHECK(!tide_str_parse_float(text_of("x"), &untouched) && untouched == 0.0f);
}

TIDE_TEST(json_floats_read_back_from_what_text_shows)
{
    // Every float's shortest text reads back as that float: a sweep over exponents and mantissas, with the
    // platform's strtof beside it, which rounds text this short correctly everywhere.
    const uint64_t mark = tide_scratch_mark();
    for (uint32_t bits = 1u; bits < 0x7F800000u; bits += 0x000F0F1Fu) {
        float v;
        memcpy(&v, &bits, sizeof v);
        const tide_str s = tide_str_add_float(TIDE_STR_EMPTY, v, 0);
        float back;
        TIDE_CHECK(tide_str_parse_float(s, &back) && bits_of(back) == bits);
        const tide_str negative = tide_json_add_float(TIDE_STR_EMPTY, -v);
        TIDE_CHECK(tide_str_parse_float(negative, &back) && bits_of(back) == (bits | 0x80000000u));
        const tide_str fixed = tide_str_add_float(TIDE_STR_EMPTY, v, TIDE_FORMAT('F', 9));
        char c_text[128];
        TIDE_REQUIRE(fixed.bytes < (int32_t)sizeof c_text);
        memcpy(c_text, fixed.ptr, (size_t)fixed.bytes);
        c_text[fixed.bytes] = '\0';
        const float reference = strtof(c_text, NULL);
        TIDE_CHECK(tide_str_parse_float(fixed, &back) && bits_of(back) == bits_of(reference));
        tide_scratch_reset(mark);
    }
}

TIDE_TEST(json_ints_read_whole_or_not_at_all)
{
    int32_t i = 5;
    int64_t l = 5;
    TIDE_CHECK(tide_str_parse_int(text_of("42"), &i) && i == 42);
    TIDE_CHECK(tide_str_parse_int(text_of("-2147483648"), &i) && i == INT32_MIN);
    TIDE_CHECK(tide_str_parse_int(text_of("2147483647"), &i) && i == INT32_MAX);
    TIDE_CHECK(!tide_str_parse_int(text_of("2147483648"), &i) && i == 0);
    TIDE_CHECK(!tide_str_parse_int(text_of("-2147483649"), &i) && i == 0);
    TIDE_CHECK(tide_str_parse_int(text_of("-0"), &i) && i == 0 && tide_str_parse_int(text_of("000123"), &i) && i == 123);
    static const char *const bad[] = {"", "-", "+1", " 1", "1 ", "1.0", "1e3", "0x1F", "12a", "--1", "1-", "9999999999999999999999"};
    for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        i = 5;
        l = 5;
        TIDE_CHECK(!tide_str_parse_int(text_of(bad[k]), &i) && i == 0);
        TIDE_CHECK(!tide_str_parse_long(text_of(bad[k]), &l) && l == 0);
    }
    TIDE_CHECK(tide_str_parse_long(text_of("9223372036854775807"), &l) && l == INT64_MAX);
    TIDE_CHECK(tide_str_parse_long(text_of("-9223372036854775808"), &l) && l == INT64_MIN);
    TIDE_CHECK(!tide_str_parse_long(text_of("9223372036854775808"), &l) && !tide_str_parse_long(text_of("-9223372036854775809"), &l));
    TIDE_CHECK(!tide_str_parse_long(text_of("18446744073709551616"), &l) && !tide_str_parse_long(text_of("99999999999999999999"), &l));
    TIDE_CHECK(tide_str_parse_int((tide_str){"12345", 3, 3}, &i) && i == 123);
    float f;
    TIDE_CHECK(tide_str_parse_float((tide_str){"1.5e10", 3, 3}, &f) && f == 1.5f);
}

static tide_json reading(const char *text)
{
    tide_json j;
    tide_json_begin(&j, text_of(text));
    return j;
}

TIDE_TEST(json_reader_takes_each_kind_and_fails_for_good)
{
    const uint64_t mark = tide_scratch_mark();
    tide_json j = reading(" [ true , false,null, -12 ,3.0 , 2.5e0, \"a\\u00e9\\ud83d\\ude00\\n\" ,\"\\u20ac\", \"x\" ] ");
    bool b = false;
    int64_t n = 0;
    float f = 0.0f;
    tide_text t = {0};
    int32_t c = 0;
    int32_t which = -5;
    static const char *const names[] = {"w", "x"};
    TIDE_CHECK(tide_json_array(&j) && tide_json_item(&j, 1) && tide_json_bool(&j, &b) && b);
    TIDE_CHECK(tide_json_item(&j, 1) && !tide_json_null(&j) && tide_json_bool(&j, &b) && !b);
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_null(&j));
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_number_next(&j) && tide_json_int(&j, -12, 0, &n) && n == -12);
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_int(&j, 0, 3, &n) && n == 3);
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_float(&j, &f) && f == 2.5f);
    TIDE_CHECK(tide_json_item(&j, 1) && !tide_json_number_next(&j) && tide_json_text(&j, &t));
    TIDE_CHECK(is(tide_text_view(t), "a\xC3\xA9\xF0\x9F\x98\x80\n") && tide_text_view(t).chars == 4);
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_char(&j, &c) && c == 0x20AC);
    TIDE_CHECK(tide_json_item(&j, 1) && tide_json_name(&j, names, 2, &which) && which == 1);
    TIDE_CHECK(!tide_json_item(&j, 1) && !j.failed && tide_json_end(&j));

    // An object's keys, by which name each is, and as text
    j = reading("{\"x\":1,\"other\":[{}],\"w\":2,\"\\u0078\":3}");
    TIDE_CHECK(tide_json_object(&j) && tide_json_field(&j, names, 2) == 1 && tide_json_skip(&j));
    TIDE_CHECK(tide_json_field(&j, names, 2) == TIDE_JSON_UNKNOWN && tide_json_skip(&j));
    TIDE_CHECK(tide_json_field(&j, names, 2) == 0 && tide_json_skip(&j));
    TIDE_CHECK(tide_json_field(&j, names, 2) == 1 && tide_json_int(&j, 0, 9, &n) && n == 3);
    TIDE_CHECK(tide_json_field(&j, names, 2) == TIDE_JSON_DONE && tide_json_end(&j));
    j = reading("{\"k\":1,\"long key\":2}");
    TIDE_CHECK(tide_json_object(&j) && tide_json_key(&j, &t, 8) && is(tide_text_view(t), "k") && tide_json_skip(&j));
    TIDE_CHECK(tide_json_key(&j, &t, 8) && is(tide_text_view(t), "long key") && tide_json_skip(&j) && !tide_json_key(&j, &t, 8));
    TIDE_CHECK(tide_json_end(&j));

    j = reading("[1,x,3]");
    n = 7;
    TIDE_CHECK(tide_json_array(&j) && tide_json_item(&j, 1) && tide_json_int(&j, 0, 9, &n) && tide_json_item(&j, 1));
    TIDE_CHECK(!tide_json_int(&j, 0, 9, &n) && j.failed && n == 1);
    TIDE_CHECK(!tide_json_item(&j, 1) && !tide_json_skip(&j) && !tide_json_null(&j) && !tide_json_bool(&j, &b) && !tide_json_end(&j));
    TIDE_CHECK(!tide_json_array(&j) && !tide_json_object(&j) && tide_json_field(&j, names, 2) == TIDE_JSON_DONE);
    TIDE_CHECK(!tide_json_text(&j, &t) && !tide_json_char(&j, &c) && !tide_json_float(&j, &f) && !tide_json_number_next(&j));

    j = reading("3.5");
    TIDE_CHECK(!tide_json_int(&j, 0, 9, &n) && j.failed);
    j = reading("10");
    TIDE_CHECK(!tide_json_int(&j, 0, 9, &n));
    j = reading("\"y\"");
    TIDE_CHECK(!tide_json_name(&j, names, 2, &which) && j.failed);
    j = reading("\"ab\"");
    TIDE_CHECK(!tide_json_char(&j, &c));
    j = reading("\"\"");
    TIDE_CHECK(tide_json_char(&j, &c) && c == 0 && tide_json_end(&j));
    j = reading("\xEF\xBB\xBF{}");
    TIDE_CHECK(tide_json_skip(&j) && tide_json_end(&j));
    j = reading("{} x");
    TIDE_CHECK(tide_json_skip(&j) && !tide_json_end(&j));
    tide_scratch_reset(mark);
}

TIDE_TEST(json_reader_refuses_what_is_not_json)
{
    static const char *const good[] = {
        "null", "true", "false", "0", "-0", "1.5", "1e5", "1E-5", "-1.25e+10", "\"\"", "\"a\"", "[]", "{}", "[[]]", "[{}]",
        "{\"a\":[]}", " [ 1 , 2 ] ", "{\"a\":{\"b\":{\"c\":null}}}", "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0041\"", "\"\\uD83D\\uDE00\"",
        "\"\xF0\x9F\x98\x80\"", "[1,\"a\",true,null,{\"k\":[1.5]}]", "123456789012345678901234567890",
        "\"\\u0000\"", "\t\r\n 1 \t\r\n",
    };
    static const char *const bad[] = {
        "", " ", "nul", "nulll", "True", "tru", "falsey", "01", "-", "+1", "1.", ".5", "1e", "1e+", "0x10", "--1", "1.5.5", "NaN",
        "Infinity", "\"", "\"a", "'a'", "\"\\x\"", "\"\\u12\"", "\"\\u12g4\"", "\"\\ud83d\"", "\"\\ud83dx\"", "\"\\ud83d\\u0041\"",
        "\"\\ude00\"", "\"a\nb\"", "\"\t\"", "\"\x01\"", "\"\xC3\"", "\"\x80\"", "\"\xC0\x80\"", "\"\xED\xA0\x80\"",
        "\"\xF4\x90\x80\x80\"", "\"\xFF\"", "\"\\\"", "[", "]", "[1", "[1,", "[1,]", "[,1]", "[1 2]", "[1,,2]", "{", "}", "{\"a\"}",
        "{\"a\":}", "{\"a\":1,}", "{,}", "{a:1}", "{1:1}", "{\"a\" 1}", "{\"a\":1 \"b\":2}", "{\"a\":1}}", "[1]]", "1 2", "{}{}",
        "[1}", "{\"a\":1]", "\"a\" \"b\"", "/* c */ 1", "// c\n1", "[1] // c", "\\u0041",
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        tide_json j = reading(good[i]);
        const bool read = tide_json_skip(&j) && tide_json_end(&j);
        if (!read) printf("    refused: %s\n", good[i]);
        TIDE_CHECK(read);
    }
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        tide_json j = reading(bad[i]);
        const bool read = tide_json_skip(&j) && tide_json_end(&j);
        if (read) printf("    read: %s\n", bad[i]);
        TIDE_CHECK(!read && j.failed);
    }
    tide_json j;
    tide_json_begin(&j, (tide_str){"\"a\0b\"", 5, 5});
    TIDE_CHECK(!tide_json_skip(&j));
    tide_json_begin(&j, (tide_str){"[1,2]]]]", 5, 5});
    TIDE_CHECK(tide_json_skip(&j) && tide_json_end(&j));
    tide_json_begin(&j, (tide_str){"\"abc\"", 4, 4});
    TIDE_CHECK(!tide_json_skip(&j));
    tide_json_begin(&j, (tide_str){"\"ab\\\"", 4, 4});
    TIDE_CHECK(!tide_json_skip(&j));
    tide_json_begin(&j, (tide_str){"\"\\u00e9\"", 6, 6});
    TIDE_CHECK(!tide_json_skip(&j));
    tide_json_begin(&j, (tide_str){"\"\xF0\x9F\x98\x80\"", 4, 4});
    TIDE_CHECK(!tide_json_skip(&j));
    tide_json_begin(&j, (tide_str){"12345", 3, 3});
    int64_t n;
    TIDE_CHECK(tide_json_int(&j, 0, 999, &n) && n == 123 && tide_json_end(&j));
    tide_json_begin(&j, (tide_str){"truefalse", 3, 3});
    TIDE_CHECK(!tide_json_skip(&j));
}

TIDE_TEST(json_reader_bounds_depth_text_and_what_it_makes)
{
    char deep[2 * TIDE_JSON_DEPTH + 8];
    for (uint32_t depth = TIDE_JSON_DEPTH - 1; depth <= TIDE_JSON_DEPTH + 1; depth++) {
        memset(deep, '[', depth);
        memset(deep + depth, ']', depth);
        deep[2 * depth] = '\0';
        tide_json j = reading(deep);
        TIDE_CHECK((tide_json_skip(&j) && tide_json_end(&j)) == (depth <= TIDE_JSON_DEPTH));
        j = reading(deep);
        uint32_t opened = 0;
        while (tide_json_array(&j)) opened++;
        TIDE_CHECK(opened == (depth <= TIDE_JSON_DEPTH ? depth : TIDE_JSON_DEPTH) && j.failed);
    }
    // A hundred thousand of them, which no stack is asked to hold
    enum { MANY = 100000 };
    char *many = malloc(2 * MANY + 1);
    TIDE_REQUIRE(many);
    memset(many, '[', MANY);
    memset(many + MANY, ']', MANY);
    many[2 * MANY] = '\0';
    tide_json j = reading(many);
    TIDE_CHECK(!tide_json_skip(&j) && j.failed);
    for (int i = 0; i < MANY; i++) memcpy(many + 2 * i, i < MANY / 2 ? "{\"" : "\":", 2); // {"{"{"..., then ":":":...
    j = reading(many);
    TIDE_CHECK(!tide_json_skip(&j));
    free(many);

    static const char one[] = "1";
    tide_json_begin(&j, (tide_str){one, (int32_t)TIDE_JSON_LIMIT, 0});
    TIDE_CHECK(j.failed && !tide_json_skip(&j) && !tide_json_end(&j));
    tide_json_begin(&j, (tide_str){one, -1, 0});
    TIDE_CHECK(j.failed);

    j = reading("[1,2,3,4,5,6,7,8,9]");
    TIDE_CHECK(tide_json_array(&j));
    uint32_t items = 0;
    while (tide_json_item(&j, TIDE_JSON_ROOM / 4u)) {
        TIDE_CHECK(tide_json_skip(&j));
        items++;
    }
    TIDE_CHECK(items == 4 && j.failed && j.made > TIDE_JSON_ROOM);
    j = reading("{\"a\":1,\"b\":2}");
    tide_text key;
    TIDE_CHECK(tide_json_object(&j) && tide_json_key(&j, &key, TIDE_JSON_ROOM - 20u) && tide_json_skip(&j));
    TIDE_CHECK(!tide_json_key(&j, &key, 1) && j.failed);
}

TIDE_TEST(json_reads_bytes_wherever_a_list_keeps_them)
{
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(&world.heap, NULL);
    const uint64_t mark = tide_scratch_mark();
    static const char small[] = "{\"a\":[1,2,3]}";
    tide_json j;
    tide_json_begin_bytes(&j, tide_list_from(small, (int32_t)strlen(small), 1u));
    TIDE_CHECK(tide_json_skip(&j) && tide_json_end(&j));
    tide_json_begin_bytes(&j, (tide_list){0});
    TIDE_CHECK(!tide_json_skip(&j) && j.failed);

    uint8_t *open = tide_list_add(&world.bytes, 1u, TIDE_IN_MATCH);
    TIDE_REQUIRE(open);
    *open = '[';
    for (int i = 0; i < 40000; i++) {
        uint8_t *digit = tide_list_add(&world.bytes, 1u, TIDE_IN_MATCH); // Each is where it is until the next is added
        TIDE_REQUIRE(digit);
        *digit = (uint8_t)('0' + i % 10);
        uint8_t *after = tide_list_add(&world.bytes, 1u, TIDE_IN_MATCH);
        TIDE_REQUIRE(after);
        *after = i == 39999 ? ']' : ',';
    }
    TIDE_CHECK(tide_block_at(world.bytes.at)->b & TIDE_LIST_CHUNKED);
    tide_json_begin_bytes(&j, world.bytes);
    int32_t count = 0;
    int64_t sum = 0;
    TIDE_CHECK(tide_json_array(&j));
    while (tide_json_item(&j, 4u)) {
        int64_t n;
        if (!tide_json_int(&j, 0, 9, &n)) break;
        sum += n;
        count++;
    }
    TIDE_CHECK(count == 40000 && sum == 180000 && tide_json_end(&j));
    tide_str text;
    TIDE_CHECK(tide_str_from_bytes(world.bytes, &text) && text.bytes == 80001 && text.chars == 80001 && text.ptr[80000] == ']');
    *(uint8_t *)tide_list_at_mut(world.bytes, 70000, 1u) = 0xC3u;
    TIDE_CHECK(!tide_str_from_bytes(world.bytes, &text) && text.bytes == 0);
    tide_json_begin_bytes(&j, world.bytes);
    TIDE_CHECK(!tide_json_skip(&j));
    tide_scratch_reset(mark);
    tide_heap_free(&world.heap);
    memset(&world, 0, sizeof world);
    tide_text_use(NULL, NULL);
}

TIDE_TEST(json_writes_text_that_is_always_json)
{
    const uint64_t mark = tide_scratch_mark();
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("")), "\"\""));
    TIDE_CHECK(is(tide_json_add_text(text_of("x="), text_of("a\"b\\c/d")), "x=\"a\\\"b\\\\c/d\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("\b\f\n\r\t\x01\x1F\x7F")), "\"\\b\\f\\n\\r\\t\\u0001\\u001f\x7F\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80")), "\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, (tide_str){"a\0b", 3, 3}), "\"a\\u0000b\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("a\xC3z")), "\"a\xEF\xBF\xBDz\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("\x80\x80z\xED\xA0\x80\xFF")), "\"\xEF\xBF\xBDz\xEF\xBF\xBD\xEF\xBF\xBD\""));
    TIDE_CHECK(is(tide_json_add_text(TIDE_STR_EMPTY, text_of("\xF0\x9F\x98")), "\"\xEF\xBF\xBD\""));
    tide_str long_text = TIDE_STR_EMPTY;
    for (int i = 0; i < 500; i++) long_text = tide_str_add(long_text, text_of("\"\xE2\x82\xAC\n\x02\xF0\x9F\x98\x80z"));
    const tide_str written = tide_json_add_text(TIDE_STR_EMPTY, long_text);
    TIDE_CHECK(written.bytes == 2 + 500 * (2 + 3 + 2 + 6 + 4 + 1) && written.chars == 2 + 500 * (2 + 1 + 2 + 6 + 1 + 1));
    TIDE_CHECK(tide_utf8_valid(written.ptr, written.bytes));
    tide_json j;
    tide_json_begin(&j, written);
    tide_text back;
    TIDE_CHECK(tide_json_text(&j, &back) && tide_json_end(&j) && tide_str_eq(tide_text_view(back), long_text));
    TIDE_CHECK(tide_text_view(back).chars == long_text.chars);

    TIDE_CHECK(is(tide_json_add_char(TIDE_STR_EMPTY, 'a'), "\"a\"") && is(tide_json_add_char(TIDE_STR_EMPTY, 0), "\"\""));
    TIDE_CHECK(is(tide_json_add_char(TIDE_STR_EMPTY, '"'), "\"\\\"\"") && is(tide_json_add_char(TIDE_STR_EMPTY, 0x1F600), "\"\xF0\x9F\x98\x80\""));
    TIDE_CHECK(is(tide_json_add_char(TIDE_STR_EMPTY, -5), "\"\"") && is(tide_json_add_char(TIDE_STR_EMPTY, 0xD800), "\"\""));

    TIDE_CHECK(is(tide_json_add_float(TIDE_STR_EMPTY, 0.0f), "0") && is(tide_json_add_float(TIDE_STR_EMPTY, -0.0f), "-0"));
    TIDE_CHECK(is(tide_json_add_float(TIDE_STR_EMPTY, 0.1f), "0.1") && is(tide_json_add_float(TIDE_STR_EMPTY, -2.5f), "-2.5"));
    TIDE_CHECK(is(tide_json_add_float(TIDE_STR_EMPTY, 1e21f), "1E+21") && is(tide_json_add_float(TIDE_STR_EMPTY, 1e-7f), "1E-7"));
    float nan, infinity;
    const uint32_t nan_bits = 0x7FC00000u, infinity_bits = 0x7F800000u;
    memcpy(&nan, &nan_bits, sizeof nan);
    memcpy(&infinity, &infinity_bits, sizeof infinity);
    TIDE_CHECK(is(tide_json_add_float(TIDE_STR_EMPTY, nan), "null") && is(tide_json_add_float(TIDE_STR_EMPTY, infinity), "null"));
    TIDE_CHECK(is(tide_json_add_float(TIDE_STR_EMPTY, -infinity), "null"));
    tide_scratch_reset(mark);
}

static tide_str piece(const tide_list pieces, const int32_t i)
{
    const tide_text *t = tide_list_at(pieces, i, sizeof(tide_text));
    return t ? tide_text_view(*t) : text_of("(none)");
}

TIDE_TEST(text_splits_and_goes_to_bytes_and_back)
{
    const uint64_t mark = tide_scratch_mark();
    tide_list parts = tide_str_split(text_of("a,b,,c"), text_of(","));
    TIDE_CHECK(tide_list_count(parts) == 4 && is(piece(parts, 0), "a") && is(piece(parts, 1), "b") && is(piece(parts, 2), ""));
    TIDE_CHECK(is(piece(parts, 3), "c") && is(piece(parts, 4), "(none)"));
    parts = tide_str_split(text_of(""), text_of(","));
    TIDE_CHECK(tide_list_count(parts) == 1 && is(piece(parts, 0), ""));
    parts = tide_str_split(text_of("abc"), text_of(""));
    TIDE_CHECK(tide_list_count(parts) == 1 && is(piece(parts, 0), "abc"));
    parts = tide_str_split(text_of("abc"), text_of("abcd"));
    TIDE_CHECK(tide_list_count(parts) == 1 && is(piece(parts, 0), "abc"));
    parts = tide_str_split(text_of("--a----b--"), text_of("--"));
    TIDE_CHECK(tide_list_count(parts) == 5 && is(piece(parts, 0), "") && is(piece(parts, 1), "a") && is(piece(parts, 2), ""));
    TIDE_CHECK(is(piece(parts, 3), "b") && is(piece(parts, 4), ""));
    parts = tide_str_split(text_of("aaa"), text_of("aa"));
    TIDE_CHECK(tide_list_count(parts) == 2 && is(piece(parts, 0), "") && is(piece(parts, 1), "a"));
    parts = tide_str_split_char(text_of("x\xE2\x82\xACy\xE2\x82\xAC"), 0x20AC);
    TIDE_CHECK(tide_list_count(parts) == 3 && is(piece(parts, 0), "x") && is(piece(parts, 1), "y") && is(piece(parts, 2), ""));
    TIDE_CHECK(piece(tide_str_split_char(text_of("\xC3\xA9,\xC3\xA9"), ','), 1).chars == 1);
    parts = tide_str_split_char(text_of("a,b"), 0);
    TIDE_CHECK(tide_list_count(parts) == 1 && is(piece(parts, 0), "a,b"));
    parts = tide_str_split_char(text_of("a,b"), -1);
    TIDE_CHECK(tide_list_count(parts) == 1);
    static const char long_separator[] = "0123456789012345678901234567890123456789";
    tide_str joined = text_of("left");
    joined = tide_str_add(tide_str_add(tide_str_add(joined, text_of(long_separator)), text_of("mid")), text_of(long_separator));
    parts = tide_str_split(joined, text_of(long_separator));
    TIDE_CHECK(tide_list_count(parts) == 3 && is(piece(parts, 0), "left") && is(piece(parts, 1), "mid") && is(piece(parts, 2), ""));
    parts = tide_str_split((tide_str){"a,b,c,d", 3, 3}, text_of(","));
    TIDE_CHECK(tide_list_count(parts) == 2 && is(piece(parts, 1), "b"));

    const tide_list bytes = tide_str_to_bytes(text_of("h\xC3\xA9"));
    TIDE_CHECK(tide_list_count(bytes) == 3 && *(const uint8_t *)tide_list_at(bytes, 1, 1u) == 0xC3u);
    TIDE_CHECK(tide_list_count(tide_str_to_bytes(TIDE_STR_EMPTY)) == 0);
    tide_str back;
    TIDE_CHECK(tide_str_from_bytes(bytes, &back) && is(back, "h\xC3\xA9") && back.chars == 2 && back.ptr[back.bytes] == '\0');
    TIDE_CHECK(tide_str_from_bytes((tide_list){0}, &back) && back.bytes == 0);
    static const uint8_t cut[] = {'h', 0xC3};
    TIDE_CHECK(!tide_str_from_bytes(tide_list_from(cut, 2, 1u), &back) && back.bytes == 0);

    TIDE_CHECK(tide_utf8_valid("", 0) && tide_utf8_valid("abc", 3) && tide_utf8_valid("\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", 9));
    TIDE_CHECK(tide_utf8_valid("\0", 1) && tide_utf8_valid("\xF4\x8F\xBF\xBF", 4) && tide_utf8_valid("\xEE\x80\x80", 3));
    TIDE_CHECK(!tide_utf8_valid("\x80", 1) && !tide_utf8_valid("\xC3", 1) && !tide_utf8_valid("\xC0\x80", 2) && !tide_utf8_valid("\xC1\xBF", 2));
    TIDE_CHECK(!tide_utf8_valid("\xE0\x80\x80", 3) && !tide_utf8_valid("\xED\xA0\x80", 3) && !tide_utf8_valid("\xF0\x80\x80\x80", 4));
    TIDE_CHECK(!tide_utf8_valid("\xF4\x90\x80\x80", 4) && !tide_utf8_valid("\xF5\x80\x80\x80", 4) && !tide_utf8_valid("\xFF", 1));
    TIDE_CHECK(!tide_utf8_valid("\xE2\x82", 2) && !tide_utf8_valid("\xF0\x9F\x98", 3) && !tide_utf8_valid("a\xE2\x82\xACz\xE2", 6));
    tide_scratch_reset(mark);
}
