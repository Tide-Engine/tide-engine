// A probe, while the game is being made.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide/time.h"
#include "tide_test.h"

#include <stdlib.h>
bool b3GetSnapshotBases(const uint8_t *image, int size, int *bases);
void b3t_forget(void);
void b3t_images(int32_t *whole, int32_t *in_parts);
int32_t b3t_restores(void);
void b3t_measure(uint64_t *step, uint64_t *image);

#define DT (1.0f / 60.0f)

static tide_world world;

static tide_player_id player(const int index)
{
    return tide_player_from_index(index);
}

typedef bool has_fn(const tide_world *, tide_entity);

static int count(const tide_world *w, has_fn *has)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        if (has(w, tide_entity_in_slot(&w->entities, i))) found++;
    }
    return found;
}

static tide_entity ball_of(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Ball(w, e) && tide_get_Ball(w, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

static float disturbed(const tide_world *w, const tide_world *built, float *highest)
{
    float most = 0;
    *highest = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Crate(w, e) || !tide_has_Crate(built, e)) continue;
        const tide_float3 now = tide_get_Box3D_Body(w, e).position;
        const tide_float3 then = tide_get_Box3D_Body(built, e).position;
        const float moved = tide_abs_f(now.x - then.x) + tide_abs_f(now.y - then.y) + tide_abs_f(now.z - then.z);
        if (moved > 0.5f) printf("      moved %.2f: kind %d from (%.2f %.2f %.2f) to (%.2f %.2f %.2f)\n", (double)moved, (int)tide_get_Crate(w, e).kind, (double)then.x, (double)then.y, (double)then.z, (double)now.x, (double)now.y, (double)now.z);
        if (moved > most) most = moved;
        if (now.y > *highest) *highest = now.y;
    }
    return most;
}

static void probe(const int round)
{
    b3t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    world.Ring.round = round - 1;
    world.Ring.bots = false;
    tide_world_player_joined(&world, player(0));
    uint64_t began = tide_time_now_ns();
    tide_world_tick(&world);
    const double first = (double)(tide_time_now_ns() - began) / 1e6;
    began = tide_time_now_ns();
    tide_world_tick(&world);
    const double second = (double)(tide_time_now_ns() - began) / 1e6;
    tide_world built = {0};
    tide_world_copy(&built, &world);
    int rested = -1;
    double settle = 0;
    for (int i = 0; i < 400; i++) {
        began = tide_time_now_ns();
        tide_world_tick(&world);
        settle += (double)(tide_time_now_ns() - began) / 1e6;
        if (!world.Box3D_Physics.moved && rested < 0) rested = i;
        if (rested >= 0) break;
    }
    // What a rollback pays: C's world made again from the image, and a tick with nothing awake
    uint64_t step_ns, image_ns;
    b3t_measure(&step_ns, &image_ns);
    double restore = 1e9;
    for (int i = 0; i < 5; i++) {
        b3t_forget();
        tide_world_set_input(&world, player(0), (Controls){.move = {0, 0.001f * (float)i}});
        began = tide_time_now_ns();
        tide_world_tick(&world);
        const double took = (double)(tide_time_now_ns() - began) / 1e6;
        if (took < restore) restore = took;
    }
    b3t_measure(&step_ns, &image_ns);
    printf("    a tick that makes C's world again: %.2f ms (of which reading the image %.2f)\n", restore, (double)image_ns / 5 / 1e6);
    {
        const int size = world.Box3D_Physics.size;
        uint8_t *image = malloc((size_t)size);
        for (int i = 0; i < size; i++) image[i] = *(const uint8_t *)tide_list_read(&world.heap, world.Box3D_Physics.image, i, 1);
        int bases[17];
        if (b3GetSnapshotBases(image, size, bases)) {
            printf("    sections:");
            for (int i = 0; i < 17; i++) printf(" %d", (i < 16 ? bases[i + 1] : size) - bases[i]);
            printf("\n");
        }
        free(image);
    }
    float highest;
    const float moved = disturbed(&world, &built, &highest);
    printf("    round %d: %d crates, %d kegs, %d tiles, %d bodies; built in %.2f + %.2f ms; at rest after %d ticks (%.1f ms in all),"
           " moved %.3f m at most, highest %.2f; physics %d bytes\n",
           round, count(&world, tide_has_Crate), count(&world, tide_has_Keg), count(&world, tide_has_Tile),
           count(&world, tide_has_Box3D_Body), first, second, rested, settle, (double)moved, (double)highest,
           world.Box3D_Physics.size);
    tide_world_free(&built);
}

TIDE_TEST(sumo_probe_arenas)
{
    for (int round = 1; round <= 3; round++) probe(round);
    tide_world_free(&world);
}

TIDE_TEST(sumo_probe_play)
{
    b3t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    tide_world_player_joined(&world, player(0));
    int32_t whole0, parts0;
    b3t_images(&whole0, &parts0);
    uint64_t total = 0;
    uint64_t worst = 0;
    int outs = 0;
    tide_world kept = {0};
    uint64_t keeping = 0;
    for (int i = 0; i < 60 * 150; i++) {
        tide_world_set_input(&world, player(0), (Controls){.away = true});
        const uint64_t began = tide_time_now_ns();
        tide_world_tick(&world);
        const uint64_t took = tide_time_now_ns() - began;
        tide_world_copy(&kept, &world);
        (void)tide_world_hash(&world);
        keeping += tide_time_now_ns() - began - took;
        total += took;
        if (took > worst) worst = took;
        if (i % 600 == 599) {
            int32_t whole, parts;
            b3t_images(&whole, &parts);
            printf("    after %d s: %.3f ms a tick, %.3f to keep, worst %.2f; %d bodies, %d crates; phase %d radius %.1f; images %d whole"
                   " %d in parts; scores",
                   (i + 1) / 60, (double)total / 600 / 1e6, (double)keeping / 600 / 1e6, (double)worst / 1e6,
                   count(&world, tide_has_Box3D_Body), count(&world, tide_has_Crate), world.Ring.phase,
                   (double)world.Ring.radius, whole - whole0, parts - parts0);
            for (int seat = 0; seat < 8; seat++) {
                for (uint32_t k = 0; k < world.entities.next_unused; k++) {
                    const tide_entity e = tide_entity_in_slot(&world.entities, k);
                    if (tide_has_Seat(&world, e) && tide_get_Seat(&world, e).seat == seat) printf(" %d", tide_get_Seat(&world, e).score);
                }
            }
            printf("\n");
            total = 0;
            worst = 0;
            keeping = 0;
            whole0 = whole;
            parts0 = parts;
        }
        (void)outs;
    }
    (void)ball_of;
    tide_world_free(&kept);
    tide_world_free(&world);
}
