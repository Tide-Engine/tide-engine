// A run of several players with no window (see players.h).

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // fork, readlink and what sockets take under strict C
#endif

#include "players.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

static uint32_t count; // The run's players: 0 for a run that isn't one of these
static uint32_t own;   // Which of them this process is, from 1

uint32_t tide_platform_players(void)
{
    return count;
}

uint32_t tide_platform_player(void)
{
    return own;
}

char *tide_platform_players_file(const char *path)
{
    const size_t n = strlen(path);
    char *out = tide_alloc(n + 16);
    size_t dot = n; // Where the name's extension starts, if it has one
    for (size_t i = n; i > 0 && path[i - 1] != '/' && path[i - 1] != '\\'; i--) {
        if (path[i - 1] == '.') {
            dot = i - 1;
            break;
        }
    }
    if (count < 2) snprintf(out, n + 16, "%s", path);
    else snprintf(out, n + 16, "%.*s-%u%s", (int)dot, path, (unsigned)own, path + dot);
    return out;
}

// A player's room has six of the player's own character for its code: the
// letters a code is written in, then its digits.
static const char letters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
_Static_assert(sizeof letters - 1 == TIDE_PLATFORM_PLAYERS_MOST, "a character for every player a run can have");

void tide_platform_players_room(const uint32_t player, char *out, const size_t size)
{
    char code[TIDE_ROOM_CODE_LENGTH + 1] = "";
    if (player >= 1 && player <= count) memset(code, letters[player - 1], TIDE_ROOM_CODE_LENGTH);
    if (size) snprintf(out, size, "%s", code);
}

#if defined(__wasm__) || defined(__ANDROID__)

// Runs with no window are a desktop's (tide_platform_open_headless).
void tide_platform_players_configure(const tide_headless_desc *desc)
{
    (void)desc;
}

void tide_platform_players_start(void)
{
}

int tide_platform_players_step(const uint32_t frame)
{
    (void)frame;
    return TIDE_KEEP_RUNNING;
}

int tide_platform_players_finish(const int code)
{
    return code;
}

bool tide_platform_players_open(const uint16_t port, tide_transport *out)
{
    (void)port;
    (void)out;
    return false;
}

bool tide_platform_players_join(const char *code, tide_transport *out, tide_address *server)
{
    (void)code;
    (void)out;
    (void)server;
    return false;
}

bool tide_platform_players_connect(const char *address, const uint16_t default_port, tide_transport *out, tide_address *server)
{
    (void)address;
    (void)default_port;
    (void)out;
    (void)server;
    return false;
}

void tide_platform_players_code(char *out, const size_t size)
{
    if (size) out[0] = '\0';
}

bool tide_platform_players_failed(void)
{
    return false;
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock;
typedef HANDLE process;
#define NO_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
typedef int sock;
typedef pid_t process;
#define NO_SOCKET (-1)
#define close_socket close
#endif

// ---------------------------------------------------------------------------
// What a player says once a frame drew, and what it's told: a size, then
// records.
//
// A player says:
//     'O' port                          its match opened to the others, on this port beside its room
//     'C'                               ...and closed
//     'S' way target end size bytes     a datagram: to the match in player `target`'s room (TO_ROOM) or
//                                       on port `target` (TO_PORT), from the `end`th time this player
//                                       joined one; or from its match to player `target` (TO_CLIENT),
//                                       who joined it for the `end`th time
// ...and is told:
//     'D' role from end size bytes      a datagram from player `from`: for its match (SERVER), from the
//                                       `end`th time that player joined one, or for what it joined
//                                       (CLIENT) the `end`th time
//     'R' end                           what it joined the `end`th time is a room nobody has open

enum { SERVER, CLIENT };
enum { TO_ROOM, TO_PORT, TO_CLIENT };

#define HELLO 0x45444954u    // What a player starts with, before which one it is: "TIDE"
#define BLOB_MOST 0x4000000u // More than a frame says: 64 MiB

static tide_writer outbox = {.grows = true}; // What this player says of the frame running

// A player's ends on the network: where its match takes the others (SERVER),
// and where it reaches a match it joined (CLIENT), one of each at a time. A
// session closes the one it had after it has the next, so each is numbered,
// and only the last of its role is live.
typedef struct end {
    uint32_t role;
    uint32_t number;
    uint32_t way, target; // A client's: how its datagrams find its server...
    tide_address server;  // ...and the address the session has for it
} end;

static uint32_t live[2], made[2];
// The datagrams that came for each, not read yet: who from, which of that
// player's ends, a size, the bytes
static tide_writer waiting[2] = {{.grows = true}, {.grows = true}};
static uint32_t read_at[2];
static uint32_t joined; // The player whose room the client's end joined: 0 for a port's, or none
static bool refused;    // The room it joined isn't open...
static bool heard;      // ...but a room that answered it once was

static void end_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    const end *e = self;
    if (e->number != live[e->role] || !size || size > UINT16_MAX) return;
    if (e->role == SERVER && to.kind != TIDE_ADDRESS_ROOM) return;
    tide_write_u8(&outbox, 'S');
    tide_write_u8(&outbox, (uint8_t)(e->role == CLIENT ? e->way : TO_CLIENT));
    tide_write_u32(&outbox, e->role == CLIENT ? e->target : to.port);
    tide_write_u32(&outbox, e->role == CLIENT ? e->number : to.host);
    tide_write_u16(&outbox, (uint16_t)size);
    tide_write_bytes(&outbox, data, size);
}

