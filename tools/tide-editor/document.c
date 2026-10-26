#include "document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "folders.h"
#include "packages.h"
#include "sys.h"

typedef struct lexed_file { const source *src; token *tokens; int count; bool syntax_errors; } lexed_file;
static VEC(lexed_file) lexed;

bool editor_source_syntax_errors(const editor_document *d, const source *src)
{
    if (!d->prog || !src) return true;
    for (int i = 0; i < lexed.count; i++) if (lexed.items[i].src == src) return lexed.items[i].syntax_errors;
    return true;
}

token *editor_source_tokens(const editor_document *d, const source *src, int *count)
{
    if (count) *count = 0;
    if (!d->prog || !src) return NULL;
    for (int i = 0; i < lexed.count; i++) if (lexed.items[i].src == src) {
        if (count) *count = lexed.items[i].count;
        return lexed.items[i].tokens;
    }
    return NULL;
}

static bool same_path(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

static char *copy(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) abort();
    memcpy(p, s, n);
    return p;
}

static void history_clear(char **items, int *count)
{
    while (*count) free(items[--*count]);
}

static void history_push(char **items, int *count, char *text)
{
    if (*count == 64) {
        free(items[0]);
        memmove(items, items + 1, 63 * sizeof(*items));
        --*count;
    }
    items[(*count)++] = text;
}

bool editor_dirty(const editor_document *d) { return strcmp(d->text, d->saved) != 0; }

static size_t offset(const source *s, const loc at)
{
    const char *p = s->text, *end = p + s->len;
    if (s->len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
    for (int line = 1; line < at.line && p < end; line++) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        p = nl ? nl + 1 : end;
    }
    const size_t col = at.col > 0 ? (size_t)at.col - 1 : 0;
    return (size_t)(p - s->text) + (col < (size_t)(end - p) ? col : (size_t)(end - p));
}

static int token_at(const editor_document *d, const loc at)
{
    for (int i = 0; i < d->token_count; i++)
        if (d->tokens[i].at.line == at.line && d->tokens[i].at.col == at.col) return i;
    return -1;
}

static int closing(const editor_document *d, const int open)
{
    int depth = 0;
    for (int i = open; i < d->token_count; i++) {
        if (d->tokens[i].kind == T_LBRACE) depth++;
        if (d->tokens[i].kind == T_RBRACE && --depth == 0) return i;
    }
    return -1;
}

static void collect(void *user, const diag_severity severity, const loc at, const char *message)
{
    editor_document *d = user;
    if (severity == DIAG_ERROR) d->errors++;
    if (severity == DIAG_WARNING) d->warnings++;
    const source *s = at.line ? diag_source(at.file) : NULL;
    if (severity == DIAG_ERROR && d->parsing && s && same_path(s->path, d->path)) d->syntax_errors = true;
    sb b = {0};
    if (s) {
        const size_t root = strlen(d->project);
        const char *path = strncmp(s->path, d->project, root) == 0 && s->path[root] == '/' ? s->path + root + 1 : s->path;
        sb_printf(&b, "%s:%d:%d: ", path, at.line, at.col);
    }
    sb_printf(&b, "%s%s", severity == DIAG_ERROR ? "error: " : severity == DIAG_WARNING ? "warning: " : "", message);
    vec_push(d->diagnostics, b.data);
}

static void package_report(void *user, const char *where, const int line, const char *message, const char *note)
{
    editor_document *d = user;
    d->errors++;
    sb b = {0};
    sb_printf(&b, "%s:%d: %s%s%s", where, line, message, note ? "; " : "", note ? note : "");
    vec_push(d->diagnostics, b.data);
}

typedef struct inputs {
    VEC(char *) paths;
    editor_document *d;
    const char *package;
    uint64_t stamp;
    bool web;
} inputs;

static void found(void *user, const char *path)
{
    inputs *in = user;
    vec_push(in->paths, str_to_cstr(str_from(path)));
}

