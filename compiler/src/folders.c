#ifndef _WIN32
#define _DEFAULT_SOURCE // lstat and readlink under strict C
#endif

#include "folders.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

static bool ends_with(const char *name, const char *extension)
{
    const size_t n = strlen(name);
    const size_t e = strlen(extension);
    if (n <= e) return false;
#ifdef _WIN32
    return _stricmp(name + n - e, extension) == 0; // Windows names ignore case
#else
    return strcmp(name + n - e, extension) == 0;
#endif
}

// `folder` + `name` + `suffix`, malloc'd.
static char *join(const char *folder, const char *name, const char *suffix)
{
    const size_t len = strlen(folder) + strlen(name) + strlen(suffix) + 1;
    char *path = malloc(len);
    if (!path) abort();
    snprintf(path, len, "%s%s%s", folder, name, suffix);
    return path;
}

// Whether `sub` (ending in '/') is a game or a package of its own.
static bool has_packages_file(const char *sub)
{
    char *path = join(sub, "tide.packages", "");
    FILE *f = fopen(path, "rb");
    free(path);
    if (f) fclose(f);
    return f != NULL;
}

static void find(const char *folder, const char *extension, const folder_file_fn found, void *user, const bool own)
{
#ifdef _WIN32
    char *pattern = join(folder, "*", "");
    WIN32_FIND_DATAA entry;
    const HANDLE search = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = entry.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue; // A link
            if (name[0] == '.') continue; // A hidden folder, like .tide/, .build/ and .git/
            char *sub = join(folder, name, "/");
            if (!own || !has_packages_file(sub)) find(sub, extension, found, user, own);
            free(sub);
        } else if (ends_with(name, extension)) {
            char *path = join(folder, name, "");
            found(user, path);
            free(path);
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
#else
    DIR *dir = opendir(folder);
    if (!dir) return;
    for (const struct dirent *entry; (entry = readdir(dir));) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char *path = join(folder, name, "");
        struct stat info;
        if (lstat(path, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                if (name[0] != '.') { // Not a hidden folder, like .tide/, .build/ and .git/
                    char *sub = join(folder, name, "/");
                    if (!own || !has_packages_file(sub)) find(sub, extension, found, user, own);
                    free(sub);
                }
            } else if (S_ISREG(info.st_mode) && ends_with(name, extension)) {
                found(user, path);
            }
        }
        free(path);
    }
    closedir(dir);
#endif
}

void folder_find(const char *folder, const char *extension, const folder_file_fn found, void *user)
{
    find(folder, extension, found, user, false);
}

void folder_find_own(const char *folder, const char *extension, const folder_file_fn found, void *user)
{
    find(folder, extension, found, user, true);
}

char *folder_of_program(void)
{
    char path[4096] = "";
#ifdef _WIN32
    const DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    if (n == 0 || n == sizeof path) return NULL;
#elif defined(__APPLE__)
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size) != 0) return NULL;
#else
    const ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return NULL;
    path[n] = '\0';
#endif
    char *slash = NULL;
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
        if (*p == '/') slash = p;
    }
    if (!slash) return NULL;
    slash[1] = '\0';
    return join(path, "", "");
}

// ---------------------------------------------------------------------------
// Assets: every file, by size, and a folder's entries

#ifdef _WIN32
static void find_files(const char *folder, const folder_enter_fn enter, const folder_sized_fn found, void *user)
{
    char *pattern = join(folder, "*", "");
    WIN32_FIND_DATAA entry;
    const HANDLE search = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = entry.cFileName;
        if (name[0] == '.') continue; // ".", ".." and hidden folders and files
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            char *sub = join(folder, name, "/");
            if (!enter || enter(user, sub)) find_files(sub, enter, found, user);
            free(sub);
        } else {
            char *path = join(folder, name, "");
            found(user, path, ((unsigned long long)entry.nFileSizeHigh << 32) | entry.nFileSizeLow);
            free(path);
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
}
#else
static void find_files(const char *folder, const folder_enter_fn enter, const folder_sized_fn found, void *user)
{
    DIR *dir = opendir(folder);
    if (!dir) return;
    for (const struct dirent *entry; (entry = readdir(dir));) {
        const char *name = entry->d_name;
        if (name[0] == '.') continue;
        char *path = join(folder, name, "");
        struct stat info;
        if (lstat(path, &info) == 0) {
            if (S_ISDIR(info.st_mode)) {
                char *sub = join(folder, name, "/");
                if (!enter || enter(user, sub)) find_files(sub, enter, found, user);
                free(sub);
            } else if (S_ISREG(info.st_mode)) {
                found(user, path, (unsigned long long)info.st_size);
            }
        }
        free(path);
    }
    closedir(dir);
}
#endif

void folder_find_files(const char *folder, const folder_sized_fn found, void *user)
{
    find_files(folder, NULL, found, user);
}

void folder_find_files_in(const char *folder, const folder_enter_fn enter, const folder_sized_fn found, void *user)
{
    find_files(folder, enter, found, user);
}

void folder_list(const char *folder, const folder_entry_fn found, void *user)
{
#ifdef _WIN32
    char *pattern = join(folder, "*", "");
    WIN32_FIND_DATAA entry;
    const HANDLE search = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (entry.cFileName[0] == '.') continue;
        found(user, entry.cFileName, (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileA(search, &entry));
    FindClose(search);
#else
    DIR *dir = opendir(folder);
    if (!dir) return;
    for (const struct dirent *entry; (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') continue;
        char *path = join(folder, entry->d_name, "");
        struct stat info;
        if (stat(path, &info) == 0) found(user, entry->d_name, S_ISDIR(info.st_mode));
        free(path);
    }
    closedir(dir);
#endif
}

bool folder_file_info(const char *path, unsigned long long *size, unsigned long long *modified)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    if (size) *size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (modified) *modified = ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    return true;
#else
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return false;
    if (size) *size = (unsigned long long)info.st_size;
    if (modified) {
#ifdef __APPLE__
        *modified = (unsigned long long)info.st_mtimespec.tv_sec * 1000000000u
                  + (unsigned long long)info.st_mtimespec.tv_nsec;
#else
        *modified = (unsigned long long)info.st_mtim.tv_sec * 1000000000u
                  + (unsigned long long)info.st_mtim.tv_nsec;
#endif
    }
    return true;
#endif
}

bool folder_make(const char *path)
{
    char *copy = malloc(strlen(path) + 1);
    if (!copy) abort();
    strcpy(copy, path);
    for (char *p = copy + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        *p = '\0';
#ifdef _WIN32
        CreateDirectoryA(copy, NULL);
#else
        mkdir(copy, 0777);
#endif
        *p = '/';
    }
#ifdef _WIN32
    CreateDirectoryA(copy, NULL);
    const DWORD attributes = GetFileAttributesA(copy);
    const bool made = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    mkdir(copy, 0777);
    struct stat st;
    const bool made = stat(copy, &st) == 0 && S_ISDIR(st.st_mode);
#endif
    free(copy);
    return made;
}

bool folder_current(char *out, const size_t size)
{
#ifdef _WIN32
    const DWORD n = GetCurrentDirectoryA((DWORD)size, out);
    if (!n || n >= size) return false;
#else
    if (!getcwd(out, size)) return false;
#endif
    for (char *c = out; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    const size_t len = strlen(out);
    if (len > 1 && out[len - 1] == '/') out[len - 1] = '\0';
    return true;
}
