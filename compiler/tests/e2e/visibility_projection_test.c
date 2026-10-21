#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

extern uint32_t visibility_present_rows;
extern uint32_t visibility_absent_rows;

void CheckPropertyPosition(tide_float3 expected, tide_float3 actual)
{
    TIDE_CHECK(!memcmp(&expected, &actual, sizeof expected));
}

static uint8_t *project(const tide_world *w, int player, uint32_t *size)
{
    return tide_game_api.client_state(w, tide_player_from_index(player), size);
}

static bool contains(const uint8_t *bytes, uint32_t size, const char *text)
{
    const size_t count = strlen(text);
    for (uint32_t i = 0; (uint64_t)i + count <= size; i++) if (!memcmp(bytes + i, text, count)) return true;
    return false;
}

static unsigned private_resident_packs;

static void pack_private_resident(void *context, const tide_resident *resident, tide_writer *out)
{
    (void)context; (void)resident;
    private_resident_packs++;
    static const char marker[] = "PRIVATE-OPAQUE-COLLIDERS-DO-NOT-TRANSMIT";
    tide_write_bytes(out, marker, sizeof marker);
}

TIDE_TEST(visibility_projection_omits_secrets_and_uses_normal_component_presence)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = project(&server, 1, &size);
    TIDE_CHECK(!contains(bytes, size, "PRIVATE-CARD-DO-NOT-TRANSMIT"));
    TIDE_CHECK(contains(bytes, size, "nested-public"));
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_has_PublicState(&client, server.Control.card));
    TIDE_CHECK(!tide_has_Secret(&client, server.Control.card));
    TIDE_CHECK(!tide_entity_alive(&client.entities, server.Control.chunk));
    TIDE_CHECK(tide_entity_alive(&client.entities, server.Control.player));
    TIDE_CHECK(tide_has_Secret(&client, server.Control.player));
    TIDE_CHECK(tide_get_Secret(&client, server.Control.card).code == 0);
    TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(1)) == UINT64_C(0xed6d2f60242f07d3));
    TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(1)) == tide_game_api.client_hash(&client, tide_player_from_index(1)));
    visibility_present_rows = visibility_absent_rows = 0;
    tide_world_tick(&client);
    TIDE_CHECK(visibility_present_rows == 1);
    TIDE_CHECK(visibility_absent_rows == 5);
    TIDE_CHECK(client.Control.known == 1);
    TIDE_CHECK(client.Control.missing == 5);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_canonicalizes_heap_offsets_and_grid_insertion_order)
{
    tide_world a = {0}, b = {0}, client = {0};
    tide_world_init(&a, 0.1f);
    tide_world_init(&b, 0.1f);
    b.Control.reverse = true;
    tide_world_tick(&a);
    tide_world_tick(&b);
    TIDE_CHECK(tide_world_hash(&a) != tide_world_hash(&b));
    uint32_t as, bs;
    uint8_t *ab = project(&a, 0, &as), *bb = project(&b, 0, &bs);
    TIDE_CHECK(as == bs && memcmp(ab, bb, as) == 0);
    TIDE_CHECK(tide_game_api.load_client_state(&client, ab, as));
    uint32_t cs;
    uint8_t *cb = project(&client, 0, &cs);
    TIDE_CHECK(cs == as && memcmp(cb, ab, as) == 0);
    tide_text_use(&client.heap, NULL);
    const PublicState state = tide_get_PublicState(&client, client.Control.card);
    const tide_grid_dir *directory = tide_grid_dir_of(state.cells);
    TIDE_CHECK(directory && directory->records == 4);
    free(cb); free(bb); free(ab);
    tide_world_free(&client);
    tide_world_free(&b);
    tide_world_free(&a);
}

