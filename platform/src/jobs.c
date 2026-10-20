// A pool of threads for ticks (tide/jobs.h): one for each of the CPU's cores,
// the caller's among them. A file of its own, since it includes the operating
// system's headers.

#if defined(__linux__)
#define _GNU_SOURCE // sysconf, and sched_setaffinity, sched_getcpu and CPU_SET (Android's too), under strict C
#elif !defined(_WIN32) && !defined(__wasm__)
#define _DEFAULT_SOURCE // sysconf under strict C
#endif

#include "tide/platform.h"

#include <stdio.h>
#include <stdlib.h>

#include "tide/time.h" // How long a round took (run)

#ifndef MOST_THREADS
#define MOST_THREADS 32u
#endif

#if defined(__wasm__) && !defined(__wasm_atomics__)

const tide_jobs *tide_platform_jobs(void)
{
    return NULL; // Built without threads
}

void tide_platform_jobs_profile(tide_profile *profile)
{
    (void)profile; // No pool to time ticks on
}

#elif defined(__wasm__)

// On the web, threads are workers that share the program's memory, which a
// page can only give them when it's cross-origin isolated (platform/web/tide.js
// says how many it has). The page's main thread can never wait, for a lock or
// a signal, only spin: so workers wait for a call on its count, and the
// caller, which only spins, opens and closes rounds with atomics alone. As on
// desktop, the caller never waits for a worker to wake (a worker only starts
// once the page's main thread is back in the browser's hands), only for the
// ones that joined.
//
// A worker with nothing to do sleeps: WebAssembly has no instruction that
// tells a CPU a loop is only waiting, and a page can't yield its core, so one
// that spun would hold a core, or half of one, from a thread with work. The
// work returns on a worker that finds none, and `more` calls as many back as
// there's work for (tide/jobs.h).

#include <pthread.h>

#include "tide_web.h"

#define OPEN 0x80000000u // In `gate`: the round takes workers. Below it: the workers in it

typedef struct pool {
    tide_jobs jobs;
    uint32_t calls;  // For workers, which they wait on: rounds started, and more asked into one
    uint32_t asleep; // Workers waiting for one, or about to
    uint32_t gate;
    void (*work)(void *context, uint32_t thread);
    void *context;
    bool started; // Its workers are made
} pool;

static pool the_pool;
static bool made;

static void *worker(void *thread);

// The workers, thread 1 and up, with the stack the program's own thread has:
// the caller is thread 0. Made the first time there's work for them, since a
// worker costs a page far more than a thread costs a program. Fewer, if the
// page won't make them all: rounds only ever take the workers that join.
static void start_workers(pool *p)
{
    p->started = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    for (uint32_t thread = 1; thread < p->jobs.threads; thread++) {
        pthread_t t;
        if (pthread_create(&t, &attr, worker, (void *)(uintptr_t)thread) != 0) break;
        pthread_detach(t);
    }
    pthread_attr_destroy(&attr);
}

