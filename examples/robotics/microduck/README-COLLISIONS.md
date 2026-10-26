# Frozen MicroDuck collision data

The playable fixture uses this fixed model of 174 spheres per duck for
walking, recovery, grabbing, and prop collisions. `sandbox.tide` spawns the
records in their frozen order; neither control mode nor posture swaps geometry.

Record order is deliberate: 131 fitted non-foot support spheres, then 26
source-inscribed non-foot interior cores, then the 16 original procedural
foot spheres, then one original procedural casing core last. The first 173
records exactly preserve the tested prefix. Keep this order when creating
collider entities. Both original feet retain six
sole spheres and two nonsole spheres. Coordinates are meters in original
MJCF body frames; apply the inverse principal rotation only when spawning.
Do not subtract the center of mass or apply mesh centering again.

## Export and verification

From this directory, with Python 3.10 or newer:

```console
python tools/export_collisions.py
python tools/export_collisions.py --check
```

The exporter uses only the Python standard library. It verifies the frozen
JSON SHA-256 and schema, finite coordinates, body and foot assignments,
positive radii, complete body coverage, and original sole counts. It emits
deterministic UTF-8/LF Tide source. `--check` compares the expected bytes
without changing files. Keep the frozen JSON byte-for-byte, including LF
line endings. Editing it requires an intentional new artifact and hash.

This is a lossless export of the frozen numeric asset, not mesh fitting.
The private tested preparer emitted nine-significant-digit decimal literals.
Those exact decimal values are the staged JSON's numeric source of truth.
The exporter emits 17 significant digits, which round-trip those values and
preserve their tested float32 runtime conversions. The higher-precision
research coordinates and their hashes remain identified in provenance.

## Derivation and scope

The research fit used pinned MuJoCo 3.10.0, 4096 deterministic Fibonacci
directions plus six signed body axes, and a bounded greedy allocation of
150 radius-0.6-mm source-hull-vertex spheres. Source sole/nonsole labels were
preserved. Fitted foot bodies were subsequently replaced with all 16 original
procedural foot proxies to retain the verified walking plant. Twenty-six
cores were constrained inside individual original geom hulls and at least
2.02 mm inward of the selected non-foot support envelope.

The final body0 casing sphere, center(0.004,0,-0.006),radius0.0115m, is an
ORIGINAL procedural approximation of the visible black casing. It is NOT
a source-inscribed core. It deliberately increases body0 support by
6.674836674mm; sampled source-body0 overshoot is2.897309268mm with a
conservative continuous bound7.274836675mm before rounding allowance.
The stronger source-inscribed and inward-margin statements above apply
only to the preceding26 cores. No whole174 zero-support-growth claim is made.

Full mesh-fit regeneration remains a research workflow using the pinned
source assets, recorded numerical environment, and referenced research
scripts. It is not claimed portable across MuJoCo or numerical-library
versions. The portable tool here reads the frozen asset and needs no STL,
MuJoCo, NumPy, SciPy, network, simulator, or build cache.

`collision_provenance.json` records upstream-relative XML/STL hashes,
attribution, modifications, research inputs, generation settings, and the
tested record-order identity. `collision_certificates.json` summarizes the
continuous support bounds and their scope. Restored procedural feet are
outside the mesh-fit support certificate. Equal plane support does not
guarantee equal forces during penetration or equal body-pair contacts.

The derived collision data is separately identified as CC BY-NC-SA 4.0 in
`LICENSE-COLLISIONS.md`. Preserve that notice, Pollen Robotics credit, the
change summary, and provenance with the data. This bundle contains no STLs.

The original geometry staging trial passed 6/7 native locomotion cases and
1/1 static visible-casing coverage check. Those counts predate the corrected
A/D mapping and Shift controls. The current native/WASI basis passes 7/8,
with D/right turning still below its unchanged 0.25 rad threshold; see
`README.md` for the current measurements and validation scope. Staging itself
runs no episodes and does not change production files.
