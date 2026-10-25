// The game's C# for Unity (unity_code.h): the Sample's Packages/com.tide.game, which tide unity wrote from its
// schema.json there, is what it writes again, and a component's script GUID is its name's.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "tide_test.h"
#include "unity_code.h"

// Whether two files hold one text, whichever line endings a checkout gave them.
static bool same_text(const char *a_path, const char *b_path)
{
    size_t a_size = 0, b_size = 0;
    char *a = sys_read_file(a_path, &a_size), *b = sys_read_file(b_path, &b_size);
    bool same = a && b;
    size_t i = 0, k = 0;
    while (same && (i < a_size || k < b_size)) {
        if (i < a_size && a[i] == '\r') i++;
        else if (k < b_size && b[k] == '\r') k++;
        else if (i < a_size && k < b_size && a[i] == b[k]) i++, k++;
        else same = false;
    }
    if (!same) printf("    %s differs from %s\n", a_path, b_path);
    free(a);
    free(b);
    return same;
}

TIDE_TEST(unity_code_writes_the_samples_package)
{
    const char *base = sys_env("TEMP") ? sys_env("TEMP") : sys_env("TMPDIR") ? sys_env("TMPDIR") : "/tmp";
    char dir[1024], from[1024], to[1024];
    snprintf(dir, sizeof dir, "%s/tide_unity_code_test", base);
    sys_remove_tree(dir);
    TIDE_REQUIRE(sys_mkdirs(dir));
    snprintf(from, sizeof from, "%s/schema.json", TIDE_UNITY_SAMPLE_PACKAGE);
    size_t size = 0;
    char *schema = sys_read_file(from, &size);
    TIDE_REQUIRE(schema != NULL);
    TIDE_REQUIRE(unity_code_write(dir, schema, NULL, NULL, 0));
    free(schema);
    static const char *const files[] = {
        "TideGame.cs", "TideGame.cs.meta", "components.json", "Components/Ball.cs", "Components/Ball.cs.meta",
        "Components/Owner.cs", "Components/Spinner.cs",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(from, sizeof from, "%s/%s", TIDE_UNITY_SAMPLE_PACKAGE, files[i]);
        snprintf(to, sizeof to, "%s/%s", dir, files[i]);
        TIDE_CHECK(same_text(from, to));
    }
    // A stale component's script goes
    snprintf(to, sizeof to, "%s/Components/Gone.cs", dir);
    TIDE_REQUIRE(sys_write_text(to, "// A component the game no longer has\n"));
    snprintf(from, sizeof from, "%s/schema.json", TIDE_UNITY_SAMPLE_PACKAGE);
    schema = sys_read_file(from, &size);
    TIDE_REQUIRE(schema != NULL);
    TIDE_REQUIRE(unity_code_write(dir, schema, NULL, NULL, 0));
    free(schema);
    TIDE_CHECK(!sys_exists(to));
    sys_remove_tree(dir);
}

// A package's unity.json (tests/unity_rules) in the C# of its components: a field of its own hidden, a component of its
// own kept from Add Component, a shape for the Scene view, and a field by the scale shown in Play divided by it. A
// unity.json with rules tide doesn't read is an error.
TIDE_TEST(unity_code_writes_a_packages_rules)
{
    const char *base = sys_env("TEMP") ? sys_env("TEMP") : sys_env("TMPDIR") ? sys_env("TMPDIR") : "/tmp";
    char dir[1024], path[1024];
    snprintf(dir, sizeof dir, "%s/tide_unity_rules_test", base);
    sys_remove_tree(dir);
    TIDE_REQUIRE(sys_mkdirs(dir));
    snprintf(path, sizeof path, "%s/schema.json", TIDE_UNITY_SAMPLE_PACKAGE);
    size_t size = 0;
    char *schema = sys_read_file(path, &size);
    TIDE_REQUIRE(schema != NULL);
    const unity_package rules[] = {{TIDE_UNITY_RULES_PACKAGE, "Rules"}};
    TIDE_REQUIRE(unity_code_write(dir, schema, NULL, rules, 1));
    snprintf(path, sizeof path, "%s/Components/Spinner.cs", dir);
    char *spinner = sys_read_file(path, &size);
    TIDE_REQUIRE(spinner != NULL);
    TIDE_CHECK(strstr(spinner, "    [global::UnityEngine.AddComponentMenu(\"Tide/Spinner\")]\n"
                               "    [global::UnityEngine.DisallowMultipleComponent]\n"
                               "    [global::UnityEngine.Icon(global::Tide.TideIcon.Component)]\n"
                               "    [global::Tide.TideShape(\"sphere\", \"speed\", \"\", \"\")]\n") != NULL);
    TIDE_CHECK(strstr(spinner, "        [global::UnityEngine.Tooltip(\"By the GameObject's scale, as a collider's size is\")]\n"
                               "        public float speed = 1f;\n") != NULL);
    TIDE_CHECK(strstr(spinner, "        [global::UnityEngine.HideInInspector] // Its package's own\n"
                               "        public int badge = 83;\n") != NULL);
    TIDE_CHECK(strstr(spinner, "            speed = global::Tide.TideScale.Unscale(speed, transform, global::Tide.TideScale.By.LargestScale);\n") != NULL);
    free(spinner);
    snprintf(path, sizeof path, "%s/Components/Owner.cs", dir);
    char *owner = sys_read_file(path, &size);
    TIDE_CHECK(owner && strstr(owner, "    [global::UnityEngine.AddComponentMenu(\"\")]\n") != NULL && !strstr(owner, "Tide/Owner"));
    free(owner);
    // A capsule without its height
    char bad[1024];
    snprintf(bad, sizeof bad, "%s/bad", dir);
    TIDE_REQUIRE(sys_mkdirs(bad));
    snprintf(path, sizeof path, "%s/unity.json", bad);
    TIDE_REQUIRE(sys_write_text(path, "{\"components\": {\"Spinner\": {\"shape\": {\"capsule\": \"speed\"}}}}\n"));
    const unity_package wrong[] = {{bad, "Bad"}};
    TIDE_CHECK(!unity_code_write(dir, schema, NULL, wrong, 1));
    free(schema);
    sys_remove_tree(dir);
}

TIDE_TEST(unity_code_guid_is_the_names)
{
    char a[33], b[33];
    unity_code_guid("Ball", a);
    unity_code_guid("Ball", b);
    TIDE_CHECK(strcmp(a, b) == 0 && strlen(a) == 32);
    TIDE_CHECK(strcmp(a, "ce03b43f744b7347571b2cbb98249f29") == 0); // The Sample's Ball, which its prefabs name
    unity_code_guid("Spinner", b);
    TIDE_CHECK(strcmp(a, b) != 0);
}
