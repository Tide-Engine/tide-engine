// tide-crash: what a player sees after a game crashed. The game's crash
// handler (platform/src/log.c) starts it from beside the game's program, with
// the folder the crash left and the game's title:
//
//   tide-crash <crash folder> <title>
//
// It's a program of its own so it starts at once however big the game is,
// and with nothing of the broken process. It asks, in a small window of its
// own (ui.h), whether to show the report; if so, it writes system.txt into
// the folder, zips the folder beside it (Crash_<time>.zip) and opens the
// file manager there, for the player to send the zip to the game's makers.
//
//   tide-crash --zip <crash folder>
//
// zips with no window and prints the zip's path. Where nobody watches
// (TIDE_HIDDEN), every run is that.
#if !defined(_WIN32)
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _DEFAULT_SOURCE
#endif
#endif

#include "crash.h"

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

typedef struct zip_entry {
    char name[64];
    uint32_t crc, size, offset;
} zip_entry;

static bool zip_folder(const char *folder, const char *zip_path)
{
    static const char *const files[] = {"crash.txt", "system.txt", "Player.log", "crash.dmp"};
    enum { FILES = sizeof files / sizeof files[0] };
    FILE *zip = crash_open(zip_path, "wb");
    if (!zip) return false;
    crc_init();
    // MS-DOS time and date of now, for every file: the report's, not the files'
    const time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    const uint32_t dos_time = local ? (uint32_t)(local->tm_hour << 11 | local->tm_min << 5 | local->tm_sec / 2) : 0u;
    const uint32_t dos_date = local ? (uint32_t)((local->tm_year - 80) << 9 | (local->tm_mon + 1) << 5 | local->tm_mday) : 0u;
    zip_entry entries[FILES];
    int count = 0;
    bool ok = true;
    static uint8_t buffer[1 << 16];
    for (int f = 0; f < FILES && ok; f++) {
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", folder, files[f]);
        FILE *in = crash_open(path, "rb");
        if (!in) continue; // crash.dmp is Windows', and a crash may have left less
        zip_entry *e = &entries[count++];
        snprintf(e->name, sizeof e->name, "%s", files[f]);
        e->offset = (uint32_t)ftell(zip);
        const uint32_t name_size = (uint32_t)strlen(e->name);
        uint8_t header[30] = {0};
        put32(header, 0x04034b50u);
        put16(header + 4, 10);         // Version needed: 1.0
        put16(header + 10, dos_time);
        put16(header + 12, dos_date);
        put16(header + 26, name_size); // CRC and sizes are written once the file's read
        ok = fwrite(header, 1, 30, zip) == 30 && fwrite(e->name, 1, name_size, zip) == name_size;
        e->crc = 0;
        e->size = 0;
        size_t got;
        while (ok && (got = fread(buffer, 1, sizeof buffer, in)) != 0) {
            e->crc = crc_add(e->crc, buffer, got);
            e->size += (uint32_t)got;
            ok = fwrite(buffer, 1, got, zip) == got;
        }
        fclose(in);
        const long end = ftell(zip);
        uint8_t sizes[12];
        put32(sizes, e->crc);
        put32(sizes + 4, e->size);
        put32(sizes + 8, e->size);
        ok = ok && fseek(zip, (long)e->offset + 14, SEEK_SET) == 0 && fwrite(sizes, 1, 12, zip) == 12
             && fseek(zip, end, SEEK_SET) == 0;
    }
    const uint32_t directory = (uint32_t)ftell(zip);
    for (int i = 0; i < count && ok; i++) {
        const zip_entry *e = &entries[i];
        const uint32_t name_size = (uint32_t)strlen(e->name);
        uint8_t header[46] = {0};
        put32(header, 0x02014b50u);
        put16(header + 4, 10);
        put16(header + 6, 10);
        put16(header + 12, dos_time);
        put16(header + 14, dos_date);
        put32(header + 16, e->crc);
        put32(header + 20, e->size);
        put32(header + 24, e->size);
        put16(header + 28, name_size);
        put32(header + 42, e->offset);
        ok = fwrite(header, 1, 46, zip) == 46 && fwrite(e->name, 1, name_size, zip) == name_size;
    }
    uint8_t end[22] = {0};
    put32(end, 0x06054b50u);
    put16(end + 8, (uint32_t)count);
    put16(end + 10, (uint32_t)count);
    put32(end + 12, (uint32_t)ftell(zip) - directory);
    put32(end + 16, directory);
    ok = ok && fwrite(end, 1, 22, zip) == 22;
    ok = fclose(zip) == 0 && ok;
    return ok && count > 0;
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

// The zip's path: the folder's, with .zip
static bool report(const char *folder, char *zip, const size_t size)
{
    char trimmed[2048];
    snprintf(trimmed, sizeof trimmed, "%s", folder);
    size_t length = strlen(trimmed);
    while (length > 1 && (trimmed[length - 1] == '/' || trimmed[length - 1] == '\\')) trimmed[--length] = '\0';
    const int fits = snprintf(zip, size, "%s.zip", trimmed);
    if (fits <= 0 || (size_t)fits >= size) return false;
    write_system(trimmed);
    return zip_folder(trimmed, zip);
}

int crash_main(const int argc, char **argv)
{
    const bool zip_only = argc == 3 && strcmp(argv[1], "--zip") == 0;
    if (!zip_only && argc != 3) {
        fprintf(stderr, "usage: tide-crash <crash folder> <title>\n       tide-crash --zip <crash folder>\n");
        return 2;
    }
    const char *folder = zip_only ? argv[2] : argv[1];
    char zip[2048];
    if (zip_only || hidden()) {
        if (!report(folder, zip, sizeof zip)) {
            fprintf(stderr, "tide-crash: can't zip %s\n", folder);
            return 1;
        }
        printf("%s\n", zip);
        return 0;
    }
    char text[1024];
    snprintf(text, sizeof text,
             "%s stopped unexpectedly.\n\nA report was saved. Show it, and send the zip file to the game's developers "
             "so they can fix it.",
             argv[2][0] ? argv[2] : "The game");
    if (crash_ask(argv[2][0] ? argv[2] : "Crash", text) != 1) return 0;
    if (!report(folder, zip, sizeof zip)) {
        fprintf(stderr, "tide-crash: can't zip %s\n", folder);
        return 1;
    }
    reveal(zip);
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
