#include "foldline_render_test.h"
#include "tide/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 1280, HEIGHT = 720, GRID_X = 21, GRID_Y = 31, SAMPLES = GRID_X * GRID_Y };

typedef struct image {
    tide_float2 points[SAMPLES];
    uint32_t pixels[SAMPLES];
    int count;
} image;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int failures;
static bool scene_samples;
// The tour's frames can be drawn between two ticks, as a player's are; the checks draw the current tick alone.
static const tide_world *presented_previous;
static float presented_alpha = 1;
static void check(bool passed, const char *message);

// Optional local evidence, using the same recorded scene as the assertions.
// The normal native/web tests neither write files nor change their resolution.
static void capture_scene(const char *name)
{
#ifndef __wasm__
    const char *folder = getenv("FOLDLINE_CAPTURES");
    if (!folder || !folder[0]) return;
    char path[1024];
    const int length = snprintf(path, sizeof path, "%s/%s.png", folder, name);
    check(length > 0 && (size_t)length < sizeof path, "capture path fits");
    tide_platform_draw(&draw);
    check(tide_platform_capture(path), "write optional renderer capture");
#else
    (void)name;
#endif
}

static void check(bool passed, const char *message)
{
    if (!passed) {
        fprintf(stderr, "FOLDLINE portal rendering failure: %s\n", message);
        failures++;
    }
}

static int color_distance(uint32_t a, uint32_t b)
{
    int distance = 0;
    for (int shift = 8; shift <= 24; shift += 8) {
        int difference = abs((int)((a >> shift) & 255) - (int)((b >> shift) & 255));
        if (difference > distance) distance = difference;
    }
    return distance;
}

static int changed(const image *a, const image *b)
{
    int count = 0;
    for (int i = 0; i < a->count; i++)
        if (color_distance(a->pixels[i], b->pixels[i]) > 14) count++;
    return count;
}

static float changed_center(const image *empty, const image *occupied)
{
    float sum = 0;
    int count = 0;
    for (int i = 0; i < empty->count; i++) {
        if (color_distance(empty->pixels[i], occupied->pixels[i]) > 14) {
            sum += occupied->points[i].x;
            count++;
        }
    }
    check(count >= 8, "a destination cube must cover several portal pixels");
    return count ? sum / (float)count : 0;
}

// The cube, where the renderer reads it: its own component, and the fold's copy of the first loose thing.
static void cube_at(tide_float3 position)
{
    Cube cube = tide_get_Cube(&world, world.Arena.cube);
    cube.at = position;
    cube.turn = (tide_quaternion){0, 0, 0, 1};
    check(tide_set_Cube(&world, world.Arena.cube, cube), "test cube exists");
    world.Fold.cube = position;
}

static void operator_at(tide_entity entity, tide_float3 position, float yaw)
{
    Operator body = tide_get_Operator(&world, entity);
    body.position = position;
    body.yaw = yaw;
    body.pitch = 0;
    check(tide_set_Operator(&world, entity, body), "test operator exists");
}

static void frame_for(const int active, const bool menu)
{
    const tide_float2 screen = {WIDTH, HEIGHT};
    const tide_devices devices = {0};
    local.Interface.open = menu;
    local.Interface.looking = false;
    local.Interface.introduced = true;
    local.Control.active = active;
    local.Control.started = !menu;
    local.Optics.warmed = 3; // Past the first frames, which are drawn with folds that aren't there
    tide_local_frame_time(&local, 0);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, screen);
    tide_gui_begin(&gui, &devices, screen, tide_platform_measure_text);
    tide_frame(&world, presented_previous, presented_alpha, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void record(image *out)
{
    frame_for(0, false);

    // Independently project the source aperture, for a yaw-zero camera.
    const Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    const float focal = HEIGHT * 0.5f / tide_tan_f(75 * TIDE_DEG2RAD_F * 0.5f);
    const float center_x = scene_samples ? WIDTH * 0.5f : WIDTH * 0.5f + (-5 - viewer.position.x) * focal / (2.4f - viewer.position.z);
    out->count = 0;
    for (int row = 0; row < GRID_Y; row++) {
        for (int column = 0; column < GRID_X; column++) {
            const float x = center_x + (column - GRID_X / 2) * 7;
            const float y = HEIGHT * 0.5f + (row - GRID_Y / 2) * 7;
            // The crosshair is presentation, outside this image assertion.
            if (tide_abs_f(x - WIDTH * 0.5f) < 20 && tide_abs_f(y - HEIGHT * 0.5f) < 20) continue;
            out->points[out->count++] = (tide_float2){x, y};
        }
    }
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, 1, out->points, out->count, out->pixels);
}

// The bench, from the middle of its floor: one end in the open before the viewer, the other on the west wall of
// the tall bay, facing into it.
static void prepare(float camera_x)
{
    operator_at(world.Arena.op0, (tide_float3){camera_x, 0, -1.5f}, 0);
    operator_at(world.Arena.op1, (tide_float3){4, 0, -8}, 0);
    world.Fold.ends.at[0] = (Portal){.open = true, .at = {-5, 1.55f, 2.4f}, .face = {0, 0, -1}, .up = {0, 1, 0}, .size = 1};
    world.Fold.ends.at[1] = (Portal){.open = true, .at = {-10, 1.55f, 10}, .face = {1, 0, 0}, .up = {0, 1, 0}, .size = 1};
    cube_at((tide_float3){-3, 0.5f, -8});
    world.Fold.cubeTurn = (tide_quaternion){0, 0, 0, 1};
    world.Fold.carried = -1;
}

