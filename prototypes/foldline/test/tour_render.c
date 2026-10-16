// A review tour: pictures of real places from exact eyes, for looking at a
// level the way a player stands in it. FOLDLINE_TOUR names a file of lines,
// FOLDLINE_CAPTURES the folder the pictures go to. Desktop only; the ordinary
// test run never reads either.
//
//   place <id>                  build a place (2 is Reception) and open its door
//   tick <n>                    run the match n ticks
//   beasts                      print every creature: kind, chamber, place, and whether it's hidden or folded
//   ride                        put both in the exit lift and ride it to the next place, drawing on the way
//   signals <mask>              force the signal bits (and the gate, when the exit's are on)
//   b <x> <y> <z> <yaw>         stand operator B there (yaw in degrees, 0 looks along +z)
//   stand <0|1> <x> <y> <z>     put A's or B's physical body there, to weigh plates as the next ticks run
//   eye <x> <y> <z> <yaw> <pitch> <name>   stand A there, look, and write <name>.png; prints whether A's first
//                                          end could be placed where the reticle is (name "-" writes no picture)
//   look <x> <y> <z> <tx> <ty> <tz> <name> stand A at feet x y z looking at a point
//   shoot <0|1> <x> <y> <z> <yaw> <pitch>  stand A there and fire its first or second end, as a player's click does
//   paper <x> <y> <z>           make the box of the room that holds that point take folds, a floor's or a ceiling's too
//   lift <right> <up> <forward> <turn> <pitch> <name>  stand A in the arrival lift's own frame (forward is out of
//                                                      its doorway, which is 2.1 m ahead of its middle) and look
#ifndef __wasm__
static void tour_eye(const tide_float3 feet, const float yaw, const float pitch)
{
    operator_at(world.Arena.op0, feet, yaw);
    Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    viewer.pitch = pitch;
    check(tide_set_Operator(&world, world.Arena.op0, viewer), "stand the tour's eye");
    frame_for(0, false);
    // What A's first button would do from here, as the reticle shows it
    const FoldShot shot = local.Interface.shot0;
    if (!shot.hit) printf("  aim: no surface\n");
    else if (!shot.valid) printf("  aim: hits %.2f %.2f %.2f, no fold fits there\n", shot.point.x, shot.point.y, shot.point.z);
    else printf("  aim: a fold fits, middle %.2f %.2f %.2f facing %.0f %.0f %.0f, size %.2f\n", shot.portal.at.x, shot.portal.at.y,
                shot.portal.at.z, shot.portal.face.x, shot.portal.face.y, shot.portal.face.z, shot.portal.size);
}

