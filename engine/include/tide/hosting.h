#pragma once

#include "tide/session.h"

// One game build, many independent authoritative matches. Call from one host
// thread; ticks may use the shared jobs pool. No window, local player or views.
// The supplied transport is owned until destroy. Credentials require an
// encrypted, authenticated transport; plain UDP is for anonymous testing only.
#define TIDE_HOSTING_CREDENTIAL_BYTES 512u
#define TIDE_HOSTING_PARAMETER_BYTES 512u
#define TIDE_HOSTING_IDENTITY_BYTES 32u

typedef struct tide_hosting tide_hosting;
typedef struct tide_hosting_client tide_hosting_client;

typedef enum tide_hosting_result {
    TIDE_HOSTING_PENDING,
    TIDE_HOSTING_ACCEPTED,
    TIDE_HOSTING_DENIED,
    TIDE_HOSTING_FULL,
    TIDE_HOSTING_NOT_FOUND,
    TIDE_HOSTING_TIMED_OUT,
    TIDE_HOSTING_INVALID,
    TIDE_HOSTING_WRONG_VERSION,
} tide_hosting_result;

typedef struct tide_hosting_request {
    uint64_t request; // Host-assigned handle, for asynchronous authorization.
    uint64_t match;   // 0 requests a new match; otherwise join this match.
    tide_address peer;
    const uint8_t *credential;
    uint32_t credential_size;
    const uint8_t *parameters; // Application-defined create parameters, copied.
    uint32_t parameter_size;
} tide_hosting_request;

typedef struct tide_hosting_grant {
    // Stable opaque identity supplied by the verifier, never by the client.
    // Zero means anonymous. Identity and credentials never enter world bytes.
    uint8_t identity[TIDE_HOSTING_IDENTITY_BYTES];
} tide_hosting_grant;

typedef struct tide_hosting_desc {
    const tide_game *game;
    tide_transport transport;
    const tide_jobs *jobs;
    uint32_t max_matches; // 0: 64; at most 4096.
    uint32_t max_connections; // 0: 256; at most 65536.
    double idle_seconds; // Empty matches expire after this; 0: 60 seconds.
    bool allow_anonymous_create;
    bool allow_anonymous_join;
    uint64_t (*entropy)(void); // Required: IDs and admission attempts are not reused.
    void *user;
    // True only where the transport protects credentials, e.g. WebRTC/DTLS.
    bool (*secure_peer)(void *user, tide_address peer);
    // Called once per request, outside ticks. PENDING is completed with resolve.
    // With no verifier, the explicit anonymous policies above decide admission.
    tide_hosting_result (*authorize)(void *user, const tide_hosting_request *request,
                                    tide_hosting_grant *grant);
    // Builds a normal server description for a newly authorized match. The
    // host supplies game/transports/jobs/entropy; select start/seed/rate here.
    // start must remain valid after return until immediate server creation;
    // use host-owned/static storage, not a callback-local stack value.
    bool (*start)(void *user, const tide_hosting_request *request, tide_server_desc *match);
    // Standard service lifecycle, outside ticks. ready runs once; poll can
    // complete external auth work or create/stop matches through the host API.
    void (*ready)(void *user, tide_hosting *host);
    void (*poll)(void *user, tide_hosting *host, double now);
} tide_hosting_desc;

tide_hosting *tide_hosting_create(const tide_hosting_desc *desc, double now);
void tide_hosting_destroy(tide_hosting *host);
void tide_hosting_update(tide_hosting *host, double now);
bool tide_hosting_resolve(tide_hosting *host, uint64_t request, tide_hosting_result result,
                          const tide_hosting_grant *grant);
// Trusted in-process entry points; never exposed as unauthenticated requests.
uint64_t tide_hosting_start(tide_hosting *host, const tide_server_desc *desc);
bool tide_hosting_stop(tide_hosting *host, uint64_t match);
uint32_t tide_hosting_match_count(const tide_hosting *host);
const void *tide_hosting_world(const tide_hosting *host, uint64_t match);
bool tide_hosting_player_identity(const tide_hosting *host, uint64_t match, tide_player_id player, uint8_t identity[32]);

typedef struct tide_hosting_client_desc {
    tide_transport transport;
    tide_address server;
    uint64_t match; // 0 creates; nonzero joins.
    uint64_t nonce; // Fresh nonzero unpredictable value per attempt.
    uint8_t simulation[32]; // Generated game's compatibility identity.
    const uint8_t *credential;
    uint32_t credential_size;
    const uint8_t *parameters;
    uint32_t parameter_size;
    bool secure; // The caller guarantees the transport's protection.
} tide_hosting_client_desc;

// Owns the supplied transport on success only. Requests retry idempotently.
tide_hosting_client *tide_hosting_client_create(const tide_hosting_client_desc *desc, double now);
void tide_hosting_client_destroy(tide_hosting_client *client);
void tide_hosting_client_update(tide_hosting_client *client, double now);
tide_hosting_result tide_hosting_client_result(const tide_hosting_client *client);
uint64_t tide_hosting_client_match(const tide_hosting_client *client);
tide_address tide_hosting_client_address(const tide_hosting_client *client);
// Once accepted, this endpoint is handed to tide_client/tide_session. Closing
// it detaches the endpoint; destroy the hosting client after the session.
tide_transport tide_hosting_client_transport(tide_hosting_client *client);