static int compare(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static uint64_t stamp_next(const uint64_t h, const uint64_t v) { return (h ^ v) * UINT64_C(1099511628211); }

static void read_folder(inputs *in, const char *folder)
{
    in->paths.count = 0;
    char *with_slash = path_join(folder, "");
    folder_find_own(with_slash, ".tide", found, in);
    folder_find_own(with_slash, ".tprefab", found, in);
    free(with_slash);
    if (in->paths.count > 1) qsort(in->paths.items, (size_t)in->paths.count, sizeof(char *), compare);
    char *manifest = path_join(folder, PACKAGES_FILE);
    in->stamp = stamp_next(in->stamp, sys_file_stamp(manifest));
    free(manifest);
    if (in->package) lex_define_package(in->package);
    for (int i = 0; i < in->paths.count; i++) {
        const char *path = in->paths.items[i];
        for (const unsigned char *p = (const unsigned char *)path; *p; p++) in->stamp = stamp_next(in->stamp, *p);
        in->stamp = stamp_next(in->stamp, sys_file_stamp(path));
        size_t len = 0;
        const bool own_file = same_path(path, in->d->path);
        char *text = own_file ? copy(in->d->text) : sys_read_file(path, &len);
        if (!text) {
            package_report(in->d, path, 0, "cannot read source file", NULL);
            continue;
        }
        len = strlen(text);
        source *s = NEW(source);
        *s = (source){str_to_cstr(str_from(path)), str_to_cstr(str_from(text)), len, 0, in->package};
        free(text);
        diag_add_source(s);
        const int before_errors = in->d->errors;
        token *t = lex_all(s);
        int count = 0; while (t[count].kind != T_EOF) count++;
        const int cache = lexed.count;
        vec_push(lexed, ((lexed_file){.src = s, .tokens = t, .count = count}));
        if (own_file) {
            // Retain the filesystem's filename spelling when replacing it.
            free(in->d->path); in->d->path = copy(path);
            in->d->src = s;
            in->d->tokens = t;
            while (t[in->d->token_count].kind != T_EOF) in->d->token_count++;
        }
        parse_file(in->d->prog, s, t, true);
        lexed.items[cache].syntax_errors = in->d->errors > before_errors;
        in->web |= web_prelude_wanted(s->text, s->len);
    }
}

static void authored_nodes(editor_document *d, prefab_node *raw, str path, const int parent)
{
    int n = -1;
    for (int i = 0; i < d->nodes.count; i++) if (str_eq(d->nodes.items[i].path, path)) n = i;
    if (n < 0) {
        n = d->nodes.count;
        vec_push(d->nodes, ((editor_node){.path = path, .parent = parent, .open = -1, .close = -1}));
    }
    editor_node *node = &d->nodes.items[n];
    node->authored = raw;
    int open = token_at(d, raw->at);
    while (open >= 0 && open < d->token_count && d->tokens[open].kind != T_LBRACE) open++;
    node->open = open < d->token_count ? open : -1;
    node->close = node->open >= 0 ? closing(d, node->open) : -1;
    for (int i = 0; i < raw->children.count; i++) {
        sb b = {0};
        if (path.len) sb_printf(&b, STR_FMT ".", STR_ARG(path));
        sb_printf(&b, STR_FMT, STR_ARG(raw->children.items[i]->name));
        authored_nodes(d, raw->children.items[i], str_from(b.data), n);
    }
}

void editor_select_prefab(editor_document *d, int index)
{
    d->nodes.count = 0;
    d->active = index >= 0 && index < d->prefabs.count ? index : 0;
    if (!d->prefabs.count) return;
    decl *p = d->prefabs.items[d->active];
    if (p->prefab_checked) {
        for (int i = 0; i < p->prefab_checked->entities.count; i++) {
            prefab_entity *e = &p->prefab_checked->entities.items[i];
            vec_push(d->nodes, ((editor_node){.path = e->path, .parent = e->parent, .effective = e->spawn, .open = -1, .close = -1}));
        }
    }
    authored_nodes(d, p->prefab_root, (str){0}, -1);
    // An authored block may be a value override of an inherited child. Keep
    // provenance independent of whether such a block has been materialized.
    for (int i = 1; i < d->nodes.count; i++) {
        editor_node *n = &d->nodes.items[i];
        n->inherited = !n->authored;
        for (int a = n->parent; a >= 0; a = d->nodes.items[a].parent) {
            const editor_node *ancestor = &d->nodes.items[a];
            const decl *base = ancestor->authored ? ancestor->authored->base_decl : NULL;
            if (!base || !base->prefab_checked) continue;
            const int skip = ancestor->path.len ? ancestor->path.len + 1 : 0;
            const str relative = {n->path.ptr + skip, n->path.len - skip};
            for (int k = 1; k < base->prefab_checked->entities.count; k++)
                if (str_eq(relative, base->prefab_checked->entities.items[k].path)) n->inherited = true;
        }
    }
}

void editor_refresh(editor_document *d)
{
    char *active = d->prefabs.count && d->active < d->prefabs.count ? copy(str_to_cstr(d->prefabs.items[d->active]->qualified)) : NULL;
    arena_reset();
    d->revision++;
    lexed.items = NULL; lexed.count = lexed.cap = 0;
    diag_reset();
    d->errors = d->warnings = d->token_count = 0;
    d->syntax_errors = false; d->parsing = true;
    d->diagnostics.count = d->diagnostics.cap = 0; d->diagnostics.items = NULL;
    d->prefabs.count = d->prefabs.cap = 0; d->prefabs.items = NULL;
    d->nodes.count = d->nodes.cap = 0; d->nodes.items = NULL;
    d->src = NULL; d->tokens = NULL;
    d->prog = program_new();
    d->prog->game_folder = d->project;
    d->prog->asset_metadata_only = true;
    d->prog->editor_authoring = true;
    diag_set_sink(collect, d);
    lex_symbols_reset(); lex_define("DEBUG");
    game_packages packages = {0};
    packages_resolve(d->project, &packages, NULL, package_report, d);
    // Define all packages before lexing any file, just as the game build does.
    for (int i = 0; i < packages.count; i++) lex_define_package(packages.items[i].name);
    inputs in = {.d = d, .stamp = UINT64_C(14695981039346656037)};
    for (int i = 0; i < packages.count; i++) {
        in.package = str_to_cstr(str_from(packages.items[i].name));
        read_folder(&in, packages.items[i].folder);
    }
    in.package = packages.file.name ? str_to_cstr(str_from(packages.file.name)) : NULL;
    read_folder(&in, d->project);
    packages_free(&packages);
    d->stamp = in.stamp;
    if (in.web) {
        source *s = NEW(source);
        s->path = WEB_PRELUDE_PATH; s->text = web_prelude(&s->len);
        const int before_errors = d->errors;
        diag_add_source(s); token *t = lex_all(s); int count = 0;
        while (t[count].kind != T_EOF) count++;
        const int cache = lexed.count;
        vec_push(lexed, ((lexed_file){.src = s, .tokens = t, .count = count})); parse_file(d->prog, s, t, true);
        lexed.items[cache].syntax_errors = d->errors > before_errors;
    }
    d->parsing = false;
    check(d->prog);
    diag_set_sink(NULL, NULL);
    for (int i = 0; i < d->prog->decls.count; i++) {
        decl *p = d->prog->decls.items[i];
        if (p->kind == DECL_PREFAB && p->unit && p->unit->src == d->src) vec_push(d->prefabs, p);
    }
    d->active = 0;
    for (int i = 0; active && i < d->prefabs.count; i++) if (str_eq_c(d->prefabs.items[i]->qualified, active)) d->active = i;
    free(active);
    editor_select_prefab(d, d->active);
    if (!d->src) snprintf(d->message, sizeof d->message, "File is outside this project's source boundary. Use --project <folder>.");
}

bool editor_open(editor_document *d, const char *path, const char *project)
{
    memset(d, 0, sizeof *d);
    d->path = path_absolute(path);
    d->text = sys_read_file(d->path, NULL);
    if (!d->text) { snprintf(d->message, sizeof d->message, "Cannot read %s", path); return false; }
    if (project) d->project = path_absolute(project);
    else {
        d->project = path_dir(d->path);
        if (strlen(d->project) == 2 && d->project[1] == ':') {
            char *drive = path_join(d->project, ""); free(d->project); d->project = drive;
        }
        char *candidate = copy(d->project);
        for (;;) {
            char *file = path_join(candidate, PACKAGES_FILE);
            const bool exists = sys_exists(file);
            free(file);
            if (exists) { free(d->project); d->project = copy(candidate); break; }
            if (strcmp(candidate, "/") == 0 || (strlen(candidate) == 3 && candidate[1] == ':' && candidate[2] == '/')) break;
            char *parent = path_dir(candidate);
            if (strlen(parent) == 2 && parent[1] == ':') { char *drive = path_join(parent, ""); free(parent); parent = drive; }
            if (!parent[0]) { free(parent); parent = copy("/"); }
            if (strcmp(parent, candidate) == 0 || !parent[0]) { free(parent); break; }
            free(candidate); candidate = parent;
        }
        free(candidate);
    }
    d->saved = copy(d->text);
    editor_refresh(d);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)d->path; *p; p++) {
        unsigned char c = *p;
#ifdef _WIN32
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
#endif
        hash = stamp_next(hash, c);
    }
    char name[64]; snprintf(name, sizeof name, ".tide/editor/%016llx.draft", (unsigned long long)hash);
    d->draft_path = path_join(d->project, name);
    char *draft = sys_read_file(d->draft_path, NULL);
    const char *prefix = "TIDE-EDITOR-DRAFT 1\n";
    if (draft && strncmp(draft, prefix, strlen(prefix)) == 0) {
        char *end = NULL; const unsigned long long n = strtoull(draft + strlen(prefix), &end, 10);
        if (*end == '\n' && n <= strlen(end + 1)) {
            d->recovery_base = malloc((size_t)n + 1); if (!d->recovery_base) abort();
            memcpy(d->recovery_base, end + 1, (size_t)n); d->recovery_base[n] = '\0';
            d->recovery = copy(end + 1 + (size_t)n);
        }
    }
    free(draft);
    if (d->recovery && strcmp(d->recovery, d->saved) == 0) {
        free(d->recovery); free(d->recovery_base); d->recovery = d->recovery_base = NULL; sys_remove(d->draft_path);
    }
    return d->src != NULL;
}

void editor_close(editor_document *d)
{
    free(d->path); free(d->project); free(d->text); free(d->saved); free(d->draft_path); free(d->recovery); free(d->recovery_base);
    history_clear(d->undo, &d->undo_count); history_clear(d->redo, &d->redo_count);
    arena_reset(); memset(d, 0, sizeof *d);
}

static void keep_draft(editor_document *d)
{
    if (!editor_dirty(d)) { sys_remove(d->draft_path); return; }
    char *folder = path_dir(d->draft_path);
    const bool exists = sys_mkdirs(folder); free(folder);
    if (exists) {
        char *work = path_join(d->project, ".tide"), *ignore = path_join(work, ".gitignore");
        if (!sys_exists(ignore)) sys_write_text(ignore, "*\n");
        sys_hide(work); free(work); free(ignore);
    }
    char *temp = malloc(strlen(d->draft_path) + 40);
    if (!temp) abort();
    snprintf(temp, strlen(d->draft_path) + 40, "%s-%u.tmp", d->draft_path, sys_pid());
    sb draft = {0}; sb_printf(&draft, "TIDE-EDITOR-DRAFT 1\n%zu\n", strlen(d->saved)); sb_put(&draft, d->saved); sb_put(&draft, d->text);
    const bool ok = exists && sys_write_file(temp, draft.data, draft.len) && sys_rename(temp, d->draft_path);
    if (!ok) { sys_remove(temp); snprintf(d->message, sizeof d->message, "Draft recovery could not be written. Save your work before closing."); }
    free(temp);
}

static uint64_t reference_count(const expr *e)
{
    if (!e) return 0;
    if (e->prefab_reference) return 1;
    uint64_t count = reference_count(e->object) + reference_count(e->lhs) + reference_count(e->rhs) + reference_count(e->cond);
    for (int i = 0; i < e->args.count; i++) count += reference_count(e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) count += reference_count(e->inits.items[i].value);
    return count;
}

static uint64_t project_reference_count(const editor_document *d)
{
    uint64_t count = 0;
    for (int i = 0; i < d->prog->decls.count; i++) {
        const decl *p = d->prog->decls.items[i];
        if (p->kind != DECL_PREFAB || !p->prefab_checked) continue;
        for (int n = 0; n < p->prefab_checked->entities.count; n++) count += reference_count(p->prefab_checked->entities.items[n].spawn);
    }
    return count;
}

static bool patch_checked(editor_document *d, const size_t from, const size_t to, const char *replacement, const bool checked, const uint64_t expected_refs)
{
    if (d->syntax_errors || !d->src || from > to || to > strlen(d->text)) {
        snprintf(d->message, sizeof d->message, "Fix this document's syntax in your code editor, then Reload.");
        return false;
    }
    const size_t n = strlen(d->text), add = strlen(replacement);
    if (add > SIZE_MAX - n - 1) return false;
    char *next = malloc(n - (to - from) + add + 1);
    if (!next) abort();
    memcpy(next, d->text, from); memcpy(next + from, replacement, add);
    memcpy(next + from + add, d->text + to, n - to + 1);
    if (strcmp(next, d->text) == 0) { free(next); return false; }
    char *before = d->text;
    d->text = next;
    d->message[0] = '\0';
    editor_refresh(d);
    const bool bindings_changed = expected_refs != UINT64_MAX && project_reference_count(d) != expected_refs;
    if (d->syntax_errors || (checked && d->errors) || bindings_changed) {
        char reason[700]; snprintf(reason, sizeof reason, "Edit rejected: %.650s", bindings_changed ? "entity paths would capture or lose another expression's binding" : d->diagnostics.count ? d->diagnostics.items[0] : "invalid Tide syntax");
        free(d->text); d->text = before; editor_refresh(d);
        snprintf(d->message, sizeof d->message, "%s", reason); return false;
    }
    history_push(d->undo, &d->undo_count, before);
    history_clear(d->redo, &d->redo_count); keep_draft(d);
    return true;
}