static int tour(void *user, float seconds)
{
    (void)user;
    (void)seconds;
    FILE *file = fopen(getenv("FOLDLINE_TOUR"), "r");
    if (!file) {
        fprintf(stderr, "FOLDLINE tour: cannot open %s\n", getenv("FOLDLINE_TOUR"));
        return 1;
    }
    char line[512];
    int number = 0;
    while (fgets(line, sizeof line, file)) {
        number++;
        char word[32] = {0}, name[128] = {0};
        float v[6] = {0};
        if (sscanf(line, "%31s", word) != 1 || word[0] == '#') continue;
        if (!strcmp(word, "place") && sscanf(line, "%*s %f", &v[0]) == 1) {
            select_place((int)v[0]);
            world.Watcher.state = Watching_Away;
            for (int tick = 0; tick < 90; tick++) tide_world_tick(&world);
        } else if (!strcmp(word, "beasts")) {
            printf("  chamber %d, arrived %.1f s ago\n", world.Arena.chamber, world.Arena.arrive);
            for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
                const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
                if (!tide_has_Beast(&world, entity)) continue;
                const Beast beast = tide_get_Beast(&world, entity);
                printf("  beast %d (chamber %d) at %.2f %.2f %.2f%s%s\n", (int)beast.kind, beast.chamber, beast.at.x, beast.at.y,
                       beast.at.z, beast.visible ? "" : ", hidden", beast.folded ? ", folded" : "");
            }
        } else if (!strcmp(word, "tick") && sscanf(line, "%*s %f", &v[0]) == 1) {
            for (int tick = 0; tick < (int)v[0]; tick++) tide_world_tick(&world);
        } else if (!strcmp(word, "signals") && sscanf(line, "%*s %f", &v[0]) == 1) {
            world.Arena.signals = (uint32_t)v[0];
        } else if (!strcmp(word, "b") && sscanf(line, "%*s %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) {
            operator_at(world.Arena.op1, (tide_float3){v[0], v[1], v[2]}, v[3] * TIDE_DEG2RAD_F);
        } else if (!strcmp(word, "ride")) {
            const Place left = world.Place;
            const int chamber = world.Arena.chamber;
            world.Arena.arrive = left.kind == PlaceKind_Vignette ? left.stay + 1 : 2;
            world.Arena.gate = 0;
            world.Arena.ride = 0;
            if (left.kind == PlaceKind_Vignette) world.Arena.door = 0;
            body_at(world.Arena.op0, lift_point(left.exit, left.exitYaw, (tide_float3){.8f, 0, -.2f}));
            body_at(world.Arena.op1, lift_point(left.exit, left.exitYaw, (tide_float3){-1.2f, 0, -.2f}));
            for (int tick = 0; tick < 10; tick++) tide_world_tick(&world);
            // Every frame is drawn half-way between two ticks, as a player's are
            static tide_world previous;
            world.Arena.ride = 1.3f - 1.0f / 60;
            for (int tick = 0; tick < 123; tick++) {
                tide_world_copy(&previous, &world);
                tide_world_tick(&world);
                presented_previous = &previous;
                presented_alpha = .5f;
                frame_for(0, false);
                presented_previous = NULL;
                presented_alpha = 1;
                if (world.Arena.chamber == chamber && tick > 3) break;
            }
        } else if (!strcmp(word, "stand") && sscanf(line, "%*s %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4) {
            body_at((int)v[0] ? world.Arena.op1 : world.Arena.op0, (tide_float3){v[1], v[2], v[3]});
        } else if (!strcmp(word, "eye") && sscanf(line, "%*s %f %f %f %f %f %127s", &v[0], &v[1], &v[2], &v[3], &v[4], name) == 6) {
            tour_eye((tide_float3){v[0], v[1], v[2]}, v[3] * TIDE_DEG2RAD_F, v[4] * TIDE_DEG2RAD_F);
            if (strcmp(name, "-")) capture_scene(name);
        } else if (!strcmp(word, "lift") && sscanf(line, "%*s %f %f %f %f %f %127s", &v[0], &v[1], &v[2], &v[3], &v[4], name) == 6) {
            const tide_float3 feet = lift_point(world.Place.arrival, world.Place.arrivalYaw, (tide_float3){v[0], v[1], v[2]});
            tour_eye(feet, world.Place.arrivalYaw + v[3] * TIDE_DEG2RAD_F, v[4] * TIDE_DEG2RAD_F);
            printf("%s: eye at %.2f %.2f %.2f yaw %.0f\n", name, feet.x, feet.y, feet.z,
                   (world.Place.arrivalYaw + v[3] * TIDE_DEG2RAD_F) * TIDE_RAD2DEG_F);
            if (strcmp(name, "-")) capture_scene(name);
        } else if (!strcmp(word, "paper") && sscanf(line, "%*s %f %f %f", &v[0], &v[1], &v[2]) == 3) {
            bool found = false;
            for (uint32_t slot = 0; slot < world.entities.next_unused && !found; slot++) {
                const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
                if (!tide_has_Wall(&world, entity)) continue;
                Wall wall = tide_get_Wall(&world, entity);
                if (wall.chamber != world.Arena.chamber || tide_abs_f(v[0] - wall.center.x) > wall.size.x * .5f
                    || tide_abs_f(v[1] - wall.center.y) > wall.size.y * .5f || tide_abs_f(v[2] - wall.center.z) > wall.size.z * .5f) continue;
                wall.takes = true;
                found = tide_set_Wall(&world, entity, wall);
            }
            check(found, "the tour's box to paper is there");
        } else if (!strcmp(word, "shoot") && sscanf(line, "%*s %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            // The match shoots from the physical body, so that's what's moved
            body_at(world.Arena.op0, (tide_float3){v[1], v[2], v[3]});
            Operator shooter = tide_get_Operator(&world, world.Arena.op0);
            shooter.yaw = v[4] * TIDE_DEG2RAD_F;
            shooter.pitch = v[5] * TIDE_DEG2RAD_F;
            check(tide_set_Operator(&world, world.Arena.op0, shooter), "stand the tour's shooter");
            tide_world_tick(&world);
            const PilotInput fire = {.amber = v[0] == 0, .cyan = v[0] != 0};
            tide_world_set_input(&world, tide_player_from_index(0), fire);
            tide_world_set_server_input(&world, fire);
            tide_world_tick(&world);
            tide_world_set_input(&world, tide_player_from_index(0), (PilotInput){0});
            tide_world_set_server_input(&world, (PilotInput){0});
            for (int tick = 0; tick < 3; tick++) tide_world_tick(&world);
            const Portal end = world.Fold.ends.at[(int)v[0]];
            printf("  shot: end %d %s at %.2f %.2f %.2f\n", (int)v[0], end.open ? "open" : "not open", end.at.x, end.at.y, end.at.z);
        } else if (!strcmp(word, "look") && sscanf(line, "%*s %f %f %f %f %f %f %127s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], name) == 7) {
            const float dx = v[3] - v[0], dz = v[5] - v[2];
            tour_eye((tide_float3){v[0], v[1], v[2]}, tide_atan2_f(dx, dz),
                     tide_atan2_f(v[4] - v[1] - 1.55f, tide_sqrt_f(dx * dx + dz * dz)));
            if (strcmp(name, "-")) capture_scene(name);
        } else {
            fprintf(stderr, "FOLDLINE tour line %d not understood: %s", number, line);
            failures++;
        }
    }
    fclose(file);
    tide_draw_forget(&draw);
    return failures ? 1 : 0;
}
#endif
