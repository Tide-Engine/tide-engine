#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static tide_player_id player(int side)
{
    return tide_player_from_index(side);
}

static void start(void)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    world.Duel.phase = Phase_Fight;
    const tide_entity fighters[] = {world.Duel.blue, world.Duel.orange};
    for (int i = 0; i < 2; i++) {
        Fighter f = tide_get_Fighter(&world, fighters[i]);
        f.bot = false;
        f.health = f.build.maxHealth;
        f.fireCooldown = 100;
        f.blockLeft = 0;
        f.round = world.Duel.round;
        f.match = world.Duel.match;
        tide_set_Fighter(&world, fighters[i], f);
        tide_world_set_input(&world, player(i), (Controls){.aim = {1, 0}});
    }
}

static void place(tide_entity entity, float x, float y)
{
    Body body = tide_get_Body(&world, entity);
    body.position = tide_f2(x, y);
    body.previous = body.position;
    body.velocity = tide_f2(0, 0);
    tide_set_Body(&world, entity, body);
}

static tide_entity shot_from(tide_entity shooter)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Bullet(&world, entity)
            && tide_entity_equal(tide_get_Bullet(&world, entity).shooter, shooter)) return entity;
    }
    return (tide_entity){0};
}

static tide_entity fire(void)
{
    Fighter fighter = tide_get_Fighter(&world, world.Duel.blue);
    fighter.fireCooldown = 0;
    tide_set_Fighter(&world, world.Duel.blue, fighter);
    tide_world_set_input(&world, player(0), (Controls){.aim = {1, 0}, .fire = true});
    tide_world_tick(&world);
    tide_world_set_input(&world, player(0), (Controls){.aim = {1, 0}});
    return shot_from(world.Duel.blue);
}

static void aim_shot(tide_entity shot, float x, float y, float vx, float vy, int bounces)
{
    place(shot, x, y);
    Body body = tide_get_Body(&world, shot);
    body.velocity = tide_f2(vx, vy);
    tide_set_Body(&world, shot, body);
    Bullet bullet = tide_get_Bullet(&world, shot);
    bullet.damage = 34;
    bullet.knockback = 0;
    bullet.bounces = bounces;
    bullet.gravity = 0; // These shots fly straight, so the tests' geometry is exact.
    tide_set_Bullet(&world, shot, bullet);
}

static bool choose(tide_entity target, int sender, int round, int slot)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_ChooseCard, 16);
    tide_bits_put(&bits, target.index, 32);
    tide_bits_put(&bits, target.generation, 32);
    tide_bits_put_varint(&bits, tide_zigzag(round));
    tide_bits_put_varint(&bits, tide_zigzag(slot));
    const uint32_t size = tide_bits_end(&bits);
    return tide_game_api.command(&world, player(sender),
        tide_game_api.command_sequence(&world, player(sender)) + 1, bytes, size);
}

// A command that names the match it was sent in, and no entity.
static bool match_command(uint32_t id, int sender, int match)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(match));
    const uint32_t size = tide_bits_end(&bits);
    return tide_game_api.command(&world, player(sender),
        tide_game_api.command_sequence(&world, player(sender)) + 1, bytes, size);
}

