#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide_test.h"

// Random.NextInt and the like: the match's rolls come from its seed, the tick, the entity and
// which code rolls, writing nothing to the world; local code's are this machine's.

static tide_world world;
static tide_world other;
static tide_local local;
static tide_local twin;
static tide_draw_list draw;
static tide_gui gui;

#define DT (1.0f / 60.0f)

static void frame(tide_local *l, const float seconds)
{
    tide_local_frame_time(l, seconds);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, l, &draw, &gui);
}

static int32_t cell(const tide_world *w, const tide_grid grid, const int x, const int y)
{
    const int32_t *c = tide_grid_read(&w->heap, grid, x, y, 0, &tide_shape_Grid2_int);
    return c ? *c : 0;
}

static int walkers(const tide_world *w, tide_entity *out, const int room)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Walker(w, e) && count < room) out[count++] = e;
    }
    return count;
}

static uint64_t hash_after(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
    return tide_world_hash(w);
}

// The seed makes the match: the same one rolls the same, another one otherwise.
TIDE_TEST(rolls_come_from_the_seed)
{
    TIDE_CHECK(tide_game_api.seeded);
    tide_world_start_seeded(&world, DT, NULL, 4242u);
    const uint64_t first = hash_after(&world, 30);
    TIDE_CHECK(world.Board.seed == 4242 && world.Board.through == 4242);
    tide_world_start_seeded(&other, DT, NULL, 4242u);
    TIDE_CHECK(hash_after(&other, 30) == first);
    tide_world_start_seeded(&other, DT, NULL, 4243u);
    TIDE_CHECK(hash_after(&other, 30) != first);
    tide_world_init(&other, DT); // Started plainly: seed 0, the same every time
    const uint64_t plain = hash_after(&other, 30);
    tide_world_init(&other, DT);
    TIDE_CHECK(hash_after(&other, 30) == plain && plain != first);
}

// Each entity rolls for itself, twins too; each roll of a run differs; two systems differ for one
// entity; a tick later all differ.
TIDE_TEST(rolls_are_an_entitys_a_systems_and_a_ticks)
{
    tide_world_start_seeded(&world, DT, NULL, 99u);
    tide_world_tick(&world);
    tide_world_tick(&world);
    tide_entity all[16];
    const int count = walkers(&world, all, 16);
    TIDE_REQUIRE(count == 8);
    Walker before[8];
    for (int a = 0; a < count; a++) {
        const Walker w = tide_get_Walker(&world, all[a]);
        const Mood m = tide_get_Mood(&world, all[a]);
        before[a] = w;
        TIDE_CHECK(w.first != w.second && w.first != m.roll && w.first != w.method);
        TIDE_CHECK(w.added >= 3000000 && w.added < 3001000);
        TIDE_CHECK(w.first >= 0 && w.first < 1000000 && w.looped >= 0 && w.looped < 10000 && w.helped >= 0 && w.helped < 1000000);
        const float length = w.direction.x * w.direction.x + w.direction.y * w.direction.y;
        TIDE_CHECK(length > 0.999f && length < 1.001f);
        for (int b = a + 1; b < count; b++) {
            const Walker o = tide_get_Walker(&world, all[b]);
            TIDE_CHECK(w.first != o.first && w.second != o.second && w.helped != o.helped);
        }
    }
    const int32_t once = world.Board.once;
    tide_world_tick(&world);
    TIDE_CHECK(world.Board.once != once);
    for (int a = 0; a < count; a++) {
        const Walker w = tide_get_Walker(&world, all[a]);
        TIDE_CHECK(w.first != before[a].first && w.second != before[a].second);
    }
}

// A roll steps nothing in the world, so a tick rerun from a snapshot rolls the same (prediction and rollback rely on it).
TIDE_TEST(rolls_repeat_when_a_tick_runs_again)
{
    tide_world_start_seeded(&world, DT, NULL, 7u);
    for (int i = 0; i < 10; i++) tide_world_tick(&world);
    tide_world_copy(&other, &world);
    const uint64_t ahead = hash_after(&world, 5);
    for (int again = 0; again < 3; again++) {
        tide_world scratch = {0};
        tide_world_copy(&scratch, &other);
        TIDE_CHECK(hash_after(&scratch, 5) == ahead);
        tide_world_free(&scratch);
    }
}

