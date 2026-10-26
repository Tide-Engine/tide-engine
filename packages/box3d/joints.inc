// Included by bridge.c: native joints use the active match's resident world.
typedef struct joint_component
{
    tide_entity a, b;
    tide_float3 anchorA, anchorB;
    tide_quaternion rotationA, rotationB;
    float constraintHertz, constraintDampingRatio;
    bool enabled, collideConnected;
    uint8_t padding[6];
    int64_t id;
    tide_float3 force, torque;
} joint_component;
_Static_assert(sizeof(joint_component) == 120 && offsetof(joint_component, id) == 88,
               "Joint must match its generated layout");

static b3JointId joint_of(int64_t id)
{
    return (b3JointId){(int32_t)(uint32_t)id, (uint16_t)(active->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

void b3t_drop_joint(int64_t handle_id)
{
    if (!active) return;
    b3JointId id = joint_of(handle_id);
    if (!b3Joint_IsValid(id)) return;
    b3DestroyJoint(id, true);
    note('j', &handle_id, sizeof handle_id);
}

// Reject invalid values before any assert-taking native call. Nothing supplied
// by a game is an allocation size or an unchecked native pointer.
static bool joint_float(float v) { return b3IsValidFloat(v) && v >= -1e12f && v <= 1e12f; }
static bool joint_vec(tide_float3 v) { return joint_float(v.x) && joint_float(v.y) && joint_float(v.z); }
static bool joint_quat(tide_quaternion q)
{
    return joint_float(q.value.x) && joint_float(q.value.y) && joint_float(q.value.z) && joint_float(q.value.w) &&
        (q.value.x != 0 || q.value.y != 0 || q.value.z != 0 || q.value.w != 0);
}

static bool joint_base(joint_component *c, int64_t a, int64_t b, b3JointDef *def)
{
    if (!active || a == b || !b3Body_IsValid(body_of(active, a)) || !b3Body_IsValid(body_of(active, b)) ||
        !joint_vec(c->anchorA) || !joint_vec(c->anchorB) || !joint_quat(c->rotationA) || !joint_quat(c->rotationB) ||
        !joint_float(c->constraintHertz) || c->constraintHertz < 0 ||
        !joint_float(c->constraintDampingRatio) || c->constraintDampingRatio < 0) return false;
    def->bodyIdA = body_of(active, a);
    def->bodyIdB = body_of(active, b);
    def->localFrameA = (b3Transform){vec(c->anchorA), quat(c->rotationA)};
    def->localFrameB = (b3Transform){vec(c->anchorB), quat(c->rotationB)};
    def->constraintHertz = c->constraintHertz;
    def->constraintDampingRatio = c->constraintDampingRatio;
    def->collideConnected = c->collideConnected;
    return true;
}

static bool joint_frame_equal(b3Transform a, b3Transform b)
{
    return same(float3(a.p), float3(b.p)) && same_turn(turn(a.q), turn(b.q));
}

static b3JointId joint_existing(joint_component *c, const b3JointDef *def, b3JointType type)
{
    b3JointId id = joint_of(c->id);
    if (!b3Joint_IsValid(id)) { c->id = 0; return (b3JointId){0}; }
    if (b3Joint_GetType(id) != type || !B3_ID_EQUALS(b3Joint_GetBodyA(id), def->bodyIdA) ||
        !B3_ID_EQUALS(b3Joint_GetBodyB(id), def->bodyIdB)) {
        b3t_drop_joint(c->id);
        c->id = 0;
        return (b3JointId){0};
    }
    return id;
}

static bool joint_update_base(b3JointId id, const b3JointDef *def)
{
    bool changed = false;
    if (!joint_frame_equal(b3Joint_GetLocalFrameA(id), def->localFrameA)) {
        b3Joint_SetLocalFrameA(id, def->localFrameA); changed = true;
    }
    if (!joint_frame_equal(b3Joint_GetLocalFrameB(id), def->localFrameB)) {
        b3Joint_SetLocalFrameB(id, def->localFrameB); changed = true;
    }
    if (b3Joint_GetCollideConnected(id) != def->collideConnected) {
        b3Joint_SetCollideConnected(id, def->collideConnected); changed = true;
    }
    float hertz, damping;
    b3Joint_GetConstraintTuning(id, &hertz, &damping);
    if (hertz != def->constraintHertz || damping != def->constraintDampingRatio) {
        b3Joint_SetConstraintTuning(id, def->constraintHertz, def->constraintDampingRatio); changed = true;
    }
    return changed;
}

static bool joint_reject(joint_component *c)
{
    b3t_drop_joint(c->id);
    c->id = 0;
    c->force = (tide_float3){0};
    c->torque = (tide_float3){0};
    return false;
}

void b3t_read_joint(joint_component *c)
{
    if (!active) { c->force=c->torque=(tide_float3){0}; return; }
    b3JointId id = joint_of(c->id);
    c->force = b3Joint_IsValid(id) ? float3(b3Joint_GetConstraintForce(id)) : (tide_float3){0};
    c->torque = b3Joint_IsValid(id) ? float3(b3Joint_GetConstraintTorque(id)) : (tide_float3){0};
}

typedef struct {
    float length;
    float lowerSpringForce;
    float upperSpringForce;
    float hertz;
    float dampingRatio;
    float minLength;
    float maxLength;
    float maxMotorForce;
    float motorSpeed;
    bool enableSpring;
    bool enableLimit;
    bool enableMotor;
} distance_joint;

bool b3t_sync_distance_joint(joint_component *c, int64_t a, int64_t b, const distance_joint *s)
{
    b3DistanceJointDef def = b3DefaultDistanceJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->length) ||
        s->length < 0 ||
        !joint_float(s->lowerSpringForce) ||
        !joint_float(s->upperSpringForce) ||
        !joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_float(s->minLength) ||
        s->minLength < 0 ||
        !joint_float(s->maxLength) ||
        s->maxLength < 0 ||
        !joint_float(s->maxMotorForce) ||
        s->maxMotorForce < 0 ||
        !joint_float(s->motorSpeed) ||
        s->length <= 0 ||
        s->lowerSpringForce > s->upperSpringForce ||
        s->minLength > s->maxLength) return joint_reject(c);
    def.length = b3MaxFloat(s->length, B3_LINEAR_SLOP);
    def.lowerSpringForce = s->lowerSpringForce;
    def.upperSpringForce = s->upperSpringForce;
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.minLength = b3MaxFloat(s->minLength, B3_LINEAR_SLOP);
    def.maxLength = b3MaxFloat(s->maxLength, B3_LINEAR_SLOP);
    def.maxMotorForce = s->maxMotorForce;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b3JointId id = joint_existing(c, &def.base, b3_distanceJoint);
    if (!id.index1) {
        id = b3CreateDistanceJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.length, sizeof def.length);
        note('J', &def.lowerSpringForce, sizeof def.lowerSpringForce);
        note('J', &def.upperSpringForce, sizeof def.upperSpringForce);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.minLength, sizeof def.minLength);
        note('J', &def.maxLength, sizeof def.maxLength);
        note('J', &def.maxMotorForce, sizeof def.maxMotorForce);
        note('J', &def.motorSpeed, sizeof def.motorSpeed);
        note('J', &def.enableSpring, sizeof def.enableSpring);
        note('J', &def.enableLimit, sizeof def.enableLimit);
        note('J', &def.enableMotor, sizeof def.enableMotor);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3DistanceJoint_GetLength(id) != def.length) { b3DistanceJoint_SetLength(id, def.length); changed = true; }
    if (b3DistanceJoint_IsSpringEnabled(id) != def.enableSpring) { b3DistanceJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b3DistanceJoint_GetSpringHertz(id) != def.hertz) { b3DistanceJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b3DistanceJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b3DistanceJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b3DistanceJoint_IsLimitEnabled(id) != def.enableLimit) { b3DistanceJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b3DistanceJoint_IsMotorEnabled(id) != def.enableMotor) { b3DistanceJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b3DistanceJoint_GetMaxMotorForce(id) != def.maxMotorForce) { b3DistanceJoint_SetMaxMotorForce(id, def.maxMotorForce); changed = true; }
    if (b3DistanceJoint_GetMotorSpeed(id) != def.motorSpeed) { b3DistanceJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b3DistanceJoint_GetMinLength(id) != def.minLength || b3DistanceJoint_GetMaxLength(id) != def.maxLength) {
        b3DistanceJoint_SetLengthRange(id, def.minLength, def.maxLength); changed = true;
    }
    float lower, upper;
    b3DistanceJoint_GetSpringForceRange(id, &lower, &upper);
    if (lower != def.lowerSpringForce || upper != def.upperSpringForce) { b3DistanceJoint_SetSpringForceRange(id, def.lowerSpringForce, def.upperSpringForce); changed = true; }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.length, sizeof def.length);
        note('j', &def.lowerSpringForce, sizeof def.lowerSpringForce);
        note('j', &def.upperSpringForce, sizeof def.upperSpringForce);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.minLength, sizeof def.minLength);
        note('j', &def.maxLength, sizeof def.maxLength);
        note('j', &def.maxMotorForce, sizeof def.maxMotorForce);
        note('j', &def.motorSpeed, sizeof def.motorSpeed);
        note('j', &def.enableSpring, sizeof def.enableSpring);
        note('j', &def.enableLimit, sizeof def.enableLimit);
        note('j', &def.enableMotor, sizeof def.enableMotor);
    }
    return true;
}

