# Dedicated servers

## CI corrections (2026-10-15)

Run `38716371711` found two server-build defects in `bfc10e14`.
The optional process hook now has a weak no-op definition on Apple targets:
a standalone Mach-O executable cannot link an absent weak reference with the
ordinary link flags, even with `weak_import`. A game's strong C definition
replaces that default. Other targets keep their existing weak reference.
Server codegen copies the original asset declaration array only when nonempty;
an asset-free program otherwise passes NULL to zero-length memcpy, which UBSan
correctly rejects. No host API, asset selection, match identity or state changes.

Existing fixtures cover the failures: `tide_server_entry` uses an asset-free
local-menu program and custom C callback, while `tide_server_smoke` and
`tide_server_visual` omit the callback. The installed CLI's server cases in
`cache_test.mjs` also build without an entry before adding one.

Validation: complete Windows native/web debug builds and all 2,337 native and
2,250 web CTests pass with Clang 23.1.2/Node 24.16.0, including Vulkan validation,
OpenGL, WebGL/WebGPU, server startup and custom callback execution. A fresh
optimized Linux tidec built in this checkout under WSL with
ASan/UBSan compiles `server_menu`, `server_visual` and `server_build`, each with
both server asset policies, without sanitizer diagnostics. Standalone header
probes cross-compiled for macOS arm64/x86_64 at -O0/-O2 reproduce the original
missing-symbol error and link successfully after the fix, both without a hook
and with a strong hook replacing the weak default (Clang/ld64.lld 23.1.2,
dead stripping enabled). These cross-links do not execute macOS binaries or
replace validation with Apple's linker on a Mac. No CI run was dispatched.

## Agreed scope (2026-10-15)

The owner requested headless lightweight dedicated servers, simulation-only asset
packs, and multiple isolated matches of one game in one native process. Match
creation and joining have separate policies: anonymous client requests for quick
startup and a pluggable verifier for restricted/trusted deployments. Match IDs
route connections. Authentication runs outside simulation; credentials do not
enter replicated state. A process entry point configures match lifecycle.

The API names, C entry point, limits, timeouts and wire envelope below are agent
picks, Provisional in docs/spec.md. No Tide declaration syntax was approved or
introduced. Managed/sandboxed hosting and the inactive build service are separate.

The subsequent agreed managed-hosting design is recorded in
[managed-hosting.md](managed-hosting.md): local artifact builds/uploads, WASM
match isolation, simulation compatibility, persistent recovery and prepaid
billing. It is planned work and does not change the native implementation below.

## Implementation and ownership

`engine/include/tide/hosting.h` and `engine/src/hosting.c` are agent-maintained on
the temporary runtime terms in runtime-ownership.md. The transport-neutral host
owns one outer transport and independent tide_server instances, with no local
client, view/event presentation, GUI or audio. Matches update in slot order,
sharing one jobs pool for systems within ticks. C globals and native crashes
remain process-wide; match-specific C state belongs in a world/context.

The outer envelope is hosting version 1, separate from the unchanged session
protocol. Requests carry an attempt nonce, target match (zero creates), bounded
credentials and create parameters. Responses carry a result, assigned match and
route. A route is bound to its originating peer and one match; payloads cannot
select another match. IDs include slot/generation and are not reused within a
host lifetime. Match IDs and routes fit 48 bits, request nonces are 64 bits.
They are routing identifiers, not authentication secrets.

Inner datagrams retain the session MTU. The 24-byte envelope splits only large
datagrams into two bounded pieces, with eight reassembly slots per connection.
Loss, duplicates and reordering still use the session's ordinary recovery.
Incoming work is bounded to 256 datagrams per update and queues to 64 datagrams
per match/client. Match/connection maxima bound allocations. Defaults are 64
matches/256 peers, maxima 4096/65536. Pending requests time out in ten seconds;
completed attempt records are retained until thirty seconds without inbound
match traffic, so retries during the admission window are idempotent. Empty
matches expire after sixty seconds by default. Ended worlds are reclaimed too.

The verifier handles create-and-enter and join independently. It may resolve
immediately or return PENDING and later call tide_hosting_resolve with the
host-assigned handle. Callbacks borrow request data; external work copies what
it needs. Stale resolutions fail. Credentials/parameters each allow 512 bytes.
Credentials require the host's secure_peer and the client's secure transport
promise; the standard native service permits credentials only on WebRTC/DTLS,
never plain UDP. The verifier supplies an opaque 32-byte identity; zero is
anonymous. No token parser, account backend, JWT implementation or provider is
bundled. The callback validates provider credentials, expiry, audience and scope.

An optional tide_server_desc.identity callback binds session slots to verified
identities outside world state. A mismatched identity cannot reuse another
account's cookie. A verified account can resume its slot without a cookie.
tide_server_player_identity/tide_hosting_player_identity expose that metadata
only to the C host. This changes no tide_game or generated-runtime callback,
inner protocol, snapshot or world hash. Ordinary sessions without this callback
keep their existing cookie semantics. Identity-aware host migration and restoring
identity metadata after process restart are not implemented.

## Standard service and clients