static float rendered_blue_x(tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    const tide_devices devices = {0};
    tide_draw_reset(draw);
    tide_gui_begin(gui, &devices, tide_f2(960, 540), NULL);
    tide_frame(&world, NULL, 1, &world, local, draw, gui);
    tide_gui_end(gui, draw);
    for (uint32_t i = 0; i < draw->count; i++) {
        const tide_draw_command *command = &draw->commands[i];
        if (command->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *run = &draw->runs[command->shaded.run];
        // The SDF art sends Shape's six float4 fields to the renderer. Find
        // blue's outlined body ellipse in that recorded instance data, so
        // this checks what draws even while its arrival and squash animate.
        if (strcmp(run->shader->name, "PlaceShape and PaintShape on Corner") != 0
            || tide_shader_instance_size(run->shader) != sizeof(Shape)) continue;
        for (uint32_t instance = 0; instance < run->count; instance++) {
            Shape shape;
            memcpy(&shape, draw->instances + run->first + instance * sizeof shape, sizeof shape);
            if (shape.form.z < 0 && shape.line.x == 2.5f
                && fabsf(shape.fill.r - 0.4598f) < 0.0001f && shape.fill.b == 1
                && shape.edge.r == 0.02f && shape.edge.g == 0.028f && shape.edge.b == 0.065f)
                return shape.box.x;
        }
    }
    return NAN;
}

TIDE_TEST(duel_client_corrections_preserve_display_and_follow_normal_movement)
{
    start();
    place(world.Duel.blue, -200, 0);
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_local_init(&local);
    TIDE_CHECK(rendered_blue_x(&local, &draw, &gui) == -200);

    tide_world before = {0};
    tide_world_copy(&before, &world);
    place(world.Duel.blue, -180, 0);
    tide_view_correction correction = {
        .before = &before, .after = &world, .tick = (uint32_t)world.Time.tick, .alpha = 1,
    };
    const uint64_t hash = tide_world_hash(&world);
    tide_local_correct(&local, &correction);
    TIDE_CHECK(rendered_blue_x(&local, &draw, &gui) == -200);
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).position.x == -180);
    TIDE_CHECK(tide_world_hash(&world) == hash);

    tide_local_frame_time(&local, 0.075f);
    TIDE_CHECK(fabsf(rendered_blue_x(&local, &draw, &gui) - -190) < 0.001f);

    // A second correction preserves what the player sees, including the
    // remaining first error. Then normal motion advances by its full amount.
    tide_world_copy(&before, &world);
    place(world.Duel.blue, -160, 0);
    tide_local_correct(&local, &correction);
    TIDE_CHECK(fabsf(rendered_blue_x(&local, &draw, &gui) - -190) < 0.001f);
    place(world.Duel.blue, -130, 0);
    TIDE_CHECK(fabsf(rendered_blue_x(&local, &draw, &gui) - -160) < 0.001f);

    tide_world_free(&before);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

TIDE_TEST(duel_round_respawn_clears_visual_correction)
{
    start();
    place(world.Duel.blue, -200, 0);
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_local_init(&local);
    TIDE_CHECK(rendered_blue_x(&local, &draw, &gui) == -200);

    tide_world before = {0};
    tide_world_copy(&before, &world);
    place(world.Duel.blue, -150, 0);
    const tide_view_correction correction = {
        .before = &before, .after = &world, .tick = (uint32_t)world.Time.tick, .alpha = 1,
    };
    tide_local_correct(&local, &correction);
    TIDE_CHECK(rendered_blue_x(&local, &draw, &gui) == -200);

    world.Duel.round++;
    world.Duel.phase = Phase_Countdown;
    world.Duel.timer = 1.5f;
    tide_world_tick(&world);
    TIDE_CHECK(rendered_blue_x(&local, &draw, &gui) == -360);

    tide_world_free(&before);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

static bool rematch(int sender, int match)
{
    return match_command(TIDE_COMMAND_ID_Rematch, sender, match);
}

static bool practice(int sender, int match)
{
    return match_command(TIDE_COMMAND_ID_Practice, sender, match);
}

static bool to_menu(int sender, int match)
{
    return match_command(TIDE_COMMAND_ID_ToMenu, sender, match);
}

static int shots(void)
{
    int count = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        if (tide_has_Bullet(&world, tide_entity_in_slot(&world.entities, i))) count++;
    }
    return count;
}

