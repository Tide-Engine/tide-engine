using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEditor;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The fields the inspector and the nodes edit with: text committed when it's done (Enter or leaving it), a type
    // typed or picked from a list, the inline value of an expression slot.
    static class GraphFields
    {
        public static TextField Text(string label, string value, Action<string> commit, bool multiline = false)
        {
            var f = new TextField(label) { value = value ?? "", isDelayed = true, multiline = multiline };
            if (multiline) f.style.whiteSpace = WhiteSpace.Normal;
            f.RegisterValueChangedCallback(e =>
            {
                if (e.newValue != e.previousValue) commit(e.newValue);
            });
            Compact(f);
            return f;
        }

        public static Toggle Toggle(string label, bool value, Action<bool> commit)
        {
            var t = new Toggle(label) { value = value };
            t.RegisterValueChangedCallback(e => commit(e.newValue));
            Compact(t);
            return t;
        }

        public static PopupField<string> Choice(string label, IList<string> options, string value, Action<string> commit)
        {
            var list = options.ToList();
            if (value != null && !list.Contains(value)) list.Add(value);
            var p = new PopupField<string>(label, list, value ?? list.FirstOrDefault() ?? "");
            p.RegisterValueChangedCallback(e => commit(e.newValue));
            Compact(p);
            return p;
        }

        static void Compact(VisualElement e)
        {
            e.style.marginTop = 1;
            e.style.marginBottom = 1;
            var l = e.Q<Label>(className: "unity-base-field__label");
            if (l != null)
            {
                l.style.minWidth = 70;
                l.style.width = 90;
            }
        }

        // The types a type field lists: Tide's and the file's own
        public static IEnumerable<string> Types(GraphDocument doc)
        {
            var own = doc?.DeclaredTypes().Distinct().OrderBy(t => t).ToList() ?? new List<string>();
            return GraphSchema.BuiltinTypes.Concat(own);
        }

        // A type: typed, or picked from the known types
        public static VisualElement Type(string label, string value, GraphDocument doc, Action<string> commit, string placeholder = null)
        {
            var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
            var f = Text(label, value, commit);
            f.style.flexGrow = 1;
            if (placeholder != null) f.textEdition.placeholder = placeholder;
            row.Add(f);
            var pick = new Button { text = "▾", tooltip = "Pick a type", style = { width = 18, paddingLeft = 0, paddingRight = 0, marginLeft = 0 } };
            pick.clicked += () =>
            {
                var menu = new GenericMenu();
                foreach (var t in Types(doc))
                {
                    var type = t;
                    var group = GraphSchema.BuiltinTypes.Contains(t) ? "" : "This file's/";
                    menu.AddItem(new GUIContent(group + type.Replace("/", "∕")), type == value, () => f.value = type);
                }
                menu.DropDown(pick.worldBound);
            };
            row.Add(pick);
            return row;
        }

        // A comma-separated list of words or types: typeArgs, typeParams, rules
        public static TextField List(string label, IEnumerable<string> values, Action<List<string>> commit)
        {
            return Text(label, string.Join(", ", values ?? Array.Empty<string>()), text => commit(SplitList(text)));
        }

        // Splits "Map<K, V>, int" at the commas between types, not inside angle brackets or parentheses
        public static List<string> SplitList(string text)
        {
            var parts = new List<string>();
            var depth = 0;
            var start = 0;
            for (var i = 0; i <= text.Length; i++)
            {
                if (i == text.Length || (text[i] == ',' && depth == 0))
                {
                    var part = text.Substring(start, i - start).Trim();
                    if (part.Length > 0) parts.Add(part);
                    start = i + 1;
                    continue;
                }
                var c = text[i];
                if (c == '<' || c == '(' || c == '[') depth++;
                else if ((c == '>' || c == ')' || c == ']') && depth > 0) depth--;
            }
            return parts;
        }

        public static IEnumerable<string> Strings(JValue a) => a == null ? Enumerable.Empty<string>() : a.Items.Where(i => i.Kind == JKind.String).Select(i => i.String);

        public static VisualElement Header(string text)
        {
            return new Label(text) { style = { unityFontStyleAndWeight = FontStyle.Bold, marginTop = 6, marginBottom = 2 } };
        }

        public static Button SmallButton(string text, Action click, string tooltip = null)
        {
            return new Button(click) { text = text, tooltip = tooltip, style = { paddingLeft = 3, paddingRight = 3, marginLeft = 1, marginRight = 1, height = 16, fontSize = 10 } };
        }

        // ---------------------------------------------------------------------------------------------------------
        // An expression slot's value: inline when it's one literal or a vector or color of literals, else a button
        // that opens its graph in the canvas

        enum Shape { None, Literal, Vector, Color, Graph }

        static readonly string[] vectorNames = { "float2", "float3", "float4", "int2", "int3", "int4", "uint2", "uint3", "uint4" };
        static readonly string[] literalKinds = { "int", "float", "char", "string", "bool" };

        static Shape ShapeOf(JValue graph, out JValue root, out List<JValue> args)
        {
            root = null;
            args = null;
            if (graph == null) return Shape.None;
            root = GraphDocument.NodeIn(graph, graph.Wire("root"));
            if (root == null) return Shape.Graph;
            var reached = GraphDocument.Reached(graph);
            var k = root.Str("k");
            if (Array.IndexOf(literalKinds, k) >= 0) return reached.Count == 1 ? Shape.Literal : Shape.Graph;
            if (k != "call" || root.Has("typeArgs") || root.Has("argNames") || root.Flag("local")) return Shape.Graph;
            var name = root.Str("name");
            var isVector = Array.IndexOf(vectorNames, name) >= 0;
            var isColor = name == "Color";
            if (!isVector && !isColor) return Shape.Graph;
            var a = root.Arr("args");
            if (a == null || a.Items.Count == 0) return Shape.Graph;
            args = new List<JValue>();
            foreach (var item in a.Items)
            {
                var n = GraphDocument.NodeIn(graph, JValue.AsId(item));
                if (n == null || (n.Str("k") != "int" && n.Str("k") != "float")) return Shape.Graph;
                args.Add(n);
            }
            if (reached.Count != 1 + args.Count) return Shape.Graph;
            if (isColor) return args.Count == 3 || args.Count == 4 ? Shape.Color : Shape.Graph;
            return args.Count == name[name.Length - 1] - '0' ? Shape.Vector : Shape.Graph;
        }

        // The slot `key` of an element (Index for a prefab entity's component)
        public static VisualElement ValueSlot(TideGraphWindow w, string label, int owner, string key, bool optional, int index = -1)
        {
            var doc = w.Doc;
            var slot = new GraphSlot(owner, key, index);
            var graph = doc.GetGraph(slot);
            var row = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center, marginTop = 1, marginBottom = 1 } };
            row.Add(new Label(label) { style = { width = 90, minWidth = 70, paddingLeft = 3, unityTextAlign = TextAnchor.MiddleLeft } });
            var shape = ShapeOf(graph, out var root, out var args);
            var body = new VisualElement { style = { flexDirection = FlexDirection.Row, flexGrow = 1, flexShrink = 1 } };
            row.Add(body);
            const GraphRedraw redraw = GraphRedraw.Canvas;
            switch (shape)
            {
                case Shape.None:
                    body.Add(new Label("none") { style = { color = new Color(0.6f, 0.6f, 0.6f), unityTextAlign = TextAnchor.MiddleLeft, flexGrow = 1 } });
                    var set = new Button { text = "Set ▾" };
                    set.clicked += () => SetMenu(w, slot, set);
                    body.Add(set);
                    return row;
                case Shape.Literal:
                {
                    var k = root.Str("k");
                    if (k == "bool")
                    {
                        var t = new Toggle { value = root.Flag("value") };
                        t.RegisterValueChangedCallback(e => w.Edit("Set value", () => root.Set("value", JValue.Of(e.newValue)), redraw));
                        body.Add(t);
                    }
                    else
                    {
                        var f = new TextField { value = root.Str("text") ?? "", isDelayed = true, style = { flexGrow = 1 }, tooltip = k };
                        f.RegisterValueChangedCallback(e => w.Edit("Set value", () => root.Set("text", JValue.Of(e.newValue), GraphSchema.NodeOrder(k)), redraw));
                        body.Add(f);
                    }
                    break;
                }
                case Shape.Vector:
                    foreach (var a in args)
                    {
                        var n = a;
                        var f = new TextField { value = n.Str("text") ?? "", isDelayed = true, style = { flexGrow = 1, minWidth = 30 } };
                        f.RegisterValueChangedCallback(e => w.Edit("Set value", () => SetNumber(n, e.newValue), redraw));
                        body.Add(f);
                    }
                    break;
                case Shape.Color:
                {
                    var c = new Color(Number(args[0]), Number(args[1]), Number(args[2]), args.Count > 3 ? Number(args[3]) : 1);
                    var cf = new ColorField { value = c, showAlpha = args.Count > 3, style = { flexGrow = 1 } };
                    var list = args;
                    cf.RegisterValueChangedCallback(e => w.Edit("Set color", () =>
                    {
                        var v = e.newValue;
                        var parts = new[] { v.r, v.g, v.b, v.a };
                        for (var i = 0; i < list.Count; i++)
                        {
                            list[i].Set("k", JValue.Of("float"));
                            list[i].Set("text", JValue.Of(Format(parts[i])), GraphSchema.NodeOrder("float"));
                        }
                    }, redraw));
                    body.Add(cf);
                    break;
                }
                case Shape.Graph:
                    body.Add(new Label($"a graph of {graph.Arr("nodes")?.Items.Count ?? 0} nodes") { style = { unityTextAlign = TextAnchor.MiddleLeft, flexGrow = 1, color = new Color(0.7f, 0.7f, 0.7f) } });
                    break;
            }
            var open = new Button(() => w.Show(slot)) { text = "↗", tooltip = "Open this value's graph in the canvas", style = { width = 22 } };
            body.Add(open);
            if (optional)
            {
                var clear = new Button(() => w.Edit("Remove value", () => Clear(doc, slot))) { text = "✕", tooltip = "No value", style = { width = 20 } };
                body.Add(clear);
            }
            return row;
        }

        static float Number(JValue n)
        {
            var text = (n.Str("text") ?? "0").TrimEnd('f', 'F', 'd', 'D');
            return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var f) ? f : 0;
        }

        static string Format(float f)
        {
            var s = Math.Round(f, 3).ToString("0.###", CultureInfo.InvariantCulture);
            return s.Contains(".") ? s : s + ".0";
        }

        // A number typed into a vector: a float's text if it has a point, else an int's
        static void SetNumber(JValue n, string text)
        {
            text = text.Trim();
            var k = text.Contains(".") || text.EndsWith("deg") ? "float" : n.Str("k");
            n.Set("k", JValue.Of(k));
            n.Set("text", JValue.Of(text.Length > 0 ? text : "0"), GraphSchema.NodeOrder(k));
        }

        static void Clear(GraphDocument doc, GraphSlot slot)
        {
            var owner = doc.Get(slot.Owner);
            if (owner == null) return;
            if (slot.Index >= 0)
            {
                var a = owner.Arr(slot.Key);
                if (a != null && slot.Index < a.Items.Count) a.Items.RemoveAt(slot.Index);
            }
            else owner.Remove(slot.Key);
        }

        static void SetMenu(TideGraphWindow w, GraphSlot slot, VisualElement at)
        {
            var menu = new GenericMenu();
            void Literal(string title, string k, string text) => menu.AddItem(new GUIContent(title), false, () => w.Edit("Set value", () => Put(w.Doc, slot, w.Doc.ValueGraph(k, text))));
            Literal("int", "int", "0");
            Literal("float", "float", "0.0");
            Literal("bool", "bool", "false");
            Literal("string", "string", "");
            Literal("char", "char", "'a'");
            foreach (var v in new[] { "float2", "float3", "float4", "int2", "int3" })
            {
                var name = v;
                menu.AddItem(new GUIContent(name), false, () => w.Edit("Set value", () => Put(w.Doc, slot, Constructor(w.Doc, name, name[name.Length - 1] - '0', name.StartsWith("int") ? "0" : "0.0"))));
            }
            menu.AddItem(new GUIContent("Color"), false, () => w.Edit("Set value", () => Put(w.Doc, slot, Constructor(w.Doc, "Color", 4, "1.0"))));
            menu.AddItem(new GUIContent("Graph"), false, () =>
            {
                w.Edit("Set value", () => Put(w.Doc, slot, Graph()));
                w.Show(slot);
            });
            menu.DropDown(at.worldBound);
        }

        static JValue Graph()
        {
            var g = JValue.NewObject();
            g.Set("root", JValue.Null());
            g.Set("nodes", JValue.NewArray());
            return g;
        }

        // A value graph of a call to `name` with `count` number literals
        public static JValue Constructor(GraphDocument doc, string name, int count, string text)
        {
            var g = Graph();
            var call = doc.NewNode("call", -200, 0);
            call.Set("name", JValue.Of(name), GraphSchema.NodeOrder("call"));
            var args = JValue.NewArray();
            var nodes = g.Arr("nodes");
            nodes.Items.Add(call);
            for (var i = 0; i < count; i++)
            {
                var n = doc.NewNode(text.Contains(".") ? "float" : "int", -420, i * 60);
                n.Set("text", JValue.Of(text), GraphSchema.NodeOrder(n.Str("k")));
                args.Items.Add(JValue.Of(n.Wire("id")));
                nodes.Items.Add(n);
            }
            call.Set("args", args, GraphSchema.NodeOrder("call"));
            g.Set("root", JValue.Of(call.Wire("id")));
            return g;
        }

        static void Put(GraphDocument doc, GraphSlot slot, JValue graph)
        {
            var e = doc.Find(slot.Owner);
            if (e == null) return;
            if (slot.Index >= 0)
            {
                var a = e.Value.Arr(slot.Key);
                if (a == null) return;
                if (slot.Index < a.Items.Count) a.Items[slot.Index] = graph;
                else a.Items.Add(graph);
            }
            else e.Value.Set(slot.Key, graph, GraphDocument.OrderOf(e));
        }
    }

    // What an edit redraws: what shows what it changed, except the field it was typed in
    [Flags]
    enum GraphRedraw { None = 0, Outline = 1, Canvas = 2, Inspector = 4, All = 7 }
}
