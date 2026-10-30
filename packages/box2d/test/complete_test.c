#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "complete.h"
#include "tide_test.h"

void b2t_check(bool enabled, bool whenever);
void b2t_forget(void);

_Static_assert(sizeof(Box2D_Joint) == 88 && offsetof(Box2D_Joint, id) == 56 && sizeof(Box2D_Physics) == 440 &&
    sizeof(Box2D_Collider) == 152 && sizeof(Box2D_Hull) == 88 && sizeof(Box2D_FixtureState) == 32,
    "native package records match generated layouts");

static tide_entity at(tide_list list, int index)
{
    return *(const tide_entity *)tide_list_at(list, index, sizeof(tide_entity));
}
static void tick(tide_world *w, int count) { for (int i = 0; i < count; i++) tide_world_tick(w); }
static void start(tide_world *w)
{
    b2t_forget(); b2t_check(true, true);
    tide_world_init(w, 1.0f / 60.0f);
    tick(w, 2);
    TIDE_REQUIRE(tide_map_count(w->Box2D_Physics.joints) == 9);
    for (int i = 0; i < 9; i++) TIDE_CHECK(tide_get_Box2D_Joint(w, at(w->Objects.joints, i)).id != 0);
}

TIDE_TEST(box2d_all_joint_types_constrain_and_drive)
{
    tide_world w = {0}; start(&w); tick(&w, 120);
    const Box2D_Body distance = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 0));
    const Box2D_Body motor = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 1));
    const Box2D_Body filter = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 2));
    const Box2D_Body mover = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 3));
    const Box2D_Body slider = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 4));
    const Box2D_Body hinge = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 5));
    const Box2D_Body pogo = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 6));
    const Box2D_Body weld = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 7));
    const Box2D_Body wheel = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 8));
    TIDE_CHECK(fabsf(distance.position.y - 4) < 0.02f);
    TIDE_CHECK(fabsf(motor.position.y - 4) < 0.03f);
    TIDE_CHECK(filter.position.y < -10);
    TIDE_CHECK(mover.position.x > 31.9f && mover.position.y > 3.9f);
    TIDE_CHECK(slider.position.x > 40.3f && slider.position.x < 40.52f && fabsf(slider.position.y - 4) < 0.02f);
    TIDE_CHECK(fabsf(hinge.position.y - 4) < 0.02f && fabsf(hinge.spin - 2) < 0.02f);
    TIDE_CHECK(pogo.position.y > 3.5f);
    TIDE_CHECK(fabsf(weld.position.y - 4) < 0.02f && fabsf(weld.spin) < 0.02f);
    TIDE_CHECK(fabsf(wheel.position.y - 4) < 0.1f && fabsf(wheel.spin - 2) < 0.02f);
    const Box2D_Joint load = tide_get_Box2D_Joint(&w, at(w.Objects.joints, 7));
    TIDE_CHECK(isfinite(load.force.y) && fabsf(load.force.y) > 0.1f);
    tide_world_free(&w);
}

TIDE_TEST(box2d_new_shapes_and_filtered_live_historical_queries)
{
    tide_world w = {0}; start(&w);
    printf("  queries: rays %d casts %d overlaps %d history %d filters %d triangle %.9g\n", w.Observed.rays, w.Observed.casts,
        w.Observed.overlaps, w.Observed.historical, w.Observed.filtered, (double)w.Observed.triangleFraction);
    TIDE_CHECK(w.Observed.rays == 6 && w.Observed.casts == 18 && w.Observed.overlaps == 18);
    TIDE_CHECK(w.Observed.historical == 24 && w.Observed.filtered == 6);
    TIDE_CHECK(fabsf(w.Observed.triangleFraction - 0.375f) < 0.0001f);
    TIDE_CHECK(w.Observed.inside == 1 && w.Observed.historicalInside == 1);
    TIDE_CHECK(!w.Observed.chainBack && !w.Observed.historicalChainBack);
    tide_world_free(&w);
}

