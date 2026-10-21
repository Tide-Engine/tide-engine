// Anonymous usage data: what's sent and why is docs/tools/telemetry.md, which
// lists every field. An event is built only from the fixed sets below, numbers
// put in ranges, and the install's random ID: never from a path, a name, code
// or an error's text. The site keeps them (docs/telemetry.js).

#include "telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtc.h"
#include "sys.h"
#include "release.h"

// Where events go, as JSON: {"events": [...]}.
#define ENDPOINT "https://tide-engine.dev/telemetry"
#define DOCS "https://tide-engine.dev/tools/telemetry"
// At most this many events wait to be sent: the oldest go first.
#define QUEUE_MAX 100
// A line of the queue is one event, which is never this long.
#define LINE_MAX 1024
// The longest a command's time can be, a day: a watched run lasts as long as
// its window.
#define MS_MAX 86400000

static telemetry_error noted_error;
static int noted_files = -1, noted_packages = -1;

void telemetry_fail(const telemetry_error error)
{
    noted_error = error;
}

void telemetry_sizes(const int files, const int packages)
{
    noted_files = files;
    noted_packages = packages;
}

// ---------------------------------------------------------------------------
// Settings: <root>/telemetry/settings, beside the channel tide follows

typedef struct settings {
    bool enabled;
    bool notice; // The one-time notice was shown
    char id[33]; // 128 random bits as hex; "" before the first event
} settings;

static char *folder_of(const char *root)
{
    return path_join(root, "telemetry");
}

static bool is_hex_id(const char *text, const size_t n)
{
    if (n != 32) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static settings read_settings(const char *root)
{
    settings s = {.enabled = true};
    char *dir = folder_of(root);
    char *path = path_join(dir, "settings");
    char *text = sys_read_file(path, NULL);
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        if (n && line[n - 1] == '\r') n--;
        if (n == 11 && strncmp(line, "enabled yes", 11) == 0) s.enabled = true;
        else if (n == 10 && strncmp(line, "enabled no", 10) == 0) s.enabled = false;
        else if (n == 12 && strncmp(line, "notice shown", 12) == 0) s.notice = true;
        else if (n > 3 && strncmp(line, "id ", 3) == 0 && is_hex_id(line + 3, n - 3)) {
            memcpy(s.id, line + 3, 32);
            s.id[32] = '\0';
        }
        line = end ? end + 1 : NULL;
    }
    free(text);
    free(path);
    free(dir);
    return s;
}

static bool write_settings(const char *root, const settings *s)
{
    char *dir = folder_of(root);
    char *path = path_join(dir, "settings");
    char text[256];
    snprintf(text, sizeof text, "# tide's usage data settings: " DOCS "\nenabled %s\n%s%s%s%s", s->enabled ? "yes" : "no",
             s->notice ? "notice shown\n" : "", s->id[0] ? "id " : "", s->id, s->id[0] ? "\n" : "");
    const bool ok = sys_mkdirs(dir) && sys_write_text(path, text);
    free(path);
    free(dir);
    return ok;
}

// A new install ID, from the system's cryptographic randomness (rtc_random, as
// signing keys are made). False if there was none to be had.
static bool new_id(char out[33])
{
    uint8_t bytes[16];
    if (!rtc_random(bytes, sizeof bytes)) return false;
    for (size_t i = 0; i < sizeof bytes; i++) snprintf(out + 2 * i, 3, "%02x", bytes[i]);
    return true;
}

// ---------------------------------------------------------------------------
// When nothing is sent

// An environment variable that turns something off: set, and not 0 (or false, off or no).
static bool says_yes(const char *value)
{
    return value && strcmp(value, "0") != 0 && strcmp(value, "false") != 0 && strcmp(value, "off") != 0
        && strcmp(value, "no") != 0;
}

static bool says_no(const char *value)
{
    return value && !says_yes(value);
}

const char *telemetry_off(const char *root, const char *version)
{
    if (says_no(sys_env("TIDE_TELEMETRY"))) return "TIDE_TELEMETRY is 0";
    // The convention: set to anything but nothing or 0
    const char *do_not_track = sys_env("DO_NOT_TRACK");
    if (do_not_track && strcmp(do_not_track, "0") != 0) return "DO_NOT_TRACK is set";
    // A development build never sends, as it never looks for updates. Tests
    // that point tide at a server of their own may say otherwise.
    const bool test = sys_env("TIDE_TELEMETRY_URL") && says_yes(sys_env("TIDE_TELEMETRY_TEST"));
    if (tide_version_dev(version) && !test) return "this is a development build (its version ends in -dev)";
    if (sys_hidden()) return "TIDE_HIDDEN is set";
    if (!read_settings(root).enabled) return "it was turned off with `tide telemetry off`";
    return NULL;
}

