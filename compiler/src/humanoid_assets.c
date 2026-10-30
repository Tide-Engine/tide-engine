#include "humanoid_assets.h"
#include "assets.h"
#include "folders.h"
#include "tide/skinned_data.h"
#include "tide/sha256.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The shared JSON reader also supplies helpers other importers use.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "tide/internal/gltf_json.h"
#pragma clang diagnostic pop

// Profiles are small authoring documents, not model payloads.
#define HUMANOID_PROFILE_MAX_BYTES (1u << 20)
#define HUMANOID_PROFILE_MAX_VALUES 16384u

static const char *const roles[TIDE_HUMANOID_COUNT] = {
    "Hips", "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot",
    "Spine", "Chest", "Neck", "Head", "LeftShoulder", "RightShoulder", "LeftUpperArm", "RightUpperArm",
    "LeftLowerArm", "RightLowerArm", "LeftHand", "RightHand", "LeftToes", "RightToes", "LeftEye", "RightEye", "Jaw",
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal", "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal", "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
    "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal", "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
    "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal", "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
    "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal", "UpperChest"
};

static bool profile_error(const asset_match *item, loc at, const char *why)
{
    diag_error(at, "humanoid profile '%s': %s", item->key, why);
    return false;
}

static bool object_keys(const jnode *v, const char *const *allowed, uint32_t count, char why[256])
{
    if (!v || v->kind != J_OBJECT) { snprintf(why, 256, "expected an object"); return false; }
    for (uint32_t i = 0; i < v->count; i++) {
        if (jget(v, v->keys[i]) != v->items[i]) {
            snprintf(why, 256, "duplicate key '%s'", v->keys[i]); return false;
        }
        if (!allowed) continue;
        bool known = false;
        for (uint32_t k = 0; k < count; k++) known |= strcmp(v->keys[i], allowed[k]) == 0;
        if (!known) { snprintf(why, 256, "unknown key '%s'", v->keys[i]); return false; }
    }
    return true;
}

static bool number(const jnode *v, float *out)
{
    double d;
    if (!v || v->kind != J_NUMBER || !parse_double(v->text, v->text + v->length, &d)
        || !(d >= -FLT_MAX && d <= FLT_MAX)) return false;
    *out = (float)d;
    return true;
}

static bool vector(const jnode *v, tide_float3 *out)
{
    return v && v->kind == J_ARRAY && v->count == 3 && number(v->items[0], &out->x)
        && number(v->items[1], &out->y) && number(v->items[2], &out->z);
}

static int32_t node_named(const tide_skinned_data_view *view, const char *name)
{
    int32_t found = -1;
    for (uint32_t i = 0; i < view->nodes; i++) {
        tide_skinned_node_data node;
        if (!tide_skinned_data_node(view, i, &node)) return -1;
        if (node.name_length != strlen(name) || memcmp(node.name, name, node.name_length)) continue;
        if (found >= 0) return -1;
        found = (int32_t)i;
    }
    return found;
}

static void add_dependency(asset_match *item, const char *path)
{
    if (!path) return;
    for (int i = 0; i < item->side_count; i++) if (!strcmp(path, item->sides[i])) return;
    const char **next = arena_alloc(sizeof *next * (size_t)(item->side_count + 1));
    if (item->side_count) memcpy(next, item->sides, sizeof *next * (size_t)item->side_count);
    next[item->side_count++] = path;
    item->sides = next;
}

static void add_dependencies(asset_match *item, const asset_match *other)
{
    add_dependency(item, other->abs);
    for (int i = 0; i < other->side_count; i++) add_dependency(item, other->sides[i]);
}

static uint32_t profile_block(sb *packed, const void *data, uint32_t count, uint32_t size)
{
    const uint8_t zero[16] = {0};
    const size_t pad = (16u - (packed->len & 15u)) & 15u;
    sb_putn(packed, (const char *)zero, pad);
    const uint32_t at = (uint32_t)packed->len;
    const tide_block header = {.a = count, .b = count};
    sb_putn(packed, (const char *)&header, sizeof header);
    sb_putn(packed, data, (size_t)count * size);
    return at;
}

// A reference's "parts": the patterns its model's parts are chosen by, as [Parts]'s
typedef struct profile_parts {
    const jnode *patterns;
} profile_parts;

static bool profile_part_kept(void *user, const char *name, const uint32_t length)
{
    const profile_parts *parts = user;
    for (uint32_t i = 0; i < parts->patterns->count; i++) {
        const jnode *p = parts->patterns->items[i];
        if (asset_name_matches((str){p->text, (int)p->length}, name, length)) return true;
    }
    return false;
}

