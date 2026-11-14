#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/storage.h"
#include "tide_test.h"

// The server's store (Storage.Save, Load and Delete in match code,
// tide/storage.h): what a tick asks is carried out by the machine that runs
// the match, against the files its host gives it (here, a few kept in
// memory), and what a load finds comes back as a Loaded event, in the same
// tick on every machine.

typedef struct kept_file {
    char path[512];
    uint8_t *data;
    uint32_t size;
} kept_file;

static kept_file files[16];
static uint32_t file_count, writes, removes, folders;

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
    return !directory && find_file(path) != NULL;
}

static tide_io_error io_create(void *user, const char *path, const bool directory)
{
    (void)user;
    (void)path;
    folders += directory;
    return TIDE_IO_NONE;
}

static tide_io_error io_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
    const kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    *data = malloc(f->size ? f->size : 1u);
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
        strcpy(f->path, path);
        f->data = NULL;
    }
    free(f->data);
    f->data = malloc(size ? size : 1u);
    memcpy(f->data, data, size);
    f->size = size;
    writes++;
    return TIDE_IO_NONE;
}

static tide_io_error io_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    (void)user;
    (void)directory;
    (void)recursive;
    kept_file *f = find_file(path);
    if (!f) return TIDE_IO_NOT_FOUND;
    free(f->data);
    *f = files[--file_count];
    removes++;
    return TIDE_IO_NONE;
}

static const tide_io io = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write, .remove = io_remove};

static void forget_files(void)
{
    for (uint32_t i = 0; i < file_count; i++) free(files[i].data);
    file_count = writes = removes = folders = 0;
}

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){0};
}

static uint32_t crates(const tide_world *w)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        count += tide_entity_alive(&w->entities, e) && tide_has_Crate(w, e);
    }
    return count;
}

// A host with the files and a guest without: what the match saved is in the
// files under its keys, what it loaded came back once each, and the guest's
// world is the host's, tick for tick
TIDE_TEST(storage_saves_loads_and_deletes_the_same_on_every_machine)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL);
    forget_files();
    tide_loopback *net = tide_loopback_create(3);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.005, .loss = 0.03});
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = &io});
    tide_session *guest = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample});
    double t = 0.0;
    tide_session_start(host, NULL, t);
    TIDE_CHECK(tide_session_open(host, tide_loopback_endpoint(net, 1)));
    tide_session_join(guest, tide_loopback_endpoint(net, 2), tide_loopback_address(1), t);
    for (int frame = 0; frame < 240; frame++) {
        t += 1.0 / 60.0;
        tide_loopback_set_time(net, t);
        tide_session_update(host, t);
        tide_session_update(guest, t);
    }
    TIDE_CHECK(tide_session_status_of(host).client.state == TIDE_SESSION_CONNECTED);
    const tide_session_status joined = tide_session_status_of(guest);
    TIDE_CHECK(joined.client.state == TIDE_SESSION_CONNECTED && joined.client.resyncs == 0);
    const tide_world *server = tide_session_server_world(host);
    TIDE_REQUIRE(server != NULL && server->Time.tick > 60);
    // boxes/7 and "a b/../c" at tick 8 (the second load of boxes/7 was the first's), big at 12, boxes/7 again at 16
    TIDE_CHECK(server->Vault.found == 4);
    TIDE_CHECK(server->Vault.missing == 2); // boxes/none, and gone once it was deleted
    int big = 0;
    for (int i = 0; i < 5000; i++) big += (uint8_t)i;
    TIDE_CHECK(server->Vault.sum == (1 + 2 + 3) + (10 + 11) + big + 4);
    TIDE_CHECK(server->Vault.crates == 4 && crates(server) == 4);

    // The files: each key's own, none outside the folder, the last save's bytes
    TIDE_CHECK(file_count == 3 && writes == 5 && removes == 1 && folders >= 1);
    const kept_file *box = find_file("saved/boxes/7.bin");
    TIDE_CHECK(box && box->size == 1 && box->data[0] == 4);
    const kept_file *odd = find_file("saved/a%20b/%2E%2E/c.bin");
    TIDE_CHECK(odd && odd->size == 2 && odd->data[0] == 10 && odd->data[1] == 11);
    TIDE_CHECK(find_file("saved/big.bin") && find_file("saved/big.bin")->size == 5000);
    TIDE_CHECK(!find_file("saved/gone.bin"));

    // The guest, which has no store: the answers came with its frames
    const tide_view_worlds seen = tide_session_view(guest);
    TIDE_REQUIRE(seen.verified != NULL);
    const tide_world *theirs = seen.verified;
    TIDE_CHECK(theirs->Vault.found == 4 && theirs->Vault.missing == 2 && theirs->Vault.sum == server->Vault.sum);
    TIDE_CHECK(crates(theirs) == 4);
    tide_session_destroy(guest);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
    forget_files();
}

// With nowhere to keep it, a match plays on: saves go nowhere and every load finds nothing
TIDE_TEST(storage_without_files_finds_nothing)
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
    TIDE_CHECK(server->Vault.found == 0 && server->Vault.missing == 6 && server->Vault.crates == 0);
    TIDE_CHECK(file_count == 0);
    tide_session_destroy(alone);
}

// A key's path: every byte a file's name can't safely have is written out, and no key leaves the folder
TIDE_TEST(storage_paths_stay_in_their_folder)
{
    char path[TIDE_STORAGE_PATH_MOST + 1u];
    TIDE_CHECK(tide_storage_path("chunks/0/3_-2_5", 15, path, sizeof path) && strcmp(path, "saved/chunks/0/3_-2_5.bin") == 0);
    TIDE_CHECK(tide_storage_path("../../etc", 9, path, sizeof path) && strcmp(path, "saved/%2E%2E/%2E%2E/etc.bin") == 0);
    TIDE_CHECK(tide_storage_path("/a//b/", 6, path, sizeof path) && strcmp(path, "saved/%2Fa%2F%2Fb%2F.bin") == 0);
    TIDE_CHECK(tide_storage_path("C:\\x", 4, path, sizeof path) && strcmp(path, "saved/%43%3A%5Cx.bin") == 0);
    TIDE_CHECK(tide_storage_path("a.b", 3, path, sizeof path) && strcmp(path, "saved/a.b.bin") == 0);
    TIDE_CHECK(tide_storage_path(".", 1, path, sizeof path) && strcmp(path, "saved/%2E.bin") == 0);
    TIDE_CHECK(!tide_storage_path("", 0, path, sizeof path));
    char most[TIDE_STORAGE_KEY_MOST + 1u];
    memset(most, '/', TIDE_STORAGE_KEY_MOST);
    TIDE_CHECK(tide_storage_path(most, TIDE_STORAGE_KEY_MOST, path, sizeof path) == 6u + TIDE_STORAGE_KEY_MOST * 3u + 4u);
    TIDE_CHECK(!tide_storage_path(most, TIDE_STORAGE_KEY_MOST + 1u, path, sizeof path));
}
