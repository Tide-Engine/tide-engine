#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

double bench_wall(void)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart / (double)frequency.QuadPart;
}

double bench_cpu(void)
{
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    const uint64_t k = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    const uint64_t u = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (double)(k + u) * 1e-7;
}

uint64_t bench_cycles(void)
{
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return (uint64_t)cycles;
}
