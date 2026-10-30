---
description: "Import skinned glTF or FBX models, sample, retarget and blend deterministic poses, use bone sockets, and draw animated characters in Tide."
---

# Skinned meshes and animation

A `SkinnedMesh<V>` holds a model's geometry, skeleton and animation clips. A `Pose` holds one character's current bone transforms. Several characters can share the same model while playing different animations.

Keep a named model selection in a component with `Asset<SkinnedMesh<V>>`:

```csharp
asset SkinnedMesh<Vertex3> hero = "art/hero.glb";
component CharacterArt { Asset<SkinnedMesh<Vertex3>> model = hero; }

view DrawCharacter(CharacterArt art)
{
    Draw.SkinnedMesh(art.model.Value, float4x4.identity);
}
```

`.Value` resolves the current packed model, preserving imported LODs and shared
skin palettes. It exposes the declared type's dynamic catalogs; use the original
asset for its inferred clip and bone names. A borrowed or decoded model value
cannot supply declaration identity. References compare by named asset identity
with `==` and `!=`; empty or unknown references resolve to an empty model.
Storing the resolved model itself uses ordinary owned value semantics.

## Import and draw

Import a glTF 2.0 `.glb` or `.gltf`, or a binary or ASCII `.fbx`, with your [vertex struct](./assets.md#meshes). A model needs no animation clips to draw: leaving out the pose draws its authored rest pose.

```csharp
asset SkinnedMesh<Vertex3> hero = "art/hero.glb";

view Character()
{
    Draw.SkinnedMesh(hero, float4x4.identity);
}
```

`hero.defaultPose` makes an owned pose with the same rest transforms, ready to edit or keep in a field. `hero.skeleton.RestPose()` remains available too. The rest pose belongs to the model's skeleton, independently of its animation catalog.

To animate it, sample a clip and pass the resulting pose:

```csharp
asset SkinnedMesh<Vertex3> hero = "art/hero.glb";

component Character { float3 position; float seconds; }

system Animate(mut Character character, Time time)
{
    character.seconds += time.dt;
}

view Characters(Character character)
{
    var pose = hero.animations.idle.Sample(character.seconds, Playback.Loop);
    Draw.SkinnedMesh(hero, float4x4.Translate(character.position), pose);
}
```

`Draw.SkinnedMesh(mesh, transform[, pose])` takes the same texture, vert, frag and values arguments as a [3D mesh](./3d.md), and also works on a render target. The optional pose goes immediately after the transform; textures and shaders can follow the transform directly when no pose is supplied. Deformation applies before your `vert`, which receives the posed mesh-local position, normal and tangent.

Each draw captures the pose as it is at that call, so later edits or samples do not change characters already drawn. The renderer can batch consecutive compatible draws of an imported model even when their poses differ. Different shaders, textures, shared values or draw settings can separate those batches.

For a list of characters, pass the skinned model to `Draw.Instanced`:

```csharp
struct CharacterInstance
{
    float4x4 transform = float4x4.identity;
    Pose pose;
    Color tint = Color.white;
}

frag CharacterColor(Vertex3 vertex, CharacterInstance item, out Color color)
{
    color = vertex.color * item.tint;
}

view Crowd()
{
    Draw.Instanced(hero, [
        CharacterInstance { pose = hero.defaultPose },
        CharacterInstance {
            transform = float4x4.Translate(float3(3, 0, 0)),
            pose = hero.defaultPose,
            tint = Color.blue
        }
    ], CharacterColor);
}
```

The optional `float4x4 transform` field follows the existing 3D instance convention; leaving the field out uses identity. The optional `Pose pose` field supplies that item's animation. Leaving the field out uses authored rest, including on a model with no clips; an explicitly invalid or incompatible pose skips its item. A `List<float4x4>` draws all items at rest. Pose data stays on the CPU, while shaders receive only the ordinary instance fields they read. Textures, custom vertex stages, shared values and render targets use the usual `Instanced` argument order. Each item captures its pose before the call returns.

A crowd seen from afar needs fewer triangles a character: `[LOD] asset SkinnedMesh<Vertex3> hero = "art/hero.glb";` has simpler versions of the model made as the game builds, and each draw picks one by how big the character is on screen. The pose, the bones and every lookup are the full model's; only the triangles drawn change. The model's size for the pick is its size at rest. See [Levels of detail](./assets.md#levels-of-detail).

An FBX declaration works the same way: `asset SkinnedMesh<Vertex3> hero = "art/hero.fbx";`. Import happens when the game builds, without Autodesk software. FBX axes and units become Y-up meters in Tide's coordinate system. Its animation stacks become clips beginning at zero, with their authored duration. Nonlinear transform curves are resampled at 60 samples per second into LINEAR channels, retaining source keys within the clip and stepped changes. Rotation curves are Euler angles that exporters often write wrapped (-182° then 178°, one degree apart): each key is first moved by whole turns to within half a turn of the key before it, and written as the nearer of its two equivalent triples (x + 180°, 180° − y, z + 180° is the same rotation) where all three axes are keyed together, as an Euler filter does, so resampled keys between them take the short way. Geometry and inheritance transforms may add unnamed helper bones. The packed model uses the same format and sampling APIs as a glTF import.

Export polygon meshes with linear or rigid skinning. Skinned FBX imports reject dual-quaternion skinning, several skin deformers on one mesh and external vertex caches. Blend shapes (morph targets) are kept: see [Blend shapes](#blend-shapes). Static `Mesh<V>` imports ignore those deformations and use the rest geometry.

## Blend shapes

A skinned model keeps its file's blend shapes (glTF morph targets, FBX blend shapes): body shapes such as `defaultHeavy`, or facial ones such as `jawOpen`. `model.Shaped(weights)` returns the model with those weights applied, computed on the CPU, and draws, samples and stores like the model itself:

```csharp
asset SkinnedMesh<Vertex3> torso = "art/torso.fbx";

component Character
{
    float heavy;
    float bakedHeavy = -1;
    SkinnedMesh<Vertex3> body;
}

system Shape(mut Character character)
{
    if (character.heavy == character.bakedHeavy) return;
    mut List<float> weights = [];
    for (int i = 0; i < torso.blendShapes.count; i++)
    {
        torso.blendShapes.TryGetAt(i, out var shape);
        weights.Add(shape.name == "defaultHeavy" ? character.heavy : shape.weight);
    }
    character.body = torso.Shaped(weights);
    character.bakedHeavy = character.heavy;
}

view DrawCharacter(Character character)
{
    Draw.SkinnedMesh(character.body, float4x4.identity);
}
```

`model.blendShapes` lists the model's shapes in order. `count`, `TryGetAt(index, out BlendShape result)` and `TryGetByName(name, out BlendShape result)` work as the animation catalog's do; a missing, empty or unnamed name returns `false` and assigns `default`. A `BlendShape` has its original `name` and `weight`, the file's default weight, which the model's vertices already have.

Weight `i` of `Shaped` is shape `i`'s weight. 0 to 1 is the authored range, and other values extrapolate. A missing weight, or one that isn't a finite number, keeps the shape's default, so `model.Shaped([])` is the model as imported. Each shape whose weight differs from its default moves the vertex fields the file fills by name, `position`, `normal` and `tangent`, by its delta times the difference; other fields, such as `uv`, `color` and fields with game defaults, are copied unchanged, as are the skeleton, animations, indices and levels of detail. Shapes apply in catalog order with plain float arithmetic, so the result is the same on every machine and can be used in match code. Normals and tangents are not renormalized.

Bake a character's shapes once, when its body changes, and keep the shaped model in a field: `Shaped` copies the vertices, and drawing a shaped model sends its vertices to the GPU like other models made while playing. Shapes are not animated on the GPU, and imported shape-weight animation is not read.

To keep only some of a file's shapes, and so a smaller pack, write `[BlendShapes("masculineFeminine", "default*")]` before the asset: names, or patterns where `*` is any run of characters. The others aren't imported; `[BlendShapes()]` keeps none. See [Blend shapes to keep](./assets.md#blend-shapes-to-keep).

In a glTF file, targets with the same name in `extras.targetNames` are one shape across all of the file's meshes, and must have the same default weight in `weights`; an unnamed target is a shape of its own, found with `TryGetAt`. POSITION, NORMAL and TANGENT targets are read, including sparse accessors. In an FBX file, each blend shape channel is a shape named after its target shape (`smile` in a channel `Blends.smile`), with the channel's default percentage as its weight. Export each channel as one target at full weight: a channel with in-between shapes is refused. The importer's own flat normals, made for a part exported without normals, don't follow shapes.

## Checked names and dynamic lookup

A known asset provides animation and bone members from its source names: `Idle` becomes `idle`, `Root|Run` becomes `rootRun`, and `Right Hand` becomes `rightHand`. Separators become word boundaries; ASCII letters, digits and underscores are kept. Leading digits, keywords and catalog operation names get an underscore prefix: `123 start` becomes `_123Start`, and `count` becomes `_count`. Colliding aliases receive source ordinals, such as `jump_1` and `jump_2`; extra underscores avoid authored names. A punctuation-only name becomes `_`. Unnamed entries and bones with duplicate original names use dynamic enumeration.

The editor reads the file's catalog without importing geometry or animation samples, and refreshes the names when the file changes. Invalid files and unavailable metadata have separate diagnostics; a requested clip is only reported missing after its catalog was inspected successfully. For an asset URL, the editor uses an existing local cache; a build fetches uncached content.

```csharp
var model = hero; // Retains the known asset's checked catalog.
var idle = model.animations.idle;
var hand = model.skeleton.bones.rightHand;

if (model.animations.TryGetByName("Victory Dance", out var celebration))
{
    var pose = celebration.Sample(0);
    Draw.SkinnedMesh(model, float4x4.identity, pose);
}
```

`TryGetByName(name, out result)` uses the exact original name and returns a bool. Missing, empty or duplicate names return `false` and assign `default` to the result. Both `animations` and `skeleton.bones` provide `count` and `TryGetAt(index, out result)` for enumeration; an index outside the catalog also returns `false` and assigns `default`. Unnamed entries remain accessible by index. `skeleton.TryGetByName(name, out result)` also finds a bone. Contract aliases must be nonempty. An animation has its original `name` and its `duration` in seconds.

A plain `SkinnedMesh<Vertex3>` helper parameter has dynamic lookup. Declare an optional contract when a helper needs checked names:

```csharp
struct Locomotion
{
    Animation idle;
    Animation run;
    [Name("Right Hand")] Bone hand;
}

asset SkinnedMesh<Vertex3, Locomotion> character = "art/character.glb";

Animation Running(SkinnedMesh<Vertex3, Locomotion> model)
{
    return model.animations.run;
}
```

The contract requires that subset; extra clips and bones remain discoverable through `TryGetByName` and `TryGetAt`. Unaliased fields match normalized source identifiers; `[Name]` matches exact source spelling. Contracts stay available through helper parameters and collection elements. A build checks an imported asset's requirements; runtime decoding checks them before returning a model.

## Animation files

For offline text-to-motion generation and learned clip transitions, see
[Tide Motion](../tools/motion.md). Its standalone native CLI exports SOMA-30 GLB
animations and supports batches that reuse loaded models. Its bone-mapping
retarget command exports clips for other humanoid rigs before the game imports them.

A separate glTF or FBX file can supply animation clips without importing typed render geometry. For the character above:

```csharp
asset AnimationCatalog movement = "art/movement.glb";

view MovingCharacter(Character character)
{
    mut var pose = hero.defaultPose;
    pose.Sample(movement.run, character.seconds, Playback.Loop);
    Draw.SkinnedMesh(hero, float4x4.identity, pose);
}
```

An `AnimationCatalog` asset holds every clip in the file and the skeleton those clips animate. A single known asset exposes its clips directly, such as `movement.run`. The editor completes these members and shows their original source names on hover, using the same rules as `hero.animations`. `movement.count`, `movement.TryGetByName("Run", out result)` and `movement.TryGetAt(index, out result)` enumerate or find clips too. Copying with `var` retains checked names; a plain `AnimationCatalog` helper parameter exposes lookup and enumeration.

The file can be `.glb`, `.gltf` or `.fbx`. The importer keeps animation and skeleton data, so an animation file need not contain a mesh. A file containing several clips imports them all.

Combine several files with a list, or use a glob:

```csharp
asset AnimationCatalog movement = [
    "kenney/survivors/walk.fbx",
    "kenney/survivors/run.fbx"
];
// Alternatively: asset AnimationCatalog movement = "kenney/survivors/*.fbx";

var walk = movement.rootWalk; // Original name: Root|Walk
var run = movement.rootRun;   // Original name: Root|Run
```

This makes one combined catalog. List entries can be constant strings or globs; list order is preserved, each glob is sorted by path, and repeated files are imported once. Clips keep their order within each source. If both files contain `Root|0.Targeting Pose`, their members become `movement.walkRoot0TargetingPose` and `movement.runRoot0TargetingPose`. Remaining collisions receive source ordinals. Hover shows the original clip name and its file. `TryGetByName` searches original names across the whole catalog and returns false for an ambiguous name, assigning `default` to its result. Each clip retains its own source skeleton; collecting clips does not retarget them.

Import a single clip directly when the file contains exactly one. Select a particular clip from a multi-clip file with its exact original name:

```csharp
asset Animation idle = "art/idle.fbx"; // Requires exactly one clip.
[Name("Root|Run")]
asset Animation run = "kenney/survivors/run.fbx";

var pose = run.Sample(seconds, Playback.Loop);
```

Missing or ambiguous selections are build errors. An `Animation` asset takes one file, while an `AnimationCatalog` asset accepts a file, a glob or a nonempty file list.

`pose.Sample` binds the clip to the pose's skeleton. Use a pose from the character when you also need its bone references for edits or sockets. You can also sample the source clip with `movement.run.Sample(seconds)` and pass that pose directly to `Draw.SkinnedMesh`; the draw binds it to the character.

Binding matches nodes by unique exact source names and matching parent chains; source node order and default poses can differ. This supports animation-only FBX exports whose defaults are the starting pose of a take. Required source local transforms are copied directly, including the defaults of unanimated ancestors. Sampling into a target pose binds the selected clip's animated nodes and their ancestors. Drawing a source pose binds all animated nodes declared by its source asset, any edited nodes and their ancestors, because a pose does not retain which clip was sampled. Unrelated target nodes keep their own rest transforms.

A clip may animate nodes the model lacks: those are skipped, as in Unity. So one body clip drives every part of a modular character, a hair part with only the root-to-head chain as well as a torso, and a clip's IK helpers (`ik_settings`, `ik_foot_root`) need no node in any part; a part's extra chains (a cape's bones) keep their rest. A node the model has must still have the same parent: a model's `head` below `neck_01` binds a clip's `head` only where the clip's `head` is below its `neck_01` too, so a clip node below one the model lacks is refused rather than moved relative to another parent. A clip none of whose animated nodes are in the model is another rig and is refused too.

An extra unnamed identity scene root, as added by FBX importing, is ignored while its pose and relevant animation channels stay at rest. Animated wrappers or deeper unnamed ancestors cannot be bound this way.

A named node at the top of either hierarchy is folded into the bones below it when the other side has no node with its name. Blender writes the armature object as such a node above the skeleton's top bone, carrying the object's transform: a rig in centimetres gets a scale of 0.01. That transform, animated or not, is combined with its children's. A clip edited in Blender and exported again therefore drives a character exported elsewhere with its bones where they were authored, and a character exported from Blender plays clips exported elsewhere. The folded node's scale must be uniform, and it must be above a bone that binds.

When a clip doesn't bind, the first failed sample or draw for each model and clip prints the reason on the console and names the node that differs, such as `"Hips" is below "Armature" in it but below "Root" in the model`.

This supports separate files exported from the same named hierarchy. It does not retarget animation: source translations, rotations and scales are used as authored, without adapting proportions or correcting coordinate spaces. Ambiguous required names and different parent chains reject binding. An incompatible sample leaves the destination unchanged, and an incompatible draw produces no draw. Bone identity and pose blending still require identical skeleton data.

## Retarget between humanoids

For imported clips, bake retargeting during the build. The result is an ordinary
`Animation` on the target skeleton, so playback only samples the baked clip:

```tide
asset HumanoidRig motionRig = "rigs/motion.humanoid.json";
asset HumanoidRig heroRig = "rigs/hero.humanoid.json";

[Retarget(motionRig, heroRig)]
asset AnimationCatalog movement = "motion/*.fbx";

[Name("Root|Run"), Retarget(motionRig, heroRig, sampleRate: 60)]
asset Animation run = "motion/run.fbx";
```

A `.humanoid.json` profile chooses an explicit anatomical reference, then
overrides automatic assignments where needed:

```json
{
  "reference": { "file": "../art/hero.glb", "pose": "rest" },
  "bones": { "LeftHand": "hand_l", "LeftToes": null },
  "referenceEdits": {
    "upperarm_l": { "rotationOffsetDegrees": [0, 0, 5] }
  }
}
```

Reference paths are relative to the profile. To use a calibration clip instead
of rest transforms, write `"reference": { "file": "reference.fbx", "clip":
"T-Pose", "seconds": 0 }`. The clip name is exact and unambiguous, and seconds
must lie within its duration. The calibration file may be separate from the
animation export; its default pose is never guessed from the first frame.

`bones` uses `HumanoidBone` names and exact source bone names. Omitted roles keep
their automatic assignment; `null` explicitly unmaps a role. `referenceEdits`
names exact bones and accepts `positionOffset` in local metres,
`rotationOffsetDegrees` as local X/Y/Z Euler angles (applied Z, X, then Y, as
`quaternion.Euler`), and a positive uniform
`scaleMultiplier`. Edits apply before discovery; explicit mappings apply last.
Rotation offsets postmultiply the existing local rotation. These edits calibrate
anatomy; use ordinary pose edits after sampling for persistent artistic offsets.

`Retarget` accepts named `rootScale`, `rootOffset`, `headingDegrees`,
`spineDistribution` and `twistDistribution` with the same meaning as the runtime
map below. Omitted root scale uses the leg-length ratio. Other defaults are zero
offset/heading and unit distributions. `sampleRate` defaults to 60 samples per
second. The baker also preserves source key times, endpoints and STEP boundaries;
between samples the target clip's interpolation approximates live retargeting.
Raise the rate when fast motion needs more precision.

Builds cache derived clips by source content, both profiles and bake options.
Changing a profile, reference model or external buffer rebuilds dependent clips.
Invalid required mappings fail the build. The source imports remain unchanged,
and clips retain their original names and duration. A profile asset is also an
ordinary `HumanoidRig` value: inspect its statuses or copy it and edit its mapping
before creating a runtime map. Each profile is a separate asset declaration.

For dynamically loaded motion, prepare a map and bake once when loading it:

```csharp
Animation BakeLoaded(RetargetMap mapping, Animation source, Skeleton target) fails IOError
{
    return try mapping.Bake(source, target);
}

AnimationCatalog BakeLoadedCatalog(RetargetMap mapping, AnimationCatalog source, Skeleton target) fails IOError
{
    return try mapping.Bake(source, target, sampleRate: 60);
}
```

The target is the skeleton used to prepare the map, such as `hero.skeleton`.
Keep the returned animation or catalog in a field and use ordinary `Sample`
during playback. The baked data remains valid after the source, target and map
are discarded, and later map edits do not change it. Catalog baking keeps source
order, names and durations and returns a plain catalog with dynamic lookup. All
its source skeletons must match the map's actual source skeleton.

`Bake` defaults to 60 samples per second and uses the same sampling rules as
build-time baking. Invalid inputs, incompatible skeletons, empty catalogs or
excessive bake work return `IOError.InvalidData` with a diagnostic explaining
the reason; a catalog failure returns no partial result. Use live pose
retargeting below when the mapping or corrections need to change during playback.

Runtime retargeting adapts a source humanoid's motion to a character's proportions
and bone axes. It is part of Tide and does not require Tide Motion. Prepare a
`RetargetMap` once, then use it for every sampled pose from that animation export.

Start with a reference pose for each rig. Use an authored T-pose or A-pose that
describes the character's anatomy; an animation file's defaults can be its first
frame instead. Keep that calibration separate from the actual clip's skeleton:

```csharp
asset SkinnedMesh<Vertex3> sourceCharacter = "art/source-reference.glb";
asset SkinnedMesh<Vertex3> hero = "art/hero.glb";
asset Animation run = "art/run.fbx";

RetargetMap PrepareRun() fails RetargetError
{
    mut var sourceRig = HumanoidRig.Auto(sourceCharacter.defaultPose);
    mut var targetRig = HumanoidRig.Auto(hero.defaultPose);

    // Correct an uncertain or missing assignment with a typed bone.
    targetRig[HumanoidBone.LeftHand] = hero.skeleton.bones.leftHand;

    // Optional calibration edits belong to the reusable reference pose.
    var shoulder = hero.skeleton.bones.leftShoulder;
    targetRig.referencePose[shoulder].rotation = quaternion.identity;

    return try RetargetMap.Create(run.skeleton, sourceRig, targetRig);
}

void Animate(mut Pose destination, RetargetMap mapping, float seconds)
{
    var source = run.Sample(seconds, Playback.Loop);
    if (!destination.Retarget(source, mapping)) return;

    // Artistic offsets are ordinary edits after retargeting.
    var hand = hero.skeleton.bones.leftHand;
    destination[hand].rotation = Math.Mul(destination[hand].rotation,
        quaternion.Euler(0, 0, 5deg));
}
```

Initialize `destination` from `hero.defaultPose`, and keep the successful
`PrepareRun()` result in a field. Handle preparation failures with Tide's normal
`fails` handling. `animation.skeleton` and `pose.skeleton` expose each value's
original skeleton, with `RestPose`, `TryGetByName` and `bones` enumeration.

`HumanoidRig.Auto(referencePose)` discovers roles using names, hierarchy and
reference geometry. Inspect `rig.GetStatus(HumanoidBone.LeftHand)`: the result is
`HumanoidBoneStatus.Automatic`, `Uncertain`, `Missing` or `Manual`. Automatic
discovery can be partial. Check the assignments and improve the reference pose
when a rig needs help; it does not silently rerun after an edit.

`rig[role]` returns a `Bone`, or `default` when unset. Assigning a bone from that
rig sets `Manual`; assigning `default` explicitly unmaps the role. A bone from
another skeleton is ignored. For fully manual mapping, initialize
`HumanoidRig { referencePose = yourReference }` and assign its roles. Role names
follow Unity's `HumanBodyBones`: `Hips`, `Spine`, `Chest`, `UpperChest`, `Neck`,
`Head`, left/right upper and lower arms and legs, hands, feet, shoulders and toes,
eyes, jaw, and each finger's proximal, intermediate and distal bones. Hips, head,
upper/lower limbs, hands and feet are required; other roles are optional.

`RetargetMap.Create(actualSourceSkeleton, sourceRig, targetRig)` validates and
freezes the mapping and reference poses. It supports source exports whose node
order, scene wrappers and default transforms differ from the calibration file.
Rebuild the map after changing assignments or reference transforms. Replacing a
rig's reference pose with another skeleton requires a new rig.

Preparation returns `RetargetError.InvalidRig`, `MissingBone`, `AmbiguousBone`,
`InvalidReferencePose`, `IncompatibleSkeleton` or `UnsupportedScale` on failure.
The console gives the failing role or skeleton detail. Required uncertain roles
need a manual assignment. References must have finite transforms and positive
uniform local scale; malformed hierarchies and ambiguous correspondence are
rejected.

These mutable settings remain available on a prepared map:

| Field | Meaning |
|---|---|
| `rootScale` | Root travel multiplier, initially the target/source leg-length ratio; zero removes travel |
| `rootOffset` | Model-space translation in metres, initially zero |
| `headingDegrees` | Rotation about the target reference hips and up axis, initially zero |
| `spineDistribution` | Extra spine and neck chain interpolation, from zero to one; initially one |
| `twistDistribution` | Longitudinal twist transferred to helper bones, from zero to one; initially one |

`destination.Retarget(source, mapping)` returns `true` on success. It preserves
the target's bone lengths and skeleton identity, so its normal bone edits,
sockets, blending and drawing work afterward. Invalid settings, unsupported
source scale animation or a source/destination from the wrong skeleton return
`false` and leave the destination unchanged. Calibration profiles and prepared
maps are ordinary owned values and can be stored in match or local fields,
copied and snapshotted with poses.

Retargeting transfers motion without foot planting or contact correction.
It transfers rotations and hips translation; other animated local translations
are not copied, preserving the target's segment lengths. A palm or leaf bone
with too little geometry to determine its roll may need calibration or a
post-transfer rotation edit.
Different body proportions can still produce foot sliding or a hand missing a
prop; those need game-specific pose correction or IK. Existing `pose.Sample`
binding remains useful for separate exports of the same rig.

## Sample, blend and edit

Time is explicit. `Sample(seconds)` clamps to the clip's endpoints. `Playback.Loop` wraps positive and negative time; an exact loop endpoint becomes time zero. Every sample begins from the authored rest transforms, so unanimated properties never retain a previous clip's values.

```csharp
mut var pose = hero.animations.idle.Sample(idleSeconds, Playback.Loop);
var running = hero.animations.run.Sample(runSeconds, Playback.Loop);
pose.Blend(running, moving); // Weight from 0 to 1.

var head = hero.skeleton.bones.head;
pose[head].rotation = Math.Mul(pose[head].rotation, lookRotation);
```

`pose[bone]` is a `BoneTransform` with parent-local `position`, quaternion `rotation` and `scale`. `pose.Sample(animation, seconds[, playback])` resamples in place and reuses its transform storage. Sampling and blending run deterministically on the CPU in match systems as well as views. A pose can be stored in an ordinary match field or a local field, with the same copy and snapshot rules as other structs containing lists.

`Pose.Blend` requires identical skeleton data and does not bind across separate exports. To blend clips from separate files, start both poses from `hero.defaultPose` and sample each clip into its target pose before blending.

A stored pose takes the latest tick's transforms in a view. For smoother playback, sample a view pose from the presentation time your game chooses, or blend two sampled poses explicitly. Keep an unwrapped clock when interpolating time across a loop boundary.

## Sockets and match use

`pose.Model(bone)` accumulates the hierarchy into a model-space matrix. Multiply it by the character's world transform to place an attachment:

```csharp
var hand = hero.skeleton.bones.rightHand;
var weaponTransform = Math.Mul(characterTransform, pose.Model(hand));
Draw.Mesh(sword, weaponTransform);
```

Systems can use the same matrices for sockets or hitboxes. Sampling leaves entity movement to your own systems; it does not extract root-motion deltas or send animation events. Bone references carry their skeleton identity, so use the character's pose when accessing its bones. An incompatible blend leaves the destination unchanged.

Imported model and animation content participates in the game's match compatibility, including assets read by its vertex defaults. Those dependencies include reads through constants, implicit struct defaults, operators and helper functions, and through another mesh's defaults. Changing their content requires the corresponding game build; a mismatched pack is refused before it replaces the running pack. Unrelated images and other visual assets can still reload without changing model compatibility.

## Load while playing

`SkinnedMesh<Vertex3>.Decode(bytes)` and its contracted form decode a self-contained GLB, returning a model `fails IOError`. Invalid data, external-resource references or missing contract requirements give `IOError.InvalidData`. Models and animation values can be stored after decoding.

Bytes can come from anywhere, so a small file can't ask for much. A model past any of these is `InvalidData` before anything is made for it:

| Limit | Most |
|---|---|
| Vertices (a part without normals has three a triangle) | 1,048,576 |
| Triangle corners (three a triangle) | 6,291,456 |
| Skeleton nodes, animations, buffers | 65,536 each |
| Skins | 4,096 |
| Inverse binds (skin joints, and one for each part no skin moves) | 262,144 |
| Vertex influences (joint/weight sets count four a vertex each) | 8,388,608 |
| Animation channels | 262,144 |
| Animation key numbers (times and values) | 16,777,216 |
| Blend shapes (morph targets of different names) | 4,096 |
| Vertices moved by blend shapes, each shape's counted | 2,097,152 |
| Numbers read from accessors, counted again for each part that names one | 67,108,864 |
| JSON values in the description | 4,194,304 |
| The file, and the decoded model | 512 MiB each |

An accessor with no buffer view is zeros and takes no bytes, but counts as much as one with them. A model at every limit at once takes about 600 MiB to decode. These limits are the engine's, the same for every game, so every machine in a match refuses the same files; `SkinnedMesh` and `Animations` assets are held to them too. The numbers are provisional.

A model refused for a limit says so on the log of each machine that decoded it, naming the limit, as `tide: SkinnedMesh.Decode refused a model: it has 2,000,000 vertices, more than the 1,048,576 a decoded model holds (TIDE_SKINNED_MAX_VERTICES)`. A build-time asset past one is an error that says the same.

Get bytes with [files or Web.Fetch](./files.md), separately from decoding. Files fetched on one machine are local; content that affects match bones must enter through synchronized [commands](./commands.md) or other match data. Decoding itself is deterministic and can run in match code.

Decoded vertices use your vertex struct's defaults too. Assets those defaults read have the same compatibility requirement even when the game declares no imported skinned model.

glTF assets support multiple skins, all supplied consecutive joint/weight sets, animated non-joint parents, and STEP, LINEAR and CUBICSPLINE channels. FBX imports polygon meshes, linear skin weights, blend shapes and baked node transforms; textures remain separate assets. Animated or GPU blend shapes, IK, animation events and an automatic playback controller are not implemented.