static bool patch(editor_document *d, const size_t from, const size_t to, const char *replacement)
{
    return patch_checked(d, from, to, replacement, false, UINT64_MAX);
}

bool editor_undo(editor_document *d, const bool redo)
{
    char **from = redo ? d->redo : d->undo, **to = redo ? d->undo : d->redo;
    int *n = redo ? &d->redo_count : &d->undo_count, *m = redo ? &d->undo_count : &d->redo_count;
    if (!*n) return false;
    history_push(to, m, d->text); d->text = from[--*n];
    editor_refresh(d); keep_draft(d); return true;
}

bool editor_save(editor_document *d)
{
    editor_refresh(d);
    if (d->syntax_errors || !d->src) { snprintf(d->message, sizeof d->message, "Save blocked: fix this document's syntax and keep it inside the project."); return false; }
    size_t disk_len = 0;
    const size_t saved_len = strlen(d->saved);
    char *disk = sys_read_file(d->path, &disk_len);
    const bool same = disk && disk_len == saved_len && memcmp(disk, d->saved, saved_len) == 0;
    free(disk);
    if (!same) {
        d->conflict = true;
        snprintf(d->message, sizeof d->message, "File changed outside Tide. Save refused; keep your draft or discard it with Reload.");
        return false;
    }
    char suffix[64]; snprintf(suffix, sizeof suffix, ".tide-editor-%u.tmp", sys_pid());
    char *temp = malloc(strlen(d->path) + strlen(suffix) + 1);
    if (!temp) abort();
    strcpy(temp, d->path); strcat(temp, suffix);
    bool ok = sys_write_text(temp, d->text);
    // Check again after writing, before replacing the source.
    disk = sys_read_file(d->path, &disk_len);
    ok = ok && disk && disk_len == saved_len && memcmp(disk, d->saved, saved_len) == 0;
    free(disk);
    if (ok) ok = sys_rename(temp, d->path);
    if (!ok) sys_remove(temp);
    free(temp);
    if (ok) {
        free(d->saved); d->saved = copy(d->text); d->conflict = false;
        sys_remove(d->draft_path);
        // Publish the replacement's filesystem stamp now. Otherwise the next
        // half-second poll reanalyses our own Save and cancels a fresh gizmo drag.
        editor_poll(d);
        snprintf(d->message, sizeof d->message, "Saved %s", path_base(d->path));
    } else snprintf(d->message, sizeof d->message, "Save failed or source changed; original file kept.");
    return ok;
}

bool editor_reload(editor_document *d, const bool discard)
{
    if (editor_dirty(d) && !discard) {
        snprintf(d->message, sizeof d->message, "Unsaved changes. Use Discard + reload to replace this draft."); return false;
    }
    char *text = sys_read_file(d->path, NULL);
    if (!text) { snprintf(d->message, sizeof d->message, "Cannot reload file; draft kept."); return false; }
    free(d->text); free(d->saved); d->text = text; d->saved = copy(text);
    history_clear(d->undo, &d->undo_count); history_clear(d->redo, &d->redo_count);
    sys_remove(d->draft_path);
    d->conflict = false; d->message[0] = '\0'; editor_refresh(d); return true;
}

bool editor_recover(editor_document *d, const bool recover)
{
    if (!d->recovery) return false;
    if (recover) {
        history_push(d->undo, &d->undo_count, d->text);
        d->text = d->recovery; d->recovery = NULL;
        d->conflict = strcmp(d->saved, d->recovery_base) != 0;
        free(d->saved); d->saved = d->recovery_base; d->recovery_base = NULL;
        editor_refresh(d); keep_draft(d);
        snprintf(d->message, sizeof d->message, "%s", d->conflict ? "Recovered draft conflicts with newer source. Save is refused; copy needed values before Reload." : "Recovered unsaved draft. Review diagnostics and Save to update the source.");
    } else { free(d->recovery); free(d->recovery_base); d->recovery = d->recovery_base = NULL; sys_remove(d->draft_path); }
    return true;
}

typedef struct watch { uint64_t stamp; } watch;
static void watch_file(void *user, const char *path)
{
    watch *w = user;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) w->stamp = stamp_next(w->stamp, *p);
    w->stamp = stamp_next(w->stamp, sys_file_stamp(path));
}

void editor_poll(editor_document *d)
{
    // Saving always compares bytes. Polling uses stamps only to avoid front-end
    // work on unchanged frames; newly added/deleted files participate too.
    const arena_mark mark = arena_checkpoint();
    game_packages packages = {0};
    packages_resolve(d->project, &packages, NULL, NULL, NULL);
    inputs in = {0}; watch w = {UINT64_C(14695981039346656037)};
    for (int k = 0; k <= packages.count; k++) {
        const char *folder = k < packages.count ? packages.items[k].folder : d->project;
        char *manifest = path_join(folder, PACKAGES_FILE);
        w.stamp = stamp_next(w.stamp, sys_file_stamp(manifest)); free(manifest);
        in.paths.count = 0; char *slash = path_join(folder, "");
        folder_find_own(slash, ".tide", found, &in); folder_find_own(slash, ".tprefab", found, &in); free(slash);
        if (in.paths.count > 1) qsort(in.paths.items, (size_t)in.paths.count, sizeof(char *), compare);
        for (int i = 0; i < in.paths.count; i++) watch_file(&w, in.paths.items[i]);
    }
    packages_free(&packages); arena_rewind(mark);
    if (w.stamp == d->stamp) return;
    char *disk = sys_read_file(d->path, NULL);
    if (!disk || strcmp(disk, d->saved) != 0) {
        if (!editor_dirty(d) && disk) { free(disk); editor_reload(d, false); return; }
        d->conflict = true;
        snprintf(d->message, sizeof d->message, "Source changed outside Tide; draft kept. Reload or save after resolving the conflict.");
    }
    free(disk); editor_refresh(d);
}

expr *editor_component(const editor_node *n, const str name)
{
    if (!n->effective) return NULL;
    for (int i = 0; i < n->effective->args.count; i++) {
        expr *e = n->effective->args.items[i];
        if (e->type_decl && (str_eq(e->type_decl->qualified, name) || str_eq(e->type_decl->name, name))) return e;
    }
    return NULL;
}

expr *editor_value(const expr *c, const field *f)
{
    for (int i = 0; i < c->inits.count; i++) if (str_eq(c->inits.items[i].name, f->name)) return c->inits.items[i].value;
    return f->default_value;
}

char *editor_reference_expression(const editor_document *d, const int node, const expr *value)
{
    if (!value || !value->prefab_reference || value->prefab_entity < 0 || value->prefab_entity >= d->nodes.count) return NULL;
    if (value->prefab_entity == node) return str_to_cstr(str_from("this"));
    const str path = d->nodes.items[value->prefab_entity].path;
    return path.len ? str_to_cstr(path) : NULL;
}

static void expression_bounds(const source *s, token *tokens, int count, int start, size_t *from, size_t *to)
{
    *from = offset(s, tokens[start].at);
    int paren = 0, brace = 0, bracket = 0;
    int i = start;
    for (; i < count; i++) {
        const tok_kind k = tokens[i].kind;
        if (!paren && !brace && !bracket && (k == T_COMMA || k == T_RBRACE || k == T_SEMI)) break;
        if (k == T_LPAREN) paren++;
        if (k == T_RPAREN) paren--;
        if (k == T_LBRACE) brace++;
        if (k == T_RBRACE) brace--;
        if (k == T_LBRACKET) bracket++;
        if (k == T_RBRACKET) bracket--;
    }
    const token *last = &tokens[i > start ? i - 1 : start];
    *to = offset(s, last->at) + (size_t)last->text.len + (last->kind == T_STRING ? 2u : 0u);
    if (*to > s->len) *to = s->len;
}

