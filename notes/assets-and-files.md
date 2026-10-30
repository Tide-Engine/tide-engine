# Assets and local files

## Humanoid pose retargeting (2026-10-30)

The owner approved a standalone runtime path, without a tide-motion dependency:
`HumanoidRig.Auto(referencePose)`, editable `rig[HumanoidBone]`,
`rig.GetStatus(role)`, `RetargetMap.Create(actualAnimation.skeleton, sourceRig,
targetRig)` failing with `RetargetError`, and `pose.Retarget(sampledPose, map)`
returning a bool. A rig keeps its reference pose and role assignments. Status is
Missing, Automatic, Uncertain or Manual; uncertain required roles prevent map
creation, and runtime diagnostics identify the side and role to assign. Manual
assignments can complete a partial automatic result before creating a map.
Assigning a default Bone explicitly unmaps the role. Bone provenance and the
reference pose's skeleton identity are checked; replacing a rig's reference
with an incompatible skeleton cannot silently reuse its old indices.

Automatic mapping combines common Unity/Mixamo, UE4 and Synty anatomical names,
namespaces, side markers and hierarchy. Explicit geometry/mesh name tokens,
IK, twist and helper names are excluded from semantic candidates. Clavicle plus
Shoulder recognizes the Synty upper arm, whereas Mixamo Shoulder is the clavicle.
Ball followed by Toes selects the proximal toe joint. Unsided fingers inherit
their mapped hand, including Synty's right-hand numeric suffix; a single generic
Finger_01/02/03 chain represents a mitten's middle finger, while competing or
branched generic chains remain uncertain. A known bilateral pair supplies a
reference-geometry side axis for anatomically named unsided limbs. A unique
intermediate non-helper node can fill a missing elbow or knee. This is not
recognition of arbitrary unnamed skeletons: ambiguity is exposed for manual
assignment. Hips, head, upper/lower arms and legs, hands and feet are required;
spine, chest, upper chest, neck, clavicles, toes and fingers are optional.

Map creation binds the actual animation export separately from the calibration
pose, by unique exact node names and the shared named ancestor chains. Reordered
nodes, extra uniform armature wrappers and different authored node defaults are
supported. The reference must describe that same source rig in its intended
calibration pose; an FBX whose node defaults are idle can use a sampled T-pose
clip as its explicit reference. Optional source roles absent or ambiguous in
the actual export are omitted from the prepared mapping, without modifying the
rig. Required roles must bind uniquely. Exporter axis/unit conversion must
already place the actual source and its reference in the same model space.

Transfer uses reference-calibrated global rotation deltas, aligned body facing
and outgoing limb directions. It handles T/A reference differences and keeps
target local translations and scales, so target segment lengths survive. Two
finger rays can determine palm roll; leaves or hands without that secondary
frame retain reference roll. Extra torso nodes interpolate source-chain global
rotation deltas by reference arc length. Limb helpers receive only axial twist,
never interpolation of elbow/knee swing; common sibling twist/roll bones receive
half the distal axial delta. Off-axis serial helpers follow the proximal joint.
`spineDistribution` and `twistDistribution` default to 1 and clamp to [0,1].
The latter settings control intermediate helpers; mapped semantic endpoints
still follow their source roles.

`rootScale` starts at target/source average leg length, `rootOffset` at zero,
and `headingDegrees` at zero. Hips travel includes source ancestor movement,
is scaled in model space and turned by the calibrated facing plus requested
heading about target up. Root offset uses target model space; rootScale zero
removes hips travel. There is no separate root-motion extraction/controller.
Reference-pose edits are calibration adjustments; geometric alignment can
cancel a swing edit. For an artistic per-bone output offset, edit the resulting
Pose after Retarget. Retarget a full target rig, then use existing compatible
binding to draw sparse modular pieces; incomplete body rigs do not guess a leg
ratio. Contact IK, ground locking, weapon/hand constraints and non-root position
animation transfer are outside this pass. Nonuniform, reflected, zero or
animated source scale is refused; positive uniform static unit wrappers work.

Preparation owns a pointer-free byte plan and the API keeps ordinary list-backed
value state, including snapshots. Apply checks immutable skeleton headers and
walks source/target nodes once, without scanning mesh or animation payloads each
frame. It stages results before mutation, allows source/output pose aliasing,
and leaves the output unchanged on invalid identity, counts, options or nonfinite
transforms. The runtime boundary is recorded in runtime-ownership.md. Original
synthetic C fixtures cover naming/ambiguity/manual assignments, T/A and roll,
root and facing calibration, different spine counts, axial helpers, independent
export defaults/order/unit wrappers, missing optional export bones, atomic
failure and snapshots, and deterministic pose bits. Public compiler fixtures
exercise the API separately; licensed local meshes are not repository fixtures.

Private validation used the owner's UE4 Kubold source with the sampled frame-0
T-pose calibration and Synty POLYGON target. Five clips (idle, right cut, top
cut, thrust and death), 593 sampled frames, all applied successfully. Every
required role and the 18 supported target finger roles mapped automatically,
without ambiguity. Independent model-space segment comparisons found a worst
core limb-direction difference of 0.16902 degrees; retained branch geometry
still differs (clavicle attachment rays up to 35.32 degrees, wrist-to-thumb-root
36.61, wrist-to-index-root 20.22). These are not contact or weapon-alignment
guarantees: differing attachment offsets and hand proportions remain target
geometry. Matching-camera 1600x1200 source/target renders for all five clips were
visually inspected, without an observed collapse or limb flip; hand contacts
still need the game's constraints/IK. Native and WASM private probes each
applied all 593 frames with zero failures; their 256 status, scale, direction
and completion lines matched at the recorded precision. All twelve new C
fixtures passed on native ASan and WASM (the retarget filter also runs one
existing test, 13/13 total); the 13-frame synthetic pose hash matched exactly,
`a669d398066dafa5`, and is pinned in the test. The two public API e2e cases also
passed on both platforms. Existing skinned CPU coverage passed 62/62 on each.

After merging the incoming Pose.Sample cache and codegen changes, overlap
validation rebuilt the affected targets. Native ASan and WASM each passed the
retarget filter (13/13), expanded skinned CPU coverage (65/65), and all seven
affected e2e CTests, including retargeting and net_alone_seen. Both private
probes again applied all 593 frames successfully; all 256 measurement lines
were identical before/after the merge and between native and WASM.

## Kept sample plans and model facts (2026-10-30)

bannerlord-duel measured `Pose.Sample` of an FBX clip onto its Synty
character at 12 µs a call: before sampling anything, each call checked both
node tables and every key of the clip (an ordinary packed `Animation` has no
load-time proof), built two name tables and bound the rigs. Timings:
notes/benchmarks.md, Pose.Sample, indexing and Model a call at a time.

`tide_skinned_sample` and `tide_skinned_sample_into` (`engine/src/skinned.c`)
now make a sample plan: whether the clip binds, each bound channel's
descriptor and output node (or folded slot), the pose before any channel
(target rest with the required source defaults over it), folded nodes'
starting locals and the folds to finish with. It's made by the same code
that made those decisions per call before: `plan_own` is the old
`tide_skinned_sample` preflight (the pack proofs included), `plan_bound` the
separate binding (`require_clip`, `bind_nodes`, `clip_folds_uniform`), and a
refusal still reports once through `report_binding`. `plan_apply` then
sizes the output, copies the starting pose, samples every channel in clip
order and applies the folds. Its arithmetic is the samplers' own: the same
search result, span, `t`, lerp, slerp, Hermite terms and the two rotation
normalizations, in the same order. What changed is only where they happen:
channels whose key times are the same bytes share one search and one
(span, t) (`plan_groups`, kept plans only); keys decode as fixed three or
four lanes, a zero in the fourth as before; and every channel's value is
made before the rotations are normalized in a loop of their own, then
written in channel order. `linear_rotation` is `tide_slerp_q` with its
nlerp branch's normalizing left to that loop (the wide-angle branch still
calls `tide_slerp_q`).

A plan is kept when both models' bytes have a key that can't name other
bytes:

- A pack block within the file's bytes (`tide_pack_file_span`), keyed by its
  offset and `tide_pack_generation()`, a new runtime function (not called by
  generated code) that changes on every load, unload and
  `tide_pack_mesh_updated`. Generated code never writes the pack; the
  runtime's writes are the load's and geometry defaults', each followed by
  `tide_pack_mesh_updated`, so a forged pack whose geometry write overlaps a
  model plans again, as its proof is dropped.
- An asset-held list (`tide_asset_list`: a component's copy of an asset),
  keyed by its SHA-256 id, which names its bytes.

Owned lists (scratch, decoded, edited) plan for each call and free the plan,
as they checked for each call before. The key has the mode (own skeleton or
into another), the clip index and both models' keys; a kept plan also
checks both views' sizes on use. Refusals are kept too. 64 sets of 4 plans,
least recently used replaced, under a spinlock, shared by every thread; a
caller holds a reference while applying, so eviction by another thread frees
nothing in use. Plans are malloc'd (a 55-node, 156-channel plan is about
7 KB) and live until evicted. Pose bits, refusals, output ownership (scratch
and world outputs, heap/chunked through the list cache), the
untouched-output-on-refusal rule and `tide_skinned_pose_into` are unchanged.

