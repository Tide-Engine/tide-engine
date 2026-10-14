#include "types.h"

#include <stdio.h>

#include "tide/devices.h" // tide_devices' size, which the input holds when match code reads devices

static const struct {
    const char *name;
    type_kind kind;
    const char *suffix;
    const char *c_name;
} builtins[] = {
    {"bool", TY_BOOL, "b", "bool"},
    {"byte", TY_BYTE, "i", "uint8_t"},
    {"char", TY_CHAR, "ch", "int32_t"},
    {"int", TY_INT, "i", "int32_t"},
    {"long", TY_LONG, "l", "int64_t"},
    {"int2", TY_INT2, "i2", "tide_int2"},
    {"int3", TY_INT3, "i3", "tide_int3"},
    {"int4", TY_INT4, "i4", "tide_int4"},
    {"float", TY_FLOAT, "f", "float"},
    {"float2", TY_FLOAT2, "f2", "tide_float2"},
    {"float3", TY_FLOAT3, "f3", "tide_float3"},
    {"float4", TY_FLOAT4, "f4", "tide_float4"},
    {"quaternion", TY_QUATERNION, "q", "tide_quaternion"},
    {"float2x2", TY_FLOAT2X2, "f2x2", "tide_float2x2"},
    {"float3x3", TY_FLOAT3X3, "f3x3", "tide_float3x3"},
    {"float4x4", TY_FLOAT4X4, "f4x4", "tide_float4x4"},
    {"Entity", TY_ENTITY, "e", "tide_entity"},
    {"LocalEntity", TY_LOCAL_ENTITY, "le", "tide_entity"},
    {"PlayerID", TY_PLAYER, "p", "tide_player_id"},
    {"Color", TY_COLOR, "c", "tide_color"},
    {"Rect", TY_RECT, "r", "tide_rect"},
    {"Texture", TY_TEXTURE, "texture", "tide_texture_ref"},
    {"DepthTexture", TY_DEPTH_TEXTURE, "depth", "tide_depth_ref"},
    {"Cubemap", TY_CUBEMAP, "cubemap", "tide_cubemap_ref"},
    {"RenderTarget", TY_RENDER_TARGET, "target", "tide_render_target"},
    {"Clip", TY_CLIP, "clip", "tide_clip_ref"},
    {"Resident", TY_RESIDENT, "resident", "tide_resident"},
};

#define BUILTIN_COUNT (sizeof builtins / sizeof builtins[0])

static const generic_form generic_forms[] = {
    {"Asset", 1, {GENERIC_VALUE}, {"the referenced mesh type, like 'Asset<Mesh<Vertex3>>'"}, NULL, "'>' after the referenced mesh type"},
    {"List", 1, {GENERIC_ELEMENT}, {"the list's element type, like 'List<int>'"}, NULL, "'>' after the list's element type"},
    {"Map", 2, {GENERIC_KEY, GENERIC_ELEMENT},
     {"the map's key type, like 'Map<int, string>'", "the map's value type, like 'Map<int, string>'"},
     "',' between the map's key and value types, like 'Map<int, string>'", "'>' after the map's value type"},
    {"Set", 1, {GENERIC_KEY}, {"the set's key type, like 'Set<int>'"}, NULL, "'>' after the set's key type"},
    {"Grid2", 1, {GENERIC_CELL}, {"the grid's cell type, like 'Grid2<int>'"}, NULL, "'>' after the grid's cell type"},
    {"Grid3", 1, {GENERIC_CELL}, {"the grid's cell type, like 'Grid2<int>'"}, NULL, "'>' after the grid's cell type"},
    {"Mesh", 1, {GENERIC_VERTEX}, {"the mesh's vertex type, like 'Mesh<Vertex3>'"}, NULL, "'>' after the mesh's vertex type"},
    {"SkinnedMesh", 2, {GENERIC_VERTEX, GENERIC_ELEMENT},
     {"the mesh's vertex type, like 'SkinnedMesh<Vertex3>'", "an optional animation and bone contract struct"},
     "',' before the contract type", "'>' after the skinned mesh's types"},
};

const generic_form generic_optional = {"?", 1, {GENERIC_VALUE}, {"the type a T? holds"}, NULL, NULL};
const generic_form generic_fails = {"fails", 1, {GENERIC_ERROR}, {"the error it fails with, like 'fails ParseError'"}, NULL, NULL};

const generic_form *generic_form_named(const str name)
{
    for (size_t i = 0; i < sizeof generic_forms / sizeof generic_forms[0]; i++) {
        if (str_eq_c(name, generic_forms[i].name)) return &generic_forms[i];
    }
    return NULL;
}

