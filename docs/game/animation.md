---
description: "Import skinned glTF or FBX models, sample and blend deterministic poses, use bone sockets, and draw animated characters in Tide."
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

An FBX declaration works the same way: `asset SkinnedMesh<Vertex3> hero = "art/hero.fbx";`. Import happens when the game builds, without Autodesk software. FBX axes and units become Y-up meters in Tide's coordinate system. Its animation stacks become clips beginning at zero, with their authored duration. Nonlinear transform curves are resampled at 60 samples per second into LINEAR channels, retaining source keys within the clip and stepped changes. Geometry and inheritance transforms may add unnamed helper bones. The packed model uses the same format and sampling APIs as a glTF import.

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

Binding matches nodes by unique exact source names and matching parent chains; source node order and default poses can differ. This supports animation-only FBX exports whose defaults are the starting pose of a take. Required source local transforms are copied directly, including the defaults of unanimated ancestors. Sampling into a target pose requires the selected clip's animated nodes and their ancestors. Drawing a source pose requires all animated nodes declared by its source asset, any edited nodes and their ancestors, because a pose does not retain which clip was sampled. Unrelated target nodes keep their own rest transforms.

An extra unnamed identity scene root, as added by FBX importing, is ignored while its pose and relevant animation channels stay at rest. Animated wrappers or deeper unnamed ancestors cannot be bound this way.

A named node at the top of either hierarchy is folded into the bones below it when the other side has no node with its name. Blender writes the armature object as such a node above the skeleton's top bone, carrying the object's transform: a rig in centimetres gets a scale of 0.01. That transform, animated or not, is combined with its children's. A clip edited in Blender and exported again therefore drives a character exported elsewhere with its bones where they were authored, and a character exported from Blender plays clips exported elsewhere. The folded node's scale must be uniform, and it must be above a bone that binds.

When a clip doesn't bind, the first failed sample or draw for each model and clip prints the reason on the console and names the node that differs, such as `"Hips" is below "Armature" in it but below "Root" in the model`.

This supports separate files exported from the same named hierarchy. It does not retarget animation: source translations, rotations and scales are used as authored, without adapting proportions or correcting coordinate spaces. Missing or ambiguous required names and different parent chains reject binding. An incompatible sample leaves the destination unchanged, and an incompatible draw produces no draw. Bone identity and pose blending still require identical skeleton data.

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

glTF assets support multiple skins, all supplied consecutive joint/weight sets, animated non-joint parents, and STEP, LINEAR and CUBICSPLINE channels. FBX imports polygon meshes, linear skin weights, blend shapes and baked node transforms; textures remain separate assets. Animated or GPU blend shapes, retargeting, IK, animation events and an automatic playback controller are not implemented.
