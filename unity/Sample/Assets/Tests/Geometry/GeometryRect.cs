using UnityEngine;

// A component of a Box2D box's rules, for GeometryTests: the shape the Scene view draws of it
[Tide.TideShape("rect", "size", "offset")]
public sealed class GeometryRect : Tide.TideComponent
{
    public Vector2 size = Vector2.one;
    public Vector2 offset;

    public override string TideName => "GeometryRect";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
