using System;
using System.Reflection;
using UnityEditor;
using UnityEditor.EditorTools;
using UnityEditor.IMGUI.Controls;
using UnityEngine;

namespace Tide.Editor
{
    // The Scene view's shapes of Tide components whose package gives one (TideShape, from its unity.json): a
    // wireframe of a selected one, as Unity draws a collider's, and an Edit Shape tool whose handles change its size,
    // radius, height and centre. The fields are as authored; the shape is drawn with the GameObject's position,
    // rotation and scale, the way Unity's colliders are.
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

        [DrawGizmo(GizmoType.Selected | GizmoType.Active)]
        static void Draw(TideComponent component, GizmoType type)
        {
            if (!Read(component, out var shape) || !component.isActiveAndEnabled) return;
            if (ToolManager.activeToolType == typeof(TideShapeTool) && Selection.activeGameObject == component.gameObject) return;
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

        public override bool IsAvailable() => TideShapes.Read(target as TideComponent, out _);

        public override void OnToolGUI(EditorWindow window)
        {
            foreach (var t in targets)
            {
                var c = t as TideComponent;
                if (c == null || !TideShapes.Read(c, out var shape)) continue;
                Edit(c, shape);
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
