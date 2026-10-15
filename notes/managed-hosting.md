# Managed hosting and deployment

## Status and agreed scope (2026-10-15)

Implemented first local-build hosting workflow, at the owner's request. GitHub
sign-in, Stripe prepaid credit and the existing Fly.io organization/relay are
selected. `hosting/README.md` is the operator/API/ABI reference;
`docs/multiplayer/hosting.md` is the developer guide. No new Tide syntax.

The service has not been deployed. No real OAuth/payment credentials, customer
payments or paid infrastructure were used during implementation validation.
Allocations in the Fly configuration are disabled and prices default to zero
(which also disables allocation). Public paid launch requires provider setup,
measured rates and the remote/security/restore checks in the operator guide.
The inactive remote mod build service remains on hold: this work does not
restore its source-build, signing or closed-source requirements.

## Commands, artifacts and compatibility

- `tide login` uses a ten-minute device code explicitly approved in an authenticated
  browser. CLI credentials last thirty days, are bound to the service origin and
  use user-bound DPAPI on Windows/private files on POSIX. `tide logout` revokes.
- `tide deploy [folder] --project name-or-id` builds locally in release mode:
  a single-threaded WASI reactor, its simulation asset pack and a web client.
  `--server-only` omits the client; `--bundle-only` builds without authentication.
  The default result includes the release, game URL, room endpoint and dashboard.
- `TIDEDEP1` bundles contain an LE32 manifest length, JSON and up to three files
  (`server.wasm`, `server.pack`, `client.html`). SHA-256 of the exact bundle is
  the immutable release ID; per-file sizes/digests are verified. A new release
  changes new matches, while old running/saved matches retain their release.
- Simulation ID is a separate generated SHA-256. Admission protocol 2 checks
  it before allocation and session protocol 15 checks it before joining.
  `tide_game.hash` remains the generated-layout/hot-reload identity.
- The compiler fingerprints an ordered canonical graph of match schema/defaults,
  systems, handlers, scenes/prefabs, tasks, transitive helpers, input sanitation
  and prediction, settings and simulation-read assets. Local fields, views and
  presentation-only dependencies are excluded. Shared dependencies participate.
  Archetype IDs/masks remain conservative wire dependencies.
- Toolchain identity includes normalized compiler/runtime sources, relevant
  build policy and Tide version, detecting source edits without manual version
  increments. CLI custom C/C++/headers and all library target variants are
  conservative inputs, including the optimization mode when C is present.
  Custom C also makes every packed asset a conservative compatibility input;
  C can read assets without a visible Tide dependency.
  Client/server identity is calculated before stripping; final binary bytes
  are never the compatibility hash. The same inputs share native/web/server IDs.
- Compatibility metadata is neither authentication nor proof that untrusted
  uploaded code obeys its claimed simulation identity.

## Service and authentication

Node 24.16+, ESM, SQLite WAL/FULL synchronization, node-datachannel 0.33.4. The
service owns accounting, routing, credentials and lifecycle outside guest state.
Developer accounts are invite-only by stable numeric GitHub ID. OAuth uses
PKCE S256, browser-bound one-use state and no repository permissions. Provider
access tokens are discarded after identity lookup. Dashboard sessions are
Secure/HttpOnly/SameSite cookies with Origin/CSRF checks on mutations. Browser
and game content have separate origins; game-host requests cannot access APIs.

Browser credentials last eight hours; CLI and runtime credentials thirty days.
Runtime credentials are revealed once, hashed at rest, revocable and scoped to
one project's match controls. They cannot publish releases or access billing,
accounts or other projects. Public client match creation can be disabled per
project while trusted runtime keys continue creating matches. Match IDs are
routing identifiers, not authorization tokens. Managed player authentication is
not implemented; supplied player credentials are rejected rather than trusted.
Native dedicated authorization callbacks remain a separate supported path.
Runtime keys have a twenty/account limit requiring explicit revocation; browser
and CLI session rotation cannot silently revoke a backend's runtime key.

Stripe hosted Checkout accepts operator-configured amounts/currency. Raw webhook
HMAC/time checks precede provider-state verification; exact account/amount/
currency/payment intent must match the stored checkout. Fulfillment is idempotent.
Refunds/disputes freeze hosting, including reversals delivered before completion.
A redirect never grants credit. No public operator/credit-minting endpoint.

