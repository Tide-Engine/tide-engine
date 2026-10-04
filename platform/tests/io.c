#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "tide/platform.h"
#include "tide_test.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

static tide_str text(const char *value) { return tide_str_from_cstr(value); }
static tide_str path(const tide_str root, const char *name) { return tide_path_combine(root, text(name)); }

static bool contents(const tide_io *io, const tide_str name, const char *expected)
{
    const tide_io_text_result read = tide_file_read_all_text(io, name);
    return read.ok && tide_str_eq(read.value, text(expected));
}

static tide_io_error finish_flush(const tide_io *io, const uint32_t ticket)
{
    tide_io_error error = TIDE_IO_FAILED;
    while (!tide_storage_flush_poll(io, ticket, &error)) {
#ifdef _WIN32
        SwitchToThread();
#else
        sched_yield();
#endif
    }
    return error;
}

TIDE_TEST(io_native_files_and_directories)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_io *io = tide_platform_io_init("dev.tide.io.tests");
    TIDE_REQUIRE(io && io->persistent_data_path[0] && io->temporary_cache_path[0]);
    char folder[80];
#ifdef _WIN32
    snprintf(folder, sizeof folder, "native-test-%lu", (unsigned long)GetCurrentProcessId());
#else
    snprintf(folder, sizeof folder, "native-test-%ld", (long)getpid());
