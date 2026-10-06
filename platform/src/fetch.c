// Web.Fetch on desktop and Android: an HTTP/1.1 GET of our own (RFC 9112:
// the status line, headers, a body by its length or in chunks, redirects),
// over the sockets of rtc/net.c and, for https://, the system's TLS that the
// relay's connection uses (rtc/tls.c: SChannel, Secure Transport, OpenSSL, and
// Java's SSLEngine on Android), so games carry no code of anyone else's for
// it. The web has the page's fetch instead (tide.js).
//
// Each fetch runs on a thread of its own, since the frame can't wait for a
// network: the frame starts it (tide_fetch_begin) and asks each frame whether
// it's done (tide_fetch_poll), and the answer comes back on the frame's
// thread, as data. Nothing is timed: a server that never answers is a fetch
// that never ends, as a browser's fetch is.
//
// What's refused, with TIDE_IO_FAILED: a body past TIDE_FETCH_LIMIT (read no
// further, so a hostile server can't fill memory), headers past 64 KiB, more
// than 5 redirects, a status outside 200 to 299 (but 404, which is
// TIDE_IO_NOT_FOUND), a certificate the system doesn't trust, and anything
// else that goes wrong.

#ifndef _WIN32
#define _DEFAULT_SOURCE // nanosleep under strict C
#endif

#include "fetch.h"

#ifndef __wasm__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtc/rtc.h"
#include "tide/page.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

enum {
    HEADER_LIMIT = 65536,
    REDIRECTS = 5,
    RECEIVE = 16384,
};

typedef struct fetch {
    struct fetch *next;
    uint32_t ticket;
    char *url;
    bool done;      // The thread is finished with it
    bool forgotten; // Nobody will poll it: the thread frees it when it's done
    tide_io_error error;
    uint8_t *body;
    size_t size;
} fetch;

static struct {
    fetch *list;
    uint32_t next_ticket;
} fetches;

#ifdef _WIN32
static SRWLOCK fetch_lock = SRWLOCK_INIT;
static void lock(void) { AcquireSRWLockExclusive(&fetch_lock); }
static void unlock(void) { ReleaseSRWLockExclusive(&fetch_lock); }
static void nap(void) { Sleep(1); }
#else
static pthread_mutex_t fetch_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { pthread_mutex_lock(&fetch_lock); }
static void unlock(void) { pthread_mutex_unlock(&fetch_lock); }
static void nap(void)
{
    struct timespec t = {0, 1000000};
    nanosleep(&t, NULL);
}
#endif

// ---------------------------------------------------------------------------
// One request

typedef struct request {
    bool secure;
    char host[256];
    uint16_t port;
    char *target; // The path and query, as the request line says it
    rtc_socket socket;
    rtc_tls *tls;
    const fetch *of; // For `forgotten`
} request;

// http://host[:port][/path[?query]][#fragment]: false for anything else.
static bool parse_url(const char *url, request *q)
{
    free(q->target);
    q->target = NULL;
    q->secure = strncmp(url, "https://", 8) == 0;
    if (!q->secure && strncmp(url, "http://", 7) != 0) return false;
    const char *host = url + (q->secure ? 8 : 7);
    const char *end = host + strcspn(host, "/?#");
    const size_t host_size = (size_t)(end - host);
    if (!host_size || host_size >= sizeof q->host || memchr(host, '@', host_size) || host[0] == '[') return false;
    memcpy(q->host, host, host_size);
    q->host[host_size] = 0;
    q->port = q->secure ? 443 : 80;
    char *colon = strchr(q->host, ':');
    if (colon) {
        char *rest = NULL;
        const unsigned long port = strtoul(colon + 1, &rest, 10);
        if (!*(colon + 1) || *rest || !port || port > 65535) return false;
        q->port = (uint16_t)port;
        *colon = 0;
    }
    if (!q->host[0]) return false;
    const char *stop = end + strcspn(end, "#"); // The fragment stays here
    const size_t target_size = (size_t)(stop - end);
    q->target = tide_alloc(target_size + 2);
    if (!target_size || end[0] == '?') {
        q->target[0] = '/';
        memcpy(q->target + 1, end, target_size);
        q->target[target_size + 1] = 0;
    } else {
        memcpy(q->target, end, target_size);
        q->target[target_size] = 0;
    }
    return true;
}

