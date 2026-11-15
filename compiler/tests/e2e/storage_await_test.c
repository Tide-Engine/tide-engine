#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/storage.h"
#include "tide/time.h"
#include "tide_test.h"

// Match tasks that await the server's store (`await Storage.Load(key)`, `await
// Storage.List(prefix, after)`): the request is the task's frame's, only the
// server's own run of a tick asks, and the answer is input to a later tick, in
// which the task goes on on every machine. Here against files kept in memory,
// carried out at once or only when the test says.

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

static bool holds(const char *path)
{
    const size_t n = strlen(path);
    for (uint32_t i = 0; i < file_count; i++) {
        if (strncmp(files[i].path, path, n) == 0 && files[i].path[n] == '/') return true;
    }
    return false;
}

static bool io_exists(void *user, const char *path, const bool directory)
{
    (void)user;
    return directory ? holds(path) : find_file(path) != NULL;
}

static tide_io_error io_create(void *user, const char *path, const bool directory)
{
    (void)user;
    (void)path;
    (void)directory;
    return TIDE_IO_NONE;
}

// A key whose name says "locked" can't be read
static tide_io_error io_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
    reads++;
    if (strstr(path, "locked")) return TIDE_IO_ACCESS_DENIED;
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

// One level of a folder, as a provider lists it; a folder named "locked" can't be listed
static tide_io_error io_list(void *user, const char *path, const bool directories, const bool recursive,
                             const tide_io_entry_fn entry, void *entry_user)
{
    (void)user;
    (void)recursive;
    if (strstr(path, "locked")) return TIDE_IO_ACCESS_DENIED;
    if (!holds(path)) return TIDE_IO_NOT_FOUND;
    const size_t n = strlen(path);
    char (*given)[512] = NULL;
    uint32_t given_count = 0, given_room = 0;
    for (uint32_t i = 0; i < file_count; i++) {
        const char *f = files[i].path;
        if (strncmp(f, path, n) != 0 || f[n] != '/') continue;
        const char *rest = f + n + 1;
        const char *slash = strchr(rest, '/');
        if (!directories) {
            if (!slash) entry(entry_user, f);
            continue;
        }
        if (!slash) continue;
        char folder[512];
        snprintf(folder, sizeof folder, "%.*s", (int)(slash - f), f);
        bool seen = false;
        for (uint32_t k = 0; k < given_count && !seen; k++) seen = strcmp(given[k], folder) == 0;
        if (seen) continue;
        if (given_count == given_room) {
            given_room = given_room ? given_room * 2u : 64u;
            given = realloc(given, given_room * sizeof *given);
        }
        strcpy(given[given_count++], folder);
        entry(entry_user, folder);
    }
    free(given);
    return TIDE_IO_NONE;
}

static const tide_io io = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write, .move = io_move,
                           .remove = io_remove, .list = io_list};

// The same files, carried out when the test runs what was posted
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

static const tide_io deferred = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write, .move = io_move,
                                 .remove = io_remove, .list = io_list, .post = io_post, .settle = io_settle};

// A folder the host can't list, which a listing of it then fails
static void put_locked(void)
{
    put_file("saved/locked/k.bin", (const uint8_t *)"x", 1);
}

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

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static uint32_t count_alive(const tide_world *w, bool (*has)(const tide_world *, tide_entity))
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        count += tide_entity_alive(&w->entities, e) && has(w, e);
    }
    return count;
}

static bool has_crate(const tide_world *w, const tide_entity e) { return tide_has_Crate(w, e); }
static bool has_holder(const tide_world *w, const tide_entity e) { return tide_has_Holder(w, e); }

