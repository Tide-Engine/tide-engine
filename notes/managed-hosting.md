# Managed hosting and deployment design

## Status and scope

Agreed product direction from the owner's design discussion, 2026-10-15.
This records planned behavior, not a shipped hosting service or permission to
start implementation. Exact APIs, formats, limits and providers remain open
where listed below. No new Tide language syntax is approved here.

The first service combines local compilation, artifact upload, a hosted web
client, sandboxed match execution, a developer dashboard and prepaid billing.
The implementation status of native dedicated servers is in
[dedicated-servers.md](dedicated-servers.md). The old remote mod build service
remains on hold; this design does not reactivate it or its withdrawn requirements
([build-service.md](build-service.md)).

## Local build and deployment

- `tide deploy` builds locally and uploads artifacts. Whole-game deployment is
  the default: a web client, a WASM server, their assets and generated release
  metadata. It returns a playable URL, a server connection address and a
  dashboard link.
- `tide deploy --server-only` omits publication of the web client and uploads
  the server with its required assets. These commands describe the intended
  interface; they are not implemented commands today.
- Source upload and remote compilation are not required. A future opt-in
  package/service can produce the same deployment bundle remotely. Hosting
  consumes artifacts independently of where they were compiled.
- Client and server artifacts come from the same resolved project inputs and
  toolchain. Platform-specific binaries and reduced server asset packs can
  differ while sharing simulation compatibility.
- A release is immutable and identifies its exact artifacts. A deployment runs
  a release in a region with configured capacity. Publishing a new release
  directs new matches to it; existing matches can finish on their old release.
  Recoverable worlds must retain access to their release under the published
  payment and retention policies.
- Projects group the game, deployments, authorized developers and billing.
  Authentication is needed for developer CLI and dashboard access. A browser
  login flow, discussed as `tide login`, is planned; its provider and protocol
  are undecided.

## Release identity and simulation compatibility

Two distinct generated identities are required:

| Identity | Purpose |
|---|---|
| Release ID | Identify exact deployment and distribution artifacts. |
| Simulation ID | Decide whether a client and server may share a match. |

Admission requires equal simulation IDs. Mismatches receive an explicit
update/version-mismatch result before creating or entering a match. Developers
do not maintain compatibility version numbers. Human-readable versions can
remain optional labels.

The simulation fingerprint includes match systems, handlers, called helpers,
tasks, simulation types and defaults, scenes and prefabs, input and command
schemas, input sanitization, simulation settings, simulation-read assets and
the relevant compiler/runtime version. Dependencies are transitive: a helper
used by both a view and simulation participates. Presentation-only views,
shaders, local UI state and assets are excluded. A purely visual change should
leave simulation compatibility unchanged.

Custom C/C++ and library dependencies are conservative fingerprint inputs
initially; the compiler cannot assume their presentation/simulation separation.
The exact treatment of target-specific native libraries versus their WASM
counterparts needs design. Fingerprints must not be hashes of the final binary:
desktop, web and server outputs naturally differ. Server asset omission also
must not create a simulation mismatch.

The existing `tide_game.hash`, emitted in `compiler/src/codegen.c` and checked
by `engine/src/session.c`, primarily fingerprints the generated header and
selected asset dependencies. Gameplay bodies can change while that header stays
the same. It is not the planned simulation fingerprint. How the new identity
coexists with layout identity and the session protocol is still open.

Compatibility metadata is not authentication, a code signature or proof of
equivalent behavior for arbitrary developer-uploaded code.

## Managed execution and authority

The managed-host direction is a separate WASM instance per match, isolating its
linear memory, globals and simulation state. Many instances can run in one host
process; compiled code can be reused. This avoids requiring a container per
match or customer for game-memory isolation. The surrounding service still
needs its own operational security boundaries.

All game C/C++ executes inside the WASM sandbox. No unrestricted native escape
is part of this design. Instances receive explicit, bounded host capabilities,
with CPU/tick budgets, memory limits and bounded service calls. Network,
storage and external-service access are controlled by the trusted host.