// Where a redirect goes: `location` as it is when it's a URL, from the
// host's root when it starts with '/', and else beside the current target.
static char *resolve(const request *q, const char *location)
{
    if (strncmp(location, "http://", 7) == 0 || strncmp(location, "https://", 8) == 0) {
        char *copy = tide_alloc(strlen(location) + 1);
        strcpy(copy, location);
        return copy;
    }
    const char *scheme = q->secure ? "https" : "http";
    const bool default_port = q->port == (q->secure ? 443 : 80);
    char port[8] = "";
    if (!default_port) snprintf(port, sizeof port, ":%u", q->port);
    size_t base = 0; // How much of the current target the location goes after
    if (location[0] != '/') {
        const char *slash = strrchr(q->target, '/');
        const char *query = strchr(q->target, '?');
        if (slash && (!query || slash < query)) base = (size_t)(slash - q->target) + 1;
    }
    const size_t size = strlen(scheme) + 3 + strlen(q->host) + strlen(port) + base + strlen(location) + 1;
    char *out = tide_alloc(size);
    snprintf(out, size, "%s://%s%s%.*s%s", scheme, q->host, port, (int)base, q->target, location);
    return out;
}

static bool forgotten(const request *q)
{
    lock();
    const bool gone = q->of->forgotten;
    unlock();
    return gone;
}

static void close_request(request *q)
{
    if (q->tls) rtc_tls_free(q->tls);
    q->tls = NULL;
    if (q->socket != RTC_NO_SOCKET) rtc_socket_close(q->socket);
    q->socket = RTC_NO_SOCKET;
}

// Connects, with TLS over it for https://, and sends the request: false if
// it couldn't.
static bool open_request(request *q)
{
    rtc_addr to;
    if (!rtc_resolve(q->host, q->port, &to)) return false;
    q->socket = rtc_tcp_connect(to);
    if (q->socket == RTC_NO_SOCKET) return false;
    for (;;) {
        const int connected = rtc_tcp_connected(q->socket);
        if (connected < 0 || forgotten(q)) return false;
        if (connected > 0) break;
        nap();
    }
    if (q->secure) {
        q->tls = rtc_tls_new(q->host);
        if (!q->tls) return false; // This system has no TLS for us
        for (;;) {
            const int done = rtc_tls_handshake(q->tls, q->socket);
            if (done < 0 || forgotten(q)) return false;
            if (done > 0) break;
            nap();
        }
    }
    const bool default_port = q->port == (q->secure ? 443 : 80);
    char port[8] = "";
    if (!default_port) snprintf(port, sizeof port, ":%u", q->port);
    const size_t size = strlen(q->target) + strlen(q->host) + 128;
    char *text = tide_alloc(size);
    const int n = snprintf(text, size, "GET %s HTTP/1.1\r\nHost: %s%s\r\nUser-Agent: tide\r\nAccept: */*\r\nConnection: close\r\n\r\n",
                           q->target, q->host, port);
    bool ok = n > 0 && (size_t)n < size;
    for (size_t sent = 0; ok && sent < (size_t)n;) {
        const int moved = q->tls ? rtc_tls_send(q->tls, q->socket, text + sent, (size_t)n - sent)
                                 : rtc_tcp_send(q->socket, text + sent, (size_t)n - sent);
        if (moved < 0 || forgotten(q)) ok = false;
        else if (moved == 0) nap();
        else sent += (size_t)moved;
    }
    free(text);
    return ok;
}

// What came, growing as it does
typedef struct bytes {
    uint8_t *data;
    size_t size, capacity;
} bytes;

