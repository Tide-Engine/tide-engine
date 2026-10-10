---
description: "Import skinned glTF models, sample and blend deterministic poses, use bone sockets, and draw animated characters in Tide."
---

# Skinned meshes and animation

A `SkinnedMesh<V>` holds a model's geometry, skeleton and animation clips. A `Pose` holds one character's current bone transforms. Several characters can share the same model while playing different animations.

## Import and draw

Import a glTF 2.0 `.glb` or `.gltf` with your [vertex struct](./assets.md#meshes). A model needs no animation clips to draw: leaving out the pose draws its authored rest pose.

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

## Checked names and dynamic lookup

A known asset provides animation and bone members from its source names: `Idle` becomes `idle`, and `RightHand` becomes `rightHand`. Only the first ASCII letter is lowercased. Invalid identifiers, keywords and colliding names have no dot member; the editor offers the members that are available.

The editor reads the file's JSON catalog without loading geometry or animation samples, and refreshes the names when the file changes. Malformed JSON and unavailable metadata have separate diagnostics; a requested clip is only reported missing after its catalog was inspected successfully. For an asset URL, the editor uses an existing local cache; a build fetches uncached content.

```csharp
var model = hero; // Retains the known asset's checked catalog.
var idle = model.animations.idle;
var hand = model.skeleton.bones.rightHand;

if (model.animations.Find("Victory Dance") is Animation celebration)
{
    var pose = celebration.Sample(0);
    Draw.SkinnedMesh(model, float4x4.identity, pose);
}
```

`Find` uses the exact original name and returns an optional value; a duplicate original name is ambiguous and returns null. Both `animations` and `skeleton.bones` provide `count` and `At(index)` for enumeration. Unnamed entries are available through `At`; `Find("")` returns null, and contract aliases must be nonempty. `skeleton.Find(name)` also finds a bone. An animation has its original `name` and its `duration` in seconds.

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

The contract requires that subset; extra clips and bones remain discoverable through `Find` and `At`. Unaliased fields match normalized source identifiers; `[Name]` matches exact source spelling. Contracts stay available through helper parameters and collection elements. A build checks an imported asset's requirements; runtime decoding checks them before returning a model.

## Animation files

A separate glTF file can supply animation clips without importing drawable geometry. For the character above:

```csharp
asset Animations movement = "art/movement.glb";

view MovingCharacter(Character character)
{
    mut var pose = hero.defaultPose;
    pose.Sample(movement.run, character.seconds, Playback.Loop);
    Draw.SkinnedMesh(hero, float4x4.identity, pose);
}
```

An `Animations` asset holds every clip in the file and the skeleton those clips animate. A single known asset exposes its clips directly, such as `movement.run`. The editor completes these members and shows their original source names on hover, using the same rules as `hero.animations`. `movement.count`, `movement.Find("Run")` and `movement.At(index)` enumerate or find clips too. Copying with `var` retains checked names; a plain `Animations` helper parameter exposes lookup and enumeration.

Both `.glb` and `.gltf` are supported. The importer keeps animation and skeleton data, so an animation file need not contain a mesh. A file containing several clips imports them all; the declaration never chooses an arbitrary first clip.

`pose.Sample` binds the clip to the pose's skeleton. Use a pose from the character when you also need its bone references for edits or sockets. You can also sample the source clip with `movement.run.Sample(seconds)` and pass that pose directly to `Draw.SkinnedMesh`; the draw binds it to the character.

Binding matches nodes by unique exact source names, matching hierarchy and authored local transforms; source node order can differ. Sampling into a target pose requires the selected clip's animated nodes and their ancestors. Drawing a source pose requires all animated nodes declared by its source asset, any edited nodes and their ancestors, because a pose does not retain which clip was sampled. Unrelated unanimated nodes can differ, and unanimated target nodes keep their rest transforms.

This supports separate files exported from the same rig; it does not retarget animation between rigs with different proportions or rest transforms. An incompatible sample leaves the destination unchanged, and an incompatible draw produces no draw.

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

Get bytes with [files or Web.Fetch](./files.md), separately from decoding. Files fetched on one machine are local; content that affects match bones must enter through synchronized [commands](./commands.md) or other match data. Decoding itself is deterministic and can run in match code.

Decoded vertices use your vertex struct's defaults too. Assets those defaults read have the same compatibility requirement even when the game declares no imported skinned model.

Assets support multiple skins, all supplied consecutive joint/weight sets, animated non-joint parents, and STEP, LINEAR and CUBICSPLINE channels. Morph targets, retargeting, IK, animation events and an automatic playback controller are not implemented.