TIDE_TEST(duel_fast_shot_hits_thin_wall_before_fighter)
{
    start();
    const tide_entity shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    place(world.Duel.blue, -300, 180);
    place(world.Duel.orange, 100, 180);
    // The whole wall can be crossed in one tick, with a fighter behind it.
    tide_entity wall = {0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Platform(&world, entity)) continue;
        Platform platform = tide_get_Platform(&world, entity);
        platform.center = tide_f2(0, -5000);
        tide_set_Platform(&world, entity, platform);
        wall = entity;
    }
    TIDE_REQUIRE(tide_has_Platform(&world, wall));
    tide_set_Platform(&world, wall, (Platform){.center = {0, 180}, .half = {5, 90}});
    aim_shot(shot, -100, 180, 18000, 0, 0);
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Bullet(&world, shot));
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).health == 100);
    tide_world_free(&world);
}

TIDE_TEST(duel_shot_selects_closest_fighter_over_query_order)
{
    start();
    const tide_entity shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    // Blue was spawned first, but orange is the first geometric contact.
    place(world.Duel.blue, 120, 180);
    place(world.Duel.orange, 0, 180);
    aim_shot(shot, -100, 180, 18000, 0, 0);
    Bullet bullet = tide_get_Bullet(&world, shot);
    bullet.shooter = (tide_entity){0}; // A neutral shot may hit either fighter.
    tide_set_Bullet(&world, shot, bullet);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).health == 66);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 100);
    TIDE_CHECK(!tide_has_Bullet(&world, shot));
    tide_world_free(&world);
}

TIDE_TEST(duel_block_returns_shot_to_original_shooter)
{
    start();
    const tide_entity shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    place(world.Duel.blue, -220, 180);
    place(world.Duel.orange, 0, 180);
    Fighter orange = tide_get_Fighter(&world, world.Duel.orange);
    orange.blockLeft = 0.2f;
    tide_set_Fighter(&world, world.Duel.orange, orange);
    aim_shot(shot, -70, 180, 4800, 0, 2);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    const Bullet returned = tide_get_Bullet(&world, shot);
    TIDE_CHECK(tide_entity_equal(returned.shooter, world.Duel.orange));
    TIDE_CHECK(returned.side == 1);
    TIDE_CHECK(returned.bounces == 2); // A block does not consume a wall bounce.
    TIDE_CHECK(tide_get_Body(&world, shot).velocity.x < 0);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).health == 100);
    aim_shot(shot, -80, 180, -18000, 0, returned.bounces);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 66);
    TIDE_CHECK(!tide_has_Bullet(&world, shot));
    tide_world_free(&world);
}

TIDE_TEST(duel_ricochet_consumes_one_bounce_then_stops_at_next_wall)
{
    start();
    const tide_entity shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    place(world.Duel.blue, -200, 180);
    place(world.Duel.orange, 200, 180);
    aim_shot(shot, 430, 180, 6000, 0, 1);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    TIDE_CHECK(tide_get_Bullet(&world, shot).bounces == 0);
    TIDE_CHECK(tide_get_Body(&world, shot).velocity.x < 0);
    aim_shot(shot, -430, 180, -6000, 0, 0);
    tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Bullet(&world, shot));
    tide_world_free(&world);
}

TIDE_TEST(duel_ground_contact_allows_jumping_away)
{
    start();
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).grounded);
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).velocity.y == 0);
    tide_world_set_input(&world, player(0), (Controls){.aim = {1, 0}, .jump = true});
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).position.y > -190);
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).velocity.y > 0);
    TIDE_CHECK(!tide_get_Fighter(&world, world.Duel.blue).grounded);
    tide_world_free(&world);
}

TIDE_TEST(duel_practice_bot_finishes_round_against_idle_player)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_set_input(&world, player(0), (Controls){0});
    tide_world_tick(&world);
    TIDE_REQUIRE(practice(0, world.Duel.match));
    // Run ordinary practice play in the real arena, with blue providing no
    // input, including the normal countdown and the bot's firing cooldown.
    for (int tick = 0; tick < 1200 && world.Duel.phase != Phase_Draft; tick++)
        tide_world_tick(&world);
    TIDE_CHECK(!world.Duel.attract);
    TIDE_CHECK(world.Duel.phase == Phase_Draft);
    TIDE_CHECK(world.Duel.draftSide == 0);
    TIDE_CHECK(world.Duel.scoreOrange == 1 && world.Duel.scoreBlue == 0);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 0);
    tide_world_free(&world);
}

