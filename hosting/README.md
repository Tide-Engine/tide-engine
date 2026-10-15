# Tide managed hosting

This is the first single-host implementation of local builds, artifact upload,
WASM matches, developer accounts and subscription-backed credit. Access is
invite-only. The checked-in Fly configuration enables bounded allocations on
the existing machine. Provider secrets, domains and operational launch checks
must be configured before paid public use.

Several game projects and developer accounts share this host. Each match runs
its game/C code in a separate WASM instance and Worker with its own bounded
memory. Games receive no filesystem handles, directories, environment secrets or
arbitrary networking API. The host selects save paths and enforces account/project
ownership for downloads, deletion and controls. Shared artifact bytes are retained
while any project references them; uploads, deletion and collection are serialized.
Small/Standard matches have 250/500 ms wall watchdogs for stuck updates, with
separate 0.25/0.5-core sampled CPU limits. Wall headroom accommodates cold WASM
compilation and shared-host scheduling; it is not a game tick interval.

## Run and validate

Use Node 24.16 or newer. From this directory:

```sh
npm ci
npm test
npm start
```

The console uses SvelteKit 3/Svelte 5 with adapter-node, matching the website's
stack. `npm test` first checks and builds the console; `npm run build` builds it
independently. The existing Node service serves the compiled UI, API and match
runtime together. Page requests authenticate the browser session and render its
account, projects and matches on the server before hydration. No initial account
fetch or wrong-view flash is needed. The production Docker build compiles the
frontend in a separate stage and installs runtime dependencies only in its final
image.

The dashboard is `http://localhost:8081`; games use `http://127.0.0.1:8081`.
These must remain different origins. Production requires two HTTPS origins.
GitHub OAuth is invite-only, with no repository scopes. Configure an OAuth app
with callback `https://console.tide-engine.dev/auth/callback`, then supply
`TIDE_GITHUB_CLIENT_ID` and `TIDE_GITHUB_CLIENT_SECRET`.

The console uses the main website's amber palette, wave logo and locally served
Inter Variable font (license at `/Inter-LICENSE.txt`). Light/dark appearance
follows the system until changed with the header theme button; the preference
is stored in this origin's browser storage. Browser sign-in failures return to
the styled sign-in page with an access/retry message. API errors remain JSON.
SvelteKit generates the per-response script nonce required by the console CSP;
personalized HTML and page data are private/no-store and vary by Cookie.

Invite the immutable numeric GitHub account ID, rather than a mutable username:

```sh
node admin.mjs invite 123456
node admin.mjs status
node admin.mjs allocations off
```

`allocations off` blocks creation **and resumption**, without stopping already
paid matches. `allocations on` removes this database block; environment limits
still apply. `freeze <github-id>` saves/suspends that account's matches;
`unfreeze` is an explicit operator decision after reviewing an incident.
There is no public invitation, free-credit or infrastructure-provisioning API.

The service tests use disposable databases and test payment responses. To run
the installed-toolchain and real browser cases as well, set `TIDE_STAGED_CLI`
to this checkout's staged `tide` executable and `TIDE_BROWSER` to Chrome/Edge.
Set `TIDE_STAGED_NATIVE=1` when that stage also contains native platform libraries
to compare native, client and server Simulation IDs together.
`TIDE_HOSTING_EVIDENCE` optionally saves desktop/phone dashboard captures.
The browser test passes `--mute-audio`, uses a disposable profile and terminates
the browser. No real GitHub login, Stripe payment or Fly resource is needed.

## Developer workflow

```sh
tide login
tide deploy
tide deploy --server-only
tide deploy --bundle-only
tide logout
```

`deploy` defaults to the current folder, creates or reuses a Small/one-match
project named after the folder, and builds release artifacts locally. Folder
names become lowercase hyphenated project names (up to 48 characters, with
`game-` prefixed when needed). No dashboard project creation is required. It
saves the service origin and immutable project ID in `tide.deploy.json` in the
game folder, so later deployments keep the target even after renaming the
folder. Commit this file to keep the target with the game; it has no secrets.
Deployment still requires an account with access to that project.
Use `--project <name-or-id>` to select another target and replace the link.
Unknown names create projects; unavailable IDs fail instead of creating one.
Invalid links, another service origin or an inaccessible project fail with
guidance to select the target explicitly. Credentials remain outside the game.
Deployment returns the immutable release ID, playable URL, room endpoint and dashboard.
`--server-only` uploads no web client. `--bundle-only` requires no account and
writes `.tide/deploy/release.tide-deploy`. No source is uploaded or built remotely.
The default service is `https://console.tide-engine.dev`; set `TIDE_HOSTING_URL`
to a configured HTTPS origin, or a localhost origin for development.

