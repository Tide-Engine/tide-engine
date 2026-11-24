using System.Collections.Generic;
using NUnit.Framework;
using Tide;
using Tide.Editor;
using UnityEditor;
using UnityEngine;

// The Scene view's wireframes of physics packages' primitive shapes (TideShapes), held against Unity's own colliders:
// a Tide box, sphere, capsule, rect or circle is drawn where a BoxCollider, SphereCollider, CapsuleCollider,
// BoxCollider2D or CircleCollider2D of the same fields on the same GameObject is, as plain geometry, since batch runs
// have no window to look at. And when one is drawn: with its GameObject or one above it selected, as a collider is,
// or always where that's asked for. The components (Geometry/) carry the attributes tide writes from Box2D's and
// Box3D's unity.json.
public class ShapeTests
{
    readonly List<Object> made = new List<Object>();
    bool always;

    GameObject Thing(string name, Transform under = null)
    {
        var thing = new GameObject(name);
        made.Add(thing);
        if (under != null) thing.transform.SetParent(under, false);
        return thing;
    }

    [SetUp]
    public void Keep()
    {
        always = TideShapes.Always;
    }

    [TearDown]
    public void Clean()
    {
        TideShapes.Always = always;
        Selection.activeObject = null;
        foreach (var o in made)
        {
            if (o != null) Object.DestroyImmediate(o);
        }
        made.Clear();
    }

    static void Same(Bounds unity, TideComponent shape, string what)
    {
        Assert.IsTrue(TideShapes.WorldBounds(shape, out var drawn), $"{what}: it has a shape to draw");
        Assert.Greater(unity.size.magnitude, 0.01f, $"{what}: Unity's collider has its bounds in this run");
        const float by = 4e-4f;
        Assert.Less(Vector3.Distance(unity.center, drawn.center), by, $"{what}: drawn about {drawn.center}, Unity's about {unity.center}");
        Assert.Less(Vector3.Distance(unity.size, drawn.size), by, $"{what}: drawn {drawn.size} across, Unity's {unity.size}");
        // ...and every point of the wires that are drawn is in that box, and on a round shape's surface (the next test)
        var lines = new List<Vector3>();
        Assert.IsTrue(TideShapes.Wires(shape, lines) && lines.Count > 0, $"{what}: it has wires");
        var roomy = unity;
        roomy.Expand(2e-3f);
        foreach (var point in lines) Assert.IsTrue(roomy.Contains(point), $"{what}: a wire's point {point} is outside Unity's bounds {unity}");
    }

    // Placements a shape is tried at: plain, scaled each axis its own, turned, mirrored, and under a parent that's
    // moved, turned and scaled
    IEnumerable<(string, GameObject)> Places(string name)
    {
        var plain = Thing(name + " plain");
        yield return ("plain", plain);
        var scaled = Thing(name + " scaled");
        scaled.transform.position = new Vector3(3, -2, 5);
        scaled.transform.localScale = new Vector3(2, 0.5f, 3);
        yield return ("scaled", scaled);
        var turned = Thing(name + " turned");
        turned.transform.SetPositionAndRotation(new Vector3(-4, 1, 2), Quaternion.Euler(20, 35, 50));
        turned.transform.localScale = new Vector3(1.5f, 2.5f, 0.75f);
        yield return ("turned and scaled", turned);
        var mirrored = Thing(name + " mirrored");
        mirrored.transform.SetPositionAndRotation(new Vector3(1, 1, 1), Quaternion.Euler(0, 90, 0));
        mirrored.transform.localScale = new Vector3(-2, 3, 1.25f);
        yield return ("mirrored", mirrored);
        var parent = Thing(name + " parent");
        parent.transform.SetPositionAndRotation(new Vector3(6, 0, -3), Quaternion.Euler(0, 40, 15));
        parent.transform.localScale = Vector3.one * 2;
        var child = Thing(name + " child", parent.transform);
        child.transform.localPosition = new Vector3(1, 2, 3);
        child.transform.localRotation = Quaternion.Euler(10, 0, 30);
        child.transform.localScale = new Vector3(0.5f, 1.5f, 1);
        yield return ("under a parent", child);
    }

