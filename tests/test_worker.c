#include "tide/worker.h"
#include "tide_test.h"

#include <stdlib.h>

#include "tide/page.h"

typedef struct worker_probe {
    uint32_t ran;
    uint32_t released;
} worker_probe;

typedef struct deferred_workers {
    tide_background *first, *last;
} deferred_workers;

static void defer(void *self, tide_background *job)
{
    deferred_workers *queue = self;
    job->next = NULL;
    if (queue->last) queue->last->next = job;
    else queue->first = job;
    queue->last = job;
}

static void run(void *context) { ((worker_probe *)context)->ran++; }
static void release(void *context) { ((worker_probe *)context)->released++; }

static bool help(void *self, const bool helpers_only)
{
    deferred_workers *queue = self;
    if (!queue->first || helpers_only) return false;
    tide_background *job = queue->first;
    queue->first = job->next;
    if (!queue->first) queue->last = NULL;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

TIDE_TEST(worker_abandoned_queued_calculations_are_skipped)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe cancelled = {0}, current = {0};
    const uint32_t old = tide_worker_start(&workers, &jobs, run, release, &cancelled);
    const uint32_t active = tide_worker_start(&workers, &jobs, run, release, &current);
    tide_worker_forget(workers, old);
    TIDE_CHECK(cancelled.released == 0);
    TIDE_CHECK(tide_worker_poll(workers, old) == NULL);
    while (help(&queue, false)) { }
    TIDE_CHECK(cancelled.ran == 0);
    TIDE_CHECK(tide_worker_poll(workers, active) == &current);
    TIDE_CHECK(tide_worker_poll(workers, active) == &current);
    TIDE_CHECK(current.ran == 1);
    tide_workers_collect(workers);
    TIDE_CHECK(cancelled.released == 1);
    TIDE_CHECK(current.released == 0);
    tide_worker_forget(workers, active);
    tide_worker_forget(workers, old);
    TIDE_CHECK(current.released == 1 && cancelled.released == 1);
    tide_workers_free(workers);
}

TIDE_TEST(worker_release_waits_for_provider_completion)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe probe = {0};
    const uint32_t ticket = tide_worker_start(&workers, &jobs, run, release, &probe);
    tide_background *job = queue.first;
    queue.first = queue.last = NULL;
    job->work(job->context);
    TIDE_CHECK(probe.ran == 1);
    // The callback has returned, but the provider still owns the queue item.
    tide_worker_forget(workers, ticket);
    tide_workers_collect(workers);
    TIDE_CHECK(probe.released == 0);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    tide_workers_collect(workers);
    TIDE_CHECK(probe.released == 1);
    tide_workers_free(workers);
}

TIDE_TEST(worker_disposal_abandons_every_queued_calculation_before_helping)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe probes[4] = {{0}};
    for (int i = 0; i < 4; i++) tide_worker_start(&workers, &jobs, run, release, &probes[i]);
    tide_workers_free(workers);
    TIDE_CHECK(queue.first == NULL);
    for (int i = 0; i < 4; i++) TIDE_CHECK(probes[i].ran == 0 && probes[i].released == 1);
    workers = NULL;
    for (int i = 0; i < 4; i++) tide_worker_start(&workers, NULL, run, release, &probes[i]);
    tide_workers_free(workers);
    for (int i = 0; i < 4; i++) TIDE_CHECK(probes[i].ran == 0 && probes[i].released == 2);
}

TIDE_TEST(worker_burst_lookup_and_retirement_keep_tickets_independent)
{
    enum { count = 16384 };
    worker_probe *probes = tide_alloc_zeroed(count, sizeof *probes);
    uint32_t *tickets = tide_alloc_zeroed(count, sizeof *tickets);
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    for (uint32_t i = 0; i < count; i++)
        tickets[i] = tide_worker_start(&workers, &jobs, run, release, &probes[i]);
    // Retire half while the provider still owns their items, in a permutation
    // unrelated to insertion, list or bucket order. Repeated forget is harmless.
    for (uint32_t k = 0; k < count; k++) {
        const uint32_t i = (k * 4051u) & (count - 1u);
        if (i & 1u) continue;
        tide_worker_forget(workers, tickets[i]);
        tide_worker_forget(workers, tickets[i]);
    }
    while (help(&queue, false)) { }
    tide_workers_collect(workers);
    for (uint32_t k = 0; k < count; k++) {
        const uint32_t i = (k * 4051u) & (count - 1u);
        if (i & 1u) {
            TIDE_CHECK(tide_worker_poll(workers, tickets[i]) == &probes[i]);
            TIDE_CHECK(probes[i].ran == 1 && probes[i].released == 0);
        } else {
            TIDE_CHECK(tide_worker_poll(workers, tickets[i]) == NULL);
            TIDE_CHECK(probes[i].ran == 0 && probes[i].released == 1);
        }
        tide_worker_forget(workers, tickets[i]);
    }
    // Reuse the group after every bucket has been emptied. Stale tickets must
    // neither publish nor abandon any new item.
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t ticket = tide_worker_start(&workers, NULL, run, release, &probes[i]);
        tide_worker_forget(workers, tickets[i]);
        TIDE_CHECK(tide_worker_poll(workers, ticket) == &probes[i]);
        tide_worker_forget(workers, ticket);
        TIDE_CHECK(probes[i].released == 2);
    }
    tide_workers_free(workers);
    free(tickets);
    free(probes);
}