bool builtin_type_named(const str name, type *out)
{
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (str_eq_c(name, builtins[i].name)) {
            *out = (type){builtins[i].kind, NULL};
            return true;
        }
    }
    return false;
}

void suggest_builtin_types(suggestion *s)
{
    for (size_t i = 0; i < BUILTIN_COUNT; i++) suggest_consider_c(s, builtins[i].name);
}

int type_dim(const type t)
{
    switch (t.kind) {
    case TY_BYTE:
    case TY_INT:
    case TY_FLOAT: return 1;
    case TY_INT2:
    case TY_FLOAT2: return 2;
    case TY_INT3:
    case TY_FLOAT3: return 3;
    case TY_INT4:
    case TY_FLOAT4: return 4;
    default: return 0;
    }
}

bool type_is_float_based(const type t)
{
    return t.kind == TY_FLOAT || t.kind == TY_FLOAT2 || t.kind == TY_FLOAT3 || t.kind == TY_FLOAT4;
}

bool type_is_int_based(const type t)
{
    return t.kind == TY_BYTE || t.kind == TY_INT || t.kind == TY_INT2 || t.kind == TY_INT3 || t.kind == TY_INT4;
}

bool type_is_numeric(const type t)
{
    return type_dim(t) > 0;
}

bool type_is_integer(const type t)
{
    return t.kind == TY_BYTE || t.kind == TY_INT || t.kind == TY_LONG;
}

int matrix_dim(const type t)
{
    switch (t.kind) {
    case TY_FLOAT2X2: return 2;
    case TY_FLOAT3X3: return 3;
    case TY_FLOAT4X4: return 4;
    default: return 0;
    }
}

// A size that's more than an int says is LAYOUT_MOST, never what the int
// would wrap to: structs of arrays of structs of arrays say gigabytes in a few
// lines, and what reads a size to refuse one that's too big has to find it so.
static int saturated(const int64_t size)
{
    return size > LAYOUT_MOST ? LAYOUT_MOST : (int)size;
}

layout type_layout(const type t)
{
    if (t.kind == TY_BOOL || t.kind == TY_BYTE) return (layout){1, 1};
    if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_EVENT) return decl_layout(t.decl);
    if (t.kind == TY_RECORD) return (layout){(int)sizeof(tide_devices), 4}; // The input's devices
    if (t.kind == TY_LONG) return (layout){8, 8};
    if (t.kind == TY_RENDER_TARGET) return (layout){24, 8}; // tide_render_target: its id, then its size, four floats
    if (t.kind == TY_TEXTURE) return (layout){48, 8};       // tide_texture_ref: an image, then a target
    if (t.kind == TY_DEPTH_TEXTURE) return (layout){24, 8}; // tide_depth_ref: a target
    if (t.kind == TY_CUBEMAP) return (layout){32, 8};       // tide_cubemap_ref: a target, then a name
    if (t.kind == TY_CLIP) return (layout){8, 8};           // tide_clip_ref: a name
    if (t.kind == TY_RESIDENT) return (layout){32, 8};      // tide_resident: its pages, then what the hash covers
    if (t.kind == TY_MAP || t.kind == TY_SET) return (layout){24, 8}; // tide_map: two lists, count, padding and insertion order
    if (t.kind == TY_MESH) return (layout){8, 4};                     // tide_mesh_lists: two lists
    if (t.kind == TY_ARRAY) {
        const layout e = type_layout(t.decl->fields.items[0].type);
        return (layout){saturated((int64_t)e.size * t.decl->array_length), e.align};
    }
    if (type_dim(t) > 0) return (layout){4 * type_dim(t), 4};
    if (matrix_dim(t) > 0) return (layout){4 * matrix_dim(t) * matrix_dim(t), 4};
    switch (t.kind) {
    case TY_QUATERNION: case TY_COLOR: case TY_RECT: return (layout){16, 4};
    case TY_ENTITY: case TY_LOCAL_ENTITY: return (layout){8, 4};
    case TY_PLAYER: return (layout){4, 4};
    case TY_ENUM: { // A byte or a ushort, as `enum Voxel : byte` says, or an int
        const int width = t.decl->width == 1 || t.decl->width == 2 ? t.decl->width : 4;
        return (layout){width, width};
    }
    default: return (layout){4, 4};
    }
}

static int64_t round_up(const int64_t n, const int align)
{
    return (n + align - 1) / align * align;
}

layout decl_layout_padded(const decl *d, int *pad)
{
    int64_t size = 0;
    int align = 1;
    for (int i = 0; i < d->fields.count; i++) {
        const layout f = type_layout(d->fields.items[i].type);
        const int before = (int)(round_up(size, f.align) - size);
        if (pad) pad[i] = before;
        size += before + f.size;
        if (f.align > align) align = f.align;
    }
    const int after = (int)(round_up(size, align) - size);
    if (pad) pad[d->fields.count] = after;
    size += after;
    if (d->fields.count == 0) size = 1; // tide_empty
    return (layout){saturated(size), align};
}

