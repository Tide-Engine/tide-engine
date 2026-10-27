---
description: "Generate animations and clip transitions offline with Tide Motion, batch model work and import the resulting GLB animations into Tide."
---

# Tide Motion

[Tide Motion](https://github.com/Tide-Engine/tide-motion) is a standalone native
animation authoring CLI. Kimodo generates human motion from text; MotionBricks
generates transitions between clips. It exports ordinary `.glb` animation assets
for your game and can use Tide to render skeleton previews.

The models use **SOMA-30**, with canonical names, hierarchy and proportions.
Tide Motion retargets between SOMA and other humanoid rigs using a reusable bone
mapping. Outputs contain an animated skeleton, without a character mesh; they
drive the skinned character you already have. Generated movement still needs
review for foot contact, root travel and the destination game.

## Install

Download your archive, its `.sha256` file and the matching installer from
[Tide Motion releases](https://github.com/Tide-Engine/tide-motion/releases/latest).
Release builds target Windows x64, Linux x64/ARM64 and Apple Silicon macOS.
Windows/Linux x64 require AVX2. Linux targets Ubuntu 24.04 or compatible system
libraries; macOS targets version 15 or newer. macOS binaries are unsigned and
unnotarized. Intel Mac and Windows ARM64 archives are not provided.

On Windows, using the downloaded version's filename:

```powershell
./install.ps1 -Archive ./tide-motion-0.2.0-windows-x64.zip -AddToPath
```

Reopen the terminal after adding PATH. On Linux/macOS, select your platform's
archive:

```sh
sh install.sh ./tide-motion-0.2.0-linux-x64.tar.gz
export PATH="$HOME/.local/share/tide-motion/bin:$PATH"
```

Persist that PATH entry in your shell profile. Prebuilt packages include ONNX
Runtime and need no Python environment or compiler. Model downloads use `curl`.
Tide Motion has its own installer and command; install it separately from `tide`.

Read the [model licenses and notices](https://github.com/Tide-Engine/tide-motion/tree/dev/licenses),
then download the models once:

```sh
tide-motion models install all --accept-licenses
```

The download is about 10 GB. Allow at least 12 GB of available RAM for CPU text
generation. `models install bricks --accept-licenses` downloads just the
transition model when you already have compatible clips. Downloads resume and
verify pinned hashes; generation works offline afterward. `--models DIR` or
`TIDE_MOTION_MODELS` selects another cache. Reinstalling preserves cached models.

## Generate and connect

```sh
tide-motion generate "A person walks forward slowly" --duration 3 --seed 4 -o walk.glb
tide-motion generate "A person stands still and waves" --duration 3 --seed 7 -o wave.glb
tide-motion connect walk.glb wave.glb --offset 1.5 0 0 --joined -o sequence.glb
tide-motion inspect sequence.glb
```

`connect` uses the source's final frames and target's first frames. `--offset`
places the target clip explicitly in metres; it does not choose a path or turn
the target. `--joined` exports both clips and their generated transition as one
animation. Omit it to export just the bridge.

MotionBricks inputs must be self-contained GLB files on the canonical SOMA rig. Bake cubic
tracks to LINEAR first. Scale tracks and matrix nodes are unsupported. Existing
output paths are refused. Each generated file has a JSON recipe beside it.

## Retarget to your character

List the character's bones and copy the
[example mapping](https://github.com/Tide-Engine/tide-motion/blob/dev/examples/humanoid.json),
replacing its values with the exact names in your rig:

```sh
tide-motion inspect character.glb --skeleton
tide-motion retarget sequence.glb --target character.glb --map humanoid.json -o character-sequence.glb
```

Mapping keys name SOMA bones, and values name your character's bones. Required
keys are `Hips`, `Chest`, `Head`, and each side's `Arm`, `ForeArm`, `Hand`, `Leg`,
`Shin`, `Foot` with `Left`/`Right` prefixes. Other SOMA bones are optional. Map
shoulders and toes when available; extra unmapped helper/twist bones are allowed.
Bone and ancestor names must be unique and nonempty, and mapped bones must keep
their anatomical ancestor order.

The tool calibrates mapped segment directions between reference poses, including
A-pose/T-pose differences, and transfers rotations while preserving target bone
lengths. Root travel scales by leg length, and hip separation establishes facing.
The mapping can override `rootScale`, `headingDegrees`, `groundOffset` and local
reference quaternions through `referenceRotations`; see the
[retargeting reference](https://github.com/Tide-Engine/tide-motion#retarget-a-humanoid).
There is no automatic bone assignment, helper-bone twist distribution or contact IK.
Non-root translation animation is reported and omitted to keep target lengths fixed.

For existing animations on your character rig, convert them to SOMA first:

```sh
tide-motion retarget existing.glb --to-soma --map humanoid.json -o existing-soma.glb
```

Connect clips on SOMA, then retarget the joined result back to the character. The
same mapping works in both directions. Retargeting runs on the CPU without loading
inference models and writes an animation-only GLB with the target hierarchy.
Use `--clip NAME` to select a source clip. Positive static uniform scale and
centimetre armature wrappers are supported; changing scale, nonuniform/negative
scale, morph tracks and cubic tracks need an appropriate export/bake first.

## Batch actions

Put related work in one manifest to load each model once:

```json
{
  "actions": [
    { "action": "generate", "prompt": "A person walks forward slowly", "duration": 3, "seed": 4, "output": "walk.glb" },
    { "action": "generate", "prompt": "A person stands still and waves", "duration": 3, "seed": 7, "output": "wave.glb" },
    { "action": "connect", "from": "walk.glb", "to": "wave.glb", "offset": [1.5, 0, 0], "joined": true, "output": "sequence.glb" }
  ]
}
```

Save as `jobs.json`, then run:

```sh
tide-motion batch jobs.json
```

Paths are relative to the manifest. Actions run in order and can consume earlier
outputs. Each backend loads on first use and stays loaded until the batch ends.
The text encoder also stays resident to avoid repeated weight reads. Actions run
sequentially; the command releases models when it exits. A failed action stops
the batch and leaves completed outputs available.

To retarget an earlier output, append an action with `"action": "retarget"`,
`"source": "sequence.glb"`, `"target": "character.glb"`, `"map": "humanoid.json"`
and a new `output` path. For the reverse direction, set `"toSoma": true` and omit
`target`. Retarget actions reuse the ordinary manifest-relative paths and do not
load an inference backend.

## Preview and use in Tide

With `tide` installed:

```sh
tide-motion preview character-sequence.glb --capture sequence.png --frames 90
```

This renders the animated skeleton from front and side. Use `--tide PATH` to
select a Tide executable. The preview does not supply a skinned character.

Copy the generated GLB into your game's assets and import its single clip:

```csharp
asset Animation movement = "art/character-sequence.glb";
```

Use the [animation APIs](../game/animation.md#animation-files) to sample into your
character's default pose, then draw that character. Offline
retargeting produces the matching named bones and parent chains; Tide's runtime
uses its ordinary animation binding. The game ships the chosen animations and needs no
inference models or Tide Motion installation.

## Commercial use and licenses

The CLI source is Apache-2.0. Its selected Kimodo and MotionBricks motion models
permit commercial use under their NVIDIA model terms. The text encoder has
separate Meta Llama 3 terms. **Built with Meta Llama 3.** The
[repository licenses](https://github.com/Tide-Engine/tide-motion/tree/dev/licenses)
and packaged notices cover these separate components; the models are downloaded
separately from the executable.

You can use generated animations in commercial games, subject to those terms and
rights in your input assets. An exported animation does not require distributing
the model weights. Redistribution of models or an AI-powered tool has its own
license obligations. No evaluation motion data or research-only SMPL-X
checkpoints are included.
