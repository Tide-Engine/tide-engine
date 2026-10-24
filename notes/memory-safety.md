# No Tide code can corrupt memory

## Shard membership and recovery reservations (2026-10-24)

Lifecycle messages validate their 0/1 flag and PlayerID against the game's
configured count before indexing a membership word. The bitset grows through
existing bounded list operations and writes through `tide_list_at_mut`, so
snapshots retain COW ownership; at the player ceiling it contains 1,024 words.
The invalid-slot regression covers zero, one past this game's count and
UINT32_MAX, with unchanged world hashes. Admission release validates the
player before touching arrays and cannot clear an active/migration identity.
Checkpoint version 5 bounds pending notification counts against remaining
bytes and the game's player count, and validates each flag/ID/desired state.
No raw pointer or authentication cookie enters match state. Recovery and
native/web validation are recorded in `notes/sharding.md`.

## Optional voice and local packets (2026-10-22)

Session local data checks packet bounds, greeted source/cookie, recipient
identity and payload size before callback/relay; sender work has a fixed
token bucket. The package validates Opus packet duration/channels and uses
bounded speaker/jitter/sample queues, generation-tagged capture, copied
playback cutoffs and SPSC ownership. Codec work never runs in the audio
callback. Host stop follows mixer forget before any queue/code is freed.
Malformed control/oversize/wrong-cookie packets, rate bounds, mute/reconnect
and finite lossy-decoder output are covered on native/WASI. Detailed contracts
and validation: `notes/voice-package.md`.

## Computed property receivers (2026-10-21)

Array property setters use checked slot helpers and compiler-known offsetof
paths. A missing slot remains NULL through nested fields, and the setter returns
without writing a shared zero sentinel. Receiver indexes are captured once for
compound operations; RHS evaluation retains ordinary ordering. properties checks
negative/maximum indexes and nested array fields. Component ownership sidecars
stay on synchronous call stacks and add no stored world/entity pointers. The
existing generation checks, hierarchy traversal bounds and scratch lifetimes
also cover ParentToWorld; projected roots consult anchors only while alive.
Replay/packing, verified reads and full validation are recorded in language.md.

## Events in runs (2026-10-20)

A run of events (notes/language.md, Events in runs) is read only by the
generated code of the build that pushed it, during the tick that pushed it:
its count is the one its pusher wrote, and `tide_command_fits` refuses the
kind in a queue unpacked from bytes, so no game value or received byte sizes
a read. Finding a target reads the table's entity column only below the
table's count (`index - first` is unsigned), and its rows can't move during
the run (structural changes wait in the queue); anything else goes through
`tide_entity_location`, as before. Counting ordinary items in a row reads
heads only below the page's `used`, in steps of the first item's own size,
which `tide_command_fits` has checked for items from bytes. `events_runs`
covers gone, reused and moved handles and the null target.

## Double transcendental and matrix values (2026-10-20)

The private binary64 adapter uses memcpy-based word access, requires binary64
without excess precision and namespaces all public/internal kernels. Large trig
argument reduction uses the bounded upstream tables and arrays for finite
binary64 inputs; public entry points handle nonfinite values before reduction.
The pow scale adjustment forms negative exponent-shift bits unsigned, avoiding
the upstream negative signed left shift. A sign comparison is made explicitly
unsigned. Source notices and provenance are retained. Matrix narrowing reuses
the checked scalar double-to-float conversion; packing/repair/interpolation walk
typed columns. No new allocation or data-dependent matrix index is introduced.
Conformance, special-value, raw-bit and generated-code coverage is recorded in
notes/language.md.

## Double values and exact decimal conversion (2026-10-20)

Double-to-int/long checks NaN and exclusive upper/inclusive lower limits before
any C integer cast. Double-to-float handles NaN and values outside binary32's
finite conversion range explicitly, including the nearest/even overflow midpoint;
vector narrowing calls the same scalar helpers. Input/command repair follows
every double component and existing explicit padding/optional clearing.

Exact number reading retains at most 800 digits plus a nonzero-tail flag. Its
128-word integers cover the complete accepted binary64 rounding range; extreme
exponents return overflow/zero before constructing powers. Printing uses 40-word
integers and a 384-byte double output buffer for the compiler's F0..F17 formats.
No decimal conversion calls libc at runtime. The old float paths retain their
bits. Runtime cases cover every finite exponent of both signs, integer and float
cast boundaries, negative zero, NaNs, largest/smallest finite values, exact
midpoints and long nonzero tails. Full contracts and validation: language.md.

