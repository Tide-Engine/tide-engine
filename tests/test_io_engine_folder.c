// The engine's folder in the persistent root (TIDE_IO_ENGINE_FOLDER, tide/io.h): the game's File and Directory
// calls refuse it however the path is written, and a provider is never asked; names merely like it are the game's.

#include <string.h>

#include "tide/io.h"
#include "tide/storage.h"
#include "tide_test.h"

static tide_str text(const char *value) { return tide_str_from_cstr(value); }

static bool in(const tide_io *io, const char *path) { return tide_io_engine_path(io, text(path), false); }
static bool above(const tide_io *io, const char *path) { return tide_io_engine_path(io, text(path), true); }

TIDE_TEST(io_engine_folder_paths_however_written)
{
    // As a native Windows provider gives its root: '/', with a drive
    const tide_io windows = {.persistent_data_path = "C:/Users/Me/AppData/LocalLow/com.studio.game"};
    TIDE_CHECK(strcmp(TIDE_STORAGE_FOLDER, "Tide/store") == 0);
    TIDE_CHECK(in(&windows, "Tide"));
    TIDE_CHECK(in(&windows, "Tide/"));
    TIDE_CHECK(in(&windows, "Tide/store/chunks/0.bin"));
    TIDE_CHECK(in(&windows, "Tide\\store\\chunks\\0.bin"));
    TIDE_CHECK(in(&windows, "tide/STORE/x.bin"));
    TIDE_CHECK(in(&windows, "TIDE"));
    TIDE_CHECK(in(&windows, "./Tide/./store"));
    TIDE_CHECK(in(&windows, "saves//../Tide/x"));
    TIDE_CHECK(in(&windows, "a/b/../../Tide/x"));
    TIDE_CHECK(in(&windows, "../com.studio.game/Tide/store/x.bin")); // Out of the root and back in
    TIDE_CHECK(in(&windows, "../COM.Studio.Game/tide/x"));
    TIDE_CHECK(in(&windows, "Tide./x"));   // Windows drops a name's trailing dots and spaces
    TIDE_CHECK(in(&windows, "Tide . /x"));
    TIDE_CHECK(in(&windows, "Tide:stream")); // An NTFS stream of the folder
    TIDE_CHECK(in(&windows, "Tide/.../x"));
    TIDE_CHECK(in(&windows, "C:/Users/Me/AppData/LocalLow/com.studio.game/Tide/store/a.bin"));
    TIDE_CHECK(in(&windows, "c:\\users\\me\\appdata\\locallow\\COM.STUDIO.GAME\\TIDE\\store"));
    TIDE_CHECK(in(&windows, "C:\\Users\\Me\\AppData\\LocalLow\\com.studio.game\\x\\..\\Tide"));
    TIDE_CHECK(in(&windows, "\\\\?\\C:\\Users\\Me\\AppData\\LocalLow\\com.studio.game\\Tide\\store"));
    TIDE_CHECK(in(&windows, "\\\\.\\C:\\Users\\Me\\AppData\\LocalLow\\com.studio.game\\Tide"));
    TIDE_CHECK(in(&windows, "/Users/Me/AppData/LocalLow/com.studio.game/Tide/x")); // The process's drive
    TIDE_CHECK(in(&windows, "Tide/store/../../Tide"));
    // The game's: other names, and a Tide folder that isn't the root's
    TIDE_CHECK(!in(&windows, "Tide2/x.txt"));
    TIDE_CHECK(!in(&windows, "tide-notes.txt"));
    TIDE_CHECK(!in(&windows, "Tided/x"));
    TIDE_CHECK(!in(&windows, "Ti de/x"));
    TIDE_CHECK(!in(&windows, ".Tide/x"));
    TIDE_CHECK(!in(&windows, "saves/Tide/x"));
    TIDE_CHECK(!in(&windows, "saves/Tide.txt"));
    TIDE_CHECK(!in(&windows, "Tide/.."));
    TIDE_CHECK(!in(&windows, "Tide/store/../../saves/a.txt"));
    TIDE_CHECK(!in(&windows, "C:/Users/Me/AppData/LocalLow/other.game/Tide/x"));
    TIDE_CHECK(!in(&windows, "D:/Tide/x"));
    TIDE_CHECK(!in(&windows, "save.txt"));
    TIDE_CHECK(!in(&windows, ""));
    // The folders it's in: a move or a delete of them takes it along
    TIDE_CHECK(above(&windows, "."));
    TIDE_CHECK(above(&windows, "Tide/.."));
    TIDE_CHECK(above(&windows, "C:/Users/Me/AppData/LocalLow/com.studio.game"));
    TIDE_CHECK(above(&windows, "C:/Users/Me/AppData/LocalLow/com.studio.game/"));
    TIDE_CHECK(above(&windows, ".."));
    TIDE_CHECK(above(&windows, "C:/Users"));
    TIDE_CHECK(above(&windows, "C:/"));
    TIDE_CHECK(above(&windows, "Tide/store"));
    TIDE_CHECK(!above(&windows, "saves"));
    TIDE_CHECK(!above(&windows, "C:/Users/Me/AppData/LocalLow/com.studio.game2"));
    TIDE_CHECK(!above(&windows, "D:/"));

    // A system's without drives, and a page's virtual root
    const tide_io posix = {.persistent_data_path = "/home/me/.local/share/game"};
    TIDE_CHECK(in(&posix, "Tide/store/x.bin") && in(&posix, "/home/me/.local/share/game/tide"));
    TIDE_CHECK(in(&posix, "/home/me//./.local/share/game/x/../Tide/store"));
    TIDE_CHECK(!in(&posix, "/home/me/.local/share/game2/Tide") && !in(&posix, "/Tide"));
    TIDE_CHECK(above(&posix, "/") && above(&posix, "/home") && !above(&posix, "/home/other"));
    const tide_io page = {.persistent_data_path = "/persistent/dev.tide.game"};
    TIDE_CHECK(in(&page, "Tide") && in(&page, "/persistent/dev.tide.game/TIDE/store/k.bin") && !in(&page, "Tide2"));

    // No root: a relative path is still the root's, and an absolute one is nobody's
    const tide_io none = {0};
    TIDE_CHECK(in(&none, "Tide/x") && !in(&none, "/Tide/x"));
    TIDE_CHECK(!in(NULL, "/x") && in(NULL, "Tide"));
}

