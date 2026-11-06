#include <stdlib.h>

#include "shard_map.h"
#include "tide/shard.h"
#include "tide_test.h"

TIDE_TEST(shard_dependencies_keep_entity_bits_separate_from_singletons)
{
    tide_shard_tick tick = {0};
    const tide_entity target = {UINT64_MAX, UINT64_MAX};
    tide_shard_code = 7;
    tide_shard_occurrence = 3;
    tide_shard_this = (tide_entity){UINT64_C(0x100000001), UINT64_C(0x200000002)};
    tide_shard_note(&tick, target);
    tide_shard_note_singleton(&tick, 2);
    tide_shard_note(&tick, target);
    TIDE_REQUIRE(tide_shard_tick_again(&tick));
    tide_shard_ref refs[2];
    TIDE_CHECK(tide_shard_touched(&tick, 7, tide_shard_this, 3, refs, 2) == 2);
    TIDE_CHECK(tide_entity_equal(refs[0].entity, target) && refs[0].singleton == 0);
    TIDE_CHECK(tide_entity_is_null(refs[1].entity) && refs[1].singleton == 3);
    TIDE_CHECK(tide_shard_skips(&tick, 7, tide_shard_this, 3));
    TIDE_CHECK(!tide_shard_skips(&tick, 7, (tide_entity){1, 2}, 3));
    tide_shard_tick_free(&tick);
    tide_shard_code = tide_shard_occurrence = 0;
    tide_shard_this = (tide_entity){0};
}

TIDE_TEST(shard_directory_uses_complete_keys_and_reclaims_clusters)
{
    map64 map = {0};
    for (uint32_t i = 0; i < 512; i++) {
        const tide_entity key = {UINT64_MAX, UINT64_MAX - i};
        map_put(&map, key, i + 1u);
    }
    TIDE_CHECK(map.count == 512u && map.room == 1024u);
    TIDE_CHECK(map_find(&map, (tide_entity){0}) == NULL);
    for (uint32_t i = 0; i < 512; i += 3) map_remove(&map, (tide_entity){UINT64_MAX, UINT64_MAX - i});
    for (uint32_t i = 0; i < 512; i++) {
        const uint64_t *value = map_find(&map, (tide_entity){UINT64_MAX, UINT64_MAX - i});
        if (i % 3 == 0) TIDE_CHECK(value == NULL);
        else TIDE_CHECK(value && *value == i + 1u);
    }
    TIDE_CHECK(!map_find(&map, (tide_entity){UINT64_MAX - 1u, UINT64_MAX}));
    map_free(&map);
}

TIDE_TEST(shard_outbox_carries_both_full_identity_words)
{
    tide_writer w = {.grows = true};
    tide_write_u32(&w, TIDE_SHARD_TO_OWNER);
    tide_write_u32(&w, 123);
    write_entity(&w, (tide_entity){UINT64_MAX, UINT64_C(0xabcdef0123456789)});
    tide_write_u32(&w, 7);
    tide_write_u32(&w, 1);
    tide_write_u8(&w, TIDE_SHARD_GONE);
    tide_shard_outgoing message;
    tide_reader r = {w.data, w.size, 0, false};
    TIDE_REQUIRE(tide_shard_next_outgoing(&r, &message));
    TIDE_CHECK(message.entity.birth == UINT64_MAX && message.entity.lineage == UINT64_C(0xabcdef0123456789));
    TIDE_CHECK(message.route == TIDE_SHARD_TO_OWNER && message.target == 123 && message.number == 7);
    TIDE_CHECK(r.at == w.size);
    for (uint32_t size = 0; size < w.size; size++) {
        r = (tide_reader){w.data, size, 0, false};
        TIDE_CHECK(!tide_shard_next_outgoing(&r, &message));
    }
    free(w.data);
}
#include "tide/shard_agent.h"
#include "tide/shard_coordinator.h"