The trusted service owns connections, admission, match routing, resource
accounting and lifecycle. Developer accounts and billing are separate from
player identities. Games retain separate authorization decisions for creating
and joining matches, with trusted creation supported. Match IDs route players;
they are not permission tokens. Public client requests cannot bypass project
quotas or payment checks. Credentials and verified account metadata stay
outside deterministic world state.

Existing native hosting auth hooks do not provide an account database, login
service, token issuer or built-in credential verifier. Managed player identity
is a possible later service, not a first-version requirement.

## Client distribution and updates

The first hosted version rejects incompatible clients. It does not automatically
download native client code from a match server. Web distribution publishes the
client at the game's URL; reopening it loads the published version. A browser
holding an incompatible client receives the mismatch result. A native store
client can direct its player to update through its distribution channel.

Production upgrades of running matches are deferred. Development hot reload
remains useful but does not supply deployment coordination automatically.
Current web reload closes the room and current native dedicated service builds
do not reload; see [hot-reload.md](hot-reload.md).

If automatic client code delivery is introduced later, a server may announce a
release ID but cannot grant that release execution authority. The client must
independently verify the game's publishing authority. Runtime server credentials
must not authorize publication. Signing, revocation, downgrade policy and any
native client sandbox require separate design. Those future questions do not
reinstate the old mod build service's signing model.

Steam's published guidance asks developers to deliver updates through Steam
and avoid required in-game downloads. Use Steam distribution as the baseline;
this is not a claim that Valve categorically prohibits hot reload. Hosted game
pages also need separation from dashboard/account origins and credentials.

## Match lifetime and recovery

| Lifetime | Behavior |
|---|---|
| While occupied (default) | End after the match is empty for a configurable grace period. A join/reconnect cancels the empty timer. |
| Until explicitly ended | Continue running without players until server code or an authorized operator ends it, subject to payment and resource policies. |

Server-side orchestration can select/change lifetime per match and configure
the empty timeout. Explicit lifetime has no implicit idle expiry or maximum
duration; optional duration limits apply when configured. Admission remains
separate: `Session.Close` closes admission, while `Session.End` ends a match.
The managed default timeout, initial-player wait and counting of disconnected
players still need precise definitions. Sixty seconds was an example, not a
new approved managed-host default.

Long-lived worlds automatically recover from unexpected interruption:

1. Detect the failure and record/notify the incident.
2. Restart the affected instance on the same release.
3. Restore the latest completed durable save through the game's save/load
   integration.
4. Keep a stable match address so players can reconnect.
5. Report recovery, including the save timestamp and potential progress loss.

Games own their save content and restoration logic; hosting provides durable
storage and recovery orchestration through an interface still to be designed.
Publishing saves must be atomic: an incomplete replacement leaves the previous
completed save available. Runtime snapshots/hot-reload layouts are not currently
a promised durable save format.

A match trap can restart one instance; host-process failure requires recovery
of all affected persistent matches. Retries use bounded backoff and stop after
repeated failures, with escalation. An explicitly ended world stays ended.
Payment suspension must also remain suspended rather than enter a recovery
loop. Recovery cannot promise to repair arbitrary broken game logic or saves.
Changing a persistent world's release requires an explicit upgrade/migration
path, separate from restarting it.

## Capacity, dashboard and billing

The initial capacity controls are region, match size (CPU/memory presets),
maximum capacity and match lifetime. Start with one region per deployment;
multiple-region placement can follow. Capacity grows on demand within the
configured limits. When full, new match requests wait briefly or receive a
clear capacity error. Ordinary scaling does not terminate ongoing matches.

Advanced spare/readiness capacity tuning is deferred. Sharing ready host
capacity across WASM games is a possibility to measure, not a promise of free
idle capacity or instant startup.

The dashboard is planned to show deployments, releases, status, logs, matches,
player counts, resource usage, recovery incidents, stop/restart controls, credit
and billing history. The existing site's private admin dashboard is not a
developer account or billing system. Account roles and notification channels
remain undecided.

Launch with prepaid credit, limited/invite-only access and conservative quotas:

