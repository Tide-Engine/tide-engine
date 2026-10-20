// Unity's prefabs and scenes as Tide prefabs (unity_assets.h), from a project
// Unity itself wrote (tests/unity_project: MakeFixture.cs.txt made it): a
// prefab, a variant of it changing a field, a prefab holding the variant with
// a field of a part nested two prefabs deep changed and an entity reference to
// that part, and a scene placing the holder and changing the variant's field
// from there, with an entity of its own.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide_test.h"
#include "unity_assets.h"

static const char *text_of(const unity_sources *s, const char *path)
{
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->items[i].path, path) == 0) return s->items[i].text;
    }
    return NULL;
}

// Whether `path`'s text is `expected`, saying how it isn't.
static bool reads(const unity_sources *s, const char *path, const char *expected)
{
    const char *got = text_of(s, path);
    if (got && strcmp(got, expected) == 0) return true;
    printf("    %s is:\n%s\n    not:\n%s\n", path, got ? got : "(missing)", expected);
    return false;
}

#define IDENTITY "localRotation = quaternion(0, 0, 0, 1), localScale = float3(1, 1, 1)"

TIDE_TEST(unity_prefabs_and_scenes_as_tide_prefabs)
{
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_FIXTURE, &s));
    TIDE_CHECK(s.count == 5); // Unity.Source's declaration and the four assets
    // Unity.Source, and the scenes the match has: Build Settings' first one on (Level) loaded as Main starts, and
    // the editor's command to have the scenes it has open
    const char *declared = text_of(&s, UNITY_SOURCE_PATH);
    TIDE_REQUIRE(declared != NULL);
    TIDE_CHECK(strstr(declared, "    component Source\n") != NULL);
    TIDE_CHECK(strstr(declared, "    event(Spawned) Begin(with Main, mut Scenes scenes)\n    {\n"
                                "        if (scenes.opened) return; // The editor's command came first\n"
                                "        scenes.loaded[1] = Scene.Load(Level);\n") != NULL);
    TIDE_CHECK(strstr(declared, "    command Open\n") != NULL);
    TIDE_CHECK(strstr(declared, "            case 1: scenes.loaded[1] = Scene.Load(Level); break;\n") != NULL);
    TIDE_CHECK(strstr(declared, "Scene.Load(Thing)") == NULL); // A prefab is no scene to open
    // A Unity scene is a Tide scene, whose entities are its prefab's, spawned into it as it loads
    TIDE_CHECK(strstr(declared, "\nscene Level { }\n") != NULL);
    TIDE_CHECK(strstr(declared, "    event(Spawned) FillLevel(with Level)\n    {\n        Spawn(Unity.Contents.Level);\n    }\n") != NULL);
    // The Tip goes under Thing through Visual, which is no entity: Visual's placement (up 1, twice the size) is the
    // Tip's. The inactive Off isn't there.
    TIDE_CHECK(reads(&s, "Assets/Fixture/Thing.prefab",
        "// From Assets/Fixture/Thing.prefab, as Tide's package for Unity writes it (TideEntity)\n"
        "prefab Thing\n{\n"
        "    Unity.Source { asset = 2, node = 0 };\n"
        "    Transform { localPosition = float3(0, 0, 0), " IDENTITY " };\n"
        "    Health { hp = 10, armor = 1 };\n"
        "    entity Arm // Arm\n    {\n"
        "        Unity.Source { asset = 2, node = 1 };\n"
        "        Transform { localPosition = float3(1, 0, 0), " IDENTITY " };\n"
        "        Part;\n    }\n"
        "    entity Tip // Tip\n    {\n"
        "        Unity.Source { asset = 2, node = 2 };\n"
        "        Transform { localPosition = float3(0, 2, 0), localRotation = quaternion(0, 0, 0, 1), localScale = float3(2, 2, 2) };\n"
        "        Part { length = 2 };\n    }\n"
        "}\n"));
    // The variant sets hp alone: armor is its prefab's
    const char *variant = text_of(&s, "Assets/Fixture/Variant.prefab");
    TIDE_CHECK(variant && strstr(variant, "    Health { hp = 20, armor = 1 };\n"));
    // The scene: the Holder it places (first, as its SceneRoots say), moved, with the variant inside it, whose armor
    // the scene sets over the variant's hp; the Tip nested two prefabs deep, longer, as the Holder sets it; the
    // Holder's reference to that Tip as a path from the scene's own entity; the Mark of the scene's own
    TIDE_CHECK(reads(&s, "Assets/Fixture/Level.unity",
        "// From Assets/Fixture/Level.unity, as Tide's package for Unity writes it (TideEntity)\n"
        "namespace Unity.Contents;\n\n"
        "prefab Level\n{\n"
        "    Unity.Source { asset = 1, node = 0 };\n"
        "    entity Holder // Holder\n    {\n"
        "        Unity.Source { asset = 1, node = 1 };\n"
        "        Transform { localPosition = float3(2, 0, 0), " IDENTITY " };\n"
        "        Link { target = Holder.Variant.Tip, self = this };\n"
        "        entity Variant // Variant\n        {\n"
        "            Unity.Source { asset = 1, node = 2 };\n"
        "            Transform { localPosition = float3(5, 0, 0), " IDENTITY " };\n"
        "            Health { hp = 20, armor = 5 };\n"
        "            entity Arm // Arm\n            {\n"
        "                Unity.Source { asset = 1, node = 3 };\n"
        "                Transform { localPosition = float3(1, 0, 0), " IDENTITY " };\n"
        "                Part;\n            }\n"
        "            entity Tip // Tip\n            {\n"
        "                Unity.Source { asset = 1, node = 4 };\n"
        "                Transform { localPosition = float3(0, 2, 0), localRotation = quaternion(0, 0, 0, 1), localScale = float3(2, 2, 2) };\n"
        "                Part { length = 3 };\n            }\n"
        "        }\n"
        "    }\n"
        "    entity Mark // Mark\n    {\n"
        "        Unity.Source { asset = 1, node = 5 };\n"
        "        Transform { localPosition = float3(0, 0, 7), " IDENTITY " };\n"
        "        Part { length = 9 };\n    }\n"
        "}\n"));
    const char *holder = text_of(&s, "Assets/Fixture/Holder.prefab");
    TIDE_CHECK(holder && strstr(holder, "    Link { target = Variant.Tip, self = this };\n"));
    // Each node's GameObject, for Unity to show it through
    TIDE_CHECK(s.map && strstr(s.map, "\"path\": \"Assets/Fixture/Level.unity\", \"scene\": true, \"nodes\": "
                                      "[\"\", \"Holder\", \"Holder/Variant\", \"Holder/Variant/Arm\", \"Holder/Variant/Visual/Tip\", \"Mark\"]"));
    unity_sources_free(&s);
}

// A folder with no prefabs or scenes of TideEntity objects: no prefabs, and a map of no assets.
TIDE_TEST(unity_assets_none_without_tide_entities)
{
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_FIXTURE "/../MissingFolder", &s));
    TIDE_CHECK(s.count == 0);
    TIDE_CHECK(s.map && strstr(s.map, "\"assets\": [") != NULL);
    unity_sources_free(&s);
}