// ---------------------------------------------------------------------------
// An event

static const char *const commands[] = {"add", "build", "deploy", "editors", "eject", "fetch", "help", "ide", "login", "logout",
                                       "open", "publish", "run", "schedule", "shaders", "unity", "update", "upgrade", "version",
                                       NULL};

static bool is_help(const char *a)
{
    return strcmp(a, "help") == 0 || strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0;
}

// The command an event names, from the allow-list or "other"; NULL for a run
// that sends nothing: tide's own (cc, _editor-preview, the sender itself, the
// editors' update an upgrade starts) and the telemetry command.
static const char *command_of(const int argc, const char *const *argv)
{
    if (argc < 2 || is_help(argv[1])) return "help";
    const char *command = argv[1];
    if (command[0] == '_' || strcmp(command, "cc") == 0 || strcmp(command, "-cc1") == 0 || strcmp(command, "telemetry") == 0)
        return NULL;
    if (argc == 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0)) return "help";
    if (strcmp(command, "--version") == 0) return "version";
    if (strcmp(command, "editors") == 0) {
        for (int i = 2; i < argc; i++)
            if (strcmp(argv[i], "--update") == 0) return NULL;
    }
    for (int i = 0; commands[i]; i++)
        if (strcmp(command, commands[i]) == 0) return commands[i];
    return "other";
}

static const char *target_of(const char *command, const int argc, const char *const *argv)
{
    if (strcmp(command, "build") && strcmp(command, "run") && strcmp(command, "unity")) return "none";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--web") == 0) return "web";
        if (strcmp(argv[i], "--android") == 0) return "android";
    }
    return "native";
}

static const char *os_name(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}

static const char *arch_name(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "other";
#endif
}

// Whether a CI service runs tide: the variables the common ones set.
static bool in_ci(void)
{
    static const char *const names[] = {"CI", "GITHUB_ACTIONS", "GITLAB_CI", "BUILDKITE", "CIRCLECI", "TRAVIS", "JENKINS_URL",
                                        "TF_BUILD", "TEAMCITY_VERSION", "BITBUCKET_BUILD_NUMBER", "APPVEYOR", "CODEBUILD_BUILD_ID",
                                        "DRONE", "BUILD_ID", "CI_NAME", NULL};
    for (int i = 0; names[i]; i++) {
        const char *value = sys_env(names[i]);
        if (value && strcmp(value, "false") != 0 && strcmp(value, "0") != 0) return true;
    }
    return false;
}

static const char *files_range(const int n)
{
    return n <= 0 ? "0" : n == 1 ? "1" : n <= 5 ? "2-5" : n <= 20 ? "6-20" : n <= 100 ? "21-100" : "101+";
}

static const char *packages_range(const int n)
{
    return n <= 0 ? "0" : n == 1 ? "1" : n <= 5 ? "2-5" : "6+";
}

static const char *error_name(const telemetry_error error, const int code)
{
    switch (error) {
    case TELEMETRY_ERROR_SETUP: return "setup";
    case TELEMETRY_ERROR_PACKAGES: return "packages";
    case TELEMETRY_ERROR_COMPILE: return "compile";
    case TELEMETRY_ERROR_C: return "c";
    case TELEMETRY_ERROR_LINK: return "link";
    case TELEMETRY_ERROR_NETWORK: return "network";
    case TELEMETRY_ERROR_GAME: return "game";
    case TELEMETRY_ERROR_NONE: break;
    }
    return code == 2 ? "usage" : "other";
}

