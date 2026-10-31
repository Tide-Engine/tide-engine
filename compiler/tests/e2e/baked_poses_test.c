#include <math.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static bool near(const float a, const float b)
{
    return fabsf(a - b) < 1e-5f;
}

static bool near3(const tide_float3 a, const float x, const float y, const float z)
{
    return near(a.x, x) && near(a.y, y) && near(a.z, z);
}

static bool rows(const BakedFrame f, const int x, const int y, const int z, const int w)
{
    return f.rows.x == x && f.rows.y == y && f.rows.z == z && f.rows.w == w;
}

static bool blend(const BakedFrame f, const float x, const float y, const float z, const float w)
{
    return near(f.blend.x, x) && near(f.blend.y, y) && near(f.blend.z, z) && near(f.blend.w, w);
}

static bool clip(const BakedClip c, const int first, const int count, const float duration, const bool loops)
{
    return c.first == first && c.count == count && near(c.duration, duration) && c.loops == loops;
}

// baked_poses.tide bakes arm.glb (baked_poses_make.mjs): Bend turns the elbow a quarter turn over 1 s, Rise lifts
// the root 2 m over 2 s, Hold is one key. glTF's x is across, as every glTF model is read (mesh_bones_test.c), so
// a turn about z that takes the elbow's end left in the file takes it right here.
TIDE_TEST(baked_poses_rows_frames_and_fades)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Seen *seen = &world.Seen;

    // Rows: whole steps over each clip's duration, a clip that loops without its end, one with no time a row
    TIDE_CHECK(seen->bones >= 2);
    TIDE_CHECK(clip(seen->bend, 0, 5, 1.0f, false));
    TIDE_CHECK(clip(seen->rise, 5, 4, 2.0f, true));
    TIDE_CHECK(clip(seen->hold, 9, 1, 0.0f, false));
    TIDE_CHECK(seen->rows == 10);
    TIDE_CHECK(seen->other == -1); // Another skeleton's pose
    TIDE_CHECK(seen->full == -1);  // Past what the GPU's list holds

    // A clip's frame: the rows round the time and how far between, held at the ends, round where it loops
    TIDE_CHECK(rows(seen->between, 2, 3, 2, 3) && blend(seen->between, 0.4f, 0, 1, 0));
    TIDE_CHECK(rows(seen->ended, 4, 4, 4, 4) && blend(seen->ended, 0, 0, 1, 0));
    TIDE_CHECK(rows(seen->before, 0, 1, 0, 1) && blend(seen->before, 0, 0, 1, 0));
    TIDE_CHECK(seen->unknown.rows.x == 0 && seen->unknown.blend.x == 0);
    TIDE_CHECK(rows(seen->round, 6, 7, 6, 7) && blend(seen->round, 0, 0, 1, 0));
    TIDE_CHECK(rows(seen->wrapped, 8, 5, 8, 5) && blend(seen->wrapped, 0.5f, 0, 1, 0));
    TIDE_CHECK(rows(seen->still, 9, 9, 9, 9));

    // Skinned from the rows: the middle row is the elbow a eighth of a turn round
    const float s45 = 0.70710678f, s22 = 0.38268343f, c22 = 0.92387953f;
    TIDE_CHECK(near3(seen->tipBent, s45, 1 + s45, 0));
    TIDE_CHECK(near3(seen->besideBent, -(1 + s45) / 2, (2 + s45) / 2, 0)); // Half the root's, half the elbow's
    // Between rows the matrices blend, linearly: the end is short of a metre from the elbow there
    TIDE_CHECK(near3(seen->tipHalfway, (s22 + s45) / 2, 1 + (c22 + s45) / 2, 0));
    TIDE_CHECK(near3(seen->tipFaded, 0.25f, 1.75f, 0)); // A quarter of the quarter turn's, over rest
    TIDE_CHECK(near3(seen->besideAlone, -s45, 1 + s45, 0)); // The elbow's alone: first of two as heavy
    TIDE_CHECK(seen->poseError < 1e-5f);

    // Sockets: a place on a bone, where the pose puts it
    TIDE_CHECK(near3(seen->socket, 1, 1, 0));
    TIDE_CHECK(near3(seen->socketByPose, seen->socket.x, seen->socket.y, seen->socket.z));
    TIDE_CHECK(near3(seen->noSocket, 0, 0, 0));
    TIDE_CHECK(near3(seen->farBone, 0, 0, 0));

    // A fader: a jump fades from what it showed, eased; once faded, the frame alone
    TIDE_CHECK(rows(seen->faded, 5, 6, 2, 3) && blend(seen->faded, 0, 0, 0.15625f, 0));
    TIDE_CHECK(rows(seen->settled, 5, 6, 5, 6) && blend(seen->settled, 0.2f, 0, 1, 0));
    // A second jump in a fade's first half fades from what showed more: the first's start
    TIDE_CHECK(rows(seen->twice, 9, 9, 5, 6) && blend(seen->twice, 0, 0.2f, 0.15625f, 0));
    TIDE_CHECK(rows(seen->cut, 2, 3, 2, 3) && blend(seen->cut, 0, 0, 1, 0));
    tide_world_free(&world);
}
