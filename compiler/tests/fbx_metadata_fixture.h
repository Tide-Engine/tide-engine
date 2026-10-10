#pragma once

// Authored FBX 7.4 ASCII catalog fixture. No geometry or curve data is needed
// to check names, hierarchy and the editor's metadata cache.
static const char fbx_catalog_fixture[] =
    "; A Tide catalog fixture, detected by its source extension.\n"
    "FBXHeaderExtension: {\n    FBXVersion: 7400\n}\n"
    "Objects: {\n"
    "    Model: 1, \"Model::Root\", \"Null\" {\n        Version: 232\n    }\n"
    "    Model: 2, \"Model::Hand\", \"LimbNode\" {\n        Version: 232\n    }\n"
    "    AnimationStack: 3, \"AnimStack::Idle\", \"\" {\n    }\n"
    "    AnimationStack: 4, \"AnimStack::Run\", \"\" {\n    }\n"
    "}\n"
    "Connections: {\n    C: \"OO\", 1, 0\n    C: \"OO\", 2, 1\n}\n";
