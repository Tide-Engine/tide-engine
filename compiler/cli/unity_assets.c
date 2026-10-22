// A Unity project's prefabs and scenes as Tide prefabs (unity_assets.h).

#include "unity_assets.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "sys.h"
#include "unity_yaml.h"

#define UNITY_GAME_OBJECT 1
#define UNITY_TRANSFORM 4
#define UNITY_MONO_BEHAVIOUR 114
#define UNITY_RECT_TRANSFORM 224
#define UNITY_PREFAB_INSTANCE 1001
#define UNITY_SCENE_ROOTS 1660057539
#define DEPTH_MOST 32 // Prefabs inside prefabs, at most

static void *grab(const size_t size)
{
    void *p = calloc(1, size ? size : 1);
    if (!p) abort();
    return p;
}

static char *dup(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *out = grab(n);
    memcpy(out, s, n);
    return out;
}

static void grow(void **items, int *capacity, const int count, const size_t size)
{
    if (count < *capacity) return;
    *capacity = *capacity ? *capacity * 2 : 8;
    *items = realloc(*items, (size_t)*capacity * size);
    if (!*items) abort();
}

typedef struct text {
    char *s;
    size_t n, capacity;
} text;

static void put(text *t, const char *s, const size_t n)
{
    if (t->n + n + 1 > t->capacity) {
        t->capacity = (t->n + n + 1) * 2;
        t->s = realloc(t->s, t->capacity);
        if (!t->s) abort();
    }
    memcpy(t->s + t->n, s, n);
    t->n += n;
    t->s[t->n] = '\0';
}

static void puts_(text *t, const char *s)
{
    put(t, s, strlen(s));
}

static void putf(text *t, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void putf(text *t, const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n > 0) put(t, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

// Every file under `dir` ending in `extension`, as Unity has them: not in folders or files it hides (".name",
// "name~"), each folder's in name order.
typedef void (*found_fn)(void *user, const char *path);

typedef struct walk {
    const char *dir, *extension;
    found_fn found;
    void *user;
    char **names;
    bool *dirs;
    int count, capacity;
} walk;

static void walk_entry(void *user, const char *name, const bool is_dir)
{
    walk *w = user;
    const size_t n = strlen(name);
    if (name[0] == '.' || (n && name[n - 1] == '~')) return;
    grow((void **)&w->names, &w->capacity, w->count, sizeof *w->names);
    w->dirs = realloc(w->dirs, sizeof *w->dirs * (size_t)w->capacity);
    if (!w->dirs) abort();
    w->names[w->count] = dup(name);
    w->dirs[w->count++] = is_dir;
}

static void folder_find(const char *dir, const char *extension, const found_fn found, void *user)
{
    walk w = {dir, extension, found, user, NULL, NULL, 0, 0};
    sys_list(dir, walk_entry, &w);
    for (int i = 1; i < w.count; i++) { // By name, so every machine finds them in one order
        for (int k = i; k > 0 && strcmp(w.names[k - 1], w.names[k]) > 0; k--) {
            char *name = w.names[k];
            const bool is_dir = w.dirs[k];
            w.names[k] = w.names[k - 1];
            w.dirs[k] = w.dirs[k - 1];
            w.names[k - 1] = name;
            w.dirs[k - 1] = is_dir;
        }
    }
    for (int i = 0; i < w.count; i++) {
        char *path = path_join(dir, w.names[i]);
        if (w.dirs[i]) folder_find(path, extension, found, user);
        else if (strlen(w.names[i]) > strlen(extension) &&
                 strcmp(w.names[i] + strlen(w.names[i]) - strlen(extension), extension) == 0) found(user, path);
        free(path);
        free(w.names[i]);
    }
    free(w.names);
    free(w.dirs);
}

// ---------------------------------------------------------------------------
// Assets by GUID, from their .meta files

typedef struct asset {
    char guid[33];
    char *path;  // Absolute
    char *local; // From the project's folder: Assets/...
    yaml_file yaml;
    bool read, readable;
    int relevant; // 0 unknown, 1 holds TideEntity objects here or in a prefab it instances, -1 not, 2 being worked out
} asset;

// The game's components as Unity has them (Packages/com.tide.game/components.json, which tide unity writes with their
// C#: unity_code.h): each one's script GUID, and how each field of it is serialized, which its YAML is read by.
typedef struct map_field {
    char *name;    // Tide's
    char *key;     // Unity's YAML's: the C# field
    char *form;    // number, bool, float2, float3, float4, int2, int3, quaternion, color, string, enum, entity, list, literal
    char *enumeration;  // An enum's Tide name
    char *element;      // A list's elements' form
    char *element_type; // ...and Tide type
} map_field;

typedef struct map_component {
    char guid[33];
    char *name;
    map_field *fields;
    int field_count;
} map_component;

typedef struct map_enum {
    char *name;
    char **members;
    long *values;
    int count;
} map_enum;

typedef struct project {
    char *assets;          // Absolute, ending in '/'
    size_t project_length; // Of the project's folder, before "Assets/"
    asset *items;
    int count, capacity;
    bool failed;
    map_component *components;
    int component_count;
    map_enum *enums;
    int enum_count;
} project;

static char *json_dup(const json *object, const char *key)
{
    const char *v = json_str(json_get(object, key));
    return v ? dup(v) : NULL;
}

// The project's Packages/com.tide.game/components.json, if tide unity has written it.
static void read_components(project *p)
{
    char *path = grab(p->project_length + 64);
    memcpy(path, p->assets, p->project_length);
    strcpy(path + p->project_length, "Packages/com.tide.game/components.json");
    size_t size = 0;
    char *body = sys_read_file(path, &size);
    free(path);
    const json *map = body ? json_parse(body, size) : NULL;
    const json *components = json_get(map, "components"), *enums = json_get(map, "enums");
    for (int i = 0; components && components->kind == JSON_ARRAY && i < components->count; i++) {
        const json *c = components->items[i];
        const char *name = json_str(json_get(c, "name")), *guid = json_str(json_get(c, "guid"));
        if (!name || !guid) continue;
        int capacity = p->component_count;
        grow((void **)&p->components, &capacity, p->component_count, sizeof *p->components);
        map_component *m = &p->components[p->component_count++];
        memset(m, 0, sizeof *m);
        snprintf(m->guid, sizeof m->guid, "%s", guid);
        m->name = dup(name);
        const json *fields = json_get(c, "fields");
        const int count = fields && fields->kind == JSON_ARRAY ? fields->count : 0;
        m->fields = grab(sizeof *m->fields * (size_t)(count + 1));
        for (int k = 0; k < count; k++) {
            const json *f = fields->items[k];
            map_field *to = &m->fields[m->field_count];
            *to = (map_field){json_dup(f, "name"), json_dup(f, "key"), json_dup(f, "form"), json_dup(f, "enum"),
                              json_dup(f, "element"), json_dup(f, "elementType")};
            if (to->name && to->key && to->form) {
                m->field_count++;
                continue;
            }
            free(to->name);
            free(to->key);
            free(to->form);
            free(to->enumeration);
            free(to->element);
            free(to->element_type);
        }
    }
    for (int i = 0; enums && enums->kind == JSON_ARRAY && i < enums->count; i++) {
        const json *e = enums->items[i];
        const json *members = json_get(e, "members"), *values = json_get(e, "values");
        const char *name = json_str(json_get(e, "name"));
        if (!name || !members || !values || members->kind != JSON_ARRAY || values->kind != JSON_ARRAY) continue;
        int capacity = p->enum_count;
        grow((void **)&p->enums, &capacity, p->enum_count, sizeof *p->enums);
        map_enum *m = &p->enums[p->enum_count++];
        m->name = dup(name);
        const int count = members->count < values->count ? members->count : values->count;
        m->members = grab(sizeof *m->members * (size_t)(count + 1));
        m->values = grab(sizeof *m->values * (size_t)(count + 1));
        m->count = 0;
        for (int k = 0; k < count; k++) {
            const char *member = json_str(members->items[k]);
            if (!member || values->items[k]->kind != JSON_NUMBER) continue;
            m->members[m->count] = dup(member);
            m->values[m->count++] = (long)values->items[k]->number;
        }
    }
    if (map) json_release();
    free(body);
}

static void free_components(project *p)
{
    for (int i = 0; i < p->component_count; i++) {
        map_component *m = &p->components[i];
        for (int k = 0; k < m->field_count; k++) {
            free(m->fields[k].name);
            free(m->fields[k].key);
            free(m->fields[k].form);
            free(m->fields[k].enumeration);
            free(m->fields[k].element);
            free(m->fields[k].element_type);
        }
        free(m->fields);
        free(m->name);
    }
    free(p->components);
    for (int i = 0; i < p->enum_count; i++) {
        for (int k = 0; k < p->enums[i].count; k++) free(p->enums[i].members[k]);
        free(p->enums[i].members);
        free(p->enums[i].values);
        free(p->enums[i].name);
    }
    free(p->enums);
}

static void index_meta(void *user, const char *path)
{
    project *p = user;
    size_t size = 0;
    char *body = sys_read_file(path, &size);
    if (!body) return;
    const char *g = strstr(body, "\nguid: ");
    if (g) {
        g += 7;
        grow((void **)&p->items, &p->capacity, p->count, sizeof *p->items);
        asset *a = &p->items[p->count++];
        memset(a, 0, sizeof *a);
        int n = 0;
        while (n < 32 && ((g[n] >= '0' && g[n] <= '9') || (g[n] >= 'a' && g[n] <= 'f'))) {
            a->guid[n] = g[n];
            n++;
        }
        a->guid[n] = '\0';
        const size_t length = strlen(path) - 5; // Without ".meta"
        a->path = grab(length + 1);
        memcpy(a->path, path, length);
        a->local = dup(strlen(a->path) > p->project_length ? a->path + p->project_length : a->path);
    }
    free(body);
}

static asset *asset_of(project *p, const char *guid)
{
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->items[i].guid, guid) == 0) return &p->items[i];
    }
    return NULL;
}

