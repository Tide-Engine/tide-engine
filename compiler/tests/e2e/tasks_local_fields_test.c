#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/text.h"
#include "tide/worker.h"
#include "tide_test.h"

// Local tasks that use local fields (tasks_local_fields.tide): what a task
// writes is its entity's, read where the local world keeps it each time the
// task goes on; the task ends with its entity, with its component, and with
// its match, and the match is the same as without it.

static tide_world world;
static tide_world verified;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// Workers whose work runs when the test says (complete), as local_workers_test.c's
static tide_background *pending;
static void defer(void *self, tide_background *job)
{
    (void)self;
    job->next = pending;
    pending = job;
}

static void complete(void)
{
    while (pending) {
        tide_background *job = pending;
        pending = job->next;
        job->work(job->context);
        __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    }
}

static bool help(void *self, const bool helpers_only)
{
    (void)self;
    if (helpers_only || !pending) return false;
    tide_background *job = pending;
    pending = job->next;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

static const tide_jobs deferred = {.threads = 1, .background = defer, .help = help};

static void start(void)
{
    pending = NULL;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    gui.jobs = &deferred;
}

static void finish(void)
{
    complete();
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
}

static void frame_on(const tide_world *w, const tide_world *confirmed)
{
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, confirmed, NULL, 1.0f, &local, &draw, &gui);
    tide_text_use(NULL, &local.heap);
}

static void frame(void)
{
    frame_on(&world, NULL);
}

static tide_entity chunk_of(const tide_world *w, const int32_t number)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Chunk(w, e) && tide_get_Chunk(w, e).number == number) return e;
    }
    return (tide_entity){0};
}

static bool text_is(const tide_text text, const char *want)
{
    const tide_str s = tide_text_read(&local.heap, text);
    return s.bytes == (int32_t)strlen(want) && memcmp(s.ptr, want, strlen(want)) == 0;
}

static int32_t face(const Chunk_local *fields, const int32_t i)
{
    return *(const int32_t *)tide_list_at(fields->faces, i, sizeof(int32_t));
}

static uint32_t remeshing(void)
{
    return local.tide_tasks_function_Remesh.count;
}

// Each chunk's task writes its own chunk's local fields, before its wait (in
// the view's run, which shares the row) and after it, through a function too
TIDE_TEST(tasks_local_fields_write_the_entitys_own)
{
    start();
    const tide_entity one = chunk_of(&world, 1), two = chunk_of(&world, 2);
    TIDE_REQUIRE(!tide_entity_is_null(one) && !tide_entity_is_null(two));
    const uint64_t hash = tide_world_hash(&world);
    frame();
    TIDE_CHECK(remeshing() == 2 && pending != NULL);
    Chunk_local a = tide_get_Chunk_local(&local, one), b = tide_get_Chunk_local(&local, two);
    TIDE_CHECK(a.steps == 1 && a.meshing && a.drawn == 0 && tide_list_count(a.faces) == 0 && text_is(a.label, "new"));
    TIDE_CHECK(b.steps == 1 && b.meshing && b.drawn == 0);
    frame(); // The workers haven't run: the tasks wait
    TIDE_CHECK(remeshing() == 2 && tide_get_Chunk_local(&local, one).steps == 1);

    complete();
    frame();
    TIDE_CHECK(remeshing() == 0);
    a = tide_get_Chunk_local(&local, one);
    b = tide_get_Chunk_local(&local, two);
    TIDE_CHECK(a.steps == 11 && !a.meshing && a.drawn == 1);
    TIDE_REQUIRE(tide_list_count(a.faces) == 3 && tide_list_count(b.faces) == 6);
    TIDE_CHECK(face(&a, 0) == 100 && face(&a, 2) == 102 && face(&b, 5) == 205);
    TIDE_CHECK(text_is(a.label, "chunk 1 at 1, 3 faces") && text_is(b.label, "chunk 2 at 1, 6 faces"));
    TIDE_CHECK(b.steps == 11 && b.drawn == 1);
    TIDE_CHECK(local.tide_fields_Chunk.rows.count == 2);
    TIDE_CHECK(tide_world_hash(&world) == hash); // Nothing of it is the match's

    // The match changes one chunk: its view starts another task for it alone
    world.Rules.edit = 2;
    tide_world_tick(&world);
    world.Rules.edit = 0;
    frame();
    TIDE_CHECK(remeshing() == 1 && tide_get_Chunk_local(&local, two).steps == 12);
    complete();
    frame();
    a = tide_get_Chunk_local(&local, one);
    b = tide_get_Chunk_local(&local, two);
    TIDE_CHECK(a.steps == 11 && a.drawn == 1);
    TIDE_CHECK(b.steps == 22 && b.drawn == 2 && !b.meshing && text_is(b.label, "chunk 2 at 2, 6 faces"));
    finish();
}

