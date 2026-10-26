// Canonical delta files transfer between native and WASM as well as between
// fresh processes. No model cache or native registration has to run first.
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "microduck.h"
#include "tide/net.h"

static tide_world world;

static bool same_entity(tide_entity first, tide_entity second)
{
    return first.index == second.index && first.generation == second.generation;
}

static tide_entity entity_at(tide_list list, int index)
{
    const tide_entity *value = tide_list_read(&world.heap, list, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static tide_entity duck_at(int seat)
{
    return seat == 0 ? world.DuckFixture.first : world.DuckFixture.second;
}

static tide_entity grab_collider(int seat, uint32_t tick)
{
    if (seat == 0) return entity_at(world.DuckSandbox.balls, tick < 520 ? 0 : 1);
    const Duck duck = tide_get_Duck(&world, duck_at(seat));
    const tide_entity child = entity_at(duck.bodies, tick < 520 ? 8 : 9);
    // Fresh-process unpack has not bound the implicit heap before the first
    // tick. Host reads must name the restored world's heap explicitly.
    for (int i = 0; i < tide_list_read_count(&world.heap, duck.colliders); ++i) {
        const tide_entity entity = entity_at(duck.colliders, i);
        if (same_entity(tide_get_Newton_Collider(&world, entity).body, child)) return entity;
    }
    return (tide_entity){0};
}

static float bounded(float value, float lower, float upper)
{
    return value < lower ? lower : value > upper ? upper : value;
}

static DuckControls action(uint32_t tick, int seat)
{
    // Tick and restored match state are the entire policy. No process-local
    // random sequence, cached handles or view state must survive the join.
    DuckControls controls = {
        .select = tick % 47 == 0 ? (seat == 0 ? 1 : -1) : 0,
        .walk = seat == 0 ? 1.0f : -0.75f,
        .drive = tide_sin_f((float)tick * 0.017f + (float)seat) * 0.25f,
        .reset = tick == 80 + (uint32_t)seat * 5 || tick == 450 + (uint32_t)seat * 5,
        .assistance = tick == 150 + (uint32_t)seat * 3 || tick == 165 + (uint32_t)seat * 3 ||
                      tick == 480 + (uint32_t)seat * 3 || tick == 495 + (uint32_t)seat * 3,
        .grab = (tick >= 320 && tick < 400) || (tick >= 520 && tick < 580),
        .grabSerial = (int32_t)tick + 1
    };
    if (!controls.grab) return controls;
    controls.grabCollider = grab_collider(seat, tick);
    const Newton_Collider collider = tide_get_Newton_Collider(&world, controls.grabCollider);
    const tide_entity entity = same_entity(collider.body, (tide_entity){0}) ? controls.grabCollider : collider.body;
    const Newton_Body body = tide_get_Newton_Body(&world, entity);
    controls.grabRevision = tide_get_SandboxBody(&world, entity).revision;
    // The collider offset and this surface point are both body-local. These
    // small proxies fit the input's +/-0.25 m anchor bounds without clamping.
    controls.grabAnchor = tide_add_f3(collider.position, tide_f3(0, collider.radius, 0));
    const tide_float3 point = tide_add_f3(body.position, tide_rotate_q(body.rotation, controls.grabAnchor));
    const float pull = seat == 0 ? 0.006f : 0.003f;
    const tide_float3 target = tide_add_f3(point, tide_f3(
        pull * tide_sin_f((float)tick * 0.019f), seat == 0 ? 0.012f : 0.002f,
        pull * tide_cos_f((float)tick * 0.023f)));
    controls.grabTarget = tide_f3(bounded(target.x, -1.5f, 1.5f),
        bounded(target.y, -0.05f, 1.5f), bounded(target.z, -1.5f, 1.5f));
    return controls;
}

static bool accepted_grab(int seat, DuckControls controls)
{
    const DuckGrab grab = tide_get_DuckGrab(&world, duck_at(seat));
    const Newton_Collider collider = tide_get_Newton_Collider(&world, controls.grabCollider);
    const tide_entity entity = same_entity(collider.body, (tide_entity){0}) ? controls.grabCollider : collider.body;
    const float force = tide_dot_f3(grab.force, grab.force);
    const float torque = tide_dot_f3(grab.torque, grab.torque);
    return grab.requested && same_entity(grab.collider, controls.grabCollider) && same_entity(grab.body, entity) &&
        grab.serial == controls.grabSerial && grab.lease > 0 &&
        grab.requestedRevision == controls.grabRevision &&
        grab.requestedRevision == tide_get_SandboxBody(&world, entity).revision &&
        tide_distance_f3(grab.anchor, controls.grabAnchor) == 0 &&
        tide_distance_f3(grab.target, controls.grabTarget) == 0 &&
        fabsf(tide_distance_f3(grab.anchor, collider.position) - collider.radius) <= 0.002f &&
        fabsf(grab.anchor.x) <= 0.25f && fabsf(grab.anchor.y) <= 0.25f && fabsf(grab.anchor.z) <= 0.25f &&
        isfinite(force) && force <= 12.0f * 12.0f + 0.0001f &&
        isfinite(torque) && torque <= 0.6f * 0.6f + 0.00001f;
}

static bool snapshot_ready(void)
{
    const Newton_Collider floor = tide_get_Newton_Collider(&world, world.DuckSandbox.floor);
    // At least the loose, ungrabbed balls are supported by actual package
    // contacts. A nonzero floor reaction rules out a collision-free seed.
    if (world.Newton_Physics.status != Newton_Status_Ok || !isfinite(floor.contactForce.y) ||
        floor.contactForce.y >= -0.01f) return false;
    for (int seat = 0; seat < 2; ++seat) {
        const Duck duck = tide_get_Duck(&world, duck_at(seat));
        const DuckGrab grab = tide_get_DuckGrab(&world, duck_at(seat));
        const DuckControls controls = {
            .grabCollider = grab_collider(seat, (uint32_t)world.Time.tick - 1),
            .grabAnchor = grab.requestedAnchor, .grabTarget = grab.requestedTarget,
            .grabRevision = grab.requestedRevision, .grabSerial = world.Time.tick
        };
        const DuckTransition *recorded = tide_list_read(&world.heap, duck.transitions,
            (duck.next + 63) % 64, sizeof(*recorded));
        if (!accepted_grab(seat, controls) || grab.requestedRevision <= 0 ||
            tide_dot_f3(grab.force, grab.force) <= 0 || !recorded || !recorded->grabRequested ||
            !same_entity(recorded->grabBody, grab.body) || !same_entity(recorded->grabCollider, grab.collider) ||
            tide_distance_f3(recorded->grabForce, grab.force) != 0 ||
            !tide_player_equal(tide_get_Owner(&world, duck_at(seat)).player, tide_player_from_index(seat))) return false;
    }
    printf("    join tick %d: floor %.6f N, active ball and articulated-child grabs\n",
        world.Time.tick, floor.contactForce.y);
    return true;
}

static bool run(int count)
{
    for (int i = 0; i < count; ++i) {
        const DuckControls controls[2] = {
            action((uint32_t)world.Time.tick, 0), action((uint32_t)world.Time.tick, 1)};
        for (int seat = 0; seat < 2; ++seat)
            tide_world_set_input(&world, tide_player_from_index(seat), controls[seat]);
        tide_world_tick(&world);
        if (world.Newton_Physics.status != Newton_Status_Ok) return false;
        for (int seat = 0; seat < 2; ++seat) {
            if (controls[seat].grab && !accepted_grab(seat, controls[seat])) {
                printf("    rejected grab at tick %d, player %d\n", world.Time.tick - 1, seat);
                return false;
            }
        }
    }
    return true;
}

static bool write_state(const char *path)
{
    tide_world_init(&world, 1.0f / 200.0f);
    if (!run(360) || !snapshot_ready()) {
        tide_world_free(&world);
        return false;
    }
    const uint64_t before = tide_world_hash(&world);
    uint32_t size = 0;
    uint8_t *bytes = tide_world_pack_delta(&world, NULL, NULL, 0, &size);
    if (!bytes || !run(240)) {
        free(bytes);
        tide_world_free(&world);
        return false;
    }
    tide_writer out = {.grows = true};
    tide_write_u64(&out, before);
    tide_write_u64(&out, tide_world_hash(&world));
    tide_write_u32(&out, size);
    tide_write_bytes(&out, bytes, size);
    free(bytes);
    FILE *file = fopen(path, "wb");
    bool ok = file && !out.overflow;
    if (file) {
        ok = ok && fwrite(out.data, 1, out.size, file) == out.size;
        ok = fclose(file) == 0 && ok;
    }
    free(out.data);
    tide_world_free(&world);
    return ok;
}

static bool read_state(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    const long length = ok ? ftell(file) : -1;
    ok = ok && length >= 20 && (uint64_t)length <= UINT32_MAX && fseek(file, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)length) : NULL;
    ok = ok && bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (!ok) { free(bytes); return false; }
    tide_reader in = {bytes, (uint32_t)length, 0, false};
    const uint64_t before = tide_read_u64(&in);
    const uint64_t after = tide_read_u64(&in);
    const uint32_t size = tide_read_u32(&in);
    const uint8_t *data = tide_read_bytes(&in, size);
    ok = !in.failed && in.at == in.size;
    if (!ok) printf("    invalid snapshot file framing\n");
    if (ok) {
        ok = tide_world_unpack_delta(&world, NULL, data, size);
        if (!ok) printf("    snapshot unpack failed\n");
    }
    if (ok) {
        const uint64_t actual = tide_world_hash(&world);
        ok = actual == before;
        if (!ok) printf("    initial hash mismatch: expected %016" PRIx64 ", got %016" PRIx64 "\n", before, actual);
    }
    if (ok) {
        ok = snapshot_ready();
        if (!ok) printf("    restored snapshot contact/grab checks failed at tick %d\n", world.Time.tick);
    }
    if (ok) {
        ok = run(240);
        if (!ok) printf("    continuation failed at tick %d (Newton status %d)\n", world.Time.tick, world.Newton_Physics.status);
    }
    if (ok) {
        const uint64_t actual = tide_world_hash(&world);
        ok = actual == after;
        if (!ok) printf("    final hash mismatch: expected %016" PRIx64 ", got %016" PRIx64 "\n", after, actual);
    }
    tide_world_free(&world);
    free(bytes);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    const bool ok = strcmp(argv[1], "write") == 0 ? write_state(argv[2]) :
                    strcmp(argv[1], "read") == 0 && read_state(argv[2]);
    printf("%s: MicroDuck fresh-process %s\n", ok ? "ok" : "FAIL", argv[1]);
    return ok ? 0 : 1;
}