static void *worker(void *thread)
{
    pool *p = &the_pool;
    uint32_t seen = 0;
    for (;;) {
        // Counted asleep before it looks at the calls: whoever calls counts
        // its call first and then looks for sleepers, so one of the two sees
        // the other (more)
        __atomic_fetch_add(&p->asleep, 1u, __ATOMIC_SEQ_CST);
        uint32_t calls;
        while ((calls = __atomic_load_n(&p->calls, __ATOMIC_SEQ_CST)) == seen) {
            __builtin_wasm_memory_atomic_wait32((int32_t *)&p->calls, (int32_t)seen, -1);
        }
        __atomic_fetch_sub(&p->asleep, 1u, __ATOMIC_SEQ_CST);
        seen = calls;
        uint32_t gate = __atomic_load_n(&p->gate, __ATOMIC_ACQUIRE);
        bool joined = false;
        while ((gate & OPEN) && !joined) {
            joined = __atomic_compare_exchange_n(&p->gate, &gate, gate + 1u, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
        if (!joined) continue; // Over before it woke
        p->work(p->context, (uint32_t)(uintptr_t)thread);
        __atomic_fetch_sub(&p->gate, 1u, __ATOMIC_RELEASE);
    }
    return NULL;
}

// A round of work, which wakes at most `others` of the workers that sleep.
// The workers are made by the first round that wants them all, as a tick with
// work for threads does: a round that asks for a few is little work, and a
// page pays far more for its workers than they'd save it, so until they're
// there it's the caller's alone.
static void run_some(void *self, const uint32_t others, void (*work)(void *context, uint32_t thread), void *context)
{
    pool *p = self;
    if (!p->started) {
        if (others < p->jobs.threads - 1u) {
            work(context, 0);
            return;
        }
        start_workers(p);
    }
    p->work = work;
    p->context = context;
    __atomic_store_n(&p->gate, OPEN, __ATOMIC_RELEASE);
    __atomic_fetch_add(&p->calls, 1u, __ATOMIC_SEQ_CST);
    __builtin_wasm_memory_atomic_notify((int32_t *)&p->calls, others);
    work(context, 0);
    __atomic_fetch_and(&p->gate, ~OPEN, __ATOMIC_ACQ_REL);
    while (__atomic_load_n(&p->gate, __ATOMIC_ACQUIRE) != 0) {
    }
}

static void run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    run_some(self, UINT32_MAX, work, context);
}

// Up to `count` of the workers that sleep come back into the round. A worker
// that was on its way to sleep sees the call counted and looks again, so
// nothing is asked of a pool whose workers are all awake.
static void more(void *self, const uint32_t count)
{
    pool *p = self;
    __atomic_fetch_add(&p->calls, 1u, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&p->asleep, __ATOMIC_SEQ_CST)) __builtin_wasm_memory_atomic_notify((int32_t *)&p->calls, count);
}

const tide_jobs *tide_platform_jobs(void)
{
    if (made) return the_pool.jobs.threads > 1 ? &the_pool.jobs : NULL;
    made = true;
    pool *p = &the_pool;
    const uint32_t n = tide_web_threads();
    p->jobs = (tide_jobs){p, n < MOST_THREADS ? n : MOST_THREADS, run, p->jobs.profile, more, run_some}; // A profile given before stays
    return p->jobs.threads > 1 ? &p->jobs : NULL;
}

