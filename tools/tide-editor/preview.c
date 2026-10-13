#include "preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define PREVIEW_LIBRARY "game-1.dll"
#else
#include <dlfcn.h>
#ifdef __APPLE__
#define PREVIEW_LIBRARY "game-1.dylib"
#else
#define PREVIEW_LIBRARY "game-1.so"
#endif
#endif

static void unload(editor_preview *p, tide_draw_list *host)
{
    if (!p->library) return;
    // Copied commands can refer to module shader metadata until the frame ends.
    tide_draw_forget(host);
    if (p->context) p->api->destroy(p->context);
#ifdef _WIN32
    FreeLibrary((HMODULE)p->library);
#else
    dlclose(p->library);
#endif
    p->library = NULL; p->api = NULL; p->context = NULL;
}

void editor_preview_close(editor_preview *p, tide_draw_list *host)
{
    if (p->builder) { int code; while (!sys_wait(p->builder, 500, &code)) {} }
    p->builder = NULL;
    unload(p, host);
    free(p->directory); free(p->building_directory);
    *p = (editor_preview){0};
}

static bool load(editor_preview *p, const char *directory, tide_draw_list *host)
{
    char *path = path_join(directory, PREVIEW_LIBRARY);
#ifdef _WIN32
    void *library = (void *)LoadLibraryA(path);
    FARPROC symbol = library ? GetProcAddress((HMODULE)library, "tide_editor_preview") : NULL;
#else
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    void *symbol = library ? dlsym(library, "tide_editor_preview") : NULL;
#endif
    free(path);
    tide_editor_preview_entry entry = NULL;
    _Static_assert(sizeof entry == sizeof symbol, "native function pointer ABI");
    memcpy(&entry, &symbol, sizeof entry);
    const tide_editor_preview_api *api = entry ? entry() : NULL;
    if (!api || api->version != TIDE_EDITOR_PREVIEW_VERSION || !api->create || !api->frame || !api->destroy) {
        snprintf(p->message, sizeof p->message, "Preview module could not be loaded; rebuild with this Tide installation.");
        if (library) {
#ifdef _WIN32
            FreeLibrary((HMODULE)library);
#else
            dlclose(library);
#endif
        }
        return false;
    }
    char *pack_path = path_join(directory, "pack-1");
    size_t size = 0; char *pack = sys_read_file(pack_path, &size); free(pack_path);
    void *context = api->create((const uint8_t *)pack, size); free(pack);
    if (!context) {
        snprintf(p->message, sizeof p->message, "Preview initialization failed; the previous preview is retained.");
#ifdef _WIN32
        FreeLibrary((HMODULE)library);
#else
        dlclose(library);
#endif
        return false;
    }
    unload(p, host);
    p->library = library; p->api = api; p->context = context;
    snprintf(p->message, sizeof p->message, api->views ? "Compiled views preview; simulation and gameplay handlers are stopped." : "No RunInEditor or EditorOnly views in this project.");
    return true;
}

void editor_preview_update(editor_preview *p, const editor_document *d, tide_draw_list *host, int wait_ms)
{
    if (!p->enabled) {
        if (p->builder) { int code; if (!sys_wait(p->builder, wait_ms, &code)) return; p->builder = NULL; }
        if (p->library || p->building_directory || p->directory || p->attempted_revision) {
            const bool local = p->local;
            editor_preview_close(p, host); p->local = local;
        }
        return;
    }
    if (p->builder) {
        int code;
        if (!sys_wait(p->builder, wait_ms, &code)) return;
        p->builder = NULL;
        if (p->building_revision == d->revision) {
            if (!code && load(p, p->building_directory, host)) {
                free(p->directory); p->directory = p->building_directory; p->building_directory = NULL;
                p->revision = p->building_revision;
            } else if (code) {
                char *log = path_join(p->building_directory, "build.log"); char *said = sys_read_file(log, NULL); free(log);
                snprintf(p->message, sizeof p->message, "Preview build failed%s:\n%s", p->api ? "; showing previous compiled state" : "", said ? said : "See the preview build log."); free(said);
            }
        }
        free(p->building_directory); p->building_directory = NULL;
    }
    if (p->attempted_revision == d->revision) return;
    p->attempted_revision = d->revision;
    if (d->errors || !d->prefabs.count) {
        snprintf(p->message, sizeof p->message, "Preview needs a valid project; fix the diagnostics below."); return;
    }
    char *work = path_join(d->project, ".tide/editor-preview");
    char name[96]; snprintf(name, sizeof name, "%u-%llu-%s", sys_pid(), (unsigned long long)d->revision, p->local ? "local" : "match");
    p->building_directory = path_join(work, name); free(work);
    if (!sys_mkdirs(p->building_directory)) { snprintf(p->message, sizeof p->message, "Cannot create preview build directory."); return; }
    char *draft = path_join(p->building_directory, "draft.txt"), *log = path_join(p->building_directory, "build.log");
    char *bin = sys_exe_dir();
#ifdef _WIN32
    char *cli = path_join(bin, "tide.exe");
#else
    char *cli = path_join(bin, "tide");
#endif
    free(bin);
    char *prefab = str_to_cstr(d->prefabs.items[d->active]->qualified);
    const char *argv[] = {cli, "_editor-preview", d->project, d->path, draft, prefab, p->building_directory, p->local ? "local" : "match", NULL};
    if (sys_write_text(draft, d->text)) p->builder = sys_start_logged(argv, NULL, log);
    p->building_revision = d->revision;
    snprintf(p->message, sizeof p->message, p->builder ? "Building the current authored preview..." : "Could not start the preview compiler.");
    free(draft); free(log); free(cli);
}

bool editor_preview_draw(editor_preview *p, tide_draw_list *host, float seconds, tide_rect viewport,
                          tide_float4x4 camera, tide_float4x4 projection, tide_measure_fn measure)
{
    if (!p->enabled || !p->api) return false;
    const tide_draw_list *draw = p->api->frame(p->context, seconds, viewport, camera, projection, measure);
    if (draw) tide_draw_append(host, draw);
    return draw != NULL;
}