// A provider that counts what it was asked, and lists a root with the engine's folder in it.
static int asked;
static const char *const listed[] = {
    "/r/a.txt", "/r/Tide", "/r/Tide/store", "/r/Tide/store/k.bin", "/r/Tide2", "/r/Tide2/b.txt", "/r/tide-notes.txt",
};

static bool fake_exists(void *user, const char *path, const bool directory)
{
    (void)user, (void)path, (void)directory;
    asked++;
    return true;
}
static tide_io_error fake_create(void *user, const char *path, const bool directory)
{
    (void)user, (void)path, (void)directory;
    asked++;
    return TIDE_IO_NONE;
}
static tide_io_error fake_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user, (void)path;
    asked++;
    *data = NULL;
    *size = 0;
    return TIDE_IO_NONE;
}
static tide_io_error fake_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    (void)user, (void)path, (void)data, (void)size, (void)append;
    asked++;
    return TIDE_IO_NONE;
}
static tide_io_error fake_copy(void *user, const char *source, const char *destination, const bool overwrite)
{
    (void)user, (void)source, (void)destination, (void)overwrite;
    asked++;
    return TIDE_IO_NONE;
}
static tide_io_error fake_move(void *user, const char *source, const char *destination, const bool overwrite,
                               const bool directory)
{
    (void)user, (void)source, (void)destination, (void)overwrite, (void)directory;
    asked++;
    return TIDE_IO_NONE;
}
static tide_io_error fake_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    (void)user, (void)path, (void)directory, (void)recursive;
    asked++;
    return TIDE_IO_NONE;
}
static tide_io_error fake_list(void *user, const char *path, const bool directories, const bool recursive,
                               const tide_io_entry_fn entry, void *entry_user)
{
    (void)user, (void)path, (void)directories, (void)recursive;
    asked++;
    for (size_t i = 0; i < sizeof listed / sizeof listed[0]; i++) entry(entry_user, listed[i]);
    return TIDE_IO_NONE;
}

