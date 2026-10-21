// Profiles (tide/profile.h): what ticks and frames say they spent, and the
// profiler's panel (tide/profiler.h), which shows it. Times here are made up,
// never measured: a test can't rest on how fast the machine is.

#include <string.h>

#include "tide/jobs.h"
#include "tide/profile.h"
#include "tide/profiler.h"
#include "tide_test.h"
#include "draw_test.h"

#define MS UINT64_C(1000000)

TIDE_TEST(profile_times_add_up_until_cleared)
{
    tide_profile p = {0};
    tide_profile_count(&p.systems, 2);
    TIDE_REQUIRE(p.systems.count == 2);
    tide_profile_add(&p.systems, 0, "Move", 10 * MS, 12 * MS);
    tide_profile_add(&p.systems, 1, "Combat.Hit", 12 * MS, 13 * MS);
    tide_profile_add(&p.systems, 0, "Move", 20 * MS, 23 * MS);
    TIDE_CHECK(strcmp(p.systems.items[0].name, "Move") == 0 && strcmp(p.systems.items[1].name, "Combat.Hit") == 0);
    TIDE_CHECK(p.systems.items[0].ms == 3.0 && p.systems.items[0].total == 5.0);
    TIDE_CHECK(p.systems.items[0].measured_ms == 5.0 && p.systems.items[0].measurements == 2);
    TIDE_CHECK(p.systems.items[1].ms == 1.0 && p.systems.items[1].total == 1.0);

    tide_profile_add(&p.systems, 1, "Combat.Hit", 5 * MS, 4 * MS); // A clock that went back is no time
    TIDE_CHECK(p.systems.items[1].ms == 0.0 && p.systems.items[1].total == 1.0);
    TIDE_CHECK(p.systems.items[1].measured_ms == 1.0 && p.systems.items[1].measurements == 2);
    tide_profile_add(&p.systems, 7, "Nowhere", 0, MS);
    TIDE_CHECK(p.systems.count == 2);

    tide_profile_clear(&p);
    TIDE_CHECK(p.systems.items[0].total == 0.0 && p.systems.items[0].ms == 3.0);
    TIDE_CHECK(p.systems.items[0].measured_ms == 5.0 && p.systems.items[0].measurements == 2);
    TIDE_CHECK(strcmp(p.systems.items[0].name, "Move") == 0);
    tide_profile_free(&p);
    TIDE_CHECK(!p.systems.items && p.systems.count == 0);
}

TIDE_TEST(profile_starts_over_for_another_build)
{
    tide_profile p = {0};
    tide_profile_count(&p.systems, 2);
    tide_profile_add(&p.systems, 0, "Move", 0, 2 * MS);
    tide_profile_add(&p.systems, 1, "Hit", 0, 2 * MS);
    tide_profile_add(&p.systems, 0, "Fall", 0, MS);
    TIDE_CHECK(strcmp(p.systems.items[0].name, "Fall") == 0 && p.systems.items[0].total == 1.0);
    TIDE_CHECK(p.systems.items[0].measured_ms == 1.0 && p.systems.items[0].measurements == 1);
    TIDE_CHECK(p.systems.items[1].total == 2.0);
    tide_profile_count(&p.systems, 3);
    TIDE_CHECK(p.systems.count == 3 && p.systems.items[1].total == 0.0 && p.systems.items[1].name[0] == '\0');
    TIDE_CHECK(p.systems.items[1].measured_ms == 0.0 && p.systems.items[1].measurements == 0);

    const char *name = "A.Name.That.Goes.On.For.Longer.Than.A.Profile.Keeps.Names";
    tide_profile_add(&p.systems, 2, name, 0, MS);
    tide_profile_add(&p.systems, 2, name, 0, MS);
    TIDE_CHECK(strlen(p.systems.items[2].name) == TIDE_PROFILE_NAME - 1 && p.systems.items[2].total == 2.0);
    tide_profile_free(&p);
}