typedef struct {
    tide_float3 linearVelocity;
    tide_float3 angularVelocity;
    float maxVelocityForce;
    float maxVelocityTorque;
    float linearHertz;
    float linearDampingRatio;
    float maxSpringForce;
    float angularHertz;
    float angularDampingRatio;
    float maxSpringTorque;
} motor_joint;

bool b3t_sync_motor_joint(joint_component *c, int64_t a, int64_t b, const motor_joint *s)
{
    b3MotorJointDef def = b3DefaultMotorJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_vec(s->linearVelocity) ||
        !joint_vec(s->angularVelocity) ||
        !joint_float(s->maxVelocityForce) ||
        s->maxVelocityForce < 0 ||
        !joint_float(s->maxVelocityTorque) ||
        s->maxVelocityTorque < 0 ||
        !joint_float(s->linearHertz) ||
        s->linearHertz < 0 ||
        !joint_float(s->linearDampingRatio) ||
        s->linearDampingRatio < 0 ||
        !joint_float(s->maxSpringForce) ||
        s->maxSpringForce < 0 ||
        !joint_float(s->angularHertz) ||
        s->angularHertz < 0 ||
        !joint_float(s->angularDampingRatio) ||
        s->angularDampingRatio < 0 ||
        !joint_float(s->maxSpringTorque) ||
        s->maxSpringTorque < 0) return joint_reject(c);
    def.linearVelocity = vec(s->linearVelocity);
    def.angularVelocity = vec(s->angularVelocity);
    def.maxVelocityForce = s->maxVelocityForce;
    def.maxVelocityTorque = s->maxVelocityTorque;
    def.linearHertz = s->linearHertz;
    def.linearDampingRatio = s->linearDampingRatio;
    def.maxSpringForce = s->maxSpringForce;
    def.angularHertz = s->angularHertz;
    def.angularDampingRatio = s->angularDampingRatio;
    def.maxSpringTorque = s->maxSpringTorque;
    b3JointId id = joint_existing(c, &def.base, b3_motorJoint);
    if (!id.index1) {
        id = b3CreateMotorJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.linearVelocity, sizeof def.linearVelocity);
        note('J', &def.angularVelocity, sizeof def.angularVelocity);
        note('J', &def.maxVelocityForce, sizeof def.maxVelocityForce);
        note('J', &def.maxVelocityTorque, sizeof def.maxVelocityTorque);
        note('J', &def.linearHertz, sizeof def.linearHertz);
        note('J', &def.linearDampingRatio, sizeof def.linearDampingRatio);
        note('J', &def.maxSpringForce, sizeof def.maxSpringForce);
        note('J', &def.angularHertz, sizeof def.angularHertz);
        note('J', &def.angularDampingRatio, sizeof def.angularDampingRatio);
        note('J', &def.maxSpringTorque, sizeof def.maxSpringTorque);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (!same(float3(b3MotorJoint_GetLinearVelocity(id)), float3(def.linearVelocity))) { b3MotorJoint_SetLinearVelocity(id, def.linearVelocity); changed = true; }
    if (!same(float3(b3MotorJoint_GetAngularVelocity(id)), float3(def.angularVelocity))) { b3MotorJoint_SetAngularVelocity(id, def.angularVelocity); changed = true; }
    if (b3MotorJoint_GetMaxVelocityForce(id) != def.maxVelocityForce) { b3MotorJoint_SetMaxVelocityForce(id, def.maxVelocityForce); changed = true; }
    if (b3MotorJoint_GetMaxVelocityTorque(id) != def.maxVelocityTorque) { b3MotorJoint_SetMaxVelocityTorque(id, def.maxVelocityTorque); changed = true; }
    if (b3MotorJoint_GetLinearHertz(id) != def.linearHertz) { b3MotorJoint_SetLinearHertz(id, def.linearHertz); changed = true; }
    if (b3MotorJoint_GetLinearDampingRatio(id) != def.linearDampingRatio) { b3MotorJoint_SetLinearDampingRatio(id, def.linearDampingRatio); changed = true; }
    if (b3MotorJoint_GetMaxSpringForce(id) != def.maxSpringForce) { b3MotorJoint_SetMaxSpringForce(id, def.maxSpringForce); changed = true; }
    if (b3MotorJoint_GetAngularHertz(id) != def.angularHertz) { b3MotorJoint_SetAngularHertz(id, def.angularHertz); changed = true; }
    if (b3MotorJoint_GetAngularDampingRatio(id) != def.angularDampingRatio) { b3MotorJoint_SetAngularDampingRatio(id, def.angularDampingRatio); changed = true; }
    if (b3MotorJoint_GetMaxSpringTorque(id) != def.maxSpringTorque) { b3MotorJoint_SetMaxSpringTorque(id, def.maxSpringTorque); changed = true; }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.linearVelocity, sizeof def.linearVelocity);
        note('j', &def.angularVelocity, sizeof def.angularVelocity);
        note('j', &def.maxVelocityForce, sizeof def.maxVelocityForce);
        note('j', &def.maxVelocityTorque, sizeof def.maxVelocityTorque);
        note('j', &def.linearHertz, sizeof def.linearHertz);
        note('j', &def.linearDampingRatio, sizeof def.linearDampingRatio);
        note('j', &def.maxSpringForce, sizeof def.maxSpringForce);
        note('j', &def.angularHertz, sizeof def.angularHertz);
        note('j', &def.angularDampingRatio, sizeof def.angularDampingRatio);
        note('j', &def.maxSpringTorque, sizeof def.maxSpringTorque);
    }
    return true;
}