static uint32_t end_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    const end *e = self;
    if (e->number != live[e->role]) return 0;
    tide_writer *w = &waiting[e->role];
    tide_reader r = {.data = w->data, .size = w->size, .at = read_at[e->role]};
    while (r.at < r.size) {
        const uint32_t player = tide_read_u32(&r);
        const uint32_t number = tide_read_u32(&r);
        const uint32_t size = tide_read_u16(&r);
        const uint8_t *bytes = tide_read_bytes(&r, size);
        if (r.failed || !bytes) break;
        if (size > capacity) continue; // Longer ones are dropped
        read_at[e->role] = r.at;
        memcpy(data, bytes, size);
        // A match knows who joined it by player and by which time that was: a
        // player who joins again is another address, as on a socket of its own
        *from = e->role == CLIENT ? e->server : (tide_address){TIDE_ADDRESS_ROOM, number, (uint16_t)player};
        return size;
    }
    w->size = 0;
    read_at[e->role] = 0;
    return 0;
}

static void end_close(void *self)
{
    end *e = self;
    if (e->number == live[e->role]) {
        live[e->role] = 0;
        waiting[e->role].size = 0;
        read_at[e->role] = 0;
        if (e->role == SERVER) {
            tide_write_u8(&outbox, 'C');
        } else {
            joined = 0;
            refused = heard = false;
        }
    }
    free(e);
}

static end *end_open(const uint32_t role, tide_transport *out)
{
    end *e = tide_alloc_zeroed(1, sizeof *e);
    e->role = role;
    e->number = live[role] = ++made[role];
    waiting[role].size = 0;
    read_at[role] = 0;
    *out = (tide_transport){.self = e, .send = end_send, .receive = end_receive, .close = end_close};
    return e;
}

bool tide_platform_players_open(const uint16_t port, tide_transport *out)
{
    if (!count) return false;
    end_open(SERVER, out);
    tide_write_u8(&outbox, 'O');
    tide_write_u16(&outbox, port);
    return true;
}

// The player whose room has `code`, as a player may type it (in either case,
// with spaces): 0 when it's none of this run's.
static uint32_t room_of(const char *code, uint32_t *packed)
{
    char first = '\0';
    int n = 0;
    for (const char *c = code; *c; c++) {
        if (*c == ' ') continue;
        const char upper = *c >= 'a' && *c <= 'z' ? (char)(*c - 'a' + 'A') : *c;
        if (!first) first = upper;
        if (upper != first) return 0;
        n++;
    }
    const char *at = first ? strchr(letters, first) : NULL;
    const char *in_codes = first ? strchr(TIDE_ROOM_CODE_LETTERS, first) : NULL;
    if (!at || !in_codes || n != TIDE_ROOM_CODE_LENGTH) return 0;
    uint32_t bits = 0; // As a room's code packs into an address (TIDE_ADDRESS_ROOM)
    for (int i = 0; i < n; i++) bits = bits << 5 | (uint32_t)(in_codes - TIDE_ROOM_CODE_LETTERS);
    *packed = bits | TIDE_ROOM_CODE_BIT;
    const uint32_t player = (uint32_t)(at - letters) + 1u;
    return player <= count ? player : 0;
}

