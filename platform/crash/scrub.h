#pragma once

// What tide-crash masks in a report before it leaves the player's machine
// (scrub.c): personal data the files would otherwise carry. Masking keeps
// every length, with '*' in place of what's masked, so a dump's structure,
// and the place of everything else in it, stays.

#include <stdbool.h>
#include <stddef.h>

// Names of this machine's to mask wherever they're a whole word: the user's,
// their home folder's and the machine's. Fewer than 3 letters, or a word as
// common as "user", isn't masked but in paths.
enum { SCRUB_NAMES = 4 };
typedef struct scrub_names {
    char name[SCRUB_NAMES][128];
    int count;
} scrub_names;

// This machine's (the environment, and the machine's name)
void scrub_names_here(scrub_names *names);
void scrub_names_add(scrub_names *names, const char *name);

// A text file's bytes (UTF-8): the folder after each Users/ or home/ in a
// path, the names, IP addresses but the machine's own (127.x, ::1), and
// email addresses.
void scrub_text(char *bytes, size_t size, const scrub_names *names);

// A minidump's (crash.dmp): paths and names in UTF-16 and in bytes, through
// the whole file, and text's rules besides in the memory it holds (its stacks).
void scrub_dump(unsigned char *bytes, size_t size, const scrub_names *names);
