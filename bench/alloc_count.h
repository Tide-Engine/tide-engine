// Counts what malloc, calloc and realloc hold, by who asked: for finding what grows in a long run. Not in any
// ordinary build. A tree of its own puts it in front of every C file (bench/README.md, What the process holds):
//
//     cmake --preset release -B build/count "-DCMAKE_C_FLAGS=-include <checkout>/bench/alloc_count.h"
//
// and a session benchmark's --every line is followed there by the callers holding the most, and those that grew
// the most since the line before (bench.c's alloc_report, which names the callers by the program's symbols).
// Windows and clang only: the callers are RtlCaptureStackBackTrace's.
//
// Every block is in one table by its address, so a block from code this isn't in front of (the C library's own,
// a system library's) is freed as it would be, uncounted. The table's lock is one for every thread: a counting
// run's timings are not a run's.
#ifndef TIDE_ALLOC_COUNT_H
#define TIDE_ALLOC_COUNT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define TIDE_ALLOC_COUNT 1
#define TIDE_ALLOC_COUNT_FRAMES 10u
#define TIDE_ALLOC_COUNT_SITES 16384u          // Callers told apart, at most (a power of two)
#define TIDE_ALLOC_COUNT_BLOCKS (1u << 24)     // Twice the blocks held at once, at most (a power of two)

typedef struct tide_alloc_site {
    uint64_t key; // 0: unused
    const char *file;
    uint32_t line, tag, depth;
    void *frames[TIDE_ALLOC_COUNT_FRAMES];
    int64_t bytes, blocks, allocs; // Held now, and blocks ever asked for
    int64_t bytes_before;          // ...at the report before (the reporter's)
} tide_alloc_site;

typedef struct tide_alloc_block {
    void *p;
    uint32_t size, site;
} tide_alloc_block;

__declspec(selectany) tide_alloc_site tide_alloc_sites[TIDE_ALLOC_COUNT_SITES];
__declspec(selectany) tide_alloc_block tide_alloc_blocks[TIDE_ALLOC_COUNT_BLOCKS];
__declspec(selectany) char tide_alloc_lock;
__declspec(selectany) uint32_t tide_alloc_tag; // Whose work this is (the bench's: 1 the bots' machines, 2 the server)
__declspec(selectany) int64_t tide_alloc_held, tide_alloc_held_blocks, tide_alloc_uncounted;

__declspec(dllimport) unsigned short __stdcall RtlCaptureStackBackTrace(unsigned long skip, unsigned long count,
                                                                        void **frames, unsigned long *hash);

static inline void tide_alloc_count_lock(void)
{
    while (__atomic_test_and_set(&tide_alloc_lock, __ATOMIC_ACQUIRE)) __builtin_ia32_pause();
}

static inline void tide_alloc_count_unlock(void)
{
    __atomic_clear(&tide_alloc_lock, __ATOMIC_RELEASE);
}

static inline uint32_t tide_alloc_count_slot(const void *p)
{
    return (uint32_t)(((uintptr_t)p >> 4) * 0x9E3779B97F4A7C15ull >> 40) & (TIDE_ALLOC_COUNT_BLOCKS - 1u);
}

// With the lock held
static inline void tide_alloc_count_add(void *p, const size_t size, const uint32_t site)
{
    uint32_t at = tide_alloc_count_slot(p);
    while (tide_alloc_blocks[at].p) at = (at + 1u) & (TIDE_ALLOC_COUNT_BLOCKS - 1u);
    tide_alloc_blocks[at] = (tide_alloc_block){p, (uint32_t)size, site};
    tide_alloc_sites[site].bytes += (int64_t)size;
    tide_alloc_sites[site].blocks++;
    tide_alloc_sites[site].allocs++;
    tide_alloc_held += (int64_t)size;
    tide_alloc_held_blocks++;
}

