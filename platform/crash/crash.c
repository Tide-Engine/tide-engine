// tide-crash: what a player sees after a game crashed. The game's crash
// handler (platform/src/log.c) starts it from beside the game's program, with
// the folder the crash left and the game's title:
//
//   tide-crash <crash folder> <title>
//
// It's a program of its own so it starts at once however big the game is,
// and with nothing of the broken process. It writes system.txt into the
// folder and zips the report beside it (Crash_<time>.zip), with what would
// say who the player is masked first (scrub.h): the folder after Users/ or
// home/ in paths, the user's and the machine's names, IP and email
// addresses. The files in the folder stay as they were; the zip is what
// leaves the machine.
//
// A game linked to a hosting project with crash reports on has crash.json
// beside it (tide build writes it): {"origin", "reports" (the project's
// report key), "build" (the program's SHA-256)}. tide-crash then sends the
// zip to <origin>/api/reports at once, without asking, while its window
// (ui.h) says so. The window shows what the report says either way (the
// crash, and the end of the log), for a player who'd like to see what led
// to it, and its button shows the zip in the file manager, to send by hand.
//
//   tide-crash --zip <crash folder>
//
// zips with no window and prints the zip's path. Where nobody watches
// (TIDE_HIDDEN), every run is that: nothing is sent from a hidden run.
//
//   tide-crash --send <crash folder>
//
// zips and sends with no window, and prints the zip's path and then "sent"
// or "not sent: <why>": the test of sending, and for a game's makers.
#if !defined(_WIN32)
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _DEFAULT_SOURCE
#endif
#endif

#include "crash.h"
#include "fetch.h" // The engine's HTTP, over the system's TLS (platform/src/fetch.c)
#include "scrub.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Files by UTF-8 paths

FILE *crash_open(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wide[2048], wide_mode[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 2048)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8)) return NULL;
    return _wfopen(wide, wide_mode);
#else
    return fopen(path, mode);
#endif
}

// ---------------------------------------------------------------------------
// A zip of stored files: no compression, which a report of a few files that
// sends once doesn't need, and nothing to go wrong

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256u; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1u ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_add(uint32_t crc, const uint8_t *bytes, const size_t size)
{
    crc = ~crc;
    for (size_t i = 0; i < size; i++) crc = crc_table[(crc ^ bytes[i]) & 255u] ^ (crc >> 8);
    return ~crc;
}

static void put16(uint8_t *at, const uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *at, const uint32_t value)
{
    put16(at, value & 0xffffu);
    put16(at + 2, value >> 16);
}

// A report's file, read into memory to be masked and zipped
typedef struct report_file {
    const char *name;
    uint8_t *data;
    size_t size;
} report_file;

enum {
    REPORT_FILES = 4,
    LOG_MOST = 4 << 20, // The end of Player.log a report has, which can be 100 MB
};

typedef struct report {
    report_file files[REPORT_FILES]; // crash.txt, system.txt, Player.log, crash.dmp: data NULL where there's none
    uint8_t *zip;
    size_t zip_size;
} report;

static void report_free(report *r)
{
    for (int i = 0; i < REPORT_FILES; i++) free(r->files[i].data);
    free(r->zip);
    memset(r, 0, sizeof *r);
}

// A file's bytes, or its last `most` from the start of a line; NULL if it
// can't be read
static uint8_t *read_file(const char *path, const size_t most, size_t *size)
{
    FILE *in = crash_open(path, "rb");
    if (!in) return NULL;
    long length = -1;
    if (fseek(in, 0, SEEK_END) == 0) length = ftell(in);
    if (length < 0) {
        fclose(in);
        return NULL;
    }
    const size_t from = (size_t)length > most ? (size_t)length - most : 0;
    const size_t count = (size_t)length - from;
    uint8_t *data = malloc(count + 1u);
    const bool ok = data && fseek(in, (long)from, SEEK_SET) == 0 && fread(data, 1, count, in) == count;
    fclose(in);
    if (!ok) {
        free(data);
        return NULL;
    }
    size_t start = 0;
    if (from) { // From the next line's start, not part of one
        while (start < count && data[start] != '\n') start++;
        if (start < count) start++;
        memmove(data, data + start, count - start);
    }
    *size = count - start;
    data[*size] = '\0';
    return data;
}