static bool report_identity_at(tide_shard_coordinator *c, const uint32_t shard, const uint32_t request,
                               const tide_entity key, const bool advertise, const uint64_t clock,
                               const tide_shard_outgoing *outgoing)
{
    tide_writer w = {.grows = true};
    tide_write_u32(&w, TIDE_SHARD_REPORT);
    tide_write_u32(&w, shard);
    tide_write_u32(&w, 0); // Applied batch
    tide_write_u32(&w, 1); // Tick
    tide_write_u32(&w, 0); // Injected
    tide_write_u32(&w, 0); // Busy float bits
    tide_write_u32(&w, shard);
    tide_write_u32(&w, shard == 0 ? TIDE_SHARD_ROOT_ROLE : TIDE_SHARD_REGION);
    tide_write_u32(&w, advertise ? 1 : 0);
    for (uint32_t i = 0; i < 4; i++) tide_write_u32(&w, 0); // ghosts/waiting/outbox/lent
    tide_write_u64(&w, clock);
    tide_write_u32(&w, 0); // Connected
    tide_write_u32(&w, 0); // Presence
    tide_write_u32(&w, request || outgoing ? 1 : 0);
    if (request) {
        tide_writer message = {.grows = true};
        tide_shard_header(&message, TIDE_SHARD_BORROW, shard, request);
        tide_write_u32(&message, shard);
        write_entity(&message, key);
        tide_write_u32(&w, TIDE_SHARD_TO_OWNER);
        tide_write_u32(&w, 0);
        write_entity(&w, key);
        tide_write_u32(&w, request);
        tide_write_u32(&w, message.size);
        tide_write_bytes(&w, message.data, message.size);
        free(message.data);
    } else if (outgoing) {
        tide_write_u32(&w, outgoing->route);
        tide_write_u32(&w, outgoing->target);
        write_entity(&w, outgoing->entity);
        tide_write_u32(&w, outgoing->number);
        tide_write_u32(&w, outgoing->size);
        tide_write_bytes(&w, outgoing->data, outgoing->size);
    }
    tide_write_u32(&w, 0); // Ghost messages
    tide_write_u8(&w, 0);  // No sample
    tide_write_u8(&w, advertise ? 1 : 0);
    if (advertise) {
        tide_write_u32(&w, 1);
        write_entity(&w, key);
    }
    tide_write_u8(&w, 0); // Ghost ownership
    tide_write_u8(&w, 0); // Checkpoint
    const bool ok = !w.overflow && tide_shard_coordinator_report(c, w.data, w.size);
    free(w.data);
    return ok;
}

static bool report_identity(tide_shard_coordinator *c, const uint32_t shard, const uint32_t request,
                            const tide_entity key, const bool advertise)
{
    return report_identity_at(c, shard, request, key, advertise, UINT64_C(0x100000001), NULL);
}

static uint32_t matching_instructions(tide_shard_coordinator *c, const uint32_t shard, const uint8_t kind)
{
    uint32_t size = 0, count = 0;
    uint8_t *bytes = tide_shard_coordinator_instructions(c, shard, &size);
    if (bytes) {
        tide_reader r = {bytes, size, 0, false};
        (void)tide_read_u32(&r);
        (void)tide_read_u32(&r);
        (void)tide_read_u8(&r);
        const uint32_t messages = tide_read_u32(&r);
        for (uint32_t i = 0; i < messages && !r.failed; i++) {
            const uint32_t n = tide_read_u32(&r);
            const uint8_t *message = tide_read_bytes(&r, n);
            if (message && n && message[0] == kind) count++;
        }
        TIDE_CHECK(!r.failed);
    }
    free(bytes);
    return count;
}

static uint32_t borrowed_instructions(tide_shard_coordinator *c, const uint32_t shard)
{
    return matching_instructions(c, shard, TIDE_SHARD_BORROW);
}