CLI approval uses a short-lived browser code and a secret one-time polling token.
Access lasts 30 days and is revocable. Credentials are bound to the service
origin; Windows stores them encrypted with the current user's DPAPI, and POSIX
uses a private folder/file. For CI, create a named deploy token in a project's
**CI deploy tokens** panel and put it in a secret named `TIDE_HOSTING_TOKEN`.
Run `tide deploy` with the committed `tide.deploy.json`, or pass
`--project <project-id>`. Browser login is unnecessary. Deploy tokens can only
upload releases for that existing project; they cannot create projects, control
matches, access billing or manage credentials. They expire after 90 days by
default, with 30/365-day or **Never expires** choices, and are individually revocable. Only hashes
are stored; the secret is shown once. Rotation keeps old tokens valid until
explicit revocation or expiry. Twenty active deploy tokens per account are
allowed, independently of runtime keys and developer sessions.
`POST /api/projects/<id>/deploy-tokens` takes `{ "name": "GitHub Actions", "days": 90 }`
and returns `{ token, expires, project }`. Send `"days": null` for no expiry;
the response and account listing then contain `"expires": null`.
`DELETE /api/projects/<id>/deploy-tokens/<hash>`
revokes it. Browser mutations require the existing Origin/CSRF checks; a normal
CLI login can also manage these tokens. Deploy tokens get only
`{ region, projects: [{ id, name }] }` from `GET /api/account`, never the full
dashboard account data. `POST /api/logout` revokes the presented token.
Do not put tokens in project files or command-line arguments.
Runtime credentials have an explicit limit of twenty per account. Repeated
browser/CLI sign-ins rotate their own sessions without revoking backend keys.

The dashboard controls capacity, lifetime, empty grace, client-requested
creation and shows Play game plus the clickable URL directly under each deployed
web project's name. Match URL fields select their text for copying. Server-only
releases explain how to include a web client. Refresh reloads the page, including
the latest console UI. The dashboard also controls
match suspension/end/resume, save downloads, release deletion, logs,
recovery activity and credit. CPU measurements are fractions of one core; WASM
memory reports linear memory, excluding the surrounding service/runtime.

## Trusted creation

Client creation is initially allowed, within paid account/project/global limits.
Disable it in the project dashboard for trusted orchestration. Generate a
project-scoped runtime credential there and keep it on the trusted backend:

```http
POST /api/projects/<project-id>/matches
Authorization: Bearer <runtime-credential>
Content-Type: application/json

{}
```

The response includes `id` and `endpoint` (`ROOMID/<match-id>`). Optional
`parameters` is base64, at most 512 decoded bytes. Runtime credentials expire
after 30 days, can be revoked and cannot publish artifacts, buy credit, access
developer accounts or control another project's matches. They can use
`/api/matches/<id>/suspend`, `/end`, `/resume`, `/policy`, `/logs` and `/save`.
`POST /policy` takes `lifetime: "occupied" | "explicit"` and `emptySeconds`.
Match IDs select a world; they do not authenticate a player. Managed player
accounts/credential verification are not part of this first service.

## Game entry and saves

Managed matches use the ordinary generated simulation/session engine, without
views, a local player, a window or audio devices. Match `Main` is automatic.
For a local menu, define `tide_server_entry` and its `start` callback as described
in `docs/c/network.md`. Select a generated typed `tide_start` there. Guard this
server C with `#ifdef TIDE_SERVER_BUILD` before including `game.h`; clients do
not need these callbacks. All game C/libraries must also build for WASM.

Native `authorize`, `secure_peer`, `ready` and `poll` hooks are rejected inside
managed instances. Trusted process orchestration belongs in the service/API;
each guest instance owns one match, rather than a native multiplexing process.

Optional game-owned durable payload callbacks, supplied together:

```c
uint32_t tide_managed_write_save(const void *world, uint8_t *out, uint32_t capacity);
bool tide_managed_read_save(void *zeroed_world, const uint8_t *data, uint32_t size);
```

Writing with `out == NULL` returns the required size. A subsequent call writes
that many bytes and returns the same size. Loading initializes a zeroed world
and restores it, returning false on failure. Without callbacks, the service
uses generated world packing as an **exact-release checkpoint**, with no
cross-release compatibility promise. The host adds reconnect metadata outside
world bytes. Saves are limited to 8 MiB including this envelope.