// A task whose entity is destroyed while it waits ends, and nothing it would
// have written is kept for an entity that's gone; the other chunk's goes on
TIDE_TEST(tasks_local_fields_end_with_their_entity)
{
    start();
    const tide_entity one = chunk_of(&world, 1), two = chunk_of(&world, 2);
    frame();
    TIDE_CHECK(remeshing() == 2);
    world.Rules.drop = 1;
    tide_world_tick(&world);
    world.Rules.drop = 0;
    TIDE_REQUIRE(!tide_has_Chunk(&world, one));
    frame(); // Its local fields go (Forgotten), then its task
    TIDE_CHECK(local.Stats.forgotten == 1 && local.Stats.lastSteps == 1);
    TIDE_CHECK(remeshing() == 1 && !tide_has_Chunk_local(&local, one));
    complete();
    frame();
    TIDE_CHECK(remeshing() == 0 && !tide_has_Chunk_local(&local, one));
    TIDE_CHECK(local.tide_fields_Chunk.rows.count == 1);
    TIDE_CHECK(tide_get_Chunk_local(&local, two).steps == 11 && tide_get_Chunk_local(&local, two).drawn == 1);
    finish();
}

// A task that reads a component's local fields ends when its entity loses
// the component, which takes its local fields with it
TIDE_TEST(tasks_local_fields_end_with_their_component)
{
    start();
    const tide_entity one = chunk_of(&world, 1), two = chunk_of(&world, 2);
    frame(); // Glows writes each glow's lit and starts Light, which waits a frame
    TIDE_CHECK(local.Stats.glows == 2 && local.tide_tasks_function_Light.count == 2);
    TIDE_CHECK(tide_get_Glow_local(&local, one).lit == 5);
    world.Rules.shed = 1;
    tide_world_tick(&world);
    world.Rules.shed = 0;
    TIDE_REQUIRE(!tide_has_Glow(&world, one) && tide_has_Glow(&world, two));
    frame();
    TIDE_CHECK(local.tide_tasks_function_Light.count == 0);
    TIDE_CHECK(local.Stats.lit == 6); // Two's alone: what its view wrote, read a frame later
    TIDE_CHECK(!tide_has_Glow_local(&local, one) && tide_get_Glow_local(&local, two).lit == 5);
    finish();
}

// ...whether it was waiting or due: a task that waits 20 frames ends at the
// first frame after its Ember is gone, and the Ember the entity gets back has
// new local fields and a task of its own, which no task of the old one writes
TIDE_TEST(tasks_local_fields_end_with_their_component_while_they_wait)
{
    start();
    const tide_entity one = chunk_of(&world, 1);
    frame(); // Embers writes lit and starts Fade, which waits 20 frames
    TIDE_CHECK(local.tide_tasks_function_Fade.count == 1 && tide_get_Ember_local(&local, one).lit == 1);
    world.Rules.quench = 1;
    tide_world_tick(&world);
    world.Rules.quench = 0;
    TIDE_REQUIRE(!tide_has_Ember(&world, one));
    frame();
    TIDE_CHECK(local.tide_tasks_function_Fade.count == 0); // Not waiting on for frames it can't go on in
    world.Rules.kindle = 1;
    tide_world_tick(&world);
    world.Rules.kindle = 0;
    TIDE_REQUIRE(tide_has_Ember(&world, one));
    frame(); // The new Ember's view starts a task of its own
    TIDE_CHECK(local.tide_tasks_function_Fade.count == 1 && tide_get_Ember_local(&local, one).lit == 1);
    for (int i = 0; i < 25; i++) frame();
    TIDE_CHECK(local.tide_tasks_function_Fade.count == 0 && tide_get_Ember_local(&local, one).lit == 99);
    finish();
}

// A singleton's local fields, written by a task beside its match fields' values
TIDE_TEST(tasks_local_fields_of_a_singleton)
{
    start();
    frame();
    TIDE_CHECK(local.Arena_local.painting && local.Arena_local.painted == 0);
    frame();
    frame();
    TIDE_CHECK(!local.Arena_local.painting && local.Arena_local.painted == 4);
    TIDE_CHECK(local.tide_tasks_function_Paint.count == 0);
    frame();
    TIDE_CHECK(local.Arena_local.painted == 4 && local.tide_tasks_function_Paint.count == 0);
    finish();
}