void tide_platform_jobs_profile(tide_profile *profile)
{
    the_pool.jobs.profile = profile;
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK pool_lock;
typedef CONDITION_VARIABLE pool_signal;
#define LOCK(l) AcquireSRWLockExclusive(l)
#define UNLOCK(l) ReleaseSRWLockExclusive(l)
#define WAIT(s, l) SleepConditionVariableSRW(s, l, INFINITE, 0)
#define WAKE_ONE(s) WakeConditionVariable(s)
#define WAKE_ALL(s) WakeAllConditionVariable(s)
#else
#include <pthread.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#ifdef __linux__
#include <sched.h>

#include "cpu_groups.h"
#ifndef TIDE_CPU_SYSFS
#define TIDE_CPU_SYSFS "/sys/devices/system/cpu" // Another, made up, for a probe of the pool's groups on a machine that has one
#endif
#endif
typedef pthread_mutex_t pool_lock;
typedef pthread_cond_t pool_signal;
#define LOCK(l) pthread_mutex_lock(l)
#define UNLOCK(l) pthread_mutex_unlock(l)
#define WAIT(s, l) pthread_cond_wait(s, l)
#define WAKE_ONE(s) pthread_cond_signal(s)
#define WAKE_ALL(s) pthread_cond_broadcast(s)
#endif


// Each round of work, the workers that wake in time join the caller. A round
// is open until the caller's own run of the work returns, which is once all
// of it is done: a worker that wakes later has nothing left to do, so the
// caller never waits for a worker to wake, only for the ones that joined.
//
// A worker the work returns on while the round is open found nothing to do
// in it, and sleeps until it's called again: for another round, or back into
// this one, when there's work for more threads than are at it (`more`, see
// tide/jobs.h). A thread that looked for work the whole round through would
// slow the ones that have it.
//
// Cores that share a last-level cache are a group (a chiplet of a Ryzen, say,
// six of its twelve cores). What a tick reads moves between groups slowly, so
// a world that fits in one group's cache ticks faster on that group's cores
// alone than on every core: a million particles, 0.086 ms a tick on six of a
// 7900X's cores, 0.27 on all twelve, 0.38 on three of each group (2026-10-20).
// Work that's more arithmetic than reading wants every core all the same. So
// each worker keeps to its group's cores, and a round calls in either the
// caller's group alone or every group: `run` tries both now and then, timing
// them, and takes what was quicker for that work (`choice`). The caller's group
// is the one the pool was made on, and a thread that runs rounds keeps to its
// cores from its first round on, or the system moves it from one group to the
// other between ticks, and the world with it: unpinned, those six took 0.13 ms
// where they took 0.086 pinned. Which threads run never changes what they
// make (tide/jobs.h), only how soon. Groups are found on Windows and on Linux
// and Android (cpu_groups.h); elsewhere, and on a machine with one cache,
// there's one.
#define MOST_GROUPS 8u
#define CHOICES 4u  // The works whose rounds `run` times apart: a tick's, and a few others'
#define TRIALS 6u   // A work's first rounds take each way in turn...
#define EXPLORE 64u // ...and then every this many, the way that was slower

typedef struct choice {
    void (*work)(void *context, uint32_t thread); // NULL: free
    uint64_t rounds;
    uint64_t took[2][3]; // The last three rounds' times each way, in ns: the caller's group alone, every group
    uint32_t tried[2];
} choice;

// What a way's rounds take: the middle of its last three, so neither a round
// the system held up nor the first, cold, one decides.
static uint64_t typical(const choice *c, const uint32_t way)
{
    const uint64_t *t = c->took[way];
    if (c->tried[way] < 3u) return c->tried[way] ? t[(c->tried[way] - 1u) % 3u] : 0u;
    const uint64_t lo = t[0] < t[1] ? t[0] : t[1], hi = t[0] < t[1] ? t[1] : t[0];
    return t[2] < lo ? lo : t[2] > hi ? hi : t[2];
}

typedef struct pool {
    tide_jobs jobs;
    pool_lock lock;
    pool_signal start[MOST_GROUPS]; // A call for each group's workers
    pool_signal done;               // The last worker in the round has left it
    uint32_t calls;                 // For workers, which they wait on: rounds started, and more asked into one
    uint32_t asleep[MOST_GROUPS];   // Each group's workers waiting for one, or about to
    bool open;                      // The round takes workers
    uint32_t inside;                // Workers running the round's work
    uint32_t inside_group[MOST_GROUPS]; // ...of each group
    uint32_t room[MOST_GROUPS];     // How many of each group's workers the round takes at once
    void (*work)(void *context, uint32_t thread);
    void *context;
    uint32_t groups;                // 1 when they aren't known
    uint32_t home;                  // The group of the threads that run rounds: the one the pool was made on
    uint32_t cores[MOST_GROUPS];    // Each group's, as many threads as it runs at once
    uint8_t worker_group[MOST_THREADS]; // By thread
    choice choices[CHOICES];
#ifdef _WIN32
    GROUP_AFFINITY affinity[MOST_GROUPS]; // Each group's logical processors
#elif defined(__linux__)
    cpu_set_t affinity[MOST_GROUPS];
#endif
} pool;

static pool the_pool;
static bool made;

static void worker_loop(const uint32_t thread)
{
    pool *p = &the_pool;
    const uint32_t g = p->worker_group[thread];
    uint32_t seen = 0;
    LOCK(&p->lock);
    for (;;) {
        // Counted asleep before it looks at the calls, and it has the lock
        // from the look until it waits: whoever calls counts its call first
        // and then looks for sleepers, so it either finds this one, and gets
        // the lock once it waits, or this one sees the call (wake)
        __atomic_fetch_add(&p->asleep[g], 1u, __ATOMIC_SEQ_CST);
        uint32_t calls;
        while ((calls = __atomic_load_n(&p->calls, __ATOMIC_SEQ_CST)) == seen) WAIT(&p->start[g], &p->lock);
        __atomic_fetch_sub(&p->asleep[g], 1u, __ATOMIC_SEQ_CST);
        seen = calls;
        if (!p->open || p->inside_group[g] >= p->room[g]) continue; // Over before it woke, or no room for its group
        p->inside++;
        p->inside_group[g]++;
        void (*const work)(void *, uint32_t) = p->work;
        void *const context = p->context;
        UNLOCK(&p->lock);
        work(context, thread);
        LOCK(&p->lock);
        p->inside_group[g]--;
        if (--p->inside == 0 && !p->open) WAKE_ONE(&p->done);
    }
}

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID thread)
{
    worker_loop((uint32_t)(uintptr_t)thread);
    return 0;
}
#else
static void *worker(void *thread)
{
#ifdef __linux__
    const pool *p = &the_pool; // Each worker keeps to its group's CPUs, as a Windows one is made to
    if (p->groups > 1u) sched_setaffinity(0, sizeof(cpu_set_t), &p->affinity[p->worker_group[(uint32_t)(uintptr_t)thread]]);
#endif
    worker_loop((uint32_t)(uintptr_t)thread);
    return NULL;
}
#endif

