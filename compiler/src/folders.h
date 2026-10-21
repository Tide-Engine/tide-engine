#pragma once

#include <stdbool.h>
#include <stddef.h>

// Files on disk, kept apart so the server doesn't see windows.h.

typedef void (*folder_file_fn)(void *user, const char *path);

// Calls `found` with every file in `folder` (a path ending in '/') and its
// subfolders whose name ends with `extension`, such as ".tide". Paths are the
// folder followed by forward slashes. Links to other folders aren't followed,
// and hidden folders (a name starting with '.', so .tide/, .build/ and .git/)
// aren't looked in: what tide makes is no part of a game.
void folder_find(const char *folder, const char *extension, folder_file_fn found, void *user);

// Like folder_find, but leaves out subfolders that hold a tide.packages file:
// each is a game or a package of its own (see packages.h).
void folder_find_own(const char *folder, const char *extension, folder_file_fn found, void *user);

// The folder this program is in, with forward slashes and ending in '/'.
// malloc'd; NULL if the OS won't say.
char *folder_of_program(void);

// Every file in `folder` (ending in '/') and its subfolders, with its size, but
// for hidden files and folders (a name starting with '.', so .tide/, .build/
// and .git/): what tide makes is no part of a game.
// Paths are the folder followed by forward slashes.
typedef void (*folder_sized_fn)(void *user, const char *path, unsigned long long size);
void folder_find_files(const char *folder, folder_sized_fn found, void *user);

// Like folder_find_files, but only goes into the subfolders `enter` says to,
// each given as its path ending in '/': for whoever knows where the files it
// wants can be, so that nothing else is walked.
typedef bool (*folder_enter_fn)(void *user, const char *folder);
void folder_find_files_in(const char *folder, folder_enter_fn enter, folder_sized_fn found, void *user);

// What's in `folder` itself (ending in '/'): each entry's name, and whether
// it's a folder. Hidden entries (a name starting with '.') are left out.
typedef void (*folder_entry_fn)(void *user, const char *name, bool is_folder);
void folder_list(const char *folder, folder_entry_fn found, void *user);

// Whether a file is at `path`, how big it is, and when it was last written
// (a number that changes when it is, retaining the system's last-write precision).
bool folder_file_info(const char *path, unsigned long long *size, unsigned long long *modified);

// Makes the folder at `path`, and every missing one on the way. True when
// it's there afterwards.
bool folder_make(const char *path);

// The folder this program runs in, with forward slashes and no slash at the
// end, into `out`. False when the system won't say.
bool folder_current(char *out, size_t size);