TIDE_TEST(duel_starts_at_the_menu_where_the_fighters_duel_by_themselves)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_set_input(&world, player(0), (Controls){0});
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.attract);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown);
    // The player is at the menu: blue shoots with no input of theirs, and
    // the two never mirror each other into a draw that goes on forever.
    bool blueShot = false;
    for (int tick = 0; tick < 3000 && world.Duel.scoreBlue + world.Duel.scoreOrange == 0; tick++) {
        tide_world_tick(&world);
        blueShot = blueShot || tide_has_Bullet(&world, shot_from(world.Duel.blue));
    }
    TIDE_CHECK(blueShot);
    TIDE_CHECK(world.Duel.scoreBlue + world.Duel.scoreOrange == 1);
    // The loser's upgrade is picked for it, and a player's pick is refused.
    TIDE_REQUIRE(world.Duel.phase == Phase_Draft);
    const int round = world.Duel.round;
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round, 0));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.round == round);
    for (int tick = 0; tick < 120 && world.Duel.phase == Phase_Draft; tick++) tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown && world.Duel.round == round + 1);

    // Picking practice starts a match the player plays: blue is theirs again.
    const int match = world.Duel.match;
    TIDE_REQUIRE(practice(0, match - 1)); // A click from an earlier menu.
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.attract && world.Duel.match == match);
    TIDE_REQUIRE(practice(0, match));
    tide_world_tick(&world);
    TIDE_CHECK(!world.Duel.attract);
    TIDE_CHECK(world.Duel.match == match + 1);
    TIDE_CHECK(world.Duel.scoreBlue == 0 && world.Duel.scoreOrange == 0);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown);
    for (int tick = 0; tick < 150 && world.Duel.phase != Phase_Fight; tick++) tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Fight);
    // Before the bot's first shot can reach it, blue has neither fired nor moved.
    for (int tick = 0; tick < 10; tick++) tide_world_tick(&world);
    TIDE_CHECK(!tide_has_Bullet(&world, shot_from(world.Duel.blue)));
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).position.x == -360);
    tide_world_free(&world);
}

TIDE_TEST(duel_second_player_starts_the_match_and_leaving_returns_to_the_menu)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.attract);
    const int match = world.Duel.match;
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(!world.Duel.attract);
    TIDE_CHECK(world.Duel.match == match + 1 && world.Duel.phase == Phase_Countdown);
    TIDE_CHECK(!tide_get_Fighter(&world, world.Duel.orange).bot);

    // Two players have no menu to go back to together.
    TIDE_REQUIRE(to_menu(0, world.Duel.match));
    tide_world_tick(&world);
    TIDE_CHECK(!world.Duel.attract && world.Duel.match == match + 1);

    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.attract);
    TIDE_CHECK(world.Duel.match == match + 2);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).bot);
    tide_world_free(&world);
}

TIDE_TEST(duel_practice_ends_at_the_menu_when_its_player_says_so)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    TIDE_REQUIRE(to_menu(0, world.Duel.match)); // Already there.
    tide_world_tick(&world);
    const int match = world.Duel.match;
    TIDE_REQUIRE(practice(0, match));
    tide_world_tick(&world);
    TIDE_REQUIRE(!world.Duel.attract);
    TIDE_REQUIRE(to_menu(1, world.Duel.match)); // Nobody's player.
    tide_world_tick(&world);
    TIDE_CHECK(!world.Duel.attract);
    TIDE_REQUIRE(to_menu(0, world.Duel.match));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.attract);
    TIDE_CHECK(world.Duel.match == match + 2);
    tide_world_free(&world);
}

