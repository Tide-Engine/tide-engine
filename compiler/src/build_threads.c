#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "build_threads.h"

#include <stdatomic.h>
#include <stddef.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct each {
    uint32_t count;
    void (*work)(void *context, uint32_t i);
    void *context;
    atomic_uint next;
} each;

static void take(each *e)
{
    for (uint32_t i; (i = atomic_fetch_add(&e->next, 1u)) < e->count;) e->work(e->context, i);
}

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID e)
{
    take(e);
    return 0;
}
#else
static void *thread_main(void *e)
{
    take(e);
    return NULL;
}
#endif

enum { THREADS_MOST = 64 };

static uint32_t processors(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const long got = (long)info.dwNumberOfProcessors;
#else
    const long got = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return got < 1 ? 1u : got > THREADS_MOST ? THREADS_MOST : (uint32_t)got;
}

void build_threads_each(const uint32_t count, const uint32_t most, void (*work)(void *context, uint32_t i), void *context)
{
    if (!count) return;
    each e = {.count = count, .work = work, .context = context};
    atomic_init(&e.next, 0u);
    uint32_t threads = processors();
    if (most && threads > most) threads = most;
    if (threads > count) threads = count;
#ifdef _WIN32
    HANDLE handles[THREADS_MOST];
#else
    pthread_t handles[THREADS_MOST];
#endif
    uint32_t started = 0;
    for (uint32_t i = 1; i < threads; i++) { // The caller's thread is the first: one that can't start leaves fewer
#ifdef _WIN32
        handles[started] = CreateThread(NULL, 0, thread_main, &e, 0, NULL);
        if (!handles[started]) break;
#else
        if (pthread_create(&handles[started], NULL, thread_main, &e) != 0) break;
#endif
        started++;
    }
    take(&e);
    for (uint32_t i = 0; i < started; i++) {
#ifdef _WIN32
        WaitForSingleObject(handles[i], INFINITE);
        CloseHandle(handles[i]);
#else
        pthread_join(handles[i], NULL);
#endif
    }
}
