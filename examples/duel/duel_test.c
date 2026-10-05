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

static bool rematch(int sender, int match)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Rematch, 16);
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
        if (command->kind == TIDE_DRAW_CIRCLE && command->b.x == 18
            && command->color.r == 0.29f && command->color.g == 0.72f && command->color.b == 1)
            return command->a.x;
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
    // Run ordinary practice play in the real arena, with blue providing no
    // input, including the normal countdown and the bot's firing cooldown.
    for (int tick = 0; tick < 1200 && world.Duel.phase != Phase_Draft; tick++)
        tide_world_tick(&world);
    TIDE_CHECK(world.Duel.phase == Phase_Draft);
    TIDE_CHECK(world.Duel.draftSide == 0);
    TIDE_CHECK(world.Duel.scoreOrange == 1 && world.Duel.scoreBlue == 0);
    TIDE_CHECK(tide_get_Fighter(&world, world.Duel.blue).health == 0);
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
