// When memory runs out (tide/page.h): waiting for the system to have more,
// what it says of its memory, and the end of the program when there's none. A
// file of its own, since it includes the operating system's headers.
#if !defined(_WIN32) && !defined(__wasm__) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // nanosleep, open and read under strict C
#endif

#include "tide/page.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "tide/text.h"
#include "tide/time.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2 // K32GetProcessMemoryInfo and K32GetPerformanceInfo are kernel32's: nothing more to link
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <time.h>
#elif !defined(__wasm__)
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#endif

// The longest an allocation waits for memory before the program ends: long
// enough for Windows to grow its page file, or for another program to let go.
#define WAIT_MOST_MS 10000u
#define WAIT_STEP_MOST_MS 100u

// Kept from the start and let go just before the program ends for want of
// memory, so what it says then, its crash's dump and the crash reporter it
// starts have some. On the web the memory is the page's whatever happens, and
// a program's grows only, so none is kept. A game's library in a run that
// reloads keeps its own, as it has its own copy of this file.
#define RESERVE_BYTES ((size_t)8u << 20)
#ifndef __wasm__
static _Atomic(void *) reserve;
__attribute__((constructor)) static void keep_reserve(void)
{
    atomic_store(&reserve, malloc(RESERVE_BYTES));
}
#endif

void tide_memory_spend_reserve(void)
{
#ifndef __wasm__
    free(atomic_exchange(&reserve, NULL)); // Once, whichever thread gets there first
#endif
}

// ---------------------------------------------------------------------------
// Text, put together without the C library's formatting, which a crash's
// handler (platform/src/log.c) can't count on

typedef struct out {
    char *bytes;
    size_t size, used;
} out;

static void put(out *o, const char *s)
{
    while (*s && o->used + 1u < o->size) o->bytes[o->used++] = *s++;
    if (o->size) o->bytes[o->used < o->size ? o->used : o->size - 1u] = '\0';
}

static void put_number(out *o, uint64_t value)
{
    char digits[24];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    char text[25];
    for (unsigned i = 0; i < count; i++) text[i] = digits[count - 1u - i];
    text[count] = '\0';
    put(o, text);
}

// Bytes for a person: 6.9 GB, 412 MB
static void put_bytes(out *o, const uint64_t bytes)
{
    const uint64_t mb = bytes >> 20;
    if (mb < 1024u) {
        put_number(o, mb);
        put(o, " MB");
        return;
    }
    const uint64_t tenths = (bytes * 10u + (1u << 29)) >> 30; // Rounded
    put_number(o, tenths / 10u);
    put(o, ".");
    put_number(o, tenths % 10u);
    put(o, " GB");
}

// ---------------------------------------------------------------------------
// What the system says

typedef struct figures {
    uint64_t program;      // Memory the program has, or 0 for unknown: on Windows its commit, elsewhere what's in RAM
    uint64_t program_ram;  // Of it in RAM (Windows), or 0
    uint64_t committed;    // All programs', against...
    uint64_t commit_limit; // ...what the system can give them (RAM and page file), or 0 for unknown
    uint64_t ram_free;     // RAM nobody uses, or 0 for unknown
    uint64_t ram;
} figures;

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
// A number after `name` in a /proc file's text, in kB, as bytes; 0 if it isn't there
static uint64_t proc_kb(const char *text, const char *name)
{
    for (const char *line = text; *line;) {
        const char *n = name, *at = line;
        while (*n && *at == *n) n++, at++;
        if (!*n && *at == ':') {
            uint64_t value = 0;
            while (*at && (*at < '0' || *at > '9') && *at != '\n') at++;
            while (*at >= '0' && *at <= '9') value = value * 10u + (uint64_t)(*at++ - '0');
            return value * 1024u;
        }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return 0;
}

static void read_proc(const char *path, char *text, const size_t size)
{
    text[0] = '\0';
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    size_t got = 0;
    for (ssize_t n; got + 1u < size && (n = read(fd, text + got, size - 1u - got)) > 0;) got += (size_t)n;
    close(fd);
    text[got] = '\0';
}
#endif

static figures get_figures(void)
{
    figures f = {0};
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters = {.cb = sizeof counters};
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&counters, sizeof counters)) {
        f.program = counters.PrivateUsage;
        f.program_ram = counters.WorkingSetSize;
    }
    PERFORMANCE_INFORMATION perf = {.cb = sizeof perf};
    if (GetPerformanceInfo(&perf, sizeof perf)) {
        f.committed = (uint64_t)perf.CommitTotal * perf.PageSize;
        f.commit_limit = (uint64_t)perf.CommitLimit * perf.PageSize;
        f.ram_free = (uint64_t)perf.PhysicalAvailable * perf.PageSize;
        f.ram = (uint64_t)perf.PhysicalTotal * perf.PageSize;
    }
#elif defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) f.program = info.phys_footprint;
    uint64_t ram = 0;
    size_t size = sizeof ram;
    if (sysctlbyname("hw.memsize", &ram, &size, NULL, 0) == 0) f.ram = ram;