// A version as releases are numbered (1.2.3, 1.2.3-nightly.4), or "unknown".
static const char *version_text(const char *version)
{
    const size_t n = strlen(version);
    if (n == 0 || n > 40 || version[0] < '0' || version[0] > '9') return "unknown";
    for (size_t i = 0; i < n; i++) {
        const char c = version[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-')) return "unknown";
    }
    return version;
}

// The event, one line of JSON.
static void event_line(char *out, const size_t size, const char *id, const char *command, const char *target, const char *version,
                       const int code, int64_t ms)
{
    if (ms < 0) ms = 0;
    if (ms > MS_MAX) ms = MS_MAX;
    int n = snprintf(out, size,
                     "{\"id\":\"%s\",\"command\":\"%s\",\"target\":\"%s\",\"version\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\","
                     "\"ci\":%s,\"ms\":%lld,\"outcome\":\"%s\"",
                     id, command, target, version_text(version), os_name(), arch_name(), in_ci() ? "true" : "false", (long long)ms,
                     code == 0 ? "ok" : "failed");
    if (code != 0) n += snprintf(out + n, size - (size_t)n, ",\"error\":\"%s\"", error_name(noted_error, code));
    if (noted_files >= 0) {
        n += snprintf(out + n, size - (size_t)n, ",\"files\":\"%s\",\"packages\":\"%s\"", files_range(noted_files),
                      packages_range(noted_packages));
    }
    snprintf(out + n, size - (size_t)n, "}");
}

// ---------------------------------------------------------------------------
// The queue: <root>/telemetry/queue, an event a line

// Whether a line of the queue is an event as tide writes them: a JSON object
// of printable characters, on one line.
static bool queued_event(const char *line, const size_t n)
{
    if (n < 2 || n >= LINE_MAX || line[0] != '{' || line[n - 1] != '}') return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)line[i] < 0x20 || (unsigned char)line[i] > 0x7e) return false;
    return true;
}

typedef struct lines {
    const char *at[QUEUE_MAX];
    size_t len[QUEUE_MAX];
    int count;
    int first; // Ring: the newest QUEUE_MAX lines
} lines;

static void add_line(lines *l, const char *at, const size_t n)
{
    int slot = l->first;
    if (l->count < QUEUE_MAX) slot = l->count++;
    else l->first = (l->first + 1) % QUEUE_MAX; // The oldest goes
    l->at[slot] = at;
    l->len[slot] = n;
}

// The events of `text`, the newest QUEUE_MAX of them, oldest first.
static void read_lines(const char *text, lines *l)
{
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        if (n && line[n - 1] == '\r') n--;
        if (queued_event(line, n)) add_line(l, line, n);
        line = end ? end + 1 : NULL;
    }
}

static const char *line_at(const lines *l, const int i, size_t *n)
{
    const int slot = (l->first + i) % QUEUE_MAX;
    *n = l->len[slot];
    return l->at[slot];
}

// Keeps the newest QUEUE_MAX events of the queue.
static void cap_queue(const char *dir, const char *queue)
{
    size_t size = 0;
    char *text = sys_read_file(queue, &size);
    if (!text) return;
    int count = 0;
    for (size_t i = 0; i < size; i++) count += text[i] == '\n';
    if (count <= QUEUE_MAX && size <= (size_t)QUEUE_MAX * LINE_MAX) {
        free(text);
        return;
    }
    lines l = {0};
    read_lines(text, &l);
    char *kept = malloc((size_t)QUEUE_MAX * (LINE_MAX + 1) + 1);
    if (!kept) abort();
    size_t len = 0;
    for (int i = 0; i < l.count; i++) {
        size_t n;
        const char *line = line_at(&l, i, &n);
        memcpy(kept + len, line, n);
        len += n;
        kept[len++] = '\n';
    }
    char name[48];
    snprintf(name, sizeof name, "queue-%u", (unsigned)sys_pid());
    char *temporary = path_join(dir, name);
    if (!sys_write_file(temporary, kept, len) || !sys_rename(temporary, queue)) sys_remove(temporary);
    free(temporary);
    free(kept);
    free(text);
}

static void append_queue(const char *dir, const char *text, const size_t n)
{
    char *queue = path_join(dir, "queue");
    FILE *f = fopen(queue, "ab");
    if (f) {
        fwrite(text, 1, n, f);
        fclose(f);
        cap_queue(dir, queue);
    }
    free(queue);
}

// A sender that is still running, by the file it took the queue into.
typedef struct senders {
    const char *dir;
    bool running;
    char *stale[16]; // Files of senders that ended before they were done
    int stale_count;
} senders;

static void find_sender(void *user, const char *name, const bool is_dir)
{
    senders *s = user;
    if (is_dir) return;
    if (strncmp(name, "body-", 5) == 0) {
        // A request's body left by a sender that stopped partway (its events are in its sending- file)
        const unsigned long pid = strtoul(name + 5, NULL, 10);
        if (!pid || !sys_process_alive((uint32_t)pid)) {
            char *path = path_join(s->dir, name);
            sys_remove(path);
            free(path);
        }
        return;
    }
    if (strncmp(name, "sending-", 8) != 0) return;
    const unsigned long pid = strtoul(name + 8, NULL, 10);
    if (pid && sys_process_alive((uint32_t)pid)) s->running = true;
    else if (s->stale_count < 16) s->stale[s->stale_count++] = path_join(s->dir, name);
}