// More of the answer into `b`: 1 for some, 0 for the connection's end, -1 for
// anything wrong.
static int receive(request *q, bytes *b, const size_t at_most)
{
    if (b->capacity - b->size < RECEIVE) {
        size_t capacity = b->capacity ? b->capacity * 2u : 2u * RECEIVE;
        while (capacity - b->size < RECEIVE) capacity *= 2u;
        if (capacity > at_most + RECEIVE) capacity = at_most + RECEIVE;
        if (capacity <= b->size) return -1; // Past what's allowed
        b->data = tide_realloc(b->data, b->capacity, capacity);
        b->capacity = capacity;
    }
    for (;;) {
        const int got = q->tls ? rtc_tls_receive(q->tls, q->socket, b->data + b->size, b->capacity - b->size)
                               : rtc_tcp_receive(q->socket, b->data + b->size, b->capacity - b->size);
        if (got > 0) {
            b->size += (size_t)got;
            return 1;
        }
        if (got < 0) return 0;
        if (forgotten(q)) return -1;
        nap();
    }
}

// A header's value in the answer's head (`head`, NUL-terminated after the
// status line), by name, letters in any case: NULL if it isn't there. The
// value is cut at its line's end, in place.
static char *header(char *head, const char *name)
{
    const size_t n = strlen(name);
    for (char *line = strstr(head, "\r\n"); line; line = strstr(line, "\r\n")) {
        line += 2;
        if (!*line) break;
        size_t i = 0;
        for (; i < n && line[i] && (line[i] | 0x20) == (name[i] | 0x20); i++) {}
        if (i == n && line[n] == ':') {
            char *value = line + n + 1;
            while (*value == ' ' || *value == '\t') value++;
            char *end = strstr(value, "\r\n");
            if (end) *end = 0;
            return value;
        }
    }
    return NULL;
}

typedef struct answer {
    int status;
    bool chunked;
    bool has_length;
    uint64_t length;
    char *location;
} answer;

