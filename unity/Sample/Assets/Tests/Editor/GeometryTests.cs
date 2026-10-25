using System.Collections.Generic;
using NUnit.Framework;
using Tide;
using Tide.Editor;
using UnityEngine;

// What the Scene view draws of physics packages' components, as plain geometry (TideGeometry), since batch runs have
// no window to look at; filling a height field from a Terrain; and a part's points shown back from its body's frame.
// The components (Geometry/) carry the attributes tide writes from Box2D's and Box3D's unity.json.
public class GeometryTests
{
    readonly List<Object> made = new List<Object>();

    T Made<T>(T o) where T : Object
    {
        made.Add(o);
        return o;
    }

    GameObject Thing(string name) => Made(new GameObject(name));

    [TearDown]
    public void Clean()
    {
        foreach (var o in made)
        {
            if (o != null) Object.DestroyImmediate(o);
        }
        made.Clear();
    }

    static void Near(Vector3 expected, Vector3 actual, string what = "")
    {
        Assert.Less(Vector3.Distance(expected, actual), 1e-4f, $"{what}: {expected} is not {actual}");
    }

    // A polygon is drawn as its points' convex hull, counterclockwise, closed and rounded; a chain in its order, closed
    // while it loops
    [Test]
    public void AnOutlineIsItsPointsHullOrItsOrder()
    {
        var polygon = Thing("Polygon").AddComponent<GeometryPolygon>();
        polygon.points = new List<Vector2> { new Vector2(0, 0), new Vector2(1, 0.2f), new Vector2(2, 0), new Vector2(1, 1) };
        polygon.radius = 0.25f;
        Assert.IsTrue(TideGeometry.Outline(polygon, out var points, out var closed, out var radius, out var flat));
        Assert.IsTrue(closed && flat);
        Assert.AreEqual(0.25f, radius);
        Assert.AreEqual(3, points.Count, "the point inside isn't the hull's");
        Near(new Vector3(0, 0), points[0]);
        Near(new Vector3(2, 0), points[1]);
        Near(new Vector3(1, 1), points[2]);

        var chain = Thing("Chain").AddComponent<GeometryChain>();
        chain.points = new List<Vector2> { new Vector2(0, 0), new Vector2(1, 1), new Vector2(2, 0) };
        Assert.IsTrue(TideGeometry.Outline(chain, out points, out closed, out _, out _));
        Assert.IsFalse(closed);
        Near(new Vector3(1, 1), points[1]);
        chain.loop = true;
        TideGeometry.Outline(chain, out _, out closed, out _, out _);
        Assert.IsTrue(closed);
    }

    // A joint's anchors are in its bodies' frames: their positions and turns, not their scales; a handle's point in the
    // world goes back into a body's frame
    [Test]
    public void AJointsAnchorsAreInItsBodiesFrames()
    {
        var a = Thing("A");
        a.transform.SetPositionAndRotation(new Vector3(1, 0, 0), Quaternion.Euler(0, 90, 0));
        a.transform.localScale = new Vector3(3, 3, 3);
        var b = Thing("B");
        b.transform.position = new Vector3(0, 5, 0);
        var joint = Thing("Hinge").AddComponent<GeometryJoint>();
        joint.a = a;
        joint.b = b;
        joint.anchorA = new Vector3(0, 0, 1);
        joint.anchorB = new Vector3(0, -1, 0);
        joint.rotationA = Quaternion.Euler(0, 0, 90);
        Assert.IsTrue(TideGeometry.JointOf(joint, out var j));
        Near(new Vector3(2, 0, 0), j.WorldA, "A's anchor, turned with A");
        Near(new Vector3(0, 4, 0), j.WorldB, "B's anchor");
        Near(a.transform.rotation * Quaternion.Euler(0, 0, 90) * Vector3.right, j.TurnA * Vector3.right, "A's frame");
        Near(joint.anchorA, (Vector3)TideGeometry.AnchorIn(a.transform, j.WorldA, typeof(Vector3)), "back into A's frame");
        Assert.AreEqual(0.5f, (float)TideGeometry.TurnIn(b.transform, Quaternion.Euler(0, 0, 0.5f * Mathf.Rad2Deg), typeof(float)),
            1e-5f, "an angle about z in B's frame");
    }

    // A mesh's wireframe is its triangles' edges, its vertices by its scale and moved by its offset; a hull's, its points
    [Test]
    public void AMeshIsDrawnByItsScaleAndOffset()
    {
        var mesh = Thing("Ramp").AddComponent<GeometryMesh>();
        mesh.vertices = new List<Vector3> { Vector3.zero, Vector3.forward, Vector3.right, new Vector3(1, 0, 1) };
        mesh.indices = new List<int> { 0, 1, 2, 2, 1, 3 };
        mesh.scale = new Vector3(2, 1, 1);
        mesh.offset = new Vector3(0, 1, 0);
        Assert.IsTrue(TideGeometry.MeshOf(mesh, 100, out var lines, out var points));
        Assert.IsNull(points);
        Assert.AreEqual(12, lines.Count, "three edges a triangle, two points an edge");
        Near(new Vector3(2, 1, 0), lines[4], "the first triangle's third vertex");
        Assert.IsTrue(TideGeometry.MeshOf(mesh, 1, out lines, out _));
        Assert.AreEqual(6, lines.Count, "one triangle of two at most one");
        mesh.indices = null;
        Assert.IsTrue(TideGeometry.MeshOf(mesh, 100, out lines, out points));
        Assert.IsNull(lines);
        Assert.AreEqual(4, points.Count);
        Near(new Vector3(2, 1, 1), points[3]);
    }