static bool read_profile(asset_match *item, const jnode *root, loc at, const char *game_folder)
{
    char why[256] = {0};
    const char *const top_keys[] = {"reference", "bones", "referenceEdits"};
    const char *const ref_keys[] = {"file", "pose", "clip", "seconds", "parts"};
    if (!object_keys(root, top_keys, 3, why)) return profile_error(item, at, why);
    const jnode *reference = jget(root, "reference");
    if (!object_keys(reference, ref_keys, 5, why)) return profile_error(item, at, "reference must specify file and pose: rest, or file, clip and seconds");
    // "parts": the model's parts as a SkinnedMesh asset's [Parts] keeps them, so the rig is that model's skeleton
    const jnode *part_list = jget(reference, "parts");
    bool parts_valid = !part_list || (part_list->kind == J_ARRAY && part_list->count);
    for (uint32_t i = 0; parts_valid && part_list && i < part_list->count; i++)
        parts_valid = part_list->items[i]->kind == J_STRING && strlen(part_list->items[i]->text) == part_list->items[i]->length;
    if (!parts_valid) return profile_error(item, at, "reference parts are a list of part names and * patterns, as [Parts] takes");
    profile_parts parts = {part_list};
    const jnode *file = jget(reference, "file"), *pose = jget(reference, "pose");
    const jnode *clip = jget(reference, "clip"), *seconds = jget(reference, "seconds");
    float time = 0;
    const bool rest = pose && pose->kind == J_STRING && !strcmp(pose->text, "rest") && !clip && !seconds;
    const bool sampled = !pose && clip && clip->kind == J_STRING && number(seconds, &time) && time >= 0;
    if (!file || file->kind != J_STRING || !file->length || strlen(file->text) != file->length || (!rest && !sampled))
        return profile_error(item, at, "reference requires a file and either \"pose\": \"rest\" or \"clip\": \"exact name\", \"seconds\": 0");
    // Resolve through the ordinary file reader, retaining all external buffers.
    // Escapes are re-encoded because asset_resolve takes a language literal.
    sb path = {0};
    if (asset_path_is_url(item->key, strlen(item->key)) && !asset_path_is_url(file->text, file->length)) {
        // Only the URL path contributes its parent. Signed/query-bearing URLs
        // may contain slashes after '?' or '#'; those are not path separators.
        const char *end = item->key + strcspn(item->key, "?#");
        const char *scheme = strstr(item->key, "://");
        if (!scheme || scheme + 3 >= end)
            return profile_error(item, at, "a URL profile needs an absolute reference file URL");
        const char *host_end = memchr(scheme + 3, '/', (size_t)(end - (scheme + 3)));
        if (!host_end) host_end = end;
        if (file->text[0] == '/') {
            sb_putn(&path, item->key, (size_t)(host_end - item->key));
        } else if (host_end == end) {
            sb_putn(&path, item->key, (size_t)(end - item->key));
            sb_put(&path, "/");
        } else {
            const char *parent = end;
            while (parent > host_end && parent[-1] != '/') parent--;
            sb_putn(&path, item->key, (size_t)(parent - item->key));
        }
    }
    for (uint32_t i = 0; i < file->length; i++) {
        const char ch = file->text[i] == '\\' ? '/' : file->text[i];
        if (ch == '"') sb_put(&path, "\\");
        sb_putn(&path, &ch, 1);
    }
    if (part_list) asset_choose_parts(profile_part_kept, &parts);
    asset_value *resolved = asset_resolve(item->abs, (str){path.data, (int)path.len}, ASSET_ANIMATIONS, false, false, at, game_folder);
    asset_choose_parts(NULL, NULL);
    if (!resolved || resolved->count != 1) return false;
    const asset_match *model = &resolved->items[0];
    add_dependencies(item, model);
    tide_skinned_data_view view;
    if (!tide_skinned_data_open(model->skinned, model->skinned_size, &view)) return profile_error(item, at, "reference model is invalid");
    const uint64_t mark = tide_scratch_mark();
    const tide_list data = tide_list_from(model->skinned, (int32_t)model->skinned_size, 1);
    tide_list transforms = {0}, bones = {0}, status = {0};
    bool ok = false;
    if (rest) ok = tide_skinned_rest(data, &transforms, 0);
    else {
        const tide_str name = {clip->text, (int32_t)clip->length, (int32_t)clip->length};
        const int32_t index = tide_skinned_find_animation(data, name);
        if (index >= 0 && time <= tide_skinned_animation_duration(data, index))
            ok = tide_skinned_sample(data, index, time, &transforms, 0);
    }
    if (!ok) { snprintf(why, sizeof why, "reference clip must have a unique exact name and seconds within its duration"); goto done; }
    const jnode *edits = jget(root, "referenceEdits");
    if (edits && !object_keys(edits, NULL, 0, why)) { ok = false; goto done; }
    for (uint32_t i = 0; edits && i < edits->count; i++) {
        const int32_t index = node_named(&view, edits->keys[i]);
        const char *const keys[] = {"positionOffset", "rotationOffsetDegrees", "scaleMultiplier"};
        const jnode *edit = edits->items[i];
        if (index < 0) { snprintf(why, sizeof why, "referenceEdits bone '%s' must name one exact bone", edits->keys[i]); ok = false; goto done; }
        if (!object_keys(edit, keys, 3, why)) { ok = false; goto done; }
        tide_float3 position = {0}, rotation = {0};
        float scale = 1;
        const jnode *p = jget(edit, keys[0]), *r = jget(edit, keys[1]), *s = jget(edit, keys[2]);
        if ((p && !vector(p, &position)) || (r && !vector(r, &rotation)) || (s && (!number(s, &scale) || !(scale > 0)))) {
            snprintf(why, sizeof why, "referenceEdits needs finite three-number offsets and a positive scaleMultiplier"); ok = false; goto done;
        }
        tide_bone_transform *t = tide_list_at_mut(transforms, index, sizeof *t);
        t->position = tide_add_f3(t->position, position);
        t->rotation = tide_normalize_q(tide_mul_q(t->rotation, tide_euler_q(tide_mul_f3(rotation, tide_f3_splat(0.017453292519943295f)))));
        t->scale = tide_mul_f3(t->scale, tide_f3_splat(scale));
    }
    if (!tide_humanoid_auto(data, transforms, &bones, &status, 0)) {
        snprintf(why, sizeof why, "reference transforms must be finite with positive scale");
        // Name the first bone that isn't
        for (int32_t i = 0; i < tide_list_count(transforms); i++) {
            const tide_bone_transform *t = tide_list_at(transforms, i, sizeof *t);
            const float x = t->scale.x, y = t->scale.y, z = t->scale.z;
            const float p[] = {t->position.x, t->position.y, t->position.z, t->rotation.value.x, t->rotation.value.y,
                               t->rotation.value.z, t->rotation.value.w, x, y, z};
            bool fine = x > 0.0f && y > 0.0f && z > 0.0f;
            for (int k = 0; k < 10; k++) fine = fine && isfinite(p[k]);
            tide_skinned_node_data node;
            if (fine || !tide_skinned_data_node(&view, (uint32_t)i, &node)) continue;
            snprintf(why, sizeof why, "reference transforms must be finite with positive scale; '%.*s' has position (%g, %g, %g) and scale (%g, %g, %g)",
                     (int)(node.name_length > 60u ? 60u : node.name_length), node.name ? node.name : "",
                     (double)p[0], (double)p[1], (double)p[2], (double)x, (double)y, (double)z);
            break;
        }
        ok = false; goto done;
    }
    const jnode *mapping = jget(root, "bones");
    if (mapping && !object_keys(mapping, roles, TIDE_HUMANOID_COUNT, why)) { ok = false; goto done; }
    for (uint32_t i = 0; mapping && i < mapping->count; i++) {
        int role = 0;
        while (strcmp(roles[role], mapping->keys[i])) role++;
        const jnode *name = mapping->items[i];
        const int32_t index = name->kind == J_STRING ? node_named(&view, name->text) : -1;
        if (name->kind != J_NULL && (name->kind != J_STRING || index < 0)) {
            snprintf(why, sizeof why, "bones.%s must name one exact reference bone, or null to unmap it", roles[role]); ok = false; goto done;
        }
        *(int32_t *)tide_list_at_mut(bones, role, sizeof(int32_t)) = index;
        *(int32_t *)tide_list_at_mut(status, role, sizeof(int32_t)) = TIDE_HUMANOID_MANUAL;
    }
    uint8_t *skeleton = NULL;
    uint32_t skeleton_size = 0;
    if (!tide_retarget_bake_skeleton(data, &skeleton, &skeleton_size, why)) { ok = false; goto done; }
    humanoid_asset *profile = NEW(humanoid_asset);
    sb packed = {0};
    sb_putn(&packed, "TIDEHUM1\0\0\0\0\0\0\0\0", 16);
    profile->data_at = profile_block(&packed, skeleton, skeleton_size, 1);
    free(skeleton);
    profile->pose_at = profile_block(&packed, tide_list_at(transforms, 0, sizeof(tide_bone_transform)), view.nodes, sizeof(tide_bone_transform));
    profile->bones_at = profile_block(&packed, tide_list_at(bones, 0, sizeof(int32_t)), TIDE_HUMANOID_COUNT, sizeof(int32_t));
    profile->status_at = profile_block(&packed, tide_list_at(status, 0, sizeof(int32_t)), TIDE_HUMANOID_COUNT, sizeof(int32_t));
    profile->data = (uint8_t *)packed.data + profile->data_at + sizeof(tide_block);
    profile->size = skeleton_size;
    profile->nodes = view.nodes;
    profile->pose = (const tide_bone_transform *)(packed.data + profile->pose_at + sizeof(tide_block));
    profile->bones = (const int32_t *)(packed.data + profile->bones_at + sizeof(tide_block));
    profile->status = (const int32_t *)(packed.data + profile->status_at + sizeof(tide_block));
    item->humanoid = profile;
    item->skinned = (const uint8_t *)packed.data;
    item->skinned_size = (uint32_t)packed.len;
done:
    tide_scratch_reset(mark);
    return ok || profile_error(item, at, why);
}