// One event sent twice to one entity in one tick: each handler run rolls for itself.
TIDE_TEST(rolls_differ_between_a_handlers_runs)
{
    tide_world_start_seeded(&world, DT, NULL, 31u);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    tide_entity all[16];
    TIDE_REQUIRE(walkers(&world, all, 16) == 8);
    const Walker poked = tide_get_Walker(&world, all[0]);
    TIDE_CHECK(poked.pokes == 2);
    TIDE_CHECK(poked.poked / 1000 != poked.poked % 1000);
    // And the same again in the same match
    tide_world_start_seeded(&other, DT, NULL, 31u);
    for (int i = 0; i < 6; i++) tide_world_tick(&other);
    TIDE_REQUIRE(walkers(&other, all, 16) == 8);
    TIDE_CHECK(tide_get_Walker(&other, all[0]).poked == poked.poked);
}

// Parallel loop steps, a foreach run at once, and a list's: each place rolls for itself.
TIDE_TEST(rolls_in_steps_are_each_places)
{
    tide_world_start_seeded(&world, DT, NULL, 8u);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    static int32_t cells[256], through[256], blocks[256], row[48];
    for (int pass = 0; pass < 2; pass++) {
        int same = 0, same_through = 0, same_blocks = 0, same_row = 0;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                const int32_t value = cell(&world, world.Field.cells, x, y);
                const int32_t other = cell(&world, world.Field.through, x, y);
                const int32_t block = cell(&world, world.Field.blocks, x, y);
                TIDE_CHECK(value >= 1 && value <= 999000 && other >= 1 && other <= 999 && block >= 1 && block <= 999);
                if (pass) same += cells[y * 16 + x] == value;
                if (pass) same_through += through[y * 16 + x] == other;
                if (pass) same_blocks += blocks[y * 16 + x] == block;
                cells[y * 16 + x] = value;
                through[y * 16 + x] = other;
                blocks[y * 16 + x] = block;
            }
        }
        TIDE_REQUIRE(tide_list_count(world.Field.row) == 48);
        for (int i = 0; i < 48; i++) {
            const int32_t value = *(const int32_t *)tide_list_at(world.Field.row, sizeof(int32_t), (uint32_t)i);
            if (pass) same_row += row[i] == value;
            row[i] = value;
        }
        // No two alike but by chance: of 256 six-digit numbers, a few pairs at the most
        int alike = 0, alike_through = 0, alike_blocks = 0;
        for (int a = 0; a < 256; a++) {
            for (int b = a + 1; b < 256; b++) {
                alike += cells[a] == cells[b];
                alike_through += through[a] == through[b];
                alike_blocks += blocks[a] == blocks[b];
            }
        }
        TIDE_CHECK(alike < 4 && alike_through < 120 && alike_blocks < 120);
        TIDE_CHECK(same < 4 && same_through < 12 && same_blocks < 12 && same_row < 3);
        tide_world_tick(&world);
    }
    // The shuffle: the same twelve numbers, in another order than they went in
    TIDE_REQUIRE(tide_list_count(world.Field.order) == 12);
    int sum = 0, moved = 0;
    for (int i = 0; i < 12; i++) {
        const int32_t value = *(const int32_t *)tide_list_at(world.Field.order, sizeof(int32_t), (uint32_t)i);
        sum += value;
        moved += value != i;
    }
    TIDE_CHECK(sum == 66 && moved > 0);
}

static int chests(const tide_world *w, Chest *out)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Chest(w, e) && count < 2) out[count++] = tide_get_Chest(w, e);
    }
    return count;
}

// A task's rolls are its own sequence across waits and awaits; tasks started alike in one tick
// roll differently, as do a handler's two for one event sent twice.
TIDE_TEST(rolls_in_tasks_go_on_across_their_waits)
{
    tide_world_start_seeded(&world, DT, NULL, 21u);
    for (int i = 0; i < 20; i++) tide_world_tick(&world);
    Chest all[2];
    TIDE_REQUIRE(chests(&world, all) == 2);
    for (int i = 0; i < 2; i++) {
        TIDE_CHECK(all[i].first != all[i].second && all[i].second != all[i].later && all[i].first != all[i].later);
        TIDE_CHECK(all[i].seed == 21);
    }
    TIDE_CHECK(all[0].first != all[1].first && all[0].second != all[1].second && all[0].later != all[1].later);
    const Chest knocked = all[0].id == 0 ? all[0] : all[1];
    TIDE_CHECK(knocked.knocks == 2 && knocked.knocked / 1000 != knocked.knocked % 1000);
    // The same match again, and from a snapshot taken while the tasks wait
    tide_world_start_seeded(&other, DT, NULL, 21u);
    for (int i = 0; i < 2; i++) tide_world_tick(&other);
    tide_world scratch = {0};
    tide_world_copy(&scratch, &other);
    for (int i = 0; i < 18; i++) tide_world_tick(&other);
    for (int i = 0; i < 18; i++) tide_world_tick(&scratch);
    Chest again[2], kept[2];
    TIDE_REQUIRE(chests(&other, again) == 2 && chests(&scratch, kept) == 2);
    for (int i = 0; i < 2; i++) {
        TIDE_CHECK(again[i].first == all[i].first && again[i].later == all[i].later && again[i].knocked == all[i].knocked);
        TIDE_CHECK(kept[i].second == all[i].second && kept[i].later == all[i].later && kept[i].knocked == all[i].knocked);
    }
    TIDE_CHECK(tide_world_hash(&other) == tide_world_hash(&world) && tide_world_hash(&scratch) == tide_world_hash(&world));
    tide_world_free(&scratch);
}

