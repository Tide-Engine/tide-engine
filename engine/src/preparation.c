#include "tide/preparation.h"

#include <stdlib.h>
#include <stdio.h>

#include "tide/page.h"
#include "tide/worker.h"
#include "tide/time.h"

struct tide_preparation {
    const tide_game *game;
    const tide_jobs *jobs;
    tide_workers *workers;
    uint32_t ticket;
    void *world;
    void *published, *view;
    uint8_t *input;
    uint32_t input_size, locked;
    const void *source;
    uint64_t source_hash;
    tide_event_log events;
    tide_event_log *per_player;
    uint32_t players;
};

static TIDE_THREAD_LOCAL tide_preparation *current;

static uint8_t *pack(const tide_game *game, const void *world, uint32_t *size)
{
    *size = game->pack_world(world, NULL, 0);
    uint8_t *bytes = tide_alloc(*size);
    if (!*size || game->pack_world(world, bytes, *size) != *size) {
        fputs("tide: couldn't capture the scene preparation world\n", stderr);
        abort();
    }
    return bytes;
}

static void unpack(const tide_game *game, void *world, const uint8_t *bytes, uint32_t size)
{
    if (!game->unpack_world(world, bytes, size)) {
        fputs("tide: couldn't restore the scene preparation world\n", stderr);
        abort();
    }
}

static void free_world(const tide_game *game, void *world)
{
    if (!world) return;
    if (game->free_world) game->free_world(world);
    free(world);
}

void tide_preparation_publish(void *world)
{
    tide_preparation *p = current;
    if (!p || p->world != world || tide_worker_cancelled()) return;
    // Page reference counts are not atomic. Neither a live worker nor its
    // published view may share COW pages with a world another thread uses.
    uint32_t size;
    uint8_t *bytes = pack(p->game, world, &size);
    void *snapshot = tide_alloc_zeroed(1, p->game->world_size);
    unpack(p->game, snapshot, bytes, size);
    free(bytes);
    while (__atomic_exchange_n(&p->locked, 1u, __ATOMIC_ACQUIRE)) {
        if (tide_worker_cancelled()) { free_world(p->game, snapshot); return; }
        tide_yield();
    }
    tide_memory_sync();
    free_world(p->game, p->published);
    p->published = snapshot;
    __atomic_store_n(&p->locked, 0u, __ATOMIC_RELEASE);
}

const void *tide_preparation_view(const tide_preparation *p) { return p ? p->view : NULL; }

static void prepare(void *context)
{
    tide_preparation *p = context;
    const tide_jobs *previous = tide_worker_enter(p->jobs);
    tide_preparation *outer = current;
    current = p;
    unpack(p->game, p->world, p->input, p->input_size);
    p->game->prepare(p->world, p->jobs, p->per_player, &p->events);
    current = outer;
    tide_worker_leave(previous);
}

bool tide_preparation_update(tide_preparation **state, const tide_game *game,
                             void *world, const tide_jobs *jobs, tide_event_log *events)
{
    return tide_preparation_update_clients(state, game, world, jobs, NULL, events);
}

bool tide_preparation_update_clients(tide_preparation **state, const tide_game *game,
                                     void *world, const tide_jobs *jobs, tide_event_log *per_player, tide_event_log *events)
{
    if (!game->prepare_pending || !game->prepare) return true;
    tide_preparation *p = *state;
    if (p && (p->game != game || p->source != world || p->source_hash != game->hash_world(world))) {
        tide_preparation_free(p);
        *state = NULL;
        p = NULL;
    }
    if (!p) {
        if (!game->prepare_pending(world)) return true;
        p = *state = tide_alloc_zeroed(1, sizeof *p);
        p->game = game;
        p->jobs = jobs;
        p->world = tide_alloc_zeroed(1, game->world_size);
        p->source = world;
        p->source_hash = game->hash_world(world);
        p->input = pack(game, world, &p->input_size);
        if (per_player) {
            p->players = tide_game_players(game);
            p->per_player = tide_alloc_zeroed(p->players, sizeof *p->per_player);
        }
        p->ticket = tide_worker_start(&p->workers, jobs, prepare, NULL, p);
    }
    if (!tide_worker_poll(p->workers, p->ticket)) {
        if (!__atomic_exchange_n(&p->locked, 1u, __ATOMIC_ACQUIRE)) {
            tide_memory_sync();
            if (p->published) {
                free_world(game, p->view);
                p->view = p->published;
                p->published = NULL;
            }
            __atomic_store_n(&p->locked, 0u, __ATOMIC_RELEASE);
        }
        return false;
    }
    game->copy_world(world, p->world);
    if (events) tide_event_log_take(events, &p->events);
    if (per_player && p->per_player)
        for (uint32_t i = 0; i < p->players; i++) tide_event_log_take(&per_player[i], &p->per_player[i]);
    tide_preparation_free(p);
    *state = NULL;
    return !game->prepare_pending(world);
}

void tide_preparation_free(tide_preparation *p)
{
    if (!p) return;
    tide_workers_free(p->workers);
    free_world(p->game, p->world);
    free_world(p->game, p->published);
    free_world(p->game, p->view);
    free(p->input);
    tide_event_log_free(&p->events);
    for (uint32_t i = 0; i < p->players; i++) tide_event_log_free(&p->per_player[i]);
    free(p->per_player);
    free(p);
}