// ---------------------------------------------------------------------------
// A tick's systems fill the profile its jobs have, on one thread too

static uint32_t ran[3];

static uint32_t one_task(const void *world, uint64_t *work)
{
    (void)world;
    *work = 1;
    return 1;
}

#define RUNS(n)                                                                  \
    static void run_##n(void *world, const uint32_t task, const uint32_t pieces) \
    {                                                                            \
        (void)world;                                                             \
        (void)task;                                                              \
        (void)pieces;                                                            \
        ran[n]++;                                                                \
    }
RUNS(0)
RUNS(1)
RUNS(2)

TIDE_TEST(profile_ticks_name_their_systems)
{
    const tide_system_tasks systems[3] = {
        {.count = one_task, .run = run_0, .name = "Fall"},
        {.count = one_task, .run = run_1, .name = "Combat.Hit"},
        {.count = one_task, .run = run_2, .name = "Land"},
    };
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 3, NULL, &queue, &entities, NULL};
    tide_profile p = {0};
    const tide_jobs alone = {.threads = 1, .profile = &p};
    memset(ran, 0, sizeof ran);
    tide_run_systems(&queue, &tick, &alone);
    tide_run_systems(&queue, &tick, &alone);
    TIDE_CHECK(ran[0] == 2 && ran[1] == 2 && ran[2] == 2);
    TIDE_REQUIRE(p.systems.count == 3);
    TIDE_CHECK(strcmp(p.systems.items[0].name, "Fall") == 0 && strcmp(p.systems.items[1].name, "Combat.Hit") == 0
               && strcmp(p.systems.items[2].name, "Land") == 0);
    for (int i = 0; i < 3; i++) TIDE_CHECK(p.systems.items[i].total >= p.systems.items[i].ms && p.systems.items[i].ms >= 0.0);

    const tide_jobs plain = {.threads = 1};
    tide_profile_clear(&p);
    tide_run_systems(&queue, &tick, &plain);
    tide_run_systems(&queue, &tick, NULL);
    TIDE_CHECK(ran[0] == 4 && p.systems.items[0].total == 0.0);
    tide_profile_free(&p);
}

// ---------------------------------------------------------------------------
// The profiler's panel

static const tide_profiler_row *row_named(const tide_profiler *p, const int column, const char *label)
{
    for (uint32_t i = 0; i < p->row_count[column]; i++) {
        if (strcmp(p->rows[column][i].label, label) == 0) return &p->rows[column][i];
    }
    return NULL;
}

static bool row_is(const tide_profiler *p, const int column, const char *label, const char *value)
{
    const tide_profiler_row *r = row_named(p, column, label);
    return r && strcmp(r->value, value) == 0;
}

static void measured(tide_profiler *p, const uint32_t systems)
{
    tide_profiler_reset(p);
    p->span = 0.6;
    p->frames = 30;
    p->longest = 0.031;
    p->part_ms[TIDE_FRAME_INPUT] = 3.0;
    p->part_ms[TIDE_FRAME_MATCH] = 60.0;
    p->part_ms[TIDE_FRAME_VIEWS] = 99.0;
    p->part_ms[TIDE_FRAME_RENDER] = 30.0;
    tide_profile *f = &p->profile;
    f->ticks = 60;
    f->systems_ms = 36.0;
    f->changes_ms = 9.0;
    tide_profile_count(&f->systems, systems);
    for (uint32_t i = 0; i < systems; i++) {
        char name[16] = "System";
        name[6] = (char)('A' + i);
        tide_profile_add(&f->systems, i, name, 0, (i + 1u) * MS);
    }
    tide_profile_count(&f->views, 3);
    tide_profile_add(&f->views, 0, "Hud", 0, 30 * MS);
    tide_profile_add(&f->views, 1, "World", 0, 60 * MS);
    tide_profile_add(&f->views, 2, "Paused", 0, 0);
    p->again_from = 100;
    p->again_known = true;
    p->mark_ns = tide_time_now_ns();
}

