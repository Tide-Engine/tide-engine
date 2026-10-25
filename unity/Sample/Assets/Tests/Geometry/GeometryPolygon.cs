using System.Collections.Generic;
using UnityEngine;

// A component of a Box2D polygon's rules, for GeometryTests: its points drawn as their hull, rounded
[Tide.TideOutline(new[] { "points" }, "radius", "", true, true)]
public sealed class GeometryPolygon : Tide.TideComponent
{
    public List<Vector2> points = new List<Vector2>();
    public float radius;

    public override string TideName => "GeometryPolygon";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