TIDE_TEST(shard_new_foreign_keys_route_after_advertisement_without_unknown_bounce)
{
    const tide_shard_coordinator_desc desc = {
        .range = 5, .dims = 2, .world = {{-40, -10, 0}, {40, 10, 0}}, .split = {2, 1, 1},
    };
    tide_shard_coordinator *c = tide_shard_coordinator_create(&desc, 0);
    TIDE_REQUIRE(c);
    const tide_entity key = {UINT64_C(0x100000001), UINT64_C(0xfedcba9876543210)};
    TIDE_CHECK(tide_shard_coordinator_owner(c, key) == -1);
    TIDE_REQUIRE(report_identity(c, 0, 1, key, false));
    tide_shard_coordinator_step(c, 1.0 / 60.0);
    TIDE_CHECK(borrowed_instructions(c, 0) == 0);
    TIDE_CHECK(borrowed_instructions(c, 1) == 0);
    TIDE_CHECK(borrowed_instructions(c, 2) == 0);
    TIDE_REQUIRE(report_identity(c, 2, 0, key, true));
    TIDE_CHECK(tide_shard_coordinator_owner(c, key) == 2);
    TIDE_REQUIRE(report_identity(c, 0, 2, key, false)); // A later bounded retry
    tide_shard_coordinator_step(c, 32.0 / 60.0);
    TIDE_CHECK(borrowed_instructions(c, 0) == 0);
    TIDE_CHECK(borrowed_instructions(c, 2) == 1);
    TIDE_CHECK(tide_shard_coordinator_owner(c, (tide_entity){key.birth, key.lineage - 1u}) == -1);
    tide_shard_coordinator_destroy(c);
}

static uint8_t *checkpoint_payload(void *user, const uint32_t shard, uint32_t *size, uint32_t *tick)
{
    (void)user;
    uint8_t *bytes = malloc(1);
    if (!bytes) return NULL;
    *bytes = (uint8_t)shard;
    *size = 1;
    *tick = 42;
    return bytes;
}

TIDE_TEST(shard_checkpoint_bounds_full_keys_and_directory_uniqueness)
{
    const tide_shard_coordinator_desc desc = {
        .range = 5, .dims = 2, .world = {{-40, -10, 0}, {40, 10, 0}}, .split = {2, 1, 1},
    };
    tide_shard_coordinator *source = tide_shard_coordinator_create(&desc, 0);
    TIDE_REQUIRE(source);
    const tide_entity key = {UINT64_MAX, UINT64_C(0xfedcba9876543210)};
    TIDE_REQUIRE(report_identity(source, 2, 0, key, true));
    uint32_t size = 0;
    uint8_t *bytes = tide_shard_coordinator_save(source, checkpoint_payload, NULL, &size);
    TIDE_REQUIRE(bytes && size >= 32);
    tide_shard_coordinator *copy = tide_shard_coordinator_load(&desc, bytes, size, 0);
    TIDE_REQUIRE(copy);
    TIDE_CHECK(tide_shard_coordinator_owner(copy, key) == 2);
    tide_shard_coordinator_destroy(copy);

    uint8_t *bad = malloc((size_t)size + 20u);
    TIDE_REQUIRE(bad);
    memcpy(bad, bytes, size);
    memset(bad + 12u, 0, 16u); // Null directory key
    TIDE_CHECK(!tide_shard_coordinator_load(&desc, bad, size, 0));
    memcpy(bad, bytes, size);
    memset(bad + 28u, 0xff, 4u); // Owner outside the checkpoint's shard set
    TIDE_CHECK(!tide_shard_coordinator_load(&desc, bad, size, 0));
    memcpy(bad, bytes, 32u);
    memcpy(bad + 32u, bytes + 12u, 20u);
    memcpy(bad + 52u, bytes + 32u, size - 32u);
    bad[8] = 2; // Duplicate full key, with a valid count and byte span
    TIDE_CHECK(!tide_shard_coordinator_load(&desc, bad, size + 20u, 0));

    // A huge count must not wrap when converted to a byte span.
    memcpy(bad, bytes, size);
    memset(bad + 8u, 0xff, 4u);
    const uint8_t *world = (const uint8_t *)(uintptr_t)1;
    uint32_t world_size = 77, tick = 88, rewritten_size = 99;
    TIDE_CHECK(!tide_shard_checkpoint_world(bad, size, 0, &world, &world_size, &tick));
    TIDE_CHECK(world == (const uint8_t *)(uintptr_t)1 && world_size == 77 && tick == 88);
    TIDE_CHECK(!tide_shard_checkpoint_rewrite(bad, size, NULL, NULL, &rewritten_size));
    TIDE_CHECK(rewritten_size == 99);
    free(bad);
    free(bytes);
    tide_shard_coordinator_destroy(source);
}

