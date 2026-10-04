#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

typedef struct files {
    uint8_t text[64], binary[64];
    uint32_t text_size, binary_size;
    int polls, writes, forgotten, begins;
    bool copy_overwrite, move_overwrite;
    bool active;
    tide_io_error flush_error;
} files;

static tide_io_error create(void *user, const char *path, bool directory)
{
    (void)user; (void)path; (void)directory;
    return TIDE_IO_NONE;
}

static tide_io_error write_file(void *user, const char *path, const uint8_t *data, uint32_t size, bool append)
{
    files *f = user;
    const bool binary = strstr(path, ".bin") != NULL;
    uint8_t *out = binary ? f->binary : f->text;
    uint32_t *length = binary ? &f->binary_size : &f->text_size;
    if (!append) *length = 0;
    if (size > 64 - *length) return TIDE_IO_OUT_OF_SPACE;
    memcpy(out + *length, data, size);
    *length += size;
    f->writes++;
    return TIDE_IO_NONE;
}

static tide_io_error read_file(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    files *f = user;
    if (strcmp(path, "missing") == 0) return TIDE_IO_NOT_FOUND;
    const bool binary = strstr(path, ".bin") != NULL;
    *size = binary ? f->binary_size : f->text_size;
    *data = malloc(*size);
    if (*size) memcpy(*data, binary ? f->binary : f->text, *size);
    return TIDE_IO_NONE;
}

static tide_io_error copy_file(void *user, const char *source, const char *destination, bool overwrite)
{
    (void)source; (void)destination;
    ((files *)user)->copy_overwrite = overwrite;
    return TIDE_IO_NONE;
}

static tide_io_error move_file(void *user, const char *source, const char *destination, bool overwrite, bool directory)
{
    (void)source; (void)destination;
    if (!directory) ((files *)user)->move_overwrite = overwrite;
    return TIDE_IO_NONE;
}

static tide_io_error remove_file(void *user, const char *path, bool directory, bool recursive)
{
    (void)user; (void)path; (void)directory; (void)recursive;
    return TIDE_IO_NONE;
}

static tide_io_error list(void *user, const char *path, bool directories, bool recursive,
                          tide_io_entry_fn entry, void *entry_user)
{
    (void)user; (void)path; (void)recursive;
    if (!directories) {
        entry(entry_user, "saves/save.txt");
        entry(entry_user, "saves/save.bin");
    }
    return TIDE_IO_NONE;
}

static uint32_t flush_begin(void *user)
{
    ((files *)user)->polls = 0;
    ((files *)user)->begins++;
    ((files *)user)->active = true;
    return 42;
}

static bool flush_poll(void *user, uint32_t ticket, tide_io_error *error)
{
    files *f = user;
    if (ticket != 42 || !f->active) { *error = TIDE_IO_FAILED; return true; }
    if (++f->polls < 3) return false;
    *error = f->flush_error;
    f->active = false;
    return true;
}

static void flush_forget(void *user, uint32_t ticket)
{
    if (ticket == 42) { ((files *)user)->forgotten++; ((files *)user)->active = false; }
}

static tide_io provider(files *f)
{
    return (tide_io){.user = f, .persistent_data_path = "saves", .temporary_cache_path = "cache",
        .create = create, .read = read_file, .write = write_file, .copy = copy_file, .move = move_file,
        .remove = remove_file, .list = list, .flush_begin = flush_begin, .flush_poll = flush_poll,
        .flush_forget = flush_forget};
}

static bool text_is(tide_local *local, tide_text text, const char *want)
{
    const tide_str value = tide_text_read(&local->heap, text);
    return value.bytes == (int32_t)strlen(want) && memcmp(value.ptr, want, (size_t)value.bytes) == 0;
}

static void frame(tide_local *local)
{
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(NULL, NULL, 1, NULL, local, &draw, &gui);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
}

