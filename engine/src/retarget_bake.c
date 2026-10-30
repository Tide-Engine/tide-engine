#include "tide/retarget_bake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "tide/internal/count_text.h"

#pragma STDC FP_CONTRACT OFF

typedef struct bake_model {
    tide_list list;
    void *bytes;
    bool copied;
    tide_skinned_data_view view;
} bake_model;

typedef struct bake_output {
    tide_skinned_clip_data *clips;
    uint32_t clip_count;
    tide_skinned_channel_data *channels;
    uint32_t channel_count, channel_room;
    uint8_t *keys;
    uint32_t key_size, key_room;
} bake_output;

typedef struct bake_times {
    uint32_t *slots, count, room;
} bake_times;

static bool fail(char why[256], const char *text)
{
    if (why) snprintf(why, 256, "%s", text);
    return false;
}

static bool limit(char why[256], const char *what, uint64_t count, uint32_t most, const char *name)
{
    char actual[32], allowed[32];
    if (why) snprintf(why, 256, "retarget bake has %s %s, more than %s; reduce sampleRate or split the animation asset (%s)",
        tide_count_text(count, actual), what, tide_count_text(most, allowed), name);
    return false;
}

static void put_word(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static void put_float(uint8_t *p, float value) { put_word(p, tide_f_bits(value)); }

static void close_model(bake_model *m)
{
    if (m->bytes) tide_list_unflatten(m->list, m->bytes, 1u, false, m->copied);
}

// Never read a forged packed descriptor through the unchecked list header.
static bool open_model(tide_list data, bake_model *m, char why[256])
{
    *m = (bake_model){.list = data};
    int32_t size;
    if (tide_pack_holds(data.at)) {
        const uint32_t at = data.at & 0x3fffffffu;
        tide_block block;
        const void *header = tide_pack_span(at, sizeof block);
        if (!header) return fail(why, "retarget bake needs complete canonical skeleton bytes");
        memcpy(&block, header, sizeof block);
        if (block.a != block.b || block.a > TIDE_SKINNED_MAX_BYTES)
            return fail(why, "retarget bake received an invalid canonical byte-list descriptor");
        size = (int32_t)block.a;
        m->bytes = (void *)tide_pack_span(at + (uint32_t)sizeof block, block.a);
    } else {
        size = tide_list_count(data);
        if (size < (int32_t)TIDE_SKINNED_HEADER_SIZE || size > (int32_t)TIDE_SKINNED_MAX_BYTES)
            return fail(why, "retarget bake needs canonical skeleton bytes within TIDE_SKINNED_MAX_BYTES");
        m->bytes = tide_list_flatten(data, 1u, false, &m->copied);
    }
    tide_skinned_data_view *v = &m->view;
    if (!m->bytes || !tide_skinned_data_open(m->bytes, (size_t)size, v))
        return fail(why, "retarget bake received invalid or truncated canonical skeleton bytes");
    if (v->nodes > TIDE_SKINNED_MAX_NODES + 1u || v->clips > TIDE_SKINNED_MAX_CLIPS
        || v->channels > TIDE_SKINNED_MAX_CHANNELS || v->key_size / 4u > TIDE_SKINNED_MAX_KEY_NUMBERS)
        return fail(why, "retarget bake input exceeds canonical node, clip, channel or key limits in tide/skinned_data.h");
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_skinned_node_data n;
        if (!tide_skinned_data_node(v, i, &n)) return fail(why, "retarget bake received an invalid skeleton node");
        const float q = n.rotation[0] * n.rotation[0] + n.rotation[1] * n.rotation[1]
            + n.rotation[2] * n.rotation[2] + n.rotation[3] * n.rotation[3];
        if (!(q > 0) || !tide_is_finite_f(q)) return fail(why, "retarget bake requires finite, nonzero reference rotations");
    }
    return true;
}

static uint32_t time_slot(uint32_t bits, uint32_t room)
{
    bits ^= bits >> 16; bits *= 0x7feb352du; bits ^= bits >> 15;
    bits *= 0x846ca68bu; bits ^= bits >> 16;
    return bits & (room - 1u);
}