// The zip of a report's files, stored (no compression, which a report of a
// few files sent once doesn't need, and nothing to go wrong), into r->zip
static bool zip_report(report *r)
{
    size_t total = 22;
    for (int i = 0; i < REPORT_FILES; i++) {
        if (r->files[i].data) total += 30u + 46u + 2u * strlen(r->files[i].name) + r->files[i].size;
    }
    if (total > 0xffffffffu) return false;
    uint8_t *zip = calloc(total, 1);
    if (!zip) return false;
    crc_init();
    // MS-DOS time and date of now, for every file: the report's, not the files'
    const time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    const uint32_t dos_time = local ? (uint32_t)(local->tm_hour << 11 | local->tm_min << 5 | local->tm_sec / 2) : 0u;
    const uint32_t dos_date = local ? (uint32_t)((local->tm_year - 80) << 9 | (local->tm_mon + 1) << 5 | local->tm_mday) : 0u;
    uint32_t crcs[REPORT_FILES] = {0}, offsets[REPORT_FILES] = {0};
    size_t at = 0;
    int count = 0;
    for (int i = 0; i < REPORT_FILES; i++) {
        const report_file *f = &r->files[i];
        if (!f->data) continue;
        count++;
        const uint32_t name_size = (uint32_t)strlen(f->name);
        crcs[i] = crc_add(0, f->data, f->size);
        offsets[i] = (uint32_t)at;
        uint8_t *header = zip + at;
        put32(header, 0x04034b50u);
        put16(header + 4, 10); // Version needed: 1.0
        put16(header + 10, dos_time);
        put16(header + 12, dos_date);
        put32(header + 14, crcs[i]);
        put32(header + 18, (uint32_t)f->size);
        put32(header + 22, (uint32_t)f->size);
        put16(header + 26, name_size);
        memcpy(header + 30, f->name, name_size);
        if (f->size) memcpy(header + 30 + name_size, f->data, f->size);
        at += 30u + name_size + f->size;
    }
    const size_t directory = at;
    for (int i = 0; i < REPORT_FILES; i++) {
        const report_file *f = &r->files[i];
        if (!f->data) continue;
        const uint32_t name_size = (uint32_t)strlen(f->name);
        uint8_t *header = zip + at;
        put32(header, 0x02014b50u);
        put16(header + 4, 10);
        put16(header + 6, 10);
        put16(header + 12, dos_time);
        put16(header + 14, dos_date);
        put32(header + 16, crcs[i]);
        put32(header + 20, (uint32_t)f->size);
        put32(header + 24, (uint32_t)f->size);
        put16(header + 28, name_size);
        put32(header + 42, offsets[i]);
        memcpy(header + 46, f->name, name_size);
        at += 46u + name_size;
    }
    uint8_t *end = zip + at;
    put32(end, 0x06054b50u);
    put16(end + 8, (uint32_t)count);
    put16(end + 10, (uint32_t)count);
    put32(end + 12, (uint32_t)(at - directory));
    put32(end + 16, (uint32_t)directory);
    r->zip = zip;
    r->zip_size = at + 22u;
    return count > 0;
}

// ---------------------------------------------------------------------------
// system.txt: what the machine is, which the log doesn't say (it has the GPU)

static void write_system(const char *folder)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/system.txt", folder);
    FILE *out = crash_open(path, "w");
    if (!out) return;
#ifdef _WIN32
    typedef LONG(WINAPI * version_fn)(OSVERSIONINFOW *);
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof version};
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const FARPROC found = ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    version_fn get_version = NULL;
    if (found) memcpy(&get_version, &found, sizeof get_version);
    if (get_version && get_version(&version) == 0) {
        fprintf(out, "system: Windows %lu.%lu build %lu\n", version.dwMajorVersion, version.dwMinorVersion,
                version.dwBuildNumber);
    }
    wchar_t cpu[256];
    DWORD size = sizeof cpu;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, NULL, cpu, &size) == ERROR_SUCCESS) {
        char name[512];
        if (WideCharToMultiByte(CP_UTF8, 0, cpu, -1, name, sizeof name, NULL, NULL)) {
            size_t length = strlen(name);
            while (length && name[length - 1] == ' ') name[--length] = '\0'; // Windows pads it
            fprintf(out, "processor: %s\n", name);
        }
    }
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    fprintf(out, "threads: %lu\n", info.dwNumberOfProcessors);
    MEMORYSTATUSEX memory = {.dwLength = sizeof memory};
    if (GlobalMemoryStatusEx(&memory)) {
        fprintf(out, "memory: %llu MB, %llu MB free\n", (unsigned long long)(memory.ullTotalPhys >> 20),
                (unsigned long long)(memory.ullAvailPhys >> 20));
    }
#else
    struct utsname name;
    if (uname(&name) == 0) fprintf(out, "system: %s %s %s %s\n", name.sysname, name.release, name.version, name.machine);