TIDE_TEST(visibility_projection_rejects_truncation_and_preserves_existing_world)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = project(&server, 1, &size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    const uint64_t hash = tide_world_hash(&client);
    const uint32_t cuts[] = {0, 1, 7, 8, 16, 64, 256, size / 2, size - 1};
    for (unsigned i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        TIDE_CHECK(!tide_game_api.load_client_state(&client, bytes, cuts[i]));
        TIDE_CHECK(tide_world_hash(&client) == hash);
    }
    bytes[4] = 0;
    TIDE_CHECK(!tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_world_hash(&client) == hash);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_keeps_filtered_sibling_links_and_hidden_scenes_absent)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = project(&server, 0, &size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(!tide_entity_alive(&client.entities, server.Control.middle));
    TIDE_CHECK(tide_entity_alive(&client.entities, server.Control.first));
    TIDE_CHECK(tide_entity_alive(&client.entities, server.Control.last));
    client.Control.destroyRoot = true;
    tide_world_tick(&client);
    TIDE_CHECK(!tide_entity_alive(&client.entities, server.Control.root));
    TIDE_CHECK(!tide_entity_alive(&client.entities, server.Control.first));
    TIDE_CHECK(!tide_entity_alive(&client.entities, server.Control.last));
    free(bytes);
    server.Control.hideMain = true;
    tide_world_tick(&server);
    bytes = project(&server, 0, &size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_world_entity_count(&client) == 0);
    TIDE_CHECK(!tide_world_ended(&client));
    tide_world_tick(&client);
    TIDE_CHECK(tide_world_entity_count(&client) == 0);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_preserves_sparse_high_entity_ids_and_raw_roundtrips)
{
    tide_world server = {0}, client = {0}, restored = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = project(&server, 1, &size);
    const tide_entity card = server.Control.card;
    const uint32_t pattern[] = {card.index, card.generation, 0, 0, 1};
    uint32_t offset = UINT32_MAX, found = 0;
    for (uint32_t i = 0; (uint64_t)i + sizeof pattern <= size; i++) if (!memcmp(bytes + i, pattern, sizeof pattern)) { offset = i; found++; }
    TIDE_CHECK(found == 1);
    const tide_entity high = {(1u << 20) + 19u, card.generation};
    memcpy(bytes + offset, &high.index, sizeof high.index);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_entity_alive(&client.entities, high));
    TIDE_CHECK(tide_has_PublicState(&client, high));
    TIDE_CHECK(!tide_has_Secret(&client, high));
    TIDE_CHECK(client.entities.pages > 1024);
    TIDE_CHECK(client.entities.page[100] == NULL);
    const uint64_t hash = tide_world_hash(&client);
    const uint32_t packed = tide_world_pack(&client, NULL, 0);
    uint8_t *raw = malloc(packed);
    TIDE_CHECK(raw != NULL);
    TIDE_CHECK(tide_world_pack(&client, raw, packed) == packed);
    TIDE_CHECK(tide_world_unpack(&restored, raw, packed));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    TIDE_CHECK(tide_has_PublicState(&restored, high));
    const tide_entity predicted = tide_entity_create(&client.entities);
    TIDE_CHECK(predicted.index > high.index);
    free(raw); free(bytes);
    tide_world_free(&restored);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_does_not_send_hidden_players_or_private_component_inputs)
{
    tide_world server = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    Controls input = {.fire = true, .secretChoice = 77};
    tide_game_api.filter_client_input(&server, tide_player_from_index(0), 1, &input);
    TIDE_CHECK(!input.fire && input.secretChoice == 0);
    input = (Controls){.fire = true, .secretChoice = 88};
    tide_game_api.filter_client_input(&server, tide_player_from_index(1), 0, &input);
    TIDE_CHECK(!input.fire && input.secretChoice == 0);
    input = (Controls){.fire = true, .secretChoice = 99};
    tide_game_api.filter_client_input(&server, tide_player_from_index(1), 1, &input);
    TIDE_CHECK(input.fire && input.secretChoice == 99);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_keeps_opaque_residents_authoritative_and_private)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    const tide_player_id player = tide_player_from_index(1);
    const uint64_t before = tide_game_api.client_hash(&server, player);
    const tide_resident_kind kind = {.name = "visibility-projection-private", .pack = pack_private_resident};
    server.PrivateCache.state.kind = tide_resident_register(&kind);
    server.PrivateCache.state.stamp = 123;
    server.PrivateCache.state.digest = 456;
    tide_writer reference = {.grows = true};
    tide_resident_pack(&server.PrivateCache.state, &reference);
    TIDE_CHECK(contains(reference.data, reference.size, "PRIVATE-OPAQUE-COLLIDERS-DO-NOT-TRANSMIT"));
    private_resident_packs = 0;
    uint32_t size;
    uint8_t *bytes = project(&server, 1, &size);
    TIDE_CHECK(private_resident_packs == 0);
    TIDE_CHECK(!contains(bytes, size, "PRIVATE-OPAQUE-COLLIDERS-DO-NOT-TRANSMIT"));
    TIDE_CHECK(tide_game_api.client_hash(&server, player) == before);
    TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(client.PrivateCache.state.kind == 0 && client.PrivateCache.state.pages == NULL);
    free(reference.data); free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_preserves_exact_affine_placement_at_hidden_boundaries)
{
    for (int hidden_component = 0; hidden_component < 2; hidden_component++) {
        tide_world server = {0}, client = {0}, copy = {0};
        tide_world_init(&server, 0.1f);
        Transform parent = tide_get_Transform(&server, server.Control.root);
        parent.localPosition = tide_f3(17001.5f, 23.25f, -75.5f);
        parent.localScale = tide_f3(2, 3, 4);
        parent.localRotation = tide_euler_q(tide_f3(0.3f, 0.7f, -0.2f));
        tide_set_Transform(&server, server.Control.root, parent);
        Transform child = tide_get_Transform(&server, server.Control.first);
        child.localPosition = tide_f3(5, 7, 11);
        child.localScale = tide_f3(0.7f, 1.3f, 2.1f);
        child.localRotation = tide_euler_q(tide_f3(-0.4f, 0.2f, 0.5f));
        tide_set_Transform(&server, server.Control.first, child);
        server.Placement.hideRoot = !hidden_component;
        server.Placement.hideRootTransform = hidden_component != 0;
        tide_world_tick(&server);
        const tide_float4x4 matrix = server.Placement.firstMatrix;
        uint32_t size;
        uint8_t *bytes = project(&server, 0, &size);
        TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
        TIDE_CHECK(tide_has_Transform(&client, client.Control.first));
        TIDE_CHECK(!tide_has_Transform(&client, client.Control.root));
        TIDE_CHECK(tide_entity_alive(&client.entities, client.Control.root) == (hidden_component != 0));
        TIDE_CHECK(tide_map_count(client.tide_visibility_state.anchors) == 2);
        TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(0)) == tide_game_api.client_hash(&client, tide_player_from_index(0)));
        tide_world_copy(&copy, &client);
        tide_world_tick(&client);
        TIDE_CHECK(!memcmp(&client.Placement.firstMatrix, &matrix, sizeof matrix));
        tide_world_tick(&copy);
        TIDE_CHECK(!memcmp(&copy.Placement.firstMatrix, &matrix, sizeof matrix));
        client.Placement.detachFirst = true;
        tide_world_tick(&client);
        TIDE_CHECK(tide_map_count(client.tide_visibility_state.anchors) == 1);
        tide_world_tick(&client);
        const tide_float4x4 local = tide_trs_f4x4(child.localPosition, tide_normalize_q(child.localRotation), child.localScale);
        TIDE_CHECK(!memcmp(&client.Placement.firstMatrix, &local, sizeof local));
        free(bytes);
        tide_world_free(&copy);
        tide_world_free(&client);
        tide_world_free(&server);
    }
}