    // A Terrain's heights as a height field: every sample of a small one; every fourth of a 513 one, as Box3D takes
    // 65,536 at most (257 a side is 66,049); a cell a hole where every one of the terrain's in it is; filled into the
    // component with its scale and bounds; its grid drawn
    [Test]
    public void ATerrainIsAHeightField()
    {
        var data = Made(new TerrainData { heightmapResolution = 33 });
        data.size = new Vector3(64, 10, 32);
        var heights = new float[33, 33];
        for (var z = 0; z < 33; z++)
        {
            for (var x = 0; x < 33; x++) heights[z, x] = x / 32f * 0.5f + z / 32f * 0.25f;
        }
        data.SetHeights(0, 0, heights);
        heights = data.GetHeights(0, 0, 33, 33); // As Unity keeps them, in 16 bits
        var field = TideGeometry.Sample(data);
        Assert.AreEqual(1, field.Step);
        Assert.AreEqual(33, field.Columns);
        Assert.AreEqual(33, field.Rows);
        Assert.AreEqual(heights[2, 3], field.Heights[3 + 2 * 33], 1e-4f, "x + z * columns");
        Near(new Vector3(2, 10, 1), field.Scale, "a cell's size and the heights'");
        Assert.AreEqual(0, field.Holes.Count, "no holes: none listed");

        var big = Made(new TerrainData { heightmapResolution = 513 });
        big.size = new Vector3(512, 20, 512);
        var surface = new bool[512, 512];
        for (var z = 0; z < 512; z++)
        {
            for (var x = 0; x < 512; x++) surface[z, x] = !(x < 4 && z < 4) && !(x == 4 && z == 0);
        }
        big.SetHoles(0, 0, surface);
        field = TideGeometry.Sample(big);
        Assert.AreEqual(4, field.Step);
        Assert.AreEqual(129, field.Columns);
        Assert.LessOrEqual(field.Columns * field.Rows, TideGeometry.MostSamples);
        Assert.AreEqual(128 * 128, field.Holes.Count);
        Assert.AreEqual(1, field.Holes[0], "every terrain cell in it a hole");
        Assert.AreEqual(0, field.Holes[1], "one of its sixteen a hole");

        var ground = Thing("Ground").AddComponent<GeometryField>();
        TideTerrains.Fill(ground, data);
        Assert.AreEqual(33, ground.columns);
        Assert.AreEqual(33 * 33, ground.heights.Count);
        Near(new Vector3(2, 10, 1), ground.scale);
        Assert.AreEqual(0f, ground.minimumHeight);
        Assert.AreEqual(1f, ground.maximumHeight);
        Assert.IsTrue(TideGeometry.TerrainOf(ground, 8, out var lines));
        // 33 samples a side drawn every 4th (8 lines at most each way): 9 lines of 32 segments each way
        Assert.AreEqual(2 * 9 * 32 * 2, lines.Count);
        Near(new Vector3(64, heights[0, 32] * 10, 0), lines[63], "the first row's last sample");
    }

    // A part's points, which the match has in its body's frame with its scale, shown back as authored: the fan of
    // unity_physics_components_by_their_rules, 3 up its kite, scaled (2, 1) and turned half a turn; a mirrored list in
    // the order it was authored in
    [Test]
    public void APartsPointsAreShownAsAuthored()
    {
        var kite = Thing("Kite");
        kite.transform.SetPositionAndRotation(new Vector3(10, 0, 0), new Quaternion(0, 0, 1, 0));
        var fan = Thing("Fan");
        fan.transform.SetParent(kite.transform, false);
        fan.transform.localPosition = new Vector3(0, 3, 0);
        fan.transform.localRotation = new Quaternion(0, 0, 1, 0);
        fan.transform.localScale = new Vector3(2, 1, 1);
        var part = fan.AddComponent<GeometryPart>();
        part.of = kite;
        var polygon = fan.AddComponent<GeometryPolygon>();
        var shown = TideParts.Points(new List<Vector2> { new Vector2(-2, 3), new Vector2(0, 2), new Vector2(2, 3) }, polygon);
        Near(new Vector2(1, 0), shown[0]);
        Near(new Vector2(0, 1), shown[1]);
        Near(new Vector2(-1, 0), shown[2]);
        Near(new Vector2(0, -0.5f), TideParts.Points(new Vector2(0, 3.5f), polygon), "a capsule's end");

        var ridge = Thing("Ridge");
        ridge.transform.localScale = new Vector3(-1, 1, 1);
        var chain = ridge.AddComponent<GeometryChain>();
        shown = TideParts.Points(new List<Vector2> { new Vector2(-2, 1), new Vector2(-1, 0), new Vector2(0, 0) }, chain);
        Near(new Vector2(0, 0), shown[0], "mirrored back, in its order");
        Near(new Vector2(2, 1), shown[2]);
    }
}
