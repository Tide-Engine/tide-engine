// Unity's prefabs and scenes as Tide prefabs (unity_assets.h), from a project
// Unity itself wrote (tests/unity_project: MakeFixture.cs.txt made it): a
// prefab, a variant of it changing a field, a prefab holding the variant with
// a field of a part nested two prefabs deep changed and an entity reference to
// that part, and a scene placing the holder and changing the variant's field
// from there, with an entity of its own.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
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
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_FIXTURE, NULL, 0, &s));
    TIDE_CHECK(s.count == 5); // UnityEngine.Source's declaration and the four assets
    // UnityEngine.Source, and the scenes the match has: Build Settings' first one on (Level) loaded as Main starts, and
    // the editor's command to have the scenes it has open
    const char *declared = text_of(&s, UNITY_SOURCE_PATH);
    TIDE_REQUIRE(declared != NULL);
    TIDE_CHECK(strstr(declared, "    component Source\n") != NULL);
    TIDE_CHECK(strstr(declared, "    event(Spawned) Begin(with Main, mut LoadedScenes scenes)\n    {\n"
                                "        if (scenes.opened) return; // The editor's command came first\n"
                                "        scenes.loaded[1] = Scene.Load(UnityEngine.Scenes.Fixture.Level);\n") != NULL);
    TIDE_CHECK(strstr(declared, "    command OpenScene\n") != NULL);
    TIDE_CHECK(strstr(declared, "            case 1: scenes.loaded[1] = Scene.Load(UnityEngine.Scenes.Fixture.Level); break;\n") != NULL);
    TIDE_CHECK(strstr(declared, "Scene.Load(Thing)") == NULL); // A prefab is no scene to open
    TIDE_CHECK(strstr(declared, "CloseScene") == NULL); // Scenes loaded by hand during Play are local: none are sent
    // A Unity scene is a Tide scene, whose entities are its prefab's, spawned into it as it loads
    TIDE_CHECK(strstr(declared, "namespace UnityEngine.Scenes.Fixture\n{\n    scene Level { } // Level.unity\n}\n") != NULL);
    TIDE_CHECK(strstr(declared, "    event(Spawned) FillFixture_Level(with UnityEngine.Scenes.Fixture.Level)\n    {\n        Spawn(UnityEngine.SceneContents.Fixture.Level);\n    }\n") != NULL);
    // The Tip goes under Thing through Visual, which is no entity: Visual's placement (up 1, twice the size) is the
    // Tip's. The inactive Off isn't there.
    TIDE_CHECK(reads(&s, "Assets/Fixture/Thing.prefab",
        "// From Assets/Fixture/Thing.prefab, as Tide's package for Unity writes it (TideEntity)\n"
        "namespace UnityEngine.Prefabs.Fixture;\n\n"
        "prefab Thing\n{\n"
        "    UnityEngine.Source { asset = 2, node = 0 };\n"
        "    Transform { localPosition = float3(0, 0, 0), " IDENTITY " };\n"
        "    Health { hp = 10, armor = 1 };\n"
        "    entity Arm // Arm\n    {\n"
        "        UnityEngine.Source { asset = 2, node = 1 };\n"
        "        Transform { localPosition = float3(1, 0, 0), " IDENTITY " };\n"
        "        Part;\n    }\n"
        "    entity Tip // Tip\n    {\n"
        "        UnityEngine.Source { asset = 2, node = 2 };\n"
        "        Transform { localPosition = float3(0, 2, 0), localRotation = quaternion(0, 0, 0, 1), localScale = float3(2, 2, 2) };\n"
        "        Part { length = 2 };\n    }\n"
        "}\n"));
    // The variant sets hp alone: armor is its prefab's
    const char *variant = text_of(&s, "Assets/Fixture/Variant.prefab");
    TIDE_CHECK(variant && strstr(variant, "    Health { hp = 20, armor = 1 };\n"));
    // The scene: the Holder it places (first, as its SceneRoots say), moved, with the variant inside it, whose armor
    // the scene sets over the variant's hp; the Tip nested two prefabs deep, longer, as the Holder sets it; the
    // Holder's reference to that Tip as a path from the scene's own entity; the scene's own GameObject named M#1/k,
    // a name with '#' and '/' in it (an entity named M_1_k)
    TIDE_CHECK(reads(&s, "Assets/Fixture/Level.unity",
        "// From Assets/Fixture/Level.unity, as Tide's package for Unity writes it (TideEntity)\n"
        "namespace UnityEngine.SceneContents.Fixture;\n\n"
        "prefab Level\n{\n"
        "    UnityEngine.Source { asset = 1, node = 0 };\n"
        "    entity Holder // Holder\n    {\n"
        "        UnityEngine.Source { asset = 1, node = 1 };\n"
        "        Transform { localPosition = float3(2, 0, 0), " IDENTITY " };\n"
        "        Link { target = Holder.Variant.Tip, self = this };\n"
        "        entity Variant // Variant\n        {\n"
        "            UnityEngine.Source { asset = 1, node = 2 };\n"
        "            Transform { localPosition = float3(5, 0, 0), " IDENTITY " };\n"
        "            Health { hp = 20, armor = 5 };\n"
        "            entity Arm // Arm\n            {\n"
        "                UnityEngine.Source { asset = 1, node = 3 };\n"
        "                Transform { localPosition = float3(1, 0, 0), " IDENTITY " };\n"
        "                Part;\n            }\n"
        "            entity Tip // Tip\n            {\n"
        "                UnityEngine.Source { asset = 1, node = 4 };\n"
        "                Transform { localPosition = float3(0, 2, 0), localRotation = quaternion(0, 0, 0, 1), localScale = float3(2, 2, 2) };\n"
        "                Part { length = 3 };\n            }\n"
        "        }\n"
        "    }\n"
        "    entity M_1_k // M#1/k\n    {\n"
        "        UnityEngine.Source { asset = 1, node = 5 };\n"
        "        Transform { localPosition = float3(0, 0, 7), " IDENTITY " };\n"
        "        Part { length = 9 };\n    }\n"
        "}\n"));
    const char *holder = text_of(&s, "Assets/Fixture/Holder.prefab");
    TIDE_CHECK(holder && strstr(holder, "    Link { target = Variant.Tip, self = this };\n"));
    // Each node's GameObject, for Unity to show it through: a name's own '#' and '/' with a '\' before them
    TIDE_CHECK(s.map && strstr(s.map, "\"path\": \"Assets/Fixture/Level.unity\", \"scene\": true, \"nodes\": "
                                      "[\"\", \"Holder\", \"Holder/Variant\", \"Holder/Variant/Arm\", \"Holder/Variant/Visual/Tip\", "
                                      "\"M\\\\#1\\\\/k\"]"));
    unity_sources_free(&s);
}

