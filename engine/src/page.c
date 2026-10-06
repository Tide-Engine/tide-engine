#include "tide/page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"

#if defined(__wasm__) && defined(__wasm_atomics__)
#include <errno.h>
#endif

// See tide/page.h.

_Noreturn void tide_out_of_memory(void)
{
    fprintf(stderr, "tide: out of memory\n");
    abort();
}

#ifdef __wasm__
// 64 bytes a round, with no way out inside it, so the compiler makes SIMD
// loads of them: a run that differs is found within its first 64 bytes that
// do, and one that's the same is read once.
bool tide_same_bytes(const void *a, const void *b, const size_t size)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    size_t i = 0;
    for (; i + 64u <= size; i += 64u) {
        uint64_t apart = 0;
        for (size_t k = 0; k < 64u; k += 8u) {
            uint64_t p, q;
            memcpy(&p, x + i + k, 8);
            memcpy(&q, y + i + k, 8);
            apart |= p ^ q;
        }
        if (apart) return false;
    }
    for (; i + 8u <= size; i += 8u) {
        uint64_t p, q;
        memcpy(&p, x + i, 8);
        memcpy(&q, y + i, 8);
        if (p != q) return false;
    }
    for (; i < size; i++) {
        if (x[i] != y[i]) return false;
    }
    return true;
}
#endif

bool tide_hidden(void)
{
    const char *value = getenv("TIDE_HIDDEN");
    return value && value[0] && strcmp(value, "0") != 0;
}

#ifdef _WIN32

#include <crtdbg.h>

// Windows' own, without its headers.
__declspec(dllimport) unsigned int __stdcall GetErrorMode(void);
__declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int mode);

// Before main, in a run nobody watches: nobody is there to click a dialog
// away, so none shows. It's here, with what every program of the engine's
// links, so no program has to remember it.
__attribute__((constructor)) static void hidden_start(void)
{
    if (!tide_hidden()) return;
    // The system's, for a crash, or a library or a disk that's missing:
    // SEM_FAILCRITICALERRORS, SEM_NOGPFAULTERRORBOX and SEM_NOOPENFILEERRORBOX
    SetErrorMode(GetErrorMode() | 0x0001u | 0x0002u | 0x8000u);
    // The C runtime's: abort() ends the program at once, with exit code 3,
    // and a failed assert says so on stderr, in a program with no console too
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
#ifdef _DEBUG // The debug runtime's reports of what its own checks found, and of a failed _ASSERT
    for (int kind = _CRT_ERROR; kind <= _CRT_ASSERT; kind++) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
#endif
}

#endif

#if defined(__wasm__) && defined(__wasm_atomics__)

#define WASM_PAGE 65536u

// The program's memory, in WebAssembly's pages, as far as any thread grew it.
static uint32_t grown;

