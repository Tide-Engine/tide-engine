#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "joints.h"
#include "tide_test.h"

void b3t_check(bool enabled, bool whenever);
void b3t_forget(void);

_Static_assert(sizeof(Box3D_Joint) == 120 && offsetof(Box3D_Joint, id) == 88 &&
               offsetof(Box3D_Joint, force) == 96 && sizeof(Box3D_Physics) == 376,
               "joint native layout must match generated layout");

static tide_entity at(tide_list list, int index)
{
    return *(const tide_entity *)tide_list_at(list, index, sizeof(tide_entity));
}
static void start(tide_world *w)
{
    b3t_check(true, true);
    tide_world_init(w, 1.0f / 60.0f);
    tide_world_tick(w);
    tide_world_tick(w);
    TIDE_REQUIRE(tide_map_count(w->Box3D_Physics.joints) == 9);
    for (int i = 0; i < 9; i++) TIDE_CHECK(tide_get_Box3D_Joint(w, at(w->Objects.joints, i)).id != 0);
}

TIDE_TEST(box3d_joint_types_constrain_and_drive_bodies)
{
    tide_world w = {0};
    start(&w);
    for (int i = 0; i < 120; i++) tide_world_tick(&w);
    const Box3D_Body distance = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 0));
    const Box3D_Body motor = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 1));
    const Box3D_Body filter = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 2));
    const Box3D_Body parallel = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 3));
    const Box3D_Body slider = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 4));
    const Box3D_Body hinge = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 5));
    const Box3D_Body ball = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 6));
    const Box3D_Body weld = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 7));
    const Box3D_Body wheel = tide_get_Box3D_Body(&w, at(w.Objects.bodies, 8));
    TIDE_CHECK(fabsf(distance.position.y - 4) < 0.02f);
    TIDE_CHECK(fabsf(motor.position.y - 4) < 0.03f);
    TIDE_CHECK(filter.position.y < -10 && parallel.position.y < -10);
    TIDE_CHECK(slider.position.x > 40.3f && slider.position.x < 40.52f && fabsf(slider.position.y - 4) < 0.02f);
    TIDE_CHECK(fabsf(hinge.position.y - 4) < 0.02f && fabsf(hinge.angularVelocity.z - 2) < 0.02f);
    TIDE_CHECK(fabsf(ball.position.y - 4) < 0.02f);
    TIDE_CHECK(fabsf(weld.position.y - 4) < 0.02f && fabsf(weld.angularVelocity.z) < 0.02f);
    TIDE_CHECK(fabsf(wheel.position.y - 4) < 0.02f && fabsf(wheel.angularVelocity.z - 2) < 0.02f);
    const Box3D_Joint load = tide_get_Box3D_Joint(&w, at(w.Objects.joints, 7));
    TIDE_CHECK(isfinite(load.force.y) && fabsf(load.force.y) > 0.1f);
    tide_world_free(&w);
}

TIDE_TEST(box3d_joint_live_settings_sleep_disable_and_lifecycle)
{
    tide_world w = {0};
    start(&w);
    const tide_entity hinge = at(w.Objects.joints, 5), moving = at(w.Objects.bodies, 5);
    Box3D_Joint joint = tide_get_Box3D_Joint(&w, hinge);
    const int64_t handle = joint.id;
    Box3D_RevoluteJoint settings = tide_get_Box3D_RevoluteJoint(&w, hinge);
    settings.motorSpeed = -3;
    tide_set_Box3D_RevoluteJoint(&w, hinge, settings);
    for (int i = 0; i < 10; i++) tide_world_tick(&w);
    TIDE_CHECK(tide_get_Box3D_Joint(&w, hinge).id == handle);
    TIDE_CHECK(fabsf(tide_get_Box3D_Body(&w, moving).angularVelocity.z + 3) < 0.02f);
    joint = tide_get_Box3D_Joint(&w, hinge);
    joint.enabled = false;
    tide_set_Box3D_Joint(&w, hinge, joint);
    tide_world_tick(&w);
    TIDE_CHECK(tide_has_Box3D_Joint(&w, hinge) && tide_get_Box3D_Joint(&w, hinge).id == 0);
    joint = tide_get_Box3D_Joint(&w, hinge);
    joint.enabled = true;
    tide_set_Box3D_Joint(&w, hinge, joint);
    tide_world_tick(&w);
    TIDE_CHECK(tide_get_Box3D_Joint(&w, hinge).id != 0 && tide_get_Box3D_Joint(&w, hinge).id != handle);
    w.Request.removeJoint = hinge;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(tide_map_count(w.Box3D_Physics.joints) == 8);
    const tide_entity ballJoint = at(w.Objects.joints, 6);
    joint = tide_get_Box3D_Joint(&w, ballJoint);
    w.Request.removeBody = joint.b;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(!tide_has_Box3D_Joint(&w, ballJoint));
    TIDE_CHECK(tide_has_Moving(&w, joint.b) && !tide_has_Box3D_Body(&w, joint.b));
    tide_world_free(&w);
}

