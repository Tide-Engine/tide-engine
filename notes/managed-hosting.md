# Managed hosting and deployment

## Status and agreed scope (2026-10-15)

Implemented first local-build hosting workflow, at the owner's request. GitHub
sign-in, Stripe prepaid credit and the existing Fly.io organization/relay are
selected. `hosting/README.md` is the operator/API/ABI reference;
`docs/multiplayer/hosting.md` is the developer guide. No new Tide syntax.

The first Fly deployment completed on 2026-10-15 through Hosting run
38754895170 attempt 2 (341051c4): one 512 MiB shared CPU machine in `fra`, one
1 GiB `hosting_data` volume, a passing Fly health check and `{"ok":true}` from
the public Fly hostname with the console Host header. All four provider secrets
are now deployed. Cloudflare DNS and Fly-managed TLS for console.tide-engine.dev
and play.tide-engine.dev are configured and verified; both HTTPS health requests
return 200. The console homepage serves GitHub sign-in. Real provider/payment
flows remain unvalidated. No customer
payment was made. Earlier implementation validation used no real provider
credentials or paid infrastructure. Allocations remain disabled in the Fly
configuration and prices default to zero
(which also disables allocation). Public paid launch requires provider setup,
measured rates and the remote/security/restore checks in the operator guide.
The inactive remote mod build service remains on hold: this work does not
restore its source-build, signing or closed-source requirements.

## Commands, artifacts and compatibility

- `tide login` uses a ten-minute device code explicitly approved in an authenticated
  browser. CLI credentials last thirty days, are bound to the service origin and
  use user-bound DPAPI on Windows/private files on POSIX. `tide logout` revokes.
- `tide deploy [folder]` creates/reuses a folder-named project and saves its ID
  and service origin in the game's `tide.deploy.json`. Repeat deploys keep that
  target after a rename; `--project name-or-id` explicitly replaces the link.
  Unavailable IDs, malformed links and service mismatches fail with selection
  guidance. Credentials stay outside the game; the link can be committed.
  Folder names normalize to lowercase ASCII letters/digits/hyphens, at most 48
  characters, prefixed with `game-` when they do not start with a letter.
  This builds locally in release mode:
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

Folder-first deployment and credit discovery (2026-10-15): the owner requested
deployment without dashboard project setup and reported an empty credit selector.
CLI project auto-creation already existed; the UI wrongly implied manual setup
was required. The empty Projects panel now leads with `tide login`/`tide deploy`,
with custom project creation secondary. The CLI persists its target as described
above, using an atomic replacement and verifying ownership through account data.
`--bundle-only` remains offline and does not read/write the deployment link.

The balance links directly to Add credit. The account response exposes payment
availability and sandbox mode, never provider secrets; amount selection appears
only with configured top-ups and both Stripe secrets. Unconfigured services show
a clear unavailable message. Test keys label Checkout as a sandbox/test payment.
After the owner confirmed sandbox testing, the running key was checked by prefix
only (sandbox=true, webhook configured); no secret value was retrieved. Fly
configuration now supplies EUR 5/10/25 top-ups. Match allocations/rates stay off.
A provider account read using the runtime key returns 200 and verifies the
expected Tide Engine sandbox account; key contents were not logged.

Validation: Svelte check has zero errors/warnings, the console and docs production
builds pass, Fly configuration validates and all 33 hosting tests pass with the
installed CLI/native/web tools and muted Chrome. Coverage includes persistent
folder links/renames, explicit selection, malformed/foreign/missing targets,
sandbox amount rendering, unavailable payments and a mocked Checkout POST
without granting credit. Desktop/phone captures of the empty project and credit
panel were inspected. The final CLI builds cleanly with strict C17 warnings.
The native debug full run passes 2,303/2,305; both server-pack clients connect
successfully, but their 5,000-frame server processes hit the existing 15-second
shutdown deadline under concurrent suite/build load. Both unchanged cases pass
in a sequential recheck (10.97/14.43 seconds). No deadline/assertion was relaxed.
All 2,255 web debug tests pass, including WebGL/WebGPU. Deployment results are
recorded after completion. Other native platforms/Android were not run; no CI
workflow was dispatched.