static int red_interior(const image *seen)
{
    int red = 0;
    for (int i = 0; i < seen->count; i++) {
        if (tide_abs_f(seen->points[i].x - WIDTH * .5f) > 35 || tide_abs_f(seen->points[i].y - HEIGHT * .5f) > 60) continue;
        const int r = (seen->pixels[i] >> 24) & 255, g = (seen->pixels[i] >> 16) & 255, b = (seen->pixels[i] >> 8) & 255;
        if (r > g + 35 && r > b + 35) red++;
    }
    return red;
}

static void select_place(const int id)
{
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectPlace;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectPlace >> 8);
    payload[10] = (uint8_t)(id * 2);
    const tide_player_id owner = tide_player_from_index(0);
    const uint32_t sequence = tide_game_api.command_sequence(&world, owner, 0) + 1;
    check(tide_game_api.command(&world, owner, 0, sequence, payload, sizeof payload), "select render fixture place");
    tide_world_tick(&world);
    tide_world_tick(&world);
}

// A turn about the vertical a quarter at a time, as play.tide's TurnBy.
static tide_float3 turn_by(const tide_float3 v, const float yaw)
{
    const int quarters = (((int)(yaw / (TIDE_PI_F * 0.5f) + (yaw < 0 ? -0.5f : 0.5f))) % 4 + 4) % 4;
    if (quarters == 1) return (tide_float3){v.z, v.y, -v.x};
    if (quarters == 2) return (tide_float3){-v.x, v.y, -v.z};
    if (quarters == 3) return (tide_float3){-v.z, v.y, v.x};
    return v;
}

static tide_float3 lift_point(const tide_float3 middle, const float yaw, const tide_float3 local)
{
    const tide_float3 turned = turn_by(local, yaw);
    return (tide_float3){middle.x + turned.x, middle.y + turned.y, middle.z + turned.z};
}

static void body_at(tide_entity entity, tide_float3 feet)
{
    Operator body = tide_get_Operator(&world, entity);
    Box3D_Body rigid = tide_get_Box3D_Body(&world, entity);
    body.position = feet;
    body.yaw = 0;
    body.pitch = 0;
    body.last = (tide_float3){feet.x, feet.y + .9f, feet.z};
    rigid.position = body.last;
    rigid.velocity = (tide_float3){0};
    check(tide_set_Operator(&world, entity, body) && tide_set_Box3D_Body(&world, entity, rigid), "position lift riders");
}

// How many of the dressing's lift frames, far doors and lamp fittings, as they were kept from earlier frames,
// differ from what the place dresses now.
static int dressing_moves(void)
{
    enum { MOST = 4096 };
    static Fixture kept[MOST];
    const tide_list lists[] = {local.Optics.furnishings, local.Optics.glass};
    int counts[2], total = 0;
    for (int list = 0; list < 2; list++) {
        counts[list] = tide_list_read_count(&local.heap, lists[list]);
        for (int index = 0; index < counts[list] && total < MOST; index++)
            kept[total++] = *(const Fixture *)tide_list_read(&local.heap, lists[list], index, sizeof(Fixture));
    }
    local.Optics.dressed = -1;
    frame_for(0, false);
    const tide_list fresh[] = {local.Optics.furnishings, local.Optics.glass};
    // The lists are compared as sets: the entities they're made from may come in another order once the last
    // place's are gone.
    static bool matched[MOST];
    int moved = 0, at = 0;
    for (int list = 0; list < 2; list++) {
        const int count = tide_list_read_count(&local.heap, fresh[list]);
        moved += abs(count - counts[list]);
        for (int index = 0; index < counts[list] && at + index < MOST; index++) matched[at + index] = false;
        for (int index = 0; index < count; index++) {
            const Fixture *now = tide_list_read(&local.heap, fresh[list], index, sizeof(Fixture));
            const float *a = &now->transform.c0.x;
            bool found = false;
            for (int other = 0; other < counts[list] && at + other < MOST && !found; other++) {
                if (matched[at + other]) continue;
                const float *b = &kept[at + other].transform.c0.x;
                bool same = true;
                for (int cell = 0; cell < 16 && same; cell++) same = tide_abs_f(a[cell] - b[cell]) <= .001f;
                if (same) found = matched[at + other] = true;
            }
            if (!found) moved++;
        }
        at += counts[list];
    }
    if (moved) fprintf(stderr, "Place %d: %d dressed fixtures moved when dressed again\n", world.Place.id, moved);
    return moved;
}