typedef struct {
    bool enabled;
} filter_joint;

bool b3t_sync_filter_joint(joint_component *c, int64_t a, int64_t b, const filter_joint *s)
{
    b3FilterJointDef def = b3DefaultFilterJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!s->enabled) { joint_reject(c); return true; }
    b3JointId id = joint_existing(c, &def.base, b3_filterJoint);
    if (!id.index1) {
        id = b3CreateFilterJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
    }
    return true;
}

typedef struct {
    float hertz;
    float dampingRatio;
    float maxTorque;
} parallel_joint;

bool b3t_sync_parallel_joint(joint_component *c, int64_t a, int64_t b, const parallel_joint *s)
{
    b3ParallelJointDef def = b3DefaultParallelJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_float(s->maxTorque) ||
        s->maxTorque < 0) return joint_reject(c);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.maxTorque = s->maxTorque;
    b3JointId id = joint_existing(c, &def.base, b3_parallelJoint);
    if (!id.index1) {
        id = b3CreateParallelJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.maxTorque, sizeof def.maxTorque);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3ParallelJoint_GetSpringHertz(id) != def.hertz) { b3ParallelJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b3ParallelJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b3ParallelJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b3ParallelJoint_GetMaxTorque(id) != def.maxTorque) { b3ParallelJoint_SetMaxTorque(id, def.maxTorque); changed = true; }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.maxTorque, sizeof def.maxTorque);
    }
    return true;
}

