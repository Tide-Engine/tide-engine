# Managed hosting and deployment

## Play-link discovery correction (2026-10-15)

The owner still could not find the console's play link. Verified the live test
project's current release includes client.html. Its controls were placed below
runtime credentials and CI token management. Moved Play game (primary button)
and the visible clickable URL directly beneath the project name, before policy,
cost and credential settings. Client-creation restrictions remain enforced and
explain using a running match link. Server-only releases explicitly explain
deploying without --server-only instead of silently omitting play controls.

The console's Refresh button now reloads the document/UI; background polling
still refreshes account data. Previously Refresh could leave an already-open
tab running the prior deployed UI indefinitely. The local shared preview can
navigate/evaluate the signed-out console but its snapshot tool repeatedly fails;
no owner browser cookie was read. Existing muted browser integration verifies
the authenticated project header/URL and responsive layout.

Validation: all 48 hosting tests pass with staged CLI/native/web and muted
Chrome. SSR asserts Play game precedes credentials and includes the full URL;
the browser checks the header placement and actual link targets. Console Svelte
check/build and docs build pass. Desktop evidence shows the primary play action
and URL beneath the project name; mobile checks report no horizontal overflow.
Source 60a8e311 (including 03cedcdb) deployed as Fly release 9, image
deployment-01M50DX2XHBX229J3QY2ED12ZF. Incoming compiler/docs changes merge
cleanly without console overlap. Machine checks and public health pass. A
temporary 60-second browser-kind verification credential checked the owner's
actual signed-in server-rendered dashboard: HTTP 200, the Play game href matches
the deployed test project, its full URL is visible, and both precede credential
settings. The verification credential was revoked immediately; no existing
browser session was accessed. An already-open old console tab needs one browser
reload to receive this UI. No new matches, payments or capacity changes.

## Shared-host isolation audit (2026-10-15)

The owner explicitly requested shared machines for different game projects and
protection against reading, modifying or deleting another game's data. This is
already a multi-project/multi-account service: projects are routing/accounting
units, not Fly apps. One host runs independent Workers/WASM memories per match.
No guest JS/native module is accepted; all game C runs inside WASM. The only
WASI operations are bounded stdout/stderr, empty argv/environment, clocks,
entropy and lifecycle calls. File/directory and arbitrary network imports are
rejected before instantiation. Saves accept bounded bytes, never guest filenames.
Host filesystem paths use validated host-generated match IDs and content hashes.
Account/project authorization precedes save/log reads and destructive controls.

Found and fixed an artifact lifecycle race: deleting the last old release could
unlink the same content-addressed artifact while another upload was registering
it for a different project. A process-local artifact guard now serializes upload,
release deletion and retention collection, including each awaited filesystem
operation. Contending mutations return 429; collection waits for uploads/deletion
to finish. Startup sets the next collection time after its initial sweep.
Existing reference checks retain bytes while any project owns a release.

Added regressions for different games/accounts running together, independent
guest state and continued operation after ending the other game; forbidden
file/directory/network/host imports; empty WASI environment and refusal of
arbitrary fd_write descriptors; cross-account save/log reads and deletion;
retaining shared release bytes and private saves; and controlled upload/delete
interleaving. WASM instances provide guest isolation within a shared Node host,
not separate OS kernels or independent machine availability. This work does not
implement multi-machine scheduling or automatic provisioning.

The live game page returns 200 and the deployed operator status confirms
accepting=true with the same two-match/128 MiB guest reservation cap. A bounded
real match probe initially started, then failed its 20 ms wall watchdog. An
isolated ten-update measurement of that exact release on Fly measured 15.8 ms
for the cold update, 3.25 ms next and 0.08–0.49 ms subsequently. The cold JIT and
shared-CPU scheduling left insufficient wall headroom. Small/Standard watchdogs
are now 250/500 ms; their independent sampled CPU limits remain 0.25/0.5 core.
Infinite-loop and CPU-overuse tests continue enforcing termination. This does
not change guest scheduling, game tick rate or deterministic simulation.

Validation: all 48 hosting tests pass with staged native/web/managed CLI and
muted Chrome, including the filesystem/environment/ownership and concurrent
artifact cases above, infinite-loop interruption and sampled CPU enforcement.
Svelte check/build and the updated docs production build pass. No engine or
compiler source changes and no CI dispatch.

Source 0b3ce5cd (including f819180d) deployed as Fly release 8, image
deployment-01M50AMTHR1J4JRZS5MXHB36FZ. Incoming Foldline-only changes merged
cleanly and do not affect hosting. Machine checks and public HTTPS health/game
client requests pass. A bounded probe used the owner's existing deployed game
through the real authenticated HTTP match API: created running, stayed running
for 1.5 seconds, then explicitly ended. Its temporary project-scoped runtime
credential was revoked; no smoke match remains active. The earlier failed probe
was also explicitly ended. Normal lease debits/refunds apply; no manual balance
grant or provider payment was made. Operator status confirms accepting=true,
maximum two matches, 128 MiB guest reservation and zero active matches afterward.
Existing Fly machine/volume remain unchanged. Helpers/logs are under ignored
build/ and contain no credential values.

