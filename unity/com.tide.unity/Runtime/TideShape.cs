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