TIDE_TEST(box3d_joints_snapshot_canonical_restore_and_replay)
{
    tide_world w = {0}, saved = {0}, restored = {0};
    start(&w);
    for (int i = 0; i < 30; i++) tide_world_tick(&w);
    tide_world_copy(&saved, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&w, bytes, size) == size);
    b3t_forget();
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    uint64_t sequence = 0;
    for (int i = 0; i < 90; i++) {
        tide_world_tick(&w);
        const uint64_t hash = tide_world_hash(&w);
        sequence = tide_hash_more(sequence, &hash, sizeof hash);
        tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&restored) == hash);
    }
    TIDE_CHECK(sequence == UINT64_C(0x78bf7bb3c89f806a));
    printf("  joint sequence: %016llx\n", (unsigned long long)sequence);
    for (int i = 0; i < 90; i++) tide_world_tick(&saved);
    TIDE_CHECK(tide_world_hash(&saved) == tide_world_hash(&w));
    tide_world_free(&restored); tide_world_free(&saved); tide_world_free(&w);
}

TIDE_TEST(box3d_joints_reject_invalid_game_values)
{
    for (int kind = 0; kind < 7; kind++) {
        tide_world w = {0};
        start(&w);
        const tide_entity e = at(w.Objects.joints, 6);
        Box3D_Joint joint = tide_get_Box3D_Joint(&w, e);
        Box3D_SphericalJoint settings = tide_get_Box3D_SphericalJoint(&w, e);
        if (kind == 0) joint.anchorA.x = NAN;
        if (kind == 1) joint.rotationA.value.w = INFINITY;
        if (kind == 2) joint.a = joint.b;
        if (kind == 3) settings.coneAngle = 3;
        if (kind == 4) settings.hertz = -1;
        if (kind == 5) settings.motorVelocity.x = NAN;
        if (kind == 6) settings.lowerTwistAngle = 1;
        tide_set_Box3D_Joint(&w, e, joint);
        tide_set_Box3D_SphericalJoint(&w, e, settings);
        tide_world_tick(&w);
        tide_world_tick(&w);
        TIDE_CHECK(!tide_has_Box3D_Joint(&w, e) && tide_map_count(w.Box3D_Physics.joints) == 8);
        tide_world_free(&w);
    }
}