bool tide_platform_players_join(const char *code, tide_transport *out, tide_address *server)
{
    uint32_t packed = 0;
    const uint32_t player = count ? room_of(code, &packed) : 0;
    if (!player) return false;
    end *e = end_open(CLIENT, out);
    e->way = TO_ROOM;
    e->target = player;
    e->server = *server = (tide_address){.kind = TIDE_ADDRESS_ROOM, .host = packed, .port = 0};
    joined = player;
    refused = heard = false;
    return true;
}

bool tide_platform_players_connect(const char *address, const uint16_t default_port, tide_transport *out, tide_address *server)
{
    if (!count) return false;
    tide_address to;
    if (!tide_address_parse(address, default_port, &to)) {
        // A name, or a name and a port: no name is looked up, as the port is all that counts here
        const char *colon = strrchr(address, ':');
        char *last = NULL;
        const unsigned long port = colon ? strtoul(colon + 1, &last, 10) : default_port;
        if (!address[0] || colon == address || (colon && (last == colon + 1 || *last)) || port > 65535ul) return false;
        to = (tide_address){.kind = TIDE_ADDRESS_IPV4, .host = 0x7F000001u, .port = (uint16_t)port};
    }
    if (!to.port) return false;
    end *e = end_open(CLIENT, out);
    e->way = TO_PORT;
    e->target = to.port;
    e->server = *server = to;
    joined = 0;
    refused = heard = false;
    return true;
}

void tide_platform_players_code(char *out, const size_t size)
{
    tide_platform_players_room(live[SERVER] ? own : live[CLIENT] ? joined : 0, out, size);
}

bool tide_platform_players_failed(void)
{
    return live[CLIENT] && refused && !heard;
}

// What this player is told, as a frame's end has it: the datagrams wait for
// its ends to read them.
static void deliver(const uint8_t *data, const uint32_t size)
{
    for (uint32_t role = 0; role < 2; role++) { // What wasn't read stays, ahead of what comes
        tide_writer *w = &waiting[role];
        if (read_at[role]) memmove(w->data, w->data + read_at[role], w->size - read_at[role]);
        w->size -= read_at[role];
        read_at[role] = 0;
    }
    tide_reader r = {.data = data, .size = size};
    while (r.at < r.size && !r.failed) {
        const uint8_t kind = tide_read_u8(&r);
        if (kind == 'R') {
            if (tide_read_u32(&r) == live[CLIENT]) refused = true;
            continue;
        }
        if (kind != 'D') break;
        const uint32_t role = tide_read_u8(&r);
        const uint32_t from = tide_read_u32(&r);
        const uint32_t number = tide_read_u32(&r);
        const uint32_t length = tide_read_u16(&r);
        const uint8_t *bytes = tide_read_bytes(&r, length);
        if (r.failed || !bytes || role > CLIENT) break;
        // For an end that's gone, or the one before this one: nobody's
        if (!live[role] || (role == CLIENT && number != live[CLIENT])) continue;
        if (role == CLIENT) heard = true;
        tide_write_u32(&waiting[role], from);
        tide_write_u32(&waiting[role], number);
        tide_write_u16(&waiting[role], (uint16_t)length);
        tide_write_bytes(&waiting[role], bytes, length);
    }
}

// ---------------------------------------------------------------------------
// Sockets: the first player listens on this machine alone, and the others
// connect to it. Everything on them waits: a frame's end is when it's said.

static bool sockets(void)
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

// Not for a process started from here: a copy that held another's connection
// open would keep its end from showing.
static void keep_from_copies(const sock s)
{
#ifdef _WIN32
    SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
#else
    fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
}

