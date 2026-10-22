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
    TIDE_REQUIRE(unity_code_write(dir, schema, NULL));
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
    TIDE_REQUIRE(unity_code_write(dir, schema, NULL));
    free(schema);
    TIDE_CHECK(!sys_exists(to));
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
