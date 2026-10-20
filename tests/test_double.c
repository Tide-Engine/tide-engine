#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/json.h"
#include "tide/math.h"
#include "tide_test.h"

TIDE_TEST(double_conversions_and_rounding_are_defined_at_extremes)
{
    const double nan = tide_d_from_bits(UINT64_C(0x7FF8123456789ABC));
    const double infinity = tide_d_from_bits(UINT64_C(0x7FF0000000000000));
    TIDE_CHECK(tide_i_from_d(nan) == 0 && tide_l_from_d(nan) == 0);
    TIDE_CHECK(tide_i_from_d(infinity) == INT32_MAX && tide_l_from_d(-infinity) == INT64_MIN);
    TIDE_CHECK(tide_i_from_d(2147483647.5) == INT32_MAX && tide_i_from_d(-2147483648.5) == INT32_MIN);
    TIDE_CHECK(tide_l_from_d(0x1.fffffffffffffp+62) == INT64_MAX - 1023);
    TIDE_CHECK(tide_l_from_d(0x1p+63) == INT64_MAX);
    TIDE_CHECK(tide_l_from_d(-0x1.0000000000001p+63) == INT64_MIN);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(DBL_MAX)) == 0x7F800000u);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(-DBL_MAX)) == 0xFF800000u);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(0x1.fffffe8p+127)) == 0x7F7FFFFFu);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(0x1.ffffffp+127)) == 0x7F800000u);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(0x1p-150)) == 0);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(0x1.0000000000001p-150)) == 1);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(-0.0)) == 0x80000000u);
    TIDE_CHECK(tide_f_bits(tide_f_from_d(nan)) == 0x7FC00000u);
    TIDE_CHECK(tide_d_bits(tide_round_d(-0.5)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_round_d(0x1.0000000000001p+52) == 0x1.0000000000001p+52);
    TIDE_CHECK(tide_trunc_d(-1.75) == -1 && tide_floor_d(-1.75) == -2 && tide_ceil_d(-1.75) == -1);
    TIDE_CHECK(tide_abs_d(-DBL_MAX) == DBL_MAX && tide_sign_d(nan) == 0);
    TIDE_CHECK(!tide_is_finite_d(nan) && !tide_is_finite_d(infinity) && tide_is_finite_d(DBL_MAX));
    TIDE_CHECK(tide_clamp_d(nan, 2, 3) == 2 && tide_min_d(2, nan) == 2 && tide_max_d(nan, 3) == 3);
    TIDE_CHECK(tide_normalize_d3(tide_d3_splat(0)).x == 0);
    TIDE_CHECK(tide_normalize_d3(tide_d3(nan, 1, 2)).y == 0);
    TIDE_CHECK(tide_normalize_d2(tide_d2(DBL_TRUE_MIN, 0)).x == 0);
}

static uint64_t next_bits(uint64_t *state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state;
}

TIDE_TEST(double_text_and_json_roundtrip_all_exponents_and_boundary_values)
{
    static const struct { const char *text; uint64_t bits; } cases[] = {
        {"0", 0}, {"-0", UINT64_C(0x8000000000000000)},
        {"0.1", UINT64_C(0x3FB999999999999A)},
        {"9007199254740993", UINT64_C(0x4340000000000000)},
        {"1.00000000000000011102230246251565404236316680908203125", UINT64_C(0x3FF0000000000000)},
        {"1.000000000000000111022302462515654042363166809082031251", UINT64_C(0x3FF0000000000001)},
        {"1.7976931348623157e308", UINT64_C(0x7FEFFFFFFFFFFFFF)},
        {"2.2250738585072014e-308", UINT64_C(0x0010000000000000)},
        {"2.225073858507201e-308", UINT64_C(0x000FFFFFFFFFFFFF)},
        {"5e-324", 1}, {"2e-324", 0}, {"3e-324", 1},
        {"-1e-999999999999999999999999999999", UINT64_C(0x8000000000000000)},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        double value;
        TIDE_REQUIRE(tide_str_parse_double(tide_str_from_cstr(cases[i].text), &value));
        TIDE_CHECK(tide_d_bits(value) == cases[i].bits);
    }
    static const char *const invalid[] = {"", "+1", " 1", "NaN", "Infinity", "1e309", "1e-", "1.2.3"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        double value = 123;
        TIDE_CHECK(!tide_str_parse_double(tide_str_from_cstr(invalid[i]), &value) && value == 0);
    }
    uint64_t state = 17;
    for (uint64_t exponent = 0; exponent < 2047; exponent++) {
        for (uint64_t negative = 0; negative < 2; negative++) {
            const uint64_t bits = (negative << 63) | (exponent << 52)
                                | (next_bits(&state) & UINT64_C(0x000FFFFFFFFFFFFF));
            const double value = tide_d_from_bits(bits);
            const uint32_t mark = tide_scratch_mark();
            const tide_str text = tide_str_add_double(TIDE_STR_EMPTY, value, 0);
            double parsed;
            TIDE_REQUIRE(tide_str_parse_double(text, &parsed));
            TIDE_CHECK(tide_d_bits(parsed) == bits);
            tide_json json;
            tide_json_begin(&json, tide_json_add_double(TIDE_STR_EMPTY, value));
            TIDE_REQUIRE(tide_json_double(&json, &parsed) && tide_json_end(&json));
            TIDE_CHECK(tide_d_bits(parsed) == bits);
            tide_scratch_reset(mark);
        }
    }
    const uint32_t mark = tide_scratch_mark();
    TIDE_CHECK(tide_str_eq(tide_str_add_double(TIDE_STR_EMPTY, DBL_MAX, ('F' << 8) | 17),
                          tide_str_from_cstr("179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.00000000000000000")));
    TIDE_CHECK(tide_str_eq(tide_json_add_double(TIDE_STR_EMPTY, -0.0), tide_str_from_cstr("-0")));
    TIDE_CHECK(tide_str_eq(tide_json_add_double(TIDE_STR_EMPTY, tide_d_from_bits(UINT64_C(0x7FF0000000000000))),
                          tide_str_from_cstr("null")));
    tide_scratch_reset(mark);
}

