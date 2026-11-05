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
tide deploy
```

Approve the CLI login in your browser after checking its code. Hosting accounts
are invite-only. By default, deployment builds a web client and a headless WASM
server from the same resolved game, checks their generated simulation IDs and
uploads both. It returns a playable URL, server endpoint and dashboard link.
The service receives built artifacts; it does not compile uploaded source.

The console shows **Play game** and the clickable game URL directly below the
project name for releases with a web client. A **Match URL** selects a specific
running match; click its field to select it for copying. Server-only releases
explain how to include a web client. **Refresh** reloads the console page.

```sh
tide deploy --server-only
tide deploy --bundle-only
tide logout
```

`--server-only` omits the hosted web client. `--bundle-only` creates the local
deployment bundle without logging in or uploading. The default folder is the
current one; another folder can follow `deploy`.

On the first deploy, Tide uses a lowercase, hyphenated version of the folder
name to create or reuse a Small project with one maximum active match. No
dashboard setup is needed. It saves the project ID and service origin in
`tide.deploy.json` in your game folder. Future deploys use this link, even if
you rename the folder. You can commit it to keep the target with the game;
it contains no credentials and grants no access to the project.

Use `tide deploy --project <name-or-id>` to select a different project and
update the link. A missing name creates a project; an unavailable ID fails.
An invalid link, a link to a different service or a project your account cannot
access requires an explicit selection. The dashboard still lets you create a
project with custom settings before deploying.

With crash reports on in the project's **Settings** tab, each deploy also saves
the project's report key in `tide.deploy.json`, and desktop builds then send
their players' crash reports to the project, where the console groups them. See
[Receiving reports](../game/files.md#receiving-reports).

`TIDE_HOSTING_URL` selects another configured service origin. A normal developer CLI
credential expires after 30 days and can be revoked with `tide logout`.

## Deploy from CI

In your console project's **CI deploy tokens**, create a named token and copy it
when shown. Store it as a CI secret named `TIDE_HOSTING_TOKEN`. Tokens belong to
one existing project, expire after 90 days by default (30 days, one year or
**Never expires** are also available), and can be revoked individually. Only their
hashes are stored by the service. Creating a new token leaves existing tokens
working so you can rotate your CI secret before revoking the old token.

With Tide installed and your game checked out, a GitHub Actions step can use:

```yaml
- name: Deploy game
  env:
    TIDE_HOSTING_TOKEN: ${{ secrets.TIDE_HOSTING_TOKEN }}
  run: tide deploy path/to/game
```

Commit the game's `tide.deploy.json`, or pass `--project <project-id>` to select
the token's project explicitly. No browser login is needed in CI. These tokens
can upload releases and discover only their project's name and ID; they cannot
create projects, change billing, manage credentials or control matches. Use
runtime credentials separately for trusted match orchestration. Keep tokens out
of project files, command arguments and logs.

## Matches and versions

Each managed match owns a separate WASM instance, including its memory and C
globals. Different game projects can share a hosting process, as can several
matches of one game. Guest code has no host filesystem access or access to other
matches' memory. The host manages each match's saves, and account/project checks
protect downloads, deletion and match controls.
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

Hosting uses a monthly subscription. Each paid period includes an allowance of
each kind of usage: match hours (a Standard match uses two an hour), crash
reports, storage (releases, saves and reports) and outgoing traffic. The
console's **Overview** shows this period's usage against each, and warns at 80%.
Allowances reset each paid period and don't carry over. Hosting requires an
active paid period. Canceling renewal keeps access until the end of the paid
period. Deploying artifacts does not start a subscription.

Extra usage starts off, so each allowance is a hard limit. At an allowance:

- new matches are refused, and running ones end, or save and suspend if they're
  long-lived worlds
- crash reports are refused and counted as dropped
- uploads past the storage allowance are refused

In **Billing**, you can turn extra usage on. Usage past an allowance is then
charged per unit from prepaid credit, at the prices **Billing** lists. You can
choose an automatic top-up amount and the maximum additional purchases per
billing cycle; this authorizes payments against your saved Stripe payment
method when credit runs low. Manual and automatic purchases share the same cap,
including pending purchases. Payment must succeed before credit is available,
and authentication failures pause automatic purchases and show guidance.
Purchased credit carries forward. Sandbox payments are labeled and charge no
real money.

The console has a page for each project, with tabs:

- **Overview**: the play link, latest release and deploy command
- **Matches**
- **Crash reports**
- **Settings**: size, maximum matches, lifetime, CI tokens, runtime credentials
  and the crash report key

Long-lived worlds save periodically and recover on the same release and match
ID after a worker or host restart. Recovery has bounded retries and reports the
save timestamp in the dashboard. Games can provide their own C save/load
callbacks ([C networking](../c/network.md)); without them, the service uses an
exact-release checkpoint. There
is no promised cross-release save migration. Players reconnect after recovery.

Saves of inactive worlds have finite retention, initially seven days, with a
final-day dashboard warning. Download or resume a world before its save expires.
An explicitly ended or payment-suspended world does not restart automatically.
Email notifications and multiple host machines/regions are not implemented yet.