// What's written goes at once, and writing to a process that ended is an
// error to read, not a signal.
static void prepare(const sock s)
{
    const int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&on, sizeof on);
#endif
    keep_from_copies(s);
}

static bool interrupted(void)
{
#ifdef _WIN32
    return false;
#else
    return errno == EINTR;
#endif
}

static bool send_all(const sock s, const void *data, size_t size)
{
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    const char *at = data;
    while (size) {
        const int most = size < 1u << 20 ? (int)size : 1 << 20;
        const int n = (int)send(s, at, most, flags);
        if (n <= 0) {
            if (n < 0 && interrupted()) continue;
            return false;
        }
        at += n;
        size -= (size_t)n;
    }
    return true;
}

static bool read_all(const sock s, void *data, size_t size)
{
    char *at = data;
    while (size) {
        const int most = size < 1u << 20 ? (int)size : 1 << 20;
        const int n = (int)recv(s, at, most, 0);
        if (n <= 0) {
            if (n < 0 && interrupted()) continue;
            return false; // The other end closed, or its process ended
        }
        at += n;
        size -= (size_t)n;
    }
    return true;
}

static bool send_blob(const sock s, const uint8_t *data, const uint32_t size)
{
    const uint8_t header[4] = {(uint8_t)size, (uint8_t)(size >> 8), (uint8_t)(size >> 16), (uint8_t)(size >> 24)};
    return send_all(s, header, sizeof header) && send_all(s, data, size);
}

static bool read_blob(const sock s, tide_writer *into)
{
    uint8_t header[4];
    into->size = 0;
    if (!read_all(s, header, sizeof header)) return false;
    const uint32_t size = header[0] | (uint32_t)header[1] << 8 | (uint32_t)header[2] << 16 | (uint32_t)header[3] << 24;
    if (size > BLOB_MOST) return false;
    if (!size) return true;
    uint8_t *space = tide_write_space(into, size);
    return space && read_all(s, space, size);
}

// Whether `s` has something to read within a moment: a connection to accept.
static bool readable(const sock s)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    struct timeval moment = {.tv_sec = 0, .tv_usec = 50000};
    return select((int)s + 1, &set, NULL, NULL, &moment) > 0;
}

// ---------------------------------------------------------------------------
// The first player's side: the others' processes, and what goes between them

typedef struct seat {
    bool open;        // Its match is open to the others...
    uint16_t port;    // ...on this port, beside its room
    sock link;        // The first player's connection to it
    process copy;     // Its process, which the first started
    bool started;
    bool ended;       // ...and which ended,
    int code;         // with this exit code
    tide_writer said; // What it said of the frame that drew
    tide_writer gets; // ...and what it's told for the next
} seat;

static seat seats[TIDE_PLATFORM_PLAYERS_MOST + 1]; // By player, from 1: the first's own is its outbox and what waits for its ends
static sock listener = NO_SOCKET;
static bool gathered;          // Every other player connected
static sock hub = NO_SOCKET;   // Another player's connection to the first
static uint16_t hub_port;
static int run_argc;
static char **run_argv;

void tide_platform_players_configure(const tide_headless_desc *desc)
{
    count = desc->players <= TIDE_PLATFORM_PLAYERS_MOST ? desc->players : TIDE_PLATFORM_PLAYERS_MOST;
    own = !count ? 0u : desc->player >= 2u && desc->player <= count ? desc->player : 1u;
    hub_port = (uint16_t)desc->hub;
    run_argc = desc->argc;
    run_argv = desc->argv;
}

static bool program_path(char *path, const size_t size)
{
#ifdef _WIN32
    const DWORD n = GetModuleFileNameA(NULL, path, (DWORD)size);
    return n && n < size;
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)size;
    return _NSGetExecutablePath(path, &n) == 0;
#else
    const ssize_t n = readlink("/proc/self/exe", path, size - 1);
    if (n <= 0) return false;
    path[n] = '\0';
    return true;
#endif
}

