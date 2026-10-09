# mockup.games

A local first slice of a collaborative game-prototyping service. Describe a small game, bring references or assets, play it together, and iterate. The default direction is a small, polished playable game; each project and iteration can instead prioritize a quick mechanics prototype.

The application lives in this directory with its own Node package. It invokes an installed Tide toolchain rather than importing engine internals, so the service can later move into its own repository.

## Run locally

Use Node **24.16 or newer**, npm, and an installed Tide package that can build web games. Earlier Node versions can crash on Windows networking; see `notes/testing.md` in the engine repository.

From `mockup/`:

```powershell
npm ci
Copy-Item .env.example .env
npm run build
```

Edit `.env` as needed. `TIDE_BIN` selects a Tide executable; otherwise the service finds the Windows installation or uses `tide` on `PATH`. `OPENAI_API_KEY` enables generation, and `OPENAI_MODEL` selects the provider model. Without a key, accounts, projects, collaboration, uploads, feedback and the real Moonwake starter still work; chat requests are saved without claiming that a game was generated.

Provision an owner account **while the service is stopped**, then start it:

```powershell
node --env-file-if-exists=.env scripts/admin.mjs owner@example.com
npm run dev
```

The provisioning command prompts for a hidden password and creates or updates that account. The data directory has one writer, so do not run this command beside a running service. `MOCKUP_ADMIN_EMAILS`, when set, further restricts provisioned owner accounts; adding an email there alone does not grant administration.

### Google and Discord sign-in