static bool add_time(bake_times *t, float time, char why[256])
{
    // Positive float bit order is numeric order. Collapse both signed zeros.
    const uint32_t bits = time == 0.0f ? 0u : tide_f_bits(time);
    if (t->room) {
        uint32_t at = time_slot(bits, t->room);
        while (t->slots[at]) {
            if (t->slots[at] == bits + 1u) return true;
            at = (at + 1u) & (t->room - 1u);
        }
    }
    if (t->count == TIDE_RETARGET_BAKE_MAX_SAMPLES)
        return limit(why, "distinct sample times", (uint64_t)t->count + 1u, TIDE_RETARGET_BAKE_MAX_SAMPLES, "TIDE_RETARGET_BAKE_MAX_SAMPLES");
    if (!t->room || t->count >= t->room / 2u) {
        const uint32_t room = t->room ? t->room * 2u : 128u;
        uint32_t *slots = tide_alloc_zeroed(room, sizeof *slots);
        for (uint32_t i = 0; i < t->room; i++) if (t->slots[i]) {
            uint32_t at = time_slot(t->slots[i] - 1u, room);
            while (slots[at]) at = (at + 1u) & (room - 1u);
            slots[at] = t->slots[i];
        }
        free(t->slots); t->slots = slots; t->room = room;
    }
    uint32_t at = time_slot(bits, t->room);
    while (t->slots[at]) at = (at + 1u) & (t->room - 1u);
    t->slots[at] = bits + 1u; t->count++;
    return true;
}

// A bounded heapsort: libc qsort need not guarantee nonquadratic work.
static void sift_times(uint32_t *a, uint32_t first, uint32_t count)
{
    while (first < count / 2u) {
        uint32_t child = first * 2u + 1u;
        if (child + 1u < count && a[child] < a[child + 1u]) child++;
        if (a[first] >= a[child]) return;
        const uint32_t value = a[first]; a[first] = a[child]; a[child] = value;
        first = child;
    }
}

static float *sorted_times(bake_times *t)
{
    uint32_t *bits = tide_alloc((size_t)t->count * sizeof *bits);
    uint32_t count = 0;
    for (uint32_t i = 0; i < t->room; i++) if (t->slots[i]) bits[count++] = t->slots[i] - 1u;
    for (uint32_t i = count / 2u; i; i--) sift_times(bits, i - 1u, count);
    for (uint32_t i = count; i > 1u; i--) {
        const uint32_t last = bits[i - 1u]; bits[i - 1u] = bits[0]; bits[0] = last;
        sift_times(bits, 0, i - 1u);
    }
    float *times = tide_alloc((size_t)count * sizeof *times);
    for (uint32_t i = 0; i < count; i++) memcpy(times + i, bits + i, sizeof *times);
    free(bits);
    return times;
}