## Execution, transport and entry point

Each match has a separate Worker and WASM instance/memory/globals. Guest C/C++
runs inside WASM too. There is no Node WASI, filesystem, process environment or
arbitrary network import. Module import/table/memory validation happens before
instantiation. Imported nonshared wasm32 memory has a preset maximum. The host
supplies bounded packets, entropy, clocks, stdout and an explicit asset pack.
A wall-time watchdog terminates nonreturning WASM; sampled Worker thread CPU
limits returning but over-budget workloads. JS heap and packet queues are bounded.
Worker memory statistics include actual WASM linear-memory bytes. Per-match
limits do not claim to fully bound Node/JIT/native RTC process RSS; production
operational/security review and host-level monitoring remain necessary.

Managed ABI 1 lives in `platform/include/tide/managed_run.h`: allocate/free,
init/update/receive/status, simulation ID and save/restore. It uses the existing
session server with raw packet queues and no platform/render/audio library.
A match `Main` starts automatically. A game with local-menu `Main` supplies
`tide_server_entry` and selects its generated typed `tide_start`, receiving
bounded start parameters. Native process hooks authorize/secure_peer/ready/poll
are rejected in managed instances; trusted orchestration lives in the service.
Guard server C with `TIDE_SERVER_BUILD` before including generated `game.h`.

The host joins the existing relay as a project room via node-datachannel and
routes admission protocol 2/multiplexed packets to one guest per match. Nonces,
peer-bound routes, fragment queues and daily egress are bounded. Wrong simulation
is rejected before credit reservation. Project room identity survives restart;
rooms are restored on startup and signaling refreshes before TURN expiry.
A hosted `?match=id` URL serves that match's original client after newer releases
are published. Server-only releases expose a room endpoint but no play link.
An unpaid project cannot use the service as free static hosting.

## Lifetime, durable saves and recovery

Default lifetime is while occupied, with sixty-second empty grace configurable
from five seconds to one hour. The timer starts at allocation, resets while
players are present, and counts the session server's connected players. Explicit
lifetime has no idle expiry. Project defaults can be overridden per match.
`Session.Close` controls admission; `Session.End` ends the guest match.

Explicit worlds save at startup/every sixty seconds, before suspension and on
graceful shutdown. Optional paired C callbacks `tide_managed_write_save` and
`tide_managed_read_save` own the durable payload. Without them, generated world
packing supplies an exact-release checkpoint, not a cross-release save format.
A host envelope preserves reconnect identities/cookies, pending player sets,
seed, tick timing, admission/start metadata outside deterministic world bytes.

A new content-addressed save is flushed/renamed before the SQLite checkpoint
changes; interruption retains the previous completed save. Recovery uses the
same release, match ID and latest completed save, with 1/2/4-second backoff and
at most three consecutive attempts. Five healthy minutes reset the retry count.
Failed restoration settles the reservation without retaining a ghost allocation.
Ended/payment-suspended worlds stay stopped. Service restart recovers only
interrupted desired-running explicit worlds. Incidents include save time and
potential progress loss. Notifications are in-dashboard; no email integration.

## Prepaid limits and operator exposure

Provisional defaults, configurable as described in README, are implementation
policy, not language semantics or approved launch pricing:

| Control | Default |
|---|---|
| Capacity | Two active matches/account and host; project maximum one or two |
| Small / Standard | 64/128 MiB, 0.25/0.5 sampled CPU core, 20/40 ms update deadline |
| Fly host | One fixed 512 MiB shared CPU machine in fra, 128 MiB total guest reservation |
| Credit | Thirty-second allocation lease, renew with five seconds left |
| Saves | 8 MiB each, eight/account and sixteen/host, seven inactive days |
| Releases/storage | Six releases and 64 MiB/account, 512 MiB/host |
| Uploads | 32 MiB bundle, 8 MiB server WASM |
| Daily creation | 200/account and 1000/host, including failed starts |
| Daily egress | 1 GiB/account and host, reserved before application sends |
| Connections | 64 peers/host, 32/project |

