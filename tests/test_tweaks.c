// tweaks.json as a dev build's host reads and writes it (tide/tweaks.h): values by name over the code's, and back
// as what differs from the code's. The values are a struct of the test's, as tidec's TideTweakValues would be.

#include <stddef.h>
#include <string.h>

#include "tide/tweaks.h"
#include "tide_test.h"

typedef struct values {
    float kick;
    int32_t rounds;
    bool automatic;
    float fog[4];
    uint8_t pace; // An enum of one byte
    double scale;
    int64_t big;
} values;

static const char *const paces[] = {"Slow", "Fast", "Wild"};
static const int32_t pace_values[] = {0, 1, 7};

static const tide_tweak table[] = {
    {"KickUp", TIDE_TWEAK_FLOAT, 1, offsetof(values, kick), 4, false, NULL, NULL, 0},
    {"Rounds", TIDE_TWEAK_INT, 1, offsetof(values, rounds), 4, false, NULL, NULL, 0},
    {"Auto", TIDE_TWEAK_BOOL, 1, offsetof(values, automatic), 1, false, NULL, NULL, 0},
    {"Fog", TIDE_TWEAK_FLOAT, 4, offsetof(values, fog), 4, true, NULL, NULL, 0},
    {"Combat.Pace", TIDE_TWEAK_ENUM, 1, offsetof(values, pace), 1, false, paces, pace_values, 3},
    {"Scale", TIDE_TWEAK_DOUBLE, 1, offsetof(values, scale), 8, false, NULL, NULL, 0},
    {"Big", TIDE_TWEAK_LONG, 1, offsetof(values, big), 8, false, NULL, NULL, 0},
};

static const void *code(void)
{
    static values v = {0.5f, 3, false, {0.1f, 0.2f, 0.3f, 1.0f}, 0, 1.0, 10};
    return &v;
}

static const tide_tweaks_api api = {table, 7, sizeof(values), code, "", NULL, NULL};

TIDE_TEST(tweaks_read_over_the_code)
{
    values v;
    const char *text = "\xEF\xBB\xBF{\n  \"KickUp\": 0.25, \"Auto\": true,\n  \"Fog\": [0.5, 0.5, 0.5, 1],\n"
                       "  \"Combat.Pace\": \"Wild\", \"Gone\": [1, \"x\", true], \"Big\": 5000000000\n}";
    TIDE_CHECK(tide_tweaks_read(&api, &v, text, strlen(text)));
    TIDE_CHECK(v.kick == 0.25f && v.rounds == 3 && v.automatic && v.fog[0] == 0.5f && v.fog[3] == 1.0f);
    TIDE_CHECK(v.pace == 7 && v.scale == 1.0 && v.big == 5000000000LL);
    // A value of the wrong shape is skipped; the rest still read
    const char *wrong = "{\"KickUp\": \"fast\", \"Rounds\": 9, \"Combat.Pace\": \"Nope\"}";
    TIDE_CHECK(tide_tweaks_read(&api, &v, wrong, strlen(wrong)));
    TIDE_CHECK(v.kick == 0.5f && v.rounds == 9 && v.pace == 0);
    // Text that isn't an object: the code's values, and false
    const char *broken = "{\"KickUp\" 0.25}";
    TIDE_CHECK(!tide_tweaks_read(&api, &v, broken, strlen(broken)));
    TIDE_CHECK(memcmp(&v, code(), sizeof v) == 0);
    TIDE_CHECK(tide_tweaks_read(&api, &v, "{}", 2) && memcmp(&v, code(), sizeof v) == 0);
}

TIDE_TEST(tweaks_write_what_differs)
{
    values v;
    memcpy(&v, code(), sizeof v);
    char out[512];
    size_t n = tide_tweaks_write(&api, &v, out, sizeof out);
    TIDE_CHECK(n == 3 && strcmp(out, "{}\n") == 0); // Nothing differs
    v.kick = 0.25f;
    v.fog[1] = 0.75f;
    v.pace = 1;
    v.automatic = true;
    n = tide_tweaks_write(&api, &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "{\n    \"KickUp\": 0.25,\n    \"Auto\": true,\n    \"Fog\": [0.100000001, 0.75, 0.300000012, 1],\n"
                           "    \"Combat.Pace\": \"Fast\"\n}\n") == 0);
    TIDE_CHECK(n == strlen(out));
    // ...and reads back as it was
    values back;
    TIDE_CHECK(tide_tweaks_read(&api, &back, out, n) && memcmp(&back, &v, sizeof v) == 0);
    // Cut short, it says how long the whole text is
    char small[8];
    TIDE_CHECK(tide_tweaks_write(&api, &v, small, sizeof small) == n && small[7] == '\0');
}
