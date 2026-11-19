using UnityEngine;

// A component of a Box3D sphere's rules, for GeometryTests: the shape the Scene view draws of it
[Tide.TideShape("sphere", "radius", "offset")]
public sealed class GeometrySphere : Tide.TideComponent
{
    public float radius = 0.5f;
    public Vector3 offset;

    public override string TideName => "GeometrySphere";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