// One ride down, from the lift out of the place it's in, with both standing in it a little off its middle.
static void ride_checks(void)
{
    const Place left = world.Place;
    const bool stopping = left.kind == PlaceKind_Vignette;
    world.Arena.arrive = stopping ? left.stay + 1 : 2;
    world.Arena.gate = 0;
    world.Arena.ride = 0;
    if (stopping) world.Arena.door = 0;
    body_at(world.Arena.op0, lift_point(left.exit, left.exitYaw, (tide_float3){.8f, 0, -.2f}));
    body_at(world.Arena.op1, lift_point(left.exit, left.exitYaw, (tide_float3){-1.2f, 0, -.2f}));
    for (int step = 0; step < 10; step++) tide_world_tick(&world); // Settle physical placement before measuring continuity.
    world.Arena.ride = 0;
    const Operator departed_a = tide_get_Operator(&world, world.Arena.op0), departed_b = tide_get_Operator(&world, world.Arena.op1);
    image before, crossing, after;
    record(&before);
    const int chamber = world.Arena.chamber;
    world.Arena.ride = 1.3f - 1.0f / 60;
    static tide_world previous;
    for (int step = 0; step < 3 && world.Arena.chamber == chamber; step++) {
        tide_world_copy(&previous, &world);
        tide_world_tick(&world);
    }
    check(world.Arena.chamber == chamber + 1, "every lift advances to its next place");
    // Includes the first frame before the arriving bodies are updated, drawn between the last tick of the place
    // left and the first of the next, as a player's is.
    presented_previous = &previous;
    presented_alpha = .5f;
    record(&crossing);
    presented_previous = NULL;
    presented_alpha = 1;
    int dark = 0;
    for (int i = 0; i < crossing.count; i++)
        if ((crossing.pixels[i] >> 8) == 0) dark++;
    check(dark > crossing.count * 9 / 10, "the scene change occurs inside the lift blackout");
    for (int step = 0; step < 31; step++) tide_world_tick(&world);
    record(&after);
    // Each stands in the lift come by as they stood in the one left by, turned as the two lifts are
    const Place came = world.Place;
    const float way = left.exitYaw + TIDE_PI_F;
    const Operator a = tide_get_Operator(&world, world.Arena.op0), b = tide_get_Operator(&world, world.Arena.op1);
    const tide_float3 wa = lift_point(came.arrival, came.arrivalYaw,
                                      turn_by((tide_float3){departed_a.position.x - left.exit.x, 0, departed_a.position.z - left.exit.z}, -way));
    const tide_float3 wb = lift_point(came.arrival, came.arrivalYaw,
                                      turn_by((tide_float3){departed_b.position.x - left.exit.x, 0, departed_b.position.z - left.exit.z}, -way));
    check(tide_abs_f(a.position.x - wa.x) < .05f && tide_abs_f(a.position.z - wa.z) < .05f && tide_abs_f(a.position.y - came.arrival.y) < .05f,
          "camera retains its position relative to the lift");
    check(tide_abs_f(b.position.x - wb.x) < .05f && tide_abs_f(b.position.z - wb.z) < .05f, "partner retains their position relative to the lift");
    check(local.Lens.lift < .01f, "arrival fades back in before its door opens");
    // The shutter is still closed, at the same local plane, throughout the reveal.
    check(world.Arena.arrive < .7f, "geometry comparison precedes the opening door");
    check(changed(&crossing, &after) > after.count / 2, "arrival reveals a rendered lift rather than remaining black");
    // The lifts and lamps dressed on that first, blended frame are where the place has them: a blended Place once
    // left the last place's lift frames and shutter standing in this one.
    check(dressing_moves() == 0, "the first frame of a place dresses its lifts and lamps where they are");
}

// Every campaign lift, including rotated landings and the terminal coda.
static void transition_checks(void)
{
    scene_samples = true;
    select_place(0);
    ride_checks();
    int stops = 1;
    while (world.Place.kind != PlaceKind_Ending && stops < 16) {
        ride_checks();
        stops++;
    }
    check(stops == 14 && world.Place.kind == PlaceKind_Ending, "all fourteen stops reach the terminal scene");
    const int chamber = world.Arena.chamber;
    for (int tick = 0; tick < 720; tick++) tide_world_tick(&world);
    check(world.Arena.chamber == chamber && world.Arena.ride == 0, "the conclusion remains available instead of wrapping");
    scene_samples = false;
}

// Allocate a single ordinary Beast row, then use the generated component
// setter for every change. No physics or AI is involved in this renderer test.
static tide_entity render_beast(void)
{
    const tide_entity entity = tide_entity_create(&world.entities);
    const uint32_t row = tide_table_add(&world.arch0_Beast, &tide_columns_arch0_Beast);
    *(tide_entity *)tide_table_cell(&world.arch0_Beast, &tide_columns_arch0_Beast, row, tide_column_arch0_Beast_entity) = entity;
    tide_entity_set_location(&world.entities, entity, (tide_location){0, row});
    return entity;
}