TIDE_TEST(profiler_says_where_a_frame_and_a_tick_went)
{
    static tide_profiler p;
    measured(&p, 12);
    p.memory = 48u * 1024u * 1024u;
    p.gpu_memory = 12u * 1024u * 1024u;
    tide_profiler_end(&p, 130);
    enum { FRAME, TICK };

    const tide_profiler_row *frame = row_named(&p, FRAME, "Frame");
    TIDE_REQUIRE(frame && frame == &p.rows[FRAME][0]);
    TIDE_CHECK(strcmp(frame->value, "20.00 ms") == 0 && strcmp(frame->note, "50 fps, longest 31.0 ms") == 0);
    TIDE_CHECK(frame->depth == 0 && frame->share == 1.0f);
    TIDE_CHECK(row_is(&p, FRAME, "Input", "0.10") && row_is(&p, FRAME, "Match", "2.00"));
    const tide_profiler_row *ticks = row_named(&p, FRAME, "Ticks");
    TIDE_REQUIRE(ticks);
    TIDE_CHECK(strcmp(ticks->value, "1.50") == 0 && strcmp(ticks->note, "2.0 a frame, 1.0 again") == 0);
    TIDE_CHECK(row_is(&p, FRAME, "Netcode", "0.50"));
    TIDE_CHECK(row_is(&p, FRAME, "Views", "3.30") && row_is(&p, FRAME, "World", "2.00") && row_is(&p, FRAME, "Hud", "1.00"));
    TIDE_CHECK(row_named(&p, FRAME, "World") < row_named(&p, FRAME, "Hud"));
    TIDE_CHECK(!row_named(&p, FRAME, "Paused") && !row_named(&p, FRAME, "1 more"));
    TIDE_CHECK(row_is(&p, FRAME, "GUI, tasks", "0.30") && row_is(&p, FRAME, "Render", "1.00"));
    const tide_profiler_row *match = row_named(&p, FRAME, "Match");
    TIDE_CHECK(match->depth == 1 && match->share > 0.099f && match->share < 0.101f && ticks->depth == 2);
    TIDE_CHECK(row_named(&p, FRAME, "Other") && row_named(&p, FRAME, "Swap, idle"));
    const tide_profiler_row *memory = row_named(&p, FRAME, "Memory");
    TIDE_REQUIRE(memory && memory == &p.rows[FRAME][p.row_count[FRAME] - 1u]);
    TIDE_CHECK(strcmp(memory->note, "48 MB in RAM, 12 MB on the GPU") == 0 && !memory->value[0] && memory->share == 0.0f && memory->depth == 0);

    const tide_profiler_row *tick = row_named(&p, TICK, "Tick");
    TIDE_REQUIRE(tick && tick == &p.rows[TICK][0]);
    TIDE_CHECK(strcmp(tick->value, "0.75 ms") == 0 && strcmp(tick->note, "100 a second") == 0);
    TIDE_CHECK(row_is(&p, TICK, "Systems", "0.60") && row_is(&p, TICK, "Changes, events", "0.15"));
    TIDE_CHECK(strcmp(p.rows[TICK][2].label, "SystemL") == 0 && strcmp(p.rows[TICK][2].value, "0.20") == 0);
    TIDE_CHECK(strcmp(p.rows[TICK][11].label, "SystemC") == 0 && row_is(&p, TICK, "2 more", "0.05"));
    TIDE_CHECK(!row_named(&p, TICK, "SystemA") && p.row_count[TICK] == 14);

    TIDE_CHECK(p.frames == 0 && p.span == 0.0 && p.profile.ticks == 0 && p.profile.systems.items[11].total == 0.0);
    TIDE_CHECK(p.again_from == 130);
    tide_profiler_free(&p);
}

