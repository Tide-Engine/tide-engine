#include "game.h"
#include "tide/hosting.h"
#include "tide_test.h"

#include <string.h>

static tide_loopback *net;
static tide_hosting *host;
static tide_hosting_client *admissions[4];
static tide_client *clients[4];
static double now;
static uint64_t random_id;
static uint64_t pending;
static int authorizations;

static uint64_t entropy(void) { return ++random_id; }

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    *(Controls *)input = (Controls){.step = (int32_t)(uintptr_t)user};
}

static void begin(const bool anonymous, const tide_net_conditions conditions)
{
    now = 0;
    random_id = 1000;
    pending = 0;
    authorizations = 0;
    memset(admissions, 0, sizeof admissions);
    memset(clients, 0, sizeof clients);
    net = tide_loopback_create(42);
    tide_loopback_set_conditions(net, conditions);
    const tide_hosting_desc d = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 1),
        .max_matches = 3, .max_connections = 8, .idle_seconds = 3, .entropy = entropy,
        .allow_anonymous_create = anonymous, .allow_anonymous_join = anonymous};
    host = tide_hosting_create(&d, now);
}

static void request_match(const int i, const uint64_t match)
{
    const tide_hosting_client_desc d = {.transport = tide_loopback_endpoint(net, (uint32_t)i + 2u),
        .server = tide_loopback_address(1), .match = match, .nonce = 100u + (uint64_t)i};
    admissions[i] = tide_hosting_client_create(&d, now);
}

static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 0.01;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 4; i++) {
            if (admissions[i]) tide_hosting_client_update(admissions[i], now);
            if (clients[i]) tide_client_update(clients[i], now);
        }
        tide_hosting_update(host, now);
    }
}

static void join_match(const int i, const int step)
{
    const tide_client_desc d = {.game = &tide_game_api, .transport = tide_hosting_client_transport(admissions[i]),
        .server = tide_hosting_client_address(admissions[i]), .sample = sample, .user = (void *)(uintptr_t)step, .lead = 2};
    clients[i] = tide_client_create(&d, now);
}

static void finish(void)
{
    for (int i = 0; i < 4; i++) {
        tide_client_destroy(clients[i]);
        tide_hosting_client_destroy(admissions[i]);
    }
    tide_hosting_destroy(host);
    tide_loopback_destroy(net);
}

TIDE_TEST(hosting_matches_are_isolated_and_keep_clients_in_sync)
{
    begin(true, (tide_net_conditions){.latency = 0.02, .jitter = 0.02, .loss = 0.15});
    TIDE_CHECK(host != NULL);
    request_match(0, 0);
    request_match(1, 0);
    run(1);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_ACCEPTED);
    TIDE_CHECK(tide_hosting_client_result(admissions[1]) == TIDE_HOSTING_ACCEPTED);
    const uint64_t a = tide_hosting_client_match(admissions[0]), b = tide_hosting_client_match(admissions[1]);
    TIDE_CHECK(a && b && a != b);
    TIDE_CHECK(tide_hosting_match_count(host) == 2);
    join_match(0, 2);
    join_match(1, 7);
    request_match(2, a);
    run(1);
    TIDE_CHECK(tide_hosting_client_result(admissions[2]) == TIDE_HOSTING_ACCEPTED);
    join_match(2, 0);
    run(4);
    const tide_world *wa = tide_hosting_world(host, a), *wb = tide_hosting_world(host, b);
    TIDE_CHECK(wa && wb && wa != wb);
    TIDE_CHECK(wa->State.joined == 2);
    TIDE_CHECK(wb->State.joined == 1);
    TIDE_CHECK(wb->State.total > wa->State.total);
    for (int i = 0; i < 3; i++) {
        const tide_client_status status = tide_client_status_of(clients[i]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0);
    }
    const int32_t before = wb->State.ticks;
    TIDE_CHECK(tide_hosting_stop(host, a));
    TIDE_CHECK(!tide_hosting_world(host, a));
    run(0.5);
    TIDE_CHECK(((const tide_world *)tide_hosting_world(host, b))->State.ticks > before);
    TIDE_CHECK(tide_hosting_match_count(host) == 1);
    finish();
}

TIDE_TEST(hosting_creation_is_idempotent_bounded_and_expires_empty_matches)
{
    begin(true, (tide_net_conditions){.loss = 0.5});
    request_match(0, 0);
    run(2);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_ACCEPTED);
    TIDE_CHECK(tide_hosting_match_count(host) == 1);
    run(4);
    TIDE_CHECK(tide_hosting_match_count(host) == 0);
    TIDE_CHECK(!tide_hosting_stop(host, tide_hosting_client_match(admissions[0])));
    finish();

    begin(false, (tide_net_conditions){0});
    request_match(0, 0);
    run(0.5);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_DENIED);
    TIDE_CHECK(tide_hosting_match_count(host) == 0);
    finish();

    begin(true, (tide_net_conditions){0});
    for (int i = 0; i < 4; i++) request_match(i, 0);
    run(0.5);
    TIDE_CHECK(tide_hosting_match_count(host) == 3);
    TIDE_CHECK(tide_hosting_client_result(admissions[3]) == TIDE_HOSTING_FULL);
    finish();
}

static tide_hosting_result authorize(void *user, const tide_hosting_request *request, tide_hosting_grant *grant)
{
    (void)user;
    (void)grant;
    authorizations++;
    pending = request->request;
    return TIDE_HOSTING_PENDING;
}