Balances use integer millionths of minor currency units (100,000,000 = EUR 1).
Allocated resource seconds consume prepaid credit; unused whole reserved seconds
are refunded once. Low-credit incidents warn; exhaustion blocks allocation and
saves/suspends explicit worlds. Resume rechecks credit, capacity and operator
allocation switch. Frozen accounts stop. No postpaid credit or auto-top-up.
Traffic/storage have hard included allowances, not surprise overage charges.
Application egress counts are not physical provider traffic or an exact currency
ceiling. Rates require measurement and explicit operator configuration.
Admission replies, game datagrams, hosted clients and authenticated save/log
downloads all reserve outgoing transfer; repeated version refusals cannot use
an unmetered application-data path. HTTP/RTC protocol overhead remains separate.

Inactive saves warn during the last day and expire after seven days; active
saves are protected. Running/saved releases cannot be deleted. Unsaved ended
matches and detailed usage retain thirty days, then usage becomes exact daily
totals. Payment deduplication persists. Logs are in-memory, bounded to 100 lines
for at most 32 matches; incidents are durable and bounded per account.

Fly runs a separate app/volume and origins, not the docs service. It has no
provisioning API token or autoscaling permission. Environment allocation enable
and the persisted admin allocation switch both gate creation/resume; existing
paid leases continue when new allocation is blocked. `admin.mjs` supplies invites,
freeze/unfreeze, aggregate status and that emergency switch. One volume is not
high availability or an independent backup.

## Validation and remaining launch work

Validation exercises SQLite/auth/payment mocks, real HTTP, WASM isolation,
memory growth limits, infinite-loop interruption, returning CPU-heavy guests,
credit/caps, checkpoint atomicity, retention, bounded recovery, real local relay/
WebRTC admission and cross-match routing. Gated tests build actual Tide games,
compare client/server IDs, test game-owned saves, run CLI device login/deploy/
logout and connect a muted headless Chrome client to an uploaded guest match.
Dashboard desktop/mobile screenshots and overflow are checked. Compiler/session
regressions cover simulation changes, view exclusions and restored reconnects.
Final suite/package results are recorded below when validation completes.

Docker builds and imports the Linux native RTC module; Fly configuration is
validated locally. Still required before enabling paid hosting: real GitHub and
Stripe test-mode setup, refund drills, remote Fly/TURN native/browser connectivity,
process/volume restart drills, external backups, process resource measurements,
security review and customer-facing rates/retention. No CI was dispatched.

Unbuilt: multi-host/region scaling, managed player auth, email notifications,
remote builds, compiled-module caching, automatic native client updates,
production match hot reload, cross-release save upgrades and auto top-up.

## Design references