TIDE_TEST(visibility_projection_omits_boundary_placement_when_leaf_transform_is_hidden)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    server.Placement.hideRoot = true;
    server.Placement.hideFirstTransform = true;
    tide_world_tick(&server);
    uint32_t size;
    uint8_t *bytes = project(&server, 0, &size);
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_entity_alive(&client.entities, client.Control.first));
    TIDE_CHECK(!tide_has_Transform(&client, client.Control.first));
    TIDE_CHECK(tide_map_count(client.tide_visibility_state.anchors) == 1);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_discloses_only_recipients_command_sequence_metadata)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    server.tide_command_state.seq0 = 137;
    server.tide_command_state.seq1 = 149;
    server.tide_command_state.seq2 = 163;
    uint32_t size;
    uint8_t *bytes = project(&server, 1, &size);
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(client.tide_command_state.seq0 == 0 && client.tide_command_state.seq1 == 149 && client.tide_command_state.seq2 == 0);
    const uint64_t hash = tide_game_api.client_hash(&server, tide_player_from_index(1));
    server.tide_command_state.seq0++;
    server.tide_command_state.seq2++;
    TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(1)) == hash);
    TIDE_CHECK(tide_game_api.client_hash(&client, tide_player_from_index(1)) == hash);
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_projection_journals_permitted_destruction_without_local_listeners)
{
    tide_world server = {0};
    tide_event_log events[TIDE_GAME_PLAYERS] = {{0}};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    const tide_entity root = server.Control.root, middle = server.Control.middle;
    server.Control.destroyRoot = true;
    TIDE_REQUIRE(tide_game_api.tick_client_events && tide_game_api.check_client_events);
    tide_game_api.tick_client_events(&server, NULL, events);
    for (uint32_t player = 0; player < TIDE_GAME_PLAYERS; player++) {
        uint32_t at = 0, root_causes = 0, hidden_causes = 0;
        for (const tide_heard *h; (h = tide_event_log_next(&events[player], &at));) {
            if (h->event != TIDE_HEARD_DESTROYED_CAUSE) continue;
            if (tide_entity_equal(h->target, root)) root_causes++;
            if (tide_entity_equal(h->target, middle)) hidden_causes++;
        }
        TIDE_CHECK(root_causes == 1);
        TIDE_CHECK(hidden_causes == (player == 1 ? 1u : 0u));
        TIDE_CHECK(tide_game_api.check_client_events(events[player].bytes, events[player].size, NULL, NULL));
        tide_event_log_free(&events[player]);
    }
    tide_world_free(&server);
}

