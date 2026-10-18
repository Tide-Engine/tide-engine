// Browser saves cross a real page reload. The shell runs the phases and
// injects storage refusal/quota errors before the affected operations. It
// counts the transactions that write, and holds one up when asked.
#include <stdio.h>
#include <string.h>

#include "tide/platform.h"
#include "tide_web.h"

static const tide_io *io;
static const char *phase;
static uint32_t ticket, earlier;
static int failures, stage;

static tide_str text(const char *s) { return tide_str_from_cstr(s); }
static tide_str save_path(const char *name)
{
    return tide_path_combine(tide_application_persistent_data_path(io), text(name));
}
static void check(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}
// The page counts the transactions that wrote files (tideWrites in
// web_io.html). A change queues a save of its own, and a flush in the same
// turn is another: the second has had its turn by the frame that finds the
// flush done, so one that wrote the same files again is in the count.
static void check_writes(const int expected, const char *what)
{
    char script[160];
    snprintf(script, sizeof script,
             "tideWrites === %d || (Tide.print(`${tideWrites} transactions wrote files, where %d should have`), false)",
             expected, expected);
    check(tide_web_eval(script), what);
}
static void read_saved(void)
{
    const tide_io_text_result saved = tide_file_read_all_text(io, save_path("saves/text.txt"));
    check(saved.ok && tide_str_eq(saved.value, text("saved \xC3\xA9\n")), "UTF-8 text survives a page reload");
    const tide_io_text_result empty = tide_file_read_all_text(io, save_path("saves/empty"));
    check(empty.ok && empty.value.bytes == 0, "empty files survive a page reload");
    const tide_io_list_result bytes = tide_file_read_all_bytes(io, save_path("saves/binary"));
    check(bytes.ok && tide_list_count(bytes.value) == 5, "binary files survive a page reload");
    const uint8_t expected[] = {0, 255, 128, 13, 10};
    for (int i = 0; bytes.ok && i < 5; i++) {
        const uint8_t *value = tide_list_at(bytes.value, i, 1);
        check(value && *value == expected[i], "binary bytes remain exact");
    }
    check(tide_directory_exists(io, save_path("saves/renamed")), "directory moves persist");
    check(!tide_file_exists(io, save_path("saves/moved")), "file deletion persists");
    const tide_io_text_result again = tide_file_read_all_text(io, save_path("saves/again"));
    check(again.ok && tide_str_eq(again.value, text("second")), "a file changed again while it was being saved is saved as it was left");
    const tide_io_text_result late = tide_file_read_all_text(io, save_path("saves/late"));
    check(late.ok && tide_str_eq(late.value, text("late")), "a file written while another was being saved is saved too");
}
static int frame(void *user, float seconds)
{
    (void)user, (void)seconds;
    tide_io_error error = TIDE_IO_NONE;
    if (!strcmp(phase, "busy") && stage == 0) {
        // The first save's transaction was made before this frame, and the
        // page keeps it from finishing: what's written here is written while
        // it's on its way.
        check_writes(1, "a save is on its way");
        check(!tide_storage_flush_poll(io, ticket, &error), "a flush isn't done before its changes committed");
        check(tide_file_write_all_text(io, save_path("saves/again"), text("second")).ok, "change a file that's being saved");
        check(tide_file_write_all_text(io, save_path("saves/late"), text("late")).ok, "write another meanwhile");
        earlier = ticket;
        ticket = tide_storage_flush_begin(io);
        tide_web_eval("tideHold = false");
        stage++;
        return TIDE_KEEP_RUNNING;
    }
    if (!tide_storage_flush_poll(io, ticket, &error)) return TIDE_KEEP_RUNNING;
    tide_storage_flush_forget(io, ticket);
    if (!strcmp(phase, "busy")) {
        check(error == TIDE_IO_NONE, "a flush made while a save was on its way finishes");
        tide_io_error first = TIDE_IO_FAILED;
        check(tide_storage_flush_poll(io, earlier, &first) && first == TIDE_IO_NONE, "the flush before it finished first");
        check_writes(2, "what changed meanwhile is one transaction more");
    } else if (!strcmp(phase, "denied") || !strcmp(phase, "read-denied")) {
        check(error == TIDE_IO_ACCESS_DENIED || error == TIDE_IO_STORAGE_UNAVAILABLE,
              "flush reports unavailable persistent storage");
    } else if (!strcmp(phase, "quota") && stage == 0) {
        check(error == TIDE_IO_OUT_OF_SPACE, "flush reports quota failure");
        check(tide_file_read_all_text(io, save_path("retry")).ok, "failed commits retain the memory copy");
        tide_web_eval("IDBDatabase.prototype.transaction = window.ioTransaction");
        ticket = tide_storage_flush_begin(io);
        stage++;
        return TIDE_KEEP_RUNNING;
    } else if (!strcmp(phase, "quota") && stage == 1) {
        check(error == TIDE_IO_NONE, "a failed commit can be retried");
        check_writes(1, "the retry is one transaction");
        check(tide_file_delete(io, save_path("retry")).ok, "clean up the retry file");
        ticket = tide_storage_flush_begin(io);
        stage++;
        return TIDE_KEEP_RUNNING;
    } else check(error == TIDE_IO_NONE, "save changes finish committing");
    if (!strcmp(phase, "write") || !strcmp(phase, "read"))
        check_writes(1, "a flush after changes saves them in one transaction");
    else if (!strcmp(phase, "quota")) check_writes(2, "a flush after a change is one transaction more");
    else if (!strcmp(phase, "other")) check_writes(0, "a flush with nothing to save makes no transaction");
    return failures ? 1 : 0;
}
int main(int argc, char **argv)
{
    phase = argc > 1 ? argv[1] + 2 : "write";
    io = tide_platform_io_init(!strcmp(phase, "other") ? "dev.tide.io.other" : "dev.tide.io.test", NULL);
    if (!strcmp(phase, "write")) {
        check(tide_directory_create_directory(io, save_path("saves/sub")).ok, "create parent directories");
        check(tide_directory_create_directory(io, save_path("saves/sub")).ok, "directory creation is idempotent");
        check(tide_file_write_all_text(io, save_path("saves/text.txt"), text("saved ")).ok, "write a file");
        check(tide_file_append_all_text(io, save_path("saves/text.txt"), text("\xC3\xA9\n")).ok, "append UTF-8 text");
        check(tide_file_create(io, save_path("saves/empty")).ok, "create an empty file");
        tide_list bytes = {0};
        const uint8_t values[] = {0, 255, 128, 13, 10};
        for (int i = 0; i < 5; i++) *(uint8_t *)tide_list_add(&bytes, 1, TIDE_IN_SCRATCH) = values[i];
        check(tide_file_write_all_bytes(io, text("saves/binary"), bytes).ok, "relative paths use the save directory");
        check(tide_file_read_all_text(io, save_path("saves/binary")).error == TIDE_IO_INVALID_DATA,
              "binary contents are not silently decoded as text");
        check(tide_file_copy(io, save_path("saves/binary"), save_path("saves/copy"), false).ok, "copy a file");
        check(tide_file_copy(io, save_path("saves/binary"), save_path("saves/copy"), false).error == TIDE_IO_ALREADY_EXISTS,
              "copy refuses overwriting by default");
        check(tide_file_move(io, save_path("saves/copy"), save_path("saves/moved"), false).ok, "move a file");
        check(tide_file_delete(io, save_path("saves/moved")).ok, "delete a file");
        check(tide_directory_move(io, save_path("saves/sub"), save_path("saves/renamed")).ok, "move a directory");
        const tide_io_list_result paths = tide_directory_get_files(io, save_path("saves"), false);
        check(paths.ok && tide_list_count(paths.value) == 3, "enumerate only files");
        if (paths.ok && tide_list_count(paths.value) == 3) {
            const tide_text *first = tide_list_at(paths.value, 0, sizeof(tide_text));
            check(first && tide_str_eq(tide_text_view(*first), save_path("saves/binary")), "enumeration returns sorted full paths");
        }
        check(tide_directory_delete(io, save_path("saves"), false).error == TIDE_IO_DIRECTORY_NOT_EMPTY,
              "directory deletion requires explicit recursion");
        check(tide_file_write_all_text(io, save_path("../other-game/file"), text("x")).error == TIDE_IO_ACCESS_DENIED,
              "paths cannot escape this game's virtual roots");
        io = tide_platform_io_init("dev.tide.io.test", NULL);
        check(tide_file_exists(io, save_path("saves/text.txt")), "reinitializing for hot reload retains pending files");
    } else if (!strcmp(phase, "busy")) {
        tide_web_eval("tideHold = true");
        check(tide_file_write_all_text(io, save_path("saves/again"), text("first")).ok, "write a file whose save the page holds up");
    } else if (!strcmp(phase, "read")) {
        read_saved();
        check(tide_directory_delete(io, save_path("saves"), true).ok, "recursive directory deletion");
    } else if (!strcmp(phase, "other")) {
        check(tide_file_read_all_text(io, save_path("saves/text.txt")).error == TIDE_IO_NOT_FOUND,
              "games on one origin have separate save locations");
    } else if (!strcmp(phase, "denied") || !strcmp(phase, "read-denied")) {
        const tide_io_void_result saved = tide_file_write_all_text(io, save_path("refused"), text("save"));
        check(!saved.ok && (saved.error == TIDE_IO_ACCESS_DENIED || saved.error == TIDE_IO_STORAGE_UNAVAILABLE),
              "unavailable persistence is reported as an error");
        const tide_str temporary = tide_path_combine(tide_application_temporary_cache_path(io), text("works"));
        check(tide_file_write_all_text(io, temporary, text("temp")).ok, "temporary files work when persistence is denied");
        check(tide_file_read_all_text(io, temporary).ok, "temporary files can be read");
    } else if (!strcmp(phase, "quota")) {
        tide_web_eval("{ window.ioTransaction = IDBDatabase.prototype.transaction;"
                      " IDBDatabase.prototype.transaction = function(names, mode, options) {"
                      " if (mode === 'readwrite') throw new DOMException('full', 'QuotaExceededError');"
                      " return window.ioTransaction.call(this, names, mode, options); }; }");
        check(tide_file_write_all_text(io, save_path("retry"), text("save")).ok, "writes are accepted before asynchronous persistence");
    }
    ticket = tide_storage_flush_begin(io);
    tide_platform_open(&(tide_window_desc){.title = "web files", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
