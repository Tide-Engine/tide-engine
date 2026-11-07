#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "perf.h"
#include "tide/draw.h"
#include "tide/gui.h"
#include "tide/pack.h"
#include "tide/retarget.h"
#include "tide/skinned_data.h"
#include "tide/time.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static volatile float consumed;
static int failed;
enum { ROUNDS = 9, CALLS = 3000, KINDS = 6 };
static tide_list target_data, raw_data, baked_data, legacy_data, ref_data;
static tide_list source_ref, target_ref, source_bones, source_status, target_bones, target_status, plan, source_pose;
static float root_scale;

static tide_list animation(const char *name)
{
    return (tide_list){tide_pack_handle(tide_pack_find(name, TIDE_PACK_BYTES))};
}
static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static void report(const char *name, const int calls, double samples[ROUNDS])
{
    printf("RAW %s %d", name, calls);
    for (int i = 0; i < ROUNDS; i++) printf(" %.6f", samples[i]);
    putchar('\n');
    qsort(samples, ROUNDS, sizeof *samples, compare_double);
    printf("RESULT %s %d median_us=%.6f min_us=%.6f max_us=%.6f\n", name, calls, samples[ROUNDS / 2], samples[0], samples[ROUNDS - 1]);
}
static bool apply(const tide_list source, tide_list *output)
{
    return tide_retarget_apply(plan, raw_data, source, target_data, root_scale, tide_f3(0,0,0), 0,1,1, output, TIDE_IN_SCRATCH);
}
static double batch(const int kind, const int round, const int calls)
{
    const uint64_t start = tide_time_now_ns();
    for (int i = 0; i < calls; i++) {
        const uint64_t mark = tide_scratch_mark();
        const float seconds = (float)((round * calls + i) % 2000) * 0.00073f;
        tide_list output = {0}, source = {0};
        bool okay;
        if (kind == 0) okay = tide_skinned_rest(target_data, &output, TIDE_IN_SCRATCH)
            && tide_skinned_sample_into(target_data, baked_data, 0, seconds, &output, TIDE_IN_SCRATCH);
        else if (kind == 1) okay = tide_skinned_sample(raw_data, 0, seconds, &output, TIDE_IN_SCRATCH);
        else if (kind == 2) okay = apply(source_pose, &output);
        else if (kind == 3) okay = tide_skinned_rest(target_data, &output, TIDE_IN_SCRATCH)
            && tide_skinned_sample(raw_data, 0, seconds, &source, TIDE_IN_SCRATCH) && apply(source, &output);
        else if (kind == 5) okay = tide_skinned_rest(target_data, &output, TIDE_IN_SCRATCH) && tide_skinned_sample_into(target_data, legacy_data, 0, seconds, &output, TIDE_IN_SCRATCH);
        else {
            tide_list sb = {0}, ss = {0}, tb = {0}, ts = {0};
            float scale;
            okay = tide_humanoid_auto(ref_data, source_ref, &sb, &ss, TIDE_IN_SCRATCH)
                && tide_humanoid_auto(target_data, target_ref, &tb, &ts, TIDE_IN_SCRATCH)
                && !tide_retarget_create(raw_data, ref_data, source_ref, sb, ss, target_data, target_ref, tb, ts, &output, &scale, TIDE_IN_SCRATCH);
        }
        if (!okay) failed++;
        if (kind != 4 && okay) consumed = ((const tide_bone_transform *)tide_list_at(output, 4, sizeof(tide_bone_transform)))->position.y;
        tide_scratch_reset(mark);
    }
    return (double)(tide_time_now_ns() - start) / 1000.0 / calls;
}
static double frame(const int mode, const int calls, const float seconds)
{
    local.Bench.calls = calls;
    local.Bench.mode = mode;
    local.Bench.seconds = seconds;
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640,360), NULL);
    const uint64_t began = tide_time_now_ns();
    tide_frame(&world, NULL,1.0f,NULL,NULL,1.0f,&local,&draw,&gui);
    const double us = (double)(tide_time_now_ns() - began) / 1000.0;
    tide_gui_end(&gui,&draw);
    consumed = local.Bench.sum;
    return us / calls;
}
int main(const int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--frames") || atoi(argv[2]) != 90) {
        fputs("usage: retarget_perf --frames 90 (90 timed frames per view case)\n", stderr);
        return 2;
    }
    if (!tide_assets_load(tide_game_pack,tide_game_pack_size)) return 2;
    tide_world_init(&world,1.0f/60.0f);
    tide_local_init(&local);
    tide_text_use(&world.heap,&local.heap);
    target_data = tide_pack_skinned_of(tide_pack_find("target",TIDE_PACK_SKINNED),sizeof(Corner)).data;
    raw_data = animation("raw"); baked_data = animation("baked"); legacy_data = animation("legacyBaked"); ref_data = animation("reference");
    if (!tide_skinned_sample(ref_data,0,0,&source_ref,TIDE_IN_SCRATCH)
        || !tide_skinned_rest(target_data,&target_ref,TIDE_IN_SCRATCH)
        || !tide_humanoid_auto(ref_data,source_ref,&source_bones,&source_status,TIDE_IN_SCRATCH)
        || !tide_humanoid_auto(target_data,target_ref,&target_bones,&target_status,TIDE_IN_SCRATCH)
        || tide_retarget_create(raw_data,ref_data,source_ref,source_bones,source_status,target_data,target_ref,target_bones,target_status,&plan,&root_scale,TIDE_IN_SCRATCH)
        || !tide_skinned_sample(raw_data,0,0.2f,&source_pose,TIDE_IN_SCRATCH)) return 3;
    // The prepared map in a game field owns a chunked byte list, rather than
    // keeping an artificial contiguous scratch plan for these timings.
    tide_list_own(&plan,1u,TIDE_IN_LOCAL);
    printf("NODES source=%d target=%d baked=%d PLAN_BYTES %d\n",tide_skinned_bone_count(raw_data),tide_skinned_bone_count(target_data),tide_skinned_bone_count(baked_data),tide_list_count(plan));
    double data[KINDS][ROUNDS];
    for (int k=0;k<KINDS;k++) (void)batch(k,0,100);
    for (int r=0;r<ROUNDS;r++) for (int turn=0;turn<KINDS;turn++) {
        const int k=(r+turn)%KINDS;
        data[k][r]=batch(k,r,k==4?100:CALLS);
    }
    const char *names[KINDS]={"C_baked_sample","C_raw_sample","C_retarget_only","C_sample_and_retarget","C_auto_and_create","C_legacy_baked_sample"};
    for (int k=0;k<KINDS;k++) report(names[k],k==4?100:CALLS,data[k]);
    // Record scratch growth per call independently of timer overhead.
    for (int k=0;k<4;k++) {
        const uint32_t mark=tide_scratch_mark();
        tide_list output={0}, source={0};
        if (k==0) { (void)tide_skinned_rest(target_data,&output,TIDE_IN_SCRATCH); (void)tide_skinned_sample_into(target_data,baked_data,0,.2f,&output,TIDE_IN_SCRATCH); }
        if (k==1) (void)tide_skinned_sample(raw_data,0,.2f,&output,TIDE_IN_SCRATCH);
        if (k==2) (void)apply(source_pose,&output);
        if (k==3) { (void)tide_skinned_rest(target_data,&output,TIDE_IN_SCRATCH); (void)tide_skinned_sample(raw_data,0,.2f,&source,TIDE_IN_SCRATCH); (void)apply(source,&output); }
        printf("SCRATCH %s %u\n",names[k],tide_scratch_mark()-mark);
        tide_scratch_reset(mark);
    }
    // Frame calls reset scratch; nothing kept above is used after this point.
    const double setup = frame(2, 1, .2f);
    printf("LOAD map_bake_and_first_sample_us=%.6f ready=%d\n", setup, (int)local.Bench.bakeReady);
    const int sizes[]={1,100,1000};
    for (int n=0;n<3;n++) {
        double views[3][ROUNDS];
        for (int m=0;m<3;m++) for (int f=0;f<3;f++) (void)frame(m,sizes[n],.2f);
        if (!local.Bench.bakeReady) { fputs("runtime Bake failed\n", stderr); return 4; }
        (void)frame(0,sizes[n],.2f);
        const float expected = consumed;
        (void)frame(2,sizes[n],.2f);
        if (consumed != expected) failed++;
        for (int r=0;r<ROUNDS;r++) for (int turn=0;turn<3;turn++) {
            const int m=(r+turn)%3;
            double sum=0;
            for (int f=0;f<10;f++) sum+=frame(m,sizes[n],(float)(r*10+f)*.013f);
            views[m][r]=sum/10;
        }
        report("Tide_baked",sizes[n],views[0]);
        report("Tide_sample_and_retarget",sizes[n],views[1]);
        report("Tide_runtime_baked",sizes[n],views[2]);
    }
    printf("FAILURES %d\n",failed);
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&world); tide_pack_unload();
    return failed?1:0;
}
