#pragma once

#include <stdbool.h>
#include <stddef.h>

// Putting a package over an installation, for tide upgrade.
//
// An installation says which version it is in its VERSION file, and it's whole
// when that's its tide's own version. The file goes before anything of the
// installation changes and comes back last, so an upgrade that stopped partway
// leaves an installation that says so, which `tide upgrade` installs again.

// Whether the installation in `root` is all of `version`.
bool tide_installation_whole(const char *root, const char *version);

// Puts the package unpacked in `fresh` over the installation in `root`: every
// file and folder of the package replaces the installation's, and what's in
// root that the package has none of (the channel, downloaded packages,
// Android's tools) stays. What's replaced goes to `trash`, a folder on root's
// volume, which the caller deletes.
//
// Nothing of the installation goes before what replaces it is in its place.
// A folder another program has open (an editor showing a header of it) can't
// be moved, so what's in it is replaced instead, file by file. False when
// something couldn't be replaced, with its path in `failed`: it stops there,
// and the installation is whole only if nothing had changed yet.
bool tide_install(const char *root, const char *fresh, const char *trash, char *failed, size_t failed_size);