static void beast_checks(void)
{
    static const char *const names[] = {"flayed", "crawler", "spider", "figure", "drowned"};
    static const char *const clips[] = {"Run", "Crawl", "Walk", "Creep", "Crawl"};
    static const float sample_times[] = {.4f, .3f, .3f, .5f, 1.8f};
    select_place(0);
    world.Arena.arrive = 2;
    world.Watcher.state = Watching_Away;
    for (int end = 0; end < 4; end++) world.Fold.ends.at[end] = (Portal){0};
    operator_at(world.Arena.op1, (tide_float3){9, 0, -8}, 0);
    cube_at((tide_float3){9, .5f, -8});
    scene_samples = true;
    const tide_entity entity = render_beast();
    for (int kind = BeastKind_Flayed; kind <= BeastKind_Drowned; kind++) {
        const bool low = kind == BeastKind_Crawler || kind == BeastKind_Spider || kind == BeastKind_Drowned;
        operator_at(world.Arena.op0, (tide_float3){0, low ? -.7f : 0, 0}, 0);
        Beast beast = {.chamber = world.Arena.chamber, .kind = (BeastKind)kind,
                       .skin = kind == BeastKind_Flayed ? BeastSkin_Dark : BeastSkin_Mannequin,
                       .at = {0, 0, 3}, .yaw = TIDE_PI_F, .scale = 1, .stretch = 1,
                       .clip = tide_text_temp(tide_str_from_cstr(clips[kind])), .loop = true};
        check(tide_set_Beast(&world, entity, beast), "set fixture beast");
        image empty, posed, animated, glowing;
        record(&empty);
        beast.visible = true;
        check(tide_set_Beast(&world, entity, beast), "show fixture beast");
        record(&posed);
        beast.time = sample_times[kind];
        check(tide_set_Beast(&world, entity, beast), "advance fixture beast clip");
        record(&animated);
        printf("Beast %s: %d silhouette samples, %d animated samples\n", names[kind], changed(&empty, &posed), changed(&posed, &animated));
        check(changed(&empty, &posed) >= 8, "each imported beast binds and draws its skinned silhouette");
        check(changed(&posed, &animated) >= 2, "each imported beast clip deforms the drawn model");
        beast.eyes = true;
        check(tide_set_Beast(&world, entity, beast), "light fixture beast eyes");
        record(&glowing);
        if (kind == BeastKind_Spider || kind == BeastKind_Flayed)
            check(changed(&animated, &glowing) > 0, "spider and flayed eyes emit in the scene");
        capture_scene(names[kind]);
    }

    // The same model must be live through a fold and disappear on its camera
    // side. This also exercises its fold-light shadow palette draws.
    scene_samples = false;
    prepare(-5);
    Beast beast = tide_get_Beast(&world, entity);
    beast.kind = BeastKind_Flayed;
    beast.skin = BeastSkin_Dark;
    beast.clip = tide_text_temp(tide_str_from_cstr("Run"));
    beast.at = (tide_float3){-7, 0, 10.6f};
    beast.time = .4f;
    beast.visible = false;
    check(tide_set_Beast(&world, entity, beast), "hide portal beast");
    image empty, seen, clipped;
    record(&empty);
    beast.visible = true;
    check(tide_set_Beast(&world, entity, beast), "show portal beast");
    record(&seen);
    check(changed(&empty, &seen) >= 8, "a beast is rendered live beyond a fold");
    capture_scene("beast-portal");
    beast.at.x = -13;
    check(tide_set_Beast(&world, entity, beast), "move beast behind fold camera plane");
    record(&clipped);
    check(changed(&empty, &clipped) <= 4, "fold camera clips a beast behind its destination plane");
    beast.visible = false;
    check(tide_set_Beast(&world, entity, beast), "retire visual fixture beast");
}

static void lamp_and_fog_checks(void)
{
    select_place(0);
    world.Arena.arrive = 2;
    // Look at carpet directly below a known lamp, clear of the central paper
    // partition. The former horizontal view sampled that partition's dark side.
    operator_at(world.Arena.op0, (tide_float3){-7.5f, 0, -7.2f}, 0);
    Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    viewer.pitch = -.6f;
    check(tide_set_Operator(&world, world.Arena.op0, viewer), "aim lighting fixture at the floor");
    for (int end = 0; end < 4; end++) world.Fold.ends.at[end] = (Portal){0};
    // Make a real named lamp group, including its fittings, then let the match
    // switch it off. The torch remains on, as in a dark campaign place.
    for (int row = 0; row < world.Place.lampsLong; row++) {
        for (int column = 0; column < world.Place.lampsWide; column++) {
            Lamp *lamp = &world.Place.lamps.at[row * 24 + column];
            lamp->state = LampState_On;
            lamp->high = 4.4f;
            lamp->group = 1;
        }
    }
    scene_samples = true;
    image on, off, fog;
    record(&on);
    capture_scene("lamp-on");
    world.Arena.darkGroups = 1 << 1;
    record(&off);
    capture_scene("lamp-off");
    printf("Named lamp group changed floor samples: %d\n", changed(&on, &off));
    check(changed(&on, &off) > 100, "a switched lamp group changes the room's actual lighting");
    world.Arena.darkGroups = 0;
    world.Place.fog = 1;
    record(&fog);
    capture_scene("place-fog");
    printf("Place fog changed floor samples: %d\n", changed(&on, &fog));
    check(changed(&on, &fog) > 100, "place fog density changes rendered geometry");
    scene_samples = false;
    select_place(0);
}

// A fold is one opening in one face, not a hole through the back of the
// cupboard. The receptive panel is only .8 m thick, within InFold's cut depth.
static void portal_back_checks(void)
{
    select_place(2);
    // The cupboard's shutter covers its back while its plate is up: someone stands on it.
    body_at(world.Arena.op1, (tide_float3){-8, 0, 4});
    for (int tick = 0; tick < 150; tick++) tide_world_tick(&world);
    world.Arena.arrive = 2;
    world.Watcher.state = Watching_Away;
    for (int end = 0; end < 4; end++) world.Fold.ends.at[end] = (Portal){0};
    operator_at(world.Arena.op1, (tide_float3){-10, 0, -6}, 0);
    operator_at(world.Arena.op0, (tide_float3){10.8f, 0, 7.6f}, TIDE_PI_F);
    cube_at((tide_float3){-5, .5f, -6});
    world.Fold.ends.at[0] = (Portal){.open = true, .at = {10.8f, 1.55f, 6}, .face = {0, 0, -1}, .up = {0, 1, 0}, .size = 1};
    world.Fold.ends.at[1] = (Portal){.at = {-12, 1.55f, -3}, .face = {1, 0, 0}, .up = {0, 1, 0}, .size = 1};
    scene_samples = true;
    image solid, rear, front, cargo;
    record(&solid);
    capture_scene("portal-rear-solid");
    world.Fold.ends.at[1].open = true;
    record(&rear);
    capture_scene("portal-rear-linked");
    printf("Portal rear changed samples: %d\n", changed(&solid, &rear));
    check(changed(&solid, &rear) <= 4, "a linked fold preserves its thin wall's opaque rear face");

    // Keep these samples inside the reverse of the aperture; the exposed side
    // of the cupboard outside it may legitimately receive its front-side glow.
    operator_at(world.Arena.op0, (tide_float3){10, 0, 7.6f}, tide_atan2_f(.8f, -.8f));
    world.Fold.ends.at[1].open = false;
    record(&solid);
    capture_scene("portal-rear-oblique-solid");
    world.Fold.ends.at[1].open = true;
    record(&rear);
    capture_scene("portal-rear-oblique");
    printf("Oblique portal rear changed samples: %d\n", changed(&solid, &rear));
    check(changed(&solid, &rear) <= 4, "an oblique rear view keeps the same opaque wall around its fold");

    operator_at(world.Arena.op0, (tide_float3){10.8f, 0, 3.8f}, 0);
    record(&front);
    capture_scene("portal-front-empty");
    cube_at((tide_float3){-9, 1.55f, -2.4f});
    record(&cargo);
    capture_scene("portal-front-cargo");
    check(changed(&front, &cargo) >= 8, "the same fold still shows live geometry from its receptive front");
    scene_samples = false;
    select_place(0);
}