layout decl_layout(const decl *d)
{
    return decl_layout_padded(d, NULL);
}

bool type_holds_long_array(const type t)
{
    if (t.kind == TY_ARRAY) {
        int64_t elements = 1;
        type element = t;
        for (; element.kind == TY_ARRAY; element = element.decl->fields.items[0].type) elements *= element.decl->array_length;
        return elements > TIDE_ARRAY_MOST || type_holds_long_array(element);
    }
    if (t.kind != TY_STRUCT) return false;
    if (t.decl->long_arrays == 0) {
        t.decl->long_arrays = 1;
        for (int i = 0; i < t.decl->fields.count && t.decl->long_arrays == 1; i++) {
            if (type_holds_long_array(t.decl->fields.items[i].type)) t.decl->long_arrays = 2;
        }
    }
    return t.decl->long_arrays == 2;
}

bool type_blends(const type t)
{
    if (t.kind == TY_FLOAT || t.kind == TY_QUATERNION || t.kind == TY_COLOR || t.kind == TY_RECT) return true;
    if (type_dim(t) >= 2) return type_is_float_based(t);
    if (matrix_dim(t) > 0) return true;
    if (t.kind != TY_STRUCT) return false;
    if (t.decl->optional_of) return false; // A T? field: a value or nothing, which nothing is between
    if (t.decl->interpolate) return true; // Its own way
    for (int i = 0; i < t.decl->fields.count; i++) {
        const field *f = &t.decl->fields.items[i];
        bool snaps = false;
        for (int k = 0; k < f->attributes.count; k++) snaps |= str_eq_c(f->attributes.items[k].name, "Snap");
        if (!snaps && type_blends(f->type)) return true;
    }
    return false;
}

type vector_type(const bool is_float, const int dim)
{
    static const type_kind floats[] = {TY_ERROR, TY_FLOAT, TY_FLOAT2, TY_FLOAT3, TY_FLOAT4};
    static const type_kind ints[] = {TY_ERROR, TY_INT, TY_INT2, TY_INT3, TY_INT4};
    if (dim < 1 || dim > 4) return (type){TY_ERROR, NULL};
    return (type){is_float ? floats[dim] : ints[dim], NULL};
}

type matrix_type(const int dim)
{
    static const type_kind kinds[] = {TY_ERROR, TY_ERROR, TY_FLOAT2X2, TY_FLOAT3X3, TY_FLOAT4X4};
    if (dim < 2 || dim > 4) return (type){TY_ERROR, NULL};
    return (type){kinds[dim], NULL};
}

bool type_is_texture_grid(const type t)
{
    if (t.kind != TY_GRID || !t.decl || t.decl->dims != 2 || t.decl->fields.count < 1) return false;
    const type_kind cell = t.decl->fields.items[0].type.kind;
    return cell == TY_COLOR || cell == TY_FLOAT;
}