enum { PROJECTION_HISTORY = 512 };

typedef struct projection_network {
    tide_loopback *net;
    tide_server *server;
    tide_client *clients[2];
    tide_world *history;
    uint32_t history_ticks[PROJECTION_HISTORY];
    int phases[2];
    double now;
} projection_network;

static void sample_projection(void *user, uint32_t tick, void *input)
{
    (void)tick;
    *(Controls *)input = (Controls){.phase = *(const int *)user};
}

static void step_projection(projection_network *f)
{
    f->now += 1.0 / 120.0;
    tide_loopback_set_time(f->net, f->now);
    for (unsigned i = 0; i < 2; i++) if (f->clients[i]) tide_client_update(f->clients[i], f->now);
    tide_server_update(f->server, f->now);
    const uint32_t tick = tide_server_tick(f->server), index = tick % PROJECTION_HISTORY;
    if (f->history_ticks[index] != tick) {
        tide_world_copy(&f->history[index], tide_server_world(f->server));
        f->history_ticks[index] = tick;
    }
}

static bool projection_matches(const projection_network *f, unsigned client)
{
    const tide_client_status status = tide_client_status_of(f->clients[client]);
    const tide_world *verified = tide_client_verified_world(f->clients[client]);
    if (!verified || status.state != TIDE_SESSION_CONNECTED || f->history_ticks[status.verified_tick % PROJECTION_HISTORY] != status.verified_tick) return false;
    const tide_world *authoritative = &f->history[status.verified_tick % PROJECTION_HISTORY];
    return tide_game_api.client_hash(verified, status.player) == tide_game_api.client_hash(authoritative, status.player);
}