static void exit_sign_checks(void)
{
    select_place(2);
    world.Arena.arrive = 2;
    for (int end = 0; end < 4; end++) world.Fold.ends.at[end] = (Portal){0};
    operator_at(world.Arena.op0, (tide_float3){6, 0, 4.2f}, 0);
    Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    viewer.pitch = tide_atan2_f(4.2f - 1.55f, 7.45f - 4.2f);
    check(tide_set_Operator(&world, world.Arena.op0, viewer), "look at the exit header from its approach");
    scene_samples = true;
    image sign, header;
    record(&sign);
    capture_scene("reception-exit-sign");
    int signs = 0;
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (!tide_has_Prop(&world, entity)) continue;
        Prop prop = tide_get_Prop(&world, entity);
        if (prop.kind != PropKind_ExitSign || prop.chamber != world.Arena.chamber) continue;
        prop.at.y -= 1000;
        check(tide_set_Prop(&world, entity, prop), "hide exit plaque to compare its unobstructed header");
        signs++;
    }
    check(signs == 1, "the Reception exit has one mounted sign");
    local.Optics.dressed = -1;
    record(&header);
    printf("Visible exit sign samples: %d\n", changed(&sign, &header));
    check(changed(&sign, &header) >= 8, "the exit plaque is visible from its room-side approach");
#ifndef __wasm__
    if (getenv("FOLDLINE_CAPTURES")) {
        select_place(2);
        world.Arena.arrive = 2;
        world.Arena.signals = 1 << 1;
        world.Arena.gate = 1;
        operator_at(world.Arena.op0, (tide_float3){4, 0, 3.8f}, tide_atan2_f(2, 4.2f));
        viewer = tide_get_Operator(&world, world.Arena.op0);
        viewer.pitch = .16f;
        check(tide_set_Operator(&world, world.Arena.op0, viewer), "frame the corrected exit approach for review");
        record(&sign);
        capture_scene("reception-exit-approach");
    }
#endif
    scene_samples = false;
    select_place(0);
}

enum { RETICLE_BAR, RETICLE_TOP, RETICLE_BOTTOM, RETICLE_PIP, RETICLE_RIM, RETICLE_PARTS };

typedef struct reticle_image {
    uint32_t pixels[2][RETICLE_PARTS];
    bool valid[2];
} reticle_image;

// These samples deliberately cover the HUD pixels excluded from record()'s
// scene grid. The fixture remains at 720p, where one UI dot is two pixels.
static void record_reticle(reticle_image *out, const int active, const bool menu)
{
    tide_float2 points[2 * RETICLE_PARTS];
    for (int end = 0; end < 2; end++) {
        const float side = end == 0 ? -1.0f : 1.0f;
        const float x = WIDTH * .5f, y = HEIGHT * .5f;
        points[end * RETICLE_PARTS + RETICLE_BAR] = (tide_float2){x + side * 10 + .25f, y + .25f};
        points[end * RETICLE_PARTS + RETICLE_TOP] = (tide_float2){x + side * 8 + .25f, y - 4 + .25f};
        points[end * RETICLE_PARTS + RETICLE_BOTTOM] = (tide_float2){x + side * 8 + .25f, y + 4 + .25f};
        points[end * RETICLE_PARTS + RETICLE_PIP] = (tide_float2){x + side * 10 + .25f, y + 12 + .25f};
        points[end * RETICLE_PARTS + RETICLE_RIM] = (tide_float2){x + side * 10 + 2.25f, y + 12 + .25f};
    }
    const uint64_t before = tide_world_hash(&world);
    frame_for(active, menu);
    check(tide_world_hash(&world) == before, "placement preview frames do not change match state");
    out->valid[0] = local.Interface.shot0.valid;
    out->valid[1] = local.Interface.shot1.valid;
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, 1, points, 2 * RETICLE_PARTS, &out->pixels[0][0]);
}

static int reticle_brightness(const uint32_t pixel)
{
    int brightest = 0;
    for (int shift = 8; shift <= 24; shift += 8) {
        const int channel = (int)((pixel >> shift) & 255);
        if (channel > brightest) brightest = channel;
    }
    return brightest;
}