static bool ends_with(const char *s, const char *end)
{
    const size_t n = strlen(s), m = strlen(end);
    return n >= m && strcmp(s + n - m, end) == 0;
}

static const yaml_file *yaml_of(project *p, asset *a)
{
    if (!a->read) {
        a->read = true;
        size_t size = 0;
        char *body = sys_read_file(a->path, &size);
        a->readable = body && yaml_read(body, &a->yaml);
        if (body && !a->readable) {
            fprintf(stderr, "%s: error: Unity wrote it in binary: set Edit > Project Settings > Editor > Asset Serialization "
                            "to Force Text, which Tide reads\n", a->local);
            p->failed = true;
        }
        free(body);
    }
    return a->readable ? &a->yaml : NULL;
}

// Whether a prefab or scene holds TideEntity objects, itself or through the prefabs it instances.
static bool relevant(project *p, asset *a)
{
    if (a->relevant) return a->relevant == 1;
    a->relevant = 2; // A prefab that includes itself isn't one, while it's worked out
    size_t size = 0;
    char *body = sys_read_file(a->path, &size);
    bool found = body && strstr(body, TIDE_UNITY_ENTITY_GUID) != NULL;
    for (const char *at = body; body && !found && (at = strstr(at, "m_SourcePrefab: {")) != NULL; at++) {
        const char *g = strstr(at, "guid: ");
        const char *close = strchr(at, '}');
        if (!g || (close && g > close)) continue;
        char guid[33] = {0};
        for (int i = 0; i < 32 && g[6 + i] && g[6 + i] != ',' && g[6 + i] != '}'; i++) guid[i] = g[6 + i];
        asset *source = asset_of(p, guid);
        found = source && source->relevant != 2 && relevant(p, source);
    }
    free(body);
    a->relevant = found ? 1 : -1;
    return found;
}

// ---------------------------------------------------------------------------
// GameObjects as Unity expands them

// Who an object is, at one level of prefab nesting: the asset it's in, and its ID there.
typedef struct ident {
    char guid[33];
    int64_t file_id;
    int what; // 0 its GameObject, 1 its Transform, 2 its TideEntity, 3 one of its Tide components (`component`)
    const map_component *component;
} ident;

typedef struct node node;

typedef struct reference {
    node *target;   // The object referenced, a GameObject of the expansion
    char guid[33];  // ...or an asset's GUID, for an asset
    bool object;    // Something was referenced at all
} reference;

// A serialized field's value at a property path ("speed", "position.x", "marks.Array.data[2]"), as Unity's YAML and
// a prefab instance's changes name them; an entity's is "@k", the component's kth reference.
typedef struct flat {
    char *path;
    char *value;
} flat;

// A component a GameObject's TideEntity lists: its type and its values, a Tide literal.
typedef struct authored {
    char *type;
    char *values;
} authored;

// A Tide component of a GameObject's own (com.tide.game's): what Unity serialized of it, by property path, and the
// objects it references.
typedef struct owned {
    const map_component *component;
    flat *flats;
    int flat_count, flat_capacity;
    reference *refs;
    int ref_count, ref_capacity;
    bool removed; // An instance's m_RemovedComponents took it away
} owned;

struct node {
    char *name;
    double pos[3], rot[4], scale[3];
    bool active;
    bool tide; // Has TideEntity
    authored *components;
    int component_count, component_capacity;
    owned *owned; // In the order they were first read, which never changes as a pending reference has its index
    int owned_count, owned_capacity;
    reference *refs;
    int ref_count, ref_capacity;
    node **children;
    int child_count, child_capacity;
    node *parent;
    ident *ids;
    int id_count, id_capacity;
    int64_t instance; // The prefab instance it came from, in the file being expanded; 0 for that file's own
    char *emitted;    // Its entity's name, once it has one
};

static node *new_node(void)
{
    node *n = grab(sizeof *n);
    n->name = dup("GameObject");
    n->rot[3] = 1;
    n->scale[0] = n->scale[1] = n->scale[2] = 1;
    n->active = true;
    return n;
}

static void add_id(node *n, const char *guid, const int64_t file_id, const int what, const map_component *component)
{
    grow((void **)&n->ids, &n->id_capacity, n->id_count, sizeof *n->ids);
    ident *i = &n->ids[n->id_count++];
    snprintf(i->guid, sizeof i->guid, "%s", guid);
    i->file_id = file_id;
    i->what = what;
    i->component = component;
}