TIDE_TEST(box2d_joint_live_settings_disable_and_component_removal)
{
    tide_world w = {0}; start(&w);
    const tide_entity hinge = at(w.Objects.joints, 5), moving = at(w.Objects.bodies, 5);
    Box2D_Joint joint = tide_get_Box2D_Joint(&w, hinge);
    const int64_t handle = joint.id;
    Box2D_RevoluteJoint settings = tide_get_Box2D_RevoluteJoint(&w, hinge);
    settings.motorSpeed = -3; tide_set_Box2D_RevoluteJoint(&w, hinge, settings); tick(&w, 10);
    TIDE_CHECK(tide_get_Box2D_Joint(&w, hinge).id == handle);
    TIDE_CHECK(fabsf(tide_get_Box2D_Body(&w, moving).spin + 3) < 0.02f);
    joint = tide_get_Box2D_Joint(&w, hinge); joint.enabled = false; tide_set_Box2D_Joint(&w, hinge, joint); tick(&w, 1);
    TIDE_CHECK(tide_has_Box2D_Joint(&w, hinge) && tide_get_Box2D_Joint(&w, hinge).id == 0);
    joint = tide_get_Box2D_Joint(&w, hinge); joint.enabled = true; tide_set_Box2D_Joint(&w, hinge, joint); tick(&w, 1);
    TIDE_CHECK(tide_get_Box2D_Joint(&w, hinge).id != 0 && tide_get_Box2D_Joint(&w, hinge).id != handle);
    w.Request.removeJoint = hinge; tick(&w, 2);
    TIDE_CHECK(tide_map_count(w.Box2D_Physics.joints) == 8);
    const tide_entity orphan = at(w.Objects.joints, 6);
    joint = tide_get_Box2D_Joint(&w, orphan); w.Request.removeBody = joint.b; tick(&w, 2);
    TIDE_CHECK(!tide_has_Box2D_Joint(&w, orphan) && !tide_has_Box2D_Body(&w, joint.b) && tide_has_Moving(&w, joint.b));
    tide_world_free(&w);
}

TIDE_TEST(box2d_shape_component_removal_replacement_and_parts)
{
    for (int kind = 0; kind < 6; kind++) {
        tide_world w = {0}; start(&w);
        w.Request.removeShape = at(w.Objects.shapes, kind); w.Request.shapeKind = kind; tick(&w, 2);
        TIDE_CHECK(w.Observed.rays == 5 && w.Observed.filtered == 5);
        TIDE_CHECK(tide_has_Box2D_Body(&w, at(w.Objects.shapes, kind)));
        tide_world_free(&w);
    }
    tide_world w = {0}; start(&w);
    const tide_entity target = at(w.Objects.shapes, 0);
    Box2D_Box box = tide_get_Box2D_Box(&w, target); box.size.x = 2; box.sensor = true; box.density = 2;
    tide_set_Box2D_Box(&w, target, box); tick(&w, 1);
    TIDE_CHECK(w.Observed.rays == 5 && w.Observed.filtered == 5);
    box.sensor = false; tide_set_Box2D_Box(&w, target, box); tick(&w, 1); TIDE_CHECK(w.Observed.rays == 6);
    Box2D_Body body = tide_get_Box2D_Body(&w, target); const int64_t old = body.id; body.id = 0;
    tide_set_Box2D_Body(&w, target, body); tick(&w, 1);
    TIDE_CHECK(tide_get_Box2D_Body(&w, target).id != old && w.Observed.rays == 6);
    w.Request.removePart = w.Objects.part; tick(&w, 2);
    TIDE_CHECK(!tide_has_Box2D_Part(&w, w.Objects.part) && tide_has_Box2D_Capsule(&w, w.Objects.part));
    tide_world_free(&w);
}

TIDE_TEST(box2d_complete_snapshot_pack_restore_replay)
{
    tide_world w = {0}, saved = {0}, restored = {0}; start(&w); tick(&w, 30); tide_world_copy(&saved, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0); uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&w, bytes, size) == size);
    b2t_forget(); TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size)); free(bytes);
    uint64_t sequence = 0;
    for (int i = 0; i < 90; i++) {
        tide_world_tick(&w); const uint64_t hash = tide_world_hash(&w); sequence = tide_hash_more(sequence, &hash, sizeof hash);
        b2t_forget(); tide_world_tick(&restored); TIDE_CHECK(tide_world_hash(&restored) == hash);
    }
    printf("  complete sequence: %016llx\n", (unsigned long long)sequence);
    TIDE_CHECK(sequence == UINT64_C(0xfd656e37c1406061));
    tick(&saved, 90); TIDE_CHECK(tide_world_hash(&saved) == tide_world_hash(&w));
    tide_world_free(&restored); tide_world_free(&saved); tide_world_free(&w);
}

