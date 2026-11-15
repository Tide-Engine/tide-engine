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

static void io_settle(void *user)
{
    (void)user;
    run_posted();
}

static const tide_io deferred = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write,
                                 .move = io_move, .remove = io_remove, .post = io_post, .settle = io_settle};

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
    tide_shards_destroy(shards);
    shards = NULL;
    for (uint32_t i = 0; i < file_count; i++) {
        free(files[i].data);
        free(files[i].path);
    }
    free(files);
    files = NULL;
    file_count = file_room = reads = 0;
}