char *editor_expression(const expr *e)
{
    if (!e || e->at.line <= 0) return str_to_cstr(str_from("default"));
    const source *s = diag_source(e->at.file);
    if (!s || !s->text) return str_to_cstr(str_from("default"));
    // Parsing a read-only slice allocates locally; no new source or diagnostics.
    token *t = NULL; int count = 0, start = -1;
    for (int i = 0; i < lexed.count; i++) if (lexed.items[i].src == s) { t = lexed.items[i].tokens; count = lexed.items[i].count; break; }
    for (int i = 0; i < count; i++) if (t[i].at.line == e->at.line && t[i].at.col == e->at.col) start = i;
    if (start < 0) return str_to_cstr(str_from("default"));
    // AST binary/call locations can be inside the expression. Prefer the
    // assignment's full span for authored/default field values.
    int first = start;
    const size_t point = offset(s, e->at);
    for (int i = start - 1; i >= 0; i--) {
        if (t[i].kind != T_ASSIGN) continue;
        size_t a, b; expression_bounds(s, t, count, i + 1, &a, &b);
        if (a <= point && point < b) { first = i + 1; break; }
    }
    size_t from, to; expression_bounds(s, t, count, first, &from, &to);
    char *result = arena_alloc(to - from + 1); memcpy(result, s->text + from, to - from); return result;
}

static expr *raw_component(const editor_node *node, const str name)
{
    if (!node->authored) return NULL;
    for (int i = 0; i < node->authored->components.count; i++) {
        expr *e = node->authored->components.items[i];
        if (str_eq(e->name, name) || (e->type_decl && str_eq(e->type_decl->qualified, name))) return e;
    }
    return NULL;
}

bool editor_node_inherited(const editor_document *d, const int node)
{
    return node >= 0 && node < d->nodes.count && d->nodes.items[node].inherited;
}

bool editor_can_edit_fields(const editor_document *d, const int node)
{
    return node >= 0 && node < d->nodes.count && !d->syntax_errors && d->src &&
        (d->nodes.items[node].authored || d->nodes.items[node].effective);
}

bool editor_can_structure(const editor_document *d, const int node)
{
    return editor_can_edit_fields(d, node) && !editor_node_inherited(d, node) &&
        d->nodes.items[node].authored && d->nodes.items[node].close >= 0;
}

const char *editor_node_source(const editor_document *d, const int node)
{
    if (node < 0 || node >= d->nodes.count) return NULL;
    const editor_node *n = &d->nodes.items[node];
    const source *s = n->effective ? diag_source(n->effective->at.file) : NULL;
    // A composed spawn's location is its introducing block in the source prefab.
    return s ? s->path : n->authored ? n->authored->unit->src->path : NULL;
}

bool editor_field_overridden(const editor_document *d, const int node, const str component, const str name)
{
    if (node < 0 || node >= d->nodes.count) return false;
    const expr *raw = raw_component(&d->nodes.items[node], component);
    for (int i = 0; raw && i < raw->inits.count; i++) if (str_eq(raw->inits.items[i].name, name)) return true;
    return false;
}

static bool editable(editor_document *d, const int node)
{
    if (node < 0 || node >= d->nodes.count) return false;
    if (!editor_can_structure(d, node)) {
        snprintf(d->message, sizeof d->message, "Inherited descendants allow existing field overrides only. Fix this document's syntax before editing.");
        return false;
    }
    return true;
}

static const char *newline(const editor_document *d) { return strstr(d->text, "\r\n") ? "\r\n" : "\n"; }

static bool insert_entry(editor_document *d, const int node, const char *entry)
{
    const editor_node *n = &d->nodes.items[node];
    const size_t end = offset(d->src, d->tokens[n->close].at);
    size_t from = end;
    while (from > 0 && (d->text[from - 1] == ' ' || d->text[from - 1] == '\t')) from--;
    const bool own_line = from == 0 || d->text[from - 1] == '\n';
    sb b = {0};
    if (!own_line) { from = end; sb_put(&b, newline(d)); }
    // Respect the block's existing indentation, including namespace blocks.
    size_t line = end;
    while (line > 0 && d->text[line - 1] != '\n') line--;
    int spaces = 0;
    while (line < end && (d->text[line] == ' ' || d->text[line] == '\t')) { spaces += d->text[line++] == '\t' ? 4 : 1; }
    for (int i = 0; i < spaces + 4 && i < 1024; i++) sb_put(&b, " ");
    sb_put(&b, entry); sb_put(&b, newline(d));
    if (!own_line) for (int i = 0; i < spaces && i < 1024; i++) sb_put(&b, " ");
    return patch(d, from, from, b.data);
}

static bool component_bounds(editor_document *d, expr *c, int *start, int *end)
{
    *start = token_at(d, c->qual_at.line ? c->qual_at : c->at);
    if (*start < 0) return false;
    int braces = 0;
    for (int i = *start; i < d->token_count; i++) {
        if (d->tokens[i].kind == T_LBRACE) braces++;
        if (d->tokens[i].kind == T_RBRACE) braces--;
        if (!braces && d->tokens[i].kind == T_SEMI) { *end = i; return true; }
    }
    return false;
}

bool editor_set_field(editor_document *d, const int node, const str component, const str name, const char *value)
{
    if (!editor_can_edit_fields(d, node)) return false;
    if (!value[0]) { snprintf(d->message, sizeof d->message, "Enter a Tide value, or Reset to remove the override."); return false; }
    if (editor_node_inherited(d, node)) {
        const expr *effective = editor_component(&d->nodes.items[node], component);
        bool exists = false;
        for (int i = 0; effective && effective->type_decl && i < effective->type_decl->fields.count; i++)
            exists |= str_eq(effective->type_decl->fields.items[i].name, name);
        if (!exists) { snprintf(d->message, sizeof d->message, "Inherited descendants allow changes to existing component fields only."); return false; }
    }
    if (!d->nodes.items[node].authored) {
        int ancestor = node;
        while (ancestor >= 0 && !d->nodes.items[ancestor].authored) ancestor = d->nodes.items[ancestor].parent;
        if (ancestor < 0) return false;
        sb b = {0};
        int depth = 0;
        // The ancestry has at most one block per path segment. Walk in path
        // order, without retaining compiler pointers through a refresh.
        const str path = d->nodes.items[node].path;
        int start = d->nodes.items[ancestor].path.len;
        if (start) start++;
        for (int end = start; end <= path.len; end++) {
            if (end < path.len && path.ptr[end] != '.') continue;
            sb_printf(&b, "entity %.*s { ", end - start, path.ptr + start);
            depth++; start = end + 1;
        }
        sb_printf(&b, STR_FMT " { " STR_FMT " = %s }; ", STR_ARG(component), STR_ARG(name), value);
        while (depth--) sb_put(&b, "} ");
        return insert_entry(d, ancestor, b.data);
    }
    expr *raw = raw_component(&d->nodes.items[node], component);
    if (!raw) {
        sb b = {0}; sb_printf(&b, STR_FMT " { " STR_FMT " = %s };", STR_ARG(component), STR_ARG(name), value);
        return insert_entry(d, node, b.data);
    }
    int start, end;
    if (!component_bounds(d, raw, &start, &end)) return false;
    for (int i = 0; i < raw->inits.count; i++) {
        field_init *f = &raw->inits.items[i];
        if (!str_eq(f->name, name)) continue;
        int t = token_at(d, f->at);
        if (t < 0 || t + 2 >= end || d->tokens[t + 1].kind != T_ASSIGN) return false;
        size_t from, to; expression_bounds(d->src, d->tokens, d->token_count, t + 2, &from, &to);
        return patch(d, from, to, value);
    }
    int open = start;
    while (open < end && d->tokens[open].kind != T_LBRACE) open++;
    sb b = {0};
    if (open == end) {
        sb_printf(&b, " { " STR_FMT " = %s }", STR_ARG(name), value);
        const size_t at = offset(d->src, d->tokens[end].at); return patch(d, at, at, b.data);
    }
    const int close = closing(d, open);
    if (close < 0) return false;
    if (raw->inits.count && d->tokens[close - 1].kind != T_COMMA) sb_put(&b, ",");
    sb_printf(&b, " " STR_FMT " = %s ", STR_ARG(name), value);
    const size_t at = offset(d->src, d->tokens[close].at); return patch(d, at, at, b.data);
}

