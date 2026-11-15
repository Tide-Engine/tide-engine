#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/shards.h"
#include "tide/storage.h"
#include "tide_test.h"

// e2e/storage_await_shards.tide on the engine's orchestrator: two regions split at x = 0, sharing one store whose
// requests are carried out only when the test says. Each mover's task asks for its key on the left, and the mover
// crosses to the right before anything is answered: its task asks again there, and gets its answer on the right.

typedef struct kept_file {
    char *path;
    uint8_t *data;
    uint32_t size;
} kept_file;

static kept_file *files;
static uint32_t file_count, file_room, reads;

static kept_file *find_file(const char *path)
{
    for (uint32_t i = 0; i < file_count; i++) {
        if (strcmp(files[i].path, path) == 0) return &files[i];
    }
    return NULL;
}

static bool io_exists(void *user, const char *path, const bool directory)
{
    (void)user;
    (void)directory;
    return find_file(path) != NULL;
}

static tide_io_error io_create(void *user, const char *path, const bool directory)
{
    (void)user;
    (void)path;
    (void)directory;
    return TIDE_IO_NONE;
}

static tide_io_error io_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
    reads++;
    const kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    *data = malloc(f->size ? f->size : 1u);
    memcpy(*data, f->data, f->size);
    *size = f->size;
    return TIDE_IO_NONE;
}

static void put_file(const char *path, const uint8_t *data, const uint32_t size)
{
    kept_file *f = find_file(path);
    if (!f) {
        if (file_count == file_room) {
            file_room = file_room ? file_room * 2u : 64u;
            files = realloc(files, file_room * sizeof *files);
        }
        f = &files[file_count++];
        f->path = malloc(strlen(path) + 1u);
        strcpy(f->path, path);
        f->data = NULL;
    }
    free(f->data);
    f->data = malloc(size ? size : 1u);
    if (size) memcpy(f->data, data, size);
    f->size = size;
}

static tide_io_error io_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    (void)user;
    (void)append;
    put_file(path, data, size);
    return TIDE_IO_NONE;
}

static void drop_file(kept_file *f)
{
    free(f->data);
    free(f->path);
    *f = files[--file_count];
}

static tide_io_error io_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    (void)user;
    (void)directory;
    (void)recursive;
    kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    drop_file(f);
    return TIDE_IO_NONE;
}

static tide_io_error io_move(void *user, const char *source, const char *destination, const bool overwrite, const bool directory)
{
    (void)user;
    (void)directory;
    kept_file *from = find_file(source);
    if (!from) return TIDE_IO_NOT_FOUND;
    if (find_file(destination) && !overwrite) return TIDE_IO_ALREADY_EXISTS;
    put_file(destination, from->data, from->size);
    drop_file(find_file(source));
    return TIDE_IO_NONE;
}

typedef struct posted_work {
    void (*work)(void *context);
    void *context;
} posted_work;

static posted_work posted[4096];
static uint32_t posted_count;

static bool io_post(void *user, void (*work)(void *context), void *context)
{
    (void)user;
    if (posted_count == sizeof posted / sizeof posted[0]) return false;
    posted[posted_count++] = (posted_work){work, context};
    return true;
}

static void run_posted(void)
{
    for (uint32_t i = 0; i < posted_count; i++) posted[i].work(posted[i].context);
    posted_count = 0;
}

static void io_settle(void *user, const void *context) // Everything posted: at least the work it waits for
{
    (void)user;
    (void)context;
    run_posted();
}

static const tide_io deferred = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write,
                                 .move = io_move, .remove = io_remove, .post = io_post, .settle = io_settle};

static void forget_files(void)
{
    for (uint32_t i = 0; i < file_count; i++) {
        free(files[i].data);
        free(files[i].path);
    }
    free(files);
    files = NULL;
    file_count = file_room = reads = 0;
    posted_count = 0;
}

static tide_shards *shards;
static double now;

static void step(void)
{
    now += 1.0 / 120.0;
    tide_shards_update(shards, now);
}

typedef struct movers {
    int owned[8], got[8], tries[8];
    float x[8];
} movers;

