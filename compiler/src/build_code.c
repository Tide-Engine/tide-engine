// Build code (docs/spec.md, Build code; build_code.h): what a game's assets' and constants' code made, found again
// by a key of what made it, or made by running it. Running it is compiling the program codegen makes for it (the
// game's C, the engine's sources and a main: build_codegen.inc) with the build's C compiler, and reading what it
// wrote: for each asset or constant, in the order build_code_order put them, its index and its value's blobs (a
// 64-bit size, then that many bytes each).
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "build_code.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_cache.h"
#include "asset_files.h"
#include "audio_files.h"
#include "types.h"
#include "asset_usage.h"
#include "folders.h"
#include "version.h"
#include "tide/draw.h"
#include "tide/sha256.h"
#include "tide/wav.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif

typedef VEC(const char *) arg_list;
typedef VEC(char *) path_list;
typedef VEC(decl *) decl_list;

// How long build code can run before the build stops it (Provisional: docs/spec.md, Build code)
#define BUILD_CODE_SECONDS 600u

// ---------------------------------------------------------------------------
// Processes: the C compiler, and the program it makes, each with its output in a file

typedef struct build_process {
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
    bool started;
} build_process;

#ifdef _WIN32
// A command line as CreateProcess's C runtime splits it again (Microsoft's rules for quotes and backslashes)
static void quote_arg(sb *o, const char *arg)
{
    if (*arg && !strpbrk(arg, " \t\"")) {
        sb_put(o, arg);
        return;
    }
    sb_put(o, "\"");
    for (const char *p = arg;; p++) {
        int slashes = 0;
        while (*p == '\\') slashes++, p++;
        if (!*p) {
            for (int i = 0; i < slashes * 2; i++) sb_put(o, "\\");
            break;
        }
        if (*p == '"') {
            for (int i = 0; i < slashes * 2 + 1; i++) sb_put(o, "\\");
        } else {
            for (int i = 0; i < slashes; i++) sb_put(o, "\\");
        }
        sb_putn(o, p, 1);
    }
    sb_put(o, "\"");
}
#endif

