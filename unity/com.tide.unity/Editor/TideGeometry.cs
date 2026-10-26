using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;

namespace Tide.Editor
{
    // What the Scene view draws of a Tide component whose package says how (TideOutline, TideJoint, TideMesh,
    // TideTerrain), as plain geometry: the drawing (TideShapes) and its tests read it from here. Points are in the
    // GameObject's own space, as authored, unless said to be in the world.
    static class TideGeometry
    {
        const BindingFlags Fields = BindingFlags.Instance | BindingFlags.Public;

        static object Value(Component c, string field) =>
            string.IsNullOrEmpty(field) ? null : c.GetType().GetField(field, Fields)?.GetValue(c);

        static float Number(Component c, string field) => Value(c, field) is float f ? f : 0;

        // A field holding a point or a list of them, as Vector3s; `flat` for Vector2s
        static void Collect(object value, List<Vector3> into, ref bool flat)
        {
            switch (value)
            {
                case Vector3 v: into.Add(v); break;
                case Vector2 v: into.Add(v); flat = true; break;
                case List<Vector3> list: into.AddRange(list); break;
                case List<Vector2> list:
                    foreach (var v in list) into.Add(v);
                    flat = true;
                    break;
            }
        }

        // An outline's points in order (the convex hull's, counterclockwise, for a hull), whether it's closed, and its
        // radius as authored. False for a component without one.
        internal static bool Outline(Component c, out List<Vector3> points, out bool closed, out float radius, out bool flat)
        {
            points = new List<Vector3>();
            closed = false;
            radius = 0;
            flat = false;
            var of = c != null ? c.GetType().GetCustomAttribute<TideOutlineAttribute>(false) : null;
            if (of == null) return false;
            foreach (var field in of.Points) Collect(Value(c, field), points, ref flat);
            radius = Number(c, of.Radius);
            closed = of.Loops || Value(c, of.Closed) is true;
            if (of.Hull)
            {
                points = Hull(points);
                closed = points.Count > 2;
            }
            return true;
        }