Explicit worlds save at startup and every 60 seconds, before normal suspension,
and during graceful shutdown. A trap restores the last completed save on the
same release/ID with 1/2/4-second backoff, at most three consecutive recovery
attempts; five healthy minutes reset the count. Explicitly ended/payment-suspended worlds stay stopped. Incidents and
save timestamps appear in the dashboard. There is no email notification yet.
Publishing new code affects new matches; upgrading existing worlds is deferred.

## Resource and payment policy

Current defaults are provisional operator settings, rather than language rules:

| Limit | Initial policy |
|---|---|
| Active matches | 2 per account and host; project chooses 1–2 |
| Match presets | Small: 64 MiB/0.25 core/250 ms wall watchdog; Standard: 128 MiB/0.5 core/500 ms |
| Fly memory reservation ceiling | 128 MiB across active matches on the fixed 512 MiB machine |
| Empty grace | 60 seconds, configurable 5–3600 |
| Startup/save deadline | 5 seconds; guest update overruns terminate that worker |
| Prepaid reservation | 30 seconds, renew with 5 seconds left for shutdown |
| Saved worlds | 8/account, 16/host, at most 8 MiB each |
| Save retention | 7 days after becoming inactive, with a final-day warning |
| Projects/releases | 3 projects and 6 releases/account; 32 projects/host |
| Artifact storage | 64 MiB/account, 512 MiB/host; 32 MiB upload/8 MiB WASM |
| Daily creation | 200/account, 1000/host, including failed starts |
| Daily outgoing transfer | 1 GiB/account **and** host, reserved before sending |
| Peers | 64/host, 32/project; packet/queue/fragment limits are also enforced |

Allocation time is charged using integer millionths of a minor currency unit:
100,000,000 units = EUR 1. Configure `TIDE_HOSTING_SMALL_RATE` and
`TIDE_HOSTING_STANDARD_RATE` in those units **per allocated second**. Zero
disables allocation. Unused whole seconds are returned on settlement; starting
a match consumes at least the elapsed rounded-up second. Traffic/storage are
included within hard allowances in this version, with no unannounced overage.
Transfer counts cover application payloads; they are not physical provider
traffic measurements or an exact infrastructure currency ceiling. Guest logs
are in-memory (100 lines for at most 32 matches); incidents persist in SQLite.

Configure `TIDE_STRIPE_SECRET`, `TIDE_STRIPE_WEBHOOK_SECRET` and
`TIDE_HOSTING_TOPUPS` (comma-separated minor-unit amounts; 500–100000 each).
Currency defaults to EUR (`TIDE_HOSTING_CURRENCY`). Use Stripe test mode first.
API requests are pinned to `2026-09-30.endive`; use that version for the webhook
endpoint too. Available payment methods come from the Stripe Dashboard settings.
Register `/webhooks/stripe` for `checkout.session.completed`,
`checkout.session.async_payment_succeeded`, `checkout.session.expired`,
`invoice.paid`, `invoice.payment_failed`, `customer.subscription.created`,
`customer.subscription.updated`, `customer.subscription.deleted`,
`payment_intent.succeeded`, `payment_intent.payment_failed`, `charge.refunded` and
`charge.dispute.created`. Raw webhook signatures, provider state, exact amount,
currency and account are checked; replay adds credit once. Refunds/disputes
freeze hosting pending review, including events delivered out of order.
Browser redirects never grant credit. There is no postpaid usage.

Set `TIDE_STRIPE_MONTHLY_PRICE` to a fixed EUR monthly recurring Price, quantity
one, and `TIDE_HOSTING_MONTHLY_AMOUNT` to its minor-unit amount (default 2000).
The same amount is granted as included hosting usage once per verified paid
subscription invoice. The price/currency/amount/period are checked against
provider state; only initial/cycle invoices qualify. Old or duplicate invoices
cannot add another allowance for the same subscription period. Subscription
Checkout, not a deploy, starts the recurring charge. Customers are persistent.

Monthly allowance expires at its service-period end and is consumed before
purchased credit. Purchased credit, including existing balances, carries
forward, but an active paid subscription and enabled extra usage are required
to spend it. Cancellation schedules the end of renewal, retaining paid-period
access. Set `TIDE_STRIPE_PORTAL_CONFIG` to a Stripe Customer Portal configuration
with payment-method updates, invoice history and cancellation at period end.
The dashboard also supplies an explicit cancel/keep-renewal action.