## Non-expiring CI tokens and game links (2026-10-15)

The owner requested optional infinite tokens. CI deploy creation now offers
Never expires (API days:null), keeping 90 days as the default. The existing
NOT NULL token expiry column uses zero for non-expiring deploy credentials;
JSON returns expires:null. Authentication, cleanup and console listings honor
this value only for deploy tokens. Browser/CLI/runtime sessions retain their
bounded lifetimes. Project scope, account suspension, twenty-token limit and
individual revocation continue to apply. Existing credentials are unchanged.

The owner also reported a game refusing credit while the console showed a paid
EUR 20 monthly allowance. The game gate already uses the billing ledger, but its
combined credit/price check reported HTTP 402 when rates were unset (charge=0).
It now distinguishes available plan credit, missing operator prices (503),
insufficient lease funds (402) and disabled allocations (503). Existing paid
matches remain accessible when new allocations are disabled. The owner then
explicitly requested normal hosting operation regardless of Stripe key mode:
Fly enables allocations with Small/Standard rates 1000/2000 integer units per
allocated second (EUR 0.036/0.072 per hour), retaining the existing machine,
volume and two-match cap. Stripe test/live mode does not gate hosting. No
subscription, balance or provider key is manually changed.

Project cards now expose a selectable Game URL for web-client releases, with
Match URL on running/starting client matches. Existing buttons remain. Restricted
client creation explains that a specific running match link should be shared.
Server-only releases have no game URL. The URL fields select their text on click.

Validation: all 43 hosting tests pass with staged CLI/native/web builds and
muted Chrome. New store coverage advances a non-expiring token ten years,
triggers cleanup and verifies suspension/revocation; reopening SQLite retains
it. HTTP coverage verifies nullable token expiry and monthly-only funds with
zero purchased credit, missing prices, disabled creation, insufficient and
expired allowance. Browser coverage creates/revokes a non-expiring token and
checks visible URL values and click-to-select. Expanded desktop/phone UI captures
were inspected; Svelte has zero errors/warnings and both production builds pass.
Source 5e1bf4ff deployed as Fly release 7, image
deployment-01M50A168MME2S59CAHBAAKGR1. Machine smoke/health checks pass. Operator
status reports accepting=true and the owner's game URL returns 200 with its
hosted client. This deployment retains the existing machine and volume.

## CI deploy credentials (2026-10-15)

The owner requested private authentication for CI. Added named, project-scoped
deploy tokens, issued/revoked in the console (or authenticated developer API).
They use the CLI's existing TIDE_HOSTING_TOKEN environment variable, so no CLI
or language change is needed. A committed target link or explicit project ID
selects the existing project; deploy tokens cannot create projects.

Secrets are random 256-bit base64url values, returned once and SHA-256 hashed at
rest. Persistent deploy_tokens rows bind the hash to its project and display
name. Expiry defaults to 90 days, with 30/365-day or no-expiry choices. Twenty/account requires
explicit revocation, independently of runtime keys; browser/CLI session rotation
does not revoke them. Existing database startup creates the new table in place.
Account suspension and token expiry/revocation immediately reject authentication.

The deploy-token request allowlist permits only its project's release upload,
a minimal account response containing region and that project's ID/name for CLI
target resolution, and self-logout. It blocks account/billing data, other
projects, match controls, project creation, deletion and credential management.
Runtime keys retain their separate match-only authority. Browser issuance and
revocation use existing CSRF checks; secrets never enter SSR/account responses.
The UI shows each token's name/expiry and an individual revoke action. Docs
include a CI secret example. No actual owner token or CI secret was created.

Validation: all 41 hosting tests pass with the installed native/web/managed CLI
and muted Chrome, including an actual env-token deployment using the committed
folder link, wrong-target/project-creation refusal and revocation. Store/HTTP
checks cover hashes, expiry, caps, session rotation, CSRF, account isolation and
the narrow API allowlist. The browser creates, reveals, hides and revokes a
token through Svelte and verifies the backend rejects it afterward. Desktop and
phone captures of the expanded panel were inspected without horizontal overflow.
The test now reports browser evaluation exceptions and waits for the correct
document after navigation, rather than accepting a previous page during reload.
Svelte check reports zero errors/warnings; console/docs production builds pass.
No engine/runtime/CLI source changes or CI workflow dispatch are involved.

Merged into dev as 00db7583; incoming changes were unrelated notes only. Deployed
that source directly to tide-hosting as Fly release 6, image
deployment-01M509J7E4F74GPYE04QDNNTKQ. The production Linux Svelte check/build,
machine smoke/health checks and public HTTPS health/SSR requests pass. The
existing machine/volume, allocation switch and pricing configuration are retained.

## Status and agreed scope (2026-10-15)

