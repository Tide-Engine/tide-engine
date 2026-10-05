// UDP for sessions: tide_platform_udp_open and tide_platform_resolve (see
// tide/platform.h). A file of its own, for the operating system's headers.

#if !defined(_WIN32) && !defined(__wasi__)
#define _DEFAULT_SOURCE // getaddrinfo and struct addrinfo under strict C
#endif

#include "tide/platform.h"

#include <stdlib.h>
#include <string.h>

#include "arrivals.h"
#include "tide/time.h"

#if defined(__wasi__)

// The web has no UDP; a browser will reach servers another way.
bool tide_platform_udp_open(const uint16_t port, tide_transport *out)
{
    (void)port;
    (void)out;
    return false;
}

bool tide_platform_udp_open_local(const uint16_t port, tide_transport *out)
{
    return tide_platform_udp_open(port, out);
}

bool tide_platform_resolve(const char *text, const uint16_t default_port, tide_address *out)
{
    return tide_address_parse(text, default_port, out);
}

#else

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET udp_socket;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int udp_socket;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

typedef struct udp {
    udp_socket socket;
    tide_arrivals *arrivals; // Reads the socket, and says when each datagram arrived
    double arrived;          // ...which for the last one given was then
} udp;

static bool started(void)
{
#ifdef _WIN32
    static bool ready;
    if (!ready) {
        WSADATA data;
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    return ready;
#else
    return true;
#endif
}

static void udp_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const udp *u = self;
    if (to.kind != TIDE_ADDRESS_IPV4) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(to.port);
    a.sin_addr.s_addr = htonl(to.host);
    sendto(u->socket, (const char *)data, (int)size, 0, (const struct sockaddr *)&a, sizeof a);
}

static uint32_t udp_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    udp *u = self;
    uint32_t ip = 0;
    uint16_t port = 0;
    const size_t n = tide_arrivals_next(u->arrivals, &ip, &port, data, capacity, &u->arrived);
    if (n) *from = (tide_address){TIDE_ADDRESS_IPV4, ip, port};
    return (uint32_t)n;
}

static void udp_close(void *self)
{
    udp *u = self;
    tide_arrivals_stop(u->arrivals);
    close_socket(u->socket);
    free(u);
}

// A ping over it is the network's round trip (tide_transport.clock): the
// system says when each datagram arrived, by the clock everything here has.
static double udp_clock(void *self)
{
    (void)self;
    return (double)tide_time_now_ns() * 1e-9;
}

static double udp_arrived(void *self)
{
    const udp *u = self;
    return u->arrived;
}

static bool udp_open(const uint32_t host, const uint16_t port, tide_transport *out)
{
    if (!started()) return false;
    const udp_socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCKET) return false;
#ifdef _WIN32
    u_long nonblocking = 1;
    const bool configured = ioctlsocket(s, FIONBIO, &nonblocking) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    const bool configured = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(host);
    udp *u = configured && bind(s, (const struct sockaddr *)&a, sizeof a) == 0 ? calloc(1, sizeof *u) : NULL;
    // Up to 256 KiB of datagrams between two frames, as a joining player's world comes
    if (u) u->arrivals = tide_arrivals_start((uintptr_t)s, 262144u);
    if (!u || !u->arrivals) {
        free(u);
        close_socket(s);
        return false;
    }
    u->socket = s;
    *out = (tide_transport){.self = u, .send = udp_send, .receive = udp_receive, .close = udp_close, .clock = udp_clock,
                            .arrived = udp_arrived};
    return true;
}

bool tide_platform_udp_open(const uint16_t port, tide_transport *out)
{
    return udp_open(INADDR_ANY, port, out);
}

bool tide_platform_udp_open_local(const uint16_t port, tide_transport *out)
{
    return udp_open(INADDR_LOOPBACK, port, out);
}

bool tide_platform_resolve(const char *text, const uint16_t default_port, tide_address *out)
{
    if (tide_address_parse(text, default_port, out)) return true;
    if (!started()) return false;
    // name or name:port
    char name[256];
    const char *colon = strrchr(text, ':');
    const size_t length = colon ? (size_t)(colon - text) : strlen(text);
    if (length == 0 || length >= sizeof name) return false;
    memcpy(name, text, length);
    name[length] = '\0';
    uint32_t port = default_port;
    if (colon) {
        char *end;
        port = (uint32_t)strtoul(colon + 1, &end, 10);
        if (*end != '\0' || port > 65535u) return false;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *found = NULL;
    if (getaddrinfo(name, NULL, &hints, &found) != 0 || !found) return false;
    const struct sockaddr_in *a = (const struct sockaddr_in *)found->ai_addr;
    *out = (tide_address){TIDE_ADDRESS_IPV4, ntohl(a->sin_addr.s_addr), (uint16_t)port};
    freeaddrinfo(found);
    return true;
}

#endif