bool humanoid_asset_read(asset_match *item, loc at, const char *game_folder)
{
    if (item->size > HUMANOID_PROFILE_MAX_BYTES)
        return profile_error(item, at, "profile exceeds HUMANOID_PROFILE_MAX_BYTES (1048576)");
    // The shared model reader keeps object keys as C strings. Refuse embedded
    // NUL in every decoded string before those keys or exact bone names can be
    // truncated by a lookup. This lexical pass also sees keys, whose decoded
    // lengths the generic object tree does not retain.
    jparser strings = {.p = (const char *)item->data, .end = (const char *)item->data + item->size};
    bool strings_ok = true;
    while (strings.p < strings.end && !strings.bad) {
        if (*strings.p != '"') { strings.p++; continue; }
        uint32_t length = 0;
        const char *decoded = jstring(&strings, &length);
        if (decoded && strlen(decoded) != length) { strings_ok = false; break; }
    }
    jfree(&strings);
    if (!strings_ok) return profile_error(item, at, "profile keys and names cannot contain NUL characters");
    jparser parser = {.p = (const char *)item->data, .end = (const char *)item->data + item->size,
                      .values_left = HUMANOID_PROFILE_MAX_VALUES};
    const jnode *root = jvalue(&parser, 0);
    jspace(&parser);
    bool ok = root && !parser.bad && parser.p == parser.end;
    if (ok) ok = read_profile(item, root, at, game_folder);
    else profile_error(item, at, parser.too_many ? "profile exceeds HUMANOID_PROFILE_MAX_VALUES (16384)" : "invalid JSON");
    jfree(&parser);
    return ok;
}

