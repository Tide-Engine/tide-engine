# `verified` and local handlers of match events

## Recorded-owner filters (2026-10-19)

Owner-approved: local match-event handlers accept with ownership and without
ownership, including verified handlers. Their Owner presence and player are
captured after match handlers at the event's queue turn, before destruction.
Later transfers/removal/destruction do not redirect an earlier event. No
explicit Owner/Session parameter is required. Without ownership complements
with ownership, including missing Owner; filters alone imply an entity target
and select match lifecycle listeners. Match code, local-world events, Forgotten
and nested queries retain their existing restrictions.

Generated event payloads add private player/presence metadata only for events
whose listeners filter ownership. Local dispatch compares that capture with
Session.player before touching attached local fields or calling the handler.
Ordinary occurrence identity/dedup stays unchanged: ownership-only corrections
do not repeat predicted occurrences; confirmed delivery uses its own capture.
No runtime API, world bytes/hash, C event header or session protocol changed.
The generated journal layout/build identity changes for affected games.

Shown/Hidden reconcile ownership as part of each listener's eligibility at
their delivered old/new endpoints. Owner value changes as well as presence
changes can produce transitions; transient changes still coalesce. Their
compiler-private availability helper accepts an optional receiving player.
Raw tick/arrival/visibility logs remain independent of local player identity;
frame reconciliation supplies Session.player. Hidden uses the prior Owner.

Authoritative recipient lifecycle journals capture Owner only when that whole
component is permitted to the recipient. A hidden Owner acts absent; metadata
is zero, and existing required-component masks exclude with-ownership listeners.
The incoming payload validator checks presence is 0/1 and an absent owner is
zero before dispatch. The seven ownership_events regressions cover delayed
delivery after transfer/removal, absent versus empty Owner, event-handler
changes, sparse filters, local fields, query rows, genuine lifecycle/death,
replay and confirmation, independent availability endpoints/corrections, and
complete journal equality across hidden-owner variants plus malformed metadata.
LSP ownership completion and hover cover local match-event handlers. Public
Events and the decided spec document the recorded-owner rule.

Validation: warning-free full native/web debug builds; 2,434/2,434 native CTest
cases (108.46 seconds), including all 239 editor cases and 32 GPU tests;
2,344/2,344 web cases (256.53 seconds), including WASI, WebGL/D3D11 and WebGPU;
and the docs production build pass. The final filter-only Spawned assertion and
corrected local-placement error fixture were also rerun natively. Existing
determinism pins remain unchanged. Private prototype targets stayed disabled;
no Gravewake browser run or CI was dispatched. Other OS/sanitizer coverage
remains CI work.

## Visibility availability and authoritative lifecycle (2026-10-15)

This supersedes the older arrival-through-`Spawned` contract below. `Shown`
and `Hidden` are engine-generated local notifications, with optional
`verified`, for match entity/component availability. A handler's complete
required/excluded component set determines whether it is available. Revealing
one component does not repeat unrelated initialization. `Shown` initializes
arrivals, reveals and actual spawns; `Spawned` is now only a genuine spawn.
`Hidden` reads the previous permitted components/singletons/query rows;
`Shown` reads the new permitted world. Ordinary replay with unchanged
availability records nothing. The final implementation reconciles availability
at local frame/delivery boundaries, rather than dispatching irreversible tick
transitions. Transient Add/Remove/Add operations coalesce to final availability;
a canceled predicted removal restores Shown. Replacing a present component does
not reinitialize it. Predicted and verified endpoints are independent, and
Hidden's old endpoint/query rows remain available until after its handler runs.

`hear_arrived(world, had, log)` records only `Shown`. The generated
`hear_visibility(world, had, destroyed, destroyed_count, log)` records old/new
availability in separate phases, skips `Hidden` for explicitly known permitted
real destructions and never infers `Destroyed` from missing state. Components
are the finest policy scope, never fields. Neither new event is game-sendable
or a match trigger; local-world components are refused, while match local
fields and local singletons retain their writable local behavior.