The owner approved a monthly minimum with included usage and opt-in prepaid
extra usage. The configurable sandbox example is EUR 20/month with EUR 20
included, not approved public pricing. Paid monthly allowance expires at its
invoice service-period end; purchased credit carries forward. Both manual and
automatic extra purchases share the customer's cycle spending cap, including
pending amounts. Extra usage/automatic charging defaults off. Deploying does
not subscribe. Cancellation retains access through the paid period. No postpaid
debt or unpaid renewal grace is added.

Implemented in billing-store.mjs and payments.mjs, with persistent Customer
mapping, hosted subscription Checkout, paid-invoice grants, period-end
cancellation and a configured Stripe Customer Portal. The server retains its
immediate transaction/lease enforcement; it does not migrate to Stripe meters
or Metronome. The previous Stripe planner's one-time-only scope is superseded
by this approved subscription change. The Stripe skill's Billing/security
references were read. The current Stripe MCP rejects both planner/docs calls
with authentication required; used official Stripe docs and the already
configured runtime sandbox key for setup without exposing it.

SQLite migration keeps existing accounts.balance as purchased credit. A separate
allowance ledger tracks each invoice/cycle, and reservation source records make
refunds restore their original bucket. Expired allowance cannot become purchased
credit. Leases are bounded by confirmed paid periods. A late renewal confirmation
may suspend worlds at that boundary; credit reconciliation enables manual resume.
Subscription access is required even with purchased funds after cancellation.
Without a configured Price, isolated local/prepaid test configurations remain
usable; the Fly service config enables monthly enforcement for all accounts.

Durable purchase IDs and exact provider request parameters are written before
external calls. Uncertain retries preserve the original amount, method and
idempotency key, and reserve cap space. Unknown operations older than 23 hours
require reconciliation rather than creating another potentially paid operation.
Off-session payments use the saved method with explicit extra-usage consent;
authentication-required intents are canceled before releasing their cap hold.
Definitive failures pause automatic buying until settings are explicitly saved.
Known pending payments/subscriptions reconcile each minute; billing requests
run separately from match ticks and abort/drain on shutdown. Existing one-time
Checkout notifications remain fulfillable; provider redirects never grant funds.
Refunds/disputes freeze mapped accounts, including subscription payments.
Restricted live key mode detection now recognizes rk_live_ as well as sk_live_.

Created sandbox Product prod_VRlTIssGVtFmiM, Price
price_1UQrw7EOEguEtdxR8VTaPR66 (EUR 20/month) and Portal configuration
bpc_1UQrw7EOEguEtdxRj71OCIMM (payment-method updates, invoice history and
period-end cancellation). Expanded the existing webhook endpoint's event list
for subscriptions/invoices/automatic payments without changing its signing
secret. All requests use the pinned 2026-09-30.endive API. No live resources,
tax collection, infrastructure scaling or real-money charges were enabled.

Validation so far: all 40 hosting tests pass with installed native/web/managed
CLI tools and muted Chrome. Seven new billing cases cover paid-only grants,
duplicate/out-of-order renewal, expiry, cancellation, source-preserving refunds,
paid-period lease bounds, shared caps, uncertain retries and authentication
failure. The real settings API is exercised in the browser, with extra usage
off initially, then opt-in. Desktop/phone light/dark UI captures were inspected;
Svelte check has zero errors/warnings and both production builds pass. Incoming
dev changes included a CLI toolchain fix; rebuilt/refreshed the private CLI and
all 40 hosting tests pass again, including native/web/server builds and Chrome.
Source 27d03438 (including d88f32c8) deployed as Fly release 5, image
deployment-01M508P29EDDHJXEVVCMP8XZ41. Machine/health checks and public HTTPS
health pass on the existing machine/volume; allocations and zero usage rates
remain unchanged. No CI was dispatched.

Real Stripe sandbox drills use the deployed modules, an isolated in-memory
Store and disposable Stripe test-clock customers. Verified a paid initial
invoice, duplicate grant prevention, a successful saved-method/off-session
EUR 5 top-up, refusal of another purchase at its cap, paid monthly renewal via
clock advancement, fresh allowance with no rollover, preserved purchased funds,
cap reset, cancellation at period end and loss of access after that period.
A separate hosted subscription Checkout smoke verifies mode/amount/customer,
reuses the pending session on retry, grants no credit before payment and opens
the configured Customer Portal. The unpaid checkout was expired; its test
customer and the test-clock customer/subscription were deleted afterward.
The owner's persistent account, subscription and balance were not modified.
Provider setup/drill helpers and logs remain under ignored build/; no API key
was read into local output or committed. Real-money pricing/tax/launch checks
remain outstanding, as do independent backup and remote hosting checks below.

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
| Small / Standard | 64/128 MiB, 0.25/0.5 sampled CPU core, 250/500 ms wall watchdog |
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
allocation switch. Frozen accounts stop. Optional prepaid automatic top-ups
share the monthly extra-purchase cap; no postpaid credit is granted.
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
production match hot reload and cross-release save upgrades.

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
