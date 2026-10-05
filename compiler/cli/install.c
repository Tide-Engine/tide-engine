#include "install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"

bool tide_installation_whole(const char *root, const char *version)
{
    char *path = path_join(root, "VERSION");
    char *text = sys_read_file(path, NULL);
    free(path);
    if (!text) return false;
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ')) text[--n] = '\0';
    const bool whole = strcmp(text, version) == 0;
    free(text);
    return whole;
}

typedef struct install {
    const char *trash;
    long long stamp; // In the trash's names, so they're not an earlier upgrade's that are still there
    int discarded;
    bool changed; // Something of the installation is the package's now, or gone
    char *failed;
    size_t failed_size;
} install;

static bool fail(const install *s, const char *path)
{
    if (s->failed && s->failed_size > 0 && !s->failed[0]) snprintf(s->failed, s->failed_size, "%s", path);
    return false;
}

// The names in a folder, taken before any of them moves, in one order on
// every file system.
typedef struct names {
    char **items;
    int count;
} names;

static void add_name(void *user, const char *name, const bool is_dir)
{
    (void)is_dir;
    names *n = user;
    n->items = realloc(n->items, sizeof(char *) * (size_t)(n->count + 1));
    char *copy = n->items ? malloc(strlen(name) + 1) : NULL;
    if (!copy) abort();
    strcpy(copy, name);
    n->items[n->count++] = copy;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static names list_names(const char *dir)
{
    names n = {0};
    sys_list(dir, add_name, &n);
    if (n.count > 1) qsort(n.items, (size_t)n.count, sizeof(char *), by_name);
    return n;
}

static bool has_name(const names *n, const char *name)
{
    for (int i = 0; i < n->count; i++) {
        if (strcmp(n->items[i], name) == 0) return true;
    }
    return false;
}

static void free_names(names *n)
{
    for (int i = 0; i < n->count; i++) free(n->items[i]);
    free(n->items);
}

// Moves `path` to the trash, and says where it is there; NULL if it can't
// move, as when another program has it, or something in it, open.
static char *discard(install *s, const char *path)
{
    for (;;) {
        char name[64];
        snprintf(name, sizeof name, "%lld-%d", s->stamp, s->discarded++);
        char *to = path_join(s->trash, name);
        if (!sys_exists(to)) {
            if (sys_rename(path, to)) return to;
            free(to);
            return NULL;
        }
        free(to);
    }
}

static bool replace(install *s, const char *from, const char *to);

// Replaces what's in the folder `to` with what's in `from`, one by one, and
// discards what `from` has none of.
static bool replace_inside(install *s, const char *from, const char *to)
{
    names fresh = list_names(from);
    bool ok = true;
    for (int i = 0; ok && i < fresh.count; i++) {
        char *child_from = path_join(from, fresh.items[i]);
        char *child_to = path_join(to, fresh.items[i]);
        ok = replace(s, child_from, child_to);
        free(child_from);
        free(child_to);
    }
    names there = ok ? list_names(to) : (names){0};
    for (int i = 0; ok && i < there.count; i++) {
        if (has_name(&fresh, there.items[i])) continue;
        char *stale = path_join(to, there.items[i]);
        char *old = discard(s, stale);
        if (old) s->changed = true;
        else ok = fail(s, stale);
        free(old);
        free(stale);
    }
    free_names(&fresh);
    free_names(&there);
    return ok;
}

static bool replace(install *s, const char *from, const char *to)
{
    // All of it at once, when both can move.
    char *old = sys_exists(to) ? discard(s, to) : NULL;
    if (old || !sys_exists(to)) {
        const bool ok = sys_rename(from, to);
        // Back as it was, when the new one can't come.
        if (ok || (old && !sys_rename(old, to))) s->changed = true;
        free(old);
        if (ok) return true;
    }
    // Another program has one of them open, or something in it. Nothing moves
    // a folder then, but what's in it still moves, one by one.
    if (!sys_is_dir(from) || (sys_exists(to) && !sys_is_dir(to))) return fail(s, to);
    if (!sys_exists(to)) {
        if (!sys_mkdirs(to)) return fail(s, to);
        s->changed = true;
    }
    return replace_inside(s, from, to);
}

// tide and tidels may be running (this very tide, or an editor's tidels).
// Windows can't overwrite or delete them, but it can rename them, so the old
// ones move aside in bin, where tide_cleanup deletes them once they've ended.
static bool replace_program(install *s, const char *from, const char *to)
{
    char aside[1024] = "";
    if (sys_exists(to)) {
        snprintf(aside, sizeof aside, "%s.old-%lld", to, s->stamp);
        if (!sys_rename(to, aside)) return fail(s, to);
    }
    const bool ok = sys_rename(from, to);
    if (ok || (aside[0] && !sys_rename(aside, to))) s->changed = true;
    return ok || fail(s, to);
}

static bool replace_programs(install *s, const char *from, const char *to)
{
    if (!sys_mkdirs(to)) return fail(s, to);
    names programs = list_names(from);
    bool ok = true;
    for (int i = 0; ok && i < programs.count; i++) {
        char *program_from = path_join(from, programs.items[i]);
        char *program_to = path_join(to, programs.items[i]);
        ok = sys_is_dir(program_from) ? replace(s, program_from, program_to) : replace_program(s, program_from, program_to);
        free(program_from);
        free(program_to);
    }
    free_names(&programs);
    return ok;
}

bool tide_install(const char *root, const char *fresh, const char *trash, char *failed, const size_t failed_size)
{
    install s = {.trash = trash, .stamp = (long long)sys_now(), .failed = failed, .failed_size = failed_size};
    if (failed && failed_size > 0) failed[0] = '\0';
    if (!sys_mkdirs(trash)) return fail(&s, trash);

    // From here until the package's VERSION is in, the installation says it
    // isn't whole.
    char *version = path_join(root, "VERSION");
    char *fresh_version = path_join(fresh, "VERSION");
    char *was = sys_exists(version) ? discard(&s, version) : NULL;
    bool ok = was || !sys_exists(version) || fail(&s, version);

    // tide itself goes after everything it builds with, so the tide of an
    // upgrade that stopped is the one that still sees a newer version to
    // upgrade to.
    names entries = ok ? list_names(fresh) : (names){0};
    for (int i = 0; ok && i < entries.count; i++) {
        if (strcmp(entries.items[i], "bin") == 0 || strcmp(entries.items[i], "VERSION") == 0) continue;
        char *from = path_join(fresh, entries.items[i]);
        char *to = path_join(root, entries.items[i]);
        ok = replace(&s, from, to);
        free(from);
        free(to);
    }
    if (ok && has_name(&entries, "bin")) {
        char *from = path_join(fresh, "bin");
        char *to = path_join(root, "bin");
        ok = replace_programs(&s, from, to);
        free(from);
        free(to);
    }
    if (ok) ok = sys_rename(fresh_version, version) || fail(&s, version);
    // Nothing changed: it's still the version it was.
    if (!ok && !s.changed && was) sys_rename(was, version);

    free_names(&entries);
    free(was);
    free(fresh_version);
    free(version);
    return ok;
}