Integration through origin/dev 48ca18ce retains the concurrent visibility and
Foldline work. Resolved only the docs-site note's competing top insertions by
keeping both entries. Because the compiler/runtime dependencies changed, rebuilt
the CLI, refreshed its private installation's engine sources/headers and reran
all 33 hosting tests with native/web/server identity and browser cases enabled;
all pass. The merged docs production build passes with both guides retained.
Clarified that committing the target file grants no project access.

Deployed c85179a6 as Fly release 3, image deployment-01M5051CME9PYXNPZV5VNAMYRK,
on the existing machine/volume; smoke, health and public HTTPS health checks pass.
A real sandbox Checkout attempt then exposed Stripe rejecting the old
payment_method_types parameter (HTTP 400), which mocks had accepted. Following
[Stripe's migration guide](https://docs.stripe.com/payments/payment-methods/dynamic-payment-methods),
removed that parameter so configured Dashboard payment methods apply, and pinned
both session creation/retrieval to 2026-09-30.endive, matching the webhook version.
Regressions assert the omitted parameter/version and provider-state fulfillment.
All seven affected auth/payment/HTTP tests pass. Deployed df6c019a as Fly release
4, image deployment-01M5059C33CH27SD5CSKA0M4Z8, with passing machine checks and
public HTTPS health. A real sandbox Checkout Session for EUR 5 now creates
successfully using the deployed Payments module and configured runtime key;
the operator probe confirms the test checkout URL, all three top-up amounts
and unchanged account balance. One unpaid sandbox Checkout record was created;
no payment was completed or credit granted by the probe. Webhook fulfillment
and refund drills still require their own real sandbox transaction tests.
The machine/volume/capacity are unchanged and match allocations remain disabled.

Owner access and console appearance (2026-10-15): the owner's live GitHub
callback reached the invite gate. Resolved the authenticated local GitHub
identity as BlenMiner (4602596) and ran the operator `admin.mjs invite` on
tide-hosting's persistent database. This invites that account; a fresh browser
sign-in is needed because OAuth state is consumed once. No credit or allocation
enablement was added.

At the owner's request, the dashboard/sign-in UI now matches the website:
amber light/dark tokens, Inter Variable, the existing wave favicon/logo and
header typography. The local font and its OFL license ship in hosting/static.
A small external theme bootstrap selects saved/system appearance before CSS
paints; Svelte owns the toggle and preference persistence. Website and console
localStorage remain independent origins.

The owner then reported the initial-view flash and explicitly requested Svelte.
Replaced hosting/web's static shell and imperative DOM rendering with SvelteKit
3/Svelte 5 and adapter-node, pinned to the website's installed versions. Browser
session lookup happens in the existing HTTP service before adapter dispatch.
The authenticated account view is attached to that IncomingMessage, read by a
server load through adapter-node's event.platform.req, and shared with the JSON
account endpoint's existing serializer. This has no global per-user state or
internal credential-bearing HTTP hop. Svelte renders the chosen sign-in/account
view, balances, projects, matches and forms before hydration; the client polls
only for subsequent updates. Signed-in pages contain no welcome section.
SvelteKit's nonce CSP supports hydration without unsafe-inline; personalized
HTML/page data are private/no-store with Vary: Cookie. A private protocol header
is overwritten from the validated configured origin, not trusted from callers.
The game origin, API routes, session/CSRF checks and WASM workers stay separate.

The service's npm pretest now runs Svelte check (warnings fail) and the production
frontend build. Docker builds the UI in a separate stage and copies its compiled
output into the runtime image; no frontend build tools are installed there.

Browser GET failures on `/auth/github` and `/auth/callback` redirect to the
styled landing page with a fixed restricted/expired/unavailable reason, clearing
the one-use login cookies. A bounded valid CLI approval code is retained for
retry; OAuth codes/state and provider messages are not reflected. Svelte
renders the fixed message on the server and removes the reason after hydration. API
errors remain JSON and all OAuth/admission checks remain enforced. HTTP
regressions exercise denial, expired state, cookie clearing, secret omission,
device retry routing and retained API errors. All 31 hosting tests pass with
the installed CLI/native/web/managed build and real muted Chrome enabled.
HTTP checks additionally verify the initial signed-in/signed-out HTML, private
cache policy, nonce CSP, injection escaping and concurrent account isolation.
Browser checks disable JavaScript and still see the match/credit dashboard;
they verify persisted theme selection and styled OAuth recovery after hydration.
Desktop light/dark and phone dashboard/error captures were inspected with no
horizontal overflow. The Linux production image builds and renders both guest
and authenticated pages with private/no-store and nonce CSP, and serves the
theme script, logo and font with the intended MIME types.

Deployed source 2ee3d6d5 (including e586ff63) directly to tide-hosting as Fly
release 2, image deployment-01M5030QHVNHBTZJWJJR6TKRS3. The existing fra
512 MiB machine and 1 GB volume were retained; smoke/health checks passed.
Live console requests verify SvelteKit-rendered guest HTML, private/no-store
caching, nonce CSP and styled expired-sign-in recovery. Authenticated first
paint was verified locally and in the Linux production image, including a
browser with JavaScript disabled; no owner session cookie was accessed.
Existing sessions remain valid. Allocations remain disabled, with no payments,
credit grants or additional hosting resources. No CI run was dispatched.

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

The manually triggered Hosting workflow (`.github/workflows/hosting.yml`) now
tests the service and, on `dev`, deploys `hosting/` to `tide-hosting`, reusing the
existing repository `FLY_API_TOKEN` as requested by the owner. Other refs test
only. Node 24.16.0 matches the service image. Installed-toolchain/browser cases
skip without staged tools; the ordinary service tests include real WebRTC.
Provider secrets remain in Fly, and the workflow does not enable allocations
or configure first-deployment domains, rates, invites or backups. See the
operator README and notes/releases-and-ci.md for the completed run and token setup.
Workflow validation passes actionlint 1.7.12, Fly configuration validation and
the production image build. Node 24.16.0 service tests pass on Windows and in
the Linux service image (29 passed, two staged-tool cases skipped).
The first owner-triggered Hosting run passed service tests but Fly configuration
validation required a token absent from that step. The workflow now supplies
the existing `FLY_API_TOKEN` to both Fly validation and deployment. The owner
authorized a fresh run after this correction. Run 38754895170 attempt 2 passes
tests, validation and deployment. The existing repository token was website-only;
its replacement combines one-year app-scoped FlyV1 credentials for the website
and hosting, retaining the same secret name and refusing an unrelated app (403).
The runtime has no copy of this deployment token. The service is healthy on Fly.
The owner then authorized Cloudflare OAuth in their own browser. DNS-only
A/AAAA records for console/play use the hosting app's public addresses, and
the console ACME CNAME provides DNS validation. Both Fly certificates are
issued and both custom-domain HTTPS health checks pass. Website release 282
adds the requested Console menu on desktop/mobile; the apex remains the docs
site. See notes/docs-site.md. Provider and other launch checks below remain
outstanding; allocations and rates were not enabled by domain setup.

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
a restricted-key creation operation.

The owner subsequently asked the agent to push the Stripe secrets. Created the
Fly app namespace `tide-hosting` in the existing personal organization and
imported `TIDE_STRIPE_WEBHOOK_SECRET` directly from the private local file with
`fly secrets import --stage`. Fly lists it as staged; no machines, volumes or
deployment were created. `TIDE_STRIPE_SECRET` is still absent. An existing
`STRIPE_PRIVATE` environment value was verified through Stripe's account API:
it belongs to a different sandbox, so it was not uploaded or modified. The
matching Tide Engine sandbox API key requires Dashboard access; the shared
browser currently requests sign-in. No secret values were printed or committed.

After the owner's next setup confirmation, `fly secrets list --app tide-hosting`
shows all four required names staged: `TIDE_GITHUB_CLIENT_ID`,
`TIDE_GITHUB_CLIENT_SECRET`, `TIDE_STRIPE_SECRET` and
`TIDE_STRIPE_WEBHOOK_SECRET`. The agent did not create or upload the three
newly present values and did not retrieve their contents. Thus their presence
is verified, while the API key's sandbox/account and permissions remain untested.
Secrets are uploaded but not deployed to a machine; no additional Stripe key
was created. The shared browser still displays Stripe sign-in.

Validation: 14 existing auth/payment/store tests pass; two isolated mock-provider
reproductions confirm the key-mode and retry findings. Runtime code is unchanged
by this review. Private setup helpers/results are under ignored build/; the
webhook secret is outside the repository. Local references and whitespace diff
checked. Official references: [restricted keys](https://docs.stripe.com/keys/restricted-api-keys),
[idempotent requests](https://docs.stripe.com/api/idempotent_requests),
[paid invoice receipts](https://docs.stripe.com/receipts) and
[Stripe MCP](https://docs.stripe.com/mcp).