static int32_t mix(const int32_t hash, const char *key)
{
    uint32_t h = (uint32_t)hash;
    for (const char *p = key; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return (int32_t)((h ^ 47u) * 16777619u);
}

static int compare_text(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// The keys the listings have, page/ then list/ (the order the tasks' first pages come in: the order they were asked)
static int32_t expected_keys_hash(void)
{
    static char page_keys[1100][16];
    const char *sorted[1100];
    for (int i = 0; i < 1100; i++) {
        snprintf(page_keys[i], sizeof page_keys[i], "page/%d", i);
        sorted[i] = page_keys[i];
    }
    qsort(sorted, 1100, sizeof sorted[0], compare_text);
    // page/'s first page and list/'s are answered in one tick, in the order asked; page/'s second after
    int32_t hash = 0;
    for (int i = 0; i < 1024; i++) hash = mix(hash, sorted[i]);
    hash = mix(mix(mix(hash, "list/a"), "list/b"), "list/c/d");
    for (int i = 1024; i < 1100; i++) hash = mix(hash, sorted[i]);
    return hash;
}

// What every machine's world has once the tasks are done
static void check_world(const tide_world *w)
{
    int big = 0;
    for (int i = 0; i < 5000; i++) big += (uint8_t)i;
    const Vault *v = &w->Vault;
    TIDE_CHECK(v->found == 4);   // boxes/7 twice, big, same
    TIDE_CHECK(v->missing == 1); // boxes/none: IOError.NotFound
    TIDE_CHECK(v->failed == 1);  // locked: IOError.Failed, not the same as nothing there
    TIDE_CHECK(v->sum == 2 * (1 + 2 + 3) + big + 50 + 51);
    TIDE_CHECK(v->same == 50 + 51); // Saved in the tick that asked for it, before it
    TIDE_CHECK(v->finished == 6);
    TIDE_CHECK(v->crates == 4 && count_alive(w, has_crate) == 4);
    TIDE_CHECK(v->pages == 3);   // page/ in two, list/ in one
    TIDE_CHECK(v->keys == 1103 && v->keysHash == expected_keys_hash());
    TIDE_CHECK(v->listFailed == 1); // locked/
    // 2 bytes of deep/a, 5 of deep/b (which the task saved before it asked), deep/none not found, and deep/'s 2 keys
    TIDE_CHECK(v->deep == 2 + 5 + 200 && v->deepFailed == 1);
    TIDE_CHECK(v->held == 0 && count_alive(w, has_holder) == 0); // Gone before its answer came
    TIDE_CHECK(v->handled == 3 + 10); // boxes/7's bytes, and its one key listed
}

// Sessions are made the same way each time: a host with `files_io`, and a guest without files if `guest`
static void play(const tide_io *files_io, const int later)
{
    tide_loopback *net = tide_loopback_create(3);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.005, .loss = 0.03});
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = files_io});
    tide_session *guest = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    double t = 0.0;
    tide_session_start(host, NULL, t);
    TIDE_CHECK(tide_session_open(host, tide_loopback_endpoint(net, 1)));
    tide_session_join(guest, tide_loopback_endpoint(net, 2), tide_loopback_address(1), t);
    for (int frame = 0; frame < 300; frame++) {
        t += 1.0 / 60.0;
        tide_loopback_set_time(net, t);
        tide_session_update(host, t);
        tide_session_update(guest, t);
        if (later && frame % later == later - 1) run_posted();
    }
    TIDE_CHECK(tide_session_status_of(host).client.state == TIDE_SESSION_CONNECTED);
    const tide_session_status joined = tide_session_status_of(guest);
    TIDE_CHECK(joined.client.state == TIDE_SESSION_CONNECTED && joined.client.resyncs == 0);
    const tide_world *server = tide_session_server_world(host);
    TIDE_REQUIRE(server != NULL && server->Time.tick > 60);
    check_world(server);
    // The guest, which has no store: the answers came with its frames, and its tasks went on in the same ticks
    const tide_view_worlds seen = tide_session_view(guest);
    TIDE_REQUIRE(seen.verified != NULL);
    check_world(seen.verified);
    tide_session_destroy(guest);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
}

TIDE_TEST(storage_await_answers_match_tasks_the_same_on_every_machine)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL && tide_game_api.stored_again != NULL);
    forget_files();
    put_locked();
    play(&io, 0);
    forget_files();
}

// Carried out later than the tick after: each task goes on in the tick its answer is given to, on every machine
TIDE_TEST(storage_await_answers_carried_out_later_are_the_same_on_every_machine)
{
    forget_files();
    put_locked();
    play(&deferred, 5);
    TIDE_CHECK(posted_count == 0);
    forget_files();
}

// The match alone, until its tasks wait for answers nothing has carried out yet
static tide_session *waiting(double *t)
{
    tide_session *alone = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = &deferred});
    tide_session_start(alone, NULL, *t);
    for (int frame = 0; frame < 60; frame++) {
        *t += 1.0 / 60.0;
        tide_session_update(alone, *t);
        const tide_world *server = tide_session_server_world(alone);
        if (server && server->Time.tick >= 8) break; // Past tick 6, where Deep asks after its wait
    }
    const tide_world *server = tide_session_server_world(alone);
    TIDE_CHECK(server != NULL && server->Time.tick >= 8 && posted_count >= 3);
    if (server) TIDE_CHECK(server->Vault.finished == 0 && server->Vault.pages == 0 && server->Vault.deep == 0); // Nothing answered
    return alone;
}

// A world packed while its tasks wait, and unpacked, is the same world: the same hash, and its tasks get their answers.
// Rewinding to it drops the answers on their way, and asks again what its tasks wait for, once each
TIDE_TEST(storage_await_waits_across_a_pack_and_a_rewind)
{
    forget_files();
    put_locked();
    double t = 0.0;
    tide_session *alone = waiting(&t);
    const tide_world *server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    const uint32_t size = tide_game_api.pack_world(server, NULL, 0);
    TIDE_REQUIRE(size > 0);
    uint8_t *packed = malloc(size);
    TIDE_REQUIRE(packed != NULL && tide_game_api.pack_world(server, packed, size) == size);
    tide_world *then = calloc(1, tide_game_api.world_size);
    TIDE_REQUIRE(then != NULL);
    TIDE_REQUIRE(tide_game_api.unpack_world(then, packed, size));
    free(packed);
    TIDE_CHECK(tide_game_api.hash_world(then) == tide_game_api.hash_world(server));
    // More ticks with nothing answered, then back to the packed world
    for (int frame = 0; frame < 10; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
    }
    TIDE_REQUIRE(tide_session_rewind(alone, then));
    const uint32_t before = reads;
    run_posted(); // What the old ticks asked is carried out now, its answers nobody's
    for (int frame = 0; frame < 300; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        if (frame % 3 == 2) run_posted();
    }
    server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    check_world(server); // Each task answered once, from what its frame kept of its request
    TIDE_CHECK(reads > before);
    tide_session_destroy(alone);
    tide_game_api.free_world(then);
    free(then);
    forget_files();
}

