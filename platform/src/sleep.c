// Whether the machine slept (tide_platform_slept): the time it spent asleep is
// a clock that counts sleep less one that doesn't, which only grows while the
// machine sleeps. The engine's own clock (tide_time_now_ns) is one that
// doesn't, so a match sees no gap where the machine slept.

#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // CLOCK_BOOTTIME under strict C
#endif

#include "tide/platform.h"

#if defined(_WIN32)
// Windows 7, which has QueryUnbiasedInterruptTime. MinGW's headers start older.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#elif !defined(__wasm__)
#include <time.h>
#endif

// Less than this asleep isn't a sleep: what the two clocks' readings differ
// by besides (Windows' tick count moves in steps of about 16 ms)
#define SLEPT_LEAST 1.0

static bool pretended; // A script's `sleep` (tide_platform_pretend_sleep)
static bool known;
static double asleep_before; // Seconds asleep since the machine started, as of the last call

// Seconds the machine spent asleep since it started, or -1 where the system
// doesn't say (the web, where the browser freezes a page it hides).
static double seconds_asleep(void)
{
#if defined(_WIN32)
    // The tick count goes on while the machine sleeps, and the unbiased interrupt time doesn't
    ULONGLONG unbiased = 0;
    if (!QueryUnbiasedInterruptTime(&unbiased)) return -1.0;
    return (double)GetTickCount64() * 1e-3 - (double)unbiased * 1e-7;
#elif defined(__APPLE__)
    mach_timebase_info_data_t base;
    if (mach_timebase_info(&base) != KERN_SUCCESS || !base.denom) return -1.0;
    const uint64_t awake = mach_absolute_time(), all = mach_continuous_time();
    return all > awake ? (double)(all - awake) * (double)base.numer / (double)base.denom * 1e-9 : 0.0;
#elif defined(__wasm__) || !defined(CLOCK_BOOTTIME)
    return -1.0;
#else
    // Linux and Android: the boot clock counts sleep, the monotonic one doesn't
    struct timespec awake, all;
    if (clock_gettime(CLOCK_MONOTONIC, &awake) != 0 || clock_gettime(CLOCK_BOOTTIME, &all) != 0) return -1.0;
    return (double)(all.tv_sec - awake.tv_sec) + (double)(all.tv_nsec - awake.tv_nsec) * 1e-9;
#endif
}

bool tide_platform_slept(void)
{
    const double asleep = seconds_asleep();
    const bool slept = pretended || (known && asleep >= 0.0 && asleep - asleep_before >= SLEPT_LEAST);
    pretended = false;
    if (asleep >= 0.0) {
        known = true;
        asleep_before = asleep;
    }
    return slept;
}

void tide_platform_pretend_sleep(void)
{
    pretended = true;
}