TIDE_TEST(profiler_waits_to_take_stock)
{
    static tide_profiler p;
    tide_profiler_reset(&p);
    TIDE_REQUIRE(p.row_count[0] == 1);
    TIDE_CHECK(strcmp(p.rows[0][0].note, "measuring") == 0);
    for (int i = 0; i < 10; i++) {
        tide_profiler_begin(&p, 0.016f);
        tide_profiler_mark(&p, TIDE_FRAME_INPUT);
        tide_profiler_end(&p, 0);
    }
    TIDE_CHECK(p.frames == 10 && strcmp(p.rows[0][0].note, "measuring") == 0);
    for (int i = 0; i < 30; i++) {
        tide_profiler_begin(&p, 0.016f);
        tide_profiler_end(&p, 0);
    }
    TIDE_CHECK(strcmp(p.rows[0][0].value, "16.00 ms") == 0 && p.frames < 30);
    TIDE_CHECK(!row_named(&p, 0, "Ticks") && p.row_count[1] == 1 && strcmp(p.rows[1][0].note, "none ran") == 0);

    const uint32_t frames = p.frames;
    tide_profiler_end(&p, 0);
    tide_profiler_mark(&p, TIDE_FRAME_RENDER);
    TIDE_CHECK(p.frames == frames && p.part_ms[TIDE_FRAME_RENDER] == 0.0);
    tide_profiler_begin(NULL, 0.016f);
    tide_profiler_mark(NULL, TIDE_FRAME_INPUT);
    tide_profiler_end(NULL, 0);
    tide_profiler_reset(NULL);
    tide_profiler_free(&p);
}

// ---------------------------------------------------------------------------
// ...as it's drawn

static tide_draw_list list;

static const tide_draw_command *text_command(const char *text)
{
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(list.text + c->text, text) == 0) return c;
    }
    return NULL;
}

static float measure(const char *text, const float size)
{
    (void)size;
    return (float)strlen(text) * 7.0f;
}

static tide_float2 font(const char *text, const float size, const tide_target_size *target, const unsigned char *weight)
{
    (void)target;
    (void)weight;
    return tide_f2(measure(text, size), size);
}

TIDE_TEST(profiler_draws_its_columns_where_they_fit)
{
    static tide_profiler p;
    measured(&p, 3);
    tide_profile_add(&p.profile.systems, 0, "Combat.Projectiles.ResolveEveryCollisionThereIs", 0, 900 * MS);
    tide_profiler_end(&p, 100);

    tide_draw_reset(&list);
    tide_draw_set_clip(&list, (tide_rect){0, 0, 10, 10});
    tide_draw_set_tint(&list, TIDE_COLOR_RED);
    tide_draw_rect(&list, tide_f2(5, 5), tide_f2(2, 2), TIDE_COLOR_WHITE);
    tide_profiler_draw(&p, &list, tide_f2(960, 540), font);
    tide_test_flatten(&list);
    TIDE_REQUIRE(list.count > 5);
    TIDE_CHECK(list.commands[0].kind == TIDE_DRAW_CLIP && list.commands[1].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(list.commands[2].kind == TIDE_DRAW_SCREEN && list.commands[3].kind == TIDE_DRAW_NO_CLIP);
    TIDE_CHECK(list.commands[4].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(list.commands[4].a.x - list.commands[4].b.x * 0.5f == 12.0f);
    TIDE_CHECK(list.commands[4].color.b > 0.078f && list.commands[4].color.b < 0.082f && !list.place.tinted);
    const tide_draw_command *frame = text_command("Frame");
    const tide_draw_command *tick = text_command("Tick");
    TIDE_REQUIRE(frame && tick);
    TIDE_CHECK(tick->a.y == frame->a.y && tick->a.x > frame->a.x + 300.0f);
    const tide_draw_command *value = text_command("20.00 ms");
    const tide_draw_command *input = text_command("0.10");
    TIDE_REQUIRE(value && input);
    TIDE_CHECK(value->a.x + measure("20.00 ms", 13) == input->a.x + measure("0.10", 13));
    TIDE_CHECK(!text_command("Combat.Projectiles.ResolveEveryCollisionThereIs"));
    bool cut = false;
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind != TIDE_DRAW_TEXT || strncmp(list.text + c->text, "Combat.Proj", 11) != 0) continue;
        cut = true;
        const float value_left = tick->a.x - 8.0f + 340.0f - 8.0f - measure("15.00", 13);
        TIDE_CHECK(c->a.x + measure(list.text + c->text, 13) < value_left);
    }
    TIDE_CHECK(cut);
    TIDE_CHECK(text_command("50 fps, longest 31.0 ms") && text_command("World") && text_command("SystemC"));

    tide_draw_reset(&list);
    tide_profiler_draw(&p, &list, tide_f2(300, 800), font);
    tide_test_flatten(&list);
    frame = text_command("Frame");
    tick = text_command("Tick");
    TIDE_REQUIRE(frame && tick);
    TIDE_CHECK(tick->a.x == frame->a.x && tick->a.y > frame->a.y + 18.0f * 10.0f);
    TIDE_CHECK(list.commands[1].kind == TIDE_DRAW_RECT && list.commands[1].b.x <= 300.0f - 24.0f);

    tide_draw_reset(&list);
    tide_profiler_draw(NULL, &list, tide_f2(960, 540), font);
    tide_test_flatten(&list);
    static tide_profiler empty;
    tide_profiler_draw(&empty, &list, tide_f2(960, 540), NULL);
    tide_test_flatten(&list);
    TIDE_CHECK(list.count == 0);
    tide_profiler_reset(&empty);
    tide_profiler_draw(&empty, &list, tide_f2(960, 540), NULL);
    tide_test_flatten(&list);
    TIDE_CHECK(text_command("measuring"));
    tide_draw_free(&list);
    tide_profiler_free(&p);
}