// The C library's sbrk, which its malloc grows the memory with: the same, and
// it notes how far, for tide_memory_sync.
void *sbrk(const intptr_t increment)
{
    if (increment == 0) return (void *)((uintptr_t)__builtin_wasm_memory_size(0) * WASM_PAGE);
    if (increment < 0 || (uintptr_t)increment % WASM_PAGE != 0) abort(); // As the C library's does
    const size_t old = __builtin_wasm_memory_grow(0, (size_t)increment / WASM_PAGE);
    if (old == SIZE_MAX) {
        errno = ENOMEM;
        return (void *)-1;
    }
    const uint32_t now = (uint32_t)__builtin_wasm_memory_size(0);
    uint32_t seen = __atomic_load_n(&grown, __ATOMIC_RELAXED);
    while (seen < now && !__atomic_compare_exchange_n(&grown, &seen, now, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    }
    return (void *)(old * WASM_PAGE);
}

void tide_memory_sync(void)
{
    // Growing by nothing brings this thread's size up to date
    if (__builtin_wasm_memory_size(0) < __atomic_load_n(&grown, __ATOMIC_ACQUIRE)) __builtin_wasm_memory_grow(0, 0);
}

void *tide_alloc(const size_t size)
{
    void *p = malloc(size ? size : 1u);
    if (!p) tide_out_of_memory();
    tide_memory_sync(); // Before anything touches it
    return p;
}

void *tide_alloc_zeroed(const size_t count, const size_t size)
{
    if (size && count > SIZE_MAX / size) tide_out_of_memory();
    void *p = tide_alloc(count * size); // Not calloc, which clears it before this thread can see it
    memset(p, 0, count * size);
    return p;
}

void *tide_realloc(void *p, const size_t old_size, const size_t size)
{
    // Not realloc, which can copy into memory this thread can't see yet
    void *grown_to = tide_alloc(size);
    if (p) memcpy(grown_to, p, old_size < size ? old_size : size);
    free(p);
    return grown_to;
}

#else

void *tide_alloc(const size_t size)
{
    void *p = malloc(size ? size : 1u);
    if (!p) tide_out_of_memory();
    return p;
}

void *tide_alloc_zeroed(const size_t count, const size_t size)
{
    void *p = calloc(count ? count : 1u, size ? size : 1u);
    if (!p) tide_out_of_memory();
    return p;
}

void *tide_realloc(void *p, const size_t old_size, const size_t size)
{
    (void)old_size;
    void *grown_to = realloc(p, size ? size : 1u);
    if (!grown_to) tide_out_of_memory();
    return grown_to;
}

#endif

// The pages a thread let go of, which it hands out again. A world and its
// snapshots let go of as many pages every tick as the tick changed and take
// as many, megabytes of them in a game with a big list or grid. One that was
// just let go of is still in the CPU's caches and costs no call. A new one of
// these sizes comes from the system each time, with a page fault as each
// 4 KiB of it is first written, and copying into those took five times as
// long.
//
// By size, for the sizes pages mostly have: a heap's (16 KiB, a list's or a
// grid's chunk too), and a chunk's lanes, which are a power of two from the
// 4 bytes of a first chunk's narrowest to the 16 KiB of a full one's widest.
// A page of another size (a block bigger than a heap's page, a table of
// structs) is the system's.
//
// A thread has its own, which cost it nothing to reach. The thread that lets
// go of a tick's pages (it drops a snapshot) is often not the one that takes
// them (it runs the system that writes), so what a thread has too many of
// goes where the others find them, several at once, and a thread with none
// looks there first. Each of those keeps as many as a tick of a big world
// changes: of every size together, 8 MB at most in a thread's own and 32 MB
// in the others'.
#define SPARE_LEAST 2u  // 4 bytes, as a power of two
#define SPARE_SIZES 13u // To 16 KiB, each twice the last
#define SPARES 256u
#define SPARES_MOVED 64u // To the others', or from them, at once
#define SPARES_SHARED 1024u
static TIDE_THREAD_LOCAL tide_page *spare[SPARE_SIZES][SPARES];
static TIDE_THREAD_LOCAL uint32_t spares[SPARE_SIZES];
static tide_page *shared[SPARE_SIZES][SPARES_SHARED];
static uint32_t shared_spares[SPARE_SIZES]; // Changed with the lock held, and read without it to see there are none
static uint32_t shared_lock;

// Held for a few moves of a pointer, so a thread that wants it spins: a web
// page's main thread may do nothing else.
static void lock_shared(void)
{
    while (__atomic_exchange_n(&shared_lock, 1u, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock_shared(void)
{
    __atomic_store_n(&shared_lock, 0u, __ATOMIC_RELEASE);
}

// Which of them pages of `size` bytes are kept in; SPARE_SIZES for none.
static uint32_t spare_size(const uint32_t size)
{
    if (size < 1u << SPARE_LEAST || size > 1u << (SPARE_LEAST + SPARE_SIZES - 1u) || (size & (size - 1u))) return SPARE_SIZES;
    return (uint32_t)__builtin_ctz(size) - SPARE_LEAST;
}

tide_page *tide_page_take(const uint32_t size)
{
    const uint32_t k = spare_size(size);
    if (k == SPARE_SIZES) return tide_alloc(sizeof(tide_page) + size);
    if (!spares[k] && __atomic_load_n(&shared_spares[k], __ATOMIC_RELAXED)) {
        lock_shared();
        uint32_t left = shared_spares[k];
        while (spares[k] < SPARES_MOVED && left) spare[k][spares[k]++] = shared[k][--left];
        __atomic_store_n(&shared_spares[k], left, __ATOMIC_RELAXED);
        unlock_shared();
        tide_memory_sync(); // Another thread may have got them from the system since this one last heard of its memory
    }
    return spares[k] ? spare[k][--spares[k]] : tide_alloc(sizeof(tide_page) + size);
}

void tide_page_give(tide_page *p)
{
    const uint32_t k = spare_size(p->size);
    if (k == SPARE_SIZES) {
        free(p);
        return;
    }
    if (spares[k] == SPARES) { // Too many for one thread: some go to the others, or back to the system
        lock_shared();
        uint32_t there = shared_spares[k];
        while (spares[k] > SPARES - SPARES_MOVED && there < SPARES_SHARED) shared[k][there++] = spare[k][--spares[k]];
        __atomic_store_n(&shared_spares[k], there, __ATOMIC_RELAXED);
        unlock_shared();
        while (spares[k] > SPARES - SPARES_MOVED) free(spare[k][--spares[k]]);
    }
    spare[k][spares[k]++] = p;
}

void tide_pages_free(void)
{
    for (uint32_t k = 0; k < SPARE_SIZES; k++) {
        while (spares[k]) free(spare[k][--spares[k]]);
        lock_shared();
        uint32_t left = shared_spares[k];
        while (left) free(shared[k][--left]);
        __atomic_store_n(&shared_spares[k], 0u, __ATOMIC_RELAXED);
        unlock_shared();
    }
}

tide_page *tide_page_new(const uint32_t size)
{
    tide_page *p = tide_alloc_zeroed(1, sizeof(tide_page) + size);
    p->refs = 1;
    p->size = size;
    p->hashed = UINT32_MAX;
    return p;
}

void tide_page_release(tide_page *p, const uint32_t refs)
{
    if (!p) return;
    p->refs -= refs;
    if (p->refs == 0) tide_page_give(p);
}

tide_page *tide_page_copy(const tide_page *p, const uint32_t refs, const uint32_t keep)
{
    tide_page *copy = tide_page_take(p->size);
    const uint32_t kept = keep < p->size ? keep : p->size;
    *copy = (tide_page){refs, p->size, p->first, UINT32_MAX, 0, 0};
    memcpy(tide_page_data(copy), tide_page_data(p), kept);
    memset((uint8_t *)tide_page_data(copy) + kept, 0, p->size - kept);
    return copy;
}

tide_page *tide_page_own_copy(tide_page *p, const uint32_t mine, const uint32_t keep)
{
    tide_page *copy = tide_page_copy(p, mine, keep);
    p->refs -= mine;
    return copy;
}

tide_page *tide_page_own_copy_around(tide_page *p, const uint32_t mine, const uint32_t keep, const uint32_t from, const uint32_t bytes)
{
    tide_page *copy = tide_page_take(p->size);
    const uint32_t kept = keep < p->size ? keep : p->size;
    const uint32_t start = from < p->size ? from : p->size; // The part left alone: start to end
    const uint32_t end = bytes < p->size - start ? start + bytes : p->size;
    *copy = (tide_page){mine, p->size, p->first, UINT32_MAX, 0, 0};
    uint8_t *to = tide_page_data(copy);
    const uint8_t *as_it_was = tide_page_data(p);
    // What's kept is copied and the rest is zeros, as in any copy, on each side of that part
    const uint32_t before = start < kept ? start : kept;
    memcpy(to, as_it_was, before);
    memset(to + before, 0, start - before);
    const uint32_t after = end < kept ? kept : end;
    memcpy(to + end, as_it_was + end, after - end);
    memset(to + after, 0, p->size - after);
    p->refs -= mine;
    return copy;
}

uint64_t tide_page_hash(tide_page *p, const uint32_t bytes)
{
    if (p->hashed != bytes) {
        p->hash = tide_hash(tide_page_data(p), bytes);
        p->hashed = bytes;
    }
    return p->hash;
}