        // The convex hull of points in the XY plane, counterclockwise (Andrew's monotone chain), as Box2D makes a
        // polygon of them
        internal static List<Vector3> Hull(List<Vector3> points)
        {
            var sorted = new List<Vector3>(points);
            sorted.Sort((a, b) => a.x != b.x ? a.x.CompareTo(b.x) : a.y.CompareTo(b.y));
            if (sorted.Count < 3) return sorted;
            float Cross(Vector3 o, Vector3 a, Vector3 b) => (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
            var hull = new List<Vector3>();
            for (var pass = 0; pass < 2; pass++)
            {
                var start = hull.Count;
                for (var k = 0; k < sorted.Count; k++)
                {
                    var p = sorted[pass == 0 ? k : sorted.Count - 1 - k];
                    while (hull.Count >= start + 2 && Cross(hull[hull.Count - 2], hull[hull.Count - 1], p) <= 0) hull.RemoveAt(hull.Count - 1);
                    hull.Add(p);
                }
                hull.RemoveAt(hull.Count - 1);
            }
            return hull;
        }

        // A joint's bodies' GameObjects and its anchors in the world: each body's position and turn, not its scale, as
        // the match's bodies have no scale. False for a component without one; a body not set leaves its anchor at its
        // own GameObject.
        internal struct Joint
        {
            public TideJointAttribute Of;
            public GameObject A, B;
            public Vector3 WorldA, WorldB;
            public Quaternion TurnA, TurnB; // Each anchor's frame in the world
        }

        internal static Quaternion Turn(object value) => value switch
        {
            Quaternion q => q,
            float angle => Quaternion.Euler(0, 0, angle * Mathf.Rad2Deg),
            _ => Quaternion.identity,
        };

        internal static Vector3 Anchor(object value) => value switch
        {
            Vector3 v => v,
            Vector2 v => v,
            _ => Vector3.zero,
        };

        internal static bool JointOf(Component c, out Joint joint)
        {
            joint = default;
            joint.Of = c != null ? c.GetType().GetCustomAttribute<TideJointAttribute>(false) : null;
            if (joint.Of == null) return false;
            joint.A = Value(c, joint.Of.A) as GameObject;
            joint.B = Value(c, joint.Of.B) as GameObject;
            var a = joint.A != null ? joint.A.transform : c.transform;
            var b = joint.B != null ? joint.B.transform : c.transform;
            joint.WorldA = a.position + a.rotation * Anchor(Value(c, joint.Of.AnchorA));
            joint.WorldB = b.position + b.rotation * Anchor(Value(c, joint.Of.AnchorB));
            joint.TurnA = a.rotation * Turn(Value(c, joint.Of.RotationA));
            joint.TurnB = b.rotation * Turn(Value(c, joint.Of.RotationB));
            return true;
        }

        // A point in the world as an anchor of a body's frame (a Vector2's in its plane)
        internal static object AnchorIn(Transform body, Vector3 world, Type type)
        {
            var local = Quaternion.Inverse(body.rotation) * (world - body.position);
            return type == typeof(Vector2) ? (object)(Vector2)local : local;
        }

        // A turn in the world as an anchor's in a body's frame (an angle about z for a float)
        internal static object TurnIn(Transform body, Quaternion world, Type type)
        {
            var local = Quaternion.Inverse(body.rotation) * world;
            if (type != typeof(float)) return local;
            var angle = local.eulerAngles.z;
            return (angle > 180 ? angle - 360 : angle) * Mathf.Deg2Rad;
        }

        // A mesh's edges as pairs of points (each triangle's three, at most `most` of them, the triangles spread over the
        // mesh beyond that), or for a hull its points: its vertices times the scale field, moved by the offset
        internal static bool MeshOf(Component c, int most, out List<Vector3> lines, out List<Vector3> points)
        {
            lines = null;
            points = null;
            var of = c != null ? c.GetType().GetCustomAttribute<TideMeshAttribute>(false) : null;
            if (of == null || !(Value(c, of.Vertices) is List<Vector3> vertices)) return false;
            var scale = Value(c, of.Scale) is Vector3 s ? s : Vector3.one;
            var offset = Value(c, of.Offset) is Vector3 o ? o : Vector3.zero;
            Vector3 At(int i) => Vector3.Scale(vertices[i], scale) + offset;
            if (!(Value(c, of.Indices) is List<int> indices))
            {
                points = new List<Vector3>(vertices.Count);
                for (var i = 0; i < vertices.Count; i++) points.Add(At(i));
                return true;
            }
            lines = new List<Vector3>();
            var triangles = indices.Count / 3;
            var step = Math.Max(1, (triangles + most - 1) / Math.Max(1, most));
            for (var t = 0; t < triangles; t += step)
            {
                int a = indices[t * 3], b = indices[t * 3 + 1], d = indices[t * 3 + 2];
                if ((uint)a >= vertices.Count || (uint)b >= vertices.Count || (uint)d >= vertices.Count) continue;
                lines.Add(At(a)); lines.Add(At(b));
                lines.Add(At(b)); lines.Add(At(d));
                lines.Add(At(d)); lines.Add(At(a));
            }
            return true;
        }

        // A height field's grid as pairs of points, at most `most` lines each way (every few samples beyond that): x by
        // column, z by row, y the height (between the bounds) by the scale
        internal static bool TerrainOf(Component c, int most, out List<Vector3> lines)
        {
            lines = null;
            var of = c != null ? c.GetType().GetCustomAttribute<TideTerrainAttribute>(false) : null;
            if (of == null || !(Value(c, of.Heights) is List<float> heights)) return false;
            var columns = Value(c, of.Columns) is int x ? x : 0;
            var rows = Value(c, of.Rows) is int z ? z : 0;
            lines = new List<Vector3>();
            if (columns < 2 || rows < 2 || heights.Count != columns * rows) return true;
            var scale = Value(c, of.Scale) is Vector3 s ? s : Vector3.one;
            var bounded = !string.IsNullOrEmpty(of.Minimum);
            float low = Number(c, of.Minimum), high = Number(c, of.Maximum);
            Vector3 At(int i, int j)
            {
                var h = heights[i + j * columns];
                if (bounded) h = Mathf.Clamp(h, low, high);
                return new Vector3(i * scale.x, h * scale.y, j * scale.z);
            }
            int across = Math.Max(1, (columns - 1 + most - 1) / most), down = Math.Max(1, (rows - 1 + most - 1) / most);
            for (var j = 0; j < rows; j += down)
            {
                for (var i = 0; i + 1 < columns; i++)
                {
                    lines.Add(At(i, j));
                    lines.Add(At(i + 1, j));
                }
            }
            for (var i = 0; i < columns; i += across)
            {
                for (var j = 0; j + 1 < rows; j++)
                {
                    lines.Add(At(i, j));
                    lines.Add(At(i, j + 1));
                }
            }
            return true;
        }

        // A Unity Terrain as a height field the package takes (Box3D's: 65,536 samples at most, 4,096 a side): every
        // `step`th sample, the fewest that fit, its heights from 0 to 1 by the terrain's height, a cell's size its
        // size over the cells. A cell is a hole where every one of the terrain's in it is. Holes: none where it has
        // none.
        internal struct Field
        {
            public int Columns, Rows, Step;
            public List<float> Heights;
            public List<byte> Holes;
            public Vector3 Scale;
        }

        internal const int MostSamples = 65536, MostSide = 4096;

        internal static Field Sample(TerrainData data)
        {
            var n = data.heightmapResolution;
            var step = 1;
            while ((n - 1) / step + 1 > MostSide || ((n - 1) / step + 1) * ((n - 1) / step + 1) > MostSamples) step *= 2;
            var field = new Field { Step = step, Columns = (n - 1) / step + 1, Rows = (n - 1) / step + 1 };
            var heights = data.GetHeights(0, 0, n, n); // [z, x]
            field.Heights = new List<float>(field.Columns * field.Rows);
            for (var j = 0; j < field.Rows; j++)
            {
                for (var i = 0; i < field.Columns; i++) field.Heights.Add(heights[j * step, i * step]);
            }
            var size = data.size;
            field.Scale = new Vector3(size.x / (field.Columns - 1), size.y, size.z / (field.Rows - 1));
            field.Holes = new List<byte>();
            var holes = data.holesResolution;
            if (holes <= 0) return field;
            var surface = data.GetHoles(0, 0, holes, holes); // [z, x], true where there's ground
            var any = false;
            var cells = new List<byte>((field.Columns - 1) * (field.Rows - 1));
            for (var j = 0; j + 1 < field.Rows; j++)
            {
                for (var i = 0; i + 1 < field.Columns; i++)
                {
                    var hole = true;
                    for (var z = j * step; z < (j + 1) * step && hole; z++)
                    {
                        for (var x = i * step; x < (i + 1) * step && hole; x++)
                        {
                            if (z < holes && x < holes && surface[z, x]) hole = false;
                        }
                    }
                    cells.Add(hole ? (byte)1 : (byte)0);
                    any |= hole;
                }
            }
            if (any) field.Holes = cells;
            return field;
        }
    }
}
