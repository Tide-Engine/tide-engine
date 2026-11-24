#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
// The managed reactor's sharded half (tide/managed_run.h with TIDE_GAME_SHARDED) built as a native program, as
// managed_store_test.c builds the rest: a world's root shard started anew from its coordinator's start (its start's
// save handed over with its first tick), then started again from its world and its journal, as the service starts a
// shard whose instance went (shard_stage, shard_replay, shard_begin), against a fake service's store.
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
// The shard's journal, as the service keeps it: each tick's record

typedef struct journal_record {
    uint32_t tick, size;
    uint8_t *data;
} journal_record;

static journal_record records[2048];
static uint32_t record_count;

void tide_managed_journal(const uint32_t tick, const void *data, const uint32_t size)
{
    if (record_count == sizeof records / sizeof records[0]) return;
    journal_record *r = &records[record_count++];
    r->tick = tick;
    r->size = size;
    r->data = malloc(size ? size : 1u);
    if (size) memcpy(r->data, data, size);
}

static void forget_records(void)
{
    for (uint32_t i = 0; i < record_count; i++) free(records[i].data);
    record_count = 0;
}

// ---------------------------------------------------------------------------
// The fake service: a value for each key, and the answers it has read and not given. A key under held/ is read and
// its answer held until the test lets it go.

typedef struct kept_value {
    char key[TIDE_STORAGE_KEY_MOST + 1u];
    uint8_t value[64];
    uint32_t size;
} kept_value;

static kept_value values[64];
static uint32_t value_count;

typedef struct waiting_answer {
    double id;
    char key[TIDE_STORAGE_KEY_MOST + 1u];
    uint32_t outcome, size;
    uint8_t value[64];
} waiting_answer;

static waiting_answer answers[64];
static uint32_t answer_count;
static bool holding = true; // held/ keys' answers wait
static uint32_t loads, saves, lists;
static double now_at;

static kept_value *find_value(const char *key)
{
    for (uint32_t i = 0; i < value_count; i++) {
        if (strcmp(values[i].key, key) == 0) return &values[i];
    }
    return NULL;
}

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
        const char *key_bytes = (const char *)tide_read_bytes(&r, key_size);
        const uint32_t data_size = tide_read_u32(&r);
        const uint8_t *data = tide_read_bytes(&r, data_size);
        if (r.failed) break;
        char key[TIDE_STORAGE_KEY_MOST + 1u] = {0};
        if (key_size) memcpy(key, key_bytes, key_size);
        if (kind == TIDE_STORAGE_SAVE) {
            saves++;
            kept_value *v = find_value(key);
            if (!v && value_count < sizeof values / sizeof values[0]) v = &values[value_count++];
            TIDE_CHECK(v != NULL && data_size <= sizeof v->value);
            if (!v || data_size > sizeof v->value) continue;
            strcpy(v->key, key);
            if (data_size) memcpy(v->value, data, data_size);
            v->size = data_size;
        } else if (kind == TIDE_STORAGE_LOAD) {
            loads++;
            TIDE_CHECK(answer_count < sizeof answers / sizeof answers[0]);
            if (answer_count == sizeof answers / sizeof answers[0]) continue;
            waiting_answer *a = &answers[answer_count++];
            memset(a, 0, sizeof *a);
            a->id = (double)id;
            strcpy(a->key, key);
            const kept_value *v = find_value(key);
            a->outcome = v ? TIDE_STORED_FOUND : TIDE_STORED_MISSING;
            if (v) {
                memcpy(a->value, v->value, v->size);
                a->size = v->size;
            }
        } else if (kind == TIDE_STORAGE_LIST) {
            lists++;
        }
    }
    TIDE_CHECK(!r.failed && r.at == size);
}

static void host_give(void)
{
    uint32_t kept_count = 0;
    for (uint32_t i = 0; i < answer_count; i++) {
        waiting_answer *a = &answers[i];
        if (holding && strncmp(a->key, "held/", 5) == 0) {
            answers[kept_count++] = *a;
            continue;
        }
        TIDE_CHECK(tide_managed_stored(a->id, a->outcome, a->size ? a->value : NULL, a->size, 0) == 1);
    }
    answer_count = kept_count;
}

static void host_step(void)
{
    host_give();
    now_at += 1.0 / 60.0;
    tide_managed_update(now_at);
    host_take();
}

static const tide_world *shard_world(void)
{
    return tide_server_world(tide_managed_server);
}

// The shard's keeper
static Keeper keeper(void)
{
    const tide_world *w = shard_world();
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_entity_alive(&w->entities, e) && tide_has_Keeper(w, e) && !tide_has_tide_ghost(w, e)) return tide_get_Keeper(w, e);
    }
    return (Keeper){.tries = -1};
}

// The coordinator of a world of one region: the root shard's start (its CONFIG message), as the service takes it
static uint8_t root_config[4096];
static uint32_t root_config_size, root_tick;