Official references used during design: [GameLift scaling](https://docs.aws.amazon.com/gameliftservers/latest/developerguide/fleets-autoscaling-target.html),
[PlayFab standby](https://learn.microsoft.com/en-us/gaming/playfab/multiplayer/servers/scaling-standby),
[Agones buffers](https://agones.dev/site/docs/reference/fleetautoscaler/),
[Steam updates](https://partner.steamgames.com/doc/store/updates?l=english),
[WASM security model](https://webassembly.org/docs/security/) and
[TUF trust](https://theupdateframework.io/docs/faq/). These informed controls;
provider selection is the owner's subsequent GitHub/Stripe/Fly decision.

Validation before integration: Clang 23.1.2/Node 24.16.0, full native debug
2,300/2,300 and web debug 2,250/2,250 CTests (including GPU/WebGPU). The final
conservative-C asset-watch correction additionally passes all 13 asset-usage
cases and installed debug/release web-watch checks. Installed native/web cache,
stripping, parallel compilation and invalidation phases pass; final integrated
package validation is repeated after merging upstream. All 31 hosting tests pass
with actual CLI/native/web/server identity comparison and Chrome integration
enabled. Desktop/mobile captures were inspected, with no horizontal overflow.
The docs site and browser compiler build, Docker image builds, Linux RTC/HTTP
smoke passes, and Fly configuration validates. Other native OS/Android and real
provider/Fly connectivity coverage remain unrun; no CI dispatch.

Integration validation after merging origin/dev at bf444057: both notes-only
conflicts retain the hosting and incoming frustum records. Complete native debug
2,305/2,305 and web debug 2,255/2,255 CTests pass. The full private-installation
native/web cache, release stripping, parallel/failure invalidation, debug/release
watch and dedicated-server integration script passes. All 31 hosting tests pass
with native/web/server IDs and real Chrome/relay/guest execution enabled. Final
policy regressions cover admission transfer reservations, save/log download
limits, cross-project runtime-key refusal/revocation and preserving runtime keys
through repeated developer sign-ins. Project cards show the allocated hourly
rate. The merged docs site/browser compiler build and Linux Docker RTC/HTTP plus
actual generated-WASM save/restore smoke pass; Fly configuration validates.
Screenshots and the whitespace diff were checked. Provider/Fly launch checks and
other-platform native coverage remain as listed above; no deployment or CI run.

## Stripe planner review and sandbox setup (2026-10-15)

At the owner's request, installed stripe@openai-curated and registered the
https://mcp.stripe.com server in user-level Codex configuration. The owner
completed OAuth in their own browser. The existing running agent did not expose
new MCP tools, so a fresh Codex app-server connection invoked the authenticated
MCP directly, without running an additional agent/model turn or reading OAuth
credentials. stripe_implementation_planner was called with Tide's existing
prepaid architecture and its decision-tree selections were accepted. No skills
fallback was needed. The authorized account is Tide Engine sandbox (test mode).

The planner selects hosted one-time Checkout. Keep the synchronous prepaid
allocation ledger and hard hosting limits. Recommended next steps, not newly
approved product/pricing requirements: persistent Stripe Customer mapping and
optional paid invoice documents for top-ups, with any later recurring/usage
Billing package designed separately. Checkout post-payment invoice generation
is priced separately; do not enable it or tax collection merely because the
products were selected in onboarding. Invoice events must not double-credit
purchases already fulfilled through Checkout. Tax treatment/registrations and
net service credit versus total payment need resolution before live collection.

Confirmed findings in hosting/payments.mjs:

- The live/test check recognizes sk_live_ only. A correctly signed live event
  is rejected with rk_live_, preventing fulfillment using a live restricted key.
  Fix the mode check before live launch and test both restricted key modes.
- A fresh idempotency key is generated per checkout request, with the payment
  record inserted after Stripe returns. A simulated local persistence failure
  followed by retry produces a second Checkout Session. Persist a purchase ID
  before the external request and reuse its idempotency key/parameters on retry.
  This reproduction establishes duplicate/orphan sessions; it does not establish
  a real double charge or loss of a paid purchase in the normal response flow.
- API requests do not pin Stripe-Version; webhook and API versions should be
  tested and pinned together. Checkout currently hardcodes card-only methods;
  use Dashboard-managed dynamic methods and test delayed settlement/reversal
  behavior before offering additional methods. Customer mapping, invoice
  generation/links and reconciliation of missed notifications remain unbuilt.

A real sandbox API read found no webhook endpoints. Created and read back
we_1UQpaeEOEguEtdxRUxRRVN8g for
https://console.tide-engine.dev/webhooks/stripe, pinned to
2026-09-30.endive (verified supported by Stripe's API schema), with only
checkout.session.completed, checkout.session.async_payment_succeeded,
charge.refunded and charge.dispute.created. Its signing secret is stored outside
the checkout in the current user's LocalAppData/Tide/hosting-setup/stripe-webhook.env,
with a user-only Windows ACL. No secret value was printed or committed. The
endpoint is registered; public delivery is not validated because the hosting
service is not deployed. No live objects, payments, subscriptions, tax settings,
pricing changes or paid services were created.

Stripe's API schema lists checkout_session_write for POST /v1/checkout/sessions.
The current runtime uses only Checkout Session create/retrieve, so begin with a
sandbox restricted key granting Checkout Sessions Write (which includes Read),
then validate the actual runtime flows in the sandbox before provisioning the
live equivalent. Customer/invoice API permissions can follow the implemented
features. Runtime keys do not need webhook administration rights. Creating the
runtime key remains an owner Dashboard/2FA step; the MCP API search did not expose
a restricted-key creation operation. The owner will push secrets to Fly.

Validation: 14 existing auth/payment/store tests pass; two isolated mock-provider
reproductions confirm the key-mode and retry findings. Runtime code is unchanged
by this review. Private setup helpers/results are under ignored build/; the
webhook secret is outside the repository. Local references and whitespace diff
checked. Official references: [restricted keys](https://docs.stripe.com/keys/restricted-api-keys),
[idempotent requests](https://docs.stripe.com/api/idempotent_requests),
[paid invoice receipts](https://docs.stripe.com/receipts) and
[Stripe MCP](https://docs.stripe.com/mcp).