// Cache only derived bytes. Canonical source imports remain borrowed and are
// never modified. A content key includes calibration, options and baker version;
// a separate output digest detects damaged files before canonical validation.
static const char *bake_cache_path(const asset_match *item, int32_t clip, const asset_match *source,
                                 const asset_match *target, const tide_retarget_bake_settings *settings,
                                 bool automatic_scale, const char *game_folder)
{
    if (!game_folder) return NULL;
    tide_sha256 hash;
    uint8_t key[32];
    tide_sha256_init(&hash);
    const uint32_t version[] = {1, TIDE_RETARGET_BAKE_VERSION, (uint32_t)clip, automatic_scale ? 1u : 0u};
    tide_sha256_add(&hash, version, sizeof version);
    tide_sha256_add(&hash, item->skinned, item->skinned_size);
    tide_sha256_add(&hash, source->skinned, source->skinned_size);
    tide_sha256_add(&hash, target->skinned, target->skinned_size);
    const float options[] = {settings->root_scale, settings->root_offset.x, settings->root_offset.y, settings->root_offset.z,
        settings->heading_degrees, settings->spine_distribution, settings->twist_distribution, settings->sample_rate};
    tide_sha256_add(&hash, options, sizeof options);
    tide_sha256_end(&hash, key);
    sb path = {0};
    sb_printf(&path, "%s/.tide/assets/retarget", game_folder);
    if (!folder_make(path.data)) return NULL;
    sb_put(&path, "/");
    for (int i = 0; i < 32; i++) sb_printf(&path, "%02x", key[i]);
    sb_put(&path, ".bin");
    return path.data;
}

