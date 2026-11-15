#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
// The managed reactor (tide/managed_run.h) built as a native program: its exports are functions this test calls, as
// the service calls them, and its imports functions this test gives, as the service does. What's checked is the
// runner's own: a hosted match's store asked of the host and answered later, a restart from a checkpoint, a host
// with no store, and answers that aren't ones.
#define TIDE_MANAGED_TEST 1
#include "tide/managed_run.h"
#include "tide_test.h"

void tide_managed_send(const uint32_t peer, const void *data, const uint32_t size)
{
    (void)peer;
    (void)data;
    (void)size;
}

uint64_t tide_managed_entropy(void)
{
    return UINT64_C(0x9E3779B97F4A7C15);
}

// ---------------------------------------------------------------------------
// The fake service: its store, a value for each key, and the answers it has read and hasn't given yet

typedef struct host_value {
    char key[TIDE_STORAGE_KEY_MOST + 1u];
    uint32_t key_size;
    uint8_t *value;
    uint32_t size;
} host_value;

static host_value *host_values;
static uint32_t host_value_count, host_value_room;

typedef struct host_answer {
    double id;
    char key[TIDE_STORAGE_KEY_MOST + 1u]; // A load's key, a listing's prefix
    uint32_t kind, outcome, more, due;
    uint8_t *data;
    uint32_t size;
} host_answer;

static host_answer *host_answers;
static uint32_t host_answer_count, host_answer_room;
static uint32_t delay;    // Updates after taking a request before its answer is given
static bool holding;      // Nothing is given while it's set
static uint32_t updates;  // Updates so far
static uint32_t loads, lists, saves, deletes; // Requests taken
static double now_at;

static host_value *host_find(const char *key, const uint32_t size)
{
    for (uint32_t i = 0; i < host_value_count; i++) {
        if (host_values[i].key_size == size && memcmp(host_values[i].key, key, size) == 0) return &host_values[i];
    }
    return NULL;
}

static void host_put(const char *key, const uint32_t size, const uint8_t *value, const uint32_t value_size)
{
    host_value *k = host_find(key, size);
    if (!k) {
        if (host_value_count == host_value_room) {
            host_value_room = host_value_room ? host_value_room * 2u : 64u;
            host_values = realloc(host_values, host_value_room * sizeof *host_values);
        }
        k = &host_values[host_value_count++];
        memcpy(k->key, key, size);
        k->key[size] = '\0';
        k->key_size = size;
        k->value = NULL;
    }
    free(k->value);
    k->value = malloc(value_size ? value_size : 1u);
    if (value_size) memcpy(k->value, value, value_size);
    k->size = value_size;
}

static void host_erase(const char *key, const uint32_t size)
{
    host_value *k = host_find(key, size);
    if (!k) return;
    free(k->value);
    *k = host_values[--host_value_count];
}

static void forget_values(void)
{
    for (uint32_t i = 0; i < host_value_count; i++) free(host_values[i].value);
    free(host_values);
    host_values = NULL;
    host_value_count = host_value_room = 0;
}

static void forget_answers(void)
{
    for (uint32_t i = 0; i < host_answer_count; i++) free(host_answers[i].data);
    free(host_answers);
    host_answers = NULL;
    host_answer_count = host_answer_room = 0;
}

static host_answer *queue_answer(const double id, const uint32_t kind, const char *key, const uint32_t key_size)
{
    if (host_answer_count == host_answer_room) {
        host_answer_room = host_answer_room ? host_answer_room * 2u : 64u;
        host_answers = realloc(host_answers, host_answer_room * sizeof *host_answers);
    }
    host_answer *p = &host_answers[host_answer_count++];
    memset(p, 0, sizeof *p);
    p->id = id;
    p->kind = kind;
    memcpy(p->key, key, key_size);
    p->key[key_size] = '\0';
    p->due = updates + delay;
    return p;
}

static int compare_kept(const void *a, const void *b)
{
    const host_value *x = *(const host_value *const *)a, *y = *(const host_value *const *)b;
    const int c = memcmp(x->key, y->key, x->key_size < y->key_size ? x->key_size : y->key_size);
    return c ? c : (x->key_size > y->key_size) - (x->key_size < y->key_size);
}