#if defined(__APPLE__)
    char cpu[256];
    size_t size = sizeof cpu;
    if (sysctlbyname("machdep.cpu.brand_string", cpu, &size, NULL, 0) == 0) fprintf(out, "processor: %s\n", cpu);
    uint64_t memory = 0;
    size = sizeof memory;
    if (sysctlbyname("hw.memsize", &memory, &size, NULL, 0) == 0) {
        fprintf(out, "memory: %llu MB\n", (unsigned long long)(memory >> 20));
    }
#else
    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo) {
        char line[512];
        while (fgets(line, sizeof line, cpuinfo)) {
            if (strncmp(line, "model name", 10) != 0) continue;
            const char *colon = strchr(line, ':');
            if (colon) fprintf(out, "processor:%s", colon + 1);
            break;
        }
        fclose(cpuinfo);
    }
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0) fprintf(out, "memory: %llu MB\n", (unsigned long long)pages * (unsigned long long)page >> 20);
    // The desktop the window would be on
    const char *session = getenv("XDG_SESSION_TYPE"), *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (session || desktop) fprintf(out, "desktop: %s %s\n", desktop ? desktop : "", session ? session : "");
#endif
    const long threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (threads > 0) fprintf(out, "threads: %ld\n", threads);
#endif
    fclose(out);
}

// ---------------------------------------------------------------------------
// The file manager, at the zip

static void reveal(const char *zip)
{
#ifdef _WIN32
    wchar_t wide[2048], command[2200];
    if (!MultiByteToWideChar(CP_UTF8, 0, zip, -1, wide, 2048)) return;
    for (wchar_t *c = wide; *c; c++) if (*c == L'/') *c = L'\\';
    _snwprintf(command, 2200, L"explorer.exe /select,\"%ls\"", wide);
    STARTUPINFOW startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#else
    char folder[2048];
    snprintf(folder, sizeof folder, "%s", zip);
    char *slash = strrchr(folder, '/');
    if (slash) *slash = '\0';
    const pid_t child = fork();
    if (child == 0) {
#if defined(__APPLE__)
        execl("/usr/bin/open", "open", "-R", zip, (char *)NULL);
#else
        execlp("xdg-open", "xdg-open", folder, (char *)NULL); // Linux's file managers select nothing they're opened at
#endif
        _exit(127);
    }
    if (child > 0) waitpid(child, NULL, 0);
#endif
}

// ---------------------------------------------------------------------------

static bool hidden(void)
{
    const char *value = getenv("TIDE_HIDDEN");
    return value && value[0] && strcmp(value, "0") != 0;
}

// Reads the folder's files (with system.txt written first), masks them and
// zips them beside it, at `zip`: the folder's path with .zip
static bool prepare(const char *folder, report *r, char *zip, const size_t size)
{
    char trimmed[2048];
    snprintf(trimmed, sizeof trimmed, "%s", folder);
    size_t length = strlen(trimmed);
    while (length > 1 && (trimmed[length - 1] == '/' || trimmed[length - 1] == '\\')) trimmed[--length] = '\0';
    const int fits = snprintf(zip, size, "%s.zip", trimmed);
    if (fits <= 0 || (size_t)fits >= size) return false;
    write_system(trimmed);
    static const char *const names[REPORT_FILES] = {"crash.txt", "system.txt", "Player.log", "crash.dmp"};
    scrub_names here;
    scrub_names_here(&here);
    for (int i = 0; i < REPORT_FILES; i++) {
        char path[2100];
        snprintf(path, sizeof path, "%s/%s", trimmed, names[i]);
        report_file *f = &r->files[i];
        f->name = names[i];
        f->data = read_file(path, i == 2 ? (size_t)LOG_MOST : (size_t)-1 / 2, &f->size); // A crash may have left less
        if (!f->data) continue;
        if (i == 3) scrub_dump(f->data, f->size, &here);
        else scrub_text((char *)f->data, f->size, &here);
    }
    if (!zip_report(r)) return false;
    FILE *out = crash_open(zip, "wb");
    if (!out) return false;
    const bool wrote = fwrite(r->zip, 1, r->zip_size, out) == r->zip_size;
    return fclose(out) == 0 && wrote;
}

