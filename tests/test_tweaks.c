// A dev build's tweaks as a host takes and writes them (tide/tweaks.h): the values file tidec writes, a value as a
// .tweaks file writes it, and writing one into its file over the line that sets it, keeping the rest. The values are a
// struct of the test's, as tidec's TideTweakValues would be.

#include <stddef.h>
#include <stdio.h>
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
    float aim[3];
} values;

static const char *const paces[] = {"Slow", "Fast", "Wild"};
static const int32_t pace_values[] = {0, 1, 7};

static tide_tweak table[] = {
    {"KickUp", TIDE_TWEAK_FLOAT, 1, offsetof(values, kick), 4, false, NULL, NULL, 0, "float", "", true},
    {"Rounds", TIDE_TWEAK_INT, 1, offsetof(values, rounds), 4, false, NULL, NULL, 0, "int", "", true},
    {"Auto", TIDE_TWEAK_BOOL, 1, offsetof(values, automatic), 1, false, NULL, NULL, 0, "bool", "", true},
    {"Fog", TIDE_TWEAK_FLOAT, 4, offsetof(values, fog), 4, true, NULL, NULL, 0, "Color", "", true},
    {"Combat.Pace", TIDE_TWEAK_ENUM, 1, offsetof(values, pace), 1, false, paces, pace_values, 3, "Combat.Rate", "", true},
    {"Scale", TIDE_TWEAK_DOUBLE, 1, offsetof(values, scale), 8, false, NULL, NULL, 0, "double", "", true},
    {"Gun.aim", TIDE_TWEAK_FLOAT, 3, offsetof(values, aim), 4, false, NULL, NULL, 0, "float3", "", true},
};

static const tide_tweaks_api api = {table, 7, sizeof(values), 0x1234u, "", NULL, NULL};

static size_t file_of(const values *v, uint8_t *out, const uint64_t layout)
{
    const uint32_t size = sizeof *v, zero = 0;
    memcpy(out, TIDE_TWEAKS_MAGIC, 8);
    memcpy(out + 8, &layout, 8);
    memcpy(out + 16, &size, 4);
    memcpy(out + 20, &zero, 4);
    memcpy(out + 24, v, sizeof *v);
    return 24 + sizeof *v;
}

TIDE_TEST(tweaks_load_this_builds_values)
{
    values v = {0}, sent = {0.25f, 5, true, {0.5f, 0.5f, 0.5f, 1}, 7, 2.0, {1, 2, 3}};
    uint8_t bytes[256];
    size_t n = file_of(&sent, bytes, 0x1234u);
    TIDE_CHECK(tide_tweaks_load(&api, &v, bytes, n) && memcmp(&v, &sent, sizeof v) == 0);
    // Another build's layout, a file cut short, or not one at all: nothing taken
    values kept = v;
    n = file_of(&sent, bytes, 0x9999u);
    TIDE_CHECK(!tide_tweaks_load(&api, &v, bytes, n));
    n = file_of(&sent, bytes, 0x1234u);
    TIDE_CHECK(!tide_tweaks_load(&api, &v, bytes, n - 1));
    bytes[0] = 'X';
    TIDE_CHECK(!tide_tweaks_load(&api, &v, bytes, n));
    TIDE_CHECK(memcmp(&v, &kept, sizeof v) == 0);
}

TIDE_TEST(tweaks_format_as_tide_writes_them)
{
    const values v = {0.25f, -5, true, {1, 0.5f, 0, 1}, 7, 0.1, {1, -2.5f, 1e10f}};
    char out[128];
    tide_tweaks_format(&table[0], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "0.25") == 0);
    tide_tweaks_format(&table[1], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "-5") == 0);
    tide_tweaks_format(&table[2], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "true") == 0);
    tide_tweaks_format(&table[3], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "Color(1, 0.5, 0, 1)") == 0);
    tide_tweaks_format(&table[4], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "Combat.Rate.Wild") == 0);
    tide_tweaks_format(&table[5], &v, out, sizeof out);
    TIDE_CHECK(strcmp(out, "0.10000000000000001d") == 0);
    tide_tweaks_format(&table[6], &v, out, sizeof out); // No exponents: Tide's numbers have none
    TIDE_CHECK(strcmp(out, "float3(1, -2.5, 10000000000)") == 0);
}

static void read_back(const char *path, char *text, const size_t room)
{
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(text, 1, room - 1, f) : 0;
    if (f) fclose(f);
    text[n] = '\0';
}

TIDE_TEST(tweaks_save_into_their_file)
{
    const char *path = "test_tweaks_save.tweaks";
    FILE *f = fopen(path, "wb");
    TIDE_REQUIRE(f != NULL);
    fputs("// Recoil\nKickUp   =  0.5 ;  // too soft\nRounds = KickUp * 4;\n\nGun.aimed = 1;\n", f);
    fclose(f);
    tide_tweak kick = table[0], aim = table[6], pace = table[4];
    kick.file = aim.file = pace.file = path;
    const values v = {0.75f, 3, false, {0}, 1, 0, {0, 1, 0}};
    TIDE_CHECK(tide_tweaks_save(&kick, &v));
    TIDE_CHECK(tide_tweaks_save(&aim, &v)); // Gun.aimed isn't Gun.aim: a line of its own
    char text[512];
    read_back(path, text, sizeof text);
    TIDE_CHECK(strcmp(text, "// Recoil\nKickUp   =  0.75 ;  // too soft\nRounds = KickUp * 4;\n\nGun.aimed = 1;\n"
                            "Gun.aim = float3(0, 1, 0);\n") == 0);
    // A file that isn't there yet is made
    remove(path);
    TIDE_CHECK(tide_tweaks_save(&pace, &v));
    read_back(path, text, sizeof text);
    TIDE_CHECK(strcmp(text, "Combat.Pace = Combat.Rate.Fast;\n") == 0);
    remove(path);
}
