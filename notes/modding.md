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

## Agreed 2026-11-14

The owner settled who builds on desktop, in a discussion that started from Steam Workshop support. Nothing of it is built.

- A mod is compiled on the player's machine from its Tide source, by a compiler the game ships with. A mod never arrives compiled, since a native library can't be checked. Platforms without a compiler remain open.
- Two ways to take mods, both wanted, the developer's choice. **Compiled with the game**: the game ships its `.tide` files and builds with its mods as one program; the first requirement above (same storage, schedule and speed) describes this way. **Loaded into the game**: the game ships finished with declarations and no code, and a mod compiled by itself is loaded as a library. The owner wants this so that a game needn't give its source away.
- Declarations are visible to mods unless the developer hides them with a marker (name to pick). Agreed "for a first run".

- C is unsafe for mods by default: a mod calls a game function that reaches `extern` only where the developer marked it for mods (marker's name to pick).
- Loaded mods: their systems run serially in a fixed order at the named phases' boundaries for a first version; desktop first, the web after.
- Loaded mods: a combination of the game's components that the game never makes should work, not be an error (the owner found the error "too limiting", adding "i could be wrong"). The proposed way, tables made at run time that the game's systems also go through, is in the spec and unchecked against the code.

The owner's main concern is malicious code. The answer given, not yet a decision of its own: a mod is Tide alone and Tide can't corrupt memory, so a mod can only make the calls the engine offers; the sandbox covers bugs of ours.

The second way needs runtime work (mods' types described at run time, a mod's systems scheduled at load), which is the owner's to agree and write. Its proposed shape and the questions awaiting the owner are in `docs/spec.md`, Mods, Open: how new combinations of the game's components are stored, the sandbox against Steam's client, and an interpreter for consoles and phones. Shipping the game as bitcode and linking again per mod set was weighed and not chosen.

## What is built

`virtual` and `override` work for ordinary packages. The first web build-service slice and `tide publish` remain in the repository with tests. The service is on hold and is not deployed. Restrictions on mod source and calls, games loading alternate builds, the joining prompt and desktop sandbox integration are not built.

## Withdrawn decisions and open design

On 2026-10-06 the owner reopened who builds games with mods. A replacement for desktop was agreed on 2026-11-14 (above). Remote compilation as a required service, fetching only from that service, service signing and revocation, the meaning of a `mods` setting, and a closed game's source being available only to the service are therefore not current requirements.

The historical proposals remain in `docs/spec.md`, Mods, with fetching a platform build made elsewhere still unagreed for platforms where no compiler runs. Do not infer an approved design from the inactive service's implementation. Resuming its deployment requires the owner's instruction.

## Documentation validation

The 2026-10-11 guide cleanup checked these statuses against the spec's Mods section and the complete build-service note. It changes documentation only; no service or compiler tests are required, and no deployment or CI run was requested.
