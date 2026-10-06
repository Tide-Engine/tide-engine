// What a game's world is with assets in the game (assets.tide) and without
// (assets_plain.tide): the same, as nothing of an asset is in a world. Both
// tests check these; a change to how worlds are kept updates them from one
// platform, and every other has to agree.
#define ASSETS_WORLD_SIZE 376u
#define ASSETS_EMPTY_WORLD_HASH 0x9c5e5501b0183900ull