static bool bake_cache_read(const char *path, asset_match *item)
{
    if (!path) return false;
    unsigned long long size, modified;
    if (!folder_file_info(path, &size, &modified) || size <= 32 || size - 32 > TIDE_RETARGET_BAKE_MAX_BYTES) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t expected[32], actual[32];
    const size_t bytes = (size_t)(size - 32);
    uint8_t *data = malloc(bytes);
    if (!data) abort();
    bool ok = fread(expected, 1, 32, file) == 32 && fread(data, 1, bytes, file) == bytes && fgetc(file) == EOF;
    fclose(file);
    if (ok) {
        tide_sha256_of(data, bytes, actual);
        ok = !memcmp(expected, actual, 32) && tide_skinned_data_validate(data, bytes);
    }
    if (ok) {
        uint8_t *owned = arena_alloc(bytes);
        memcpy(owned, data, bytes);
        item->skinned = owned;
        item->skinned_size = (uint32_t)bytes;
    }
    free(data);
    return ok;
}

static void bake_cache_write(const char *path, const asset_match *item)
{
    if (!path) return;
    sb fresh = {0};
    sb_printf(&fresh, "%s.new", path);
    FILE *file = fopen(fresh.data, "wb");
    if (!file) return;
    uint8_t hash[32];
    tide_sha256_of(item->skinned, item->skinned_size, hash);
    bool ok = fwrite(hash, 1, 32, file) == 32 && fwrite(item->skinned, 1, item->skinned_size, file) == item->skinned_size;
    ok = fclose(file) == 0 && ok;
    if (ok) { remove(path); ok = rename(fresh.data, path) == 0; }
    if (!ok) remove(fresh.data);
}

bool humanoid_asset_bake(asset_match *item, int32_t clip, const asset_match *source, const asset_match *target,
                          tide_retarget_bake_settings settings, bool automatic_scale, loc at, const char *game_folder)
{
    const humanoid_asset *s = source->humanoid, *t = target->humanoid;
    if (!s || !t) return false;
    add_dependencies(item, source);
    add_dependencies(item, target);
    const char *cache = bake_cache_path(item, clip, source, target, &settings, automatic_scale, game_folder);
    if (bake_cache_read(cache, item)) return true;
    const uint64_t mark = tide_scratch_mark();
    const tide_list actual = tide_list_from(item->skinned, (int32_t)item->skinned_size, 1);
    const tide_list sd = tide_list_from(s->data, (int32_t)s->size, 1), td = tide_list_from(t->data, (int32_t)t->size, 1);
    const tide_list sp = tide_list_from(s->pose, (int32_t)s->nodes, sizeof(tide_bone_transform));
    const tide_list tp = tide_list_from(t->pose, (int32_t)t->nodes, sizeof(tide_bone_transform));
    const tide_list sbones = tide_list_from(s->bones, TIDE_HUMANOID_COUNT, sizeof(int32_t));
    const tide_list sstatus = tide_list_from(s->status, TIDE_HUMANOID_COUNT, sizeof(int32_t));
    const tide_list tbones = tide_list_from(t->bones, TIDE_HUMANOID_COUNT, sizeof(int32_t));
    const tide_list tstatus = tide_list_from(t->status, TIDE_HUMANOID_COUNT, sizeof(int32_t));
    tide_list plan = {0};
    float scale = 1;
    const int32_t error = tide_retarget_create(actual, sd, sp, sbones, sstatus, td, tp, tbones, tstatus, &plan, &scale, 0);
    char why[256] = {0};
    bool ok = error == TIDE_RETARGET_OK;
    if (!ok) {
        static const char *const errors[] = {"", "InvalidRig", "MissingBone", "AmbiguousBone", "InvalidReferencePose", "IncompatibleSkeleton", "UnsupportedScale"};
        snprintf(why, sizeof why, "%s: check the source and target profiles' role mappings and reference poses", error >= 1 && error <= 6 ? errors[error] : "InvalidRig");
    } else {
        if (automatic_scale) settings.root_scale = scale;
        uint8_t *baked = NULL;
        uint32_t size = 0;
        ok = tide_retarget_bake(actual, clip, td, plan, &settings, &baked, &size, why);
        if (ok) {
            uint8_t *owned = arena_alloc(size);
            memcpy(owned, baked, size);
            free(baked);
            item->skinned = owned;
            item->skinned_size = size;
            bake_cache_write(cache, item);
        }
    }
    tide_scratch_reset(mark);
    if (!ok) diag_error(at, "can't retarget '%s': %s", item->key, why);
    return ok;
}