typedef struct copying {
    const char *from, *to;
} copying;

static void copy_entry(void *user, const char *name, bool is_dir);

// Every file under `from` into `to`.
static void copy_tree(const char *from, const char *to)
{
    sys_mkdirs(to);
    copying c = {from, to};
    sys_list(from, copy_entry, &c);
}

static void copy_entry(void *user, const char *name, bool is_dir)
{
    const copying *c = user;
    char from[1024], to[1024];
    snprintf(from, sizeof from, "%s/%s", c->from, name);
    snprintf(to, sizeof to, "%s/%s", c->to, name);
    if (is_dir) {
        copy_tree(from, to);
        return;
    }
    size_t size = 0;
    char *body = sys_read_file(from, &size);
    if (body) sys_write_file(to, body, size);
    free(body);
}

// A scene the editor hasn't saved its changes to: the copy it wrote of it as it is, at Play, in the project's
// Library/Tide/unsaved, is read in its place, and changes the stamp; the scene again once the copy's gone.
TIDE_TEST(unity_unsaved_scene_read_from_the_editor_copy)
{
    sys_remove_tree(TIDE_UNITY_SCRATCH);
    copy_tree(TIDE_UNITY_FIXTURE "/..", TIDE_UNITY_SCRATCH);
    const char *assets = TIDE_UNITY_SCRATCH "/Assets";
    const unsigned long long saved = unity_assets_stamp(assets);
    size_t size = 0;
    char *scene = sys_read_file(TIDE_UNITY_FIXTURE "/Fixture/Level.unity", &size);
    TIDE_REQUIRE(scene != NULL);
    char *nine = strstr(scene, "values: length = 9");
    TIDE_REQUIRE(nine != NULL);
    nine[strlen("values: length = ")] = '4'; // The editor's unsaved change: the length 9 is 4
    const char *copy = TIDE_UNITY_SCRATCH "/" UNITY_UNSAVED_FOLDER "Assets/Fixture/Level.unity";
    TIDE_REQUIRE(sys_mkdirs(TIDE_UNITY_SCRATCH "/" UNITY_UNSAVED_FOLDER "Assets/Fixture"));
    TIDE_REQUIRE(sys_write_file(copy, scene, size));
    free(scene);
    TIDE_CHECK(unity_assets_stamp(assets) != saved);
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(assets, NULL, 0, &s));
    const char *level = text_of(&s, "Assets/Fixture/Level.unity");
    TIDE_CHECK(level && strstr(level, "        Part { length = 4 };\n") && !strstr(level, "length = 9"));
    unity_sources_free(&s);
    TIDE_REQUIRE(sys_remove(copy));
    TIDE_CHECK(unity_assets_stamp(assets) == saved);
    TIDE_REQUIRE(unity_assets_read(assets, NULL, 0, &s));
    level = text_of(&s, "Assets/Fixture/Level.unity");
    TIDE_CHECK(level && strstr(level, "        Part { length = 9 };\n"));
    unity_sources_free(&s);

    // A scene the editor never saved: its copy is the scene UnityEngine.Scenes.Untitled, Unity's path for it ""
    scene = sys_read_file(TIDE_UNITY_FIXTURE "/Fixture/Level.unity", &size);
    TIDE_REQUIRE(scene != NULL);
    const char *untitled = TIDE_UNITY_SCRATCH "/" UNITY_UNSAVED_FOLDER UNITY_UNTITLED_SCENE;
    TIDE_REQUIRE(sys_write_file(untitled, scene, size));
    TIDE_CHECK(unity_assets_stamp(assets) != saved);
    TIDE_REQUIRE(unity_assets_read(assets, NULL, 0, &s));
    const char *contents = text_of(&s, UNITY_UNTITLED_SCENE);
    TIDE_CHECK(contents && strstr(contents, "namespace UnityEngine.SceneContents;\n\nprefab Untitled\n{\n")
               && strstr(contents, "    entity M_1_k // M#1/k\n"));
    TIDE_CHECK(s.map && strstr(s.map, "\"path\": \"\", \"scene\": true"));
    const char *declared = text_of(&s, UNITY_SOURCE_PATH);
    TIDE_CHECK(declared && strstr(declared, "namespace UnityEngine.Scenes\n{\n    scene Untitled { } // Untitled.unity\n}\n"));
    unity_sources_free(&s);
    // ...whose name a saved Assets/Untitled.unity takes: an error that says what to do
    TIDE_REQUIRE(sys_write_file(TIDE_UNITY_SCRATCH "/Assets/Untitled.unity", scene, size));
    TIDE_REQUIRE(sys_write_text(TIDE_UNITY_SCRATCH "/Assets/Untitled.unity.meta",
                                "fileFormatVersion: 2\nguid: 5e1f0c2a9d3b4e6f8a7b6c5d4e3f2a1b\n"));
    free(scene);
    TIDE_CHECK(!unity_assets_read(assets, NULL, 0, &s));
    unity_sources_free(&s);
    sys_remove_tree(TIDE_UNITY_SCRATCH);
}