// Sample rolls with the sampling machine's generator and reads its last frame's match seed; with no local state it still rolls.
TIDE_TEST(rolls_in_sample_are_the_machines_that_sends)
{
    static tide_devices devices;
    tide_world_start_seeded(&world, DT, NULL, 77u);
    tide_local_init(&local);
    frame(&local, DT);
    const tide_input a = tide_input_sample(&devices, &local);
    const tide_input b = tide_input_sample(&devices, &local);
    TIDE_CHECK(a.luck != b.luck && a.luck >= 0 && a.luck < 1000000 && a.seed == 77 && b.seed == 77);
    const tide_input alone = tide_input_sample(&devices, NULL);
    const tide_input alone_again = tide_input_sample(&devices, NULL);
    TIDE_CHECK(alone.seed == 0 && alone.luck != alone_again.luck);
    // What the match reads is what was sent
    tide_world_set_server_input(&world, a);
    tide_world_tick(&world);
    TIDE_CHECK(world.Luck.last == a.luck);
    tide_local_free(&local);
}

// The bits on every platform (native, WebAssembly, Android) must give this hash; update it
// only for an intentional change, after checking the others agree.
TIDE_TEST(rolls_crossplatform_match)
{
    tide_world_start_seeded(&world, DT, NULL, 20261008u);
    const uint64_t h = hash_after(&world, 120);
    const uint64_t expected = 0xA597DE5DB79E0D7Cull;
    if (h != expected) printf("    rolls match: hash is 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n", h, expected);
    TIDE_CHECK(h == expected);
}

// Local code rolls with this machine's generator: nothing of the match, the same again where
// frames are as long (a run with no window), different where they aren't.
TIDE_TEST(rolls_in_views_are_this_machines)
{
    tide_world_start_seeded(&world, DT, NULL, 555u);
    tide_world_tick(&world);
    tide_world_tick(&world);
    const uint64_t match = tide_world_hash(&world);
    tide_local_init(&local);
    tide_local_init(&twin);
    float last = -1.0f;
    for (int i = 0; i < 20; i++) {
        frame(&local, DT);
        frame(&twin, DT);
        TIDE_CHECK(local.Fx.last >= 0.0f && local.Fx.last < 1.0f && local.Fx.helped >= 10.0f && local.Fx.helped < 20.0f);
        TIDE_CHECK(local.Fx.last != last);
        TIDE_CHECK(local.Fx.last == twin.Fx.last && local.Fx.helped == twin.Fx.helped);
        TIDE_CHECK(local.Fx.seed == 555);
        last = local.Fx.last;
    }
    TIDE_CHECK(local.Fx.frames == 20);
    // A local handler and a local task roll too, and read the seed
    TIDE_CHECK(local.Fx.flashes == 1 && local.Fx.flash >= 5.0f && local.Fx.flash < 6.0f && local.Fx.flashSeed == 555);
    TIDE_CHECK(local.Fx.twinkle >= 1.0f && local.Fx.twinkle < 2.0f && local.Fx.twinkled >= 2.0f && local.Fx.twinkled < 3.0f);
    TIDE_CHECK(local.Fx.twinkleSeed == 555 && local.Fx.flash == twin.Fx.flash && local.Fx.twinkled == twin.Fx.twinkled);
    TIDE_CHECK(tide_world_hash(&world) == match);
    // Each entity a view runs for gets its own number
    int32_t shines[8];
    int count = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Glint(&world, e)) continue;
        TIDE_REQUIRE(tide_has_Glint_local(&local, e) && count < 8);
        shines[count] = tide_get_Glint_local(&local, e).shine;
        for (int k = 0; k < count; k++) TIDE_CHECK(shines[k] != shines[count]);
        count++;
    }
    TIDE_CHECK(count == 4);
    // Frames of another length: other numbers
    frame(&local, DT);
    frame(&twin, DT * 1.5f);
    TIDE_CHECK(local.Fx.last != twin.Fx.last);
    // With no match, a view still rolls, and the seed reads 0
    tide_draw_reset(&draw);
    tide_local_frame_time(&local, DT);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Fx.seed == 0 && local.Fx.frames == 22);
    tide_local_free(&local);
    tide_local_free(&twin);
}