static void add_child(node *parent, node *child)
{
    if (child->parent || child == parent) return;
    grow((void **)&parent->children, &parent->child_capacity, parent->child_count, sizeof *parent->children);
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

static void free_node(node *n)
{
    if (!n) return;
    for (int i = 0; i < n->child_count; i++) free_node(n->children[i]);
    for (int i = 0; i < n->component_count; i++) {
        free(n->components[i].type);
        free(n->components[i].values);
    }
    free(n->components);
    for (int i = 0; i < n->owned_count; i++) {
        owned *o = &n->owned[i];
        for (int k = 0; k < o->flat_count; k++) {
            free(o->flats[k].path);
            free(o->flats[k].value);
        }
        free(o->flats);
        free(o->refs);
    }
    free(n->owned);
    free(n->refs);
    free(n->children);
    free(n->ids);
    free(n->name);
    free(n->emitted);
    free(n);
}

// The node in `roots` (and under them) that is (guid, file_id) as `what`, from instance `instance` (0: any).
static node *find(node **roots, const int count, const char *guid, const int64_t file_id, const int what, const int64_t instance)
{
    for (int r = 0; r < count; r++) {
        node *n = roots[r];
        if (!instance || n->instance == instance) {
            for (int i = 0; i < n->id_count; i++) {
                if (n->ids[i].file_id == file_id && n->ids[i].what == what && strcmp(n->ids[i].guid, guid) == 0) return n;
            }
        }
        node *inside = find(n->children, n->child_count, guid, file_id, what, instance);
        if (inside) return inside;
    }
    return NULL;
}

// The identity (guid, file_id) as `what` of a node in `roots` (and under them), and the node.
static const ident *find_id(node **roots, const int count, const char *guid, const int64_t file_id, const int what, node **owner)
{
    for (int r = 0; r < count; r++) {
        node *n = roots[r];
        for (int i = 0; i < n->id_count; i++) {
            if (n->ids[i].file_id == file_id && n->ids[i].what == what && strcmp(n->ids[i].guid, guid) == 0) {
                *owner = n;
                return &n->ids[i];
            }
        }
        const ident *inside = find_id(n->children, n->child_count, guid, file_id, what, owner);
        if (inside) return inside;
    }
    return NULL;
}

// The node of (guid, file_id) as any of its objects, and which one it is.
static node *find_any(node **roots, const int count, const char *guid, const int64_t file_id, const int64_t instance, int *what)
{
    for (int w = 0; w < 4; w++) {
        node *n = find(roots, count, guid, file_id, w, instance);
        if (n) {
            if (what) *what = w;
            return n;
        }
    }
    return NULL;
}

static void set_instance(node *n, const int64_t instance)
{
    n->instance = instance;
    for (int i = 0; i < n->child_count; i++) set_instance(n->children[i], instance);
}

static void add_instance_ids(node *n, const char *source, const char *file, const int64_t instance)
{
    const int count = n->id_count;
    for (int i = 0; i < count; i++) {
        if (strcmp(n->ids[i].guid, source) != 0) continue;
        const int64_t id = (int64_t)(((uint64_t)instance ^ (uint64_t)n->ids[i].file_id) & 0x7FFFFFFFFFFFFFFFull);
        add_id(n, file, id, n->ids[i].what, n->ids[i].component);
    }
    for (int i = 0; i < n->child_count; i++) add_instance_ids(n->children[i], source, file, instance);
}

// The fields of a Tide component value, "a = 1, b = float3(1, 2, 3)": each as its name and its whole text.
typedef struct value_field {
    char *name;
    char *text;
} value_field;

static int split_fields(const char *values, value_field **out)
{
    int count = 0, capacity = 0;
    *out = NULL;
    const char *start = values;
    int depth = 0;
    char quote = 0;
    for (const char *c = values;; c++) {
        if (quote) {
            if (*c == '\\' && c[1]) c++;
            else if (*c == quote) quote = 0;
            if (*c) continue;
        }
        if (*c == '"' || *c == '\'') quote = *c;
        else if (*c == '(' || *c == '[' || *c == '{') depth++;
        else if (*c == ')' || *c == ']' || *c == '}') depth--;
        if (*c == '\0' || (*c == ',' && depth == 0) || (*c == ';' && depth == 0)) {
            const char *a = start, *b = c;
            while (a < b && (*a == ' ' || *a == '\n' || *a == '\t')) a++;
            while (b > a && (b[-1] == ' ' || b[-1] == '\n' || b[-1] == '\t')) b--;
            if (b > a) {
                grow((void **)out, &capacity, count, sizeof **out);
                value_field *f = &(*out)[count++];
                f->text = grab((size_t)(b - a) + 1);
                memcpy(f->text, a, (size_t)(b - a));
                const char *eq = strchr(f->text, '=');
                size_t name_length = eq ? (size_t)(eq - f->text) : strlen(f->text);
                while (name_length && f->text[name_length - 1] == ' ') name_length--;
                f->name = grab(name_length + 1);
                memcpy(f->name, f->text, name_length);
            }
            if (*c == '\0') break;
            start = c + 1;
        }
    }
    return count;
}

static void free_fields(value_field *fields, const int count)
{
    for (int i = 0; i < count; i++) {
        free(fields[i].name);
        free(fields[i].text);
    }
    free(fields);
}

static char *merge_values(const char *base, const char *patch)
{
    value_field *a, *b;
    const int na = split_fields(base, &a), nb = split_fields(patch, &b);
    text out = {0};
    puts_(&out, "");
    for (int i = 0; i < na; i++) {
        const char *chosen = a[i].text;
        for (int k = 0; k < nb; k++) {
            if (strcmp(a[i].name, b[k].name) == 0) chosen = b[k].text;
        }
        if (out.n) puts_(&out, ", ");
        puts_(&out, chosen);
    }
    for (int k = 0; k < nb; k++) {
        bool there = false;
        for (int i = 0; i < na; i++) there |= strcmp(a[i].name, b[k].name) == 0;
        if (there) continue;
        if (out.n) puts_(&out, ", ");
        puts_(&out, b[k].text);
    }
    free_fields(a, na);
    free_fields(b, nb);
    return out.s;
}

static void set_component(node *n, const int at, const char *type, const char *values, const bool patch)
{
    if (at < 0 || at > 10000) return;
    while (n->component_count <= at) {
        grow((void **)&n->components, &n->component_capacity, n->component_count, sizeof *n->components);
        n->components[n->component_count++] = (authored){dup(""), dup("")};
    }
    authored *c = &n->components[at];
    if (type) {
        free(c->type);
        c->type = dup(type);
    }
    if (values) {
        char *merged = patch ? merge_values(c->values, values) : dup(values);
        free(c->values);
        c->values = merged;
    }
}

static void resize_components(node *n, const int count)
{
    if (count < 0 || count > 10000) return;
    while (n->component_count > count) {
        authored *c = &n->components[--n->component_count];
        free(c->type);
        free(c->values);
    }
    if (count > n->component_count) set_component(n, count - 1, NULL, NULL, false);
}

// The slot of the GameObject's own Tide component `c`, made where it has none.
static int owned_slot(node *n, const map_component *c)
{
    for (int i = 0; i < n->owned_count; i++) {
        if (n->owned[i].component == c) return i;
    }
    grow((void **)&n->owned, &n->owned_capacity, n->owned_count, sizeof *n->owned);
    n->owned[n->owned_count] = (owned){c, NULL, 0, 0, NULL, 0, 0, false};
    return n->owned_count++;
}

static void set_flat(owned *a, const char *path, const char *value)
{
    for (int i = 0; i < a->flat_count; i++) {
        if (strcmp(a->flats[i].path, path) == 0) {
            free(a->flats[i].value);
            a->flats[i].value = dup(value);
            return;
        }
    }
    grow((void **)&a->flats, &a->flat_capacity, a->flat_count, sizeof *a->flats);
    a->flats[a->flat_count++] = (flat){dup(path), dup(value)};
}

static const char *get_flat(const owned *a, const char *path)
{
    for (int i = 0; i < a->flat_count; i++) {
        if (strcmp(a->flats[i].path, path) == 0) return a->flats[i].value;
    }
    return NULL;
}

static const map_field *field_of(const map_component *c, const char *key)
{
    for (int i = 0; i < c->field_count; i++) {
        if (strcmp(c->fields[i].key, key) == 0) return &c->fields[i];
    }
    return NULL;
}

static const map_component *component_of(const project *p, const char *guid)
{
    for (int i = 0; guid[0] && i < p->component_count; i++) {
        if (strcmp(p->components[i].guid, guid) == 0) return &p->components[i];
    }
    return NULL;
}

static void resize_refs(node *n, const int count)
{
    if (count < 0 || count > 10000) return;
    while (n->ref_count < count) {
        grow((void **)&n->refs, &n->ref_capacity, n->ref_count, sizeof *n->refs);
        memset(&n->refs[n->ref_count++], 0, sizeof *n->refs);
    }
    n->ref_count = count;
}

// ---------------------------------------------------------------------------
// One file, expanded

static const yaml_document *document(const yaml_file *f, const int64_t file_id)
{
    for (int i = 0; i < f->count; i++) {
        if (f->documents[i].file_id == file_id) return &f->documents[i];
    }
    return NULL;
}

static bool is_transform(const yaml_document *d)
{
    return d && (d->class_id == UNITY_TRANSFORM || d->class_id == UNITY_RECT_TRANSFORM);
}

static bool is_tide_entity(const yaml_document *d)
{
    if (!d || d->class_id != UNITY_MONO_BEHAVIOUR || d->stripped) return false;
    char guid[33];
    yaml_reference(yaml_get(d->body, "m_Script"), guid);
    return strcmp(guid, TIDE_UNITY_ENTITY_GUID) == 0;
}

// The node a document of the file (its own object, or a stripped one of an instance) stands for.
static node *node_of(const yaml_file *f, const char *guid, node **all, const int count, const int64_t file_id)
{
    const yaml_document *d = document(f, file_id);
    if (!d) return NULL;
    if (d->stripped) {
        char source[33], unused[33];
        const int64_t id = yaml_reference(yaml_get(d->body, "m_CorrespondingSourceObject"), source);
        const int64_t instance = yaml_reference(yaml_get(d->body, "m_PrefabInstance"), unused);
        const int what = d->class_id == UNITY_GAME_OBJECT ? 0 : is_transform(d) ? 1 : 2;
        node *n = find(all, count, source, id, what, instance);
        return n ? n : find_any(all, count, source, id, instance, NULL);
    }
    return find_any(all, count, guid, file_id, 0, NULL);
}

static void read_trs(node *n, const yaml_node *t)
{
    const yaml_node *pos = yaml_get(t, "m_LocalPosition"), *rot = yaml_get(t, "m_LocalRotation"), *scale = yaml_get(t, "m_LocalScale");
    n->pos[0] = yaml_number(pos, "x", 0);
    n->pos[1] = yaml_number(pos, "y", 0);
    n->pos[2] = yaml_number(pos, "z", 0);
    n->rot[0] = yaml_number(rot, "x", 0);
    n->rot[1] = yaml_number(rot, "y", 0);
    n->rot[2] = yaml_number(rot, "z", 0);
    n->rot[3] = yaml_number(rot, "w", 1);
    n->scale[0] = yaml_number(scale, "x", 1);
    n->scale[1] = yaml_number(scale, "y", 1);
    n->scale[2] = yaml_number(scale, "z", 1);
}

// "components.Array.data[3].values": the index after `prefix`, and what follows it.
static bool array_path(const char *path, const char *prefix, int *index, const char **rest)
{
    const size_t n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0) return false;
    char *end = NULL;
    const long i = strtol(path + n, &end, 10);
    if (end == path + n || *end != ']' || i < 0 || i > 10000) return false;
    *index = (int)i;
    *rest = end + 1;
    return true;
}

// A reference waiting for every node of the file to be there: `n`'s `at`th, to (guid, file_id) of the file `in`.
typedef struct pending {
    node *n;
    int component; // Its own Tide component's slot (node's owned), whose references it's of; -1 for TideEntity's
    int at;
    char guid[33];
    int64_t file_id;
} pending;

typedef struct expansion {
    project *p;
    asset *file;
    const yaml_file *f;
    node **all; // This file's GameObjects and its instances' roots, each a tree
    int count, capacity;
    pending *refs;
    int ref_count, ref_capacity;
} expansion;

static void add_top(expansion *x, node *n)
{
    grow((void **)&x->all, &x->capacity, x->count, sizeof *x->all);
    x->all[x->count++] = n;
}

static void add_pending(expansion *x, node *n, const int at, const yaml_node *ref)
{
    char guid[33];
    const int64_t id = yaml_reference(ref, guid);
    if (at >= n->ref_count) resize_refs(n, at + 1);
    if (!id && !guid[0]) {
        memset(&n->refs[at], 0, sizeof n->refs[at]);
        return;
    }
    grow((void **)&x->refs, &x->ref_capacity, x->ref_count, sizeof *x->refs);
    pending *r = &x->refs[x->ref_count++];
    r->n = n;
    r->component = -1;
    r->at = at;
    snprintf(r->guid, sizeof r->guid, "%s", guid);
    r->file_id = id;
}

// A Tide component's entity field: its value "@k", k a reference of the component's own, resolved once every node of
// the file is there; nothing referenced is none.
static void component_reference(expansion *x, node *n, const int slot, const char *path, const yaml_node *ref)
{
    owned *a = &n->owned[slot];
    char guid[33];
    const int64_t id = yaml_reference(ref, guid);
    if (!id && !guid[0]) {
        set_flat(a, path, "");
        return;
    }
    grow((void **)&a->refs, &a->ref_capacity, a->ref_count, sizeof *a->refs);
    memset(&a->refs[a->ref_count], 0, sizeof *a->refs);
    char k[24];
    snprintf(k, sizeof k, "@%d", a->ref_count);
    set_flat(a, path, k);
    grow((void **)&x->refs, &x->ref_capacity, x->ref_count, sizeof *x->refs);
    pending *r = &x->refs[x->ref_count++];
    r->n = n;
    r->component = slot;
    r->at = a->ref_count++;
    snprintf(r->guid, sizeof r->guid, "%s", guid);
    r->file_id = id;
}