static bool gather_times(const tide_skinned_data_view *v, const tide_skinned_clip_data *clip,
                         float rate, bake_times *times, uint64_t *reads, char why[256])
{
    const double steps = (double)clip->length * (double)rate;
    if (steps >= (double)TIDE_RETARGET_BAKE_MAX_SAMPLES)
        return limit(why, "regular samples", TIDE_RETARGET_BAKE_MAX_SAMPLES + 1u,
            TIDE_RETARGET_BAKE_MAX_SAMPLES, "TIDE_RETARGET_BAKE_MAX_SAMPLES");
    if (!add_time(times, 0, why) || !add_time(times, clip->length, why)) return false;
    for (uint32_t i = 1; (double)i < steps; i++) {
        const float time = (float)((double)i / (double)rate);
        if (time < clip->length && !add_time(times, time, why)) return false;
    }
    for (uint32_t i = 0; i < clip->channels; i++) {
        tide_skinned_channel_data channel;
        if (!tide_skinned_data_channel(v, clip->first_channel + i, &channel))
            return fail(why, "retarget bake received an invalid animation channel");
        const uint32_t width = channel.path == TIDE_SKINNED_ROTATION ? 4u : 3u;
        const uint32_t copies = channel.interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
        *reads += (uint64_t)channel.keys * (1u + width * copies);
        if (*reads > TIDE_SKINNED_MAX_KEY_NUMBERS)
            return limit(why, "input key numbers", *reads, TIDE_SKINNED_MAX_KEY_NUMBERS, "TIDE_SKINNED_MAX_KEY_NUMBERS");
        tide_skinned_node_data node;
        if (!tide_skinned_data_node(v, channel.node, &node)) return fail(why, "retarget bake received an invalid channel node");
        float previous = -1.0f;
        for (uint32_t k = 0; k < channel.keys; k++) {
            float time, value[4], in[4], out[4];
            if (!tide_skinned_data_key(v, &channel, k, &time, value, in, out) || time < 0 || !(time > previous))
                return fail(why, "retarget bake requires finite animation keys with strictly increasing nonnegative times");
            previous = time;
            if (channel.path == TIDE_SKINNED_SCALE) for (uint32_t axis = 0; axis < 3u; axis++) {
                if (tide_abs_f(value[axis] - node.scale[axis]) > tide_abs_f(node.scale[axis]) * 0.00001f
                    || in[axis] != 0 || out[axis] != 0) {
                    if (why) snprintf(why, 256, "clip '%.*s' animates scale of '%.*s'; retargeting requires static positive uniform scale",
                        (int)(clip->name_length < 64u ? clip->name_length : 64u), clip->name,
                        (int)(node.name_length < 64u ? node.name_length : 64u), node.name);
                    return false;
                }
            }
            if (time <= clip->length) {
                if (!add_time(times, time, why)) return false;
                if (k && time > 0 && channel.interpolation == TIDE_SKINNED_STEP) {
                    const uint32_t before_bits = tide_f_bits(time) - 1u;
                    float before; memcpy(&before, &before_bits, sizeof before);
                    if (!add_time(times, before, why)) return false;
                }
            }
        }
    }
    return true;
}

static bool key_room(bake_output *b, uint32_t numbers, uint32_t *at, char why[256])
{
    const uint64_t size = (uint64_t)b->key_size + (uint64_t)numbers * 4u;
    if (size / 4u > TIDE_SKINNED_MAX_KEY_NUMBERS)
        return limit(why, "output key numbers", size / 4u, TIDE_SKINNED_MAX_KEY_NUMBERS, "TIDE_SKINNED_MAX_KEY_NUMBERS");
    if (size > TIDE_RETARGET_BAKE_MAX_BYTES)
        return limit(why, "key bytes", size, TIDE_RETARGET_BAKE_MAX_BYTES, "TIDE_RETARGET_BAKE_MAX_BYTES");
    if (size > b->key_room) {
        uint32_t room = b->key_room ? b->key_room : 4096u;
        while (room < size) room *= 2u;
        b->keys = tide_realloc(b->keys, b->key_room, room); b->key_room = room;
    }
    *at = b->key_size; b->key_size = (uint32_t)size;
    return true;
}

static void components(const tide_bone_transform *t, uint32_t path, float values[4])
{
    if (path == TIDE_SKINNED_ROTATION) {
        values[0] = t->rotation.value.x; values[1] = t->rotation.value.y;
        values[2] = t->rotation.value.z; values[3] = t->rotation.value.w;
    } else {
        const tide_float3 v = path == TIDE_SKINNED_TRANSLATION ? t->position : t->scale;
        values[0] = v.x; values[1] = v.y; values[2] = v.z; values[3] = 0;
    }
}

static bool equal_components(const float *a, const float *b, uint32_t width)
{
    for (uint32_t i = 0; i < width; i++) if (a[i] != b[i]) return false;
    return true;
}