// The group of the processor the calling thread is on now.
static uint32_t caller_group(const pool *p)
{
#ifdef _WIN32
    if (p->groups > 1u) {
        PROCESSOR_NUMBER at;
        GetCurrentProcessorNumberEx(&at);
        for (uint32_t g = 0; g < p->groups; g++) {
            if (p->affinity[g].Group == at.Group && ((p->affinity[g].Mask >> at.Number) & 1u)) return g;
        }
    }
#elif defined(__linux__)
    const int cpu = p->groups > 1u ? sched_getcpu() : -1;
    for (uint32_t g = 0; cpu >= 0 && cpu < CPU_SETSIZE && g < p->groups; g++) {
        if (CPU_ISSET(cpu, &p->affinity[g])) return g;
    }
#else
    (void)p;
#endif
    return 0;
}

// Up to `count` sleeping workers of the groups whose room in the round isn't
// full, the caller's group's first. The lock is taken and let go before
// they're woken, so one that had looked already is waiting by then, and none
// wakes only to wait for the lock. Nothing is asked of a pool whose workers
// are all awake: a worker on its way to sleep sees the call counted, and
// looks again.
static void wake(pool *p, const uint32_t home, uint32_t count)
{
    bool any = false;
    for (uint32_t g = 0; g < p->groups && !any; g++) any = __atomic_load_n(&p->asleep[g], __ATOMIC_SEQ_CST) != 0u;
    if (!any || !count) return;
    LOCK(&p->lock);
    UNLOCK(&p->lock);
    for (uint32_t k = 0; k < p->groups && count; k++) {
        const uint32_t g = (home + k) % p->groups;
        const uint32_t asleep = __atomic_load_n(&p->asleep[g], __ATOMIC_SEQ_CST);
        const uint32_t room = __atomic_load_n(&p->room[g], __ATOMIC_RELAXED);
        const uint32_t inside = __atomic_load_n(&p->inside_group[g], __ATOMIC_RELAXED);
        uint32_t n = room > inside ? room - inside : 0u;
        if (n > asleep) n = asleep;
        if (n > count) n = count;
        if (!n) continue;
        if (n >= asleep) {
            WAKE_ALL(&p->start[g]);
        } else {
            for (uint32_t i = 0; i < n; i++) WAKE_ONE(&p->start[g]);
        }
        count -= n;
    }
}

// A round of work, which takes at most `others` workers: of the caller's
// group alone, or with `every`, of every group.
static _Thread_local bool kept_home; // This thread keeps to the home group's cores

static void round_of(pool *p, const bool every, const uint32_t others, void (*work)(void *context, uint32_t thread),
                     void *context)
{
    const uint32_t home = p->home;
#ifdef _WIN32
    if (p->groups > 1u && !kept_home) {
        SetThreadGroupAffinity(GetCurrentThread(), &p->affinity[home], NULL);
        kept_home = true;
    }
#elif defined(__linux__)
    if (p->groups > 1u && !kept_home) {
        sched_setaffinity(0, sizeof(cpu_set_t), &p->affinity[home]);
        kept_home = true;
    }
#else
    (void)kept_home;
#endif
    LOCK(&p->lock);
    p->work = work;
    p->context = context;
    p->open = true;
    p->inside = 0;
    uint32_t left = others;
    for (uint32_t k = 0; k < p->groups; k++) {
        const uint32_t g = (home + k) % p->groups;
        uint32_t room = k == 0 ? p->cores[g] - 1u : every ? p->cores[g] : 0u; // The caller is one of its group's
        if (room > left) room = left;
        p->room[g] = room;
        left -= room;
    }
    __atomic_fetch_add(&p->calls, 1u, __ATOMIC_SEQ_CST);
    UNLOCK(&p->lock);
    wake(p, home, others);
    work(context, 0);
    LOCK(&p->lock);
    p->open = false;
    while (p->inside) WAIT(&p->done, &p->lock);
    UNLOCK(&p->lock);
}