Raw tick availability still carries delivered listener masks for replay, but
it is no longer the presentation source of truth. `availability_codegen.inc`
generates local predicted/verified COW world snapshots and a private pending
cause log, only in games with availability listeners. Before ordinary local
handlers and `tide_local_forget`, each frame compares delivered endpoints using
the existing component-aware helper, emits Hidden(old) then Shown(new), and
updates the snapshots. Raw TIDE_HEARD_AVAILABILITY records are ignored during
local dispatch. Ordinary custom-event occurrence matching, event-time payloads
and relative ordering remain unchanged. Genuine Spawned/Destroyed retain
TIDE_HEARD_LIFECYCLE for authoritative replacement of guessed verified payloads.

Unchanged endpoint ticks plus physical world hashes and birth-page COW identity
skip the component/query scan and snapshot copy. Birth-page comparison matters
because born identity is intentionally outside the match hash. Same-tick
corrected state still reconciles. At most two permitted snapshots are retained;
private pointers/cause logs are zeroed in local pack/unpack and cleared on
free/disconnect. A local rebuild initializes availability afresh, so Shown
preparation must safely reuse/replace existing local resources. No stale world
ABI survives a code swap.

Bulk destruction uses sorted private fixed-header causes, unions duplicate
endpoint/certification flags, and binary-searches a privately sorted copy of
the explicit destroyed handles. Callback inputs may be unsorted and stay
caller-owned. These changes reorder no game events and prevent quadratic
chunk-unload reconciliation. An ignored scratch harness compiled generated
lifecycle C with clang -O2 against the debug engine library, timing only
reconciliation with a monotonic nanosecond clock (best of five). At 1k/4k/16k
unique causes, collection improved from 0.570/8.160/128.743 ms to
0.211/0.711/2.689 ms; with duplicate records, 1.106/16.641/259.099 ms became
0.394/0.888/4.406 ms. With real previous endpoints containing those entities,
single-record removal improved from 0.819/11.893/194.572 ms to
0.496/2.615/10.610 ms; duplicate-record removal improved from
1.314/21.922/360.723 ms to 0.689/3.309/17.775 ms. Construction, tick execution,
game-handler dispatch and local-field forgetting are outside those timings.
The 4096-entity regression verifies duplicate union, canceled predicted cause
pruning, genuine-death conceal suppression, arbitrary callback input order and
unchanged caller storage. After the independent malformed-journal validator
follow-up, fresh lifecycle passes 16/16 on native and WASI. Projection,
local_events and handover targets pass on both too (4/4 CTest each).

Every visibility-enabled generated game supplies `tick_client_events(world,
jobs, per_player)`, one tick for all `TIDE_GAME_PLAYERS` recipient logs.
Genuine Spawned/Destroyed select authorized listeners after match handlers
and before removal. Their union payload clears component slots no authorized
listener reads. Query records include only permitted rows and use effective
component presence for both required and excluded filters. Singletons remain
public, and existing plain-copy heap-handle clearing still applies. Recipient
snapshots additionally clear hidden scene policy/membership fields, hidden
Visibility fields and opaque Resident state, recursively through inline values
and fixed arrays, matching the canonical recipient codec. This covers direct
component/singleton snapshots and query rows; ordinary local full-world records
keep their existing behavior.
`check_client_events(data,size,&build,&count)` validates every header/span,
exact generated size, flags, listener masks, IDs, row stride/count and required
matching later row records before handler casts. Before typed reads it also
checks canonical boolean
representations and zero owned-container, Resident and private spans recursively,
including fixed arrays and recorded query rows. Generic custom events are
not server-journaled: they enter a discarded scratch log on this server path
and remain local products of projected client simulation. There is no generic
authoritative outward-event API.

Visibility-enabled spawn birth identities hash only the public spawn tick and
final entity handle (including generation). Archetype, enclosing scene and
component values are excluded: even a payload-free cause must not expose a
brute-forceable digest of a concealed card. This narrows corrected-spawn identity
to that key for visibility games; changing only a component value on the same
tick/handle does not replace its local fields. Full-world games retain their
existing content-derived birth identity. Packed recipient worlds still have
unknown birth identity and match by handle.

