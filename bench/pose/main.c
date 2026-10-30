// Sampling a separate animation export into a model's pose: the cost of a call
// to tide_skinned_sample_into (Pose.Sample), tide_skinned_sample
// (Animation.Sample) and the game's Sampled() in a view, best of several
// batches; and a view indexing a pose and asking its Model. Each run first
// checks that kept sample plans (the pack's bytes) give the same bits as
// plans made for one call (owned copies of the bytes).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/draw.h"
#include "tide/gui.h"
#include "tide/pack.h"
#include "tide/skinned.h"
#include "tide/time.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_list owned_copy(const tide_list l)
{
    return tide_list_from(tide_list_at(l, 0, 1u), tide_list_count(l), 1u);
}

static bool same_pose(const tide_list a, const tide_list b)
{
    const int32_t count = tide_list_count(a);
    if (count != tide_list_count(b) || count <= 0) return false;
    for (int32_t i = 0; i < count; i++) {
        if (memcmp(tide_list_at(a, i, sizeof(tide_bone_transform)), tide_list_at(b, i, sizeof(tide_bone_transform)),
                   sizeof(tide_bone_transform)) != 0) return false;
    }
    return true;
}

// Kept and single-call plans of the separate binding, the clip on its own
// skeleton and the model's own clip, from before the clip's start to past
// its end.
static bool check(const tide_list model, const tide_list clip)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list owned_model = owned_copy(model), owned_clip = owned_copy(clip);
    bool same = true;
    for (int step = -60; same && step <= 300; step++) {
        const float seconds = (float)step / 240.0f;
        const uint32_t at = tide_scratch_mark();
        tide_list kept = {0}, alone = {0};
        same = tide_skinned_sample_into(model, clip, 0, seconds, &kept, TIDE_IN_SCRATCH)
            && tide_skinned_sample_into(owned_model, owned_clip, 0, seconds, &alone, TIDE_IN_SCRATCH) && same_pose(kept, alone);
        kept = alone = (tide_list){0};
        same = same && tide_skinned_sample(clip, 0, seconds, &kept, TIDE_IN_SCRATCH)
            && tide_skinned_sample(owned_clip, 0, seconds, &alone, TIDE_IN_SCRATCH) && same_pose(kept, alone);
        kept = alone = (tide_list){0};
        same = same && tide_skinned_sample_into(model, model, 0, seconds, &kept, TIDE_IN_SCRATCH)
            && tide_skinned_sample_into(owned_model, owned_model, 0, seconds, &alone, TIDE_IN_SCRATCH) && same_pose(kept, alone);
        tide_scratch_reset(at);
    }
    tide_scratch_reset(mark);
    return same;
}

enum { CALLS = 1000 };

// Microseconds a call, best of `batches` batches of CALLS calls.
static double best_call(const int kind, const tide_list model, const tide_list clip, const int batches)
{
    double best = 1e30;
    for (int batch = 0; batch < batches; batch++) {
        const uint64_t began = tide_time_now_ns();
        for (int i = 0; i < CALLS; i++) {
            const uint32_t mark = tide_scratch_mark();
            const float seconds = (float)(batch * CALLS + i) * 0.00073f;
            tide_list pose = {0};
            if (kind == 0) {
                tide_skinned_rest(model, &pose, TIDE_IN_SCRATCH);
                tide_skinned_sample_into(model, clip, 0, seconds, &pose, TIDE_IN_SCRATCH);
            } else tide_skinned_sample(clip, 0, seconds, &pose, TIDE_IN_SCRATCH);
            tide_scratch_reset(mark);
        }
        const double us = (double)(tide_time_now_ns() - began) / 1e3 / CALLS;
        if (us < best) best = us;
    }
    return best;
}

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// The view's 1000 calls of a mode, best of `frames` frames.
static double best_view(const int mode, const int frames)
{
    double best = 1e30;
    for (int f = 0; f < frames; f++) {
        local.Bench.calls = CALLS;
        local.Bench.mode = mode;
        local.Bench.seconds = (float)f / 60.0f;
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(640, 360), NULL);
        const uint64_t began = tide_time_now_ns();
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        const double us = (double)(tide_time_now_ns() - began) / 1e3 / CALLS;
        tide_gui_end(&gui, &draw);
        if (us < best) best = us;
    }
    return best;
}

int main(const int argc, char **argv)
{
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true;
        else {
            puts("usage: bench_pose [--quick]\n"
                 "Times sampling a separate 64-bone, 192-channel 60 Hz clip into a model's pose, per call.\n"
                 "--quick runs the bit checks and one short batch of each.");
            return strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    const tide_list model = tide_pack_skinned_of(tide_pack_find("character", TIDE_PACK_SKINNED), sizeof(Corner)).data;
    const tide_list clip = {tide_pack_handle(tide_pack_find("idle", TIDE_PACK_BYTES))};
    if (tide_skinned_bone_count(model) != 66 || tide_skinned_animation_count(clip) != 1) {
        fprintf(stderr, "pose: the model or clip isn't the fixture's (%d bones, %d clips)\n", tide_skinned_bone_count(model),
                tide_skinned_animation_count(clip));
        return 1;
    }
    if (!check(model, clip)) {
        fprintf(stderr, "pose: a kept sample plan gave other bits than one made for its call\n");
        return 1;
    }
    puts("pose: kept and single-call plans sample the same bits");
    const int batches = quick ? 1 : 20, frames = quick ? 2 : 30;
    const uint32_t mark = tide_scratch_mark();
    const tide_list owned_model = owned_copy(model), owned_clip = owned_copy(clip);
    printf("Pose.Sample (rest + separate clip): %.3f us a call; owned bytes, planned each call: %.3f us\n",
           best_call(0, model, clip, batches), best_call(0, owned_model, owned_clip, batches));
    printf("Animation.Sample (its own skeleton): %.3f us a call; owned bytes: %.3f us\n",
           best_call(1, model, clip, batches), best_call(1, owned_model, owned_clip, batches));
    tide_scratch_reset(mark);
    printf("Sampled() in a view, 1000 a frame: %.3f us a call\n", best_view(0, frames));
    printf("copy[bone] = rest[bone] for 40 bones by index: %.3f us for the 40\n", best_view(1, frames));
    printf("rest.Model(bones.middle), 33 deep, its Bone by name: %.3f us a call\n", best_view(2, frames));
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return 0;
}
