#ifndef _WIN32
#define _DEFAULT_SOURCE // Sockets under strict C
#endif

#include "serve.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock;
#define NO_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock;
#define NO_SOCKET (-1)
#define close_socket close
#endif

// See serve.h. Connections are answered one request each, then closed; ones
// that don't send a whole request are closed when there's no room for more,
// the oldest first.

#define CONNECTIONS 16
#define REQUEST_BYTES 4096

typedef struct connection {
    sock s;
    uint64_t opened; // Its place among the connections, to close the oldest
    uint32_t got;
    char request[REQUEST_BYTES];
    // A POST's body, once its headers are in: `want` bytes, `have` of them so far
    uint8_t *body;
    uint32_t want, have;
} connection;

struct serve {
    sock listener;
    connection connections[CONNECTIONS];
    uint64_t opened;
};

static void set_blocking(const sock s, const bool blocking)
{
#ifdef _WIN32
    u_long nonblocking = blocking ? 0 : 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
#endif
}

serve *serve_open(uint16_t *port)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return NULL;
#endif
    const sock listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == NO_SOCKET) return NULL;
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // This machine only
    address.sin_port = 0;                             // Any free port
    socklen_t length = sizeof address;
    if (bind(listener, (struct sockaddr *)&address, sizeof address) != 0 || listen(listener, 16) != 0
        || getsockname(listener, (struct sockaddr *)&address, &length) != 0) {
        close_socket(listener);
        return NULL;
    }
    set_blocking(listener, false);
    serve *s = calloc(1, sizeof *s);
    if (!s) abort();
    s->listener = listener;
    for (int i = 0; i < CONNECTIONS; i++) s->connections[i].s = NO_SOCKET;
    *port = ntohs(address.sin_port);
    return s;
}

static void close_connection(connection *c)
{
    close_socket(c->s);
    c->s = NO_SOCKET;
    free(c->body);
    c->body = NULL;
    c->want = c->have = 0;
    c->got = 0;
}

static bool send_all(const sock s, const char *data, size_t size)
{
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL; // A page that went away mustn't end tide
#else
    const int flags = 0;
#endif
    while (size > 0) {
        const int chunk = size > (1u << 20) ? (1 << 20) : (int)size;
        const int sent = (int)send(s, data, chunk, flags);
        if (sent <= 0) return false;
        data += sent;
        size -= (size_t)sent;
    }
    return true;
}