static bool coordinate(void)
{
    uint8_t desc[77] = {0};
    const double lo[3] = {-32.0, -32.0, -32.0}, hi[3] = {32.0, 32.0, 32.0}, seconds = 60.0;
    const uint32_t split[3] = {1u, 1u, 1u}, most = 2000u;
    const float busiest = 0.5f;
    memcpy(desc, lo, 24);
    memcpy(desc + 24, hi, 24);
    memcpy(desc + 48, split, 12);
    memcpy(desc + 60, &most, 4);
    desc[64] = 1;
    memcpy(desc + 65, &seconds, 8);
    memcpy(desc + 73, &busiest, 4);
    if (tide_managed_coordinator_create(0.0, desc, sizeof desc) != 1) return false;
    const uint32_t size = tide_managed_coordinator_actions();
    tide_reader r = {tide_managed_bytes(), size, 0, false};
    const uint32_t count = tide_read_u32(&r);
    bool found = false;
    for (uint32_t i = 0; i < count && !r.failed; i++) {
        const uint32_t kind = tide_read_u8(&r), shard = tide_read_u32(&r), tick = tide_read_u32(&r);
        const uint32_t config_size = tide_read_u32(&r);
        const uint8_t *config = tide_read_bytes(&r, config_size);
        (void)tide_read_bytes(&r, tide_read_u32(&r)); // Its world: none for a start anew
        (void)tide_read_bytes(&r, tide_read_u32(&r));
        if (!r.failed && kind == TIDE_SHARD_ACTION_START && shard == TIDE_SHARD_ROOT && config_size <= sizeof root_config) {
            memcpy(root_config, config, config_size);
            root_config_size = config_size;
            root_tick = tick;
            found = true;
        }
    }
    // The coordinator is an instance of its own: this one goes on as the shard
    tide_shard_coordinator_destroy(tide_managed_coordinator);
    tide_managed_coordinator = NULL;
    (void)tide_managed_give(NULL, 0);
    return found && !r.failed;
}

// The instance gone, as the service's goes: nothing it had and hadn't handed over, nor the answers the service read
// for it
static void instance_gone(void)
{
    if (tide_managed_agent) tide_shard_agent_destroy(tide_managed_agent);
    tide_managed_agent = NULL;
    if (tide_managed_server) {
        tide_managed_dropping = true;
        tide_server_destroy(tide_managed_server);
        tide_managed_dropping = false;
    }
    tide_managed_server = NULL;
    tide_managed_ticks_before = 0;
    tide_managed_ended = false;
    tide_managed_first = tide_managed_count = 0;
    (void)tide_managed_give(NULL, 0);
    free(tide_managed_asked.data);
    tide_managed_asked = (tide_writer){.grows = true};
    tide_managed_asked_count = 0;
    tide_managed_keeps = false;
    tide_managed_id_base = tide_managed_id_last = 0;
    answer_count = 0;
    loads = saves = lists = 0;
}

TIDE_TEST(managed_store_a_shard_starts_hands_its_start_over_and_goes_on_from_its_journal)
{
    TIDE_REQUIRE(tide_game_api.stored != NULL && tide_game_api.stored_again != NULL && tide_game_api.shards != NULL);
    value_count = 0;
    forget_records();
    holding = true;
    now_at = 0.0;
    TIDE_REQUIRE(coordinate());
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_shard_new(now_at, 7, TIDE_SHARD_ROOT, root_tick, root_config, root_config_size, NULL, 0) == 1);
    host_take();
    TIDE_CHECK(saves == 0); // The start's save is the first tick's to hand over
    host_step();
    const kept_value *started = find_value("started");
    TIDE_CHECK(started != NULL && started->size == 4); // ...and it was
    // On to tick 3, then the shard's world as a checkpoint has it, and the journal goes on from it
    for (int i = 0; i < 100 && shard_world()->Time.tick < 3; i++) host_step();
    TIDE_REQUIRE(shard_world()->Time.tick == 3 && record_count > 0);
    const uint32_t checkpoint_tick = records[record_count - 1].tick + 1u;
    const uint32_t world_size = tide_game_api.pack_world(shard_world(), NULL, 0);
    uint8_t *world = malloc(world_size);
    TIDE_REQUIRE(world && tide_game_api.pack_world(shard_world(), world, world_size) == world_size);
    // Tick 4 asks for "a", which is answered and its task goes on, in ticks the journal has; tick 6 for "held/b", which
    // waits
    for (int i = 0; i < 100 && (keeper().tries < 1 || shard_world()->Time.tick < 10); i++) host_step();
    TIDE_CHECK(loads == 2 && answer_count == 1);
    Keeper k = keeper();
    TIDE_CHECK(k.got == 3 && k.held == 0 && k.tries == 1);
    const uint64_t hash = tide_game_api.hash_world(shard_world());
    const int32_t tick = shard_world()->Time.tick;
    const uint32_t journaled = record_count;

    // A new instance, from the world and the journal since: the ticks the answer to "a" came in are replayed, so only
    // "held/b" is asked again
    instance_gone();
    TIDE_REQUIRE(tide_managed_store(1) == 1);
    TIDE_REQUIRE(tide_managed_shard_stage(world, world_size, checkpoint_tick, NULL, 0) == 1);
    for (uint32_t i = 0; i < journaled; i++) TIDE_CHECK(tide_managed_shard_replay(records[i].data, records[i].size) == 1);
    TIDE_REQUIRE(tide_managed_shard_begin(now_at, 7, TIDE_SHARD_ROOT) == 1);
    TIDE_CHECK(shard_world()->Time.tick == tick && tide_game_api.hash_world(shard_world()) == hash);
    host_take();
    TIDE_CHECK(loads == 0 && saves == 0); // Asked as its first tick ends
    host_step();
    TIDE_CHECK(loads == 1 && lists == 0 && answer_count == 1 && strcmp(answers[0].key, "held/b") == 0);
    // Let go, it's answered once, there
    holding = false;
    for (int i = 0; i < 100 && keeper().tries < 2; i++) host_step();
    k = keeper();
    TIDE_CHECK(k.got == 3 && k.held == 5 && k.tries == 2);
    TIDE_CHECK(loads == 1 && answer_count == 0);
    free(world);
    instance_gone();
    forget_records();
}