TIDE_TEST(worker_admission_bounds_pending_captures_and_frame_transfers)
{
    tide_workers *workers = NULL;
    const tide_worker_limits limits = {.pending = 2, .capture_bytes = 24, .transfer_bytes = 16};
    tide_workers_frame(&workers, &limits, NULL);
    worker_probe probes[3] = {{0}};
    const uint32_t a = tide_worker_reserve(&workers, NULL);
    const uint32_t b = tide_worker_reserve(&workers, NULL);
    TIDE_REQUIRE(a && b);
    TIDE_CHECK(tide_worker_poll(workers, a) == NULL); // Not submitted yet
    TIDE_CHECK(!tide_worker_reserve(&workers, NULL));
    tide_worker_transferred(workers, 8, 0, false);
    tide_worker_submit(workers, a, run, release, &probes[0], 8);
    tide_worker_transferred(workers, 8, 0, false);
    tide_worker_submit(workers, b, run, release, &probes[1], 8);
    TIDE_CHECK(!tide_worker_transfer_ready(workers));
    TIDE_CHECK(tide_worker_poll(workers, a) == &probes[0]);
    tide_worker_forget(workers, a);
    TIDE_CHECK(!tide_worker_reserve(&workers, NULL)); // Frame budget is still spent
    tide_workers_frame(&workers, &limits, NULL);
    const uint32_t c = tide_worker_reserve(&workers, NULL);
    TIDE_REQUIRE(c);
    tide_worker_transferred(workers, 32, 0, false);
    tide_worker_submit(workers, c, run, release, &probes[2], 32); // Oversize finishes; no permanent starvation
    TIDE_CHECK(!tide_worker_transfer_ready(workers));
    tide_workers_frame(&workers, &limits, NULL);
    TIDE_CHECK(tide_worker_transfer_ready(workers));
    tide_worker_forget(workers, b);
    TIDE_CHECK(!tide_worker_reserve(&workers, NULL)); // Retained oversized capture
    TIDE_CHECK(tide_worker_poll(workers, c) == &probes[2]);
    tide_worker_forget(workers, c);
    const uint32_t unused = tide_worker_reserve(&workers, NULL);
    TIDE_REQUIRE(unused);
    tide_worker_forget(workers, unused); // Reservation has no callback/context
    tide_workers_free(workers);
    for (int i = 0; i < 3; i++) TIDE_CHECK(probes[i].released == 1);
}

TIDE_TEST(worker_profiles_publish_once_after_provider_completion)
{
    tide_profile profile = {0};
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .profile = &profile, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe probe = {0};
    const uint32_t ticket = tide_worker_start(&workers, &jobs, run, release, &probe);
    TIDE_CHECK(profile.workers.count == 0);
    TIDE_REQUIRE(help(&queue, false));
    TIDE_CHECK(profile.workers.count == 0); // Background code never mutates the profile
    TIDE_CHECK(tide_worker_poll(workers, ticket) == &probe);
    TIDE_REQUIRE(profile.workers.count == 4);
    TIDE_CHECK(profile.worker_runs == 1 && profile.workers.items[0].measurements == 1);
    TIDE_CHECK(profile.workers.items[1].measurements == 1);
    TIDE_CHECK(tide_worker_poll(workers, ticket) == &probe);
    tide_worker_forget(workers, ticket);
    TIDE_CHECK(profile.worker_runs == 1);
    tide_profile_clear(&profile);
    TIDE_CHECK(profile.worker_runs == 0 && profile.workers.items[1].total == 0.0);
    TIDE_CHECK(profile.workers.items[1].measurements == 1);
    tide_workers_free(workers);
    tide_profile_free(&profile);
}