static bool after_key(const host_value *k, const char *after, const uint32_t after_size)
{
    const int c = memcmp(k->key, after, k->key_size < after_size ? k->key_size : after_size);
    return c > 0 || (c == 0 && k->key_size > after_size);
}

// A varint and its bytes
static uint32_t put_key(uint8_t *out, const char *key, const uint32_t size)
{
    uint32_t at = 0;
    if (size < 128u) out[at++] = (uint8_t)size;
    else {
        out[at++] = (uint8_t)(size & 127u) | 128u;
        out[at++] = (uint8_t)(size >> 7);
    }
    memcpy(out + at, key, size);
    return at + size;
}

// A listing's page, as the service makes one: sorted, past `after`, 1,024 at the most
static void list_page(host_answer *p, const char *prefix, const uint32_t prefix_size, const char *after, const uint32_t after_size)
{
    const host_value **sorted = malloc((host_value_count ? host_value_count : 1u) * sizeof *sorted);
    uint32_t n = 0;
    for (uint32_t i = 0; i < host_value_count; i++) {
        const host_value *k = &host_values[i];
        if (k->key_size >= prefix_size && memcmp(k->key, prefix, prefix_size) == 0 && after_key(k, after, after_size)) sorted[n++] = k;
    }
    qsort(sorted, n, sizeof *sorted, compare_kept);
    p->outcome = TIDE_STORED_FOUND;
    p->more = n > TIDE_STORAGE_LIST_MOST;
    if (p->more) n = TIDE_STORAGE_LIST_MOST;
    p->data = malloc((size_t)n * (TIDE_STORAGE_KEY_MOST + 2u) + 1u);
    for (uint32_t i = 0; i < n; i++) p->size += put_key(p->data + p->size, sorted[i]->key, sorted[i]->key_size);
    free(sorted);
}

// What the match asked since the last call, carried out in its order: saves and deletes at once, loads and listings
// read now (so a load after a save of its key finds it) and answered once they're due
static void host_take(void)
{
    const uint32_t size = tide_managed_store_requests();
    if (!size) return;
    tide_reader r = {tide_managed_bytes(), size, 0, false};
    const uint32_t count = tide_read_u32(&r);
    for (uint32_t i = 0; i < count && !r.failed; i++) {
        const uint32_t kind = tide_read_u8(&r);
        const uint64_t id = tide_read_u64(&r);
        const uint32_t key_size = tide_read_u16(&r);
        const char *key = (const char *)tide_read_bytes(&r, key_size);
        const uint32_t data_size = tide_read_u32(&r);
        const uint8_t *data = tide_read_bytes(&r, data_size);
        const bool answered = kind == TIDE_STORAGE_LOAD || kind == TIDE_STORAGE_LIST;
        TIDE_CHECK(kind >= TIDE_STORAGE_SAVE && kind <= TIDE_STORAGE_LIST && (id != 0) == answered);
        TIDE_CHECK(key_size <= TIDE_STORAGE_KEY_MOST && (key_size || kind == TIDE_STORAGE_LIST) && (key || !key_size));
        if (r.failed) break;
        if (kind == TIDE_STORAGE_SAVE) {
            saves++;
            host_put(key, key_size, data, data_size);
        } else if (kind == TIDE_STORAGE_DELETE) {
            deletes++;
            host_erase(key, key_size);
        } else if (kind == TIDE_STORAGE_LOAD) {
            loads++;
            host_answer *p = queue_answer((double)id, kind, key, key_size);
            const host_value *k = host_find(key, key_size);
            if (strstr(p->key, "locked")) p->outcome = TIDE_STORED_FAILED;
            else if (!k) p->outcome = TIDE_STORED_MISSING;
            else {
                p->outcome = TIDE_STORED_FOUND;
                p->data = malloc(k->size ? k->size : 1u);
                memcpy(p->data, k->value, k->size);
                p->size = k->size;
            }
        } else {
            lists++;
            host_answer *p = queue_answer((double)id, kind, key ? key : "", key_size);
            list_page(p, key ? key : "", key_size, data ? (const char *)data : "", data_size);
        }
    }
    TIDE_CHECK(!r.failed && r.at == size);
}

