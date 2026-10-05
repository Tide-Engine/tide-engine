#include "game.h"
#include "tide_test.h"

static tide_world world;

// C keeps its counter between ticks, so the world ticks once, whichever test runs first.
static const Log *ticked(void)
{
    static bool done;
    if (!done) {
        tide_world_init(&world, 1.0f);
        tide_world_tick(&world);
        done = true;
    }
    return &world.Log;
}

TIDE_TEST(overloads_extern_c_is_passed_defaults)
{
    const Log *log = ticked();
    TIDE_CHECK(log->scaled == 6.0f);     // by = 2
    TIDE_CHECK(log->scaled_by == 15.0f);
    TIDE_CHECK(log->total == 3.5f);      // A struct's default, by address
    TIDE_CHECK(log->given == 11.0f);
}

TIDE_TEST(overloads_extern_names_pick_parameters)
{
    const Log *log = ticked();
    TIDE_CHECK(log->named == 8.0f); // c_scale(by: 4, value: 2)
    TIDE_CHECK(log->digits == 21);  // ones is counted first, as written: c_digits(tens: 2, ones: 1)
}

TIDE_TEST(overloads_extern_each_names_its_c_function)
{
    const Log *log = ticked();
    TIDE_CHECK(log->floats == 3.5f); // c_mix_floats adds
    TIDE_CHECK(log->ints == 12);     // c_mix_ints multiplies
}
