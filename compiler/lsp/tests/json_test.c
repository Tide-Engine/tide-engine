#include <limits.h>
#include <math.h>
#include <string.h>

#include "json.h"
#include "tide_test.h"

TIDE_TEST(lsp_json_rejects_malformed_numbers)
{
    static const char *const invalid[] = {"01", "-01", "1.", "1e", "1e+", "1e-", "1+2", "1-2", "1e2.3", "-", "[1e+]", "1e999"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        TIDE_CHECK(json_parse(invalid[i], strlen(invalid[i])) == NULL);
        json_release();
    }
}

TIDE_TEST(lsp_json_rejects_invalid_raw_utf8)
{
    static const char *const invalid[] = {"\"\x80\"", "\"\xc0\xaf\"", "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"",
                                          "\"\xf0\x80\x80\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xf0\x9f\""};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        TIDE_CHECK(json_parse(invalid[i], strlen(invalid[i])) == NULL);
        json_release();
    }
}

TIDE_TEST(lsp_json_embedded_zero_never_matches_a_shorter_key_or_c_string)
{
    static const char text[] = "{\"method\\u0000suffix\":1,\"method\":\"shutdown\\u0000suffix\"}";
    const json *v = json_parse(text, sizeof text - 1);
    const json *method = json_get(v, "method");
    TIDE_REQUIRE(method && method->kind == JSON_STRING);
    TIDE_CHECK(json_str(method) == NULL);
    jbuf written = {0};
    jb_json(&written, v);
    TIDE_CHECK(strcmp(written.data, text) == 0);
    jb_free(&written);
    json_release();
}

TIDE_TEST(lsp_json_keeps_valid_numbers)
{
    static const char *const valid[] = {"0", "-0", "123", "-123", "0.25", "1e+2", "1E-2", "-0.5e3"};
    static const double values[] = {0, 0, 123, -123, 0.25, 100, 0.01, -500};
    for (size_t i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        const json *v = json_parse(valid[i], strlen(valid[i]));
        TIDE_CHECK(v && v->kind == JSON_NUMBER && v->number == values[i]);
        json_release();
    }
    // No silent truncation of a long, finite JSON number.
    static const char long_number[] = "0.00000000000000000000000000000000000000000000000000000000000000001";
    const json *v = json_parse(long_number, sizeof long_number - 1);
    TIDE_CHECK(v && v->kind == JSON_NUMBER && v->number > 0 && v->number < 1e-60);
    json_release();
}

TIDE_TEST(lsp_json_integer_conversion_has_defined_bounds)
{
    json number = {.kind = JSON_NUMBER};
    static const double invalid[] = {2147483648.0, -2147483649.0, INFINITY, -INFINITY, NAN, 1.5};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        number.number = invalid[i];
        TIDE_CHECK(json_int(&number, 17) == 17);
    }
    number.number = INT_MAX;
    TIDE_CHECK(json_int(&number, 17) == INT_MAX);
    number.number = INT_MIN;
    TIDE_CHECK(json_int(&number, 17) == INT_MIN);
}

TIDE_TEST(lsp_json_strings_keep_unicode_and_embedded_zero)
{
    static const char text[] = "\"A\\u00e9\\ud83d\\ude00\\u0000Z\"";
    static const char decoded[] = "A\xc3\xa9\xf0\x9f\x98\x80\0Z";
    const json *v = json_parse(text, sizeof text - 1);
    TIDE_REQUIRE(v && v->kind == JSON_STRING);
    TIDE_CHECK(v->string_len == sizeof decoded - 1 && memcmp(v->string, decoded, sizeof decoded - 1) == 0);
    jbuf written = {0};
    jb_json(&written, v);
    const json *again = json_parse(written.data, written.len);
    TIDE_CHECK(again && again->string_len == sizeof decoded - 1 && memcmp(again->string, decoded, sizeof decoded - 1) == 0);
    jb_free(&written);
    json_release();
}

TIDE_TEST(lsp_json_unpaired_surrogates_use_valid_utf8)
{
    // An editor can send half a UTF-16 pair while typing. Do not corrupt the
    // next escape or emit UTF-8 encodings of surrogate code points.
    static const char *const texts[] = {"\"\\ud800\"", "\"\\udfff\"", "\"\\ud800\\u0041\"", "\"\\ud800\\ud800\""};
    static const char *const decoded[] = {"\xef\xbf\xbd", "\xef\xbf\xbd", ("\xef\xbf\xbd" "A"), "\xef\xbf\xbd\xef\xbf\xbd"};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        const json *v = json_parse(texts[i], strlen(texts[i]));
        TIDE_CHECK(v && v->kind == JSON_STRING && strcmp(v->string, decoded[i]) == 0);
        json_release();
    }
}

TIDE_TEST(lsp_json_rejects_controls_and_truncated_strings)
{
    static const char *const invalid[] = {"\"line\nbreak\"", "\"tab\ttext\"", "\"\\", "\"\\u123\"", "\"\\q\"", "\"unfinished"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        TIDE_CHECK(json_parse(invalid[i], strlen(invalid[i])) == NULL);
        json_release();
    }
}