Create a web OAuth client in [Google Cloud](https://console.cloud.google.com/apis/credentials), or an application in the [Discord Developer Portal](https://discord.com/developers/applications). Set the matching client ID and client secret in `.env`. Complete credentials enable that provider's button; incomplete configurations stay hidden. Client secrets and provider tokens never go to the client.

Register the exact redirect URL for each enabled provider:

- Local Google: `http://127.0.0.1:4173/api/auth/google/callback`
- Local Discord: `http://127.0.0.1:4173/api/auth/discord/callback`
- Public Google: `https://mockup.games/api/auth/google/callback`
- Public Discord: `https://mockup.games/api/auth/discord/callback`

Use the same hostname and port as the application, including locally. Set `MOCKUP_PUBLIC_ORIGIN` and `MOCKUP_SECURE_COOKIES=1` behind an HTTPS proxy. The callback origin is configured by the operator, not taken from request headers. Return destinations are limited to the home page and invitation paths.

Existing email/password users sign in first, then connect a provider from their account. Social identities use the provider's permanent account ID and require a verified email; matching email addresses alone never merge accounts or grant administration. Tokens are used for the sign-in exchange and are not retained. The owner account stays explicitly provisioned.

Implementation references: [Google OpenID Connect](https://developers.google.com/identity/openid-connect/openid-connect), [Discord OAuth2](https://docs.discord.com/developers/topics/oauth2). The Google button uses the [official branding asset](https://developers.google.com/identity/branding-guidelines).

Open [http://127.0.0.1:4173](http://127.0.0.1:4173). The preview server uses port `4174`. `HOST`, `PORT`, `PREVIEW_PORT` and `MOCKUP_DATA_DIR` are configurable. `npm start` runs already-built client files; `npm run dev` refreshes the client bundle first.

## What works

- Google and Discord sign-in when configured, local email/password fallback, cookie sessions, shared projects and invitation links.
- Concurrent editing of the shared game brief through Yjs, with live teammate cursors and typing/editing presence.
- A shared playtest: one browser runs the Tide game and streams its canvas to spectators through WebRTC. Anyone in the room can take control immediately; held input is cleared at each handoff. Taking control does not move the simulation into another browser.
- Responsive phone layouts, touch movement and dash in Moonwake, and touch forwarding for shared control. Spectator streaming currently carries video only.
- An actual Tide/WebAssembly starter, Moonwake, with procedural visuals, twelve collectible moonseeds, obstacles, dash, particles, completion and restart.
- An optional Responses API generation pipeline. The agent receives the current source, brief, recent conversation, uploaded assets and feedback, then returns Tide files. Failed compilation can receive two repair passes, for up to three build attempts.
- A shared asset library that preserves original uploaded bytes and distinguishes game assets from visual references. Uploads are limited to 10 MB each. The first builder accepts exact uploaded paths for PNG/PSD images, WAV/Ogg audio and self-contained GLB models. Other formats can be retained as references; JPEG/WebP game-texture conversion and multi-file glTF imports are not implemented.
- Separate revision artifacts and source sets. Work based on a superseded revision stays as a candidate; accepting a version checks the currently shared revision before changing it. Earlier versions remain available.
- Feedback pins with the played revision, an actual screenshot when capture succeeds, recent controller inputs, viewport information and available browser diagnostics. These are visual/input captures: browser world snapshots and replay are not implemented.

The playtest host must remain connected. Another browser can host after it leaves, but the current simulation is not transferred. Localhost works directly; testing from another device needs HTTPS, a reachable `HOST`, and matching `MOCKUP_PUBLIC_ORIGIN` and `MOCKUP_PREVIEW_ORIGIN` settings. Reliable connections across arbitrary networks need a configured TURN service. `MOCKUP_ICE_SERVERS` accepts the WebRTC ICE-server JSON configuration. The current room shares one run; automated setup of separate networked game instances for multiplayer designs remains unbuilt.

## Owner view

The provisioned owner can inspect every project, its members, revisions and recent jobs. Administration shows the global account count, per-project usage and the latest 100 jobs, rather than a separate global user directory.

Provider usage records input tokens, cached input tokens, output tokens and request duration, including charged failed or incomplete attempts. Set all three rate variables in `.env.example` to enable estimated USD costs. Rates are captured with each usage record; missing rates remain unavailable. Estimates exclude build hosting, storage and other infrastructure charges.

Build wall time is measured. Project storage includes actual original asset files, published HTML artifacts and each job's work folders and compiler caches, including failed builds. Files are measured without following symlinks or Windows junctions; shared original blobs are counted once in the global total and wherever referenced in each project's total. Saved source arrays and feedback capture payloads are counted as their serialized UTF-8 bytes in the store. Shared account/project/job metadata, CRDT state, session records and JSON field overhead are excluded, so this remains attributed project storage rather than a measurement of the entire data directory. Runtime counters show process memory, JavaScript heap, uptime, active/queued jobs and account/project counts. Work folders and compiler caches currently remain on disk without a retention policy.

## Tests

Run from `mockup/`:

```powershell
npm run check
npm test
npm run test:starter
npm run test:browser
```

The server and pipeline tests cover accounts, authorization, persistence, concurrent brief edits, control handoffs, revision races, usage accounting, asset validation and compile repair with provider/build stand-ins. They make no paid model requests.

The starter test builds and runs the same gameplay checks natively and under WebAssembly/WASI, including mobile multitouch input, packed-world restoration and exact replay hashes. The browser test needs Chrome or Edge; set `TIDE_BROWSER` if automatic discovery cannot find it. It uses a temporary data directory and a muted headless browser to exercise real streaming, remote input, concurrent editing, captured feedback, phone layout and owner administration.

## Boundaries and next work

This is a local development service. Billing, plans, free-use allowances, email verification/password recovery, isolated public build workers, global resource quotas, retention cleanup, browser world-state export, replay and state-preserving host migration remain unbuilt. The provider integration has not been verified with a live paid request. No TURN service, public deployment or `mockup.games` domain configuration is supplied here.

Generated previews use a separate origin and port from the authenticated application, with sandboxed frames, scoped preview tickets and restricted browser policies. The local builder also validates source paths and exact uploaded asset references. Those measures do not replace filesystem/process/network isolation for public multi-tenant builds.

For extraction, keep `client/`, `public/`, `server/`, `runtime/`, `scripts/`, `starter/` and the package files together. Generation currently reads optional Tide documentation from the parent repository, and the starter's web test uses `../cmake/run_wasi.mjs`; bundle those references or replace their paths when moving the directory. Runtime data in `.data/`, secrets in `.env`, generated client dependencies and build caches are ignored by Git and need separate handling when moving a running installation.