// A destroy the client predicted ends the task, as it ends the entity's
// views; its local fields stay while the confirmed world has the entity, so a
// destroy the server undoes finds them as the task left them, and the view,
// whose handle of it says it ended, starts another
TIDE_TEST(tasks_local_fields_after_a_wrong_prediction)
{
    start();
    const tide_entity one = chunk_of(&world, 1);
    tide_world_copy(&verified, &world);
    frame_on(&world, &verified);
    TIDE_CHECK(remeshing() == 2);
    world.Rules.drop = 1;
    tide_world_tick(&world);
    world.Rules.drop = 0;
    TIDE_REQUIRE(!tide_has_Chunk(&world, one) && tide_has_Chunk(&verified, one));
    frame_on(&world, &verified);
    TIDE_CHECK(remeshing() == 1 && tide_has_Chunk_local(&local, one) && local.Stats.forgotten == 0);
    complete();
    frame_on(&world, &verified);
    TIDE_CHECK(remeshing() == 0);
    Chunk_local a = tide_get_Chunk_local(&local, one);
    TIDE_CHECK(a.steps == 1 && a.meshing && a.drawn == 0);

    // The server says it wasn't destroyed: what was kept for it is back, and a new task makes its faces
    tide_world_tick(&verified);
    tide_world_copy(&world, &verified);
    frame_on(&world, &verified);
    TIDE_CHECK(remeshing() == 1 && tide_get_Chunk_local(&local, one).steps == 2);
    complete();
    frame_on(&world, &verified);
    a = tide_get_Chunk_local(&local, one);
    TIDE_CHECK(remeshing() == 0 && a.steps == 12 && !a.meshing && a.drawn == 1 && local.Stats.forgotten == 0);
    tide_world_free(&verified);
    finish();
}

// The match is over: its entities' tasks end with it, even when the next
// match has entities with the same handles, whose views start their own
TIDE_TEST(tasks_local_fields_end_with_their_match)
{
    start();
    const tide_entity one = chunk_of(&world, 1);
    frame();
    TIDE_CHECK(remeshing() == 2);
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, NULL);
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f); // The next match, by the next frame
    TIDE_REQUIRE(tide_entity_equal(chunk_of(&world, 1), one));
    frame();
    TIDE_CHECK(local.Stats.forgotten == 2);
    TIDE_CHECK(remeshing() == 2); // The new match's, which its views started
    TIDE_CHECK(tide_get_Chunk_local(&local, one).steps == 1);
    // ...and a task of the local world's that takes the match's singleton: the old one ended, the new one waits
    TIDE_CHECK(local.tide_tasks_function_Paint.count == 1 && local.Arena_local.painting && local.Arena_local.painted == 0);
    complete();
    frame();
    TIDE_CHECK(remeshing() == 0);
    TIDE_CHECK(tide_get_Chunk_local(&local, one).steps == 11 && tide_get_Chunk_local(&local, one).drawn == 1);
    frame();
    TIDE_CHECK(local.tide_tasks_function_Paint.count == 0 && local.Arena_local.painted == 4);
    finish();
}

// The local state as bytes, as hot reloading carries it: the waiting tasks go
// on with their entities' local fields, which went too, after their workers
// start again
TIDE_TEST(tasks_local_fields_in_the_local_states_bytes)
{
    start();
    const tide_entity one = chunk_of(&world, 1), two = chunk_of(&world, 2);
    frame();
    TIDE_CHECK(remeshing() == 2 && pending != NULL);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(tide_local_unpack(&local, bytes, size));
    free(bytes);
    TIDE_CHECK(pending == NULL); // The old workers were drained
    tide_text_use(NULL, &local.heap);
    TIDE_CHECK(tide_get_Chunk_local(&local, one).steps == 1 && tide_get_Chunk_local(&local, one).meshing);
    frame();
    TIDE_CHECK(pending != NULL && remeshing() == 2);
    complete();
    frame();
    TIDE_CHECK(remeshing() == 0);
    TIDE_CHECK(tide_get_Chunk_local(&local, one).steps == 11 && tide_get_Chunk_local(&local, two).drawn == 1);
    TIDE_CHECK(text_is(tide_get_Chunk_local(&local, two).label, "chunk 2 at 1, 6 faces"));
    finish();
}