static tide_shard_coordinator *horizon_coordinator(void)
{
    const tide_shard_coordinator_desc desc = {
        .range = 5, .dims = 2, .world = {{-40, -10, 0}, {40, 10, 0}}, .split = {2, 1, 1},
    };
    return tide_shard_coordinator_create(&desc, 0);
}

static void report_all_past(tide_shard_coordinator *c, const uint64_t clock)
{
    for (uint32_t i = 0; i < 3; i++)
        TIDE_CHECK(report_identity_at(c, i, 0, (tide_entity){0}, false, clock, NULL));
}

static bool report_lifetime_message(tide_shard_coordinator *c, const uint32_t shard, const uint64_t clock,
                                    const tide_entity key, const uint8_t kind, const uint32_t number,
                                    const bool malformed)
{
    tide_writer w = {.grows = true};
    tide_shard_header(&w, (tide_shard_kind)kind, shard, number);
    if (kind == TIDE_SHARD_ARRIVE) {
        tide_write_u8(&w, 0);
        if (!malformed) {
            tide_write_u32(&w, shard);
            tide_write_u32(&w, 1); // Tree count
            tide_write_u32(&w, 16); // Only the coordinator's opaque record prefix is relevant.
            write_entity(&w, key);
        }
    } else write_entity(&w, key);
    const tide_shard_outgoing message = {
        .route = kind == TIDE_SHARD_ARRIVE ? TIDE_SHARD_TO_SHARD : TIDE_SHARD_TO_RELEASE,
        .target = 1, .entity = key, .number = number, .data = w.data, .size = w.size,
    };
    const bool ok = report_identity_at(c, shard, 0, key, false, clock, &message);
    free(w.data);
    return ok;
}

TIDE_TEST(shard_old_dead_key_after_private_slot_reuse_gets_authoritative_gone)
{
    tide_shard_coordinator *c = horizon_coordinator();
    TIDE_REQUIRE(c);
    tide_entities entities = {0};
    const tide_entity dead = {5, UINT64_MAX};
    tide_entity_create_key(&entities, dead);
    const uint32_t slot = tide_entity_slot_index(&entities, dead);
    TIDE_REQUIRE(report_identity_at(c, 2, 0, dead, true, 6, NULL));
    tide_entity_destroy(&entities, dead);
    const tide_entity replacement = {6, UINT64_MAX};
    tide_entity_create_key(&entities, replacement);
    TIDE_CHECK(tide_entity_slot_index(&entities, replacement) == slot);
    TIDE_CHECK(!tide_entity_died_here(&entities, dead)); // No lifetime history retained by the private slot.
    report_all_past(c, 10);
    TIDE_REQUIRE(report_lifetime_message(c, 2, 10, dead, TIDE_SHARD_FREED, 1, false));
    tide_shard_coordinator_step(c, 1.0 / 60.0);
    TIDE_CHECK(tide_shard_coordinator_owner(c, dead) == -1);
    (void)matching_instructions(c, 0, TIDE_SHARD_GONE);
    TIDE_REQUIRE(report_identity_at(c, 0, 1, dead, false, 10, NULL));
    tide_shard_coordinator_step(c, 2.0 / 60.0);
    TIDE_CHECK(matching_instructions(c, 0, TIDE_SHARD_GONE) == 1);
    tide_entities_free(&entities);
    tide_shard_coordinator_destroy(c);
}