Extra usage defaults off. Customers select a top-up amount and a cap in minor
units (up to 100000 extra per cycle). This explicitly authorizes automatic
off-session prepaid payments. Manual Checkout and automatic PaymentIntent
purchases share that cap, including pending/uncertain purchases. Purchases are
assigned to the paid cycle in which they started. No credit is spendable until
provider-confirmed payment. Processing payments retain their cap reservation;
authentication-required payments are canceled before releasing it, with
automatic charging paused until settings are saved again. A confirmed payment
can complete after opt-out; previously authorized pending purchases are honored.

Purchase IDs and customer idempotency keys are persisted before external calls.
Ambiguous failures retry the same provider operation; after 23 hours, unknown
operations require support reconciliation rather than risking another charge
after Stripe's idempotency retention. Open hosted checkouts are reused; expire
or finish them before selecting a different amount. The service reconciles
known subscriptions/pending payment IDs each minute, separately from match ticks.
Legacy one-time Checkout notifications remain fulfillable. Without a configured
subscription Price, the existing local/prepaid test configuration remains usable.

Lease refunds return to their original source, and expired allowance cannot
become purchased credit. New leases cannot extend past a confirmed paid period.
If renewal confirmation arrives late, a world can suspend at that boundary;
after credit updates, resume it through the dashboard. No unpaid renewal grace
or automatic restart of payment-suspended worlds is supplied.

The dashboard links **Plan & extra credit** beside the balance to billing.
It shows amount selection only when top-ups and both Stripe secrets are
configured; otherwise it explains that purchases are unavailable. Test-mode
keys display a sandbox label and test-payment button. The current Fly setup
offers a configurable EUR 20/month sandbox plan with EUR 20 included and
EUR 5/10/25 extra top-ups. Allocations are enabled within the two-match host cap;
Small/Standard rates are EUR 0.036/0.072 per allocated hour. Stripe test/live
mode changes payment processing, not hosting eligibility. The runtime key also needs Customer,
Subscription, Invoice, Customer Portal Session and PaymentIntent permissions;
Product/Price/webhook configuration belongs to operator setup, not the runtime.

Low credit produces dashboard incidents. Exhaustion stops new allocations,
saves explicit worlds and suspends them. Resume requires credit/capacity.
Inactive saves expire; download them before expiry. Old releases cannot be
deleted while running/saved worlds need them. Detailed allocation ledger and
unsaved ended-match records retain 30 days; older usage becomes exact daily
totals. Payment fulfillment/deduplication records remain intact.

## Fly setup and launch boundary

`fly.toml` targets the existing Fly organization/relay setup, using a separate
`tide-hosting` app, one `fra` machine, a `hosting_data` volume and separate
console/game domains. Do not put it in the docs app or reuse the docs volume.
Create a small volume (at least 1 GiB) and configure DNS/certificates for both
domains, provider secrets, measured rates, top-up amounts and invites. Deploy
from this directory with `fly deploy --ha=false`; keep the machine count at one.
The service has no Fly API token or autoscaling/provisioning capability.

The **Hosting** GitHub Actions workflow (`.github/workflows/hosting.yml`) runs
independently of engine releases and docs deployments. Start it manually on
`dev` to test the service and deploy this directory to `tide-hosting`:

```sh
gh workflow run hosting.yml --ref dev
```

Other refs run the service tests without deploying. The workflow uses Node
24.16.0, runs `npm ci` and `npm test`, validates the Fly configuration and deploys
with `--remote-only --ha=false`. The installed-toolchain/browser integration
cases skip because this workflow does not stage Tide or a browser. It reuses
the existing repository `FLY_API_TOKEN`; GitHub OAuth and Stripe credentials
remain in Fly secrets. Deployment applies the checked-in rates and allocation
switch; domains, provider secrets and invites require operator setup.
The shared Fly secret contains app-scoped deployment credentials for both
`tide-website` and `tide-hosting`, expiring in October 2027; renew both together.

The existing relay must supply working TURN credentials for this NAT-hosted
service; verify real remote native/browser players through TURN before launch.
Local loopback tests do not establish Fly NAT reachability. No dedicated public
UDP allocation is required by this initial WebRTC path.

Only after provider test-mode fulfillment/refunds, remote connectivity, restart
and restore drills, host RSS/CPU/traffic measurements, external save/database
backup and customer-facing rates/retention are reviewed should allocations be
enabled (`TIDE_HOSTING_ACCEPTING=1`, plus `admin.mjs allocations on`). A single
volume is not high availability or an independent backup. No paid resources or
production payments were created by implementation validation.

Unbuilt: multiple host machines/regions, email notifications, managed player
auth, cross-release save upgrades, remote builds, native client auto-updates,
reusable compiled-module caching. WASM memory/import validation
and worker interruption are exercised locally; the surrounding Node/native RTC
service still needs production security/operational review.