#endif
    const tide_str root = path(tide_application_temporary_cache_path(io), folder);
    if (tide_directory_exists(io, root)) TIDE_REQUIRE(tide_directory_delete(io, root, true).ok);
    TIDE_REQUIRE(tide_directory_create_directory(io, path(root, "nested/deeper")).ok);
    TIDE_CHECK(tide_directory_exists(io, root));
    TIDE_CHECK(!tide_file_exists(io, root));
    const tide_str first = path(root, "z-Ã©ð.txt"), empty = path(root, "empty.txt");
    TIDE_REQUIRE(tide_file_create(io, empty).ok);
    TIDE_CHECK(contents(io, empty, ""));
    TIDE_CHECK(!tide_file_read_all_text(io, path(root, "absent.txt")).ok);
    TIDE_CHECK(tide_file_read_all_text(io, path(root, "absent.txt")).error == TIDE_IO_NOT_FOUND);
    TIDE_REQUIRE(tide_file_write_all_text(io, first, text("hello ð")).ok);
    TIDE_CHECK(contents(io, first, "hello ð"));
    TIDE_REQUIRE(tide_file_append_all_text(io, first, text("\nworld")).ok);
    TIDE_CHECK(contents(io, first, "hello ð\nworld"));
    TIDE_REQUIRE(tide_file_create(io, first).ok);
    TIDE_CHECK(contents(io, first, ""));
    TIDE_CHECK(tide_file_write_all_text(io, path(root, "missing/file"), text("no")).error == TIDE_IO_NOT_FOUND);
    const tide_str copied = path(root, "copied.txt");
    TIDE_REQUIRE(tide_file_write_all_text(io, first, text("source")).ok);
    TIDE_REQUIRE(tide_file_copy(io, first, copied, false).ok);
    TIDE_CHECK(contents(io, copied, "source"));
    TIDE_CHECK(tide_file_copy(io, first, copied, false).error == TIDE_IO_ALREADY_EXISTS);
    TIDE_REQUIRE(tide_file_write_all_text(io, first, text("changed")).ok);
    TIDE_REQUIRE(tide_file_copy(io, first, copied, true).ok);
    TIDE_CHECK(contents(io, copied, "changed"));
    TIDE_CHECK(!tide_file_copy(io, first, first, true).ok);
    TIDE_CHECK(contents(io, first, "changed"));
    const tide_str moved = path(root, "moved.txt");
    TIDE_REQUIRE(tide_file_move(io, copied, moved, false).ok);
    TIDE_CHECK(!tide_file_exists(io, copied) && tide_file_exists(io, moved));
    TIDE_REQUIRE(tide_file_write_all_text(io, first, text("move source")).ok);
    TIDE_CHECK(tide_file_move(io, first, moved, false).error == TIDE_IO_ALREADY_EXISTS);
    TIDE_CHECK(contents(io, first, "move source") && contents(io, moved, "changed"));
    TIDE_REQUIRE(tide_file_move(io, first, moved, true).ok);
    TIDE_CHECK(!tide_file_exists(io, first) && contents(io, moved, "move source"));
    TIDE_CHECK(tide_directory_delete(io, root, false).error == TIDE_IO_DIRECTORY_NOT_EMPTY);
    TIDE_CHECK(tide_directory_get_files(io, moved, false).error == TIDE_IO_NOT_DIRECTORY);

    tide_list bytes = {0};
    for (int32_t i = 0; i < 50000; i++) *(uint8_t *)tide_list_add(&bytes, 1, TIDE_IN_SCRATCH) = (uint8_t)(i * 7);
    const tide_str binary = path(root, "nested/deeper/a.bin");
    TIDE_REQUIRE(tide_file_write_all_bytes(io, binary, bytes).ok);
    const tide_io_list_result read = tide_file_read_all_bytes(io, binary);
    TIDE_REQUIRE(read.ok && tide_list_count(read.value) == 50000);
    for (int32_t i = 0; i < 50000; i++) TIDE_CHECK(*(const uint8_t *)tide_list_at(read.value, i, 1) == (uint8_t)(i * 7));
    TIDE_CHECK(tide_file_read_all_text(io, binary).error == TIDE_IO_INVALID_DATA);
    tide_list invalid_utf8 = {0};
    *(uint8_t *)tide_list_add(&invalid_utf8, 1, TIDE_IN_SCRATCH) = 0xC0;
    *(uint8_t *)tide_list_add(&invalid_utf8, 1, TIDE_IN_SCRATCH) = 0x80;
    TIDE_REQUIRE(tide_file_write_all_bytes(io, path(root, "invalid.bin"), invalid_utf8).ok);
    TIDE_CHECK(tide_file_read_all_text(io, path(root, "invalid.bin")).error == TIDE_IO_INVALID_DATA);
    const char nul_path[] = {'x', 0, 'y', 0};
    const tide_str bad = {nul_path, 3, 3};
    TIDE_CHECK(tide_file_create(io, bad).error == TIDE_IO_INVALID_PATH);
    TIDE_CHECK(!tide_file_exists(io, bad));
    TIDE_CHECK(tide_file_write_all_text(io, moved, (tide_str){"\xED\xA0\x80", 3, 1}).error == TIDE_IO_INVALID_DATA);

    const tide_io_list_result files = tide_directory_get_files(io, root, false);
    TIDE_REQUIRE(files.ok && tide_list_count(files.value) == 3);
    TIDE_CHECK(tide_str_eq(tide_text_view(*(const tide_text *)tide_list_at(files.value, 0, sizeof(tide_text))), empty));
    const tide_io_list_result all = tide_directory_get_files(io, root, true);
    TIDE_CHECK(all.ok && tide_list_count(all.value) == 4);
    const tide_io_list_result dirs = tide_directory_get_directories(io, root, true);
    TIDE_CHECK(dirs.ok && tide_list_count(dirs.value) == 2);
    TIDE_REQUIRE(tide_directory_move(io, path(root, "./nested"), path(root, "renamed")).ok);
    TIDE_CHECK(tide_file_exists(io, path(root, "renamed/deeper/a.bin")));
    TIDE_CHECK(tide_directory_move(io, path(root, "renamed"), root).error == TIDE_IO_ALREADY_EXISTS);
    TIDE_CHECK(tide_file_exists(io, path(root, "renamed/deeper/a.bin")) && tide_directory_exists(io, root));
    TIDE_CHECK(tide_platform_io_init("dev.tide.io.tests") == io && tide_file_exists(io, moved));
    const uint32_t ticket = tide_storage_flush_begin(io);
    // Writes made while an earlier snapshot flushes stay dirty for the next
    // flush. Both tickets complete independently and carry their results.
    TIDE_REQUIRE(tide_file_write_all_text(io, moved, text("after begin")).ok);
    const uint32_t later = tide_storage_flush_begin(io);
    TIDE_CHECK(ticket && later && ticket != later);
    TIDE_CHECK(finish_flush(io, ticket) == TIDE_IO_NONE);
    TIDE_CHECK(finish_flush(io, later) == TIDE_IO_NONE);
    tide_storage_flush_forget(io, tide_storage_flush_begin(io));
    TIDE_REQUIRE(tide_directory_delete(io, root, true).ok);
    TIDE_CHECK(!tide_directory_exists(io, root));
    TIDE_CHECK(tide_file_delete(io, moved).ok);
    TIDE_CHECK(tide_directory_delete(io, root, true).error == TIDE_IO_NOT_FOUND);
    tide_scratch_reset(mark);
}