// Starts `tide _telemetry-send`, detached, unless a sender is at it already.
static void start_sender(const char *dir)
{
    senders s = {.dir = dir};
    sys_list(dir, find_sender, &s);
    for (int i = 0; i < s.stale_count; i++) free(s.stale[i]);
    if (s.running) return;
    char *bin = sys_exe_dir();
#ifdef _WIN32
    char *tide = path_join(bin, "tide.exe");
#else
    char *tide = path_join(bin, "tide");
#endif
    const char *const argv[] = {tide, "_telemetry-send", NULL};
    sys_start_detached(argv);
    free(tide);
    free(bin);
}

void tide_telemetry_record(const char *root, const int argc, const char *const *argv, const int code, const int64_t ms,
                           const char *version)
{
    const char *command = command_of(argc, argv);
    if (!command || telemetry_off(root, version)) return;
    settings s = read_settings(root);
    // The first run that would send says so, once, and sends nothing itself: whoever turns it off then has sent
    // nothing at all. Nothing is ever sent from an installation whose settings can't be kept.
    const bool made = !s.id[0];
    if (made && !new_id(s.id)) return;
    const bool first = !s.notice;
    s.notice = true;
    if ((made || first) && !write_settings(root, &s)) return;
    if (first) {
        fputs("tide: sends anonymous usage data (no code, paths or names); see " DOCS ", or turn it off with `tide telemetry off`\n",
              stderr);
        return;
    }
    char line[LINE_MAX];
    event_line(line, sizeof line - 1, s.id, command, target_of(command, argc, argv), version, code, ms);
    const size_t n = strlen(line);
    line[n] = '\n';
    char *dir = folder_of(root);
    append_queue(dir, line, n + 1);
    start_sender(dir);
    free(dir);
}

// ---------------------------------------------------------------------------
// The sender

typedef struct taken {
    char *text; // Every file's events, one after the other
    size_t len;
} taken;

static void take_file(taken *t, const char *path)
{
    size_t n = 0;
    char *text = sys_read_file(path, &n);
    if (text) {
        t->text = realloc(t->text, t->len + n + 2);
        if (!t->text) abort();
        memcpy(t->text + t->len, text, n);
        t->len += n;
        if (n && text[n - 1] != '\n') t->text[t->len++] = '\n';
        t->text[t->len] = '\0';
    }
    free(text);
    sys_remove(path);
}

// Sends what's queued once. 0 when nothing was, 1 once it's sent or the site
// refused it, -1 when it couldn't be sent: it's queued again then.
static int send_queued(const char *dir, const char *queue, const bool with_stale)
{
    char name[48];
    snprintf(name, sizeof name, "sending-%u", (unsigned)sys_pid());
    char *sending = path_join(dir, name);
    // The queue is this sender's once it has moved it. What a sender that
    // stopped partway took is sent too.
    senders others = {.dir = dir};
    if (with_stale) sys_list(dir, find_sender, &others);
    taken t = {0};
    if (sys_rename(queue, sending)) take_file(&t, sending);
    for (int i = 0; i < others.stale_count; i++) {
        take_file(&t, others.stale[i]);
        free(others.stale[i]);
    }
    lines l = {0};
    read_lines(t.text, &l);
    if (!l.count) {
        free(t.text);
        free(sending);
        return 0;
    }
    // What's being sent stays in this sender's file until it's done.
    char *body = malloc((size_t)QUEUE_MAX * (LINE_MAX + 1) + 32);
    char *events = malloc((size_t)QUEUE_MAX * (LINE_MAX + 1) + 1);
    if (!body || !events) abort();
    size_t len = (size_t)snprintf(body, 32, "{\"events\":[");
    size_t events_len = 0;
    for (int i = 0; i < l.count; i++) {
        size_t n;
        const char *line = line_at(&l, i, &n);
        if (i) body[len++] = ',';
        memcpy(body + len, line, n);
        len += n;
        memcpy(events + events_len, line, n);
        events_len += n;
        events[events_len++] = '\n';
    }
    len += (size_t)snprintf(body + len, 4, "]}");
    sys_write_file(sending, events, events_len);
    snprintf(name, sizeof name, "body-%u.json", (unsigned)sys_pid());
    char *body_path = path_join(dir, name);
    char *data = malloc(strlen(body_path) + 2);
    if (!data) abort();
    data[0] = '@';
    strcpy(data + 1, body_path);
    const bool written = sys_write_file(body_path, body, len);

    const char *custom = sys_env("TIDE_TELEMETRY_URL"); // For tests: a server of their own
    const char *url = custom ? custom : ENDPOINT;
    // -q ignores the user's curl settings; no redirects are followed. A short
    // timeout: nobody waits for this, but it shouldn't linger either.
    const char *argv[24] = {sys_tool("curl"), "-q", "-fsS", "--connect-timeout", "3", "-m", "5", "-H",
                            "Content-Type: application/json", "--data-binary", data};
    int a = 11;
    if (!custom) {
        argv[a++] = "--proto";
        argv[a++] = "=https";
    }
    argv[a++] = url;
    argv[a] = NULL;
    const int code = written ? sys_run(argv, NULL, true) : -1;
    sys_remove(body_path);
    // Sent, or refused by the site (22: an HTTP error), which sending again
    // wouldn't change: done. Otherwise (offline, a timeout) they wait for the
    // next command's sender, at the end of the queue, which keeps its newest.
    const bool done = code == 0 || code == 22;
    if (!done) append_queue(dir, events, events_len);
    sys_remove(sending);
    free(data);
    free(body_path);
    free(events);
    free(body);
    free(t.text);
    free(sending);
    return done ? 1 : -1;
}