TIDE_TEST(duel_menu_duel_starts_over_after_a_finished_match)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_tick(&world);
    const int match = world.Duel.match;
    world.Duel.phase = Phase_Finished;
    world.Duel.scoreOrange = 5;
    world.Duel.timer = 0.1f;
    TIDE_REQUIRE(rematch(0, match)); // Nobody at the menu asked for one.
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.match == match && world.Duel.phase == Phase_Finished);
    for (int tick = 0; tick < 10; tick++) tide_world_tick(&world);
    TIDE_CHECK(world.Duel.match == match + 1);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown && world.Duel.scoreOrange == 0);

    // A match that players finished waits for them.
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    const int played = world.Duel.match;
    world.Duel.phase = Phase_Finished;
    world.Duel.scoreBlue = 5;
    world.Duel.timer = 0.1f;
    for (int tick = 0; tick < 30; tick++) tide_world_tick(&world);
    TIDE_CHECK(world.Duel.match == played && world.Duel.phase == Phase_Finished);
    tide_world_free(&world);
}

TIDE_TEST(duel_shots_drop_as_fast_as_their_fighters_build_says)
{
    start();
    tide_entity shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    const Build build = tide_get_Fighter(&world, world.Duel.blue).build;
    TIDE_CHECK(build.gravity > 0);
    TIDE_CHECK(tide_get_Bullet(&world, shot).gravity == build.gravity);
    // In the open, away from platforms and fighters.
    place(world.Duel.blue, -400, -150);
    place(world.Duel.orange, 400, -150);
    place(shot, -300, 200);
    Body body = tide_get_Body(&world, shot);
    body.velocity = tide_f2(300, 0);
    tide_set_Body(&world, shot, body);
    for (int tick = 0; tick < 30; tick++) tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    body = tide_get_Body(&world, shot);
    // Half a second: each tick's speed is the one after its drop.
    const float dt = 1.0f / 60.0f;
    float y = 200, speed = 0;
    for (int tick = 0; tick < 30; tick++) {
        speed -= build.gravity * dt;
        y += speed * dt;
    }
    TIDE_CHECK(fabsf(body.position.y - y) < 0.01f);
    TIDE_CHECK(fabsf(body.position.x + 150) < 0.01f);
    TIDE_CHECK(body.velocity.y < -200);

    // A fighter with no gravity in its build fires shots that fly straight.
    Fighter blue = tide_get_Fighter(&world, world.Duel.blue);
    blue.build.gravity = 0;
    tide_set_Fighter(&world, world.Duel.blue, blue);
    Bullet old = tide_get_Bullet(&world, shot);
    old.lifetime = 0;
    tide_set_Bullet(&world, shot, old);
    tide_world_tick(&world);
    TIDE_REQUIRE(shots() == 0);
    shot = fire();
    TIDE_REQUIRE(tide_has_Bullet(&world, shot));
    TIDE_CHECK(tide_get_Bullet(&world, shot).gravity == 0);
    place(shot, -300, 200);
    body = tide_get_Body(&world, shot);
    body.velocity = tide_f2(300, 0);
    tide_set_Body(&world, shot, body);
    for (int tick = 0; tick < 30; tick++) tide_world_tick(&world);
    TIDE_CHECK(tide_get_Body(&world, shot).position.y == 200);
    tide_world_free(&world);
}