static build_process start_process(const char *const *argv, const char *log)
{
    build_process p = {0};
#ifdef _WIN32
    sb line = {0};
    for (int i = 0; argv[i]; i++) {
        if (i) sb_put(&line, " ");
        if (i == 0) { // The program's own path, which Windows looks for with its own slashes
            char *program = str_to_cstr(str_from(argv[0]));
            for (char *c = program; *c; c++) if (*c == '/') *c = 0x5C; // A backslash
            quote_arg(&line, program);
        } else quote_arg(&line, argv[i]);
    }
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE out = CreateFileA(log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return p;
    STARTUPINFOA si = {0};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = si.hStdError = out;
    PROCESS_INFORMATION pi = {0};
    char *command = malloc(line.len + 1);
    if (!command) abort();
    memcpy(command, line.data, line.len + 1);
    // Below normal: build code and the engine's compiles share the machine with whoever is building
    if (CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        p.process = pi.hProcess;
        p.started = true;
    }
    free(command);
    CloseHandle(out);
#else
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    p.started = posix_spawnp(&p.pid, argv[0], &actions, NULL, (char *const *)argv, environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
#endif
    return p;
}

// Its exit code once it's done, or -1 if it couldn't start or ran past `seconds` (0: no limit), when it's stopped.
static int wait_process(build_process *p, const unsigned seconds)
{
    if (!p->started) return -1;
    p->started = false;
#ifdef _WIN32
    if (WaitForSingleObject(p->process, seconds ? seconds * 1000u : INFINITE) != WAIT_OBJECT_0) {
        TerminateProcess(p->process, 1);
        WaitForSingleObject(p->process, INFINITE);
        CloseHandle(p->process);
        return -1;
    }
    DWORD code = 1;
    GetExitCodeProcess(p->process, &code);
    CloseHandle(p->process);
    return (int)code;
#else
    int status = 0;
    for (unsigned long waited = 0;; waited++) {
        const pid_t done = waitpid(p->pid, &status, seconds ? WNOHANG : 0);
        if (done == p->pid) break;
        if (done < 0) return -1;
        if (seconds && waited >= (unsigned long)seconds * 100u) {
            kill(p->pid, SIGKILL);
            waitpid(p->pid, &status, 0);
            return -1;
        }
        const struct timespec tick = {0, 10000000L};
        nanosleep(&tick, NULL);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

static unsigned processors(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const unsigned n = (unsigned)info.dwNumberOfProcessors;
#else
    const long got = sysconf(_SC_NPROCESSORS_ONLN);
    const unsigned n = got > 0 ? (unsigned)got : 1u;
#endif
    return n < 1u ? 1u : n > 8u ? 8u : n;
}

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n >= 0 ? malloc((size_t)n + 1u) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[n] = 0;
        *size = (size_t)n;
    }
    return data;
}

// What a log says, for a diagnostic's notes: its last lines, at most `most` of them.
static void note_log(const char *path, const int most)
{
    size_t size = 0;
    char *text = (char *)read_all(path, &size);
    if (!text || !size) {
        free(text);
        return;
    }
    int lines = 0;
    size_t from = size;
    while (from > 0 && lines <= most) {
        from--;
        if (text[from] == '\n' && from + 1 < size) lines++;
    }
    if (lines > most) from++;
    char *line = text + from;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        const size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        if (*line) diag_note("%s", line);
        if (!end) break;
        line = end + 1;
    }
    free(text);
}

// ---------------------------------------------------------------------------
// What was made, by key: in this process, then in the game's cache

typedef struct made_entry {
    uint8_t key[32];
    uint8_t *data;
    size_t size;
} made_entry;

static VEC(made_entry) made;

static const char *cache_folder(const codegen_options *opts)
{
    return opts->game_folder ? opts->game_folder : opts->out_dir;
}

static const made_entry *find_made(const codegen_options *opts, const uint8_t key[32])
{
    for (int i = 0; i < made.count; i++) {
        if (!memcmp(made.items[i].key, key, 32)) return &made.items[i];
    }
    size_t size = 0;
    uint8_t *data = asset_cache_read(cache_folder(opts), key, &size);
    if (!data) return NULL;
    made_entry e = {.data = data, .size = size};
    memcpy(e.key, key, 32);
    vec_push(made, e);
    return &made.items[made.count - 1];
}

static void keep_made(const codegen_options *opts, const uint8_t key[32], const uint8_t *data, const size_t size)
{
    made_entry e = {.data = malloc(size ? size : 1u), .size = size};
    if (!e.data) abort();
    if (size) memcpy(e.data, data, size);
    memcpy(e.key, key, 32);
    vec_push(made, e);
    // Every value has a blob's size at least, so it's never empty, which the cache refuses
    asset_cache_write(cache_folder(opts), key, data, size);
}

// ---------------------------------------------------------------------------
// Order: what one reads, first

typedef struct root_info {
    decl *d;
    uint8_t key[32];
    const decl **reads;
    int read_count;
    int state; // 0 not placed, 1 placing, 2 placed
} root_info;

static int root_of(const root_info *roots, const int count, const decl *d)
{
    for (int i = 0; i < count; i++) {
        if (roots[i].d == d) return i;
    }
    return -1;
}

static bool place(root_info *roots, const int count, const int i, decl_list *order)
{
    if (roots[i].state == 2) return true;
    if (roots[i].state == 1) {
        diag_error(roots[i].d->at, "'" STR_FMT "' is made by build code that reads it, through what it reads",
                   STR_ARG(roots[i].d->qualified));
        diag_note("an asset's or a constant's code can read what other code made, but not, through it, what it makes itself");
        return false;
    }
    roots[i].state = 1;
    for (int k = 0; k < roots[i].read_count; k++) {
        const int other = root_of(roots, count, roots[i].reads[k]);
        if (other >= 0 && !place(roots, count, other, order)) return false;
    }
    roots[i].state = 2;
    vec_push(*order, roots[i].d);
    return true;
}

// How many blobs a root's value is (build_codegen.inc's gen_build_main writes them)
static int blobs_of(const decl *d)
{
    if (d->kind == DECL_CONST) return 1;
    switch (d->asset_kind) {
    case ASSET_GRID: case ASSET_TEXTURE: case ASSET_CLIP: return 2;
    case ASSET_MESH: return 3; // Its vertices, its triangles and its levels of detail
    default: return 1;
    }
}

// ---------------------------------------------------------------------------
// Reading what was made into the declarations

typedef struct blob {
    const uint8_t *data;
    uint64_t size;
} blob;

static bool take_blobs(const uint8_t *data, const size_t size, size_t *at, blob *out, const int count)
{
    for (int i = 0; i < count; i++) {
        uint64_t n;
        if (size - *at < sizeof n) return false;
        memcpy(&n, data + *at, sizeof n);
        *at += sizeof n;
        if (n > size - *at) return false;
        out[i] = (blob){data + *at, n};
        *at += (size_t)n;
    }
    return true;
}

static int32_t utf8_chars(const uint8_t *data, const size_t size)
{
    int32_t n = 0;
    for (size_t i = 0; i < size; i++) n += (data[i] & 0xC0u) != 0x80u;
    return n;
}

static uint8_t *copy_bytes(const void *data, const size_t size)
{
    uint8_t *out = malloc(size ? size : 1u);
    if (!out) abort();
    if (size) memcpy(out, data, size);
    return out;
}

// The asset's one item, as its file was (its key and where it's read from, for whoever watches it) or new
static asset_match *built_item(decl *d)
{
    asset_value *v = NEW(asset_value);
    v->kind = d->asset_kind;
    v->items = NEW(asset_match);
    v->count = 1;
    if (d->asset && d->asset->count) {
        v->items[0].key = d->asset->items[0].key;
        v->items[0].abs = d->asset->items[0].abs;
        v->items[0].sides = d->asset->items[0].sides;
        v->items[0].side_count = d->asset->items[0].side_count;
    } else v->items[0].key = str_to_cstr(d->qualified);
    d->asset = v;
    return &v->items[0];
}

static bool finite_floats(const uint8_t *data, const size_t count)
{
    for (size_t i = 0; i < count; i++) {
        float f;
        memcpy(&f, data + i * sizeof f, sizeof f);
        if (f - f != 0.0f) return false;
    }
    return true;
}

// A value's bytes, as the program wrote them, into its declaration (and for a list, the pack's assets).
static bool apply(program *prog, decl *d, const uint8_t *data, const size_t size)
{
    blob b[3];
    size_t at = 0;
    if (!take_blobs(data, size, &at, b, blobs_of(d)) || at != size) {
        diag_error(d->at, "what build code made of '" STR_FMT "' was cut short; build the game again", STR_ARG(d->qualified));
        return false;
    }
    build_value *v = NEW(build_value);
    d->built = v;
    if (d->kind == DECL_CONST) {
        v->data = copy_bytes(b[0].data, (size_t)b[0].size);
        v->size = (size_t)b[0].size;
        if (d->return_type.kind == TY_STRING) v->chars = utf8_chars(v->data, v->size);
        tide_sha256 s;
        tide_sha256_init(&s);
        if (d->return_type.kind == TY_LIST) {
            const type element = d->return_type.decl->fields.items[0].type;
            v->element = (uint32_t)type_layout(element).size;
            if (!v->element || v->size % v->element) {
                diag_error(d->at, "what build code made of '" STR_FMT "' isn't a list of its elements", STR_ARG(d->qualified));
                return false;
            }
            v->count = (uint32_t)(v->size / v->element);
            tide_sha256_add(&s, &v->element, sizeof v->element);
        }
        tide_sha256_add(&s, v->data, v->size);
        tide_sha256_end(&s, v->hash);
        if (d->return_type.kind == TY_LIST) { // The pack's, as an asset's list is
            d->asset_kind = ASSET_VALUES;
            asset_match *item = built_item(d);
            item->data = v->data;
            item->size = v->size;
            item->built = true;
            item->element = v->element;
            item->count = v->count;
            memcpy(item->hash, v->hash, 32);
            vec_push(prog->assets, d);
        }
        return true;
    }
    tide_sha256 s;
    tide_sha256_init(&s);
    tide_sha256_add(&s, "built", 5);
    for (int i = 0; i < blobs_of(d); i++) {
        tide_sha256_add(&s, &b[i].size, sizeof b[i].size);
        tide_sha256_add(&s, b[i].data, (size_t)b[i].size);
    }
    tide_sha256_end(&s, v->hash);
    switch (d->asset_kind) {
    case ASSET_TEXT: case ASSET_BYTES: {
        asset_match *item = built_item(d);
        v->data = copy_bytes(b[0].data, (size_t)b[0].size);
        v->size = (size_t)b[0].size;
        item->data = v->data;
        item->size = v->size;
        memcpy(item->hash, v->hash, 32);
        return true;
    }
    case ASSET_GRID: case ASSET_TEXTURE: {
        int32_t wh[2] = {0, 0};
        if (b[0].size == sizeof wh) memcpy(wh, b[0].data, sizeof wh);
        if (wh[0] <= 0 || wh[1] <= 0) {
            diag_error(d->at, "build code left '" STR_FMT "' with no size: an image has one", STR_ARG(d->name));
            diag_note("give the grid its size as the code makes it: '" STR_FMT " = Grid2(256, 256);'", STR_ARG(d->name));
            return false;
        }
        if (wh[0] > 16384 || wh[1] > 16384 || (uint64_t)wh[0] * (uint64_t)wh[1] > 16777216u) {
            diag_error(d->at, "build code made '" STR_FMT "' %d by %d: an image is at most 16384 pixels a side, and "
                              "4096 by 4096 pixels in all", STR_ARG(d->name), (int)wh[0], (int)wh[1]);
            return false;
        }
        const size_t pixels = (size_t)wh[0] * (size_t)wh[1];
        if (b[1].size != pixels * sizeof(tide_color)) {
            diag_error(d->at, "what build code made of '" STR_FMT "' isn't its pixels", STR_ARG(d->qualified));
            return false;
        }
        if (!finite_floats(b[1].data, pixels * 4u)) {
            diag_error(d->at, "build code left a pixel of '" STR_FMT "' that isn't a number (NaN or infinite)", STR_ARG(d->name));
            return false;
        }
        asset_match *item = built_item(d);
        v->width = wh[0];
        v->height = wh[1];
        item->image.width = (uint32_t)wh[0];
        item->image.height = (uint32_t)wh[1];
        item->image.pixels = (tide_color *)copy_bytes(b[1].data, (size_t)b[1].size);
        memcpy(item->hash, v->hash, 32);
        return true;
    }
    case ASSET_MESH: {
        const decl *fn = d->build_fn;
        const type t = fn->params.items[0].type;
        const decl *vertex = t.kind == TY_STRUCT && t.decl->asset_ref_of ? t.decl->asset_ref_of->fields.items[0].type.decl : NULL;
        const uint32_t vertex_size = vertex ? (uint32_t)type_layout((type){TY_STRUCT, (decl *)vertex}).size : 0u;
        if (!vertex_size || b[0].size % vertex_size || b[1].size % 4u) {
            diag_error(d->at, "what build code made of '" STR_FMT "' isn't its vertices and triangles", STR_ARG(d->qualified));
            return false;
        }
        const uint64_t vertices = b[0].size / vertex_size, indices = b[1].size / 4u;
        if (vertices > UINT32_MAX || indices > UINT32_MAX) {
            diag_error(d->at, "build code made '" STR_FMT "' too big for a mesh", STR_ARG(d->name));
            return false;
        }
        if (indices % 3u) {
            diag_error(d->at, "build code left '" STR_FMT "' with %llu indices: a mesh's triangles are three each",
                       STR_ARG(d->name), (unsigned long long)indices);
            return false;
        }
        uint32_t *index = malloc((size_t)indices * sizeof *index + 1u);
        if (!index) abort();
        for (uint64_t i = 0; i < indices; i++) {
            int32_t n;
            memcpy(&n, b[1].data + i * 4u, sizeof n);
            if (n < 0 || (uint64_t)n >= vertices) {
                diag_error(d->at, "build code left '" STR_FMT "' with a triangle of vertex %d, and it has %llu vertices",
                           STR_ARG(d->name), (int)n, (unsigned long long)vertices);
                free(index);
                return false;
            }
            index[i] = (uint32_t)n;
        }
        // Its levels: each one's error, and its triangles, of the same vertices (MeshLevel)
        tide_mesh_levels *levels = NULL;
        if (b[2].size) {
            levels = NEW(tide_mesh_levels);
            VEC(uint32_t) all = {0};
            size_t from = 0;
            float before = 0.0f;
            while (from < b[2].size) {
                float error;
                int32_t n;
                if (b[2].size - from < 8u) break;
                memcpy(&error, b[2].data + from, 4);
                memcpy(&n, b[2].data + from + 4u, 4);
                from += 8u;
                const uint32_t level = levels->count;
                if (level >= TIDE_MESH_LEVELS) {
                    diag_error(d->at, "build code made '" STR_FMT "' more than %d levels of detail", STR_ARG(d->name), (int)TIDE_MESH_LEVELS);
                    diag_note("a mesh has its full triangles and at most %d simpler levels", (int)TIDE_MESH_LEVELS);
                    return false;
                }
                if (!(error - error == 0.0f) || error < before || error <= 0.0f) {
                    diag_error(d->at, "level %u of '" STR_FMT "' has an error of %g: each level's is a positive distance, in the "
                                      "mesh's units, and never less than the level's before it", level + 1u, STR_ARG(d->name), (double)error);
                    return false;
                }
                if (n <= 0 || n % 3 || (uint64_t)n * 4u > b[2].size - from) {
                    diag_error(d->at, "level %u of '" STR_FMT "' has %d indices: a level's triangles are three each, and it has some",
                               level + 1u, STR_ARG(d->name), (int)n);
                    return false;
                }
                levels->error[level] = error;
                levels->first[level] = (uint32_t)all.count;
                levels->indices[level] = (uint32_t)n;
                for (int32_t k = 0; k < n; k++) {
                    int32_t index;
                    memcpy(&index, b[2].data + from + (size_t)k * 4u, 4);
                    if (index < 0 || (uint64_t)index >= vertices) {
                        diag_error(d->at, "level %u of '" STR_FMT "' has a triangle of vertex %d, and the mesh has %llu vertices",
                                   level + 1u, STR_ARG(d->name), (int)index, (unsigned long long)vertices);
                        return false;
                    }
                    vec_push(all, (uint32_t)index);
                }
                from += (size_t)n * 4u;
                levels->count++;
                before = error;
            }
            levels->index = all.items;
            levels->index_count = (uint32_t)all.count;
            if (d->lod) {
                diag_error(d->at, "'" STR_FMT "' has [LOD], which makes its levels, and its code made levels too", STR_ARG(d->name));
                diag_note("keep one: [LOD] for the levels the build makes, or the code's own; [LOD(n)] is for the tolerance, "
                          "and code that makes levels can scale its errors instead");
                return false;
            }
        }
        asset_match *item = built_item(d);
        item->levels = levels;
        v->data = copy_bytes(b[0].data, (size_t)b[0].size);
        v->size = (size_t)b[0].size;
        v->element = vertex_size;
        v->count = (uint32_t)vertices;
        v->indices = index;
        v->index_count = (uint32_t)indices;
        item->built = true;
        item->data = v->data;
        item->size = v->size;
        item->element = vertex_size;
        item->count = v->count;
        item->mesh.vertices = v->count;
        item->mesh.indices = v->index_count;
        item->mesh.index = index;
        memcpy(item->hash, v->hash, 32);
        return true;
    }
    case ASSET_CLIP: {
        int32_t shape[2] = {0, 0};
        if (b[0].size == sizeof shape) memcpy(shape, b[0].data, sizeof shape);
        const int32_t rate = shape[0], channels = shape[1];
        if (channels < 1 || channels > 2 || rate < 1 || rate > 384000) {
            diag_error(d->at, "build code left '" STR_FMT "' at %d samples a second in %d channels: a clip is mono or stereo, "
                              "at 1 to 384000 samples a second", STR_ARG(d->name), (int)rate, (int)channels);
            return false;
        }
        const size_t samples = (size_t)(b[1].size / sizeof(float));
        if (b[1].size % (sizeof(float) * (size_t)channels) || samples > TIDE_SOUND_MAX_SAMPLES - 1u) {
            diag_error(d->at, "build code left '" STR_FMT "' with %zu samples: %s", STR_ARG(d->name), samples,
                       samples > TIDE_SOUND_MAX_SAMPLES - 1u ? "a clip holds fewer" : "a stereo clip's are left and right, in pairs");
            return false;
        }
        if (!finite_floats(b[1].data, samples)) {
            diag_error(d->at, "build code left a sample of '" STR_FMT "' that isn't a number (NaN or infinite)", STR_ARG(d->name));
            return false;
        }
        size_t wav_size = 0;
        uint8_t *wav = audio_float_wav(b[1].data, (uint32_t)(samples / (size_t)channels), (uint32_t)channels, (uint32_t)rate, &wav_size);
        asset_match *item = built_item(d);
        item->data = wav;
        item->size = wav_size;
        v->rate = rate;
        v->channels = channels;
        memcpy(item->hash, v->hash, 32);
        return true;
    }
    default:
        diag_error(d->at, "build code can't make '" STR_FMT "''s kind of asset", STR_ARG(d->qualified));
        return false;
    }
}

// ---------------------------------------------------------------------------
// Running it

typedef struct engine_files {
    path_list sources;
    tide_sha256 *hash;
} engine_files;

static void hash_file(tide_sha256 *hash, const char *path)
{
    size_t size = 0;
    uint8_t *data = read_all(path, &size);
    tide_sha256_add(hash, path, strlen(path));
    if (data) tide_sha256_add(hash, data, size);
    free(data);
}

static void found_source(void *user, const char *path)
{
    engine_files *f = user;
    vec_push(f->sources, str_to_cstr(str_from(path)));
    hash_file(f->hash, path);
}

static void found_header(void *user, const char *path)
{
    hash_file(((engine_files *)user)->hash, path);
}

static void push_args(arg_list *argv, const char *const *more)
{
    for (int i = 0; more && more[i]; i++) vec_push(*argv, more[i]);
}

// The engine's objects, compiled once for these flags and these sources, in `folder`
static bool engine_objects(const codegen_options *opts, const char *folder, const char *const *common, path_list *objects)
{
    tide_sha256 hash;
    tide_sha256_init(&hash);
    for (int i = 0; opts->build_cc[i]; i++) tide_sha256_add(&hash, opts->build_cc[i], strlen(opts->build_cc[i]) + 1);
    for (int i = 0; common[i]; i++) tide_sha256_add(&hash, common[i], strlen(common[i]) + 1);
    engine_files files = {.hash = &hash};
    sb engine = {0}, include = {0};
    sb_printf(&engine, "%s/", opts->build_engine);
    sb_printf(&include, "%s/", opts->build_include);
    folder_find(engine.data, ".c", found_source, &files);
    folder_find(engine.data, ".h", found_header, &files);
    folder_find(include.data, ".h", found_header, &files);
    if (!files.sources.count) {
        diag_error((loc){0}, "build code needs the engine's sources, and '%s' has none", opts->build_engine);
        return false;
    }
    uint8_t key[32];
    tide_sha256_end(&hash, key);
    char hex[17];
    for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02x", key[i]);
    sb dir = {0};
    sb_printf(&dir, "%s/engine-%s", folder, hex);
    if (!folder_make(dir.data)) {
        diag_error((loc){0}, "can't make the folder '%s' for build code's program", dir.data);
        return false;
    }
    sb stamp = {0};
    sb_printf(&stamp, "%s/done", dir.data);
    const bool done = folder_file_info(stamp.data, NULL, NULL);
    const unsigned most = processors();
    build_process *running = calloc((size_t)files.sources.count, sizeof *running);
    if (!running) abort();
    for (int i = 0; i < files.sources.count; i++) {
        sb object = {0};
        sb_printf(&object, "%s/%d.o", dir.data, i);
        vec_push(*objects, object.data);
    }
    bool ok = true;
    int failed = -1, waited = 0;
    for (int i = 0; !done && i < files.sources.count; i++) {
        if (i - waited >= (int)most && wait_process(&running[waited++], 0) != 0 && ok) ok = false, failed = waited - 1;
        arg_list argv = {0};
        push_args(&argv, opts->build_cc);
        push_args(&argv, common);
        vec_push(argv, "-c");
        vec_push(argv, files.sources.items[i]);
        vec_push(argv, "-o");
        vec_push(argv, objects->items[i]);
        vec_push(argv, NULL);
        sb log = {0};
        sb_printf(&log, "%s/%d.log", dir.data, i);
        running[i] = start_process(argv.items, log.data);
        if (!running[i].started && ok) ok = false, failed = i;
    }
    for (; !done && waited < files.sources.count; waited++) {
        if (wait_process(&running[waited], 0) != 0 && ok) ok = false, failed = waited;
    }
    if (!ok) {
        diag_error((loc){0}, "the engine's sources didn't compile for build code's program: '%s'",
                   failed >= 0 ? files.sources.items[failed] : "?");
        if (failed >= 0) {
            sb log = {0};
            sb_printf(&log, "%s/%d.log", dir.data, failed);
            note_log(log.data, 12);
        }
        diag_note("the compiler was '%s'; check that it's clang and can build C for this machine", opts->build_cc[0]);
    } else if (!done) {
        FILE *f = fopen(stamp.data, "wb");
        if (f) fclose(f);
    }
    free(running);
    return ok;
}

static bool run_program(program *prog, const codegen_options *opts, root_info *roots, const int count)
{
    const decl *first = roots[0].d;
    if (!opts->build_cc || !opts->build_cc[0] || !opts->build_include || !opts->build_engine) {
        diag_error(first->at, "'" STR_FMT "' is made by build code, and this build has no C compiler to run it with",
                   STR_ARG(first->qualified));
        diag_note("tide run and tide build compile it; tidec takes '--build-cc <clang>', '--build-include <engine/include>' "
                  "and '--build-engine <engine/src>'");
        return false;
    }
    sb folder = {0};
    sb_printf(&folder, "%s/.tide/build-code", cache_folder(opts));
    if (!folder_make(folder.data)) {
        diag_error(first->at, "can't make the folder '%s' for build code's program", folder.data);
        return false;
    }
    // The program: the game's C and its main, generated from `prog`, and a pack of what its code starts from
    sb pack = {0};
    sb_printf(&pack, "%s/build.pack", folder.data);
    codegen_options build = {0};
    build.name = "build";
    build.out_dir = folder.data;
    build.pack = pack.data;
    build.game_folder = opts->game_folder;
    build.defines = opts->defines;
    build.define_count = opts->define_count;
    build.external_identity = opts->external_identity;
    build.scene = opts->scene;
    build.build_program = true;
    remove(pack.data);
    if (!codegen(prog, &build) || diag_error_count()) return false;
    static const char *const common_flags[] = {"-std=c17", "-O1", "-fno-fast-math", "-ffp-contract=off", "-w", NULL};
    arg_list common = {0};
    push_args(&common, common_flags);
    push_args(&common, opts->build_flags);
    sb include = {0};
    sb_printf(&include, "-I%s", opts->build_include);
    vec_push(common, include.data);
    vec_push(common, NULL);
    path_list objects = {0};
    if (!engine_objects(opts, folder.data, common.items, &objects)) return false;
    sb exe = {0}, source = {0}, log = {0};
#ifdef _WIN32
    sb_printf(&exe, "%s/build.exe", folder.data);
#else
    sb_printf(&exe, "%s/build", folder.data);
#endif
    sb_printf(&source, "%s/build.c", folder.data);
    sb_printf(&log, "%s/build.log", folder.data);
    arg_list argv = {0};
    push_args(&argv, opts->build_cc);
    for (int i = 0; common.items[i]; i++) vec_push(argv, common.items[i]);
    sb here = {0};
    sb_printf(&here, "-I%s", folder.data);
    vec_push(argv, here.data);
    vec_push(argv, source.data);
    for (int i = 0; i < objects.count; i++) vec_push(argv, objects.items[i]);
    vec_push(argv, "-o");
    vec_push(argv, exe.data);
    push_args(&argv, opts->build_libs);
#if !defined(_WIN32)
    vec_push(argv, "-lm");
    vec_push(argv, "-lpthread");
#endif
    vec_push(argv, NULL);
    build_process cc = start_process(argv.items, log.data);
    if (wait_process(&cc, 0) != 0) {
        diag_error(first->at, "build code's program didn't compile");
        note_log(log.data, 12);
        return false;
    }
    sb results = {0}, run_log = {0};
    sb_printf(&results, "%s/results.bin", folder.data);
    sb_printf(&run_log, "%s/run.log", folder.data);
    remove(results.data);
    const char *const run[] = {exe.data, pack.data, results.data, NULL};
    build_process p = start_process(run, run_log.data);
    const int code = wait_process(&p, BUILD_CODE_SECONDS);
    size_t size = 0;
    uint8_t *data = read_all(results.data, &size);
    // What it wrote, each root's value in turn: what's whole is kept, though something after it failed
    size_t at = 0;
    int finished = 0;
    while (data && size - at >= 4u) {
        uint32_t index;
        memcpy(&index, data + at, sizeof index);
        if (index >= (uint32_t)count) break;
        const size_t start = at + sizeof index;
        size_t end = start;
        blob b[3];
        if (!take_blobs(data, size, &end, b, blobs_of(roots[index].d))) break;
        keep_made(opts, roots[index].key, data + start, end - start);
        at = end;
        finished++;
    }
    free(data);
    if (code != 0 || finished != count) {
        const decl *stopped = finished < count ? roots[finished].d : first;
        if (code < 0) diag_error(stopped->at, "build code making '" STR_FMT "' didn't start, or ran for more than %u minutes "
                                 "and the build stopped it", STR_ARG(stopped->qualified), BUILD_CODE_SECONDS / 60u);
        else diag_error(stopped->at, "build code making '" STR_FMT "' failed", STR_ARG(stopped->qualified));
        note_log(run_log.data, 12);
        return false;
    }
    // Debug.Log in build code: what it said, said as the game builds
    size_t said_size = 0;
    char *said = (char *)read_all(run_log.data, &said_size);
    if (said && said_size) fprintf(stderr, "%s%s", said, said[said_size - 1] == '\n' ? "" : "\n");
    free(said);
    return true;
}

bool build_code_run(program *prog, const codegen_options *opts, bool *again)
{
    *again = false;
    const int count = prog->build_roots.count;
    if (!count) return true;
    root_info *roots = arena_alloc(sizeof *roots * (size_t)count);
    for (int i = 0; i < count; i++) {
        roots[i] = (root_info){.d = prog->build_roots.items[i]};
        program_build_key(prog, roots[i].d, roots[i].key, &roots[i].reads, &roots[i].read_count);
    }
    decl_list order = {0};
    for (int i = 0; i < count; i++) {
        if (!place(roots, count, i, &order)) return false;
    }
    for (int i = 0; i < count; i++) prog->build_roots.items[i] = order.items[i];
    // Keys in the same order: roots[i] is build_roots.items[i]
    root_info *ordered = arena_alloc(sizeof *ordered * (size_t)count);
    for (int i = 0; i < count; i++) ordered[i] = roots[root_of(roots, count, order.items[i])];
    bool all = true;
    for (int i = 0; i < count && all; i++) all = find_made(opts, ordered[i].key) != NULL;
    if (!all) {
        if (!run_program(prog, opts, ordered, count)) return false;
        *again = true;
        return true;
    }
    bool ok = true;
    for (int i = 0; i < count; i++) {
        const made_entry *e = find_made(opts, ordered[i].key);
        ok &= apply(prog, ordered[i].d, e->data, e->size);
    }
    return ok;
}
