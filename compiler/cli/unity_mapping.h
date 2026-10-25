#pragma once

#include <stdbool.h>

// What a package says of its own components in a Unity project: its unity.json (UNITY_MAPPING_FILE), which tide unity
// reads for each package the game lists. The converter (unity_assets.c) bakes by it, and the game's C# (unity_code.c)
// is written by it:
//
//   {
//     "source": [lines of the package's Tide that a Unity project alone compiles],
//     "components": {
//       "Box3D.Body": {"hidden": ["id", "stepPosition"]},
//       "Box3D.BoxMade": {"hidden": true},
//       "Box3D.Box": {"scaled": {"size": "absScale", "offset": "scale"}, "shape": {"box": "size", "center": "offset"}}
//     }
//   }
//
// - "hidden" fields are the package's own: not shown, authored or baked. A component "hidden": true isn't one a
//   GameObject is given in Unity (no Add Component entry), and isn't baked.
// - "scaled" fields are relative to the GameObject's scale in the world, as Unity's colliders' sizes are: baked times
//   it, shown in Play divided by it. By `scale` and `absScale` (each axis, with its sign or without), `largestScale`,
//   `largestScaleXY`, `largestScaleXZ`, `scaleX`, `scaleY` and `scaleZ`.
// - "shape" is what the Scene view draws of the component, and edits with handles: {"box": size, "center": offset},
//   {"sphere": radius, "center": ...}, {"capsule": radius, "height": height, "center": ...} (standing along y), and
//   in Unity's XY plane {"rect": size, ...} and {"circle": radius, ...}, each naming the component's fields.
// - "source" is compiled as a source of the package's own, in a Unity project alone ("Library/Tide/Unity.<package>.tide"):
//   its glue for Unity (Box3D's bodies start at their entities' Transforms, which follow them).

#define UNITY_MAPPING_FILE "unity.json"

// One of the game's packages, for its unity.json
typedef struct unity_package {
    const char *folder;
    const char *name;
} unity_package;

typedef enum unity_scale {
    UNITY_SCALE_NONE,
    UNITY_SCALE_EACH,     // By each axis's scale, with its sign: an offset
    UNITY_SCALE_EACH_ABS, // ...without: a size
    UNITY_SCALE_LARGEST,  // By the largest of the three, as Unity's sphere colliders are
    UNITY_SCALE_LARGEST_XY,
    UNITY_SCALE_LARGEST_XZ,
    UNITY_SCALE_X,
    UNITY_SCALE_Y,
    UNITY_SCALE_Z,
} unity_scale;

typedef struct unity_scaled {
    char *field;
    unity_scale scale;
} unity_scaled;

typedef struct unity_component_rules {
    char *name;      // The component's Tide name: "Box3D.Box"
    bool hidden;     // The package's own: no GameObject is given it in Unity
    char **hidden_fields;
    int hidden_count;
    unity_scaled *scaled;
    int scaled_count;
    char *shape;     // "box", "sphere", "capsule", "rect", "circle", or NULL
    char *size;      // The shape's size field: a box's or rect's size, a sphere's, capsule's or circle's radius
    char *height;    // A capsule's height field
    char *center;    // Its offset field, or NULL
} unity_component_rules;

typedef struct unity_glue {
    char *package; // Its name
    char *path;    // "Library/Tide/Unity.<package>.tide"
    char *text;
} unity_glue;

typedef struct unity_mappings {
    unity_component_rules *components;
    int component_count;
    unity_glue *glue;
    int glue_count;
} unity_mappings;

// The unity.json of each of `packages` that has one. False after saying what's wrong with one; what read is in `out`
// either way.
bool unity_mappings_read(const unity_package *packages, int count, unity_mappings *out);
void unity_mappings_free(unity_mappings *m);

// The rules for the component of Tide name `name`, or NULL.
const unity_component_rules *unity_rules_of(const unity_mappings *m, const char *name);
bool unity_field_hidden(const unity_component_rules *r, const char *field);
unity_scale unity_field_scale(const unity_component_rules *r, const char *field);

// The name of a scale as unity.json and the C# say it ("absScale"), or NULL for none.
const char *unity_scale_name(unity_scale scale);

// A value relative to the GameObject times its scale in the world (`scale`, each axis's): `axis` the value's axis
// (0 to 2), or -1 for a number.
double unity_scale_factor(unity_scale by, const double scale[3], int axis);
