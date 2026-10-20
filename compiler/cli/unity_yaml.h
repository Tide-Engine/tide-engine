#pragma once

// The YAML Unity writes its text assets in (Force Text serialization): a file
// of documents, each `--- !u!<class> &<fileID>` (and `stripped` for an object
// of a prefab instance), holding one object. Enough of YAML for those: block
// maps and sequences, flow maps and sequences on one line, and plain, single-
// and double-quoted scalars, which can go on over following lines.

#include <stdbool.h>
#include <stdint.h>

typedef enum yaml_kind { YAML_SCALAR, YAML_MAP, YAML_SEQ } yaml_kind;

typedef struct yaml_node {
    yaml_kind kind;
    char *text; // A scalar's
    int count;  // A map's keys and values, a sequence's items
    char **keys;
    struct yaml_node **items;
} yaml_node;

typedef struct yaml_document {
    int class_id;     // Unity's class: 1 GameObject, 4 Transform, 114 MonoBehaviour, 224 RectTransform, 1001 PrefabInstance
    int64_t file_id;  // The object's ID in its file
    bool stripped;    // A prefab instance's object, standing in for the source prefab's
    char *type;       // Its one key: "GameObject", "Transform", ...
    yaml_node *body;  // ...and what it holds
} yaml_document;

typedef struct yaml_file {
    yaml_document *documents;
    int count;
} yaml_file;

// Reads a Unity text asset; false when it isn't one (a binary asset, or text Unity didn't write).
bool yaml_read(const char *text, yaml_file *out);
void yaml_free(yaml_file *file);

// A map's value for `key`, or NULL.
const yaml_node *yaml_get(const yaml_node *map, const char *key);
// ...as text (a scalar's), or `otherwise`.
const char *yaml_text(const yaml_node *map, const char *key, const char *otherwise);
// A scalar's number, or `otherwise`.
double yaml_number(const yaml_node *map, const char *key, double otherwise);
// A reference ({fileID: n, guid: g, type: t}): its fileID, and its guid into `guid` ("" for this file's).
int64_t yaml_reference(const yaml_node *node, char guid[33]);
