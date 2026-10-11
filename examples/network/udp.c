// A network of the game's own (tide/network.h): its matches are opened and
// joined on plain UDP sockets, in the place of Tide's rooms, with nothing of
// the engine changed for it. It's the simplest network there is, and the
// docs' walkthrough (docs/multiplayer/own-network.md): a host is a socket on
// a port, the text players join by is its address, "127.0.0.1:50123", and a
// player is the address its datagrams come from.
//
// It stays on this machine (127.0.0.1): open it to a network by binding
// INADDR_ANY and showing the machine's own address. There's no host
// migration here: nothing says who a match's next host would be.

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // fcntl's flags under strict C
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/network.h"

#if defined(__wasm__)

// A page has no UDP: nothing is registered there, and its matches stay in
// Tide's rooms. The function is still there, as the game's Tide calls it.
bool UseUdpNetwork(void)
{
    return false;
}

#else

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET udp_socket;
typedef int udp_length;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int udp_socket;
typedef socklen_t udp_length;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

// A transport's own: its socket.
typedef struct udp_end {
    udp_socket socket;
} udp_end;

// What the match this machine opened or joined is joined by, "" for none.
static char udp_address[TIDE_NETWORK_ADDRESS_BYTES];

// An address is the network's own to give a meaning: here an IPv4 address
// and a port, which fit it as they are.
static void udp_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const udp_end *end = self;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(to.port);
    a.sin_addr.s_addr = htonl(to.host);
    sendto(end->socket, (const char *)data, (int)size, 0, (const struct sockaddr *)&a, sizeof a);
}

// The next datagram that's there, never waiting for one. A socket hands a
// datagram that's longer than the room for it over cut short, or not at all:
// both are dropped here, as the session expects.
static uint32_t udp_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    const udp_end *end = self;
    char whole[TIDE_NET_MTU + 1u];
    for (;;) {
        struct sockaddr_in a;
        udp_length length = sizeof a;
        const int n = (int)recvfrom(end->socket, whole, sizeof whole, 0, (struct sockaddr *)&a, &length);
        if (n < 0) {
#ifdef _WIN32
            // Windows says so here when a datagram this socket sent found nobody: the next one may still be there
            if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAEMSGSIZE) continue;
#endif
            return 0;
        }
        if (n == 0 || (uint32_t)n > capacity || (uint32_t)n > TIDE_NET_MTU) continue;
        memcpy(data, whole, (size_t)n);
        *from = (tide_address){TIDE_ADDRESS_NETWORK, ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
        return (uint32_t)n;
    }
}

static void udp_close(void *self)
{
    udp_end *end = self;
    close_socket(end->socket);
    free(end);
    udp_address[0] = '\0';
}

// A socket that never waits, on this machine's `port` (0: any free one), as
// a transport. False without one.
static bool udp_open(const uint16_t port, tide_transport *out, uint16_t *bound)
{
#ifdef _WIN32
    static bool ready;
    WSADATA started;
    if (!ready && WSAStartup(MAKEWORD(2, 2), &started) != 0) return false;
    ready = true;
#endif
    const udp_socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD_SOCKET) return false;
#ifdef _WIN32
    u_long on = 1;
    const bool never_waits = ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    const bool never_waits = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(0x7F000001u); // 127.0.0.1
    udp_length length = sizeof a;
    udp_end *end = NULL;
    if (never_waits && bind(s, (const struct sockaddr *)&a, sizeof a) == 0
        && getsockname(s, (struct sockaddr *)&a, &length) == 0) {
        end = malloc(sizeof *end);
    }
    if (!end) {
        close_socket(s);
        return false;
    }
    end->socket = s;
    *bound = ntohs(a.sin_port);
    *out = (tide_transport){.self = end, .send = udp_send, .receive = udp_receive, .close = udp_close};
    return true;
}

// Session.Open(): a socket on a port of the system's choosing, which is the
// text players join by.
static bool udp_host(void *self, tide_transport *out)
{
    (void)self;
    uint16_t port = 0;
    if (!udp_open(0, out, &port)) return false;
    snprintf(udp_address, sizeof udp_address, "127.0.0.1:%u", (unsigned)port);
    return true;
}

// Session.Join(text): a socket of this machine's own, and the host's address
// from the text, which a player may have typed: text that isn't an address
// joins nothing.
static bool udp_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self;
    tide_address host;
    uint16_t port = 0;
    if (!tide_address_parse(address, 0, &host) || !host.port) return false;
    if (!udp_open(0, out, &port)) return false;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, host.host, host.port};
    snprintf(udp_address, sizeof udp_address, "%u.%u.%u.%u:%u", (unsigned)(host.host >> 24), (unsigned)(host.host >> 16 & 255u),
             (unsigned)(host.host >> 8 & 255u), (unsigned)(host.host & 255u), (unsigned)host.port);
    return true;
}

// Session.room.
static void udp_room(void *self, char *out, const size_t size)
{
    (void)self;
    snprintf(out, size, "%s", udp_address);
}

// What the game's Tide calls before its first Session.Open or Session.Join:
// `extern bool UseUdpNetwork();`. True where there's UDP.
bool UseUdpNetwork(void)
{
    tide_network_set(&(tide_network){.host = udp_host, .join = udp_join, .address = udp_room});
    return true;
}

#endif
