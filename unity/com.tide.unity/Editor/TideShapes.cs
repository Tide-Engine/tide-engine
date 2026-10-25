using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEditor.EditorTools;
using UnityEditor.IMGUI.Controls;
using UnityEngine;

namespace Tide.Editor
{
    // The Scene view's shapes of Tide components whose package gives one (from its unity.json): a wireframe of a
    // selected one, as Unity draws a collider's, and an Edit Shape tool whose handles change it. A primitive's
    // (TideShape) size, radius, height and centre; an outline's (TideOutline) points; a joint's (TideJoint) anchors and
    // their turns. A mesh (TideMesh) and a height field (TideTerrain) are drawn, from the lists Unity's fill. The
    // fields are as authored; the shape is drawn with the GameObject's position, rotation and scale, the way Unity's
    // colliders are, a joint's anchors with their bodies' positions and turns.
    static class TideShapes
    {
        internal static readonly Color Wire = new Color(0.55f, 0.85f, 1f, 0.9f);

        // A component's shape, read from its fields: false for a component without one, or fields it doesn't have
        internal struct Shape
        {
            public TideShapeAttribute Of;
            public FieldInfo Size, Center, Height;
        }

        internal static bool Read(TideComponent component, out Shape shape)
        {
            shape = default;
            if (component == null) return false;
            var type = component.GetType();
            shape.Of = type.GetCustomAttribute<TideShapeAttribute>(false);
            if (shape.Of == null) return false;
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public;
            shape.Size = type.GetField(shape.Of.Size, flags);
            shape.Center = string.IsNullOrEmpty(shape.Of.Center) ? null : type.GetField(shape.Of.Center, flags);
            shape.Height = string.IsNullOrEmpty(shape.Of.Height) ? null : type.GetField(shape.Of.Height, flags);
            return shape.Size != null;
        }

        internal static bool Flat(Shape shape) => shape.Of.Kind == "rect" || shape.Of.Kind == "circle";

        internal static Vector3 Center(Shape shape, TideComponent c)
        {
            switch (shape.Center?.GetValue(c))
            {
                case Vector3 v: return v;
                case Vector2 v: return v;
                default: return Vector3.zero;
            }
        }

        static float Number(FieldInfo field, TideComponent c) => field?.GetValue(c) is float f ? f : 0;

        static Vector3 Size(Shape shape, TideComponent c)
        {
            switch (shape.Size.GetValue(c))
            {
                case Vector3 v: return v;
                case Vector2 v: return v;
                case float f: return new Vector3(f, f, f);
                default: return Vector3.zero;
            }
        }

        // The matrix its shape is drawn in: a box's or rect's with the scale, as a BoxCollider's; a round one's without
        // it, its radius and height times the scale's factor, as a SphereCollider's and CapsuleCollider's
        internal static Matrix4x4 Matrix(Shape shape, TideComponent c)
        {
            var t = c.transform;
            if (shape.Of.Kind == "box" || shape.Of.Kind == "rect") return t.localToWorldMatrix;
            return Matrix4x4.TRS(t.position, t.rotation, Vector3.one);
        }

        // A round shape's centre in its matrix: with the scale (its sign too)
        internal static Vector3 ScaledCenter(Shape shape, TideComponent c) => Vector3.Scale(Center(shape, c), c.transform.lossyScale);

        internal static float RadiusFactor(Shape shape, TideComponent c)
        {
            var s = c.transform.lossyScale;
            var by = shape.Of.Kind == "sphere" ? TideScale.By.LargestScale
                : shape.Of.Kind == "circle" ? TideScale.By.LargestScaleXY : TideScale.By.LargestScaleXZ;
            return TideScale.Factor(s, by, 0);
        }

        internal static bool Editing(TideComponent c) =>
            ToolManager.activeToolType == typeof(TideShapeTool) && Selection.activeGameObject == c.gameObject;

        // At most this many lines each way of a mesh's or height field's wireframe
        const int MostLines = 20000, MostGridLines = 64;

