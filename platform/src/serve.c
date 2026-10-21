#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE
#endif

#include "tide/serve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "tide/time.h"

#if defined(__wasm__) || defined(__ANDROID__)
int tide_serve(const tide_serve_desc *desc)
{
    (void)desc;
    fputs("tide: dedicated server executables are desktop builds\n", stderr);
    return 2;
}
#else

#include <signal.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <time.h>
#else
#include <time.h>
#include <unistd.h>
#endif

static volatile sig_atomic_t serve_stopped;

static void serve_stop(const int signal_number)
{
    (void)signal_number;
    serve_stopped = 1;
}

static bool serve_number(const char *text, const uint32_t most, uint32_t *out)
{
    uint64_t n = 0;
    if (!*text) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        n = n * 10u + (unsigned)(*text - '0');
        if (n > most) return false;
    }
    *out = (uint32_t)n;
    return true;
}

static bool serve_secure(void *user, const tide_address peer)
{
    (void)user;
    return peer.kind == TIDE_ADDRESS_ROOM; // WebRTC's authenticated DTLS channel.
}

static bool serve_load(const tide_serve_desc *desc)
{
    if (!desc->assets) return true;
    char path[4096];
#ifdef _WIN32
    const DWORD length = GetModuleFileNameA(NULL, path, sizeof path);
    if (!length || length >= sizeof path) return false;
#elif defined(__APPLE__)
    uint32_t executable_room = sizeof path;
    if (_NSGetExecutablePath(path, &executable_room)) return false;
#else
    const ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1u);
    if (length < 0 || (size_t)length >= sizeof path - 1u) return false;
    path[length] = 0;
#endif
    char *base = path;
    for (char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    const size_t room = sizeof path - (size_t)(base - path);
    const int written = snprintf(base, room, "%s.pack", desc->pack_name);
    if (written < 0 || (size_t)written >= room) return false;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "tide: cannot open server assets at %s\n", path); return false; }
    bool ok = fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? ftell(f) : -1;
    ok = size >= 0 && (uint64_t)size <= UINT32_MAX && fseek(f, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)size ? (size_t)size : 1u) : NULL;
    ok = bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (ok) ok = desc->assets(bytes, (size_t)size);
    free(bytes);
    return ok;
}

int tide_serve(const tide_serve_desc *desc)
{
    if (!desc || !desc->game || !desc->pack_name) return 2;
    uint32_t port = 7777, frames = 0;
    bool udp_only = false;
    tide_hosting_desc host = {.game = desc->game, .entropy = tide_platform_entropy, .secure_peer = serve_secure};
    for (int i = 1; i < desc->argc; i++) {
        const char *a = desc->argv[i];
        if (strcmp(a, "--anonymous") == 0) {
            host.allow_anonymous_create = host.allow_anonymous_join = true;
        } else if (strcmp(a, "--udp-only") == 0) udp_only = true;
        else {
            uint32_t value;
            const uint32_t most = strcmp(a, "--port") == 0 ? 65535u
                                : strcmp(a, "--max-matches") == 0 ? 4096u
                                : strcmp(a, "--max-connections") == 0 ? TIDE_PLAYERS_MOST
                                : strcmp(a, "--frames") == 0 ? 1000000000u : 0u;
            if (!most || i + 1 >= desc->argc || !serve_number(desc->argv[++i], most, &value) || !value) {
                fprintf(stderr, "tide: invalid server option %s; use --port, --max-matches, --max-connections, "
                                "--anonymous, --udp-only or --frames\n", a);
                return 2;
            }
            if (strcmp(a, "--port") == 0) port = value;
            else if (strcmp(a, "--max-matches") == 0) host.max_matches = value;
            else if (strcmp(a, "--max-connections") == 0) host.max_connections = value;
            else frames = value;
        }
    }
    if (tide_hidden() && !frames) {
        fputs("tide: TIDE_HIDDEN is set; give the server --frames <n> to bound the run\n", stderr);
        return 2;
    }
    if (desc->entry) desc->entry(&host);
    if (desc->main_is_local && !host.start) {
        fputs("tide: Main is local; define tide_server_entry and select a match scene in its start callback\n", stderr);
        return 2;
    }
    if (!host.authorize && !host.allow_anonymous_create && !host.allow_anonymous_join) {
        fputs("tide: configure authentication in tide_server_entry, or use --anonymous for quick startup\n", stderr);
        return 2;
    }
    if (!serve_load(desc)) return 1;
    host.jobs = tide_platform_jobs();
    if (!(udp_only ? tide_platform_udp_open((uint16_t)port, &host.transport)
                   : tide_platform_host_open((uint16_t)port, &host.transport))) {
        fprintf(stderr, "tide: cannot listen on server port %u\n", (unsigned)port);
        tide_pack_unload();
        return 1;
    }
    tide_hosting *h = tide_hosting_create(&host, (double)tide_time_now_ns() * 1e-9);
    if (!h) { host.transport.close(host.transport.self); tide_pack_unload(); return 1; }
    serve_stopped = 0;
    void (*previous_int)(int) = signal(SIGINT, serve_stop);
    void (*previous_term)(int) = signal(SIGTERM, serve_stop);
    fprintf(stderr, "tide: dedicated server listening on port %u\n", (unsigned)port);
    if (host.ready) host.ready(host.user, h);
#ifdef _WIN32
    // A per-process timer avoids Sleep(1)'s coarse system timer resolution,
    // without changing the global multimedia timer settings.
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2u, TIMER_ALL_ACCESS);
#endif
    uint32_t update = 0;
    char shown[TIDE_ROOM_CODE_LENGTH + 1] = {0};
    while (!serve_stopped && (!frames || update++ < frames)) {
        const double now = (double)tide_time_now_ns() * 1e-9;
        if (host.poll) host.poll(host.user, h, now);
        tide_hosting_update(h, now);
        if (!udp_only) {
            char room[TIDE_ROOM_CODE_LENGTH + 1] = {0};
            tide_platform_room_code(room, sizeof room);
            if (*room && strcmp(shown, room) != 0) {
                memcpy(shown, room, sizeof shown);
                fprintf(stderr, "tide: server room %s; join %s/0 to request a match\n", room, room);
            }
        }
#ifdef _WIN32
        const LARGE_INTEGER due = {.QuadPart = -10000};
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(1);
#else
        const struct timespec pause = {.tv_nsec = 1000000};
        nanosleep(&pause, NULL);
#endif
    }
#ifdef _WIN32
    if (timer) CloseHandle(timer);
#endif
    if (previous_int != SIG_ERR) signal(SIGINT, previous_int);
    if (previous_term != SIG_ERR) signal(SIGTERM, previous_term);
    tide_hosting_destroy(h);
    tide_pack_unload();
    return 0;
}
#endif
