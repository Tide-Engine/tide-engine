# Geometry

Mesh building for Tide, in namespace `Geometry`: shapes appended to a game's
own vertex and index lists, normals and tangents worked out, vertices welded,
meshes merged through transforms and measured. It is ordinary generic Tide
code, with no C library, and fills any vertex struct by the field names a mesh
asset uses (`position`, `normal`, `uv`, `tangent`). Games omitting it compile
none of it.

```sh
tide add geometry
```

```csharp
struct Corner
{
    float3 position;
    float3 normal;
}

Geometry.Sphere(scenery.ball, scenery.ballFaces, radius: 1, rings: 12, segments: 20);
Geometry.Box(scenery.crates, scenery.crateFaces, float3(1), float4x4.TRS(at, turn, float3(1)));
Geometry.RecalculateNormals(scenery.ground, scenery.groundFaces);
```

The lists it fills go to `Draw.Mesh`. See `docs/packages/geometry.md` for the
whole API.