## Shared player ceiling (2026-10-20)

The approved 65,536 ceiling uses uint32/varint player and fragment indices,
including player index 65,535 and the distinct server input slot 65,536. Frame
bounds widen input/event arithmetic before accepting allocation sizes, and
handwritten game tables above the ceiling or supported frame size are refused.
Kick checks the game's own count before indexing. Wire sets maintain a bounded
active-word prefix; their wire decoder cannot add bits beyond fixed storage.

Client handover digests and recovery output arrays are sized for their game.
Managed checkpoints contain game-sized value arrays rather than pointers and
are staged on the heap; restore bounds its bitset prefixes. Generated raw and
network serialization headers also move off the stack, preventing two large
input headers from overflowing the 1 MiB stack. The exact-ceiling fixture checks
last-slot inputs, Audience membership, delta round-trip and reconnect/leave
behavior; the 270-session fixture covers actual migration and projection.
Contracts and final validation: netcode.md and language.md.

Recorded containers (2026-10-19) keep fixed journal heap handles zero and store
only bounded serialized values in optional tails. Size arithmetic widens before
the uint32 record limit; tail length and zero padding are checked before reads.
Recipient fixed-value checks still precede typed decoding, which validates
counts, UTF-8, map order/keys, grid shapes/records and the existing nesting
guard in an isolated temporary heap. Failed validation frees that heap without
touching match/local state. Successful delivery frees its separate heap only
after handlers have acquired ordinary local ownership of explicit copies.
Context restoration includes both heap resolvers and scratch state. Source
world/availability endpoints are resolved explicitly while recording. The
event_containers and retained visibility_lifecycle forgery cases cover this;
implementation and final validation are in language.md.

Query local fields (2026-10-19) keep all local-field pointers out of recorded
rows; only entity/birth values are added when needed. Lookup validates attached
identity before reading, returning defaults on a birth mismatch. Mutable reset
releases old ownership and initializes defaults in the same cell, preserving an
outer alias rather than removing/moving its row. All new helpers are generated
over existing bounded table/attached APIs. Local heap fields retain actual local
ownership. Mutable references are acquired at row entry, so resets never occur
in unspecified C operand order. Read-only borrows sharing query writes acquire
the same cell, avoiding stale default borrows. The eleven
query_local_fields cases include differing predicted /
verified births under an active outer alias, containers, COW snapshots and
5,002 rows; the scalar safety fixture covers repeated host snapshots/frames.

Recorded-owner event filters (2026-10-19) capture only a player value and an
integer presence flag, never a world pointer or component borrow. Recipient
lifecycle payload validation checks that flag is 0/1 and absent-owner bytes
are zero after validating the complete generated span. Ownership is checked
before attached local fields are created/accessed. ownership_events covers
forged metadata, destroyed targets and delivery without a current world.

Mutable World.Set overlap (2026-10-19) uses compiler-private synchronous
stack bindings, keyed by world, component and full entity generation. Target
validation still precedes replacement. Container replacement releases/assigns
the active mutable copy's current ownership rather than stale stored handles;
source values and read-only helper arguments retain ordinary deep value copies.
Final row write-back uses current COW pages. Bindings never escape into a
world/task frame or survive a wait. world_set_mut checks nested containers,
stale/missing targets, snapshots, replay and packed state without runtime changes.

Texture metadata (2026-10-18) reads no pixel payload. Grid references reuse
mesh.c's bounded see_grid directory/record validation, including arbitrary
stale handles and dimensions; packed names resolve against the current pack;
managed targets resolve live state and check selected-color validity before
calling a borrowed local host resolver. Existing malformed-grid texture tests
retain the same bounds path. No simulation pointer or new owned handle is added.

## Recipient state decoding (2026-10-15)