static bool lists(const tide_io_list_result r, const char *const *expected, const int32_t count)
{
    if (!r.ok || tide_list_count(r.value) != count) return false;
    for (int32_t i = 0; i < count; i++) {
        if (!tide_str_eq(tide_text_view(*(const tide_text *)tide_list_at(r.value, i, sizeof(tide_text))), text(expected[i]))) {
            return false;
        }
    }
    return true;
}

TIDE_TEST(io_engine_folder_refused_to_the_games_calls)
{
    const uint64_t mark = tide_scratch_mark();
    const tide_io io = {.persistent_data_path = "/r", .exists = fake_exists, .create = fake_create, .read = fake_read,
                        .write = fake_write, .copy = fake_copy, .move = fake_move, .remove = fake_remove, .list = fake_list};
    const tide_io *p = &io;
    asked = 0;
    const tide_str store = text("Tide/store/k.bin"), mine = text("saves/a.txt");
    TIDE_CHECK(!tide_file_exists(p, store) && !tide_directory_exists(p, text("tide")));
    TIDE_CHECK(tide_file_create(p, store).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_read_all_text(p, store).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_read_all_bytes(p, text("Tide\\store\\k.bin")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_write_all_text(p, text("/r/x/../TIDE/store/k.bin"), text("x")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_append_all_text(p, store, text("x")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_write_all_bytes(p, store, (tide_list){0}).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_copy(p, store, mine, true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_copy(p, mine, store, true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_move(p, store, mine, true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_move(p, mine, store, true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_file_delete(p, store).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_create_directory(p, text("Tide/mine")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_get_files(p, text("Tide"), true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_get_directories(p, text("tide/store"), false).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_delete(p, text("Tide"), true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_move(p, text("Tide"), text("old")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_move(p, text("saves"), text("Tide/saves")).error == TIDE_IO_ACCESS_DENIED);
    // The folders it's in: deleting or moving one would take the store along
    TIDE_CHECK(tide_directory_delete(p, text("/r"), true).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_delete(p, text("."), false).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(tide_directory_move(p, text("/r"), text("/elsewhere")).error == TIDE_IO_ACCESS_DENIED);
    TIDE_CHECK(asked == 0); // Not one reached the provider
    // An invalid path is still that first
    const char nul[] = {'T', 'i', 'd', 'e', 0, 'x'};
    TIDE_CHECK(tide_file_create(p, (tide_str){nul, 6, 6}).error == TIDE_IO_INVALID_PATH);

    // The game's own, similar names among them
    TIDE_CHECK(tide_file_exists(p, mine) && tide_file_exists(p, text("Tide2/x")) && asked == 2);
    TIDE_CHECK(tide_file_write_all_text(p, text("Tide2/x.txt"), text("x")).ok);
    TIDE_CHECK(tide_file_write_all_text(p, text("tide-notes.txt"), text("x")).ok);
    TIDE_CHECK(tide_file_write_all_text(p, text("saves/Tide/x.txt"), text("x")).ok);
    TIDE_CHECK(tide_file_move(p, text("Tide2/x.txt"), text("Tided.txt"), false).ok);
    TIDE_CHECK(tide_directory_delete(p, text("saves"), true).ok && tide_directory_move(p, text("a"), text("b")).ok);
    TIDE_CHECK(tide_file_delete(p, text("/r")).ok); // A file's delete can't take a folder along
    // A listing of the root has the engine's folder by its name, and nothing in it
    static const char *const kept[] = {"/r/Tide", "/r/Tide2", "/r/Tide2/b.txt", "/r/a.txt", "/r/tide-notes.txt"};
    TIDE_CHECK(lists(tide_directory_get_files(p, text("/r"), true), kept, 5));
    TIDE_CHECK(lists(tide_directory_get_files(p, text(""), true), NULL, 0) == false); // An empty path is invalid
    // One that isn't above it is as the provider gave it, sorted
    static const char *const all[] = {"/r/Tide", "/r/Tide/store", "/r/Tide/store/k.bin", "/r/Tide2", "/r/Tide2/b.txt",
                                      "/r/a.txt", "/r/tide-notes.txt"};
    TIDE_CHECK(lists(tide_directory_get_files(p, text("saves"), false), all, 7));
    tide_scratch_reset(mark);
}