bool editor_reset_field(editor_document *d, const int node, const str component, const str name)
{
    if (!editor_can_edit_fields(d, node)) return false;
    expr *raw = raw_component(&d->nodes.items[node], component);
    if (!raw) return false;
    for (int i = 0; i < raw->inits.count; i++) {
        field_init *f = &raw->inits.items[i];
        if (!str_eq(f->name, name)) continue;
        if (editor_node_inherited(d, node) && raw->inits.count == 1) {
            // Bare component entries reset component defaults; they are not a
            // value override. Remove empty inherited blocks all the way up.
            int remove = node;
            const prefab_node *authored = d->nodes.items[node].authored;
            if (authored->components.count == 1 && !authored->children.count) {
                for (;;) {
                    const int parent = d->nodes.items[remove].parent;
                    if (parent < 0 || !editor_node_inherited(d, parent)) break;
                    const prefab_node *p = d->nodes.items[parent].authored;
                    if (!p || p->components.count || p->children.count != 1) break;
                    remove = parent;
                }
                const editor_node *n = &d->nodes.items[remove];
                const int t = token_at(d, n->authored->at);
                if (t < 1) return false;
                return patch(d, offset(d->src, d->tokens[t - 1].at), offset(d->src, d->tokens[n->close].at) + 1, "");
            }
            int start, end;
            if (!component_bounds(d, raw, &start, &end)) return false;
            return patch(d, offset(d->src, d->tokens[start].at), offset(d->src, d->tokens[end].at) + 1, "");
        }
        const int t = token_at(d, f->at);
        if (t < 0 || t + 2 >= d->token_count) return false;
        size_t from, to; expression_bounds(d->src, d->tokens, d->token_count, t + 2, &from, &to);
        from = offset(d->src, d->tokens[t].at);
        int next = t + 2;
        while (next < d->token_count && offset(d->src, d->tokens[next].at) < to) next++;
        if (next < d->token_count && d->tokens[next].kind == T_COMMA) to = offset(d->src, d->tokens[next].at) + 1;
        else if (t > 0 && d->tokens[t - 1].kind == T_COMMA) from = offset(d->src, d->tokens[t - 1].at);
        return patch(d, from, to, "");
    }
    return false;
}

bool editor_add_component(editor_document *d, const int node, const str component)
{
    if (!editable(d, node)) return false;
    if (editor_component(&d->nodes.items[node], component) || raw_component(&d->nodes.items[node], component)) return false;
    sb b = {0}; sb_printf(&b, STR_FMT ";", STR_ARG(component)); return insert_entry(d, node, b.data);
}

bool editor_remove_component(editor_document *d, const int node, const str component)
{
    if (!editable(d, node)) return false;
    expr *raw = raw_component(&d->nodes.items[node], component);
    int start, end;
    if (!raw) { snprintf(d->message, sizeof d->message, "Inherited component: edit the source prefab. Remove only deletes authored entries/overrides."); return false; }
    if (!component_bounds(d, raw, &start, &end)) return false;
    return patch(d, offset(d->src, d->tokens[start].at), offset(d->src, d->tokens[end].at) + 1, "");
}

bool editor_add_child(editor_document *d, const int node, const char *base)
{
    if (!editable(d, node)) return false;
    char child[32]; bool used;
    int suffix = 0;
    do {
        snprintf(child, sizeof child, "Entity%d", ++suffix);
        sb path = {0};
        if (d->nodes.items[node].path.len) sb_printf(&path, STR_FMT ".", STR_ARG(d->nodes.items[node].path));
        sb_put(&path, child);
        used = false;
        for (int i = 0; i < d->nodes.count; i++) used |= str_eq_c(d->nodes.items[i].path, path.data);
    } while (used && suffix < 1025);
    return editor_add_named_child(d, node, child, base);
}

static bool valid_name(editor_document *d, const char *name)
{
    if (name && lex_identifier_ok(str_from(name))) return true;
    snprintf(d->message, sizeof d->message, "Enter a valid Tide identifier, such as Level or Player1.");
    return false;
}

static str child_path(const str parent, const char *name)
{
    sb b = {0};
    if (parent.len) sb_printf(&b, STR_FMT ".", STR_ARG(parent));
    sb_put(&b, name); return str_from(b.data);
}

static bool available_child(editor_document *d, const int parent, const char *name, const int except)
{
    if (!valid_name(d, name)) return false;
    const str path = child_path(d->nodes.items[parent].path, name);
    for (int i = 0; i < d->nodes.count; i++) if (i != except && str_eq(d->nodes.items[i].path, path)) {
        snprintf(d->message, sizeof d->message, "An object named %s already exists under this parent.", name); return false;
    }
    return true;
}

bool editor_add_named_child(editor_document *d, const int node, const char *name, const char *base)
{
    if (!editable(d, node) || !available_child(d, node, name, -1)) return false;
    // Resolve a prefab declaration instead of accepting arbitrary source text.
    decl *prefab = NULL;
    if (base && base[0]) {
        for (int i = 0; i < d->prog->decls.count; i++) {
            decl *p = d->prog->decls.items[i];
            if (p->kind == DECL_PREFAB && str_eq_c(p->qualified, base)) prefab = p;
        }
        if (!prefab || prefab == d->prefabs.items[d->active]) {
            snprintf(d->message, sizeof d->message, "Choose another existing prefab by its qualified name."); return false;
        }
    }
    sb b = {0}; sb_printf(&b, "entity %s", name);
    if (prefab) sb_printf(&b, " : " STR_FMT, STR_ARG(prefab->qualified));
    sb_put(&b, prefab ? " { }" : " { Transform; }"); return insert_entry(d, node, b.data);
}

bool editor_add_model(editor_document *d, const int node, const char *name, const str component, const str field_name, const char *asset)
{
    if (!editable(d, node) || !available_child(d, node, name, -1)) return false;
    decl *c = NULL, *a = NULL;
    for (int i = 0; i < d->prog->decls.count; i++) {
        decl *p = d->prog->decls.items[i];
        if (p->kind == DECL_COMPONENT && str_eq(p->qualified, component)) c = p;
        if (p->kind == DECL_ASSET && str_eq_c(p->qualified, asset)) a = p;
    }
    if (!c || !a) return false;
    bool field_exists = false;
    for (int i = 0; i < c->fields.count; i++) {
        const field *f = &c->fields.items[i];
        if (str_eq(f->name, field_name) && f->type.kind == TY_STRUCT && f->type.decl &&
            f->type.decl->asset_ref_of && a->return_type.kind == TY_MESH && !a->asset_many &&
            f->type.decl->asset_ref_of == a->return_type.decl) field_exists = true;
    }
    if (!field_exists) { snprintf(d->message, sizeof d->message, "Choose a matching singular Mesh asset and Asset<Mesh> component field."); return false; }
    sb b = {0}; sb_printf(&b, "entity %s { Transform; " STR_FMT " { " STR_FMT " = " STR_FMT " }; }",
        name, STR_ARG(c->qualified), STR_ARG(field_name), STR_ARG(a->qualified));
    return insert_entry(d, node, b.data);
}

bool editor_remove_node(editor_document *d, const int node)
{
    if (!editable(d, node) || d->nodes.items[node].parent < 0) return false;
    editor_node *n = &d->nodes.items[node];
    const int name = token_at(d, n->authored->at);
    if (name < 1 || !str_eq_c(d->tokens[name - 1].text, "entity")) return false;
    return patch(d, offset(d->src, d->tokens[name - 1].at), offset(d->src, d->tokens[n->close].at) + 1, "");
}

typedef struct text_edit { size_t from, to; const char *text; } text_edit;
typedef struct text_edits { text_edit *items; int count, cap; bool invalid; } text_edits;

static bool under(const str path, const str root)
{
    return !root.len || (path.len >= root.len && memcmp(path.ptr, root.ptr, (size_t)root.len) == 0 &&
        (path.len == root.len || path.ptr[root.len] == '.'));
}

static str replace_path(const str path, const str old, const str replacement)
{
    if (!under(path, old)) return path;
    sb b = {0}; sb_putn(&b, replacement.ptr, (size_t)replacement.len);
    sb_putn(&b, path.ptr + old.len, (size_t)(path.len - old.len)); return str_from(b.data ? b.data : "");
}

static void add_edit(text_edits *edits, const size_t from, const size_t to, const char *text)
{
    for (int i = 0; i < edits->count; i++) if (edits->items[i].from == from && edits->items[i].to == to) {
        if (strcmp(edits->items[i].text, text) != 0) edits->invalid = true;
        return;
    }
    vec_push(*edits, ((text_edit){from, to, text}));
}

static int compare_edits(const void *a, const void *b)
{
    const text_edit *x = a, *y = b;
    return x->from < y->from ? -1 : x->from > y->from ? 1 : x->to < y->to ? -1 : x->to > y->to;
}

static char *edited_slice(const editor_document *d, text_edits *edits, const size_t from, const size_t to)
{
    if (edits->count > 1) qsort(edits->items, (size_t)edits->count, sizeof(*edits->items), compare_edits);
    sb b = {0}; size_t at = from;
    for (int i = 0; i < edits->count; i++) {
        const text_edit e = edits->items[i];
        if (e.from < from || e.to > to) continue;
        if (e.from < at || e.from > e.to) { edits->invalid = true; return NULL; }
        sb_putn(&b, d->text + at, e.from - at); sb_put(&b, e.text); at = e.to;
    }
    sb_putn(&b, d->text + at, to - at); return b.data ? b.data : str_to_cstr((str){0});
}

