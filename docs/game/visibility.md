---
description: "Control Tide client state with scene, entity and whole-component visibility, shared audience groups, partial simulation and local availability events."
---

# State visibility

Visibility decides which match scenes, entities and components a player's machine receives. The server still runs the full match. A client predicts with the state it is allowed to have, and authoritative corrections supply consequences that its partial simulation could not reproduce.

The API names below are provisional. Visibility applies to entities and whole components. To hide a card's face while keeping its back public, put the face in a separate component; individual fields do not have visibility rules.

## Share an audience

An `Audience` is a set of players. `Audience.All` includes everyone and `Audience.None` includes nobody. `Audience.Only(player)` selects one player; `Audience.From(players)` takes a `List<PlayerID>`. An audience value has `Add(player)`, `Remove(player)` and `Contains(player)`.

A `VisibilityGroup` gives many entities one live audience:

```csharp
singleton Region { VisibilityGroup group; }
component Block { int kind; }
scene Main { }

event(Spawned) Setup(mut Region region, with Main)
{
    region.group = Visibility.CreateGroup(Audience.None);
    Spawn(Block { kind = 1 }, Visibility { group = region.group });
    Spawn(Block { kind = 2 }, Visibility { group = region.group });
}

event(PlayerJoined joined) Admit(Region region)
{
    Visibility.Set(region.group, Audience.Only(joined.player));
}
```

`Visibility.Set(group, audience)` replaces the audience for every entity using that group. Build an audience with all interested players before setting it. Changing an `Audience` variable after `CreateGroup` or `Set` does not change the group until another `Set` call.

For a voxel world, use one group per chunk and update its audience as players approach or leave. For distant players on a map, give each player entity its own group and compute the observers in match code. The engine supplies the mechanism; the game chooses distances, teams, occlusion and update frequency.

An entity without `Visibility` is public, subject to its enclosing scene. A default `Visibility` uses an all-player group. `Visibility.DestroyGroup(group)` makes existing references to that group see nobody; a later group does not reuse the stale handle.

## Restrict a scene

Scenes use the same groups. Put `Visibility` on the loaded root:

```csharp
var group = Visibility.CreateGroup(Audience.Only(player));
var hand = Scene.Load(Hand { player = player }, Visibility { group = group });
```

The root's group limits every entity owned by that scene, including entities
spawned later. `Visibility.Set(group, audience)` updates them together. Use
`Audience.None` for a server-only scene, or share a group across multiple scenes
and entities. Scene ownership still controls loading and unloading; destroying
a group conceals its members without unloading them.

## Hide a component

Keep public and private data in separate components:

```csharp
component Card { int back; }
component CardFace { int rank; int suit; }

event(Spawned) ProtectFace(Owner owner, mut Visibility visibility, with CardFace)
{
    visibility.Set<CardFace>(Audience.Only(owner.player));
}
```

The entity must carry `Visibility`. Set the restriction during construction, before initial state can be published; waiting for the next system tick can expose the initial component. Update the policy when the holder changes. `visibility.Set<CardFace>(audience)` restricts the whole component. It also accepts a shared group: `visibility.Set<CardFace>(handGroup)`. `visibility.Reset<CardFace>()` removes that override, so the component follows the entity again.

The effective audience is the intersection of the scene root's group, the entity's group and the component's restriction. An entity or component cannot broaden its enclosing scope. Ordinary transform parenting does not inherit visibility. A component outside a recipient's audience is absent on that client, including from queries and `Has<T>()`; `without CardFace` can therefore match there.

For poker, keep the public card back on the entity and restrict `CardFace` to the holder. Put an undealt deck in an audience of nobody, or a private scene with no members. Membership, group contents and component overrides are server policy; clients receive their permitted state rather than the policy's private membership data.

`Time.seed` is public, and default `Random` state derives from it. Hiding card faces alone does not make a shuffle derived from that public seed unpredictable. A secret shuffle needs a private seed supplied authoritatively, with its RNG state and undealt deck kept restricted.

## Spatial parents

A public child can remain visible while its transform parent or the parent's `Transform` is hidden. The projected hierarchy cuts that private link. Where the visible subtree contains a permitted `Transform`, an internal placement anchor preserves its composed placement, including rotated nonuniform scales. The child's local transform values stay unchanged; no private parent entity or component payload is copied.

That derived placement is part of the permitted spatial state. A visible entity with no permitted `Transform` in its visible subtree does not receive an anchor just because its parent has one. Calling `world.LocalToWorld` on such an entity in a partial client can omit hidden influence. `world.Parent` reflects the projected links, so a concealed parent can appear null. Choose visibility for spatial state according to what players may infer from visible children.

## Availability and actual lifetime

Use `local event(Shown)` to initialize presentation when an entity or a handler's required components become available. It runs for arrivals, reveals and genuine spawns. `local event(Hidden)` removes presentation when that availability ends, and reads the last permitted state. The same forms accept `verified Shown` and `verified Hidden`.

`local event(Spawned)` and `local event(Destroyed)` describe actual match lifetime. Joining, revealing an existing entity or receiving a repair does not fabricate either event. A concealed entity's later destruction does not announce its death to a recipient who could not see it at destruction. [`Forgotten`](./local-state.md#letting-go) remains the cleanup hook for local fields.

Each handler's component requirements determine its availability. A handler reading `Card` can remain active while a handler reading `CardFace` becomes hidden. A `Shown` handler should prepare resources; a true `Spawned` handler can play the effect for a new object.

**Provisional delivery rule:** availability follows the endpoints delivered to local presentation. Changes inside one tick or between frames coalesce to their final availability, so adding, removing and adding a component before a frame cannot leave its presentation released. A correction that restores an entity or component runs `Shown` again; an unchanged replay does nothing. Predicted and verified handlers follow separate presentation endpoints, and their queries read the corresponding old or new endpoint. A local rebuild clears this history and prepares available objects again, so `Shown` initialization should reuse or replace existing resources safely.

## Consequences across a visibility boundary

The server may simulate a hidden object affecting a public one. The client receives the corrected public result; it does not need the hidden cause. Design public state to contain the consequences players need, such as damage, a revealed projectile or a public combat log.

Custom `local event(...)` and `local event(verified ...)` handlers currently hear events produced by the client's permitted simulation. They are not a general authoritative server-to-audience event channel. A hidden system's custom event is not automatically forwarded to other players. Choose public fields and input payloads deliberately, and decide what each command handler publishes.

Commands execute only on the authoritative server in this path; clients receive permitted results. Projected clients also cannot become a new host: host migration needs a trusted complete-state handover, which is not currently implemented.

Match task continuations and opaque `Resident` state stay on the authoritative server in a visibility-enabled match. They are not copied into projected clients. Publish the state needed for presentation and prediction in ordinary permitted components. Public match singletons remain public, so keep per-entity secrets in components or private scenes.

The current physics packages also keep information about their whole world in public `Physics` fields. Box2D's `Physics.image` is a full native snapshot byte list. Box3D's `Physics.stamp` is a hash derived from native world operations. Both packages keep public collider and rewind-history lists. Hiding a `Body`, collider or entity does not restrict those values. Concealing players with these packages needs visibility-aware package integration first; omitting opaque `Resident` state does not hide the other public fields.

See [Scenes](./scenes.md#private-scenes) for scene ownership and audiences and [Networking](../multiplayer/networking.md#who-sees-what) for verification and corrections.