static uint64_t hash_double(uint64_t hash, const double value)
{
    const uint64_t bits = value != value ? UINT64_C(0x7FF8000000000000) : tide_d_bits(value);
    for (unsigned i = 0; i < 8; i++) hash = (hash ^ ((bits >> (8 * i)) & 255u)) * UINT64_C(0x100000001B3);
    return hash;
}

TIDE_TEST(crossplatform_double_arithmetic_geometry_and_text)
{
    uint64_t state = 123, hash = UINT64_C(0xCBF29CE484222325);
    for (int i = 0; i < 10000; i++) {
        const double a = tide_d_from_bits(next_bits(&state));
        const double b = tide_d_from_bits(next_bits(&state));
        hash = hash_double(hash, tide_add_d(a, b));
        hash = hash_double(hash, tide_mul_d(a, b));
        hash = hash_double(hash, tide_div_d(a, b));
        hash = hash_double(hash, tide_sqrt_d(tide_abs_d(a)));
        hash = hash_double(hash, tide_round_d(a));
        hash = hash_double(hash, tide_i_from_d(a));
        hash = hash_double(hash, tide_f_from_d(a));
        const double x = (double)(int32_t)(uint32_t)next_bits(&state) * 0x1p-20;
        const double y = (double)(int32_t)(uint32_t)next_bits(&state) * 0x1p-20;
        const double z = (double)(int32_t)(uint32_t)next_bits(&state) * 0x1p-20;
        const tide_double3 v = tide_d3(x, y, z);
        const tide_double3 n = tide_normalize_d3(v);
        hash = hash_double(hash, tide_length_d3(v));
        hash = hash_double(hash, n.x);
        hash = hash_double(hash, n.y);
        hash = hash_double(hash, n.z);
        hash = hash_double(hash, tide_hash_d3(v));
        if (i % 19 == 0 && tide_is_finite_d(a)) {
            const uint32_t mark = tide_scratch_mark();
            const tide_str text = tide_str_add_double(TIDE_STR_EMPTY, a, 0);
            for (int k = 0; k < text.bytes; k++) hash = (hash ^ (uint8_t)text.ptr[k]) * UINT64_C(0x100000001B3);
            double value;
            TIDE_REQUIRE(tide_str_parse_double(text, &value));
            hash = hash_double(hash, value);
            tide_scratch_reset(mark);
        }
    }
    printf("    double hash: %016" PRIX64 "\n", hash);
    TIDE_CHECK(hash == UINT64_C(0x8D8432D419DF4D83));
}

TIDE_TEST(double_parser_handles_a_long_subnormal_halfway_value)
{
    static const char halfway[] =
        "0.00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000247032822920623272088284396434110686182529901307162382212792841250337753635"
        "1043759326499181808179961898982823477228588654633283551779698981993873980053909390631503565951557022"
        "6392290858392449105184435931802849936536152500319370457678249219365623669863658480757001585769269903"
        "7063119282795585513329278343384093519780155312465972635795746227664652728272200563740064854999770965"
        "9947045402082816622623785739345073633900796776193057750674017632467360096895134053553745851666113422"
        "3766678604162159680461914467291840300530057530849048765391711386591646239524912623653881879636239373"
        "2804238910186723484976682350898633885879256283027559956575244555072551893136908362547791869486679949"
        "68324049705821028513185451396213837722826145437693412532098591327667236328125";
    double value;
    TIDE_REQUIRE(tide_str_parse_double(tide_str_from_cstr(halfway), &value));
    TIDE_CHECK(tide_d_bits(value) == 0); // Exactly halfway rounds to even zero.
    char above[2200], below[2200];
    const size_t count = sizeof halfway - 1;
    memcpy(above, halfway, count);
    memset(above + count, '0', sizeof above - count - 2);
    above[sizeof above - 2] = '1';
    above[sizeof above - 1] = 0;
    TIDE_REQUIRE(tide_str_parse_double(tide_str_from_cstr(above), &value));
    TIDE_CHECK(tide_d_bits(value) == 1);
    memcpy(below, halfway, count);
    below[count - 1]--;
    memset(below + count, '9', sizeof below - count - 1);
    below[sizeof below - 1] = 0;
    TIDE_REQUIRE(tide_str_parse_double(tide_str_from_cstr(below), &value));
    TIDE_CHECK(tide_d_bits(value) == 0);
}