// The answers that are due, given last first: the match puts them back in the order they were asked
static void host_give(void)
{
    if (holding) return;
    uint32_t kept_count = 0;
    for (uint32_t i = host_answer_count; i-- > 0;) {
        host_answer *p = &host_answers[i];
        if (p->due > updates) continue;
        TIDE_CHECK(tide_managed_stored(p->id, p->outcome, p->data, p->size, p->more) == 1);
        // Once is all an answer is taken
        TIDE_CHECK(tide_managed_stored(p->id, p->outcome, p->data, p->size, p->more) == 0);
        free(p->data);
        p->data = NULL;
        p->id = 0;
    }
    for (uint32_t i = 0; i < host_answer_count; i++) {
        if (host_answers[i].id != 0) host_answers[kept_count++] = host_answers[i];
    }
    host_answer_count = kept_count;
}

static const tide_world *match_world(void)
{
    return tide_server_world(tide_managed_server);
}

// One update, as the service makes it: the answers it has, the update, then what the match asked
static void host_step(void)
{
    host_give();
    now_at += 1.0 / 60.0;
    tide_managed_update(now_at);
    updates++;
    host_take();
}

static void host_step_to(const int32_t tick)
{
    for (uint32_t i = 0; i < 1000u && match_world()->Time.tick < tick; i++) host_step();
}

// A new instance of the module, as the service makes one after the last stopped: nothing the last had and hadn't
// handed over, and none of the answers the service read for it
static void fresh_instance(void)
{
    if (tide_managed_server) {
        tide_managed_dropping = true;
        tide_server_destroy(tide_managed_server);
        tide_managed_dropping = false;
    }
    tide_managed_server = NULL;
    tide_managed_ended = false;
    tide_managed_first = tide_managed_count = 0;
    (void)tide_managed_give(NULL, 0);
    free(tide_managed_asked.data);
    tide_managed_asked = (tide_writer){.grows = true};
    tide_managed_asked_count = 0;
    tide_managed_keeps = false;
    tide_managed_id_base = tide_managed_id_last = 0;
    forget_answers();
    holding = false;
    delay = 0;
    updates = loads = lists = saves = deletes = 0;
    now_at = 0.0;
}

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

// The keys of the event listings, in the order they came: boxes/'s, then every page/ key sorted
static int32_t expected_keys_hash(void)
{
    static char page_keys[1030][16];
    const char *sorted[1030];
    for (int i = 0; i < 1030; i++) {
        snprintf(page_keys[i], sizeof page_keys[i], "page/%d", i);
        sorted[i] = page_keys[i];
    }
    qsort(sorted, 1030, sizeof sorted[0], compare_text);
    int32_t hash = mix(mix(0, "boxes/1"), "boxes/2");
    for (int i = 0; i < 1030; i++) hash = mix(hash, sorted[i]);
    return hash;
}

// The Loaded answers' keys in the order tick 4 asked them
static int32_t expected_order(void)
{
    int32_t hash = 0;
    const char *asked[] = {"boxes/1", "boxes/2", "boxes/none", "locked", "gone", "same"};
    for (uint32_t i = 0; i < sizeof asked / sizeof asked[0]; i++) hash = mix(hash, asked[i]);
    return hash;
}

// What the match's events made of the host's answers
static void check_events(const Vault *v)
{
    TIDE_CHECK(v->found == 3);              // boxes/1, boxes/2, same (saved before its load in one tick)
    TIDE_CHECK(v->missing == 2);            // boxes/none, gone (deleted before its load in one tick)
    TIDE_CHECK(v->failed == 1);             // locked
    TIDE_CHECK(v->sum == (1 + 2 + 3) + (10 + 11) + (50 + 51));
    TIDE_CHECK(v->order == expected_order()); // Given last first, taken in the order asked
    TIDE_CHECK(v->listings == 3 && v->listFailed == 0); // boxes/, and page/ in two
    TIDE_CHECK(v->keys == 2 + 1030 && v->keysHash == expected_keys_hash());
}

// ...and its tasks
static void check_tasks(const Vault *v)
{
    TIDE_CHECK(v->awaited == 1 && v->awaitedSum == 1 + 2 + 3 && v->awaitedMissing == 1);
    TIDE_CHECK(v->pages == 2 && v->pageKeys == 1030);
}