// Every shard's own movers (not the ghosts of its neighbours'), by id
static movers gather(void)
{
    movers m;
    memset(&m, 0, sizeof m);
    for (uint32_t s = 0; s < tide_shards_count(shards); s++) {
        const tide_world *w = tide_shards_world(shards, s);
        if (!w) continue;
        for (uint32_t i = 0; i < w->entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&w->entities, i);
            if (!tide_entity_alive(&w->entities, e) || !tide_has_Mover(w, e) || tide_has_tide_ghost(w, e)) continue;
            const Mover mover = tide_get_Mover(w, e);
            if (mover.id < 0 || mover.id >= 8) continue;
            m.owned[mover.id]++;
            m.got[mover.id] = mover.got;
            m.tries[mover.id] = mover.tries;
            m.x[mover.id] = mover.position.x;
        }
    }
    return m;
}

TIDE_TEST(storage_await_tasks_ask_again_on_the_shard_they_cross_to)
{
    const tide_shards_desc d = {
        .game = &tide_game_api,
        .tick_rate = 60,
        .io = &deferred,
        .world = {{-40.0, -10.0, 0.0}, {40.0, 10.0, 0.0}},
        .split = {2, 1, 1},
    };
    now = 0;
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    // Until every mover is well past the border, nothing carried out: their tasks asked on the left and wait
    movers m = gather();
    for (int tries = 0; tries < 2000; tries++) {
        step();
        m = gather();
        bool past = true;
        for (int i = 0; i < 8; i++) past &= m.owned[i] == 1 && m.x[i] > 6.0f;
        if (past) break;
    }
    for (int i = 0; i < 8; i++) {
        TIDE_CHECK(m.owned[i] == 1 && m.x[i] > 6.0f);
        TIDE_CHECK(m.got[i] == 0 && m.tries[i] == 0); // Nothing answered yet
    }
    TIDE_CHECK(posted_count > 0);
    // Then carried out as they come: the left's answers find no task, the right's do
    for (int frame = 0; frame < 240; frame++) {
        step();
        if (frame % 4 == 3) run_posted();
    }
    m = gather();
    for (int i = 0; i < 8; i++) {
        TIDE_CHECK(m.owned[i] == 1);
        TIDE_CHECK(m.got[i] == i + 1 && m.tries[i] == 1); // Answered once, where the mover is
    }
    TIDE_CHECK(reads >= 16); // Asked on the left, then on the right
    // What the root's start saved, before its server was made: carried out with its first tick's requests
    const kept_file *started = find_file("saved/started.bin");
    TIDE_CHECK(started != NULL && started->size == 4);
    tide_shards_destroy(shards);
    shards = NULL;
    forget_files();
}

// Until every mover's task has asked and nothing is answered: a world mid-wait
static void until_waiting(void)
{
    for (int tries = 0; tries < 200; tries++) {
        step();
        bool asked = tide_shards_count(shards) >= 2;
        for (uint32_t s = 0; asked && s < tide_shards_count(shards); s++) {
            const tide_world *w = tide_shards_world(shards, s);
            asked = !w || w->Time.tick >= 4; // Asked at 3, and none has reached the border
        }
        if (asked) break;
    }
    const movers m = gather();
    for (int i = 0; i < 8; i++) TIDE_CHECK(m.owned[i] == 1 && m.got[i] == 0 && m.tries[i] == 0);
    TIDE_CHECK(posted_count > 0);
}

static void until_answered(void)
{
    for (int frame = 0; frame < 480; frame++) {
        step();
        if (frame % 4 == 3) run_posted();
    }
    const movers m = gather();
    for (int i = 0; i < 8; i++) TIDE_CHECK(m.owned[i] == 1 && m.got[i] == i + 1 && m.tries[i] == 1);
}

static const tide_shards_desc split_desc = {
    .game = &tide_game_api,
    .tick_rate = 60,
    .io = &deferred,
    .world = {{-40.0, -10.0, 0.0}, {40.0, 10.0, 0.0}},
    .split = {2, 1, 1},
};

