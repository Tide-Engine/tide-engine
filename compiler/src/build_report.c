#include "build_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"

static build_report *current;

build_report *build_report_current(void) { return current; }

double build_report_clock_ms(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec * 1e3 + (double)now.tv_nsec * 1e-6;
}

static char *copy(const char *text)
{
    const size_t n = strlen(text) + 1u;
    char *out = malloc(n);
    if (!out) abort();
    memcpy(out, text, n);
    return out;
}

// Room for one more of `size` bytes in `*items`, which has `count`: doubling at each power of two.
static void *grow(void *items, const int count, const size_t size)
{
    if (count & (count - 1)) return items;
    void *more = realloc(items, size * (size_t)(count ? count * 2 : 8));
    if (!more) abort();
    return more;
}

void build_report_begin(void)
{
    if (current) return;
    current = calloc(1, sizeof *current);
    if (!current) abort();
    current->started = current->stage_started = build_report_clock_ms();
}

static void add_time(build_report_time **items, int *count, const char *name, const double ms, const bool merge)
{
    for (int i = 0; merge && i < *count; i++) {
        if (strcmp((*items)[i].name, name) == 0) {
            (*items)[i].ms += ms;
            return;
        }
    }
    *items = grow(*items, *count, sizeof **items);
    (*items)[(*count)++] = (build_report_time){copy(name), ms};
}

void build_report_stage(const char *name)
{
    if (!current) return;
    const double now = build_report_clock_ms();
    // A step said twice in a row (a web build's two links) is one step
    if (current->stage && name && strcmp(current->stage, name) == 0) return;
    if (current->stage) {
        add_time(&current->stages, &current->stage_count, current->stage, now - current->stage_started, false);
        free(current->stage);
    }
    current->stage = name ? copy(name) : NULL;
    current->stage_started = now;
}

void build_report_phase(const char *name, const double ms)
{
    if (current) add_time(&current->phases, &current->phase_count, name, ms, true);
}

void build_report_compiled(const char *source, const double ms, const bool engine)
{
    if (!current) return;
    current->compiles = grow(current->compiles, current->compile_count, sizeof *current->compiles);
    current->compiles[current->compile_count++] = (build_report_compile){copy(source), ms, engine};
}

void build_report_output(const char *path, const int64_t bytes)
{
    if (!current || bytes < 0) return;
    for (int i = 0; i < current->output_count; i++) {
        if (strcmp(current->outputs[i].path, path) == 0) {
            current->outputs[i].bytes = bytes;
            return;
        }
    }
    current->outputs = grow(current->outputs, current->output_count, sizeof *current->outputs);
    current->outputs[current->output_count++] = (build_report_file){copy(path), bytes};
}

void build_report_cut(const char *line)
{
    if (!current) return;
    current->cuts = grow(current->cuts, current->cut_count, sizeof *current->cuts);
    current->cuts[current->cut_count++] = copy(line);
}

static void free_asset(build_report_asset *a)
{
    free(a->name);
    free(a->type);
    for (int i = 0; i < a->file_count; i++) free(a->files[i]);
    free(a->files);
}

void build_report_assets_clear(void)
{
    if (!current) return;
    for (int i = 0; i < current->asset_count; i++) free_asset(&current->assets[i]);
    current->asset_count = 0;
    current->pack_size = 0;
}

build_report_asset *build_report_add_asset(const char *name, const char *type)
{
    if (!current) return NULL;
    current->assets = grow(current->assets, current->asset_count, sizeof *current->assets);
    build_report_asset *a = &current->assets[current->asset_count++];
    *a = (build_report_asset){.name = copy(name), .type = copy(type)};
    return a;
}

void build_report_asset_file(build_report_asset *a, const char *path)
{
    if (!a || !path) return;
    for (int i = 0; i < a->file_count; i++) if (strcmp(a->files[i], path) == 0) return;
    a->files = grow(a->files, a->file_count, sizeof *a->files);
    a->files[a->file_count++] = copy(path);
}

// ---------------------------------------------------------------------------
// Writing it

static int64_t file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
#ifdef _WIN32
    const int64_t size = _fseeki64(f, 0, SEEK_END) == 0 ? _ftelli64(f) : -1; // long is 32 bits here
#else
    const int64_t size = fseek(f, 0, SEEK_END) == 0 ? (int64_t)ftell(f) : -1;