static const host_value *value_of(const char *key)
{
    return host_find(key, (uint32_t)strlen(key));
}

TIDE_TEST(managed_store_saves_loads_lists_and_deletes_with_answers_given_later)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL && tide_game_api.stored_again != NULL);
    fresh_instance();
    forget_values();
    TIDE_CHECK(tide_managed_store(0) == 0); // No version is no store
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1);
    TIDE_CHECK(tide_managed_store(1) == 0); // Too late: its server is made
    delay = 3;
    host_take();
    TIDE_CHECK(saves == 0); // The start's save is handed over as the first tick ends
    host_step_to(3);
    TIDE_CHECK(saves == 1 + 4 + 1030 && value_of("start") && value_of("start")->value[0] == 9 && value_of("page/1029"));
    host_step_to(5); // Time.tick is the tick to come: tick 4 has run
    TIDE_CHECK(loads == 6 && lists == 2 && deletes == 1 && !value_of("gone") && value_of("same"));
    // Nothing answered until the host gives it, three updates on
    const uint32_t asked = updates;
    while (updates < asked + 3u) {
        TIDE_CHECK(match_world()->Vault.found == 0 && match_world()->Vault.listings == 0);
        host_step();
    }
    host_step_to(match_world()->Time.tick + 12);
    check_events(&match_world()->Vault);
    // The tasks of tick 8: answered as late, and each goes on
    host_step_to(match_world()->Time.tick > 8 ? match_world()->Time.tick + 16 : 24);
    check_tasks(&match_world()->Vault);
    // page/ asked again by its first page's handler, after that page's last key; the tasks' two loads and two pages
    TIDE_CHECK(loads == 6 + 2 && lists == 3 + 2 && host_answer_count == 0);
    check_events(&match_world()->Vault);
    fresh_instance();
}

// A match that goes on from its checkpoint in a new instance (the service's recovery: init, then restore): its tasks
// ask again what they waited for, its store is still the host's, and the start of the server the restore replaces
// writes nothing over it
TIDE_TEST(managed_store_a_match_restored_from_its_checkpoint_asks_again_and_keeps_its_store)
{
    fresh_instance();
    forget_values();
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1);
    host_step_to(8);
    // Tick 8's tasks ask as it ends: the host reads what they ask, and the instance stops before it gives the answers
    holding = true;
    host_step_to(12);
    TIDE_REQUIRE(loads == 6 + 2 && lists >= 3 + 1);
    check_events(&match_world()->Vault);
    TIDE_CHECK(match_world()->Vault.awaited == 0 && match_world()->Vault.pages == 0);
    const uint32_t size = tide_managed_save(NULL, 0);
    uint8_t *checkpoint = malloc(size);
    TIDE_REQUIRE(checkpoint && tide_managed_save(checkpoint, size) == size);
    const uint64_t hash = tide_game_api.hash_world(match_world());

    fresh_instance();
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1); // Its start saves "start" anew
    TIDE_REQUIRE(tide_managed_restore(checkpoint, size, now_at) == 1);
    TIDE_REQUIRE(tide_managed_restore(checkpoint, size, now_at) == 1); // Twice: the first's asks again go with it
    TIDE_CHECK(tide_game_api.hash_world(match_world()) == hash);
    host_take();
    TIDE_CHECK(saves == 0 && loads == 0 && lists == 0); // Asked as its first tick ends
    host_step();
    TIDE_CHECK(saves == 0 && loads == 2 && lists == 1); // What its tasks wait for, once each, and no start's save
    host_step_to(match_world()->Time.tick + 16);
    check_tasks(&match_world()->Vault);
    check_events(&match_world()->Vault);
    TIDE_CHECK(value_of("start") && value_of("start")->value[0] == 9); // The match's, not a fresh start's
    TIDE_CHECK(lists == 2 && host_answer_count == 0);
    free(checkpoint);
    fresh_instance();
}