static bool reticle_color(const uint32_t pixel, const int end)
{
    const int r = (pixel >> 24) & 255, g = (pixel >> 16) & 255, b = (pixel >> 8) & 255;
    if (end == 0) return r > g + 30 && g > b + 25;
    if (end == 1) return g > r + 70 && b > r + 70 && abs(g - b) < 40;
    if (end == 2) return r > b + 50 && b > g + 20;
    return b > r + 30 && r > g + 20;
}

static void reticle_placed(const reticle_image *seen, const int end, const bool placed)
{
    const int contrast = color_distance(seen->pixels[end][RETICLE_PIP], seen->pixels[end][RETICLE_RIM]);
    check(placed ? contrast < 8 : contrast > 70, placed ? "a planted end has a filled status square" : "an unplanted end has a hollow status square");
}

static void reticle_ready(const reticle_image *seen, const int end, const bool ready, const reticle_image *reference)
{
    check(seen->valid[end] == ready, ready ? "HUD previews a permitted placement for this end" : "HUD refuses this end's current placement");
    if (ready) {
        check(color_distance(seen->pixels[end][RETICLE_BAR], seen->pixels[end][RETICLE_TOP]) < 8 &&
              color_distance(seen->pixels[end][RETICLE_BAR], seen->pixels[end][RETICLE_BOTTOM]) < 8,
              "a permitted placement draws both bright bracket caps");
    } else {
        check(reticle_brightness(reference->pixels[end][RETICLE_BAR]) - reticle_brightness(seen->pixels[end][RETICLE_BAR]) > 50,
              "an unavailable placement dims its side bar");
        check(color_distance(reference->pixels[end][RETICLE_TOP], seen->pixels[end][RETICLE_TOP]) > 30 &&
              color_distance(reference->pixels[end][RETICLE_BOTTOM], seen->pixels[end][RETICLE_BOTTOM]) > 30,
              "an unavailable placement removes both bright bracket caps");
    }
}

static tide_entity reticle_rear_wall(void)
{
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (!tide_has_Wall(&world, entity)) continue;
        const Wall wall = tide_get_Wall(&world, entity);
        if (wall.chamber == world.Arena.chamber && wall.takes &&
            tide_abs_f(wall.center.x + 10.5f) < .01f && wall.center.z > 5) return entity;
    }
    check(false, "the reticle fixture has an off-camera receptive wall");
    return (tide_entity){0};
}

static void reticle_checks(void)
{
    select_place(0);
    world.Arena.arrive = 2;
    world.Arena.ride = 0;
    world.Watcher.state = Watching_Away;
    for (int end = 0; end < 4; end++) world.Fold.ends.at[end] = (Portal){0};
    world.Fold.busy.at[0] = world.Fold.busy.at[1] = 0;
    world.Fold.holding.at[0] = world.Fold.holding.at[1] = 0;
    operator_at(world.Arena.op0, (tide_float3){-6, 0, -7}, TIDE_PI_F);
    operator_at(world.Arena.op1, (tide_float3){-7, 0, -7}, TIDE_PI_F);

    reticle_image ready, placed, blocked, overlap, opposite, other, disabled;
    record_reticle(&ready, 0, false);
    capture_scene("reticle-ready");
    const Portal target = local.Interface.shot0.portal;
    for (int end = 0; end < 2; end++) {
        reticle_ready(&ready, end, true, &ready);
        reticle_placed(&ready, end, false);
        check(reticle_color(ready.pixels[end][RETICLE_BAR], end), "operator A's placement buttons use amber and cyan");
    }

    // A preplaced end counts too. Its off-camera position does not alter the
    // aimed surface, so readiness and stored placement can be checked apart.
    const tide_entity rear = reticle_rear_wall();
    world.Fold.ends.at[0] = (Portal){.open = true, .at = {-10, 1.3f, 8}, .face = {1, 0, 0}, .up = {0, 1, 0}, .wall = rear, .size = 1};
    record_reticle(&placed, 0, false);
    capture_scene("reticle-one-placed");
    reticle_placed(&placed, 0, true);
    reticle_placed(&placed, 1, false);
    for (int end = 0; end < 2; end++) reticle_ready(&placed, end, true, &ready);

    operator_at(world.Arena.op0, (tide_float3){-6, 0, -7}, TIDE_PI_F * .5f);
    record_reticle(&blocked, 0, false);
    capture_scene("reticle-blocked");
    for (int end = 0; end < 2; end++) reticle_ready(&blocked, end, false, &ready);
    reticle_placed(&blocked, 0, true);
    reticle_placed(&blocked, 1, false);
    check(color_distance(placed.pixels[0][RETICLE_PIP], blocked.pixels[0][RETICLE_PIP]) < 8,
          "a planted square stays filled when the current aim becomes invalid");

    // One unlinked end leaves its wall solid. Replacing itself is valid, and
    // so is putting the other button there, which replaces it.
    operator_at(world.Arena.op0, (tide_float3){-6, 0, -7}, TIDE_PI_F);
    world.Fold.ends.at[0] = target;
    record_reticle(&overlap, 0, false);
    reticle_ready(&overlap, 0, true, &ready);
    reticle_ready(&overlap, 1, true, &ready);
    reticle_placed(&overlap, 0, true);
    reticle_placed(&overlap, 1, false);
    world.Fold.ends.at[0] = (Portal){0};
    world.Fold.ends.at[1] = target;
    record_reticle(&opposite, 0, false);
    reticle_ready(&opposite, 0, true, &ready);
    reticle_ready(&opposite, 1, true, &ready);
    reticle_placed(&opposite, 0, false);
    reticle_placed(&opposite, 1, true);

    // Deliberately asymmetric ownership catches reading A's state while B is
    // active, independently of the palette changing to rose and violet.
    world.Fold.ends.at[0] = (Portal){.open = true, .at = {-10, 1.3f, 8}, .face = {1, 0, 0}, .up = {0, 1, 0}, .wall = rear, .size = 1};
    world.Fold.ends.at[1] = (Portal){0};
    world.Fold.ends.at[3] = (Portal){.open = true, .at = {-10, 1.3f, 4}, .face = {1, 0, 0}, .up = {0, 1, 0}, .wall = rear, .size = 1};
    record_reticle(&other, 1, false);
    capture_scene("reticle-operator-b");
    reticle_placed(&other, 0, false);
    reticle_placed(&other, 1, true);
    for (int end = 0; end < 2; end++) {
        reticle_ready(&other, end, true, &other);
        check(reticle_color(other.pixels[end][RETICLE_BAR], end + 2), "operator B's placement buttons use rose and violet");
    }

    record_reticle(&placed, 0, false);
    for (int gate = 0; gate < 3; gate++) {
        world.Place.dead = gate == 0;
        world.Fold.holding.at[0] = gate == 1 ? 1 : 0;
        Operator viewer = tide_get_Operator(&world, world.Arena.op0);
        viewer.taken = gate == 2 ? .1f : 0;
        check(tide_set_Operator(&world, world.Arena.op0, viewer), "set the reticle's ordinary action gates");
        record_reticle(&disabled, 0, false);
        if (gate != 0)
            for (int end = 0; end < 2; end++) reticle_ready(&disabled, end, false, &placed);
        else
            for (int end = 0; end < 2; end++) check(!disabled.valid[end], "a dead device previews nothing");
        if (gate == 0) {
            // Where the device is dead there are no marks at all, placed or not: only the reticle's middle
            for (int end = 0; end < 2; end++)
                check(color_distance(disabled.pixels[end][RETICLE_RIM], placed.pixels[end][RETICLE_RIM]) > 30,
                      "a dead device shows no fold status marks");
            continue;
        }
        reticle_placed(&disabled, 0, true);
        reticle_placed(&disabled, 1, false);
    }
    world.Place.dead = false;
    world.Fold.holding.at[0] = 0;
    Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    viewer.taken = 0;
    check(tide_set_Operator(&world, world.Arena.op0, viewer), "restore the reticle fixture's active operator");
    record_reticle(&disabled, 0, true);
    check(!disabled.valid[0] && !disabled.valid[1], "the menu clears placement previews");
    for (int end = 0; end < 2; end++)
        check(color_distance(disabled.pixels[end][RETICLE_RIM], placed.pixels[end][RETICLE_RIM]) > 30,
              "the menu hides the fold status marks");
    select_place(0);
}

