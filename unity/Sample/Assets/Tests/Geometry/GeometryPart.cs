using UnityEngine;

// A component of a Box2D part's rules, for GeometryTests: its entity's shapes are on `of`'s body
[Tide.TidePart("of")]
public sealed class GeometryPart : Tide.TideComponent
{
    public GameObject of;

    public override string TideName => "GeometryPart";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
