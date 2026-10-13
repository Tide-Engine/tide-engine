#include "document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "folders.h"
#include "packages.h"
#include "sys.h"

typedef struct lexed_file { const source *src; token *tokens; int count; } lexed_file;
static VEC(lexed_file) lexed;

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
    qsort(in->paths.items, (size_t)in->paths.count, sizeof(char *), compare);
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
        token *t = lex_all(s);
        int count = 0; while (t[count].kind != T_EOF) count++;
        vec_push(lexed, ((lexed_file){s, t, count}));
        if (own_file) {
            // Retain the filesystem's filename spelling when replacing it.
            free(in->d->path); in->d->path = copy(path);
            in->d->src = s;
            in->d->tokens = t;
            while (t[in->d->token_count].kind != T_EOF) in->d->token_count++;
        }
        parse_file(in->d->prog, s, t, true);
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
}

void editor_refresh(editor_document *d)
{
    char *active = d->prefabs.count && d->active < d->prefabs.count ? copy(str_to_cstr(d->prefabs.items[d->active]->qualified)) : NULL;
    arena_reset();
    d->revision++;
    lexed.items = NULL; lexed.count = lexed.cap = 0;
    diag_reset();
    d->errors = d->warnings = d->token_count = 0;
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
        diag_add_source(s); token *t = lex_all(s); int count = 0;
        while (t[count].kind != T_EOF) count++;
        vec_push(lexed, ((lexed_file){s, t, count})); parse_file(d->prog, s, t, true);
    }
    d->syntax_errors = d->errors > 0;
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

static bool patch(editor_document *d, const size_t from, const size_t to, const char *replacement)
{
    if (d->syntax_errors || !d->src || from > to || to > strlen(d->text)) {
        snprintf(d->message, sizeof d->message, "Fix source syntax/package errors in your code editor, then Reload.");
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
    if (d->syntax_errors) {
        char reason[700]; snprintf(reason, sizeof reason, "Edit rejected: %.650s", d->diagnostics.count ? d->diagnostics.items[0] : "invalid Tide syntax");
        free(d->text); d->text = before; editor_refresh(d);
        snprintf(d->message, sizeof d->message, "%s", reason); return false;
    }
    history_push(d->undo, &d->undo_count, before);
    history_clear(d->redo, &d->redo_count); keep_draft(d);
    return true;
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
    if (d->errors || !d->src || !d->prefabs.count) { snprintf(d->message, sizeof d->message, "Save blocked: fix the %d project error(s) below and keep this file in the project.", d->errors); return false; }
    char *disk = sys_read_file(d->path, NULL);
    const bool same = disk && strcmp(disk, d->saved) == 0;
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
    disk = sys_read_file(d->path, NULL);
    ok = ok && disk && strcmp(disk, d->saved) == 0;
    free(disk);
    if (ok) ok = sys_rename(temp, d->path);
    if (!ok) sys_remove(temp);
    free(temp);
    if (ok) {
        free(d->saved); d->saved = copy(d->text); d->conflict = false;
        sys_remove(d->draft_path);
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
        qsort(in.paths.items, (size_t)in.paths.count, sizeof(char *), compare);
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

static bool editable(editor_document *d, const int node)
{
    if (node < 0 || node >= d->nodes.count) return false;
    if (!d->nodes.items[node].authored || d->nodes.items[node].close < 0 || d->syntax_errors) {
        snprintf(d->message, sizeof d->message, "Inherited descendants are read-only. Edit their source prefab; fix source syntax before editing.");
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
    if (!editable(d, node)) return false;
    if (!value[0]) { snprintf(d->message, sizeof d->message, "Enter a Tide value, or Reset to remove the override."); return false; }
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
    if (!editable(d, node)) return false;
    expr *raw = raw_component(&d->nodes.items[node], component);
    if (!raw) return false;
    for (int i = 0; i < raw->inits.count; i++) {
        field_init *f = &raw->inits.items[i];
        if (!str_eq(f->name, name)) continue;
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
    sb b = {0}; sb_printf(&b, "entity %s%s%s { Transform; }", child, base && base[0] ? " : " : "", base ? base : "");
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
