// System headers stay apart from a game's generated component names.
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

void tide_lag_sleep(const double seconds)
{
#ifdef _WIN32
    // Sleep's ordinary timer can round a 144 Hz frame up to about 16 ms.
    // A high-resolution waitable timer keeps the requested pace without
    // changing the owner's timer resolution or spinning through the wait.
    static HANDLE timer;
    static int tried;
    if (!tried) {
        tried = 1;
        timer = CreateWaitableTimerExW(NULL, NULL, 0x00000002u, TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(seconds * 1e7);
    if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
    else Sleep((DWORD)(seconds * 1000.0));
#else
    struct timespec wanted = {(time_t)seconds, (long)((seconds - (double)(time_t)seconds) * 1e9)};
    while (nanosleep(&wanted, &wanted) && errno == EINTR) {}
#endif
}