- Require payment before public hosting; any trial has a fixed allowance and
  expiry. No unlimited free usage or open postpaid credit is planned.
- Meter allocated resource time. Disclose storage and any traffic charges;
  exact rates, presets, metering units and allowances need cost measurements.
- Enforce per-account limits on compute, match creation, bandwidth, storage
  and retained artifacts, independently of who requests a match.
- Reserve credit before allocation, including a bounded save/shutdown allowance.
  Warn at low credit and stop accepting new matches before exhaustion.
- At usable-credit exhaustion, save and suspend running worlds, including
  worlds configured to run until explicitly ended. Saving has a bounded budget;
  failed saving must not enable unlimited running time, and previously completed
  saves remain the recovery point.
- Give suspended worlds a published, finite save-retention period, with warnings
  before deletion. Optional automatic top-ups have a customer-configured cap.
- Bound the operator's total provisioned capacity separately from customer
  credit. Start with a small pool, a provisioning ceiling and a switch to block
  new allocations. Billing alerts alone do not enforce an infrastructure cap.

Exact warning thresholds, retention duration, retry limits, queue timeouts,
payment-failure handling and suspension/resumption semantics are still open.
Do not promise that delayed usage metering or a provider budget alert gives an
exact hard currency ceiling; resource enforcement must bound exposure too.

## Implementation sequence and open decisions

A proposed first technical milestone is an end-to-end prototype: locally build
and upload compatible client/server artifacts, run isolated WASM matches with
enforced budgets, and join through the hosted web client. This sequence is a
proposal, not an implementation assignment.

Before paid launch, resolve and validate:

- Artifact/manifest format, compatibility closure, protocol negotiation and
  pairing client/server outputs across platforms.
- WASM runtime, scheduling, permitted imports and interruption/budget enforcement.
- Infrastructure, persistent storage and payment providers; none are selected.
- Developer authentication, project permissions and CLI credentials.
- Match entry/start configuration and managed equivalents of native C hooks.
- Stable routing, reconnect identity restoration and durable save/load callbacks.
- Health detection, retry/backoff policy, notifications and upgrade behavior.
- Capacity/credit reservations under concurrent requests, bounded traffic and
  storage, retention cleanup and operator cost ceilings.

No account service, dashboard, billing integration, deploy command, managed WASM
runtime, simulation fingerprint or durable recovery is implemented by this note.
No change to existing Tide syntax or runtime behavior is made.

## Research used in the discussion

Official references checked on 2026-10-15; product capabilities and prices can
change. These inform the controls above and do not select a provider:

- [GameLift capacity buffers and min/max scaling](https://docs.aws.amazon.com/gameliftservers/latest/developerguide/fleets-autoscaling-target.html)
  and [instance pricing](https://aws.amazon.com/gamelift/servers/pricing/instance-pricing/).
- [PlayFab standby scaling](https://learn.microsoft.com/en-us/xbox/playfab/multiplayer/servers/scaling-standby)
  and [consumption billing](https://learn.microsoft.com/en-us/xbox/playfab/multiplayer/servers/billing-for-thunderhead).
- [Edgegap on-demand/reserved pricing](https://edgegap.com/resources/pricing)
  and [persistence and recovery responsibilities](https://docs.edgegap.com/learn/orchestration/persistence).
- [Nodecraft sleeping and always-on plans](https://nodecraft.com/pricing).
- [Agones buffer and room-capacity autoscaling](https://agones.dev/site/docs/reference/fleetautoscaler/).
- [Steam update guidance](https://partner.steamgames.com/doc/store/updates?l=english),
  [WebAssembly sandbox model](https://webassembly.org/docs/security/), and
  [TUF update trust and key management](https://theupdateframework.io/docs/faq/).

## Validation of this record

Documentation only: checked the discussion against current dedicated-server and
reload notes, the compiler's generated-header hash and the session join check.
Local references resolve and the diff passes whitespace checks. No engine,
compiler or docs-site build is required for this unbuilt text. No infrastructure
deployment or CI dispatch is part of this work.