// How many of a coarse grid over the whole picture differ between two frames of the scene.
static int whole_frame_changes(const bool a_visible, const tide_entity entity)
{
    enum { WIDE = 160, HIGH = 90 };
    static tide_float2 points[WIDE * HIGH];
    static uint32_t first[WIDE * HIGH], second[WIDE * HIGH];
    for (int row = 0; row < HIGH; row++)
        for (int column = 0; column < WIDE; column++)
            points[row * WIDE + column] = (tide_float2){(column + .5f) * WIDTH / WIDE, (row + .5f) * HEIGHT / HIGH};
    for (int pass = 0; pass < 2; pass++) {
        Beast beast = tide_get_Beast(&world, entity);
        beast.visible = pass == 0 ? a_visible : !a_visible;
        check(tide_set_Beast(&world, entity, beast), "show or hide the folded figure");
        frame_for(0, false);
        tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, 1, points, WIDE * HIGH, pass == 0 ? first : second);
    }
    int count = 0;
    for (int i = 0; i < WIDE * HIGH; i++)
        if (color_distance(first[i], second[i]) > 14) count++;
    return count;
}

// Reception's figure is seen through the fold open on arrival, and isn't there to a look through the bars at it.
static void folded_figure_checks(void)
{
    select_place(2);
    world.Arena.arrive = 10;
    world.Watcher.state = Watching_Away;
    tide_entity figure = {0};
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (tide_has_Beast(&world, entity) && tide_get_Beast(&world, entity).folded && tide_get_Beast(&world, entity).chamber == world.Arena.chamber)
            figure = entity;
    }
    check(figure.index != 0, "Reception has a figure seen only through folds");
    operator_at(world.Arena.op1, (tide_float3){-10, 0, 6}, 0);
    operator_at(world.Arena.op0, (tide_float3){-6.5f, 0, -6.5f}, tide_atan2_f(-5.5f, 3.5f));
    const int through = whole_frame_changes(true, figure);
    capture_scene("folded-figure-through");
    // Straight at its place, with the pair closed: through either end it's there, as it should be
    operator_at(world.Arena.op0, (tide_float3){-2, 0, -5}, tide_atan2_f(11.5f, 4.2f));
    const bool opened[2] = {world.Fold.ends.at[0].open, world.Fold.ends.at[1].open};
    world.Fold.ends.at[0].open = world.Fold.ends.at[1].open = false;
    const int direct = whole_frame_changes(true, figure);
    capture_scene("folded-figure-direct");
    world.Fold.ends.at[0].open = opened[0];
    world.Fold.ends.at[1].open = opened[1];
    printf("Folded figure: %d samples through the fold, %d looking straight at its place\n", through, direct);
    check(through >= 6, "the figure is seen through the fold");
    check(direct == 0, "the figure is not there to a direct look, nor in any shadow");
    select_place(0);
}

