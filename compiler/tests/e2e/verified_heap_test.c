#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_draw_list draw;
static tide_gui gui;
static tide_local local;

static bool is(const tide_text field, const char *expected)
{
    const tide_str s = tide_text_read(&local.heap, field);
    const bool same = (size_t)s.bytes == strlen(expected) && memcmp(s.ptr, expected, (size_t)s.bytes) == 0;
    if (!same) printf("    got      \"%.*s\"\n    expected \"%s\"\n", (int)s.bytes, s.ptr, expected);
    return same;
}

static void frame(const tide_world *drawn, const tide_world *confirmed)
{
    tide_draw_reset(&draw);
    tide_frame(drawn, NULL, 1.0f, confirmed, &local, &draw, &gui);
}

// A world's bytes, to tell that reading it changed nothing of it.
static uint8_t *packed(const tide_world *w, uint32_t *size)
{
    *size = tide_world_pack(w, NULL, 0);
    uint8_t *bytes = malloc(*size);
    tide_world_pack(w, bytes, *size);
    return bytes;
}

// What Read gives of pack 1 after 2 ticks, and after 5
#define PACK_2 "pack 1 at 2|2:3:2|item 1=1,item 2=2,|tag 1,tag 2,1:0|2:20:0:0:33|name 1=1,name 2=2,2:0|slot 2=4:|seen 1,seen 2,2:1:0"
#define PACK_5                                                                                                                  \
    "pack 1 at 5|5:15:5|item 1=1,item 2=2,item 3=3,item 4=4,item 5=5,|tag 1,tag 2,tag 3,tag 4,tag 5,1:1|5:20:50:1:165|"        \
    "name 1=1,name 2=2,name 3=3,name 4=4,name 5=5,2:1|slot 2=4:slot 5|seen 1,seen 2,seen 3,seen 4,seen 5,5:1:1"
#define BOARD_2 "board at 2|3|note 1,note 2,|2:0:3"
#define BOARD_5 "board at 5|15|note 1,note 2,note 3,note 4,note 5,|2:5:15"

// Two worlds that went on for different numbers of ticks: each view reads
// both, and each world's text, lists, maps, sets and grid from its own heap.
TIDE_TEST(verified_heap_reads_the_confirmed_world_beside_the_drawn_one)
{
    static tide_world drawn, confirmed;
    tide_world_init(&drawn, 1.0f);
    tide_world_init(&confirmed, 1.0f);
    for (int i = 0; i < 5; i++) tide_world_tick(&drawn);
    for (int i = 0; i < 2; i++) tide_world_tick(&confirmed);
    uint32_t size, size_after;
    uint8_t *before = packed(&confirmed, &size);
    const uint64_t hash = tide_world_hash(&confirmed), drawn_hash = tide_world_hash(&drawn);

    tide_local_init(&local);
    frame(&drawn, &confirmed);
    TIDE_CHECK(is(local.Probe.drawn, PACK_5));
    TIDE_CHECK(is(local.Probe.sure, PACK_2));
    TIDE_CHECK(is(local.Probe.copied, "pack 1 at 2|item 2|11|item 1|tag 2|slot 1"));
    TIDE_CHECK(is(local.Probe.only, "pack 1 at 2:item 1=1,item 2=2,tag 1,tag 2,;pack 2 at 2:item 1=1,item 2=2,tag 1,tag 2,;"));
    TIDE_CHECK(is(local.Probe.board, BOARD_5));
    TIDE_CHECK(is(local.Probe.sureBoard, BOARD_2));
    TIDE_CHECK(local.Probe.grown == 500 + 2 + 1000); // The confirmed scores 1 and 2, changed in a copy
    TIDE_CHECK(tide_scratch_mark() == 0);            // The copies a frame made are gone with it

    // Reading changed neither world
    uint8_t *after = packed(&confirmed, &size_after);
    TIDE_CHECK(size_after == size && memcmp(before, after, size) == 0);
    TIDE_CHECK(tide_world_hash(&confirmed) == hash && tide_world_hash(&drawn) == drawn_hash);
    free(before);
    free(after);

    // The confirmed world goes on: views read it as it is now, and what one put in local state is as it was
    for (int i = 0; i < 3; i++) tide_world_tick(&confirmed);
    tide_world_tick(&drawn);
    frame(&drawn, &confirmed);
    TIDE_CHECK(is(local.Probe.sure, PACK_5));
    TIDE_CHECK(is(local.Probe.sureBoard, BOARD_5));
    TIDE_CHECK(is(local.Probe.keptLabel, "pack 1 at 2"));
    TIDE_REQUIRE(tide_list_read_count(&local.heap, local.Probe.kept) == 2);
    const Item *kept = tide_list_read(&local.heap, local.Probe.kept, 1, sizeof(Item));
    TIDE_CHECK(kept->count == 2 && is(kept->name, "item 2"));
    TIDE_CHECK(local.Probe.grown == 500 + 2 + 3 + 4 + 5 + 1000);
    TIDE_CHECK(tide_scratch_mark() == 0);

    tide_local_free(&local);
    tide_world_free(&drawn);
    tide_world_free(&confirmed);
}

// A confirmed world that's a snapshot of the drawn one from ticks ago, as a
// client's is: they share pages, and each is still read as itself.
TIDE_TEST(verified_heap_reads_a_snapshot_as_it_was)
{
    static tide_world drawn, confirmed;
    tide_world_init(&drawn, 1.0f);
    for (int i = 0; i < 2; i++) tide_world_tick(&drawn);
    tide_world_copy(&confirmed, &drawn);
    for (int i = 0; i < 3; i++) tide_world_tick(&drawn);
    tide_local_init(&local);
    frame(&drawn, &confirmed);
    TIDE_CHECK(is(local.Probe.drawn, PACK_5));
    TIDE_CHECK(is(local.Probe.sure, PACK_2));
    TIDE_CHECK(is(local.Probe.board, BOARD_5));
    TIDE_CHECK(is(local.Probe.sureBoard, BOARD_2));
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&drawn);
    tide_world_free(&confirmed);
}

// One world and no server: it's the confirmed one too.
TIDE_TEST(verified_heap_one_world)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 5; i++) tide_world_tick(&world);
    tide_local_init(&local);
    frame(&world, NULL);
    TIDE_CHECK(is(local.Probe.drawn, PACK_5));
    TIDE_CHECK(is(local.Probe.sure, PACK_5));
    TIDE_CHECK(is(local.Probe.sureBoard, BOARD_5));
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_local_free(&local);
    tide_world_free(&world);
}
