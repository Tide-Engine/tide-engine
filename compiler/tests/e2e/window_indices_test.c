#include "game.h"
#include "tide_test.h"

static tide_world world, before;

static int32_t read_cell(const tide_world *w, const tide_grid cells, const tide_grid_shape *shape,
                         const int32_t x, const int32_t y, const int32_t z)
{
    const int32_t *value = tide_grid_read(&w->heap, cells, x, y, z, shape);
    return value ? *value : 0;
}

static void seed_cell(tide_grid *cells, const tide_grid_shape *shape, const int32_t x, const int32_t y,
                      const int32_t z, const int32_t value)
{
    tide_text_use(&world.heap, NULL);
    int32_t *out = tide_grid_poke(cells, x, y, z, shape, true, TIDE_IN_MATCH);
    *out = value;
}

TIDE_TEST(window_indices_finite_edges_and_partial_blocks)
{
    tide_world_init(&world, 1.0f);
    for (int32_t y = 0; y < 71; y++) {
        for (int32_t x = 0; x < 133; x++) seed_cell(&world.Plane.cells, &tide_shape_Grid2_int, x, y, 0, 1 + x + y * 137);
    }
    for (int32_t z = 0; z < 19; z++) {
        for (int32_t y = 0; y < 21; y++) {
            for (int32_t x = 0; x < 35; x++) {
                seed_cell(&world.Volume.cells, &tide_shape_Grid3_int, x, y, z, 1 + x + y * 37 + z * 809);
            }
        }
    }
    tide_world_copy(&before, &world);
    tide_world_tick(&world);
    for (int32_t y = 0; y < 71; y++) {
        for (int32_t x = 0; x < 133; x++) {
            int32_t expected = read_cell(&before, before.Plane.cells, &tide_shape_Grid2_int, x, y, 0);
            if (x >= 1 && y >= 1 && x % 3 == 1 && y % 2 == 1 && x + 2 < 133 && y + 1 < 71) {
                expected = read_cell(&before, before.Plane.cells, &tide_shape_Grid2_int, x - 1, y, 0)
                         + read_cell(&before, before.Plane.cells, &tide_shape_Grid2_int, x + 3, y + 2, 0) + x + y;
            } else if (x >= 3 && y >= 2 && x % 3 == 0 && y % 2 == 0) {
                expected = read_cell(&before, before.Plane.cells, &tide_shape_Grid2_int, x - 3, y - 2, 0) - (x - 2);
            }
            TIDE_CHECK(read_cell(&world, world.Plane.cells, &tide_shape_Grid2_int, x, y, 0) == expected);
        }
    }
    for (int32_t z = 0; z < 19; z++) {
        for (int32_t y = 0; y < 21; y++) {
            for (int32_t x = 0; x < 35; x++) {
                int32_t expected = read_cell(&before, before.Volume.cells, &tide_shape_Grid3_int, x, y, z);
                if (x % 2 == 1 && y % 3 == 2 && z % 2 == 1 && x + 1 < 35 && y + 2 < 21 && z + 1 < 19) {
                    expected = read_cell(&before, before.Volume.cells, &tide_shape_Grid3_int, x - 1, y - 1, z - 1)
                             + read_cell(&before, before.Volume.cells, &tide_shape_Grid3_int, x + 2, y + 3, z + 2)
                             + x + y * 10 + z * 100;
                } else if (x >= 2 && y >= 4 && z >= 2 && x % 2 == 0 && y % 3 == 1 && z % 2 == 0) {
                    expected = read_cell(&before, before.Volume.cells, &tide_shape_Grid3_int, x - 2, y - 2, z - 2) - (z - 1);
                }
                TIDE_CHECK(read_cell(&world, world.Volume.cells, &tide_shape_Grid3_int, x, y, z) == expected);
            }
        }
    }
    TIDE_CHECK(read_cell(&before, before.Plane.cells, &tide_shape_Grid2_int, 64, 65, 0) == 1 + 64 + 65 * 137);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&before);
    tide_world_free(&world);
}

TIDE_TEST(window_indices_open_negative_and_sparse_tiles)
{
    tide_world_init(&world, 1.0f);
    const int32_t positions[][2] = {{-65, -16}, {-63, -13}, {65, 68}, {-66, -17}, {-66, -16}, {64, 68}};
    for (uint32_t i = 0; i < sizeof positions / sizeof positions[0]; i++) {
        seed_cell(&world.Open.cells, &tide_shape_Grid2_int, positions[i][0], positions[i][1], 0, 100 + (int32_t)i);
    }
    tide_world_copy(&before, &world);
    tide_world_tick(&world);
    // Whole blocks, including ones starting before negative chunks and sparse
    // tiles: only a nonzero first cell writes, and the rest retain old values.
    for (int32_t y = -132; y <= 132; y++) {
        for (int32_t x = -132; x <= 132; x++) {
            int32_t expected = read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, x, y, 0);
            if ((x - 1) % 2 == 0 && (y - 2) % 3 == 0 && expected) {
                expected = read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, x - 1, y - 1, 0)
                         + read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, x + 2, y + 3, 0) + x + y;
            } else if ((x - 2) % 2 == 0 && (y - 4) % 3 == 0
                       && read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, x - 1, y - 2, 0)) {
                expected = read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, x - 2, y - 2, 0) - (x - 1);
            }
            TIDE_CHECK(read_cell(&world, world.Open.cells, &tide_shape_Grid2_int, x, y, 0) == expected);
        }
    }
    TIDE_CHECK(read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, -65, -16, 0) == 100);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&before);
    tide_world_free(&world);
}

TIDE_TEST(window_indices_large_origins_keep_wrapping_position_arithmetic)
{
    tide_world_init(&world, 1.0f);
    // These are block starts near both int limits, with room for their halos.
    // Adding x and y in the step wraps; addressing its window must stay local.
    const int32_t high_x = 2147483519, high_y = 2147483522;
    const int32_t low_x = -2147483519, low_y = -2147483521;
    seed_cell(&world.Open.cells, &tide_shape_Grid2_int, high_x, high_y, 0, 7);
    seed_cell(&world.Open.cells, &tide_shape_Grid2_int, low_x, low_y, 0, 9);
    tide_world_copy(&before, &world);
    tide_world_tick(&world);
    TIDE_CHECK(read_cell(&world, world.Open.cells, &tide_shape_Grid2_int, high_x, high_y, 0) == -255);
    TIDE_CHECK(read_cell(&world, world.Open.cells, &tide_shape_Grid2_int, low_x, low_y, 0) == 256);
    TIDE_CHECK(read_cell(&world, world.Open.cells, &tide_shape_Grid2_int, high_x + 1, high_y + 2, 0) == -high_x);
    TIDE_CHECK(read_cell(&world, world.Open.cells, &tide_shape_Grid2_int, low_x + 1, low_y + 2, 0) == -low_x);
    TIDE_CHECK(read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, high_x, high_y, 0) == 7);
    TIDE_CHECK(read_cell(&before, before.Open.cells, &tide_shape_Grid2_int, low_x, low_y, 0) == 9);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&before);
    tide_world_free(&world);
}