The generated visibility decoder builds a temporary world and publishes it only
after consuming the exact packet and validating entity handles, duplicate rows,
presence/bool bytes, container counts, UTF-8, grid records and hierarchy links.
Lengths and allocation arithmetic widen before bounds checks. Recursive values
have a 256-level codec nesting guard; encoding stops with a diagnostic advising
splitting deep data across entities, and decoding rejects excess nesting without
changing the existing world. Element counts are checked against remaining wire
bytes where their minimum representation is nonzero. There is no small fixed
world-size cap: recipient state must support large chunked worlds and sparse
global entity IDs. Allocation exhaustion follows the existing runtime policy.

Sparse entity raw/delta readers check page span arithmetic, ascending unique page
indices, packet spans and free-list validity. Missing pages read as absent slots,
never through a NULL pointer. `test_entity_sparse.c` exercises raw truncations,
delta/base combinations and COW writes across holes. `visibility_projection`
rejects truncated canonical state without changing an existing world. Typed
authoritative lifecycle records are checked for payload size, allowed listener
bits and query-row shapes before generated handlers cast their bytes. A generated
plain-value walk then checks raw boolean bytes are 0 or 1 and rejects nonzero
owned heap handles, Resident bytes and private scene/Visibility fields. It walks
inline records and fixed arrays in both event union slots and query rows, using
byte offsets so malformed bool/pointer representations are never read as C
values. Otherwise a handler forwarding a whole component/singleton to an
ordinary helper could reach a forged heap offset despite the direct field-read
restriction on recorded handlers. `visibility_lifecycle` covers malformed spans,
masks, direct/nested boolean representations, forged list offsets through helper
indirection, Resident bytes and private metadata, while valid logs still run.

## Immutable assets and received heap state (2026-10-11)

New immutable descriptors retain owned bytes outside match pages; no raw pointer enters a game list. Generated writes detach with actual ownership before obtaining writable addresses. Refcounted snapshots and retired pools preserve borrows through source release, including helper side effects during evaluation. `_at_mut` rejects immutable descriptor/pack storage. Packing sizes widen before the uint32 limit check. Dictionary decode checks shapes, multiplication/byte limits, unique IDs and content hashes; page patches check asset identity and source slice bounds. Before publication, registered descriptor spans/identities/counts and free/pending chains are validated, including duplicate locations and cycle rejection. A location/identity sum and count enter the world hash so omitted lease records fail authentication. Live grid headers are cell data and are never scanned as allocations. `test_asset_lists.c` covers every truncated reference packet, unknown descriptor IDs, forged and cleanly omitted leases, missing-pack fallback and snapshot lifetime; generated mutations, mixed grid state and lossy-session retry are checked by `asset_sharing`. Native/WASI cross-transfer evidence uses the owner's real project. Sanitizer/other-platform validation remains CI work; CI was not dispatched.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Language

