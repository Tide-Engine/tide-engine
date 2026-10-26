// tide upgrade's package put over an installation: what's replaced, what
// stays, and that nothing is lost when a file can't be replaced.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "install.h"
#include "sys.h"
#include "tide_test.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define WORK "install_test_out"
#define ROOT WORK "/Tide"
#define FRESH ROOT "/.upgrade/new"
#define TRASH ROOT "/.upgrade/old"

static void put(const char *root, const char *file, const char *text)
{
    char *path = path_join(root, file);
    char *dir = path_dir(path);
    TIDE_CHECK(sys_mkdirs(dir));
    TIDE_CHECK(sys_write_text(path, text));
    free(dir);
    free(path);
}

static bool there(const char *root, const char *file)
{
    char *path = path_join(root, file);
    const bool found = sys_exists(path);
    free(path);
    return found;
}

static bool says(const char *root, const char *file, const char *text)
{
    char *path = path_join(root, file);
    char *got = sys_read_file(path, NULL);
    const bool same = got && strcmp(got, text) == 0;
    free(got);
    free(path);
    return same;
}

// Version 1.0.0, installed, with what a user's tide gathers beside the package.
static void old_installation(void)
{
    sys_remove_tree(WORK);
    put(ROOT, "bin/tide", "old tide");
    put(ROOT, "bin/tidels", "old tidels");
    put(ROOT, "include/tide/run.h", "old run.h");
    put(ROOT, "include/tide/gone.h", "a header 1.1.0 has none of");
    put(ROOT, "lib/native/libtide_platform.a", "old platform");
    put(ROOT, "src/engine/world.c", "old world.c");
    put(ROOT, "src/engine/gone.c", "a source 1.1.0 has none of");
    put(ROOT, "VERSION", "1.0.0\n");
    put(ROOT, "channel", "nightly");
    put(ROOT, "android/ndk/clang", "the NDK");
    put(ROOT, "packages/dice/dice.tide", "a game's package");
}

// Version 1.1.0's package, unpacked.
static void new_package(void)
{
    sys_remove_tree(ROOT "/.upgrade");
    put(FRESH, "bin/tide", "new tide");
    put(FRESH, "bin/tidels", "new tidels");
    put(FRESH, "include/tide/run.h", "new run.h");
    put(FRESH, "include/tide/draw.h", "new draw.h");
    put(FRESH, "lib/native/libtide_platform.a", "new platform");
    put(FRESH, "src/engine/world.c", "new world.c");
    put(FRESH, "web/tide.js", "new tide.js");
    put(FRESH, "VERSION", "1.1.0\n");
}

static bool install(char failed[1024])
{
    return tide_install(ROOT, FRESH, TRASH, failed, 1024);
}

// All of 1.1.0, nothing of 1.0.0, and what isn't the package's as it was.
static void check_upgraded(void)
{
    TIDE_CHECK(tide_installation_whole(ROOT, "1.1.0"));
    TIDE_CHECK(says(ROOT, "bin/tide", "new tide"));
    TIDE_CHECK(says(ROOT, "bin/tidels", "new tidels"));
    TIDE_CHECK(says(ROOT, "include/tide/run.h", "new run.h"));
    TIDE_CHECK(says(ROOT, "include/tide/draw.h", "new draw.h"));
    TIDE_CHECK(says(ROOT, "lib/native/libtide_platform.a", "new platform"));
    TIDE_CHECK(says(ROOT, "src/engine/world.c", "new world.c"));
    TIDE_CHECK(says(ROOT, "web/tide.js", "new tide.js"));
    TIDE_CHECK(!there(ROOT, "include/tide/gone.h"));
    TIDE_CHECK(!there(ROOT, "src/engine/gone.c"));
    TIDE_CHECK(says(ROOT, "channel", "nightly"));
    TIDE_CHECK(says(ROOT, "android/ndk/clang", "the NDK"));
    TIDE_CHECK(says(ROOT, "packages/dice/dice.tide", "a game's package"));
}

TIDE_TEST(cli_an_installation_is_whole_when_its_version_is_its_tides)
{
    old_installation();
    TIDE_CHECK(tide_installation_whole(ROOT, "1.0.0"));
    TIDE_CHECK(!tide_installation_whole(ROOT, "1.1.0"));
    put(ROOT, "VERSION", "1.0.0\r\n");
    TIDE_CHECK(tide_installation_whole(ROOT, "1.0.0"));
    sys_remove(ROOT "/VERSION");
    TIDE_CHECK(!tide_installation_whole(ROOT, "1.0.0"));
    sys_remove_tree(WORK);
}

TIDE_TEST(cli_an_upgrade_replaces_the_package_and_keeps_the_rest)
{
    old_installation();
    new_package();
    char failed[1024];
    TIDE_CHECK(install(failed));
    TIDE_CHECK(failed[0] == '\0');
    check_upgraded();
    sys_remove_tree(WORK);
}