// A round for a few workers, of the caller's group, which has in its cache
// what the caller just made: never more of them than the group has.
static void run_some(void *self, const uint32_t others, void (*work)(void *context, uint32_t thread), void *context)
{
    round_of(self, false, others, work, context);
}

// A round for every worker the work is quicker with: the caller's group
// alone, or every group, as the work's rounds took either way.
static void run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    pool *p = self;
    if (p->groups < 2u) {
        round_of(p, true, UINT32_MAX, work, context);
        return;
    }
    choice *c = NULL;
    for (uint32_t i = 0; i < CHOICES && !c; i++) {
        if (p->choices[i].work == work || !p->choices[i].work) c = &p->choices[i];
    }
    if (!c) c = &p->choices[0]; // More works than kept: the first makes way
    if (c->work != work) *c = (choice){.work = work};
    const uint64_t n = c->rounds++;
    bool every = n & 1u;
    if (n >= TRIALS) {
        every = typical(c, 1) < typical(c, 0);
        if (n % EXPLORE == 0) every = !every;
    }
    const uint64_t began = tide_time_now_ns();
    round_of(p, every, UINT32_MAX, work, context);
    const uint32_t way = every ? 1u : 0u;
    c->took[way][c->tried[way]++ % 3u] = tide_time_now_ns() - began;
}

static void more(void *self, const uint32_t count)
{
    pool *p = self;
    __atomic_fetch_add(&p->calls, 1u, __ATOMIC_SEQ_CST);
    wake(p, p->home, count);
}

// The CPU's cores, not counting the second thread a core runs (SMT): two
// threads of a tick on one core share its execution units and cache, and go
// slower than one, while a spinning one holds the other back. Logical
// processors when the system won't say. On Windows, Linux and Android, also
// the groups of cores that share a last-level cache, into `p`; elsewhere `p`
// gets one.
static uint32_t cores(pool *p)
{
    p->groups = 1;
#ifdef _WIN32
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationAll, NULL, &size);
    uint8_t *buffer = size ? malloc(size) : NULL;
    uint32_t physical = 0;
    GROUP_AFFINITY caches[MOST_GROUPS];
    uint32_t cache_count = 0, per_cache[MOST_GROUPS] = {0};
    BYTE level = 0;
    bool too_many = false;
    if (buffer && GetLogicalProcessorInformationEx(RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)buffer, &size)) {
        // The last level of cache there is: its caches are the groups
        for (DWORD at = 0; at < size;) {
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(buffer + at);
            if (info->Relationship == RelationCache && info->Cache.Type == CacheUnified && info->Cache.Level > level) {
                level = info->Cache.Level;
            }
            at += info->Size;
        }
        for (DWORD at = 0; at < size;) {
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(buffer + at);
            if (info->Relationship == RelationCache && info->Cache.Type == CacheUnified && info->Cache.Level == level) {
                if (cache_count < MOST_GROUPS) caches[cache_count++] = info->Cache.GroupMask;
                else too_many = true;
            }
            at += info->Size;
        }
        for (DWORD at = 0; at < size;) {
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(buffer + at);
            if (info->Relationship == RelationProcessorCore) {
                physical++;
                const GROUP_AFFINITY *core = &info->Processor.GroupMask[0];
                for (uint32_t c = 0; c < cache_count; c++) {
                    if (caches[c].Group == core->Group && (caches[c].Mask & core->Mask)) {
                        per_cache[c]++;
                        break;
                    }
                }
            }
            at += info->Size;
        }
        // Groups where there are several, each of two cores or more, and
        // between them every core
        uint32_t counted = 0;
        bool small = false;
        for (uint32_t c = 0; c < cache_count; c++) {
            counted += per_cache[c];
            small |= per_cache[c] < 2u;
        }
        if (cache_count > 1u && !too_many && !small && counted == physical) {
            p->groups = cache_count;
            for (uint32_t c = 0; c < cache_count; c++) {
                p->cores[c] = per_cache[c];
                p->affinity[c] = caches[c];
            }
        }
    }
    free(buffer);
    if (physical) {
        if (p->groups == 1u) p->cores[0] = physical;
        return physical;
    }
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    p->cores[0] = (uint32_t)info.dwNumberOfProcessors;
    return p->cores[0];
