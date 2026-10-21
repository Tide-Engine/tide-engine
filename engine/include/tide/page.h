#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Storage of each thread's own, for state code on several threads keeps
// apart (the scratch area, the heaps code runs with, the task running).
#if defined(__wasm__) && !defined(__wasm_atomics__)
#define TIDE_THREAD_LOCAL // Web builds without threads
#else
#define TIDE_THREAD_LOCAL _Thread_local
#endif

// Pages: the memory a world keeps its data in (its archetypes' columns, its
// entity table and its heap), shared with its snapshots until one of them
// changes it.
//
// A snapshot (tide_world_copy) points at the world's pages rather than copying
// them. Whoever changes a shared page first gets a copy of their own
// (tide_page_own), so a world and its snapshots only cost the memory of what's
// different between them. A page also keeps the hash of its contents until it
// changes, so hashing a world only reads what changed since it was last
// hashed.
//
// Pages hold plain data, with no pointers, and the world and its snapshots
// are used from one thread at a time.

typedef struct tide_page {
    uint32_t refs;   // References to it: one from each world or snapshot that has it, or more (see tide_heap)
    uint32_t size;   // Bytes after the header
    uint32_t first;  // A heap's: where it starts, in pages
    uint32_t hashed; // The bytes `hash` covers, or UINT32_MAX until it's hashed again
    uint64_t hash;
    // 0, but while a chunk's rows are written in pieces (tide_table_lanes_begin_pieces): the page this one
    // takes the place of, as a number. It also keeps the data 16 bytes in from the header's start, as malloc's is
    uint64_t was;
} tide_page;

// Ends the program, saying it ran out of memory. Worlds grow as they need, so
// running out of memory is the only way they can't, and it can't happen the
// same way on every machine.
_Noreturn void tide_out_of_memory(void);

// Whether this is a run nobody watches: TIDE_HIDDEN is set in the environment
// to anything but nothing or 0, as in the shell of an automated run or a build
// server, which mustn't put anything on somebody's screen, or in their ears. The
// platform layer shows no window then, and opens no audio device
// (tide/platform.h). A program that stops shows no dialog either,
// whether it stopped as above or crashed: on Windows, the C runtime's (a debug
// build's abort() shows one that waits for a click) and the system's are off
// from the program's start, so what it said on stderr is all there is.
bool tide_hidden(void);

// malloc, calloc and realloc (which takes the old block's size), for memory
// code on several threads uses: running out ends the program. On the web,
// they make sure this thread can use the memory they give (tide_memory_sync)
// before anything touches it, which calloc and realloc can't.
void *tide_alloc(size_t size);
void *tide_alloc_zeroed(size_t count, size_t size);
void *tide_realloc(void *p, size_t old_size, size_t size);

// On the web with threads: when another thread grew the program's memory, V8
// (Node 24's, at least) lets this one use what was added only once it hears
// of it, which can come after it got memory there from malloc or from another
// thread. memcpy and memset into it trap until then. This brings it up to
// date, and costs a comparison when it is. Code that picks up what other
// threads made calls it first (tide/jobs.h's tasks and the systems' ends).
#if defined(__wasm__) && defined(__wasm_atomics__)
void tide_memory_sync(void);
#else
static inline void tide_memory_sync(void)
{
}
#endif

// Whether two runs of bytes are the same: what memcmp says of them, for the
// runs a tick compares whole (a page against the one a snapshot shares, a
// list's chunk against what C gave for it). The web's C library compares a
// byte at a time, which a tick that compares megabytes can't afford, so there
// it's 64 bytes at a time.
#ifdef __wasm__
bool tide_same_bytes(const void *a, const void *b, size_t size);
#else
static inline bool tide_same_bytes(const void *a, const void *b, const size_t size)
{
    return memcmp(a, b, size) == 0;
}
#endif

// A page of `size` bytes, zeroed, with one reference.
tide_page *tide_page_new(uint32_t size);

static inline void *tide_page_data(const tide_page *p)
{
    return (void *)(uintptr_t)(p + 1);
}

static inline void tide_page_retain(tide_page *p)
{
    p->refs++;
}

// Lets go of `refs` references; the last one lets go of the page
// (tide_page_give). NULL does nothing.
void tide_page_release(tide_page *p, uint32_t refs);

// Memory for a page of `size` bytes, with whatever was there in it, header
// and all: for a page whose every byte is about to be set. It's one that was
// let go of, when there's one of that size, which costs no call and is still
// in the CPU's caches; else it's new.
tide_page *tide_page_take(uint32_t size);

// Lets go of a page nothing refers to: it's kept for the next page of its
// size, this thread's or another's, or freed when enough are kept.
void tide_page_give(tide_page *p);

// Frees the pages kept to hand out again, this thread's and the ones threads
// share. For hot reloading, before a game's library is unloaded (tide/host.h).
void tide_pages_free(void);

// A copy of shared `p` to change, for tide_page_own.
tide_page *tide_page_own_copy(tide_page *p, uint32_t mine, uint32_t keep);

// The same, for whoever is about to write over all of the `bytes` from `from`
// on: those aren't copied, and are whatever was in memory until they're
// written.
tide_page *tide_page_own_copy_around(tide_page *p, uint32_t mine, uint32_t keep, uint32_t from, uint32_t bytes);

// `p` to change, through `mine` of its references: itself when nobody else
// has it, or else a copy of its first `keep` bytes (the rest zero), which
// takes those references over. Its hash is out of date either way.
static inline tide_page *tide_page_own(tide_page *p, const uint32_t mine, const uint32_t keep)
{
    if (p->refs != mine) return tide_page_own_copy(p, mine, keep);
    p->hashed = UINT32_MAX;
    return p;
}

// A copy of `p`'s first `keep` bytes (the rest zero), with `refs` references
// and its hash out of date. `p` keeps its own.
tide_page *tide_page_copy(const tide_page *p, uint32_t refs, uint32_t keep);

// The hash of its first `bytes` bytes, kept until it changes.
uint64_t tide_page_hash(tide_page *p, uint32_t bytes);

// The pages a hash is about to ask of whose own hash isn't of what they hold
// now, gathered so that they're hashed on several threads before it asks
// (tide/jobs.h): the hash then finds each one's kept, and comes out as it
// would have. A page is a piece of work of its own, whichever thread takes
// it, so a world that changed in megabytes is hashed by as many threads as
// come. A zeroed one is ready to use.
typedef struct tide_stale_page {
    tide_page *page;
    uint32_t bytes; // Of it that are in use: what its hash covers
} tide_stale_page;

typedef struct tide_stale_pages {
    tide_stale_page *pages;
    uint32_t count;
    uint32_t room;
    uint64_t bytes; // To hash, in all
} tide_stale_pages;

// `p` joins them, if its hash isn't of its first `bytes` bytes as they are.
// Each page once: a world has no page twice among what it hashes.
void tide_page_stale(tide_stale_pages *s, tide_page *p, uint32_t bytes);

// Hashes them on `jobs`' threads, where there's enough for that to pay, and
// lets the list go: it's zeroed. With less, or no threads (NULL), nothing is
// hashed here, and the hash that follows does it as it asks. Never from
// inside a tick, or anything else those threads are running. Returns how
// many pages it hashed, for whoever measures or tests it: 0 when it left
// them to the hash that follows.
struct tide_jobs;
uint32_t tide_pages_hash(tide_stale_pages *s, const struct tide_jobs *jobs);