    [Test]
    public void BoxesSpheresAndCapsulesAreDrawnWhereUnitysCollidersAre()
    {
        foreach (var (where, thing) in Places("Box"))
        {
            var box = thing.AddComponent<GeometryBox>();
            box.size = new Vector3(1, 2, 3);
            box.offset = new Vector3(0.5f, -0.25f, 1);
            var unity = thing.AddComponent<BoxCollider>();
            unity.size = box.size;
            unity.center = box.offset;
            Physics.SyncTransforms();
            Same(unity.bounds, box, "a box " + where);
        }
        foreach (var (where, thing) in Places("Sphere"))
        {
            var sphere = thing.AddComponent<GeometrySphere>();
            sphere.radius = 0.75f;
            sphere.offset = new Vector3(-0.5f, 0.25f, 1);
            var unity = thing.AddComponent<SphereCollider>();
            unity.radius = sphere.radius;
            unity.center = sphere.offset;
            Physics.SyncTransforms();
            Same(unity.bounds, sphere, "a sphere " + where);
        }
        foreach (var (where, thing) in Places("Capsule"))
        {
            var capsule = thing.AddComponent<GeometryCapsule>();
            capsule.radius = 0.4f;
            capsule.height = 2.5f;
            capsule.offset = new Vector3(0.25f, 0.5f, -1);
            var unity = thing.AddComponent<CapsuleCollider>();
            unity.direction = 1; // Along y, as Box3D's
            unity.radius = capsule.radius;
            unity.height = capsule.height;
            unity.center = capsule.offset;
            Physics.SyncTransforms();
            Same(unity.bounds, capsule, "a capsule " + where);
        }
    }

    // Flat shapes, turned about z alone as a 2D body is
    [Test]
    public void RectsAndCirclesAreDrawnWhereUnitys2DCollidersAre()
    {
        var places = new[]
        {
            (at: new Vector3(0, 0, 0), turn: 0f, scale: new Vector3(1, 1, 1)),
            (at: new Vector3(3, -2, 0), turn: 0f, scale: new Vector3(2, 0.5f, 1)),
            (at: new Vector3(-4, 1, 0), turn: 35f, scale: new Vector3(1.5f, 2.5f, 1)),
            (at: new Vector3(1, 1, 0), turn: -120f, scale: new Vector3(-2, 3, 1)),
        };
        foreach (var place in places)
        {
            var thing = Thing("Rect");
            thing.transform.SetPositionAndRotation(place.at, Quaternion.Euler(0, 0, place.turn));
            thing.transform.localScale = place.scale;
            var rect = thing.AddComponent<GeometryRect>();
            rect.size = new Vector2(1, 2);
            rect.offset = new Vector2(0.5f, -0.25f);
            var unity = thing.AddComponent<BoxCollider2D>();
            unity.size = rect.size;
            unity.offset = rect.offset;
            Physics2D.SyncTransforms();
            Same(unity.bounds, rect, $"a rect at {place.at} turned {place.turn} scaled {place.scale}");

            var round = Thing("Circle");
            round.transform.SetPositionAndRotation(place.at, Quaternion.Euler(0, 0, place.turn));
            round.transform.localScale = place.scale;
            var circle = round.AddComponent<GeometryCircle>();
            circle.radius = 0.75f;
            circle.offset = new Vector2(-0.5f, 0.25f);
            var unityCircle = round.AddComponent<CircleCollider2D>();
            unityCircle.radius = circle.radius;
            unityCircle.offset = circle.offset;
            Physics2D.SyncTransforms();
            Same(unityCircle.bounds, circle, $"a circle at {place.at} turned {place.turn} scaled {place.scale}");
        }
    }