static bool apply_edits(editor_document *d, text_edits *edits, const uint64_t expected_refs)
{
    char *text = edited_slice(d, edits, 0, strlen(d->text));
    if (edits->invalid || !text) {
        snprintf(d->message, sizeof d->message, "Edit refused: overlapping or context-dependent source references."); return false;
    }
    return patch_checked(d, 0, strlen(d->text), text, true, expected_refs);
}

static bool node_bounds(const editor_document *d, const int node, size_t *from, size_t *to)
{
    const editor_node *n = &d->nodes.items[node];
    const int name = n->authored ? token_at(d, n->authored->at) : -1;
    if (name < 1 || n->close < 0 || !str_eq_c(d->tokens[name - 1].text, "entity")) return false;
    *from = offset(d->src, d->tokens[name - 1].at); *to = offset(d->src, d->tokens[n->close].at) + 1;
    return true;
}

static bool reference_bounds(const editor_document *d, const expr *e, size_t *from, size_t *to)
{
    const source *s = diag_source(e->at.file);
    if (!s || !same_path(s->path, d->path)) return false;
    const expr *first = e;
    while (first->kind == E_MEMBER && first->object) first = first->object;
    const int end = token_at(d, e->at);
    if (end < 0) return false;
    *from = offset(d->src, first->at); *to = offset(d->src, d->tokens[end].at) + (size_t)d->tokens[end].text.len;
    return true;
}

typedef struct path_change { prefab_instance *instance; str old, next; } path_change;
typedef struct reference_edits {
    editor_document *d;
    text_edits edits;
    VEC(path_change) changes;
    bool external;
    size_t only_from, only_to; // Duplicate: rewrite only copied source bytes.
} reference_edits;

static str changed_path(const reference_edits *r, const prefab_instance *instance, const str path)
{
    for (int i = 0; i < r->changes.count; i++) {
        const path_change *c = &r->changes.items[i];
        if (c->instance == instance && under(path, c->old)) return replace_path(path, c->old, c->next);
    }
    return path;
}

static void rewrite_references(reference_edits *r, const prefab_instance *p, const expr *e)
{
    if (!e) return;
    if (e->prefab_reference) {
        if (e->kind == E_THIS || e->prefab_entity < 0 || e->prefab_entity >= p->entities.count) return;
        size_t from = 0, to = 0;
        if (r->only_to && (!reference_bounds(r->d, e, &from, &to) || from < r->only_from || to > r->only_to)) return;
        const str old_target = p->entities.items[e->prefab_entity].path;
        const str target = changed_path(r, p, old_target);
        const str scope = r->only_to ? e->prefab_scope : changed_path(r, p, e->prefab_scope);
        const int old_skip = e->prefab_scope.len ? e->prefab_scope.len + 1 : 0;
        const int skip = scope.len ? scope.len + 1 : 0;
        if (!under(target, scope) || target.len < skip || old_target.len < old_skip) { r->edits.invalid = true; return; }
        const str old_text = {old_target.ptr + old_skip, old_target.len - old_skip};
        const str text = {target.ptr + skip, target.len - skip};
        if (str_eq(text, old_text)) return;
        if (!reference_bounds(r->d, e, &from, &to)) { if (!r->only_to) r->external = true; return; }
        if (r->only_to && (from < r->only_from || to > r->only_to)) return;
        if (!text.len) { r->edits.invalid = true; return; }
        add_edit(&r->edits, from, to, str_to_cstr(text)); return;
    }
    rewrite_references(r, p, e->object); rewrite_references(r, p, e->lhs);
    rewrite_references(r, p, e->rhs); rewrite_references(r, p, e->cond);
    for (int i = 0; i < e->args.count; i++) rewrite_references(r, p, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) rewrite_references(r, p, e->inits.items[i].value);
}

static void rewrite_instance(reference_edits *r, const prefab_instance *p)
{
    for (int i = 0; i < p->entities.count; i++) rewrite_references(r, p, p->entities.items[i].spawn);
}

static bool checked_structure(editor_document *d, const int node)
{
    if (!editable(d, node) || d->nodes.items[node].parent < 0) return false;
    if (d->errors) { snprintf(d->message, sizeof d->message, "Fix project errors before changing hierarchy paths so every entity reference can be checked."); return false; }
    return true;
}

static bool same_loc(const loc a, const loc b) { return a.file == b.file && a.line == b.line && a.col == b.col; }

static bool has_directives(const char *text, const size_t len)
{
    bool start = true;
    for (size_t i = 0; i < len; i++) {
        const char c = text[i];
        if (start && c == '#') return true;
        if (c == '\n') start = true;
        else if (c != ' ' && c != '\t' && c != '\r') start = false;
    }
    return false;
}

static bool checked_configurations(editor_document *d)
{
    for (int i = 0; i < lexed.count; i++) {
        const source *s = lexed.items[i].src;
        const size_t len = strlen(s->path);
        if (len >= 8 && same_path(s->path + len - 8, ".tprefab") && has_directives(s->text, s->len)) {
            snprintf(d->message, sizeof d->message, "Hierarchy path edits need every authored branch checked. Edit conditional prefab sources in your code editor."); return false;
        }
    }
    return true;
}

static void rewrite_override_names(reference_edits *r, const prefab_instance *p, const prefab_node *raw, const str path, const bool reparent)
{
    const str next = changed_path(r, p, path);
    if (!str_eq(next, path)) {
        // Descendant names do not change when their ancestor moves/renames.
        const char *old_name = str_to_cstr(path), *new_name = str_to_cstr(next);
        const char *dot = strrchr(old_name, '.'); old_name = dot ? dot + 1 : old_name;
        dot = strrchr(new_name, '.'); new_name = dot ? dot + 1 : new_name;
        if (reparent) r->external = true; // Moving a consumer's override needs a separate document transaction.
        else if (strcmp(old_name, new_name) != 0) {
            if (!same_path(raw->unit->src->path, r->d->path)) r->external = true;
            else add_edit(&r->edits, offset(r->d->src, raw->at), offset(r->d->src, raw->at) + (size_t)raw->name.len, new_name);
        }
    }
    for (int i = 0; i < raw->children.count; i++) {
        prefab_node *child = raw->children.items[i];
        rewrite_override_names(r, p, child, child_path(path, str_to_cstr(child->name)), reparent);
    }
}

static bool gather_changes(editor_document *d, const int node, const str next, reference_edits *r)
{
    r->d = d;
    const str old = d->nodes.items[node].path;
    const loc origin = d->nodes.items[node].authored->at;
    for (int i = 0; i < d->prog->decls.count; i++) {
        decl *decl = d->prog->decls.items[i];
        prefab_instance *p = decl->kind == DECL_PREFAB ? decl->prefab_checked : NULL;
        if (!p) continue;
        for (int k = 1; k < p->entities.count; k++) {
            const prefab_entity *e = &p->entities.items[k];
            if (!same_loc(e->spawn->at, origin) || e->path.len < old.len) continue;
            const int prefix = e->path.len - old.len;
            if (memcmp(e->path.ptr + prefix, old.ptr, (size_t)old.len) != 0) continue;
            sb b = {0}; sb_putn(&b, e->path.ptr, (size_t)prefix); sb_putn(&b, next.ptr, (size_t)next.len);
            vec_push(r->changes, ((path_change){p, e->path, str_from(b.data)}));
        }
    }
    // A consumer can add a sibling the base does not contain. Adding the base
    // child at that path would silently turn it into an inherited override.
    for (int c = 0; c < r->changes.count; c++) {
        const path_change *change = &r->changes.items[c];
        const prefab_instance *p = change->instance;
        for (int k = 0; k < p->entities.count; k++)
            if (str_eq(p->entities.items[k].path, change->next) && !under(p->entities.items[k].path, change->old)) {
                snprintf(d->message, sizeof d->message, "An instance already has an object at the destination path. Choose another name or parent."); return false;
            }
    }
    return true;
}

static bool hierarchy_changes(editor_document *d, const int node, const str next, const bool reparent, reference_edits *r)
{
    if (!checked_configurations(d) || !gather_changes(d, node, next, r)) return false;
    for (int i = 0; i < d->prog->decls.count; i++) {
        decl *decl = d->prog->decls.items[i];
        prefab_instance *p = decl->kind == DECL_PREFAB ? decl->prefab_checked : NULL;
        if (!p) continue;
        rewrite_instance(r, p);
        // The active declaration's node header is handled by the action itself.
        if (decl != d->prefabs.items[d->active]) rewrite_override_names(r, p, decl->prefab_root, (str){0}, reparent);
    }
    if (r->external || r->edits.invalid) {
        snprintf(d->message, sizeof d->message, "Hierarchy change needs edits in another prefab source or an instance override. Update those in your code editor first."); return false;
    }
    return true;
}

