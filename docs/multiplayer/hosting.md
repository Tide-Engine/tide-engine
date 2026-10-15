---
title: Managed hosting
description: Build Tide games locally, upload client and server artifacts, and manage isolated matches with prepaid credit.
---

# Managed hosting

Tide's first managed-hosting implementation builds your game on your machine
and uploads artifacts to a configured hosting service. Public hosted access is
not launched yet. GitHub sign-in, Stripe prepaid credit and Fly.io are the
selected initial providers.

## Deploy a game

```sh
tide login
tide deploy --project my-game
```

Approve the CLI login in your browser after checking its code. Hosting accounts
are invite-only. By default, deployment builds a web client and a headless WASM
server from the same resolved game, checks their generated simulation IDs and
uploads both. It returns a playable URL, server endpoint and dashboard link.
The service receives built artifacts; it does not compile uploaded source.

```sh
tide deploy --project my-game --server-only
tide deploy --bundle-only
tide logout
```

`--server-only` omits the hosted web client. `--bundle-only` creates the local
deployment bundle without logging in or uploading. The default folder is the
current one; another folder can follow `deploy`. An existing project can be
selected by name or ID. A missing name creates a Small project with a maximum
of one active match; use the dashboard to choose another configuration first.

`TIDE_HOSTING_URL` selects another configured service origin. The operator setup
is documented in the repository's `hosting/README.md`. A normal developer CLI
credential expires after 30 days and can be revoked with `tide logout`.

## Matches and versions

Each managed match owns a separate WASM instance, including its memory and C
globals. The service can run several matches of the same game in one process.
Views, the local menu, rendering and audio do not run on the server. Match
`Main` is the default entry; games with a local menu select their match scene
through the [server entry callback](../c/network.md#dedicated-hosting).

An endpoint looks like `ROOMID/0` to request a match, or `ROOMID/<match-id>` to
join one. Match IDs route players; they are not authentication credentials.
The default project allows client-requested creation within paid quotas. For
trusted orchestration, disable client creation and issue a project-scoped
runtime credential in the dashboard. It can control that project's matches
through the HTTP API and cannot publish builds. Player-account authentication
is separate and is not supplied by this first managed service.

Compatibility uses a generated **Simulation ID**, covering match code, its
transitive helpers, schemas, defaults, settings, simulation assets and toolchain.
Presentation-only edits can remain compatible. Custom C and libraries are
fingerprinted conservatively, including all assets C could read. A different simulation is refused before match
allocation/admission, with update guidance in `Disconnected.message`.
Developers do not maintain a compatibility version number.

The **Release ID** names exact uploaded artifacts. A new release receives new
matches; an existing world continues on its original release. The first version
does not update a running production world or download native client code.

## Lifetime, credit and recovery

Matches end after all player connections leave and the empty grace passes
(60 seconds by default). A long-lived world can instead run until explicitly
ended. Its owner can change this policy or suspend/end/resume a match through
the dashboard/API. Admission remains separate from ending a world.

Hosting uses prepaid credit. The dashboard shows configured prices, capacity,
WASM memory, sampled CPU use, logs, saves, incidents and billing history. At low
credit it warns; exhaustion saves and suspends even long-lived worlds. Capacity,
bandwidth, storage and operator limits also apply. Included allowances are hard
limits, rather than automatic paid overages.

Long-lived worlds save periodically and recover on the same release and match
ID after a worker or host restart. Recovery has bounded retries and reports the
save timestamp in the dashboard. Games can provide their own C save/load
callbacks; without them, the service uses an exact-release checkpoint. There
is no promised cross-release save migration. Players reconnect after recovery.

Saves of inactive worlds have finite retention, initially seven days, with a
final-day dashboard warning. Download or resume a world before its save expires.
An explicitly ended or payment-suspended world does not restart automatically.
Email notifications, multiple host machines/regions and automatic top-ups are
not implemented yet.