int tide_telemetry_send(const char *root, const char *version)
{
    char *dir = folder_of(root);
    char *queue = path_join(dir, "queue");
    if (telemetry_off(root, version)) {
        // Turned off since these were queued: they're never sent.
        sys_remove(queue);
    } else {
        // A command that ends while this sends starts no sender of its own,
        // as this one is running, so what it queued is sent next: this
        // sender's file goes before it looks at the queue again, and a
        // command queues its event before it looks for senders.
        for (int round = 0; round < 8 && send_queued(dir, queue, round == 0) > 0; round++) {
            // Until there's nothing left, or it can't be sent
        }
    }
    free(queue);
    free(dir);
    return 0;
}

// ---------------------------------------------------------------------------
// tide telemetry on|off|status

int tide_telemetry(const char *root, const int count, const char *const *args, const char *version)
{
    const char *what = count == 1 ? args[0] : NULL;
    if (!what || (strcmp(what, "on") && strcmp(what, "off") && strcmp(what, "status"))) {
        fprintf(stderr, "tide: telemetry takes on, off or status, like `tide telemetry status`\n");
        return 2;
    }
    settings s = read_settings(root);
    char *dir = folder_of(root);
    if (strcmp(what, "status") != 0) {
        s.enabled = strcmp(what, "on") == 0;
        s.notice = true; // Whoever says so has seen what it is
        if (!write_settings(root, &s)) {
            fprintf(stderr, "tide: couldn't write %s/settings\n", dir);
            free(dir);
            return 1;
        }
        if (!s.enabled) {
            char *queue = path_join(dir, "queue");
            sys_remove(queue); // Nothing that waits is sent
            free(queue);
        }
        const char *off = telemetry_off(root, version);
        if (!s.enabled) printf("Usage data is off: tide sends nothing.\n");
        else if (off) printf("Usage data is on, but nothing is sent while %s.\n", off);
        else printf("Usage data is on; `tide telemetry status` says what is sent.\n");
        free(dir);
        return 0;
    }
    const char *off = telemetry_off(root, version);
    if (off) printf("Usage data: off, as %s\n", off);
    else printf("Usage data: on\n");
    const char *custom = sys_env("TIDE_TELEMETRY_URL");
    printf("\nEach command sends one anonymous event to %s, after it ends:\n"
           "  the command (from a fixed list), its target (native, web, android or none),\n"
           "  tide's version, the OS and CPU, whether a CI service runs it, how long it took,\n"
           "  whether it worked and, if not, the kind of failure (from a fixed list), and how\n"
           "  many .tide files and packages the game has, in ranges.\n"
           "Never paths, project or file names, code, error messages, user or machine names.\n\n",
           custom ? custom : ENDPOINT);
    printf("Install ID: %s\n", s.id[0] ? s.id : "none yet (made at random on the first run that sends)");
    printf("Settings:   %s\n", dir);
    printf("Details:    " DOCS "\n");
    printf("Turn it off with `tide telemetry off`, TIDE_TELEMETRY=0 or DO_NOT_TRACK=1.\n");
    free(dir);
    return 0;
}