// Calculations that say which they are, in the order a provider's one thread ran them.
typedef struct ordered_probe {
    uint32_t *order, *runs;
    uint32_t id, released;
} ordered_probe;

static void run_ordered(void *context)
{
    ordered_probe *p = context;
    p->order[(*p->runs)++] = p->id;
}
static void release_ordered(void *context) { ((ordered_probe *)context)->released++; }

static uint32_t start_at(tide_workers **workers, const tide_jobs *jobs, const uint32_t priority, ordered_probe *probe)
{
    const uint32_t ticket = tide_worker_reserve_at(workers, jobs, priority);
    if (ticket) tide_worker_submit(*workers, ticket, run_ordered, release_ordered, probe, 0);
    return ticket;
}

TIDE_TEST(worker_priorities_order_queued_calculations_and_keep_each_levels_order)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    uint32_t order[8] = {0}, runs = 0;
    ordered_probe probes[7];
    static const uint32_t priorities[7] = {TIDE_WORKER_BACKGROUND, TIDE_WORKER_BACKGROUND, TIDE_WORKER_NORMAL, TIDE_WORKER_URGENT,
                                           77u, TIDE_WORKER_URGENT, TIDE_WORKER_BACKGROUND}; // An unknown number is normal
    for (uint32_t i = 0; i < 7; i++) {
        probes[i] = (ordered_probe){order, &runs, i, 0};
        TIDE_REQUIRE(start_at(&workers, &jobs, priorities[i], &probes[i]));
    }
    // The provider has the first alone, whatever its priority: it's never taken back or passed
    TIDE_CHECK(queue.first && !queue.first->next);
    while (help(&queue, false)) { }
    static const uint32_t want[7] = {0, 3, 5, 2, 4, 1, 6};
    TIDE_REQUIRE(runs == 7);
    for (uint32_t i = 0; i < 7; i++) TIDE_CHECK(order[i] == want[i]);
    tide_workers_free(workers);
    for (uint32_t i = 0; i < 7; i++) TIDE_CHECK(probes[i].released == 1);
}

TIDE_TEST(worker_priorities_hand_a_provider_as_many_as_its_other_threads)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 4, .background = defer, .help = help};
    tide_workers *workers = NULL;
    uint32_t order[8] = {0}, runs = 0;
    ordered_probe probes[6];
    for (uint32_t i = 0; i < 6; i++) {
        probes[i] = (ordered_probe){order, &runs, i, 0};
        start_at(&workers, &jobs, i == 5 ? TIDE_WORKER_URGENT : TIDE_WORKER_BACKGROUND, &probes[i]);
    }
    uint32_t handed = 0;
    for (const tide_background *job = queue.first; job; job = job->next) handed++;
    TIDE_CHECK(handed == 3);
    while (help(&queue, false)) { }
    // The three it had, then the urgent one before the background ones that waited with it
    static const uint32_t want[6] = {0, 1, 2, 5, 3, 4};
    TIDE_REQUIRE(runs == 6);
    for (uint32_t i = 0; i < 6; i++) TIDE_CHECK(order[i] == want[i]);
    tide_workers_free(workers);
}

TIDE_TEST(worker_priorities_age_so_nothing_waits_for_ever)
{
    enum { urgent = 64 };
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    const tide_worker_limits limits = {.pending = 1024};
    tide_workers_frame(&workers, &limits, NULL);
    uint32_t order[urgent + 8] = {0}, runs = 0;
    ordered_probe probes[urgent + 4];
    static const uint32_t first[4] = {TIDE_WORKER_NORMAL, TIDE_WORKER_BACKGROUND, TIDE_WORKER_NORMAL, TIDE_WORKER_NORMAL};
    for (uint32_t i = 0; i < 4; i++) { // The provider's, then one background and two normal ones that wait
        probes[i] = (ordered_probe){order, &runs, 1000 + i, 0};
        TIDE_REQUIRE(start_at(&workers, &jobs, first[i], &probes[i]));
    }
    for (uint32_t i = 0; i < urgent; i++) {
        probes[4 + i] = (ordered_probe){order, &runs, i, 0};
        TIDE_REQUIRE(start_at(&workers, &jobs, TIDE_WORKER_URGENT, &probes[4 + i]));
    }
    while (help(&queue, false)) { }
    TIDE_REQUIRE(runs == urgent + 4);
    // With urgent work always waiting, a normal one starts after every four urgent starts, and the background
    // one after sixteen starts of the others
    static const uint32_t want[19] = {1000, 0, 1, 2, 3, 1002, 4, 5, 6, 7, 1003, 8, 9, 10, 11, 12, 13, 1001, 14};
    for (uint32_t i = 0; i < 19; i++) TIDE_CHECK(order[i] == want[i]);
    for (uint32_t i = 19; i < urgent + 4; i++) TIDE_CHECK(order[i] == i - 4);
    tide_workers_free(workers);
}