// One request and its answer's body (or where it redirects to, in
// `a->location`).
static tide_io_error exchange(request *q, answer *a, uint8_t **body, size_t *body_size)
{
    *a = (answer){0};
    bytes in = {0};
    tide_io_error result = TIDE_IO_FAILED;
    if (!open_request(q)) goto done;
    // The head, up to the empty line that ends it
    size_t head_end = 0, scanned = 0;
    while (!head_end) {
        for (size_t i = scanned; i + 4 <= in.size; i++) {
            if (memcmp(in.data + i, "\r\n\r\n", 4) == 0) { head_end = i + 4; break; }
        }
        if (head_end) break;
        scanned = in.size > 3 ? in.size - 3 : 0;
        if (in.size >= HEADER_LIMIT) goto done;
        if (receive(q, &in, HEADER_LIMIT) <= 0) goto done;
    }
    in.data[head_end - 2] = 0; // The head as text, its last line's end kept
    if (strncmp((const char *)in.data, "HTTP/1.", 7) != 0 || head_end < 15) goto done;
    a->status = atoi((const char *)in.data + 9);
    if (a->status < 100 || a->status > 999) goto done;
    char *head = (char *)in.data;
    const char *length = header(head, "Content-Length");
    const char *encoding = header(head, "Transfer-Encoding");
    const char *location = header(head, "Location");
    if (encoding) {
        // The last encoding has to be chunked, and none of them packs the body
        const char *last = strrchr(encoding, ',');
        last = last ? last + 1 : encoding;
        while (*last == ' ') last++;
        char kind[16];
        snprintf(kind, sizeof kind, "%s", last);
        for (char *c = kind; *c; c++) *c = (char)(*c | 0x20);
        if (strncmp(kind, "chunked", 7) != 0 || strchr(encoding, ',')) goto done;
        a->chunked = true;
    } else if (length) {
        char *rest = NULL;
        a->length = strtoull(length, &rest, 10);
        if (rest == length || (*rest && *rest != ' ')) goto done;
        a->has_length = true;
    }
    if (location) {
        a->location = tide_alloc(strlen(location) + 1);
        strcpy(a->location, location);
    }
    const bool redirect = a->status == 301 || a->status == 302 || a->status == 303 || a->status == 307 || a->status == 308;
    if (redirect && a->location) { result = TIDE_IO_NONE; goto done; } // The body, if any, isn't wanted
    if (a->status == 404) { result = TIDE_IO_NOT_FOUND; goto done; }
    if (a->status < 200 || a->status > 299) goto done;
    if (a->has_length && a->length > TIDE_FETCH_LIMIT) goto done;
    // The body: by its length, in chunks, or to the connection's end
    bytes out = {0};
    const uint8_t *rest = in.data + head_end;
    size_t rest_size = in.size - head_end;
    if (!a->chunked) {
        // As long as the length says, or to the connection's end, up to the limit
        const size_t want = a->has_length ? (size_t)a->length : (size_t)TIDE_FETCH_LIMIT + 1u;
        out.capacity = a->has_length ? (want < RECEIVE ? RECEIVE : want) : 2u * RECEIVE;
        if (out.capacity < rest_size) out.capacity = rest_size;
        out.data = tide_alloc(out.capacity);
        memcpy(out.data, rest, rest_size);
        out.size = rest_size;
        while (out.size < want) {
            const int more = receive(q, &out, want);
            if (more < 0) { free(out.data); goto done; }
            if (more == 0) break; // The end
        }
        if (out.size > want) out.size = want;
        if (a->has_length ? out.size < want : out.size > TIDE_FETCH_LIMIT) { free(out.data); goto done; }
    } else {
        // Each chunk: its size in hex on a line, its bytes, and a line's end;
        // a chunk of 0 ends them, and trailers follow to an empty line
        bytes stream = {0};
        stream.capacity = rest_size + RECEIVE;
        stream.data = tide_alloc(stream.capacity);
        memcpy(stream.data, rest, rest_size);
        stream.size = rest_size;
        size_t at = 0;
        bool ok = true, ended = false;
        while (ok && !ended) {
            // A whole line for the size
            const uint8_t *nl = memchr(stream.data + at, '\n', stream.size - at);
            if (!nl) {
                if (stream.size - at > 1024) { ok = false; break; } // No such line
                if (at) { memmove(stream.data, stream.data + at, stream.size - at); stream.size -= at; at = 0; }
                if (receive(q, &stream, TIDE_FETCH_LIMIT) <= 0) ok = false;
                continue;
            }
            char *rest_hex = NULL;
            stream.data[nl - stream.data] = 0; // The line as text, for the number
            const unsigned long long n = strtoull((const char *)stream.data + at, &rest_hex, 16);
            if (rest_hex == (char *)stream.data + at || n > TIDE_FETCH_LIMIT) { ok = false; break; }
            at = (size_t)(nl - stream.data) + 1;
            if (n == 0) { ended = true; break; }
            if (out.size + n > TIDE_FETCH_LIMIT) { ok = false; break; }
            // The chunk's bytes and the CRLF after them
            while (ok && stream.size - at < n + 2) {
                if (at) { memmove(stream.data, stream.data + at, stream.size - at); stream.size -= at; at = 0; }
                if (receive(q, &stream, TIDE_FETCH_LIMIT) <= 0) ok = false;
            }
            if (!ok) break;
            if (out.capacity - out.size < n) {
                size_t capacity = out.capacity ? out.capacity * 2u : RECEIVE;
                while (capacity - out.size < n) capacity *= 2u;
                out.data = tide_realloc(out.data, out.capacity, capacity);
                out.capacity = capacity;
            }
            memcpy(out.data + out.size, stream.data + at, (size_t)n);
            out.size += (size_t)n;
            at += (size_t)n + 2;
        }
        free(stream.data);
        if (!ok || !ended) { free(out.data); goto done; }
    }
    *body = out.data;
    *body_size = out.size;
    result = TIDE_IO_NONE;
done:
    close_request(q);
    free(in.data);
    return result;
}

