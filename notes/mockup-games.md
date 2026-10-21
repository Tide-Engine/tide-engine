# mockup.games moved to its own repository

On 2026-10-09, the owner moved the service from `mockup/` to [Tide-Engine/mockup-games](https://github.com/Tide-Engine/mockup-games), checked out locally at `D:\Projects\mockup-games`.

The new repository preserves the three original service commits through a subtree extraction. Its `notes/service.md` carries the implementation contracts, product decisions, tests and limitations previously recorded here. Its `SETUP.md` is the operator checklist; deployment configuration and GitHub Actions live alongside the service. Make service changes there.

The standalone service uses an installed Tide package. Eight generator reference pages were copied from this repository's `docs/`, and `cmake/run_wasi.mjs` was copied into its `scripts/`, removing sibling-checkout dependencies. These are versioned snapshots; engine changes do not automatically update the service.

Ignored local data and starter artifacts were copied separately. The source and destination store hashes matched before the old `mockup/` directory was removed. No local data or OAuth secrets were published.

Extraction validation passed 34 service tests, JavaScript checks, muted browser collaboration checks and native/WASI starter checks. Windows and Linux starter replay hashes matched at `6704842711661338406`; the standalone production container passed persistence, routing, preview, SSE and shutdown checks. No engine code or build target changed, so no engine suite was required for this relocation.

The old mod build service (`builds/`, now in tide-services) remains on hold and unrelated to mockup.games.
