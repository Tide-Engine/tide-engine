using System.Collections.Generic;
using UnityEngine;

// A component of a Box3D height field's rules, for GeometryTests
[Tide.TideTerrain("heights", "holes", "columns", "rows", "scale", "minimumHeight", "maximumHeight")]
public sealed class GeometryField : Tide.TideComponent
{
    public List<float> heights = new List<float>();
    public List<byte> holes = new List<byte>();
    public int columns, rows;
    public Vector3 scale = Vector3.one;
    public float minimumHeight, maximumHeight = 1;

    public override string TideName => "GeometryField";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