- **No Tide code can corrupt memory.** It's the language's promise to every game (the owner, 2026-10-05: "a Tide issue, not a mod issue"), which mods will rest on, and it's tested as one. Tide alone, that is: a game's C is trusted, as ever. What it rests on, and what code that touches a game's values has to keep:
  - The first two (nothing a game says is trusted to be a place, and ints never do what C leaves undefined) are in `AGENTS.md`, Language.
  - **Places at the ints' edge are worked out 64 bits wide** (`engine/src/grid.c`). A parallel loop's offset is any int and its cells are anywhere an int can say, so its geometry is in `int64_t` until a place is known to be a cell's: an offset is first what's left of it by its block's size (`tide_par_begin`, `tide_par_list_begin`), a task's blocks are numbered from its own first one (`tide_par_tile`'s `origin`), so block k's place is a cell's and never past an int, and a window is where it starts and how many cells it is across (`extent`), never where it ends. Along an open axis a block that would go past the last int is left out, and a cell a step reads past it is the one its own arithmetic wraps to (`place_past`). A foreach's runs end past their last cell, which can be the last int: the end and what counts up to it are `int64_t` in generated code too (`gen_grid_foreach`).
  - **A stack that runs out ends at its guard page** on Linux and Android: generated code and the engine compile with `-fstack-clash-protection` there (`cmake/TideFlags.cmake`, `config_flags` in `compiler/cli/build.c`), so a function with locals bigger than a page touches each page as it makes room. Windows' targets check big frames by themselves. Without it, a frame of 64 KiB (a struct of matrices is enough) steps over a thread's 4 KiB guard and writes in whatever is below: measured, `subq $65544, %rsp` and then a `memcpy`. What recursion with no end does beyond that isn't decided: it crashes, with no message of the engine's, and on the web's threads other than the page's own there's no guard at all (see `docs/spec.md`, Open, at the end). macOS wasn't checked: Apple's clang is said to check big frames by default.
  - **A game's struct fits the bytes the engine keeps it in.** Where a struct of the game's is read and written in room of a fixed size, tidec refuses one that's bigger, by the size generated C has for it (`decl_layout`, `compiler/src/types.h`): an `audio` declaration's state in the megabyte a state has at the most and its knobs in an `Effect`'s 256 (`check_audio_sizes`; see Rendering and platform, Audio). What comes to keep a struct that way checks it the same way: a C `_Static_assert` in generated code is no error of the language's, and a clamp at run time doesn't stop the write. The mixer also gives an effect the bytes it says it keeps, and runs none that says more than there are, so C that fills a `tide_audio_effect` by hand is held to its own number.
  - **A value too big to copy never is.** A struct with a long array (an echo's delay line, hundreds of kilobytes) would overflow the audio thread's stack as a local, an argument or a temporary, so the checker lets such a value be read only where it is (`in_place_ok`), and generated C has no function that takes one by value (see Rendering and platform, Audio, A state can be big).
  - **`compiler/tests/safety/`** is the tests: each `.tide` file there is a game that tries a way in, and says in its first lines what it expects: `// expect: <text>` for a line it prints with `Debug.Log` (one or more), `// stops: <text>` for the engine's own stop, `// ticks: <n>`, and `// draws` for one whose frames also go through the renderer in a second test (`_gpu`, label `gpu`). A new file is picked up by the next build. `safety/host.c` runs it as a session would: snapshots that share the world's pages before every tick, views twice a tick with a snapshot as the confirmed world, local events, the world as bytes and back and a tick more on both, on the platform layer's threads, with a hand made up frame by frame that presses the middle of the screen, drags far off it, tabs, steps and types more than a field holds. What a game's frames play goes through its `audio` declarations as the mixer would run them, each state between bytes that have to stay as they were (`run_effect`; see Rendering and platform, Audio). `safety/expect_safety.cmake` checks how it ended, and fails on anything a sanitizer says. They run in every suite (label `safety`), where they pin what each case gives, the same on every platform, and they mean the most in the `sanitize` preset.
  - **The `sanitize` preset** (Linux and macOS) is the whole build, optimized, with AddressSanitizer and UndefinedBehaviorSanitizer (`TIDE_SANITIZE`): `cmake --workflow --preset sanitize`. On Windows it's a WSL build. Leaks aren't looked for (tidec and the tests end with what they hold). Box2D's and Box3D's own sources have one check off there (`nonnull-attribute`: they clear arrays they haven't got). CI runs it on Linux in every run of the Build workflow (see Packaging and releases), with the LLVM the other Linux jobs use: the whole suite passed that way before the job was added, but for the `gpu` tests, which no runner has a window for. ASan sees a read or write outside an allocation, and a world's heap, the scratch area and an archetype's pages are each big allocations of the engine's own, so a mistake inside one isn't seen: there, the tests rest on what they print, and on the world coming back from its bytes with the same hash.
  - **JSON is read from bytes anyone could have sent** (2026-10-11; `engine/src/json.c`, `tide/json.h`). A reader has its text's end beside where it is, and every read is checked against it: nothing depends on a NUL, a closing quote is looked for before a string is decoded, and a backslash at the end is no escape. What a string decodes to is never longer than what's written (an escape of six bytes is at most three of UTF-8, a pair of twelve is four), which is what its block in the scratch area is sized by; keys and enum names go through a 128-byte buffer and are no key when longer. Arrays and objects nest `TIDE_JSON_DEPTH` (32) deep, in what's skipped as in what's read, so neither `tide_json_skip` nor generated readers, which call each other as the types nest, recurse deeper than that, and generated readers fill values where they're kept (a list's slot, a struct's field) rather than through locals: a map's value is the one a reader holds on its stack, once a level. Text of `TIDE_JSON_LIMIT` (64 MiB) or more isn't looked at, and what a read makes (elements, entries, text) is counted against `TIDE_JSON_ROOM` (128 MiB) before it's made, so a few kilobytes of `[{},{},...]` for a big struct fail the read rather than end the program at the scratch area's limit. Numbers are digits and a saturating power of ten: an int that doesn't fit fails the read (nothing wraps), and a float's big integers are bounded by the 128 digits kept and the range floats have. `tide_str_split` counts its pieces before it makes their list and stops at 2^28-1, the last one the rest of the text. `compiler/tests/safety/json` reads 131,072 open brackets, every start of a document, every byte of one made ten others (2,590 reads: 628 are still a lobby, the rest nothing), numbers of 131,072 digits, a megabyte of escapes, a million pieces, and keeps what it read in the world across the host's snapshots and packing; `tests/test_json.c` has the reader's calls against text cut inside a string, an escape and a character, and both limits without the memory.
  - What the tests found the first time (2026-10-05), all fixed: a parallel loop with an offset or cells near an int's limit overflowed its geometry and crashed; a foreach over an open grid never reached the cells at the top of an int's range; text of a size no int holds, a clip's corner and a render target's pixel count were made ints before they were clamped; an int slider across all the ints made its value from a float that didn't hold it. Beside them, in what the existing suite already ran: the server counted the players being sent a tick's packed world on the server rather than on the pack, so a pack another tick replaced was freed under a player still being sent it (`shared_pack` in `engine/src/session.c`), and a match's start was read as a struct where it lay in a message, unaligned.
  - **Skinned model input** (2026-10-08): raw glTF/GLB decoding checks accessor, hierarchy, palette, influence and key spans before indexing or allocating. Portable blob getters check record/reference bounds, and full import/pack validation checks their contents. The generated runtime Decode path validates vertex field types before its fixed attribute copies. Models expose no writable backing bytes; typed Bone indexing checks skeleton identity and range before a TRS read/write, with a default read and no write for an incompatible reference. `tests/test_skinned_decode.c` cuts exact-sized allocations at every length and spoils every byte; `tests/test_pack.c` checks malformed records and unchanged loaded assets after a rejected content preflight. The focused 14 skinned tests passed on Windows clang AddressSanitizer. `compiler/tests/e2e/skinned` checks Pose copy-on-write, persistent storage, snapshot/serialization replay and native/web match hashes. Other sanitizer targets remain the requested CI workflow's job.

## Rewind geometry and endpoint heaps (2026-10-11)

Historical queries retain only values: entity generations, shape/body handles, finite pose/geometry and bounded native point arrays. Native helpers check shape counts, finite/range inputs and rotations before indexing or querying. Capture checks list capacity failure and disables that incomplete history endpoint rather than memcpy through NULL. Rewind integer differences widen before subtraction; malformed fractions fail, without repair into an accepted timestamp. BVH traversal checks forward/end jump bounds and uses ordinary checked Tide list indexing.

Custom view interpolation snapshots each endpoint's text, nested lists/maps/sets and readonly grid directory/chunks before calling the custom routine, preserving the correct heap and both world hashes. Directory allocation sizes widen to 64 bits and reject overflow; grid shapes are generated constants. Original grid storage is untouched. Only match offsets are retagged as verified; owned scratch results retain their tag. `verified_heap` exercises component, singleton, mixed and query interpolation, text, keyed map values and grid cells, with scratch cleanup and unchanged endpoint hashes.

## T? fields (2026-10-11)

A field declared `T?` is its value and whether there is one, in its holder's bytes (`notes/language.md`, T? fields). What's read of one with no value is never what it held: generated code reads the value only behind `ok` (`tide_optional_<T>_get` gives zeros otherwise), `!` gives the type's default, and what's kept is zeros from the moment the field is given nothing, its text and lists released, so no handle to a block the heap gave out again is left in it. Reading one that holds a list copies the list, so no local shares a world's. A field of an element past a list's end reads as nothing and a write there does nothing, as for any field of an element. A command's or an input's flag comes off the wire as one bit, and an input from C with no value but bytes beside it is zeroed before `Sanitize`. No new runtime path takes game values: it's all generated code over the struct, text and list helpers there were. `compiler/tests/safety/optional_fields` sets, takes away and sets again text, lists and structs of them every tick for 64 ticks, assigns fields to themselves and to each other, reads and writes fields of elements at the ints' edges, and does the same in the local world, under the host's snapshots and packing; its sums would differ by any stale read.
