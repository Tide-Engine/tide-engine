#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/list.h"
#include "tide/text.h"

// Local file operations. The host supplies a provider; the engine only
// converts its bytes to Tide's scratch values. A provider outlives the local
// state that uses it, including hot reloads. No provider data is match state.
typedef enum tide_io_error {
    TIDE_IO_NONE = 0,
    TIDE_IO_NOT_FOUND = 1,
    TIDE_IO_ALREADY_EXISTS = 2,
    TIDE_IO_ACCESS_DENIED = 3,
    TIDE_IO_INVALID_PATH = 4,
    TIDE_IO_NOT_DIRECTORY = 5,
    TIDE_IO_DIRECTORY_NOT_EMPTY = 6,
    TIDE_IO_OUT_OF_SPACE = 7,
    TIDE_IO_STORAGE_UNAVAILABLE = 8,
    TIDE_IO_INVALID_DATA = 9,
    TIDE_IO_FAILED = 10,
} tide_io_error;

typedef void (*tide_io_entry_fn)(void *user, const char *path);

typedef struct tide_io {
    void *user;
    const char *persistent_data_path;
    const char *temporary_cache_path;
    bool (*exists)(void *user, const char *path, bool directory);
    tide_io_error (*create)(void *user, const char *path, bool directory);
    // The provider allocates read bytes with malloc (or tide_alloc); the
    // wrapper frees them. Empty files may return NULL and size zero.
    tide_io_error (*read)(void *user, const char *path, uint8_t **data, uint32_t *size);
    tide_io_error (*write)(void *user, const char *path, const uint8_t *data, uint32_t size, bool append);
    tide_io_error (*copy)(void *user, const char *source, const char *destination, bool overwrite);
    tide_io_error (*move)(void *user, const char *source, const char *destination, bool overwrite, bool directory);
    tide_io_error (*remove)(void *user, const char *path, bool directory, bool recursive);
    tide_io_error (*list)(void *user, const char *path, bool directories, bool recursive, tide_io_entry_fn entry,
                          void *entry_user);
    uint32_t (*flush_begin)(void *user);
    // false while pending; true consumes a terminal result and writes its
    // error (NONE on success). forget releases a canceled ticket.
    bool (*flush_poll)(void *user, uint32_t ticket, tide_io_error *error);
    void (*flush_forget)(void *user, uint32_t ticket);
} tide_io;

typedef struct tide_io_void_result {
    tide_io_error error;
    bool ok;
} tide_io_void_result;

typedef struct tide_io_text_result {
    tide_str value;
    tide_io_error error;
    bool ok;
} tide_io_text_result;

typedef struct tide_io_list_result {
    tide_list value;
    tide_io_error error;
    bool ok;
} tide_io_list_result;

bool tide_file_exists(const tide_io *io, tide_str path);
tide_io_void_result tide_file_create(const tide_io *io, tide_str path);
tide_io_text_result tide_file_read_all_text(const tide_io *io, tide_str path);
tide_io_void_result tide_file_write_all_text(const tide_io *io, tide_str path, tide_str text);
tide_io_void_result tide_file_append_all_text(const tide_io *io, tide_str path, tide_str text);
tide_io_list_result tide_file_read_all_bytes(const tide_io *io, tide_str path);
tide_io_void_result tide_file_write_all_bytes(const tide_io *io, tide_str path, tide_list bytes);
tide_io_void_result tide_file_copy(const tide_io *io, tide_str source, tide_str destination, bool overwrite);
tide_io_void_result tide_file_move(const tide_io *io, tide_str source, tide_str destination, bool overwrite);
tide_io_void_result tide_file_delete(const tide_io *io, tide_str path);

bool tide_directory_exists(const tide_io *io, tide_str path);
tide_io_void_result tide_directory_create_directory(const tide_io *io, tide_str path);
tide_io_list_result tide_directory_get_files(const tide_io *io, tide_str path, bool recursive);
tide_io_list_result tide_directory_get_directories(const tide_io *io, tide_str path, bool recursive);
tide_io_void_result tide_directory_move(const tide_io *io, tide_str source, tide_str destination);
tide_io_void_result tide_directory_delete(const tide_io *io, tide_str path, bool recursive);

tide_str tide_path_combine(tide_str a, tide_str b);
tide_str tide_path_get_file_name(tide_str path);
tide_str tide_path_get_directory_name(tide_str path);
tide_str tide_path_get_extension(tide_str path);
tide_str tide_application_persistent_data_path(const tide_io *io);
tide_str tide_application_temporary_cache_path(const tide_io *io);

uint32_t tide_storage_flush_begin(const tide_io *io);
bool tide_storage_flush_poll(const tide_io *io, uint32_t ticket, tide_io_error *error);
void tide_storage_flush_forget(const tide_io *io, uint32_t ticket);