    // In Play a part's offset is the match's, in its body's frame: shown back as authored (TideParts.Placed, which the
    // generated ReadLive calls), the shape is drawn where the match has it, which is where Unity's collider is
    [Test]
    public void APartsShapeInPlayIsDrawnWhereTheMatchHasIt()
    {
        var body = Thing("Cart");
        body.transform.SetPositionAndRotation(new Vector3(5, 1, -2), Quaternion.Euler(0, 60, 20));
        var wheel = Thing("Wheel", body.transform);
        wheel.transform.localPosition = new Vector3(1, -0.5f, 2);
        wheel.transform.localScale = new Vector3(2, 2, 2);
        wheel.AddComponent<GeometryPart>().of = body;
        var box = wheel.AddComponent<GeometryBox>();
        var authored = new Vector3(0.25f, 0.5f, -0.75f);
        box.size = new Vector3(1, 0.5f, 1);
        // As tide bakes it (unity_assets.c's put_placed): times the scale, in the world by the part's placement, then
        // in the body's frame by its position and turn
        var world = wheel.transform.position + wheel.transform.rotation * Vector3.Scale(authored, wheel.transform.lossyScale);
        var baked = Quaternion.Inverse(body.transform.rotation) * (world - body.transform.position);
        box.offset = TideParts.Placed(baked, box, TideScale.By.Scale);
        Assert.Less(Vector3.Distance(authored, box.offset), 1e-4f, $"the match's offset is shown as authored: {box.offset}");
        var unity = wheel.AddComponent<BoxCollider>();
        unity.size = box.size;
        unity.center = authored;
        Physics.SyncTransforms();
        Same(unity.bounds, box, "a part's box in Play");
        // The body moves, as the match moves it: the part's GameObject is under it, and the shape with it
        body.transform.SetPositionAndRotation(new Vector3(-3, 4, 1), Quaternion.Euler(30, -45, 0));
        Physics.SyncTransforms();
        Same(unity.bounds, box, "a part's box after its body moved");
    }

    // The wires themselves, which are what the gizmo hands Unity to draw as lines: a box's are its twelve edges
    // between its eight corners, a rect's its four sides, and every point of a round shape's is on its surface
    [Test]
    public void AShapesWiresAreItsEdgesAndRings()
    {
        var thing = Thing("Box");
        thing.transform.SetPositionAndRotation(new Vector3(1, 2, 3), Quaternion.Euler(10, 20, 30));
        thing.transform.localScale = new Vector3(2, 1, 0.5f);
        var box = thing.AddComponent<GeometryBox>();
        box.size = new Vector3(1, 2, 3);
        box.offset = new Vector3(0.25f, 0, -1);
        var lines = new List<Vector3>();
        Assert.IsTrue(TideShapes.Wires(box, lines));
        Assert.AreEqual(24, lines.Count, "twelve edges");
        var corners = new HashSet<Vector3>();
        for (var i = 0; i < lines.Count; i += 2)
        {
            // Each is an edge: its ends are corners (every axis at a side), and it runs along one axis of the box
            Vector3 a = thing.transform.InverseTransformPoint(lines[i]) - box.offset, b = thing.transform.InverseTransformPoint(lines[i + 1]) - box.offset;
            var along = 0;
            for (var axis = 0; axis < 3; axis++)
            {
                Assert.AreEqual(box.size[axis] * 0.5f, Mathf.Abs(a[axis]), 1e-4f, $"edge {i / 2} starts at a corner: {a}");
                Assert.AreEqual(box.size[axis] * 0.5f, Mathf.Abs(b[axis]), 1e-4f, $"edge {i / 2} ends at a corner: {b}");
                if (Mathf.Abs(a[axis] - b[axis]) > 1e-4f) along++;
            }
            Assert.AreEqual(1, along, $"edge {i / 2} runs along one axis: {a} to {b}");
            corners.Add(new Vector3(Mathf.Round(a.x * 100), Mathf.Round(a.y * 100), Mathf.Round(a.z * 100)));
            corners.Add(new Vector3(Mathf.Round(b.x * 100), Mathf.Round(b.y * 100), Mathf.Round(b.z * 100)));
        }
        Assert.AreEqual(8, corners.Count, "between all eight corners");

        var flat = Thing("Rect").AddComponent<GeometryRect>();
        flat.size = new Vector2(2, 1);
        lines.Clear();
        Assert.IsTrue(TideShapes.Wires(flat, lines));
        Assert.AreEqual(8, lines.Count, "a rect's four sides");
        foreach (var point in lines)
        {
            Assert.AreEqual(1f, Mathf.Abs(point.x), 1e-5f);
            Assert.AreEqual(0.5f, Mathf.Abs(point.y), 1e-5f);
            Assert.AreEqual(0f, point.z, 1e-5f);
        }

        var ball = Thing("Sphere");
        ball.transform.position = new Vector3(-2, 0, 4);
        ball.transform.localScale = new Vector3(1, 3, 2);
        var sphere = ball.AddComponent<GeometrySphere>();
        sphere.radius = 0.5f;
        lines.Clear();
        Assert.IsTrue(TideShapes.Wires(sphere, lines));
        Assert.AreEqual(3 * 64 * 2, lines.Count, "three rings");
        foreach (var point in lines) Assert.AreEqual(1.5f, Vector3.Distance(point, ball.transform.position), 1e-4f, "on the sphere, by its largest scale");
        for (var i = 1; i + 1 < 64 * 2; i += 2) Assert.Less(Vector3.Distance(lines[i], lines[i + 1]), 1e-5f, "a ring's lines join");
        Assert.Less(Vector3.Distance(lines[0], lines[64 * 2 - 1]), 1e-4f, "and close");

        var pill = Thing("Capsule").AddComponent<GeometryCapsule>();
        pill.radius = 0.5f;
        pill.height = 3;
        lines.Clear();
        Assert.IsTrue(TideShapes.Wires(pill, lines));
        foreach (var point in lines)
        {
            // On the capsule: half a unit from its middle line, which runs a unit up and a unit down
            var onLine = new Vector3(0, Mathf.Clamp(point.y, -1, 1), 0);
            Assert.AreEqual(0.5f, Vector3.Distance(point, onLine), 1e-4f, $"on the capsule: {point}");
        }
        var most = 0f;
        foreach (var point in lines) most = Mathf.Max(most, Mathf.Abs(point.y));
        Assert.AreEqual(1.5f, most, 1e-4f, "its height from end to end");
        // One shorter than it's wide is a sphere, as Box3D makes it
        pill.height = 0.2f;
        lines.Clear();
        TideShapes.Wires(pill, lines);
        foreach (var point in lines) Assert.AreEqual(0.5f, point.magnitude, 1e-4f);
    }

