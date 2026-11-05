// The program's log and what a crash leaves (tide_platform_log_begin,
// tide/platform.h): everything written to stderr goes on to where it went and
// to Player.log in the game's folder of saved data, through a pipe a thread
// of its own reads; a crash writes a folder of its own beside it.
//
// A crash's handler runs where the program broke, maybe with its heap broken
// too, and on Linux and macOS inside a signal: it allocates nothing and
// formats its text itself, with what it prepared at the start.
#if !defined(_WIN32) && !defined(__wasm__)
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _DEFAULT_SOURCE
#endif
#endif

#include "tide/platform.h"

#include "files.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TIDE_VERSION
#define TIDE_VERSION "unknown"
#endif

#ifdef __wasm__

// The browser's console has the output, and a page has no folder to keep it in.
void tide_platform_log_begin(const char *app_id, const char *title, const char *directory)
{
    (void)app_id;
    (void)title;
    (void)directory;
}

const char *tide_platform_log_path(void)
{
    return NULL;
}

void tide_platform_log_note(const char *text)
{
    (void)text;
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <signal.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h> // _NSGetExecutablePath
#endif
#define CRASHES 1 // Android's own crash reports (tombstones) have what a crash leaves
#endif
#endif
#ifdef _WIN32
#define CRASHES 1
#endif

#define LOG_LIMIT (100ull << 20) // A game that logs every frame stops filling the disk here
#define CRASHES_KEPT 10
#define PATH_MOST 1024

static bool started;
static char log_path[PATH_MOST];      // Player.log
static char crashes_path[PATH_MOST];  // Crashes, where each crash's folder goes
static uint64_t logged;               // Bytes the thread wrote to the log, up to LOG_LIMIT
static volatile int tee_done;         // The thread read the pipe's end: everything written before is in the log

// ---------------------------------------------------------------------------
// Text, put together without the C library's formatting, which a crash's
// handler can't count on

typedef struct text {
    char bytes[1024];
    size_t used;
} text;

static void put(text *t, const char *s)
{
    while (*s && t->used + 1u < sizeof t->bytes) t->bytes[t->used++] = *s++;
    t->bytes[t->used] = '\0';
}

static void put_number(text *t, uint64_t value, const unsigned width)
{
    char digits[24];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof digits);
    while (count < width && count < sizeof digits) digits[count++] = '0';
    char reversed[25];
    for (unsigned i = 0; i < count; i++) reversed[i] = digits[count - 1u - i];
    reversed[count] = '\0';
    put(t, reversed);
}

#ifdef CRASHES
static void put_hex(text *t, uint64_t value)
{
    char digits[19] = "0x";
    char reversed[16];
    unsigned count = 0;
    do {
        reversed[count++] = "0123456789abcdef"[value & 15u];
        value >>= 4;
    } while (value);
    for (unsigned i = 0; i < count; i++) digits[2u + i] = reversed[count - 1u - i];
    digits[2u + count] = '\0';
    put(t, digits);
}
#endif

// UTC, from seconds since 1970: as a name (2026-11-05_143012), or for a
// person (2026-11-05 14:30:12 UTC). Days to a date by Howard Hinnant's
// civil_from_days.
static void put_time(text *t, const int64_t seconds, const bool name)
{
    int64_t days = seconds / 86400, rest = seconds % 86400;
    if (rest < 0) {
        rest += 86400;
        days--;
    }
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t of_era = days - era * 146097;
    const int64_t year_of_era = (of_era - of_era / 1460 + of_era / 36524 - of_era / 146096) / 365;
    const int64_t of_year = of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t shifted = (5 * of_year + 2) / 153;
    const int64_t day = of_year - (153 * shifted + 2) / 5 + 1;
    const int64_t month = shifted < 10 ? shifted + 3 : shifted - 9;
    const int64_t year = year_of_era + era * 400 + (month <= 2);
    put_number(t, (uint64_t)year, 4);
    put(t, "-");
    put_number(t, (uint64_t)month, 2);
    put(t, "-");
    put_number(t, (uint64_t)day, 2);
    put(t, name ? "_" : " ");
    put_number(t, (uint64_t)(rest / 3600), 2);
    if (!name) put(t, ":");
    put_number(t, (uint64_t)(rest / 60 % 60), 2);
    if (!name) put(t, ":");
    put_number(t, (uint64_t)(rest % 60), 2);
    if (!name) put(t, " UTC");
}

