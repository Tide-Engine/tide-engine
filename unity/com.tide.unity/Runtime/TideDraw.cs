using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace Tide
{
    // tide_draw_command as tide/draw.h lays it out (44 bytes): a command of what
    // Tide's views drew this frame (tide_embed_draw's packet).
    [StructLayout(LayoutKind.Explicit, Size = 44)]
    public struct TideDrawCommand
    {
        [FieldOffset(0)] public byte kind;
        [FieldOffset(1)] public byte blend;
        [FieldOffset(4)] public uint text; // TEXT's offset in the text; SHAPE's place among the shapes
        [FieldOffset(8)] public Vector2 a;
        [FieldOffset(16)] public Vector2 b;
        [FieldOffset(8)] public uint first; // MESH: its indices...
        [FieldOffset(12)] public uint count; // ...and how many
        [FieldOffset(20)] public ushort anchor; // TEXT: the place of its box at `a`
        [FieldOffset(28)] public Color color;
    }

    // tide_draw_shape_data (60 bytes): the rest of a smooth shape.
    [StructLayout(LayoutKind.Explicit, Size = 60)]
    public struct TideDrawShape
    {
        [FieldOffset(0)] public Vector2 turn;
        [FieldOffset(8)] public float radius;
        [FieldOffset(12)] public float outline;
        [FieldOffset(16)] public float soft;
        [FieldOffset(20)] public float sweep;
        [FieldOffset(24)] public uint form;
        [FieldOffset(28)] public Color color2;
        [FieldOffset(44)] public Color outlineColor;
    }

    // tide_vertex (32 bytes): a 2D mesh's corner.
    [StructLayout(LayoutKind.Sequential, Size = 32)]
    public struct TideDrawVertex
    {
        public Vector2 position;
        public Vector2 uv;
        public Color color;
    }

    // Draws what Tide's views drew (Draw's 2D calls) over Unity's frame, from the
    // frame's draw packet (tide/embed.h's tide_embed_draw): shapes, lines, 2D
    // meshes and text, under Tide's 2D cameras and in screen pixels after
    // Draw.Screen. TideRunner adds it with `drawTide`. Drawn with GL from OnGUI,
    // so in any render pipeline. Not yet: 3D meshes, a game's own shaders,
    // textures, render targets, clips and stencil (those commands are skipped),
    // and Tide's own font: text is Unity's.
    [DisallowMultipleComponent]
    public sealed unsafe class TideDraw : MonoBehaviour
    {
        enum Kind : byte
        {
            Clear, Camera, Circle, WireCircle, Rect, WireRect, Line, Text, Screen, Mesh, Clip, NoClip,
            Camera3D, Mesh3D, Shaded, Target, Blit, Shape, Stencil,
        }

        const uint Magic = 0x44444954; // "TIDD"
        const int Segments = 40;

        public TideRunner Runner { get; set; }

        Material material;
        GUIStyle textStyle;
        bool warned;

        // This frame's view of 2D: Tide's camera, or the screen's pixels
        bool screen;
        Vector2 center;
        float size;
        float width, height;

        void OnGUI()
        {
            if (Event.current.type != EventType.Repaint || Runner == null) return;
            var bytes = Runner.DrawPacket(out var length);
            if (bytes == null || length < 64) return;
            var header = (uint*)bytes;
            if (header[0] != Magic || header[1] != 1 || header[9] != sizeof(TideDrawCommand) || header[10] != sizeof(TideDrawShape)
                || header[11] != sizeof(TideDrawVertex))
            {
                if (!warned) Debug.LogWarning("Tide: the draw packet isn't one this package reads: update the package or tide so they agree.", this);
                warned = true;
                return;
            }
            uint commandCount = header[2], shapeCount = header[3], vertexCount = header[4], indexCount = header[5], textBytes = header[6];
            ulong at = 64;
            var commands = (TideDrawCommand*)(bytes + Part(ref at, commandCount, (uint)sizeof(TideDrawCommand)));
            var shapes = (TideDrawShape*)(bytes + Part(ref at, shapeCount, (uint)sizeof(TideDrawShape)));
            var vertices = (TideDrawVertex*)(bytes + Part(ref at, vertexCount, (uint)sizeof(TideDrawVertex)));
            var indices = (uint*)(bytes + Part(ref at, indexCount, 4));
            var text = bytes + Part(ref at, textBytes, 1);
            if (at > length) return;

            if (material == null)
            {
                material = new Material(Shader.Find("Hidden/Internal-Colored")) { hideFlags = HideFlags.HideAndDontSave };
                material.SetInt("_Cull", 0);
                material.SetInt("_ZWrite", 0);
                material.SetInt("_ZTest", (int)UnityEngine.Rendering.CompareFunction.Always);
            }
            width = Screen.width;
            height = Screen.height;
            screen = false;
            center = Vector2.zero;
            size = 0;
            var target = 0u;
            for (uint i = 0; i < commandCount; i++)
            {
                var c = commands[i];
                var kind = (Kind)c.kind;
                if (kind == Kind.Target)
                {
                    target = *(uint*)((byte*)&commands[i] + 8); // 0 is the window, which alone is drawn here
                    continue;
                }
                if (target != 0) continue;
                switch (kind)
                {
                case Kind.Camera:
                    screen = false;
                    center = c.a;
                    size = c.b.x;
                    break;
                case Kind.Screen:
                    screen = true;
                    break;
                case Kind.Rect:
                case Kind.WireRect:
                case Kind.Circle:
                case Kind.WireCircle:
                case Kind.Line:
                case Kind.Mesh:
                case Kind.Shape:
                    Begin(c.blend);
                    if (kind == Kind.Rect) Quad(c.a - c.b * 0.5f, c.a + c.b * 0.5f, c.color, c.color);
                    else if (kind == Kind.WireRect) WireBox(c.a, c.b, c.color);
                    else if (kind == Kind.Circle) Ellipse(c.a, new Vector2(c.b.x, c.b.x), c.color, c.color);
                    else if (kind == Kind.WireCircle) Ring(c.a, c.b.x, Pixel(), c.color);
                    else if (kind == Kind.Line) Line(c.a, c.b, Pixel(), c.color);
                    else if (kind == Kind.Mesh) Mesh(c, vertices, vertexCount, indices, indexCount);
                    else if (c.text < shapeCount) Shape(c, shapes[c.text]);
                    End();
                    break;
                case Kind.Text:
                    if (c.text < textBytes) Words(c, text + c.text, (int)(textBytes - c.text));
                    break;
                }
            }
        }

        static ulong Part(ref ulong at, uint count, uint each)
        {
            var from = (at + 7) & ~7ul;
            at = from + (ulong)count * each;
            return from;
        }

        void Begin(byte blend)
        {
            // Alpha over what's there, or added, as light is; the rest as alpha for now
            material.SetInt("_SrcBlend", (int)UnityEngine.Rendering.BlendMode.SrcAlpha);
            material.SetInt("_DstBlend", blend == 1 ? (int)UnityEngine.Rendering.BlendMode.One : (int)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
            material.SetPass(0);
            GL.PushMatrix();
            GL.LoadPixelMatrix(0, width, height, 0); // GUI pixels: from the top left, y down
            GL.Begin(GL.TRIANGLES);
        }

        static void End()
        {
            GL.End();
            GL.PopMatrix();
        }

        // Where a point of the units the commands are in is, in pixels from the top left.
        Vector2 Place(Vector2 p)
        {
            if (screen) return p; // Draw.Screen's: pixels from the top left already
            // A camera's size is half the height it sees; 0, a pixel a unit. Its center is the screen's, as Tide's
            // first camera's is (0, 0)
            var scale = size > 0 ? height / (2 * size) : 1f;
            var x = (p.x - center.x) * scale + width * 0.5f;
            var y = (p.y - center.y) * scale + height * 0.5f; // y up
            return new Vector2(x, height - y);
        }

        // A pixel, in the units the commands are in.
        float Pixel()
        {
            return screen || size <= 0 ? 1f : 2 * size / height;
        }

        static void Vertex(Vector2 p, Color c)
        {
            GL.Color(c);
            GL.Vertex3(p.x, p.y, 0);
        }

        void Triangle(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc)
        {
            Vertex(Place(a), ca);
            Vertex(Place(b), cb);
            Vertex(Place(c), cc);
        }

        // A box from `low` to `high`, `top` its color at its top (the larger y, or the smaller after Draw.Screen).
        void Quad(Vector2 low, Vector2 high, Color top, Color bottom)
        {
            var upper = screen ? low.y : high.y;
            var lower = screen ? high.y : low.y;
            var a = new Vector2(low.x, upper);
            var b = new Vector2(high.x, upper);
            var c = new Vector2(high.x, lower);
            var d = new Vector2(low.x, lower);
            Triangle(a, b, c, top, top, bottom);
            Triangle(a, c, d, top, bottom, bottom);
        }

        void WireBox(Vector2 at, Vector2 sizes, Color color)
        {
            var half = sizes * 0.5f;
            var w = Pixel();
            Line(at + new Vector2(-half.x, -half.y), at + new Vector2(half.x, -half.y), w, color);
            Line(at + new Vector2(half.x, -half.y), at + new Vector2(half.x, half.y), w, color);
            Line(at + new Vector2(half.x, half.y), at + new Vector2(-half.x, half.y), w, color);
            Line(at + new Vector2(-half.x, half.y), at + new Vector2(-half.x, -half.y), w, color);
        }

        void Line(Vector2 from, Vector2 to, float thickness, Color color)
        {
            var along = to - from;
            if (along.sqrMagnitude <= 0) return;
            var side = new Vector2(-along.y, along.x).normalized * (thickness * 0.5f);
            Triangle(from - side, from + side, to + side, color, color, color);
            Triangle(from - side, to + side, to - side, color, color, color);
        }

        void Ellipse(Vector2 at, Vector2 radii, Color top, Color bottom)
        {
            for (var i = 0; i < Segments; i++)
            {
                var p = Around(at, radii, i);
                var q = Around(at, radii, i + 1);
                Triangle(at, p, q, Mix(top, bottom, at, at, radii), Mix(top, bottom, p, at, radii), Mix(top, bottom, q, at, radii));
            }
        }

        void Ring(Vector2 at, float radius, float thickness, Color color)
        {
            var outer = new Vector2(radius, radius);
            var inner = new Vector2(Mathf.Max(0, radius - thickness), Mathf.Max(0, radius - thickness));
            for (var i = 0; i < Segments; i++)
            {
                Triangle(Around(at, inner, i), Around(at, outer, i), Around(at, outer, i + 1), color, color, color);
                Triangle(Around(at, inner, i), Around(at, outer, i + 1), Around(at, inner, i + 1), color, color, color);
            }
        }

        static Vector2 Around(Vector2 at, Vector2 radii, int i)
        {
            var angle = i * (2 * Mathf.PI / Segments);
            return at + new Vector2(Mathf.Cos(angle) * radii.x, Mathf.Sin(angle) * radii.y);
        }

        // A shape's color at `p`: its top's to its bottom's
        static Color Mix(Color top, Color bottom, Vector2 p, Vector2 at, Vector2 radii)
        {
            var t = radii.y > 0 ? Mathf.Clamp01((at.y + radii.y - p.y) / (2 * radii.y)) : 0;
            return Color.Lerp(top, bottom, t);
        }

        // A smooth shape: a box or an ellipse, filled from its top's color to its bottom's, with its outline; an arc
        // as part of a ring. Round corners, soft edges and turns aren't drawn yet.
        void Shape(TideDrawCommand c, TideDrawShape s)
        {
            var form = s.form & 0xFF;
            var half = c.b * 0.5f;
            if (form == 2) // An arc: part of a ring, its outline how thick
            {
                var middle = Mathf.Atan2(s.turn.y, s.turn.x);
                var thickness = Mathf.Max(s.outline, Pixel());
                for (var i = 0; i < Segments; i++)
                {
                    var a0 = middle - s.sweep + 2 * s.sweep * i / Segments;
                    var a1 = middle - s.sweep + 2 * s.sweep * (i + 1) / Segments;
                    var outer = 1f;
                    var inner = Mathf.Max(0, 1 - thickness / Mathf.Max(half.y, 1e-6f));
                    Triangle(Arc(c.a, half, a0, inner), Arc(c.a, half, a0, outer), Arc(c.a, half, a1, outer), c.color, c.color, c.color);
                    Triangle(Arc(c.a, half, a0, inner), Arc(c.a, half, a1, outer), Arc(c.a, half, a1, inner), c.color, c.color, c.color);
                }
                return;
            }
            if (form == 1) Ellipse(c.a, half, c.color, s.color2);
            else Quad(c.a - half, c.a + half, c.color, s.color2);
            if (s.outline > 0 && s.outlineColor.a > 0)
            {
                if (form == 1) RingEllipse(c.a, half, s.outline, s.outlineColor);
                else
                {
                    var w = s.outline;
                    Quad(new Vector2(c.a.x - half.x, c.a.y + half.y - w), c.a + half, s.outlineColor, s.outlineColor);
                    Quad(c.a - half, new Vector2(c.a.x + half.x, c.a.y - half.y + w), s.outlineColor, s.outlineColor);
                    Quad(c.a - half, new Vector2(c.a.x - half.x + w, c.a.y + half.y), s.outlineColor, s.outlineColor);
                    Quad(new Vector2(c.a.x + half.x - w, c.a.y - half.y), c.a + half, s.outlineColor, s.outlineColor);
                }
            }
        }

        static Vector2 Arc(Vector2 at, Vector2 half, float angle, float r)
        {
            return at + new Vector2(Mathf.Cos(angle) * half.x * r, Mathf.Sin(angle) * half.y * r);
        }

        void RingEllipse(Vector2 at, Vector2 half, float thickness, Color color)
        {
            var inner = new Vector2(Mathf.Max(0, half.x - thickness), Mathf.Max(0, half.y - thickness));
            for (var i = 0; i < Segments; i++)
            {
                Triangle(Around(at, inner, i), Around(at, half, i), Around(at, half, i + 1), color, color, color);
                Triangle(Around(at, inner, i), Around(at, half, i + 1), Around(at, inner, i + 1), color, color, color);
            }
        }

        // A 2D mesh's triangles, each corner's color times the command's (its tint). Its texture isn't drawn yet.
        void Mesh(TideDrawCommand c, TideDrawVertex* vertices, uint vertexCount, uint* indices, uint indexCount)
        {
            var first = c.first;
            var count = c.count;
            if (first > indexCount || count > indexCount - first) return;
            for (var i = 0u; i + 2 < count; i += 3)
            {
                uint x = indices[first + i], y = indices[first + i + 1], z = indices[first + i + 2];
                if (x >= vertexCount || y >= vertexCount || z >= vertexCount) continue;
                Triangle(vertices[x].position, vertices[y].position, vertices[z].position,
                         vertices[x].color * c.color, vertices[y].color * c.color, vertices[z].color * c.color);
            }
        }

        void Words(TideDrawCommand c, byte* bytes, int most)
        {
            var n = 0;
            while (n < most && bytes[n] != 0) n++;
            var words = System.Text.Encoding.UTF8.GetString(bytes, n);
            var at = Place(c.a);
            var pixels = c.b.x / Pixel();
            textStyle ??= new GUIStyle(GUI.skin.label) { wordWrap = false, richText = false, padding = new RectOffset() };
            textStyle.fontSize = Mathf.Max(1, Mathf.RoundToInt(pixels));
            textStyle.normal.textColor = c.color;
            var content = new GUIContent(words);
            var box = textStyle.CalcSize(content);
            // The anchor, as Unity's TextAnchor: which of the box's places is at `a`
            var column = c.anchor % 3;
            var row = c.anchor / 3;
            var x = at.x - box.x * (column * 0.5f);
            var y = at.y - box.y * (row * 0.5f);
            GUI.Label(new Rect(x, y, box.x, box.y), content, textStyle);
        }

        void OnDestroy()
        {
            if (material != null) Destroy(material);
        }
    }
}