`TIDE_HEARD_DESTROYED_CAUSE` is a private payload-free record for destruction
permitted at event time, even with no death listeners. It has zero listener
and wire delivery flags. Normal predicted ticks record it too; the hearing
pipeline retains it as private predicted/verified metadata. Sessions remove
guessed causes from verified runs when adopting authoritative journals and
forward checked causes internally, never to a game handler. Local pending
causes retain separate endpoint bits until their clocks reach the death. An
authoritative certification bit survives consuming one endpoint; unconfirmed
predicted causes expire once an explicit verified endpoint passes their tick,
including when the server concealed rather than killed the entity. This avoids
stale guesses suppressing later Hidden notifications. This prevents previous-visible/current-dead inference
from suppressing concealment when a policy hides and destroys in one tick;
reveal-then-destroy records a true permitted cause. Sessions derive suppression
IDs from these records. Missing retained event history permits availability
cleanup without inventing a death; Forgotten remains the resource-release
fallback once both endpoints lack the component/entity.

Fixtures: `visibility_lifecycle` covers entity/component/without transitions,
old/new query snapshots, Add/Remove/replacement, arrival versus true spawn,
per-listener replay dedup, per-recipient event-time state and private union/query
scrubbing, malformed/truncated journal rejection, hidden deaths and same-tick
hide/reveal plus destruction. Additional endpoint regressions pin same-tick
Add/Remove/Add resource correctness, canceled conceal restoration, independent
verified query snapshots, delayed verified death causes, no matching Destroyed
listener, excluded private local-pack state/rebuild reinitialization, and stale
predicted-cause expiration before a later conceal. Complete recipient-journal
equality regressions cover hidden-value spawn/death variants, a cause-only death
with no eligible listener, hidden scene membership in direct/query snapshots,
and different opaque Resident stamps/digests. At that checkpoint, native/WASI
lifecycle passed 14/14; the bulk/validator follow-ups bring it to 16/16 above.
Projection and handover targets pass on both, local_events 4/4 and
handover 6/6. local_events now counts reappearance
after a corrected predicted removal, while its shield-at-spawn assertion remains
a genuine Spawned event-time handler. Existing local_events/handover initializers and
Duel/tower-defense presentation initializers use Shown. Reserved-name collisions
in fixtures/prototypes are renamed without changing their simulation formulas.
The initial five lifecycle tests, local_events, handover and tasks_local passed
native. The no-local-listener destruction-cause regression also passes in the
projection fixture, alongside exact scoped anchor placement and hidden-leaf
no-anchor checks. Fresh native session visibility fixtures pass 6/6: permitted death
uses final authoritative value 1260 at tick 259 exactly once for predicted and
verified listeners, unauthorized recipients receive none, private cause markers
never reach handlers, and history-expired recovery retains honest availability
semantics. Final native/web integration results are recorded with the visibility
validation. Before the endpoint follow-up, the full native debug suite passed
2,369/2,369 in 54.15 seconds, including 34 GPU tests and optional Cracked/Foldline
prototype tests. The optional web run was interrupted after 2,287 passes; it is
not a complete-suite pass. The changed Cracked/Foldline native/WASI/browser
coverage passed. Before the final birth/snapshot privacy follow-up, the fresh
standard native suite passed 2,356/2,356 in 379.95 seconds, including 31 GPU tests.
After the birth/snapshot privacy fix and namespace fixture, standard native
passed 2,357/2,357 in 83.03 seconds. Fresh complete standard native/web suites
for the final bulk-reconciliation/payload-validator follow-ups are pending.
User contracts
are in docs/spec.md, docs/game/events.md, docs/game/visibility.md and
docs/c/hosts.md.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Networking