TIDE_TEST(duel_a_build_remembers_the_cards_it_was_given_in_order)
{
    start();
    world.Duel.phase = Phase_Draft;
    world.Duel.draftSide = 0;
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).build.cards == 0);
    TIDE_REQUIRE(choose(world.Duel.blue, 0, world.Duel.round, 2));
    tide_world_tick(&world);
    const int first = tide_get_Fighter(&world, world.Duel.blue).build.cards;
    TIDE_CHECK(first >= 1 && first <= 8); // One card: its number, from 1.
    world.Duel.phase = Phase_Draft;
    world.Duel.draftSide = 0;
    TIDE_REQUIRE(choose(world.Duel.blue, 0, world.Duel.round, 0));
    tide_world_tick(&world);
    const int both = tide_get_Fighter(&world, world.Duel.blue).build.cards;
    TIDE_CHECK(both % 16 == first);
    TIDE_CHECK(both / 16 >= 1 && both / 16 <= 8);
    tide_world_free(&world);
}

TIDE_TEST(duel_releasing_aim_stick_keeps_direction_until_mouse_is_used)
{
    start();
    tide_local local = {0};
    tide_local_init(&local);
    local.Canvas.screenHalf = tide_f2(480, 270);
    local.Canvas.scale = 1;
    local.Canvas.gamepadAim = true;
    tide_devices devices = {0};
    devices.gamepad.connected = true;
    devices.gamepad.rightStick = tide_f2(0, 0.8f);
    Controls input = tide_input_sample(&devices, &local);
    TIDE_CHECK(!input.pointAim);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    Fighter fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x == 0 && fighter.aim.y == 1);

    devices.gamepad.rightStick = tide_f2(0, 0);
    input = tide_input_sample(&devices, &local);
    TIDE_CHECK(!input.pointAim && input.aim.x == 0 && input.aim.y == 0);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x == 0 && fighter.aim.y == 1);

    // Mouse movement takes over immediately, before the next view updates
    // the remembered input mode. Point to the right of the current fighter.
    const Body body = tide_get_Body(&world, world.Duel.blue);
    devices.mouse.position = tide_f2(local.Canvas.screenHalf.x + body.position.x + 300,
        local.Canvas.screenHalf.y + body.position.y);
    devices.mouse.delta = tide_f2(1, 0);
    input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.pointAim);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x > 0.999f && fabsf(fighter.aim.y) < 0.0001f);

    tide_devices_consume(&devices);
    tide_button_set(&devices.mouse.right, true);
    input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.pointAim && input.block); // A new button press also takes over.
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).blockLeft > 0);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(duel_releasing_aim_touch_or_blocking_keeps_direction)
{
    start();
    tide_local local = {0};
    tide_local_init(&local);
    local.Canvas.screenHalf = tide_f2(480, 270);
    local.Canvas.scale = 1;
    local.Canvas.touchAim = true;
    tide_devices devices = {0};
    devices.touchscreen.connected = true;
    tide_touch *touch = &devices.touchscreen.touches.at[0];
    touch->id = 1;
    touch->startPosition = tide_f2(720, 278); // Began on the aim/fire half.
    touch->position = tide_f2(120, 278); // Now directly above blue in world space.
    tide_button_set(&touch->press, true);
    Controls input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.pointAim && input.fire);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    Fighter fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x == 0 && fighter.aim.y == 1);

    tide_devices_consume(&devices);
    tide_button_set(&touch->press, false);
    // A sampled button includes any time it was held within that tick.
    // Consume the release window before checking the first neutral sample.
    input = tide_input_sample(&devices, &local);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    tide_devices_consume(&devices);
    input = tide_input_sample(&devices, &local);
    TIDE_CHECK(!input.pointAim && !input.fire && input.aim.x == 0 && input.aim.y == 0);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x == 0 && fighter.aim.y == 1);

    // Touching only the block corner must not replace aim with the mouse's
    // idle position, which a touchscreen device might never have updated.
    tide_devices_consume(&devices);
    tide_touch *block = &devices.touchscreen.touches.at[1];
    block->id = 2;
    block->startPosition = tide_f2(920, 40);
    block->position = block->startPosition;
    tide_button_set(&block->press, true);
    input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.block && !input.pointAim && !input.fire);
    tide_world_set_input(&world, player(0), input);
    tide_world_tick(&world);
    fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(fighter.aim.x == 0 && fighter.aim.y == 1);
    TIDE_CHECK(fighter.blockLeft > 0);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(duel_simultaneous_knockout_replays_round_without_awarding_point)
{
    start();
    Fighter blue = tide_get_Fighter(&world, world.Duel.blue);
    Fighter orange = tide_get_Fighter(&world, world.Duel.orange);
    blue.fireCooldown = 0;
    orange.fireCooldown = 0;
    tide_set_Fighter(&world, world.Duel.blue, blue);
    tide_set_Fighter(&world, world.Duel.orange, orange);
    tide_world_set_input(&world, player(0), (Controls){.aim = {1, 0}, .fire = true});
    tide_world_set_input(&world, player(1), (Controls){.aim = {-1, 0}, .fire = true});
    tide_world_tick(&world);
    const tide_entity blueShot = shot_from(world.Duel.blue);
    const tide_entity orangeShot = shot_from(world.Duel.orange);
    TIDE_REQUIRE(tide_has_Bullet(&world, blueShot) && tide_has_Bullet(&world, orangeShot));
    tide_world_set_input(&world, player(0), (Controls){.aim = {1, 0}});
    tide_world_set_input(&world, player(1), (Controls){.aim = {-1, 0}});
    place(world.Duel.blue, -200, 180);
    place(world.Duel.orange, 200, 180);
    aim_shot(blueShot, 120, 180, 6000, 0, 0);
    aim_shot(orangeShot, -120, 180, -6000, 0, 0);
    Bullet bullet = tide_get_Bullet(&world, blueShot);
    bullet.damage = 100;
    tide_set_Bullet(&world, blueShot, bullet);
    bullet = tide_get_Bullet(&world, orangeShot);
    bullet.damage = 100;
    tide_set_Bullet(&world, orangeShot, bullet);
    const int round = world.Duel.round;
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 0);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).health == 0);
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.round == round + 1);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown);
    TIDE_CHECK(world.Duel.scoreBlue == 0 && world.Duel.scoreOrange == 0);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 100);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.orange).health == 100);
    tide_world_free(&world);
}

