// What a game's world hashes to with assets in the game (assets.tide) and
// without (assets_plain.tide): the same, as nothing of an asset is in a world.
// Both tests check it, on every platform; a change to how worlds are kept
// updates it from one platform, and every other has to agree.
#define ASSETS_EMPTY_WORLD_HASH 0x5709e92688371e8aull