- Game code tells verified state from predicted state with one word, `verified`, a parameter's modifier like `mut` (agreed 2026-10-05; `docs/spec.md`, Views and drawing, and Events). For example, a player death visual can wait until the death is verified.
  - In a view, `verified Health health` and `with verified Dead` read the world the server has confirmed, the session's verified world, which `tide_frame` is handed. A view with components of both worlds runs for the drawn world's entities that are the same entity in the confirmed one (`decl.verified_join`: each row looks the entity up there, `gen_row_join` in `compiler/src/codegen.c`, by handle and by what each was born as); one whose components are all `verified` runs for the confirmed world's entities (`decl.verified_entities`, whose `need_mask` is theirs). A view that takes any is handed the confirmed world (`decl.takes_verified`: `tide_run_view_<View>` gets `tide_verified`, which is `w` where the host gave no verified world). Values aren't blended. Showing other players from confirmed state, blended between confirmed ticks, is open, and may belong at another level.
  - A `verified` value is whole (owner, 2026-10-05: `verified` is valid wherever it makes sense): what the confirmed world keeps in its heap is read there, beside the heap of the world the frame draws. A handle's two `where` bits have a fourth value for it, `TIDE_IN_VERIFIED` (`tide/text.h`), which is only ever in a handle, never a `where` a write is given: `tide_frame` says which heap it is (`tide_text_use_verified`), reads find a handle's heap with `tide_heap_read`, and everything that changes a block still finds its heap with `tide_heap_of`, which the confirmed world's never is, so nothing writes there. Where a view's wrapper copies a `verified` component or singleton out of the confirmed world, it retags the copy's text, lists, maps, sets and grids (`tide_confirmed`, `gen_confirmed_copy` in `compiler/src/codegen.c`), so nothing of them is copied and functions the value is passed to read it by what it says. A list's elements and a map's entries are in the heap with their own handles, which still say the drawn world's, so a list or map whose elements hold text is copied to the scratch area with that text retagged (text is all elements can hold); the wrapper frees those when it's done (`tide_confirmed_mark`). A read path that's added to the runtime resolves a handle's heap with `tide_heap_read`, or it reads a `verified` value as empty. `compiler/tests/e2e/verified_heap` reads every kind from two worlds that ticked differently, and from a snapshot that shares the drawn world's pages, and checks the confirmed world's bytes are as they were.
  - In match code it's an error: the match is the same on every machine and has no "confirmed".
  - `compiler/tests/e2e/verified_views` checks views on one world, on two worlds that disagree about a handle, and on a client that predicts wrong for a while.