// A host older than the store, which never offers it: the match plays on keeping nothing, as before
TIDE_TEST(managed_store_a_host_with_no_store_keeps_nothing)
{
    fresh_instance();
    forget_values();
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1);
    TIDE_CHECK(tide_managed_store(1) == 0);
    host_step_to(24);
    const Vault *v = &match_world()->Vault;
    TIDE_CHECK(tide_managed_store_requests() == 0 && saves == 0 && loads == 0 && host_value_count == 0);
    TIDE_CHECK(v->found == 0 && v->missing == 6 && v->failed == 0 && v->order == expected_order());
    TIDE_CHECK(v->listings == 2 && v->keys == 0 && v->listFailed == 0);
    TIDE_CHECK(v->awaited == 0 && v->awaitedMissing == 2 && v->pages == 1 && v->pageKeys == 0);
    TIDE_CHECK(tide_managed_stored(1.0, TIDE_STORED_MISSING, NULL, 0, 0) == 0);
    fresh_instance();
}

static host_answer *pending_for(const uint32_t kind, const char *key)
{
    for (uint32_t i = 0; i < host_answer_count; i++) {
        if (host_answers[i].kind == kind && strcmp(host_answers[i].key, key) == 0) return &host_answers[i];
    }
    return NULL;
}

// Keys from strings, each a varint and its bytes
static uint32_t keys_of(uint8_t *out, const char *const *keys, const uint32_t count)
{
    uint32_t size = 0;
    for (uint32_t i = 0; i < count; i++) size += put_key(out + size, keys[i], (uint32_t)strlen(keys[i]));
    return size;
}

// Answers that aren't ones change nothing: the right one is still taken after them, and the match is as it would be
TIDE_TEST(managed_store_refuses_answers_that_arent_ones)
{
    fresh_instance();
    forget_values();
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1);
    holding = true;
    host_step_to(5);
    const host_answer *load = pending_for(TIDE_STORAGE_LOAD, "boxes/1"), *list = pending_for(TIDE_STORAGE_LIST, "boxes/");
    TIDE_REQUIRE(load && list && load->outcome == TIDE_STORED_FOUND && load->size == 3);
    const uint8_t three[3] = {1, 2, 3};
    // Ids it never gave
    TIDE_CHECK(tide_managed_stored(0.0, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(-1.0, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(NAN, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(load->id + 0.5, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored((double)tide_managed_id_last + 1.0, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(1e300, TIDE_STORED_FOUND, three, 3, 0) == 0);
    // A load's answer that isn't one
    TIDE_CHECK(tide_managed_stored(load->id, 3u, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_MISSING, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_FAILED, three, 3, 0) == 0);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_FOUND, three, 3, 1) == 0);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_FOUND, three, 3, 2) == 0);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_FOUND, NULL, 3, 0) == 0);
    uint8_t *huge = calloc(1, (size_t)TIDE_STORAGE_VALUE_MOST + 1u);
    TIDE_REQUIRE(huge != NULL);
    TIDE_CHECK(tide_managed_stored(load->id, TIDE_STORED_FOUND, huge, TIDE_STORAGE_VALUE_MOST + 1u, 0) == 0);
    free(huge);
    // A listing's that isn't one
    uint8_t bytes[1100 * 16];
    const char *unsorted[] = {"boxes/2", "boxes/1"}, *outside[] = {"boxes/1", "other"}, *twice[] = {"boxes/1", "boxes/1"};
    const char *bad_utf8[] = {"boxes/\xff"}, *right[] = {"boxes/1", "boxes/2"};
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, unsorted, 2), 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, outside, 2), 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, twice, 2), 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, bad_utf8, 1), 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, right, 2), 1) == 0); // more, with 2
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FAILED, bytes, keys_of(bytes, right, 2), 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FAILED, NULL, 0, 1) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_MISSING, NULL, 0, 0) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, right, 2) - 1u, 0) == 0); // Cut short
    const uint8_t empty_key[] = {0};
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, empty_key, 1, 0) == 0);
    char long_key[TIDE_STORAGE_KEY_MOST + 2u];
    memset(long_key, 'x', sizeof long_key);
    memcpy(long_key, "boxes/", 6);
    long_key[TIDE_STORAGE_KEY_MOST + 1u] = '\0';
    const char *too_long[] = {long_key};
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, too_long, 1), 0) == 0);
    static char many[TIDE_STORAGE_LIST_MOST + 1u][16];
    const char *many_keys[TIDE_STORAGE_LIST_MOST + 1u];
    for (uint32_t i = 0; i <= TIDE_STORAGE_LIST_MOST; i++) {
        snprintf(many[i], sizeof many[i], "boxes/%04u", i);
        many_keys[i] = many[i];
    }
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, many_keys, TIDE_STORAGE_LIST_MOST + 1u), 1) == 0);
    TIDE_CHECK(tide_managed_stored(list->id, TIDE_STORED_FOUND, bytes, keys_of(bytes, many_keys, TIDE_STORAGE_LIST_MOST + 1u), 0) == 0);
    // ...and then the right ones, and the match is as the first test's
    holding = false;
    host_step_to(match_world()->Time.tick + 12);
    check_events(&match_world()->Vault);
    host_step_to(match_world()->Time.tick > 8 ? match_world()->Time.tick + 16 : 24);
    check_tasks(&match_world()->Vault);

    // A server a restore replaced: its ids are refused, and the one made asks again with its own
    fresh_instance();
    forget_values();
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_init(now_at, 7, NULL, 0, NULL, 0) == 1);
    holding = true;
    host_step_to(12);
    const host_answer *waiting = pending_for(TIDE_STORAGE_LOAD, "boxes/1");
    TIDE_REQUIRE(waiting != NULL);
    const double stale = waiting->id;
    const uint32_t size = tide_managed_save(NULL, 0);
    uint8_t *checkpoint = malloc(size);
    TIDE_REQUIRE(checkpoint && tide_managed_save(checkpoint, size) == size);
    TIDE_REQUIRE(tide_managed_restore(checkpoint, size, now_at) == 1);
    forget_answers(); // The service drops what it read for the server that went
    // Once the new server's tasks have asked again (a load of boxes/1 first, as the stale id's was), that id still
    // names nothing of it
    host_step();
    TIDE_REQUIRE(loads == 6 + 2 + 2 && host_answer_count == 3);
    TIDE_CHECK(tide_managed_stored(stale, TIDE_STORED_FOUND, three, 3, 0) == 0);
    TIDE_CHECK(stale < host_answers[0].id);
    holding = false;
    host_step_to(match_world()->Time.tick + 16);
    check_tasks(&match_world()->Vault);
    free(checkpoint);
    fresh_instance();
    forget_values();
}