#ifdef CRASHES
static bool make_new_folder(const char *path);

// A folder of its own for a crash in Crashes, by its UTC time, with a number
// after that where another crash had the same second
static bool new_crash_folder(text *folder, const int64_t seconds)
{
    for (unsigned n = 1; n <= 9u; n++) {
        *folder = (text){0};
        put(folder, crashes_path);
        put(folder, "/Crash_");
        put_time(folder, seconds, true);
        if (n > 1u) {
            put(folder, "_");
            put_number(folder, n, 1);
        }
        if (make_new_folder(folder->bytes)) return true;
    }
    return false;
}
#endif

static const char *system_name(void)
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown system";
#endif
}

static const char *processor_name(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86-64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__)
    return "ARM";
#else
    return "unknown processor";
#endif
}

// ---------------------------------------------------------------------------
// The system's side: files, the pipe and its thread, and the crash's handler

#ifdef _WIN32

static HANDLE log_file = INVALID_HANDLE_VALUE;
static int original = -1;   // Where stderr went before (a C runtime descriptor), or -1
static int pipe_write = -1; // The pipe's writing end, which stderr is now
static HANDLE pipe_read = INVALID_HANDLE_VALUE;
static HANDLE tee_thread;
static wchar_t wide_log[PATH_MOST], wide_crashes[PATH_MOST];

static bool widen(const char *path, wchar_t *out, const int size)
{
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, size);
    for (int i = 0; i < count; i++) if (out[i] == L'/') out[i] = L'\\';
    return count > 0;
}

static int64_t now_seconds(void)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const uint64_t ticks = ((uint64_t)now.dwHighDateTime << 32) | now.dwLowDateTime; // 100 ns since 1601
    return (int64_t)(ticks / 10000000u) - 11644473600;
}

static void write_handle(const HANDLE file, const void *bytes, const size_t size)
{
    DWORD wrote;
    if (file != INVALID_HANDLE_VALUE) WriteFile(file, bytes, (DWORD)size, &wrote, NULL);
}

static void write_original(const void *bytes, const size_t size)
{
    if (original >= 0) _write(original, bytes, (unsigned)size);
}

static HANDLE open_log(const wchar_t *path)
{
    // Appended to, so the thread's writes and a crash's don't overwrite each
    // other; and open to others, to read it while the game runs
    return CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void rotate(void)
{
    wchar_t previous[PATH_MOST];
    char narrow[PATH_MOST];
    snprintf(narrow, sizeof narrow, "%s", log_path);
    char *end = strrchr(narrow, '/');
    if (!end) return;
    snprintf(end + 1, sizeof narrow - (size_t)(end + 1 - narrow), "Player-prev.log");
    if (widen(narrow, previous, PATH_MOST)) MoveFileExW(wide_log, previous, MOVEFILE_REPLACE_EXISTING);
}

static DWORD WINAPI log_tee(void *unused);

static bool start_tee(void)
{
    // A program with no console (a game built for players) has no stderr:
    // it gets one that writes to nothing, which the log then sees
    if (_fileno(stderr) < 0 || _get_osfhandle(_fileno(stderr)) == -1 || _get_osfhandle(_fileno(stderr)) == -2) {
        if (!freopen("NUL", "w", stderr)) return false;
    } else {
        original = _dup(_fileno(stderr));
    }
    int ends[2];
    if (_pipe(ends, 65536, _O_BINARY | _O_NOINHERIT) != 0) return false;
    fflush(stderr);
    pipe_read = (HANDLE)_get_osfhandle(ends[0]);
    if (_dup2(ends[1], _fileno(stderr)) != 0) {
        _close(ends[0]);
        _close(ends[1]);
        return false;
    }
    _close(ends[1]);
    pipe_write = _fileno(stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(pipe_write));
    tee_thread = CreateThread(NULL, 0, log_tee, NULL, 0, NULL);
    return tee_thread != NULL;
}

static bool read_pipe(char *bytes, const size_t size, size_t *got)
{
    DWORD read;
    if (!ReadFile(pipe_read, bytes, (DWORD)size, &read, NULL) || !read) return false;
    *got = read;
    return true;
}

// The pipe's writing end goes, so the thread reads the rest and stops
static void end_tee(void)
{
    if (pipe_write < 0) return;
    if (original >= 0) _dup2(original, pipe_write);
    else {
        const int none = _open("NUL", _O_WRONLY);
        if (none >= 0) {
            _dup2(none, pipe_write);
            _close(none);
        }
    }
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(pipe_write));
}

