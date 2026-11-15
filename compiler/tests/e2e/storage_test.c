#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/storage.h"
#include "tide/time.h"
#include "tide_test.h"
#ifdef STORAGE_PLATFORM
#include "tide/platform.h"
#endif

// The server's store (Storage.Save, Load, Delete and List in match code,
// tide/storage.h): what a tick asks is carried out by the machine that runs
// the match, against the files its host gives it (here, files kept in memory,
// and the platform's own in a folder of the test's), and what a load or a
// listing finds comes back as a Loaded or Listed event, in the same tick on
// every machine, however late it's carried out.

typedef struct kept_file {
    char *path;
    uint8_t *data;
    uint32_t size;
} kept_file;

static kept_file *files;
static uint32_t file_count, file_room, writes, removes, folders, moves, lists;

static kept_file *find_file(const char *path)
{
    for (uint32_t i = 0; i < file_count; i++) {
        if (strcmp(files[i].path, path) == 0) return &files[i];
    }
    return NULL;
}

// Whether a file is in the folder `path`, at any depth
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
    folders += directory;
    return TIDE_IO_NONE;
}

static tide_io_error io_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
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
    writes++;
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
    removes++;
    return TIDE_IO_NONE;
}

// A save goes through a file of its own, moved over the key's once whole
static tide_io_error io_move(void *user, const char *source, const char *destination, const bool overwrite, const bool directory)
{
    (void)user;
    (void)directory;
    kept_file *from = find_file(source);
    if (!from) return TIDE_IO_NOT_FOUND;
    kept_file *to = find_file(destination);
    if (to && !overwrite) return TIDE_IO_ALREADY_EXISTS;
    put_file(destination, from->data, from->size);
    drop_file(find_file(source));
    moves++;
    return TIDE_IO_NONE;
}

