#include <math.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static bool near3(const tide_float3 a, const float x, const float y, const float z)
{
    return fabsf(a.x - x) < 1e-5f && fabsf(a.y - y) < 1e-5f && fabsf(a.z - z) < 1e-5f;
}

// legs.glb (mesh_bones_make.mjs): a triangle in centimetres under an armature scaled by 0.01, its mesh's node
// 500 cm aside. A Mesh with bones has it at rest, in metres, where a SkinnedMesh of the file draws it.
TIDE_TEST(mesh_bones_are_the_model_at_rest_with_its_skeletons_bones)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Seen *seen = &world.Seen;
    TIDE_CHECK(seen->corners == 3);
    TIDE_CHECK(near3(seen->first, 0.0f, 1.0f, 0.0f));
    TIDE_CHECK(near3(seen->second, 0.0f, 0.5f, 0.0f));
    TIDE_CHECK(near3(seen->third, -0.1f, 0.75f, 0.0f)); // glTF's x across, as every glTF mesh is read
    TIDE_CHECK(near3(seen->turned, 0.0f, 0.0f, 1.0f));
    // The bones are the skeleton's, as a Bone of it is numbered; the heaviest first, and the weights sum to 1
    TIDE_REQUIRE(seen->hip > 0 && seen->knee > 0 && seen->hip != seen->knee);
    TIDE_CHECK(seen->firstBones.x == seen->hip && seen->firstBones.y == 0 && seen->firstBones.z == 0 && seen->firstBones.w == 0);
    TIDE_CHECK(seen->thirdBones.x == seen->knee && seen->thirdBones.y == seen->hip);
    TIDE_CHECK(fabsf(seen->thirdWeights.x - 0.75f) < 1e-6f && fabsf(seen->thirdWeights.y - 0.25f) < 1e-6f);
    TIDE_CHECK(seen->thirdWeights.z == 0.0f && seen->thirdWeights.w == 0.0f);
    tide_world_free(&world);
}