// Whether the request has an X-Tide header, its name in any case
static bool tide_header(const char *request)
{
    for (const char *line = strstr(request, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        const char *name = line + 2, *want = "x-tide:";
        size_t i = 0;
        while (want[i] && tolower((unsigned char)name[i]) == want[i]) i++;
        if (!want[i]) return true;
    }
    return false;
}

static void reply(connection *c, const serve_fn answer, void *user)
{
    char method[8] = "";
    char path[2048] = "";
    serve_reply r = {404, "text/plain; charset=utf-8", "Not found\n", 10};
    const bool read = sscanf(c->request, "%7s %2047s", method, path) == 2;
    if (read && strcmp(method, "GET") == 0) {
        // The query goes to `answer` only from the page's own requests, with their X-Tide header: another site's
        // page can't send one here without asking first (CORS), which this server never allows
        char *query = strchr(path, '?');
        if (query && !tide_header(c->request)) *query = '\0';
        answer(user, path, NULL, 0, &r);
    } else if (read && strcmp(method, "POST") == 0 && c->body && tide_header(c->request)) {
        answer(user, path, c->body, c->want, &r); // Its X-Tide header: the page's own, as no other site can send one
    }
    // The page is cross-origin isolated, so the game can share its memory with
    // workers and run ticks on threads (platform/web/tide.js), and editors
    // can still show it in a frame of theirs.
    char header[640];
    const int n = snprintf(header, sizeof header,
                           "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                           "Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Embedder-Policy: require-corp\r\n"
                           "Cross-Origin-Resource-Policy: cross-origin\r\n"
                           "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                           r.status, r.status == 200 ? "OK" : "Not Found", r.type, (unsigned long)r.size);
    set_blocking(c->s, true);
    if (send_all(c->s, header, (size_t)n)) send_all(c->s, r.body, r.size);
    close_connection(c);
}

// A POST's Content-Length: false where it has none, or more than a body may have
static bool content_length(const char *request, uint64_t *length)
{
    for (const char *line = strstr(request, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        const char *name = line + 2, *want = "content-length:";
        size_t i = 0;
        while (want[i] && tolower((unsigned char)name[i]) == want[i]) i++;
        if (want[i]) continue;
        const char *at = name + i;
        while (*at == ' ' || *at == '\t') at++;
        uint64_t n = 0;
        const char *digits = at;
        for (; *at >= '0' && *at <= '9' && n <= SERVE_BODY_MOST; at++) n = n * 10u + (uint64_t)(*at - '0');
        if (at == digits || n > SERVE_BODY_MOST) return false;
        *length = n;
        return true;
    }
    return false;
}

// Starts a POST's body of `length` bytes, with what came after its headers (`from`, to what's been read)
static bool body_begin(connection *c, const char *from, const uint64_t length)
{
    const uint32_t already = (uint32_t)(c->request + c->got - from);
    if (already > length) return false;
    c->body = malloc((size_t)length);
    if (!c->body) return false;
    memcpy(c->body, from, already);
    c->want = (uint32_t)length;
    c->have = already;
    return true;
}

// Takes a connection waiting to be accepted, closing the oldest one when
// there's no room.
static void accept_one(serve *s)
{
    const sock accepted = accept(s->listener, NULL, NULL);
    if (accepted == NO_SOCKET) return;
    connection *slot = NULL;
    for (int i = 0; i < CONNECTIONS; i++) {
        connection *c = &s->connections[i];
        if (c->s == NO_SOCKET) {
            slot = c;
            break;
        }
        if (!slot || c->opened < slot->opened) slot = c;
    }
    if (slot->s != NO_SOCKET) close_connection(slot);
    set_blocking(accepted, false);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    slot->s = accepted;
    slot->opened = ++s->opened;
    slot->got = 0;
}

void serve_poll(serve *s, const int ms, const serve_fn answer, void *user)
{
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(s->listener, &readable);
    sock highest = s->listener;
    for (int i = 0; i < CONNECTIONS; i++) {
        const sock c = s->connections[i].s;
        if (c == NO_SOCKET) continue;
        FD_SET(c, &readable);
        if (c > highest) highest = c;
    }
    struct timeval wait = {ms / 1000, (ms % 1000) * 1000};
    if (select((int)highest + 1, &readable, NULL, NULL, &wait) <= 0) return;
    for (int i = 0; i < CONNECTIONS; i++) {
        connection *c = &s->connections[i];
        if (c->s == NO_SOCKET || !FD_ISSET(c->s, &readable)) continue;
        if (c->body) { // The rest of a POST's body
            const uint32_t room = c->want - c->have;
            const int got = (int)recv(c->s, (char *)c->body + c->have, room > (1u << 20) ? (1 << 20) : (int)room, 0);
            if (got <= 0) {
                close_connection(c);
                continue;
            }
            c->have += (uint32_t)got;
            if (c->have == c->want) reply(c, answer, user);
            continue;
        }
        const int got = (int)recv(c->s, c->request + c->got, REQUEST_BYTES - 1u - c->got, 0);
        if (got <= 0) {
            close_connection(c);
            continue;
        }
        c->got += (uint32_t)got;
        c->request[c->got] = '\0';
        const char *end = strstr(c->request, "\r\n\r\n");
        if (!end) {
            if (c->got >= REQUEST_BYTES - 1u) close_connection(c); // Too long for us
            continue;
        }
        // A POST: its body, as long as it says, after its headers. The page's own alone, with its X-Tide header:
        // another site's isn't taken, nor its body held
        uint64_t length = 0;
        if (strncmp(c->request, "POST ", 5) == 0 && (!tide_header(c->request) || !content_length(c->request, &length))) {
            close_connection(c);
            continue;
        }
        if (!length) {
            reply(c, answer, user);
            continue;
        }
        if (!body_begin(c, end + 4, length)) close_connection(c);
        else if (c->have == c->want) reply(c, answer, user);
    }
    if (FD_ISSET(s->listener, &readable)) accept_one(s);
}
