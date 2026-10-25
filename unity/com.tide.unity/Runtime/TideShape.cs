using System;
using UnityEngine;

namespace Tide
{
    // What the Scene view draws of a Tide component, and edits with handles, from its package's unity.json ("shape"):
    // a box, sphere or capsule, or in the XY plane a rect or circle. Its fields by name: the size (a box's or rect's
    // size, a sphere's, capsule's or circle's radius), the centre (none: the GameObject's position) and a capsule's
    // height. They're as authored: the Scene view applies the GameObject's scale, as it does for Unity's colliders.
    [AttributeUsage(AttributeTargets.Class, Inherited = false)]
    public sealed class TideShapeAttribute : Attribute
    {
        public readonly string Kind, Size, Center, Height;

        public TideShapeAttribute(string kind, string size, string center = "", string height = "")
        {
            Kind = kind;
            Size = size;
            Center = center;
            Height = height;
        }
    }

    // A component whose lists its inspector fills from a Mesh (its package's unity.json, "mesh"): the vertices' field
    // (float3s), and the indices' (ints, three a triangle; none for a hull). The GameObject's MeshFilter's mesh as the
    // component's added.
    [AttributeUsage(AttributeTargets.Class, Inherited = false)]
    public sealed class TideMeshAttribute : Attribute
    {
        public readonly string Vertices, Indices;

        public TideMeshAttribute(string vertices, string indices = "")
        {
            Vertices = vertices;
            Indices = indices;
        }
    }

    // A component that makes its entity a part of another's body (its package's unity.json, "part"): `Of`, its field
    // naming the body's GameObject (none: the nearest above with the body, which the package finds as the match starts).
    [AttributeUsage(AttributeTargets.Class, Inherited = false)]
    public sealed class TidePartAttribute : Attribute
    {
        public readonly string Of;

        public TidePartAttribute(string of)
        {
            Of = of;
        }
    }

    // A field that's its package's answer (a joint's force: its unity.json's "readBack"): shown in Play, never authored
    [AttributeUsage(AttributeTargets.Field, Inherited = false)]
    public sealed class TideReadBackAttribute : Attribute
    {
    }

    // Where a shape is on its body, as the match has it (in the body's frame, its unity.json's "placed"), shown from the
    // GameObject as authored: a part's from its body's GameObject, any other's divided by its scale.
    public static class TideParts
    {
        static readonly System.Collections.Generic.Dictionary<Type, System.Reflection.FieldInfo> partFields =
            new System.Collections.Generic.Dictionary<Type, System.Reflection.FieldInfo>();

        // The GameObject whose body `shape`'s entity is a part of, or null for none
        public static GameObject BodyOf(Component shape)
        {
            foreach (var c in shape.GetComponents<TideComponent>())
            {
                var type = c.GetType();
                if (!partFields.TryGetValue(type, out var field))
                {
                    var part = (TidePartAttribute)Attribute.GetCustomAttribute(type, typeof(TidePartAttribute), false);
                    field = part != null ? type.GetField(part.Of) : null;
                    partFields[type] = field;
                }
                if (field?.GetValue(c) is GameObject body && body != shape.gameObject) return body;
            }
            return null;
        }

        static Vector3 FromBody(Vector3 value, Component shape, GameObject body)
        {
            var world = body.transform.position + body.transform.rotation * value;
            return Quaternion.Inverse(shape.transform.rotation) * (world - shape.transform.position);
        }

        public static Vector3 Placed(Vector3 value, Component shape, TideScale.By by)
        {
            var body = BodyOf(shape);
            return body != null ? TideScale.Unscale(FromBody(value, shape, body), shape.transform, by)
                : TideScale.Unscale(value, shape.transform, by);
        }

        public static Vector3 Placed(Vector3 value, Component shape)
        {
            var body = BodyOf(shape);
            return body != null ? FromBody(value, shape, body) : value;
        }

        public static Vector2 Placed(Vector2 value, Component shape, TideScale.By by)
        {
            var body = BodyOf(shape);
            return body != null ? TideScale.Unscale((Vector2)FromBody(value, shape, body), shape.transform, by)
                : TideScale.Unscale(value, shape.transform, by);
        }

        public static Vector2 Placed(Vector2 value, Component shape)
        {
            var body = BodyOf(shape);
            return body != null ? (Vector2)FromBody(value, shape, body) : value;
        }
    }

    // A field by the GameObject's scale (its package's unity.json, "scaled"): authored without it, as Unity's colliders'
    // sizes are, baked and in the match times it. In Play a component shows its entity's value divided by it again.
    public static class TideScale
    {
        // As tide unity reads them: each axis (Scale), each axis without its sign (AbsScale), the largest axis without
        // its sign (of all, of x and y, of x and z), or one axis without its sign
        public enum By { Scale, AbsScale, LargestScale, LargestScaleXY, LargestScaleXZ, ScaleX, ScaleY, ScaleZ }

        // The factor of an axis (0, 1 or 2; any for a number) by a lossy scale
        public static float Factor(Vector3 scale, By by, int axis)
        {
            var a = new Vector3(Mathf.Abs(scale.x), Mathf.Abs(scale.y), Mathf.Abs(scale.z));
            switch (by)
            {
                case By.Scale: return scale[axis];
                case By.AbsScale: return a[axis];
                case By.LargestScale: return Mathf.Max(a.x, Mathf.Max(a.y, a.z));
                case By.LargestScaleXY: return Mathf.Max(a.x, a.y);
                case By.LargestScaleXZ: return Mathf.Max(a.x, a.z);
                case By.ScaleX: return a.x;
                case By.ScaleY: return a.y;
                case By.ScaleZ: return a.z;
            }
            return 1;
        }

        static float Divide(float value, float by) => by != 0 ? value / by : value;

        public static float Unscale(float value, Transform transform, By by) =>
            Divide(value, Factor(transform.lossyScale, by, 0));

        public static Vector2 Unscale(Vector2 value, Transform transform, By by)
        {
            var s = transform.lossyScale;
            return new Vector2(Divide(value.x, Factor(s, by, 0)), Divide(value.y, Factor(s, by, 1)));
        }

        public static Vector3 Unscale(Vector3 value, Transform transform, By by)
        {
            var s = transform.lossyScale;
            return new Vector3(Divide(value.x, Factor(s, by, 0)), Divide(value.y, Factor(s, by, 1)),
                Divide(value.z, Factor(s, by, 2)));
        }
    }
}