bool editor_rename_node(editor_document *d, const int node, const char *name)
{
    if (!checked_structure(d, node)) return false;
    const int parent = d->nodes.items[node].parent;
    if (!available_child(d, parent, name, node)) return false;
    const str next = child_path(d->nodes.items[parent].path, name);
    if (str_eq(next, d->nodes.items[node].path)) return false;
    reference_edits r = {0};
    if (!hierarchy_changes(d, node, next, false, &r)) return false;
    const prefab_node *raw = d->nodes.items[node].authored;
    add_edit(&r.edits, offset(d->src, raw->at), offset(d->src, raw->at) + (size_t)raw->name.len, name);
    return apply_edits(d, &r.edits, project_reference_count(d));
}

bool editor_duplicate_node(editor_document *d, const int node, const char *name)
{
    if (!checked_structure(d, node) || !available_child(d, d->nodes.items[node].parent, name, -1)) return false;
    size_t from, to; if (!node_bounds(d, node, &from, &to)) return false;
    if (has_directives(d->text + from, to - from)) {
        snprintf(d->message, sizeof d->message, "Duplicate conditional object blocks in your code editor so every branch's references can be updated."); return false;
    }
    const str next = child_path(d->nodes.items[d->nodes.items[node].parent].path, name);
    reference_edits consumers = {0};
    if (!checked_configurations(d) || !gather_changes(d, node, next, &consumers)) return false;
    uint64_t expected_refs = project_reference_count(d);
    for (int c = 0; c < consumers.changes.count; c++) {
        const path_change *change = &consumers.changes.items[c];
        const prefab_instance *instance = change->instance;
        for (int i = 0; i < instance->entities.count; i++) if (under(instance->entities.items[i].path, change->old))
            expected_refs += reference_count(instance->entities.items[i].spawn);
    }
    reference_edits r = {.d = d, .only_from = from, .only_to = to};
    prefab_instance *p = d->prefabs.items[d->active]->prefab_checked;
    vec_push(r.changes, ((path_change){p, d->nodes.items[node].path, next}));
    rewrite_instance(&r, p);
    const prefab_node *raw = d->nodes.items[node].authored;
    add_edit(&r.edits, offset(d->src, raw->at), offset(d->src, raw->at) + (size_t)raw->name.len, name);
    char *block = edited_slice(d, &r.edits, from, to);
    if (r.edits.invalid) return false;
    sb b = {0}; sb_put(&b, newline(d)); sb_put(&b, block);
    return patch_checked(d, to, to, b.data, true, expected_refs);
}

bool editor_reparent_node(editor_document *d, const int node, const int parent)
{
    if (!checked_structure(d, node) || !editable(d, parent)) return false;
    if (parent == d->nodes.items[node].parent) return false;
    if (under(d->nodes.items[parent].path, d->nodes.items[node].path)) {
        snprintf(d->message, sizeof d->message, "An object cannot be parented to itself or its descendants."); return false;
    }
    const char *name = str_to_cstr(d->nodes.items[node].authored->name);
    if (!available_child(d, parent, name, -1)) return false;
    const str next = child_path(d->nodes.items[parent].path, name);
    reference_edits r = {0}; if (!hierarchy_changes(d, node, next, true, &r)) return false;
    size_t from, to; if (!node_bounds(d, node, &from, &to)) return false;
    char *block = edited_slice(d, &r.edits, from, to);
    if (r.edits.invalid) return false;
    int count = 0;
    for (int i = 0; i < r.edits.count; i++) if (r.edits.items[i].from < from || r.edits.items[i].to > to) r.edits.items[count++] = r.edits.items[i];
    r.edits.count = count; add_edit(&r.edits, from, to, "");
    const size_t insert = offset(d->src, d->tokens[d->nodes.items[parent].close].at);
    sb b = {0}; sb_put(&b, newline(d)); sb_put(&b, block); sb_put(&b, newline(d));
    add_edit(&r.edits, insert, insert, b.data);
    return apply_edits(d, &r.edits, project_reference_count(d));
}

typedef struct source_folder { const char *target; bool found; } source_folder;

static bool enter_source_folder(void *user, const char *path)
{
    source_folder *folder = user;
    const size_t len = strlen(path), target_len = strlen(folder->target);
    const bool exact = target_len + 1 == len &&
#ifdef _WIN32
        _strnicmp(path, folder->target, target_len) == 0;
#else
        strncmp(path, folder->target, target_len) == 0;
#endif
    if (exact) folder->found = true;
    return !exact && target_len >= len &&
#ifdef _WIN32
        _strnicmp(path, folder->target, len) == 0;
#else
        strncmp(path, folder->target, len) == 0;
#endif
}

static void ignore_source_file(void *user, const char *path, const unsigned long long size)
{
    (void)user; (void)path; (void)size;
}

static bool new_file_path(editor_document *d, const char *path, const char *project, char **absolute)
{
    *absolute = path_absolute(path);
    char *root = path_absolute(project), *folder = path_dir(*absolute);
    const size_t n = strlen(root), len = strlen(*absolute);
    const bool in_root = len > n && (*absolute)[n] == '/' &&
#ifdef _WIN32
        _strnicmp(*absolute, root, n) == 0;
#else
        strncmp(*absolute, root, n) == 0;
#endif
    bool ok = in_root && len > 8 && same_path(*absolute + len - 8, ".tprefab") && sys_is_dir(folder);
    // Match the folder scanner's hidden-folder and nested-project boundaries.
    for (const char *p = in_root ? *absolute + n + 1 : ""; ok && *p; p++)
        if (((p == *absolute + n + 1 || p[-1] == '/') && p[0] == '.') || p[0] == ':') ok = false;
    char *candidate = copy(folder);
    while (ok && !same_path(candidate, root)) {
        char *marker = path_join(candidate, PACKAGES_FILE);
        if (sys_exists(marker)) ok = false;
        free(marker);
        char *parent = path_dir(candidate);
        if (same_path(parent, candidate) || !parent[0]) ok = false;
        free(candidate); candidate = parent;
    }
    if (ok && !same_path(folder, root)) {
        source_folder source = {.target = folder};
        char *slash = path_join(root, "");
        // This scanner skips directory links/reparse points just as source
        // discovery does; refuse those before opening any destination file.
        folder_find_files_in(slash, enter_source_folder, ignore_source_file, &source);
        free(slash); ok = source.found;
    }
    free(candidate); free(folder); free(root);
    if (!ok) snprintf(d->message, sizeof d->message, "Choose a .tprefab path in an existing source folder of this project, outside nested projects and hidden folders.");
    else if (sys_exists(*absolute)) {
        snprintf(d->message, sizeof d->message, "File already exists; choose a new .tprefab filename."); ok = false;
    }
    return ok;
}

static bool write_new_file(editor_document *d, const char *path, const char *text)
{
    // C17 exclusive creation prevents overwrites even when another program
    // creates the chosen name after validation. Existing files are never opened.
    FILE *f = fopen(path, "wbx");
    if (!f) { snprintf(d->message, sizeof d->message, "Cannot create new file; the filename may exist or its folder is not writable."); return false; }
    const size_t len = strlen(text);
    const bool written = fwrite(text, 1, len, f) == len;
    const bool closed = fclose(f) == 0;
    if (!written || !closed) { sys_remove(path); snprintf(d->message, sizeof d->message, "Could not finish writing the new file; original document kept."); return false; }
    return true;
}

static bool declaration_available(editor_document *d, const str name, const decl *except)
{
    for (int i = 0; d->prog && i < d->prog->decls.count; i++) {
        const decl *p = d->prog->decls.items[i];
        if (p != except && str_eq(p->qualified, name)) {
            snprintf(d->message, sizeof d->message, "A declaration named " STR_FMT " already exists in this project.", STR_ARG(name)); return false;
        }
    }
    return true;
}

typedef struct own_document_search { const char *path; bool found; } own_document_search;

static void find_own_document(void *user, const char *path)
{
    own_document_search *search = user;
    if (same_path(path, search->path)) search->found = true;
}

static bool reserved_prefab_name(const str name)
{
    // Match declaration reservations, which include the public built-in
    // owners except Audio, plus engine names without static members.
    if (builtin_owner(name) && !str_eq_c(name, "Audio")) return true;
    static const char *const names[] = {
        "Devices", "Spawn", "Send", "Scene", "Action", "DrawList", "string",
        "Transform", "Visibility", "VisibilityGroup"
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) if (str_eq_c(name, names[i])) return true;
    return false;
}