// Unity's hex of a list of numbers (List<int>, List<float>: each element's bytes, low first) as the list's paths.
static bool hex_list(owned *a, const char *key, const map_field *f, const char *hex)
{
    const char *t = f->element_type ? f->element_type : "";
    const size_t size = strcmp(t, "double") == 0 || strcmp(t, "long") == 0 ? 8 : strcmp(t, "byte") == 0 || strcmp(t, "bool") == 0 ? 1 : 4;
    const size_t n = strlen(hex);
    if (n % (size * 2) != 0) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    const size_t count = n / (size * 2);
    char path[256], value[64];
    snprintf(path, sizeof path, "%s.Array.size", key);
    snprintf(value, sizeof value, "%zu", count);
    set_flat(a, path, value);
    for (size_t e = 0; e < count; e++) {
        uint8_t bytes[8] = {0};
        for (size_t b = 0; b < size; b++) {
            char pair[3] = {hex[(e * size + b) * 2], hex[(e * size + b) * 2 + 1], 0};
            bytes[b] = (uint8_t)strtoul(pair, NULL, 16);
        }
        if (strcmp(t, "float") == 0) {
            float v;
            memcpy(&v, bytes, 4);
            snprintf(value, sizeof value, "%.9g", (double)v);
        } else if (strcmp(t, "double") == 0) {
            double v;
            memcpy(&v, bytes, 8);
            snprintf(value, sizeof value, "%.17g", v);
        } else if (strcmp(t, "long") == 0) {
            int64_t v;
            memcpy(&v, bytes, 8);
            snprintf(value, sizeof value, "%lld", (long long)v);
        } else if (size == 1) {
            snprintf(value, sizeof value, "%u", (unsigned)bytes[0]);
        } else if (strcmp(t, "PlayerID") == 0) {
            uint32_t v;
            memcpy(&v, bytes, 4);
            snprintf(value, sizeof value, "%u", (unsigned)v);
        } else {
            int32_t v;
            memcpy(&v, bytes, 4);
            snprintf(value, sizeof value, "%d", (int)v);
        }
        snprintf(path, sizeof path, "%s.Array.data[%zu]", key, e);
        set_flat(a, path, value);
    }
    return true;
}

// What Unity serialized of a value at `path`, as paths: a map's keys after a '.', a sequence's items as
// path.Array.data[i] with path.Array.size, a scalar as itself.
static void flatten(owned *a, const char *path, const yaml_node *v)
{
    if (!v) return;
    char inner[512];
    if (v->kind == YAML_SCALAR) {
        set_flat(a, path, v->text ? v->text : "");
    } else if (v->kind == YAML_MAP) {
        for (int i = 0; i < v->count; i++) {
            snprintf(inner, sizeof inner, "%s.%s", path, v->keys[i]);
            flatten(a, inner, v->items[i]);
        }
    } else {
        char size[24];
        snprintf(inner, sizeof inner, "%s.Array.size", path);
        snprintf(size, sizeof size, "%d", v->count);
        set_flat(a, inner, size);
        for (int i = 0; i < v->count; i++) {
            snprintf(inner, sizeof inner, "%s.Array.data[%d]", path, i);
            flatten(a, inner, v->items[i]);
        }
    }
}

// A Tide component's document: its serialized fields into its GameObject's slot for it.
static void read_component(expansion *x, node *n, const map_component *c, const yaml_node *body)
{
    const int slot = owned_slot(n, c);
    owned *a = &n->owned[slot];
    a->removed = false;
    for (int i = 0; i < c->field_count; i++) {
        const map_field *f = &c->fields[i];
        const yaml_node *v = yaml_get(body, f->key);
        if (!v) continue; // A field Unity hasn't serialized yet: its default
        if (strcmp(f->form, "entity") == 0) component_reference(x, n, slot, f->key, v);
        else if (strcmp(f->form, "list") == 0 && v->kind == YAML_SCALAR && hex_list(a, f->key, f, v->text ? v->text : "")) continue;
        else flatten(a, f->key, v);
    }
}

static void apply_modification(expansion *x, node **roots, const int count, const yaml_node *m)
{
    char guid[33];
    const int64_t target = yaml_reference(yaml_get(m, "target"), guid);
    const char *path = yaml_text(m, "propertyPath", "");
    const char *value = yaml_text(m, "value", "");
    int what = -1;
    node *n = find_any(roots, count, guid, target, 0, &what);
    if (!n) return;
    const double number = strtod(value, NULL);
    if (what == 0) {
        if (strcmp(path, "m_Name") == 0) {
            free(n->name);
            n->name = dup(value);
        } else if (strcmp(path, "m_IsActive") == 0) {
            n->active = number != 0;
        }
    } else if (what == 1) {
        static const char *const axes[] = {"x", "y", "z", "w"};
        for (int a = 0; a < 4; a++) {
            char p[64];
            snprintf(p, sizeof p, "m_LocalPosition.%s", axes[a]);
            if (a < 3 && strcmp(path, p) == 0) n->pos[a] = number;
            snprintf(p, sizeof p, "m_LocalRotation.%s", axes[a]);
            if (strcmp(path, p) == 0) n->rot[a] = number;
            snprintf(p, sizeof p, "m_LocalScale.%s", axes[a]);
            if (a < 3 && strcmp(path, p) == 0) n->scale[a] = number;
        }
    } else if (what == 2) {
        int index;
        const char *rest;
        if (strcmp(path, "components.Array.size") == 0) {
            resize_components(n, (int)number);
        } else if (array_path(path, "components.Array.data[", &index, &rest)) {
            // An instance's values are a patch: the fields it changed, over its source's
            if (strcmp(rest, ".type") == 0) set_component(n, index, value, NULL, false);
            else if (strcmp(rest, ".values") == 0) set_component(n, index, NULL, value, true);
        } else if (strcmp(path, "references.Array.size") == 0) {
            resize_refs(n, (int)number);
        } else if (array_path(path, "references.Array.data[", &index, &rest) && !*rest) {
            add_pending(x, n, index, yaml_get(m, "objectReference"));
        }
    } else if (what == 3) {
        // A change of a Tide component's field, by its property path: an entity's is the object it references
        node *owner = NULL;
        const ident *id = find_id(roots, count, guid, target, 3, &owner);
        if (!id || !id->component || owner != n) return;
        const int slot = owned_slot(n, id->component);
        char key[256];
        snprintf(key, sizeof key, "%s", path);
        char *dot = strchr(key, '.');
        if (dot) *dot = '\0';
        const map_field *f = field_of(id->component, key);
        if (!f) return; // A field the component no longer has: left out
        if (strcmp(f->form, "entity") == 0 && strcmp(path, f->key) == 0) component_reference(x, n, slot, path, yaml_get(m, "objectReference"));
        else set_flat(&n->owned[slot], path, value);
    }
}

static bool expand(project *p, asset *file, int depth, node ***roots, int *root_count);