#ifdef _WIN32
// An argument onto a command line, as the C runtime takes one apart.
static void quote(tide_writer *line, const char *arg)
{
    if (line->size) tide_write_u8(line, ' ');
    if (arg[0] && !strpbrk(arg, " \t\n\v\"")) {
        tide_write_bytes(line, arg, (uint32_t)strlen(arg));
        return;
    }
    tide_write_u8(line, '"');
    for (const char *p = arg;; p++) {
        int slashes = 0;
        for (; *p == '\\'; p++) slashes++;
        if (!*p) slashes *= 2; // Before the closing quote, each stands for itself only when doubled
        else if (*p == '"') slashes = slashes * 2 + 1;
        for (int i = 0; i < slashes; i++) tide_write_u8(line, '\\');
        if (!*p) break;
        tide_write_u8(line, (uint8_t)*p);
    }
    tide_write_u8(line, '"');
}
#endif

// Starts player `player`'s process: this program again, with what it was
// given and which player it is, and where the first listens.
static bool start_copy(const uint32_t player, const uint16_t port, process *out)
{
    char path[4096], number[16], listens[16];
    if (!program_path(path, sizeof path)) return false;
    snprintf(number, sizeof number, "%u", (unsigned)player);
    snprintf(listens, sizeof listens, "%u", (unsigned)port);
#ifdef _WIN32
    tide_writer line = {.grows = true};
    quote(&line, path);
    for (int i = 1; i < run_argc; i++) quote(&line, run_argv[i]);
    quote(&line, "--player");
    quote(&line, number);
    quote(&line, listens);
    tide_write_u8(&line, '\0');
    // What it prints goes where this process's does
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION started;
    const BOOL ok = CreateProcessA(NULL, (char *)line.data, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &started);
    free(line.data);
    if (!ok) return false;
    CloseHandle(started.hThread);
    *out = started.hProcess;
    return true;
#else
    char **args = tide_alloc_zeroed((size_t)run_argc + 5u, sizeof *args);
    int n = 0;
    args[n++] = path;
    for (int i = 1; i < run_argc; i++) args[n++] = run_argv[i];
    args[n++] = "--player";
    args[n++] = number;
    args[n++] = listens;
    const pid_t pid = fork();
    if (pid == 0) {
        execv(path, args);
        _exit(127);
    }
    free(args);
    if (pid < 0) return false;
    *out = pid;
    return true;
#endif
}