The same keys keep a model's facts on each thread (`known_model`: node count,
skeleton hash and fingerprint, size, node table offset, and node validity
once asked; 16 slots, the last hit checked first), which
`tide_skinned_fingerprint`, `tide_skinned_bone_count`,
`tide_skinned_compatible` and `tide_skinned_model` read rather than opening
the model: a Pose's indexing and Model check its identity on every use
(`tide_skin_pose_has`). Names looked up by `tide_skinned_find_bone`,
`_find_animation` and `_find_shape` (a rig's members, each read) are kept
too, up to 40 bytes, 64 slots, matched by the model's key and the name's
bytes. Keyless bytes, and a failed open, take the old paths. `Model`
multiplies only the bone's ancestors, root down, with the same TRS and
products as the all-node pass (`chain_model`); bits are unchanged.

Tests (`tests/test_skinned.c`): `skinned_known_models_answer_as_their_bytes_do`
(pack- and asset-held bytes give the same names, counts, fingerprints,
compatibility and every bone's Model, bit for bit against the all-node
product, as owned bytes, before and after a reload at the same offsets
renaming a bone and changing the skeleton; it fails without the generation
in the key); `skinned_kept_sample_plans_match_plans_made_for_one_call`
(pack- and asset-held bytes against owned copies: the exact rig, separate
bindings both ways, an armature folded in and out, refused bindings and
clips, scratch and world outputs, twice so the second pass reads kept
plans); `skinned_kept_sample_plans_follow_reloaded_packs` (another pack at
the same offsets with a changed key and rest plans again);
`skinned_kept_sample_plans_survive_eviction` (2,400 refused keys among valid
samples). Leaving the generation out of the key fails the reload test and
the two existing proof-invalidation tests. `bench_pose --quick` checks kept
against single-call plans over a 192-channel clip. The pinned cross-platform
pose bits (`skinned_crossplatform_pose_bits`) are unchanged.

## FBX keys closer than a float are one key (2026-10-29, bug)

Reported by the owner from the duel (`bannerlord-duel`'s `notes/gaps.md`):
Mixamo clips downloaded onto a Synty character, four of the five slashes of
its Great Sword Pack and three of the Pro Sword and Shield Pack's attacks,
were refused: "has FBX animation keys too close together for float
timestamps". The owner: "i dont like when the engine is in the way". Their
last key is 2e-8 s before the take's end (1.2666666454 s of a 38-frame take
ending at 1.2666666667: the tick count's rounding), where `fbx_channel` adds
its end boundary sample; as floats both are 1.26666665 s, and the importer
refused any key not after the one before. Now keys whose float times aren't
after the previous key's are one key, at the earlier's time with the later's
value (the end's evaluated value there). Only files that were refused import
differently: wherever every float time rose before, the output is the same
bytes. All seven of the owner's files then build.

Test: `fbx_keys_closer_than_a_float_are_one_key` (`animation/close_*.fbx`
from `fbx_make.mjs`: Bone0's last key one tick before a 38-frame take's end;
the channel's times rise and end at the take's end, with the last key's
value; fails before this change, refused). Validation: `tidec_fbx_files`,
`tidec_e2e_fbx_assets`, `tidec_e2e_animations`, `tidec_e2e_blend_shapes`,
`tidec_animations_identity` and the FBX and animation diagnostics, native
debug (18 tests). The importer runs only in native tidec, so web was not
run.

## FBX Euler keys unwrapped before baking (2026-10-27, bug)

Follow-up the same day: HeavyStab01 still swung `spine_01` about 45 degrees
per 1/240 s near 0.34 s. Its exporter switched to the rotation's other Euler
triple between source keys, (179.99, 180.30, -183.12) then (0.00, 0.10,
-2.96), which is (x + 180, 180 - y, z + 180) and the same rotation for every
Tait-Bryan order; per-axis unwrapping can't see it, and the bake's in-between
key landed half a turn away. Hemisphere continuity or shortest-path sampling
can't help either: both neighbouring keys agree; the inserted key itself is a
different rotation. `fbx_unwrap_rotations` is now the full Euler filter: where
a rotation's three curves have keys at the same times, each key takes whichever
of its two triples (each unwrapped by whole turns) is nearer the previous key
by summed angle, negating the middle axis's tangent slopes when the twin is
taken; curves keyed apart keep per-axis unwrapping. On the real clips the
largest adjacent-key change is then 28 degrees (LightCombo01A, an IK hand) and
50 degrees (HeavyStab01's `elbowAttach_l` at 0.88 s, where Y passes through
the gimbal from 79.5 to 139.9 degrees between source keys: authored motion,
neither triple is near). Test:
`fbx_euler_keys_in_their_other_triple_bake_as_the_same_rotation`
(`animation/twin_*.fbx`, (0, 10, 0) then (180, 170, 180) over one 30 fps frame;
every baked key is 10 degrees about y; fails before this change). No existing
expectation or pinned hash changed (99 affected native tests).

Reported by the owner: Synty's `A_MOD_SWD_Attack_LightCombo01A_Neut.fbx`
spun `spine_01` a full turn in 33 ms (keys at 0.0333/0.05/0.0667 s of
(-0.9945, 0.10, ..), (0.0986, 0.9948, ..), (0.9949, -0.09, ..)), also
`spine_02` at 0.20-0.37 s and HeavyStab01 at 0.34 s. The brief suspected a
q to -q sign flip interpolated the long way; that wasn't it. `tide_slerp_q`
already takes the shorter path for LINEAR keys, and the converter already
makes consecutive baked quaternions hemisphere-continuous. The middle key
was a 60 Hz bake key half a rotation from both neighbours: the clip's Euler
curves are keyed at 30 fps, wrapped (Z -182.72 then 178.20, one degree
apart), and `minimum_sample_rate` 60 has ufbx evaluate the curve between
them, sweeping through 0. `fbx_unwrap_rotations` (`fbx_files.c`) now moves
each `Lcl Rotation` curve key by whole turns to within 180 degrees of the one
before, before baking (Maya's Euler filter); the rotation at every key is
unchanged. After it, the clip's largest change between adjacent keys is about
28 degrees per 1/60 s (an IK hand mid-swing). A genuine full turn keyed
between two adjacent keys is now read as the short way; authored spins keep
their intermediate keys. No glTF decoder change: its LINEAR keys already
sample the short way, and rewriting their signs would only change bytes.

Tests: `fbx_wrapped_euler_rotation_keys_bake_the_short_way` (ASCII/binary
`animation/wrapped_*.fbx` from `fbx_make.mjs`: Bone0 179 to -179 degrees over
one 30 fps frame; every baked key within a degree of the half turn; fails
without the fix) and `skinned_linear_rotation_between_opposite_signs_takes_the_short_way`
(a glTF clip q to -q samples q throughout). No existing expectation or pinned
hash changed.


## Choosing which blend shapes an asset imports (2026-10-27)

The owner chose an asset attribute (verbatim in docs/spec.md): Sidekick heads
carry ~50 facial shapes, which doubled the character tool's pack to 235 MB.
`[BlendShapes("masculineFeminine", "default*")]` before a `SkinnedMesh<V>`
asset (single, list, map, glob) keeps only matching shapes.

Built: `check_blend_shapes_attribute` (`check.c`, the asset attribute loop
beside `[LOD]`) records constant-text patterns (escapes decoded as `[Name]`'s)
on the declaration; misuse on another asset kind, a non-text pattern, named
arguments, a repeat, or the attribute on a non-asset are diagnosed, with
BlendShapes among the unknown-attribute suggestions. After resolving,
`keep_blend_shapes` replaces each of the declaration's items' canonical bytes
(per-declaration `asset_match` copies; the shared file cache, the typed
geometry and its hash are untouched, since dropped shapes' defaults are
already in the vertices) with `tide_skinned_data_keep_shapes`
(`skinned_data.h`, `skinned_decode.c`, build-time only): it reads the
validated model back into the decoder's tables and runs the decoder's own
`encode`, so keeping everything is byte-identical and keeping nothing is the
version 1 model a file without targets decodes to. Packing, identity, LOD keys
and fingerprints all see the filtered bytes. Matching is `*` = any run
(including none), else exact; `[BlendShapes()]` keeps none; a pattern matching
nothing is fine; an unnamed shape is only kept by a pattern matching the empty
name. The editor's catalog metadata is unaffected (shapes have no dot members).

Tests: `skinned_shapes_kept_by_name_rewrite_the_model_as_the_decoder_would`
(keep all = same bytes, keep none = the no-target decode byte for byte, keep
one, invalid input refused); `tidec_e2e_blend_shapes` gains five declarations
(FBX `jaw*`, empty, a list and a map over `fbx/shapes/shapes_*.fbx` with a
pattern matching nothing, a glTF) checked through counts, names, baked
defaults and Shaped, plus a pack scan finding exactly one version 1 model
(the empty one); three `tidec_error_blend_shapes_attribute_*` fixtures;
`lsp_levels_of_detail` checks completion, hover, misuse and the misspelling
suggestion.


## Clips skip nodes the model lacks (2026-10-27)

The owner chose lenient separate-rig binding for modular characters (verbatim
in docs/spec.md, Separate-file binding): Synty Sidekick clips animate
`ik_settings`, `ik_foot_root` and the whole body, while a part file has some
of the chain (a hair part root..head) or extra chains (a cape's `abac_dyn_*`).
`bind_nodes` (`engine/src/skinned.c`) now maps a required source node with no
same-named target node to `BIND_SKIPPED`: its channels are left out of
`tide_skinned_sample_into`, its locals out of `tide_skinned_pose_into` (and so
out of the generated draw path, which binds through `pose_into`). A top-level
source node with no target name and no bound child, formerly "missing" after
the fold pass, is skipped too; one above bound bones still folds. Chosen rule
for a present node below an absent one: its source parent must be bound and
map to its target parent, as before, so a node below a skipped node is refused
("is below X in it, which the model hasn't got") rather than re-parented; a
whole absent subtree is skipped. Still refused, with the console reason:
ambiguous required names on either side, unnamed required nodes, different
parents, uneven fold scale, and a clip or pose whose named required nodes are
all absent ("the model has none of the nodes it moves"), which keeps a wholly
different rig a reported failure. Exact-skeleton sampling, canonical bytes,
generated C and the pose-bit pin are unchanged.

Tests: `skinned_separate_bindings_skip_nodes_the_model_lacks` (extra animated
top-level, chained and leaf nodes give bit-identical sample_into and
pose_into results to the plain export, and the pose draws; a target with only
the arm, for the moving clip and the hand-only clip; a node below an absent
elbow refused by both routes with the pose unchanged; another rig and a
duplicate name refused). `skinned_separate_poses_bind_procedural_edits_...`
now expects an edited node the model lacks to bind and move nothing (it
formerly expected refusal). All 60 skinned runtime tests pass.


## Blend shapes: imported and baked on the CPU (2026-10-27)

The owner chose "Import + bake" (verbatim in `docs/spec.md`, Skinned meshes,
Decided) for an internal character tool built from Synty's Sidekick packs,
whose parts are skinned FBX with body shapes (`masculineFeminine`,
`defaultHeavy`...) and ~50 ARKit facial shapes. GPU per-draw weights were not
chosen. This supersedes the "morph targets are refused" statements in the
skinned entries below; static `Mesh<V>` imports still take rest geometry.

**Canonical format.** `tide/skinned_data.h` version 2 adds two tables after a
136-byte header (words 30..33): shapes (20 bytes: name offset/length in the
string section, first delta, delta count, default weight) and deltas (40
bytes: vertex, position, normal and tangent xyz). A model without shapes is
still written as version 1 with its 120-byte header, byte for byte what it
was: no existing pack, Simulation ID, pinned hash or hand-built test blob
changed. Version 2 requires at least one shape, so each model has one form.
`tide_skinned_data_shape`/`_shape_delta` check their records;
`tide_skinned_data_validate` also requires shapes' ranges to follow each
other in order and cover the delta table, and each shape's vertices to
increase strictly. The vertices are the authored geometry with every shape at
its file default (so a nonzero glTF `weights` or FBX DeformPercent is the
model's rest); the skeleton SHA-256 is unchanged. The chunk-capacity
assertion in `skinned.c` now covers the 136-byte header, so owned byte lists
still hold the whole header in their first run.

**Decoder** (`skinned_decode.c`, shared by build-time imports and runtime
`Decode`). A primitive's `targets` (POSITION, NORMAL, TANGENT; dense or
sparse accessors through the existing `floats`, counted against
`TIDE_SKINNED_MAX_READS`) are read one target at a time, x-reflected like the
part's own values, added into the part's vertices at the default weight in
target order, and kept as deltas only where a vertex moves. A mesh's targets
map to model-level shapes once per mesh (`mesh_shapes`): a name from
`extras.targetNames` is one shape across every mesh (a sorted name index, one
binary search each); same-named targets must share their `weights` default;
an unnamed target is one shape for its mesh, the same for every part and every
node placing it. Refused: a name twice in one mesh, differing defaults, a
`weights` count unlike the targets', parts of one mesh with different target
counts, a target that isn't an object, wrong accessor counts. NORMAL targets
are ignored on a part without normals (the importer's own flat normals don't
follow shapes; such a part's deltas are copied to each triangle-corner vertex
of their source) and TANGENT targets on a part without tangents. Animation
channels targeting `weights` are now skipped instead of refusing the file.
New limits, one `#define` each: `TIDE_SKINNED_MAX_SHAPES` (4,096) and
`TIDE_SKINNED_MAX_SHAPE_DELTAS` (2,097,152, 80 MiB); the "every limit" peak is
now about 600 MiB by arithmetic, not remeasured.

**FBX** (`fbx_files.c`). Skinned conversion no longer refuses blend
deformers. Each channel of every blend deformer is a glTF target of every
material part: POSITION from the shape's offsets at each part vertex's FBX
vertex (`ufbx_get_blend_shape_offset_index`), NORMAL when the mesh has normals
and the shape normal offsets, both as sparse accessors over zeros (no view and
no sparse block where a shape moves nothing in that part). The mesh says
`weights` (the channel's DeformPercent / 100) and `extras.targetNames`: the
keyframe shape's name, else the channel's. That is `masculineFeminine` for a
Maya-style channel `TORSBlends.masculineFeminine`, so one name drives every
part. A channel with more than one keyframe, or one whose target weight isn't
100%, is refused before any output with export guidance (Tide's shapes are
linear). Blender's FullWeights-less shapes and offset weights are not applied
specially. Static conversion emits no targets, so static FBX packs are
byte-identical.

**Language** (`skinned_types.inc`, `skinned_ops.inc`, `skinned_codegen.inc`,
`skinned_adapters.inc`, `skinned_language.h`). `model.blendShapes` (catalog
type `BlendShapes`, `count`/`TryGetAt`/`TryGetByName`, no checked dot members:
enumeration and exact-name lookup only, which keeps editor metadata reading
unchanged), `BlendShape` (hidden data and index; `name`, `weight`), and
`model.Shaped(List<float>)` returning the receiver's own type, so inferred
and contracted catalogs survive. The names: `blendShapes`/`BlendShape` follow
Unity's term and avoid claiming a bare `Shape`, a common game type name;
`Shaped` is the owner's example. Both types are made the first time a program
names `blendShapes`, `Shaped` or `BlendShape`, so other skinned programs keep
their generated C and header. The generated `tide_skin_<model>_shaped` passes
`offsetof` of the vertex struct's `position`, `normal` and `tangent` fields
(the static importer's fill rules) to `tide_skinned_shaped`; Decode results
and stored models work alike. A shaped model keeps the asset's `data` and
`indices` handles, so pack proofs, palette memoization, atlas batching and LOD
levels (found by the index list) still apply; only `vertices` is a new list,
which the draw list content-hashes per draw like any vertex list made while
playing (an upload when it changes, a hash per draw of a stored model).

**Runtime** (`skinned.c`): `tide_skinned_shape_count`, `_find_shape`,
`_shape_name`, `_shape_weight` and `tide_skinned_shaped`. Shaped returns
`vertices` itself when no weight differs from its default (missing and
non-finite weights are defaults), when the data is invalid, or when the counts
or field places don't fit; otherwise a scratch copy, prepared with
`tide_list_prepare` so a copy of pack vertices is its own before writing
(found by the e2e: an unprepared copy of pack data refuses `_at_mut`). Each
shape in order adds `(w - default) * delta` per component with basic IEEE
operations under the build's no-contraction flags; an addition that would
leave float range leaves that component, so huge weights can't make
infinities or NaN in match state. Normals aren't renormalized.

**Tests.** `tests/test_skinned_shapes.c` (6): two meshes sharing a named
shape plus unnamed and sparse targets, exact deltas, defaults baked into
vertices, repeatable bytes; a shapeless model staying version 1; a flat part's
corner deltas and a skipped weights channel; nine refusals including the
4,097-target limit; every cut length and nine spoiled fields of the shape
tables; baking (defaults/empty/NaN return the same list, mismatched counts and
places, exact results against a reference, base unchanged, repeat bits,
positions only, overflow kept). `tidec_fbx_files`:
`fbx_blend_shapes_become_shapes_and_in_betweens_are_refused` (ASCII and
binary fixtures from `fbx_make.mjs` in `e2e/fbx/shapes/`, byte-identical
canonical data, shape names, defaults, deltas, static rest geometry, the
in-between refusal). `tidec_e2e_blend_shapes` (FBX asset and a GLB from
`blend_shapes_make.mjs` decoded at runtime: lookup, Shaped values checked
exactly, extrapolation, defaults, a helper, a stored shaped model across
ticks, snapshots and world packing, and both views' recorded vertices).
Three `tidec_error_blend_shapes_*` diagnostics and
`lsp_skinned_blend_shapes_complete_hover_and_help`. A Blender 4.4 export of a
skinned cube with shape keys (not committed) decodes from FBX and GLB to the
same shape names, defaults and delta counts.

**Gaps.** No GPU morphing or shape-weight animation; no checked dot members
for shapes; FBX in-between shapes and Blender's per-vertex offset weights
aren't read; flat-shaded parts' generated normals don't follow shapes;
normals aren't renormalized after baking; Synty's own files were not
imported here (licensed; the owner's tool will be the first real use).


## Prehashed release asset loading (2026-10-27)

Owner-approved tide_pack_find_id(name[32], kind) and
tide_pack_find_mesh_id(name[32], kind, layout) select the existing SHA-256
qualified-name keys. The ordinary string lookup delegates to the ID lookup;
its C API and pack version/bytes are unchanged. NULL/unknown IDs, wrong kinds
and wrong mesh layouts return NULL, including after unload. Lookup remains a
read-only directory scan with the same selected entry and lifetime as before.

Codegen's strip_names emits static name-ID arrays for asset loads/default fills
and for the private raw-pack fingerprint requests. Requests take exactly one
of qualified or id; duplicate selected IDs, malformed packs and mismatched
content retain the existing rejection paths before any loaded state changes.
Diagnostics use IDs/ordinals, mapped privately beside the generated C. Map keys,
catalog/bone/clip names and actual asset contents retain their game semantics.

pack_prehashed_lookup_preserves_names_kinds_layouts_and_reload covers ordinary
and typed lookup, missing/NULL/wrong-kind/wrong-layout IDs, replacement and
unload. The fingerprint all-kinds fixture compares named and hashed requests
with identical expected hashes and checks duplicate/missing/ambiguous requests.
tidec_release_names runs the existing skinned lifecycle/snapshot tests through
the stripped loader with its unchanged deterministic pin. Full validation and
artifact inspection results are in cli-and-packages.md.

## Blender's armature node folds into separate-export binding (2026-10-27)

Found from the Parry prototype: a Synty clip round-tripped through Blender 4.4
(FBX export, `bake_anim`, unit scale applied) sampled as the rest pose on its
character. Blender always writes the armature object as a named node above the
skeleton's top bone (`Armature`, scale 0.01 for a centimetre rig, constant
T/R/S channels baked with the take), so `Root`'s parent chain differed from the
character's (`Root` top-level, its 0.01 put there by ufbx's
`ADJUST_TRANSFORMS` unit conversion) and `bind_nodes` rejected it silently.

`engine/src/skinned.c` now folds a named top-level node (parent is node zero or
the unnamed FBX wrapper) whose name is absent from the other hierarchy. Source
side: such a node maps to `BIND_FOLDED`, its sampled local (defaults plus this
clip's channels) or its source Pose local is composed into each bound child's
local (`fold_into`: position `A.p + A.r*(A.s*C.p)`, rotation `A.r*C.r`
normalized, scale `A.s*C.s`). Target side: a bound node whose target parent is
such a node gets that parent's rest divided out (`fold_out`), so a character
exported from Blender also plays clips exported elsewhere; the target node
itself keeps its rest. Both are exact TRS only for uniform scale: folded
scales (default, every key, cubic tangents; the actual Pose value on the draw
path) must agree within relative 1e-5, otherwise binding fails before output
is touched. A source fold needs a bound child, so an unmatched top-level node
moving nothing bound is still "missing" (the existing rejection fixtures
depend on that). Matched names are never folded. Exact-skeleton sampling, the
generated-C boundary, canonical bytes and the importer are unchanged; sample
and pose routes give bit-identical results.

Failed bindings now say why: `bind_nodes` writes the differing node (missing,
ambiguous, unnamed, different parent, uneven fold scale) and
`report_binding` prints it once per (target skeleton, source skeleton, clip)
through `tide_debug_log`, with a 64-slot atomic table (worker-safe, never in
match state; past 64 pairs it stops reporting). Invalid data prints nothing.
A build-time diagnostic was not built: the compiler doesn't track which model
a Pose belongs to at a `Sample` call.

Evidence: `A_Attack_HeavyStab01_Sword.fbx` through the owner's round-trip
script, sampled into `PolygonSyntyCharacter.fbx` against the original clip at
every fifth frame: worst bone model-space difference 0.0099 mm by both
`sample_into` and `pose_into`. Between frames it reaches 23 mm at the weapon
hand, because Blender bakes at the scene's 30 fps while the importer resamples
the original curves at 60 Hz; a smaller `bake_anim_step` in the export should
reduce that (not measured). Blender's glTF export of the same scene binds too (its `Armature` node
has the axis rotation) and matches within 0.01 mm once shifted by two frames:
the exporter's clip starts two frames early and is two frames longer, an export
timing matter rather than binding. The Synty files are not committed.

Test: `skinned_separate_bindings_fold_unmatched_top_level_nodes`
(`tests/test_skinned.c`) builds a Blender-shaped glTF rig (translated, rotated
`Armature` at 0.01 with an animated uniform scale) and checks arm/hand
model-space matrices against the source's own sampling at five times in both
directions, sample/pose bit parity, the target's armature kept at rest,
uneven-scale rejection on both routes and a same-named armature beside the arm
rejected. Validation: 53 native skinned runtime tests, `tide_tests` on web,
`tidec_fbx_files`, animation/skinned/FBX E2E and both platform skinned tests.

## Missing static tangents for normal maps (2026-10-25)

The owner approved normal maps in Render3D and the corresponding static-import
completion. `mesh_file_read` retains the original canonical import, including
zero missing tangents and unsplit vertices. `mesh_file_with_tangents` makes an
owned copy for static consumers whose vertex layout declares `float4 tangent`,
then generates missing tangents after the existing node/coordinate transforms
and flat-normal creation, for glTF, converted FBX and OBJ. Supplied tangents
retain their exact existing conversion path, even when zero, nonorthogonal or
present in only some primitives. Skinned import and
runtime Decode are unchanged; no runtime function, representation or language
syntax was added. `has_tangents` continues to mean authored source data for
editor metadata, rather than whether the filled tangent stream is usable.

Generation uses the standard UV-derivative/Gram-Schmidt basis described by
[Eric Lengyel](https://terathon.com/blog/tangent-space.html), with normalized
per-corner directions weighted by corner angle. Differences, products and sums
use doubles; angles use Tide's existing deterministic `tide_atan2_f` of cross
magnitude and dot (the native compiler does not link the double transcendental
backend). This keeps narrow valid corners whose cosine rounds to 1; when the
sine is below float range, it is also the angle to better than float precision
and remains a double weight. Arithmetic order
and FP contraction stay fixed. No MikkTSpace source was already vendored, and
this implementation adds no third-party source or license. It does not promise
exact agreement with a MikkTSpace normal-map bake; export authored tangents for
that basis.

Generated xyz is finite, normalized and orthogonal to the imported normal;
w is +1 or -1, with bitangent `w * cross(normal, tangent.xyz)`. Opposite signs
never share an accumulator. A vertex used by both signs gets one duplicate,
in original vertex order: position, normal, both UVs and color are copied
byte-for-byte, and only negative-sign corners are remapped. Existing vertex
order, triangles/winding, materials, source attribute seams and bounds remain
unchanged. These duplicates exist only in the float4-tangent variant. Other
layouts retain the original counts, indices, every attribute and canonical
mesh hash exactly, including zero missing float3 tangents. No other welding,
reordering or compaction is added.

`asset_files.c` keeps the canonical mesh and a separate lazy generated variant
per cached source file. `check_asset_decl` selects that variant by value only
for a static float4-tangent consumer, including collections, before codegen or
identity calculation. Metadata-only/editor checking keeps canonical data even
when the variant is warm. Generation deep-copies every mutable stream and
index; it never reimports a file or changes another declaration's data.
Source/side-buffer metadata changes and explicit notifications invalidate both
cached meshes, including independent failed-generation state. Private source
presence bits are retained with the canonical import but excluded from its
hash and packed bytes. Original source digests remain `asset_match.hash`.

Degenerate geometry/UVs, omitted UVs and projections too close to parallel use
a deterministic least-aligned-axis fallback; zero/invalid normals use +Z as
the fallback plane without changing the normal stream. Double intermediates
avoid overflow/underflow for finite float geometry/UV derivatives. Nonfinite
source attributes retain their existing diagnostics; nonfinite results of an
extreme node transform cannot contaminate generated tangents. Private source
bits distinguish missing OBJ UV corners from authored (0, 0), so partial UV
triangles use fallback rather than inventing a mapping.

`mesh_lod_source` has a compiler-private optional float4 tangent stream.
`mesh_lod.c` adds only its sign to chart identity, separately from the existing
weighted values: no tangent direction seam, error weight or unit changes.
Codegen supplies it only for static layouts containing float4 tangent, leaving
skinned and non-tangent LOD behavior unchanged. Canonical/skinned consumers
retain their exact version-2 LOD cache keys. Only the static float4-tangent
variant uses version 3, so authored models with tangents cannot reuse old
sign-blind levels. Variant mesh hashes include the final generated
stream, duplicates and remapped indices. Pack/LOD metadata layout is unchanged.

Regressions in `mesh_files_test.c` cover a plane, flat normals, shared mirrored
UVs, authored UV seams, corner-angle weighting, narrow valid corners, missing/degenerate/extreme
inputs, nearly parallel normals, nonuniform/mirrored node transforms and
transform overflow, nonfinite attribute rejection, exact authored preservation,
mixed authored/generated primitives and OBJ's flipped/partly absent UVs. They
check normalized orthogonality, reconstructed bitangent UV direction, repeatable
hashes and every indexed non-tangent stream against authored controls.
`mesh_lod_test.c` checks opposite signs with otherwise identical UV/normal data
at every level, each side's area/bounds, and exact unchanged indices/errors
when only tangent direction changes. `mesh_variants_test.c` checks cold/warm
both-order typed selection, canonical hashes and all streams, float3 and warm
editor isolation, actual per-name packed payloads, and physical mirrored-grid
LOD cache/order consistency. The side-buffer notification regression warms and
invalidates both variants while pinning source size/time. `asset_meshes` also
declares the same mirrored source as position-only and normal-mapped vertices
in both orders, checks original plain counts/indices/bytes and mapped seams
through actual match reads, and checks the generated fallback rather than zero.

Focused native validation: `tidec`, importer/asset-files/LOD test executables,
`tidec_e2e_asset_meshes` and `tidec_e2e_lod` build warning-free. All five focused
CTests pass, including 25 importer and 11 LOD cases, the new typed cache/pack/
metadata/LOD regressions, same-size side-buffer invalidation, both-order match
reads, and existing exact-allocation cut/corruption coverage. Full native/web
suites remain the parent task's validation; no full suite, web build, commit,
merge, push or CI dispatch was run by this implementation sub-agent.

Final review additionally found the narrow-corner cosine-rounding case above.
The atan2 correction and its regression (including a sub-float-angle aspect
ratio) rebuild warning-free and pass both focused CTests again. Integration
review then required typed variant isolation to preserve non-tangent CPU data;
the source and new regressions above implement it. All five focused native
CTests pass after that correction. Fresh review found no blocking correctness
issues. Remaining coverage gaps: an equal-hash authored-tangent source with a
seeded legacy LOD disk cache, and controlled generation-only failure with
repeated diagnostics and invalidation recovery (ordinary success/invalidation
is covered; forcing the real split vertex limit is impractical for a unit test).

Resource collection helpers (2026-10-18) borrow existing Clip, Texture and
Mesh<V> asset lists/maps through ordinary read-only synchronous parameters.
Direct assets and forwarded borrows pass their current handles without copying
the collection or retaining pack data. Resource/value lifetimes, reload,
canonical validation, pack kinds, identities and pruning remain unchanged.
Collections cannot escape into storage, results, error payloads or tasks.
`asset_borrows` and fourteen diagnostics cover the completion; contracts and
validation are in notes/language.md and the public functions/assets pages.

2026-10-18 test-save isolation: native `tide_platform_io_init` now takes an
optional directory after app_id. NULL retains normal app storage; the desktop
host supplies `--data-dir` when requested. Full native paths are resolved before
relative I/O or exposing Application.persistentDataPath; app ID/root pairs reuse
the provider and pending flushes. Changing roots uses the existing app-switch
cleanup. Unusable overrides fail without fallback; temporary roots and native
absolute file access are unchanged. Web callers pass NULL. Contracts/tests and
validation: `notes/standard-host.md`, Explicit save directory for test runs.

Simulation identity (2026-10-15): simulation-read assets contribute source
digests and canonical imported skinned data/external buffer digests to the new
Simulation ID. Presentation-only assets do not. Ordinary simulation-read asset
edits now set assets_affect_match and trigger regeneration in watch mode;
pack-only reload remains available for visual-only assets. The server computes
identity before pruning, so its reduced pack and full client agree. Custom C
remains conservative for server pack retention and includes every packed asset
in compatibility, since C reads cannot be inferred. Compiler regression tests cover
visual/simulation text edits. Full contracts/tests: managed-hosting.md.

Dedicated server packs (2026-10-15): closed server builds keep simulation asset
dependencies and omit client-only assets after computing the client's unchanged
compatibility identity. C projects retain all assets unless explicitly opting in.
Dynamic match Asset<Mesh<V>> and Asset<SkinnedMesh<V>> types keep every matching declaration and its vertex
defaults. Different pack digests use self-contained network transfer. Source
validation still processes all assets. Contracts and tests: dedicated-servers.md.

## FBX indices past their arrays are clamped (2026-10-22)

Synty's POLYGON packs, opened by Blender and Unity, include FBX files with an
attribute index past its array (`SM_Prop_Cabin_01.fbx` of Alpine Mountain);
the importer refused them as "isn't an FBX that decodes (Bad index)", found
while building a prototype. `fbx_files.c` and `model_metadata.c` now load with
`UFBX_INDEX_ERROR_HANDLING_CLAMP` rather than `ABORT_LOADING`: such an index
reads its array's last element, as those tools read the file. Clamping keeps
every index in range, where `NO_INDEX` would hand the importer `UFBX_NO_INDEX`
to guard against. A polygon corner past the vertices is still refused by ufbx
itself ("Failed to load"), which this does not change.
`fbx_index_past_its_array_is_clamped_to_the_last_element` (`tidec_fbx_files`)
rewrites the ASCII triangle's first UV index past the array and checks the
corner reads the last UV. The cabin itself (a paid asset, not in this
repository) was checked through the prototype's build.

## Persistent model references (2026-10-12; skinned extension 2026-10-25)

Approved Asset<Mesh<V>> fields store a checked singular asset declaration's
identity, separate from borrowed Mesh handles. Value resolves its current global;
geometry reloads require no reference rewrite. Generated typed switches use no
engine API or global runtime registry. Compiler/storage contracts and tests are
in notes/language.md, Checked static asset references. The native editor uses the
existing static importer/cache for browsing and nested scene previews; copied
geometry survives front-end arena replacement (notes/editors.md). Imported colors
and fixed preview lighting are visual only; custom vertex defaults/materials and
SkinnedMesh previews are not evaluated by the editor.

The owner approved the same typed identity for Asset<SkinnedMesh<V>>, including
transparent aliases. Value exposes the declared model type and its dynamic
catalogs; no inferred-name forwarding is added. Its generated switch returns
the current asset global's original data/vertices/indices handles, preserving
packed-model palette and LOD proofs. Persistent model assignment still follows
ordinary owned-value rules. Empty/unknown references resolve empty; missing or
changed skinned pack payloads retain the existing atomic compatibility rejection.
No runtime API is added, and native editor previews remain static-only.
Compiler, worker, server-retention and LSP coverage is listed in notes/language.md,
Checked skinned asset references. Root reported the initial Asset E2E passing;
new actual-unload and erased-catalog regression additions remain uncompiled.
Unload preserves the existing borrowed lifetime: it clears the pack/proof registry,
not generated asset globals. The follow-up tests compare checked catalog/direct
skinned draws with the named asset and retain pre-unload captures through deferred
palette preparation. They do not dereference stale typed lists or require Count
to return zero. Reload restores the same reference identity and packed fast paths.

## Retained ordinary asset values (2026-10-11)

Production sharing now implements the owner's approved detach-on-write direction, including ordinary asset lists, not only Animation/SkinnedMesh data. A field owns a small heap descriptor naming immutable bytes by SHA-256 of the layout, length and content. The heap retains that content outside simulation pages; copies and snapshots retain it across pack replacement/unload. Reads and repeated assignment of the same known asset avoid payload copies. First write detaches into ordinary owned storage. Already-owned destinations reuse their storage on asset reset, avoiding the prototype's doubled pending allocations. Arbitrary ordinary values retain their existing copying path; no broad content comparisons were added.

Immutable canonical model data is validated once in its retained allocation and can reuse skinned version/sampling proof after unload. Mutable/detached data keeps full validation. Generated nested-list/map/text ownership traversals detach outer storage before retagging or writing; Texture/Clip retain their existing small resource-reference behavior. Private optional origins on detached list chunks enable smaller asset-relative network pages. These hints neither enter hashes nor change values. Saves include each retained payload once and remain self-contained; edited save pages still use the ordinary full representation.

`tests/test_asset_lists.c` covers 100 independent fields sharing one payload, same-asset assignment, local/field mutation, snapshots, raw packing without a pack, sparse/dense transfer, base reuse, missing assets and malformed descriptors. `compiler/tests/e2e/asset_sharing` covers generated mutation and nested/text/map ownership plus real loopback join/retry under loss. The owner's unchanged 100-character project also produces identical simulation, palette and whole transfer bytes on native/WASI; each decodes the other's known-pack and self-contained packets, including self-contained decoding after unload. Structural evidence and reproduction are in `bench/results/asset-production/`. Timing remains deferred; prototype CPU gains are not a production timing claim. See `notes/ecs.md` and `notes/netcode.md` for storage and wire contracts.

## AnimationCatalog lookup API, 2026-10-11

The owner's approved `AnimationCatalog` name replaces `Animations` in asset
and value annotations. Checked members, file-list/glob imports, nested asset
collections, source skeletons and canonical packing remain the same.
Dynamic lookup uses bool/out `TryGetAt` and `TryGetByName`, including model
animation/bone catalogs and Skeleton name lookup. Failure assigns `default`;
missing/empty/ambiguous names and invalid indices are unsuccessful.
Historical API spellings in the entries below are superseded by this change.

Existing animation, skinned and FBX E2E fixtures use the new methods.
`animation_lookup` checks success and failure outputs, int-boundary indices,
duplicate/empty/unnamed clips, existing and delayed locals, discards, named
arguments and a receiver retained across a later argument clearing its catalog.
Its C harness checks owned field outputs, stable heap use during repeated
replacement/failure, pack unload, snapshots, replay and serialization.
Checker fixtures reject missing out, wrong types, read-only places, list
element outputs, reads before assignment and the old methods with replacement
guidance. No runtime signature or portable representation changed.
Validation: warning-free native and web debug builds; all 2,150 native and
2,121 web CTests pass, including the owned-output fixture on native and WASI,
unchanged deterministic pose hashes, and OpenGL/WebGL/WebGPU checks. The docs
site build passes. Final wording/dispatch cleanup passes focused catalog checks.

## Owned-animation validation and separate binding (2026-10-11)

The owner's existing test project Animation component usage is retained.
The runtime now expresses full owned/unproven key validation as vectorizable
reductions and reuses complete node-table validation within separate binding.
All descriptors/spans, first-time nonnegativity, strictly increasing finite
times, finite values/tangents and both skeletons' nodes still pass before any
output mutation. Private node reads remain inside those unchanged opened
models. Exact name/parent matching, wrapper rejection, source-default copying,
interpolation expressions and ordinary List/struct ownership stay unchanged;
no persistent proof or cache is inferred from an owned model's handle/hash.

`skinned_owned_key_scans_check_every_lane_and_tail_before_writing` corrupts
every scalar of long rotation and cubic rotation/translation tracks, including
tangents, every key time, signed-zero duplicates and finite unordered times.
Scratch and heap sampling plus separate-rig sampling reject without changing
poses or heap hashes. `skinned_binding_validates_all_nodes_before_private_reads`
independently spoils every node's parent/name bounds and all ten TRS scalars on
both sides of sample/pose binding, then verifies successful restored binding.
Existing aliases, snapshots, pack invalidation, wrappers and exact CPU/GPU
checks remain. Performance evidence and the still-large owned match state are
recorded in `notes/benchmarks.md`.

Warning-free full native debug 2,136/2,136 and web debug 2,107/2,107 CTests
pass, including the existing exact pose bits and OpenGL/WebGL/WebGPU checks.
After the final test-only key-count/tail addition, both complete runtime test
executables pass again. No compiler, canonical asset layout or API changes.
Linux/macOS/MinGW/Android/sanitizer coverage and CI were not run here.

Integration with `origin/dev` through `14cc6a73` keeps incoming Session/Clipboard,
package tooling and the LOD triangle floor. The merge is clean; this runtime
source is byte-identical to the measured candidate. Both complete native/web
debug builds pass, followed by 36 native/38 web runtime, skinned/GPU, LOD,
Session and Rooms checks. The saved timing binaries remain frozen; no timing
comparison is claimed for the merged compiler/tool changes.

## Separate animation exports with different defaults (2026-10-11)

Integration with `origin/dev` through `268cc751` retains incoming automatic texture compression, enum shorthand and omitted-playback editor hints. The notes insertion conflict preserves both entries; runtime binding code had no conflict. Complete native/web builds and the merged docs build pass, followed by 50 native and 56 web dependency checks: the complete runtime suites, animation/catalog/FBX and skinning paths, texture compression, and OpenGL/WebGL/WebGPU draw/shader checks. The earlier performance binaries/results remain frozen; no post-merge timing comparison is claimed.

The owner approved binding unique exact bone names and their complete parent chains while copying source local transforms directly, even when authored defaults differ. test project reproduces the previous rejection: the character FBX has a skin and bind pose, while its idle/run/jump exports have neither and store take-specific defaults. Their named hierarchies match. The importer already retained usable animation data; requiring equal defaults at runtime was the blocker.

`bind_nodes` now ignores default TRS differences. In-place sampling copies the full source defaults for required channel targets and ancestors before applying sampled channels; unrelated target nodes retain target rest. Binding a sampled source pose follows the same rule. This is direct local-transform binding, without proportion or coordinate-space retargeting. Missing/ambiguous required names, changed parent chains and unsafe unnamed wrappers still reject before changing output. Exact skeleton fingerprints, Bone identity and blending remain strict. No importer, canonical bytes, generated-C API or pack layout changes.

The new runtime regression covers differing translation/rotation/scale, an unanimated required ancestor, sample-versus-pose binding parity, aliases and persistent snapshot ownership. Existing rejection/wrapper tests remain. All 48 native skinned checks, 2,079 native and 2,050 web debug CTests pass, including unchanged cross-platform pose bits and OpenGL/WebGL/WebGPU checks; the docs production build passes. A scratch copy of the owner's assets binds all six imported clips by both routes, and hidden native captures show run/jump poses on a 100-character grid. The owner's project sources were not changed. Linux/macOS/MinGW/Android/sanitizers and CI were not run. Owned-animation validation performance and remaining state-copy costs are recorded in `notes/benchmarks.md`.

Automatic color texture compression (2026-10-11): eligible ordinary `Texture` assets now add a complete BC1/BC3 mip chain beside their exact RGBA fallback. `[Raw]`, small/non-power-of-two images and cubemap faces keep their existing payload. Build-tool-only stb_dxt 1.12 is pinned to `2c980bb59875b0d32144a71867fbdebb2f77cd20`, with its license installed alongside the tools. Content identity distinguishes the compressed variant; logical asset values/fingerprints and generated calls stay unchanged. Pack size grows in this version. Full contracts and evidence are in `notes/textures.md`; DDS/KTX source imports remain unbuilt.

## Levels of detail (agreed and built 2026-10-11)

- `docs/spec.md`, Levels of detail, has the design: the owner decided opt-in with `[LOD]` on a mesh asset's declaration (opt-out was the first proposal; "that way no surprises for the user"), everything else automatic and made at build time with a cache, a pick for each instance of `Draw.Instanced`, skinned meshes included, and two overrides (a level by its number, and a camera for a target to pick through; "they probably want it to match the visuals the main camera rendered"). Every other rule there is a pick, Provisional. `docs/game/assets.md` (Levels of detail) is the users' page. What's built, and what code relies on:
  - **The language** (`compiler/src/check.c`): `[LOD]` is read with the asset's other attributes, in the loop that reads `[Filter]`, and sets `decl.lod`; it takes nothing, goes once, and is an error on an asset that isn't `ASSET_MESH` or `ASSET_SKINNED`, and on any declaration that's no asset (in the declarations' attribute loop, which also suggests it for `[Lod]`). `Draw.LOD`, `Draw.SetLOD`, `Draw.LODCamera` and `Draw.SetLODCamera` are `add_setting` signatures in `builtins.c`, so both forms, each one's misuse and quick fix, a target's own and the editors' completion and hover are the family's code. The forms with no arguments are settings of no parameters, as `Screen` is, whose block form is `tide_draw_push_no_lod` or `tide_draw_push_no_lod_camera`.
  - **Nothing in generated draw code changed.** A level is found by the mesh's indices' list (below), so `Draw.Mesh(rock, ...)`, `Draw.Mesh(rock.vertices, rock.indices, ...)`, a list's element and a map's value are all the same calls they were. A game with no `[LOD]` and none of the settings generates the C it did.
  - **Making the levels** (`compiler/src/mesh_lod.c`, `mesh_lod.h`; tidec's alone, nothing of it is in the engine or a game): edges collapsed onto one of their ends, the cheapest first from a heap, by quadrics. Vertices are numbered by their position's bits (`place`), and again by position with uvs and colors (`chart`): topology and the planes are a position's, so a mesh whose every triangle has corners of its own is still a surface, and a seam is a position with two charts. A collapse is refused where the edge isn't a side of one triangle (for a position on an open edge) or of two (inside), where another position is next to both ends (the surface would fold), where a vertex's chart has nothing beside it along the edge (a seam would tear) and where a triangle that stays would turn more than `TURN_LEAST` or go flat. A position gathers its triangles' planes by area, and ten times an open edge's own plane; a vertex gathers what its values (uv, uv2 and color times their weights, then the normal times its) do over its triangles, as a slope and where from, Hoppe's way. What a collapse costs is both ends' planes at the end it goes to, over their weight, plus what the moved vertices' values were against the ones they take, summed over the area they held them on (not averaged: see the crowd's two gaps below), plus, for a skinned mesh, the edge's length times how differently the two follow their bones (`mesh_lod_source.apart`). A level's error is the square root of the dearest collapse so far, times the mesh's extent. Corners at a position are a linked list through their triangles' corners, so a collapse splices two lists; a position with more than `FAN_MOST` (96) corners stays.
    - **Everything is `+ - * /` and `sqrt` on doubles in a fixed order**, with `#pragma STDC FP_CONTRACT OFF`, and ties in the heap go by the positions' numbers, so a file gives the same levels wherever it's built. Nothing reads a level but a draw, so this is for packs that are the same bytes, not for the match.
    - **A file's numbers are somebody else's**: an index past the vertices and a triangle with two corners in one place are left out, positions that are no number make no plane and pass no check, and a mesh with no extent has no levels.
    - A chain ends at `MESH_LOD_MOST` (7) levels, where a level can't take a quarter of the triangles left, where it would be off by more than half the ball's radius, and before a level under `triangles_least`. A mesh of fewer than `mesh_least` triangles has none.
  - **The cache** (`mesh_lod_cached`): by a key that's the SHA-256 of a version text, the mesh file's hash and, for a skinned model, its bytes' hash (`mesh_levels` in `codegen.c`). Kept for the process in a table, and in `<game>/.tide/assets/lod/<key>.lod` where there's a game folder: a header of words, then the indices. A file is checked whole before it's taken (its size to the byte, each level within the indices and whole triangles, errors that rise, every index under the mesh's vertices), and made again otherwise; it's written beside its name and renamed. The version text changes whenever `mesh_lod.c` would give other levels.
  - **Skinned meshes** go through the same `pack_mesh`, as their typed geometry always did. `bone_weights_of` reads each vertex's influences from the model's bytes through the checked getters (`tide/skinned_data.h`), and `bones_apart` is half the sum of the weights' differences, bone by bone: 0 for two vertices that follow alike, 1 for two with no bone in common. Bytes that aren't a model of that many vertices give no weights, and the levels are made as a mesh that follows none.
  - **The pack** (`engine/src/pack.c`, `pack_load.c`): `tide_pack_mesh_levels` is `tide_pack_mesh` with a `tide_mesh_levels`. The levels are a block of their own (`levels_block`: 28 words, which are the ball, the count, the indices' count and each level's error, first index and count, then the indices), and the mesh's indices' block names it in the word after its element's size, which was 0: 1 + the block's offset, as a block can be the region's first, at 0. **That was a bug as first built**: the first leveled mesh of a pack had no levels, which `pack_meshes_have_levels_of_detail_beside_their_lists` now pins with a pack that starts with one. The pack's version didn't change: an older pack says 0 there, and an older loader never read the word. The content a mesh is shared and told by has the levels' key in it, so the same file with and without `[LOD]` is two meshes, and a mesh marked `[LOD]` that got no levels is the mesh it is without.
    - `tide_pack_list_levels(indices, &levels)` reads them back for a list of the loaded pack: false for any other list, a mesh with none, and a block that isn't whole (a count over 7, a level past the indices or not whole triangles, an error that's no number or under the level before's, a ball that's none, a place outside the region). The indices are the pack's own memory; one past the mesh's vertices is the reader's to leave out, which the draw list does.
  - **The draw list** (`engine/src/draw.c`, `tide/draw.h`): `tide_draw_mesh_levels(d, mesh, levels)` gives a filled mesh its levels, which the list copies into the mesh's own data (`tide_draw_mesh_data.levels`, `level_error`, `level_first`, `level_count`, `level_indices`, `center`, `radius`), leaving out triangles with an index past the vertices and ending the levels at one with nothing left or an error that doesn't rise. `mesh_levels` in `engine/src/mesh.c` calls it where a mesh of Tide's lists is filled, for indices of the pack that have some.
    - **A level is a mesh of the list's with no vertices** (`level_mesh`): the same `space`, `id` and `other` as its mesh, a version that's a hash of the mesh's and the level's number, its own indices, `base` naming the mesh's place and `level` its number. So a run names a level as it names any mesh: no command, run or renderer step has a field for it, and `mesh_run`'s and `shaded_instances`' merging are the code they were. A frame that draws a level marks its mesh drawn too, so the mesh keeps its place while any of its levels shows. The mesh remembers each level's place (`level_mesh[]`), checked before it's believed.
    - **The pick** (`lod_view_of`, `pick_level`): once a draw, the destination's camera as the length of its matrix's y row (clip space is 2 for the destination's height, whatever its shape, and a camera that's `fit` only has its x changed), its w row and that row's length; then for each instance, the mesh's center through the transform, the transform's longest column as how many times bigger it makes the mesh, and the w at the nearest the ball comes. The level is the last whose error is no more than that w over (the bigness times the pixels a unit is at w = 1). A w that isn't above 0, and any number that's none, is the full mesh. An orthographic camera has a w row of (0, 0, 0, 1), so size alone picks.
    - **Heights:** the window's is `tide_draw_list.screen`, which a host says with `tide_draw_screen_size` (`tide_run`'s frame does, in `tide/host.h`, beside its `tide_draw_reset`; a list nobody told picks nothing). A target's is its size's, times its scale where it has one of its own, or the window's times its share where it follows the window. So a pixel is a logical one but for a target with its own scale.
    - **Where it's asked:** `tide_draw_instance` (one more load of the mesh's `levels`, and for a mesh that has some, `one_level`), `tide_draw_instances` and `tide_draw_shaded_instances`, which sort a draw's instances by level into `tide_draw_list.level_room` (a counting sort, in order) and record a run a level through the functions that were there (`instances_placed`, `shaded_placed`). A shader's instance is picked by the `float4x4` that starts its bytes, where it has one. `tide_draw_append` and the primitives call the inner functions and pick nothing. `tide_draw_mesh_level` is the pick as a function, for tests and hosts.
    - **The settings:** `tide_draw_place.lod` (0, or 1 + the level) and `lod_camera` (0, or 1 + a place in `cameras_3d`, of a camera no command names), each destination's own, saved and popped as `TIDE_DRAW_SAVED_LOD` and `TIDE_DRAW_SAVED_LOD_CAMERA`, and back to 0 at `tide_draw_reset` and `tide_draw_defaults`. They record no command. A named level needs no camera and no size.
    - **Gathering** (`tide_draw_gather_levels`, which `tide_draw_prepare` calls first, so the renderer and `tide_draw_append` have it done): a view that runs for each entity records an instance at a time, each at its own level, which is a run for every change of level. Where runs of one mesh's levels follow each other with nothing between, drawn with the same of everything (`level_follows`: what `mesh_run` and `shaded_instances` merge by, with the mesh's family in the mesh's place), their instances are sorted by level in place, the first commands get a run a level, the finest first, and the rest are made to draw nothing (a mesh of 0, or a zeroed run, as `tide_draw_forget` leaves them, which renderers and `append` already pass over). It writes through the list's pointers only, as the list is `const` there, and is a scan of the commands that does nothing where `tide_draw_list.leveled` isn't set. A renderer that never prepares draws the same picture in more runs.
    - `tide_draw_append` takes a level with its mesh (`append_mesh`): the destination list gets the mesh too, and the level names its place there.
  - **The renderer** (`platform/src/platform.c`): `mesh_for` uploads a level's indices alone and its mesh's vertices through the mesh's own `gpu_mesh` (`gpu_mesh.base`, `corners_of`), so the GPU has each vertex once however many levels show; the index width goes by the mesh's vertex count, which a level's data repeats. A place that was a whole mesh's and is a level's now lets its vertex buffer go. Nothing in `gpu.h` or the web's `tide.js` changed.
  - **New runtime functions** (maintained with the draw list and the pack, on the terms in `notes/runtime-ownership.md`): `tide_pack_mesh_levels`, `tide_pack_list_levels`, `tide_draw_mesh_levels`, `tide_draw_gather_levels`, `tide_draw_mesh_level`, `tide_draw_screen_size`, `tide_draw_lod_pixels`, and the settings' `tide_draw_set_lod`, `tide_draw_unset_lod`, `tide_draw_push_lod`, `tide_draw_push_no_lod`, `tide_draw_set_lod_camera_3d`, `tide_draw_set_lod_camera_matrices`, `tide_draw_unset_lod_camera` and their three pushes. Generated code calls only the settings'. `tide_draw_list`, `tide_draw_place` and `tide_draw_mesh_data` grew, so hosts, libraries and generated games rebuild together.
  - **Tests:** `tidec_mesh_lod` (`compiler/tests/mesh_lod_test.c`: a flat square that loses nearly all its triangles and none of its area, a ball that stays closed with errors that rise, a seam of two colors no triangle crosses, a mesh with corners of its own, a simple mesh left alone, the same levels each time, a spoiled mesh, a source that says what's apart, and the cache's file read, cut and of another mesh); `tests/test_draw_levels.c` (the pick against worked-out distances, a scale, the destination's transform, the pixels allowed, no size and no number; a level as a mesh with no vertices that keeps its mesh's place; levels checked as they're given; instances grouped, named levels, a target's own camera and the one it's told, gathering for meshes and for a shader's runs, and a list appended to another); `pack_meshes_have_levels_of_detail_beside_their_lists` and `pack_levels_of_detail_spoiled_are_none` in `tests/test_pack.c`; `compiler/tests/e2e/lod` (`lod.tide` with the models `lod_make.mjs` writes into `e2e/assets/lod/`: a ball of 960 triangles and a two-bone tube of 768, as a mesh alone, a list, a map, by its two lists, through a frag, as instances, with each setting's forms on `Draw` and on a target, skinned, and with the pack loaded again; the match reads the full mesh's counts); `compiler/tests/errors/lod_*.tide` and `draw_lod_*.tide`, a file a diagnostic; `levels_of_detail` in `platform/tests/draw.c` (a mesh whose levels are other triangles of its corners, so a pixel says which was drawn: named, picked for each instance, and drawn alone then gathered, for an ordinary 3D mesh and one through a shader, with the draw calls counted); `lsp_levels_of_detail`.
  - **Measured** (the debug compiler, unoptimized, on this Windows machine, once each): a ball of 261,120 triangles builds in 1.0 s with no `[LOD]`, 7.8 s the first time with it, and 1.5 s after that, from the cache's 3.1 MB file. Its pack goes from 6.30 MB to 9.41 MB: the levels are as many bytes again as the mesh's own indices, and no vertex more. **Not measured:** the same with an optimized compiler, and what the one more load in `tide_draw_instance` costs a frame of instances of meshes with no levels (`bench/results/draw.jsonl` has the numbers it would be set against).
  - **What it saves a frame** (2026-10-11, the owner's question; a scratch program over the C API, not kept: an optimized build on this Windows machine's OpenGL 3.3, a 1280 by 720 offscreen frame read back, the middle of 60 frames after 15, once each). Instances are in rows from near to far across a 60 degree view. The optimized compiler makes the 261,120-triangle ball's levels in 1.8 s, and builds from the cache in 0.2 s.
    - **A heavy mesh, the ball of 261,120 triangles.** 400 instances from 5 to 400 units: 6.40 ms whole (104 M triangles) and 0.92 ms picked (0.82 M: every instance at its last level, 2,040 triangles, which is within a pixel even at 5 units, as the file has far more triangles than a ball that size shows). 100 instances from 3 to 40 units: 2.11 ms whole, 0.79 ms picked. The same file with no `[LOD]` draws in the time the leveled one does whole (5.9 ms).
    - **A light mesh, the test ball of 960 triangles, 40,000 instances.** From 5 to 600 units, a draw each: 5.03 ms whole and 4.07 ms picked (38.4 M triangles to 3.6 M, in 7 draw calls for 2). From 3 to 30 units, where two thirds stay whole: 5.73 ms whole and 6.60 ms picked, so picking costs more than it saves there. As one `tide_draw_instances` call: 2.11 ms far and 5.10 ms near, the best of each.
    - **Where the cost is:** recording 40,000 single draws of a mesh with no levels takes 1.60 ms, and 2.1 to 2.25 ms where each picks: 13 to 16 ns a draw more, for the pick, the level's place, and commands that change level, which `tide_draw_gather_levels` then sorts as the frame is rendered. One instanced call records in 0.44 to 0.52 ms against 0.14 ms with no levels: about 8 ns an instance. `lod_view_kept` keeps the camera's part of the pick between single draws (it was worked out at each, 18 ns a draw more); what's left to take is the gathering. A mesh with no levels records in the time it did (1.59 against 1.60 ms).
    - **So:** it pays by the triangles it takes off. A model of thousands of triangles drawn small wins several times over; one of a thousand triangles, on a GPU this fast, gains little a draw each and wants `Draw.Instanced`, or no `[LOD]`. One GPU, one run: no other machine, backend or a phone was measured.
  - **No level under 500 triangles** (2026-10-11; the owner, of those numbers: "fixed floor is fine"; 500 is Claude's pick; split in two 2026-10-30, below: a mesh of under 1,000 has none, and a level has 64 at the least). `MESH_LOD_TRIANGLES_LEAST` in `mesh_lod.h`, which `mesh_levels` in `codegen.c` gives the source as `triangles_least`: `mesh_lod_make` ends the chain where the next level would aim under it, so a mesh of under 1,000 triangles has none and is packed, drawn and recorded as one with no `[LOD]`. A source that says 0 has no floor, which is what the maker's own tests go by. The cache's version text says the number (a `_Static_assert` beside it), so levels made before it are made again. The test models grew for it: `ball.glb` is 9,024 triangles (four levels, of 4,511, 2,255, 1,126 and 563) and `arm.glb` 2,048 (two), and `light.glb` is the old ball of 960, marked and with none. `mesh_lod_makes_no_level_under_the_fewest_triangles` pins the rule, and `lod_assets_have_levels_beside_the_full_mesh` the counts.
    - **Measured with it** (the same scratch program and machine, once each; whole, then picked a draw each, then picked as one instanced call, in ms; about 39 M triangles whole in each):

      | Triangles | Levels | Instances | 5 to 600 units | 3 to 30 units |
      |---|---|---|---|---|
      | 960 | 0 | 40,000 | 6.34, 6.38, 2.98 | 6.82, 6.73, 4.19 |
      | 3,968 | 2 | 10,000 | 3.58, 2.55, 2.31 | 3.49, 2.86, 2.81 |
      | 16,128 | 5 | 2,500 | 2.97, 1.58, 0.93 | 3.12, 1.62, 0.95 |
      | 65,024 | 7 | 600 | 2.88, 1.04, 0.48 | 3.44, 1.35, 1.16 |
      | 261,120 | 7 | 150 | 2.85, 0.89, 0.51 | 2.80, 0.88, 0.85 |

      Picked is no slower than whole in any row now. The first row's mesh has no levels, so its three numbers are the same draws as an unmarked mesh's (its two a-draw-each numbers differ by the run's noise; the instanced one is faster as one call records in 0.18 ms where 40,000 take 2.2). The machine was busier than for the numbers above, so set rows against each other, not against those.
  - **Validation:** full native debug (2,135 tests) and web debug (2,106) suites pass after merging `origin/dev` through `e6a4133a` (texture compression and idle GPU assets among it, in the same renderer and pack files), with the pixel test on OpenGL 3.3, WebGL 2 and WebGPU, and the docs site builds. Linux, macOS, MinGW, Android and the sanitizers are a requested CI run's.
  - **Not built:** a setting in the language for the pixels a level may be off by (`tide_draw_lod_pixels` is there for C); levels for `SkinnedMesh<V>.Decode` models; anything that tells a game its mesh would gain from `[LOD]`. The first real exporter's model measured is the crowd's below; the weights (`WEIGHT_NORMAL`, `WEIGHT_UV`, `WEIGHT_COLOR` in `mesh_lod.c`) were tuned on it alone.
  - **A crowd's two gaps: the pick kept the full mesh, and the chain ended at its first level** (2026-10-30; the Bannerlord duel's battle of 20,000, `tools/army` in that repository, Tide 0.5.0-nightly.59). Its far fighters are `[LOD] asset Mesh<CrowdCorner>` of Synty's `PolygonSyntyCharacter.fbx` (4,896 triangles; `joints` and `weights`, so read skinned, at rest, in metres, radius 1.12) drawn as one `Draw.Instanced` through a vert that skins on the GPU, about 10,600 of them 30 to 150 m off at 960 by 540.
    - **The cause of both was the values' share of a collapse's cost.** It was the area-weighted mean of how far a vertex's values (normal, uv, color, times their weights) were from what they became, in the mesh's whole extent: a normal turned 28 degrees on a 4 cm patch of the model (a bevel or a buckle, its corners sharing a position and a uv but not a normal) cost as much as a surface 0.49 m off. One such collapse, at 4,136 triangles left, made the 2,448-triangle level 1 say 0.495 m (the pick allows 0.06 m at 30 m), and the next level would have said 0.63, over half the reach, so the chain ended. Traced by the cost's three parts: the planes' share stayed under 2 cm, the bones' (`bones_apart`) under 3 cm, and uvs gave nothing (their share was 0 throughout); with normals unweighed the chain went on to 611 triangles at 2 cm. The rest bounds and the instance's transform through the vert's struct were right: once levels said what they're off by, the same draw picked them.
    - **Now** (`weigh` in `mesh_lod.c`): the values' share is summed over the area they held, not averaged, so a value off by e over a patch L across costs as much as a surface off by e times L, and `WEIGHT_NORMAL` and `WEIGHT_COLOR` are 2 (they were 0.5): a normal 30 degrees off, or a color off by half, over a pixel is a pixel off. The weights were picked on the crowd: at 0.5 the far fighters' shading went darker and the far band's color moved (mean red 111 to 103) at 24.9 M vertices; at 1, 31.3 M and still darker; at 2, 45.6 M with each band's color as close to the full mesh's as `Draw.LOD(1)`'s (which the duel's agent found the same by eye); at 4, 81.1 M. Pixels more than 32 off the full mesh in the furthest band: 8% for level 1 forced, 16% for level 3, 21% picked at 2. Most of that is the crowd's aliasing, a figure there being a few pixels.
    - **The floor is two** (`mesh_lod.h`): `MESH_LOD_MESH_LEAST` (1,000), under which a mesh has no levels, which is what the measurement behind the old floor was of, and `MESH_LOD_TRIANGLES_LEAST` (64), under which no level is made, as a level costs only a draw call more where it's in use. With the old 500 the crowd's chain stopped at 611. `mesh_lod_source.mesh_least` is new beside `triangles_least`, both 0 (none) unless a caller says.
    - **The crowd's levels now:** 2,448, 1,224, 611, 304, 151 and 75 triangles, off by 4.4, 7.0, 10.5, 13.9, 23.7 and 33.9 cm. The cache's version texts are 4 and 5, saying both numbers, so levels made before are made again.
    - **Measured** (`tide run --release tools/army --frames 300 --capture x.png --stats` on a scratch copy of the duel's `combat/` and `tools/army/` with the crowd at the default pick, three runs each, interleaved, this Windows machine's GPU on Vulkan): nightly.59 174,316,125 vertices and GPU 16.2 ms; with this, 45,596,328 and 5.9 to 6.2 ms; `Draw.LOD(1)` with nightly.59, the duel's workaround, 96,266,475 and 8.1 to 9.3 ms. The "after" was this checkout's release `tide.exe` in a copy of the nightly.59 installation (the same engine files: HEAD was nightly.59). Single runs in between read up to 13 ms under other load: compare interleaved runs.
    - **Tests:** `mesh_lod_weighs_what_a_surface_shows_by_the_patch_it_is_on` (a flat square with a patch two cells across whose normals lean 37 degrees: every level off by under twice the patch's size, which the averaged measure fails; and the same square leaning one way and the other at each corner, whose last level is off by more); `mesh_lod_makes_no_level_under_the_fewest_triangles` pins both floors. In `e2e/lod`, the ball now has seven levels (to 70 triangles), the skinned arm five, and the near draws moved from 2 and 2.5 units to 1.5 and 1.75, as the ball's first level is within a pixel at 2 now. The near arm's check is the level within a pixel there, which is no longer the full mesh.
    - **Not built:** a measure of what thinner limbs take off a crowd's coverage in all. A far figure loses a thin arm within a pixel, which is right for one, and many of them show more ground between them. At the picked weights each band's color is within what level 1 forced moves it.
  - **Levels ordered for the vertex cache** (2026-10-30, the owner: "it will help in the future if not here"). `order_for_cache` in `mesh_lod.c` reorders each level's triangles as it's made, by Forsyth's linear-speed scoring over 32 cache places, with `x * sqrt(x)` and `2 / sqrt(n)` for his `pow`s, so the order is the same on every machine; ties go to the lower triangle number, and where nothing near the cache is left the next is the first undrawn in the list. Only a level's order changes: the full mesh's indices are the file's, as games read them. The cache's version texts are 6 and 7.
    - **Measured** (vertices transformed a triangle, a 16-place FIFO): the test ball's bigger levels from 1.0 to 1.5 down to 0.65 to 0.70; levels under about 60 triangles were in the cache already. The duel's fighter, whose triangles share few corners, reaches the least there is at each level (2.81 to 2.55, 2.61 to 2.28, 2.37 to 2.00, 2.14 to 1.85 against 1.81). Its GPU time didn't change measurably: three interleaved runs each, 5.54, 6.18 and 5.54 ms unordered against 5.95, 6.25 and 5.42 ordered, so this GPU's own vertex reuse, or the frame's other costs, hide it there.
    - **Test:** `mesh_lod_orders_levels_for_the_vertex_cache` (each of the ball's levels of 100 triangles or more under 0.75 a triangle; unordered, they're 1.0 to 1.5).

## Conservative release asset pruning (2026-10-10)

- `codegen_options.strip_unused_assets` defaults to false. Closed standalone release builds opt in; debug, watched/layout and C-host builds keep all assets. Automatic callers also opt out when the game or a package contributes C/C++ sources or prebuilt libraries, whose generated API or `tide_pack_find` uses cannot be inferred from Tide. `tidec --strip-unused-assets` is an explicit opt-in for callers that can account for external uses.
- Checking and source resolution still process every declaration before codegen, preserving missing-file, invalid-format and contract diagnostics. At the start of codegen, `program_strip_unused_assets` in `asset_usage.c` follows the checked emitted program and compacts only `program.assets`, retaining source order and leaving declarations and name bindings intact. Removed declarations contribute no generated asset globals/loaders, pack entries, asset fingerprints or `game_info` watch paths/model-identity flag. Existing pack writing removes a stale pack when the retained list is empty.
- The traversal retains references in every emitted function/method, including uncalled helpers, constants and defaults, settled and written call arguments, operators, shader samplers/data, typed mesh/skinned vertex defaults and generated container/Decode defaults. It roots generated type helpers conservatively, so an otherwise unused type's default can keep an asset. It does not attempt routine reachability or remove individual entries from a referenced glob collection. An iterative pointer-visited work list handles shared expressions and cycles without recursive expression walking.
- `tidec_asset_usage` passed all seven focused native cases: checked defaults/helpers/operators/shaders/collections, opt-in versus default codegen/pack/watch/model identity, unchanged invalid unused-image diagnostics, runtime Decode and implicit defaults, stale-pack/API removal, header stability across unused animation contents, and a shared/cyclic expression graph with 100,000 levels. A measured game with unreferenced media reduced its pack from 1,205,632 to 16,560 bytes, retaining exactly its referenced `keepPixels` entry; unused URL/path/content markers were absent from the shipped artifacts. The same option is used by the CLI and CMake release gates; their integration validation is recorded in the CLI and Tide-program notes.

## Media format audit and expansion (2026-10-10)

- Build-time image assets now accept JPEG/JPG, TGA and BMP through `image_files.c`, alongside the existing PNG and PSD readers. The helper owns free-compatible `tide_color` pixels; all existing Texture/Grid2<Color>/list/map/cubemap packing is reused. Strong signatures take precedence over the source name; TGA uses a case-insensitive source suffix, including URL paths before queries/fragments. Runtime `grid.Decode` remains PNG-only. New imports are capped at 16384 pixels per side and 512 MiB source/decoder-plus-output memory; profiles and JPEG EXIF orientation aren't applied.
- **Import limits** (2026-10-26, from fuzzing; Provisional numbers, Claude's picks, as the owner asked for clear limits that are easy to change; `notes/memory-safety.md`, Remote assets). `image_files.h`: `TIDE_IMAGE_MAX_BYTES` (512 MiB of source, and of decoder plus output: the former private `IMAGE_MEMORY_LIMIT`) and `TIDE_IMAGE_MAX_JPEG_SCANS` (256: each progressive scan decodes the picture again; a 132 KB file of 2,000 scans took 26 s). `psd_files.h`: `TIDE_PSD_MAX_PIXELS` (8192², 1 GiB of colors; a ZIP composite under a megabyte could ask for 4 GiB; PSDs past it that imported before now fail, the one result change for valid files, while PNG imports keep 16384²). `fbx_files.h` names the byte, node, vertex and index limits it had, now counted across every node that uses a mesh (skinned vertices too), and adds `TIDE_FBX_MAX_TRIANGULATION` (2^32 in all, a polygon's corners squared summed; a 100,000-corner polygon took 3.5 s) and `TIDE_FBX_MAX_BAKED_KEYS` (2^24 over all takes, held through each bake's ufbx memory cap and checked after each: ufbx bakes a curve's whole range before Tide trims it to its take, and a 1.3 KB take baked 6M keys). A material part resets only the corners it used, not the whole corner map. Converted GLBs are byte-identical for every fixture. Tests: `image_jpeg_scans_and_psd_pixels_are_refused_past_their_limits`, `fbx_polygon_triangulation_work_is_bounded_before_cutting`, `fbx_portable_isnan_reads_a_low_word_of_0x80000000_without_overflow`. Not limited: a 1 KB JPEG can still ask for a whole 512 MiB picture (under a second), and the editor's metadata reader (`model_metadata.c`) keeps its own ufbx limits.
- JPEG/TGA/BMP use pinned stb_image 2.30 (`2c980bb59875b0d32144a71867fbdebb2f77cd20`), with only those decoders, no stdio or SIMD, budgeted allocations and structural preflight for unsupported encodings/truncated payloads. Vendored local fixes are recorded beside the source. Its license ships with the tools; no decoder enters generated games. PNG/PSD decoded-pixel semantics remain unchanged.
- Build-time Clip assets add MP3 and native FLAC through `audio_files.c` and pinned dr_libs (`dfe8377631000664666519fdb83da193fd8037f4`), using scalar build-tool decoders. Imported audio becomes IEEE float32 PCM WAV, retaining source rate/channels. The cache keeps converted bytes separately from original file bytes, frees both on invalidation, hashes the original source and leaves `List<byte>` imports unchanged. Existing WAV/Vorbis bytes stay encoded. Runtime Clip/mixer APIs are unchanged; only Vorbis streams long audio. Limits/variants and diagnostics are in `notes/audio.md`.
- Image/audio failures name the file, recognized format and reason, followed by accepted alternatives. PNG size/header/corruption, PSD variants, WAV codecs/channels/depth/truncation and Ogg Opus are distinguishable. Unknown source formats no longer pretend to be corrupt PNG or WAV. Asset-type errors describe images and include SkinnedMesh rather than advertising PNG only.
- Static/skinned glTF and editor metadata share required-extension validation in `tide/internal/gltf_extensions.h`: only required KHR_mesh_quantization is supported. Required Draco/EXT or KHR meshopt compression gives export guidance; optional compression may use valid core fallback buffers/accessors. Static attribute widths, nonfinite data, invalid modes/missing POSITION, invalid negative child indices and GLB headers/chunk bounds are checked rather than silently accepted. Malformed `.glb` paths select GLB diagnostics, including original URL suffixes. Failed OBJ/DAE/other skinned source formats give glTF2/FBX export guidance; recognized-content feature errors survive misleading suffixes.
- USD/Collada/Alembic/STL/PLY/Blender project imports, WebP/TIFF/SVG/animated images, HDR/EXR assets, DDS/KTX GPU compression and AIFF/Opus/AAC audio remain unbuilt. Embedded model materials/textures still aren't imported. Existing static versus skinned feature distinctions above still apply.
- Validation: importer unit and compiler diagnostic fixtures, native/web asset pack round trips and language-server completion/diagnostic regressions cover the added formats and unchanged raw bytes. Final suite results are recorded after validation below.


Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Tech stack

- **FBX assets** (2026-10-10, requested by the owner): binary and ASCII `.fbx` files work with static `Mesh<V>`, animated `SkinnedMesh<V[, Contract]>` and standalone `Animations`, including singleton/list/map declarations and build-time URLs. `compiler/src/fbx_files.c` converts a memory-loaded ufbx scene into a temporary self-contained GLB, then the existing static/skinned decoders produce the ordinary packed data. There is no FBX parser, converter executable or new representation in a game; runtime `SkinnedMesh.Decode` still accepts self-contained GLB only. Static imports take rest geometry and ignore deformation and animation.
  - Animation-only FBX files retain their hierarchy and clips without requiring triangles, matching the existing glTF skinned import contract. `SkinnedMesh` and `Animations` use the same skinned conversion/cache, while static `Mesh` still diagnoses a file without drawable triangles. Specification-written ASCII/binary meshless fixtures in `fbx/clips/`, generated by `fbx_make.mjs`, cover identical canonical data, empty geometry, named clips, metadata and static rejection in `tidec_fbx_files`; `animations` imports them as singletons/lists/maps and samples owned copies after pack unload and across snapshots/serialization.
  - ufbx v0.23.1 is pinned at `26a482ae66871d7de36eb722aa060bce95bce274` in `compiler/third_party/ufbx/`. `tide_fbx` links only the native build tools, with filesystem access disabled, embedded content ignored, strict IEEE arithmetic and upstream's portable fdlibm math (`UFBX_EXTERNAL_MATH`, `UFBX_STANDARD_C`); its license notices ship in `licenses/ufbx/`. The reader converts source axes and units to right-handed Y-up meters before the existing X reflection. Polygon triangulation, normals, tangents, two UV sets, vertex colors and material base colors use the existing vertex-field rules. Geometry and inheritance transforms can introduce unnamed helper nodes; their hierarchy is retained by the skinned importer and matched by editor metadata.
  - Linear and single-influence rigid skinning retain all positive weights and each skin's inverse-bind palette. Unweighted vertices follow the mesh node. Animation stacks are trimmed to their playback range, shifted to start at zero and baked to LINEAR transform channels at 60 samples per second where resampling is needed, retaining authored keys. Stepped limits use adjacent float timestamps, so no representable float lies in the transition. Morphs, vertex caches, dual-quaternion skinning and multiple skin deformers on one mesh are refused for skinned imports. Textures and materials beyond base color remain outside mesh import. Source/parser/result/conversion budgets are 512 MiB, with 128 hierarchy levels and bounded node, geometry and key counts; excessive or invalid input reports a diagnostic.
  - Tests: `tidec_fbx_files` covers ASCII/binary equivalence, units/axes, mirrored transforms, UV/color conversion, five influences and inverse binds, named clips, real Blender/Maya exports, exact-sized cuts/corruption and same-size/time cache notifications. `fbx_assets` covers typed static/skinned assets and collections, contracts, deterministic match sampling, draw-list capture, snapshots/serialization and runtime FBX rejection on native and web. `tide_cli_assets` checks ASCII FBX at plain/query/fragment URLs, one fetch per complete URL, ETags and offline reuse. Editor metadata and completion tests are recorded in `notes/editors.md`.
  - Real animated Maya skinning is compared at five times against ufbx's independently evaluated scene, through Tide's actual pose sampler and skin palettes, within 10 micrometers. Baking must finish before trimming and shifting a take's time: ufbx's early `trim_start_time` changes when inherited-scale helpers are evaluated and moved this fixture's surface by 2.98 mm. The importer now trims the complete bake itself. Format detection gives GLB magic priority and recognizes an ASCII FBX header only at a top-level line outside strings/comments, preserving glTF files whose names contain `FBXHeaderExtension`.
  - Local ufbx patches are documented in its vendored README: the standard-C inflate fallback uses `memmove` for overlapping short-distance copies; portable math uses defined unsigned bit operations and bounded exponent multiplication instead of negative signed shifts. Linux ASan/UBSan exposed both cases on real Maya fixtures; the importer and metadata tests pass with both sanitizers enabled. Windows and sanitized Linux tidec produced byte-identical FBX packs (SHA-256 `865c3c818c69d61ee29cd6987191aea84f9c39645dd444e25584f23132ccfbe0`) and generated headers. The packed match hash is `2430772888184797451` on native and web.
  - Final validation: all 1,988 web-debug tests passed, including WebGL/WebGPU. Native debug passed 2,014 of 2,015 tests initially; the unrelated `last_light_browser_process` fixture failed its inherited-stderr precondition before calling `stopBrowser`, then passed on isolated retry. The same precondition is intermittently reproducible without parallel tests; no unrelated source was changed by this task. The subsequent merge from `origin/dev` included its fixture fix; native's cleanup test and web's cleanup/WebGL/WebGPU tests passed after merging. Packaged and installed Tide built this FBX game for desktop and web and ran three hidden native frames. All 2,434 packaged files, including the new license notices, are verified against the local installation after replacement. Other host platforms remain for a manually requested CI run.
  - Integration with the concurrent OBJ diagnostic change retains its guidance and adds FBX to the accepted skinned formats in compiler/editor messages and expected diagnostics. After merging, all 118 scoped native tests and 111 scoped web tests passed (assets, skinned meshes, FBX, metadata and the complete language-server suite where available). The package and installation were rebuilt and reverified for the merged implementation.

- Asset cache invalidation preserves the filesystem's subsecond last-write time (2026-10-09): `folder_file_info` returns nanoseconds on Linux/macOS and the existing FILETIME on Windows. File bytes, decoded images/models and external glTF buffer dependencies compare this opaque timestamp and file size. Whole-second POSIX timestamps previously reused stale data after a same-size edit within one second even though the CLI watcher detected the change. `tidec_asset_cache` fixes timestamps within one second and checks text/bytes and static/skinned external-buffer reloads; the package's `cache_test.mjs` forces the same case through web hot reload and checks the generated model identity.
  - Validation: the three cache regressions passed on Windows and Linux under WSL; an isolated Linux executable using the old `folders.c` failed all three with stale bytes, geometry and hashes. The full native debug suite passed 2,005 tests and web-debug passed 1,979, including WebGL/WebGPU; the installed CLI's `--watch-only` regression passed. macOS compilation remains for a manually requested CI run.

- Packed maps use format version 2: each entry starts with a positive signed 64-bit insertion order, `1..count`, as the runtime's maps do. `tide_pack_map_of` and `tide_pack_mesh_map_of` initialize the map's last order from that count; texture and clip maps retain the orders when their values are expanded at load. Copies can then add entries and resume a cursor without reusing old orders. The loader rejects version 1 and leaves a previously loaded pack usable. `tests/test_pack.c` traverses text/grid, texture, clip and mesh maps through `tide_map_next`, adds to a copied clip map, and checks rejection of the old layout. Mesh entry padding stays zero in the builder.

- Local files are `File`, `Directory`, `Path`, and the read-only runtime properties `Application.persistentDataPath` and `Application.temporaryCachePath` (`docs/game/files.md`). File contents are UTF-8 text or `List<byte>`; `byte` is an unsigned 8-bit scalar. File/directory operations, application paths and `await Storage.Flush()` are local only, including functions that reach them; their results reach the match through input. `Path` is pure. Whole-file operations fail with `IOError`, except `Exists`, which is a bool. Directory results are sorted full paths, with recursion off by default; directory creation creates parents, writes do not, and recursive deletion is explicit. Relative paths use the persistent root. Save identity is the stable `appId`, never the window title. Native files use the OS; browser files are an in-memory filesystem loaded from IndexedDB before startup, with automatic background commits and an awaitable flush that confirms preceding changes committed. The page keeps its files and commits across hot reload. Storage failures are reported, and failed commits retain dirty data for retry; temporary storage stays usable when persistence is unavailable. Open file handles and streams are outside this first API. The renderer keeps Vulkan's pipeline cache in the temporary cache folder (`pipelines.vulkan`, `notes/rendering.md`, Vulkan). The standard host names the game for that whether the game uses files or not (`tide_platform_name_game`, called by `tide_run_bind_io` before the game's I/O is bound, and by `tide_platform_io_init`). The folder is made only when the cache is first written, so a game that never uses files gets no persistent folder from it.
  - A browser save is one IndexedDB transaction, and the saves of a page go one after another (`saveFiles` and `tree.chain` in `platform/web/tide.js`). A change queues a save for the end of its turn, and a flush is a save too, so a game that changes files and flushes before it returns to the browser asks for two. Each takes what's dirty when its turn comes, never when it's asked for: the second comes once the first committed and finds nothing left, so it opens no transaction. Copied when asked, both wrote every changed file, and the flush was done after the first with the second still on its way, which is what a page that then went elsewhere left behind. What follows from taking it then: a save commits the files as they are when its transaction opens, so a flush may also commit what changed after it was asked for and before its turn, and reports that transaction's failure; what a failed save left dirty, and a file changed again while its save was on its way (its entry in `tree.dirty` is then another object than the one the save wrote, so the save's end leaves it), go with the next save. `tide_platform_web_io` counts the transactions that write in each of its phases, and its `busy` phase changes files while the page holds a save's transaction open.

- Assets are built (agreed 2026-10-06; `docs/spec.md`, Assets, where the picks beyond what was agreed are Provisional; `docs/game/assets.md` for users): `asset Grid2<Color> hero = "art/hero.png";` is a declaration (`DECL_ASSET`) that resolves to a plain type, read-only, read by name wherever a value of its type is, in match code too, as every machine has the same pack. What's built, and what code relies on:
  - **The language.** `asset` is a declaration word (`parse_asset` in `compiler/src/parser.c`): a type, a name, `=`, an expression and `;`. The type is `string` (UTF-8), `List<byte>` (any file) or `Grid2<Color>` (a PNG or a PSD composite), or a `List<T>` or `Map<string, T>` of one, for a glob's matches or for the one file a path names, which is the one place a list or a map holds a grid (`asset_type` in `check.c` resolves the type at `PLACE_ASSET`, where `element_ok` takes one, and reads its shape off the tree; see A type is read as a tree, and Containers nest, under Language). The path is a constant text (`is_constant`, `text_literal`), relative to the declaring file whatever its namespace. A name is looked up as `NAME_ASSET` after `NAME_CONST` (`use_asset`: `BIND_ASSET`, with the declaration in `expr.constant`); `check_writable` refuses it, `local` and `virtual` are refused, and a frag samples one by its bare name only (`check_grid_sample`, `ex_sample` in `shader.c`: `shader_sample.asset`, `shader_texture.asset`), never an element of a list of them.
  - **Resolving** is `compiler/src/asset_files.c` (tidec_core, so tidels and tide share it; `folders.c` moved there from `compiler/lsp/`): a glob (`*` within a name, `**` across folders, `?`; `asset_glob_match`, which tidels uses too) walks from its fixed prefix and sorts matches by their written paths' bytes, which are a map's keys. The walk only goes into folders the pattern could match in (`glob_reaches`, through `folder_find_files_in`): each folder's name has to match the pattern's part in its place, up to a part with a `**`, so `*.png` reads one folder's entries and none of the folders under it. Walking everything under the prefix, as it first did, walked the whole disk for a file at its root, which is where the language server's tests have theirs (`file:///test.tide`): `tidels_tests` took 70 seconds on Windows and never ended on Linux. A file is read once per process until its size or time changes (`read_at`), or an explicit editor notification marks its path or a parent folder changed (`asset_files_changed`). Notifications mark existing reads without freeing them until the next resolution, and also invalidate meshes whose external buffer paths changed, even when size/time match, hashed with SHA-256 and, for a grid, decoded with `tide_png_decode` or `psd_file_read`; a URL is fetched with curl through `popen` once per process (`fetch_url`) into `<game>/.tide/assets/urls/<sha256 of the URL>` beside its `.etag`, asked for again with `If-None-Match` (a 304 keeps the cache; the header goes to curl in `<hash>.request`, `-H @file`, never on the command line, since an ETag is the server's and a quote of it would end its argument on `cmd.exe`: `notes/memory-safety.md`, Remote assets), and taken from the cache when the server can't be reached; with no game folder (tidec on its own) a temporary folder stands in. The diagnostics say what tidels says, so its fixes attach to them: `there's no file at '<path>' (<abs>)` with a note naming a file of the same bytes (by the hashes `.tide/assets.sha256` keeps, which tidels writes) or files of that name, in order of their paths (`candidate_order`, as the server sorts its own: a system lists a folder in whatever order it keeps it in, which is by name on Windows and not on Linux), `'<glob>' matches no file`, `'<path>' isn't a PNG or a PSD that decodes`, `'<path>' isn't UTF-8 text`, and one each for a glob where the type holds one file, a type an asset can't be, a path that isn't constant. **One file where the type holds many is no error** (the owner, 2026-10-08, of `asset List<Clip> shot = "sfx/shot.wav";` and `asset Map<string, Clip> shot = "sfx/shot.wav";`: "should still be valid, i know its only one entry but this allows to change the scope of the search without changing the signature"): `asset_resolve` gives a value that's `many` with one item, whose key is the path as written, which is what a glob's key for that file is, so `write_pack` makes the list or the map of one with the code a glob's goes through, and nothing in codegen or the loader has a rule for it. A URL is taken the same way, its key the URL (Claude's pick, Provisional in `docs/spec.md`, Assets: the owner spoke of files). A `Cubemap` still wants its glob, and `[Raw]` still refuses a list or a map. To the editor such a path is a single file's, as everything there goes by whether the path is a glob, never by the type: a rename rewrites it, a missing one is found by its bytes, and completion in it offers the globs too. Tests: `compiler/tests/e2e/audio` (the owner's two lines, played by place and by key), `assets` (text, bytes and grids, a key through a `const string`), `asset_textures`, `tide_cli_assets` (a URL, fetched once for three declarations), `lsp_asset_one_file_for_a_list_or_a_map`. tidec takes `--game <folder>` (tide passes the game's) for the hashes and the cache.
  - **The pack** (`tide/pack.h`: the builder in `engine/src/pack.c`, which tidec compiles too with `png_read.c`, `sha256.c`, `page.c` and `net.c`; the loader in `pack_load.c`) is one region laid out as a heap lays blocks out, so the code that reads text, lists, maps and grids reads it unchanged: a text or `List<byte>` is a block with a NUL after its bytes, a text's saying its characters in its header and a list's its room (below); a `Grid2<Color>` its directory and the chunks that have a color in them, with the file's hash in the directory's header as the version the draw list tells it by (`tide_pack_grid_version`, which `tide_draw_grid_texture` reads for a pack grid); a glob's list a block of handles; a map its entries and an index hashed as the game's lookups hash. A file is in the region once for each kind of block it's read as (by its content's SHA-256 and that kind), and a declaration is an entry by the SHA-256 of its qualified name and its kind. **A file's text and its bytes are one block only where each byte is a character** (2026-10-08): a block's header has two words, which a text reads as its bytes and its characters (`tide_text_view`, for every `tide_text` handle: a list's elements, a map's keys and values) and a list as its count and its room (`b`, of which a read only asks `TIDE_LIST_CHUNKED`, but which everything that writes a list takes to be no less than the count), so one block can't say both of text with a character of several bytes. Shared, as it was, such a text's `.length` was its bytes wherever it was read through a handle (`oneRules[RULES].length` 27 for a file of 26 characters), and right only for an `asset string` alone, whose characters are its entry's `extra`. So text goes through `tide_pack_string` (`engine/src/pack.c`: `pack_item` for `ASSET_TEXT`, and a map's keys, in `compiler/src/codegen.c`), which gives the bytes' own block (`tide_pack_bytes`, shared as `TIDE_PACK_BYTES`) for text of a byte a character, and otherwise a block of the text's own with its characters in `b`, shared as `TIDE_PACK_TEXT`: a file with characters past ASCII that's declared as text and as a `List<byte>` is in the pack as each. A pack of text that's all ASCII is the bytes it was, and the file format's version didn't change. New code that puts text in the pack calls `tide_pack_string`, never `tide_pack_bytes`. At load (`tide_pack_load`) the region is at the top of the scratch area's offsets, from `TIDE_PACK_BASE` (512 MiB; `tide_pack_holds`), so a handle into it is a scratch handle (`TIDE_IN_SCRATCH`), which `tide_scratch_at` reads through once each thread has mapped the region into its slots (`tide_pack_use`, which generated code calls beside `tide_text_use` and in `tide_prepare`); the scratch area stops short of it while a pack is loaded. A grid with no heap reads its chunks the same way (`tide_grid_chunk_at`), cached against a heap that never moves (`tide_pack_still`). No world holds any of it. Generated C has a global per asset (`tide_asset_<name>`), `tide_assets_load(bytes, size)` (declared in the header with `TIDE_HAS_ASSETS`, so only games with assets change their header), which finds each name's entry, says on stderr which a pack from another build lacks (they read as empty), and `tide_pack_use` where it sets the thread's heaps.
  - **Where the pack is.** tidec writes `<out>/<name>.pack` beside the generated C (nothing for a game with no assets), and with `--pack-c` the same as `<name>_pack.c` (`tide_game_pack`, `tide_game_pack_size`), which `tide_add_game` gives `HOST` programs so a test loads it itself; a game with a window gets `<target>.pack` beside its program (`cmake/copy_pack.cmake`), inside its page (`tide_web_page ... PACK`, `web_page.mjs`: `TIDE_PACK` as base64) and in its APK (`tide_android_app ... PACK`, `tide_apk --pack-if-any`: `assets/game.pack`). `tide build` does the same (`place_pack`, `make_page`, `apk_desc.pack`). The standard host loads it before the first frame (`tide_run_load_assets` in `tide/host.h`, from `tide_platform_pack`: `tide_window_pack`, which on desktop reads `<program>.pack` by the program's own path, on the web asks the page (`pack_size`, `pack_copy`) and on Android the APK's assets) and hands it to each reloaded library (`tide_host_game.assets`). Under `tide run` tide writes the pack into the run's folder as `pack-<n>` with each build and again when a file an asset names changes (`asset_stamp` over `game_info.asset_paths`, `repack`: the front end runs, nothing compiles or links), which the host loads between frames (`check_pack` in `platform/src/reload.c`), and `tide run --web` serves (`/build` says the newest pack, `/pack-<n>`; the page fetches it and calls the program's `tide_pack_changed`): a saved image is in the running game within a moment, as data.
  - **Tests:** `tests/test_png.c` (the reader against the writer and hand-made PNGs), `tests/test_pack.c` (built, loaded, read through the list, map, grid and draw-list code; text of two-byte characters alone, in a list and as a map's keys and values, with its characters, beside the same content's bytes), `compiler/tests/e2e/assets` (each type, globs as lists and maps, one with a `*` for a folder, two names of one file, a path through a constant, the `.length` of a list's and a map's text whose characters aren't a byte each, with the same file's bytes, a grid drawn and sampled in a frag, match code, a world packed and back, a stale pack; its files are `-text` in `.gitattributes`, as an asset is its file's bytes, line endings and all, and CI's Windows checkouts would make a text file's Windows') with `assets_plain` (the same game with none: the same empty world hash, `assets_same.h`), `compiler/tests/errors/asset_*.tide` (one a diagnostic), `tide_cli_assets` (`compiler/cli/tests/assets_test.mjs`: the pack, its C, and URLs against a server on this machine: fetched once, asked with the ETag, built from the cache offline, an error uncached), and the docs' live example (`docs/examples/assets.tide`). A game with no `asset` generates the C it did, byte for byte (old and new tidec over every game's source list, 211 games).
  - **Not built here** (other cards): `grid.Decode` and `Web.Fetch` (built since, with the reader), command channels, and tidels' refactors (built since: renames rewrite paths, a moved file is found by its bytes, watchers per named file).
- **An asset can be a `Texture`**, an image that's only drawn (agreed 2026-10-07, of `asset Texture hero = "art/hero.png";`: "ah yes lets add `Texture` there"; built the same day; each rule is Claude's pick, Provisional in `docs/spec.md`, Assets, Texture assets). `Grid2<Color>` stays the image code reads. What's built, and what code relies on:
  - **The language:** `asset_type` (`compiler/src/check.c`) takes `Texture`, alone or as a glob's `List<Texture>` or `Map<string, Texture>` (`ASSET_TEXTURE` in `asset_files.h`, a PNG or PSD decoded as a grid's is), the one place `element_ok` lets a list or a map hold one. It's a `TY_TEXTURE` value like any other, so every rule of a `Texture` is its own with no code here: a draw's texture, a frag's `Texture` source, a blit's, a field or a local, and match code refused ("rendering resources belong to this machine"). A frag or vert samples one by its bare name (`check_grid_sample`, `ex_sample`: `shader_sample.asset`), never one of a list's.
  - **The pack:** `tide_pack_texture` (`engine/src/pack.c`) is a block of the image's width, its height and its pixels, a byte a channel, rows from the top (tidec makes them of the decoded colors as the draw list makes a grid's, `channel_byte` in `codegen.c`), shared by the file's SHA-256, whose first 8 bytes are the block's version (never 0). Kinds `TIDE_PACK_TEXTURE` (6; a list's elements' too, in `extra`) and `TIDE_PACK_TEXTURES` (7: a map of them). **In the file a list or a map of textures is handles**, as one of grids is, so a pack is the same bytes on every platform; Tide reads their elements as `tide_texture_ref`, whose layout is the platform's, so `tide_pack_load` lays those out after the file's region (`lay_textures` in `pack_load.c`, with room counted first, `textures_room`) and has the loaded entries name them.
  - **A `Texture` of the pack's** is a `tide_texture_ref` with no pixels and no target, and `image.version` a name: a number made of its declaration's name's hash and, for one of a glob's, its place in the list or its key in the map (`texture_name`). **Not what's in the image**, on purpose: what a field keeps of `hero` or `frames[2]` goes on naming that declaration's image when `tide run` loads the pack again with the file edited, where a name by content went blank. The loader keeps the images by that name in a hash table (`packed_texture`, `texture_place`), and `tide_pack_texture_find(named, &image)` gives the loaded pack's pixels, size and content version. A name the pack hasn't got (another build's, a glob that shrank) draws as the default `Texture`: no texture.
  - **The draw list copies nothing of it** (`pack_slot`, `pack_texture` in `engine/src/draw.c`): its texture is `TIDE_PIXELS_PACK`, told by the content's version (so two names of one file are one texture, and an edited file another, which the platform uploads), with `pixels` pointing at the pack's own and `capacity` 0, noted again each time a command names the image. So a frame's textures point into the pack that's loaded while it's recorded: **a list that drew one isn't rendered after another pack is loaded**, which the hosts keep (a pack is loaded between frames: `check_pack`, `tide_run_load_assets`). What frees a list's pixels goes through `free_pixels`, which leaves those alone, and `append_texture` hands the pointer on. The platform's `texture_for` is unchanged: it uploads from `pixels` when the version isn't what it has.
  - **Generated C:** `tide_asset_<name>` is a `tide_texture_ref` from `tide_pack_texture_of(e)`, a list's a `tide_list` of them and a map's a `tide_map` through `tide_pack_map_of`; a frag's sample by name is `tide_draw_texture_source(tide_draw, tide_asset_<name>)`. A game with no `Texture` asset generates the C it did, byte for byte (old and new tidec over every game's source list, 239 of them, with `--layout` and without).
  - **Editors:** a `Texture`'s path completes PNGs and PSDs (`collect_assets`), and its hover says what it is and how its declaration says it's sampled.
  - Tests: `tests/test_pack.c` (a texture alone, a list, a map, a hundred small ones, the list's borrowed pixels through an append and frames that drop them, the pack gone, loaded again with the image elsewhere, with the file edited, and with no such declaration; blocks cut short); `compiler/tests/e2e/asset_textures` (drawn by name, by place, by key, as a field's value and through a `foreach`, a key and a place the glob hasn't got, in 3D and into a target, sampled in a frag, the pack loaded again and an empty one); `compiler/tests/errors/asset_texture_*.tide` and `texture_list_not_asset.tide`; pass 10 of `tide_platform_shaders` (`platform/tests/shaders.tide` with `four.png`: pixels on OpenGL 3.3, WebGL 2 and WebGPU, whose device is then lost, so the image is uploaded again from the pack); `lsp_asset_textures_and_declared_sampling`.
  - Later additions: mipmaps, JPEG/TGA/BMP imports and automatic BC1/BC3 GPU compression are now built; see `notes/textures.md` and Media import expansion above. A `Texture`'s size or pixels in Tide remain unbuilt.
- **PSD image assets** (agreed and built 2026-10-08): the owner wanted native imports without Photoshop as a build or CI dependency. `Texture`, `Grid2<Color>` and a `Cubemap`'s faces accept the saved RGB composite of PSD version 1, alone or through their existing globs, lists and maps. `compiler/src/psd_files.c` and `psd_files.h` are build-time code in tidec_core, shared by tidec, tide and tidels; none of the reader ships in a game. The implementation is our own from Adobe's published file format, with ZIP using the existing `tide_inflate`, so there is no new library or license dependency.
  - **What decodes:** RGB, 8 or 16 bits per channel; raw, PackBits/RLE, ZIP and ZIP with prediction. Prediction accumulates bytes for 8-bit samples and big-endian words for 16-bit samples, separately per row. Color channels become floats by `sample / max`, with only basic IEEE operations. A negative layer count (including `Lr16`) or a merged-transparency marker (`Mtrn`, `Mt16`) identifies the fourth channel as opacity; otherwise extra channels are saved selections/spot channels, ignored for the image. Markers are independent of block order. The saved white matte is removed to give straight RGB, with transparent pixels clear black. PNG packing, declared sampling and `[Raw]` then work as before; the pack format and generated asset C are unchanged.
  - **Saved composite required:** Version Info resource 1057's `hasRealMergedData` flag refuses placeholder composites; a missing image body is also refused. The diagnostic explains saving with **Maximize Compatibility** enabled. Layers, text, effects, masks and smart objects are not rendered or extracted; only their already saved result is imported. CMYK, gray, indexed, 32-bit and PSB are refused with guidance. Color profiles are not converted: save RGB art as sRGB for matching display colors. Runtime `grid.Decode` remains PNG only.
  - **Bounds and memory:** headers and every section, resource and row length are checked against the supplied bytes. PSD1 tagged blocks keep four-byte lengths for both `8BIM` and `8B64` signatures. Older writers may omit the global-mask section, with or without tagged blocks after it; a partial length stays an error. Dimensions have PNG's 16384-per-side ceiling; raw/RLE sizes and a ZIP expansion bound are checked before allocating pixels. RLE cannot overrun a row; ZIP is streamed through one row, checks its zlib header and Adler-32, and rejects short or excess output. Failed decodes free their allocations and return an empty image. There is no full planar image copy beside the decoded colors.
  - **Integration:** `read_file.image` caches the PSD decode under the existing file size/mtime invalidation and source SHA-256. Saving a PSD triggers the existing asset repack path; URL decoding recognizes the signature independently of the URL cache's filename. Editor completion offers PSDs and format globs (`notes/editors.md`); users' instructions are in `docs/game/assets.md` and `docs/spec.md`.
  - **Tests:** `tidec_psd_files` checks pixels, opacity and unsupported files; exact-sized allocations cut at every byte and byte mutations exercise parser bounds, with malformed RLE/ZIP and omitted-global-mask regressions. `tidec_e2e_asset_psds` reads packed grids, lists/maps and textures, declared `[Raw]` sampling and repeated pack loads on native and web builds. `compiler/tests/errors/asset_psd_*.tide` pins diagnostic guidance. Checked-in fixtures are small specification-written PSDs generated by `asset_psds_make.mjs`. Nine real files from psd-tools' fixtures also matched an independent saved-composite reader, including six with Photoshop CS3/CC/CC 2017/CC 2018 metadata, 8/16-bit RGB, raw/RLE and merged transparency; the 8-bit saved planes matched Pillow too. These external files and tools were used only for verification and are not committed or build dependencies. Real Photoshop ZIP exports remain untested; both ZIP variants have synthetic pixel and malformed-stream coverage. The native (1920 tests) and web (1895 tests) suites passed after sequential retries of initial parallel-run failures; latest decoder changes passed focused native/web checks and editor tests, and the docs site built.
- **A field's default can name an asset** (built 2026-10-07; the owner, of `local Texture test = logo;` in a component, which was "default values must be constant expressions": "I feel like this should be possible?"; each rule is Claude's pick, Provisional in `docs/spec.md`, Assets, A field's default). `docs/game/assets.md` (A field's default) is the users' page. What's built, and what code relies on:
  - **The checker permits it.** `check_default` (`compiler/src/check.c`) sets `checker.assets_constant` around its `is_constant` and `check_constant_expr`, and `is_constant` then takes an asset's name as it takes a constant's, so an asset is a constant anywhere in the default's expression (a struct's value, `table.count`, an operator). A default is written out where it's given (`gen_value`), and an asset there is its global, `tide_asset_<name>`, as anywhere. The original field-default change needed no codegen change; mesh vertex defaults now require the loader's two passes described under Generated C below.
  - **So the value is read when the default is given, not while compiling**: at a spawn or an Add, in `tide_world_init` and `tide_local_init`, in a view's wrapper for local fields nothing wrote (`tide_fresh<n>`, `tide_attach_`), and in the layout's `defaults` function a hot reload fills new fields with. Text, bytes, lists and maps are copies the world owns (`tide_text_temp`, `tide_list<n>_copy`, `tide_map<n>_copy`, then `tide_own_`); a `Texture`, a `Clip` and a `Cubemap` are their declarations' names.
  - **The pack is loaded before anything asks a build for a default.** The standard host always did (`tide_run_load_assets` before `tide_run_begin`); `swap` in `platform/src/reload.c` now hands a new build the pack before `carry_over` and before starting over, where it did after. `tide_platform_reload` publishes a second build of another layout while the game runs and checks it has the pack as its local state is made (it fails with the old order). A C host does the same: `tide_assets_load` before `tide_world_init` and `tide_local_init`, or the defaults read empty.
  - **Nothing else that takes a constant takes an asset**, and that had a hole: `is_constant` passes any chain of names (`names_only`), so `Art.rules` and `rules.length` reached `use_asset` in a `const`, a setting, an asset's path (which then had no file, with no error) and a parameter's default. `check_constant_expr` sets `checker.constants_only`, and `use_asset` refuses there unless `assets_constant` is set, with a note that says where one goes; `check_constant` clears `assets_constant` for the constant it checks. A plain asset name in those places is each one's own "must be a constant" error, as before.
  - **A `Grid2<Color>` field's default that's an asset's grid** is "a grid can't be copied" (`check_default`); into a `Texture` field it's the grid's conversion.
  - **Editors:** completion after a field's `=` offers assets beside constants (`complete_assets` in the default's branch of `compiler/lsp/analysis.c`); hover, references and the lens come from the `BIND_ASSET` the checker leaves.
  - **Not built:** one of a glob's as a default (`frames[0]`, `icons["star.png"]`: indexing is no constant expression, and code that indexes a list wants its caches declared); an asset as a parameter's default (a rendering resource takes none, by an older rule).
  - **Tests:** `compiler/tests/e2e/asset_defaults` (the match's text, bytes, a list and a map as its own copies, packed and back; a local singleton's, a struct's own, a struct value's, a clip and a `Sound`'s; a component's local fields with no row and in the row a view's write made, drawn; declared sampling carried; a grid asset as a `Texture`); `compiler/tests/errors/asset_default_*.tide`, `asset_as_constant*.tide`, `asset_path_names_asset.tide` and `asset_param_default.tide`, a file a diagnostic; `lsp_asset_textures_and_declared_sampling` (no diagnostic, hover, completion, the refusal); `tide_platform_reload` for the order.
- **An asset can be a `Mesh<V>`**, a model from a glTF, OBJ or FBX file (FBX added 2026-10-10; agreed 2026-10-08 from a proposal: the owner picked glTF and OBJ, `Mesh<V>` over the game's vertex struct filled by its fields' names with its lists read by any code, both ways of drawing it, and a file's parts as one mesh; built the same day; each rule beyond those is Claude's pick, Provisional in `docs/spec.md`, Assets, Mesh assets). `docs/game/assets.md` (Meshes) is the users' page. What's built, and what code relies on:
  - **The reader is tidec's alone** (`compiler/src/mesh_files.c`, `mesh_files.h`; nothing of it is in the engine, a game or a page): glTF 2.0 and Wavefront OBJ from their specifications, and FBX through the build-time importer above, with a JSON reader, a base64 reader and a number reader of its own (tidels' JSON reader is one tree at a time, and analysis runs while a message's is alive). Whatever the file, it gives one `mesh_file`: every stream there for every vertex (position, normal, uv, uv2, color, tangent), the indices, what the file itself had (for the editor's hover), and a SHA-256 of all of it, which is the mesh's identity whatever files it came from.
  - **What it does to a file's numbers**, in `add_primitive` and `add_triangle`: a node's matrix (column after column, as glTF writes one), then `x` negated; normals through the matrix's cofactors by its determinant's sign, then normalized; tangents through the matrix, with `w` turned once for the negated `x` and once more under a node that mirrors; triangles wound the other way unless the node mirrors (`determinant < 0`); a strip's and a fan's triangles made in the file's sense first. A part with no normals gets three vertices a triangle with the way its front faces (`add_part`): Tide's front is the side `(b - a) x (c - a)` points to. Negations are written `0.0f - x` and `normalize3` adds 0, so a zero comes out a plain one whatever sign arithmetic left it. A glTF's color times its material's `baseColorFactor` goes through `to_display` (the sRGB curve, with `tide_pow_f`: `engine/src/math.c` is among the engine sources tidec compiles for it).
  - **The same bits whichever machine builds**, since match code reads a pack's vertices: `parse_double` takes 19 digits and gets there by one multiplication or division by a power of ten a double holds exactly (up to 1e22), or several, never by the C library's `strtod`; the file has `#pragma STDC FP_CONTRACT OFF` beside the build's `-ffp-contract=off`; nothing else is more than `+ - * /` and `sqrtf`.
  - **A glTF primitive keeps only the vertices its triangles use** (fixed 2026-10-08): `add_primitive` reads and validates its indices before selecting accessor entries in their original order, then remaps those indices. `read_floats` decodes only selected entries, with the original accessor counts, whole-view bounds and every sparse selector still checked; a sparse override is applied through the original-to-compact map. Transforms, normals, tangents and material color conversion likewise run only for selected vertices. Sharing stays within a normal-bearing primitive, and each material/node stays separate; a part without normals still gets three vertices per triangle. Triangles, strips and fans keep their winding, and incomplete trailing triangles stay ignored. Previously every normal-bearing primitive copied the entire accessor, which material primitives may share: a 60,000-vertex model split across 20 materials packed 1,200,000 vertices, of which 1,140,000 were never indexed. With the native debug compiler, its pack fell from 48,240,144 to 2,640,144 bytes and its median import time over three runs from 4.02 to 0.29 seconds; its expanded indexed vertex bytes kept the same SHA-256 before/after and as a file with narrower accessor ranges. `mesh_gltf_keeps_only_referenced_vertices_of_each_primitive` compares all output streams, indices and mesh hashes with compact controls: strided attributes, sparse positions on used and unused entries, repeated/nonmonotonic indices, different material colors, a mirrored nonuniform transform, and parts with or without normals. `mesh_gltf_drops_vertices_outside_complete_triangles` pins trailing entries of indexed and nonindexed triangles. Focused native build and `tidec_mesh_files` passed; the aggregate native/web suites cover the reader through the existing asset and drawing tests.
  - **Nothing a file says is trusted to be a place**: tidels reads a game's files at every keystroke. `view_at` checks a view against its buffer and an offset against the view, `read_floats` and `read_indices` check the last element's end against the view's bytes (a stride past 255 is refused first, so that product can't overflow), an index against the vertices, a sparse accessor's indices against its count; a node is placed once (`placed`, with a depth limit), JSON nests 64 deep at the most, an OBJ's face names places that are there. `tidec_mesh_files` (`compiler/tests/mesh_files_test.c`) reads a .glb, a .gltf and an .obj cut at every length and with every byte spoiled three ways, each from an allocation of exactly its size.
  - **Sparse accessors apply to triangle indices too** (fixed 2026-10-08): `sparse_of` checks the selector and replacement views for both vertex attributes and indices, then `read_indices` applies the replacements over the dense indices or their initial zeros. It formerly ignored sparse index data, collapsing a sparse-only triangle to `[0, 0, 0]` and leaving dense overrides unapplied. `mesh_gltf_reads_sparse_index_accessors` covers every byte/ushort/uint selector and value width, both initial forms, and the same mesh hash as dense data; `mesh_gltf_refuses_invalid_sparse_index_accessors` covers out-of-range selectors, excessive counts, short views and missing or invalid descriptors. The sparse fixture is also cut and spoiled from exact-sized allocations. The end-to-end `asset_meshes` fixtures use dense index replacements in `strip.gltf` and zero-initialized sparse indices in `inline.gltf`, with their original geometry and checks unchanged, so the pack and match reads exercise both on native and web builds.
  - **Resolving** (`compiler/src/asset_files.c`): `ASSET_MESH`. A file is read as a mesh once (`read_file.mesh`), until it changes or a file it was read with does: a `.gltf`'s buffers can be files beside it, which `read_side` reads through `read_at` and notes (`sides`, `sides_same`). **`read_at` can move the list of files**, so `read_item` finds the .gltf's entry again by its path after the mesh is read. `asset_match.mesh` is by value for the same reason, and `asset_match.sides` are what `write_pack` adds to `game_info.asset_paths`, so `tide run` packs again when a `.bin` is saved. What a file is where its first bytes don't say (`glTF` for a .glb) is its name's to say, and the name is the path as written: a fetched URL's file has another.
  - **A mesh URL's format comes from its path** (fixed 2026-10-08): `ends_with` in `mesh_files.c` uses an HTTP(S) URL's path ending before its query or fragment, so `tree.obj?revision=1` and `tree.gltf#model` read as the same formats as their plain URLs. A URL's host, query or fragment can't supply the suffix. Local names keep their literal suffix, including `#` and `?`, and GLB's magic still takes precedence. Fetching, the URL cache and a map's key keep the complete URL. `mesh_file_names_use_url_paths_and_keep_local_suffixes` pins those distinctions. `tide_cli_assets` serves OBJ, glTF with an embedded buffer and GLB at plain, query and fragment URLs, checks full URL map keys and cache hashes, one fetch per complete URL, ETag reuse, equal packs and offline cache reuse.
  - **The type** (`compiler/src/check.c`): `TY_MESH`, with a `DECL_MESH` for each vertex struct (`mesh_of`, `program.meshes`), its form in `generic_forms` (`GENERIC_VERTEX`), so the parser reads `Mesh<...>` as it reads `List<...>`. `resolve_mesh` says where one can be written (not a field, a constant or a pattern), `element_ok` lets a list or a map hold one in an asset's type alone, and `mesh_vertex_ok` checks the struct at the declaration, which is after every struct's fields have their types. **A game's own type named `Mesh` is still its own**: only `Mesh<...>` is the built-in, and `Mesh` alone names it only where no type has the name.
  - **A `Mesh` value is two handles into the loaded pack** (`tide_mesh_lists` in `tide/mesh.h`), never copied and never written: `check_member` gives `vertices` and `indices` as plain list types, `check_writable` refuses a write through one whatever holds it (a `mut` local names the pack's too), and `holds_mesh` keeps one out of tasks (`check_var`, `check_signature`), as a pack is loaded again between frames. That's also why no field holds one: what a `Texture` of the pack's does for that (naming its declaration, looked up at each use) isn't built for meshes.
  - **A draw's Mesh** (`check_mesh_draw`): where the first argument is a `Mesh`, the arguments become its two lists, each an `E_MEMBER` of the same expression (`expr.mesh_part` 1 and 2), and the rest of the checker and codegen see the lists they always did. What the call wrote stays in `expr.written` (`settled`), which the editors and `assigned.c` walk. **The Mesh's expression is one node under two**: `collect_ordered` in `codegen.c` collects what keeps its order in it for the first only, and forces hoisting where there's any, so `Draw.Mesh(all[art.turn++ % 2], ...)` steps once and both lists read the same temporary. What walks `args` of a mesh draw some other way visits that expression twice; a call in it with no side effects is written, and called, twice (`Pick(0)`).
  - **Generated C:** `tide_asset_<name>` is a `tide_mesh_lists`, a `tide_list` of them or a `tide_map`, found with `tide_pack_find_mesh(name, kind, layout)`; `mesh.vertices` is the struct's member. The fields no file fills are given their defaults after each load, by a loop over `tide_pack_mesh_open` that assigns from the struct's own default value (`gen_value`), only where the struct has such a field with a default. **Loading has two passes** (fixed 2026-10-08): `gen_assets` initializes every asset global and applies declared texture sampling before it gives any mesh vertex a default. A single mesh, a list and a map can therefore read an asset declared later; previously a first load read its zero-initialized global and a replacement pack read the previous pack's value. `mesh_layout` is the vertex struct as text (its size, each field's name, type and place, and that default value), and its hash is both the layout number an entry says and part of what a mesh's blocks are shared and told by (`pack_mesh`), so a default expression that changes is another mesh to the draw list. Games with no mesh vertex defaults retain their generated C.
  - **Vertex defaults refresh the stored version** (fixed 2026-10-08): after each default-fill loop, generated code calls `tide_pack_mesh_updated(vertices, vertexSize)` in `pack_load.c`. It hashes the final vertex bytes once into the block's existing nonzero version; zero-filled padding remains the builder's. A default that reads another asset can change while the model file and layout stay the same. Previously the loaded vertices changed but `list_version` still saw the model's old identity and the draw list kept its previous vertex copy. Shared blocks, identical loads and different dependency bytes producing the same defaults keep the same version. The draw path still reads only the block metadata; counts, stride and indices already join its cache key, so no source-version mixing or per-draw vertex hashing is needed. Games without mesh vertex defaults retain their generated C. `mesh_assets_refresh_drawn_defaults_when_another_asset_changes` draws a single mesh, a list and a map with a float default from later text, verifies copied vertex values after dependency-only changes and restoration, and checks cache reuse for repeated loads and equal computed defaults.
  - **The pack** (`engine/src/pack.c`, `pack_load.c`): `TIDE_PACK_MESH` (11: its vertices' block, its indices' in `size`), `TIDE_PACK_MESH_LIST` (12: a block of `tide_mesh_lists`) and `TIDE_PACK_MESHES` (13: a map's entries, each a key and a mesh); `extra` is the layout number for all three, so a map's count is its block's. A mesh's two handles are the same bytes on every platform, so a list or a map of them is read where the file has it, with nothing laid out at load, where textures' and clips' are. **A mesh's block says what's in it after its elements** (`versioned_list`): its header's `next`, which only a heap's free blocks use, is how far from the block's start 8 bytes of version and the element's size are. `check_meshes` makes a mesh whose blocks aren't whole an empty one as the pack is loaded, in lists and maps too, and `tide_pack_mesh_of` gives none for vertices of another size than the code's.
  - **The draw list tells a pack's mesh by its file and final defaults**: `list_version` in `engine/src/mesh.c` asks `tide_pack_list_version` first for a list in the scratch area. Without it a pack's list was hashed whole at every draw, as a list made along the way is: megabytes a draw call for a model. So a mesh is filled into the list once, drawn from a view a thousand times a frame costs a block's header each, and a pack loaded again with the file as it was is the mesh the list and the GPU have.
  - **Hot reload** needed nothing of its own: `tide run` packs again when a named file changes, the mesh has another version, and the draw list makes another; the one nothing draws goes as any does.
  - **Editors:** `notes/editors.md`, Assets, the editor side.
  - **Tests:** `tidec_mesh_files` (the numbers it reads, a .glb, a .gltf and an .obj cut and spoiled anywhere, what a description says that its bytes don't bear out); `compiler/tests/e2e/asset_meshes` with the files `asset_meshes_make.mjs` writes into `e2e/assets/meshes/` (every field by its name against values worked out by hand: a node that moves, nodes in nodes that scale, one that mirrors, one that turns; bytes as positions, a strip, a fan, a sparse accessor, a buffer beside the file and one inside it; a part with no normals; an OBJ; the same file as three structs; a glob as a list and a map, one file for a list; a Mesh in a local, a parameter and a result; the fields no file fills; a copy changed with the mesh as it was; the draws as runs, one mesh however it's named, a step in the Mesh's expression run once, the pack loaded again; a pack of another layout). `mesh_assets_defaults_read_forward_assets_of_the_current_pack` checks single/list/map meshes whose vertex defaults read later text, byte-list, text-list, text-map and mesh assets, on first and repeated loads and after replacing the UTF-8 text with a shorter value and restoring it. `asset_defaults` continues to pin declared sampling carried into field defaults. `compiler/tests/errors/asset_mesh_*.tide`, a file a diagnostic, with the refused files in `e2e/assets/meshes/bad/`; `pack_meshes_*` in `tests/test_pack.c` (two lists, a list and a map, versions, the vertices to write, drawn once, loaded again elsewhere and edited; blocks cut short); pass 21 of `tide_platform_shaders` (`Models` in `platform/tests/shaders.tide`, `run_model_checks`: a file's front is what's drawn with nothing said and its back isn't, mirrored with both sides, its uvs the texture as it is, a vertex of the game's through a frag with its default, an .obj the same square as a .glb, instances, a list's, a map's, by its two lists: the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found); `lsp_asset_meshes`. The docs' `model` example draws a .glb of several meshes and nodes with no normals, looked at with no window. Not run: Android, and OpenGL on a Mac. The original glTF/OBJ test files are made by scripts from their specifications. FBX now has real Blender and Maya exporter fixtures as recorded above; real exporter GLB/OBJ files remain unproven here.
  - **Not built:** the textures inside a model file (declare supported images separately), a material beyond its color, a file's parts apart, skins, animation and morph targets, `Mesh<V>` as a field's type (for a mesh a game makes, or naming an asset's as a `Texture` does), a mesh read from bytes while the game runs, a 2D mesh from a file, and a frag or a vert reading `mesh.vertices` (`data_of` in `shader.c` resolves an asset by its declaration alone, with no part of one; it's an error that says what to write, and a gap by the rule that GPU code refuses only what a GPU can't do).
- Runtime loading (built 2026-10-06, from the Assets design in `docs/spec.md`, Provisional, Decoding and Fetching; the picks beyond it are listed there): `grid.Decode(bytes)` and `await Web.Fetch(url)`. What's built, and what code relies on:
  - **The PNG reader is the engine's** (`engine/include/tide/png.h`, `engine/src/png_read.c`, from RFC 2083 with its own inflate from RFC 1951 under zlib's wrapper, no code of anyone else's): `tide_png_decode` gives a whole picture as `tide_color`s, `tide_png_decode_rows` hands rows over as they're decoded (an interlaced picture's pass by pass), `tide_inflate` is the raw deflate stream into a sink, and `tide_grid_decode` is `grid.Decode`: it decodes into a new grid in the world's heap, writes each row's cells a chunk's span at a time (a span of zeros makes no chunk), and only once the whole picture decoded lets the old grid go, so a failed decode leaves the grid as it was. A sample is `v / max` in floats, which IEEE 754 defines, so a picture decoded in a tick is the same bits on every machine (`compiler/tests/e2e/decode` pins the world's hash, native and web). What's made for a decode is a few rows. A picture of more than `TIDE_GRID_DECODE_MAX_PIXELS` (4096², 256 MiB of cells; 2026-10-26, the owner asked for clear limits that are easy to change) is refused in `grid_began` before a cell is made, and inflated bytes past the last scanline are read for the checksum up to `TIDE_PNG_MAX_EXTRA_BYTES` (1 MiB), so a decode's work is its picture's: both in the Limits block of `tide/png.h`. Pictures within the limit that together fill the heap still end in `tide_out_of_memory` (`compiler/tests/safety/decode.tide`: 4097 by 16 decodes, 4097 by 4096 white pixels a bit each is `InvalidData` and the grid stays 4097 by 16). `tide_platform_png_decode` (`platform/tests/png_decode.mjs`) reads back pictures Node's zlib wrote of every color type, depth, filter and interlacing, and refuses files wrong in each way. The asset card decodes at build time with the same reader.
  - **tidec:** `Decode` is a grid method beside `Clear` (`check_grid_decode` in `compiler/src/check.c`, `CALL_GRID` with the name), on a `Grid2<Color>` only, taking a `List<byte>`, written like any grid write (`check_writable`), refused on the GPU, giving `void fails IOError`; generated C calls `tide_checked_grid_decode`, an adapter `gen_io_adapters` writes when a game decodes (`program.decodes`), and only such a game's header includes `tide/png.h`. `Web.Fetch` is a built-in owner like `Storage` (`compiler/src/builtins.c`): awaited only, local only through `check_io_use` (the message says the network, not files: `decl.io_network`), with `expr.web_fetch` beside `storage_flush` wherever that's a predicate (codegen's hoisting, the shader writer, parallel steps). Its await is `gen_await`'s: `tide_web_fetch_begin` once, `tide_web_fetch_poll` each frame, the frame's `tide_fetch_ticket` (a field of its own, `decl.uses_fetch`, which `tide_forget_io_*` lets go of with the flush's), the body a scratch list the local copies as any. A program that calls neither generates the C it did, byte for byte.
  - **The provider:** `tide_io` gained `fetch_begin`, `fetch_poll` and `fetch_forget` (`tide/io.h`), the ticket pattern of the flush's; the engine's `tide_web_fetch_begin` checks the URL (`http://` or `https://`, text) and gives `TIDE_FETCH_INVALID_URL` for one that isn't, which polls as `InvalidPath` with no provider asked, and `TIDE_FETCH_LIMIT` (64 MiB) is the body's most, which providers stop reading at and the wrapper checks again. Desktop and Android: `platform/src/fetch.c`, an HTTP/1.1 client of our own (status, headers, a body by its length, in chunks or to the connection's end, up to 5 redirects, relative ones resolved; headers found by name in any order without changing the head; a URL's or redirect's bytes the request line can't carry, a space, controls or non-ASCII, sent as `%XX` in its target and refused in its host) over `rtc/net.c`'s sockets and `rtc/tls.c`'s TLS, a thread a fetch, polled by the frame; a forgotten fetch's thread stops at its next look and frees it. No timeouts (a pick: a browser's `fetch` has none), but the one a request asks for (`SetTimeout`, below). The web: the page's `fetch` (`io_fetch_*` in `platform/web/tide.js`, `tide_web.h`), aborted when forgotten or past the limit. The page test's server answers routes (`cmake/run_web_served_test.mjs --routes <module>`, which also puts `port=<port>` in the page's address, so the program gets `--port=<port>`).
  - Tests: `tidec_e2e_fetch` (natively, `compiler/tests/e2e/fetch.mjs` starts a server with `platform/tests/fetch_routes.mjs`, a path a case: a body as it is, 404, 500, a URL that isn't one, a port nobody listens on, chunks, three redirects, a loop, a body said and a body sent past the limit, 204 and an empty body; for requests also `/moved-late`, a `Location` after `Content-Length`, and a URL with a space and a line break that must arrive as one request line with no header of its own) and `tidec_e2e_fetch_web` (the same game in a page, served); `compiler/tests/errors/fetch_*.tide` and `decode_*.tide`. Not tested here: HTTPS end to end (the TLS path is the relay's, exercised by `tide_platform_rooms`), and Android.

## Skinned meshes and animation

- **Rest-draw proof identity** (2026-10-10): `tide_pack_skinned_proof(data, identity, version)` supplies the current registered canonical block's nonzero numeric identity together with its existing content hash in one lookup. Each distinct validated block receives a fresh identity after all load-time sanitizing writes; duplicate records naming one block share it. Equal-content reloads and reused offsets receive different identities, rejected loads retain the current identity, and unload or overlapping geometry/default writes invalidate it. The counter is never reset on unload and saturation disables new proofs rather than reusing identities. This visual-only runtime identity is absent from pack bytes, game state, snapshots and deterministic hashes. `tide_pack_skinned_version` retains its original contract. The omitted-pose draw cache can therefore reuse owned rest captures without rescanning node/palette tables or hashing rest transforms on each instance. `pack_skinned_proof_identities_follow_loaded_blocks` checks shared/distinct blocks, optional outputs, owned exclusion, rejected/equal reloads and unload/reload; the geometry-alias regression additionally verifies selective identity invalidation.
- **Standalone animation assets** (2026-10-10): `asset Animations movement = "movement.glb";` imports the file's clip catalog without a vertex type or typed render geometry. Animation-only glTF files with nodes/channels and no meshes are accepted; ASCII and binary FBX use the same shared converter as SkinnedMesh assets. A single static asset exposes inferred members directly (`movement.run`); `Animations` annotations and collection elements retain dynamic `count`, `Find(originalName)` and `At(index)` access. Inferred catalogs convert to ordinary dynamic values through the existing owned-list copy path. Lists/maps accept globs or one file, with ordinary path keys. The resolver shares the skinned portable-blob cache, external-buffer dependencies, URL path and invalidation behavior. The pack stores each canonical blob as existing packed bytes, with catalogs storing source-list handles and lists/maps storing nested catalog values; there is no new runtime function or pack kind. Canonical content, list order and map keys participate in game identity and private logical preflight before loading, and watched animation edits rebuild the game. `compiler/tests/e2e/animations` covers meshless imports, GLB and ASCII/binary FBX clip imports, direct/erased members, singleton collections, persistent copies, pack unload, snapshots, serialization, stable storage and rejected changed canonical content. `tidec_animations_identity` checks external animation edits, restoration, collection key semantics and unrelated ordinary assets; the side-buffer notification regression also includes `ASSET_ANIMATIONS`. `animations_asset_obj`, `animations_asset_member` and `animations_erased_member` pin diagnostics.
- **OBJ import diagnostics** (2026-10-10): a failed `SkinnedMesh<V>` import whose written filename ends in `.obj` explains that skinned assets require glTF 2.0 (`.gltf` or `.glb`) or FBX (`.fbx`), with `Mesh<V>` and `Draw.Mesh` as the static-OBJ alternative. Full builds and catalog-only editor inspection use `asset_skinned_obj_diagnostic`; URL suffix checks use only the path before its query/fragment, case-insensitively. The helper runs after decoding or metadata inspection fails, so valid glTF/GLB content accepted under another filename remains accepted, malformed `.gltf` files retain their JSON diagnostics, and an uncached URL remains unavailable without being fetched. `skinned_asset_obj` pins the compiler error and note; `asset_skinned_obj_diagnostics_follow_written_file_names` covers local/URL suffix boundaries, and `asset_mesh_side_notifications_override_unchanged_metadata` also checks valid glTF content named `.obj`. Editor protocol regressions are recorded in `notes/editors.md`.
- **Design agreed 2026-10-08.** `docs/spec.md`, Skinned meshes and animation, records the agreed design; implemented pieces are recorded below. A skinned model supplies a skeleton and animations, and deterministic CPU sampling supplies pose data that both match systems and views can read, including bones used for sockets, hitboxes and root motion. The existing static mesh importer still ignores skins and animation; its Not built list above remains true for `Mesh<V>`.
- An optional contract states the animations and bones a game requires. Statically imported assets expose animation and bone members checked at compile time. Runtime content can use `Find` for dynamic lookup, or validate the required subset against a contract; extra animations and bones do not by themselves make it incompatible. Inferred members clean separators, prefix invalid leading/reserved spellings and disambiguate animation collisions; `[Name("source name")]` aliases use the exact source spelling. `Find` always uses the original spelling and refuses ambiguous duplicates.
- **`Web.Request(url)` and `await request.Send()`** (built 2026-10-11; the owner asked for a general request primitive and for the builder style; the rest is Claude's picks, Provisional in `docs/spec.md`, Tasks). What's built, and what code relies on:
  - **The request and the response are Tide source the compiler carries** (`web_prelude` in `compiler/src/builtins.c`, path `<tide>/web.tide`): `struct WebRequest` with its `Set` methods, `struct WebResponse`, and the function `Web.Request(url)` calls (`WEB_REQUEST_MAKER`). So a request is a struct like a game's, and every rule of a struct with text and a list in it is the compiler's own: copies, the scratch area, a local spilled across a wait, fields set directly. Written by hand in C it would have been a second copy of those rules. `compile_inputs` and tidels' `analysis_run` add the source after the game's files when a file's text has `Web.Request`, `WebRequest` or `WebResponse` (`web_prelude_wanted`, a plain search: a comment counts, which only costs the two structs). A program without them generates the C it did, byte for byte.
  - **The compiler knows two things of it** (`compiler/src/check.c`): `Web.Request(url)` is a call of the maker (`web_decl` finds the prelude's declarations by name and file, so a game's own `WebRequest` is never taken for it), and `Send` on a `WebRequest` is `check_web_send`: local code only (`check_io_use`), awaited, no arguments, giving `WebResponse fails IOError`. It sets `web_fetch` as well as `web_request`, so everything that knows an awaited fetch knows it: the task's `tide_fetch_ticket`, the forget when a task is dropped, the order and parallel checks.
  - **Generated C** (`codegen.c`, the `web_fetch` block): the request's expression is worked out once, inside `if (!ticket)`, so a resumed task never builds it again; `tide_web_request_begin(io, url, method, headers, body)` then `tide_web_request_poll`, whose `tide_web_answer` becomes the result's `.value = {.status, .body}`. The ticket is forgotten with `tide_web_fetch_forget`: a request's ticket is a fetch's.
  - **The engine's wrapper checks what's sent** (`engine/src/io.c`), the same for every provider: the URL as a fetch's (`TIDE_FETCH_INVALID_URL`); the method a word of 1 to 16 letters, made capitals, and not CONNECT, TRACE or TRACK, which a page can't send; each header a `Name: value` line whose name is a token and whose value is printable ASCII and tabs, which is what a page's fetch takes; the body under `TIDE_FETCH_LIMIT`. Anything else is the ticket `TIDE_REQUEST_INVALID`, which polls as `TIDE_IO_INVALID_DATA` with no provider asked. `Host`, `Content-Length`, `Connection` and `Transfer-Encoding` are left out (`header_is_ours`). Providers get headers as `Name: value\r\n` lines.
  - **A header can't smuggle another:** `SetHeader` turns a `\n` in a name or value into a space (Tide text has no `\r` escape), and the wrapper refuses any control character, a `\r` among them.
  - **The provider** (`tide_io`): `request_begin(user, method, url, headers, body, size)` and `request_poll(user, ticket, &error, &status, &data, &size)`, both new, agreed with the feature; `fetch_forget` lets a request's ticket go. Desktop and Android: `platform/src/fetch.c`, where a request is a fetch with a method, headers and bytes to send (`tide_request_begin`, `tide_request_poll`), `Content-Length` said for a body or a POST, PUT or PATCH, `User-Agent` and `Accept` said unless the request says them, no body read for HEAD, 204 and 304, and any status from 200 up the result. Redirects are a browser's: a 303, or a POST's 301 or 302, goes on as a GET with no body; a 307 or 308 as it was. The web: `io_request_begin` and `io_request_status` in `platform/web/tide.js`, over the same `startFetch` as a fetch, whose tickets, poll, read and forget it shares; `tide.release.js` was regenerated.
  - Tests: `tidec_e2e_fetch` and `tidec_e2e_fetch_web` (`compiler/tests/e2e/fetch.tide`, `fetch_check.h`), 15 cases against `/echo`, `/see-other` and `/again` of `platform/tests/fetch_routes.mjs`, natively and in a page: a POST's method, header and body; a GET by default; a 409 and a 404 as answers; a request in a variable across waits, changed by field and by call; HEAD; both kinds of redirect; a line break in a value; `Content-Length` left to the sender; a bad method, a refused method, a bad header name, a bad URL and a port nobody listens on. `compiler/tests/errors/web_request_*.tide`; `lsp_web_requests`.
  - **A time limit** (built 2026-10-11; the lead agent said it would add one and the owner didn't object; the builder's shape is the owner's, every rule the agent's pick, Provisional in `docs/spec.md`, Local files). `SetTimeout(seconds)` in the prelude sets `WebRequest.timeout`, a float, which every other `Set` carries along; 0 or less is none, the default, so nothing times out that didn't ask to, and nothing retries. `Send` fails with **`IOError.TimedOut`**, a new member (11, last, so the others keep their values; `TIDE_IO_TIMED_OUT`), picked over `Failed` so a game can tell a slow service from one it can't reach. **It's a member only in a program that has the request's code** (`add_builtins` in `check.c` looks for the prelude's file among the program's; `web_prelude_wanted` also takes a file that says `IOError.TimedOut`): every game's header declares `IOError`, so a member for all would have changed every game's header and with it its hash, which is what says two builds are one game. A program with no request declares the `IOError` it did and generates the C it did, byte for byte: old and new tidec over all 272 source lists in `build/debug`, with `--layout` and without, 265 the same, five the old compiler refuses (the fetch and function-task e2e programs and the three that build the lobby package), and `last_light`'s two failing with both. What follows from it: `.TimedOut` as shorthand, in a program whose files name neither a request nor `IOError.TimedOut`, is no member.
    - **The limit is the whole exchange's**: from `Send` to the body's last byte, redirects among them. The engine's wrapper makes it what providers take (`tide_web_request_begin`'s new last parameter): 0 for 0, less or NaN, and no more than `TIDE_REQUEST_TIMEOUT_MOST` (1,000,000 s), so no provider converts a float it can't hold. `tide_io.request_begin` gained the same parameter.
    - **Desktop and Android** (`platform/src/fetch.c`): the request's `deadline` is `tide_time_now_ns()` plus the limit, set before its thread starts and never changed, and it's the one authority. The thread asks `forgotten(q)` wherever it waits (connecting, the TLS handshake, sending, receiving), which is now also true past the deadline, so it stops there, closes its socket and TLS and frees its buffers as a forgotten fetch's does, and ends as `TIDE_IO_TIMED_OUT`. `tide_request_poll` says the same itself, under the lock, for a request past its deadline whose thread isn't done, and marks it forgotten, so a thread inside `rtc_resolve` (the system's name lookup, which nothing interrupts) costs the game no wait and frees the request when the lookup returns. An answer that completed before the poll looked is the answer.
    - **The web** (`platform/web/tide.js`): `startFetch` takes the seconds, and a `setTimeout` marks the entry late and aborts its `AbortController`; the fetch's `finally` makes a late entry `timedOut` (11) with no bytes, whatever it had read, and clears the timer, as `io_fetch_forget` does. `tide_web_io_request_begin` gained the parameter. `tide.release.js` was regenerated (67,144 bytes). A hidden page's timers are slowed by browsers, so a limit can pass late there.
    - **`Web.Fetch` has no limit:** it's one call with no builder; `Web.Request(url).SetTimeout(s).Send()` is the fetch with one.
    - Tests: six more cases in `tidec_e2e_fetch` and `tidec_e2e_fetch_web` (21 in all), against `/silent` (takes the request, never answers), `/stall` (five bytes of a body of eleven, then nothing) and `/slow` (answers 100 ms later) of `platform/tests/fetch_routes.mjs`: `TimedOut` at 0.3 s for the first two, an answer under a 30 s limit, a limit of -1 as none, the limit kept through later `Set` calls and set by its field, and a request answered after one that timed out. Natively also `fetch_time_limits_reach_a_provider_as_seconds_or_none` (a provider that notes what it's handed for 5, 0.25, 0, -1, NaN, both infinities and 2,000,000) and `fetch_a_request_past_its_time_limit_is_timed_out_where_its_polled` (not before 0.2 s by `tide_time_now_ns`, `TIDE_IO_TIMED_OUT` after, and the same server with no limit never done until forgotten). The only waits are the limits themselves, 1.1 s in all. `lsp_web_requests` (completion of `SetTimeout`, `timeout` and `IOError.TimedOut`, hover).
  - Not built: an answer's headers, cancelling a request from code (no design: `docs/spec.md`, Tasks, Open), a limit on connecting apart from the whole, text bodies (`SetBody` takes bytes), and a warning for a `request.SetHeader(...)` written as a statement, which gives a request nobody keeps (built since: a dropped value warns).
- `asset` URLs are fetched at build time, while `await Web.Fetch(url)` fetches bytes locally as the game runs. Fetch and decode are separate. A requested contract must validate before loading reports success. The language's decoding API and ownership integration are recorded with their implementation; the byte decoder is below.
- **Compiler integration** (built 2026-10-08): `ASSET_SKINNED` keeps its portable blob and canonical geometry alongside ordinary asset matches. `.gltf` external buffers are watched independently; a changed side buffer invalidates the skinned decoder cache. Typed vertex packing uses the static mesh layout/default machinery, and singleton/list/map values share the ordinary pack lifetime. URLs use the existing build-time fetch/cache path; the language server separately reads cached catalog metadata without fetching or decoding geometry (glTF JSON, or FBX without geometry/animation curves).
  - **Integration with vertex defaults and explicit invalidation** (2026-10-09): `gen_assets` keeps both loading passes for static and skinned models, opens the appropriate typed geometry and calls `tide_pack_mesh_updated` after filling each block's defaults. The helper checks a versioned block independently of its asset kind, so final typed values, shared blocks and repeated loads retain the existing version contract. Skinned content and vertex-default dependency preflight run before `tide_pack_load`; an old compiled loader refuses changed dependency bytes even when the canonical model or computed default value stays the same. Ordinary static meshes retain their existing dependency-only reload behavior in `asset_meshes`. `compiler/tests/e2e/skinned_defaults` draws singleton/list/map models whose defaults read later assets directly and through constants, omitted nested defaults, operators, helper calls/default arguments and another mesh's defaults. It checks first and repeated matching loads, rejection of shorter text and equal-length changed text without replacing the old pack entry, final vertex versions, copied draw data or an owned match model, and successful unrelated texture pixel replacement. This separate fixture leaves the existing skinned pose hash pins unchanged. `asset_files_changed` checks both `sides` and `skin_sides` before invalidating a cache owner. Native `tidec_asset_files`, in `compiler/tests/asset_files_test.c`, changes valid external geometry while holding its size/time fixed, observes the old warm cache before notification and the changed geometry/hash afterward, then restores the original bytes. Static and skinned cases have separate cache lifetimes, so a static side list cannot accidentally satisfy the skinned regression. The editor's metadata-only skinned path deliberately does not inspect geometry or side buffers; full-import cache coverage therefore belongs to the compiler resolver.
  - Generated model, catalog, animation and pose structs use ordinary list copy, persistent heap, snapshot, serialization and local-field rules. `Bone` carries the skeleton fingerprint and node index; poses hold immutable model bytes and a list of `tide_bone_transform` values. `skinned_codegen.inc` and `skinned_adapters.inc` emit checked member lookup, optional lookup, deterministic Clamp/Loop sampling, in-place sampling/blending, bone access and model matrices. Pose element writes retain ordinary checked indexing and copy-on-write; an incompatible bone reads the default TRS and writes nothing. Runtime `SkinnedMesh<V[, Contract]>.Decode` validates self-contained GLB bytes and the required contract before reporting success, then fills typed vertices including declared defaults.
  - A known uncontracted asset can satisfy an explicit contract only when every required original or normalized name occurs exactly once. `var` retains inference; a plain `SkinnedMesh<V>` annotation or parameter erases checked names, while a contracted type preserves its required subset. Full original names remain available to this compile-time proof even when they cannot be dot members. Runtime models need `Find`, or their declared contract.
  - Skinned canonical bytes, map keys, vertex layout/defaults and the content of assets their defaults reach contribute to the generated header's game compatibility hash. The dependency walk follows checked asset/constant bindings, implicit type defaults, resolved operators/helpers and settled arguments; a referenced static mesh contributes its own default dependencies. Runtime `SkinnedMesh<V>.Decode` seeds the same vertex-default closure without requiring an imported skinned asset. Models can affect match sampling and owned typed vertices, so generated `tide_assets_load` checks content/count/layout and dependency fingerprints before replacing the loaded pack. Dependency fingerprints are compiler support shared by build-time hashing and the generated preflight, with no new engine API. They authenticate reachable logical payloads, including whole grid chunks and their zero-filled edge cells, while excluding physical handles, allocation padding, spare directory capacity and cached versions. Unrelated ordinary assets stay outside that closure. `tidec_skinned_default_identity` (`compiler/tests/skinned_default_identity.mjs`) separately isolates fourteen accepted programs: direct text/bytes, implicit constant and nested defaults, operators/helpers/compound operators/omitted parameters, missing-map-value defaults, static mesh defaults, text lists/maps, texture collection counts, grid cells and Decode-only defaults. Each changes its dependency, restores it, and changes an unrelated visual image, checking both the complete generated header and game identity. The pre-fix compiler accepted the original twelve but retained the header after every dependency edit; independent review added the compound operator and missing-map-value cases. All fourteen final identity probes passed with the repaired native compiler; the skinned rejection/retained-state fixture and ordinary static-mesh reload control also passed directly. The docs production build passed. The CLI and hot-reload notes record watched rebuild behavior.
  - **Dependency preflight selects names in a batch** (2026-10-09): the shared private `pack_fingerprint.inc` hashes selected names into a temporary index, scans the validated pack entry directory once, then hashes the same payloads in declaration order. Missing, duplicated or wrong-kind selected entries fail before loading; unselected entries remain outside the closure. Both build-time output mode and generated expected-hash comparison mode release checked, bounded lookup scratch on every return, and generated C keeps its requests/expected hashes static instead of making a dependency-sized stack array. The scalar compiler wrapper remains for raw-pack tests; no engine API, digest, layout or game-identity change is introduced. Previously each selected dependency scanned every entry: with N selected names/N entries and one shared byte of text, five warmed sweeps against the same native debug library measured 1k/2k/4k/8k/16k at 9.35/27.24/89.50/313.72/1197.16 ms for the unoptimized helper, versus 5.53/10.29/20.69/41.17/80.36 ms in batch mode. Optimizing only helper/caller gave 5.70/12.67/30.75/83.77/253.65 ms before and 4.99/9.94/19.97/40.38/79.80 ms after; the linked library and payload hashing remained unchanged. This is cold asset-load work, with no added draw/tick cost. `tidec_pack_fingerprint` covers mixed descriptors for all supported asset kinds, 1,024 named entries with reverse/subset selection and collisions, output/expected modes, failure order and malformed directory/count/name inputs. All nine raw tests passed under strict standalone clang and Windows AddressSanitizer.
  - **Imported model packs authenticate typed source geometry too** (2026-10-09): generated loading uses a second private logical-fingerprint batch for every imported skinned declaration, even without vertex defaults. This expected packed-content digest stays out of the header/game identity, whose canonical model, layout and selected dependency hashes already determine the source geometry. The pack is built once before these checks are emitted, including model-only programs. Kind, layout, model count/order, map keys/index and canonical bytes retain their existing checks, and raw typed vertices/indices now must match before replacement or defaults. Previously changing only a source-filled position in a candidate pack passed blob preflight and load (`x = 8` became `25`), letting owned match geometry differ under the same identity. The expected raw default slots remain the builder's zero values; defaults fill only the loaded copy afterward, and repeated matching loads remain valid. Cache versions and allocation padding stay excluded. `tidec_pack_fingerprint` compares singleton/list/map record, exact-length cut and malformed map-index rejections with the runtime oracle, rejects changed typed source positions, and checks ignored versions/padding plus loaded default writes and repeated input checks. All eleven raw tests passed strict standalone clang and Windows AddressSanitizer. `skinned_defaults` adds a changed-source-position rejection while retaining the old entry, final version, owned world/positions and copied draw vertices; the new test failed against the old generated loader. The former generated canonical-only loop also scanned the full entry directory per imported declaration. With 16k declarations sharing a valid 1,136-byte model, five warmed native debug sweeps measured 2,194 ms before, including 1,333 ms above an identical-payload one-entry control. Full logical batch checks, including the newly authenticated geometry, measured 985 ms against a 991 ms one-entry control; the residual difference is measurement noise, with directory work reduced to one pass. A single list/map declaration containing many models already had one directory lookup; it gains geometry authentication rather than this lookup speedup.
  - `game_info.assets_affect_match` also includes selected vertex-default asset dependencies in Decode-only games. The CLI then rebuilds watched asset changes with their corresponding identity instead of offering a pack-only update that the old loader must refuse. A Decode type with no asset dependencies adds no such restriction; unrelated ordinary assets remain outside the compatibility fingerprint even when the watcher rebuilds the containing game. The 2026-10-09 integration passed the full native suite (2,008 tests), web-debug suite (1,981 tests), all fourteen isolated identity cases, and the docs production build.
  - **Final batch/geometry validation** (2026-10-09): both full builds passed, followed by all 2,009 native and 1,981 web-debug tests. Complete generated-header hashes and game identities stayed identical for all fourteen dependency families and an imported-model-only fixture. The eleven standalone fingerprint tests also passed strict clang and Windows AddressSanitizer; the new typed-position rejection was confirmed failing against the previous generated loader before passing with the fix.
- **Portable skinned-model decoder** (built 2026-10-08): `tide/skinned_data.h`, `engine/src/skinned_decode.c` and `skinned_data.c`. The agreed runtime boundary is immutable bytes owned through ordinary lists, with parent-local TRS poses owned separately and CPU sampling; no stored native pointers or process-local resource handles. `tide_skinned_data_decode` produces a malloc-owned portable blob from glTF 2.0 JSON or GLB, using an optional external-buffer callback for build-time import. Runtime decoding accepts a self-contained GLB only and never requests external resources. The caller releases the temporary allocation with `tide_skinned_data_free` after placing its bytes in owned storage. The runtime's URL fetch remains local.
- **Model decode limits and key lookups** (2026-10-26, from fuzzing; `notes/memory-safety.md`, Remote assets). glTF accessors with no buffer view read as zeros, so a few bytes could claim any count, and object key lookups were linear and repeated per node: a 276-byte GLB peaked at 1.76 GB in `SkinnedMesh.Decode`, a 3 MB file took over 100 s. Objects of more than 8 keys keep a sorted key index (`gltf_json.h`, an own merge sort, as MSVC's `qsort` can be driven quadratic by chosen keys; a repeated key still resolves to its first occurrence), a skin reuses one node table and an animation clears only what it set. Limits, one commented `#define` each, checked from counts before allocating: `tide/skinned_data.h` for the runtime decode and tidec's SkinnedMesh/Animations imports (`TIDE_SKINNED_MAX_BYTES` 512 MiB kept, then JSON values 4,194,304, nodes 65,536, skins 4,096, clips 65,536, buffers 65,536, vertices 1,048,576, indices 6,291,456, palettes 262,144, influences 8,388,608, channels 262,144, key numbers 16,777,216, and `TIDE_SKINNED_MAX_READS` 67,108,864 numbers read in all, counted again for each part naming an accessor; a model at every limit measured 577 MB peak), and `compiler/src/mesh_files.h` for static meshes (vertices 16,777,216 as before, indices 25,165,824, JSON values 4,194,304, reads 536,870,912). Zero-backed accessors count like any. Tables grow to their limit, never past it. OBJ's line counter is 64-bit and `push_floats` refuses past 2^30 numbers. Old and new decoders gave identical output for 22,356 inputs. Tests: `skinned_decode_refuses_files_past_its_limits`, `skinned_decode_objects_with_many_keys_read_as_before`, `mesh_gltf_refuses_files_past_its_limits`, `mesh_gltf_objects_with_many_keys_read_as_before`. The numbers are Claude's picks, Provisional in `docs/spec.md` and `docs/game/animation.md`.
  - The blob has little-endian scalars, checked 32-bit offsets/counts, parent-first nodes with an identity root, source node indices and exact source names, node-local rest TRS, palette entries (node plus inverse bind), canonical vertex attributes, variable influence ranges and triangle indices, clips, channels, key data and strings. It retains all source nodes, including non-joint parents and nodes outside the selected geometry's scene; geometry follows the default scene, or the static importer's fallback when no scene is named. A skeleton's SHA-256 covers its names, parents and rest transforms independently of its geometry and animations.
  - Positions reflect X, transforms and inverse binds use `S*M*S` with `S = diag(-1,1,1,1)`, and quaternion rotations become `(x,-y,-z,w)`. Skinned node transforms are not baked into geometry. Rigid attachments keep node-local vertices and one influence on a node's identity-inverse-bind palette. Several skins retain separate palettes even where they share nodes; every supplied consecutive joint/weight set is read without truncation and positive weights are normalized in a fixed order. Missing normals produce flat local normals and separate triangle vertices. STEP, LINEAR and CUBICSPLINE channels preserve their key times and cubic tangents; unsupported morph targets or channels fail explicitly.
  - `tide_skinned_data_open` checks the header/table spans, and each getter checks its record and referenced spans before reading. Full validation checks parent ordering, finite values, inverse binds, influence/index ranges, channel shapes and increasing nonnegative key times. JSON, decimal parsing, buffers and accessors are shared with the existing static importer through `tide/internal/gltf_json.h` and `gltf_read.h`; `mesh_number` keeps its public signature, and static `Mesh<V>` keeps its flattening behavior. Shared parser/buffer allocation and the decoder's temporary arrays use `tide_alloc`, `tide_alloc_zeroed` and `tide_realloc`, including tracked old capacities, so a WebAssembly worker synchronizes memory growth before touching its allocation.
  - Tests: `tests/test_skinned_decode.c` covers a non-joint parent, source nodes out of hierarchy order, five influences from two sets, inverse-bind conversion, a rigid attachment, exact source names and all three interpolation modes; compile-time external glTF and runtime GLB produce the same bytes. Runtime external buffers and JSON-only input are refused. A GLB cut at every length and spoiled three ways at every byte is decoded from exact-sized allocations; a portable blob is cut and spoiled too. All five tests passed natively. Language asset/cache/pack integration and rendering are separate pieces; the raw decoder adds none of them by itself.
  - Import limits still present: morph targets and unsupported required extensions are refused; runtime external resources are refused. Unlike the static importer's referenced-vertex compaction, normal-bearing skinned primitives currently retain their complete accessor. No real exporter GLB has been used as a smoke fixture yet. The shared static importer passed all ten of its tests under Windows clang AddressSanitizer after factoring and worker-safe allocation changes.
- **Skinned pack records** (built 2026-10-08): `TIDE_PACK_SKINNED` (14), `TIDE_PACK_SKINNEDS` (15) and `TIDE_PACK_SKINNED_MAP` (16). A model is three portable list handles (`tide_skinned_lists`: immutable model bytes, typed vertices, indices); a singleton is a one-element record block, a list has 12-byte rows, and a map has 24-byte rows: signed 64-bit insertion order at offset 0, text key at 8 and model at 12. The block payload and row stride keep each order aligned to eight bytes. Skinned maps follow pack version 2's ordinary map contract: orders `1..count`, with `last_order = count` on load, so cursors and additions to copies retain their order. The model bytes use `tide_pack_bytes`, geometry uses the existing versioned mesh blocks, and all three entry kinds carry the vertex-layout number. `tide_pack_skinned_list`/`map` build the records; `_of`, `_map_of` and `_open` read them and supply vertex-default writes before use, with the same lifetime as the loaded pack.
  - Loading validates the complete canonical blob, typed geometry spans and element sizes, matching vertex/index counts and canonical index values, tagged/aligned model handles, and map key/index spans. Skinned map validation also checks source insertion orders, a power-of-two index at most half full, exactly one index slot per row, and each key's probe chain reaching its own row before an empty slot. An invalid model becomes three empty lists together, including in a list or map; invalid record blocks and maps become empty. Validation happens before consumers receive the records; getters still check their spans. The new skinned entry kinds use the current pack version without a further version change.
  - **Immutable model draw provenance** (2026-10-10): after complete record validation and all load-time sanitizing writes, a private loaded-pack hash table revalidates and records each distinct canonical byte-list handle and its exact content hash. Only spans inside the original file region qualify. `tide_pack_skinned_version` supplies that proof to the runtime draw path, avoiding repeated full validation and hashing for unchanged imported models. `tide_pack_skinned_sample_proof` also supplies the load-time fact that every channel starts at a nonnegative time, allowing the sampler to skip its selected-channel preflight when true. A false flag retains selected-clip checks and does not invalidate the model. It is recomputed on replacement and gated by the same current provenance; zero-channel models qualify and negative zero is accepted. Ordinary packed byte assets, owned copies and runtime decoded models do not acquire that proof. Replacement/unload clears the table, including when a new pack reuses an offset for changed or invalid content. A sorted span index invalidates proofs when packed geometry/default writes overlap a model's header or payload in a forged pack; ordinary disjoint writes take logarithmic lookup time. It stores no borrowed pointers and changes neither the portable pack nor game/snapshot data. Four `pack_skinned_verified_versions_*` regressions check singleton/list/map sharing, many-model lookup growth, raw-byte exclusion, copied-model edits/rejection, captured textures, replacement/unload, forged geometry aliases and header counts, and later malformed-record clearing that changes or spoils an earlier model. All 23 focused native skinned tests passed with the unchanged pose-bit hash.
  - `tide_pack_skinned_content` remains the C host's raw canonical-content checker and the regression oracle for kind, vertex layout, model count and source-order SHA-256s. Singleton/list hashes cover the complete canonical bytes; map hashes cover key bytes followed by canonical bytes, because a lookup key affects match behavior too. Header, record, byte, key and map-index spans are checked before hashing, and a changed canonical model is rejected without touching the loaded pack. Generated loading now uses the complete compiler-private logical batch described above before `tide_pack_load`, also authenticating typed source geometry that the canonical-only checker does not cover.
  - Tests: `tests/test_pack.c` checks singleton/list/map access, geometry sharing and default writes, layout mismatch, invalid node data, inconsistent geometry counts, cut byte/geometry/record spans, forged handles and invalid map keys/index slots. Skinned map traversal and additions to a copy retain insertion orders; invalid low/high order words, duplicate/omitted index rows and misplaced probe chains fail both loading and preflight. Preflight also checks changed content/count/layout, a renamed map key, malformed handles and exact-sized cuts at every byte while retaining the current pack. After integrating pack version 2's map layout, all four skinned pack tests, all 22 matching `pack`, all 15 matching `map` and all 14 matching `skinned` passed in an isolated Windows clang AddressSanitizer build; the shared static importer passed all ten tests again. Compiler import/cache/generated wrappers and rendering are recorded by their own integration.

- **Media-format validation (2026-10-10):** the full Windows debug suite ran 2,047 tests and the web-debug suite 2,017. Two existing expectations (a recursive PNG fixture count and the old malformed-GLB editor error) were updated; the affected native and web tests then passed. The production docs build passed. Image imports passed Windows ASan and Linux ASan/UBSan/leak checks with matching pixel hashes; audio decoders passed isolated Linux ASan/UBSan/leak checks including 36,200 malformed exact-allocation inputs. These imports are build-time tools; runtime image decoding remains PNG-only and runtime audio remains WAV/Vorbis.

- Media-format integration with the concurrent standalone `Animations` and rest-pose changes preserves meshless FBX, canonical animation packing and original recognized-content diagnostics. Unsupported Animations sources now get the same targeted glTF/FBX guidance as skinned models; OBJ remains static-only in editor suggestions. Two new diagnostic fixtures pin unsupported DAE and required-extension errors on renamed animation sources. After merging dev, complete native/web builds and 152 native plus 142 web checks of overlapping asset/model/animation/import/editor/runtime paths passed, as did the merged production docs build.

- **Clean names, combined catalogs and selected clips** (2026-10-11): checked imports clean exporter separators (`Root|Run` -> `rootRun`, `Root|0.Targeting Pose` -> `root0TargetingPose`), prefix numeric/keyword/reserved identifiers, and disambiguate normalization collisions with source ordinals while reserving authored aliases. Duplicate original animations remain checked index-bound members; ambiguous original bones still require enumeration because canonical node order differs from metadata. Plain `Animations` imports accept constant path lists and globs, preserving written order, sorted glob order and source clip order, deduplicating resolved files, and prefixing colliding members with filename stems before ordinal fallback. `Find` keeps exact original-name semantics across the whole catalog and rejects duplicates. `asset Animation` requires one clip or a unique exact `[Name("source")]` selection from one file. Each clip keeps its source rig. Catalog values carry `List<List<byte>>` source blobs through existing copy/ownership/snapshot machinery; collection assets add one nesting level. Model catalog operations borrow raw bytes directly; escaped model catalogs wrap the source handle. There is no exported runtime API or new pack kind. Private preflight authenticates every nested source before replacement, with a fixed bounded depth; game identity includes selected ordinals and combined-source keys that affect inferred aliases. `animation_imports` E2E covers file lists, constants, glob overlap/deduplication, filename and ordinal collisions, keywords/digits, duplicate and unnamed clips, different source rigs, selected clips, model catalogs, contracts, persistent copies, snapshots, serialization and rejection of changed later-source data. New `animation_asset_*`, file-list and reordered-catalog error fixtures pin diagnostics. Inferred destination conversions require the same original clip at every checked ordinal to preserve indexed lookup. Full-build metadata extraction failures are diagnosed before clip selection or flattened indices can be published. Direct model and single-source catalog Find retain the existing runtime lookup path. `tidec_animations_identity` additionally checks repeated-source identity, selected ordinals, collision filename changes, source order and later-source edits.

- Validation (2026-10-11): complete warning-free native/web debug builds and all 2,077 native plus 2,047 web CTests pass, including OpenGL/WebGL/WebGPU and unchanged existing deterministic pins. The production docs build passes. The language server built here was also queried against the owner's actual test project FBX assets: `runFbx.rootRun` and `runFbx.root0TargetingPose` complete; combining the existing idle/run files offers `rootIdle`, `rootRun`, `idleRoot0TargetingPose` and `runRoot0TargetingPose`. Only in-memory documents were changed for that inspection; no project source was edited. The installed Tide tools are separate from this checkout and were not replaced.

## Assets referenced by prefab factories (2026-10-12)

Release asset usage visits every expanded entity spawn of an emitted prefab
factory, using the existing iterative checked-expression work list. Assets named
only by authored component values are retained; an unused prefab still receives
ordinary source diagnostics but does not keep its explicit asset values in a
stripped release. No pack, importer, runtime or model identity contract changed.
asset_usage_retains_instantiated_prefab_values checks a nested prefab-only text
asset, pruning of an unused prefab asset and a one-entry generated pack.