// Hot reloading while the tasks wait: new code of the same layout, then another layout carried over by copying the
// world. The tables swapped out are zeroed, so anything still using them would fail. The batches on their way are
// taken by the build that runs when they're ready, and the tasks, carried over, get their answers once each
static tide_game builds[3];
static int migrations;

static bool copy_over(void *user, const void *from, void *to)
{
    (void)user;
    migrations++;
    tide_world_copy(to, from);
    return true;
}

TIDE_TEST(storage_await_waits_across_a_hot_reload)
{
    forget_files();
    put_locked();
    for (int i = 0; i < 3; i++) builds[i] = tide_game_api;
    builds[2].hash ^= 1u; // Another layout
    tide_session *alone = tide_session_create(&(tide_session_desc){.game = &builds[0], .tick_rate = 60, .sample = sample, .io = &deferred});
    TIDE_REQUIRE(alone != NULL);
    double t = 0.0;
    tide_session_start(alone, NULL, t);
    for (int frame = 0; frame < 60; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        const tide_world *server = tide_session_server_world(alone);
        if (server && server->Time.tick >= 8) break;
    }
    const tide_world *server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL && server->Time.tick >= 8 && posted_count >= 3 && server->Vault.finished == 0);
    TIDE_REQUIRE(tide_session_can_reload(alone));
    tide_session_set_game(alone, &builds[1]);
    memset(&builds[0], 0, sizeof builds[0]);
    run_posted(); // Tick 2's saves and tick 4's asks, for the new build to take
    for (int frame = 0; frame < 3; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
    }
    migrations = 0;
    TIDE_REQUIRE(tide_session_migrate(alone, &builds[2], copy_over, NULL));
    TIDE_CHECK(migrations >= 1);
    memset(&builds[1], 0, sizeof builds[1]);
    for (int frame = 0; frame < 300; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        if (frame % 3 == 2) run_posted();
    }
    server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    check_world(server);
    tide_session_destroy(alone);
    forget_files();
}

// An answer whose task is gone (its entity went, or nothing ever waited for it) changes nothing
TIDE_TEST(storage_await_answer_for_no_task_changes_nothing)
{
    forget_files();
    double t = 0.0;
    tide_session *alone = waiting(&t);
    const tide_world *server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    tide_world *copy = calloc(1, tide_game_api.world_size);
    TIDE_REQUIRE(copy != NULL);
    tide_game_api.copy_world(copy, server);
    const uint64_t hash = tide_game_api.hash_world(copy);
    static const uint8_t bytes[3] = {1, 2, 3};
    const tide_stored answer = {.kind = TIDE_STORAGE_LOAD, .to = TIDE_STORAGE_TO_TASK, .outcome = TIDE_STORED_FOUND,
                                .target = {.birth = 12345, .lineage = 678}, .number = 1, .key = "boxes/7", .key_size = 7,
                                .data = bytes, .data_size = 3};
    uint32_t size = 0;
    uint8_t *message = tide_stored_message(&answer, &size);
    TIDE_REQUIRE(message != NULL);
    TIDE_CHECK(!tide_game_api.stored(copy, message, size));
    TIDE_CHECK(tide_game_api.hash_world(copy) == hash);
    tide_stored listing = answer; // A listing named as no task's either
    listing.kind = TIDE_STORAGE_LIST;
    listing.data = NULL;
    listing.data_size = 0;
    free(message);
    message = tide_stored_message(&listing, &size);
    TIDE_REQUIRE(message != NULL);
    TIDE_CHECK(!tide_game_api.stored(copy, message, size));
    TIDE_CHECK(tide_game_api.hash_world(copy) == hash);
    free(message);
    tide_game_api.free_world(copy);
    free(copy);
    tide_session_destroy(alone);
    forget_files();
}

// With nowhere to keep it, every awaited load finds nothing and every listing no keys: the tasks go on
TIDE_TEST(storage_await_without_files_finds_nothing)
{
    forget_files();
    tide_session *alone = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    double t = 0.0;
    tide_session_start(alone, NULL, t);
    for (int frame = 0; frame < 120; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
    }
    const tide_world *server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    const Vault *v = &server->Vault;
    TIDE_CHECK(v->found == 0 && v->missing == 6 && v->failed == 0 && v->finished == 6 && v->crates == 0);
    TIDE_CHECK(v->pages == 3 && v->keys == 0 && v->listFailed == 0);
    TIDE_CHECK(v->deep == -100 - 1000 && v->deepFailed == 1 && v->handled == 0);
    tide_session_destroy(alone);
}