// What the window shows of the report: crash.txt, and the log's last lines
static char *preview(const report *r)
{
    enum { CRASH_MOST = 16384, LOG_LINES = 80 };
    const report_file *crash = &r->files[0], *log = &r->files[2];
    const size_t crash_size = crash->data ? (crash->size < CRASH_MOST ? crash->size : CRASH_MOST) : 0;
    size_t from = log->data ? log->size : 0;
    for (int lines = 0; from > 0; from--) {
        if (log->data[from - 1] == '\n' && from < log->size && ++lines > LOG_LINES) break;
    }
    const size_t log_size = log->data ? log->size - from : 0;
    static const char rule[] = "\n--- The end of the game's log (Player.log) ---\n\n";
    char *text = malloc(crash_size + sizeof rule + log_size + 64u);
    if (!text) return NULL;
    size_t at = 0;
    if (crash_size) {
        memcpy(text, crash->data, crash_size);
        at = crash_size;
    } else {
        at = (size_t)sprintf(text, "The crash left no crash.txt.\n");
    }
    if (log_size) {
        memcpy(text + at, rule, sizeof rule - 1u);
        at += sizeof rule - 1u;
        memcpy(text + at, log->data + from, log_size);
        at += log_size;
    }
    text[at] = '\0';
    for (size_t i = 0; i < at; i++) if (text[i] == '\0') text[i] = ' '; // A NUL in a log ends nothing here
    return text;
}

// ---------------------------------------------------------------------------
// Sending: crash.json beside this program says where

typedef struct report_link {
    char origin[256];
    char key[65];
    char build[65];
} report_link;

// The folder this program is in, with a slash
static bool own_folder(char *out, const size_t size)
{
#ifdef _WIN32
    wchar_t wide[2048];
    const DWORD length = GetModuleFileNameW(NULL, wide, 2048);
    if (!length || length >= 2048) return false;
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)size, NULL, NULL)) return false;
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)size;
    if (_NSGetExecutablePath(out, &length) != 0) return false;
#else
    const ssize_t length = readlink("/proc/self/exe", out, size - 1);
    if (length <= 0) return false;
    out[length] = '\0';
#endif
    char *slash = strrchr(out, '/');
    char *back = strrchr(out, '\\');
    if (back && (!slash || back > slash)) slash = back;
    if (!slash) return false;
    slash[1] = '\0';
    return true;
}

// A string field's value in crash.json's small object: letters, digits and
// what an origin has, nothing to unescape
static bool field(const char *json, const char *name, char *out, const size_t size)
{
    char quoted[64];
    snprintf(quoted, sizeof quoted, "\"%s\"", name);
    const char *at = strstr(json, quoted);
    if (!at) return false;
    at += strlen(quoted);
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') at++;
    if (*at++ != ':') return false;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') at++;
    if (*at++ != '"') return false;
    size_t n = 0;
    while (at[n] && at[n] != '"' && at[n] != '\\' && (unsigned char)at[n] >= 0x20) n++;
    if (at[n] != '"' || n >= size) return false;
    memcpy(out, at, n);
    out[n] = '\0';
    return true;
}