typedef struct {
    float hertz;
    float dampingRatio;
    float targetTranslation;
    float lowerTranslation;
    float upperTranslation;
    float maxMotorForce;
    float motorSpeed;
    bool enableSpring;
    bool enableLimit;
    bool enableMotor;
} prismatic_joint;

bool b3t_sync_prismatic_joint(joint_component *c, int64_t a, int64_t b, const prismatic_joint *s)
{
    b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_float(s->targetTranslation) ||
        !joint_float(s->lowerTranslation) ||
        !joint_float(s->upperTranslation) ||
        !joint_float(s->maxMotorForce) ||
        s->maxMotorForce < 0 ||
        !joint_float(s->motorSpeed) ||
        s->lowerTranslation > s->upperTranslation) return joint_reject(c);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.targetTranslation = s->targetTranslation;
    def.lowerTranslation = s->lowerTranslation;
    def.upperTranslation = s->upperTranslation;
    def.maxMotorForce = s->maxMotorForce;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b3JointId id = joint_existing(c, &def.base, b3_prismaticJoint);
    if (!id.index1) {
        id = b3CreatePrismaticJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.targetTranslation, sizeof def.targetTranslation);
        note('J', &def.lowerTranslation, sizeof def.lowerTranslation);
        note('J', &def.upperTranslation, sizeof def.upperTranslation);
        note('J', &def.maxMotorForce, sizeof def.maxMotorForce);
        note('J', &def.motorSpeed, sizeof def.motorSpeed);
        note('J', &def.enableSpring, sizeof def.enableSpring);
        note('J', &def.enableLimit, sizeof def.enableLimit);
        note('J', &def.enableMotor, sizeof def.enableMotor);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3PrismaticJoint_IsSpringEnabled(id) != def.enableSpring) { b3PrismaticJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b3PrismaticJoint_GetSpringHertz(id) != def.hertz) { b3PrismaticJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b3PrismaticJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b3PrismaticJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b3PrismaticJoint_GetTargetTranslation(id) != def.targetTranslation) { b3PrismaticJoint_SetTargetTranslation(id, def.targetTranslation); changed = true; }
    if (b3PrismaticJoint_IsLimitEnabled(id) != def.enableLimit) { b3PrismaticJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b3PrismaticJoint_IsMotorEnabled(id) != def.enableMotor) { b3PrismaticJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b3PrismaticJoint_GetMaxMotorForce(id) != def.maxMotorForce) { b3PrismaticJoint_SetMaxMotorForce(id, def.maxMotorForce); changed = true; }
    if (b3PrismaticJoint_GetMotorSpeed(id) != def.motorSpeed) { b3PrismaticJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b3PrismaticJoint_GetLowerLimit(id) != def.lowerTranslation || b3PrismaticJoint_GetUpperLimit(id) != def.upperTranslation) {
        b3PrismaticJoint_SetLimits(id, def.lowerTranslation, def.upperTranslation); changed = true;
    }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.targetTranslation, sizeof def.targetTranslation);
        note('j', &def.lowerTranslation, sizeof def.lowerTranslation);
        note('j', &def.upperTranslation, sizeof def.upperTranslation);
        note('j', &def.maxMotorForce, sizeof def.maxMotorForce);
        note('j', &def.motorSpeed, sizeof def.motorSpeed);
        note('j', &def.enableSpring, sizeof def.enableSpring);
        note('j', &def.enableLimit, sizeof def.enableLimit);
        note('j', &def.enableMotor, sizeof def.enableMotor);
    }
    return true;
}

