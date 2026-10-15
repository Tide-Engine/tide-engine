// Inspect the actual generated dressing, rather than repeating the formulas
// that build it. Coplanar pieces must meet at edges and never share an area.
static bool seam_close(const float a, const float b)
{
    return tide_abs_f(a - b) < .0001f;
}

static bool seam_tint(const Fixture *fixture, const float r, const float g, const float b)
{
    return seam_close(fixture->tint.r, r) && seam_close(fixture->tint.g, g) && seam_close(fixture->tint.b, b);
}

static int seam_family(const Fixture *fixture)
{
    if (seam_tint(fixture, .16f, .17f, .17f) &&
        (fixture->size.y > 3 || (seam_close(fixture->size.y, .14f) && (fixture->size.x > 6 || fixture->size.z > 6)))) return 1;
    if (seam_close(fixture->luminous, .6f) &&
        (seam_tint(fixture, .95f, .9f, .5f) || seam_tint(fixture, .95f, .5f, .35f))) return 2;
    if (seam_tint(fixture, .95f, .72f, .08f) && seam_close(fixture->luminous, .3f)) return 3;
    if (seam_tint(fixture, .2f, .21f, .21f) && seam_close(fixture->size.y, .014f)) return 4;
    return 0;
}

static void seam_bounds(const Fixture *fixture, float low[3], float high[3])
{
    const tide_float4x4 *m = &fixture->transform;
    const float center[3] = {m->c3.x, m->c3.y, m->c3.z};
    const float half[3] = {
        (tide_abs_f(m->c0.x) + tide_abs_f(m->c1.x) + tide_abs_f(m->c2.x)) * .5f,
        (tide_abs_f(m->c0.y) + tide_abs_f(m->c1.y) + tide_abs_f(m->c2.y)) * .5f,
        (tide_abs_f(m->c0.z) + tide_abs_f(m->c1.z) + tide_abs_f(m->c2.z)) * .5f
    };
    for (int axis = 0; axis < 3; axis++) {
        low[axis] = center[axis] - half[axis];
        high[axis] = center[axis] + half[axis];
    }
}

static bool seam_coplanar_overlap(const Fixture *first, const Fixture *second)
{
    float a0[3], a1[3], b0[3], b1[3];
    seam_bounds(first, a0, a1);
    seam_bounds(second, b0, b1);
    for (int normal = 0; normal < 3; normal++) {
        // Opposite-facing faces that only touch are ordinary solid joints.
        if (!seam_close(a0[normal], b0[normal]) && !seam_close(a1[normal], b1[normal])) continue;
        bool area = true;
        for (int axis = 0; axis < 3; axis++) {
            if (axis == normal) continue;
            const float low = a0[axis] > b0[axis] ? a0[axis] : b0[axis];
            const float high = a1[axis] < b1[axis] ? a1[axis] : b1[axis];
            if (high - low <= .0001f) area = false;
        }
        if (area) return true;
    }
    return false;
}

static void seam_list_checks(const tide_list fixtures, int seen[5])
{
    const int count = tide_list_count(fixtures);
    for (int first = 0; first < count; first++) {
        const Fixture *a = tide_list_at(fixtures, first, sizeof *a);
        const int family = seam_family(a);
        if (!family) continue;
        seen[family]++;
        for (int second = first + 1; second < count; second++) {
            const Fixture *b = tide_list_at(fixtures, second, sizeof *b);
            if (seam_family(b) != family) continue;
            if (seam_coplanar_overlap(a, b)) {
                fprintf(stderr, "Foldline coplanar dressing: map %d family %d pieces %d/%d\n", world.Place.id, family, first, second);
                check(false, "shared trim, frame and warning-band faces never overlap coplanarly");
            }
        }
    }
}

#ifndef __wasm__
static void seam_camera(const tide_float3 feet, const tide_float3 target)
{
    const float dx = target.x - feet.x, dz = target.z - feet.z;
    operator_at(world.Arena.op0, feet, tide_atan2_f(dx, dz));
    Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    viewer.pitch = tide_atan2_f(target.y - feet.y - 1.55f, tide_sqrt_f(dx * dx + dz * dz));
    check(tide_set_Operator(&world, world.Arena.op0, viewer), "look along the lift lining's oblique seam");
    frame_for(0, false);
}
#endif

static void zfight_render_checks(void)
{
    int seen[5] = {0};
    for (int id = 2; id < 16; id++) {
        select_place(id);
        world.Arena.arrive = 2;
        frame_for(0, false);
        tide_text_use(NULL, &local.heap);
        seam_list_checks(local.Optics.furnishings, seen);
        seam_list_checks(local.Optics.dynamic, seen);
    }
    for (int family = 1; family <= 4; family++)
        check(seen[family] > 0, "all four repaired dressing families are exercised by the campaign");
#ifndef __wasm__
    if (getenv("FOLDLINE_CAPTURES")) {
        for (int id = 2; id < 16; id++) {
            select_place(id);
            world.Arena.arrive = 2;
            world.Arena.door = 1;
            operator_at(world.Arena.op0,
                        lift_point(world.Place.arrival, world.Place.arrivalYaw, (tide_float3){0, 0, -1.2f}), world.Place.arrivalYaw);
            Operator viewer = tide_get_Operator(&world, world.Arena.op0);
            viewer.pitch = .08f;
            check(tide_set_Operator(&world, world.Arena.op0, viewer), "frame each campaign arrival lining");
            frame_for(0, false);
            char name[40];
            snprintf(name, sizeof name, "arrival-stop-%02d", id - 1);
            capture_scene(name);
        }
        select_place(2);
        world.Arena.arrive = 2;
        world.Arena.door = 1;
        world.Arena.gate = 1;
        world.Arena.signals = 1 << 1;
        const tide_float3 target = lift_point(world.Place.arrival, world.Place.arrivalYaw, (tide_float3){-3, 1, 1.3f});
        for (int frame = 0; frame < 3; frame++) {
            const tide_float3 feet = lift_point(world.Place.arrival, world.Place.arrivalYaw, (tide_float3){-1.9f + frame * .04f, 0, .2f});
            seam_camera(feet, target);
            capture_scene(frame == 0 ? "lift-arrival-seam-0" : frame == 1 ? "lift-arrival-seam-1" : "lift-arrival-seam-2");
        }
        seam_camera(lift_point(world.Place.exit, world.Place.exitYaw, (tide_float3){2.2f, 0, 3.8f}),
                    lift_point(world.Place.exit, world.Place.exitYaw, (tide_float3){3, 1.1f, 1.5f}));
        capture_scene("lift-exit-seam-oblique");
    }
#endif
}