TIDE_TEST(worker_priorities_release_cancelled_waiting_work_at_once)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    uint32_t order[4] = {0}, runs = 0;
    ordered_probe probes[4];
    uint32_t tickets[4];
    for (uint32_t i = 0; i < 4; i++) {
        probes[i] = (ordered_probe){order, &runs, i, 0};
        tickets[i] = start_at(&workers, &jobs, i == 2 ? TIDE_WORKER_URGENT : TIDE_WORKER_NORMAL, &probes[i]);
    }
    tide_worker_forget(workers, tickets[2]); // Waiting, the next to start: gone with its captures, never run
    TIDE_CHECK(probes[2].released == 1);
    tide_worker_forget(workers, tickets[0]); // The provider's: skipped there, released once it says it's done
    TIDE_CHECK(probes[0].released == 0);
    tide_worker_forget(workers, tickets[2]);
    while (help(&queue, false)) { }
    tide_workers_collect(workers);
    TIDE_CHECK(runs == 2 && order[0] == 1 && order[1] == 3);
    TIDE_CHECK(probes[0].released == 1 && probes[2].released == 1);
    TIDE_CHECK(tide_worker_poll(workers, tickets[1]) == &probes[1] && tide_worker_poll(workers, tickets[2]) == NULL);
    tide_workers_free(workers);
    for (uint32_t i = 0; i < 4; i++) TIDE_CHECK(probes[i].released == 1);
}

TIDE_TEST(worker_priorities_admit_each_level_by_its_own_room)
{
    tide_workers *workers = NULL;
    const tide_worker_limits limits = {.pending = 8, .capture_bytes = 80};
    tide_workers_frame(&workers, &limits, NULL);
    uint32_t tickets[24], count = 0;
    while (count < 24 && (tickets[count] = tide_worker_reserve_at(&workers, NULL, TIDE_WORKER_BACKGROUND)) != 0) count++;
    TIDE_CHECK(count == 4 && !tide_worker_can_reserve_at(workers, TIDE_WORKER_BACKGROUND)); // Half the limit
    TIDE_CHECK(tide_worker_can_reserve(workers) && tide_worker_can_reserve_at(workers, TIDE_WORKER_NORMAL));
    while (count < 24 && (tickets[count] = tide_worker_reserve(&workers, NULL)) != 0) count++;
    TIDE_CHECK(count == 12 && !tide_worker_can_reserve(workers)); // The limit, whatever else is pending
    while (count < 24 && (tickets[count] = tide_worker_reserve_at(&workers, NULL, TIDE_WORKER_URGENT)) != 0) count++;
    TIDE_CHECK(count == 20 && !tide_worker_can_reserve_at(workers, TIDE_WORKER_URGENT));
    tide_worker_forget(workers, tickets[0]); // Room of a level is that level's alone
    TIDE_CHECK(tide_worker_can_reserve_at(workers, TIDE_WORKER_BACKGROUND) && !tide_worker_can_reserve(workers));
    for (uint32_t i = 1; i < count; i++) tide_worker_forget(workers, tickets[i]);
    // Captures kept, likewise: background work to half of them
    worker_probe probe = {0};
    const uint32_t back = tide_worker_reserve_at(&workers, NULL, TIDE_WORKER_BACKGROUND);
    tide_worker_submit(workers, back, run, release, &probe, 40);
    TIDE_CHECK(!tide_worker_can_reserve_at(workers, TIDE_WORKER_BACKGROUND) && tide_worker_can_reserve(workers));
    const uint32_t normal = tide_worker_reserve(&workers, NULL);
    tide_worker_submit(workers, normal, run, release, &probe, 80);
    TIDE_CHECK(!tide_worker_can_reserve(workers) && tide_worker_can_reserve_at(workers, TIDE_WORKER_URGENT));
    const uint32_t urgent = tide_worker_reserve_at(&workers, NULL, TIDE_WORKER_URGENT);
    tide_worker_submit(workers, urgent, run, release, &probe, 80);
    TIDE_CHECK(!tide_worker_can_reserve_at(workers, TIDE_WORKER_URGENT));
    tide_worker_forget(workers, normal);
    TIDE_CHECK(tide_worker_can_reserve(workers) && !tide_worker_can_reserve_at(workers, TIDE_WORKER_URGENT));
    tide_workers_free(workers);
    TIDE_CHECK(probe.released == 3);
}