        [DrawGizmo(GizmoType.Selected | GizmoType.Active)]
        static void Draw(TideComponent component, GizmoType type)
        {
            if (component == null || !component.isActiveAndEnabled) return;
            DrawOutline(component);
            DrawJoint(component);
            DrawMesh(component);
            DrawTerrain(component);
            if (!Read(component, out var shape) || Editing(component)) return;
            using (new Handles.DrawingScope(Wire, Matrix(shape, component)))
            {
                var center = Center(shape, component);
                switch (shape.Of.Kind)
                {
                    case "box":
                        Handles.DrawWireCube(center, Size(shape, component));
                        break;
                    case "rect":
                        var r = Size(shape, component);
                        Handles.DrawWireCube(center, new Vector3(r.x, r.y, 0));
                        break;
                    case "sphere":
                        var radius = Number(shape.Size, component) * RadiusFactor(shape, component);
                        var at = ScaledCenter(shape, component);
                        Handles.DrawWireDisc(at, Vector3.up, radius);
                        Handles.DrawWireDisc(at, Vector3.right, radius);
                        Handles.DrawWireDisc(at, Vector3.forward, radius);
                        break;
                    case "circle":
                        Handles.DrawWireDisc(ScaledCenter(shape, component), Vector3.forward,
                            Number(shape.Size, component) * RadiusFactor(shape, component));
                        break;
                    case "capsule":
                        Capsule(ScaledCenter(shape, component), Number(shape.Size, component) * RadiusFactor(shape, component),
                            Number(shape.Height, component) * Mathf.Abs(component.transform.lossyScale.y));
                        break;
                }
            }
        }

        // An outline's line through its points in the world, rounded by its radius times the scale's (a flat one's in
        // the GameObject's plane): each side of an open one, the outside of a closed one, with arcs where they meet
        static void DrawOutline(TideComponent c)
        {
            if (!TideGeometry.Outline(c, out var points, out var closed, out var radius, out var flat) || points.Count == 0) return;
            var t = c.transform;
            var world = points.ConvertAll(p => t.TransformPoint(p));
            var r = radius * TideScale.Factor(t.lossyScale, flat ? TideScale.By.LargestScaleXY : TideScale.By.LargestScale, 0);
            var normal = t.forward;
            using (new Handles.DrawingScope(Wire))
            {
                var edges = closed ? world.Count : world.Count - 1;
                if (r <= 0)
                {
                    for (var i = 0; i < edges; i++) Handles.DrawLine(world[i], world[(i + 1) % world.Count]);
                    return;
                }
                var middle = Vector3.zero;
                foreach (var p in world) middle += p / world.Count;
                Vector3 Side(Vector3 a, Vector3 b, bool outward)
                {
                    var side = Vector3.Cross(normal, (b - a).normalized);
                    return outward && Vector3.Dot(side, (a + b) * 0.5f - middle) < 0 ? -side : side;
                }
                for (var i = 0; i < edges; i++)
                {
                    var a = world[i];
                    var b = world[(i + 1) % world.Count];
                    var side = Side(a, b, closed) * r;
                    Handles.DrawLine(a + side, b + side);
                    if (!closed) Handles.DrawLine(a - side, b - side);
                }
                if (!closed)
                {
                    Handles.DrawWireDisc(world[0], normal, r);
                    Handles.DrawWireDisc(world[world.Count - 1], normal, r);
                    return;
                }
                for (var i = 0; i < world.Count; i++)
                {
                    var before = Side(world[(i + world.Count - 1) % world.Count], world[i], true);
                    var after = Side(world[i], world[(i + 1) % world.Count], true);
                    Handles.DrawWireArc(world[i], normal, before, Vector3.SignedAngle(before, after, normal), r);
                }
            }
        }

