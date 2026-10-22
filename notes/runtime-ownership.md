# Runtime written by Claude for now

## Tide's audio inside Unity (2026-10-22)

Owner: "lets get audio working". An embedded frame now submits its audio:
`tide/host.h` calls `tide_platform_audio` embedded too (it skipped it), and
`platform.c`'s embedded branch submits to the mixer and stops natively, or opens
the page's own audio device once on the web. The new embed surface is the
agent's: `tide_embed_audio` and `tide_embed_microphone` (embed ABI 5, `embed.c`,
over the existing `tide_audio_out_fill` and `tide_audio_in_push`),
`tide_platform_embed_microphone` (`embedded.h`), and `tide_embed_status`'s
`microphone` byte. `tide_embed_destroy` forgets the mixer first. Generated code
calls nothing new. Contracts and tests: `notes/unity.md`, Audio and the editor's
workflow.

## Tide's renderer inside Unity (2026-10-22)

Owner: "let's do the 3d drawing and shaders". The Unity front end's drawing
replays the renderer's GPU calls (`notes/unity.md`, Tide's renderer in Unity). The new platform
surface is the agent's:
- `platform/src/gpu_record.c`: `tide_gpu_record` and its begin/end/reset/close,
  declared in `gpu.h`.
- `tide_platform_embed_render`, `_render_view` and `_reset` (`embedded.h`, at the end of
  `platform.c`), which point `gpu` at it and run `draw_list`. The scene view's picture
  added `view_only_3d` and `view_world_to_clip`, read in `camera_3d_to_clip` and
  `draw_list`'s step loop.
- `TIDE_RENDERER_WGSL` (`shaders.h`), the list `platform/tools/spirv_shaders.c`
  now writes from.

`tide/host.h`'s embedded camera became matrices (`tide_run_view_camera_transform`,
`_projection`, set with `tide_draw_set_camera_matrices`) for `tide/embed.h`'s new
`tide_embed_camera`. Generated code calls nothing new.

## Optional session voice (2026-10-22)

The owner requested an optional Opus voice package and selected working session
voice after the need for local audio/session hooks was explained. This maintains
the existing agent-owned package/session/platform areas. No new generated-C
runtime function or language builtin is introduced: ordinary extern calls reach
the package. New C host surface is `tide_voice_provider`, set/get registration,
`tide_host_game.voice`, and `tide_session_listen_data`, `_send_data` and
`_data_generation` with its callback/byte limit. The codec, queues and stream
state are in `packages/voice`; mixer and capture APIs remain unchanged. Hosts
and libraries rebuild together for the optional host-table member. Stop runs
after mixer forget and before unloading package code. Full contracts, limits,
tests and provisional implementation choices: `notes/voice-package.md`.

## Session.invite (2026-10-21)

