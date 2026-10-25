using System.Collections.Generic;
using UnityEngine;

// A component of a Box3D mesh collider's rules, for GeometryTests
[Tide.TideMesh("vertices", "indices", "scale", "offset")]
public sealed class GeometryMesh : Tide.TideComponent
{
    public List<Vector3> vertices = new List<Vector3>();
    public List<int> indices = new List<int>();
    public Vector3 scale = Vector3.one;
    public Vector3 offset;

    public override string TideName => "GeometryMesh";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