TIDE_TEST(hosting_authentication_is_async_fail_closed_and_outside_worlds)
{
    begin(false, (tide_net_conditions){0});
    tide_hosting_destroy(host);
    const tide_hosting_desc d = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 1),
        .entropy = entropy, .authorize = authorize};
    host = tide_hosting_create(&d, now);
    request_match(0, 0);
    run(1);
    TIDE_CHECK(pending != 0);
    TIDE_CHECK(authorizations == 1);
    TIDE_CHECK(tide_hosting_match_count(host) == 0);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_PENDING);
    const tide_hosting_grant grant = {.identity = {1, 2, 3}};
    TIDE_CHECK(tide_hosting_resolve(host, pending, TIDE_HOSTING_ACCEPTED, &grant));
    TIDE_CHECK(!tide_hosting_resolve(host, pending, TIDE_HOSTING_ACCEPTED, &grant));
    run(0.5);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_ACCEPTED);
    join_match(0, 3);
    run(1);
    TIDE_CHECK(tide_client_status_of(clients[0]).state == TIDE_SESSION_CONNECTED);
    request_match(1, tide_hosting_client_match(admissions[0]));
    run(0.5);
    TIDE_CHECK(tide_hosting_resolve(host, pending, TIDE_HOSTING_DENIED, NULL));
    run(0.5);
    TIDE_CHECK(tide_hosting_client_result(admissions[1]) == TIDE_HOSTING_DENIED);
    TIDE_CHECK(((const tide_world *)tide_hosting_world(host, tide_hosting_client_match(admissions[0])))->State.joined == 1);
    finish();
}

TIDE_TEST(hosting_authenticated_reconnect_cannot_take_another_accounts_slot)
{
    begin(false, (tide_net_conditions){0});
    tide_hosting_destroy(host);
    const tide_hosting_desc d = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 1),
        .entropy = entropy, .authorize = authorize};
    host = tide_hosting_create(&d, now);
    const tide_hosting_grant a = {.identity = {11}}, b = {.identity = {22}};
    request_match(0, 0);
    run(0.1);
    TIDE_CHECK(tide_hosting_resolve(host, pending, TIDE_HOSTING_ACCEPTED, &a));
    run(0.1);
    join_match(0, 2);
    run(0.5);
    const tide_client_status original = tide_client_status_of(clients[0]);
    TIDE_CHECK(original.state == TIDE_SESSION_CONNECTED && original.cookie != 0);
    const uint64_t match = tide_hosting_client_match(admissions[0]);
    request_match(1, match);
    run(0.1);
    TIDE_CHECK(tide_hosting_resolve(host, pending, TIDE_HOSTING_ACCEPTED, &b));
    run(0.1);
    const tide_client_desc stolen = {.game = &tide_game_api, .transport = tide_hosting_client_transport(admissions[1]),
        .server = tide_hosting_client_address(admissions[1]), .cookie = original.cookie};
    clients[1] = tide_client_create(&stolen, now);
    run(0.5);
    TIDE_CHECK(tide_client_status_of(clients[1]).state != TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(clients[0]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(((const tide_world *)tide_hosting_world(host, match))->State.joined == 1);
    request_match(2, match);
    run(0.1);
    TIDE_CHECK(tide_hosting_resolve(host, pending, TIDE_HOSTING_ACCEPTED, &a));
    run(0.1);
    join_match(2, 2); // Same authenticated identity can resume without a cookie.
    run(0.5);
    TIDE_CHECK(tide_client_status_of(clients[2]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_player_equal(tide_client_status_of(clients[2]).player, original.player));
    TIDE_CHECK(((const tide_world *)tide_hosting_world(host, match))->State.joined == 1);
    finish();
}

TIDE_TEST(hosting_credentials_require_transport_protection_and_requests_are_bounded)
{
    begin(false, (tide_net_conditions){0});
    tide_hosting_destroy(host);
    const tide_hosting_desc hd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 1),
        .entropy = entropy, .authorize = authorize};
    host = tide_hosting_create(&hd, now);
    const uint8_t token[] = {1, 2, 3};
    tide_hosting_client_desc d = {.transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .nonce = 123, .credential = token, .credential_size = sizeof token};
    TIDE_CHECK(!tide_hosting_client_create(&d, now));
    d.secure = true; // Even a lying client cannot bypass the host's transport check.
    admissions[0] = tide_hosting_client_create(&d, now);
    run(0.1);
    TIDE_CHECK(tide_hosting_client_result(admissions[0]) == TIDE_HOSTING_DENIED);
    TIDE_CHECK(authorizations == 0);
    TIDE_CHECK(tide_hosting_match_count(host) == 0);
    tide_transport raw = tide_loopback_endpoint(net, 3);
    uint8_t bytes[40] = {0};
    tide_writer w = {.data = bytes, .capacity = sizeof bytes};
    tide_write_u32(&w, UINT32_C(0x48534454));
    tide_write_u8(&w, 1);
    tide_write_u8(&w, 1);
    tide_write_u16(&w, 0);
    tide_write_u64(&w, 456);
    tide_write_u64(&w, 0);
    tide_write_u16(&w, UINT16_MAX);
    tide_write_u16(&w, UINT16_MAX);
    for (uint32_t n = 0; n <= w.size; n++) raw.send(raw.self, tide_loopback_address(1), bytes, n);
    run(0.1);
    TIDE_CHECK(tide_hosting_match_count(host) == 0);
    raw.close(raw.self);
    finish();
}