bool editor_initialize_prefab(editor_document *d, const char *name)
{
    if (!d->prog || !d->src || !d->text || !d->saved || !d->project) {
        snprintf(d->message, sizeof d->message, "Open a document inside this project's source boundary before creating its root."); return false;
    }
    if (d->prefabs.count) {
        snprintf(d->message, sizeof d->message, "This document already has a prefab root. Select it to add objects or components."); return false;
    }
    if (d->syntax_errors || editor_source_syntax_errors(d, d->src)) {
        snprintf(d->message, sizeof d->message, "Fix this document's syntax in your code editor, then Reload before creating its root."); return false;
    }
    if (d->recovery || d->recovery_base) {
        snprintf(d->message, sizeof d->message, "Recover or discard the pending draft before creating a root."); return false;
    }
    if (d->conflict) {
        snprintf(d->message, sizeof d->message, "Resolve the outside source conflict before creating a root. Your draft is kept."); return false;
    }
    own_document_search search = {.path = d->path};
    char *folder = path_join(d->project, "");
    folder_find_own(folder, ".tide", find_own_document, &search);
    folder_find_own(folder, ".tprefab", find_own_document, &search);
    free(folder);
    if (!search.found) {
        snprintf(d->message, sizeof d->message, "Create the root in this project's own discoverable source files, outside dependencies and nested projects."); return false;
    }
    size_t disk_len = 0;
    const size_t saved_len = strlen(d->saved);
    char *disk = sys_read_file(d->path, &disk_len);
    const bool same = disk && disk_len == saved_len && memcmp(disk, d->saved, saved_len) == 0;
    free(disk);
    if (!same) {
        d->conflict = true;
        snprintf(d->message, sizeof d->message, "File changed outside Tide. Reload it before creating a root; your draft is kept."); return false;
    }
    if (!valid_name(d, name)) return false;
    if (reserved_prefab_name(str_from(name))) {
        snprintf(d->message, sizeof d->message, "%s is built into the language. Choose another root name.", name); return false;
    }
    const unit *file = NULL;
    for (int i = 0; i < d->prog->units.count; i++) {
        const unit *u = d->prog->units.items[i];
        if (u->src == d->src && !u->outer) { file = u; break; }
    }
    if (!file) {
        snprintf(d->message, sizeof d->message, "Reload this document before creating its root so its namespace can be checked."); return false;
    }
    if (d->src->package && !under(file->ns, str_from(d->src->package))) {
        snprintf(d->message, sizeof d->message, "Start this package source with 'namespace %s;' before creating its root.", d->src->package); return false;
    }
    if (!declaration_available(d, child_path(file->ns, name), NULL)) return false;
    const size_t end = strlen(d->text);
    const size_t bom = end >= 3 && memcmp(d->text, "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
    const char *nl = newline(d);
    sb addition = {0};
    if (end > bom && d->text[end - 1] != '\n') sb_put(&addition, nl);
    sb_printf(&addition, "prefab %s {%s    Transform;%s}%s", name, nl, nl, nl);
    return patch(d, end, end, addition.data);
}

bool editor_create(editor_document *d, const char *path, const char *project, const char *name)
{
    memset(d, 0, sizeof *d);
    if (!project || !valid_name(d, name)) return false;
    char *absolute = NULL;
    if (!new_file_path(d, path, project, &absolute)) { free(absolute); return false; }
    sb b = {0}; sb_printf(&b, "prefab %s {\n    Transform;\n}\n", name);
    char *text = copy(b.data);
    if (!write_new_file(d, absolute, text)) { free(absolute); free(text); return false; }
    const bool opened = editor_open(d, absolute, project);
    const bool ok = opened && !d->syntax_errors && d->prefabs.count == 1 &&
        declaration_available(d, d->prefabs.items[0]->qualified, d->prefabs.items[0]);
    if (!ok) {
        char reason[1024]; snprintf(reason, sizeof reason, "%s", d->message[0] ? d->message : "New file is outside this project's discoverable source boundary.");
        char *disk = sys_read_file(absolute, NULL);
        if (disk && strcmp(disk, text) == 0) sys_remove(absolute);
        free(disk); editor_close(d); snprintf(d->message, sizeof d->message, "%s", reason);
    }
    free(absolute); free(text); return ok;
}

typedef struct extraction_refs { editor_document *d; prefab_instance *p; str root; int self; text_edits edits; bool outside; } extraction_refs;

static void extract_references(extraction_refs *r, const expr *e)
{
    if (!e) return;
    if (e->prefab_reference) {
        if (e->prefab_entity < 0 || e->prefab_entity >= r->p->entities.count) { r->outside = true; return; }
        const str path = r->p->entities.items[e->prefab_entity].path;
        if (!under(path, r->root)) { r->outside = true; return; }
        if (e->kind == E_THIS) return;
        // Base-prefab expressions keep their own relative scope and source.
        // Only values written in this extracted block need rebasing.
        size_t from, to;
        if (!reference_bounds(r->d, e, &from, &to)) return;
        if (under(e->prefab_scope, r->root)) return;
        if (str_eq(path, r->root)) {
            if (e->prefab_entity != r->self) { r->outside = true; return; }
            add_edit(&r->edits, from, to, "this");
        } else {
            const str relative = {path.ptr + r->root.len + 1, path.len - r->root.len - 1};
            add_edit(&r->edits, from, to, str_to_cstr(relative));
        }
        return;
    }
    extract_references(r, e->object); extract_references(r, e->lhs); extract_references(r, e->rhs); extract_references(r, e->cond);
    for (int i = 0; i < e->args.count; i++) extract_references(r, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) extract_references(r, e->inits.items[i].value);
}

bool editor_extract_prefab(editor_document *d, const int node, const char *path, const char *name)
{
    if (!checked_structure(d, node) || !valid_name(d, name)) return false;
    const editor_node *n = &d->nodes.items[node];
    if (n->authored->base.len) { snprintf(d->message, sizeof d->message, "Extract an authored object with its own root; this object is already a prefab instance."); return false; }
    decl *active = d->prefabs.items[d->active];
    const str qualified = child_path(active->unit->ns, name);
    if (!declaration_available(d, qualified, NULL)) return false;
    char *absolute = NULL;
    if (!new_file_path(d, path, d->project, &absolute)) { free(absolute); return false; }
    extraction_refs refs = {.d = d, .p = active->prefab_checked, .root = n->path};
    uint64_t expected_refs = project_reference_count(d);
    for (int i = 0; i < d->nodes.count; i++) if (under(d->nodes.items[i].path, n->path)) {
        refs.self = i; extract_references(&refs, d->nodes.items[i].effective);
        expected_refs += reference_count(d->nodes.items[i].effective);
    }
    if (refs.outside) {
        snprintf(d->message, sizeof d->message, "Extraction needs a self-contained subtree: remove references to outside objects or from descendants to this root first."); free(absolute); return false;
    }
    const size_t body_from = offset(d->src, d->tokens[n->open].at), body_to = offset(d->src, d->tokens[n->close].at) + 1;
    if (has_directives(d->text + body_from, body_to - body_from)) {
        snprintf(d->message, sizeof d->message, "Extract conditional object blocks in your code editor so every branch's references can be rebased."); free(absolute); return false;
    }
    char *body = edited_slice(d, &refs.edits, body_from, body_to);
    if (!body || refs.edits.invalid) { free(absolute); return false; }
    sb asset = {0};
    if (active->unit->ns.len) sb_printf(&asset, "namespace " STR_FMT ";%s", STR_ARG(active->unit->ns), newline(d));
    for (int i = 0; i < active->unit->usings.count; i++) sb_printf(&asset, "using " STR_FMT ";%s", STR_ARG(active->unit->usings.items[i]), newline(d));
    sb_printf(&asset, "prefab %s %s%s", name, body, newline(d));
    char *new_text = copy(asset.data);
    size_t from, to; if (!node_bounds(d, node, &from, &to)) { free(absolute); free(new_text); return false; }
    sb instance = {0}; sb_printf(&instance, "entity " STR_FMT " : " STR_FMT " {}", STR_ARG(n->authored->name), STR_ARG(qualified));
    char *replacement = copy(instance.data);
    if (!write_new_file(d, absolute, new_text)) { free(absolute); free(new_text); free(replacement); return false; }
    const bool ok = patch_checked(d, from, to, replacement, true, expected_refs);
    if (!ok) {
        char reason[sizeof d->message]; snprintf(reason, sizeof reason, "%s", d->message);
        char *disk = sys_read_file(absolute, NULL);
        if (disk && strcmp(disk, new_text) == 0) sys_remove(absolute);
        free(disk);
        // The rejected transaction's rollback analysis still saw the asset.
        // Refresh after cleanup so no deleted declaration remains selectable.
        editor_refresh(d); snprintf(d->message, sizeof d->message, "%s", reason);
    }
    free(absolute); free(new_text); free(replacement); return ok;
}