#endif
    fclose(f);
    return size;
}

static int64_t asset_files_size(const build_report_asset *a)
{
    int64_t total = 0;
    for (int i = 0; i < a->file_count; i++) {
        const int64_t size = file_size(a->files[i]);
        if (size > 0) total += size;
    }
    return total;
}

static void json_text(sb *o, const char *s)
{
    sb_put(o, "\"");
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (*c == '"' || *c == '\\') sb_printf(o, "\\%c", *c);
        else if (*c == '\n') sb_put(o, "\\n");
        else if (*c < 0x20) sb_printf(o, "\\u%04x", *c);
        else sb_putn(o, (const char *)c, 1);
    }
    sb_put(o, "\"");
}

static void bytes_text(char *out, const size_t size, const double bytes)
{
    if (bytes >= 1024.0 * 1024.0 * 1024.0) snprintf(out, size, "%.2f GiB", bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024.0 * 1024.0) snprintf(out, size, "%.2f MiB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024.0) snprintf(out, size, "%.1f KiB", bytes / 1024.0);
    else snprintf(out, size, "%.0f B", bytes);
}

static int by_pack_bytes(const void *a, const void *b)
{
    const build_report_asset *x = a, *y = b;
    if ((x->left_out != NULL) != (y->left_out != NULL)) return x->left_out ? 1 : -1;
    if (x->pack_bytes != y->pack_bytes) return x->pack_bytes < y->pack_bytes ? 1 : -1;
    return strcmp(x->name, y->name);
}

static int by_time(const void *a, const void *b)
{
    const build_report_compile *x = a, *y = b;
    if (x->ms != y->ms) return x->ms < y->ms ? 1 : -1;
    return strcmp(x->source, y->source);
}

static void free_report(build_report *r)
{
    for (int i = 0; i < r->stage_count; i++) free(r->stages[i].name);
    for (int i = 0; i < r->phase_count; i++) free(r->phases[i].name);
    for (int i = 0; i < r->asset_count; i++) free_asset(&r->assets[i]);
    for (int i = 0; i < r->compile_count; i++) free(r->compiles[i].source);
    for (int i = 0; i < r->output_count; i++) free(r->outputs[i].path);
    for (int i = 0; i < r->cut_count; i++) free(r->cuts[i]);
    free(r->stage);
    free(r->stages);
    free(r->phases);
    free(r->assets);
    free(r->compiles);
    free(r->outputs);
    free(r->cuts);
    free(r);
}

static void write_json(sb *o, const build_report *r, const build_report_about *about, const uint64_t packed)
{
    sb_put(o, "{\n  \"tide\": ");
    json_text(o, about->version);
    sb_put(o, ",\n  \"game\": ");
    json_text(o, about->game);
    sb_put(o, ",\n  \"folder\": ");
    json_text(o, about->folder);
    sb_put(o, ",\n  \"mode\": ");
    json_text(o, about->mode);
    sb_put(o, ",\n  \"target\": ");
    json_text(o, about->target);
    sb_printf(o, ",\n  \"succeeded\": %s", about->succeeded ? "true" : "false");
    sb_printf(o, ",\n  \"totalMs\": %.1f,\n  \"stages\": [", r->total_ms);
    for (int i = 0; i < r->stage_count; i++) {
        sb_put(o, i ? ",\n    {\"name\": " : "\n    {\"name\": ");
        json_text(o, r->stages[i].name);
        sb_printf(o, ", \"ms\": %.1f}", r->stages[i].ms);
    }
    sb_put(o, r->stage_count ? "\n  ],\n  \"tidePhases\": [" : "],\n  \"tidePhases\": [");
    for (int i = 0; i < r->phase_count; i++) {
        sb_put(o, i ? ",\n    {\"name\": " : "\n    {\"name\": ");
        json_text(o, r->phases[i].name);
        sb_printf(o, ", \"ms\": %.1f}", r->phases[i].ms);
    }
    sb_printf(o, "%s],\n  \"compiles\": {\"upToDate\": %d, \"files\": [", r->phase_count ? "\n  " : "", r->reused);
    for (int i = 0; i < r->compile_count; i++) {
        sb_put(o, i ? ",\n    {\"source\": " : "\n    {\"source\": ");
        json_text(o, r->compiles[i].source);
        sb_printf(o, ", \"ms\": %.1f, \"engine\": %s}", r->compiles[i].ms, r->compiles[i].engine ? "true" : "false");
    }
    sb_printf(o, "%s]},\n  \"pack\": {\"bytes\": %llu, \"assetBytes\": %llu, \"assets\": [", r->compile_count ? "\n  " : "",
              (unsigned long long)r->pack_size, (unsigned long long)packed);
    bool any = false;
    for (int i = 0; i < r->asset_count; i++) {
        const build_report_asset *a = &r->assets[i];
        if (a->left_out) continue;
        sb_put(o, any ? ",\n    {\"name\": " : "\n    {\"name\": ");
        any = true;
        json_text(o, a->name);
        sb_put(o, ", \"type\": ");
        json_text(o, a->type);
        sb_printf(o, ", \"packBytes\": %llu, \"fileBytes\": %lld, \"importMs\": %.1f, \"files\": [",
                  (unsigned long long)a->pack_bytes, (long long)asset_files_size(a), a->import_ms);
        for (int k = 0; k < a->file_count; k++) {
            if (k) sb_put(o, ", ");
            json_text(o, a->files[k]);
        }
        sb_put(o, "]}");
    }
    sb_put(o, any ? "\n  ]},\n  \"leftOut\": [" : "]},\n  \"leftOut\": [");
    any = false;
    for (int i = 0; i < r->asset_count; i++) {
        const build_report_asset *a = &r->assets[i];
        if (!a->left_out) continue;
        sb_put(o, any ? ",\n    {\"name\": " : "\n    {\"name\": ");
        any = true;
        json_text(o, a->name);
        sb_put(o, ", \"type\": ");
        json_text(o, a->type);
        sb_put(o, ", \"why\": ");
        json_text(o, a->left_out);
        sb_printf(o, ", \"fileBytes\": %lld, \"files\": [", (long long)asset_files_size(a));
        for (int k = 0; k < a->file_count; k++) {
            if (k) sb_put(o, ", ");
            json_text(o, a->files[k]);
        }
        sb_put(o, "]}");
    }
    sb_put(o, any ? "\n  ],\n  \"cuts\": [" : "],\n  \"cuts\": [");
    for (int i = 0; i < r->cut_count; i++) {
        sb_put(o, i ? ",\n    " : "\n    ");
        json_text(o, r->cuts[i]);
    }
    sb_put(o, r->cut_count ? "\n  ],\n  \"outputs\": [" : "],\n  \"outputs\": [");
    for (int i = 0; i < r->output_count; i++) {
        sb_put(o, i ? ",\n    {\"path\": " : "\n    {\"path\": ");
        json_text(o, r->outputs[i].path);
        sb_printf(o, ", \"bytes\": %lld}", (long long)r->outputs[i].bytes);
    }
    sb_put(o, r->output_count ? "\n  ]\n}\n" : "]\n}\n");
}

// A path from the game's folder when it's in it, as a table shows it.
static const char *from_folder(const char *path, const char *folder)
{
    const size_t n = strlen(folder);
    if (n && strncmp(path, folder, n) == 0 && (path[n] == '/' || path[n] == '\\')) return path + n + 1;
    return path;
}

static void write_table(sb *o, const build_report *r, const build_report_about *about, const uint64_t packed)
{
    char size[32], files[32];
    sb_printf(o, "%s: %s %s build%s, %.2f s (tide %s)\n%s\n", about->game, about->mode, about->target,
              about->succeeded ? "" : " that failed", r->total_ms / 1e3, about->version, about->folder);

    sb_put(o, "\nTime                                        ms      %\n");
    const double total = r->total_ms > 0.0 ? r->total_ms : 1.0;
    for (int i = 0; i < r->stage_count; i++) {
        const build_report_time *t = &r->stages[i];
        sb_printf(o, "  %-38s %8.0f  %5.1f\n", t->name, t->ms, 100.0 * t->ms / total);
        if (strcmp(t->name, "compiling Tide code and assets") != 0) continue;
        for (int k = 0; k < r->phase_count; k++) sb_printf(o, "    %-36s %8.0f\n", r->phases[k].name, r->phases[k].ms);
    }

    if (r->compile_count || r->reused) {
        build_report_compile *sorted = malloc(sizeof *sorted * (size_t)(r->compile_count ? r->compile_count : 1));
        if (!sorted) abort();
        memcpy(sorted, r->compiles, sizeof *sorted * (size_t)r->compile_count);
        qsort(sorted, (size_t)r->compile_count, sizeof *sorted, by_time);
        double sum = 0.0;
        for (int i = 0; i < r->compile_count; i++) sum += sorted[i].ms;
        sb_printf(o, "\nC/C++: %d compiled (%.0f ms of compiler time), %d up to date\n", r->compile_count, sum, r->reused);
        const int shown = r->compile_count < 15 ? r->compile_count : 15;
        for (int i = 0; i < shown; i++) {
            sb_printf(o, "  %-38s %8.0f%s\n", sorted[i].source, sorted[i].ms, sorted[i].engine ? "  engine" : "");
        }
        if (shown < r->compile_count) sb_printf(o, "  ...and %d more\n", r->compile_count - shown);
        free(sorted);
    }

    build_report_asset *assets = malloc(sizeof *assets * (size_t)(r->asset_count ? r->asset_count : 1));
    if (!assets) abort();
    memcpy(assets, r->assets, sizeof *assets * (size_t)r->asset_count);
    qsort(assets, (size_t)r->asset_count, sizeof *assets, by_pack_bytes);
    int packed_count = 0;
    for (int i = 0; i < r->asset_count; i++) packed_count += !assets[i].left_out;
    bytes_text(size, sizeof size, (double)r->pack_size);
    sb_printf(o, "\nPack: %s (%llu bytes), %d asset%s\n", r->pack_size ? size : "none", (unsigned long long)r->pack_size,
              packed_count, packed_count == 1 ? "" : "s");
    if (packed_count) {
        sb_printf(o, "  %-34s %-24s %11s %11s %9s\n", "asset", "type", "in pack", "files", "import ms");
        for (int i = 0; i < r->asset_count; i++) {
            const build_report_asset *a = &assets[i];
            if (a->left_out) continue;
            bytes_text(size, sizeof size, (double)a->pack_bytes);
            bytes_text(files, sizeof files, (double)asset_files_size(a));
            sb_printf(o, "  %-34s %-24s %11s %11s %9.1f\n", a->name, a->type, a->pack_bytes ? size : "shared",
                      a->file_count ? files : "-", a->import_ms);
        }
        bytes_text(size, sizeof size, (double)(r->pack_size > packed ? r->pack_size - packed : 0));
        sb_printf(o, "  %-34s %-24s %11s\n", "(names and tables)", "", size);
    }

    if (packed_count < r->asset_count) {
        sb_printf(o, "\nLeft out: %d asset%s\n", r->asset_count - packed_count, r->asset_count - packed_count == 1 ? "" : "s");
        sb_printf(o, "  %-34s %-24s %11s  %s\n", "asset", "type", "files", "why");
        for (int i = 0; i < r->asset_count; i++) {
            const build_report_asset *a = &assets[i];
            if (!a->left_out) continue;
            bytes_text(files, sizeof files, (double)asset_files_size(a));
            sb_printf(o, "  %-34s %-24s %11s  %s\n", a->name, a->type, a->file_count ? files : "-", a->left_out);
        }
    }
    free(assets);

    if (r->cut_count) {
        sb_put(o, "\nThis mode\n");
        for (int i = 0; i < r->cut_count; i++) sb_printf(o, "  %s\n", r->cuts[i]);
    }
    if (r->output_count) {
        sb_put(o, "\nOutputs\n");
        for (int i = 0; i < r->output_count; i++) {
            bytes_text(size, sizeof size, (double)r->outputs[i].bytes);
            sb_printf(o, "  %11s  %s\n", size, from_folder(r->outputs[i].path, about->folder));
        }
    }
}

char *build_report_end(const build_report_about *about, const bool json)
{
    build_report *r = current;
    if (!r) return NULL;
    build_report_stage(NULL);
    r->total_ms = build_report_clock_ms() - r->started;
    uint64_t packed = 0;
    for (int i = 0; i < r->asset_count; i++) packed += r->assets[i].pack_bytes;
    sb o = {0};
    if (json) write_json(&o, r, about, packed);
    else write_table(&o, r, about, packed);
    char *text = copy(o.data ? o.data : "");
    free_report(r);
    current = NULL;
    return text;
}
