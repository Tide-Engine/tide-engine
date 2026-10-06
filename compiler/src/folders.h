#pragma once

#include <stdbool.h>

// Files on disk, kept apart so the server doesn't see windows.h.

typedef void (*folder_file_fn)(void *user, const char *path);

// Calls `found` with every file in `folder` (a path ending in '/') and its
// subfolders whose name ends with `extension`, such as ".tide". Paths are the
// folder followed by forward slashes. Links to other folders aren't followed.
void folder_find(const char *folder, const char *extension, folder_file_fn found, void *user);

// Like folder_find, but leaves out subfolders that hold a tide.packages file:
// each is a game or a package of its own (see packages.h).
void folder_find_own(const char *folder, const char *extension, folder_file_fn found, void *user);

// The folder this program is in, with forward slashes and ending in '/'.
// malloc'd; NULL if the OS won't say.
char *folder_of_program(void);

// Every file in `folder` (ending in '/') and its subfolders, with its size, but
// for hidden folders (a name starting with '.', so .tide/ and .git/) and the
// build/ folder at `folder` itself: what tide makes is no part of a game.
// Paths are the folder followed by forward slashes.
typedef void (*folder_sized_fn)(void *user, const char *path, unsigned long long size);
void folder_find_files(const char *folder, folder_sized_fn found, void *user);

// What's in `folder` itself (ending in '/'): each entry's name, and whether
// it's a folder. Hidden entries (a name starting with '.') are left out.
typedef void (*folder_entry_fn)(void *user, const char *name, bool is_folder);
void folder_list(const char *folder, folder_entry_fn found, void *user);

// Whether a file is at `path`, how big it is, and when it was last written
// (a number that changes when it is; the system's units).
bool folder_file_info(const char *path, unsigned long long *size, unsigned long long *modified);

// Makes the folder at `path`, and every missing one on the way. True when
// it's there afterwards.
bool folder_make(const char *path);