- **Local handlers of match events** (agreed 2026-10-05; `docs/spec.md`, Events): `local event(Hit hit) ShowHit(Body body, mut Fighter fighter, mut Fx fx)` hears the match's `Hit` on each machine, at the start of a frame, before the views, for each tick simulated since the last frame, in order. It takes the event, the target entity's components (read-only, `mut` reaching only their local fields, as in views), match singletons (read-only) and local singletons (`mut`), all of the match's as the event's turn left them, even if the entity is gone by the frame. The engine's own events have them too. `local event(verified Died died)` runs once, when the server confirmed the tick that sent the event.
  - **Once, under rollback:** each tick remembers which events it delivered locally. Run again, the same event (tick, event, entity and fields) stays silent; one only the corrected tick has is heard then, late; one the correction removed isn't taken back.
  - **Events only local code handles** cost the match nothing: its bytes, hash, snapshots and traffic are the same without them, and a machine with nobody to show it to does no work for them. `compiler/tests/e2e/local_events_same.h` pins that against the same game without them (`local_events_same`, `local_events_plain`), as `local_fields_same.h` does for local fields: a change to how worlds are kept or sent updates its numbers too.
  - In the compiler, such a handler is a listener: `decl.listens` (`check_listener` in `compiler/src/check.c`), kept in its event's `listeners`, never in its `handlers`, which the tick runs. Nothing of it is in the world. A tick records what listeners take only when it's given a log: generated `tide_world_tick_events` sets the log for the thread that runs the tick (a thread-local of the generated `.c`, `tide_listening`), and as each event's turn comes, after the match's handlers of it, `tide_hear_<Event>` (`gen_hear`) pushes a `tide_heard` (`tide/events.h`) and then a `tide_heard_<Event>`: the event, and the components and singletons its listeners take, gathered then, with a bit for each listener that runs for the entity. `Spawned` is recorded as the spawn applies and `Destroyed` before the row goes. An event with listeners and no match handler isn't even queued while no tick records (`tide_listeners`), unless it holds text, whose heap blocks every machine has to take the same way.
  - The frame runs them from the records (`tide_local_hear`, `gen_heard`), before `tide_local_forget`, so a handler still finds the local fields of an entity that's gone since; a row kept for another entity that had the handle goes first (`tide_let_go_<Component>_local`, by what each was born as). Games with listeners of entity events keep what entities are born as (`entities_born`).
  - Text, lists, maps, sets and grids of the match aren't recorded yet: a listener that names one is an error, and the copies have them empty. `World`, `Devices`, the input and `async` are errors there too. Ownership filters are supported by the 2026-10-19 rule above.
  - **A machine hears `Spawned` once for every entity of the match it comes to have** (agreed 2026-10-07, after the owner set a `local Texture` field in a `local event(Spawned)` handler for boxes the match's start spawned, and expected them textured: "view events should likely have a way to init themselves on join"; the details are Claude's picks, Provisional in `docs/spec.md`, Events). In the tick that spawns it, as before, or when the world it's in arrives: the entities a match starts with are spawned by `tide_world_start`, outside any tick, and a machine's world is always one it was sent, its own server's too, so neither was ever heard. What's built, and what code relies on:
    - **Generated `tide_world_hear_arrived(w, had, events)`** (`gen_hear_arrived` in `compiler/src/codegen.c`), the game's `tide_game.hear_arrived`: for each archetype some listener of `Spawned` runs for (`heard_for`), each row's entity that isn't alive in `had` by its handle (NULL: every one) goes through `tide_hear_Spawned`, the function a tick's spawn records with, so the handlers' bits, sparse filters and what they take are the same code. Then the rows its handlers' queries read (`tide_hear_rows`), and every record it made says the world's own tick. It's static, in the `.c` alone and only in a game with such a listener (`hears_arrivals`), so no game's header or hash moved: old and new tidec over every game's source list, with `--layout` and without, 256 games the same and the 6 with such a handler (Duel and its tests, two samples, two tests) with only those lines more.
    - **Rows too**: `compiler/tests/e2e/local_events` has `Spawned` handlers of both kinds with a query, whose rows are the arrived world's.
    - **Every archetype a listener runs for, not only the ones spawns make**, where a tick records at spawn sites alone: an entity that came to a component by an `Add` arrives with it. So `tide_hear_Spawned` has a caller in a game whose listener takes a component nothing spawns with, which used to be an unused function, an error.
    - **A client records them as a world it was sent is adopted** (`hear_arrived` in `engine/src/session.c`, from `load_snapshot`, before the world takes a slot of its ring, as the worlds it's compared with may be in that slot): into its host's log directly, never through the hearing's ticks, with the session's tick. Its first world: every entity, as both kinds of handler (`as` both bits), but for those of `tide_client_desc.base`, a world this machine had (a match that changed hands, a web build reloaded), which the client keeps a copy of until then (`tide_client.had`; `base` itself goes when a world built from it comes out wrong). A world sent again, in two passes where they differ: `TIDE_HEARD_PREDICTED` for what its own world at that tick lacked (it heard the ticks up to there as it ran them), `TIDE_HEARD_VERIFIED` for what its last confirmed world lacked (the ticks since never were confirmed one by one). Only when its host takes the log (`taken`), as for ticks.
    - **A record's `born` is 0 for an entity of a world that came as bytes**, which is the same as anything: the frame's handler finds whatever local fields are kept under the handle, and writes rows that say 0.
    - **Nothing else is made up**: no `Destroyed` for what a world sent again no longer has, no `PlayerJoined` for who was there.
    - **A host that ticks one world itself** calls `tide_game_api.hear_arrived(&world, NULL, &events)` once the world has started (`docs/c/hosts.md`): records with `as` 0, both kinds of handler.
    - **Effect handlers fire on join too**, which the owner accepted: Duel's `ShotFired` flashes at each bullet in flight (and now sets its `trail`, which was the origin for those), and the tower defense sample's build ring pops on each standing tower. The connect-four sample's "Player N joined the game" line would have named the hands that were there, so it hears `PlayerJoined` now, which a machine hears from its own on, with a query for the hand that tick spawned. Nothing tells a handler a fresh spawn from an arrival: that's an open question, not built.
    - Tests: `compiler/tests/e2e/local_events` (a host with one world: the start's entities, a world it had, a world from 40 ticks before; the match on a network: the first player's world, a late joiner's; `local_events_a_world_sent_again_says_what_is_new`: a machine that stopped past target 5's spawn hears it once and nothing it had; one machine through a session) and `compiler/tests/e2e/handover` (a local field counts each mover's `Spawned` on each machine: 1 through two handovers). Both new cases fail with `ran` and `sure` taken for NULL.
- The contract for what local handlers hear (Claude's first version, proposed 2026-10-05, for the owner to confirm; `tide/events.h`):
  - `tide_event_log` is a growing run of records, each a `tide_heard` (its size, tick, event, `wants` and `as` (`TIDE_HEARD_PREDICTED`, `TIDE_HEARD_VERIFIED`), target and what it was born as, the bits of the listeners that run, and `same`, a hash of the event's fields) and then what the game's build lays out after it. A log says whose build its records are (`build`), and another build takes none of them. A record whose event is `TIDE_HEARD_ROWS` is no event: the rows a tick recorded for its events' handlers' queries, after the tick's events (see Queries in views and local handlers, under ECS and simulation state), which an event says it reads with `TIDE_HEARD_READS_ROWS` in its `wants`.
  - `tide_game.tick_events(world, jobs, events)` is `tide_game.tick` recording into `events`; NULL in a game with no listeners. Generated `tide_world_tick_events(w, jobs, events)` is always there, and `tide_world_tick` and `tide_world_tick_on` record nothing.
  - `tide_game.hear_arrived(world, had, events)` records a `Spawned` for each entity `world` has that `had` doesn't (NULL: every one), as `world` has it; NULL in a game with no local handler of the match's `Spawned`. It's in the game's table alone, as `hash_world_on` is, with no declaration in the header.
  - `tide_client_desc.events` is the log a client's host takes from, once it's `taken`; `tide_session_events(session)` gives a session's, and marks it taken, so a session records from the first call on. A client keeps a `tide_hearing`: for each tick it ran and hasn't had confirmed, what the tick delivered (`tide_heard_mark`) and what its last run sent (`run_tick` in `engine/src/session.c`: `tide_hearing_begin`, `tide_hearing_end`). A tick's new events go out as `TIDE_HEARD_PREDICTED`; once the server confirms the tick, its last run's go out as `TIDE_HEARD_VERIFIED` (`tide_hearing_confirm`). A world the server sends again drops the ticks before it unheard (`tide_hearing_skip_to`), but for the `Spawned` of what it has that this machine didn't (above); a host that stopped taking for `TIDE_HEARD_TICKS` ticks is recorded nothing more until it asks again. The server's own world records nothing.
  - Generated `tide_local_events(local, events)` moves a log's records into the local state and leaves it empty; the next `tide_frame` runs them first. Records with no `as` come straight from a tick, with no session between, and run both kinds of handler: a C host that ticks a world itself passes a log to `tide_world_tick_events` and hands it over, and what it ticks is all there is. `tide_host_game.events` is `tide_local_events`, which the standard host calls before `frame` with `tide_session_events`, asked for before the session's update.
  - Hot reloading drops what's recorded (`tide_hearing_rebuilt` in `tide_session_set_game` and `tide_session_migrate`): what each tick delivered is kept, so ticks run again stay silent, and ticks confirmed as the old build ran them tell `verified` handlers nothing. A match that changes hands starts a new client, which hears the ticks after the new host's confirmed one again.
  - `compiler/tests/e2e/local_events` checks all of it: a C host with one world, a match on the loopback network where a client predicts wrong (heard once through replays, late for what only the correction has, never taken back, `verified` only what the server had), and one machine through a session.

## Confirmed views versus events (2026-10-11)

Verified view values now blend separate confirmed endpoints by default, including verified query bindings and mixed joins; membership/discrete fields use the upper endpoint. Verified event/local handlers still read exact confirmed event state. This does not interpolate or delay handler dispatch. Frame hosts forward the new interval arguments; blended heap copies keep reads attached to their actual endpoints.
