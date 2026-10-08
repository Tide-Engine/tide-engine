// Paths made whole (path_absolute): a file that isn't there yet, as the one a
// run with no window is given to write, is where it was typed from this
// folder, under the path it has once it's written.

#ifndef _WIN32
#define _DEFAULT_SOURCE // symlink under strict C
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "tide_test.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#define WORK "sys_test_out"

// From the root, and with forward slashes alone: the same file from any folder.
static bool whole(const char *path)
{
    if (strchr(path, '\\')) return false;
#ifdef _WIN32
    return isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/';
#else
    return path[0] == '/';
#endif
}

// Whether `typed` made whole is `name` in `folder`, a path that's whole already.
static bool is_in(const char *typed, const char *folder, const char *name)
{
    char *got = path_absolute(typed);
    char *expected = path_join(folder, name);
    const bool same = strcmp(got, expected) == 0;
    if (!same) printf("    '%s' is '%s', not '%s'\n", typed, got, expected);
    free(expected);
    free(got);
    return same;
}

TIDE_TEST(absolute_path_of_a_file_not_there_yet)
{
    sys_remove_tree(WORK);
    TIDE_REQUIRE(sys_mkdirs(WORK "/shots"));
    char *here = path_absolute(".");
    char *work = path_absolute(WORK);
    char *shots = path_absolute(WORK "/shots");
    TIDE_CHECK(whole(here));
    TIDE_CHECK(is_in(WORK, here, WORK));
    TIDE_CHECK(is_in(WORK "/shots", work, "shots"));

    // A file to be written, in a folder that's there: nothing is made of it
    char *frame = path_absolute(WORK "/shots/frame.png");
    TIDE_CHECK(whole(frame));
    TIDE_CHECK(is_in(WORK "/shots/frame.png", shots, "frame.png"));
    TIDE_CHECK(!sys_exists(frame));
    // A name alone is in this folder
    TIDE_CHECK(is_in("sys_test_not_there.png", here, "sys_test_not_there.png"));
    TIDE_CHECK(!sys_exists("sys_test_not_there.png"));
    // Folders that aren't there either, which whoever writes the file may make
    TIDE_CHECK(is_in(WORK "/not/there/frame.png", work, "not/there/frame.png"));
    TIDE_CHECK(is_in(WORK "/not/", work, "not"));
    TIDE_CHECK(!sys_exists(WORK "/not"));
    // The part that's there says where it is: up from a folder is the one it's in
    TIDE_CHECK(is_in(WORK "/shots/../frame.png", work, "frame.png"));
    TIDE_CHECK(is_in("./" WORK "/shots/./frame.png", shots, "frame.png"));
    // A whole path stays what it is
    char *elsewhere = path_join(work, "not/there/frame.png");
    TIDE_CHECK(is_in(elsewhere, work, "not/there/frame.png"));
    free(elsewhere);
    // No path names nothing
    char *none = path_absolute("");
    TIDE_CHECK(strcmp(none, "") == 0);
    free(none);

    // Written through the whole path, it's the file that was typed, and its path is the same once it's there
    TIDE_CHECK(sys_write_text(frame, "a frame"));
    TIDE_CHECK(sys_exists(WORK "/shots/frame.png"));
    TIDE_CHECK(is_in(WORK "/shots/frame.png", shots, "frame.png"));

#ifndef _WIN32
    // Through a link, a file is in the folder the link names, there or not
    TIDE_REQUIRE(symlink("shots", WORK "/link") == 0);
    TIDE_CHECK(is_in(WORK "/link", work, "shots"));
    TIDE_CHECK(is_in(WORK "/link/frame.png", shots, "frame.png"));
    TIDE_CHECK(is_in(WORK "/link/later.png", shots, "later.png"));
    TIDE_CHECK(is_in(WORK "/link/../later.png", work, "later.png"));
#endif

    free(frame);
    free(shots);
    free(work);
    free(here);
    sys_remove_tree(WORK);
}