TIDE_TEST(io_local_calls_and_async_flush)
{
    files f = {0};
    const tide_io io = provider(&f);
    tide_local local = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    TIDE_CHECK(local.tide_io == &io);
    TIDE_CHECK(tide_uses_io());
    TIDE_CHECK(local.Results.wrote && local.Results.missing);
    TIDE_CHECK(text_is(&local, local.Results.text, "h\xc3\xa9llo!"));
    TIDE_CHECK(text_is(&local, local.Results.fileName, "save.txt"));
    TIDE_CHECK(text_is(&local, local.Results.parent, "saves"));
    TIDE_CHECK(text_is(&local, local.Results.extension, ".txt"));
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Results.bytes) == 3);
    tide_text_use(NULL, &local.heap);
    const uint8_t *last = tide_list_read(&local.heap, local.Results.bytes, 2, 1);
    TIDE_CHECK(last && *last == 255);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Results.paths) == 2);
    const tide_text *first = tide_list_read(&local.heap, local.Results.paths, 0, sizeof(tide_text));
    TIDE_CHECK(first && text_is(&local, *first, "saves/save.bin"));
    TIDE_CHECK(!f.copy_overwrite && f.move_overwrite);
    TIDE_CHECK(local.Results.saved == 0);
    frame(&local);
    TIDE_CHECK(local.Results.saved == 0);
    frame(&local);
    TIDE_CHECK(local.Results.saved == 0);
    frame(&local);
    TIDE_CHECK(local.Results.saved == 1 && local.Results.after == 17);
    TIDE_CHECK(f.forgotten == 1);
    TIDE_CHECK(strcmp(tide_game_api.app_id, "tests.tide.io") == 0);
    tide_local_free(&local);
    TIDE_CHECK(local.tide_io == &io);
}

TIDE_TEST(io_flush_failure_is_typed)
{
    files f = {.flush_error = TIDE_IO_OUT_OF_SPACE};
    const tide_io io = provider(&f);
    tide_local local = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    frame(&local);
    frame(&local);
    frame(&local);
    TIDE_CHECK(local.Results.saved == -IOError_OutOfSpace && local.Results.after == 17);
    tide_local_free(&local);
}

TIDE_TEST(io_cancel_flush_releases_ticket)
{
    files f = {0};
    const tide_io io = provider(&f);
    tide_local local = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    TIDE_CHECK(f.forgotten == 0);
    tide_local_free(&local);
    TIDE_CHECK(f.forgotten == 1);
}

TIDE_TEST(io_provider_is_not_saved_and_is_rebound_when_loading)
{
    files f = {0}, other = {0};
    const tide_io io = provider(&f), io2 = provider(&other);
    tide_local local = {0}, loaded = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *data = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, data, size) == size);
    const tide_io *packed = (const tide_io *)(uintptr_t)1;
    memcpy(&packed, data + offsetof(tide_local, tide_io), sizeof packed);
    TIDE_CHECK(packed == NULL);
    tide_local_set_io(&loaded, &io2);
    TIDE_CHECK(tide_local_unpack(&loaded, data, size));
    TIDE_CHECK(loaded.tide_io == &io2);
    frame(&loaded);
    frame(&loaded);
    frame(&loaded);
    frame(&loaded);
    TIDE_CHECK(other.begins == 1 && loaded.Results.saved == 1);
    tide_local_free(&loaded);
    tide_local_free(&local);
    free(data);
}

TIDE_TEST(io_restoring_in_place_restarts_a_canceled_flush)
{
    files f = {0};
    const tide_io io = provider(&f);
    tide_local local = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *data = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, data, size) == size);
    TIDE_CHECK(tide_local_unpack(&local, data, size));
    TIDE_CHECK(f.forgotten == 1 && f.begins == 1);
    for (int i = 0; i < 4; i++) frame(&local);
    TIDE_CHECK(f.begins == 2 && local.Results.saved == 1);
    tide_local_free(&local);
    free(data);
}

TIDE_TEST(io_hot_reload_rebind_cancels_and_restarts_flush)
{
    files f = {0};
    const tide_io io = provider(&f);
    tide_local local = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(f.forgotten == 1 && f.begins == 1);
    for (int i = 0; i < 4; i++) frame(&local);
    TIDE_CHECK(f.begins == 2 && local.Results.saved == 1);
    tide_local_free(&local);
}

TIDE_TEST(io_failed_unpack_does_not_cancel_source_ticket)
{
    files f = {0};
    const tide_io io = provider(&f);
    tide_local local = {0}, loaded = {0};
    tide_local_set_io(&local, &io);
    tide_local_init(&local);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *data = malloc(size);
    TIDE_CHECK(tide_local_pack(&local, data, size) == size);
    tide_local_set_io(&loaded, &io);
    TIDE_CHECK(!tide_local_unpack(&loaded, data, size - 1));
    TIDE_CHECK(f.active && f.forgotten == 0);
    for (int i = 0; i < 4; i++) frame(&local);
    TIDE_CHECK(local.Results.saved == 1);
    tide_local_free(&loaded);
    tide_local_free(&local);
    free(data);
}