typedef struct {
    float targetAngle;
    float hertz;
    float dampingRatio;
    float lowerAngle;
    float upperAngle;
    float maxMotorTorque;
    float motorSpeed;
    bool enableSpring;
    bool enableLimit;
    bool enableMotor;
} revolute_joint;

bool b3t_sync_revolute_joint(joint_component *c, int64_t a, int64_t b, const revolute_joint *s)
{
    b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->targetAngle) ||
        !joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_float(s->lowerAngle) ||
        !joint_float(s->upperAngle) ||
        !joint_float(s->maxMotorTorque) ||
        s->maxMotorTorque < 0 ||
        !joint_float(s->motorSpeed) ||
        s->lowerAngle > s->upperAngle) return joint_reject(c);
    def.targetAngle = b3ClampFloat(s->targetAngle, -B3_PI, B3_PI);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.lowerAngle = b3ClampFloat(s->lowerAngle, -0.99f * B3_PI, 0.99f * B3_PI);
    def.upperAngle = b3ClampFloat(s->upperAngle, -0.99f * B3_PI, 0.99f * B3_PI);
    def.maxMotorTorque = s->maxMotorTorque;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b3JointId id = joint_existing(c, &def.base, b3_revoluteJoint);
    if (!id.index1) {
        id = b3CreateRevoluteJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.targetAngle, sizeof def.targetAngle);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.lowerAngle, sizeof def.lowerAngle);
        note('J', &def.upperAngle, sizeof def.upperAngle);
        note('J', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('J', &def.motorSpeed, sizeof def.motorSpeed);
        note('J', &def.enableSpring, sizeof def.enableSpring);
        note('J', &def.enableLimit, sizeof def.enableLimit);
        note('J', &def.enableMotor, sizeof def.enableMotor);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3RevoluteJoint_GetTargetAngle(id) != def.targetAngle) { b3RevoluteJoint_SetTargetAngle(id, def.targetAngle); changed = true; }
    if (b3RevoluteJoint_IsSpringEnabled(id) != def.enableSpring) { b3RevoluteJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b3RevoluteJoint_GetSpringHertz(id) != def.hertz) { b3RevoluteJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b3RevoluteJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b3RevoluteJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b3RevoluteJoint_IsLimitEnabled(id) != def.enableLimit) { b3RevoluteJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b3RevoluteJoint_IsMotorEnabled(id) != def.enableMotor) { b3RevoluteJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b3RevoluteJoint_GetMaxMotorTorque(id) != def.maxMotorTorque) { b3RevoluteJoint_SetMaxMotorTorque(id, def.maxMotorTorque); changed = true; }
    if (b3RevoluteJoint_GetMotorSpeed(id) != def.motorSpeed) { b3RevoluteJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b3RevoluteJoint_GetLowerLimit(id) != def.lowerAngle || b3RevoluteJoint_GetUpperLimit(id) != def.upperAngle) {
        b3RevoluteJoint_SetLimits(id, def.lowerAngle, def.upperAngle); changed = true;
    }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.targetAngle, sizeof def.targetAngle);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.lowerAngle, sizeof def.lowerAngle);
        note('j', &def.upperAngle, sizeof def.upperAngle);
        note('j', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('j', &def.motorSpeed, sizeof def.motorSpeed);
        note('j', &def.enableSpring, sizeof def.enableSpring);
        note('j', &def.enableLimit, sizeof def.enableLimit);
        note('j', &def.enableMotor, sizeof def.enableMotor);
    }
    return true;
}

typedef struct {
    float hertz;
    float dampingRatio;
    tide_quaternion targetRotation;
    float coneAngle;
    float lowerTwistAngle;
    float upperTwistAngle;
    float maxMotorTorque;
    tide_float3 motorVelocity;
    bool enableSpring;
    bool enableConeLimit;
    bool enableTwistLimit;
    bool enableMotor;
} spherical_joint;

