using UnityEngine;

// A component of a Box3D joint's rules, for GeometryTests
[Tide.TideJoint("a", "b", "anchorA", "anchorB", "rotationA", "rotationB")]
public sealed class GeometryJoint : Tide.TideComponent
{
    public GameObject a, b;
    public Vector3 anchorA, anchorB;
    public Quaternion rotationA = Quaternion.identity, rotationB = Quaternion.identity;

    public override string TideName => "GeometryJoint";

    protected override unsafe void ReadLive(byte* values, byte* locals, Tide.TidePacket packet, Tide.TideRunner runner)
    {
    }
}