TIDE_TEST(duel_cards_persist_between_rounds_and_reset_on_rematch)
{
    start();
    const Build baseline = tide_get_Fighter(&world, world.Duel.blue).build;
    const int match = world.Duel.match;
    const int round = world.Duel.round;
    world.Duel.phase = Phase_Draft;
    world.Duel.draftSide = 0;
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round, 0));
    tide_world_tick(&world);
    const Build upgrade = tide_get_Fighter(&world, world.Duel.blue).build;
    TIDE_CHECK(memcmp(&baseline, &upgrade, sizeof upgrade) != 0);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown && world.Duel.round == round + 1);
    tide_world_tick(&world);
    Fighter fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(memcmp(&upgrade, &fighter.build, sizeof upgrade) == 0);
    TIDE_CHECK(fighter.health == upgrade.maxHealth);
    TIDE_CHECK(tide_get_Body(&world, world.Duel.blue).position.x == -360);
    world.Duel.scoreBlue = 5;
    world.Duel.phase = Phase_Finished;
    TIDE_REQUIRE(rematch(0, world.Duel.match));
    tide_world_tick(&world);
    tide_world_tick(&world);
    fighter = tide_get_Fighter(&world, world.Duel.blue);
    TIDE_CHECK(world.Duel.match == match + 1);
    TIDE_CHECK(world.Duel.scoreBlue == 0 && world.Duel.scoreOrange == 0);
    TIDE_CHECK(memcmp(&baseline, &fighter.build, sizeof baseline) == 0);
    TIDE_CHECK(fighter.health == 100);
    tide_world_free(&world);
}

