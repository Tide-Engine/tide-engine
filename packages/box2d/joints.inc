// Included by bridge.c: native joints use the active match's cached world.
typedef struct joint_component
{
    tide_entity a, b;
    tide_float2 anchorA, anchorB;
    float angleA, angleB;
    float constraintHertz, constraintDampingRatio;
    bool enabled, collideConnected;
    uint8_t padding[6];
    int64_t id;
    tide_float2 force;
    float torque;
    uint8_t tail[4];
    int64_t configuration;
} joint_component;
_Static_assert(sizeof(joint_component) == 88 && offsetof(joint_component, id) == 56,
               "Joint must match its generated layout");

static b2JointId joint_of(int64_t id)
{
    return (b2JointId){(int32_t)(uint32_t)id, (uint16_t)(active->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

void b2t_drop_joint(int64_t handle_id)
{
    if (!active) return;
    b2JointId id = joint_of(handle_id);
    if (!b2Joint_IsValid(id)) return;
    b2DestroyJoint(id);
    note('j', &handle_id, sizeof handle_id);
}

// Reject invalid values before any assert-taking native call. Nothing supplied
// by a game is an allocation size or an unchecked native pointer.
static bool joint_float(float v) { return b2IsValidFloat(v) && v >= -1e12f && v <= 1e12f; }
static bool joint_vec(tide_float2 v) { return joint_float(v.x) && joint_float(v.y); }

static bool joint_base(joint_component *c, int64_t a, int64_t b, b2JointDef *def)
{
    if (!active || a == b || !b2Body_IsValid(body_of(active, a)) || !b2Body_IsValid(body_of(active, b)) ||
        !joint_vec(c->anchorA) || !joint_vec(c->anchorB) || !joint_float(c->angleA) || !joint_float(c->angleB) ||
        !joint_float(c->constraintHertz) || c->constraintHertz < 0 ||
        !joint_float(c->constraintDampingRatio) || c->constraintDampingRatio < 0) return false;
    def->bodyIdA = body_of(active, a);
    def->bodyIdB = body_of(active, b);
    def->localFrameA = (b2Transform){scene_vec(c->anchorA), b2MakeRot(c->angleA)};
    def->localFrameB = (b2Transform){scene_vec(c->anchorB), b2MakeRot(c->angleB)};
    def->constraintHertz = c->constraintHertz;
    def->constraintDampingRatio = c->constraintDampingRatio;
    def->collideConnected = c->collideConnected;
    return true;
}

static bool joint_frame_equal(b2Transform a, b2Transform b)
{
    return a.p.x == b.p.x && a.p.y == b.p.y && a.q.c == b.q.c && a.q.s == b.q.s;
}

static b2JointId joint_existing(joint_component *c, const b2JointDef *def, b2JointType type)
{
    b2JointId id = joint_of(c->id);
    if (!b2Joint_IsValid(id)) { c->id = 0; return (b2JointId){0}; }
    if (b2Joint_GetType(id) != type || !B2_ID_EQUALS(b2Joint_GetBodyA(id), def->bodyIdA) ||
        !B2_ID_EQUALS(b2Joint_GetBodyB(id), def->bodyIdB)) {
        b2t_drop_joint(c->id);
        c->id = 0;
        return (b2JointId){0};
    }
    return id;
}

static bool joint_update_base(b2JointId id, const b2JointDef *def)
{
    bool changed = false;
    if (!joint_frame_equal(b2Joint_GetLocalFrameA(id), def->localFrameA)) {
        b2Joint_SetLocalFrameA(id, def->localFrameA); changed = true;
    }
    if (!joint_frame_equal(b2Joint_GetLocalFrameB(id), def->localFrameB)) {
        b2Joint_SetLocalFrameB(id, def->localFrameB); changed = true;
    }
    if (b2Joint_GetCollideConnected(id) != def->collideConnected) {
        b2Joint_SetCollideConnected(id, def->collideConnected); changed = true;
    }
    float hertz, damping;
    b2Joint_GetConstraintTuning(id, &hertz, &damping);
    if (hertz != def->constraintHertz || damping != def->constraintDampingRatio) {
        b2Joint_SetConstraintTuning(id, def->constraintHertz, def->constraintDampingRatio); changed = true;
    }
    return changed;
}

static bool joint_reject(joint_component *c)
{
    b2t_drop_joint(c->id);
    c->id = 0;
    c->force = (tide_float2){0};
    c->torque = 0;
    return false;
}

void b2t_read_joint(joint_component *c)
{
    if (!active) { c->force=(tide_float2){0}; c->torque=0; return; }
    b2JointId id = joint_of(c->id);
    c->force = b2Joint_IsValid(id) ? scene_float(b2Joint_GetConstraintForce(id)) : (tide_float2){0};
    c->torque = b2Joint_IsValid(id) ? b2Joint_GetConstraintTorque(id) : 0;
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

bool b2t_sync_distance_joint(joint_component *c, int64_t a, int64_t b, const distance_joint *s)
{
    b2DistanceJointDef def = b2DefaultDistanceJointDef();
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
    def.length = b2MaxFloat(s->length, 0.005f);
    def.lowerSpringForce = s->lowerSpringForce;
    def.upperSpringForce = s->upperSpringForce;
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.minLength = b2MaxFloat(s->minLength, 0.005f);
    def.maxLength = b2MaxFloat(s->maxLength, 0.005f);
    def.maxMotorForce = s->maxMotorForce;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b2JointId id = joint_existing(c, &def.base, b2_distanceJoint);
    if (!id.index1) {
        id = b2CreateDistanceJoint(active->id, &def);
        const int32_t kind = b2_distanceJoint;
        note('T', &kind, sizeof kind);
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
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2DistanceJoint_GetLength(id) != def.length) { b2DistanceJoint_SetLength(id, def.length); changed = true; }
    if (b2DistanceJoint_IsSpringEnabled(id) != def.enableSpring) { b2DistanceJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b2DistanceJoint_GetSpringHertz(id) != def.hertz) { b2DistanceJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b2DistanceJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b2DistanceJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b2DistanceJoint_IsLimitEnabled(id) != def.enableLimit) { b2DistanceJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b2DistanceJoint_IsMotorEnabled(id) != def.enableMotor) { b2DistanceJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b2DistanceJoint_GetMaxMotorForce(id) != def.maxMotorForce) { b2DistanceJoint_SetMaxMotorForce(id, def.maxMotorForce); changed = true; }
    if (b2DistanceJoint_GetMotorSpeed(id) != def.motorSpeed) { b2DistanceJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b2DistanceJoint_GetMinLength(id) != def.minLength || b2DistanceJoint_GetMaxLength(id) != def.maxLength) {
        b2DistanceJoint_SetLengthRange(id, def.minLength, def.maxLength); changed = true;
    }
    float lower, upper;
    b2DistanceJoint_GetSpringForceRange(id, &lower, &upper);
    if (lower != def.lowerSpringForce || upper != def.upperSpringForce) { b2DistanceJoint_SetSpringForceRange(id, def.lowerSpringForce, def.upperSpringForce); changed = true; }
    if (changed) {
        b2Joint_WakeBodies(id);
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
    tide_float2 linearVelocity;
    float angularVelocity;
    float maxVelocityForce;
    float maxVelocityTorque;
    float linearHertz;
    float linearDampingRatio;
    float maxSpringForce;
    float angularHertz;
    float angularDampingRatio;
    float maxSpringTorque;
} motor_joint;

bool b2t_sync_motor_joint(joint_component *c, int64_t a, int64_t b, const motor_joint *s)
{
    b2MotorJointDef def = b2DefaultMotorJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_vec(s->linearVelocity) ||
        !joint_float(s->angularVelocity) ||
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
    def.linearVelocity = scene_vec(s->linearVelocity);
    def.angularVelocity = s->angularVelocity;
    def.maxVelocityForce = s->maxVelocityForce;
    def.maxVelocityTorque = s->maxVelocityTorque;
    def.linearHertz = s->linearHertz;
    def.linearDampingRatio = s->linearDampingRatio;
    def.maxSpringForce = s->maxSpringForce;
    def.angularHertz = s->angularHertz;
    def.angularDampingRatio = s->angularDampingRatio;
    def.maxSpringTorque = s->maxSpringTorque;
    b2JointId id = joint_existing(c, &def.base, b2_motorJoint);
    if (!id.index1) {
        id = b2CreateMotorJoint(active->id, &def);
        const int32_t kind = b2_motorJoint;
        note('T', &kind, sizeof kind);
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
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if ((b2MotorJoint_GetLinearVelocity(id).x != def.linearVelocity.x || b2MotorJoint_GetLinearVelocity(id).y != def.linearVelocity.y)) { b2MotorJoint_SetLinearVelocity(id, def.linearVelocity); changed = true; }
    if (b2MotorJoint_GetAngularVelocity(id) != def.angularVelocity) { b2MotorJoint_SetAngularVelocity(id, def.angularVelocity); changed = true; }
    if (b2MotorJoint_GetMaxVelocityForce(id) != def.maxVelocityForce) { b2MotorJoint_SetMaxVelocityForce(id, def.maxVelocityForce); changed = true; }
    if (b2MotorJoint_GetMaxVelocityTorque(id) != def.maxVelocityTorque) { b2MotorJoint_SetMaxVelocityTorque(id, def.maxVelocityTorque); changed = true; }
    if (b2MotorJoint_GetLinearHertz(id) != def.linearHertz) { b2MotorJoint_SetLinearHertz(id, def.linearHertz); changed = true; }
    if (b2MotorJoint_GetLinearDampingRatio(id) != def.linearDampingRatio) { b2MotorJoint_SetLinearDampingRatio(id, def.linearDampingRatio); changed = true; }
    if (b2MotorJoint_GetMaxSpringForce(id) != def.maxSpringForce) { b2MotorJoint_SetMaxSpringForce(id, def.maxSpringForce); changed = true; }
    if (b2MotorJoint_GetAngularHertz(id) != def.angularHertz) { b2MotorJoint_SetAngularHertz(id, def.angularHertz); changed = true; }
    if (b2MotorJoint_GetAngularDampingRatio(id) != def.angularDampingRatio) { b2MotorJoint_SetAngularDampingRatio(id, def.angularDampingRatio); changed = true; }
    if (b2MotorJoint_GetMaxSpringTorque(id) != def.maxSpringTorque) { b2MotorJoint_SetMaxSpringTorque(id, def.maxSpringTorque); changed = true; }
    if (changed) {
        b2Joint_WakeBodies(id);
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

bool b2t_sync_filter_joint(joint_component *c, int64_t a, int64_t b, const filter_joint *s)
{
    b2FilterJointDef def = b2DefaultFilterJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    def.base.collideConnected = false;
    if (!s->enabled) { joint_reject(c); return true; }
    b2JointId id = joint_existing(c, &def.base, b2_filterJoint);
    if (!id.index1) {
        id = b2CreateFilterJoint(active->id, &def);
        const int32_t kind = b2_filterJoint;
        note('T', &kind, sizeof kind);
        c->id = handle(id.index1, id.generation);
        note('J', &c->id, sizeof c->id);
        note('J', &a, sizeof a); note('J', &b, sizeof b);
        note('J', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('J', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('J', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('J', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('J', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('J', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('J', &def.base.collideConnected, sizeof def.base.collideConnected);
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (changed) {
        b2Joint_WakeBodies(id);
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
    float targetTranslation;
    float lowerTranslation;
    float upperTranslation;
    float maxMotorForce;
    float motorSpeed;
    bool enableSpring;
    bool enableLimit;
    bool enableMotor;
} prismatic_joint;

bool b2t_sync_prismatic_joint(joint_component *c, int64_t a, int64_t b, const prismatic_joint *s)
{
    b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
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
    b2JointId id = joint_existing(c, &def.base, b2_prismaticJoint);
    if (!id.index1) {
        id = b2CreatePrismaticJoint(active->id, &def);
        const int32_t kind = b2_prismaticJoint;
        note('T', &kind, sizeof kind);
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
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2PrismaticJoint_IsSpringEnabled(id) != def.enableSpring) { b2PrismaticJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b2PrismaticJoint_GetSpringHertz(id) != def.hertz) { b2PrismaticJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b2PrismaticJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b2PrismaticJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b2PrismaticJoint_GetTargetTranslation(id) != def.targetTranslation) { b2PrismaticJoint_SetTargetTranslation(id, def.targetTranslation); changed = true; }
    if (b2PrismaticJoint_IsLimitEnabled(id) != def.enableLimit) { b2PrismaticJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b2PrismaticJoint_IsMotorEnabled(id) != def.enableMotor) { b2PrismaticJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b2PrismaticJoint_GetMaxMotorForce(id) != def.maxMotorForce) { b2PrismaticJoint_SetMaxMotorForce(id, def.maxMotorForce); changed = true; }
    if (b2PrismaticJoint_GetMotorSpeed(id) != def.motorSpeed) { b2PrismaticJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b2PrismaticJoint_GetLowerLimit(id) != def.lowerTranslation || b2PrismaticJoint_GetUpperLimit(id) != def.upperTranslation) {
        b2PrismaticJoint_SetLimits(id, def.lowerTranslation, def.upperTranslation); changed = true;
    }
    if (changed) {
        b2Joint_WakeBodies(id);
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

bool b2t_sync_revolute_joint(joint_component *c, int64_t a, int64_t b, const revolute_joint *s)
{
    b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
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
    def.targetAngle = b2ClampFloat(s->targetAngle, -3.14159265358979323846f, 3.14159265358979323846f);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.lowerAngle = b2ClampFloat(s->lowerAngle, -0.99f * 3.14159265358979323846f, 0.99f * 3.14159265358979323846f);
    def.upperAngle = b2ClampFloat(s->upperAngle, -0.99f * 3.14159265358979323846f, 0.99f * 3.14159265358979323846f);
    def.maxMotorTorque = s->maxMotorTorque;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b2JointId id = joint_existing(c, &def.base, b2_revoluteJoint);
    if (!id.index1) {
        id = b2CreateRevoluteJoint(active->id, &def);
        const int32_t kind = b2_revoluteJoint;
        note('T', &kind, sizeof kind);
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
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2RevoluteJoint_GetTargetAngle(id) != def.targetAngle) { b2RevoluteJoint_SetTargetAngle(id, def.targetAngle); changed = true; }
    if (b2RevoluteJoint_IsSpringEnabled(id) != def.enableSpring) { b2RevoluteJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b2RevoluteJoint_GetSpringHertz(id) != def.hertz) { b2RevoluteJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b2RevoluteJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b2RevoluteJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b2RevoluteJoint_IsLimitEnabled(id) != def.enableLimit) { b2RevoluteJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b2RevoluteJoint_IsMotorEnabled(id) != def.enableMotor) { b2RevoluteJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b2RevoluteJoint_GetMaxMotorTorque(id) != def.maxMotorTorque) { b2RevoluteJoint_SetMaxMotorTorque(id, def.maxMotorTorque); changed = true; }
    if (b2RevoluteJoint_GetMotorSpeed(id) != def.motorSpeed) { b2RevoluteJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b2RevoluteJoint_GetLowerLimit(id) != def.lowerAngle || b2RevoluteJoint_GetUpperLimit(id) != def.upperAngle) {
        b2RevoluteJoint_SetLimits(id, def.lowerAngle, def.upperAngle); changed = true;
    }
    if (changed) {
        b2Joint_WakeBodies(id);
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
    float linearHertz;
    float angularHertz;
    float linearDampingRatio;
    float angularDampingRatio;
} weld_joint;

bool b2t_sync_weld_joint(joint_component *c, int64_t a, int64_t b, const weld_joint *s)
{
    b2WeldJointDef def = b2DefaultWeldJointDef();
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
    b2JointId id = joint_existing(c, &def.base, b2_weldJoint);
    if (!id.index1) {
        id = b2CreateWeldJoint(active->id, &def);
        const int32_t kind = b2_weldJoint;
        note('T', &kind, sizeof kind);
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
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2WeldJoint_GetLinearHertz(id) != def.linearHertz) { b2WeldJoint_SetLinearHertz(id, def.linearHertz); changed = true; }
    if (b2WeldJoint_GetAngularHertz(id) != def.angularHertz) { b2WeldJoint_SetAngularHertz(id, def.angularHertz); changed = true; }
    if (b2WeldJoint_GetLinearDampingRatio(id) != def.linearDampingRatio) { b2WeldJoint_SetLinearDampingRatio(id, def.linearDampingRatio); changed = true; }
    if (b2WeldJoint_GetAngularDampingRatio(id) != def.angularDampingRatio) { b2WeldJoint_SetAngularDampingRatio(id, def.angularDampingRatio); changed = true; }
    if (changed) {
        b2Joint_WakeBodies(id);
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
    float hertz;
    float dampingRatio;
    float lowerTranslation;
    float upperTranslation;
    float maxMotorTorque;
    float motorSpeed;
    bool enableSpring;
    bool enableLimit;
    bool enableMotor;
} wheel_joint;

bool b2t_sync_wheel_joint(joint_component *c, int64_t a, int64_t b, const wheel_joint *s)
{
    b2WheelJointDef def = b2DefaultWheelJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_float(s->hertz) ||
        s->hertz < 0 ||
        !joint_float(s->dampingRatio) ||
        s->dampingRatio < 0 ||
        !joint_float(s->lowerTranslation) ||
        !joint_float(s->upperTranslation) ||
        !joint_float(s->maxMotorTorque) ||
        s->maxMotorTorque < 0 ||
        !joint_float(s->motorSpeed) ||
        s->lowerTranslation > s->upperTranslation) return joint_reject(c);
    def.hertz = s->hertz;
    def.dampingRatio = s->dampingRatio;
    def.lowerTranslation = s->lowerTranslation;
    def.upperTranslation = s->upperTranslation;
    def.maxMotorTorque = s->maxMotorTorque;
    def.motorSpeed = s->motorSpeed;
    def.enableSpring = s->enableSpring;
    def.enableLimit = s->enableLimit;
    def.enableMotor = s->enableMotor;
    b2JointId id = joint_existing(c, &def.base, b2_wheelJoint);
    if (!id.index1) {
        id = b2CreateWheelJoint(active->id, &def);
        const int32_t kind = b2_wheelJoint;
        note('T', &kind, sizeof kind);
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
        note('J', &def.lowerTranslation, sizeof def.lowerTranslation);
        note('J', &def.upperTranslation, sizeof def.upperTranslation);
        note('J', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('J', &def.motorSpeed, sizeof def.motorSpeed);
        note('J', &def.enableSpring, sizeof def.enableSpring);
        note('J', &def.enableLimit, sizeof def.enableLimit);
        note('J', &def.enableMotor, sizeof def.enableMotor);
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2WheelJoint_IsSpringEnabled(id) != def.enableSpring) { b2WheelJoint_EnableSpring(id, def.enableSpring); changed = true; }
    if (b2WheelJoint_GetSpringHertz(id) != def.hertz) { b2WheelJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b2WheelJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b2WheelJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b2WheelJoint_IsLimitEnabled(id) != def.enableLimit) { b2WheelJoint_EnableLimit(id, def.enableLimit); changed = true; }
    if (b2WheelJoint_IsMotorEnabled(id) != def.enableMotor) { b2WheelJoint_EnableMotor(id, def.enableMotor); changed = true; }
    if (b2WheelJoint_GetMaxMotorTorque(id) != def.maxMotorTorque) { b2WheelJoint_SetMaxMotorTorque(id, def.maxMotorTorque); changed = true; }
    if (b2WheelJoint_GetMotorSpeed(id) != def.motorSpeed) { b2WheelJoint_SetMotorSpeed(id, def.motorSpeed); changed = true; }
    if (b2WheelJoint_GetLowerLimit(id) != def.lowerTranslation || b2WheelJoint_GetUpperLimit(id) != def.upperTranslation) {
        b2WheelJoint_SetLimits(id, def.lowerTranslation, def.upperTranslation); changed = true;
    }
    if (changed) {
        b2Joint_WakeBodies(id);
        note('j', &def.base.localFrameA.p, sizeof def.base.localFrameA.p); note('j', &def.base.localFrameA.q, sizeof def.base.localFrameA.q);
        note('j', &def.base.localFrameB.p, sizeof def.base.localFrameB.p); note('j', &def.base.localFrameB.q, sizeof def.base.localFrameB.q);
        note('j', &def.base.constraintHertz, sizeof def.base.constraintHertz);
        note('j', &def.base.constraintDampingRatio, sizeof def.base.constraintDampingRatio);
        note('j', &def.base.collideConnected, sizeof def.base.collideConnected);
        note('j', &c->id, sizeof c->id);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.lowerTranslation, sizeof def.lowerTranslation);
        note('j', &def.upperTranslation, sizeof def.upperTranslation);
        note('j', &def.maxMotorTorque, sizeof def.maxMotorTorque);
        note('j', &def.motorSpeed, sizeof def.motorSpeed);
        note('j', &def.enableSpring, sizeof def.enableSpring);
        note('j', &def.enableLimit, sizeof def.enableLimit);
        note('j', &def.enableMotor, sizeof def.enableMotor);
    }
    return true;
}


// Creation-only Pogo settings have no native setters. Their canonical key
// lives in the match component, so a restore compares the same configuration.
static void joint_note_base(const joint_component *c, int64_t a, int64_t b)
{
    note('J', &c->id, sizeof c->id);
    note('J', &a, sizeof a); note('J', &b, sizeof b);
    note('J', &c->anchorA, sizeof c->anchorA); note('J', &c->anchorB, sizeof c->anchorB);
    note('J', &c->angleA, sizeof c->angleA); note('J', &c->angleB, sizeof c->angleB);
    note('J', &c->constraintHertz, sizeof c->constraintHertz);
    note('J', &c->constraintDampingRatio, sizeof c->constraintDampingRatio);
    note('J', &c->collideConnected, sizeof c->collideConnected);
}

typedef struct {
    tide_float2 linearVelocity;
    tide_float2 maxVelocityForce;
} mover_joint;

bool b2t_sync_mover_joint(joint_component *c, int64_t a, int64_t b, const mover_joint *s)
{
    b2MoverJointDef def = b2DefaultMoverJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_vec(s->linearVelocity)) return joint_reject(c);
    def.linearVelocity = scene_vec(s->linearVelocity);
    if (!joint_vec(s->maxVelocityForce)) return joint_reject(c);
    if (s->maxVelocityForce.x < 0 || s->maxVelocityForce.y < 0) return joint_reject(c);
    def.maxVelocityForce = scene_vec(s->maxVelocityForce);
    b2JointId id = joint_existing(c, &def.base, b2_moverJoint);
    if (!id.index1) {
        id = b2CreateMoverJoint(active->id, &def);
        const int32_t kind = b2_moverJoint;
        note('T', &kind, sizeof kind);
        c->id = handle(id.index1, id.generation);
        joint_note_base(c, a, b);
        note('J', &def.linearVelocity, sizeof def.linearVelocity);
        note('J', &def.maxVelocityForce, sizeof def.maxVelocityForce);
        if (!id.index1) return joint_reject(c);
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2MoverJoint_GetLinearVelocity(id).x != def.linearVelocity.x || b2MoverJoint_GetLinearVelocity(id).y != def.linearVelocity.y) { b2MoverJoint_SetLinearVelocity(id, def.linearVelocity); changed = true; }
    if (b2MoverJoint_GetMaxVelocityForce(id).x != def.maxVelocityForce.x || b2MoverJoint_GetMaxVelocityForce(id).y != def.maxVelocityForce.y) { b2MoverJoint_SetMaxVelocityForce(id, def.maxVelocityForce); changed = true; }
    if (changed) {
        b2Joint_WakeBodies(id);
        joint_note_base(c, a, b);
        note('j', &def.linearVelocity, sizeof def.linearVelocity);
        note('j', &def.maxVelocityForce, sizeof def.maxVelocityForce);
    }
    return true;
}

typedef struct {
    tide_float2 normal;
    float hertz;
    float dampingRatio;
    float restLength;
    float maxTensionForce;
    float maxCompressionForce;
    float impulse;
    float velocity;
} pogo_joint;

bool b2t_sync_pogo_joint(joint_component *c, int64_t a, int64_t b, const pogo_joint *s)
{
    b2PogoJointDef def = b2DefaultPogoJointDef();
    if (!c->enabled) { joint_reject(c); return true; }
    if (!joint_base(c, a, b, &def.base)) return joint_reject(c);
    if (!joint_vec(s->normal)) return joint_reject(c);
    def.normal = scene_vec(s->normal);
    if (!joint_float(s->hertz)) return joint_reject(c);
    if (s->hertz < 0) return joint_reject(c);
    def.hertz = s->hertz;
    if (!joint_float(s->dampingRatio)) return joint_reject(c);
    if (s->dampingRatio < 0) return joint_reject(c);
    def.dampingRatio = s->dampingRatio;
    if (!joint_float(s->restLength)) return joint_reject(c);
    if (s->restLength < 0) return joint_reject(c);
    def.restLength = s->restLength;
    if (!joint_float(s->maxTensionForce)) return joint_reject(c);
    if (s->maxTensionForce < 0) return joint_reject(c);
    def.maxTensionForce = s->maxTensionForce;
    if (!joint_float(s->maxCompressionForce)) return joint_reject(c);
    if (s->maxCompressionForce < 0) return joint_reject(c);
    def.maxCompressionForce = s->maxCompressionForce;
    if (!joint_float(s->impulse)) return joint_reject(c);
    def.impulse = s->impulse;
    if (!joint_float(s->velocity)) return joint_reject(c);
    def.velocity = s->velocity;
    if (b2LengthSquared(def.normal) < 0.999f || b2LengthSquared(def.normal) > 1.001f) return joint_reject(c);
    b2JointId id = joint_existing(c, &def.base, b2_pogoJoint);
    const float configuration[] = {s->normal.x, s->normal.y, s->maxTensionForce, s->maxCompressionForce, s->impulse, s->velocity};
    const int64_t key = (int64_t)tide_hash_more(UINT64_C(14695981039346656037), configuration, sizeof configuration);
    if (id.index1 && c->configuration != key) { b2t_drop_joint(c->id); c->id = 0; id = (b2JointId){0}; }
    c->configuration = key;
    if (!id.index1) {
        id = b2CreatePogoJoint(active->id, &def);
        const int32_t kind = b2_pogoJoint;
        note('T', &kind, sizeof kind);
        c->id = handle(id.index1, id.generation);
        joint_note_base(c, a, b);
        note('J', &def.normal, sizeof def.normal);
        note('J', &def.hertz, sizeof def.hertz);
        note('J', &def.dampingRatio, sizeof def.dampingRatio);
        note('J', &def.restLength, sizeof def.restLength);
        note('J', &def.maxTensionForce, sizeof def.maxTensionForce);
        note('J', &def.maxCompressionForce, sizeof def.maxCompressionForce);
        note('J', &def.impulse, sizeof def.impulse);
        note('J', &def.velocity, sizeof def.velocity);
        if (!id.index1) return joint_reject(c);
        b2Joint_WakeBodies(id);
        return true;
    }
    bool changed = joint_update_base(id, &def.base);
    if (b2PogoJoint_GetSpringHertz(id) != def.hertz) { b2PogoJoint_SetSpringHertz(id, def.hertz); changed = true; }
    if (b2PogoJoint_GetSpringDampingRatio(id) != def.dampingRatio) { b2PogoJoint_SetSpringDampingRatio(id, def.dampingRatio); changed = true; }
    if (b2PogoJoint_GetRestLength(id) != def.restLength) { b2PogoJoint_SetRestLength(id, def.restLength); changed = true; }
    if (changed) {
        b2Joint_WakeBodies(id);
        joint_note_base(c, a, b);
        note('j', &def.normal, sizeof def.normal);
        note('j', &def.hertz, sizeof def.hertz);
        note('j', &def.dampingRatio, sizeof def.dampingRatio);
        note('j', &def.restLength, sizeof def.restLength);
        note('j', &def.maxTensionForce, sizeof def.maxTensionForce);
        note('j', &def.maxCompressionForce, sizeof def.maxCompressionForce);
        note('j', &def.impulse, sizeof def.impulse);
        note('j', &def.velocity, sizeof def.velocity);
    }
    return true;
}
