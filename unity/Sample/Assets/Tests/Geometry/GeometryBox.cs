using UnityEngine;

// A component of a Box3D box's rules, for GeometryTests: the shape the Scene view draws of it
[Tide.TideShape("box", "size", "offset")]
public sealed class GeometryBox : Tide.TideComponent
{
    public Vector3 size = Vector3.one;
    public Vector3 offset;

    public override string TideName => "GeometryBox";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