static bool append_channels(bake_output *b, const tide_skinned_data_view *target,
                            const tide_bone_transform *frames, const float *times, uint32_t count, char why[256])
{
    uint32_t many_times = UINT32_MAX, one_time = UINT32_MAX;
    for (uint32_t node = 0; node < target->nodes; node++) {
        tide_skinned_node_data rest;
        if (!tide_skinned_data_node(target, node, &rest)) return fail(why, "retarget bake target skeleton changed during baking");
        for (uint32_t path = 0; path < 3u; path++) {
            const uint32_t width = path == TIDE_SKINNED_ROTATION ? 4u : 3u;
            float first[4]; components(frames + node, path, first);
            bool constant = true;
            for (uint32_t frame = 1; constant && frame < count; frame++) {
                float value[4]; components(frames + (size_t)frame * target->nodes + node, path, value);
                constant = equal_components(first, value, width);
            }
            const float *defaults = path == TIDE_SKINNED_TRANSLATION ? rest.position
                : path == TIDE_SKINNED_ROTATION ? rest.rotation : rest.scale;
            if (constant && equal_components(first, defaults, width)) continue;
            if (b->channel_count == TIDE_SKINNED_MAX_CHANNELS)
                return limit(why, "output channels", (uint64_t)b->channel_count + 1u, TIDE_SKINNED_MAX_CHANNELS, "TIDE_SKINNED_MAX_CHANNELS");
            if (b->channel_count == b->channel_room) {
                const uint32_t room = b->channel_room ? b->channel_room * 2u : 128u;
                b->channels = tide_realloc(b->channels, (size_t)b->channel_room * sizeof *b->channels,
                    (size_t)room * sizeof *b->channels); b->channel_room = room;
            }
            const uint32_t keys = constant ? 1u : count;
            uint32_t *time_at = constant ? &one_time : &many_times;
            if (*time_at == UINT32_MAX) {
                if (!key_room(b, keys, time_at, why)) return false;
                for (uint32_t k = 0; k < keys; k++) put_float(b->keys + *time_at + k * 4u, times[k]);
            }
            uint32_t values_at;
            if ((uint64_t)keys * width > UINT32_MAX || !key_room(b, keys * width, &values_at, why)) return false;
            for (uint32_t k = 0; k < keys; k++) {
                float value[4]; components(frames + (size_t)k * target->nodes + node, path, value);
                for (uint32_t axis = 0; axis < width; axis++) put_float(b->keys + values_at + (k * width + axis) * 4u, value[axis]);
            }
            b->channels[b->channel_count++] = (tide_skinned_channel_data){node, path, TIDE_SKINNED_LINEAR, keys, *time_at, values_at};
        }
    }
    return true;
}

static bool encode(const tide_skinned_data_view *target, const bake_output *b,
                   uint8_t **out, uint32_t *out_size, char why[256])
{
    uint64_t strings = 0;
    for (uint32_t i = 0; i < target->nodes; i++) {
        tide_skinned_node_data node;
        if (!tide_skinned_data_node(target, i, &node)) return fail(why, "retarget bake received an invalid target skeleton");
        strings += (uint64_t)node.name_length + 1u;
    }
    for (uint32_t i = 0; i < b->clip_count; i++) strings += (uint64_t)b->clips[i].name_length + 1u;
    const uint64_t node_at = TIDE_SKINNED_HEADER_SIZE;
    const uint64_t clip_at = node_at + (uint64_t)target->nodes * TIDE_SKINNED_NODE_SIZE;
    const uint64_t channel_at = clip_at + (uint64_t)b->clip_count * TIDE_SKINNED_CLIP_SIZE;
    const uint64_t string_at = channel_at + (uint64_t)b->channel_count * TIDE_SKINNED_CHANNEL_SIZE;
    const uint64_t key_at = (string_at + strings + 3u) & ~(uint64_t)3u;
    const uint64_t size = key_at + b->key_size;
    if (size > TIDE_RETARGET_BAKE_MAX_BYTES)
        return limit(why, "encoded bytes", size, TIDE_RETARGET_BAKE_MAX_BYTES, "TIDE_RETARGET_BAKE_MAX_BYTES");
    uint8_t *bytes = tide_alloc_zeroed(1u, (size_t)size);
    put_word(bytes, TIDE_SKINNED_DATA_MAGIC); put_word(bytes + 4, TIDE_SKINNED_DATA_VERSION);
    put_word(bytes + 8, (uint32_t)size);
    put_word(bytes + 16, target->nodes); put_word(bytes + 20, (uint32_t)node_at);
    for (uint32_t at = 24u; at <= 64u; at += 8u) put_word(bytes + at + 4u, (uint32_t)size);
    put_word(bytes + 48, b->clip_count); put_word(bytes + 52, (uint32_t)clip_at);
    put_word(bytes + 56, b->channel_count); put_word(bytes + 60, (uint32_t)channel_at);
    put_word(bytes + 72, (uint32_t)string_at); put_word(bytes + 76, (uint32_t)strings);
    put_word(bytes + 80, (uint32_t)key_at); put_word(bytes + 84, b->key_size);
    memcpy(bytes + 88, target->skeleton_hash, 32u);
    uint32_t next_name = (uint32_t)string_at;
    for (uint32_t i = 0; i < target->nodes; i++) {
        tide_skinned_node_data node;
        if (!tide_skinned_data_node(target, i, &node)) { free(bytes); return fail(why, "retarget bake target skeleton changed during encoding"); }
        uint8_t *to = bytes + node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE;
        memcpy(to, target->data + target->node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE, TIDE_SKINNED_NODE_SIZE);
        put_word(to + 8, next_name);
        memcpy(bytes + next_name, node.name, (size_t)node.name_length + 1u); next_name += node.name_length + 1u;
    }
    for (uint32_t i = 0; i < b->clip_count; i++) {
        const tide_skinned_clip_data c = b->clips[i];
        uint8_t *to = bytes + clip_at + (size_t)i * TIDE_SKINNED_CLIP_SIZE;
        put_word(to, next_name); put_word(to + 4, c.name_length);
        memcpy(bytes + next_name, c.name, (size_t)c.name_length + 1u); next_name += c.name_length + 1u;
        put_word(to + 8, c.first_channel); put_word(to + 12, c.channels); put_float(to + 16, c.length);
    }
    for (uint32_t i = 0; i < b->channel_count; i++) {
        const tide_skinned_channel_data c = b->channels[i];
        uint8_t *to = bytes + channel_at + (size_t)i * TIDE_SKINNED_CHANNEL_SIZE;
        put_word(to, c.node); put_word(to + 4, c.path); put_word(to + 8, c.interpolation); put_word(to + 12, c.keys);
        put_word(to + 16, (uint32_t)key_at + c.times_at); put_word(to + 20, (uint32_t)key_at + c.values_at);
    }
    if (b->key_size) memcpy(bytes + key_at, b->keys, b->key_size);
    *out = bytes; *out_size = (uint32_t)size;
    return true;
}