bool b3t_sync_spherical_joint(joint_component *c, int64_t a, int64_t b, const spherical_joint *s)
{
    b3SphericalJointDef def = b3DefaultSphericalJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_quat(s->targetRotation) ||
        !joint_float(s->coneAngle) ||
        s->coneAngle < 0 ||
        !joint_float(s->lowerTwistAngle) ||
        !joint_float(s->upperTwistAngle) ||
        !joint_float(s->maxMotorTorque) ||
        s->maxMotorTorque < 0 ||
        !joint_vec(s->motorVelocity) ||
        s->lowerTwistAngle > s->upperTwistAngle ||
        s->coneAngle > 0.5f * B3_PI) return joint_reject(c);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.targetRotation = quat(s->targetRotation);
    def.coneAngle = s->coneAngle;
    def.lowerTwistAngle = b3ClampFloat(s->lowerTwistAngle, -0.99f * B3_PI, 0.99f * B3_PI);
    def.upperTwistAngle = b3ClampFloat(s->upperTwistAngle, -0.99f * B3_PI, 0.99f * B3_PI);
    def.maxMotorTorque = s->maxMotorTorque;
    def.motorVelocity = vec(s->motorVelocity);
    def.enableSpring = s->enableSpring;
    def.enableConeLimit = s->enableConeLimit;
    def.enableTwistLimit = s->enableTwistLimit;
    def.enableMotor = s->enableMotor;
    b3JointId id = joint_existing(c, &def.base, b3_sphericalJoint);
    if (!id.index1) {
        id = b3CreateSphericalJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.targetRotation, sizeof def.targetRotation);
        note('J', &def.coneAngle, sizeof def.coneAngle);
        note('J', &def.lowerTwistAngle, sizeof def.lowerTwistAngle);
        note('J', &def.upperTwistAngle, sizeof def.upperTwistAngle);
        note('J', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('J', &def.motorVelocity, sizeof def.motorVelocity);
        note('J', &def.enableSpring, sizeof def.enableSpring);
        note('J', &def.enableConeLimit, sizeof def.enableConeLimit);
        note('J', &def.enableTwistLimit, sizeof def.enableTwistLimit);
        note('J', &def.enableMotor, sizeof def.enableMotor);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3SphericalJoint_IsSpringEnabled(id) != def.enableSpring) { b3SphericalJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b3SphericalJoint_GetSpringHertz(id) != def.hertz) { b3SphericalJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b3SphericalJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b3SphericalJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (!same_turn(turn(b3SphericalJoint_GetTargetRotation(id)), turn(def.targetRotation))) { b3SphericalJoint_SetTargetRotation(id, def.targetRotation); changed = true; }
    if (b3SphericalJoint_IsConeLimitEnabled(id) != def.enableConeLimit) { b3SphericalJoint_EnableConeLimit(id, def.enableConeLimit); changed = true; }
    if (b3SphericalJoint_GetConeLimit(id) != def.coneAngle) { b3SphericalJoint_SetConeLimit(id, def.coneAngle); changed = true; }
    if (b3SphericalJoint_IsTwistLimitEnabled(id) != def.enableTwistLimit) { b3SphericalJoint_EnableTwistLimit(id, def.enableTwistLimit); changed = true; }
    if (b3SphericalJoint_IsMotorEnabled(id) != def.enableMotor) { b3SphericalJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b3SphericalJoint_GetMaxMotorTorque(id) != def.maxMotorTorque) { b3SphericalJoint_SetMaxMotorTorque(id, def.maxMotorTorque); changed = true; }
    if (!same(float3(b3SphericalJoint_GetMotorVelocity(id)), float3(def.motorVelocity))) { b3SphericalJoint_SetMotorVelocity(id, def.motorVelocity); changed = true; }
    if (b3SphericalJoint_GetLowerTwistLimit(id) != def.lowerTwistAngle || b3SphericalJoint_GetUpperTwistLimit(id) != def.upperTwistAngle) {
        b3SphericalJoint_SetTwistLimits(id, def.lowerTwistAngle, def.upperTwistAngle); changed = true;
    }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.targetRotation, sizeof def.targetRotation);
        note('j', &def.coneAngle, sizeof def.coneAngle);
        note('j', &def.lowerTwistAngle, sizeof def.lowerTwistAngle);
        note('j', &def.upperTwistAngle, sizeof def.upperTwistAngle);
        note('j', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('j', &def.motorVelocity, sizeof def.motorVelocity);
        note('j', &def.enableSpring, sizeof def.enableSpring);
        note('j', &def.enableConeLimit, sizeof def.enableConeLimit);
        note('j', &def.enableTwistLimit, sizeof def.enableTwistLimit);
        note('j', &def.enableMotor, sizeof def.enableMotor);
    }
    return true;
}

typedef struct {
    float linearHertz;
    float angularHertz;
    float linearDampingRatio;
    float angularDampingRatio;
} weld_joint;

bool b3t_sync_weld_joint(joint_component *c, int64_t a, int64_t b, const weld_joint *s)
{
    b3WeldJointDef def = b3DefaultWeldJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->linearHertz) ||
        s->linearHertz < 0 ||
        !joint_float(s->angularHertz) ||
        s->angularHertz < 0 ||
        !joint_float(s->linearDampingRatio) ||
        s->linearDampingRatio < 0 ||
        !joint_float(s->angularDampingRatio) ||
        s->angularDampingRatio < 0) return joint_reject(c);
    def.linearHertz = s->linearHertz;
    def.angularHertz = s->angularHertz;
    def.linearDampingRatio = s->linearDampingRatio;
    def.angularDampingRatio = s->angularDampingRatio;
    b3JointId id = joint_existing(c, &def.base, b3_weldJoint);
    if (!id.index1) {
        id = b3CreateWeldJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.linearHertz, sizeof def.linearHertz);
        note('J', &def.angularHertz, sizeof def.angularHertz);
        note('J', &def.linearDampingRatio, sizeof def.linearDampingRatio);
        note('J', &def.angularDampingRatio, sizeof def.angularDampingRatio);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3WeldJoint_GetLinearHertz(id) != def.linearHertz) { b3WeldJoint_SetLinearHertz(id, def.linearHertz); changed = true; }
    if (b3WeldJoint_GetAngularHertz(id) != def.angularHertz) { b3WeldJoint_SetAngularHertz(id, def.angularHertz); changed = true; }
    if (b3WeldJoint_GetLinearDampingRatio(id) != def.linearDampingRatio) { b3WeldJoint_SetLinearDampingRatio(id, def.linearDampingRatio); changed = true; }
    if (b3WeldJoint_GetAngularDampingRatio(id) != def.angularDampingRatio) { b3WeldJoint_SetAngularDampingRatio(id, def.angularDampingRatio); changed = true; }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.linearHertz, sizeof def.linearHertz);
        note('j', &def.angularHertz, sizeof def.angularHertz);
        note('j', &def.linearDampingRatio, sizeof def.linearDampingRatio);
        note('j', &def.angularDampingRatio, sizeof def.angularDampingRatio);
    }
    return true;
}