// ---------------------------------------------------------------------------
// The line itself with a host that keeps the store (tide_storage_host), as a server's has one

typedef struct line_host {
    uint32_t calls, requests, saves;
    uint64_t numbers[8];
} line_host;

static void line_keep(void *user, const tide_storage *requests, const uint64_t number)
{
    line_host *h = user;
    if (h->calls < 8u) h->numbers[h->calls] = number;
    h->calls++;
    h->requests += requests->count;
    for (uint32_t i = 0; i < requests->count; i++) h->saves += requests->items[i].kind == TIDE_STORAGE_SAVE;
}

static uint8_t *line_answers[8];
static uint32_t line_sizes[8], line_answer_count;

static void line_take(void *user, uint8_t *message, const uint32_t size)
{
    (void)user;
    if (line_answer_count < 8u) {
        line_answers[line_answer_count] = message;
        line_sizes[line_answer_count++] = size;
    } else free(message);
}

static tide_str line_text(const char *s)
{
    return (tide_str){s, (int32_t)strlen(s), (int32_t)strlen(s)};
}

static int line_world;

static void line_ask(tide_storage *store, const uint32_t kind, const char *key)
{
    tide_storage_bind(&line_world, store);
    tide_storage_ask(&line_world, kind, line_text(key), (tide_list){0}, (tide_str){0}, TIDE_STORAGE_TO_EVENT, (tide_entity){0});
    tide_storage_unbind(&line_world);
}

