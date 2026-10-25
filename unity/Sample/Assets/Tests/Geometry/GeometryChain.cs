using System.Collections.Generic;
using UnityEngine;

// A component of a Box2D chain's rules, for GeometryTests: its points in order, closed while it loops
[Tide.TideOutline(new[] { "points" }, "", "loop")]
public sealed class GeometryChain : Tide.TideComponent
{
    public List<Vector2> points = new List<Vector2>();
    public bool loop;

    public override string TideName => "GeometryChain";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
