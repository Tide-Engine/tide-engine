#pragma once

// `tide publish`: sends a version of a game's source to the build service
// (Tide-Engine/tide-services, builds/), which builds the game with mods from it.

// Publishes the game in `folder` with the key in TIDE_GAME_KEY, which names
// the game too: every file a build reads from its folder (tide_game_sources),
// as one archive that's the same bytes for the same files on every machine,
// so the same source is the same version wherever it's published from. The
// service is TIDE_BUILDS_URL's, or builds.tide-engine.dev. Returns the exit
// code.
int tide_publish(const char *folder);
