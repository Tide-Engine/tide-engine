#pragma once

// `tide add`, `tide update` and `tide eject`: changing the tide.packages of the game in
// `folder` (see packages.h). Both add the lines of what its packages need
// that it doesn't list yet. Return the exit code.

// Adds a package: from git (a URL or a source, with @ref to follow a branch
// or version, at that ref's newest commit), or a folder on this machine.
int tide_add(const char *folder, const char *what);

// Downloads the packages from git that the game lists and this machine
// doesn't have yet, each at its commit, with nothing built: what a build would
// download as it starts, so one after it needs no network.
int tide_fetch(const char *folder);

// Moves each named package (by its name, its repository's, or its source),
// or with none every package from git, to the newest commit of what it
// follows.
int tide_update(const char *folder, const char *const *names, int count);

// Copies one of the game's packages (named as for tide_update), from git or
// one that comes with tide, into packages/ in the game's folder, and makes
// its line that folder: the package is the game's own to change from there.
int tide_eject(const char *folder, const char *name);
