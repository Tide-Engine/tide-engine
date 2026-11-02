---
description: "Build meshes in Tide code with the Geometry package: boxes, spheres, planes and other shapes, normals and tangents, welding, merging and bounds."
---

# Geometry

`geometry` builds meshes in code, in namespace `Geometry`. It appends shapes to a game's own vertex and index lists, works out normals and tangents, welds vertices, merges meshes through transforms and measures their bounds. The lists it fills are the ones [`Draw.Mesh`](../game/meshes-and-textures.md) takes.

```sh
tide add geometry
```

Every function is [generic](../language/generics.md) over the game's vertex struct. It fills in the fields that struct has, by the names a [mesh asset](../game/assets.md#meshes) uses:

| Field | Type | What Geometry puts there |
|---|---|---|
| `position` | `float3`, or `float2` for a 2D mesh | Where the corner is |
| `normal` | `float3` | The way the surface faces |
| `uv` | `float2` | Where the texture is, (0, 0) at its top left |
| `tangent` | `float4` (or `float3`) | The way the texture's u goes; `w` is the side its v is on |

Every other field keeps the default the struct declares, such as `Color color = Color.white;`. A struct needs a `position` and nothing else.

```csharp
struct Corner
{
    float3 position;
    float3 normal;
}

singleton Scenery
{
    List<Corner> ball;
    List<int> ballFaces;
}

event(Spawned) Build(with Main, mut Scenery s)
{
    Geometry.Sphere(s.ball, s.ballFaces, radius: 1, rings: 12, segments: 20);
}

view Show(Scenery s, Marble marble)
{
    Draw.Mesh(s.ball, s.ballFaces, float4x4.TRS(marble.position, marble.roll, float3(1)), Glass);
}
```

All of it is ordinary Tide math in a fixed order. The same calls make the same bits on every machine, so match code can build collision shapes with it, as views build what they draw.

## Shapes

Each shape appends its vertices and triangles, and returns the index of its first new vertex. Each takes an optional `transform` last, which places it: positions move, normals and tangents turn, and a transform that mirrors turns the triangles over so their fronts stay outside. Triangles' fronts are clockwise, as everywhere in Tide.

| Function | What it makes |
|---|---|
| `Triangle(vertices, indices, a, b, c)` | One flat triangle; uvs (0, 0), (1, 0) and (0, 1) |
| `Quad(vertices, indices, a, b, c, d)` | A flat face, its corners clockwise from its top left; the texture across it |
| `Polygon(vertices, indices, outline)` | A flat convex polygon from a `List<float3>`, in triangles from its first corner |
| `Box(vertices, indices, size)` | A box around the origin with sharp edges: 24 vertices, the texture on each face |
| `Plane(vertices, indices, size, cellsX, cellsZ)` | A rectangle on the x-z plane facing +y, cut into cells whose vertices a game can move |
| `Sphere(vertices, indices, radius, rings, segments)` | A smooth ball around the origin |
| `Cylinder(vertices, indices, radius, height, segments)` | A cylinder on the y axis with flat ends |
| `Cone(vertices, indices, bottomRadius, topRadius, height, segments)` | A cone, or a cylinder narrower at one end |
| `Disc(vertices, indices, radius, segments)` | A flat disc on the x-z plane facing +y |

`Triangle`, `Quad` and `Polygon` also take `float2` corners, for 2D meshes. Counts of rings, segments and cells are kept between their least (3 segments, 2 rings, 1 cell) and 1,024, so a mistaken number can't make a mesh that takes all of a machine's memory.

## Working on a mesh

| Function | What it does |
|---|---|
| `RecalculateNormals(vertices, indices)` | Each vertex's normal from the triangles it's in, weighed by their areas: smooth where triangles share vertices, sharp where they don't |
| `RecalculateTangents(vertices, indices)` | Each vertex's tangent from its triangles' uvs, as a mesh asset makes the ones its file hasn't got. Needs `normal`, `uv` and a `float4 tangent` |
| `Append(vertices, indices, fromVertices, fromIndices, transform)` | Another mesh added through a transform: pieces, or copies of one, made one mesh that draws in one call |
| `TransformMesh(vertices, indices, transform)` | A mesh moved where it is |
| `Weld(vertices, indices)` | Vertices that are equal made one. The vertex struct declares `==` |
| `WeldPositions(vertices, indices, tolerance)` | Vertices whose positions are within `tolerance` made one, whatever else they hold |
| `RemoveUnused(vertices, indices)` | Triangles with a corner past the vertices or two corners the same taken out, then the vertices nothing uses |
| `BoundsOf(vertices)` | A `Bounds` around every position (`min`, `max`, `Center()`, `Size()`, `Extents()`, `Contains(point)`), or `null` for none |

A triangle that names a vertex the list hasn't got is left out by each of these, rather than read past the list.

### Ground from a plane

```csharp
mut List<Render3D.MeshVertex> ground = [];
mut List<int> faces = [];
Geometry.Plane(ground, faces, float2(200, 200), cellsX: 128, cellsZ: 128);
for (var k = 0; k < ground.count; k++)
{
    ground[k].position.y = Math.Perlin(ground[k].position.xz * 0.05) * 6;
}
Geometry.RecalculateNormals(ground, faces);
Geometry.RecalculateTangents(ground, faces);
```

### Pieces as one mesh

A mesh asset's lists go in like any others:

```csharp
asset Mesh<Corner> rock = "art/rock.glb";

foreach (var spot in spots)
{
    Geometry.Append(scenery.rocks, scenery.rockFaces, rock.vertices, rock.indices,
                    float4x4.TRS(spot.position, spot.turn, float3(spot.size)));
}
```

### Smooth normals for a welded mesh

A box's corners are 24 vertices, a face's own four each, because their normals differ. Welding by position alone makes 8, and normals worked out again make it round at the corners:

```csharp
Geometry.Box(blob, blobFaces, float3(1));
Geometry.WeldPositions(blob, blobFaces);
Geometry.RecalculateNormals(blob, blobFaces);
```

## Notes

- `RecalculateTangents` follows the same method as a mesh asset's generated tangents, in `float` rather than the importer's `double`, so the two are close but not bit for bit the same. Where a vertex's triangles disagree about the side the texture's v is on, the asset importer splits the vertex in two. `RecalculateTangents` keeps the vertex count and takes the +1 side.
- Lists in the match follow the match's rules: build them where the code can change them, such as a `Spawned` handler or a system.