    // A shape is drawn while its GameObject or one above it is selected, as a collider on a Rigidbody's child is drawn
    // with its body; one that isn't, only where Always Show Tide Shapes is on, and then as an unselected one
    [Test]
    public void ShapesAreDrawnWhenUnitysCollidersAre()
    {
        TideShapes.Always = false;
        Assert.IsTrue(TideShapes.Drawn(GizmoType.Selected | GizmoType.Active | GizmoType.InSelectionHierarchy, out var selected) && selected,
            "the selected GameObject's");
        Assert.IsTrue(TideShapes.Drawn(GizmoType.InSelectionHierarchy | GizmoType.NonSelected, out selected) && selected,
            "one under the selected GameObject, as a body's parts");
        Assert.IsFalse(TideShapes.Drawn(GizmoType.NonSelected | GizmoType.NotInSelectionHierarchy, out _), "none that isn't selected");
        TideShapes.Always = true;
        Assert.IsTrue(TideShapes.Drawn(GizmoType.NonSelected | GizmoType.NotInSelectionHierarchy, out selected) && !selected,
            "every one where that's asked for, an unselected one fainter");
        Assert.IsTrue(TideShapes.Drawn(GizmoType.InSelectionHierarchy, out selected) && selected);
        Assert.Less(TideShapes.Faint.a, TideShapes.Wire.a);
        TideShapes.Always = false;
        Assert.IsFalse(EditorPrefs.GetBool("Tide.AlwaysShowShapes", true), "the choice is this machine's, kept");

        // Unity asks for every Tide component where unselected ones are drawn: only types with something to draw
        Assert.IsTrue(TideShapes.Shaped(typeof(GeometryBox)) && TideShapes.Shaped(typeof(GeometryCircle)));
        Assert.IsTrue(TideShapes.Shaped(typeof(GeometryPolygon)) && TideShapes.Shaped(typeof(GeometryJoint)));
        Assert.IsTrue(TideShapes.Shaped(typeof(GeometryMesh)) && TideShapes.Shaped(typeof(GeometryField)));
        Assert.IsFalse(TideShapes.Shaped(typeof(GeometryPart)), "a part has no shape of its own");

        // Unity calls the drawing for the selected GameObject's components and those under it: its DrawGizmo says so
        var draw = typeof(TideShapes).GetMethod("Draw", System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
        var when = (DrawGizmo)System.Attribute.GetCustomAttribute(draw, typeof(DrawGizmo));
        Assert.IsNotNull(when, "the drawing is a gizmo of TideComponent's");
        var flags = when.drawOptions;
        Assert.IsTrue((flags & GizmoType.InSelectionHierarchy) != 0 && (flags & GizmoType.NonSelected) != 0 && (flags & GizmoType.Selected) != 0,
            $"asked for selected, under a selection and unselected: {flags}");
    }
}