static bool read_link(report_link *l)
{
    char path[2100];
    if (!own_folder(path, 2048)) return false;
    strcat(path, "crash.json");
    size_t size = 0;
    char *text = (char *)read_file(path, 4096, &size);
    if (!text) return false;
    memset(l, 0, sizeof *l);
    const bool ok = field(text, "origin", l->origin, sizeof l->origin) && field(text, "reports", l->key, sizeof l->key);
    if (ok && !field(text, "build", l->build, sizeof l->build)) l->build[0] = '\0';
    free(text);
    const bool local = strncmp(l->origin, "http://localhost:", 17) == 0 || strncmp(l->origin, "http://127.0.0.1:", 17) == 0;
    const size_t key = strlen(l->key);
    return ok && (local || strncmp(l->origin, "https://", 8) == 0) && key >= 16
           && strspn(l->key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == key
           && strspn(l->build, "0123456789abcdef") == strlen(l->build);
}

typedef struct sending {
    uint32_t ticket; // 0 once it's done, or for none
    bool sent;
    char why[128]; // Why it wasn't
} sending;

static void send_report(sending *s, const report_link *l, const report *r)
{
    char url[320], headers[320];
    snprintf(url, sizeof url, "%s/api/reports", l->origin);
    snprintf(headers, sizeof headers, "X-Tide-Report-Key: %s\r\n%s%s%sContent-Type: application/zip\r\n", l->key,
             l->build[0] ? "X-Tide-Build: " : "", l->build, l->build[0] ? "\r\n" : "");
    s->ticket = tide_request_begin("POST", url, headers, r->zip, (uint32_t)r->zip_size, 30.0f);
    if (s->ticket == TIDE_FETCH_INVALID_URL || s->ticket == TIDE_REQUEST_INVALID) {
        s->ticket = 0;
        snprintf(s->why, sizeof s->why, "the address in crash.json is invalid");
    }
}

// Whether the sending's done, saying how in `s`
static bool sent_yet(sending *s)
{
    if (!s->ticket) return true;
    tide_io_error error;
    int32_t status;
    uint8_t *data = NULL;
    uint32_t size = 0;
    if (!tide_request_poll(s->ticket, &error, &status, &data, &size)) return false;
    free(data);
    s->ticket = 0;
    s->sent = error == TIDE_IO_NONE && status >= 200 && status < 300;
    if (s->sent) return true;
    if (error == TIDE_IO_TIMED_OUT) snprintf(s->why, sizeof s->why, "the service took too long to answer");
    else if (error != TIDE_IO_NONE) snprintf(s->why, sizeof s->why, "the service couldn't be reached");
    else snprintf(s->why, sizeof s->why, "the service answered %d", (int)status);
    return true;
}

static const char *const saved_line =
    "A report was saved. Show it, and send the zip file to the game's developers so they can fix it.";

static const char *status_line(void *user, bool *settled)
{
    sending *s = user;
    static int said = -1; // 0 sending, 1 sent, 2 not sent
    const bool done = sent_yet(s);
    *settled = done;
    const int now = !done ? 0 : s->sent ? 1 : 2;
    if (now == said) return NULL;
    said = now;
    if (now == 0) return "Sending a report to the game's developers...";
    if (now == 1) return "A report was sent to the game's developers, so they can fix it. Here's what it says.";
    return "The report couldn't be sent. Show it, and send the zip file to the game's developers so they can fix it.";
}

static const char *saved_status(void *user, bool *settled)
{
    (void)user;
    *settled = true;
    return saved_line;
}

int crash_main(const int argc, char **argv)
{
    const bool zip_only = argc == 3 && strcmp(argv[1], "--zip") == 0;
    const bool send_only = argc == 3 && strcmp(argv[1], "--send") == 0;
    if (!zip_only && !send_only && argc != 3) {
        fprintf(stderr, "usage: tide-crash <crash folder> <title>\n       tide-crash --zip <crash folder>\n"
                        "       tide-crash --send <crash folder>\n");
        return 2;
    }
    const char *folder = zip_only || send_only ? argv[2] : argv[1];
    char zip[2048];
    report r = {0};
    if (!prepare(folder, &r, zip, sizeof zip)) {
        fprintf(stderr, "tide-crash: can't zip %s\n", folder);
        report_free(&r);
        return 1;
    }
    report_link l;
    const bool linked = read_link(&l);
    if (zip_only || (hidden() && !send_only)) {
        printf("%s\n", zip);
        report_free(&r);
        return 0;
    }
    sending s = {0};
    if (send_only) {
        printf("%s\n", zip);
        if (!linked) snprintf(s.why, sizeof s.why, "there's no crash.json beside tide-crash");
        else send_report(&s, &l, &r);
        while (!sent_yet(&s)) {
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
        }
        if (s.sent) printf("sent\n");
        else printf("not sent: %s\n", s.why);
        fflush(stdout);
        report_free(&r);
        return s.sent ? 0 : 1;
    }
    if (linked) send_report(&s, &l, &r);
    char heading[512];
    const char *title = argv[2][0] ? argv[2] : "The game";
    snprintf(heading, sizeof heading, "%s stopped unexpectedly.", title);
    char *text = preview(&r);
    const crash_view view = {.title = argv[2][0] ? argv[2] : "Crash", .heading = heading, .preview = text, .show = "Show report"};
    const int answer = crash_show(&view, linked ? status_line : saved_status, &s);
    free(text);
    if (answer == 1) reveal(zip);
    // A window closed before the report was sent: it goes on, for as long as the request may take
    while (!sent_yet(&s)) {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    report_free(&r);
    return 0;
}


#ifdef _WIN32
// Arguments as UTF-8, whatever the system's code page. WinMain, which MSVC's
// and MinGW's links both start a program of the Windows subsystem at (no
// console shows), with the command line read wide.
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR line, int show)
{
    (void)instance;
    (void)previous;
    (void)line;
    (void)show;
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return 1;
    char **argv = calloc((size_t)count + 1u, sizeof *argv);
    for (int i = 0; argv && i < count; i++) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0, NULL, NULL);
        argv[i] = malloc((size_t)(size > 0 ? size : 1));
        if (!argv[i]) return 1;
        argv[i][0] = '\0';
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argv[i], size, NULL, NULL);
    }
    LocalFree(wide);
    if (!argv) return 1;
    // No console, but the handles its caller gave it, where it gave them (a test's pipes)
    return crash_main(count, argv);
}
#else
int main(int argc, char **argv)
{
    return crash_main(argc, argv);
}
#endif