// The game's own components (com.tide.game's, as its components.json says how they're serialized), from
// tests/unity_components, written as Unity writes them: a prefab's fields of each shape (a struct holding an entity,
// a set, a char, an array, a list of structs among them), a field the component no longer has and a component the
// game no longer has, both left out; and a scene's instance of it changing fields (an element of each list, a color's
// channel, its entity to one of the scene's own, a struct's field, an entity in a list's struct, a set's item, the
// char) and removing a component.
TIDE_TEST(unity_components_as_tide_components)
{
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_COMPONENTS_FIXTURE, NULL, 0, &s));
    const char *ship = text_of(&s, "Assets/Ship.prefab");
    TIDE_REQUIRE(ship != NULL);
    TIDE_CHECK(strstr(ship, "    Ship { speed = 2.5, heading = float3(0, 0, 1), tint = Color(1, 0.5, 0, 1), title = \"Flag \\\"one\\\"\", "
                            "crew = Pilot, marks = [1, 2], stops = [float2(1, 2), float2(3, 4)], mode = Mode.Fast, fixed = true, "
                            "cargo = Hold { mass = 12, owner = Pilot }, tags = [5, 6], initial = 'A', slots = [1, 2, 3], "
                            "holds = [Hold { mass = 1, owner = default }, Hold { mass = 2, owner = this }] };\n"
                            "    entity Pilot // Pilot\n") != NULL);
    TIDE_CHECK(strstr(ship, "        Cargo { mass = 4 };\n") != NULL);
    TIDE_CHECK(strstr(ship, "Removed") == NULL && strstr(ship, "gone") == NULL);
    const char *fleet = text_of(&s, "Assets/Fleet.unity");
    TIDE_REQUIRE(fleet != NULL);
    TIDE_CHECK(strstr(fleet, "    entity Flagship // Flagship\n    {\n") != NULL);
    TIDE_CHECK(strstr(fleet, "        Ship { speed = 7, heading = float3(0, 0, 1), tint = Color(1, 1, 0, 1), title = \"Flag \\\"one\\\"\", "
                             "crew = Dock, marks = [1, 2, 9], stops = [float2(1, 2), float2(3, 8)], mode = Mode.Fast, "
                             "extra = float2(1, 2), fixed = true, cargo = Hold { mass = 20, owner = Flagship.Pilot }, tags = [5, 7], "
                             "initial = 'B', slots = [1, 2, 3], holds = [Hold { mass = 1, owner = Dock }, Hold { mass = 2, owner = this }] };\n") != NULL);
    TIDE_CHECK(strstr(fleet, "Cargo") == NULL);
    unity_sources_free(&s);
}