// An installation an upgrade half-finished: its new tide, headers of the
// version before, and nothing else of the package.
TIDE_TEST(cli_an_upgrade_finishes_one_that_stopped_partway)
{
    old_installation();
    put(ROOT, "bin/tide", "new tide");
    sys_remove_tree(ROOT "/lib");
    sys_remove_tree(ROOT "/src");
    sys_remove(ROOT "/VERSION");
    TIDE_CHECK(!tide_installation_whole(ROOT, "1.1.0"));
    new_package();
    char failed[1024];
    TIDE_CHECK(install(failed));
    check_upgraded();
    sys_remove_tree(WORK);
}

#ifdef _WIN32

// Only Windows keeps a file another program has open from moving, and a
// folder with such a file anywhere in it.

// A folder as an editor watches the one a file it shows is in.
static HANDLE watch(const char *root, const char *folder, const DWORD share)
{
    char *path = path_join(root, folder);
    const HANDLE h = CreateFileA(path, FILE_LIST_DIRECTORY, share, NULL, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    free(path);
    return h;
}

// A file as a program reading it has it, which nothing can move meanwhile.
static HANDLE hold(const char *root, const char *file)
{
    char *path = path_join(root, file);
    const HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(path);
    return h;
}

TIDE_TEST(cli_an_upgrade_gets_past_a_folder_an_editor_watches)
{
    old_installation();
    new_package();
    const HANDLE editor = watch(ROOT, "include/tide", FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    TIDE_REQUIRE(editor != INVALID_HANDLE_VALUE);
    // The folder above the open file doesn't move either.
    TIDE_CHECK(!sys_rename(ROOT "/include", ROOT "/include.moved"));
    char failed[1024];
    TIDE_CHECK(install(failed));
    check_upgraded();
    CloseHandle(editor);
    sys_remove_tree(WORK);
}

// A terminal whose folder it is, which keeps the folder itself from moving too.
TIDE_TEST(cli_an_upgrade_gets_past_a_folder_a_program_is_in)
{
    old_installation();
    new_package();
    const HANDLE terminal = watch(ROOT, "src/engine", FILE_SHARE_READ | FILE_SHARE_WRITE);
    TIDE_REQUIRE(terminal != INVALID_HANDLE_VALUE);
    TIDE_CHECK(!sys_rename(ROOT "/src/engine", ROOT "/src/engine.moved"));
    char failed[1024];
    TIDE_CHECK(install(failed));
    check_upgraded();
    CloseHandle(terminal);
    sys_remove_tree(WORK);
}

TIDE_TEST(cli_an_upgrade_that_cant_replace_a_file_loses_nothing)
{
    old_installation();
    new_package();
    const HANDLE compiler = hold(ROOT, "src/engine/world.c");
    TIDE_REQUIRE(compiler != INVALID_HANDLE_VALUE);
    char failed[1024];
    TIDE_CHECK(!install(failed));
    TIDE_CHECK(strstr(failed, "src/engine/world.c") != NULL);
    // It says it isn't whole, and every part of it is still there, of one
    // version or the other; tide itself, which goes last, is the old one.
    TIDE_CHECK(!tide_installation_whole(ROOT, "1.0.0"));
    TIDE_CHECK(!tide_installation_whole(ROOT, "1.1.0"));
    TIDE_CHECK(says(ROOT, "bin/tide", "old tide"));
    TIDE_CHECK(there(ROOT, "include/tide/run.h"));
    TIDE_CHECK(there(ROOT, "lib/native/libtide_platform.a"));
    TIDE_CHECK(says(ROOT, "src/engine/world.c", "old world.c"));
    CloseHandle(compiler);

    // The next upgrade finishes it.
    new_package();
    TIDE_CHECK(install(failed));
    check_upgraded();
    sys_remove_tree(WORK);
}

TIDE_TEST(cli_an_upgrade_that_changed_nothing_leaves_the_installation_whole)
{
    old_installation();
    sys_remove_tree(ROOT "/.upgrade");
    put(FRESH, "include/tide/run.h", "new run.h");
    put(FRESH, "VERSION", "1.1.0\n");
    sys_remove(ROOT "/include/tide/gone.h");
    const HANDLE compiler = hold(ROOT, "include/tide/run.h");
    TIDE_REQUIRE(compiler != INVALID_HANDLE_VALUE);
    char failed[1024];
    TIDE_CHECK(!install(failed));
    TIDE_CHECK(strstr(failed, "include/tide/run.h") != NULL);
    TIDE_CHECK(tide_installation_whole(ROOT, "1.0.0"));
    TIDE_CHECK(says(ROOT, "include/tide/run.h", "old run.h"));
    CloseHandle(compiler);
    sys_remove_tree(WORK);
}

#endif