static bool expand(project *p, asset *file, const int depth, node ***roots, int *root_count)
{
    *roots = NULL;
    *root_count = 0;
    if (depth > DEPTH_MOST) {
        fprintf(stderr, "%s: error: its prefabs nest more than %d deep, or one includes itself\n", file->local, DEPTH_MOST);
        p->failed = true;
        return false;
    }
    const yaml_file *f = yaml_of(p, file);
    if (!f) return false;
    expansion x = {p, file, f, NULL, 0, 0, NULL, 0, 0};
    // The file's own GameObjects, their Transforms
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (d->class_id != UNITY_GAME_OBJECT || d->stripped) continue;
        node *n = new_node();
        free(n->name);
        n->name = dup(yaml_text(d->body, "m_Name", "GameObject"));
        n->active = yaml_number(d->body, "m_IsActive", 1) != 0;
        add_id(n, file->guid, d->file_id, 0, NULL);
        add_top(&x, n);
    }
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (!is_transform(d) || d->stripped) continue;
        char unused[33];
        node *n = find(x.all, x.count, file->guid, yaml_reference(yaml_get(d->body, "m_GameObject"), unused), 0, 0);
        if (!n) continue;
        read_trs(n, d->body);
        add_id(n, file->guid, d->file_id, 1, NULL);
    }
    // Its prefab instances, each expanded from its prefab with the instance's changes
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (d->class_id != UNITY_PREFAB_INSTANCE || d->stripped) continue;
        char guid[33];
        yaml_reference(yaml_get(d->body, "m_SourcePrefab"), guid);
        asset *source = asset_of(p, guid);
        if (!source) {
            fprintf(stderr, "%s: error: it holds an instance of a prefab that isn't in the project (GUID %s)\n", file->local, guid);
            p->failed = true;
            continue;
        }
        node **inner;
        int inner_count;
        if (!expand(p, source, depth + 1, &inner, &inner_count)) continue;
        for (int k = 0; k < inner_count; k++) set_instance(inner[k], d->file_id);
        // What the instance's objects are in this file: Unity numbers each (instance ^ its ID in the prefab), so a
        // file instancing this one reaches objects nested in it by that
        for (int k = 0; k < inner_count; k++) add_instance_ids(inner[k], source->guid, file->guid, d->file_id);
        const yaml_node *mod = yaml_get(d->body, "m_Modification");
        const yaml_node *changes = yaml_get(mod, "m_Modifications");
        for (int k = 0; changes && changes->kind == YAML_SEQ && k < changes->count; k++) {
            apply_modification(&x, inner, inner_count, changes->items[k]);
        }
        const yaml_node *removed = yaml_get(mod, "m_RemovedComponents");
        for (int k = 0; removed && removed->kind == YAML_SEQ && k < removed->count; k++) {
            char g[33];
            const int64_t id = yaml_reference(removed->items[k], g);
            node *owner = NULL;
            const ident *component = find_id(inner, inner_count, g, id, 3, &owner);
            if (component && component->component) {
                owner->owned[owned_slot(owner, component->component)].removed = true;
                continue;
            }
            node *n = find(inner, inner_count, g, id, 2, 0);
            if (!n) continue;
            n->tide = false;
            resize_components(n, 0);
        }
        const yaml_node *gone = yaml_get(mod, "m_RemovedGameObjects");
        for (int k = 0; gone && gone->kind == YAML_SEQ && k < gone->count; k++) {
            char g[33];
            const int64_t id = yaml_reference(gone->items[k], g);
            node *n = find(inner, inner_count, g, id, 0, 0);
            if (n) n->active = false; // Gone, with what's under it
        }
        for (int k = 0; k < inner_count; k++) add_top(&x, inner[k]);
        free(inner);
    }
    // TideEntity components: of the file's GameObjects, and those added to its instances' objects
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (!is_tide_entity(d)) continue;
        char unused[33];
        node *n = node_of(f, file->guid, x.all, x.count, yaml_reference(yaml_get(d->body, "m_GameObject"), unused));
        if (!n) continue;
        n->tide = true;
        resize_components(n, 0);
        const yaml_node *list = yaml_get(d->body, "components");
        for (int k = 0; list && list->kind == YAML_SEQ && k < list->count; k++) {
            set_component(n, k, yaml_text(list->items[k], "type", ""), yaml_text(list->items[k], "values", ""), false);
        }
        resize_refs(n, 0);
        const yaml_node *refs = yaml_get(d->body, "references");
        for (int k = 0; refs && refs->kind == YAML_SEQ && k < refs->count; k++) add_pending(&x, n, k, refs->items[k]);
        add_id(n, file->guid, d->file_id, 2, NULL);
    }
    // Tide components of their own (com.tide.game's): of the file's GameObjects, and those added to its instances'
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (d->class_id != UNITY_MONO_BEHAVIOUR || d->stripped) continue;
        char script[33];
        yaml_reference(yaml_get(d->body, "m_Script"), script);
        const map_component *c = component_of(p, script);
        if (!c) continue; // Unity's own, a game's, or a component the game no longer has
        char unused[33];
        node *n = node_of(f, file->guid, x.all, x.count, yaml_reference(yaml_get(d->body, "m_GameObject"), unused));
        if (!n) continue;
        read_component(&x, n, c, d->body);
        add_id(n, file->guid, d->file_id, 3, c);
    }
    // Parents: each own Transform's children in its order, then what went under an instance's objects
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        if (!is_transform(d) || d->stripped) continue;
        node *parent = node_of(f, file->guid, x.all, x.count, d->file_id);
        const yaml_node *children = yaml_get(d->body, "m_Children");
        for (int k = 0; parent && children && children->kind == YAML_SEQ && k < children->count; k++) {
            char unused[33];
            node *child = node_of(f, file->guid, x.all, x.count, yaml_reference(children->items[k], unused));
            if (child) add_child(parent, child);
        }
    }
    for (int i = 0; i < f->count; i++) {
        const yaml_document *d = &f->documents[i];
        char unused[33];
        if (is_transform(d) && !d->stripped) {
            const int64_t father = yaml_reference(yaml_get(d->body, "m_Father"), unused);
            const yaml_document *fd = document(f, father);
            if (fd && fd->stripped) {
                node *parent = node_of(f, file->guid, x.all, x.count, father);
                node *child = node_of(f, file->guid, x.all, x.count, d->file_id);
                if (parent && child) add_child(parent, child);
            }
        } else if (d->class_id == UNITY_PREFAB_INSTANCE && !d->stripped) {
            const int64_t father = yaml_reference(yaml_get(yaml_get(d->body, "m_Modification"), "m_TransformParent"), unused);
            const yaml_document *fd = document(f, father);
            if (!fd || !fd->stripped) continue;
            node *parent = node_of(f, file->guid, x.all, x.count, father);
            for (int k = 0; parent && k < x.count; k++) {
                if (x.all[k]->instance == d->file_id && !x.all[k]->parent) add_child(parent, x.all[k]);
            }
        }
    }
    // References, now every node of the file is there
    for (int i = 0; i < x.ref_count; i++) {
        pending *r = &x.refs[i];
        reference *to = r->component >= 0 ? &r->n->owned[r->component].refs[r->at] : &r->n->refs[r->at];
        memset(to, 0, sizeof *to);
        to->object = true;
        if (r->guid[0] && strcmp(r->guid, file->guid) != 0) {
            node *inner = find_any(x.all, x.count, r->guid, r->file_id, 0, NULL); // An object of a prefab it instances
            if (inner) to->target = inner;
            else snprintf(to->guid, sizeof to->guid, "%s", r->guid); // An asset
        } else {
            to->target = node_of(f, file->guid, x.all, x.count, r->file_id);
        }
    }
    free(x.refs);
    // The roots: a scene's in its order, else those with no parent in the file's order
    const yaml_node *order = NULL;
    for (int i = 0; i < f->count; i++) {
        if (f->documents[i].class_id == UNITY_SCENE_ROOTS) order = yaml_get(f->documents[i].body, "m_Roots");
    }
    node **out = grab(sizeof *out * (size_t)(x.count + 1));
    int out_count = 0;
    for (int k = 0; order && order->kind == YAML_SEQ && k < order->count; k++) {
        char unused[33];
        const int64_t id = yaml_reference(order->items[k], unused);
        const yaml_document *d = document(f, id);
        for (int i = 0; i < x.count; i++) {
            // A root's Transform, or a prefab instance, whose roots are the instance's
            node *n = d && d->class_id == UNITY_PREFAB_INSTANCE ? (x.all[i]->instance == id ? x.all[i] : NULL)
                                                                : node_of(f, file->guid, x.all, x.count, id);
            bool taken = false;
            for (int j = 0; j < out_count; j++) taken |= out[j] == n;
            if (n && !n->parent && !taken) out[out_count++] = n;
            if (!d || d->class_id != UNITY_PREFAB_INSTANCE) break;
        }
    }
    for (int k = 0; k < x.count; k++) {
        bool taken = false;
        for (int j = 0; j < out_count; j++) taken |= out[j] == x.all[k];
        if (!x.all[k]->parent && !taken) out[out_count++] = x.all[k];
    }
    free(x.all);
    *roots = out;
    *root_count = out_count;
    return true;
}

// ---------------------------------------------------------------------------
// The Tide prefab

static bool tide_keyword(const char *s)
{
    static const char *const words[] = {"this", "entity", "prefab", "scene", "true", "false", "null", "default", "var", "mut",
                                        "local", "if", "else", "for", "foreach", "while", "return", "namespace", "using",
                                        "component", "singleton", "system", "view", "event", "command", "input", NULL};
    for (int i = 0; words[i]; i++) {
        if (strcmp(s, words[i]) == 0) return true;
    }
    return false;
}

// A GameObject's name as an entity's: letters, digits and _, not starting with a digit nor a keyword. The package's
// TidePaths does the same, to find each entity's GameObject.
static char *entity_name(const char *name)
{
    const size_t n = strlen(name);
    char *out = grab(n + 3);
    size_t k = 0;
    if (!n || (name[0] >= '0' && name[0] <= '9')) out[k++] = '_';
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        out[k++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ? c : '_';
    }
    out[k] = '\0';
    if (tide_keyword(out)) out[k++] = '_';
    return out;
}

typedef struct trs {
    double pos[3], rot[4], scale[3];
} trs;

static trs trs_of(const node *n)
{
    trs t;
    memcpy(t.pos, n->pos, sizeof t.pos);
    memcpy(t.rot, n->rot, sizeof t.rot);
    memcpy(t.scale, n->scale, sizeof t.scale);
    return t;
}

static void rotate(const double q[4], const double v[3], double out[3])
{
    // v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    const double cx = q[1] * v[2] - q[2] * v[1] + q[3] * v[0];
    const double cy = q[2] * v[0] - q[0] * v[2] + q[3] * v[1];
    const double cz = q[0] * v[1] - q[1] * v[0] + q[3] * v[2];
    out[0] = v[0] + 2 * (q[1] * cz - q[2] * cy);
    out[1] = v[1] + 2 * (q[2] * cx - q[0] * cz);
    out[2] = v[2] + 2 * (q[0] * cy - q[1] * cx);
}