// A folder's files or folders, one level: their paths, as a provider gives them. A folder named "locked" can't be
// listed, and one nothing is in isn't there.
static tide_io_error io_list(void *user, const char *path, const bool directories, const bool recursive,
                             const tide_io_entry_fn entry, void *entry_user)
{
    (void)user;
    (void)recursive;
    lists++;
    if (strstr(path, "locked")) return TIDE_IO_ACCESS_DENIED;
    if (!holds(path)) return TIDE_IO_NOT_FOUND;
    const size_t n = strlen(path);
    char (*given)[512] = NULL; // Each folder once
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

// The same files, carried out later: what's posted waits until the test runs it, as a thread of the provider's own
// might take as long as it likes
typedef struct posted_work {
    void (*work)(void *context);
    void *context;
} posted_work;

static posted_work posted[4096];
static uint32_t posted_count, posted_most, posted_ever;

static bool io_post(void *user, void (*work)(void *context), void *context)
{
    (void)user;
    if (posted_count == sizeof posted / sizeof posted[0]) return false;
    posted[posted_count++] = (posted_work){work, context};
    posted_ever++;
    if (posted_count > posted_most) posted_most = posted_count;
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

static void forget_files(void)
{
    for (uint32_t i = 0; i < file_count; i++) {
        free(files[i].data);
        free(files[i].path);
    }
    free(files);
    files = NULL;
    file_count = file_room = writes = removes = folders = moves = lists = 0;
    posted_count = posted_most = posted_ever = 0;
}

// Files in the folder that are no key's: what a listing never has
static void put_strays(void)
{
    static const char *const strays[] = {"saved/list/stray.txt", "saved/list/z.bin.new", "saved/list/Bad.bin",
                                         "saved/list/x%2e.bin", "saved/list/q.r/s.bin", "saved/list/con.bin",
                                         "saved/list/%2Fy.bin.bin"};
    for (size_t i = 0; i < sizeof strays / sizeof strays[0]; i++) put_file(strays[i], (const uint8_t *)"x", 1);
    char path[128];
    // A hashed key's value without its key, its key without its value, and a key that isn't its file's hash
    snprintf(path, sizeof path, "saved/%%%%/%s.bin", "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    put_file(path, (const uint8_t *)"x", 1);
    snprintf(path, sizeof path, "saved/%%%%/%s.key", "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    put_file(path, (const uint8_t *)"list/d", 6);
    snprintf(path, sizeof path, "saved/%%%%/%s.bin", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    put_file(path, (const uint8_t *)"x", 1);
    snprintf(path, sizeof path, "saved/%%%%/%s.key", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    put_file(path, (const uint8_t *)"list/e", 6);
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

// As the fixture's Mix: a key's bytes into a hash, then a separator
static int32_t mix(const int32_t hash, const char *key)
{
    uint32_t h = (uint32_t)hash;
    for (const char *p = key; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return (int32_t)((h ^ 47u) * 16777619u);
}

static void long_key(char *out, const char letter)
{
    memcpy(out, "list/", 5);
    memset(out + 5, letter, 70);
    out[75] = '\0';
}

static int compare_text(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

// What the fixture's listings should come to, its keys sorted by their bytes
static void expected_listings(int32_t *folder, int32_t *partial, int32_t *paged)
{
    char q[80];
    long_key(q, 'Q');
    const char *const in_folder[] = {q, "list/a", "list/b", "list/c/d", "list/c/e"};
    *folder = *partial = *paged = 0;
    for (int i = 0; i < 5; i++) *folder = mix(*folder, in_folder[i]);
    for (int i = 0; i < 5; i++) *partial = mix(*partial, in_folder[i]);
    *partial = mix(*partial, "lista");
    static char pages[1100][16];
    for (int i = 0; i < 1100; i++) snprintf(pages[i], sizeof pages[i], "page/%d", i);
    qsort(pages, 1100, sizeof pages[0], compare_text);
    for (int i = 0; i < 1100; i++) *paged = mix(*paged, pages[i]);
}

static bool shelf_of(const tide_world *w, Shelf *out)
{
    if (!tide_entity_alive(&w->entities, w->Vault.shelf) || !tide_has_Shelf(w, w->Vault.shelf)) return false;
    *out = tide_get_Shelf(w, w->Vault.shelf);
    return true;
}

// What every machine's world has once the fixture has run: what it saved, loaded and listed. `rewound`: the ticks
// from 8 on ran twice, and their first run's saves were carried out before their second run's loads
static void check_world(const tide_world *w, const bool rewound)
{
    int big = 0;
    for (int i = 0; i < 5000; i++) big += (uint8_t)i;
    // boxes/7, "a b/../c", con, a and a.bin/x at tick 8 (the second load of boxes/7 was the first's), big at 12,
    // boxes/7 again at 16, same at 20
    TIDE_CHECK(w->Vault.found == 8);
    TIDE_CHECK(w->Vault.missing == 2); // boxes/none, and gone once it was deleted
    TIDE_CHECK(w->Vault.failed == 1);  // locked: not the same as nothing there
    // (Rewound, boxes/7's first load finds the 4 tick 12 saved the first time)
    TIDE_CHECK(w->Vault.sum == (rewound ? 4 : 1 + 2 + 3) + (10 + 11) + 20 + 30 + 40 + big + 4 + (50 + 51));
    TIDE_CHECK(w->Vault.same == 50 + 51); // Saved in the tick that loaded it, before the load
    TIDE_CHECK(w->Vault.crates == 8 && crates(w) == 8);
    Shelf shelf;
    TIDE_REQUIRE(shelf_of(w, &shelf));
    int32_t folder, partial, paged;
    expected_listings(&folder, &partial, &paged);
    TIDE_CHECK(shelf.listings == 5);  // list/ once though asked twice, lis, page/ in two answers, locked/
    TIDE_CHECK(shelf.folder == 5 && shelf.folderHash == folder); // No stray file, no key without its value
    TIDE_CHECK(shelf.partial == 6 && shelf.partialHash == partial);
    TIDE_CHECK(shelf.pages == 2 && shelf.paged == 1100 && shelf.pagedHash == paged);
    TIDE_CHECK(shelf.failures == 1);
}

// The files the fixture leaves: each key's own, none outside the folder, the last save's bytes, each written beside
// and moved over, a hashed key's key beside its value
static void check_files(void)
{
    // 1,116 saves, two of them of hashed keys, which write their keys too
    TIDE_CHECK(writes == 1118 && moves == 1118 && removes == 3 && folders >= 1);
    TIDE_CHECK(find_file("saved/%63on.bin") && find_file("saved/a.bin") && find_file("saved/a%2Ebin/x.bin"));
    const kept_file *box = find_file("saved/boxes/7.bin");
    TIDE_CHECK(box && box->size == 1 && box->data[0] == 4);
    const kept_file *odd = find_file("saved/a%20b/%2E%2E/c.bin");
    TIDE_CHECK(odd && odd->size == 2 && odd->data[0] == 10 && odd->data[1] == 11);
    TIDE_CHECK(find_file("saved/big.bin") && find_file("saved/big.bin")->size == 5000);
    TIDE_CHECK(!find_file("saved/gone.bin"));
    char key[80], path[TIDE_STORAGE_PATH_MOST + 1u];
    long_key(key, 'Q');
    const uint32_t n = tide_storage_path(key, 75, path, sizeof path);
    TIDE_REQUIRE(n && strncmp(path, "saved/%%/", 9) == 0);
    TIDE_CHECK(find_file(path) != NULL);
    memcpy(path + n - 4, ".key", 5);
    const kept_file *beside = find_file(path);
    TIDE_CHECK(beside && beside->size == 75 && memcmp(beside->data, key, 75) == 0);
    long_key(key, 'R'); // Deleted, both
    const uint32_t r = tide_storage_path(key, 75, path, sizeof path);
    TIDE_CHECK(!find_file(path));
    memcpy(path + r - 4, ".key", 5);
    TIDE_CHECK(!find_file(path));
    TIDE_CHECK(find_file("saved/page/1099.bin") && !find_file("saved/page/1100.bin"));
}

// A host with the files and a guest without, over a lossy network, until both have every answer: the guest's world
// is the host's, tick for tick. `later`: the host's files carry its requests out only every `later` frames.
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
    for (int frame = 0; frame < 360; frame++) {
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
    check_world(server, false);
    // The guest, which has no store: the answers came with its frames
    const tide_view_worlds seen = tide_session_view(guest);
    TIDE_REQUIRE(seen.verified != NULL);
    check_world(seen.verified, false);
    tide_session_destroy(guest);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
    check_files();
}

TIDE_TEST(storage_saves_loads_lists_and_deletes_the_same_on_every_machine)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL);
    forget_files();
    put_strays();
    put_file("saved/locked/k.bin", (const uint8_t *)"x", 1); // A folder the host can't list
    play(&io, 0);
    forget_files();
}

// Carried out off the tick's thread, later than the tick after: the answers go to the first tick after they're ready,
// in the order they were asked, and every machine's world is the same as when they're carried out at once
TIDE_TEST(storage_answers_carried_out_later_are_the_same_on_every_machine)
{
    forget_files();
    put_strays();
    put_file("saved/locked/k.bin", (const uint8_t *)"x", 1);
    play(&deferred, 5);
    TIDE_CHECK(posted_ever > 3 && posted_most >= 2); // Ticks' batches waited for each other
    TIDE_CHECK(posted_count == 0);
    forget_files();
}

// A server that stops cleanly saves what its ticks asked, though nothing was carried out yet
TIDE_TEST(storage_saves_survive_a_server_that_stops)
{
    forget_files();
    tide_session *alone = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = &deferred});
    double t = 0.0;
    tide_session_start(alone, NULL, t);
    const tide_world *server = NULL;
    for (int frame = 0; frame < 60 && !(server && server->Time.tick >= 10); frame++) { // Past ticks 5, 8 and 9, nothing run
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        server = tide_session_server_world(alone);
    }
    TIDE_REQUIRE(server != NULL && server->Time.tick >= 10 && server->Time.tick < 12);
    TIDE_CHECK(posted_count >= 3 && !find_file("saved/boxes/7.bin"));
    tide_session_destroy(alone);
    TIDE_CHECK(posted_count == 0);
    const kept_file *box = find_file("saved/boxes/7.bin");
    TIDE_CHECK(box && box->size == 3 && box->data[2] == 3);
    TIDE_CHECK(find_file("saved/page/1099.bin") != NULL);
    forget_files();
}

// Going back drops the answers on their way, those still being carried out too: the ticks run again ask again, and
// what they find is taken once
TIDE_TEST(storage_rewind_drops_answers_still_being_carried_out)
{
    forget_files();
    put_strays();
    put_file("saved/locked/k.bin", (const uint8_t *)"x", 1);
    tide_session *alone = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .tick_rate = 60, .sample = sample, .io = &deferred});
    double t = 0.0;
    tide_session_start(alone, NULL, t);
    tide_world *then = NULL;
    for (int frame = 0; frame < 30; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        const tide_world *server = tide_session_server_world(alone);
        if (!then && server && server->Time.tick >= 7) { // Before the loads of tick 8 and the listings of 9
            run_posted(); // Tick 5's saves
            then = calloc(1, tide_game_api.world_size);
            TIDE_REQUIRE(then != NULL);
            tide_game_api.copy_world(then, server);
        }
    }
    const tide_world *server = tide_session_server_world(alone);
    TIDE_REQUIRE(then && server && server->Time.tick > 20 && posted_count >= 3); // Loads and listings asked, none answered
    TIDE_CHECK(server->Vault.found == 0 && tide_has_Shelf(server, server->Vault.shelf));
    TIDE_REQUIRE(tide_session_rewind(alone, then));
    run_posted(); // The old ticks' answers come now: nobody's
    for (int frame = 0; frame < 300; frame++) {
        t += 1.0 / 60.0;
        tide_session_update(alone, t);
        if (frame % 3 == 2) run_posted();
    }
    server = tide_session_server_world(alone);
    TIDE_REQUIRE(server != NULL);
    check_world(server, true); // Each answer once
    tide_session_destroy(alone);
    tide_game_api.free_world(then);
    free(then);
    forget_files();
}

// With nowhere to keep it, a match plays on: saves go nowhere, every load finds nothing and every listing no keys
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
    TIDE_CHECK(server->Vault.found == 0 && server->Vault.missing == 11 && server->Vault.failed == 0 && server->Vault.crates == 0);
    Shelf shelf;
    TIDE_REQUIRE(shelf_of(server, &shelf));
    TIDE_CHECK(shelf.listings == 4 && shelf.folder == 0 && shelf.paged == 0 && shelf.pages == 1 && shelf.failures == 0);
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
    // A folder has no '.': none ends in one (which Windows drops), none is another key's file
    TIDE_CHECK(tide_storage_path("a.bin/x", 7, path, sizeof path) && strcmp(path, "saved/a%2Ebin/x.bin") == 0);
    TIDE_CHECK(tide_storage_path("a./b", 4, path, sizeof path) && strcmp(path, "saved/a%2E/b.bin") == 0);
    // Windows devices, in any case, with an extension or as a folder
    TIDE_CHECK(tide_storage_path("con", 3, path, sizeof path) && strcmp(path, "saved/%63on.bin") == 0);
    TIDE_CHECK(tide_storage_path("com1.x", 6, path, sizeof path) && strcmp(path, "saved/%63om1.x.bin") == 0);
    TIDE_CHECK(tide_storage_path("lpt9/a", 6, path, sizeof path) && strcmp(path, "saved/%6Cpt9/a.bin") == 0);
    TIDE_CHECK(tide_storage_path("console", 7, path, sizeof path) && strcmp(path, "saved/console.bin") == 0);
    // A part too long for a file's name: the key's hash, in a folder no other key's path has
    char long_key_bytes[120];
    memset(long_key_bytes, 'Q', sizeof long_key_bytes); // Capitals: three bytes each written out, 360 in all
    TIDE_CHECK(tide_storage_path(long_key_bytes, sizeof long_key_bytes, path, sizeof path) == 9u + 64u + 4u
               && strncmp(path, "saved/%%/", 9) == 0);
    char other[TIDE_STORAGE_PATH_MOST + 1u];
    long_key_bytes[0] = 'R';
    TIDE_CHECK(tide_storage_path(long_key_bytes, sizeof long_key_bytes, other, sizeof other) && strcmp(path, other) != 0);
    TIDE_CHECK(tide_storage_path(".", 1, path, sizeof path) && strcmp(path, "saved/%2E.bin") == 0);
    TIDE_CHECK(!tide_storage_path("", 0, path, sizeof path));
    char most[TIDE_STORAGE_KEY_MOST + 1u];
    memset(most, '/', TIDE_STORAGE_KEY_MOST);
    TIDE_CHECK(tide_storage_path(most, TIDE_STORAGE_KEY_MOST, path, sizeof path) == 6u + TIDE_STORAGE_KEY_MOST * 3u + 4u);
    TIDE_CHECK(!tide_storage_path(most, TIDE_STORAGE_KEY_MOST + 1u, path, sizeof path));
}

// The other way: a key's file gives the key back, and a file that's no key's gives nothing
TIDE_TEST(storage_paths_give_their_keys_back)
{
    static const char *const keys[] = {"chunks/0/3_-2_5", "../../etc", "/a//b/", "C:\\x", "a.b", "a.bin/x", "a./b", "con",
                                       "com1.x", "lpt9/a", "console", ".", "a", "a/", "//", "x y/z.w.v", "%41", "Ünïcödé/ключ",
                                       "a\nb", "zz/..", "-_.-"};
    char path[TIDE_STORAGE_PATH_MOST + 1u], key[TIDE_STORAGE_KEY_MOST];
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const uint32_t n = (uint32_t)strlen(keys[i]);
        const uint32_t p = tide_storage_path(keys[i], n, path, sizeof path);
        TIDE_CHECK(p != 0);
        const uint32_t k = tide_storage_key(path, p, key, sizeof key);
        TIDE_CHECK(k == n && memcmp(key, keys[i], n) == 0);
    }
    char most[TIDE_STORAGE_KEY_MOST];
    memset(most, '/', sizeof most); // Every byte written out: the longest path there is
    const uint32_t p = tide_storage_path(most, sizeof most, path, sizeof path);
    TIDE_CHECK(p && tide_storage_key(path, p, key, sizeof key) == sizeof most);
    // Not a key's: another name, a save that stopped, small hex digits, capitals, a folder with a '.', a device's name
    // as it is, a '/' written out where it's a folder's, bytes that aren't UTF-8, a hashed key, nothing at all
    static const char *const strays[] = {"saved/a.txt", "saved/a.bin.new", "saved/a%2e.bin", "saved/A.bin", "saved/q.r/s.bin",
                                         "saved/con.bin", "saved/a%2Fb.bin", "saved/%FF.bin", "saved/%C3.bin", "saved/.bin",
                                         "saved/%%/0123.bin", "saved/a/.bin", "saved//a.bin", "other/a.bin", "saved/a%2",
                                         "saved/a%4.bin", "saved/a b.bin", "saved/a.bin/x.bin", "saved/%2E%2E.bin.bin"};
    for (size_t i = 0; i < sizeof strays / sizeof strays[0]; i++) {
        TIDE_CHECK(tide_storage_key(strays[i], (uint32_t)strlen(strays[i]), key, sizeof key) == 0);
    }
    char hashed[200];
    memset(hashed, 'Q', 120);
    const uint32_t h = tide_storage_path(hashed, 120, path, sizeof path);
    TIDE_CHECK(h && tide_storage_key(path, h, key, sizeof key) == 0); // Its file doesn't say what it is
}

// ---------------------------------------------------------------------------
// Listings, asked of the store straight

static int the_world; // Requests are a world's: any address will do
static uint8_t *answers[8];
static uint32_t answer_sizes[8], answer_count;

static void take_answer(void *user, uint8_t *message, const uint32_t size)
{
    (void)user;
    if (answer_count < 8) {
        answers[answer_count] = message;
        answer_sizes[answer_count++] = size;
    } else free(message);
}

static void forget_answers(void)
{
    for (uint32_t i = 0; i < answer_count; i++) free(answers[i]);
    answer_count = 0;
}

static tide_str text(const char *s)
{
    return (tide_str){s, (int32_t)strlen(s), (int32_t)strlen(s)};
}

static void save_key(tide_storage *store, const char *key)
{
    tide_storage_ask(&the_world, TIDE_STORAGE_SAVE, text(key), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
    (void)store;
}

static void list_keys(const char *prefix, const char *after)
{
    tide_storage_ask(&the_world, TIDE_STORAGE_LIST, text(prefix), (tide_list){0}, text(after), TIDE_STORAGE_TO_EVENT,
                     (tide_entity){7, 9});
}

// One listing, served now: its keys joined by spaces into `out`
static bool listed(const tide_io *files_io, tide_storage *store, const char *prefix, const char *after, char *out,
                   const size_t room, uint32_t *count, bool *more, bool *failed)
{
    list_keys(prefix, after);
    uint64_t next = 0;
    forget_answers();
    tide_storage_serve(store, files_io, &next, take_answer, NULL);
    tide_stored answer;
    if (answer_count != 1 || !tide_stored_read(answers[0], answer_sizes[0], &answer)) return false;
    if (answer.kind != TIDE_STORAGE_LIST || answer.target.birth != 7 || answer.target.lineage != 9
        || answer.key_size != strlen(prefix) || memcmp(answer.key, prefix, answer.key_size) != 0
        || answer.after_size != strlen(after)) return false;
    *count = answer.count;
    *more = answer.more;
    *failed = answer.outcome == TIDE_STORED_FAILED;
    size_t at = 0;
    out[0] = '\0';
    const char *key;
    uint32_t size, from = 0, seen = 0;
    while (tide_stored_next_key(&answer, &from, &key, &size)) {
        if (at + size + 2 < room) {
            at += (size_t)snprintf(out + at, room - at, "%s%.*s", at ? " " : "", (int)size, key);
        }
        seen++;
    }
    return seen == answer.count;
}

TIDE_TEST(storage_lists_keys_by_prefix_in_order_and_in_pages)
{
    forget_files();
    tide_storage store = {0};
    tide_storage_bind(&the_world, &store);
    static const char *const keys[] = {"list/b", "list/a", "list/c/e", "list/c/d", "lista", "a//b", "a/c", "a/", "z",
                                       "deep/x/y/z", "deep/x/w", "deep/v", "con/x", "a.b/c"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) save_key(&store, keys[i]);
    char q[80];
    long_key(q, 'Q');
    save_key(&store, q);
    uint64_t next = 0;
    tide_storage_serve(&store, &io, &next, take_answer, NULL);
    put_strays();
    char got[4096];
    uint32_t count;
    bool more, failed;
    TIDE_CHECK(listed(&io, &store, "list/", "", got, sizeof got, &count, &more, &failed) && !failed && !more && count == 5);
    char want[256];
    snprintf(want, sizeof want, "%s list/a list/b list/c/d list/c/e", q);
    TIDE_CHECK(strcmp(got, want) == 0);
    // A part's start, and a prefix that ends in the middle of a folder's name
    TIDE_CHECK(listed(&io, &store, "lis", "", got, sizeof got, &count, &more, &failed) && count == 6);
    TIDE_CHECK(strstr(got, "list/c/e lista") != NULL);
    TIDE_CHECK(listed(&io, &store, "list/c", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/c/d list/c/e") == 0);
    TIDE_CHECK(listed(&io, &store, "de", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "deep/v deep/x/w deep/x/y/z") == 0);
    // A '/' that's no folder's: "a//b" and "a/" are files beside the folder "a", in "saved"
    TIDE_CHECK(listed(&io, &store, "a/", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "a/ a//b a/c") == 0);
    TIDE_CHECK(listed(&io, &store, "a//", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "a//b") == 0);
    TIDE_CHECK(listed(&io, &store, "con/", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "con/x") == 0);
    TIDE_CHECK(listed(&io, &store, "a.b/", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "a.b/c") == 0);
    // After a key: those past it, whether it's one or not
    TIDE_CHECK(listed(&io, &store, "list/", "list/b", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/c/d list/c/e") == 0);
    TIDE_CHECK(listed(&io, &store, "list/", "list/bb", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/c/d list/c/e") == 0);
    TIDE_CHECK(listed(&io, &store, "list/", "list/c", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/c/d list/c/e") == 0);
    TIDE_CHECK(listed(&io, &store, "list/", "m", got, sizeof got, &count, &more, &failed) && count == 0 && !more);
    TIDE_CHECK(listed(&io, &store, "list/", "a", got, sizeof got, &count, &more, &failed) && count == 5);
    // Every key, and none
    TIDE_CHECK(listed(&io, &store, "", "", got, sizeof got, &count, &more, &failed) && count == sizeof keys / sizeof keys[0] + 1u);
    TIDE_CHECK(listed(&io, &store, "nothing/", "", got, sizeof got, &count, &more, &failed) && count == 0 && !failed);
    // Keys past an answer's 1,024: the first 1,024, then the rest after the last of them
    for (int i = 0; i < 1025; i++) {
        char key[32];
        snprintf(key, sizeof key, "page/%04d", i);
        save_key(&store, key);
    }
    tide_storage_serve(&store, &io, &next, take_answer, NULL);
    static char pages[TIDE_STORAGE_LIST_MOST * 12u];
    TIDE_CHECK(listed(&io, &store, "page/", "", pages, sizeof pages, &count, &more, &failed) && count == 1024 && more);
    TIDE_CHECK(strncmp(pages, "page/0000 page/0001", 19) == 0 && strstr(pages, "page/1023") && !strstr(pages, "page/1024"));
    TIDE_CHECK(listed(&io, &store, "page/", "page/1023", got, sizeof got, &count, &more, &failed) && count == 1 && !more
               && strcmp(got, "page/1024") == 0);
    TIDE_CHECK(listed(&io, &store, "page/", "page/0000", pages, sizeof pages, &count, &more, &failed) && count == 1024 && !more);
    TIDE_CHECK(listed(&io, &store, "page/10", "", got, sizeof got, &count, &more, &failed) && count == 25);
    // ...and in 1,100 folders, which a page walks only where they can hold one of its keys: those past the greatest it
    // has once it has enough, and before `after`, are left alone
    for (int i = 0; i < 1100; i++) {
        char key[32];
        snprintf(key, sizeof key, "f/%04d/k", i);
        save_key(&store, key);
    }
    tide_storage_serve(&store, &io, &next, take_answer, NULL);
    uint32_t walked = lists;
    TIDE_CHECK(listed(&io, &store, "f/", "", pages, sizeof pages, &count, &more, &failed) && count == 1024 && more);
    TIDE_CHECK(strncmp(pages, "f/0000/k f/0001/k", 17) == 0 && strstr(pages, "f/1023/k") && !strstr(pages, "f/1024/k"));
    TIDE_CHECK(lists - walked < 2u * 1100u); // Not every folder: those past the 1,025th weren't
    walked = lists;
    TIDE_CHECK(listed(&io, &store, "f/", "f/1023/k", got, sizeof got, &count, &more, &failed) && count == 76 && !more);
    TIDE_CHECK(strncmp(got, "f/1024/k f/1025/k", 17) == 0 && strstr(got, "f/1099/k"));
    TIDE_CHECK(lists - walked < 2u * 200u); // Those before it weren't either
    TIDE_CHECK(listed(&io, &store, "f/05", "", got, sizeof got, &count, &more, &failed) && count == 100 && !more);
    // Asked twice in a tick: answered once
    list_keys("list/", "");
    list_keys("list/", "");
    list_keys("list/", "list/a");
    forget_answers();
    tide_storage_serve(&store, &io, &next, take_answer, NULL);
    TIDE_CHECK(answer_count == 2);
    // A folder that can't be listed fails the listing where its keys could be among the answer's: no keys, not "more".
    // Where they'd all be past the answer's last, it doesn't, and the page after does
    put_file("saved/locked/k.bin", (const uint8_t *)"x", 1);
    TIDE_CHECK(listed(&io, &store, "locked/", "", got, sizeof got, &count, &more, &failed) && failed && count == 0 && !more);
    TIDE_CHECK(listed(&io, &store, "", "", pages, sizeof pages, &count, &more, &failed) && !failed && count == 1024 && more);
    TIDE_CHECK(listed(&io, &store, "", "lista", got, sizeof got, &count, &more, &failed) && failed && count == 0);
    TIDE_CHECK(listed(&io, &store, "lo", "", got, sizeof got, &count, &more, &failed) && failed);
    TIDE_CHECK(listed(&io, &store, "l", "", got, sizeof got, &count, &more, &failed) && failed); // Fewer than a page: it could hold one
    // No files at all: nothing kept, no keys; files that can't be listed: failed
    TIDE_CHECK(listed(NULL, &store, "list/", "", got, sizeof got, &count, &more, &failed) && !failed && count == 0);
    const tide_io unlisted = {.exists = io_exists, .create = io_create, .read = io_read, .write = io_write};
    TIDE_CHECK(listed(&unlisted, &store, "list/", "", got, sizeof got, &count, &more, &failed) && failed && count == 0);
    forget_answers();
    tide_storage_unbind(&the_world);
    tide_storage_free(&store);
    forget_files();
}

// An answer's bytes are checked before anything reads them
TIDE_TEST(storage_answers_that_arent_are_refused)
{
    const uint8_t keys[] = {1, 'a', 2, 'b', 'c'};
    tide_stored made = {.kind = TIDE_STORAGE_LIST, .outcome = TIDE_STORED_FOUND, .key = "p", .key_size = 1, .after = "", .data = keys,
                        .data_size = sizeof keys, .count = 2, .more = true};
    uint32_t size = 0;
    uint8_t *message = tide_stored_message(&made, &size);
    tide_stored read;
    TIDE_CHECK(tide_stored_read(message, size, &read) && read.count == 2 && read.more && read.kind == TIDE_STORAGE_LIST);
    tide_stored cut;
    for (uint32_t n = 0; n < size; n++) TIDE_CHECK(!tide_stored_read(message, n, &cut)); // Every part short of whole
    message[size - 1] = 0xFF; // A key that isn't UTF-8
    TIDE_CHECK(!tide_stored_read(message, size, &cut));
    free(message);
    made.count = 3; // More keys than it has
    message = tide_stored_message(&made, &size);
    TIDE_CHECK(!tide_stored_read(message, size, &cut));
    free(message);
    const tide_stored load = {.kind = TIDE_STORAGE_LOAD, .to = TIDE_STORAGE_TO_TASK, .outcome = TIDE_STORED_FOUND, .key = "k",
                              .key_size = 1, .data = keys, .data_size = 3, .target = {11, 12}, .number = 5};
    message = tide_stored_message(&load, &size);
    TIDE_CHECK(tide_stored_read(message, size, &read) && read.to == TIDE_STORAGE_TO_TASK && read.target.birth == 11
               && read.number == 5 && read.data_size == 3);
    message[1] = 9; // No such kind
    TIDE_CHECK(!tide_stored_read(message, size, &cut));
    free(message);
}

// A server's line: each tick's requests carried out after the last's, answers taken in the order they were asked,
// up to the first batch not yet carried out; a forgotten one's answers dropped, its saves done
TIDE_TEST(storage_line_keeps_the_order_requests_were_asked_in)
{
    forget_files();
    tide_storage store = {0};
    tide_storage_line line = {0};
    tide_storage_bind(&the_world, &store);
    save_key(&store, "k/1");
    list_keys("k/", "");
    tide_storage_send(&line, &store, &deferred, true); // Batch 1: a save and a listing
    tide_storage_ask(&the_world, TIDE_STORAGE_LOAD, text("k/1"), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_TASK, (tide_entity){3, 4});
    tide_storage_send(&line, &store, &deferred, true); // Batch 2: a load for a task
    save_key(&store, "k/2");
    tide_storage_send(&line, &store, &deferred, true); // Batch 3: a save, no answers
    TIDE_CHECK(tide_storage_waiting(&line) == 3 && posted_count == 3);
    forget_answers();
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 0); // Nothing carried out yet
    posted[1].work(posted[1].context); // The second first: its answer waits for the first's
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 0);
    posted[0].work(posted[0].context);
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 2 && tide_storage_waiting(&line) == 1);
    tide_stored a, b;
    TIDE_REQUIRE(answer_count == 2 && tide_stored_read(answers[0], answer_sizes[0], &a) && tide_stored_read(answers[1], answer_sizes[1], &b));
    TIDE_CHECK(a.kind == TIDE_STORAGE_LIST && a.count == 1 && a.number == 0);
    // (Carried out before the save of batch 1 here, which the provider's own thread never does: so not found)
    TIDE_CHECK(b.kind == TIDE_STORAGE_LOAD && b.to == TIDE_STORAGE_TO_TASK && b.target.birth == 3 && b.target.lineage == 4
               && b.outcome == TIDE_STORED_MISSING && b.number == 1);
    posted_count = 0;
    posted[2].work(posted[2].context);
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 0 && tide_storage_waiting(&line) == 0);
    // Forgotten: its answers dropped as they come, its save done
    list_keys("k/", "");
    save_key(&store, "k/3");
    tide_storage_send(&line, &store, &deferred, true);
    tide_storage_forget(&line);
    run_posted();
    forget_answers();
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 0 && answer_count == 0 && find_file("saved/k/3.bin"));
    // Finished with work still posted: it's done, and its answers freed
    save_key(&store, "k/4");
    list_keys("k/", "");
    tide_storage_send(&line, &store, &deferred, true);
    tide_storage_finish(&line);
    TIDE_CHECK(posted_count == 0 && find_file("saved/k/4.bin") && !line.first);
    // ...and with answers nobody takes, loads and listings aren't carried out
    tide_storage_ask(&the_world, TIDE_STORAGE_LOAD, text("k/1"), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
    save_key(&store, "k/5");
    const uint32_t before = lists;
    tide_storage_send(&line, &store, &io, false);
    list_keys("k/", "");
    tide_storage_send(&line, &store, &io, false);
    TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 0 && find_file("saved/k/5.bin") && lists == before);
    tide_storage_finish(&line);
    tide_storage_unbind(&the_world);
    tide_storage_free(&store);
    forget_answers();
    forget_files();
}

#ifdef STORAGE_PLATFORM
// The platform's own files, in a folder of the test's: what the standard hosts keep. Keys saved and listed from
// real folders, files a person put there that aren't keys', and requests carried out on the provider's own thread.
static const tide_io *platform_files(void)
{
    const tide_io *files_io = tide_platform_io_init("tests.tide.storage", "storage_files");
    if (files_io) (void)files_io->remove(files_io->user, "saved", true, true); // What an earlier run left
    return files_io;
}

static void put_stray(const tide_io *files_io, const char *path)
{
    char folder[256];
    snprintf(folder, sizeof folder, "%s", path);
    char *slash = strrchr(folder, '/');
    if (slash) {
        *slash = '\0';
        (void)files_io->create(files_io->user, folder, true);
    }
    TIDE_CHECK(files_io->write(files_io->user, path, (const uint8_t *)"x", 1, false) == TIDE_IO_NONE);
}

TIDE_TEST(storage_lists_the_platforms_files)
{
    const tide_io *files_io = platform_files();
    TIDE_REQUIRE(files_io && files_io->post && files_io->settle && files_io->list);
    tide_storage store = {0};
    tide_storage_bind(&the_world, &store);
    static const char *const keys[] = {"list/b", "list/a", "list/c/e", "list/c/d", "lista", "a//b", "a/c", "con", "Big/Name"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) save_key(&store, keys[i]);
    char q[80];
    long_key(q, 'Q');
    save_key(&store, q);
    uint64_t next = 0;
    tide_storage_serve(&store, files_io, &next, take_answer, NULL);
    // A save that stopped, a file of another name, one in capitals, one in a folder with a '.', a hashed value alone
    put_stray(files_io, "saved/list/z.bin.new");
    put_stray(files_io, "saved/list/stray.txt");
    put_stray(files_io, "saved/list/Bad.bin");
    put_stray(files_io, "saved/list/q.r/s.bin");
    put_stray(files_io, "saved/%%/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff.bin");
    char got[4096], want[256];
    uint32_t count;
    bool more, failed;
    TIDE_CHECK(listed(files_io, &store, "list/", "", got, sizeof got, &count, &more, &failed) && !failed && count == 5);
    snprintf(want, sizeof want, "%s list/a list/b list/c/d list/c/e", q);
    TIDE_CHECK(strcmp(got, want) == 0);
    TIDE_CHECK(listed(files_io, &store, "", "", got, sizeof got, &count, &more, &failed) && !failed && count == 10);
    TIDE_CHECK(strncmp(got, "Big/Name a//b a/c con ", 22) == 0);
    TIDE_CHECK(listed(files_io, &store, "list/", "list/a", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/b list/c/d list/c/e") == 0);
    // Deleted: gone from the listing, its key file too
    tide_storage_ask(&the_world, TIDE_STORAGE_DELETE, text(q), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
    tide_storage_serve(&store, files_io, &next, take_answer, NULL);
    TIDE_CHECK(listed(files_io, &store, "list/", "", got, sizeof got, &count, &more, &failed) && strcmp(got, "list/a list/b list/c/d list/c/e") == 0);
    // On the provider's thread: carried out in order, answered once each's done
    tide_storage_line line = {0};
    for (int i = 0; i < 20; i++) {
        char key[32];
        snprintf(key, sizeof key, "line/%02d", i);
        save_key(&store, key);
        tide_storage_ask(&the_world, TIDE_STORAGE_LOAD, text(key), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
        tide_storage_send(&line, &store, files_io, true);
    }
    files_io->settle(files_io->user);
    forget_answers();
    uint32_t taken = 0;
    while (tide_storage_waiting(&line)) {
        taken += tide_storage_receive(&line, take_answer, NULL);
        for (uint32_t i = 0; i < answer_count; i++) {
            tide_stored a;
            TIDE_CHECK(tide_stored_read(answers[i], answer_sizes[i], &a) && a.outcome == TIDE_STORED_FOUND);
        }
        forget_answers();
    }
    TIDE_CHECK(taken == 20);
    tide_storage_finish(&line);
    forget_answers();
    tide_storage_unbind(&the_world);
    tide_storage_free(&store);
    (void)files_io->remove(files_io->user, "saved", true, true);
}

// What a tick's end costs with many loads: carried out where it ends (as before) and handed to the provider's
// thread (as now), against the platform's files. Measured, not pinned: the numbers go to notes/voxels.md.
TIDE_TEST(storage_measure_tick_end)
{
    const tide_io *files_io = platform_files();
    TIDE_REQUIRE(files_io && files_io->post);
    tide_storage store = {0};
    tide_storage_bind(&the_world, &store);
    // 845 chunks saved, a view's worth, 300 bytes each (a List<byte> in the scratch area)
    const uint64_t mark = tide_scratch_mark();
    tide_list chunk = {0};
    tide_block *block = tide_scratch_block(300, "a chunk", &chunk.at);
    block->a = block->b = 300;
    memset(block + 1, 7, 300);
    for (int i = 0; i < 845; i++) {
        char key[32];
        snprintf(key, sizeof key, "chunks/0/%d_%d_%d", i % 13, i / 169, (i / 13) % 13);
        tide_storage_ask(&the_world, TIDE_STORAGE_SAVE, text(key), chunk, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
    }
    tide_scratch_reset(mark);
    uint64_t next = 0;
    tide_storage_serve(&store, files_io, &next, take_answer, NULL);
    double before_ms[3], after_ms[3], thread_ms[3];
    for (int round = 0; round < 3; round++) {
        // 845 loads of chunks there and 845 of chunks never saved, as a player's first view of fresh ground asks
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < 1690; i++) {
                char key[32];
                snprintf(key, sizeof key, "chunks/%d/%d_%d_%d", i < 845 ? 0 : 1, i % 13, (i % 845) / 169, (i / 13) % 13);
                tide_storage_ask(&the_world, TIDE_STORAGE_LOAD, text(key), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT,
                                 (tide_entity){(uint64_t)i + 1u, 1});
            }
            forget_answers();
            const uint64_t began = tide_time_now_ns();
            if (pass == 0) {
                tide_storage_serve(&store, files_io, &next, take_answer, NULL);
                before_ms[round] = (double)(tide_time_now_ns() - began) / 1e6;
            } else {
                tide_storage_line line = {0};
                tide_storage_send(&line, &store, files_io, true);
                after_ms[round] = (double)(tide_time_now_ns() - began) / 1e6;
                files_io->settle(files_io->user);
                thread_ms[round] = (double)(tide_time_now_ns() - began) / 1e6;
                TIDE_CHECK(tide_storage_receive(&line, take_answer, NULL) == 1690u);
                tide_storage_finish(&line);
            }
            forget_answers();
        }
    }
    printf("  tick_end with 1,690 loads (845 found): %.2f, %.2f, %.2f ms carried out there; %.3f, %.3f, %.3f ms handing "
           "them over (the provider's thread took %.2f, %.2f, %.2f ms)\n",
           before_ms[0], before_ms[1], before_ms[2], after_ms[0], after_ms[1], after_ms[2], thread_ms[0], thread_ms[1], thread_ms[2]);
    tide_storage_unbind(&the_world);
    tide_storage_free(&store);
    (void)files_io->remove(files_io->user, "saved", true, true);
}
#endif