// Waits for player `s`'s process to end, for its exit code.
static void copy_wait(seat *s)
{
    if (!s->started || s->ended) return;
#ifdef _WIN32
    WaitForSingleObject(s->copy, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(s->copy, &code);
    CloseHandle(s->copy);
    s->code = (int)code;
#else
    int status = 0;
    while (waitpid(s->copy, &status, 0) < 0 && errno == EINTR) { }
    s->code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    s->ended = true;
}

// Whether it ended already.
static bool copy_ended(seat *s)
{
    if (!s->started || s->ended) return s->ended;
#ifdef _WIN32
    if (WaitForSingleObject(s->copy, 0) == WAIT_OBJECT_0) copy_wait(s);
#else
    int status = 0;
    if (waitpid(s->copy, &status, WNOHANG) == s->copy) {
        s->code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        s->ended = true;
    }
#endif
    return s->ended;
}

// A process's exit code as a run's: what a program ends with by itself, or 1
// for one the system stopped.
static int as_exit(const int code)
{
    return code > 0 && code < 256 ? code : 1;
}

static const char *exit_text(const int code, char *out, const size_t size)
{
    if (code >= 0 && code < 256) snprintf(out, size, "exit code %d", code);
    else snprintf(out, size, "exit code 0x%08X", (unsigned)code);
    return out;
}

// Nothing more goes between the players: each of the others ends at its
// frame's end, where it finds nobody.
static void hang_up(void)
{
    if (listener != NO_SOCKET) close_socket(listener);
    listener = NO_SOCKET;
    for (uint32_t p = 2; p <= count; p++) {
        if (seats[p].link != NO_SOCKET) close_socket(seats[p].link);
        seats[p].link = NO_SOCKET;
    }
}

void tide_platform_players_start(void)
{
    if (!count) return;
    if (!sockets()) {
        fprintf(stderr, "tide: --players: this machine has no sockets for the run's players to talk through\n");
        exit(1);
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (own != 1u) {
        local.sin_port = htons(hub_port);
        hub = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        const uint8_t hello[8] = {(uint8_t)HELLO, (uint8_t)(HELLO >> 8), (uint8_t)(HELLO >> 16), (uint8_t)(HELLO >> 24), (uint8_t)own};
        if (hub == NO_SOCKET || !hub_port || connect(hub, (const struct sockaddr *)&local, sizeof local) != 0 ||
            !send_all(hub, hello, sizeof hello)) {
            fprintf(stderr, "tide: player %u can't find the run's first player: --player is what a run of several players "
                            "starts its others with, and --players what a command line says\n", (unsigned)own);
            exit(2);
        }
        prepare(hub);
        return;
    }
    for (uint32_t p = 0; p <= count; p++) {
        seats[p].link = NO_SOCKET;
        seats[p].said.grows = seats[p].gets.grows = true;
    }
    if (count == 1u) {
        gathered = true;
        return;
    }
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    int size = sizeof local;
#else
    socklen_t size = sizeof local;
#endif
    if (listener == NO_SOCKET || bind(listener, (const struct sockaddr *)&local, sizeof local) != 0 ||
        listen(listener, (int)count) != 0 || getsockname(listener, (struct sockaddr *)&local, &size) != 0) {
        fprintf(stderr, "tide: --players: can't listen on this machine for the run's other players\n");
        exit(1);
    }
    keep_from_copies(listener);
    for (uint32_t p = 2; p <= count; p++) {
        if (!start_copy(p, ntohs(local.sin_port), &seats[p].copy)) {
            fprintf(stderr, "tide: --players: can't start the game again for player %u\n", (unsigned)p);
            exit(tide_platform_players_finish(1));
        }
        seats[p].started = true;
    }
}

// Waits for every other player to connect. The player whose process ended
// before it did, or 0.
static uint32_t gather(void)
{
    uint32_t left = count - 1u;
    while (left) {
        if (!readable(listener)) {
            for (uint32_t p = 2; p <= count; p++) {
                if (seats[p].link == NO_SOCKET && copy_ended(&seats[p])) return p;
            }
            continue;
        }
        const sock s = accept(listener, NULL, NULL);
        uint8_t hello[8] = {0};
        if (s == NO_SOCKET) continue;
        const bool said = read_all(s, hello, sizeof hello);
        const uint32_t word = said ? hello[0] | (uint32_t)hello[1] << 8 | (uint32_t)hello[2] << 16 | (uint32_t)hello[3] << 24 : 0u;
        const uint32_t player = hello[4];
        if (word != HELLO || player < 2u || player > count || seats[player].link != NO_SOCKET) {
            close_socket(s); // Not one of this run's
            continue;
        }
        prepare(s);
        seats[player].link = s;
        left--;
    }
    gathered = true;
    close_socket(listener);
    listener = NO_SOCKET;
    return 0;
}

// What the players said of the frame that drew, to whom it's for.
static void route(void)
{
    // Whose match is open is as the frame left it, before what was sent in it arrives
    for (uint32_t p = 1; p <= count; p++) {
        const tide_writer *said = p == 1u ? &outbox : &seats[p].said;
        tide_reader r = {.data = said->data, .size = said->size};
        while (r.at < r.size && !r.failed) {
            const uint8_t kind = tide_read_u8(&r);
            if (kind == 'O') {
                seats[p].open = true;
                seats[p].port = tide_read_u16(&r);
            } else if (kind == 'C') {
                seats[p].open = false;
            } else if (kind == 'S') {
                tide_read_bytes(&r, 9);
                tide_read_bytes(&r, tide_read_u16(&r));
            } else {
                break;
            }
        }
    }
    for (uint32_t p = 1; p <= count; p++) {
        const tide_writer *said = p == 1u ? &outbox : &seats[p].said;
        tide_reader r = {.data = said->data, .size = said->size};
        while (r.at < r.size && !r.failed) {
            const uint8_t kind = tide_read_u8(&r);
            if (kind == 'O') {
                tide_read_u16(&r);
                continue;
            }
            if (kind == 'C') continue;
            if (kind != 'S') break;
            const uint32_t way = tide_read_u8(&r);
            const uint32_t target = tide_read_u32(&r);
            const uint32_t number = tide_read_u32(&r);
            const uint32_t size = tide_read_u16(&r);
            const uint8_t *bytes = tide_read_bytes(&r, size);
            if (r.failed || !bytes) break;
            uint32_t to = 0;
            if (way == TO_ROOM) {
                if (target >= 1u && target <= count && seats[target].open) to = target;
                if (!to) { // As the relay says of a room that isn't one
                    tide_write_u8(&seats[p].gets, 'R');
                    tide_write_u32(&seats[p].gets, number);
                }
            } else if (way == TO_PORT) {
                for (uint32_t q = 1; q <= count && !to; q++) {
                    if (seats[q].open && seats[q].port == target) to = q;
                }
            } else if (target >= 1u && target <= count) {
                to = target;
            }
            if (!to) continue;
            tide_writer *gets = &seats[to].gets;
            tide_write_u8(gets, 'D');
            tide_write_u8(gets, way == TO_CLIENT ? CLIENT : SERVER);
            tide_write_u32(gets, p);
            tide_write_u32(gets, number);
            tide_write_u16(gets, (uint16_t)size);
            tide_write_bytes(gets, bytes, size);
        }
    }
}

// Player `player`'s process ended before the run did: the run ends with it.
static int gone(const uint32_t player, const uint32_t frame)
{
    char when[48] = "before the run's first frame", how[32];
    if (frame) snprintf(when, sizeof when, "at frame %u", (unsigned)frame);
    hang_up();
    copy_wait(&seats[player]);
    fprintf(stderr, "tide: player %u's game ended %s, with %s: the run ends there\n", (unsigned)player, when,
            exit_text(seats[player].code, how, sizeof how));
    return as_exit(seats[player].code);
}

int tide_platform_players_step(const uint32_t frame)
{
    if (!count) return TIDE_KEEP_RUNNING;
    if (own != 1u) {
        static tide_writer told = {.grows = true};
        // Nobody there: the first ended the run, and said why
        if (!send_blob(hub, outbox.data, outbox.size) || !read_blob(hub, &told)) return 1;
        outbox.size = 0;
        deliver(told.data, told.size);
        return TIDE_KEEP_RUNNING;
    }
    uint32_t lost = gathered ? 0u : gather();
    if (lost) return gone(lost, 0);
    for (uint32_t p = 2; p <= count && !lost; p++) {
        if (!read_blob(seats[p].link, &seats[p].said)) lost = p;
    }
    if (lost) return gone(lost, frame);
    route();
    outbox.size = 0;
    for (uint32_t p = 2; p <= count && !lost; p++) {
        if (!send_blob(seats[p].link, seats[p].gets.data, seats[p].gets.size)) lost = p;
        seats[p].gets.size = 0;
    }
    if (lost) return gone(lost, frame);
    deliver(seats[1].gets.data, seats[1].gets.size);
    seats[1].gets.size = 0;
    return TIDE_KEEP_RUNNING;
}

int tide_platform_players_finish(int code)
{
    if (!count) return code;
    if (own != 1u) {
        if (hub != NO_SOCKET) close_socket(hub);
        hub = NO_SOCKET;
        return code;
    }
    // A run of one frame heard from nobody yet: the others are let in before
    // the door closes, or they'd take the run for one that isn't there
    if (!gathered && code == 0 && listener != NO_SOCKET) gather();
    hang_up();
    for (uint32_t p = 2; p <= count; p++) {
        char how[32];
        copy_wait(&seats[p]);
        if (!seats[p].started || seats[p].code == 0 || code != 0) continue;
        fprintf(stderr, "tide: player %u's game ended with %s\n", (unsigned)p, exit_text(seats[p].code, how, sizeof how));
        code = as_exit(seats[p].code);
    }
    return code;
}

#endif
