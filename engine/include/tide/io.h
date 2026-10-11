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
    TIDE_IO_TIMED_OUT = 11, // A request's own time limit passed with no whole answer
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
    // Web.Fetch: an HTTP GET of `url` (http:// or https://, checked before
    // this is called), which goes on by itself. begin gives a ticket (never
    // 0). poll is false while it's going; true consumes it, with its error, or
    // NONE and the body, allocated with malloc (or tide_alloc), which the
    // wrapper frees (NULL and 0 for an empty one). A body past
    // TIDE_FETCH_LIMIT bytes is TIDE_IO_FAILED, read no further. forget
    // releases a ticket nobody will poll again, stopping what it can.
    uint32_t (*fetch_begin)(void *user, const char *url);
    bool (*fetch_poll)(void *user, uint32_t ticket, tide_io_error *error, uint8_t **data, uint32_t *size);
    void (*fetch_forget)(void *user, uint32_t ticket);
    // Web.Request's Send: any method, with headers and a body, and an answer
    // of any status is its result. `method` is capitals; `headers` is a line
    // each, "Name: value\r\n", checked before this is called (names are
    // tokens, values printable ASCII, and none of them is the connection's
    // own); `body` is `size` bytes, read before begin returns. Tickets are as
    // a fetch's, which fetch_forget lets go, and never TIDE_REQUEST_INVALID either. poll is false while
    // it's going; true consumes it, with its error, or NONE, the answer's
    // status and its body, allocated as a fetch's and under the same limit.
    // Redirects are followed as a browser's fetch follows them. `timeout` is
    // the seconds the whole of it may take, from begin to the answer's last
    // byte, redirects among them, by a clock that never steps back: 0 for no
    // limit, and never negative or more than TIDE_REQUEST_TIMEOUT_MOST. Past
    // it, poll gives TIDE_IO_TIMED_OUT and no answer, and the provider stops
    // and frees what the request held.
    uint32_t (*request_begin)(void *user, const char *method, const char *url, const char *headers, const uint8_t *body,
                              uint32_t size, float timeout);
    bool (*request_poll)(void *user, uint32_t ticket, tide_io_error *error, int32_t *status, uint8_t **data, uint32_t *size);
} tide_io;

// The most a fetched body may be: a hostile server can't fill memory.
#define TIDE_FETCH_LIMIT (64u << 20)
// The ticket tide_web_fetch_begin gives for a URL that isn't one (no
// http:// or https://, or not text): polling it is TIDE_IO_INVALID_PATH at
// once, and no provider is asked. Providers never give it.
#define TIDE_FETCH_INVALID_URL 0xFFFFFFFFu
// The ticket tide_web_request_begin gives for a request that can't be sent as
// it's written (a method that's no word or one a page can't send, a header
// that's no "Name: value", a body past TIDE_FETCH_LIMIT): polling it is
// TIDE_IO_INVALID_DATA at once. Providers never give it.
#define TIDE_REQUEST_INVALID 0xFFFFFFFEu
// The longest a request's time limit is, in seconds: a longer one is this long.
#define TIDE_REQUEST_TIMEOUT_MOST 1000000.0f

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

// await Web.Fetch(url), a local task's: begin starts it (0 with no provider,
// which polls as TIDE_IO_FAILED), poll is false while it's going and true once
// it's done, with the body as a List<byte> in the scratch area (`value`, when
// `ok`), and forget lets a ticket go that won't be polled again.
uint32_t tide_web_fetch_begin(const tide_io *io, tide_str url);
bool tide_web_fetch_poll(const tide_io *io, uint32_t ticket, tide_io_list_result *result);
void tide_web_fetch_forget(const tide_io *io, uint32_t ticket);

// await request.Send(), a local task's: a WebRequest's url, method, headers
// (a line each, "Name: value\n", as SetHeader writes them) and body. begin
// is as a fetch's, and its ticket is one: tide_web_fetch_forget lets it go.
// poll gives the answer once there is one,
// whatever its status: its status and its body, a List<byte> in the scratch
// area. `ok` is false only when no answer came: TIDE_IO_INVALID_PATH for a
// URL that isn't one, TIDE_IO_INVALID_DATA for a request that can't be sent
// as written, TIDE_IO_TIMED_OUT when its `timeout` passed first, and
// TIDE_IO_FAILED for the rest. `timeout` is the request's own, in seconds
// (SetTimeout): 0 or less, or no number, is no limit.
typedef struct tide_web_answer {
    tide_list body;
    int32_t status;
    tide_io_error error;
    bool ok;
} tide_web_answer;

uint32_t tide_web_request_begin(const tide_io *io, tide_str url, tide_str method, tide_str headers, tide_list body,
                                float timeout);
bool tide_web_request_poll(const tide_io *io, uint32_t ticket, tide_web_answer *answer);