// Box3D's and Box2D's own components on TideEntity GameObjects, baked by their packages' unity.json
// (tests/unity_physics, whose components.json is as tide writes it: no [Native] field, as Body's id, and no field
// that's read back, as Joint's force): shapes by the GameObject's scale, as Unity's colliders' sizes are (a size
// without the scale's sign, an offset with it); the packages' own fields and components left out; a part's shapes
// where its GameObject is on its body, the body above it or the one it names, in 3D and 2D; a joint naming its
// bodies; a mesh's lists; and their Tide for a Unity project, each a source of its package's.
TIDE_TEST(unity_physics_components_by_their_rules)
{
    const unity_package packages[] = {{TIDE_BOX3D_PACKAGE, "Box3D"}, {TIDE_BOX2D_PACKAGE, "Box2D"}};
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_PHYSICS_FIXTURE, packages, 2, &s));
    const char *yard = text_of(&s, "Assets/Yard.unity");
    TIDE_REQUIRE(yard != NULL);
    // A floor scaled (10, 1, 10): no id or stepType, which Unity has of it
    TIDE_CHECK(strstr(yard, "        Box3D.Body { type = Box3D.BodyType.Static, gravityScale = 1 };\n"
                            "        Box3D.Box { size = float3(10, 1, 10), offset = float3(0, 0.25, 0), sensor = false };\n") != NULL);
    // A ball scaled (2, 3, 1)
    TIDE_CHECK(strstr(yard, "        Box3D.Sphere { radius = 1.5, offset = float3(0, 1.5, 0), sensor = false };\n") != NULL);
    // A pillar scaled (2, 1.5, 1)
    TIDE_CHECK(strstr(yard, "        Box3D.Capsule { radius = 1, height = 3, offset = float3(0, 0, 0), sensor = false };\n") != NULL);
    // A mirrored box, and no CircleMade, which is Box2D's own
    TIDE_CHECK(strstr(yard, "        Box3D.Box { size = float3(2, 1, 1), offset = float3(-2, 0, 0), sensor = true };\n    }\n") != NULL);
    TIDE_CHECK(strstr(yard, "CircleMade") == NULL && strstr(yard, "stepType") == NULL && strstr(yard, "id = 7") == NULL);
    // A cart turned upside down about z: its wheel, scaled 2, is a part of it with no body named, its sphere where the
    // wheel puts it on the cart; its fin, turned, names the cart, its box where it is but unturned
    TIDE_CHECK(strstr(yard, "            Box3D.Sphere { radius = 0.5, offset = float3(1, -0.3, 0), sensor = false };\n"
                            "            Box3D.Part { of = default };\n") != NULL);
    TIDE_CHECK(strstr(yard, "            Box3D.Box { size = float3(1, 1, 1), offset = float3(0, 0.5, -1), sensor = false };\n"
                            "            Box3D.Part { of = Cart };\n") != NULL);
    // A hinge between the cart and the ball, whose force is read back, never authored
    TIDE_CHECK(strstr(yard, "        Box3D.Joint { a = Cart, b = Ball, anchorA = float3(0, 1, 0) };\n"
                            "        Box3D.RevoluteJoint { enableMotor = true };\n") != NULL);
    // A ramp's mesh: its triangle as Unity has it, its scale the GameObject's
    TIDE_CHECK(strstr(yard, "        Box3D.MeshCollider { vertices = [float3(0, 0, 0), float3(0, 0, 1), float3(1, 0, 0)], "
                            "indices = [0, 1, 2], scale = float3(2, 1, 1), offset = float3(0, 0, 0) };\n") != NULL);
    // A kite in 2D, turned upside down: its tail's circle where the tail puts it, in the kite's frame
    TIDE_CHECK(strstr(yard, "        Box2D.Body { type = Box2D.BodyType.Dynamic, gravityScale = 0.25 };\n"
                            "        Box2D.Box { size = float2(1, 2), offset = float2(0, 0) };\n") != NULL);
    TIDE_CHECK(strstr(yard, "            Box2D.Circle { radius = 0.5, offset = float2(2, 1) };\n"
                            "            Box2D.Part { of = default };\n") != NULL);
    const char *glue = text_of(&s, "Library/Tide/Unity.Box3D.tide");
    TIDE_CHECK(glue && strstr(glue, "    system StartAtTransform(mut Body body, Transform transform)\n") != NULL
               && strstr(glue, "    system PartOfTheBodyAbove(mut Part part, World world)\n") != NULL);
    glue = text_of(&s, "Library/Tide/Unity.Box2D.tide");
    TIDE_CHECK(glue && strstr(glue, "    system PartOfTheBodyAbove(mut Part part, World world)\n") != NULL);
    TIDE_CHECK(s.items[s.count - 1].package && strcmp(s.items[s.count - 1].package, "Box2D") == 0);
    unity_sources_free(&s);
    // With no package's rules, every field Unity has, as it has it
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_PHYSICS_FIXTURE, NULL, 0, &s));
    yard = text_of(&s, "Assets/Yard.unity");
    TIDE_CHECK(yard && strstr(yard, "Box3D.Box { size = float3(1, 1, 1), offset = float3(0, 0.25, 0), sensor = false };") != NULL
               && strstr(yard, "            Box3D.Sphere { radius = 0.25, offset = float3(0, 0.1, 0), sensor = false };\n") != NULL
               && strstr(yard, "Box2D.CircleMade;") != NULL);
    unity_sources_free(&s);
}

// A folder with no prefabs or scenes of TideEntity objects: no prefabs, and a map of no assets.
TIDE_TEST(unity_assets_none_without_tide_entities)
{
    unity_sources s;
    TIDE_REQUIRE(unity_assets_read(TIDE_UNITY_FIXTURE "/../MissingFolder", NULL, 0, &s));
    TIDE_CHECK(s.count == 0);
    TIDE_CHECK(s.map && strstr(s.map, "\"assets\": [") != NULL);
    unity_sources_free(&s);
}