`platform/src/serve.c`, `tide/serve.h` and `tide/server_run.h` implement a desktop
console service. `tide_server_entry(tide_hosting_desc *)` is an optional weak C
process hook. start selects a normal typed tide_start per authorized create;
ready/poll support trusted in-process lifecycle and asynchronous verification.
They are service callbacks, not invoked by the transport-neutral update API.
Match Main is the default; local Main requires a start callback. There is no
Tide orchestration declaration yet. Callbacks and their user state are process
state, not simulation state.

The service uses UDP plus one shared WebRTC room, or --udp-only. It never opens
the platform window/device/frame APIs. Pack bytes load beside the executable
without linking the window's loader. SIGINT/SIGTERM stop it; --frames bounds
service iterations for tests and is required under TIDE_HIDDEN. Iterations use
real time, not capture time. A one-millisecond sleep avoids busy waiting; Windows
uses a private high-resolution waitable timer without changing global multimedia
timer resolution. No render/audio/device work is done. The shared jobs pool is
created once, not per match.

The normal host recognizes endpoint/0 (create) and endpoint/<id> (join) in
Session.Connect and built-in Session.Join, including CLI --connect/--join.
Admission updates before the session, pending state presents Connecting, and
accepted clients get the assigned endpoint/id in Session.room. Game-registered
networks retain their own Join text. Custom authenticated clients use hosting.h;
the built-in address form is anonymous. Sessions close their virtual endpoint
before its hosting client releases the outer transport.

`tide run/build --server` and tide_add_game(... SERVER) generate the service
launcher. --anonymous enables testing policies; otherwise configure the verifier
or explicit host policies. Server caches are separate from native player caches;
there is no hot reload or GUI subsystem selection for server executables.
Default CLI output uses <name>-server and its own pack, preserving player
artifacts. Server builds do not copy the unused MoltenVK runtime beside them.
Debug/release server links collect unreachable client functions/data. The
installed platform archive and header/source installation include the new files.

## Server packs and compatibility

The compiler first derives the ordinary client's public header and game hash,
including existing animation/default fingerprints. Only then does server codegen
select the simulation asset closure. Server-build macros are appended outside
that hash. Omitted client asset globals remain zero declarations so unused client
helpers can compile and be removed by section collection; loaders, pack entries
and private runtime preflight include only retained assets. An all-visual game
has no server pack. Checking/import diagnostics still visit every declaration;
this optimizes shipped/loaded data, not full build-time source validation.

The iterative collector follows match systems/handlers, input defaults/Sanitize,
match data defaults, called helpers/operators/tasks and expanded prefabs. Local
fields, views, interpolation, Sample/Predict and uncalled helpers are not roots.
Match scene/singleton/event defaults are conservative roots; component defaults
are followed through reachable match code, excluding local-only placements. A reachable
Asset<Mesh<V>> resolver keeps all matching mesh declarations: an input/command
can select a mesh named only in client UI. Retained meshes bring their defaults'
dependencies. A referenced collection stays whole.

Automatic stripping requires no C/C++ or libraries in the game or its packages.
--strip-server-assets (or CMake STRIP_SERVER_ASSETS with SERVER) is an explicit
closed-world promise for custom C users. C uses by pack name cannot be inferred.
Client and reduced server packs differ in SHA-256, so session protocol 14 uses
its existing self-contained transfer fallback instead of pack-offset references.

## Validation

Focused native checks pass: independent lossy/reordered loopback matches, stop
isolation, idempotency/capacity/idle cleanup, asynchronous fail-closed admission,
credential transport gating, malformed/truncated request bounds, account-bound
reconnects and two real UDP matches without graphics. C entry-point fixtures
start two typed scenes from a local-menu build without running menu/views; a
visual-only server runs with no pack. Asset tests pin helper reachability,
dynamic asset references/defaults and byte-identical client header prefix.
A full-pack native client joins a reduced-pack server in another process,
receives owned asset text and verifies twenty ticks with zero resyncs, both over
real UDP and WebRTC through a local relay. No graphics context is created.

Full Windows debug validation passes 2,300 native and 2,250 web tests with Clang
23.1.2 and Node 24.16.0, including WASI hosting, native GPU, WebGL/WebGPU and
existing exact deterministic pins. A freshly installed private CLI with this checkout's
MinGW platform library passes cache_test.mjs --server-only: separate player/server
outputs, simulation-only packs, conservative custom-C packs, explicit C opt-in,
generated game.h availability and C cache invalidation on a Tide schema edit,
plus a bounded optimized console server run. The native MinGW server/default
and custom-entry targets also build. The production docs build passes.

Server C includes the generated game header and its cache stamp includes that
header, so typed startup/layout edits rebuild an unchanged entry file. These
server-only include/stamp additions preserve ordinary game C cache policy.

Linux/macOS, Android, sanitizers, the full MinGW suite and an actual browser
connecting to this new dedicated host were not run. Existing web room browser
tests pass; the dedicated WebRTC fixture uses a native client. Native GPU tests
passed without a configured Vulkan validation layer. No CI dispatch, deployment
or build-service work is part of this task.
