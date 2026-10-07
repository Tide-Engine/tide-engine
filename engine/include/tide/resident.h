#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/net.h"
#include "tide/page.h"

// Resident state: the memory of a C library that keeps a big state between
// calls (a physics world), kept by the world instead of serialized into it.
//
// The library allocates everything it owns from an arena the engine keeps at
// a fixed address for the whole process, and says what it may have written
// (tide_arena_touch). When its tick is done it commits (tide_arena_commit):
// every touched page is copied into the tide_resident of the world that
// ticked, pages the world shares with its snapshots like every other page,
// and a page nothing touched stays the one the world had. Before a tick of a
// world whose stamp isn't the arena's, the library restores
// (tide_arena_restore): the pages that differ from what the arena holds, by
// identity, are copied back in. So a rollback costs the pages of the ticks
// between, and nothing is written out.
//
// The arena's bytes are this process's own: they hold pointers, and threads
// allocate in whatever order they get there. Nothing of them is hashed or
// sent. What the match agrees on is the digest the library writes with each
// commit, which the world's hash covers with the stamp, and a machine that
// has none of the state is sent canonical bytes the library writes itself
// (tide_resident_kind.pack), only when a world is packed: a join, a resync,
// a migration, a reload.

enum {
    TIDE_ARENA_PAGE_SHIFT = 14,
    TIDE_ARENA_PAGE = 1u << TIDE_ARENA_PAGE_SHIFT, // 16 KiB, a page as the world keeps them
    TIDE_ARENA_DEFAULT = 64u << 20,                // Bytes an arena has unless its kind says
    TIDE_ARENA_USER = 256,                         // Bytes of its first page the library keeps its own things in
    TIDE_RESIDENT_KINDS = 8,                       // Kinds a process can have
};

typedef struct tide_arena tide_arena;

// The pages a resident keeps: a table shared between a world and its
// snapshots until one commits into it.
typedef struct tide_resident_pages {
    uint32_t refs;
    uint32_t count; // Pages the state spans
    tide_page *page[]; // Each NULL until it was touched once
} tide_resident_pages;

// The field: what generated code copies, frees, hashes (`stamp` and `digest`
// only) and packs (through the kind's functions). Zeroed, it holds nothing.
typedef struct tide_resident {
    tide_resident_pages *pages; // This machine's, or NULL
    uint64_t kind;              // Which library's: its registered name's hash, or 0
    uint64_t stamp;             // Which state, the library's own numbering
    uint64_t digest;            // What the match agrees on of it
} tide_resident;

enum { TIDE_RESIDENT_HASHED = 16 }; // Where `stamp` starts: the 16 bytes the hash covers

// A kind of resident state, registered by the library as it loads.
typedef struct tide_resident_kind {
    const char *name;     // Unique, and the same across builds: "Box3D"
    uint32_t arena_bytes; // 0 for TIDE_ARENA_DEFAULT
    // The canonical bytes of the state `r` names, for a machine that has none
    // of it: the library restores `r` into its arena first if it has to.
    void (*pack)(void *context, const tide_resident *r, tide_writer *w);
    // Makes the arena's state from `size` canonical bytes and commits it into
    // `r` (which is zeroed but for its kind). False when the bytes aren't it.
    bool (*unpack)(void *context, tide_resident *r, tide_reader *rd, uint32_t size);
    // The arena was taken over by this build of the library (a hot reload),
    // with the state the last build left: set what of the library's own is
    // kept in it again (callbacks, and anything that pointed into the last
    // build's code or constants).
    void (*relink)(void *context, tide_arena *arena);
    void *context;
} tide_resident_kind;

// Registers `kind` (copied) under its name, or replaces the one of that name:
// a library loaded again registers again. Gives the kind's number, the name's
// hash, which a world's resident names it by.
uint64_t tide_resident_register(const tide_resident_kind *kind);
uint64_t tide_resident_kind_of(const char *name);

// ---------------------------------------------------------------------------
// The registry of arenas, which outlives a build of the game's library: it
// rides in the local state, which the host keeps, and the library adopts it
// as it loads (generated code does, from tide_local_init and the reload's
// rebinding). Without one adopted, the engine uses one of its own.

typedef struct tide_residents {
    uint64_t kind[TIDE_RESIDENT_KINDS];
    tide_arena *arena[TIDE_RESIDENT_KINDS];
} tide_residents;

void tide_residents_adopt(tide_residents *table);

// The kind's arena, made the first time it's asked for. A kind nothing
// registered gets one of the default size.
tide_arena *tide_resident_arena(uint64_t kind);

// ---------------------------------------------------------------------------
// The arena. Its first page holds the allocator's own state and
// TIDE_ARENA_USER bytes for the library (tide_arena_user), which commits and
// restores carry like any page.

// `size` bytes, from any thread, aligned to 64 at the least (a block under
// half a page is aligned to its size rounded up to a power of two, a bigger
// one to a page). Running out stops the program, naming the kind. A new
// block counts as touched whole. Freeing says the size it was asked with.
void *tide_arena_alloc(tide_arena *a, size_t size);
void tide_arena_free(tide_arena *a, void *p, size_t size);
// realloc: a new block with the first `old_size` bytes of `p`, which is freed.
void *tide_arena_realloc(tide_arena *a, void *p, size_t old_size, size_t size);

// Whether `p` is in the arena.
bool tide_arena_holds(const tide_arena *a, const void *p);
void *tide_arena_user(tide_arena *a);
uint64_t tide_arena_stamp(const tide_arena *a); // What the arena holds: the last commit's or restore's stamp, 0 for nothing

// What may have been written: `size` bytes from `p`, from any thread.
void tide_arena_touch(tide_arena *a, const void *p, size_t size);
// Everything the arena has handed out.
void tide_arena_touch_all(tide_arena *a);

// Copies every touched page into `r` (which keeps `kind`, `stamp` and
// `digest` too), and clears the touches. `r` is the world's that ticked.
void tide_arena_commit(tide_arena *a, tide_resident *r, uint64_t stamp, uint64_t digest);

// Makes the arena the state `r` holds: the pages that differ from what it
// holds are copied in. Nothing may be touched and uncommitted. A resident
// with no pages empties the arena.
void tide_arena_restore(tide_arena *a, const tide_resident *r);

// For tests: whether every page `r` has is what the arena holds there,
// byte for byte, and which page differs first (-1 for none).
int64_t tide_arena_differs(const tide_arena *a, const tide_resident *r);

// What's been handed out, in bytes, and the arena's pages in use.
size_t tide_arena_used(const tide_arena *a);
uint32_t tide_arena_pages(const tide_arena *a);

// ---------------------------------------------------------------------------
// What generated code calls for the field.

void tide_resident_copy(tide_resident *to, const tide_resident *from);
void tide_resident_free(tide_resident *r);
static inline uint64_t tide_resident_hash(const uint64_t h, const tide_resident *r)
{
    return tide_hash_more(h, &r->stamp, 2u * sizeof(uint64_t));
}
// The canonical bytes: a kind nothing registered packs as nothing.
uint32_t tide_resident_packed_size(const tide_resident *r);
void tide_resident_pack(const tide_resident *r, tide_writer *w);
bool tide_resident_unpack(tide_resident *r, tide_reader *rd);