        // A joint: a line between its anchors, a dotted one from each body to its anchor
        static void DrawJoint(TideComponent c)
        {
            if (!TideGeometry.JointOf(c, out var joint) || Editing(c)) return;
            using (new Handles.DrawingScope(Wire))
            {
                Handles.DrawLine(joint.WorldA, joint.WorldB);
                var size = HandleUtility.GetHandleSize(joint.WorldA) * 0.04f;
                Handles.DotHandleCap(0, joint.WorldA, Quaternion.identity, size, EventType.Repaint);
                Handles.DotHandleCap(0, joint.WorldB, Quaternion.identity, size, EventType.Repaint);
                if (joint.A != null) Handles.DrawDottedLine(joint.A.transform.position, joint.WorldA, 3);
                if (joint.B != null) Handles.DrawDottedLine(joint.B.transform.position, joint.WorldB, 3);
            }
        }

        // A mesh's triangles, or a hull's points, in the GameObject's space with its scale
        static void DrawMesh(TideComponent c)
        {
            if (!TideGeometry.MeshOf(c, MostLines, out var lines, out var points)) return;
            using (new Handles.DrawingScope(Wire, c.transform.localToWorldMatrix))
            {
                if (lines != null)
                {
                    Handles.DrawLines(lines.ToArray());
                    return;
                }
                foreach (var p in points)
                {
                    var size = HandleUtility.GetHandleSize(c.transform.TransformPoint(p)) * 0.02f;
                    Handles.DotHandleCap(0, p, Quaternion.identity, size, EventType.Repaint);
                }
            }
        }

        // A height field's grid, from the GameObject's origin, its corner
        static void DrawTerrain(TideComponent c)
        {
            if (!TideGeometry.TerrainOf(c, MostGridLines, out var lines) || lines.Count == 0) return;
            using (new Handles.DrawingScope(Wire, c.transform.localToWorldMatrix)) Handles.DrawLines(lines.ToArray());
        }

        // A field's new value, with Undo and as a prefab instance's change
        internal static void Assign(TideComponent c, FieldInfo field, object value)
        {
            if (field == null || value == null || !field.FieldType.IsInstanceOfType(value)) return;
            Undo.RecordObject(c, "Edit Tide Shape");
            field.SetValue(c, value);
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
        }

        // A capsule along y: `height` end to end, as a CapsuleCollider's
        static void Capsule(Vector3 center, float radius, float height)
        {
            var half = Mathf.Max(0, height * 0.5f - radius);
            var top = center + Vector3.up * half;
            var bottom = center - Vector3.up * half;
            Handles.DrawWireDisc(top, Vector3.up, radius);
            Handles.DrawWireDisc(bottom, Vector3.up, radius);
            foreach (var side in new[] { Vector3.right, Vector3.forward })
            {
                Handles.DrawLine(top + side * radius, bottom + side * radius);
                Handles.DrawLine(top - side * radius, bottom - side * radius);
                var normal = Vector3.Cross(side, Vector3.up);
                Handles.DrawWireArc(top, normal, side, -180, radius);
                Handles.DrawWireArc(bottom, normal, side, 180, radius);
            }
        }
    }

    // Edit Shape: the handles of a Tide component's shape, a tool of its own in the Scene view's toolbar and its
    // inspector, as Unity's Edit Collider is. A change is the component's fields, with Undo.
    [EditorTool("Edit Tide Shape", typeof(TideComponent))]
    sealed class TideShapeTool : EditorTool
    {
        readonly BoxBoundsHandle box = new BoxBoundsHandle();
        readonly SphereBoundsHandle sphere = new SphereBoundsHandle();
        readonly CapsuleBoundsHandle capsule = new CapsuleBoundsHandle();
        GUIContent icon;

        public override GUIContent toolbarIcon =>
            icon ??= new GUIContent(EditorGUIUtility.IconContent("EditCollider").image, "Edit the Tide component's shape");

        public override bool IsAvailable() => Editable(target as TideComponent);

        internal static bool Editable(TideComponent c) =>
            c != null && (TideShapes.Read(c, out _) || c.GetType().IsDefined(typeof(TideOutlineAttribute), false)
                          || c.GetType().IsDefined(typeof(TideJointAttribute), false));

        public override void OnToolGUI(EditorWindow window)
        {
            foreach (var t in targets)
            {
                var c = t as TideComponent;
                if (c == null) continue;
                if (TideShapes.Read(c, out var shape)) Edit(c, shape);
                EditOutline(c);
                EditJoint(c);
            }
        }

