#pragma once

#include <stdbool.h>

// What a package says of its own components in a Unity project: its unity.json (UNITY_MAPPING_FILE), which tide unity
// reads for each package the game lists. The converter (unity_assets.c) bakes by it, and the game's C# (unity_code.c)
// is written by it:
//
//   {
//     "source": [lines of the package's Tide that a Unity project alone compiles],
//     "components": {
//       "Box3D.Body": {"hidden": ["stepPosition"]},
//       "Box2D.BoxMade": {"hidden": true},
//       "Box3D.Box": {"scaled": {"size": "absScale", "offset": "scale"}, "shape": {"box": "size", "center": "offset"}},
//       "Box3D.Part": {"part": {"of": "of", "body": "Box3D.Body"}},
//       "Box3D.MeshCollider": {"placed": "offset", "mesh": {"vertices": "vertices", "indices": "indices"}},
//       "Box3D.Joint": {"readBack": ["force", "torque"]}
//     }
//   }
//
// Every component and field named is one of the package's: unity_code_write says which isn't. A component's [Native]
// fields are its package's own without saying so: no C# field, never authored or baked.
//
// - "hidden" fields are the package's own: not shown, authored or baked. A component "hidden": true isn't one a
//   GameObject is given in Unity (no Add Component entry), and isn't baked.
// - "scaled" fields are relative to the GameObject's scale in the world, as Unity's colliders' sizes are: baked times
//   it, shown in Play divided by it. By `scale` and `absScale` (each axis, with its sign or without), `largestScale`,
//   `largestScaleXY`, `largestScaleXZ`, `scaleX`, `scaleY` and `scaleZ`.
// - "shape" is what the Scene view draws of the component, and edits with handles: {"box": size, "center": offset},
//   {"sphere": radius, "center": ...}, {"capsule": radius, "height": height, "center": ...} (standing along y), and
//   in Unity's XY plane {"rect": size, ...} and {"circle": radius, ...}, each naming the component's fields.
// - "placed" is the field that places the component on its entity's body (a shape's offset): the shape's "center"
//   without saying. On a part (below) it's baked where the GameObject is against its body's.
// - "part" makes its entity a part of another's body: "of", its entity field naming the body (none: the nearest
//   entity above with the "body" component, which the package's "source" gives it at run time too), and "body", that
//   component's Tide name. The entity's placed fields are baked in the body's frame, after the scale; a part turned
//   against its body, which only a round shape can be, is a warning.
// - "mesh" names a component's list fields that Unity fills from a Mesh (com.tide.unity's inspector; the GameObject's
//   MeshFilter's as the component's added): "vertices" (float3), and "indices" (int, three a triangle; none for a
//   hull).
// - "readBack" fields are the package's answers (a joint's force): shown in Play, never authored or baked.
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
    char *file;      // The unity.json it's in, for what's said of it
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
    char *placed;    // The field placing it on its body: "placed", else the shape's center; NULL for none
    char *part_of;   // A part's entity field naming its body, or NULL: no part
    char *part_body; // ...and the body's component
    char *vertices;  // A mesh's list fields, or NULL
    char *indices;
    char **read_back;
    int read_back_count;
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
bool unity_field_read_back(const unity_component_rules *r, const char *field);

// The fields a component's rules name, to check they're its: up to `capacity` into `out`; how many.
int unity_rules_fields(const unity_component_rules *r, const char **out, int capacity);

// The name of a scale as unity.json and the C# say it ("absScale"), or NULL for none.
const char *unity_scale_name(unity_scale scale);

// A value relative to the GameObject times its scale in the world (`scale`, each axis's): `axis` the value's axis
// (0 to 2), or -1 for a number.
double unity_scale_factor(unity_scale by, const double scale[3], int axis);
