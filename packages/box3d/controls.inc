typedef struct body_settings {
    tide_float3 force, torque;
    float sleepSpeed, safetyFactor;
    bool enabled, sleeping;
    bool lockPositionX, lockPositionY, lockPositionZ;
    bool lockRotationX, lockRotationY, lockRotationZ;
    bool fastRotation, contactRecycling;
    uint8_t padding[2];
} body_settings;
_Static_assert(sizeof(body_settings) == 44, "BodySettings ABI changed");

static body_settings configure_body(body b, body_settings s)
{
    const b3BodyId id = body_of(active, b.id);
    if (!b3Body_IsValid(id)) return s;
    bool changed = false;
    if (s.enabled != b3Body_IsEnabled(id)) {
        if (s.enabled) b3Body_Enable(id); else b3Body_Disable(id);
        changed = true;
    }
    if (s.sleeping != b3Body_IsSleepEnabled(id)) { b3Body_EnableSleep(id, s.sleeping); changed = true; }
    if (finite1(s.sleepSpeed) && s.sleepSpeed >= 0 && s.sleepSpeed != b3Body_GetSleepThreshold(id)) {
        b3Body_SetSleepThreshold(id, s.sleepSpeed); changed = true;
    }
    if (finite1(s.safetyFactor) && s.safetyFactor >= 0.01f && s.safetyFactor <= 0.5f && s.safetyFactor != b3Body_GetSafetyFactor(id)) {
        b3Body_SetSafetyFactor(id, s.safetyFactor); changed = true;
    }
    const b3MotionLocks locks = {s.lockPositionX, s.lockPositionY, s.lockPositionZ,
                                s.lockRotationX, s.lockRotationY, s.lockRotationZ};
    const b3MotionLocks old = b3Body_GetMotionLocks(id);
    if (memcmp(&old, &locks, sizeof locks)) { b3Body_SetMotionLocks(id, locks); changed = true; }
    if (s.fastRotation != b3Body_IsFastRotationAllowed(id)) { b3Body_AllowFastRotation(id, s.fastRotation); changed = true; }
    if (s.contactRecycling != b3Body_IsContactRecyclingEnabled(id)) { b3Body_EnableContactRecycling(id, s.contactRecycling); changed = true; }
    if (changed) {
        wake(id);
        note('O', &b.id, sizeof b.id);
        // Cleared padding, with forces recorded separately below.
        body_settings clean = s;
        memset(clean.padding, 0, sizeof clean.padding);
        clean.force = clean.torque = (tide_float3){0};
        note('O', &clean, sizeof clean);
    }
    if (finite3(s.force) && !none(s.force)) {
        b3Body_ApplyForceToCenter(id, vec(s.force), true);
        note('F', &b.id, sizeof b.id); note('F', &s.force, sizeof s.force);
    }
    if (finite3(s.torque) && !none(s.torque)) {
        b3Body_ApplyTorque(id, vec(s.torque), true);
        note('T', &b.id, sizeof b.id); note('T', &s.torque, sizeof s.torque);
    }
    if (!none(s.force) || !none(s.torque)) {
        s.force = s.torque = (tide_float3){0};
    }
    return s;
}
void b3t_body_settings(tide_rows *rows, physics *p)
{
    (void)p;
    for (uint32_t i=0;i<tide_rows_count(rows);i++) {
        body b; body_settings s;
        tide_rows_get(rows,0,i,&b); tide_rows_get(rows,1,i,&s);
        s = configure_body(b,s);
        tide_rows_set(rows,1,i,&s);
        if (!b.settings_applied) { b.settings_applied=true; tide_rows_set(rows,0,i,&b); }
    }
}
void b3t_default_body_settings(tide_rows *rows, physics *p)
{
    (void)p;
    const uint32_t *flags=(const uint32_t *)tide_rows_ints(rows,0,offsetof(body,rewind));
    for (uint32_t i=0;i<tide_rows_count(rows);i++) {
        if (!(flags[i] & 0x0000ff00u)) continue;
        body b; tide_rows_get(rows,0,i,&b);
        const body_settings s = {.sleepSpeed=0.05f,.safetyFactor=0.5f,.enabled=true,.sleeping=true,.contactRecycling=true};
        (void)configure_body(b,s);
        b.settings_applied=false; tide_rows_set(rows,0,i,&b);
    }
}
