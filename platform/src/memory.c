// How much memory the program has (tide_platform_memory). A file of its own,
// since it includes the operating system's headers.

#if !defined(_WIN32) && !defined(__wasm__) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // sysconf under strict C
#endif

#include "tide/platform.h"

#if defined(__wasm__)

uint64_t tide_platform_memory(void)
{
    return (uint64_t)__builtin_wasm_memory_size(0) * 65536u; // Its pages are 64 KiB
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2 // GetProcessMemoryInfo is kernel32's, so there's nothing more to link
#include <windows.h>
#include <psapi.h>

uint64_t tide_platform_memory(void)
{
    PROCESS_MEMORY_COUNTERS counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) return 0;
    return counters.WorkingSetSize;
}

#elif defined(__APPLE__)

#include <mach/mach.h>

uint64_t tide_platform_memory(void)
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return 0;
    return info.phys_footprint;
}

#else

#include <stdio.h>
#include <unistd.h>

// Linux and Android: /proc/self/statm counts pages, all of them and then those in RAM.
uint64_t tide_platform_memory(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long long all = 0, in_ram = 0;
    const int read = fscanf(f, "%llu %llu", &all, &in_ram);
    fclose(f);
    const long page = sysconf(_SC_PAGESIZE);
    return read == 2 && page > 0 ? (uint64_t)in_ram * (uint64_t)page : 0;
}

#endif
