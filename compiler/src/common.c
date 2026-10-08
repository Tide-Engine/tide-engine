#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // isatty and getcwd under strict C
#endif

#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Memory

typedef struct arena_block {
    struct arena_block *next;
    size_t used;
    size_t cap;
    _Alignas(16) char data[];
} arena_block;

static arena_block *arena_head;

void *arena_alloc(size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (!arena_head || arena_head->used + size > arena_head->cap) {
        const size_t cap = size > 1u << 20 ? size : 1u << 20;
        arena_block *block = malloc(sizeof(arena_block) + cap);
        if (!block) {
            fprintf(stderr, "tidec: out of memory\n");
            exit(1);
        }
        block->next = arena_head;
        block->used = 0;
        block->cap = cap;
        arena_head = block;
    }
    void *p = arena_head->data + arena_head->used;
    arena_head->used += size;
    memset(p, 0, size);
    return p;
}

void arena_reset(void)
{
    while (arena_head) {
        arena_block *next = arena_head->next;
        free(arena_head);
        arena_head = next;
    }
}

void *vec_grow(void *items, const size_t old_cap, const size_t new_cap, const size_t elem_size)
{
    void *p = arena_alloc(new_cap * elem_size);
    if (items) memcpy(p, items, old_cap * elem_size);
    return p;
}

// ---------------------------------------------------------------------------
// Strings

str str_from(const char *cstr)
{
    return (str){cstr, (int)strlen(cstr)};
}

bool str_eq(const str a, const str b)
{
    // An empty str may have no pointer, which memcmp isn't to be given
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, (size_t)a.len) == 0);
}

bool str_eq_c(const str a, const char *cstr)
{
    return str_eq(a, str_from(cstr));
}

bool str_starts_with_c(const str a, const char *prefix)
{
    const size_t n = strlen(prefix);
    return (size_t)a.len >= n && memcmp(a.ptr, prefix, n) == 0;
}

char *str_to_cstr(const str s)
{
    char *p = arena_alloc((size_t)s.len + 1);
    memcpy(p, s.ptr, (size_t)s.len);
    return p;
}

void sb_putn(sb *b, const char *s, const size_t n)
{
    if (b->line == 0) b->line = 1;
    if (b->len + n + 1 > b->cap) {
        // Most builders hold a name or a line of code, so a new one takes
        // little: the arena never frees, and code that makes thousands would
        // otherwise take pages for a few bytes each.
        size_t cap = b->cap ? b->cap * 2 : 64;
        while (cap < b->len + n + 1) cap *= 2;
        b->data = vec_grow(b->data, b->cap, cap, 1);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') b->line++;
    }
}

void sb_put(sb *b, const char *s)
{
    sb_putn(b, s, strlen(s));
}

void sb_printf(sb *b, const char *fmt, ...)
{
    char stack[1024];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(stack, sizeof stack, fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof stack) {
        sb_putn(b, stack, (size_t)n);
        return;
    }
    char *heap = malloc((size_t)n + 1);
    va_start(args, fmt);
    vsnprintf(heap, (size_t)n + 1, fmt, args);
    va_end(args);
    sb_putn(b, heap, (size_t)n);
    free(heap);
}

// ---------------------------------------------------------------------------
// Diagnostics

static const source **sources; // As many as the program has files: it grows
static int source_count;
static int source_room;
static int error_count;
static diag_sink sink;
static void *sink_user;
static diag_suggestion_sink suggestion_sink;
static void *suggestion_user;

// With TIDEC_TIMES set in the environment, says on stderr how long each phase
// of a compile took: the time since the last call, under `what`.
void diag_phase(const char *what)
{
    static int enabled = -1;
    static struct timespec last;
    if (enabled < 0) {
        const char *set = getenv("TIDEC_TIMES");
        enabled = set && set[0] && set[0] != '0';
        timespec_get(&last, TIME_UTC);
    }
    if (!enabled) return;
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    const double ms = (double)(now.tv_sec - last.tv_sec) * 1e3 + (double)(now.tv_nsec - last.tv_nsec) * 1e-6;
    if (what) fprintf(stderr, "tidec: %-22s %9.1f ms\n", what, ms);
    last = now;
}

void diag_reset(void)
{
    source_count = 0;
    error_count = 0;
}

