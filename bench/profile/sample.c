// A sampling profiler for Windows, which needs no administrator rights: it
// starts a program, and every so often suspends each of its threads, reads
// where it is, and lets it go. At the end it prints where the program's own
// code spent its samples, by function and by line, through llvm-symbolizer
// and the program's PDB (a release build has one).
//
//   tide_sample [--hz <n>] [--top <n>] [--lines <n>] -- program [args...]
//
// Samples outside the program's own image (the system's DLLs: waits, the
// kernel's calls) are counted as "elsewhere" per DLL. A thread that's
// waiting shows up there too, so on several threads compare samples of the
// program's code, not shares of the whole.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sample_count {
    uint64_t address; // Relative to its module's base
    uint32_t module;  // Index into modules, or UINT32_MAX for unknown
    uint32_t count;
} sample_count;

typedef struct module_info {
    uint64_t base, size;
    char path[MAX_PATH];
} module_info;

static sample_count *table;
static size_t table_size, table_used;
static module_info modules[512];
static uint32_t module_count;

static void count_sample(const uint64_t address, const uint32_t module)
{
    if (table_used * 2u >= table_size) {
        const size_t old_size = table_size;
        sample_count *old = table;
        table_size = table_size ? table_size * 2u : 65536u;
        table = calloc(table_size, sizeof *table);
        if (!table) exit(3);
        table_used = 0;
        for (size_t i = 0; i < old_size; i++) {
            if (!old[i].count) continue;
            size_t h = (size_t)((old[i].address * 0x9E3779B97F4A7C15ull) >> 20) & (table_size - 1u);
            while (table[h].count) h = (h + 1u) & (table_size - 1u);
            table[h] = old[i];
            table_used++;
        }
        free(old);
    }
    size_t h = (size_t)((address * 0x9E3779B97F4A7C15ull) >> 20) & (table_size - 1u);
    while (table[h].count && (table[h].address != address || table[h].module != module)) h = (h + 1u) & (table_size - 1u);
    if (!table[h].count) {
        table[h].address = address;
        table[h].module = module;
        table_used++;
    }
    table[h].count++;
}

static void refresh_modules(HANDLE process)
{
    HMODULE handles[512];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process, handles, sizeof handles, &needed, LIST_MODULES_ALL)) return;
    const DWORD n = needed / sizeof(HMODULE);
    module_count = 0;
    for (DWORD i = 0; i < n && i < 512; i++) {
        MODULEINFO info;
        if (!GetModuleInformation(process, handles[i], &info, sizeof info)) continue;
        module_info *m = &modules[module_count++];
        m->base = (uint64_t)(uintptr_t)info.lpBaseOfDll;
        m->size = info.SizeOfImage;
        if (!GetModuleFileNameExA(process, handles[i], m->path, MAX_PATH)) strcpy(m->path, "?");
    }
}

static uint32_t module_of(const uint64_t rip)
{
    for (uint32_t i = 0; i < module_count; i++) {
        if (rip >= modules[i].base && rip - modules[i].base < modules[i].size) return i;
    }
    return UINT32_MAX;
}

// The preferred base the image was linked at, which llvm-symbolizer's
// addresses are relative to.
static uint64_t image_base(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0x140000000ull;
    uint8_t head[4096];
    const size_t n = fread(head, 1, sizeof head, f);
    fclose(f);
    if (n < 0x40) return 0x140000000ull;
    uint32_t pe;
    memcpy(&pe, head + 0x3c, 4);
    if (pe + 24u + 32u > n) return 0x140000000ull;
    uint64_t base = 0;
    const uint16_t magic = (uint16_t)(head[pe + 24] | head[pe + 25] << 8);
    if (magic == 0x20b) memcpy(&base, head + pe + 24 + 24, 8);
    else {
        uint32_t b32;
        memcpy(&b32, head + pe + 24 + 28, 4);
        base = b32;
    }
    return base;
}

typedef struct named {
    char *name;
    uint64_t count;
} named;

static int by_count(const void *a, const void *b)
{
    const named *x = a, *y = b;
    return x->count < y->count ? 1 : x->count > y->count ? -1 : strcmp(x->name, y->name);
}

static void add_named(named **list, size_t *count, size_t *capacity, const char *name, const uint64_t n)
{
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*list)[i].name, name) == 0) {
            (*list)[i].count += n;
            return;
        }
    }
    if (*count == *capacity) {
        *capacity = *capacity ? *capacity * 2u : 256u;
        *list = realloc(*list, *capacity * sizeof **list);
        if (!*list) exit(3);
    }
    (*list)[*count].name = _strdup(name);
    (*list)[*count].count = n;
    (*count)++;
}

static int by_address(const void *a, const void *b)
{
    const sample_count *x = a, *y = b;
    return x->address < y->address ? -1 : x->address > y->address;
}

