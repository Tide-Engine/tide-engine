using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // How the board looks: its sizes (tidec's layout's, compiler/src/graph_layout.c, so what it places doesn't
    // overlap), what's drawn in code (wires, pins, icons) in each of the editor's two themes, and the small pieces
    // every card, frame and popup is made of. The rest is GraphBoard.uss.
    static class GraphLook
    {
        public const string StylePath = "Packages/com.tide.unity/Editor/Graph/GraphBoard.uss";

        public const float CardWidth = 300, CardRow = 26, CardHead = 48, CardGap = 40;
        public const float FrameHead = 36, FramePad = 12, FrameBottom = 24, FrameGap = 80, LaneGap = 80, EntryGap = 60, EntryWidth = 320;
        // Where an item draws all of itself, only its name, or nothing
        public const float FarZoom = 0.38f;

        static StyleSheet sheet;
        static Font mono;

        public static StyleSheet Sheet => sheet ? sheet : sheet = AssetDatabase.LoadAssetAtPath<StyleSheet>(StylePath);

        public static Font Mono => mono ? mono : mono = Font.CreateDynamicFontFromOSFont(new[] { "Consolas", "Menlo", "DejaVu Sans Mono", "Courier New" }, 12);

        // What's painted, not styled: a theme's colours
        public sealed class Palette
        {
            public Color Grid, GridStrong, Exec, Drag, Outline, FarNode, PinRim;
        }

        static readonly Palette dark = new Palette
        {
            Grid = new Color(1, 1, 1, 0.03f), GridStrong = new Color(1, 1, 1, 0.06f), Exec = new Color(0.93f, 0.93f, 0.93f), Drag = new Color(1f, 0.78f, 0.25f),
            Outline = new Color(1, 1, 1, 0.25f), FarNode = new Color(1, 1, 1, 0.16f), PinRim = new Color(0.1f, 0.1f, 0.1f),
        };

        static readonly Palette light = new Palette
        {
            Grid = new Color(0, 0, 0, 0.03f), GridStrong = new Color(0, 0, 0, 0.06f), Exec = new Color(0.16f, 0.16f, 0.16f), Drag = new Color(0.78f, 0.42f, 0f),
            Outline = new Color(0, 0, 0, 0.35f), FarNode = new Color(0, 0, 0, 0.2f), PinRim = new Color(0.95f, 0.95f, 0.95f),
        };

        public static Palette Of(bool lightTheme) => lightTheme ? light : dark;

        // ---------------------------------------------------------------------------------------------------------
        // Pieces

        public static T Class<T>(this T e, params string[] names) where T : VisualElement
        {
            foreach (var n in names) if (!string.IsNullOrEmpty(n)) e.AddToClassList(n);
            return e;
        }

        public static VisualElement Row(params string[] classes)
        {
            var row = new VisualElement();
            foreach (var c in classes) row.AddToClassList(c);
            return row;
        }

        public static Label Text(string text, params string[] classes)
        {
            var l = new Label(text ?? "");
            foreach (var c in classes) l.AddToClassList(c);
            return l;
        }

        // A button that looks as its class says, not as Unity's buttons do
        public static Button Plain(Action click, params string[] classes)
        {
            var b = new Button(click);
            b.RemoveFromClassList(Button.ussClassName);
            foreach (var c in classes) b.AddToClassList(c);
            return b;
        }

        public static Button IconButton(string shape, string tooltip, Action click, params string[] classes)
        {
            var b = Plain(click, "tg-icon-button");
            foreach (var c in classes) b.AddToClassList(c);
            b.tooltip = tooltip;
            b.Add(new GraphIcon(shape) { pickingMode = PickingMode.Ignore });
            return b;
        }

        // A button of an icon and words
        public static Button Labelled(string shape, string text, Action click, params string[] classes)
        {
            var b = Plain(click, classes);
            if (shape != null) b.Add(new GraphIcon(shape) { pickingMode = PickingMode.Ignore });
            b.Add(new Label(text) { pickingMode = PickingMode.Ignore, style = { paddingLeft = 0, paddingRight = 0, marginLeft = 0, marginRight = 0 } });
            return b;
        }

        // A kind in plain words, in its group's colour
        public static Label Kind(string title, string group)
        {
            var l = new Label(title) { pickingMode = PickingMode.Ignore };
            l.AddToClassList("tg-kind");
            if (!string.IsNullOrEmpty(group)) l.AddToClassList("tg-kind--" + group);
            return l;
        }

        // Text edited where it's drawn: plain until the pointer or the keyboard is on it. `commit` gets what was
        // typed when it's done (Enter, or leaving it) and differs.
        public static TextField Inline(string value, Action<string> commit, string placeholder = null, params string[] classes)
        {
            var f = new TextField { value = value ?? "", isDelayed = true };
            f.AddToClassList("tg-inline");
            foreach (var c in classes) f.AddToClassList(c);
            if (placeholder != null) f.textEdition.placeholder = placeholder;
            f.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                if (e.newValue != e.previousValue) commit(e.newValue);
            });
            return f;
        }

        // A name: what's typed is made a name Tide takes (letters, digits and _), and nothing typed leaves it as it was
        public static TextField Name(string value, Action<string> commit, string placeholder = null, params string[] classes)
        {
            TextField f = null;
            f = Inline(value, typed =>
            {
                var name = Identifier(typed);
                if (name.Length == 0 || name == value)
                {
                    f.SetValueWithoutNotify(value ?? "");
                    return;
                }
                commit(name);
            }, placeholder, classes);
            return f;
        }

        // What's typed as a name of Tide's: "my speed" is mySpeed, "2nd" is _2nd
        public static string Identifier(string text)
        {
            var sb = new StringBuilder();
            var upper = false;
            foreach (var c in (text ?? "").Trim())
            {
                if (char.IsLetterOrDigit(c) && c < 128 || c == '_')
                {
                    sb.Append(upper && sb.Length > 0 ? char.ToUpperInvariant(c) : c);
                    upper = false;
                }
                else upper = true;
            }
            if (sb.Length > 0 && char.IsDigit(sb[0])) sb.Insert(0, '_');
            return sb.ToString();
        }

        // A name with dots in it: a namespace's, an override's
        public static string DottedName(string text)
        {
            var parts = new List<string>();
            foreach (var part in (text ?? "").Split('.'))
            {
                var name = Identifier(part);
                if (name.Length > 0) parts.Add(name);
            }
            return string.Join(".", parts);
        }

        public static string LowerFirst(string s) => string.IsNullOrEmpty(s) ? s : char.ToLowerInvariant(s[0]) + s.Substring(1);
        public static string UpperFirst(string s) => string.IsNullOrEmpty(s) ? s : char.ToUpperInvariant(s[0]) + s.Substring(1);

        // A note as a card's or a frame's title shows it: its start, on a line or two
        public static string Brief(string s, int most = 96)
        {
            if (string.IsNullOrEmpty(s)) return "";
            s = string.Join(" ", s.Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0));
            return s.Length > most ? s.Substring(0, most - 1).TrimEnd() + "…" : s;
        }

        // The first line of a message, and no more than fits beside a node
        public static string FirstLine(string s, int most = 110)
        {
            if (string.IsNullOrEmpty(s)) return "";
            var i = s.IndexOf('\n');
            if (i >= 0) s = s.Substring(0, i);
            return s.Length > most ? s.Substring(0, most - 1) + "…" : s;
        }

        // The element under a pointer event's target that is of a type, or null
        public static T Up<T>(VisualElement e) where T : VisualElement
        {
            for (; e != null; e = e.parent)
                if (e is T t) return t;
            return null;
        }

        // Whether a click is on something that takes it itself: a field, a button, a pin
        public static bool TakesClicks(VisualElement e, VisualElement within)
        {
            for (; e != null && e != within; e = e.parent)
                if (e is TextField || e is Toggle || e is Button || e is BasePopupField<string, string> || e is UnityEditor.UIElements.ColorField || e is PortView) return true;
            return false;
        }
    }

    // A small icon, painted in its text colour: more (three dots), plus, close, down, right, play, search, check,
    // warn, note, code, text
    sealed class GraphIcon : VisualElement
    {
        readonly string shape;

        public GraphIcon(string shape)
        {
            this.shape = shape;
            AddToClassList("tg-icon");
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = contentRect;
            if (r.width <= 0 || r.height <= 0) return;
            var c = r.center;
            var s = Mathf.Min(r.width, r.height) / 14f;
            var color = resolvedStyle.color;
            p.strokeColor = color;
            p.fillColor = color;
            p.lineWidth = 1.6f * s;
            p.lineCap = LineCap.Round;
            p.lineJoin = LineJoin.Round;
            Vector2 P(float x, float y) => c + new Vector2(x, y) * s;
            void Line(params Vector2[] points)
            {
                p.BeginPath();
                p.MoveTo(points[0]);
                for (var i = 1; i < points.Length; i++) p.LineTo(points[i]);
                p.Stroke();
            }
            void Dot(Vector2 at, float radius)
            {
                p.BeginPath();
                p.Arc(at, radius * s, 0, 360);
                p.Fill();
            }
            switch (shape)
            {
                case "more":
                    Dot(P(-4.5f, 0), 1.4f);
                    Dot(P(0, 0), 1.4f);
                    Dot(P(4.5f, 0), 1.4f);
                    break;
                case "plus":
                    Line(P(-4.5f, 0), P(4.5f, 0));
                    Line(P(0, -4.5f), P(0, 4.5f));
                    break;
                case "close":
                    Line(P(-3.5f, -3.5f), P(3.5f, 3.5f));
                    Line(P(-3.5f, 3.5f), P(3.5f, -3.5f));
                    break;
                case "down":
                    Line(P(-3.5f, -1.5f), P(0, 2f), P(3.5f, -1.5f));
                    break;
                case "right":
                    Line(P(-1.5f, -3.5f), P(2f, 0), P(-1.5f, 3.5f));
                    break;
                case "play":
                    p.BeginPath();
                    p.MoveTo(P(-3.5f, -4.5f));
                    p.LineTo(P(4.5f, 0));
                    p.LineTo(P(-3.5f, 4.5f));
                    p.ClosePath();
                    p.Fill();
                    break;
                case "search":
                    p.BeginPath();
                    p.Arc(P(-1, -1), 3.6f * s, 0, 360);
                    p.Stroke();
                    Line(P(1.8f, 1.8f), P(5, 5));
                    break;
                case "check":
                    Line(P(-4, 0.5f), P(-1, 3.5f), P(4.5f, -3.5f));
                    break;
                case "warn":
                    Line(P(0, -5), P(5.5f, 4.5f), P(-5.5f, 4.5f), P(0, -5));
                    Line(P(0, -1.2f), P(0, 1.4f));
                    Dot(P(0, 3.1f), 0.5f);
                    break;
                case "note":
                    Line(P(-4, -4.5f), P(4, -4.5f), P(4, 2), P(1, 4.5f), P(-4, 4.5f), P(-4, -4.5f));
                    Line(P(-1.8f, -1.5f), P(1.8f, -1.5f));
                    Line(P(-1.8f, 1), P(0.5f, 1));
                    break;
                case "code":
                    Line(P(-2, -3.5f), P(-5.5f, 0), P(-2, 3.5f));
                    Line(P(2, -3.5f), P(5.5f, 0), P(2, 3.5f));
                    break;
                case "list":
                    Line(P(-2, -3.5f), P(5, -3.5f));
                    Line(P(-2, 0), P(5, 0));
                    Line(P(-2, 3.5f), P(5, 3.5f));
                    Dot(P(-4.8f, -3.5f), 0.8f);
                    Dot(P(-4.8f, 0), 0.8f);
                    Dot(P(-4.8f, 3.5f), 0.8f);
                    break;
            }
        }
    }

    // The colours of values' types, Blueprint-like: a few kinds of their own, any other a hue of its name's
    static class TideGraphTypes
    {
        public static readonly Color Untyped = new Color(0.62f, 0.72f, 0.88f);

        public static Color ColorOf(string type, bool light = false)
        {
            var c = Base(type);
            // On a light board, the same hues, deeper
            if (light)
            {
                Color.RGBToHSV(c, out var h, out var s, out var v);
                c = Color.HSVToRGB(h, Mathf.Min(1, s * 1.35f + 0.1f), v * 0.68f);
            }
            return c;
        }

        static Color Base(string type)
        {
            if (string.IsNullOrEmpty(type)) return Untyped;
            var t = type.TrimEnd('?');
            var generic = t.IndexOf('<');
            var name = generic > 0 ? t.Substring(0, generic) : t;
            var array = name.IndexOf('[');
            if (array > 0) name = name.Substring(0, array);
            switch (name)
            {
                case "bool": return new Color(0.9f, 0.3f, 0.3f);
                case "byte":
                case "int":
                case "long": return new Color(0.3f, 0.88f, 0.75f);
                case "float":
                case "double": return new Color(0.6f, 0.92f, 0.35f);
                case "string":
                case "char": return new Color(0.95f, 0.45f, 0.85f);
                case "int2":
                case "int3":
                case "int4":
                case "float2":
                case "float3":
                case "float4":
                case "double2":
                case "double3":
                case "double4": return new Color(0.98f, 0.8f, 0.3f);
                case "quaternion": return new Color(0.65f, 0.65f, 1f);
                case "Entity": return new Color(0.35f, 0.6f, 1f);
                case "Color": return new Color(1f, 0.6f, 0.3f);
                case "void": return new Color(0.6f, 0.6f, 0.6f);
            }
            // Any other: its own hue, from its name
            var hash = 17;
            foreach (var c in name) hash = unchecked(hash * 31 + c);
            var hue = (hash & 0xffff) / 65536f;
            return Color.HSVToRGB(hue, 0.45f, 0.95f);
        }
    }

    // A type, as it's shown wherever one goes: its colour's dot and its name, and a click (or Enter) opens the
    // picker. Never typed.
    sealed class GraphTypeChip : Button
    {
        public string Type { get; private set; }

        public GraphTypeChip(string type, string unset, bool light, Action<GraphTypeChip> pick)
        {
            RemoveFromClassList(ussClassName);
            AddToClassList("tg-type");
            Type = type;
            var dot = new VisualElement { pickingMode = PickingMode.Ignore };
            dot.AddToClassList("tg-type-dot");
            dot.style.backgroundColor = TideGraphTypes.ColorOf(type, light);
            Add(dot);
            Add(new Label(string.IsNullOrEmpty(type) ? unset ?? "" : type) { pickingMode = PickingMode.Ignore });
            if (string.IsNullOrEmpty(type)) AddToClassList("tg-type--unset");
            tooltip = string.IsNullOrEmpty(type) ? unset : type;
            clicked += () => pick(this);
        }
    }
}