static bool projection_stage(const projection_network *f, int phase)
{
    for (unsigned i = 0; i < 2; i++) {
        const tide_client_status status = tide_client_status_of(f->clients[i]);
        const tide_world *verified = tide_client_verified_world(f->clients[i]);
        if (!verified || status.state != TIDE_SESSION_CONNECTED || verified->Control.phase != phase) return false;
        const bool private_owner = phase == 1 ? i == 1 : i == 0;
        if (tide_entity_alive(&verified->entities, verified->Control.chunk) != private_owner
            || tide_has_Secret(verified, verified->Control.card) != private_owner
            || tide_entity_alive(&verified->entities, verified->Control.player) == private_owner) return false;
        if (phase == 3 && tide_get_Health(verified, verified->Control.card).value > 980) return false;
        if (!projection_matches(f, i)) return false;
    }
    return true;
}

TIDE_TEST(visibility_generated_sessions_stream_reveals_concealments_and_hidden_impacts_under_loss)
{
    projection_network f = {0};
    memset(f.history_ticks, 0xff, sizeof f.history_ticks);
    f.history = calloc(PROJECTION_HISTORY, sizeof *f.history);
    TIDE_REQUIRE(f.history != NULL);
    f.net = tide_loopback_create(0x7669736962696c69ULL);
    tide_loopback_set_conditions(f.net, (tide_net_conditions){.latency = 0.03, .jitter = 0.01, .loss = 0.1});
    f.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 20,
        .transports = {tide_loopback_endpoint(f.net, 1)}, .wait_for_first = true}, f.now);
    f.clients[0] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
        .transport = tide_loopback_endpoint(f.net, 2), .server = tide_loopback_address(1),
        .sample = sample_projection, .user = &f.phases[0], .lead = 2}, f.now);
    for (unsigned i = 0; i < 1800 && tide_client_status_of(f.clients[0]).state != TIDE_SESSION_CONNECTED; i++) step_projection(&f);
    TIDE_REQUIRE(tide_client_status_of(f.clients[0]).state == TIDE_SESSION_CONNECTED);
    TIDE_REQUIRE(tide_client_status_of(f.clients[0]).player.id == 1);
    f.clients[1] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
        .transport = tide_loopback_endpoint(f.net, 3), .server = tide_loopback_address(1),
        .sample = sample_projection, .user = &f.phases[1], .lead = 2}, f.now);
    for (unsigned i = 0; i < 1800 && !projection_stage(&f, 0); i++) step_projection(&f);
    TIDE_CHECK(projection_stage(&f, 0));
    TIDE_CHECK(tide_client_status_of(f.clients[1]).player.id == 2);
    for (int phase = 1; phase <= 3; phase++) {
        f.phases[0] = phase;
        for (unsigned i = 0; i < 1200 && !projection_stage(&f, phase); i++) step_projection(&f);
        TIDE_CHECK(projection_stage(&f, phase));
    }
    const uint32_t verified = tide_client_status_of(f.clients[1]).verified_tick;
    for (unsigned i = 0; i < 240; i++) step_projection(&f);
    TIDE_CHECK(tide_client_status_of(f.clients[1]).verified_tick >= verified + 20);
    TIDE_CHECK(projection_matches(&f, 0));
    TIDE_CHECK(projection_matches(&f, 1));
    tide_client_destroy(f.clients[1]);
    tide_client_destroy(f.clients[0]);
    tide_server_destroy(f.server);
    tide_loopback_destroy(f.net);
    for (unsigned i = 0; i < PROJECTION_HISTORY; i++) tide_world_free(&f.history[i]);
    free(f.history);
}