bool tide_retarget_bake_skeleton(tide_list data, uint8_t **out, uint32_t *out_size, char why[256])
{
    if (why) why[0] = 0;
    if (!out || !out_size) return fail(why, "retarget bake needs both output pointers");
    bake_model model;
    bool valid = open_model(data, &model, why);
    if (valid) valid = encode(&model.view, &(bake_output){0}, out, out_size, why);
    close_model(&model);
    return valid;
}

bool tide_retarget_bake(tide_list source_data, int32_t clip_index, tide_list target_data, tide_list plan,
                        const tide_retarget_bake_settings *settings, uint8_t **out, uint32_t *out_size, char why[256])
{
    if (why) why[0] = 0;
    if (!out || !out_size || !settings) return fail(why, "retarget bake needs settings and both output pointers");
    const float rate = settings->sample_rate == 0 ? TIDE_RETARGET_BAKE_DEFAULT_RATE : settings->sample_rate;
    if (!(rate > 0) || !tide_is_finite_f(rate) || !tide_is_finite_f(settings->root_scale) || settings->root_scale < 0
        || !tide_is_finite_f(settings->root_offset.x) || !tide_is_finite_f(settings->root_offset.y)
        || !tide_is_finite_f(settings->root_offset.z) || !tide_is_finite_f(settings->heading_degrees)
        || !tide_is_finite_f(settings->spine_distribution) || !tide_is_finite_f(settings->twist_distribution))
        return fail(why, "retarget bake requires finite settings, nonnegative rootScale and positive sampleRate (or 0 for 60 Hz)");
    const uint32_t mark = tide_scratch_mark();
    bake_model source = {0}, target = {0};
    bake_output b = {0};
    bool valid = open_model(source_data, &source, why) && open_model(target_data, &target, why);
    if (valid && (clip_index < -1 || (clip_index >= 0 && (uint32_t)clip_index >= source.view.clips)))
        valid = fail(why, "retarget bake clip index is outside the source catalog");
    if (valid && !source.view.clips) valid = fail(why, "retarget bake source has no animation clips");
    if (valid) {
        b.clip_count = clip_index < 0 ? source.view.clips : 1u;
        b.clips = tide_alloc_zeroed(b.clip_count, sizeof *b.clips);
    }
    tide_list sampled = {0}, retargeted = {0};
    uint64_t total_samples = 0, total_transforms = 0, reads = 0, sample_reads = 0;
    for (uint32_t i = 0; valid && i < b.clip_count; i++) {
        const uint32_t index = clip_index < 0 ? i : (uint32_t)clip_index;
        tide_skinned_clip_data clip;
        bake_times set = {0};
        float *times = NULL;
        tide_bone_transform *frames = NULL;
        valid = tide_skinned_data_clip(&source.view, index, &clip);
        if (!valid) fail(why, "retarget bake received an invalid clip name, channel range or duration");
        const uint64_t reads_before = reads;
        if (valid) valid = gather_times(&source.view, &clip, rate, &set, &reads, why);
        if (valid) {
            total_samples += set.count;
            total_transforms += (uint64_t)set.count * ((uint64_t)source.view.nodes + target.view.nodes);
            sample_reads += (reads - reads_before) * set.count;
            if (total_samples > TIDE_RETARGET_BAKE_MAX_SAMPLES)
                valid = limit(why, "samples across clips", total_samples, TIDE_RETARGET_BAKE_MAX_SAMPLES, "TIDE_RETARGET_BAKE_MAX_SAMPLES");
            else if (total_transforms > TIDE_RETARGET_BAKE_MAX_TRANSFORMS)
                valid = limit(why, "evaluated node transforms", total_transforms, TIDE_RETARGET_BAKE_MAX_TRANSFORMS, "TIDE_RETARGET_BAKE_MAX_TRANSFORMS");
            else if (sample_reads > TIDE_RETARGET_BAKE_MAX_KEY_READS)
                valid = limit(why, "sampled input key numbers", sample_reads, TIDE_RETARGET_BAKE_MAX_KEY_READS, "TIDE_RETARGET_BAKE_MAX_KEY_READS");
        }
        if (valid) {
            times = sorted_times(&set);
            frames = tide_alloc((size_t)set.count * target.view.nodes * sizeof *frames);
            tide_list_resize(&sampled, (int32_t)source.view.nodes, sizeof(tide_bone_transform), TIDE_IN_SCRATCH);
            tide_list_resize(&retargeted, (int32_t)target.view.nodes, sizeof(tide_bone_transform), TIDE_IN_SCRATCH);
        }
        for (uint32_t frame = 0; valid && frame < set.count; frame++) {
            const uint32_t frame_mark = tide_scratch_mark();
            valid = tide_skinned_sample(source_data, (int32_t)index, times[frame], &sampled, TIDE_IN_SCRATCH)
                && tide_retarget_apply(plan, source_data, sampled, target_data,
                    settings->root_scale, settings->root_offset, settings->heading_degrees,
                    settings->spine_distribution, settings->twist_distribution, &retargeted, TIDE_IN_SCRATCH);
            if (!valid && why) snprintf(why, 256, "retarget bake failed for clip '%.*s' at %.9g s; verify the prepared map, reference skeletons and static uniform scale",
                (int)(clip.name_length < 64u ? clip.name_length : 64u), clip.name, (double)times[frame]);
            for (uint32_t node = 0; valid && node < target.view.nodes; node++) {
                const tide_bone_transform *value = tide_list_at(retargeted, (int32_t)node, sizeof *value);
                if (!value) { valid = fail(why, "retarget bake produced an incomplete target pose"); break; }
                tide_bone_transform *to = frames + (size_t)frame * target.view.nodes + node;
                *to = *value;
                if (frame && tide_dot_f4(to[-(ptrdiff_t)target.view.nodes].rotation.value, to->rotation.value) < 0)
                    to->rotation.value = tide_neg_f4(to->rotation.value);
            }
            tide_scratch_reset(frame_mark);
        }
        if (valid) {
            b.clips[i] = clip; b.clips[i].first_channel = b.channel_count;
            valid = append_channels(&b, &target.view, frames, times, set.count, why);
            b.clips[i].channels = b.channel_count - b.clips[i].first_channel;
        }
        free(set.slots); free(times); free(frames);
    }
    if (valid) valid = encode(&target.view, &b, out, out_size, why);
    free(b.clips); free(b.channels); free(b.keys);
    close_model(&source); close_model(&target);
    tide_scratch_reset(mark);
    return valid;
}
