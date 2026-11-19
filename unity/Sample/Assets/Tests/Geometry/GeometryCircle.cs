using UnityEngine;

// A component of a Box2D circle's rules, for GeometryTests: the shape the Scene view draws of it
[Tide.TideShape("circle", "radius", "offset")]
public sealed class GeometryCircle : Tide.TideComponent
{
    public float radius = 0.5f;
    public Vector2 offset;

    public override string TideName => "GeometryCircle";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
