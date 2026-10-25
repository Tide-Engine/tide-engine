#pragma once

// A Unity project's prefabs and scenes as Tide prefabs, for `tide unity`: the
// GameObjects with Unity's TideEntity component (unity/com.tide.unity) become
// entities, their Tide components what TideEntity holds, their Transform and
// parent Unity's. Prefab instances and variants are expanded as Unity expands
// them, an instance's component values layered over its source's field by
// field. Each prefab becomes the Tide prefab UnityEngine.Prefabs.<Name>, after
// its file; each scene the Tide scene UnityEngine.Scenes.<Name>, whose
// entities are the prefab UnityEngine.SceneContents.<Name>, its roots under an
// entity of its own. The text is Tide's .tprefab syntax, which
// tidec checks as any authored prefab.
//
// Each entity also gets UnityEngine.Source { asset, node }, a component declared in
// a source of its own (UNITY_SOURCE_PATH): match state like any other, by
// which Unity's package finds the GameObject showing the entity through the
// map (unity_sources.map): for each asset its GUID, its path, whether it's a
// scene, and each node's GameObject as a path of names from the asset's root
// ("Body/Arm", "Arm#1" for a second Arm among siblings; "" for the root, and
// for a scene's own entity, which no GameObject is).
//
// With scenes among them, that source also has the match's Unity scenes:
// UnityEngine.LoadedScenes (which it loaded, by asset number), Build
// Settings' first scene (ProjectSettings/EditorBuildSettings.asset) loaded as
// Main starts, as a player build starts with it, and UnityEngine.OpenScene,
// the editor's command to have the scenes it has open instead, taken from the
// match's first player alone.

#include <stdbool.h>

#include "unity_mapping.h"

// What the package's TideEntity.cs.meta says: how its components are found in a Unity file.
#define TIDE_UNITY_ENTITY_GUID "4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e"

typedef struct unity_source {
    char *path;    // The asset's, from the project's folder: Assets/Enemies/Enemy.prefab
    char *text;    // Its prefab declaration
    char *package; // A package's Tide for a Unity project (its unity.json's "source"), by the package's name; else NULL
} unity_source;

typedef struct unity_sources {
    unity_source *items; // UnityEngine.Source's declaration first, when there are any
    int count;
    char *map; // JSON: {"assets": [{"guid": "...", "path": "Assets/...", "scene": false, "nodes": ["", "Body"]}]}
} unity_sources;

#define UNITY_SOURCE_PATH "Library/Tide/Unity.tide" // Where messages say UnityEngine.Source is declared

// The prefabs and scenes under `assets` (a Unity project's Assets folder) that hold TideEntity objects, as Tide
// source. False after saying what's wrong: an asset in binary serialization, one that doesn't read, two prefabs of
// one name, a reference to something that isn't an entity.
// `packages` are the game's: each one's unity.json (unity_mapping.h) says which of its components' fields are hidden
// (not baked) and which are by the GameObject's scale (baked times it), and its "source" is a source of that
// package's, after the assets'.
bool unity_assets_read(const char *assets, const unity_package *packages, int package_count, unity_sources *out);
void unity_sources_free(unity_sources *sources);

// A scene the editor has changes to that it hasn't saved is read from the copy the editor writes of it as it is, at
// Play (UNITY_UNSAVED_FOLDER, then its path from the project's folder), in its place; messages still name the scene.
// The editor deletes the copy once the scene is saved, or closed without saving.
#define UNITY_UNSAVED_FOLDER "Library/Tide/unsaved/"
// ...and the editor's first scene that was never saved, which has no file but this copy: the Tide scene
// UnityEngine.Scenes.Untitled, whose path in the map is Unity's for it, "".
#define UNITY_UNTITLED_SCENE "Untitled.unity"

// Changes whenever one of the prefabs, scenes or metas under `assets` does, or one comes or goes, or one of the
// editor's unsaved copies does.
unsigned long long unity_assets_stamp(const char *assets);