TIDE_TEST(box2d_joints_reject_invalid_game_values)
{
    for (int kind = 0; kind < 5; kind++) {
        tide_world w = {0}; start(&w); const tide_entity e = at(w.Objects.joints, 5);
        Box2D_Joint joint = tide_get_Box2D_Joint(&w, e);
        Box2D_RevoluteJoint settings = tide_get_Box2D_RevoluteJoint(&w, e);
        if (kind == 0) joint.anchorA.x = NAN;
        if (kind == 1) joint.a = joint.b;
        if (kind == 2) joint.constraintHertz = -1;
        if (kind == 3) settings.lowerAngle = 1, settings.upperAngle = -1;
        if (kind == 4) settings.maxMotorTorque = INFINITY;
        tide_set_Box2D_Joint(&w, e, joint); tide_set_Box2D_RevoluteJoint(&w, e, settings); tick(&w, 2);
        TIDE_CHECK(!tide_has_Box2D_Joint(&w, e)); tide_world_free(&w);
    }
}

TIDE_TEST(box2d_collision_masks_groups_and_sensor_filter_edits)
{
    tide_world w = {0}; start(&w); tick(&w, 60);
    for (int i = 0; i < 3; i++) {
        const Box2D_Body a = tide_get_Box2D_Body(&w, at(w.Objects.filters, i * 2));
        const Box2D_Body b = tide_get_Box2D_Body(&w, at(w.Objects.filters, i * 2 + 1));
        TIDE_CHECK(i == 1 ? b.position.x - a.position.x > 0.98f : fabsf(b.position.x - a.position.x - 0.2f) < 0.001f);
    }
    Box2D_CollisionFilter filter = {.category = 1, .mask = 0};
    tide_set_Box2D_CollisionFilter(&w, w.Objects.sensor, filter); tick(&w, 2);
    TIDE_CHECK(w.Observed.inside == 0 && w.Observed.historicalInside == 0);
    filter.mask = -1; tide_set_Box2D_CollisionFilter(&w, w.Objects.sensor, filter); tick(&w, 2);
    TIDE_CHECK(w.Observed.inside == 1 && w.Observed.historicalInside == 1);
    tide_world_free(&w);
}

TIDE_TEST(box2d_body_settings_follow_writes_and_native_restore)
{
    tide_world w = {0}, restored = {0}; start(&w);
    const tide_entity e = at(w.Objects.bodies, 2);
    Box2D_Body body = tide_get_Box2D_Body(&w, e);
    body.gravityScale = 0; body.linearDamping = 1; body.angularDamping = 2; body.sleepSpeed = 0.1f;
    body.velocity = (tide_float2){2, 0}; body.spin = 2; body.fixedRotation = true; body.bullet = true;
    const float y = body.position.y; tide_set_Box2D_Body(&w, e, body); tide_world_copy(&restored, &w);
    for (int i = 0; i < 30; i++) {
        tide_world_tick(&w); b2t_forget(); tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&restored));
    }
    body = tide_get_Box2D_Body(&w, e);
    TIDE_CHECK(fabsf(body.position.y - y) < 0.001f && body.velocity.x < 1.5f && body.velocity.x > 1);
    TIDE_CHECK(body.spin == 0);
    tide_world_free(&restored); tide_world_free(&w);
}

TIDE_TEST(box2d_invalid_geometry_removes_the_fixture_without_asserting)
{
    for (int kind = 0; kind < 6; kind++) {
        tide_world w = {0}; start(&w); const tide_entity e = at(w.Objects.shapes, kind);
        if (kind == 0) { Box2D_Box s = tide_get_Box2D_Box(&w, e); s.size.x = NAN; tide_set_Box2D_Box(&w, e, s); }
        if (kind == 1) { Box2D_Circle s = tide_get_Box2D_Circle(&w, e); s.radius = -1; tide_set_Box2D_Circle(&w, e, s); }
        if (kind == 2) { Box2D_Capsule s = tide_get_Box2D_Capsule(&w, e); s.a.x = INFINITY; tide_set_Box2D_Capsule(&w, e, s); }
        if (kind == 3) { Box2D_Polygon s = tide_get_Box2D_Polygon(&w, e); s.radius = -1; tide_set_Box2D_Polygon(&w, e, s); }
        if (kind == 4) { Box2D_Segment s = tide_get_Box2D_Segment(&w, e); s.b = s.a; tide_set_Box2D_Segment(&w, e, s); }
        if (kind == 5) { Box2D_Chain s = tide_get_Box2D_Chain(&w, e); s.friction = NAN; tide_set_Box2D_Chain(&w, e, s); }
        tick(&w, 2); TIDE_CHECK(w.Observed.rays == 5); tide_world_free(&w);
    }
}

