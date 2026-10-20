#pragma once

// A Unity project's prefabs and scenes as Tide prefabs, for `tide unity`: the
// GameObjects with Unity's TideEntity component (unity/com.tide.unity) become
// entities, their Tide components what TideEntity holds, their Transform and
// parent Unity's. Prefab instances and variants are expanded as Unity expands
// them, an instance's component values layered over its source's field by
// field. Each asset becomes one prefab named after its file; a scene's roots
// go under an entity of its own. The text is Tide's .tprefab syntax, which
// tidec checks as any authored prefab.
//
// Each entity also gets Unity.Source { asset, node }, a component declared in
// a source of its own (UNITY_SOURCE_PATH): match state like any other, by
// which Unity's package finds the GameObject showing the entity through the
// map (unity_sources.map): for each asset its GUID, its path, whether it's a
// scene, and each node's GameObject as a path of names from the asset's root
// ("Body/Arm", "Arm#1" for a second Arm among siblings; "" for the root, and
// for a scene's own entity, which no GameObject is).

#include <stdbool.h>

// What the package's TideEntity.cs.meta says: how its components are found in a Unity file.
#define TIDE_UNITY_ENTITY_GUID "4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e"

typedef struct unity_source {
    char *path; // The asset's, from the project's folder: Assets/Enemies/Enemy.prefab
    char *text; // Its prefab declaration
} unity_source;

typedef struct unity_sources {
    unity_source *items; // Unity.Source's declaration first, when there are any
    int count;
    char *map; // JSON: {"assets": [{"guid": "...", "path": "Assets/...", "scene": false, "nodes": ["", "Body"]}]}
} unity_sources;

#define UNITY_SOURCE_PATH "Library/Tide/Unity.tide" // Where messages say Unity.Source is declared

// The prefabs and scenes under `assets` (a Unity project's Assets folder) that hold TideEntity objects, as Tide
// source. False after saying what's wrong: an asset in binary serialization, one that doesn't read, two prefabs of
// one name, a reference to something that isn't an entity.
bool unity_assets_read(const char *assets, unity_sources *out);
void unity_sources_free(unity_sources *sources);

// Changes whenever one of the prefabs, scenes or metas under `assets` does, or one comes or goes.
unsigned long long unity_assets_stamp(const char *assets);