TIDE_TEST(duel_rematch_command_rejects_an_earlier_match)
{
    start();
    const int oldMatch = world.Duel.match;
    world.Duel.phase = Phase_Finished;
    TIDE_REQUIRE(rematch(0, oldMatch));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.match == oldMatch + 1);
    tide_world_tick(&world);

    // A delayed request from the previous match can arrive after this one
    // also ends. Its delivery sequence is fresh, so the gameplay guard matters.
    world.Duel.phase = Phase_Finished;
    world.Duel.scoreOrange = 5;
    const int match = world.Duel.match;
    const int round = world.Duel.round;
    TIDE_REQUIRE(rematch(0, oldMatch));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Finished);
    TIDE_CHECK(world.Duel.match == match && world.Duel.round == round);
    TIDE_CHECK(world.Duel.scoreOrange == 5);

    TIDE_REQUIRE(rematch(0, match));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown);
    TIDE_CHECK(world.Duel.match == match + 1 && world.Duel.round == round + 1);
    TIDE_CHECK(world.Duel.scoreBlue == 0 && world.Duel.scoreOrange == 0);
    tide_world_free(&world);
}

TIDE_TEST(duel_card_commands_validate_sender_round_slot_and_drafting_side)
{
    start();
    const Build before = tide_get_Fighter(&world, world.Duel.blue).build;
    const int round = world.Duel.round;
    world.Duel.phase = Phase_Draft;
    world.Duel.draftSide = 0;
    TIDE_REQUIRE(choose(world.Duel.blue, 1, round, 0)); // Another player's fighter.
    tide_world_tick(&world);
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round - 1, 0)); // An earlier draft.
    tide_world_tick(&world);
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round, -1));
    tide_world_tick(&world);
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round, 3));
    tide_world_tick(&world);
    TIDE_REQUIRE(choose(world.Duel.orange, 1, round, 0)); // The winner cannot draft.
    tide_world_tick(&world);
    const Build after = tide_get_Fighter(&world, world.Duel.blue).build;
    TIDE_CHECK(memcmp(&before, &after, sizeof before) == 0);
    TIDE_CHECK(world.Duel.phase == Phase_Draft && world.Duel.round == round);
    TIDE_REQUIRE(choose(world.Duel.blue, 0, round, 1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Countdown && world.Duel.round == round + 1);
    tide_world_free(&world);
}

// What a machine shows of the duel, as a host runs a frame: its length first,
// then the views. `w` is NULL where there's no match.
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(const tide_world *w, const float seconds)
{
    static tide_devices devices;
    tide_local_frame_time(&local, seconds);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(1280, 720), NULL);
    tide_frame(w, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(duel_effects_move_by_the_frames_length_with_a_match_and_with_none)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame(&world, 0.25f);
    TIDE_CHECK(local.Fx.dt == 0.05f); // A stall is a step, not a leap
    frame(&world, 0.008f);
    TIDE_CHECK(local.Fx.dt == 0.008f);
    for (int tick = 0; tick < 5; tick++) tide_world_tick(&world); // However many ticks a frame saw
    frame(&world, 0.008f);
    TIDE_CHECK(local.Fx.dt == 0.008f);

    // Joining another machine's match: there's none here meanwhile, and the
    // frames go on, with the backdrop's clock and the spinner
    const float now = local.Fx.now;
    const int32_t frames = local.Fx.frame;
    tide_local_set_session(&local, TIDE_SESSION_CONNECTING, (tide_player_id){0}, 0, false, false, "");
    frame(NULL, 0.25f);
    TIDE_CHECK(local.Fx.frame == frames + 1 && local.Fx.now == now + 0.05f);
    TIDE_CHECK(fabsf(local.DuelMenu.turn - 0.18f) < 1e-5f); // 0.72 turns a second
    for (int i = 0; i < 5; i++) frame(NULL, 0.25f);
    TIDE_CHECK(fabsf(local.DuelMenu.turn - 0.08f) < 1e-5f); // Past a turn, it starts over
    TIDE_CHECK(draw.count > 0);
    tide_local_free(&local);
    tide_world_free(&world);
}