static void join_tee(const DWORD most_ms)
{
    if (tee_thread) WaitForSingleObject(tee_thread, most_ms);
}

static bool make_folder(const char *path)
{
    wchar_t wide[PATH_MOST];
    return widen(path, wide, PATH_MOST) && (CreateDirectoryW(wide, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
}

static bool make_new_folder(const char *path)
{
    wchar_t wide[PATH_MOST];
    return widen(path, wide, PATH_MOST) && CreateDirectoryW(wide, NULL);
}

// The crash folders but the newest CRASHES_KEPT - 1, before this run can add one
static void prune_crashes(void)
{
    wchar_t pattern[PATH_MOST + 16];
    _snwprintf(pattern, PATH_MOST + 16, L"%ls\\Crash_*", wide_crashes);
    WIN32_FIND_DATAW found;
    wchar_t names[64][64];
    int count = 0;
    const HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || wcslen(found.cFileName) >= 64) continue;
        if (count < 64) wcscpy(names[count++], found.cFileName);
    } while (FindNextFileW(find, &found));
    FindClose(find);
    // Names sort by time: the oldest go
    for (int i = 1; i < count; i++) {
        for (int k = i; k > 0 && wcscmp(names[k - 1], names[k]) > 0; k--) {
            wchar_t swap[64];
            wcscpy(swap, names[k]);
            wcscpy(names[k], names[k - 1]);
            wcscpy(names[k - 1], swap);
        }
    }
    for (int i = 0; i + (CRASHES_KEPT - 1) < count; i++) {
        static const wchar_t *const files[] = {L"crash.dmp", L"crash.txt", L"Player.log", L"system.txt"};
        wchar_t path[PATH_MOST + 96];
        for (size_t f = 0; f < sizeof files / sizeof files[0]; f++) {
            _snwprintf(path, PATH_MOST + 96, L"%ls\\%ls\\%ls", wide_crashes, names[i], files[f]);
            DeleteFileW(path);
        }
        _snwprintf(path, PATH_MOST + 96, L"%ls\\%ls", wide_crashes, names[i]);
        RemoveDirectoryW(path);
        _snwprintf(path, PATH_MOST + 96, L"%ls\\%ls.zip", wide_crashes, names[i]); // tide-crash's
        DeleteFileW(path);
    }
}

// MINIDUMP_EXCEPTION_INFORMATION and MiniDumpWriteDump, from dbghelp.dll,
// which is loaded at the start: loading a library in a crash may not work.
#pragma pack(push, 4)
typedef struct dump_exception {
    DWORD thread;
    EXCEPTION_POINTERS *pointers;
    BOOL client_pointers;
} dump_exception;
#pragma pack(pop)
typedef BOOL(WINAPI *write_dump_fn)(HANDLE process, DWORD process_id, HANDLE file, int type,
                                    const dump_exception *exception, const void *user, const void *callback);
static write_dump_fn write_dump;

static void prepare_crashes(void)
{
    const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    const FARPROC found = dbghelp ? GetProcAddress(dbghelp, "MiniDumpWriteDump") : NULL;
    if (found) memcpy(&write_dump, &found, sizeof write_dump);
}

// "game.exe+0x1a2b", for an address in a module
static void put_place(text *t, const uint64_t address)
{
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const wchar_t *)(uintptr_t)address, &module)) {
        put_hex(t, address);
        return;
    }
    wchar_t wide[PATH_MOST];
    const DWORD length = GetModuleFileNameW(module, wide, PATH_MOST);
    const wchar_t *base = wide;
    for (DWORD i = 0; i < length; i++) if (wide[i] == L'\\' || wide[i] == L'/') base = wide + i + 1;
    char name[256];
    const int size = length ? WideCharToMultiByte(CP_UTF8, 0, base, -1, name, sizeof name, NULL, NULL) : 0;
    put(t, size > 0 ? name : "?");
    put(t, "+");
    put_hex(t, address - (uint64_t)(uintptr_t)module);
}

