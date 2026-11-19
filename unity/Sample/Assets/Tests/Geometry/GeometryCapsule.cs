using UnityEngine;

// A component of a Box3D capsule's rules, for GeometryTests: the shape the Scene view draws of it
[Tide.TideShape("capsule", "radius", "offset", "height")]
public sealed class GeometryCapsule : Tide.TideComponent
{
    public float radius = 0.5f;
    public float height = 2f;
    public Vector3 offset;

    public override string TideName => "GeometryCapsule";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
