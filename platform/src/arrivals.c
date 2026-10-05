// When a UDP socket's datagrams arrived (see arrivals.h).
//
// Linux, macOS and Android stamp each datagram as it comes in, and hand the
// stamp over with it (SO_TIMESTAMP). Windows only does from Windows 11 on, so
// there the system calls us when one arrives, on a thread of its pool, which
// reads it and notes the time; the frame then takes what was read.

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasi__)
#define _DEFAULT_SOURCE // SO_TIMESTAMP and the control messages under strict C
#endif

#include "arrivals.h"

#if !defined(__wasi__) // The web has no sockets: its rooms' datagrams arrive in the page (tide.js)

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/time.h"

static double now(void)
{
    return (double)tide_time_now_ns() * 1e-9;
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// What was read and nobody took yet, one after another: each a `held`, then
// its bytes.
typedef struct held {
    double arrived;
    uint32_t ip;
    uint16_t port;
    uint16_t size;
} held;

struct tide_arrivals {
    SOCKET socket;
    HANDLE readable; // Set by the system when the socket has something to read
    HANDLE waiting;  // The pool's wait on it
    CRITICAL_SECTION lock;
    uint8_t *bytes;
    uint32_t from; // Where the oldest starts
    uint32_t to;   // ...and the newest ends
    uint32_t capacity;
    uint32_t room;
};

// Room for `size` more bytes at the end: what's taken makes room first, then
// more memory, up to `room`. Under the lock.
static bool make_room(tide_arrivals *a, const uint32_t size)
{
    if (a->to + size <= a->capacity) return true;
    if (a->from > 0) {
        memmove(a->bytes, a->bytes + a->from, a->to - a->from);
        a->to -= a->from;
        a->from = 0;
        if (a->to + size <= a->capacity) return true;
    }
    uint32_t capacity = a->capacity ? a->capacity : 16384u;
    while (capacity < a->to + size && capacity < a->room) capacity *= 2u;
    if (capacity > a->room) capacity = a->room;
    if (capacity < a->to + size) return false; // Like a socket's own buffer when it's full: lost
    uint8_t *bytes = realloc(a->bytes, capacity);
    if (!bytes) return false;
    a->bytes = bytes;
    a->capacity = capacity;
    return true;
}

// The system's call when the socket has something: everything it has, each
// noted as arriving now. Reading is what makes the system say so again for
// the next one.
static VOID CALLBACK on_readable(const PVOID context, const BOOLEAN timed_out)
{
    (void)timed_out;
    tide_arrivals *a = context;
    for (;;) {
        uint8_t data[2048];
        struct sockaddr_in from;
        int length = sizeof from;
        const int n = recvfrom(a->socket, (char *)data, (int)sizeof data, 0, (struct sockaddr *)&from, &length);
        if (n < 0) {
            // Windows reports a datagram that couldn't be delivered on the
            // next read, and one too long for `data` as an error: skip both.
            const int error = WSAGetLastError();
            if (error == WSAECONNRESET || error == WSAEMSGSIZE) continue;
            return;
        }
        if (n == 0 || from.sin_family != AF_INET) continue;
        const held h = {now(), ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), (uint16_t)n};
        EnterCriticalSection(&a->lock);
        if (make_room(a, (uint32_t)sizeof h + (uint32_t)n)) {
            memcpy(a->bytes + a->to, &h, sizeof h);
            memcpy(a->bytes + a->to + sizeof h, data, (size_t)n);
            a->to += (uint32_t)sizeof h + (uint32_t)n;
        }
        LeaveCriticalSection(&a->lock);
    }
}

