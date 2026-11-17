#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/storage.h"
#include "tide_test.h"

// Match tasks that await the store in a game that hides state: the answers never go in players' frames, the server's
// run of each task goes on with its answer, and a player has what it changed through the state it may have. Files
// kept in memory, carried out when the test says.

typedef struct kept_file {
    char path[128];
    uint8_t data[64];
    uint32_t size;
} kept_file;

static kept_file files[64];
static uint32_t file_count;

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
    if (!directory) return find_file(path) != NULL;
    const size_t n = strlen(path);
    for (uint32_t i = 0; i < file_count; i++) {
        if (strncmp(files[i].path, path, n) == 0 && files[i].path[n] == '/') return true;
    }
    return false;
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
    const kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    *data = malloc(f->size ? f->size : 1u);
    if (!*data) return TIDE_IO_FAILED;
    memcpy(*data, f->data, f->size);
    *size = f->size;
    return TIDE_IO_NONE;
}

static tide_io_error io_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    (void)user;
    (void)append;
    kept_file *f = find_file(path);
    if (!f) {
        if (file_count == sizeof files / sizeof files[0] || strlen(path) >= sizeof f->path) return TIDE_IO_FAILED;
        f = &files[file_count++];
        snprintf(f->path, sizeof f->path, "%s", path);
    }
    if (size > sizeof f->data) return TIDE_IO_FAILED;
    if (size) memcpy(f->data, data, size);
    f->size = size;
    return TIDE_IO_NONE;
}

static tide_io_error io_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    (void)user;
    (void)directory;
    (void)recursive;
    kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    *f = files[--file_count];
    return TIDE_IO_NONE;
}

static tide_io_error io_move(void *user, const char *source, const char *destination, const bool overwrite, const bool directory)
{
    (void)directory;
    const kept_file *from = find_file(source);
    if (!from) return TIDE_IO_NOT_FOUND;
    if (find_file(destination) && !overwrite) return TIDE_IO_ALREADY_EXISTS;
    const kept_file moved = *from;
    const tide_io_error error = io_write(user, destination, moved.data, moved.size, false);
    if (!error) (void)io_remove(user, source, false, false);
    return error;
}

// One level of a folder
static tide_io_error io_list(void *user, const char *path, const bool directories, const bool recursive,
                             const tide_io_entry_fn entry, void *entry_user)
{
    (void)user;
    (void)recursive;
    const size_t n = strlen(path);
    for (uint32_t i = 0; i < file_count; i++) {
        const char *f = files[i].path;
        if (strncmp(f, path, n) != 0 || f[n] != '/') continue;
        const char *slash = strchr(f + n + 1, '/');
        if (!directories && !slash) entry(entry_user, f);
        if (directories && slash) { // Each folder once: at the first file in it
            bool first = true;
            for (uint32_t k = 0; k < i && first; k++) {
                first = strncmp(files[k].path, f, (size_t)(slash - f + 1)) != 0;
            }
            if (!first) continue;
            char folder[128];
            snprintf(folder, sizeof folder, "%.*s", (int)(slash - f), f);
            entry(entry_user, folder);
        }
    }
    return TIDE_IO_NONE;
}

typedef struct posted_work {
    void (*work)(void *context);
    void *context;
} posted_work;

static posted_work posted[1024];
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

static const tide_io deferred = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write, .move = io_move,
                                 .remove = io_remove, .list = io_list, .post = io_post, .settle = io_settle};

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static void check_vault(const Vault *v)
{
    TIDE_CHECK(v->found == 2 && v->missing == 1);
    TIDE_CHECK(v->sum == 1 + 2 + 3 + 10 + 11);
    TIDE_CHECK(v->listed == 2);
    TIDE_CHECK(v->refused == 1);
}

TIDE_TEST(storage_await_in_a_game_that_hides_state_reaches_players_through_its_state)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL && tide_game_api.stored_again != NULL && tide_game_api.client_state != NULL);
    file_count = posted_count = 0;
    tide_loopback *net = tide_loopback_create(3);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.005, .loss = 0.03});
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = &deferred});
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
        if (frame % 4 == 3) run_posted(); // Answers a few ticks later, as a thread of the files' own gives them
    }
    const tide_world *server = tide_session_server_world(host);
    TIDE_REQUIRE(server != NULL && server->Time.tick > 60);
    check_vault(&server->Vault);
    TIDE_CHECK(find_file("Tide/store/hidden/a.bin") != NULL && find_file("Tide/store/hidden/b.bin") != NULL);
    // The guest, whose own runs of the tasks never went on: what the server's did is in the state it has
    const tide_session_status joined = tide_session_status_of(guest);
    TIDE_CHECK(joined.client.state == TIDE_SESSION_CONNECTED);
    const tide_view_worlds seen = tide_session_view(guest);
    TIDE_REQUIRE(seen.verified != NULL);
    const tide_world *verified = seen.verified;
    check_vault(&verified->Vault);
    tide_session_destroy(guest);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
}