TIDE_TEST(io_path_helpers)
{
    const uint32_t mark = tide_scratch_mark();
    TIDE_CHECK(tide_str_eq(tide_path_combine(text("a/b/"), text("c.txt")), text("a/b/c.txt")));
    TIDE_CHECK(tide_str_eq(tide_path_combine(text("a"), text("/other")), text("/other")));
    TIDE_CHECK(tide_str_eq(tide_path_combine(text("C:\\root"), text("next")), text("C:/root/next")));
    TIDE_CHECK(tide_str_eq(tide_path_get_file_name(text("a\\b/c.txt")), text("c.txt")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("a/b.txt")), text("a")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("/file")), text("/")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("C:/file")), text("C:/")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("file")), text("")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("/")), text("")));
    TIDE_CHECK(tide_str_eq(tide_path_get_directory_name(text("C:/")), text("")));
    TIDE_CHECK(tide_path_get_file_name((tide_str){0}).bytes == 0);
    TIDE_CHECK(tide_str_eq(tide_path_get_extension(text("a/b.tar.gz")), text(".gz")));
    TIDE_CHECK(tide_str_eq(tide_path_get_extension(text("a.ext/b")), text("")));
    TIDE_CHECK(tide_str_eq(tide_path_get_extension(text("file.")), text("")));
    tide_scratch_reset(mark);
}

#ifdef _WIN32
TIDE_TEST(io_native_flush_failure_survives_a_forgotten_waiter)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_io *io = tide_platform_io_init("dev.tide.io.tests");
    char name[80];
    snprintf(name, sizeof name, "locked-flush-%lu.txt", (unsigned long)GetCurrentProcessId());
    const tide_str file = path(tide_application_temporary_cache_path(io), name);
    TIDE_REQUIRE(tide_file_write_all_text(io, file, text("retained")).ok);
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tide_str_c(file), -1, NULL, 0);
    TIDE_REQUIRE(count > 0);
    wchar_t *wide = tide_scratch_memory((size_t)count * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, tide_str_c(file), -1, wide, count);
    // Deny the flush worker's write handle while still allowing file reads.
    HANDLE held = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    TIDE_REQUIRE(held != INVALID_HANDLE_VALUE);
    const uint32_t abandoned = tide_storage_flush_begin(io);
    tide_storage_flush_forget(io, abandoned);
    const uint32_t restored = tide_storage_flush_begin(io);
    TIDE_CHECK(finish_flush(io, restored) == TIDE_IO_ACCESS_DENIED);
    CloseHandle(held);
    TIDE_CHECK(finish_flush(io, tide_storage_flush_begin(io)) == TIDE_IO_NONE);
    TIDE_CHECK(contents(io, file, "retained"));
    TIDE_REQUIRE(tide_file_delete(io, file).ok);
    tide_scratch_reset(mark);
}
#endif

#ifndef _WIN32
TIDE_TEST(io_recursive_delete_does_not_follow_directory_links)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_io *io = tide_platform_io_init("dev.tide.io.tests");
    char folder[80];
    snprintf(folder, sizeof folder, "links-test-%ld", (long)getpid());
    const tide_str root = path(tide_application_temporary_cache_path(io), folder);
    if (tide_directory_exists(io, root)) TIDE_REQUIRE(tide_directory_delete(io, root, true).ok);
    TIDE_REQUIRE(tide_directory_create_directory(io, path(root, "outside")).ok);
    TIDE_REQUIRE(tide_directory_create_directory(io, path(root, "inside")).ok);
    TIDE_REQUIRE(tide_file_write_all_text(io, path(root, "outside/keep.txt"), text("keep")).ok);
    TIDE_REQUIRE(symlink(tide_str_c(path(root, "outside")), tide_str_c(path(root, "inside/link"))) == 0);
    const tide_io_list_result files = tide_directory_get_files(io, path(root, "inside"), true);
    TIDE_CHECK(files.ok && tide_list_count(files.value) == 0);
    TIDE_REQUIRE(tide_directory_delete(io, path(root, "inside"), true).ok);
    TIDE_CHECK(contents(io, path(root, "outside/keep.txt"), "keep"));
    TIDE_REQUIRE(tide_directory_delete(io, root, true).ok);
    tide_scratch_reset(mark);
}
#endif
