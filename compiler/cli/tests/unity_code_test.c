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

static char *read_written(const char *dir, const char *file)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    size_t size = 0;
    return sys_read_file(path, &size);
}

// Whether a unity.json of `text` beside the physics fixture's game is refused
static bool refused(const char *dir, const char *schema, const char *outline, const char *text)
{
    char bad[1024], path[1024];
    snprintf(bad, sizeof bad, "%s/bad", dir);
    sys_remove_tree(bad);
    if (!sys_mkdirs(bad)) return false;
    snprintf(path, sizeof path, "%s/unity.json", bad);
    if (!sys_write_text(path, text)) return false;
    const unity_package wrong[] = {{bad, "Bad"}};
    return !unity_code_write(dir, schema, outline, wrong, 1);
}

// The physics packages' rules (their real unity.json files, over tests/unity_rules_physics: the schema and outline
// tide writes for a game listing box3d and box2d, cut to their components the rules name): a [Native] field is no C#
// field and isn't authored; a part's shapes, and anything whose field places it on its body, are shown in Play from
// where the body is; a mesh's lists are filled from a Mesh and drawn by their scale; a shape's points are shown in the
// GameObject's own space, and drawn as an outline; a joint's anchors and a height field are drawn; a field read back
// is shown in Play, never authored. A rule naming a component or a field the game hasn't, or points of another type,
// is an error.
TIDE_TEST(unity_code_writes_a_physics_packages_rules)
{
    const char *base = sys_env("TEMP") ? sys_env("TEMP") : sys_env("TMPDIR") ? sys_env("TMPDIR") : "/tmp";
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/tide_unity_physics_rules_test", base);
    sys_remove_tree(dir);
    TIDE_REQUIRE(sys_mkdirs(dir));
    char *schema = read_written(TIDE_UNITY_RULES_PHYSICS, "schema.json");
    char *outline = read_written(TIDE_UNITY_RULES_PHYSICS, "outline.json");
    TIDE_REQUIRE(schema && outline);
    const unity_package rules[] = {{TIDE_BOX3D_PACKAGE, "Box3D"}, {TIDE_BOX2D_PACKAGE, "Box2D"}};
    TIDE_REQUIRE(unity_code_write(dir, schema, outline, rules, 2));
    char *body = read_written(dir, "Components/Box3D/Body.cs");
    TIDE_CHECK(body && strstr(body, "public float gravityScale = 1f;") != NULL && strstr(body, "        public long id") == NULL);
    free(body);
    char *part = read_written(dir, "Components/Box3D/Part.cs");
    TIDE_CHECK(part && strstr(part, "    [global::Tide.TidePart(\"of\")]\n") != NULL && strstr(part, "        public long box") == NULL);
    free(part);
    char *sphere = read_written(dir, "Components/Box3D/Sphere.cs");
    TIDE_CHECK(sphere && strstr(sphere, "            offset = global::Tide.TideParts.Placed(offset, this, global::Tide.TideScale.By.Scale);\n") != NULL
               && strstr(sphere, "            radius = global::Tide.TideScale.Unscale(radius, transform, global::Tide.TideScale.By.LargestScale);\n") != NULL);
    free(sphere);
    char *mesh = read_written(dir, "Components/Box3D/MeshCollider.cs");
    TIDE_CHECK(mesh && strstr(mesh, "    [global::Tide.TideMesh(\"vertices\", \"indices\", \"scale\", \"offset\")]\n") != NULL
               && strstr(mesh, "public global::System.Collections.Generic.List<global::UnityEngine.Vector3> vertices") != NULL
               && strstr(mesh, "            offset = global::Tide.TideParts.Placed(offset, this, global::Tide.TideScale.By.Scale);\n") != NULL);
    free(mesh);
    char *joint = read_written(dir, "Components/Box3D/Joint.cs");
    TIDE_CHECK(joint && strstr(joint, "        [global::System.NonSerialized, global::Tide.TideReadBack] public global::UnityEngine.Vector3 force;\n") != NULL
               && strstr(joint, "    [global::Tide.TideJoint(\"a\", \"b\", \"anchorA\", \"anchorB\", \"rotationA\", \"rotationB\")]\n") != NULL);
    free(joint);
    char *joint2 = read_written(dir, "Components/Box2D/Joint.cs");
    TIDE_CHECK(joint2 && strstr(joint2, "    [global::Tide.TideJoint(\"a\", \"b\", \"anchorA\", \"anchorB\", \"angleA\", \"angleB\")]\n") != NULL);
    free(joint2);
    char *field = read_written(dir, "Components/Box3D/HeightFieldCollider.cs");
    TIDE_CHECK(field && strstr(field, "    [global::Tide.TideTerrain(\"heights\", \"holes\", \"columns\", \"rows\", \"scale\", "
                                      "\"minimumHeight\", \"maximumHeight\")]\n") != NULL
               && strstr(field, "public global::System.Collections.Generic.List<float> heights") != NULL);
    free(field);
    char *capsule = read_written(dir, "Components/Box2D/Capsule.cs");
    TIDE_CHECK(capsule && strstr(capsule, "    [global::Tide.TideOutline(new[] { \"a\", \"b\" }, \"radius\", \"\", false, false)]\n") != NULL
               && strstr(capsule, "            a = global::Tide.TideParts.Points(a, this);\n") != NULL
               && strstr(capsule, "            radius = global::Tide.TideScale.Unscale(radius, transform, global::Tide.TideScale.By.LargestScaleXY);\n") != NULL);
    free(capsule);
    char *polygon = read_written(dir, "Components/Box2D/Polygon.cs");
    TIDE_CHECK(polygon && strstr(polygon, "    [global::Tide.TideOutline(new[] { \"points\" }, \"radius\", \"\", true, true)]\n") != NULL
               && strstr(polygon, "            points = global::Tide.TideParts.Points(points, this);\n") != NULL);
    free(polygon);
    char *chain = read_written(dir, "Components/Box2D/Chain.cs");
    TIDE_CHECK(chain && strstr(chain, "    [global::Tide.TideOutline(new[] { \"points\" }, \"\", \"loop\", false, false)]\n") != NULL);
    free(chain);
    // components.json: what the converter bakes, none of the native or read back fields (Value keeps the layout)
    char *map = read_written(dir, "components.json");
    TIDE_CHECK(map && strstr(map, "{\"name\": \"Box3D.Body\", \"guid\": ") != NULL && strstr(map, "\"id\"") == NULL
               && strstr(map, "\"box\"") == NULL && strstr(map, "\"force\"") == NULL && strstr(map, "\"vertices\"") != NULL);
    free(map);
    // Rules of a component the game hasn't (one its package had once), of a field it hasn't, and a part of no body
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box3D.BoxMade\": {\"hidden\": true}}}\n"));
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box3D.Joint\": {\"readBack\": [\"forces\"]}}}\n"));
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box3D.Part\": {\"part\": {\"of\": \"of\", \"body\": \"Box3D.Corpse\"}}}}\n"));
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box3D.MeshCollider\": {\"mesh\": {\"indices\": \"indices\"}}}}\n"));
    // Points of a number, a joint without its anchors, an outline without its points
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box2D.Polygon\": {\"points\": [\"radius\"]}}}\n"));
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box3D.Joint\": {\"joint\": {\"a\": \"a\", \"b\": \"b\"}}}}\n"));
    TIDE_CHECK(refused(dir, schema, outline, "{\"components\": {\"Box2D.Chain\": {\"outline\": {\"closed\": true}}}}\n"));
    free(schema);
    free(outline);
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