// With the lock held
static inline void tide_alloc_count_remove(const void *p)
{
    uint32_t at = tide_alloc_count_slot(p);
    while (tide_alloc_blocks[at].p && tide_alloc_blocks[at].p != p) at = (at + 1u) & (TIDE_ALLOC_COUNT_BLOCKS - 1u);
    if (!tide_alloc_blocks[at].p) {
        tide_alloc_uncounted++;
        return;
    }
    const tide_alloc_block b = tide_alloc_blocks[at];
    tide_alloc_sites[b.site].bytes -= b.size;
    tide_alloc_sites[b.site].blocks--;
    tide_alloc_held -= b.size;
    tide_alloc_held_blocks--;
    // Close the gap, so every block is still found from its own slot
    uint32_t gap = at;
    for (uint32_t next = (at + 1u) & (TIDE_ALLOC_COUNT_BLOCKS - 1u); tide_alloc_blocks[next].p;
         next = (next + 1u) & (TIDE_ALLOC_COUNT_BLOCKS - 1u)) {
        const uint32_t home = tide_alloc_count_slot(tide_alloc_blocks[next].p);
        if (((next - home) & (TIDE_ALLOC_COUNT_BLOCKS - 1u)) >= ((next - gap) & (TIDE_ALLOC_COUNT_BLOCKS - 1u))) {
            tide_alloc_blocks[gap] = tide_alloc_blocks[next];
            gap = next;
        }
    }
    tide_alloc_blocks[gap].p = NULL;
}

// With the lock held: the caller's place in the sites
static inline uint32_t tide_alloc_count_site(const char *file, const uint32_t line, void *const *frames, const uint32_t depth)
{
    uint64_t key = 0xcbf29ce484222325ull ^ line ^ (uint64_t)tide_alloc_tag << 32;
    for (uint32_t i = 0; i < depth; i++) key = (key ^ (uintptr_t)frames[i]) * 0x100000001b3ull;
    key |= 1u;
    uint32_t at = (uint32_t)(key >> 20) & (TIDE_ALLOC_COUNT_SITES - 1u);
    for (uint32_t tried = 0; tried < TIDE_ALLOC_COUNT_SITES - 1u; tried++, at = (at + 1u) & (TIDE_ALLOC_COUNT_SITES - 1u)) {
        tide_alloc_site *s = &tide_alloc_sites[at];
        if (s->key == key) return at;
        if (s->key) continue;
        s->key = key;
        s->file = file;
        s->line = line;
        s->tag = tide_alloc_tag;
        s->depth = depth;
        for (uint32_t i = 0; i < depth; i++) s->frames[i] = frames[i];
        return at;
    }
    return at; // Full: the last one looked at takes the rest
}

static inline void *tide_alloc_count_took(void *p, const size_t size, const char *file, const int line)
{
    if (!p) return p;
    void *frames[TIDE_ALLOC_COUNT_FRAMES];
    const uint32_t depth = RtlCaptureStackBackTrace(0, TIDE_ALLOC_COUNT_FRAMES, frames, NULL);
    tide_alloc_count_lock();
    tide_alloc_count_add(p, size, tide_alloc_count_site(file, (uint32_t)line, frames, depth));
    tide_alloc_count_unlock();
    return p;
}

static inline void *tide_alloc_count_malloc(const size_t size, const char *file, const int line)
{
    return tide_alloc_count_took(malloc(size), size, file, line);
}

static inline void *tide_alloc_count_calloc(const size_t count, const size_t size, const char *file, const int line)
{
    return tide_alloc_count_took(calloc(count, size), count * size, file, line);
}

static inline void tide_alloc_count_free(void *p)
{
    if (p) {
        tide_alloc_count_lock();
        tide_alloc_count_remove(p);
        tide_alloc_count_unlock();
    }
    free(p);
}

static inline void *tide_alloc_count_realloc(void *p, const size_t size, const char *file, const int line)
{
    if (p) {
        // Out of the table before the block can be another thread's
        tide_alloc_count_lock();
        tide_alloc_count_remove(p);
        tide_alloc_count_unlock();
    }
    void *grown = realloc(p, size);
    if (!grown && p && size) {
        // Still the old block, its size no longer known: counted as none
        tide_alloc_count_took(p, 0, file, line);
        return NULL;
    }
    return tide_alloc_count_took(grown, size, file, line);
}

#define malloc(size) tide_alloc_count_malloc((size), __FILE__, __LINE__)
#define calloc(count, size) tide_alloc_count_calloc((count), (size), __FILE__, __LINE__)
#define realloc(p, size) tide_alloc_count_realloc((p), (size), __FILE__, __LINE__)
#define free(p) tide_alloc_count_free(p)

#endif