TIDE_TEST(box3d_all_joint_settings_update_without_recreating_handles)
{
    tide_world w={0}; start(&w);
    int64_t ids[9];
    for(int i=0;i<9;i++) ids[i]=tide_get_Box3D_Joint(&w,at(w.Objects.joints,i)).id;
    tide_entity e=at(w.Objects.joints,0);
    Box3D_DistanceJoint distance=tide_get_Box3D_DistanceJoint(&w,e);
    distance.enableSpring=true; distance.hertz=4; distance.dampingRatio=1;
    distance.lowerSpringForce=-3; distance.upperSpringForce=3;
    distance.enableLimit=true; distance.minLength=0.5f; distance.maxLength=2;
    distance.enableMotor=true; distance.maxMotorForce=2; distance.motorSpeed=0.25f;
    tide_set_Box3D_DistanceJoint(&w,e,distance);
    e=at(w.Objects.joints,1);
    Box3D_MotorJoint motor=tide_get_Box3D_MotorJoint(&w,e);
    motor.linearVelocity=(tide_float3){0.1f,0,0}; motor.angularVelocity=(tide_float3){0,0,0.2f};
    motor.maxVelocityForce=2; motor.maxVelocityTorque=2; motor.linearHertz=4; motor.angularHertz=5;
    motor.maxSpringForce=10; motor.maxSpringTorque=10; motor.linearDampingRatio=0.7f; motor.angularDampingRatio=0.8f;
    tide_set_Box3D_MotorJoint(&w,e,motor);
    e=at(w.Objects.joints,3);
    Box3D_ParallelJoint parallel=tide_get_Box3D_ParallelJoint(&w,e);
    parallel.hertz=4; parallel.dampingRatio=0.5f; parallel.maxTorque=10;
    tide_set_Box3D_ParallelJoint(&w,e,parallel);
    e=at(w.Objects.joints,4);
    Box3D_PrismaticJoint slider=tide_get_Box3D_PrismaticJoint(&w,e);
    slider.enableSpring=true; slider.hertz=4; slider.dampingRatio=1; slider.targetTranslation=0.2f;
    slider.lowerTranslation=-0.2f; slider.upperTranslation=0.6f; slider.maxMotorForce=2; slider.motorSpeed=-1;
    tide_set_Box3D_PrismaticJoint(&w,e,slider);
    e=at(w.Objects.joints,5);
    Box3D_RevoluteJoint hinge=tide_get_Box3D_RevoluteJoint(&w,e);
    hinge.enableSpring=true; hinge.hertz=4; hinge.dampingRatio=1; hinge.targetAngle=0.4f;
    hinge.enableLimit=true; hinge.lowerAngle=-0.8f; hinge.upperAngle=0.8f; hinge.motorSpeed=-2;
    tide_set_Box3D_RevoluteJoint(&w,e,hinge);
    e=at(w.Objects.joints,6);
    Box3D_SphericalJoint ball=tide_get_Box3D_SphericalJoint(&w,e);
    ball.enableSpring=true; ball.hertz=4; ball.dampingRatio=1; ball.targetRotation=(tide_quaternion){{0,0,0.1f,1}};
    ball.coneAngle=0.7f; ball.lowerTwistAngle=-0.8f; ball.upperTwistAngle=0.8f;
    ball.enableMotor=true; ball.maxMotorTorque=2; ball.motorVelocity=(tide_float3){0.1f,0.2f,0.3f};
    tide_set_Box3D_SphericalJoint(&w,e,ball);
    e=at(w.Objects.joints,7);
    Box3D_WeldJoint weld=tide_get_Box3D_WeldJoint(&w,e);
    weld.linearHertz=4; weld.linearDampingRatio=1; weld.angularHertz=10; weld.angularDampingRatio=0.7f;
    tide_set_Box3D_WeldJoint(&w,e,weld);
    e=at(w.Objects.joints,8);
    Box3D_WheelJoint wheel=tide_get_Box3D_WheelJoint(&w,e);
    wheel.suspensionHertz=3; wheel.suspensionDampingRatio=0.5f; wheel.enableSuspensionLimit=true;
    wheel.lowerSuspensionLimit=-0.4f; wheel.upperSuspensionLimit=0.5f; wheel.maxSpinTorque=2; wheel.spinSpeed=-2;
    wheel.enableSteering=true; wheel.steeringHertz=3; wheel.steeringDampingRatio=1; wheel.targetSteeringAngle=0.2f;
    wheel.maxSteeringTorque=10; wheel.enableSteeringLimit=true; wheel.lowerSteeringLimit=-0.3f; wheel.upperSteeringLimit=0.3f;
    tide_set_Box3D_WheelJoint(&w,e,wheel);
    Box3D_Joint base=tide_get_Box3D_Joint(&w,e);
    base.anchorA.x=0.1f; base.rotationB=(tide_quaternion){{0,0,0.1f,1}};
    base.constraintHertz=40; base.constraintDampingRatio=1; base.collideConnected=true;
    tide_set_Box3D_Joint(&w,e,base);
    for(int tick=0;tick<30;tick++) tide_world_tick(&w);
    for(int i=0;i<9;i++) {
        const Box3D_Joint joint=tide_get_Box3D_Joint(&w,at(w.Objects.joints,i));
        TIDE_CHECK(joint.id==ids[i]);
        TIDE_CHECK(isfinite(joint.force.x) && isfinite(joint.force.y) && isfinite(joint.force.z));
        TIDE_CHECK(isfinite(joint.torque.x) && isfinite(joint.torque.y) && isfinite(joint.torque.z));
    }
    tide_world_free(&w);
}
