#pragma once

// Runs the original command with the game's pinned release, installing it beside
// Unity's cached versions after consent. -1 means continue with this executable.
int tide_use_pinned(const char *folder, int argc, char **argv, const char *version);