#include "zfight_render.c"
#include "tour_render.c"

static int render_checks(void *user, float seconds)
{
    (void)user;
    (void)seconds;
    image empty, occupied, moved, wall;

    prepare(-5);
    record(&empty);
    int spread = 0;
    for (int i = 1; i < empty.count; i++) {
        const int distance = color_distance(empty.pixels[0], empty.pixels[i]);
        if (distance > spread) spread = distance;
    }
    check(spread > 30, "portal interior contains destination geometry rather than a flat fill");
    world.Fold.ends.at[1].open = false;
    record(&wall);
    check(changed(&empty, &wall) >= 40, "a linked opening shows a different image from the solid source wall");

    prepare(-5);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    check(changed(&empty, &occupied) >= 8, "moving the cube beyond the exit updates the portal in the next drawn frame");
    cube_at((tide_float3){-7, 1.55f, 9.4f});
    record(&moved);
    check(changed(&occupied, &moved) >= 8, "a cube moving sideways beyond the exit moves in the portal image");

    prepare(-5);
    operator_at(world.Arena.op1, (tide_float3){-7, 0, 10.6f}, TIDE_PI_F * 0.5f);
    record(&occupied);
    check(changed(&empty, &occupied) >= 8, "the other operator is rendered live beyond the exit");

    // Geometry between the virtual eye and exit plane must be discarded. The cube is further behind the wall
    // than anything part of the way through a fold is, which would also be drawn at the fold's other end.
    prepare(-5);
    cube_at((tide_float3){-13, 1.55f, 10.6f});
    record(&occupied);
    check(changed(&empty, &occupied) <= 4, "the exit plane clips a cube on the camera side of its wall");

    // The cube unfolds to (-5.6, 1.55, 5.4). Its image follows that depth,
    // rather than sliding rigidly with a picture painted on the source wall.
    prepare(-5.5f);
    record(&empty);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    const float left_center = changed_center(&empty, &occupied);
    prepare(-4.5f);
    record(&empty);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    const float right_center = changed_center(&empty, &occupied);
    const float focal = HEIGHT * 0.5f / tide_tan_f(75 * TIDE_DEG2RAD_F * 0.5f);
    const float expected_left = WIDTH * 0.5f + (-5.6f + 5.5f) * focal / 6.9f;
    const float expected_right = WIDTH * 0.5f + (-5.6f + 4.5f) * focal / 6.9f;
    printf("Portal cube projection: left %.1f (%.1f), right %.1f (%.1f)\n", left_center, expected_left, right_center, expected_right);
    check(tide_abs_f(left_center - expected_left) < 22, "left camera agrees with independently projected destination cube");
    check(tide_abs_f(right_center - expected_right) < 22, "right camera agrees with independently projected destination cube");
    check(left_center - right_center > 35 && left_center - right_center < 90,
          "lateral camera movement preserves destination depth parallax");

    // A destination below the blood is opaque red, both directly and inside the other pair: the bench's strip of
    // blood runs from x 2 to 10 and z -3 to -1.
    prepare(-5);
    world.Fold.ends.at[1] = (Portal){.open = true, .at = {6, -2, -2}, .face = {-1, 0, 0}, .up = {0, 1, 0}, .size = 1};
    record(&occupied);
    printf("Submerged portal interior red samples: %d\n", red_interior(&occupied));
    check(red_interior(&occupied) > 90, "a submerged destination shows opaque red instead of dry geometry");
    prepare(-5);
    world.Fold.ends.at[2] = (Portal){.open=true, .at={-7.5f,1.55f,10}, .face={-1,0,0}, .up = {0, 1, 0}, .size=1};
    world.Fold.ends.at[3] = (Portal){.open=true, .at={6,-2,-2}, .face={-1,0,0}, .up = {0, 1, 0}, .size=1};
    record(&occupied);
    printf("Nested submerged portal interior red samples: %d\n", red_interior(&occupied));
    check(red_interior(&occupied) > 55, "a submerged destination remains red through the other operator's fold");
    portal_back_checks();
    exit_sign_checks();
    beast_checks();
    lamp_and_fog_checks();
    transition_checks();
    reticle_checks();
    folded_figure_checks();
    zfight_render_checks();

    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_render_targets_free();
    tide_world_free(&world);
    if (failures) return 1;
    puts("FOLDLINE portal rendering checks passed");
    return 0;
}

int main(void)
{
    check(tide_assets_load(tide_game_pack, tide_game_pack_size), "load renderer fixture assets");
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_tick(&world);
    select_place(0); // The bench, where the portal checks are made
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
#ifndef __wasm__
    if (getenv("FOLDLINE_CAPTURES"))
        tide_platform_open_headless(&(tide_headless_desc){.width = WIDTH, .height = HEIGHT, .frames = 1});
    else
#endif
        tide_platform_open(&(tide_window_desc){.title = "FOLDLINE portal rendering", .width = WIDTH, .height = HEIGHT, .hidden = true});
#ifndef __wasm__
    if (getenv("FOLDLINE_TOUR") && getenv("FOLDLINE_CAPTURES")) tide_platform_run(tour, NULL);
#endif
    tide_platform_run(render_checks, NULL);
}