tide_arrivals *tide_arrivals_start(const uintptr_t socket, const uint32_t room)
{
    tide_arrivals *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->socket = (SOCKET)socket;
    a->room = room;
    InitializeCriticalSection(&a->lock);
    // An event that resets as the wait on it is satisfied: one call for each
    // time the system says there's something. The call runs on the pool's
    // waiting thread itself, so it's there as soon as the datagram is.
    a->readable = CreateEventW(NULL, FALSE, FALSE, NULL);
    const bool selected = a->readable && WSAEventSelect(a->socket, a->readable, FD_READ) == 0;
    if (!selected || !RegisterWaitForSingleObject(&a->waiting, a->readable, on_readable, a, INFINITE, WT_EXECUTEINWAITTHREAD)) {
        if (selected) WSAEventSelect(a->socket, NULL, 0);
        if (a->readable) CloseHandle(a->readable);
        DeleteCriticalSection(&a->lock);
        free(a);
        return NULL;
    }
    return a;
}

size_t tide_arrivals_next(tide_arrivals *a, uint32_t *ip, uint16_t *port, void *out, const size_t capacity, double *arrived)
{
    size_t size = 0;
    EnterCriticalSection(&a->lock);
    while (!size && a->from < a->to) {
        held h;
        memcpy(&h, a->bytes + a->from, sizeof h);
        if (h.size <= capacity) {
            memcpy(out, a->bytes + a->from + sizeof h, h.size);
            *ip = h.ip;
            *port = h.port;
            *arrived = h.arrived;
            size = h.size;
        }
        a->from += (uint32_t)sizeof h + h.size;
    }
    if (a->from == a->to) a->from = a->to = 0;
    LeaveCriticalSection(&a->lock);
    return size;
}

void tide_arrivals_stop(tide_arrivals *a)
{
    if (!a) return;
    WSAEventSelect(a->socket, NULL, 0);
    UnregisterWaitEx(a->waiting, INVALID_HANDLE_VALUE); // Returns once no call is running, or will
    CloseHandle(a->readable);
    DeleteCriticalSection(&a->lock);
    free(a->bytes);
    free(a);
}

#else

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

struct tide_arrivals {
    int socket;
    double emptied; // When it last had nothing more: what's read since arrived after
};

tide_arrivals *tide_arrivals_start(const uintptr_t socket, const uint32_t room)
{
    (void)room; // The system holds what nobody read yet
    tide_arrivals *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->socket = (int)socket;
    a->emptied = now();
    // Without it, a datagram arrives as it's read: a ping then counts the wait
    const int on = 1;
    setsockopt(a->socket, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof on);
    return a;
}

size_t tide_arrivals_next(tide_arrivals *a, uint32_t *ip, uint16_t *port, void *out, const size_t capacity, double *arrived)
{
    for (;;) {
        struct sockaddr_in from;
        struct iovec bytes = {out, capacity};
        union {
            struct cmsghdr aligned;
            char data[256];
        } control;
        struct msghdr m;
        memset(&m, 0, sizeof m);
        m.msg_name = &from;
        m.msg_namelen = sizeof from;
        m.msg_iov = &bytes;
        m.msg_iovlen = 1;
        m.msg_control = control.data;
        m.msg_controllen = sizeof control.data;
        const ssize_t n = recvmsg(a->socket, &m, 0);
        const double read = now();
        if (n < 0) {
            if (errno == EINTR) continue;
            a->emptied = read;
            return 0;
        }
        if (n == 0 || from.sin_family != AF_INET || (m.msg_flags & MSG_TRUNC)) continue; // Not IPv4, or too long
        // The system's stamp is by the wall's clock: how long ago that is, by
        // the same clock, is how long the datagram waited. Never longer than
        // since the socket last had nothing, whatever that clock did meanwhile.
        double waited = 0.0;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
            if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_TIMESTAMP) continue;
            struct timeval stamp, wall;
            memcpy(&stamp, CMSG_DATA(c), sizeof stamp);
            gettimeofday(&wall, NULL);
            waited = (double)(wall.tv_sec - stamp.tv_sec) + (double)(wall.tv_usec - stamp.tv_usec) * 1e-6;
        }
        if (waited > read - a->emptied) waited = read - a->emptied;
        if (waited < 0.0) waited = 0.0;
        *ip = ntohl(from.sin_addr.s_addr);
        *port = ntohs(from.sin_port);
        *arrived = read - waited;
        return (size_t)n;
    }
}

void tide_arrivals_stop(tide_arrivals *a)
{
    free(a);
}

#endif

#endif
