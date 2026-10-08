---
description: "Import skinned glTF models, sample and blend deterministic poses, use bone sockets, and draw animated characters in Tide."
---

# Skinned meshes and animation

A `SkinnedMesh<V>` holds a model's geometry, skeleton and animation clips. A `Pose` holds one character's current bone transforms. Several characters can share the same model while playing different animations.

## Import and draw

Import a glTF 2.0 `.glb` or `.gltf` with your [vertex struct](./assets.md#meshes), then sample an animation and draw its pose:

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

`Draw.SkinnedMesh(mesh, transform, pose)` takes the same texture, vert, frag and values arguments as a [3D mesh](./3d.md), and also works on a render target. Deformation applies before your `vert`, which receives the posed mesh-local position, normal and tangent. Use `hero.skeleton.RestPose()` to draw the authored rest pose.

## Checked names and dynamic lookup

A known asset provides animation and bone members from its source names: `Idle` becomes `idle`, and `RightHand` becomes `rightHand`. Only the first ASCII letter is lowercased. Invalid identifiers, keywords and colliding names have no dot member; the editor offers the members that are available.

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

A stored pose takes the latest tick's transforms in a view. For smoother playback, sample a view pose from the presentation time your game chooses, or blend two sampled poses explicitly. Keep an unwrapped clock when interpolating time across a loop boundary.

## Sockets and match use

`pose.Model(bone)` accumulates the hierarchy into a model-space matrix. Multiply it by the character's world transform to place an attachment:

```csharp
var hand = hero.skeleton.bones.rightHand;
var weaponTransform = Math.Mul(characterTransform, pose.Model(hand));
Draw.Mesh(sword, weaponTransform);
```

Systems can use the same matrices for sockets or hitboxes. Sampling leaves entity movement to your own systems; it does not extract root-motion deltas or send animation events. Bone references carry their skeleton identity. Sampling or blending an incompatible pose leaves the destination unchanged; an incompatible skinned draw produces no draw.

Imported model and animation content participates in the game's match compatibility. Changing that content requires the corresponding game build; a pack with different skinned content is refused before it replaces the running pack.

## Load while playing

`SkinnedMesh<Vertex3>.Decode(bytes)` and its contracted form decode a self-contained GLB, returning a model `fails IOError`. Invalid data, external-resource references or missing contract requirements give `IOError.InvalidData`. Models and animation values can be stored after decoding.

Get bytes with [files or Web.Fetch](./files.md), separately from decoding. Files fetched on one machine are local; content that affects match bones must enter through synchronized [commands](./commands.md) or other match data. Decoding itself is deterministic and can run in match code.

Assets support multiple skins, all supplied consecutive joint/weight sets, animated non-joint parents, and STEP, LINEAR and CUBICSPLINE channels. Morph targets, retargeting, IK, animation events and an automatic playback controller are not implemented.