// Answers given in any order go to ticks in the order asked; a batch with nothing to answer isn't waited for; a
// forgotten one's answers are refused; a server that stops hands its saves alone, and waits for nothing
TIDE_TEST(managed_store_a_hosted_line_keeps_the_order_asked_and_waits_for_nothing_else)
{
    line_host h = {0};
    const tide_storage_host host = {.user = &h, .keep = line_keep};
    tide_storage_line line = {.host = &host};
    tide_storage store = {0};
    line_ask(&store, TIDE_STORAGE_SAVE, "a");
    line_ask(&store, TIDE_STORAGE_LOAD, "a");
    line_ask(&store, TIDE_STORAGE_LIST, "");
    tide_storage_send(&line, &store, NULL, true); // Numbers 0 and 1
    line_ask(&store, TIDE_STORAGE_LOAD, "b");
    tide_storage_send(&line, &store, NULL, true); // Number 2
    line_ask(&store, TIDE_STORAGE_SAVE, "c");
    tide_storage_send(&line, &store, NULL, true); // Nothing to answer: no batch
    TIDE_CHECK(h.calls == 3 && h.requests == 5 && h.numbers[0] == 0 && h.numbers[1] == 2 && tide_storage_waiting(&line) == 2);
    const uint8_t value[2] = {7, 8};
    TIDE_CHECK(tide_storage_answer(&line, 2, TIDE_STORED_FOUND, value, 2, false));
    TIDE_CHECK(tide_storage_receive(&line, line_take, NULL) == 0); // It waits for the answers asked before it
    TIDE_CHECK(tide_storage_answer(&line, 1, TIDE_STORED_FOUND, NULL, 0, false)); // No keys
    TIDE_CHECK(!tide_storage_answer(&line, 1, TIDE_STORED_FOUND, NULL, 0, false)); // Answered already
    TIDE_CHECK(tide_storage_receive(&line, line_take, NULL) == 0);
    TIDE_CHECK(tide_storage_answer(&line, 0, TIDE_STORED_MISSING, NULL, 0, false));
    TIDE_CHECK(tide_storage_receive(&line, line_take, NULL) == 3 && tide_storage_waiting(&line) == 0);
    for (uint32_t i = 0; i < line_answer_count; i++) {
        tide_stored read;
        TIDE_CHECK(tide_stored_read(line_answers[i], line_sizes[i], &read) && read.number == i);
        if (i == 2) TIDE_CHECK(read.outcome == TIDE_STORED_FOUND && read.data_size == 2 && read.data[1] == 8);
        free(line_answers[i]);
    }
    line_answer_count = 0;
    TIDE_CHECK(!tide_storage_answer(&line, 0, TIDE_STORED_MISSING, NULL, 0, false)); // Its batch is gone
    TIDE_CHECK(!tide_storage_answer(&line, 3, TIDE_STORED_MISSING, NULL, 0, false)); // Never asked
    // Forgotten (a rewind): nothing waits for its answers, which are refused
    line_ask(&store, TIDE_STORAGE_LOAD, "d");
    tide_storage_send(&line, &store, NULL, true); // Number 3
    tide_storage_forget(&line);
    TIDE_CHECK(!tide_storage_answer(&line, 3, TIDE_STORED_MISSING, NULL, 0, false));
    TIDE_CHECK(tide_storage_receive(&line, line_take, NULL) == 0 && tide_storage_waiting(&line) == 0);
    // A server that stops: its saves and deletes alone
    const uint32_t calls = h.calls, requests = h.requests;
    line_ask(&store, TIDE_STORAGE_LOAD, "e");
    tide_storage_send(&line, &store, NULL, false);
    TIDE_CHECK(h.calls == calls && store.count == 0); // Nothing to hand over
    line_ask(&store, TIDE_STORAGE_SAVE, "f");
    line_ask(&store, TIDE_STORAGE_LOAD, "g");
    line_ask(&store, TIDE_STORAGE_DELETE, "f");
    tide_storage_send(&line, &store, NULL, false);
    TIDE_CHECK(h.calls == calls + 1u && h.requests == requests + 2u && tide_storage_waiting(&line) == 0);
    // ...and doesn't wait for its answers
    line_ask(&store, TIDE_STORAGE_LOAD, "h");
    tide_storage_send(&line, &store, NULL, true);
    TIDE_CHECK(tide_storage_waiting(&line) == 1);
    tide_storage_finish(&line);
    TIDE_CHECK(tide_storage_waiting(&line) == 0 && line.first == NULL && line.last == NULL);
    tide_storage_free(&store);
}