Owner-approved (every web game's multiplayer as "host a room, copy a link,
send it"). Generated code calls no runtime function for it: `session.invite`
reads the generated `tide_local.tide_invite` (256 bytes, so a game's layout
hash changes once), which the generated `tide_local_set_session` sets to the
room and the new generated `tide_local_set_invite` replaces. New platform
surface, the agent's: `tide_platform_room_invite(room, out, size)` and
`TIDE_INVITE_BYTES` (`tide/platform.h`, `platform/src/rooms.c`), the page's
`tide_web_room_invite` import (`tide_web.h`, `tide.js`), and the optional
`tide_host_game.set_invite` after `set_session`, which `tide/run.h` fills and
`tide/host.h` calls each frame: hosts and game libraries rebuild together.
`tide_embed_status` is unchanged. Contracts and tests: webrtc-and-rooms.md.

## Draw.Sprite (2026-10-21)

Generated code calls four new `tide/draw.h` functions for `Draw.Sprite`, the
owner having asked for a sprite call ("yes do it", 2026-10-20):
`tide_draw_sprite(d, texture, center, size, color)`, `tide_draw_sprite_rotated`
(a rotation before the color), `tide_draw_sprite_part` (a `tide_rect` of uvs)
and `tide_draw_sprite_part_rotated`, all inline over
`tide_draw_sprite_placed` (more of a run of quads is stored where the call
is, as a mesh's is). The texture is a `tide_texture_ref`, as
`tide_draw_mesh_texture`'s, already sampled as the call says. The list gained
`pack_named`, `pack_slot` and `pack_frame`, the pack image found last this
frame. Contracts in drawing.md, A sprite in one call.

## Drawing's third round (2026-10-20)

Generated code calls the same `tide/draw.h` names as before; several are now
header-inline over out-of-line functions that are the old bodies
(`tide_draw_plain_placed`, `tide_draw_round_rect_placed`, `tide_draw_mesh_placed`,
`tide_draw_mesh_3d_placed`, `tide_draw_instance_stored`), and
`tide_draw_mesh_with` is what `mesh.c` calls for Tide's lists. The draw list
gained `quads`, `quad_count`, `quad_capacity` and `mesh_last`, and
`tide_draw_kind` gained `TIDE_DRAW_QUADS` at its end: hosts and libraries
rebuild with the engine, as for any draw-list layout change. The embed packet
is unchanged (quads go out as meshes). Contracts in drawing.md, Recording
inline and quads as quads.

Plain shapes as records, after: the list gained `plains`, `plain_count` and
`plain_capacity`, `tide_draw_kind` `TIDE_DRAW_PLAIN` (after
`TIDE_DRAW_QUADS`), the command a `plain` member, and `tide/draw.h`
`tide_draw_plain`, `tide_draw_color_bytes`, `tide_draw_plain_color` and
`tide_draw_expand` (a list's commands with each plain shape one of its own,
which the embed packet goes out as), and `plain_open` and
`tide_draw_plain_run` (a run's count, the open one's being the list's records
from its first on). `tide_draw_rect` and the other plain shapes keep their
names and arguments. `tide_gui_group` gained `panel_plain`, `commands_from`
and `plains_from`.

## Map views generated code keeps (2026-10-20)

Owner-approved (the performance pass's second round: "a list view resolved
once per function", for map lookups): generated code now calls `tide/map.h`'s
`tide_map_view` (a struct of two `tide_list_view`s), `tide_map_views_none`,
`tide_map_find_in` and `tide_map_view_entry`, and through them
`tide/list.h`'s `tide_list_view_hold`, `_holds`, `_find` and `_at`, all
inline. `tide_list_view_of` became `tide_list_view_find`, which fills a view
in place (it grew a held list's identity and the first 32 chunks' places);
`tide_map_find` keeps its signature over `tide_map_find_in`. Each function
declares `tide_mv`, a view for each map it reads or sets by key (codegen's
`declare_list_caches`), beside `tide_lc`. Contracts in ecs.md and
language.md, Map views. `tide_list_cache_fill_mut` finds a side-by-side
list's block once (internal; same results).

`tide_scratch_mark`, `tide_scratch_reset` and `tide_heap_flush` are inline
over `tide_scratch_top` and the out-of-line `tide_scratch_reset_to` and
`tide_heap_flush_pending` (the functions as they were), as
`tide_list_prepare` is over `tide_list_prepare_borrowed`: generated code
calls the same names as before (ecs.md, Per-row scratch reset and heap
flush inline).

## A page's hash by stripes (2026-10-20)

`tide_hash_wide` in `engine/src/net.c` keeps its signature with a new
definition (ecs.md, A page's hash by stripes), and `tide/net.h` gains
`tide_hash_wide_baseline`, the same number without AVX2, for tests. Generated
code calls neither directly and nothing new; `tide_page_hash` is unchanged.
Session `PROTOCOL` is 17 (netcode.md).

## Events in runs (2026-10-20)

No runtime function is new to generated code, and none changed its signature
or behavior. Generated code that records a system's one Send in runs now
pushes them with `tide_queue_push` (one item a run, notes/ecs.md, Events in
runs) and no longer calls `tide_queue_push_run`, which stays in tide/table.h
with its tests (`tests/test_table.c`). `tide_table_column_mut`'s inline fast
path skips writing back a page pointer that didn't change.

## Performance pass (2026-10-20)

No runtime function is new to generated code. `tide/list.h` gains
`tide_list_view`, `tide_list_view_of` and `tide_list_view_at`, and `tide/map.h`
`tide_map_hash_word`; `tide_list_prepare` is inline over the new
`tide_list_prepare_borrowed` (the function as it was). These are helpers of
the inline `tide_map_find`, `tide_map_hash_bytes` and `tide_list_prepare`
(and of map.c), which generated code reaches only
through those, as before. `engine/src/grid.c`'s window functions keep their
signatures; `buffer_for` is private. Generated list helpers read
`tide_list_cache`'s fields as `tide_list_cached_at_mut` does. Contracts are in
ecs.md and schedule-and-threads.md, Performance pass.

## View packets and schemas for tide/embed.h (2026-10-20)

The owner approved the Unity front end's interface (`notes/unity.md`, Phase 1
interface): a view packet written by generated code, a command packer and a
schema. Generated code built with `tidec --embed` now calls new agent-maintained
engine functions in `engine/src/embed_game.c` (`tide/embed_game.h`):
`tide_packet_begin`, `_grow`, `_section`, `_text`, `_list`, `_places`, `_end`
and `_free`, and `tide_schema_json`. They grow buffers through `tide_realloc`
and bound every size; a game built without `--embed` calls none of them, and
no existing function, struct or the generated header changed. The platform's
`tide/embed.h` host is agent-maintained with the rest of `platform/`.
`tide_embed_game` later gained a last member, `defaults`, a generated function
`tide_schema_json` calls for each component's default bytes (Unity's inspector
shows them), and then `component_scenes` after `component_locals`, a generated
table of which components are scenes' tags, which the schema marks (2026-10-21);
still no generated call into the engine beyond those above. On 2026-10-22 the packet gained an EVENTS section
(`TIDE_PACKET_EVENTS`, version 3: the events this machine heard, which the generated code keeps in a `tide_packet`
of its own and writes with `tide_packet_grow`), `tide_embed_game` its last members `event_count`/`events`, the
schema an "events" list and kind "event", and `defaults` commands' values too; no new engine function. The same
day `tide_packet_text` and `tide_packet_list` began each entry from a multiple of 8 of STRINGS' bytes (version 4), and
generated code packs lists, maps, sets and grids with the functions above.

## Double Position attribute (2026-10-20)

The owner-approved attribute extension changes compiler acceptance and uses
existing generated correction helpers. Double errors retain the field's type;
no new generated-C/runtime function, storage contract or runtime implementation
is introduced. Scope and validation: notes/language.md.

## Double math and matrices (2026-10-20)

The owner's completion request extends the existing temporary math maintenance
area with tide_*_d transcendental functions and inline tide_*_d2x2/d3x3/d4x4
matrix helpers, following float naming and operation order. No engine state,
container or networking runtime API changes. The vendored binary64 kernels
live in engine/src/math64 and ship as source through the existing recursive
engine-source installation/CLI build. Float kernels remain in math.c unchanged.
Contracts, numeric accuracy limits and validation: notes/language.md.

## Double numeric values (2026-10-20)

The owner-approved scalar/vector addition extends the existing temporary math,
text/JSON and migration maintenance areas. Generated basic math/conversion/hash
calls use inline tide_*_d/d2/d3/d4 in tide/math.h. Text calls
`tide_str_add_double`, `tide_str_add_d2/d3/d4`, `tide_str_parse_double`,
`tide_json_double` and `tide_json_add_double`, following their float counterparts.
The feature is authorized; these helper shapes and narrowing/literal choices
are first-build implementation picks, recorded provisionally in the spec.

No world/container/session runtime structure or exported networking function
changes. Input doubles reuse two ordered tide_bits_put/get calls. Layout metadata
appends TIDE_LAYOUT_DOUBLE; generated games and migration hosts rebuild together.
Full contracts, gaps, safety and validation: notes/language.md and
notes/memory-safety.md.

## Shared player ceiling (2026-10-20)

The owner approved the shared 65,536-player/connection ceiling. This maintains
the existing temporary player/session/hosting/platform exception. No new runtime
function is generated or exported. tide_player_set gains an active-word prefix;
tide_server_recovery carries caller-provided game-sized digest/identity output
buffers instead of global-ceiling arrays. Its getter fills those buffers.
Generated hosts/games rebuild together through the Session ABI marker. Session
protocol 16 and managed checkpoint envelope 2 are described in netcode.md.

## Inside Out conventional completions (2026-10-20)

Under the owner's conventional API-completion instruction and existing math
maintenance exception, generated Math calls use inline tide_asint_f/f2/f3/f4,
tide_asfloat_i/i2/i3/i4 and tide_hash_f/f2/f3/f4. Bit casts use memcpy; float
hashes reuse existing integer xxHash mixing. No simulation layout or arithmetic
changed. The standard host additionally obtains the existing window ratios
through tide_platform_screen_scale and supplies draw-list screen_scale metadata;
generated frames call no platform function. Hosts/libraries rebuild for that C
layout addition. Private CPU aggregate borrowing adds no runtime entry point or
public Tide reference semantics. Contracts/validation: notes/language.md and
notes/drawing.md.

## Explicit skinned instance adapters (2026-10-20)

The approved `Draw.Instanced` skinned-model overload needs no new runtime API.
Generated code uses the existing rest/pose atlas or saturated-layout capture,
separate-rig pose binding, scratch reset and shaded-draw recording interfaces.
Runtime implementation and ownership stay as recorded below. Pose values are
captured before return and compatible items batch through existing palette
pages. Compiler fixtures and platform pixels are recorded in `notes/drawing.md`.


Recorded containers (2026-10-19) introduce no runtime API, struct or function
contract. Compiler-private read analysis and self-contained journal tails reuse
existing value codecs, tide_writer/reader, heap resolvers/allocation and event
log ownership operations. The shared generated codec moves from
client_state_codegen.inc to value_codec_codegen.inc; recipient state retains
its existing encoding. Delivery/validation uses a temporary heap and restores
both resolvers/scratch afterward. Details and validation: notes/language.md.

Query local fields (2026-10-19) add private generated get/mut/reset helpers and
conditional row birth metadata. They use existing tide_attached_*, tide_table_*,
scratch, seeding and owned-value functions. No runtime declaration or new
generated-C/runtime function contract is introduced. Mutable mismatched births
reset in place so active outer field pointers stay valid. See language.md.

Recorded-owner event filters (2026-10-19) use compiler-private payload metadata
and optional player parameters on generated private availability helpers.
No engine runtime API, tide_heard structure, world state or session protocol
changes. The existing agent-maintained event log/hearing functions retain
their occurrence deduplication contract. Details and validation are in
notes/verified-and-local-handlers.md.

## Conventional resource metadata (2026-10-18)

Within the owner's authorized API-completion pass, generated local code now
calls `tide_texture_size(tide_texture_ref)` and borrows/restores a frame resolver
through `tide_texture_target_pixels(tide_target_pixels_fn)`. Both live in the
existing temporary draw/mesh maintenance area. The resolver is a borrowed
machine-local callback, carried by tide_draw_list.target_pixels, not a world
pointer or a simulation allocation. Hosts and game libraries rebuild together
for the draw-list C layout change. Asset/grid/target safety and coverage are in
notes/textures.md; no container layout or simulation ABI changed.

## Approved math API completions (2026-10-18)

The owner authorized FromToRotation and Scale as conventional additions within
the existing math maintenance exception. Generated code now calls inline
`tide_fromto_q(float3, float3)` and `tide_scaling_f4x4(float3)` in `tide/math.h`.
The former uses a private inline `tide_fromto_unit_f3` to normalize directions
across the finite float range before building the shortest rotation. The
existing two-argument `tide_scale_f4x4` still means scalar multiplication of a
matrix. No runtime data structure, world layout or existing function changed.
Contracts and validation are in `notes/language.md` and `notes/shaders.md`.

## Recipient visibility (2026-10-15)

The owner approved entity/component visibility, shared live audience groups,
partial client simulation, authoritative corrections and separate availability
events. This work maintains the existing temporary session/entity/event runtime
exception. Generated code uses private specialized group/policy and canonical
value codecs built on existing containers; it calls no new exported runtime
function. Optional `tide_game` projection, input-filter, lifecycle-journal and
availability callbacks extend the existing session boundary. Hosts and generated
games rebuild together through the Session ABI identity marker. Protocol 15's
simulation identity includes the recipient state and listener journal schemas.

Entity metadata pages may be absent between visible global entity IDs. Dense
tables keep their previous hash/pack/delta representation; sparse tables have an
explicit indexed-page representation. Generated local delivery reconciles
Shown/Hidden against its last presented predicted and verified endpoints, with
private COW snapshots and destruction-cause tracking. Rollback can restore a
resource after canceled concealment without replaying unchanged availability.
Contracts and validation are in ecs.md, netcode.md, memory-safety.md and
verified-and-local-handlers.md. The Tide API spellings and conservative handling
of opaque server state are documented
separately from agreed visibility semantics in docs/spec.md.

## Managed hosting and simulation identity (2026-10-15)

The owner requested implementation of local deployment, isolated WASM matches,
prepaid billing and durable recovery. The existing temporary session/hosting
exception includes `tide_game.simulation[32]`, protocol 15 HELLO negotiation,
hosting envelope 2 and the handwritten managed reactor launcher. New host-only
`tide_server_recovery`/getter and descriptor identities restore reconnect
cookies/identities, pending players, seed, dt, start and admission metadata;
`tide_session_fail_message` provides actionable host admission failures.
Generated Tide calls no new runtime function. World packing/hashes and game
save content remain separate from host metadata. Earlier statements below
about unchanged protocol/ABI describe the original native-host increment.
Full contracts, ownership, validation and launch gaps: managed-hosting.md.

## Frustum culling (2026-10-15)

The owner approved automatic culling plus developer bounds and disable controls. Generated settings call new agent-maintained draw functions: `tide_draw_set_frustum_culling`, `tide_draw_unset_frustum_culling`, `tide_draw_push_frustum_culling`, `tide_draw_set_bounds`, `tide_draw_unset_bounds`, `tide_draw_push_bounds` and `tide_draw_push_no_bounds`. They use the existing draw-list boundary. Place, mesh-cache, shader and draw-list C layouts grow with visual bounds/plane caches, so hosts/libraries/generated games rebuild together. No simulation layout or hash changes. Contracts, conservative fallbacks, append behavior and validation are in `notes/drawing.md`, Frustum culling.

## Dedicated hosting (2026-10-15)

The owner approved multiple headless matches per process, match IDs, separate
create/join policies and pluggable authentication. New hosting.h/hosting.c and
platform serve/server_run hosts are agent-maintained under the temporary runtime
exception. Optional tide_server_desc.identity and host metadata getters bind
reconnects to verified identities outside world state. Generated Tide calls no
new runtime function; the handwritten server launcher/host uses these APIs.
No inner session protocol, tide_game ABI, snapshot or hash changes. Contracts,
provisional C entry points and validation are in dedicated-servers.md.

## Vulkan's SPIR-V in tide_shader (2026-10-14)

`tide_shader` (`tide/draw.h`, agent-maintained) has two more fields, `spirv` and `spirv_words`. Generated code fills them under `TIDE_SPIRV`, which the header defines everywhere but the web (Android since 2026-10-14, when it began drawing with Vulkan), with the words tidec translated from the shader's WGSL. No runtime function is new, and generated code calls nothing it didn't. The struct's layout changed, so hosts, game libraries and generated games rebuild together, as with every `tide/draw.h` layout change. The platform's `tide_gpu_pipeline_desc` (`platform/src/gpu.h`) carries the same two fields. Contracts and tests are in `notes/rendering.md` and `notes/shaders.md`, both under Vulkan / SPIR-V.

## Input prediction callback (2026-10-13)

The owner approved input Predict() and [DontPredict]. Their generated adapter
adds optional `tide_game.predict_inputs(world, keep) -> bool` to the existing
agent-maintained session boundary. It changes only remote inputs of a predicted
world, preserves real sampled slots and suppresses invented button edges. The
session tracks changed guesses for authoritative replay; the existing let-go
callback still enforces the guess limit. No new exported runtime function or
match-world state is added. Hosts and generated games rebuild together through
the changed Session ABI header marker. Contracts/tests are in notes/netcode.md
and notes/language.md.

## Retained asset lists boundary (2026-10-11)

The owner explicitly approved `tide_list_prepare(tide_list *list, uint32_t element_size, uint32_t where)` before generated writable element access. It preserves the logical value and detaches borrowed asset storage with actual destination ownership. This task maintains the temporary list/heap/pack/session implementation and its generated wrappers; no user-facing reference type or mutability change was added. Runtime reads use existing list interfaces, and `_at_mut` refuses immutable descriptors/raw pack data until preparation. `engine/src/asset_store.*` owns private retained content/proof/pool/origin lifetimes. `notes/ecs.md` describes the storage contract, `notes/netcode.md` the protocol 14 callback change, and `notes/assets-and-files.md` the asset semantics and tests. Animation interpolation and palette math remain unchanged.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Layout

A game's own network (2026-10-11): new runtime surface, the agent's, in a new `engine/include/tide/network.h` and `engine/src/network.c`. The owner asked for what it's for (a game brings its own transport and service with no engine change) and saw a sketch of the provider; the functions' shapes are the agent's. Generated code calls none of it: a game's own C calls `tide_network_set`, and hosts call `tide_network_get` (through the new `tide_host_game.network`, which `tide/run.h` fills) and `tide_network_match_host`, `_join`, `_leave`, `_update`, `_after` and `_address`. `tide/net.h` gained `TIDE_ADDRESS_NETWORK`, which `tide_address_format` writes and `tide_client_update` gives a room's 15 seconds for a first answer; `tide_host_game` grew by one pointer at its end, so hosts and game libraries rebuild together; generated `tide_local.tide_room` is 64 bytes where it was 8 (`TIDE_NETWORK_ADDRESS_BYTES`), so a game's layout hash changes once. No session function changed its signature and the wire format is as it was: a network's meeting place travels as the room's key. Contracts and tests are in `notes/own-network.md`.

Texture compression (2026-10-11): `tide_draw_texture` now carries an optional borrowed `tide_texture_blocks` mip chain beside its RGBA fallback. Pack construction/lookup helpers are called by compiler/engine implementation code only; generated code gains no runtime call and `Texture` values keep their existing layout. Hosts/libraries rebuild with the C draw structure. GPU residency retains no CPU pack pointer. Contracts, safety bounds and validation are in `notes/textures.md`.

JSON and text as bytes, pieces and numbers (2026-10-11): generated code calls new runtime functions, all the agent's, in a new `engine/src/json.c` and in `engine/src/text.c`. The owner approved the language feature (typed JSON generated at compile time) and the text primitives' names; the functions' shapes are the agent's, on the same terms as the rest of the text and list runtime below. `tide/json.h`: `tide_json` (a reader: where it is, its text's end, depth, what it has made, whether it failed), `tide_json_begin`, `tide_json_begin_bytes`, `tide_json_end`, `tide_json_null`, `tide_json_bool`, `tide_json_int`, `tide_json_float`, `tide_json_text`, `tide_json_char`, `tide_json_name`, `tide_json_number_next`, `tide_json_array`, `tide_json_item`, `tide_json_object`, `tide_json_field`, `tide_json_key`, `tide_json_skip`, and for writing `tide_json_add_text`, `tide_json_add_char` and `tide_json_add_float`; the header is included only by a game that calls `Json.Read` or `Json.Write`. `tide/text.h`: `tide_str_parse_int`, `tide_str_parse_long`, `tide_str_parse_float` and `tide_utf8_valid` (defined in `json.c`, with the numbers' code). `tide/list.h`: `tide_str_to_bytes`, `tide_str_from_bytes`, `tide_str_split` and `tide_str_split_char` (defined in `text.c`, beside its search). Nothing existing changed its signature or its layout, and no world byte is new: what a read makes is ordinary text, lists and maps in the scratch area. Contracts and tests are in `notes/language.md`; what the reader does with hostile bytes is in `notes/memory-safety.md`.

Web requests (2026-10-11): `tide_io` gained `request_begin` and `request_poll`, and the engine `tide_web_request_begin`, `tide_web_request_poll` and `tide_web_answer` (`tide/io.h`), which generated code calls for `await request.Send()`; `platform/src/fetch.c` and `platform/web/tide.js` implement them beside the fetch they extend. The owner asked for the feature; the functions' shapes are the agent's. Contracts and tests are in `notes/assets-and-files.md`.

A request's time limit (2026-10-11): `tide_io.request_begin` and the engine's `tide_web_request_begin` each gained a last parameter, `float timeout` (seconds; 0 is none), `tide_io_error` gained `TIDE_IO_TIMED_OUT` (11), and `tide/io.h` has `TIDE_REQUEST_TIMEOUT_MOST`. `platform/src/fetch.c`'s `tide_request_begin` and the page's `io_request_begin` (`tide_web.h`, `tide.js`) take the same parameter. Nothing else changed its signature; a host's own `tide_io` that fills `request_begin` takes one more argument. The owner didn't see the shapes: the lead agent announced the feature and the owner didn't object. Generated code passes `request.timeout` and calls nothing new. Functions that start tasks (the same day) call no runtime function that wasn't called: they're handed the task's owner and worlds as hidden parameters and call the generated `tide_start_*` functions. Contracts and tests are in `notes/assets-and-files.md` and `notes/language.md`.

The temporary-maintenance exception applies to the AI agent working on an assigned task. References to Claude below record who wrote these implementations, not an exclusive right to maintain them. The owner still takes them over when ready, and new runtime functions needed by generated code require agreement before either side depends on them. This clarifies the existing working agreement; it does not transfer ownership of other runtime work.

`tide_draw_list` owns a retained texture lookup index alongside its texture array (`texture_index`, `texture_index_capacity`). `tide_draw_reset` rebuilds it when textures are pruned and compacted, `tide_draw_forget` clears it, and `tide_draw_free` releases it. Its private `skinned` pointer owns frame-local palette memoization; reset clears its entries before pruning, forgetting clears them, and freeing releases its retained capacities through private `engine/src/skinned_cache.h` lifecycle helpers. No generated runtime call changes; the C structure layout does, so hosts, libraries and generated games rebuild together.

### Runtime written by Claude for now

Owned-animation scans and binding (2026-10-11): `channel_valid` checks the first time separately, then reduces independent adjacent-time comparisons and finite-value bit checks, allowing native/Wasm vectorization without changing interpolation arithmetic or accepted keys. Every time, value and cubic tangent remains checked before output writes; either ordering of consecutive signed zeros is invalid. Separate sample/pose binding validates both complete node tables before private little-endian node readers reuse those checks for names, parents and local TRS within the same opened models. No cache, retained pointer, generated runtime call, API, representation, world byte or allocation change is introduced. `notes/assets-and-files.md` records the failure/ownership coverage; paired measurements and remaining component-owned state costs are in `notes/benchmarks.md`.

Separate-export binding and owned-channel validation (2026-10-11): the owner approved matching unique names/parent chains independently of source defaults, with direct source local TRS copying. This supersedes the rest-equality requirement in the original separate-export implementation below. In-place sampling now initializes required ancestors from source defaults, keeping parity with rebinding sampled poses; unrelated target nodes retain target rest. Exact Bone/blend compatibility and unsafe-wrapper rejection are unchanged. The generated-C boundary, canonical representation and public function signatures are unchanged. `channel_valid` scans already checked channel spans directly, checking all key times and every value/cubic tangent for finiteness before any output writes; it avoids repeating descriptor/span checks and constructing temporary vectors for every key. No cache, borrowed lifetime or deterministic arithmetic changes. All 48 skinned checks, 2,079 native and 2,050 web debug CTests and the docs build pass. `notes/assets-and-files.md` records real-export evidence and remaining binding limits; `notes/benchmarks.md` records the paired owned-animation measurement and remaining world-copy cost.

Large-scope optimization (2026-10-11): existing `tide_map_find` is now forced inline in `tide/map.h`, allowing fixed generated kinds to specialize checked list addressing and hash/equality calls. Its signature, stored hashes, probe order, insertion order and snapshot bytes are unchanged; all callers rebuild. Explicit skinned capture now passes the existing current pack proof's unique numeric identity into the private model memo. A matching identity skips repeated node/palette byte comparisons; different identities still compare exact owned dependencies, including equal-content reloads and version collisions. Captured bytes remain owned for deferred preparation after unload. No generated runtime entry point or draw-list layout changes. New regressions cover map collision chains across chunks/index wrap and immutable snapshots, plus immediate/atlas captures across equal and changed pack reloads. Full native (2,078) and web (2,048) debug suites pass, including deterministic and GPU checks. Measurements and remaining scope limits are in `notes/benchmarks.md`.

Omitted-rest capture validation (2026-10-11): the frozen implementation passes all 2,067 native and 2,037 web debug CTests, plus 46 focused native skinned and 30 pack checks. The WASI runtime passes all 406 checks with the unchanged pose-bit assertion `e5d23f82e5d18f26`. OpenGL, WebGL and WebGPU palette/pixel checks pass; native preparation still matches serial bytes with three worker callbacks. The complete 2,895-file production/test manifest stays unchanged throughout validation, and builds are warning-free. Performance comparisons are recorded separately in `notes/benchmarks.md`.

Sampling/capture optimization validation (2026-10-10): the frozen implementation passes all 2,067 native and 2,037 web debug CTests, plus 42 focused native skinned runtime checks. The WASI runtime passes all 402 checks with the unchanged pose-bit assertion `e5d23f82e5d18f26`. OpenGL, WebGL and WebGPU palette/pixel checks pass; native preparation matches serial bytes with three worker callbacks. All builds are warning-free. Paired performance measurements are recorded separately in `notes/benchmarks.md`.

- `engine/include/tide/entity.h` and `engine/src/entity.c` (the entity table) are a temporary implementation Claude wrote so generated code could run. The owner takes them over later. Until then Claude maintains them. Generated code depends on the functions declared in `entity.h`.
- `engine/include/tide/devices.h`, `engine/src/devices.c` (input devices) and `engine/include/tide/player.h` (`PlayerID`) are Claude's too, on the same terms. tidec reads the device member lists from `devices.h`, so Tide and C always agree. `tide_devices` has no padding the compiler adds: the input holds one when match code reads devices.
- `engine/include/tide/math.h`, `engine/src/math.c` (vectors, quaternions, matrices and transcendental functions) and `engine/src/noise.c` (Perlin, simplex and cellular noise) are Claude's too, on the same terms. Generated code calls them by the names `tide_<function>_<type>`. The transcendental functions are in-house, computed in double from basic operations; CORE-MATH remains an option to replace them.
- `engine/include/tide/random.h` (random number generators: the state, its seeding and every method) is Claude's too, on the same terms. Generated code calls the `tide_random_*` functions by the names in `compiler/src/builtins.c`, and uses `tide_random` as Tide's `Random`.
- `engine/include/tide/color.h`, `engine/include/tide/draw.h`, `engine/src/draw.c`, `engine/src/draw_coordinates.c`, `engine/src/primitives.c` (colors, the draw list, render targets, screen and world conversions, and the 3D primitives' meshes), `engine/include/tide/mesh.h` and `engine/src/mesh.c` (Tide's mesh draws: lists, and grids as textures) are Claude's too, on the same terms. Generated views call the `tide_draw_*` and `tide_render_target_*` functions (`tide_draw_mesh_shaded` and `tide_draw_grid_texture` for a mesh through a shader), and a game's C does when a view passes it its list.
- `engine/include/tide/attached.h` and `engine/src/attached.c` (what the local world keeps for the match's entities: the tables of local fields) are Claude's too, on the same terms. Generated code calls the `tide_attached_*` functions.
- `engine/include/tide/skinned.h`, `engine/src/skinned.c`, `engine/include/tide/skinned_data.h`, `engine/src/skinned_data.c` and `engine/src/skinned_decode.c` (portable model data and decoding, deterministic animation/pose operations and GPU palette capture) are temporary runtime implementations maintained with the transpiler, on the same terms. The approved generated-C boundary uses ordinary immutable `List<byte>` model data and owned `List<BoneTransform>` pose data; `tide_bone_transform` is the ten-float C alias (40 bytes). Generated adapters call the `tide_skinned_*` functions and either `tide_draw_skinned_atlas` or the immediate `tide_draw_skinned_data`; omitted poses use `tide_draw_skinned_rest_atlas` or `tide_draw_skinned_rest_data` with the same respective output shapes. No platform headers enter simulation code.
  - The raw decoder returns free-compatible temporary canonical bytes through `tide_skinned_data_decode`/`free`; runtime input is self-contained GLB only. Build-time glTF uses the same decoder with an explicit side-buffer callback. Parser/accessor code is shared with the static importer in `tide/internal/gltf_json.h` and `gltf_read.h`, and allocations use the engine's worker-safe page functions. Full portable validation and checked getters never retain pointers beyond the caller's bytes. The pack's model-content preflight runs before replacing a loaded pack; its entry records are three ordinary list handles (`notes/assets-and-files.md`).
  - Sampling starts from authored defaults each time, supports STEP, LINEAR (shortest-path quaternion Slerp) and CUBICSPLINE (Hermite with tangent times the key interval, normalized rotations), and clamps the already selected playback time. Hierarchies resolve parent before child; skin palettes are global node transforms times inverse binds. Blend supports aliased inputs/output, ordinary list COW ownership and snapshots. Invalid checked input leaves an output pose unchanged; lookup failure is -1, missing names are empty text, invalid model matrices are identity. Compatibility compares the complete portable skeleton SHA-256 and node count.
  - **Separate animation exports** (2026-10-10): the generated-C boundary also has `tide_skinned_sample_into(targetData, animationData, clip, seconds, transforms, where)` and `tide_skinned_pose_into(targetData, sourceData, sourceTransforms, transforms, where)`. They bind required nodes by unique exact names, the complete parent chain and numerically equal authored rest TRS; opposite quaternion signs are equivalent. Source node indices and unrelated unanimated branches do not affect binding. Sampling requires only the selected clip's channel targets and ancestors; rebinding a pose requires all its source clips' targets, changed local transforms and their ancestors, so a rest-equal frame still verifies its animated rig. Other target nodes start at target rest. Missing or ambiguous required names, changed parents/rests, invalid data and a wrong transform count leave the destination untouched. An unnamed identity scene wrapper directly below the canonical root is transparent only while source pose values and relevant clip channels remain at rest, including zero cubic tangents; an animated/nonidentity wrapper and deeper unnamed ancestors still fail. Wrappers are skipped rather than copied onto node zero, preserving procedural edits of the canonical root. This accommodates the harmless scene-root node and constant duration channel emitted by FBX conversion without altering imported bytes. Exact skeleton compatibility and Bone fingerprints remain strict, and exact-model sampling delegates the existing sampler without changing its bits. Binding uses scratch name indexes and stages aliased pose copies before resizing, with ordinary persistent list ownership and snapshots. It adds no retargeting or canonical-blob change. Five `skinned_separate_*` runtime tests cover animation-only glTF exports, reordered nodes and extra branches, selected/union clip validation, procedural edits, quaternion signs, rejected bindings, aliased output, captured GPU palettes, exact bits, match snapshots and safe/rejected scene wrappers.
  - `tests/test_skinned.c` pins these behaviors, malformed data and captured GPU streams. `skinned_crossplatform_pose_bits` hashes sampled/blended local TRS and model matrices across all interpolation modes, expected `e5d23f82e5d18f26`; native and WASI must agree. GPU computation is solely visual, before a user vert; the matrices are evaluated deterministically on the CPU.
  - **Packed draw validation** (2026-10-10): `pack_load.c` collects canonical byte-list handles from completely validated skinned records, revalidates them after all load-time sanitizing writes, and computes each distinct blob's exact content hash once. Only bytes wholly inside the original file region qualify; relocated mutable texture/clip references do not. `tide_pack_skinned_version` is runtime support for this current-pack provenance; generated C keeps its existing calls. `tide_draw_skinned_data` uses registered validity/hash while continuing to check the opened model and pose count. Other packed bytes, copied/owned models and runtime Decode values retain full validation and content hashing at each draw. The registry owns no model pointers, is cleared on replacement/unload, and never enters world state, snapshots or deterministic hashes. Sorted header/payload spans with prefix maximum ends let `tide_pack_mesh_updated` invalidate physically aliased proofs in forged packs in logarithmic time for ordinary disjoint blocks. Packed model opening checks the complete list header and claimed payload against the current region before reading them. Four provenance regressions cover these boundaries; all 24 focused native skinned tests passed with the unchanged pose-bit hash after the compatibility improvement below.
  - **Frame-local palette memoization** (2026-10-10): `tide_draw_skinned_data` checks current pack proof before every lookup. A full canonical-content version indexes exact owned copies of node/palette tables, including all parent and inverse-bind dependencies; pose hashes index exact owned current local-TRS bytes, never list handles, pointers or page hashes. A hit also verifies the draw list's retained texture slot identity/version/shape/epoch before marking it used. Mid-frame pack replacement, scratch-handle reuse, procedural edits and heap COW/snapshots cannot reuse a different capture. Entries and byte-arena usage reset before texture compaction, capacities remain for later frames, and free releases them. Unproven/owned/decoded models bypass the memo and keep full validation/hash behavior. Draw-only hierarchy/palette preparation reads validated little-endian parents/inverse binds without revalidating names/rest fields; public Model/Palette retain checked getters. TRS/matrix arithmetic and deterministic state are unchanged. Four tests in `tests/test_skinned_cache.c` exercise the memo's lifecycle, captures, chunked/snapshot ownership, index growth, slot guards, replacement and alias invalidation, alongside ordinary owned fallback.
  - **Deferred atlas capture** (2026-10-10): the approved `tide_draw_skinned_atlas(..., textures[2], info[4])` adds only the per-instance matrix base to the existing counts; the legacy function remains immediate. Only currently proven packed models enter the atlas, and every call checks that provenance before lookup. Capture copies the exact validated node/palette dependencies and current TRS into the draw list's retained arena, with exact comparisons behind the hash indexes. `tide_draw_prepare(const tide_draw_list *, const tide_jobs *)` completes pending palettes from those owned bytes; it never retains a pack/list/heap pointer across the call or passes a handle between threads. Workers atomically claim four-pose chunks, read fixed arena/model arrays and write disjoint preallocated page slices, using their own scratch only for global matrices. A join precedes renderer upload or source append. Small work and NULL jobs run serially; large work uses `run_some` with at most three additional workers, falling back to `run` if needed. Ordinary pages target 4 MiB, an oversized single palette stays within the prior 32 MiB maximum, and page identities come from a separate atomic namespace, stable across resets. Page versions hash ordered captured dependencies; identical frames may retain a version. Pending state is discarded before texture compaction on reset/forget and all retained storage is freed with the list. Append first prepares the source, then copies pages under immutable source-id/content-version identities, including already copied pages. Unproven, invalidated, owned and decoded models retain the complete immediate checked fallback with base zero. The four `skinned_atlas_*` tests in `tests/test_skinned_cache.c` compare exact public palette bits, dispose/overwrite source dependencies before preparation, exercise COW snapshots, page growth/splitting and job contracts, retain two appended source frames, and check invalidation/fallback; the platform test exercises the actual pool. These visual caches never enter world storage, snapshots or deterministic hashes.
  - **Omitted rest capture** (2026-10-11): the two `tide_draw_skinned_rest_*` functions construct authored local TRS only on the first capture for a current immutable-pack proof identity. A frame-local index then reuses the already owned rest capture without filling or hashing TRS and without comparing node/palette tables per instance. Every call obtains `tide_pack_skinned_proof`; its nonzero identity uniquely names one registered block in one load, including identical reloads, while invalidation removes the proof. Entries retain only numeric proof/version and owned-cache indexes. Immediate hits keep the existing texture-slot guard; atlas hits check the current owned page identity, version, shape, epoch and slice bounds, permitting legitimate page growth and completed preparation. A rejected atlas palette slot takes the checked immediate path with base zero and remains rejected on repeated calls; influence-only misses may recapture normally. Immediate and atlas captures can share an entry. Unproven, invalidated, owned and decoded values construct/check rest through the existing explicit preparation path; explicit `Pose` draws and procedural edits keep their existing behavior. Reset/forget clears the rest index before texture compaction, free releases its storage, and append still prepares and copies owned source pixels. Three `skinned_rest_capture_*` tests cover exact rest palette parity, explicit pose edits, repeated hits after preparation/page growth, alternating capture modes, slot corruption/recovery, reset/forget, append after unload, identical/changed reloads and index growth, unchanged-byte proof invalidation, malformed data and owned fallback.
  - **Rest initialization** (2026-10-10): `tide_skinned_rest` reuses current immutable-pack node validity, retaining complete node checks for owned/unregistered or invalidated models. All three private `fill_rest` callers establish node validity before resizing/writing; it decodes only the ten stored little-endian TRS words rather than repeating parent/name/finite checks. Float bits, output ownership and failure-before-output behavior are unchanged. Proof invalidation forces the checked fallback before rest output is touched. `skinned_rest_rechecks_invalidated_proof_and_keeps_owned_outputs` pins exact packed/raw/owned rest equality, then independently corrupts parent/name/rest position/rotation/scale through a writable packed alias and ordinary copies; all reject while sampled scratch/heap outputs and the heap hash stay unchanged.
  - **Owned model compatibility** (2026-10-10): `tide_skinned_compatible` reads a private header-only identity instead of flattening a whole owned model. Ordinary byte lists keep at least the complete 120-byte model header in their first contiguous run, pinned by a compile-time chunk-capacity assertion. The existing `tide_skinned_data_open` checks only that header and all declared table spans against the logical total size; its partial view stays local and supplies only node count and the complete 32-byte skeleton fingerprint, never table getters or retained pointers. Packed handles use the checked header/payload path above. All prior header rejection and empty-input behavior remains, including compatibility's existing header-only treatment of body data. This adds no cache or API and leaves fingerprint, sampling and matrix arithmetic unchanged. `skinned_compatibility_reads_owned_chunked_headers_and_keeps_snapshots` uses a half-megabyte chunked model with nodes at its distant end, checks repeated read-only comparisons and unchanged heap/scratch state, all header/identity rejection cases, header-valid malformed body behavior, and a preserved COW snapshot.
  - **Verified model sampling** (2026-10-10): `tide_skinned_sample` reuses the same current-pack proof for registered loaded models, skipping repeated hierarchy validation and complete selected-channel key scans. It still checks the selected clip getter. Since 2026-10-11, `tide_pack_skinned_sample_proof(data, nonnegativeFirstTimes)` obtains current provenance plus an optional stronger fact in one registry lookup: after full validation and all sanitizing writes, the loader checks every channel's first key once for `time >= 0`, including negative zero and channels outside any clip. This is vacuously true for zero channels. When true, sampling skips the selected-channel preflight entirely; proven finite strictly increasing keys establish all later times. When false, the ordinary proof remains usable and sampling checks only the selected clip's first times, so an unrelated negative-first channel does not reject a valid clip. Full portable validation permits first times above -1, so the stronger fact never replaces ordinary validity. Replacement recomputes it; unload or overlapping writes remove its proof, and a failed query leaves the optional output untouched. Owned/unregistered/decoded models retain the complete checks. Standalone `Animations` assets stored as ordinary packed bytes do too, unless deduplication shares their bytes with a separately verified model block. After this validation, private little-endian readers fetch only times when binary search is needed and decode only the selected endpoint values/tangents, preserving stored float bits and positive-zero fourth lanes for three-component channels. Both sampler callers prove these spans and finite values before writing any output; the model remains open throughout and no unchecked reader or pointer escapes. Scratch pose initialization and the main sampler retain a contiguous transform pointer only after sizing and only while no resize is possible; selection uses the actual list tag, while heap/chunked outputs retain the existing checked COW path. Required binary searches, interpolation expressions and both rotation normalizations stay unchanged; binding and public palette boundaries are unchanged. `skinned_packed_samples_match_owned_bits_and_reject_negative_first_keys` compares exact TRS bits across every interpolation mode and several times, rejects negative-first clips without altering output, leaves unrelated valid clips usable, and covers same-handle reloads that change the stronger flag, negative zero, optional outputs and owned-model exclusion. `skinned_packed_sampling_rechecks_later_keys_after_proof_invalidation` changes only a later key through a forged geometry alias, verifies invalidation, and requires the full fallback scan to reject it without altering the output pose. Three `skinned_sampling_*` regressions add nonuniform five-key searches, exact STEP keys and cubic midpoints, endpoints/signed zeros/nonfinite clocks across raw/packed/owned models, malformed later times/values/tangents/spans with unchanged scratch/heap outputs, and correctly sized chunked heap outputs requested through the scratch path while preserving snapshots. Final validation of sampling, rest initialization, palette memoization and transient GPU texture reuse passes all 36 focused native skinned tests and the complete Windows debug suites: 2,061 native and 2,032 web checks, including unchanged deterministic pose hashes and OpenGL/WebGL/WebGPU pixels.
  - **Per-call sampling intervals and transient capture reads** (2026-10-10): current immutable-pack proof allows the main sampler to decode its six channel descriptor words and inspect only the first key time during preflight. All selected channels pass before output is resized or written; owned/unregistered/invalidated models still perform complete descriptor and key checks. The second pass reads only already validated descriptors. Four stack-local interval slots reuse only the original binary-search result when key count and complete little-endian time bytes agree exactly, including physically separate equal arrays. Only tracks of at most 32 keys qualify, bounding comparisons at 128 bytes; collisions replace a slot, longer tracks keep the original search, and no cache state or pointer survives the call. Values, modes and paths are sampled normally with the existing expressions and both rotation normalizations; separate-rig sampling retains its uncached search. `skinned_sampling_intervals_match_isolated_search_bits` compares combined clips against isolated one-channel searches across mixed paths/modes, equal/different time arrays, count collisions, long and one-key tracks, exact keys/endpoints/nonfinite clocks and raw/packed/owned data plus persistent outputs. Negative-first coverage includes a later selected channel under valid proof, accepted signed zero and unchanged scratch/heap output and heap hashes. `skinned_sampling_rechecks_later_channel_fields_after_proof_invalidation` independently corrupts all six fields of a later channel through a packed alias and requires checked rejection before output mutation. Private draw pose-index hashes use the existing four-chain `tide_hash_wide` (stripes since 2026-10-20, ecs.md), retaining model-index mixing and exact-byte collision checks; this changes no deterministic hash or palette arithmetic. Synchronous capture borrows only actual contiguous scratch TRS, while heap/chunked inputs retain a contiguous staging copy. Retained cache/atlas arenas always copy TRS before returning, so existing disposal, scratch-reuse, COW/snapshot and deferred-preparation tests continue to pin capture lifetime.
- `engine/include/tide/events.h` and `engine/src/events.c` (what a tick records of the match's events for local handlers, and what a client remembers of the ticks it ran, so each is heard once) are Claude's too, on the same terms. Generated code calls the `tide_event_log_*` functions, and sessions the `tide_hearing_*` ones.
- `engine/include/tide/debug.h` and `engine/src/debug.c` (`Debug.Log` and `Debug.Assert`) are Claude's too, on the same terms. Generated code calls `tide_debug_log` and `tide_debug_assert`.
- `engine/include/tide/page.h`, `engine/src/page.c` (pages that worlds share with their snapshots), `engine/include/tide/table.h`, `engine/src/table.c` (archetype storage and the command queue), `engine/include/tide/jobs.h`, `engine/src/jobs.c` (running a tick's systems on threads), `engine/include/tide/heap.h`, `engine/src/heap.c` (a world's heap), `engine/include/tide/text.h`, `engine/src/text.c` (text, its formatting and the scratch area) and `engine/include/tide/list.h`, `engine/src/list.c` (lists), `engine/include/tide/map.h`, `engine/src/map.c` (maps and sets: two lists, the entries in order and an index by hash) and `engine/include/tide/grid.h`, `engine/src/grid.c` (grids, and loops over their cells) are Claude's too, on the same terms. Generated code calls `tide_run_systems`, `tide_recording`, `tide_new_entity`, `tide_settled`, `tide_parallel_for`, `tide_pages_hash`, the `tide_table_*`, `tide_queue_*`, `tide_heap_*`, `tide_str_*`, `tide_text_*`, `tide_scratch_*`, `tide_list_*`, `tide_map_*`, `tide_grid_*` and `tide_par_*` functions.
- `engine/include/tide/rows.h` and `engine/src/rows.c` (what a system written in C is handed: a run of rows, and its components as arrays) are Claude's too, on the same terms. Generated code calls `tide_rows_begin` and `tide_rows_end`, and a game's C the rest.
- `engine/include/tide/net.h`, `engine/src/net.c` (addresses, transports, the loopback network, packing bytes and bits, and hashes), `engine/include/tide/delta.h`, `engine/src/delta.c` (worlds as deltas: what changed from a base, page by page), `engine/include/tide/session.h`, `engine/src/session.c` (servers, clients and sessions: the netcode) and `platform/src/udp.c` (the UDP transport) are Claude's too, on the same terms, as a first version the owner takes over. Generated code calls the `tide_bits_*`, `tide_write_*`, `tide_hash*`, `tide_delta_*` and `tide_needs_*` functions (and each part's `*_pack_delta`, `*_unpack_delta`, `*_hash_pages` and `*_need_pages`) and fills a `tide_game`.
- `engine/include/tide/gui.h` and `engine/src/gui.c` (the GUI's widgets, controls, theme, layout, focus, typing and claims) are Claude's too, on the same terms. Generated views and the functions they call call the `tide_gui_*` functions, with IDs tidec derives from where each widget is called and the entity the view runs for, and `tide_frame` calls `tide_gui_view` before each view. Tide's `Anchor` has `tide_anchor`'s values.
- `engine/include/tide/profile.h`, `engine/src/profile.c` (what ticks and frames say they took), `engine/include/tide/profiler.h`, `engine/src/profiler.c` (the profiler's panel) and `engine/include/tide/stats.h`, `engine/src/stats.c` (the stats' drawer) are Claude's too, on the same terms. Generated code calls `tide_profile_now`, `tide_profile_tick` and `tide_profile_view`.
- `engine/include/tide/layout.h` (a game's data layout, which tidec describes for hot reloading) is Claude's too, on the same terms.
- `engine/include/tide/sha256.h` and `engine/src/sha256.c` (SHA-256, for sessions' cookie digests and the desktop WebRTC's crypto, which uses it through `rtc.h`) are Claude's too, on the same terms.
- `platform/` (the platform layer), `demo/`, `bench/`, `relay/` and `packages/` are Claude's too, on the same terms.
  - What the platform layer does with input devices and pixels is in `notes/input-devices.md`.

Map iteration calls `tide_map_next` with a last insertion order and an index hint, both ordinary locals that tasks save across waits. Reading a cursor changes no map and allocates nothing. Entry headers are signed 64-bit orders: positive live, negative removed, with the same magnitude retained until compaction. `tide_map` keeps the last order across `Clear`; copies and assignment keep it too. Its explicit reserved word stays zero. `tests/test_map.c` checks compaction after the cursor's entry is removed, removals without compaction, clear/copy/assignment, orders beyond 32 bits and at the signed limit, and unchanged heap/map/scratch state during traversal.

Levels of detail (2026-10-11, agreed with the owner as the feature was: `docs/spec.md`, Levels of detail): generated views call the new settings' functions, `tide_draw_set_lod`, `tide_draw_unset_lod`, `tide_draw_push_lod`, `tide_draw_push_no_lod`, `tide_draw_set_lod_camera_3d`, `tide_draw_set_lod_camera_matrices`, `tide_draw_unset_lod_camera`, `tide_draw_push_lod_camera_3d`, `tide_draw_push_lod_camera_matrices` and `tide_draw_push_no_lod_camera`, and nothing else new: a mesh draw is the call it was, and finds its levels by its indices' list (`tide_pack_list_levels`, in `engine/src/mesh.c`). The standard host calls `tide_draw_screen_size` each frame. `tide_draw_list`, `tide_draw_place` and `tide_draw_mesh_data` grew, so hosts, libraries and generated games rebuild together. The whole of it is in `notes/assets-and-files.md`, Levels of detail.

Static and skinned mesh vertex defaults finish with `tide_pack_mesh_updated(vertices, vertexSize)` (`tide/pack.h`, `engine/src/pack_load.c`). Generated asset loading calls it after filling a mesh's defaults and before drawing; it refreshes the stored nonzero version from final vertex bytes once per fill. The renderer keeps its constant-time packed-list version lookup. Repeated loads and shared blocks with the same final values retain the same version.

Generated skinned loading also validates each imported model's typed geometry and canonical bytes, and the asset content reached by vertex defaults, before `tide_pack_load`, preserving the loaded pack, globals and retained draw/owned model values on rejection. The fingerprint walker is private compiler support reused in the generated preflight, with no new exported engine API; selected names are resolved together in one directory pass per batch, and temporary lookup storage is released before returning. Ordinary static-mesh defaults and unrelated visual assets retain their reload behavior. `notes/assets-and-files.md` records the closure and compatibility regressions, including runtime Decode-only defaults.

Clip preparation in `engine/src/audio.c` queues stable indices into its decoded cache. Gathering may reallocate that cache; it finishes before jobs resolve the indices and decode into the final allocation. Decoded WAV samples and opened Vorbis packets own their bytes until `tide_clips_free`, so pack replacement or unload leaves active voices' data valid. `tests/test_audio.c` checks preparation across cache growth and replacement, aliases, repeated preparation and retained samples.

## Approved physics completion hook (2026-10-11)

The owner explicitly approved the internal tick-completion hook before generated code depended on it. Generated ticks now use `tide_rows_finish_tick`, and physics C registers through `tide_rows_after_tick`; both live in the existing agent-maintained rows runtime. The callback is after queued changes, with final readonly entity/Snap metadata, deterministic slots/order and stable singleton contexts. This keeps the agreed Tide API unchanged. The compiler also uses existing scratch/grid access helpers to build private readonly interpolation snapshots; no additional runtime function is introduced there.

## T? fields (2026-10-11)

Generated code calls no new runtime function for fields declared `T?`, and no engine header or structure changed: what a field is kept as, its conversion to and from the `T?` code reads, its text and list ownership, its packing and its JSON are generated, over `tide_text_view`, `tide_text_temp`, `tide_text_release`, the generated list and map helpers and the `tide_json_*` and `tide_bits_*` functions there were (`notes/language.md`, T? fields).