#elif defined(__wasm__)
    f.program = (uint64_t)__builtin_wasm_memory_size(0) * 65536u;
#else
    char text[4096];
    read_proc("/proc/self/status", text, sizeof text);
    f.program = proc_kb(text, "VmRSS");
    read_proc("/proc/meminfo", text, sizeof text);
    f.ram = proc_kb(text, "MemTotal");
    f.ram_free = proc_kb(text, "MemAvailable");
    f.committed = proc_kb(text, "Committed_AS");
    f.commit_limit = proc_kb(text, "CommitLimit");
#endif
    return f;
}

// Whether the system as a whole is out: what it can still give is less than
// a twentieth of it. Unknown counts as no.
static bool system_out(const figures *f)
{
#if defined(_WIN32)
    return f->commit_limit && f->committed + f->commit_limit / 20u >= f->commit_limit;
#else
    return f->ram && f->ram_free < f->ram / 20u; // Linux and macOS give memory past their RAM and page file
#endif
}

size_t tide_memory_figures(char *text, const size_t size)
{
    out o = {text, size, 0};
    if (size) text[0] = '\0';
    const figures f = get_figures();
    put(&o, "this program has ");
    if (f.program) put_bytes(&o, f.program);
    else put(&o, "an unknown amount");
    if (f.program_ram) {
        put(&o, " (");
        put_bytes(&o, f.program_ram);
        put(&o, " of it in RAM)");
    }
#if defined(_WIN32)
    if (f.commit_limit) {
        put(&o, "; all programs have ");
        put_bytes(&o, f.committed);
        put(&o, " of the ");
        put_bytes(&o, f.commit_limit);
        put(&o, " the system can give (its RAM and page file)");
    }
#endif
    if (f.ram) {
        put(&o, "; ");
        if (f.ram_free) {
            put_bytes(&o, f.ram_free);
            put(&o, " of ");
        }
        put_bytes(&o, f.ram);
        put(&o, f.ram_free ? " RAM free" : " RAM");
    }
    return o.used;
}

// ---------------------------------------------------------------------------
// Waiting, and the end

#ifndef __wasm__
static void sleep_ms(const uint32_t ms)
{
#if defined(_WIN32)
    Sleep(ms);
#else
    const struct timespec t = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    nanosleep(&t, NULL);
#endif
}
#endif

bool tide_memory_wait(const uint64_t bytes, uint32_t *waited_ms)
{
    (void)bytes;
#ifdef __wasm__
    (void)waited_ms;
    return false; // A page's memory has a most it grows to, and its own thread can't wait
#else
    if (*waited_ms == 0) tide_scratch_trim(0); // This thread's own spare first
    if (*waited_ms >= WAIT_MOST_MS) return false;
    uint32_t step = *waited_ms < 1u ? 1u : *waited_ms; // Doubling: 1, 2, 4... up to WAIT_STEP_MOST_MS
    if (step > WAIT_STEP_MOST_MS) step = WAIT_STEP_MOST_MS;
    if (step > WAIT_MOST_MS - *waited_ms) step = WAIT_MOST_MS - *waited_ms;
    sleep_ms(step);
    *waited_ms += step;
    return true;
#endif
}

static _Atomic uint64_t said_at_ns; // When tide_memory_waited last said so, 0 for never

void tide_memory_waited(const uint64_t bytes, const uint32_t waited_ms)
{
    if (!waited_ms) return;
    // Once a second at the most: when memory runs short, it does on every thread at once
    const uint64_t now = tide_time_now_ns() | 1u;
    uint64_t was = atomic_load(&said_at_ns);
    if (was && now - was < 1000000000u) return;
    if (!atomic_compare_exchange_strong(&said_at_ns, &was, now)) return;
    char figures_text[512];
    tide_memory_figures(figures_text, sizeof figures_text);
    fprintf(stderr, "tide: memory ran short: %llu bytes came after waiting %u ms for the system to have them; %s\n",
            (unsigned long long)bytes, (unsigned)waited_ms, figures_text);
}

_Noreturn void tide_memory_ran_out(const char *what, const uint32_t waited_ms)
{
    tide_memory_spend_reserve();
    char figures_text[512];
    tide_memory_figures(figures_text, sizeof figures_text);
    figures_text[0] = figures_text[0] == 't' ? 'T' : figures_text[0]; // "This program has", a sentence of its own
    const figures f = get_figures();
    char waited[96] = "";
    if (waited_ms) snprintf(waited, sizeof waited, " after waiting %u s for more", (unsigned)((waited_ms + 500u) / 1000u));
    fprintf(stderr, "tide: out of memory: %s%s. %s%s\n", what, waited, figures_text,
            system_out(&f) ? ". The whole system is out of memory: close other programs, or give the system a bigger page file"
                           : "");
    abort();
}

_Noreturn void tide_out_of_memory(void)
{
    tide_memory_ran_out("there's no more memory", 0);
}