static void put_exception(text *t, const DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: put(t, "access violation"); return;
    case EXCEPTION_STACK_OVERFLOW: put(t, "stack overflow"); return;
    case EXCEPTION_ILLEGAL_INSTRUCTION: put(t, "illegal instruction"); return;
    case EXCEPTION_INT_DIVIDE_BY_ZERO: put(t, "integer divide by zero"); return;
    case EXCEPTION_IN_PAGE_ERROR: put(t, "page error"); return;
    case 0x40000015: put(t, "abort"); return; // STATUS_FATAL_APP_EXIT, as the abort handler names it
    default: put(t, "exception "); put_hex(t, code); return;
    }
}

// The crashing thread's stack, from where it broke, as the system unwinds it
static void put_stack(const HANDLE file, CONTEXT context)
{
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
    for (int frame = 0; frame < 64; frame++) {
#if defined(_M_X64) || defined(__x86_64__)
        const DWORD64 pc = context.Rip;
#else
        const DWORD64 pc = context.Pc;
#endif
        if (!pc) break;
        text line = {0};
        put(&line, "  ");
        put_place(&line, pc);
        put(&line, "\n");
        write_handle(file, line.bytes, line.used);
        write_handle(log_file, line.bytes, line.used);
        write_original(line.bytes, line.used);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &base, NULL);
        if (function) {
            void *handler;
            DWORD64 establisher;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, pc, function, &context, &handler, &establisher, NULL);
        } else {
#if defined(_M_X64) || defined(__x86_64__)
            // A leaf function: its return address is on top of the stack
            if (!context.Rsp) break;
            context.Rip = *(const DWORD64 *)(uintptr_t)context.Rsp;
            context.Rsp += 8;
#else
            if (context.Pc == context.Lr) break;
            context.Pc = context.Lr;
#endif
        }
    }
#else
    (void)file;
    (void)context;
#endif
}