TIDE_TEST(shard_absence_horizon_waits_for_missing_reports_recovery_and_birth_clock)
{
    for (uint32_t scenario = 0; scenario < 5; scenario++) {
        tide_shard_coordinator *c = horizon_coordinator();
        TIDE_REQUIRE(c);
        tide_entity key = {5, UINT64_MAX};
        if (scenario == 0) {
            TIDE_REQUIRE(report_identity_at(c, 0, 0, key, false, 10, NULL));
            TIDE_REQUIRE(report_identity_at(c, 1, 0, key, false, 10, NULL)); // Shard2 unheard.
        } else {
            report_all_past(c, 10);
            if (scenario == 1) tide_shard_coordinator_restarted(c, 2);
            if (scenario == 2) key.birth = 10; // Same clock.
            if (scenario == 3) key.birth = 11; // Future.
            if (scenario == 4) key.birth = 0;  // Unkeyed trusted C.
        }
        TIDE_REQUIRE(report_identity_at(c, 0, 1, key, false, 10, NULL));
        tide_shard_coordinator_step(c, 1.0 / 60.0);
        TIDE_CHECK(matching_instructions(c, 0, TIDE_SHARD_GONE) == 0);
        // A late birth announcement still resolves the pending logical reference.
        TIDE_REQUIRE(report_identity_at(c, 2, 0, key, true, key.birth + 1u, NULL));
        TIDE_CHECK(tide_shard_coordinator_owner(c, key) == 2);
        TIDE_REQUIRE(report_identity_at(c, 0, 2, key, false, 12, NULL));
        tide_shard_coordinator_step(c, 2.0 / 60.0);
        TIDE_CHECK(matching_instructions(c, 2, TIDE_SHARD_BORROW) == 1);
        tide_shard_coordinator_destroy(c);
    }
}

TIDE_TEST(shard_pending_and_delayed_handoffs_never_prove_a_birth_absent)
{
    tide_shard_coordinator *c = horizon_coordinator();
    TIDE_REQUIRE(c);
    const tide_entity key = {5, UINT64_MAX};
    report_all_past(c, 10);
    // Requester0 is carried before sender2, and a same-tick spawn+handoff has
    // never appeared in an owned-key report. The pending ARRIVE protects it.
    TIDE_REQUIRE(report_identity_at(c, 0, 1, key, false, 10, NULL));
    TIDE_REQUIRE(report_lifetime_message(c, 2, 10, key, TIDE_SHARD_ARRIVE, 1, false));
    tide_shard_coordinator_step(c, 1.0 / 60.0);
    TIDE_CHECK(matching_instructions(c, 0, TIDE_SHARD_GONE) == 0);
    TIDE_CHECK(tide_shard_coordinator_owner(c, key) == 1);
    // Destination1 has not applied/reported the queued ARRIVE, yet its
    // directory key must survive further requests beyond the birth horizon.
    TIDE_REQUIRE(report_identity_at(c, 0, 2, key, false, 20, NULL));
    tide_shard_coordinator_step(c, 2.0 / 60.0);
    TIDE_CHECK(matching_instructions(c, 0, TIDE_SHARD_GONE) == 0);
    TIDE_CHECK(matching_instructions(c, 1, TIDE_SHARD_BORROW) == 1);
    TIDE_CHECK(tide_shard_coordinator_owner(c, key) == 1);
    tide_shard_coordinator_destroy(c);
}

TIDE_TEST(shard_malformed_pending_arrive_disables_absence_proof)
{
    tide_shard_coordinator *c = horizon_coordinator();
    TIDE_REQUIRE(c);
    const tide_entity unknown = {5, UINT64_MAX};
    report_all_past(c, 10);
    TIDE_REQUIRE(report_identity_at(c, 0, 1, unknown, false, 10, NULL));
    TIDE_REQUIRE(report_lifetime_message(c, 2, 10, (tide_entity){6, 99}, TIDE_SHARD_ARRIVE, 1, true));
    tide_shard_coordinator_step(c, 1.0 / 60.0);
    TIDE_CHECK(matching_instructions(c, 0, TIDE_SHARD_GONE) == 0);
    tide_shard_coordinator_destroy(c);
}