// `child` placed under `parent`: a GameObject that's no entity passes its placement on to the entities under it.
static trs compose(const trs parent, const trs child)
{
    trs t;
    const double scaled[3] = {parent.scale[0] * child.pos[0], parent.scale[1] * child.pos[1], parent.scale[2] * child.pos[2]};
    double turned[3];
    rotate(parent.rot, scaled, turned);
    for (int i = 0; i < 3; i++) {
        t.pos[i] = parent.pos[i] + turned[i];
        t.scale[i] = parent.scale[i] * child.scale[i];
    }
    const double *a = parent.rot, *b = child.rot;
    t.rot[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    t.rot[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    t.rot[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    t.rot[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    return t;
}

static void number(text *t, double v)
{
    if (!isfinite(v)) v = 0;
    if (v == 0) v = 0; // No -0
    char s[64];
    snprintf(s, sizeof s, "%.9g", v);
    puts_(t, s);
}

typedef struct emitter {
    project *p;
    const asset *file;
    node *root;
    text out;
    int asset;  // Its number in the map
    text nodes; // The map's nodes: each entity's GameObject, by its path, as JSON strings
    int node_count;
} emitter;

static void put_json(text *t, const char *s)
{
    puts_(t, "\"");
    for (const char *c = s; *c; c++) {
        if (*c == '"' || *c == '\\') {
            put(t, "\\", 1);
            put(t, c, 1);
        } else if ((unsigned char)*c < 0x20) {
            putf(t, "\\u%04x", (unsigned)(unsigned char)*c);
        } else {
            put(t, c, 1);
        }
    }
    puts_(t, "\"");
}

// `n`'s GameObject from the asset's: names from its root, "Arm#1" for a second Arm under one parent.
static void game_object_path(const emitter *e, const node *n, text *out)
{
    if (!n || n == e->root) return;
    game_object_path(e, n->parent, out);
    if (out->n) puts_(out, "/");
    puts_(out, n->name);
    int before = 0;
    for (int i = 0; n->parent && i < n->parent->child_count && n->parent->children[i] != n; i++) {
        before += strcmp(n->parent->children[i]->name, n->name) == 0;
    }
    if (before) putf(out, "#%d", before);
}

// A path from the prefab's root to `n`, as authored values name entities, or NULL for none.
static char *path_to(const emitter *e, const node *n)
{
    if (!n || !n->emitted || n == e->root) return NULL;
    char *parts[DEPTH_MOST * 8];
    int count = 0;
    for (const node *at = n; at && at != e->root && count < (int)(sizeof parts / sizeof parts[0]); at = at->parent) {
        if (at->emitted) parts[count++] = at->emitted;
    }
    text t = {0};
    puts_(&t, "");
    for (int i = count - 1; i >= 0; i--) {
        if (t.n) puts_(&t, ".");
        puts_(&t, parts[i]);
    }
    return t.s;
}

static void indent(text *t, const int depth);

// What a reference names, as an entity field's value: `this`, an entity's path, or `default`; `k` is its number as
// the error says it.
static void put_reference(emitter *e, const node *n, const reference *r, const long k, text *t)
{
    if (!r || !r->object) {
        puts_(t, "default");
    } else if (r->target == n) {
        puts_(t, "this");
    } else if (r->target && r->target->emitted) {
        char *path = path_to(e, r->target);
        if (path) {
            puts_(t, path);
            free(path);
        } else {
            fprintf(stderr, "%s: error: %s names its prefab's root, which only the root's own values can, as 'this'\n",
                    e->file->local, n->name);
            e->p->failed = true;
            puts_(t, "default");
        }
    } else if (r->target) {
        fprintf(stderr, "%s: error: %s's reference @%ld is to %s, which has no TideEntity (or is inactive): only entities "
                        "can be named\n", e->file->local, n->name, k, r->target->name);
        e->p->failed = true;
        puts_(t, "default");
    } else {
        fprintf(stderr, "%s: error: %s's reference @%ld is to an asset (GUID %s): Tide fields take entities from Unity's "
                        "references, not assets yet\n", e->file->local, n->name, k, r->guid);
        e->p->failed = true;
        puts_(t, "default");
    }
}

// A component's values with each `@k` (its TideEntity's kth reference) as what it names.
static char *resolve_values(emitter *e, const node *n, const char *values)
{
    text t = {0};
    puts_(&t, "");
    char quote = 0;
    for (const char *c = values; *c; c++) {
        if (quote) {
            put(&t, c, 1);
            if (*c == '\\' && c[1]) put(&t, ++c, 1);
            else if (*c == quote) quote = 0;
            continue;
        }
        if (*c == '"' || *c == '\'') quote = *c;
        if (*c != '@' || !(c[1] >= '0' && c[1] <= '9')) {
            put(&t, c, 1);
            continue;
        }
        char *end = NULL;
        const long k = strtol(c + 1, &end, 10);
        c = end - 1;
        put_reference(e, n, k >= 0 && k < n->ref_count ? &n->refs[k] : NULL, k, &t);
    }
    return t.s;
}

// A number Unity wrote, as Tide writes it: an integer as itself, else as `number` does; false for none.
static bool put_number(text *t, const char *v)
{
    if (!v || !*v) return false;
    const char *c = v + (*v == '-');
    bool integer = *c != '\0';
    for (; *c; c++) integer = integer && *c >= '0' && *c <= '9';
    if (integer) {
        puts_(t, v);
        return true;
    }
    char *end = NULL;
    const double d = strtod(v, &end);
    if (end == v) return false;
    number(t, d);
    return true;
}

static void put_string(text *t, const char *v)
{
    puts_(t, "\"");
    for (const char *c = v; *c; c++) {
        if (*c == '"' || *c == '\\') {
            put(t, "\\", 1);
            put(t, c, 1);
        } else if (*c == '\n') {
            puts_(t, "\\n");
        } else if (*c == '\t') {
            puts_(t, "\\t");
        } else if (*c != '\r') {
            put(t, c, 1);
        }
    }
    puts_(t, "\"");
}

// A value of `form` Unity serialized at `path` of an own component, as a Tide value into `t`: false where Unity has
// nothing of it, which leaves the field its default.
static bool put_form(emitter *e, const node *n, const owned *o, const map_field *f, const char *form, const char *path, text *t)
{
    char inner[512];
    static const struct { const char *form, *tide, *axes; } vectors[] = {
        {"float2", "float2", "xy"}, {"float3", "float3", "xyz"}, {"float4", "float4", "xyzw"}, {"int2", "int2", "xy"},
        {"int3", "int3", "xyz"}, {"quaternion", "quaternion", "xyzw"}, {"color", "Color", "rgba"},
    };
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        if (strcmp(form, vectors[i].form) != 0) continue;
        bool any = false;
        for (const char *a = vectors[i].axes; *a; a++) {
            snprintf(inner, sizeof inner, "%s.%c", path, *a);
            any = any || get_flat(o, inner);
        }
        if (!any) return false;
        putf(t, "%s(", vectors[i].tide);
        for (const char *a = vectors[i].axes; *a; a++) {
            snprintf(inner, sizeof inner, "%s.%c", path, *a);
            if (a != vectors[i].axes) puts_(t, ", ");
            if (!put_number(t, get_flat(o, inner))) puts_(t, *a == 'w' && form[0] == 'q' ? "1" : "0");
        }
        puts_(t, ")");
        return true;
    }
    if (strcmp(form, "list") == 0) {
        snprintf(inner, sizeof inner, "%s.Array.size", path);
        const char *size = get_flat(o, inner);
        if (!size) return false;
        const long count = strtol(size, NULL, 10);
        puts_(t, "[");
        for (long i = 0; i < count && i < 1000000; i++) {
            if (i) puts_(t, ", ");
            snprintf(inner, sizeof inner, "%s.Array.data[%ld]", path, i);
            text element = {0};
            puts_(&element, "");
            const bool given = put_form(e, n, o, f, f->element ? f->element : "", inner, &element);
            puts_(t, given ? element.s : "default");
            free(element.s);
        }
        puts_(t, "]");
        return true;
    }
    const char *v = get_flat(o, path);
    if (!v) return false;
    if (strcmp(form, "number") == 0) return put_number(t, v);
    if (strcmp(form, "bool") == 0) {
        puts_(t, strtol(v, NULL, 10) ? "true" : "false");
        return true;
    }
    if (strcmp(form, "string") == 0) {
        put_string(t, v);
        return true;
    }
    if (strcmp(form, "literal") == 0) {
        if (!v[0]) return false; // Its default
        puts_(t, v);
        return true;
    }
    if (strcmp(form, "entity") == 0) {
        const long k = v[0] == '@' ? strtol(v + 1, NULL, 10) : -1;
        put_reference(e, n, k >= 0 && k < o->ref_count ? &o->refs[k] : NULL, k, t);
        return true;
    }
    if (strcmp(form, "enum") == 0) {
        const long value = strtol(v, NULL, 10);
        for (int i = 0; f->enumeration && i < e->p->enum_count; i++) {
            const map_enum *m = &e->p->enums[i];
            if (strcmp(m->name, f->enumeration) != 0) continue;
            for (int k = 0; k < m->count; k++) {
                if (m->values[k] != value) continue;
                putf(t, "%s.%s", m->name, m->members[k]);
                return true;
            }
        }
        return false; // A value the enum has no member of: its default
    }
    return false;
}

// An own component's declaration of an entity: its type, with the fields Unity serialized.
static void write_owned(emitter *e, const node *n, const owned *o, const int depth)
{
    text values = {0};
    puts_(&values, "");
    for (int i = 0; i < o->component->field_count; i++) {
        const map_field *f = &o->component->fields[i];
        text value = {0};
        puts_(&value, "");
        if (put_form(e, n, o, f, f->form, f->key, &value)) putf(&values, "%s%s = %s", values.n ? ", " : "", f->name, value.s);
        free(value.s);
    }
    indent(&e->out, depth);
    if (values.n) putf(&e->out, "%s { %s };\n", o->component->name, values.s);
    else putf(&e->out, "%s;\n", o->component->name);
    free(values.s);
}

static void indent(text *t, const int depth)
{
    for (int i = 0; i < depth; i++) puts_(t, "    ");
}

static bool emitted_entity(const node *n)
{
    return n->tide && n->active;
}

static void name_children(node *n);
static void write_entity(emitter *e, node *n, trs placement, const int depth, const bool transform);

// The entities under `n`: its children that are, and those under its children that aren't, in order.
static void collect(node *n, node ***out, int *count, int *capacity)
{
    for (int i = 0; i < n->child_count; i++) {
        node *c = n->children[i];
        if (!c->active) continue;
        if (emitted_entity(c)) {
            grow((void **)out, capacity, *count, sizeof **out);
            (*out)[(*count)++] = c;
        } else {
            collect(c, out, count, capacity);
        }
    }
}

// Names the entities under `n`, each among its siblings once: a second Bullet is Bullet_2.
static void name_children(node *n)
{
    node **kids = NULL;
    int count = 0, capacity = 0;
    collect(n, &kids, &count, &capacity);
    for (int i = 0; i < count; i++) {
        char *base = entity_name(kids[i]->name);
        char *name = dup(base);
        for (int k = 2; ; k++) {
            bool taken = false;
            for (int j = 0; j < i; j++) taken |= strcmp(kids[j]->emitted, name) == 0;
            if (!taken) break;
            free(name);
            name = grab(strlen(base) + 16);
            sprintf(name, "%s_%d", base, k);
        }
        free(base);
        free(kids[i]->emitted);
        kids[i]->emitted = name;
        name_children(kids[i]);
    }
    free(kids);
}

// A node's placement relative to the entity above it: through the GameObjects between them that aren't entities.
static trs placement_under(const node *entity_parent, node *n)
{
    trs t = trs_of(n);
    for (node *at = n->parent; at && at != entity_parent; at = at->parent) t = compose(trs_of(at), t);
    return t;
}

static void write_entity(emitter *e, node *n, const trs placement, const int depth, const bool transform)
{
    // Which GameObject shows it
    text path = {0};
    puts_(&path, "");
    if (transform) game_object_path(e, n, &path); // Not a scene's own entity, which none does
    if (e->node_count) puts_(&e->nodes, ", ");
    put_json(&e->nodes, path.s);
    free(path.s);
    indent(&e->out, depth);
    putf(&e->out, "UnityEngine.Source { asset = %d, node = %d };\n", e->asset, e->node_count++);
    // Tide's Transform: Unity's placement, with what the TideEntity says of it over it
    const char *own_transform = NULL;
    for (int i = 0; i < n->component_count; i++) {
        if (strcmp(n->components[i].type, "Transform") == 0) own_transform = n->components[i].values;
    }
    if (transform) {
        text values = {0};
        puts_(&values, "localPosition = float3(");
        number(&values, placement.pos[0]);
        puts_(&values, ", ");
        number(&values, placement.pos[1]);
        puts_(&values, ", ");
        number(&values, placement.pos[2]);
        puts_(&values, "), localRotation = quaternion(");
        for (int i = 0; i < 4; i++) {
            if (i) puts_(&values, ", ");
            number(&values, placement.rot[i]);
        }
        puts_(&values, "), localScale = float3(");
        number(&values, placement.scale[0]);
        puts_(&values, ", ");
        number(&values, placement.scale[1]);
        puts_(&values, ", ");
        number(&values, placement.scale[2]);
        puts_(&values, ")");
        char *merged = own_transform ? merge_values(values.s, own_transform) : dup(values.s);
        char *resolved = resolve_values(e, n, merged);
        indent(&e->out, depth);
        putf(&e->out, "Transform { %s };\n", resolved);
        free(resolved);
        free(merged);
        free(values.s);
    }
    for (int i = 0; i < n->component_count; i++) {
        const authored *c = &n->components[i];
        if (!c->type[0] || strcmp(c->type, "Transform") == 0) continue;
        indent(&e->out, depth);
        if (c->values[0]) {
            char *resolved = resolve_values(e, n, c->values);
            putf(&e->out, "%s { %s };\n", c->type, resolved);
            free(resolved);
        } else {
            putf(&e->out, "%s;\n", c->type);
        }
    }
    for (int i = 0; i < n->owned_count; i++) {
        if (!n->owned[i].removed) write_owned(e, n, &n->owned[i], depth);
    }
    node **kids = NULL;
    int count = 0, capacity = 0;
    collect(n, &kids, &count, &capacity);
    for (int i = 0; i < count; i++) {
        node *k = kids[i];
        indent(&e->out, depth);
        putf(&e->out, "entity %s // %s\n", k->emitted, k->name);
        indent(&e->out, depth);
        puts_(&e->out, "{\n");
        write_entity(e, k, placement_under(n, k), depth + 1, true);
        indent(&e->out, depth);
        puts_(&e->out, "}\n");
    }
    free(kids);
}

static char *format_name(const char *first, const char *second)
{
    const size_t n = strlen(first) + strlen(second) + 2;
    char *out = grab(n);
    snprintf(out, n, "%s.%s", first, second);
    return out;
}

// The folders an asset is in under Assets, as the tail of its namespace: "Enemies.Bosses" for
// Assets/Enemies/Bosses/Goblin.prefab, each a name as an entity's is; "" for one in Assets itself. Tide finds it by
// its file's name alone too while only one has it (find_in_folders in check.c).
static char *asset_folders(const char *path)
{
    const char *start = strncmp(path, "Assets/", 7) == 0 ? path + 7 : path;
    text t = {0};
    puts_(&t, "");
    for (const char *at = start;;) {
        const char *slash = strchr(at, '/');
        if (!slash) break;
        const size_t n = (size_t)(slash - at);
        char *raw = grab(n + 1);
        memcpy(raw, at, n);
        char *part = entity_name(raw);
        putf(&t, "%s%s", t.s[0] ? "." : "", part);
        free(part);
        free(raw);
        at = slash + 1;
    }
    return t.s;
}

// The base of an asset's file name, as a prefab's name.
static char *asset_name(const char *path)
{
    const char *base = path;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\') base = c + 1;
    }
    const char *dot = strrchr(base, '.');
    const size_t n = dot ? (size_t)(dot - base) : strlen(base);
    char *raw = grab(n + 1);
    memcpy(raw, base, n);
    char *name = entity_name(raw);
    free(raw);
    return name;
}

static char *write_prefab(project *p, const asset *a, node **roots, const int count, const bool scene, const int number,
                          text *map)
{
    emitter e = {p, a, NULL, {0}, number, {0}, 0};
    puts_(&e.nodes, "");
    char *name = asset_name(a->local);
    char *folders = asset_folders(a->local);
    putf(&e.out, "// From %s, as Tide's package for Unity writes it (TideEntity)\n", a->local);
    // A prefab's is UnityEngine.Prefabs.Folders.Name; a scene's, UnityEngine.SceneContents.Folders.Name, what its
    // scene type, UnityEngine.Scenes.Folders.Name, loads (unity_declarations)
    putf(&e.out, "namespace UnityEngine.%s%s%s;\n\n", scene ? "SceneContents" : "Prefabs", folders[0] ? "." : "", folders);
    free(folders);
    putf(&e.out, "prefab %s\n{\n", name);
    if (scene || count != 1) {
        // A scene's roots go under an entity of its own: the scene's
        node *top = new_node();
        free(top->name);
        top->name = dup(name);
        for (int i = 0; i < count; i++) {
            roots[i]->parent = NULL;
            add_child(top, roots[i]);
        }
        e.root = top;
        name_children(top);
        write_entity(&e, top, trs_of(top), 1, false);
        top->child_count = 0; // The roots are the caller's
        free_node(top);
    } else {
        e.root = roots[0];
        name_children(roots[0]);
        write_entity(&e, roots[0], trs_of(roots[0]), 1, true);
    }
    puts_(&e.out, "}\n");
    free(name);
    if (number) puts_(map, ",");
    puts_(map, "\n    {\"guid\": ");
    put_json(map, a->guid);
    puts_(map, ", \"path\": ");
    put_json(map, a->local);
    putf(map, ", \"scene\": %s, \"nodes\": [%s]}", scene ? "true" : "false", e.nodes.s);
    free(e.nodes.s);
    return e.out.s;
}

// ---------------------------------------------------------------------------
// The match's Unity scenes (UNITY_SOURCE_PATH's declarations)

typedef struct unity_scene {
    int number;  // The asset's in the map, as UnityEngine.Source has it
    char *name;  // Its name under UnityEngine.Scenes and UnityEngine.SceneContents: its folders' and its own, "Sample.Bonus"
    char *base;  // ...its own: the file's, "Bonus"
    char *ns;    // ...and its folders', "Sample", or ""
    char *handler; // What its Fill handler is called after: the name, with '_' for each '.'
    char guid[33];
} unity_scene;

// The first scene Build Settings has on, as ProjectSettings/EditorBuildSettings.asset says: its GUID into `guid`
// ("" for none).
static void first_build_scene(const project *p, char guid[33])
{
    guid[0] = '\0';
    char *dir = grab(p->project_length + 64);
    memcpy(dir, p->assets, p->project_length);
    strcpy(dir + p->project_length, "ProjectSettings/EditorBuildSettings.asset");
    size_t size = 0;
    char *body = sys_read_file(dir, &size);
    free(dir);
    yaml_file f;
    if (!body || !yaml_read(body, &f)) {
        free(body);
        return;
    }
    free(body);
    for (int d = 0; d < f.count && !guid[0]; d++) {
        const yaml_node *scenes = yaml_get(f.documents[d].body, "m_Scenes");
        for (int i = 0; scenes && scenes->kind == YAML_SEQ && i < scenes->count; i++) {
            if (yaml_number(scenes->items[i], "enabled", 0) == 0) continue;
            snprintf(guid, 33, "%s", yaml_text(scenes->items[i], "guid", ""));
            break;
        }
    }
    yaml_free(&f);
}

// UnityEngine.Source, and with the project's scenes of Tide entities: which the match has loaded (Unity loads and unloads
// them as it does), Build Settings' first as Main starts, as a player build starts with it, and the editor's command
// to have the scenes it has open, which only the match's first player can send: the server's as the match started.
static char *unity_declarations(const unity_scene *scenes, const int count, const char *first)
{
    text t = {0};
    puts_(&t, "// What Tide's package for Unity needs of the match: which of Unity's GameObjects shows an entity, and which of\n"
              "// Unity's scenes the match has\n"
              "namespace UnityEngine\n{\n    component Source\n    {\n        int asset;\n        int node;\n    }\n");
    if (count) {
        puts_(&t, "\n    // The project's scenes the match has loaded, by their asset numbers in Source, and the player whose editor\n"
                  "    // can choose them\n"
                  "    singleton LoadedScenes\n    {\n        Map<int, Entity> loaded;\n        PlayerID opener;\n        bool heard;\n"
                  "        bool opened; // The editor chose them\n    }\n"
                  "\n    // The server's player joins first, before the match's first tick\n"
                  "    event(PlayerJoined joined) Arrived(mut LoadedScenes scenes)\n    {\n"
                  "        if (scenes.heard) return;\n        scenes.heard = true;\n        scenes.opener = joined.player;\n    }\n");
        for (int i = 0; first && i < count; i++) {
            if (strcmp(scenes[i].name, first) != 0) continue;
            putf(&t, "\n    // Build Settings' first scene, which a player build starts with\n"
                     "    event(Spawned) Begin(with Main, mut LoadedScenes scenes)\n    {\n"
                     "        if (scenes.opened) return; // The editor's command came first\n"
                     "        scenes.loaded[%d] = Scene.Load(UnityEngine.Scenes.%s);\n    }\n", scenes[i].number, scenes[i].name);
        }
        puts_(&t, "\n    // The editor's, as Play starts there: a scene it has open; `only` unloads the others the match loaded for it\n"
                  "    command OpenScene\n    {\n        int asset;\n        bool only;\n    }\n"
                  "\n    command(OpenScene open, PlayerID sender) Opening(mut LoadedScenes scenes)\n    {\n"
                  "        // The first player's, which may come in the first tick, before its PlayerJoined does\n"
                  "        if (!scenes.heard)\n        {\n            scenes.heard = true;\n            scenes.opener = sender;\n        }\n"
                  "        if (sender != scenes.opener) return;\n        scenes.opened = true;\n"
                  "        if (open.only)\n        {\n            mut List<int> others = [];\n"
                  "            foreach (var entry in scenes.loaded)\n            {\n"
                  "                if (entry.key != open.asset) others.Add(entry.key);\n            }\n"
                  "            foreach (var asset in others)\n            {\n"
                  "                Scene.Unload(scenes.loaded[asset]);\n                scenes.loaded.Remove(asset);\n            }\n        }\n"
                  "        if (scenes.loaded.Has(open.asset)) return;\n"
                  "        switch (open.asset)\n        {\n");
        for (int i = 0; i < count; i++) {
            putf(&t, "            case %d: scenes.loaded[%d] = Scene.Load(UnityEngine.Scenes.%s); break;\n", scenes[i].number,
                 scenes[i].number, scenes[i].name);
        }
        puts_(&t, "        }\n    }\n");
        // Each scene's entities, its prefab's, as it loads
        for (int i = 0; i < count; i++) {
            putf(&t, "\n    event(Spawned) Fill%s(with UnityEngine.Scenes.%s)\n    {\n        Spawn(UnityEngine.SceneContents.%s);\n    }\n",
                 scenes[i].handler, scenes[i].name, scenes[i].name);
        }
    }
    puts_(&t, "}\n");
    // The scenes, as Tide's: what game code loads (Scene.Load(UnityEngine.Scenes.Level)) and filters by (with scene ...)
    if (count) puts_(&t, "\n// The project's Unity scenes, as Tide's, each in its folders' namespace\n");
    for (int i = 0; i < count; i++) {
        putf(&t, "namespace UnityEngine.Scenes%s%s\n{\n    scene %s { } // %s.unity\n}\n", scenes[i].ns[0] ? "." : "",
             scenes[i].ns, scenes[i].base, scenes[i].base);
    }
    return t.s;
}
// ---------------------------------------------------------------------------

static void stamp_file(void *user, const char *path)
{
    unsigned long long *h = user;
    for (const char *c = path; *c; c++) *h = (*h ^ (unsigned char)*c) * 0x100000001B3ull;
    *h = (*h ^ sys_file_stamp(path)) * 0x100000001B3ull;
}

unsigned long long unity_assets_stamp(const char *assets)
{
    unsigned long long h = 0xCBF29CE484222325ull;
    char *dir = path_join(assets, "");
    folder_find(dir, ".prefab", stamp_file, &h);
    folder_find(dir, ".unity", stamp_file, &h);
    folder_find(dir, ".meta", stamp_file, &h);
    // ...and Build Settings, whose first scene the match starts with
    char *absolute = path_absolute(assets);
    char *project = path_dir(absolute);
    char *settings = path_join(project, "ProjectSettings/EditorBuildSettings.asset");
    stamp_file(&h, settings);
    free(settings);
    // ...and the game's components as Unity has them, which their YAML is read by
    char *components = path_join(project, "Packages/com.tide.game/components.json");
    stamp_file(&h, components);
    free(components);
    free(project);
    free(absolute);
    free(dir);
    return h;
}

bool unity_assets_read(const char *assets, unity_sources *out)
{
    *out = (unity_sources){0};
    project p = {0};
    char *absolute = path_absolute(assets);
    p.assets = path_join(absolute, "");
    free(absolute);
    // The project's folder: the one Assets is in
    const size_t length = strlen(p.assets);
    p.project_length = length >= 7 && strcmp(p.assets + length - 7, "Assets/") == 0 ? length - 7 : length;
    folder_find(p.assets, ".meta", index_meta, &p);
    read_components(&p);
    int capacity = 0;
    text map = {0};
    puts_(&map, "{\"assets\": [");
    grow((void **)&out->items, &capacity, out->count, sizeof *out->items);
    out->items[out->count++] = (unity_source){dup(UNITY_SOURCE_PATH), NULL}; // Its text once the assets are known
    unity_scene *scenes = NULL;
    int scene_count = 0, scene_capacity = 0;
    char first_guid[33];
    first_build_scene(&p, first_guid);
    const char *first = NULL;
    int number = 0;
    for (int i = 0; i < p.count; i++) {
        asset *a = &p.items[i];
        const bool scene = ends_with(a->path, ".unity");
        if (!scene && !ends_with(a->path, ".prefab")) continue;
        if (!relevant(&p, a)) continue;
        node **roots;
        int count;
        if (!expand(&p, a, 0, &roots, &count)) continue;
        if (count > 0) {
            grow((void **)&out->items, &capacity, out->count, sizeof *out->items);
            unity_source *s = &out->items[out->count++];
            s->path = dup(a->local);
            if (scene) {
                grow((void **)&scenes, &scene_capacity, scene_count, sizeof *scenes);
                unity_scene *u = &scenes[scene_count++];
                u->number = number;
                u->base = asset_name(a->local);
                u->ns = asset_folders(a->local);
                u->name = u->ns[0] ? format_name(u->ns, u->base) : dup(u->base);
                u->handler = dup(u->name);
                for (char *c = u->handler; *c; c++) {
                    if (*c == '.') *c = '_';
                }
                snprintf(u->guid, sizeof u->guid, "%s", a->guid);
                if (strcmp(a->guid, first_guid) == 0) first = u->name;
            }
            s->text = write_prefab(&p, a, roots, count, scene, number++, &map);
        }
        for (int k = 0; k < count; k++) free_node(roots[k]);
        free(roots);
    }
    puts_(&map, "\n]}\n");
    out->map = map.s;
    out->items[0].text = unity_declarations(scenes, scene_count, first);
    for (int i = 0; i < scene_count; i++) {
        free(scenes[i].name);
        free(scenes[i].base);
        free(scenes[i].ns);
        free(scenes[i].handler);
    }
    free(scenes);
    if (out->count == 1) { // No asset holds TideEntity objects: no UnityEngine.Source either
        free(out->items[0].path);
        free(out->items[0].text);
        out->count = 0;
    }
    // Two prefabs, or two scenes, of one folder whose names are one Tide name (My Level and My_Level, which a name
    // can't tell apart)
    for (int i = 1; i < out->count; i++) {
        for (int k = i + 1; k < out->count; k++) {
            if (ends_with(out->items[i].path, ".unity") != ends_with(out->items[k].path, ".unity")) continue;
            char *a = asset_name(out->items[i].path), *b = asset_name(out->items[k].path);
            char *fa = asset_folders(out->items[i].path), *fb = asset_folders(out->items[k].path);
            if (strcmp(a, b) == 0 && strcmp(fa, fb) == 0) {
                fprintf(stderr, "%s: error: %s has the same Tide name, %s%s%s: rename one\n", out->items[k].path,
                        out->items[i].path, fa, fa[0] ? "." : "", a);
                p.failed = true;
            }
            free(a);
            free(b);
            free(fa);
            free(fb);
        }
    }
    for (int i = 0; i < p.count; i++) {
        free(p.items[i].path);
        free(p.items[i].local);
        if (p.items[i].readable) yaml_free(&p.items[i].yaml);
    }
    free(p.items);
    free_components(&p);
    free(p.assets);
    return !p.failed;
}

void unity_sources_free(unity_sources *sources)
{
    for (int i = 0; i < sources->count; i++) {
        free(sources->items[i].path);
        free(sources->items[i].text);
    }
    free(sources->items);
    free(sources->map);
    *sources = (unity_sources){0};
}
