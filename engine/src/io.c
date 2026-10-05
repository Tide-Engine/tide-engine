#include "tide/io.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static tide_io_void_result result(const tide_io_error error)
{
    return (tide_io_void_result){error, error == TIDE_IO_NONE};
}

// Reject overlong encodings, surrogate code points and values past Unicode.
static bool utf8(const uint8_t *data, const uint32_t size)
{
    for (uint32_t i = 0; i < size;) {
        const uint8_t first = data[i++];
        if (first < 0x80) continue;
        uint32_t code, count, minimum;
        if (first >= 0xC2 && first <= 0xDF) { code = first & 0x1Fu; count = 1; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { code = first & 0x0Fu; count = 2; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { code = first & 7u; count = 3; minimum = 0x10000; }
        else return false;
        if (count > size - i) return false;
        while (count--) {
            const uint8_t next = data[i++];
            if ((next & 0xC0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    }
    return true;
}

static tide_io_error path_error(const tide_str path)
{
    if (!path.ptr || path.bytes <= 0 || memchr(path.ptr, 0, (size_t)path.bytes)
        || !utf8((const uint8_t *)path.ptr, (uint32_t)path.bytes)) return TIDE_IO_INVALID_PATH;
    return TIDE_IO_NONE;
}

static tide_str copy_text(const char *data, const int32_t size)
{
    char *copy = tide_scratch_memory((size_t)size + 1u);
    if (size) memcpy(copy, data, (size_t)size);
    copy[size] = 0;
    return (tide_str){copy, size, tide_utf8_chars(copy, size)};
}

static bool exists(const tide_io *io, const tide_str path, const bool directory)
{
    return path_error(path) == TIDE_IO_NONE && io && io->exists && io->exists(io->user, tide_str_c(path), directory);
}

bool tide_file_exists(const tide_io *io, const tide_str path) { return exists(io, path, false); }
bool tide_directory_exists(const tide_io *io, const tide_str path) { return exists(io, path, true); }

static tide_io_void_result create(const tide_io *io, const tide_str path, const bool directory)
{
    const tide_io_error invalid = path_error(path);
    if (invalid) return result(invalid);
    return result(io && io->create ? io->create(io->user, tide_str_c(path), directory) : TIDE_IO_STORAGE_UNAVAILABLE);
}

tide_io_void_result tide_file_create(const tide_io *io, const tide_str path) { return create(io, path, false); }
tide_io_void_result tide_directory_create_directory(const tide_io *io, const tide_str path) { return create(io, path, true); }

static tide_io_error read_file(const tide_io *io, const tide_str path, uint8_t **data, uint32_t *size)
{
    const tide_io_error invalid = path_error(path);
    if (invalid) return invalid;
    if (!io || !io->read) return TIDE_IO_STORAGE_UNAVAILABLE;
    const tide_io_error error = io->read(io->user, tide_str_c(path), data, size);
    if (error) return error;
    return *size > INT32_MAX || (*size && !*data) ? TIDE_IO_INVALID_DATA : TIDE_IO_NONE;
}

tide_io_text_result tide_file_read_all_text(const tide_io *io, const tide_str path)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    tide_io_error error = read_file(io, path, &data, &size);
    tide_str text = TIDE_STR_EMPTY;
    if (!error && !utf8(data, size)) error = TIDE_IO_INVALID_DATA;
    if (!error) text = copy_text((const char *)data, (int32_t)size);
    free(data);
    return (tide_io_text_result){text, error, error == TIDE_IO_NONE};
}

tide_io_list_result tide_file_read_all_bytes(const tide_io *io, const tide_str path)
{
    uint8_t *data = NULL;
    uint32_t size = 0;
    const tide_io_error error = read_file(io, path, &data, &size);
    tide_list list = {0};
    if (!error && size) {
        tide_block *block = tide_scratch_block(size, "file bytes", &list.at);
        block->a = block->b = size;
        memcpy(block + 1, data, size);
    }
    free(data);
    return (tide_io_list_result){list, error, error == TIDE_IO_NONE};
}

static tide_io_void_result write_file(const tide_io *io, const tide_str path, const uint8_t *data, const uint32_t size,
                                      const bool append)
{
    const tide_io_error invalid = path_error(path);
    if (invalid) return result(invalid);
    return result(io && io->write ? io->write(io->user, tide_str_c(path), data, size, append) : TIDE_IO_STORAGE_UNAVAILABLE);
}

static tide_io_void_result write_text(const tide_io *io, const tide_str path, const tide_str text, const bool append)
{
    if (text.bytes < 0 || (text.bytes && !text.ptr) || !utf8((const uint8_t *)text.ptr, (uint32_t)text.bytes)) {
        return result(TIDE_IO_INVALID_DATA);
    }
    return write_file(io, path, (const uint8_t *)text.ptr, (uint32_t)text.bytes, append);
}

tide_io_void_result tide_file_write_all_text(const tide_io *io, const tide_str path, const tide_str text)
{
    return write_text(io, path, text, false);
}

tide_io_void_result tide_file_append_all_text(const tide_io *io, const tide_str path, const tide_str text)
{
    return write_text(io, path, text, true);
}

tide_io_void_result tide_file_write_all_bytes(const tide_io *io, const tide_str path, const tide_list bytes)
{
    const int32_t count = tide_list_count(bytes);
    uint8_t *data = count ? tide_scratch_memory((size_t)count) : NULL;
    for (int32_t i = 0; i < count; i++) data[i] = *(const uint8_t *)tide_list_at(bytes, i, 1);
    return write_file(io, path, data, (uint32_t)count, false);
}

static tide_io_void_result transfer(const tide_io *io, const tide_str source, const tide_str destination,
                                    const bool overwrite, const bool move, const bool directory)
{
    tide_io_error error = path_error(source);
    if (!error) error = path_error(destination);
    if (error) return result(error);
    if (!io || (move ? !io->move : !io->copy)) return result(TIDE_IO_STORAGE_UNAVAILABLE);
    const char *from = tide_str_c(source), *to = tide_str_c(destination);
    return result(move ? io->move(io->user, from, to, overwrite, directory) : io->copy(io->user, from, to, overwrite));
}

tide_io_void_result tide_file_copy(const tide_io *io, const tide_str source, const tide_str destination, const bool overwrite)
{
    return transfer(io, source, destination, overwrite, false, false);
}

tide_io_void_result tide_file_move(const tide_io *io, const tide_str source, const tide_str destination, const bool overwrite)
{
    return transfer(io, source, destination, overwrite, true, false);
}

tide_io_void_result tide_directory_move(const tide_io *io, const tide_str source, const tide_str destination)
{
    return transfer(io, source, destination, false, true, true);
}

static tide_io_void_result remove_path(const tide_io *io, const tide_str path, const bool directory, const bool recursive)
{
    const tide_io_error error = path_error(path);
    if (error) return result(error);
    return result(io && io->remove ? io->remove(io->user, tide_str_c(path), directory, recursive) : TIDE_IO_STORAGE_UNAVAILABLE);
}

tide_io_void_result tide_file_delete(const tide_io *io, const tide_str path) { return remove_path(io, path, false, false); }
tide_io_void_result tide_directory_delete(const tide_io *io, const tide_str path, const bool recursive)
{
    return remove_path(io, path, true, recursive);
}

typedef struct listing {
    tide_list list;
    tide_io_error error;
} listing;

static void list_entry(void *user, const char *path)
{
    listing *entries = user;
    const size_t size = strlen(path);
    if (size > INT32_MAX || !utf8((const uint8_t *)path, (uint32_t)size)) { entries->error = TIDE_IO_INVALID_DATA; return; }
    const tide_text text = tide_text_temp((tide_str){path, (int32_t)size, tide_utf8_chars(path, (int32_t)size)});
    *(tide_text *)tide_list_add(&entries->list, sizeof(tide_text), TIDE_IN_SCRATCH) = text;
}

static int compare_entries(const void *a, const void *b)
{
    const tide_str x = tide_text_view(*(const tide_text *)a), y = tide_text_view(*(const tide_text *)b);
    const int32_t common = x.bytes < y.bytes ? x.bytes : y.bytes;
    const int compared = common ? memcmp(x.ptr, y.ptr, (size_t)common) : 0;
    return compared ? compared : (x.bytes > y.bytes) - (x.bytes < y.bytes);
}

static tide_io_list_result list_paths(const tide_io *io, const tide_str path, const bool directories, const bool recursive)
{
    listing entries = {0};
    tide_io_error error = path_error(path);
    if (!error) error = io && io->list ? io->list(io->user, tide_str_c(path), directories, recursive, list_entry, &entries)
                                      : TIDE_IO_STORAGE_UNAVAILABLE;
    if (!error) error = entries.error;
    tide_list list = entries.list;
    if (!error) {
        const int32_t count = tide_list_count(list);
        if (count > 1) qsort(tide_list_at_mut(list, 0, sizeof(tide_text)), (size_t)count, sizeof(tide_text), compare_entries);
    } else list.at = 0;
    return (tide_io_list_result){list, error, error == TIDE_IO_NONE};
}

tide_io_list_result tide_directory_get_files(const tide_io *io, const tide_str path, const bool recursive)
{
    return list_paths(io, path, false, recursive);
}

tide_io_list_result tide_directory_get_directories(const tide_io *io, const tide_str path, const bool recursive)
{
    return list_paths(io, path, true, recursive);
}

static bool separator(const char c) { return c == '/' || c == '\\'; }

tide_str tide_path_combine(const tide_str a, const tide_str b)
{
    if (!a.bytes) return b;
    if (!b.bytes) return a;
    if (separator(b.ptr[0]) || (b.bytes >= 2 && b.ptr[1] == ':')) return b;
    int32_t end = a.bytes;
    while (end > 1 && separator(a.ptr[end - 1])) end--;
    const bool slash = !separator(a.ptr[end - 1]);
    char *combined = tide_scratch_memory((size_t)end + (size_t)b.bytes + (slash ? 2u : 1u));
    memcpy(combined, a.ptr, (size_t)end);
    if (slash) combined[end++] = '/';
    memcpy(combined + end, b.ptr, (size_t)b.bytes);
    combined[end + b.bytes] = 0;
    for (int32_t i = 0; i < end + b.bytes; i++) if (combined[i] == '\\') combined[i] = '/';
    return (tide_str){combined, end + b.bytes, tide_utf8_chars(combined, end + b.bytes)};
}

tide_str tide_path_get_file_name(const tide_str path)
{
    if (path.bytes <= 0 || !path.ptr) return TIDE_STR_EMPTY;
    int32_t start = path.bytes;
    while (start && !separator(path.ptr[start - 1])) start--;
    return copy_text(path.ptr + start, path.bytes - start);
}

tide_str tide_path_get_directory_name(const tide_str path)
{
    if (path.bytes <= 0 || !path.ptr) return TIDE_STR_EMPTY;
    if ((path.bytes == 1 && separator(path.ptr[0]))
        || (path.bytes == 3 && path.ptr[1] == ':' && separator(path.ptr[2]))) return TIDE_STR_EMPTY;
    int32_t end = path.bytes;
    while (end && !separator(path.ptr[end - 1])) end--;
    if (!end) return TIDE_STR_EMPTY;
    while (end > 1 && separator(path.ptr[end - 1])) end--;
    if (end == 2 && path.ptr[1] == ':' && path.bytes > 2 && separator(path.ptr[2])) end++;
    return copy_text(path.ptr, end);
}

tide_str tide_path_get_extension(const tide_str path)
{
    if (path.bytes <= 0 || !path.ptr) return TIDE_STR_EMPTY;
    for (int32_t i = path.bytes; i > 0; i--) {
        if (separator(path.ptr[i - 1])) break;
        if (path.ptr[i - 1] == '.') return i == path.bytes ? TIDE_STR_EMPTY : copy_text(path.ptr + i - 1, path.bytes - i + 1);
    }
    return TIDE_STR_EMPTY;
}

tide_str tide_application_persistent_data_path(const tide_io *io)
{
    return tide_str_from_cstr(io && io->persistent_data_path ? io->persistent_data_path : "");
}

tide_str tide_application_temporary_cache_path(const tide_io *io)
{
    return tide_str_from_cstr(io && io->temporary_cache_path ? io->temporary_cache_path : "");
}

uint32_t tide_storage_flush_begin(const tide_io *io) { return io && io->flush_begin ? io->flush_begin(io->user) : 0; }

bool tide_storage_flush_poll(const tide_io *io, const uint32_t ticket, tide_io_error *error)
{
    if (!io || !io->flush_poll || !ticket) { *error = TIDE_IO_STORAGE_UNAVAILABLE; return true; }
    return io->flush_poll(io->user, ticket, error);
}

void tide_storage_flush_forget(const tide_io *io, const uint32_t ticket)
{
    if (io && io->flush_forget && ticket) io->flush_forget(io->user, ticket);
}