        // A handle on each of an outline's points, moved in the view's plane: a flat one's stays in the GameObject's
        static void EditOutline(TideComponent c)
        {
            var of = c.GetType().GetCustomAttribute<TideOutlineAttribute>(false);
            if (of == null) return;
            var t = c.transform;
            using (new Handles.DrawingScope(TideShapes.Wire))
            {
                foreach (var name in of.Points)
                {
                    var field = c.GetType().GetField(name, BindingFlags.Instance | BindingFlags.Public);
                    switch (field?.GetValue(c))
                    {
                        case Vector3 v:
                            if (Moved(t, v, out var moved3)) TideShapes.Assign(c, field, moved3);
                            break;
                        case Vector2 v:
                            if (Moved(t, v, out var moved2)) TideShapes.Assign(c, field, (Vector2)moved2);
                            break;
                        case List<Vector3> list:
                            for (var i = 0; i < list.Count; i++)
                            {
                                if (!Moved(t, list[i], out var m)) continue;
                                Undo.RecordObject(c, "Edit Tide Shape");
                                list[i] = m;
                                Changed(c);
                            }
                            break;
                        case List<Vector2> list:
                            for (var i = 0; i < list.Count; i++)
                            {
                                if (!Moved(t, list[i], out var m)) continue;
                                Undo.RecordObject(c, "Edit Tide Shape");
                                list[i] = m;
                                Changed(c);
                            }
                            break;
                    }
                }
            }
        }

        // A point's handle, the point as authored (the GameObject's own space) where it was moved
        static bool Moved(Transform t, Vector3 local, out Vector3 moved)
        {
            var world = t.TransformPoint(local);
            EditorGUI.BeginChangeCheck();
            var to = Handles.FreeMoveHandle(world, HandleUtility.GetHandleSize(world) * 0.05f, Vector3.zero, Handles.DotHandleCap);
            moved = t.InverseTransformPoint(to);
            return EditorGUI.EndChangeCheck();
        }

        static void Changed(TideComponent c)
        {
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
        }

        // A joint's anchors moved, and their frames turned, in the world: each kept in its body's frame
        static void EditJoint(TideComponent c)
        {
            if (!TideGeometry.JointOf(c, out var joint)) return;
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public;
            var type = c.GetType();
            Handles.color = TideShapes.Wire;
            Handles.DrawLine(joint.WorldA, joint.WorldB);
            var ends = new[]
            {
                (body: joint.A != null ? joint.A.transform : c.transform, at: joint.WorldA, turn: joint.TurnA, anchor: joint.Of.AnchorA,
                    rotation: joint.Of.RotationA),
                (body: joint.B != null ? joint.B.transform : c.transform, at: joint.WorldB, turn: joint.TurnB, anchor: joint.Of.AnchorB,
                    rotation: joint.Of.RotationB),
            };
            foreach (var end in ends)
            {
                var anchor = type.GetField(end.anchor, flags);
                var rotation = string.IsNullOrEmpty(end.rotation) ? null : type.GetField(end.rotation, flags);
                EditorGUI.BeginChangeCheck();
                var at = Handles.PositionHandle(end.at, rotation != null ? end.turn : end.body.rotation);
                if (EditorGUI.EndChangeCheck() && anchor != null)
                {
                    TideShapes.Assign(c, anchor, TideGeometry.AnchorIn(end.body, at, anchor.FieldType));
                }
                if (rotation == null) continue;
                var size = HandleUtility.GetHandleSize(end.at);
                EditorGUI.BeginChangeCheck();
                // An angle turns about the body's z alone; a quaternion any way
                var turned = rotation.FieldType == typeof(float)
                    ? Handles.Disc(end.turn, end.at, end.body.forward, size * 0.8f, false, 0)
                    : Handles.RotationHandle(end.turn, end.at);
                if (EditorGUI.EndChangeCheck()) TideShapes.Assign(c, rotation, TideGeometry.TurnIn(end.body, turned, rotation.FieldType));
            }
        }