// The whole fetch: requests, through redirects, to the body.
static tide_io_error get(fetch *f)
{
    request q = {.socket = RTC_NO_SOCKET, .of = f};
    char *url = f->url;
    tide_io_error result = TIDE_IO_FAILED;
    for (int hops = 0; hops <= REDIRECTS; hops++) {
        if (!parse_url(url, &q)) break;
        answer a;
        uint8_t *body = NULL;
        size_t size = 0;
        result = exchange(&q, &a, &body, &size);
        if (result == TIDE_IO_NONE && a.location) {
            char *next = resolve(&q, a.location);
            free(a.location);
            if (url != f->url) free(url);
            url = next;
            result = TIDE_IO_FAILED; // Unless the next answers
            continue;
        }
        free(a.location);
        if (result == TIDE_IO_NONE) {
            f->body = body;
            f->size = size;
        }
        break;
    }
    if (url != f->url) free(url);
    free(q.target);
    return result;
}

#ifdef _WIN32
static DWORD WINAPI fetch_thread(void *arg)
#else
static void *fetch_thread(void *arg)
#endif
{
    fetch *f = arg;
    const tide_io_error error = get(f);
    lock();
    f->error = error;
    f->done = true;
    const bool gone = f->forgotten;
    if (gone) {
        fetch **at = &fetches.list;
        while (*at != f) at = &(*at)->next;
        *at = f->next;
    }
    unlock();
    if (gone) {
        free(f->body);
        free(f->url);
        free(f);
    }
    return 0;
}

uint32_t tide_fetch_begin(const char *url)
{
    fetch *f = tide_alloc_zeroed(1, sizeof *f);
    f->url = tide_alloc(strlen(url) + 1);
    strcpy(f->url, url);
    lock();
    do { fetches.next_ticket++; } while (!fetches.next_ticket || fetches.next_ticket == TIDE_FETCH_INVALID_URL);
    f->ticket = fetches.next_ticket;
    f->next = fetches.list;
    fetches.list = f;
    unlock();
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, fetch_thread, f, 0, NULL);
    const bool started = thread != NULL;
    if (thread) CloseHandle(thread);
#else
    pthread_t thread;
    const bool started = pthread_create(&thread, NULL, fetch_thread, f) == 0;
    if (started) pthread_detach(thread);
#endif
    if (!started) { // No thread to fetch on: it's done, having failed
        lock();
        f->done = true;
        f->error = TIDE_IO_FAILED;
        unlock();
    }
    return f->ticket;
}

bool tide_fetch_poll(const uint32_t ticket, tide_io_error *error, uint8_t **data, uint32_t *size)
{
    lock();
    fetch **at = &fetches.list;
    while (*at && (*at)->ticket != ticket) at = &(*at)->next;
    fetch *f = *at;
    if (!f) {
        unlock();
        *error = TIDE_IO_FAILED;
        *data = NULL;
        *size = 0;
        return true;
    }
    if (!f->done) {
        unlock();
        return false;
    }
    *at = f->next;
    unlock();
    *error = f->error;
    *data = f->body;
    *size = (uint32_t)f->size;
    if (f->size > TIDE_FETCH_LIMIT) { // Never, but the limit is the wrapper's promise
        free(f->body);
        *error = TIDE_IO_FAILED;
        *data = NULL;
        *size = 0;
    }
    free(f->url);
    free(f);
    return true;
}

void tide_fetch_forget(const uint32_t ticket)
{
    lock();
    fetch **at = &fetches.list;
    while (*at && (*at)->ticket != ticket) at = &(*at)->next;
    fetch *f = *at;
    if (f && f->done) *at = f->next; // Its thread is gone: free it here
    else if (f) f->forgotten = true;  // Its thread frees it, and stops when it looks
    unlock();
    if (f && f->done) {
        free(f->body);
        free(f->url);
        free(f);
    }
}

#endif