typedef struct {
    float suspensionHertz;
    float suspensionDampingRatio;
    float lowerSuspensionLimit;
    float upperSuspensionLimit;
    float maxSpinTorque;
    float spinSpeed;
    float steeringHertz;
    float steeringDampingRatio;
    float targetSteeringAngle;
    float maxSteeringTorque;
    float lowerSteeringLimit;
    float upperSteeringLimit;
    bool enableSuspensionSpring;
    bool enableSuspensionLimit;
    bool enableSpinMotor;
    bool enableSteering;
    bool enableSteeringLimit;
} wheel_joint;

bool b3t_sync_wheel_joint(joint_component *c, int64_t a, int64_t b, const wheel_joint *s)
{
    b3WheelJointDef def = b3DefaultWheelJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->suspensionHertz) ||
        s->suspensionHertz < 0 ||
        !joint_float(s->suspensionDampingRatio) ||
        s->suspensionDampingRatio < 0 ||
        !joint_float(s->lowerSuspensionLimit) ||
        !joint_float(s->upperSuspensionLimit) ||
        !joint_float(s->maxSpinTorque) ||
        s->maxSpinTorque < 0 ||
        !joint_float(s->spinSpeed) ||
        !joint_float(s->steeringHertz) ||
        s->steeringHertz < 0 ||
        !joint_float(s->steeringDampingRatio) ||
        s->steeringDampingRatio < 0 ||
        !joint_float(s->targetSteeringAngle) ||
        !joint_float(s->maxSteeringTorque) ||
        s->maxSteeringTorque < 0 ||
        !joint_float(s->lowerSteeringLimit) ||
        !joint_float(s->upperSteeringLimit) ||
        s->lowerSuspensionLimit > s->upperSuspensionLimit ||
        s->lowerSteeringLimit > s->upperSteeringLimit) return joint_reject(c);
    def.suspensionHertz = s->suspensionHertz;
    def.suspensionDampingRatio = s->suspensionDampingRatio;
    def.lowerSuspensionLimit = s->lowerSuspensionLimit;
    def.upperSuspensionLimit = s->upperSuspensionLimit;
    def.maxSpinTorque = s->maxSpinTorque;
    def.spinSpeed = s->spinSpeed;
    def.steeringHertz = s->steeringHertz;
    def.steeringDampingRatio = s->steeringDampingRatio;
    def.targetSteeringAngle = s->targetSteeringAngle;
    def.maxSteeringTorque = s->maxSteeringTorque;
    def.lowerSteeringLimit = s->lowerSteeringLimit;
    def.upperSteeringLimit = s->upperSteeringLimit;
    def.enableSuspensionSpring = s->enableSuspensionSpring;
    def.enableSuspensionLimit = s->enableSuspensionLimit;
    def.enableSpinMotor = s->enableSpinMotor;
    def.enableSteering = s->enableSteering;
    def.enableSteeringLimit = s->enableSteeringLimit;
    b3JointId id = joint_existing(c, &def.base, b3_wheelJoint);
    if (!id.index1) {
        id = b3CreateWheelJoint(active->id, &def);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('J', &def.suspensionHertz, sizeof def.suspensionHertz);
        note('J', &def.suspensionDampingRatio, sizeof def.suspensionDampingRatio);
        note('J', &def.lowerSuspensionLimit, sizeof def.lowerSuspensionLimit);
        note('J', &def.upperSuspensionLimit, sizeof def.upperSuspensionLimit);
        note('J', &def.maxSpinTorque, sizeof def.maxSpinTorque);
        note('J', &def.spinSpeed, sizeof def.spinSpeed);
        note('J', &def.steeringHertz, sizeof def.steeringHertz);
        note('J', &def.steeringDampingRatio, sizeof def.steeringDampingRatio);
        note('J', &def.targetSteeringAngle, sizeof def.targetSteeringAngle);
        note('J', &def.maxSteeringTorque, sizeof def.maxSteeringTorque);
        note('J', &def.lowerSteeringLimit, sizeof def.lowerSteeringLimit);
        note('J', &def.upperSteeringLimit, sizeof def.upperSteeringLimit);
        note('J', &def.enableSuspensionSpring, sizeof def.enableSuspensionSpring);
        note('J', &def.enableSuspensionLimit, sizeof def.enableSuspensionLimit);
        note('J', &def.enableSpinMotor, sizeof def.enableSpinMotor);
        note('J', &def.enableSteering, sizeof def.enableSteering);
        note('J', &def.enableSteeringLimit, sizeof def.enableSteeringLimit);
        b3Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b3WheelJoint_IsSuspensionEnabled(id) != def.enableSuspensionSpring) { b3WheelJoint_EnableSuspension(id, def.enableSuspensionSpring); changed = true; }
    if (b3WheelJoint_GetSuspensionHertz(id) != def.suspensionHertz) { b3WheelJoint_SetSuspensionHertz(id, def.suspensionHertz); changed = true; }
    if (b3WheelJoint_GetSuspensionDampingRatio(id) != def.suspensionDampingRatio) { b3WheelJoint_SetSuspensionDampingRatio(id, def.suspensionDampingRatio); changed = true; }
    if (b3WheelJoint_IsSuspensionLimitEnabled(id) != def.enableSuspensionLimit) { b3WheelJoint_EnableSuspensionLimit(id, def.enableSuspensionLimit); changed = true; }
    if (b3WheelJoint_IsSpinMotorEnabled(id) != def.enableSpinMotor) { b3WheelJoint_EnableSpinMotor(id, def.enableSpinMotor); changed = true; }
    if (b3WheelJoint_GetMaxSpinTorque(id) != def.maxSpinTorque) { b3WheelJoint_SetMaxSpinTorque(id, def.maxSpinTorque); changed = true; }
    if (b3WheelJoint_GetSpinMotorSpeed(id) != def.spinSpeed) { b3WheelJoint_SetSpinMotorSpeed(id, def.spinSpeed); changed = true; }
    if (b3WheelJoint_IsSteeringEnabled(id) != def.enableSteering) { b3WheelJoint_EnableSteering(id, def.enableSteering); changed = true; }
    if (b3WheelJoint_GetSteeringHertz(id) != def.steeringHertz) { b3WheelJoint_SetSteeringHertz(id, def.steeringHertz); changed = true; }
    if (b3WheelJoint_GetSteeringDampingRatio(id) != def.steeringDampingRatio) { b3WheelJoint_SetSteeringDampingRatio(id, def.steeringDampingRatio); changed = true; }
    if (b3WheelJoint_GetTargetSteeringAngle(id) != def.targetSteeringAngle) { b3WheelJoint_SetTargetSteeringAngle(id, def.targetSteeringAngle); changed = true; }
    if (b3WheelJoint_GetMaxSteeringTorque(id) != def.maxSteeringTorque) { b3WheelJoint_SetMaxSteeringTorque(id, def.maxSteeringTorque); changed = true; }
    if (b3WheelJoint_IsSteeringLimitEnabled(id) != def.enableSteeringLimit) { b3WheelJoint_EnableSteeringLimit(id, def.enableSteeringLimit); changed = true; }
    if (b3WheelJoint_GetLowerSuspensionLimit(id) != def.lowerSuspensionLimit || b3WheelJoint_GetUpperSuspensionLimit(id) != def.upperSuspensionLimit) {
        b3WheelJoint_SetSuspensionLimits(id, def.lowerSuspensionLimit, def.upperSuspensionLimit); changed = true;
    }
    if (b3WheelJoint_GetLowerSteeringLimit(id) != def.lowerSteeringLimit || b3WheelJoint_GetUpperSteeringLimit(id) != def.upperSteeringLimit) {
        b3WheelJoint_SetSteeringLimits(id, def.lowerSteeringLimit, def.upperSteeringLimit); changed = true;
    }
    if (changed) {
        b3Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.suspensionHertz, sizeof def.suspensionHertz);
        note('j', &def.suspensionDampingRatio, sizeof def.suspensionDampingRatio);
        note('j', &def.lowerSuspensionLimit, sizeof def.lowerSuspensionLimit);
        note('j', &def.upperSuspensionLimit, sizeof def.upperSuspensionLimit);
        note('j', &def.maxSpinTorque, sizeof def.maxSpinTorque);
        note('j', &def.spinSpeed, sizeof def.spinSpeed);
        note('j', &def.steeringHertz, sizeof def.steeringHertz);
        note('j', &def.steeringDampingRatio, sizeof def.steeringDampingRatio);
        note('j', &def.targetSteeringAngle, sizeof def.targetSteeringAngle);
        note('j', &def.maxSteeringTorque, sizeof def.maxSteeringTorque);
        note('j', &def.lowerSteeringLimit, sizeof def.lowerSteeringLimit);
        note('j', &def.upperSteeringLimit, sizeof def.upperSteeringLimit);
        note('j', &def.enableSuspensionSpring, sizeof def.enableSuspensionSpring);
        note('j', &def.enableSuspensionLimit, sizeof def.enableSuspensionLimit);
        note('j', &def.enableSpinMotor, sizeof def.enableSpinMotor);
        note('j', &def.enableSteering, sizeof def.enableSteering);
        note('j', &def.enableSteeringLimit, sizeof def.enableSteeringLimit);
    }
    return true;
}
