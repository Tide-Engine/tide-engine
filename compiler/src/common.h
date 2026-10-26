#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Memory. Everything comes from one arena and is never freed individually.
// tidec runs once and exits; the language server resets the arena before each
// analysis, so nothing the compiler allocates may outlive one.

void *arena_alloc(size_t size); // Zeroed.
void arena_reset(void);         // Frees everything arena_alloc returned.

// A temporary allocation scope. Rewind invalidates everything allocated
// after the checkpoint: none of it may be kept in an object that survives.
// A checkpoint cannot outlive arena_reset or a rewind past its position.
typedef struct arena_mark {
    void *block;
    size_t used;
} arena_mark;

arena_mark arena_checkpoint(void);
void arena_rewind(arena_mark mark);

#define NEW(T) ((T *)arena_alloc(sizeof(T)))

// Growable array. Declare with `VEC(T) name;` zero-initialized.
#define VEC(T)                                                                 \
    struct {                                                                   \
        T *items;                                                              \
        int count;                                                             \
        int cap;                                                               \
    }

#define vec_push(v, x)                                                         \
    do {                                                                       \
        if ((v).count == (v).cap) {                                            \
            const int vec_old_cap_ = (v).cap;                                  \
            (v).cap = (v).cap ? (v).cap * 2 : 8;                               \
            (v).items = vec_grow((v).items, (size_t)vec_old_cap_, (size_t)(v).cap, sizeof(*(v).items)); \
        }                                                                      \
        (v).items[(v).count++] = (x);                                          \
    } while (0)

// A bigger copy of `items`, from the arena.
void *vec_grow(void *items, size_t old_cap, size_t new_cap, size_t elem_size);

// ---------------------------------------------------------------------------
// Strings. Slices point into the source buffer or the arena.

typedef struct str {
    const char *ptr;
    int len;
} str;

#define STR_FMT "%.*s"
#define STR_ARG(s) (s).len, (s).ptr

str str_from(const char *cstr);
bool str_eq(str a, str b);
bool str_eq_c(str a, const char *cstr);
bool str_starts_with_c(str a, const char *prefix);
char *str_to_cstr(str s); // Arena copy, NUL-terminated.

// String builder that also counts lines, for #line directives.
typedef struct sb {
    char *data;
    size_t len;
    size_t cap;
    int line; // 1-based line the next character lands on.
} sb;

void sb_put(sb *b, const char *s);
void sb_putn(sb *b, const char *s, size_t n);
void sb_printf(sb *b, const char *fmt, ...);

// ---------------------------------------------------------------------------
// Diagnostics.

typedef struct loc {
    int line;
    int col;
    int file; // Which source file: the index diag_add_source gave it
} loc;

typedef struct source {
    const char *path; // As given on the command line.
    const char *text;
    size_t len;
    int file;         // Set by diag_add_source
    const char *package; // The package the file is in (its name, which is its namespace); NULL for the game's own
} source;

typedef enum diag_severity {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE, // Belongs to the previous error or warning; `at` is zero, or where it points.
    DIAG_INFO, // A standalone informational diagnostic, without affecting compilation.
} diag_severity;

// Receives diagnostics instead of stderr, for tools such as the language server.
typedef void (*diag_sink)(void *user, diag_severity severity, loc at, const char *message);

// Forgets every source file and error, before compiling a program.
void diag_reset(void);

// With TIDEC_TIMES set in the environment, says on stderr how long the phase
// that just ended took (the time since the last call), under `what`; NULL
// starts the clock.
void diag_phase(const char *what);
// Registers a source file and sets src->file: locations in it carry that index.
void diag_add_source(source *src);
const source *diag_source(int file);
int diag_source_count(void);
void diag_set_sink(diag_sink sink, void *user); // NULL prints to stderr again.
void diag_error(loc at, const char *fmt, ...);
void diag_warning(loc at, const char *fmt, ...);
void diag_info(loc at, const char *fmt, ...);
void diag_note(const char *fmt, ...); // Attaches to the previous error or warning.
// A note about another place in the code, which its text names too: a sink
// gets `at`, where diag_note gives it none.
void diag_note_at(loc at, const char *fmt, ...);
int diag_error_count(void);
// Whether what's printed to stderr is colored: it's a terminal that takes
// escape sequences, and NO_COLOR isn't set. For what else writes there, like
// a C compiler tide runs.
bool diag_colored(void);

// "did you mean ...?" for a misspelled name: offer every name that would have
// been right, then suggest_note adds a note naming the closest one, if any is
// close enough (the same letters in another case, or a typo or two away).
typedef struct suggestion {
    str wrong;
    str best;
    int distance;
    // The first name with `wrong` as a whole word of it, for when none is
    // close: `right` for rightArrow, `shift` for leftShift. One it starts
    // (word_rank 0) wins over one where it's a later word (1).
    str word;
    int word_rank;
} suggestion;

suggestion suggest_start(str wrong);
void suggest_consider(suggestion *s, str candidate);
void suggest_consider_c(suggestion *s, const char *candidate);
void suggest_note(const suggestion *s);
// Whether the closest name offered (`best`) is close enough to suggest, for a
// "did you mean" that isn't a diagnostic's note.
bool suggest_close(const suggestion *s);

// What to write in place of `wrong` (a slice of the source), which the last
// diagnostic's note said in its own words: editors offer the change, as they
// do a "did you mean".
void suggest_replacement(str wrong, const char *instead);

// Receives the name each "did you mean" note suggests, after the note, with
// the name written (a slice of the source, where it's the source's): for
// editors, to offer the change. The note belongs to the previous diagnostic.
typedef void (*diag_suggestion_sink)(void *user, str wrong, str best);
void diag_set_suggestion_sink(diag_suggestion_sink sink, void *user); // NULL for none

// The order of a game's files, which decides the default order of its systems:
// byte by byte, with backslashes as forward slashes, so it's the same on every
// platform. Returns like strcmp.
int path_compare(const char *a, const char *b);