TIDE_TEST(profiler_keeps_its_look_whatever_the_guis_theme)
{
    static tide_profiler p;
    static tide_gui gui;
    static tide_draw_list themed;
    measured(&p, 3);
    tide_profiler_end(&p, 100);
    const tide_devices devices = {0};
    uint32_t first[2];
    for (int pass = 0; pass < 2; pass++) {
        tide_draw_list *to = pass == 0 ? &list : &themed;
        tide_draw_reset(to);
        tide_gui_begin(&gui, &devices, tide_f2(960, 540), font);
        if (pass == 1) {
            tide_gui_theme loud = tide_gui_theme_default();
            loud.text = loud.panel = loud.control = (tide_color){1.0f, 0.0f, 1.0f, 1.0f};
            loud.radius = 9.0f;
            tide_gui_set_theme(&gui, loud);
        }
        tide_gui_button(&gui, 1, (tide_rect){500, 400, 100, 30}, "Play");
        tide_gui_end(&gui, to);
        tide_test_flatten(to);
        first[pass] = to->count;
        tide_profiler_draw(&p, to, tide_f2(960, 540), font);
        tide_test_flatten(to);
    }
    TIDE_REQUIRE(first[0] == first[1] && list.count == themed.count && list.count > first[0] + 4);
    TIDE_CHECK(themed.commands[1].kind == TIDE_DRAW_SHAPE && list.commands[1].kind == TIDE_DRAW_RECT);
    for (uint32_t i = first[0]; i < list.count; i++) {
        const tide_draw_command *a = &list.commands[i];
        const tide_draw_command *b = &themed.commands[i];
        TIDE_CHECK(a->kind == b->kind && a->kind != TIDE_DRAW_SHAPE);
        TIDE_CHECK(a->color.r == b->color.r && a->color.g == b->color.g && a->color.b == b->color.b && a->color.a == b->color.a);
        if (a->kind == TIDE_DRAW_TEXT) TIDE_CHECK(strcmp(list.text + a->text, themed.text + b->text) == 0);
    }
    tide_draw_free(&list);
    tide_draw_free(&themed);
    tide_profiler_free(&p);
}