int main(int argc, char **argv)
{
    double hz = 5000.0;
    int top = 40, lines = 40, arg = 1;
    for (; arg < argc; arg++) {
        if (strcmp(argv[arg], "--") == 0) {
            arg++;
            break;
        }
        if (strcmp(argv[arg], "--hz") == 0 && arg + 1 < argc) hz = atof(argv[++arg]);
        else if (strcmp(argv[arg], "--top") == 0 && arg + 1 < argc) top = atoi(argv[++arg]);
        else if (strcmp(argv[arg], "--lines") == 0 && arg + 1 < argc) lines = atoi(argv[++arg]);
        else {
            fprintf(stderr, "usage: tide_sample [--hz <n>] [--top <n>] [--lines <n>] -- program [args...]\n");
            return 2;
        }
    }
    if (arg >= argc) {
        fprintf(stderr, "tide_sample: no program to run\n");
        return 2;
    }
    char command[8192] = "";
    for (int i = arg; i < argc; i++) {
        strncat(command, "\"", sizeof command - strlen(command) - 1);
        strncat(command, argv[i], sizeof command - strlen(command) - 1);
        strncat(command, "\" ", sizeof command - strlen(command) - 1);
    }
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "tide_sample: couldn't start %s (%lu)\n", argv[arg], GetLastError());
        return 2;
    }

    LARGE_INTEGER frequency, now, next;
    QueryPerformanceFrequency(&frequency);
    const int64_t period = (int64_t)((double)frequency.QuadPart / hz);
    QueryPerformanceCounter(&next);
    uint64_t total = 0, rounds = 0;
    DWORD threads[256];
    HANDLE handles[256];
    uint32_t thread_count = 0;
    uint64_t last_refresh = 0;
    while (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
        if (rounds - last_refresh >= 256u || rounds == 0) { // The threads and modules it has now
            last_refresh = rounds;
            for (uint32_t i = 0; i < thread_count; i++) CloseHandle(handles[i]);
            thread_count = 0;
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te;
            memset(&te, 0, sizeof te);
            te.dwSize = sizeof te;
            if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te)) {
                do {
                    if (te.th32OwnerProcessID != pi.dwProcessId || thread_count == 256) continue;
                    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                    if (!t) continue;
                    threads[thread_count] = te.th32ThreadID;
                    handles[thread_count++] = t;
                } while (Thread32Next(snap, &te));
            }
            if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
            refresh_modules(pi.hProcess);
        }
        for (uint32_t i = 0; i < thread_count; i++) {
            if (SuspendThread(handles[i]) == (DWORD)-1) continue;
            CONTEXT c = {.ContextFlags = CONTEXT_CONTROL};
            if (GetThreadContext(handles[i], &c)) {
                const uint32_t m = module_of(c.Rip);
                count_sample(m == UINT32_MAX ? c.Rip : c.Rip - modules[m].base, m);
                total++;
            }
            ResumeThread(handles[i]);
        }
        rounds++;
        next.QuadPart += period;
        for (;;) { // Spin to the next round: Sleep is too coarse
            QueryPerformanceCounter(&now);
            if (now.QuadPart >= next.QuadPart) break;
            if (next.QuadPart - now.QuadPart > frequency.QuadPart / 1000) Sleep(0);
            else YieldProcessor();
        }
        if (now.QuadPart - next.QuadPart > 50 * period) next = now; // Fell behind
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    (void)threads;

    // The program's own image: the module whose path is the program's
    char program[MAX_PATH];
    if (!GetFullPathNameA(argv[arg], MAX_PATH, program, NULL)) strcpy(program, argv[arg]);
    uint32_t own = UINT32_MAX;
    for (uint32_t i = 0; i < module_count; i++) {
        if (_stricmp(modules[i].path, program) == 0 || strstr(modules[i].path, ".exe")) {
            own = i;
            break;
        }
    }
    // Everything else, by DLL
    named *elsewhere = NULL;
    size_t elsewhere_count = 0, elsewhere_capacity = 0;
    uint64_t own_total = 0;
    sample_count *mine = malloc(table_used * sizeof *mine + 1);
    size_t mine_count = 0;
    for (size_t i = 0; i < table_size; i++) {
        if (!table[i].count) continue;
        if (table[i].module == own && own != UINT32_MAX) {
            mine[mine_count++] = table[i];
            own_total += table[i].count;
        } else {
            const char *name = table[i].module == UINT32_MAX ? "(unknown)" : strrchr(modules[table[i].module].path, '\\');
            add_named(&elsewhere, &elsewhere_count, &elsewhere_capacity, name ? name + (name[0] == '\\') : "?", table[i].count);
        }
    }
    printf("\n%llu samples over %llu rounds (exit %lu); %llu in %s\n", (unsigned long long)total,
           (unsigned long long)rounds, exit_code, (unsigned long long)own_total, own == UINT32_MAX ? "?" : modules[own].path);
    qsort(elsewhere, elsewhere_count, sizeof *elsewhere, by_count);
    // Beside the program's own samples: ntdll is mostly threads waiting, the
    // C runtime's DLL mostly memcpy and memset the program called
    for (size_t i = 0; i < elsewhere_count && i < 8; i++) {
        printf("  elsewhere %10llu samples, %7.2f%% of the program's  %s\n", (unsigned long long)elsewhere[i].count,
               own_total ? 100.0 * (double)elsewhere[i].count / (double)own_total : 0.0, elsewhere[i].name);
    }
    if (own == UINT32_MAX || !mine_count) return (int)exit_code;

    // Symbolize the program's addresses through llvm-symbolizer, in one go
    qsort(mine, mine_count, sizeof *mine, by_address);
    const uint64_t base = image_base(modules[own].path);
    char in_path[MAX_PATH], out_path[MAX_PATH], temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    snprintf(in_path, sizeof in_path, "%stide_sample_%lu.in", temp, GetCurrentProcessId());
    snprintf(out_path, sizeof out_path, "%stide_sample_%lu.out", temp, GetCurrentProcessId());
    FILE *in = fopen(in_path, "wb");
    for (size_t i = 0; i < mine_count; i++) fprintf(in, "0x%llx\n", (unsigned long long)(base + mine[i].address));
    fclose(in);
    const char *symbolizer = getenv("TIDE_SYMBOLIZER");
    char run[4 * MAX_PATH];
    snprintf(run, sizeof run, "\"\"%s\" --obj=\"%s\" --output-style=LLVM --functions=short < \"%s\" > \"%s\"\"",
             symbolizer ? symbolizer : "C:\\Program Files\\LLVM\\bin\\llvm-symbolizer.exe", modules[own].path, in_path, out_path);
    if (system(run) != 0) {
        fprintf(stderr, "tide_sample: llvm-symbolizer failed\n");
        return (int)exit_code;
    }
    FILE *out = fopen(out_path, "rb");
    if (!out) return (int)exit_code;
    named *functions = NULL, *outer = NULL, *line_list = NULL;
    size_t function_count = 0, function_capacity = 0, outer_count = 0, outer_capacity = 0, line_count = 0, line_capacity = 0;
    // Each address gives frames (innermost first): a function line, then a
    // file:line:column line, then a blank line after its last frame
    char fn[1024], loc[2048], first_fn[1024], first_loc[2048], last_fn[1024];
    size_t at = 0;
    int frame = 0;
    while (at < mine_count && fgets(fn, sizeof fn, out)) {
        fn[strcspn(fn, "\r\n")] = 0;
        if (!fn[0]) { // The end of this address's frames
            if (frame) {
                add_named(&functions, &function_count, &function_capacity, first_fn, mine[at].count);
                add_named(&outer, &outer_count, &outer_capacity, last_fn, mine[at].count);
                char key[3200];
                snprintf(key, sizeof key, "%-40s %s", first_fn, first_loc);
                add_named(&line_list, &line_count, &line_capacity, key, mine[at].count);
            }
            frame = 0;
            at++;
            continue;
        }
        if (!fgets(loc, sizeof loc, out)) break;
        loc[strcspn(loc, "\r\n")] = 0;
        // Shorten paths to their last two parts
        char *slash = strrchr(loc, '\\');
        if (!slash) slash = strrchr(loc, '/');
        if (slash) {
            char *before = slash;
            while (before > loc && before[-1] != '\\' && before[-1] != '/') before--;
            memmove(loc, before, strlen(before) + 1);
        }
        if (!frame) {
            strcpy(first_fn, fn);
            strcpy(first_loc, loc);
        }
        strcpy(last_fn, fn);
        frame++;
    }
    fclose(out);
    remove(in_path);
    remove(out_path);
    qsort(functions, function_count, sizeof *functions, by_count);
    qsort(outer, outer_count, sizeof *outer, by_count);
    qsort(line_list, line_count, sizeof *line_list, by_count);
    printf("\n  innermost function (inlined code counts where it was written), %% of the program's samples:\n");
    for (size_t i = 0; i < function_count && (int)i < top; i++) {
        printf("  %6.2f%%  %s\n", 100.0 * (double)functions[i].count / (double)own_total, functions[i].name);
    }
    printf("\n  outermost function (what it was inlined into):\n");
    for (size_t i = 0; i < outer_count && (int)i < top; i++) {
        printf("  %6.2f%%  %s\n", 100.0 * (double)outer[i].count / (double)own_total, outer[i].name);
    }
    printf("\n  lines:\n");
    for (size_t i = 0; i < line_count && (int)i < lines; i++) {
        printf("  %6.2f%%  %s\n", 100.0 * (double)line_list[i].count / (double)own_total, line_list[i].name);
    }
    return (int)exit_code;
}
