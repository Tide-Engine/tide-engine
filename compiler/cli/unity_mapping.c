// What a package says of its own components in a Unity project (unity_mapping.h).

#include "unity_mapping.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "sys.h"

static char *copy_n(const char *s, const size_t n)
{
    char *out = malloc(n + 1);
    if (!out) abort();
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char *copy(const char *s)
{
    return s ? copy_n(s, strlen(s)) : NULL;
}

static void *more(void *items, const int count, const size_t size)
{
    void *grown = realloc(items, size * (size_t)(count + 1));
    if (!grown) abort();
    return grown;
}

static const struct {
    const char *name;
    unity_scale scale;
} scales[] = {
    {"scale", UNITY_SCALE_EACH}, {"absScale", UNITY_SCALE_EACH_ABS}, {"largestScale", UNITY_SCALE_LARGEST},
    {"largestScaleXY", UNITY_SCALE_LARGEST_XY}, {"largestScaleXZ", UNITY_SCALE_LARGEST_XZ}, {"scaleX", UNITY_SCALE_X},
    {"scaleY", UNITY_SCALE_Y}, {"scaleZ", UNITY_SCALE_Z},
};

const char *unity_scale_name(const unity_scale scale)
{
    for (size_t i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        if (scales[i].scale == scale) return scales[i].name;
    }
    return NULL;
}

static double largest(const double a, const double b)
{
    return fabs(a) > fabs(b) ? fabs(a) : fabs(b);
}

double unity_scale_factor(const unity_scale by, const double scale[3], const int axis)
{
    switch (by) {
    case UNITY_SCALE_EACH: return axis >= 0 && axis < 3 ? scale[axis] : 1;
    case UNITY_SCALE_EACH_ABS: return axis >= 0 && axis < 3 ? fabs(scale[axis]) : 1;
    case UNITY_SCALE_LARGEST: return largest(largest(scale[0], scale[1]), scale[2]);
    case UNITY_SCALE_LARGEST_XY: return largest(scale[0], scale[1]);
    case UNITY_SCALE_LARGEST_XZ: return largest(scale[0], scale[2]);
    case UNITY_SCALE_X: return fabs(scale[0]);
    case UNITY_SCALE_Y: return fabs(scale[1]);
    case UNITY_SCALE_Z: return fabs(scale[2]);
    case UNITY_SCALE_NONE: break;
    }
    return 1;
}

// One component's rules: false for what isn't one, with `why` said.
static bool read_rules(unity_component_rules *r, const json *spec, const char **why)
{
    const json *hidden = json_get(spec, "hidden"), *scaled = json_get(spec, "scaled"), *shape = json_get(spec, "shape");
    if (hidden && hidden->kind == JSON_TRUE) {
        r->hidden = true;
    } else if (hidden && hidden->kind == JSON_ARRAY) {
        for (int i = 0; i < hidden->count; i++) {
            const char *field = json_str(hidden->items[i]);
            if (!field) return *why = "\"hidden\" is true, or a list of fields", false;
            r->hidden_fields = more(r->hidden_fields, r->hidden_count, sizeof *r->hidden_fields);
            r->hidden_fields[r->hidden_count++] = copy(field);
        }
    } else if (hidden && hidden->kind != JSON_FALSE) {
        return *why = "\"hidden\" is true, or a list of fields", false;
    }
    if (scaled && scaled->kind != JSON_OBJECT) return *why = "\"scaled\" is an object of fields, each a scale", false;
    for (int i = 0; scaled && i < scaled->count; i++) {
        const char *by = json_str(scaled->items[i]);
        unity_scale scale = UNITY_SCALE_NONE;
        for (size_t k = 0; by && k < sizeof scales / sizeof scales[0]; k++) {
            if (strcmp(by, scales[k].name) == 0) scale = scales[k].scale;
        }
        if (scale == UNITY_SCALE_NONE) {
            return *why = "a \"scaled\" field's scale is scale, absScale, largestScale, largestScaleXY, largestScaleXZ, "
                          "scaleX, scaleY or scaleZ",
                   false;
        }
        r->scaled = more(r->scaled, r->scaled_count, sizeof *r->scaled);
        r->scaled[r->scaled_count++] = (unity_scaled){copy_n(scaled->keys[i], scaled->key_lens[i]), scale};
    }
    if (shape) {
        static const char *const kinds[] = {"box", "sphere", "capsule", "rect", "circle"};
        for (size_t k = 0; k < sizeof kinds / sizeof kinds[0] && !r->shape; k++) {
            const char *size = json_str(json_get(shape, kinds[k]));
            if (!size) continue;
            r->shape = copy(kinds[k]);
            r->size = copy(size);
        }
        r->height = copy(json_str(json_get(shape, "height")));
        r->center = copy(json_str(json_get(shape, "center")));
        if (!r->shape || (strcmp(r->shape, "capsule") == 0) != (r->height != NULL)) {
            return *why = "a \"shape\" names its size field as \"box\", \"sphere\", \"capsule\" (with \"height\"), \"rect\" or "
                          "\"circle\", and its \"center\"",
                   false;
        }
    }
    return true;
}

static bool read_file(unity_mappings *out, const char *path, const char *package)
{
    size_t size = 0;
    char *body = sys_read_file(path, &size);
    if (!body) return true;
    const json *root = json_parse(body, size);
    const json *components = json_get(root, "components"), *source = json_get(root, "source");
    bool ok = root && root->kind == JSON_OBJECT;
    if (!ok) fprintf(stderr, "%s: error: it isn't JSON tide reads: an object, with \"components\" and \"source\"\n", path);
    if (source && source->kind == JSON_ARRAY) {
        size_t length = strlen(path) + 128;
        for (int i = 0; i < source->count; i++) length += (json_str(source->items[i]) ? strlen(json_str(source->items[i])) : 0) + 1;
        char *text = malloc(length);
        if (!text) abort();
        int at = snprintf(text, length, "// From %s: the package's Tide for a Unity project\n", path);
        for (int i = 0; i < source->count; i++) {
            const char *line = json_str(source->items[i]);
            at += snprintf(text + at, length - (size_t)at, "%s\n", line ? line : "");
        }
        char name[256];
        snprintf(name, sizeof name, "Library/Tide/Unity.%s.tide", package);
        out->glue = more(out->glue, out->glue_count, sizeof *out->glue);
        out->glue[out->glue_count++] = (unity_glue){copy(package), copy(name), text};
    }
    for (int i = 0; components && components->kind == JSON_OBJECT && i < components->count; i++) {
        unity_component_rules r = {0};
        r.name = copy_n(components->keys[i], components->key_lens[i]);
        const char *why = NULL;
        if (!read_rules(&r, components->items[i], &why)) {
            fprintf(stderr, "%s: error: %s's rules aren't ones tide reads: %s\n", path, r.name, why);
            ok = false;
        }
        out->components = more(out->components, out->component_count, sizeof *out->components);
        out->components[out->component_count++] = r;
    }
    if (root) json_release();
    free(body);
    return ok;
}

bool unity_mappings_read(const unity_package *packages, const int count, unity_mappings *out)
{
    *out = (unity_mappings){0};
    bool ok = true;
    for (int i = 0; i < count; i++) {
        char *path = path_join(packages[i].folder, UNITY_MAPPING_FILE);
        if (sys_exists(path)) ok = read_file(out, path, packages[i].name) && ok;
        free(path);
    }
    return ok;
}

void unity_mappings_free(unity_mappings *m)
{
    for (int i = 0; i < m->component_count; i++) {
        unity_component_rules *r = &m->components[i];
        for (int k = 0; k < r->hidden_count; k++) free(r->hidden_fields[k]);
        free(r->hidden_fields);
        for (int k = 0; k < r->scaled_count; k++) free(r->scaled[k].field);
        free(r->scaled);
        free(r->name);
        free(r->shape);
        free(r->size);
        free(r->height);
        free(r->center);
    }
    free(m->components);
    for (int i = 0; i < m->glue_count; i++) {
        free(m->glue[i].package);
        free(m->glue[i].path);
        free(m->glue[i].text);
    }
    free(m->glue);
    *m = (unity_mappings){0};
}

const unity_component_rules *unity_rules_of(const unity_mappings *m, const char *name)
{
    for (int i = 0; m && name && i < m->component_count; i++) {
        if (strcmp(m->components[i].name, name) == 0) return &m->components[i];
    }
    return NULL;
}

bool unity_field_hidden(const unity_component_rules *r, const char *field)
{
    for (int i = 0; r && i < r->hidden_count; i++) {
        if (strcmp(r->hidden_fields[i], field) == 0) return true;
    }
    return false;
}

unity_scale unity_field_scale(const unity_component_rules *r, const char *field)
{
    for (int i = 0; r && i < r->scaled_count; i++) {
        if (strcmp(r->scaled[i].field, field) == 0) return r->scaled[i].scale;
    }
    return UNITY_SCALE_NONE;
}