static void copy_file(const wchar_t *from, const wchar_t *to)
{
    const HANDLE source = CreateFileW(from, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (source == INVALID_HANDLE_VALUE) return;
    const HANDLE target = CreateFileW(to, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (target != INVALID_HANDLE_VALUE) {
        static char buffer[65536];
        DWORD got;
        while (ReadFile(source, buffer, sizeof buffer, &got, NULL) && got) write_handle(target, buffer, got);
        CloseHandle(target);
    }
    CloseHandle(source);
}

// tide-crash, beside the program, where it is, and the game's title for it:
// found at the start
static wchar_t reporter[PATH_MOST], reporter_title[256];

static void find_reporter(const char *title)
{
    wchar_t program[PATH_MOST];
    const DWORD length = GetModuleFileNameW(NULL, program, PATH_MOST);
    if (!length || length >= PATH_MOST) return;
    wchar_t *slash = wcsrchr(program, L'\\');
    if (!slash) return;
    slash[1] = L'\0';
    _snwprintf(reporter, PATH_MOST, L"%lstide-crash.exe", program);
    reporter[PATH_MOST - 1] = L'\0';
    if (GetFileAttributesW(reporter) == INVALID_FILE_ATTRIBUTES) {
        reporter[0] = L'\0';
        return;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, title, -1, reporter_title, 256)) reporter_title[0] = L'\0';
    for (wchar_t *c = reporter_title; *c; c++) if (*c == L'"') *c = L'\''; // It's quoted on the command line
}

static void start_reporter(const wchar_t *folder)
{
    if (!reporter[0]) return;
    static wchar_t command[PATH_MOST * 2 + 300];
    _snwprintf(command, sizeof command / sizeof command[0], L"\"%ls\" \"%ls\" \"%ls\"", reporter, folder,
               reporter_title);
    command[sizeof command / sizeof command[0] - 1] = L'\0';
    STARTUPINFOW startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (CreateProcessW(reporter, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

static volatile LONG crashing;

static void record_crash(EXCEPTION_POINTERS *pointers)
{
    if (InterlockedExchange(&crashing, 1)) Sleep(INFINITE); // Another thread is writing its crash; the program ends with it
    end_tee();
    // The crash's folder, named by its time
    text folder;
    wchar_t wide_folder[PATH_MOST], path[PATH_MOST + 32];
    const bool made = new_crash_folder(&folder, now_seconds()) && widen(folder.bytes, wide_folder, PATH_MOST);
    // The dump first, while the stack is as it was: walking it reads memory that may be broken
    if (made && write_dump) {
        _snwprintf(path, PATH_MOST + 32, L"%ls\\crash.dmp", wide_folder);
        const HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (dump != INVALID_HANDLE_VALUE) {
            const dump_exception exception = {GetCurrentThreadId(), pointers, FALSE};
            // MiniDumpWithUnloadedModules, MiniDumpWithIndirectlyReferencedMemory, MiniDumpWithThreadInfo
            write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump, 0x20 | 0x40 | 0x1000, &exception, NULL, NULL);
            CloseHandle(dump);
        }
    }
    HANDLE report = INVALID_HANDLE_VALUE;
    if (made) {
        _snwprintf(path, PATH_MOST + 32, L"%ls\\crash.txt", wide_folder);
        report = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    text line = {0};
    put(&line, "tide: crashed: ");
    put_exception(&line, pointers->ExceptionRecord->ExceptionCode);
    put(&line, " at ");
    put_place(&line, (uint64_t)(uintptr_t)pointers->ExceptionRecord->ExceptionAddress);
    if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && pointers->ExceptionRecord->NumberParameters >= 2) {
        put(&line, pointers->ExceptionRecord->ExceptionInformation[0] == 1 ? ", writing " : ", reading ");
        put_hex(&line, pointers->ExceptionRecord->ExceptionInformation[1]);
    }
    put(&line, "\n");
    join_tee(2000); // What was written before the crash goes first, unless the thread can't
    write_handle(report, line.bytes, line.used);
    write_handle(log_file, line.bytes, line.used);
    write_original(line.bytes, line.used);
    put_stack(report, *pointers->ContextRecord);
    if (report != INVALID_HANDLE_VALUE) CloseHandle(report);
    if (made) {
        text said = {0};
        put(&said, "tide: what the crash left is in ");
        put(&said, folder.bytes);
        put(&said, "\n");
        write_handle(log_file, said.bytes, said.used);
        write_original(said.bytes, said.used);
        _snwprintf(path, PATH_MOST + 32, L"%ls\\Player.log", wide_folder);
        copy_file(wide_log, path);
        start_reporter(wide_folder);
    }
}

static LONG WINAPI crashed(EXCEPTION_POINTERS *pointers)
{
    record_crash(pointers);
    return EXCEPTION_CONTINUE_SEARCH; // On to the system's, which ends the program as it would have
}

// abort(), which every stop of the engine's and of generated code ends in,
// is no exception: it's written as one, from here
static void aborted(int signal)
{
    (void)signal;
    CONTEXT context;
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {.ExceptionCode = 0x40000015}; // STATUS_FATAL_APP_EXIT
#if defined(_M_X64) || defined(__x86_64__)
    record.ExceptionAddress = (void *)(uintptr_t)context.Rip;
#elif defined(_M_ARM64) || defined(__aarch64__)
    record.ExceptionAddress = (void *)(uintptr_t)context.Pc;
#endif
    EXCEPTION_POINTERS pointers = {&record, &context};
    record_crash(&pointers);
}

static void catch_crashes(void)
{
    SetUnhandledExceptionFilter(crashed);
    signal(SIGABRT, aborted);
}

#else // POSIX

static int log_fd = -1;
static int original = -1; // Where stderr went before, or -1
static int pipe_read = -1;
static bool piped;
static pthread_t tee_thread;

static int64_t now_seconds(void)
{
    return (int64_t)time(NULL);
}

static void write_fd(const int fd, const void *bytes, size_t size)
{
    const char *at = bytes;
    while (fd >= 0 && size) {
        const ssize_t wrote = write(fd, at, size);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) return;
        at += wrote;
        size -= (size_t)wrote;
    }
}

static void write_original(const void *bytes, const size_t size)
{
    write_fd(original, bytes, size);
}

static void rotate(void)
{
    char previous[PATH_MOST];
    snprintf(previous, sizeof previous, "%s", log_path);
    char *end = strrchr(previous, '/');
    if (!end) return;
    snprintf(end + 1, sizeof previous - (size_t)(end + 1 - previous), "Player-prev.log");
    rename(log_path, previous);
}

static void *log_tee(void *unused);

static bool start_tee(void)
{
    int ends[2];
    if (pipe(ends) != 0) return false;
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    original = dup(STDERR_FILENO);
    if (original >= 0) fcntl(original, F_SETFD, FD_CLOEXEC);
    fflush(stderr);
    if (dup2(ends[1], STDERR_FILENO) < 0) {
        close(ends[0]);
        close(ends[1]);
        return false;
    }
    close(ends[1]);
    setvbuf(stderr, NULL, _IONBF, 0);
    pipe_read = ends[0];
    piped = pthread_create(&tee_thread, NULL, log_tee, NULL) == 0;
    return piped;
}

static bool read_pipe(char *bytes, const size_t size, size_t *got)
{
    for (;;) {
        const ssize_t read_now = read(pipe_read, bytes, size);
        if (read_now < 0 && errno == EINTR) continue;
        if (read_now <= 0) return false;
        *got = (size_t)read_now;
        return true;
    }
}

// The pipe's writing end goes, so the thread reads the rest and stops
static void end_tee(void)
{
    if (!piped) return;
    if (original >= 0) dup2(original, STDERR_FILENO);
    else {
        const int none = open("/dev/null", O_WRONLY);
        if (none >= 0) {
            dup2(none, STDERR_FILENO);
            close(none);
        }
    }
}

#ifdef CRASHES
static bool make_folder(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool make_new_folder(const char *path)
{
    return mkdir(path, 0755) == 0;
}
#endif

#ifdef CRASHES

// The crash folders but the newest CRASHES_KEPT - 1, before this run can add one
static void prune_crashes(void)
{
    DIR *folder = opendir(crashes_path);
    if (!folder) return;
    char names[64][64];
    int count = 0;
    const struct dirent *entry;
    while ((entry = readdir(folder)) != NULL) {
        if (strncmp(entry->d_name, "Crash_", 6) != 0 || strlen(entry->d_name) >= 64 || count >= 64) continue;
        snprintf(names[count++], 64, "%s", entry->d_name);
    }
    closedir(folder);
    // Names sort by time: the oldest go
    for (int i = 1; i < count; i++) {
        for (int k = i; k > 0 && strcmp(names[k - 1], names[k]) > 0; k--) {
            char swap[64];
            memcpy(swap, names[k], 64);
            memcpy(names[k], names[k - 1], 64);
            memcpy(names[k - 1], swap, 64);
        }
    }
    for (int i = 0; i + (CRASHES_KEPT - 1) < count; i++) {
        static const char *const files[] = {"crash.txt", "Player.log", "system.txt"};
        char path[PATH_MOST + 96];
        for (size_t f = 0; f < sizeof files / sizeof files[0]; f++) {
            snprintf(path, sizeof path, "%s/%s/%s", crashes_path, names[i], files[f]);
            unlink(path);
        }
        snprintf(path, sizeof path, "%s/%s", crashes_path, names[i]);
        rmdir(path);
        snprintf(path, sizeof path, "%s/%s.zip", crashes_path, names[i]); // tide-crash's
        unlink(path);
    }
}

static void prepare_crashes(void)
{
    // backtrace loads what it unwinds with the first time: not in a signal
    void *frames[4];
    backtrace(frames, 4);
}

static void put_signal(text *t, const int signal)
{
    switch (signal) {
    case SIGSEGV: put(t, "SIGSEGV (bad memory access)"); return;
    case SIGBUS: put(t, "SIGBUS (bad memory access)"); return;
    case SIGILL: put(t, "SIGILL (illegal instruction)"); return;
    case SIGFPE: put(t, "SIGFPE (arithmetic error)"); return;
    case SIGABRT: put(t, "SIGABRT (abort)"); return;
    case SIGTRAP: put(t, "SIGTRAP (trap)"); return;
    default: put(t, "signal "); put_number(t, (uint64_t)signal, 1); return;
    }
}

static void copy_file(const char *from, const char *to)
{
    const int source = open(from, O_RDONLY);
    if (source < 0) return;
    const int target = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (target >= 0) {
        static char buffer[65536];
        ssize_t got;
        while ((got = read(source, buffer, sizeof buffer)) > 0) write_fd(target, buffer, (size_t)got);
        close(target);
    }
    close(source);
}

// tide-crash, beside the program, where it is, and the game's title for it:
// found at the start
static char reporter[PATH_MOST], reporter_title[256];

static void find_reporter(const char *title)
{
    char program[PATH_MOST];
#if defined(__APPLE__)
    uint32_t size = sizeof program;
    if (_NSGetExecutablePath(program, &size) != 0) return;
#else
    const ssize_t length = readlink("/proc/self/exe", program, sizeof program - 1u);
    if (length <= 0) return;
    program[length] = '\0';
#endif
    char *slash = strrchr(program, '/');
    if (!slash) return;
    slash[1] = '\0';
    const int fits = snprintf(reporter, sizeof reporter, "%stide-crash", program);
    if (fits <= 0 || (size_t)fits >= sizeof reporter || access(reporter, X_OK) != 0) {
        reporter[0] = '\0';
        return;
    }
    snprintf(reporter_title, sizeof reporter_title, "%s", title);
}

// In a signal: fork and exec are safe there, and the child does nothing else
static void start_reporter(const char *folder)
{
    if (!reporter[0]) return;
    const pid_t child = fork();
    if (child != 0) return;
    char *const arguments[] = {reporter, (char *)folder, reporter_title, NULL};
    execv(reporter, arguments);
    _exit(127);
}

static volatile sig_atomic_t crashing;
static const int caught[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
static char alternate_stack[65536]; // The main thread's handler runs here when its own stack is what overflowed

static void crashed(const int signal, siginfo_t *info, void *context)
{
    (void)context;
    if (crashing) {
        for (;;) pause(); // Another thread is writing its crash; the program ends with it
    }
    crashing = 1;
    end_tee();
    // Up to two seconds for the thread to write what was written before the crash
    for (int i = 0; i < 200 && !tee_done; i++) {
        const struct timespec wait = {0, 10000000};
        nanosleep(&wait, NULL);
    }
    text folder;
    const bool made = new_crash_folder(&folder, now_seconds());
    int report = -1;
    if (made) {
        text path = folder;
        put(&path, "/crash.txt");
        report = open(path.bytes, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    text line = {0};
    put(&line, "tide: crashed: ");
    put_signal(&line, signal);
    if (signal == SIGSEGV || signal == SIGBUS) {
        put(&line, " at ");
        put_hex(&line, (uint64_t)(uintptr_t)info->si_addr);
    }
    put(&line, "\n");
    write_fd(report, line.bytes, line.used);
    write_fd(log_fd, line.bytes, line.used);
    write_fd(STDERR_FILENO, line.bytes, line.used); // The original stderr again (end_tee): the thread closes its own copy
    void *frames[64];
    const int count = backtrace(frames, 64);
    if (report >= 0) backtrace_symbols_fd(frames, count, report);
    if (log_fd >= 0) backtrace_symbols_fd(frames, count, log_fd);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    if (report >= 0) close(report);
    if (made) {
        text said = {0};
        put(&said, "tide: what the crash left is in ");
        put(&said, folder.bytes);
        put(&said, "\n");
        write_fd(log_fd, said.bytes, said.used);
        write_fd(STDERR_FILENO, said.bytes, said.used);
        text path = folder;
        put(&path, "/Player.log");
        copy_file(log_path, path.bytes);
        start_reporter(folder.bytes);
    }
    // On to the system's, which ends the program as it would have (a core
    // file, macOS's crash report)
    struct sigaction plain = {0};
    plain.sa_handler = SIG_DFL;
    sigemptyset(&plain.sa_mask);
    sigaction(signal, &plain, NULL);
    raise(signal);
}

static void catch_crashes(void)
{
    const stack_t stack = {.ss_sp = alternate_stack, .ss_size = sizeof alternate_stack};
    sigaltstack(&stack, NULL);
    struct sigaction action = {0};
    action.sa_sigaction = crashed;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof caught / sizeof caught[0]; i++) sigaction(caught[i], &action, NULL);
}

#endif // CRASHES

#endif // POSIX

// ---------------------------------------------------------------------------
// Both

static void write_log(const void *bytes, const size_t size)
{
#ifdef _WIN32
    write_handle(log_file, bytes, size);
#else
    write_fd(log_fd, bytes, size);
#endif
}

static void forward(const char *bytes, const size_t size)
{
    write_original(bytes, size);
    if (logged >= LOG_LIMIT) return;
    write_log(bytes, size);
    logged += size;
    if (logged >= LOG_LIMIT) {
        static const char full[] = "\ntide: the log stops here, at 100 MB\n";
        write_log(full, sizeof full - 1u);
    }
}

#ifdef _WIN32
static DWORD WINAPI log_tee(void *unused)
#else
static void *log_tee(void *unused)
#endif
{
    (void)unused;
    static char bytes[4096];
    size_t got;
    while (read_pipe(bytes, sizeof bytes, &got)) forward(bytes, got);
    tee_done = 1;
#ifdef _WIN32
    return 0;
#else
    // Android's stderr was a pipe to the system's log, which ends once nothing writes to it
    if (original >= 0) close(original);
    original = -1;
    return NULL;
#endif
}

// The program ends: what's in the pipe is written before it goes
static void log_end(void)
{
    fflush(NULL);
    end_tee();
#ifdef _WIN32
    join_tee(INFINITE);
#else
    if (piped) pthread_join(tee_thread, NULL);
    piped = false;
#endif
}

void tide_platform_log_begin(const char *app_id, const char *title, const char *directory)
{
    if (started) return;
    started = true;
    char *folder = tide_platform_data_folder(app_id, directory);
    if (!folder) return;
    const int fits = snprintf(log_path, sizeof log_path, "%s/Player.log", folder);
    const int fits_crashes = snprintf(crashes_path, sizeof crashes_path, "%s/Crashes", folder);
    free(folder);
    if (fits <= 0 || (size_t)fits >= sizeof log_path || fits_crashes <= 0 || (size_t)fits_crashes >= sizeof crashes_path) {
        log_path[0] = crashes_path[0] = '\0';
        return;
    }
#ifdef _WIN32
    if (!widen(log_path, wide_log, PATH_MOST) || !widen(crashes_path, wide_crashes, PATH_MOST)) {
        log_path[0] = '\0';
        return;
    }
    rotate();
    log_file = open_log(wide_log);
    if (log_file == INVALID_HANDLE_VALUE) {
        log_path[0] = '\0';
        return;
    }
#else
    rotate();
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0) {
        log_path[0] = '\0';
        return;
    }
#endif
    text header = {0};
    put(&header, "Tide ");
    put(&header, TIDE_VERSION);
    put(&header, ", ");
    put(&header, app_id && *app_id ? app_id : "dev.tide.game");
    put(&header, ", ");
    put(&header, system_name());
    put(&header, " ");
    put(&header, processor_name());
    put(&header, ", started ");
    put_time(&header, now_seconds(), false);
    put(&header, "\n");
    write_log(header.bytes, header.used);
    if (!start_tee()) return;
    atexit(log_end);
#ifdef CRASHES
    if (make_folder(crashes_path)) {
        prune_crashes();
        prepare_crashes();
        find_reporter(title && *title ? title : app_id && *app_id ? app_id : "The game");
        catch_crashes();
    }
#else
    (void)title;
#endif
}

const char *tide_platform_log_path(void)
{
    return log_path[0] ? log_path : NULL;
}

void tide_platform_log_note(const char *text)
{
    if (!log_path[0] || !text) return;
    struct text line = {0};
    put(&line, text);
    put(&line, "\n");
    write_log(line.bytes, line.used);
}

#endif