        void Edit(TideComponent c, TideShapes.Shape shape)
        {
            var scale = c.transform.lossyScale;
            using (new Handles.DrawingScope(TideShapes.Wire, TideShapes.Matrix(shape, c)))
            {
                var center = TideShapes.Center(shape, c);
                switch (shape.Of.Kind)
                {
                    case "box":
                    case "rect":
                    {
                        var flat = shape.Of.Kind == "rect";
                        box.axes = flat ? PrimitiveBoundsHandle.Axes.X | PrimitiveBoundsHandle.Axes.Y : PrimitiveBoundsHandle.Axes.All;
                        box.center = center;
                        var size = shape.Size.GetValue(c);
                        box.size = size is Vector2 v2 ? new Vector3(v2.x, v2.y, 0) : size is Vector3 v3 ? v3 : Vector3.zero;
                        EditorGUI.BeginChangeCheck();
                        box.DrawHandle();
                        if (EditorGUI.EndChangeCheck())
                        {
                            Undo.RecordObject(c, "Edit Tide Shape");
                            Set(shape.Size, c, box.size);
                            Set(shape.Center, c, box.center);
                        }
                        break;
                    }
                    case "sphere":
                    case "circle":
                    {
                        var factor = TideShapes.RadiusFactor(shape, c);
                        sphere.axes = shape.Of.Kind == "circle"
                            ? PrimitiveBoundsHandle.Axes.X | PrimitiveBoundsHandle.Axes.Y : PrimitiveBoundsHandle.Axes.All;
                        sphere.center = TideShapes.ScaledCenter(shape, c);
                        sphere.radius = (shape.Size.GetValue(c) is float r ? r : 0) * factor;
                        EditorGUI.BeginChangeCheck();
                        sphere.DrawHandle();
                        if (EditorGUI.EndChangeCheck())
                        {
                            Undo.RecordObject(c, "Edit Tide Shape");
                            SetNumber(shape.Size, c, factor != 0 ? sphere.radius / factor : sphere.radius);
                            Set(shape.Center, c, Unscaled(sphere.center, scale));
                        }
                        break;
                    }
                    case "capsule":
                    {
                        var factor = TideShapes.RadiusFactor(shape, c);
                        var y = Mathf.Abs(scale.y);
                        capsule.heightAxis = CapsuleBoundsHandle.HeightAxis.Y;
                        capsule.center = TideShapes.ScaledCenter(shape, c);
                        capsule.radius = (shape.Size.GetValue(c) is float r ? r : 0) * factor;
                        capsule.height = (shape.Height?.GetValue(c) is float h ? h : 0) * y;
                        EditorGUI.BeginChangeCheck();
                        capsule.DrawHandle();
                        if (EditorGUI.EndChangeCheck())
                        {
                            Undo.RecordObject(c, "Edit Tide Shape");
                            SetNumber(shape.Size, c, factor != 0 ? capsule.radius / factor : capsule.radius);
                            SetNumber(shape.Height, c, y != 0 ? capsule.height / y : capsule.height);
                            Set(shape.Center, c, Unscaled(capsule.center, scale));
                        }
                        break;
                    }
                }
            }
        }

        static Vector3 Unscaled(Vector3 v, Vector3 scale) => new Vector3(scale.x != 0 ? v.x / scale.x : v.x,
            scale.y != 0 ? v.y / scale.y : v.y, scale.z != 0 ? v.z / scale.z : v.z);

        static void SetNumber(FieldInfo field, TideComponent c, float v)
        {
            if (field == null || field.FieldType != typeof(float)) return;
            field.SetValue(c, v);
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
        }

        // A Vector3 into a field that's a Vector3 or Vector2 (a flat shape's)
        static void Set(FieldInfo field, TideComponent c, Vector3 v)
        {
            if (field == null) return;
            if (field.FieldType == typeof(Vector2)) field.SetValue(c, (Vector2)v);
            else if (field.FieldType == typeof(Vector3)) field.SetValue(c, v);
            else return;
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
        }
    }
}
