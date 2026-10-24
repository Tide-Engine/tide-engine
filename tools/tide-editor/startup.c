#include "startup.h"
#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t position(const source *s, const loc at)
{
    size_t i = s->len >= 3 && memcmp(s->text, "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
    for (int line = 1; line < at.line && i < s->len; line++) {
        while (i < s->len && s->text[i] != '\n') i++;
        if (i < s->len) i++;
    }
    const size_t col = at.col > 0 ? (size_t)at.col - 1 : 0;
    return i + (col < s->len - i ? col : s->len - i);
}

static bool own_source(const char *project, const char *file)
{
    // path_absolute normalizes separators and dot segments. Its Windows drive
    // root spelling is C:, so restore the slash before resolving it again.
    char *drive = strlen(project) == 2 && project[1] == ':' ? path_join(project, "") : NULL;
    char *root = path_absolute(drive ? drive : project), *path = path_absolute(file);
    free(drive);
    const size_t n = strlen(root), len = strlen(path);
    const bool prefix = n > 0 && len > n &&
#ifdef _WIN32
        _strnicmp(root, path, n) == 0;
#else
        strncmp(root, path, n) == 0;
#endif
    const bool owned = prefix && (root[n - 1] == '/' || path[n] == '/');
    free(root); free(path);
    return owned;
}

static bool project_source(const editor_document *d, const source *src)
{
    // An authored package project tags its own files too. Dependencies have a
    // different package, even when fetched beneath the project's directory.
    const char *package = d->src ? d->src->package : NULL;
    const bool own_package = !src->package || (package && strcmp(package, src->package) == 0);
    return own_package && own_source(d->project, src->path);
}

static bool startup_name_visible(const editor_document *d, const unit *from, const decl *selected)
{
    // Qualified names are exact. A bare global name can be shadowed by a
    // prefab in the settings namespace (or one of its parent namespaces).
    for (int i = 0; i < d->prog->decls.count; i++) {
        const decl *p = d->prog->decls.items[i];
        if (p != selected && p->kind == DECL_PREFAB && str_eq(p->qualified, selected->qualified)) return false;
    }
    if (memchr(selected->qualified.ptr, '.', (size_t)selected->qualified.len)) return true;
    for (str ns = from ? from->ns : (str){0}; ns.len;) {
        sb scoped = {0}; sb_printf(&scoped, STR_FMT "." STR_FMT, STR_ARG(ns), STR_ARG(selected->qualified));
        const bool folders = str_eq_c(ns, "UnityEngine.Prefabs") || str_eq_c(ns, "UnityEngine.Scenes") || str_eq_c(ns, "UnityEngine.SceneContents");
        int descendants = 0;
        for (int i = 0; i < d->prog->decls.count; i++) {
            const decl *p = d->prog->decls.items[i];
            if (p->kind != DECL_PREFAB || !str_eq(p->name, selected->name)) continue;
            if (str_eq_c(p->qualified, scoped.data)) return false;
            // These compiler namespaces also resolve a unique descendant by
            // its short name; ambiguous folder aliases continue at the parent.
            if (folders && p->unit && p->unit->ns.len > ns.len && p->unit->ns.ptr[ns.len] == '.' &&
                memcmp(p->unit->ns.ptr, ns.ptr, (size_t)ns.len) == 0) descendants++;
        }
        if (descendants == 1) return false;
        int dot = ns.len - 1;
        while (dot >= 0 && ns.ptr[dot] != '.') dot--;
        ns.len = dot < 0 ? 0 : dot;
    }
    return true;
}

static bool rhs_span(const source *src, const token *tokens, const int count, const field *setting, size_t *from, size_t *to)
{
    int i = 0;
    while (i < count &&
           !(tokens[i].at.line == setting->at.line && tokens[i].at.col == setting->at.col)) i++;
    if (i + 2 >= count || tokens[i].kind != T_IDENT || tokens[i + 1].kind != T_ASSIGN) return false;
    i += 2;
    if (tokens[i].kind == T_EOF || tokens[i].kind == T_SEMI) return false;
    *from = position(src, tokens[i].at);
    int last = i;
    while (i < count && tokens[i].kind != T_SEMI) last = i++;
    if (i == count) return false;
    // End at the last RHS token, retaining trivia before the semicolon.
    *to = position(src, tokens[last].at) + (size_t)tokens[last].text.len + (tokens[last].kind == T_STRING ? 2u : 0u);
    return *from <= *to && *to <= src->len;
}

static bool disk_matches(const char *path, const char *text, const size_t len)
{
    size_t disk_len = 0;
    char *disk = sys_read_file(path, &disk_len);
    const bool same = disk && disk_len == len && memcmp(disk, text, len) == 0;
    free(disk);
    return same;
}

static bool create_startup(const char *path, const sb *text)
{
    // Exclusive creation closes the final absence-check/rename overwrite race.
    FILE *f = fopen(path, "wbx");
    if (!f) return false;
    const bool written = fwrite(text->data, 1, text->len, f) == text->len;
    const bool closed = fclose(f) == 0;
    if (!written || !closed) sys_remove(path);
    return written && closed;
}

bool editor_set_startup(editor_document *d)
{
    if (d->recovery) {
        snprintf(d->message, sizeof d->message, "Recover or discard the pending draft before choosing the project's startup."); return false;
    }
    if (!d->prog || d->active < 0 || d->active >= d->prefabs.count || editor_dirty(d) || d->conflict || d->syntax_errors) {
        snprintf(d->message, sizeof d->message, "Save this scene before choosing it as the project's startup."); return false;
    }
    if (!disk_matches(d->path, d->saved, strlen(d->saved))) {
        snprintf(d->message, sizeof d->message, "Startup was not changed: the active scene changed outside Tide. Reload it first."); return false;
    }
    const arena_mark mark = arena_checkpoint();
    decl *block = NULL, *existing_block = NULL; field *existing = NULL;
    int own_blocks = 0;
    for (int b = 0; b < d->prog->settings.count; b++) {
        decl *candidate = d->prog->settings.items[b];
        const source *s = candidate->unit ? candidate->unit->src : NULL;
        if (s && project_source(d, s)) {
            own_blocks++;
            if (!block) block = candidate;
        }
        for (int f = 0; f < candidate->fields.count; f++) if (str_eq_c(candidate->fields.items[f].name, "startScene")) {
            if (existing) {
                snprintf(d->message, sizeof d->message, "Resolve duplicate startScene settings in your code editor first.");
                arena_rewind(mark); return false;
            }
            existing = &candidate->fields.items[f]; existing_block = candidate;
        }
    }
    if (existing) block = existing_block;
    else if (own_blocks > 1) {
        snprintf(d->message, sizeof d->message, "Choose a startScene settings block in your code editor first; several project settings blocks exist.");
        arena_rewind(mark); return false;
    }
    const source *src = block && block->unit ? block->unit->src : NULL;
    char *path = src ? path_absolute(src->path) : path_join(d->project, "Startup.tide");
    if (!own_source(d->project, path) || (src && !project_source(d, src))) {
        snprintf(d->message, sizeof d->message, "Set startScene in a project settings block; the selected settings file cannot be replaced safely.");
        free(path); arena_rewind(mark); return false;
    }
    if (!startup_name_visible(d, block ? block->unit : NULL, d->prefabs.items[d->active])) {
        snprintf(d->message, sizeof d->message, "This settings namespace shadows or duplicates the selected global prefab. Move the settings block to global scope and resolve duplicate names in your code editor.");
        free(path); arena_rewind(mark); return false;
    }
    sb replacement = {0};
    const char *nl = src && strstr(src->text, "\r\n") ? "\r\n" : "\n";
    const str name = d->prefabs.items[d->active]->qualified;
    if (!src) sb_printf(&replacement, "settings {%s    startScene = " STR_FMT ";%s}%s", nl, STR_ARG(name), nl, nl);
    else {
        int count = 0;
        const token *tokens = editor_source_tokens(d, src, &count);
        size_t from = position(src, block->end), to = from;
        if (editor_source_syntax_errors(d, src) || !tokens || (existing && !rhs_span(src, tokens, count, existing, &from, &to))) {
            snprintf(d->message, sizeof d->message, "Fix syntax errors in the settings source in your code editor before setting startup.");
            free(path); arena_rewind(mark); return false;
        }
        sb_putn(&replacement, src->text, from);
        if (existing) sb_printf(&replacement, STR_FMT, STR_ARG(name));
        else sb_printf(&replacement, "%s    startScene = " STR_FMT ";%s", nl, STR_ARG(name), nl);
        sb_putn(&replacement, src->text + to, src->len - to);
    }
    bool ok;
    if (!src) ok = create_startup(path, &replacement);
    else {
        char *temp = malloc(strlen(path) + 64); if (!temp) abort();
        sprintf(temp, "%s.tide-startup-%u.tmp", path, sys_pid());
        const bool created = disk_matches(path, src->text, src->len) && create_startup(temp, &replacement);
        ok = created && disk_matches(path, src->text, src->len) && sys_rename(temp, path);
        if (created && !ok) sys_remove(temp);
        free(temp);
    }
    // Refresh invalidates the old qualified name. Keep an owned copy and use
    // it for both selection restoration and the result message.
    char *selected = NULL;
    if (ok) {
        selected = malloc((size_t)name.len + 1); if (!selected) abort();
        memcpy(selected, name.ptr, (size_t)name.len); selected[name.len] = '\0';
        if (src == d->src) {
            // A .tide file may hold both settings and authored prefabs. Publish
            // our own disk replacement as its saved baseline before analysis;
            // otherwise refresh overlays the old text and Save sees a conflict.
            char *text = malloc(replacement.len + 1), *saved = malloc(replacement.len + 1);
            if (!text || !saved) abort();
            memcpy(text, replacement.data, replacement.len + 1);
            memcpy(saved, replacement.data, replacement.len + 1);
            free(d->text); free(d->saved); d->text = text; d->saved = saved;
            while (d->undo_count) free(d->undo[--d->undo_count]);
            while (d->redo_count) free(d->redo[--d->redo_count]);
            d->conflict = false; sys_remove(d->draft_path);
        }
    }
    free(path); arena_rewind(mark);
    if (ok) {
        editor_refresh(d);
        for (int i = 0; i < d->prefabs.count; i++) if (str_eq_c(d->prefabs.items[i]->qualified, selected)) {
            editor_select_prefab(d, i); break;
        }
        if (d->active < 0 || d->active >= d->prefabs.count ||
            !str_eq_c(d->prefabs.items[d->active]->qualified, selected) || d->prog->startup != d->prefabs.items[d->active]) {
            snprintf(d->message, sizeof d->message, "Startup source was written but no longer resolves to %s. Reload and fix its settings scope in your code editor.", selected);
            ok = false;
        } else snprintf(d->message, sizeof d->message, "Project startup set to %s.", selected);
        free(selected);
    } else snprintf(d->message, sizeof d->message, "Startup was not changed: settings source changed outside Tide, the filename exists, or it could not be written.");
    return ok;
}