static bool skin_catalog_guarantees(const decl *wanted, const decl *known)
{
    if (known->skin_metadata_status != 1) return false;
    for (int i = 0; i < wanted->skin_catalog.count; i++) {
        const skinned_member *member = &wanted->skin_catalog.items[i];
        if (wanted->skin_kind == SKIN_ANIMATIONS && member->index >= 0) {
            // Inferred clip members are bound to source ordinals, including
            // duplicate names. A conversion must preserve those ordinals.
            if (member->index >= known->skin_sources.count
                || !str_eq(member->source_name, known->skin_sources.items[member->index])) return false;
            continue;
        }
        int matches = 0;
        for (int j = 0; j < known->skin_sources.count; j++) {
            const str source = known->skin_sources.items[j];
            bool equal = source.len == member->source_name.len;
            for (int k = 0; equal && k < source.len; k++) {
                char ch = source.ptr[k];
                if (!member->explicit_name && k == 0 && ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
                equal = ch == member->source_name.ptr[k];
            }
            matches += equal;
        }
        if (matches != 1) return false;
    }
    return true;
}

bool type_assignable(const type to, const type from)
{
    if (to.kind == TY_ERROR || from.kind == TY_ERROR) return true;
    if (to.kind == from.kind && to.decl == from.decl) return true;
    if (to.kind == TY_COMPONENT && from.kind == TY_COMPONENT
        && (to.decl->placement_of ? to.decl->placement_of : to.decl)
            == (from.decl->placement_of ? from.decl->placement_of : from.decl)) return true;
    if (to.kind == TY_STRUCT && from.kind == TY_STRUCT && to.decl->skin_kind == SKIN_ANIMATIONS
        && from.decl->skin_kind == SKIN_ANIMATIONS) {
        // An annotation erases static names; dynamic values cannot gain an
        // inferred catalog's compile-time guarantees.
        return (!to.decl->skin_metadata_status && !to.decl->skin_inferred && !to.decl->skin_catalog.count)
            || skin_catalog_guarantees(to.decl, from.decl);
    }
    if (to.kind == TY_STRUCT && from.kind == TY_STRUCT && to.decl->skin_kind == SKIN_MESH
        && from.decl->skin_kind == SKIN_MESH) {
        if (to.decl->skin_vertex.decl != from.decl->skin_vertex.decl) return false;
        if (to.decl->skin_inferred)
            return skin_catalog_guarantees(to.decl->skin_animations, from.decl->skin_animations)
                && skin_catalog_guarantees(to.decl->skin_skeleton->skin_bones, from.decl->skin_skeleton->skin_bones);
        if (!to.decl->skin_contract || to.decl->skin_contract == from.decl->skin_contract) return true;
        return from.decl->skin_inferred
            && skin_catalog_guarantees(to.decl->skin_animations, from.decl->skin_animations)
            && skin_catalog_guarantees(to.decl->skin_skeleton->skin_bones, from.decl->skin_skeleton->skin_bones);
    }
    // A field declared T?, kept as a struct of the value and whether there is one: what a T? takes
    if (to.kind == TY_STRUCT && to.decl->optional_of) return type_assignable((type){TY_OPTIONAL, to.decl->optional_of}, from);
    // A value goes where a T? goes: int? best = 5.
    if (to.kind == TY_OPTIONAL && from.kind != TY_OPTIONAL && from.kind != TY_FAILABLE) {
        return type_assignable(to.decl->fields.items[0].type, from);
    }
    if (from.kind == TY_BYTE && (to.kind == TY_INT || to.kind == TY_LONG || to.kind == TY_FLOAT)) return true;
    // A char goes where an int does, as its code point; an int becomes a char only with char(n)
    if (from.kind == TY_CHAR && (to.kind == TY_INT || to.kind == TY_LONG || to.kind == TY_FLOAT)) return true;
    if (to.kind == TY_TEXTURE) return type_is_texture_grid(from); // A grid of colors or of floats is a texture
    if (to.kind == TY_LONG) return from.kind == TY_INT; // An int fits a long; floats convert explicitly, both ways
    return type_is_float_based(to) && type_is_int_based(from) && type_dim(to) == type_dim(from);
}

const char *type_suffix(const type t)
{
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (builtins[i].kind == t.kind) return builtins[i].suffix;
    }
    return "?";
}

const char *type_name(const type t)
{
    static char buf[4][128];
    static int next;
    switch (t.kind) {
    case TY_ERROR: return "<error>";
    case TY_VOID: return "nothing";
    case TY_STRING: return "string";
    case TY_ACTION: return "Action";
    case TY_DRAW_LIST: return "DrawList";
    case TY_WORLD: return "World";
    case TY_QUERY: return "entity query";
    case TY_COMPONENT:
    case TY_SINGLETON:
    case TY_INPUT:
    case TY_RECORD:
    case TY_STRUCT:
    case TY_EVENT:
    case TY_ENUM:
    case TY_LIST:
    case TY_MAP:
    case TY_SET:
    case TY_GRID:
    case TY_ARRAY:
    case TY_MESH:
    case TY_OPTIONAL:
    case TY_FAILABLE: {
        char *b = buf[next++ % 4];
        const str name = t.decl->qualified.len > 0 ? t.decl->qualified : t.decl->name;
        snprintf(b, sizeof buf[0], STR_FMT, STR_ARG(name));
        return b;
    }
    default:
        for (size_t i = 0; i < BUILTIN_COUNT; i++) {
            if (builtins[i].kind == t.kind) return builtins[i].name;
        }
        return "?";
    }
}

const char *type_c_name(const type t)
{
    if (t.kind == TY_STRING) return "tide_str";
    if (t.kind == TY_LIST) return "tide_list";
    if (t.kind == TY_MAP || t.kind == TY_SET) return "tide_map";
    if (t.kind == TY_GRID) return "tide_grid";
    if (t.kind == TY_MESH) return "tide_mesh_lists";
    if (t.kind == TY_DRAW_LIST) return "tide_draw_list *";
    for (size_t i = 0; i < BUILTIN_COUNT; i++) {
        if (builtins[i].kind == t.kind) return builtins[i].c_name;
    }
    return "void";
}