static void edit_all_joints(tide_world *w)
{
#define EDIT(Type, index, edits) do { \
    const tide_entity e = at(w->Objects.joints, index); \
    Box2D_##Type s = tide_get_Box2D_##Type(w, e); \
    edits; \
    tide_set_Box2D_##Type(w, e, s); \
} while (0)
    EDIT(DistanceJoint, 0, s.enableSpring=true; s.hertz=4; s.dampingRatio=1; s.lowerSpringForce=-10; s.upperSpringForce=10;
        s.enableLimit=true; s.minLength=0.5f; s.maxLength=1.5f; s.enableMotor=true; s.maxMotorForce=2; s.motorSpeed=-0.2f);
    EDIT(MotorJoint, 1, s.linearVelocity.x=0.2f; s.angularVelocity=0.3f; s.maxVelocityForce=1; s.maxVelocityTorque=1;
        s.linearHertz=12; s.angularHertz=12; s.linearDampingRatio=0.7f; s.angularDampingRatio=0.8f);
    EDIT(FilterJoint, 2, s.enabled=false);
    EDIT(MoverJoint, 3, s.linearVelocity.x=-1; s.maxVelocityForce.x=2; s.maxVelocityForce.y=2);
    EDIT(PrismaticJoint, 4, s.enableSpring=true; s.hertz=3; s.dampingRatio=1; s.targetTranslation=0.1f;
        s.lowerTranslation=-0.25f; s.upperTranslation=0.25f; s.motorSpeed=-1);
    EDIT(RevoluteJoint, 5, s.enableSpring=true; s.targetAngle=0.3f; s.hertz=4; s.dampingRatio=1;
        s.enableLimit=true; s.lowerAngle=-0.5f; s.upperAngle=0.5f; s.motorSpeed=-2);
    EDIT(PogoJoint, 6, s.hertz=12; s.dampingRatio=1; s.restLength=0.2f; s.maxTensionForce=3; s.maxCompressionForce=20);
    EDIT(WeldJoint, 7, s.linearHertz=10; s.angularHertz=12; s.linearDampingRatio=1; s.angularDampingRatio=1);
    EDIT(WheelJoint, 8, s.hertz=5; s.dampingRatio=1; s.enableLimit=true; s.lowerTranslation=-0.25f;
        s.upperTranslation=0.25f; s.motorSpeed=-2; s.maxMotorTorque=2);
#undef EDIT
}

TIDE_TEST(box2d_every_joint_settings_family_replays_after_live_edits)
{
    tide_world w = {0}, restored = {0}; start(&w); tick(&w, 20); tide_world_copy(&restored, &w);
    int64_t handles[9];
    for (int i = 0; i < 9; i++) handles[i] = tide_get_Box2D_Joint(&w, at(w.Objects.joints, i)).id;
    edit_all_joints(&w); edit_all_joints(&restored);
    for (int tick_index = 0; tick_index < 30; tick_index++) {
        tide_world_tick(&w); b2t_forget(); tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&restored));
    }
    for (int i = 0; i < 9; i++) {
        const Box2D_Joint joint = tide_get_Box2D_Joint(&w, at(w.Objects.joints, i));
        TIDE_CHECK(isfinite(joint.force.x) && isfinite(joint.force.y) && isfinite(joint.torque));
        if (i == 2) TIDE_CHECK(joint.id == 0);
        else if (i == 6) TIDE_CHECK(joint.id != 0 && joint.id != handles[i]);
        else TIDE_CHECK(joint.id == handles[i]);
    }
    const Box2D_Body mover = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 3));
    const Box2D_Body slider = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 4));
    const Box2D_Body wheel = tide_get_Box2D_Body(&w, at(w.Objects.bodies, 8));
    TIDE_CHECK(mover.velocity.x < -0.9f);
    TIDE_CHECK(slider.position.x >= 39.74f && slider.position.x <= 40.26f);
    TIDE_CHECK(fabsf(wheel.spin + 2) < 0.02f);
    tide_world_free(&restored); tide_world_free(&w);
}

TIDE_TEST(box2d_joint_types_are_part_of_the_native_world_stamp)
{
    tide_world slider = {0}, hinge = {0}; start(&slider); tide_world_copy(&hinge, &slider);
    const tide_entity e = at(slider.Objects.joints, 0);
    slider.Request.replaceJointType = hinge.Request.replaceJointType = e;
    slider.Request.jointType = 1; hinge.Request.jointType = 2;
    for (int i = 0; i < 2; i++) { tide_world_tick(&slider); tide_world_tick(&hinge); }
    TIDE_CHECK(tide_has_Box2D_PrismaticJoint(&slider, e) && tide_has_Box2D_RevoluteJoint(&hinge, e));
    TIDE_CHECK(slider.Box2D_Physics.stamp != hinge.Box2D_Physics.stamp);
    tide_world_free(&hinge); tide_world_free(&slider);
}
