# Modding

Subsystem requirements and status, moved from `AGENTS.md` on 2026-10-11. The language design, including provisional behavior and open questions, remains in `docs/spec.md`, Mods. The inactive service's implementation and tests are in `notes/build-service.md` of `Tide-Engine/tide-services`.

## Agreed requirements

- A mod is a package added by someone other than the game's developer. It compiles with the game as one program, with the same storage, schedule and speed. Nothing is interpreted, and games carry no separate runtime for mods.
- Every machine of a match runs the same build, local code included. Even a view-only mod can give a player an advantage. A build's mods are a set; listing order changes nothing.
- A mod is Tide alone: no `extern`, C, C++ or libraries. Its files are virtual, as a web page's are. Its code must not reach outside the game; `Session.Connect` and `Clipboard.Copy` are examples of calls the future sandbox must restrict.
- Replacement requires both an explicit `override` and a declaration marked `virtual` by its developer. Matching uses the name and parameters, including system filters. The override substitutes code while compiling and retains the original declaration's schedule position and ordering attributes. Two mods replacing one declaration are an error naming both. Calling the replaced body is not supported.
- Before joining a different modded build, local code must agree after being told its mods. Nothing is fetched before that agreement. The player prompt belongs to the game, in its own look and language; the engine draws none. Whether to prompt for developer-approved mods is the game's choice. Telling players about arbitrary mods was a condition of the proposed service; that service is on hold.
- Memory safety is Tide's promise to every game, not an extra rule for mods. The compiler alone is not a security boundary. Desktop modded builds require the OS sandbox as a second layer; the web has the browser's. A system that cannot sandbox a build runs no mods. WebAssembly is not the agreed desktop sandbox: it would put the whole native game in a runtime.
- Mods must have parity across Tide's target platforms, phones and consoles included (owner, 2026-10-06). Whether a store or system permits downloaded code is the game's developer's responsibility; the engine does not preemptively remove mod support from a platform.

## What is built

`virtual` and `override` work for ordinary packages. The first web build-service slice and `tide publish` remain in the repository with tests. The service is on hold and is not deployed. Restrictions on mod source and calls, games loading alternate builds, the joining prompt and desktop sandbox integration are not built.

## Withdrawn decisions and open design

On 2026-10-06 the owner reopened who builds games with mods. No replacement was agreed. Remote compilation as a required service, fetching only from that service, service signing and revocation, the meaning of a `mods` setting, and a closed game's source being available only to the service are therefore not current requirements.

The alternatives and historical proposals remain in `docs/spec.md`, Mods: compiling on the player's machine where a compiler can run, or fetching a platform build made elsewhere. Do not infer an approved design from the inactive service's implementation. Resuming its deployment requires the owner's instruction.

## Documentation validation

The 2026-10-11 guide cleanup checked these statuses against the spec's Mods section and the complete build-service note. It changes documentation only; no service or compiler tests are required, and no deployment or CI run was requested.