// A world taken up again from a checkpoint saved while its tasks wait: its shards' servers, made from the
// checkpoint's worlds, ask again what each waits for, and each task is answered once
TIDE_TEST(storage_await_shards_from_a_checkpoint_ask_again)
{
    now = 0;
    shards = tide_shards_create(&split_desc, now);
    TIDE_REQUIRE(shards != NULL);
    until_waiting();
    uint32_t size = 0;
    uint8_t *saved = tide_shards_save(shards, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    tide_shards_destroy(shards); // Its saves carried out; its loads' answers nobody's
    TIDE_CHECK(posted_count == 0 && find_file("saved/mover/7.bin") != NULL);
    const uint32_t before = reads;
    shards = tide_shards_load(&split_desc, saved, size, now);
    free(saved);
    TIDE_REQUIRE(shards != NULL);
    // The last mover is answered on the left, where the checkpoint had it, long before it crosses (which asks again
    // on arrival anyway)
    for (int frame = 0; frame < 12; frame++) {
        step();
        run_posted();
    }
    const movers early = gather();
    TIDE_CHECK(early.owned[7] == 1 && early.got[7] == 8 && early.tries[7] == 1 && early.x[7] < 0.0f);
    until_answered();
    TIDE_CHECK(reads - before >= 8u); // Asked again where they are
    tide_shards_destroy(shards);
    shards = NULL;
    forget_files();
}

// The same from a checkpoint and the journal since, in whose ticks the answers came: every shard made again as it
// was, its tasks answered as they were, and none of them asks again
static uint8_t **records[8];
static uint32_t *sizes[8];
static uint32_t record_counts[8], record_rooms[8];

static void keep_record(void *user, const uint32_t shard, const uint32_t tick, const uint8_t *record, const uint32_t size)
{
    (void)user;
    (void)tick;
    if (shard >= 8u) return;
    if (record_counts[shard] == record_rooms[shard]) {
        record_rooms[shard] = record_rooms[shard] ? record_rooms[shard] * 2u : 256u;
        records[shard] = realloc(records[shard], record_rooms[shard] * sizeof *records[shard]);
        sizes[shard] = realloc(sizes[shard], record_rooms[shard] * sizeof *sizes[shard]);
        TIDE_REQUIRE(records[shard] && sizes[shard]);
    }
    uint8_t *copy = malloc(size ? size : 1u);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, record, size);
    records[shard][record_counts[shard]] = copy;
    sizes[shard][record_counts[shard]++] = size;
}

static bool give_record(void *user, const uint32_t shard, const uint32_t index, const uint8_t **record, uint32_t *size)
{
    (void)user;
    if (shard >= 8u || index >= record_counts[shard]) return false;
    *record = records[shard][index];
    *size = sizes[shard][index];
    return true;
}

TIDE_TEST(storage_await_shards_recovered_from_a_journal_have_their_answers)
{
    now = 0;
    tide_shards_desc d = split_desc;
    d.journal = keep_record;
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    until_waiting();
    uint32_t size = 0;
    uint8_t *saved = tide_shards_save(shards, &size);
    TIDE_REQUIRE(saved != NULL && size > 0);
    until_answered(); // In the journal's ticks
    const uint32_t count = tide_shards_count(shards);
    TIDE_REQUIRE(count <= 8u);
    uint64_t hashes[8] = {0};
    for (uint32_t s = 0; s < count; s++) {
        if (tide_shards_world(shards, s)) hashes[s] = tide_game_api.hash_world(tide_shards_world(shards, s));
    }
    tide_shards_destroy(shards);
    TIDE_CHECK(posted_count == 0);
    const uint32_t before = reads;
    shards = tide_shards_recover(&d, saved, size, give_record, NULL, now);
    free(saved);
    TIDE_REQUIRE(shards != NULL);
    TIDE_CHECK(tide_shards_count(shards) == count);
    for (uint32_t s = 0; s < count; s++) {
        if (tide_shards_world(shards, s)) TIDE_CHECK(tide_game_api.hash_world(tide_shards_world(shards, s)) == hashes[s]);
    }
    const movers m = gather();
    for (int i = 0; i < 8; i++) TIDE_CHECK(m.owned[i] == 1 && m.got[i] == i + 1 && m.tries[i] == 1);
    for (int frame = 0; frame < 60; frame++) {
        step();
        run_posted();
    }
    TIDE_CHECK(reads == before); // Nothing waits: nothing asked again
    tide_shards_destroy(shards);
    shards = NULL;
    for (uint32_t s = 0; s < 8u; s++) {
        for (uint32_t i = 0; i < record_counts[s]; i++) free(records[s][i]);
        free(records[s]);
        free(sizes[s]);
        records[s] = NULL;
        sizes[s] = NULL;
        record_counts[s] = record_rooms[s] = 0;
    }
    forget_files();
}
