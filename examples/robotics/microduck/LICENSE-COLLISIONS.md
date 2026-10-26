# MicroDuck derived collision data

Copyright Pollen Robotics.

The derived collision data in `collision_spheres.json`, its generated
representation `collisions.tide`, and the geometry measurements in
`collision_certificates.json` are identified as an adaptation under
**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
(CC BY-NC-SA 4.0)**.

Source: [Pollen Robotics microduck_rl](https://github.com/pollen-robotics/microduck_rl),
revision `b02fdea4902c2b93b7d34a83a6f05da4ececb7d3`.
The upstream [model license notice](https://github.com/pollen-robotics/microduck_rl/blob/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/LICENSE-MODELS.md)
identifies its STL assets as CC BY-NC-SA 4.0.

Changes: source mesh convex supports were converted into sphere proxies in
the original XML body frames. The selected model uses 131 support spheres,
26 source-inscribed interior cores, 16 retained procedural foot spheres, and
one original procedural visible-casing core appended last. That final core
is authored from the project visual casing, is not source-inscribed, and
deliberately extends body0 support. Frozen decimal
literals preserve the tested runtime geometry. No endorsement by Pollen
Robotics is claimed.

License: https://creativecommons.org/licenses/by-nc-sa/4.0/

Canonical legal code: https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode

A full text copy is supplied as `LICENSE-CC-BY-NC-SA-4.0.txt`.
The material is supplied as-is, without warranties, as specified by that license.

This notice identifies the collision data adaptation. The separate XML/model,
neural network, engine, and exporter code retain their applicable code or asset
licenses. This notice does not replace or extend those licenses.