#elif defined(__APPLE__)
    int physical = 0;
    size_t size = sizeof physical;
    if (sysctlbyname("hw.physicalcpu", &physical, &size, NULL, 0) == 0 && physical > 0) {
        p->cores[0] = (uint32_t)physical;
        return p->cores[0];
    }
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    p->cores[0] = n > 0 ? (uint32_t)n : 1u;
    return p->cores[0];
#else
#ifdef __linux__
    tide_cpu_groups found;
    tide_cpu_groups_find(TIDE_CPU_SYSFS, &found);
    if (found.count > 1u && found.count <= MOST_GROUPS) {
        p->groups = found.count;
        for (uint32_t g = 0; g < found.count; g++) {
            p->cores[g] = found.cores[g];
            CPU_ZERO(&p->affinity[g]);
            for (uint32_t cpu = 0; cpu < TIDE_CPU_MOST && cpu < CPU_SETSIZE; cpu++) {
                if (tide_cpu_has(&found.cpus[g], cpu)) CPU_SET(cpu, &p->affinity[g]);
            }
        }
        return found.physical;
    }
#endif
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t logical = n > 0 ? (uint32_t)n : 1u;
    // Linux says whether the cores run two threads each; Android too, where it lets a program read it
    FILE *smt = fopen("/sys/devices/system/cpu/smt/active", "r");
    if (smt) {
        const int active = fgetc(smt);
        fclose(smt);
        if (active == '1' && logical > 1u) logical /= 2u;
    }
    p->cores[0] = logical;
    return logical;
#endif
}

const tide_jobs *tide_platform_jobs(void)
{
    if (made) return the_pool.jobs.threads > 1 ? &the_pool.jobs : NULL;
    made = true;
    pool *p = &the_pool;
    const uint32_t n = cores(p);
    p->jobs = (tide_jobs){p, n < MOST_THREADS ? n : MOST_THREADS, run, p->jobs.profile, more, run_some}; // A profile given before stays
#ifdef _WIN32
    InitializeSRWLock(&p->lock);
    for (uint32_t g = 0; g < MOST_GROUPS; g++) InitializeConditionVariable(&p->start[g]);
    InitializeConditionVariable(&p->done);
#else
    pthread_mutex_init(&p->lock, NULL);
    for (uint32_t g = 0; g < MOST_GROUPS; g++) pthread_cond_init(&p->start[g], NULL);
    pthread_cond_init(&p->done, NULL);
#endif
    // Each group's share of the threads, the caller's group's first, the
    // caller one of them: as many as the group has cores, while there are
    // threads. The workers are thread 1 and up, group by group.
    const uint32_t home = p->home = caller_group(p);
    uint32_t left = p->jobs.threads, thread = 1;
    for (uint32_t k = 0; k < p->groups; k++) {
        const uint32_t g = (home + k) % p->groups;
        const uint32_t share = p->cores[g] < left ? p->cores[g] : left;
        left -= share;
        p->cores[g] = share; // A group given fewer runs no more at once
        for (uint32_t i = k == 0 ? 1u : 0u; i < share; i++) p->worker_group[thread++] = (uint8_t)g;
    }
    // The workers. Fewer, if the system won't make them all.
    uint32_t threads = 1;
    for (; threads < p->jobs.threads; threads++) {
#ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, worker, (LPVOID)(uintptr_t)threads, CREATE_SUSPENDED, NULL);
        if (!h) break;
        if (p->groups > 1u) SetThreadGroupAffinity(h, &p->affinity[p->worker_group[threads]], NULL);
        ResumeThread(h);
        CloseHandle(h);
#else
        pthread_t t;
        if (pthread_create(&t, NULL, worker, (void *)(uintptr_t)threads) != 0) break;
        pthread_detach(t);
#endif
    }
    p->jobs.threads = threads;
    if (threads < n) { // A group short of what it was given runs no more than it has
        uint32_t have[MOST_GROUPS] = {0};
        have[home] = 1;
        for (uint32_t t = 1; t < threads; t++) have[p->worker_group[t]]++;
        for (uint32_t g = 0; g < p->groups; g++) {
            if (p->cores[g] > have[g]) p->cores[g] = have[g];
        }
    }
    return threads > 1 ? &p->jobs : NULL;
}

void tide_platform_jobs_profile(tide_profile *profile)
{
    the_pool.jobs.profile = profile;
}

#endif