void diag_add_source(source *src)
{
    if (source_count == source_room) {
        source_room = source_room ? source_room * 2 : 256;
        sources = realloc(sources, (size_t)source_room * sizeof *sources);
        if (!sources) {
            fprintf(stderr, "tidec: out of memory\n");
            exit(1);
        }
    }
    src->file = source_count;
    sources[source_count++] = src;
}

const source *diag_source(const int file)
{
    return file >= 0 && file < source_count ? sources[file] : NULL;
}

int diag_source_count(void)
{
    return source_count;
}

void diag_set_sink(const diag_sink new_sink, void *user)
{
    sink = new_sink;
    sink_user = user;
}

void diag_set_suggestion_sink(const diag_suggestion_sink new_sink, void *user)
{
    suggestion_sink = new_sink;
    suggestion_user = user;
}

static void to_sink(const diag_severity severity, const loc at, const char *fmt, va_list args)
{
    char message[1024];
    vsnprintf(message, sizeof message, fmt, args);
    sink(sink_user, severity, at, message);
}

// Whether diagnostics are colored: stderr is a terminal that takes escape
// sequences, and NO_COLOR isn't set (no-color.org). Never for a file or a
// pipe, so what tools and tests read is plain text.
bool diag_colored(void)
{
    static int enabled = -1;
    if (enabled >= 0) return enabled;
    enabled = 0;
    const char *off = getenv("NO_COLOR");
    if (off && off[0]) return false;
#ifdef _WIN32
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    if (GetConsoleMode(err, &mode)) {
        enabled = (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
                  SetConsoleMode(err, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    }
#else
    const char *term = getenv("TERM");
    enabled = isatty(STDERR_FILENO) && term && strcmp(term, "dumb") != 0;
#endif
    return enabled;
}

#define PLAIN "\x1b[0m"
#define BOLD "\x1b[1m"
#define DIM "\x1b[2m"
#define CYAN "\x1b[36m"

static const char *paint(const char *color)
{
    return diag_colored() ? color : "";
}

// An error's red, a warning's yellow.
static const char *kind_color(const char *kind)
{
    return kind[0] == 'e' ? "\x1b[1;31m" : "\x1b[1;33m";
}

// A file's path as a diagnostic says it: from the folder the compiler was
// run in when the file is under it, and whole otherwise.
static const char *shown_path(const char *path)
{
    static char here[4096];
    static size_t here_len;
    static bool asked;
    if (!asked) {
        asked = true;
#ifdef _WIN32
        const DWORD n = GetCurrentDirectoryA(sizeof here, here);
        here_len = n < sizeof here ? n : 0;
#else
        here_len = getcwd(here, sizeof here) ? strlen(here) : 0;
#endif
        while (here_len > 0 && (here[here_len - 1] == '/' || here[here_len - 1] == '\\')) here_len--;
    }
    if (here_len == 0 || strlen(path) <= here_len + 1) return path;
    for (size_t i = 0; i < here_len; i++) {
        char a = path[i];
        char b = here[i];
#ifdef _WIN32
        a = a == '\\' ? '/' : (char)tolower((unsigned char)a);
        b = b == '\\' ? '/' : (char)tolower((unsigned char)b);
#endif
        if (a != b) return path;
    }
    const char after = path[here_len];
    if (after != '/' && after != '\\') return path;
    return path + here_len + 1;
}

// Prints the offending source line with a caret under the column, in the
// color of what it's for.
static void print_excerpt(const source *src, const loc at, const char *color)
{
    const char *p = src->text;
    const char *end = src->text + src->len;
    if (src->len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3; // Byte-order mark.
    for (int line = 1; line < at.line && p < end; p++) {
        if (*p == '\n') line++;
    }
    const char *line_end = p;
    while (line_end < end && *line_end != '\n' && *line_end != '\r') line_end++;

    fprintf(stderr, "%s%5d |%s %.*s\n%s      |%s ", paint(DIM), at.line, paint(PLAIN), (int)(line_end - p), p, paint(DIM),
            paint(PLAIN));
    for (int i = 0; i < at.col - 1 && p + i < line_end; i++) fputc(p[i] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "%s^%s\n", paint(color), paint(PLAIN));
}

// Not const: va_list is an array on some targets, and vfprintf takes it as is.
static void report(const loc at, const char *kind, const char *fmt, va_list args)
{
    const source *src = diag_source(at.file);
    const char *color = kind_color(kind);
    if (src) fprintf(stderr, "%s%s:%d:%d:%s ", paint(BOLD), shown_path(src->path), at.line, at.col, paint(PLAIN));
    else fputs("tidec: ", stderr);
    fprintf(stderr, "%s%s:%s %s", paint(color), kind, paint(PLAIN), paint(BOLD));
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "%s\n", paint(PLAIN));
    if (src && at.line > 0) print_excerpt(src, at, color);
}

void diag_error(const loc at, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (sink) to_sink(DIAG_ERROR, at, fmt, args);
    else report(at, "error", fmt, args);
    va_end(args);
    error_count++;
}

void diag_warning(const loc at, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (sink) to_sink(DIAG_WARNING, at, fmt, args);
    else report(at, "warning", fmt, args);
    va_end(args);
}

void diag_info(const loc at, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (sink) to_sink(DIAG_INFO, at, fmt, args);
    else report(at, "info", fmt, args);
    va_end(args);
}

// Not const: va_list is an array on some targets, and vfprintf takes it as is.
static void note(const loc at, const char *fmt, va_list args)
{
    if (sink) {
        to_sink(DIAG_NOTE, at, fmt, args);
    } else {
        fprintf(stderr, "      %s= note:%s ", paint(CYAN), paint(PLAIN));
        vfprintf(stderr, fmt, args);
        fputc('\n', stderr);
    }
}

void diag_note(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    note((loc){0, 0, 0}, fmt, args);
    va_end(args);
}

void diag_note_at(const loc at, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    note(at, fmt, args);
    va_end(args);
}

int diag_error_count(void)
{
    return error_count;
}

// ---------------------------------------------------------------------------
// Suggestions

suggestion suggest_start(const str wrong)
{
    return (suggestion){wrong, {NULL, 0}, 1 << 30};
}

static char lower(const char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Edits (insert, delete, replace, swap two neighbors) between two names,
// ignoring case. Names are short, so the table fits on the stack.
static int distance(const str a, const str b)
{
    enum { MAX = 64 };
    if (a.len >= MAX || b.len >= MAX) return 1 << 30;
    int d[MAX][MAX];
    for (int i = 0; i <= a.len; i++) d[i][0] = i;
    for (int j = 0; j <= b.len; j++) d[0][j] = j;
    for (int i = 1; i <= a.len; i++) {
        for (int j = 1; j <= b.len; j++) {
            const int cost = lower(a.ptr[i - 1]) != lower(b.ptr[j - 1]);
            int best = d[i - 1][j - 1] + cost;
            if (d[i - 1][j] + 1 < best) best = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < best) best = d[i][j - 1] + 1;
            if (i > 1 && j > 1 && lower(a.ptr[i - 1]) == lower(b.ptr[j - 2]) && lower(a.ptr[i - 2]) == lower(b.ptr[j - 1])
                && d[i - 2][j - 2] + 1 < best) {
                best = d[i - 2][j - 2] + 1;
            }
            d[i][j] = best;
        }
    }
    return d[a.len][b.len];
}

void suggest_consider(suggestion *s, const str candidate)
{
    if (candidate.len == 0 || str_eq(candidate, s->wrong)) return;
    const int d = distance(s->wrong, candidate);
    if (d < s->distance) {
        s->distance = d;
        s->best = candidate;
    }
}

void suggest_consider_c(suggestion *s, const char *candidate)
{
    suggest_consider(s, str_from(candidate));
}

bool suggest_close(const suggestion *s)
{
    // One typo in a short name, two in a longer one. Beyond that it's a guess.
    const int allowed = s->wrong.len <= 4 ? 1 : 2;
    return s->best.len > 0 && s->distance <= allowed;
}

void suggest_note(const suggestion *s)
{
    if (!suggest_close(s)) return;
    diag_note("did you mean '" STR_FMT "'?", STR_ARG(s->best));
    if (suggestion_sink) suggestion_sink(suggestion_user, s->wrong, s->best);
}

void suggest_replacement(const str wrong, const char *instead)
{
    if (suggestion_sink) suggestion_sink(suggestion_user, wrong, str_from(instead));
}

int path_compare(const char *a, const char *b)
{
    for (;; a++, b++) {
        const char x = *a == '\\' ? '/' : *a;
        const char y = *b == '\\' ? '/' : *b;
        if (x != y || x == '\0') return (unsigned char)x - (unsigned char)y;
    }
}
