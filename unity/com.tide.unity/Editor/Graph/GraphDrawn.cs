using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // What the board drew, as every graph editor writes it for the same graph (tools/tide-graph/test/drawn/README.md):
    // each frame's, card's and node's box on the board, its words and its pins, and each wire with its colour. Boxes
    // are in board coordinates at zoom 1; colours are the theme's the board is drawn in. Arrays are sorted by id,
    // wires by where they go to, then where they come from.
    static class GraphDrawn
    {
        public static JValue Dump(TideGraphWindow window, string graphName)
        {
            var board = window.Board;
            var light = window.Light;
            var o = JValue.NewObject();
            o.Set("graph", JValue.Of(graphName));
            var frames = new List<(int id, JValue v)>();
            var cards = new List<(int id, JValue v)>();
            var nodes = new List<(int id, int frame, JValue v)>();
            var wires = new List<(int to, string toPort, int from, string fromPort, JValue v)>();
            foreach (var item in board.Items.OrderBy(i => i.Id))
            {
                var e = item.Element;
                if (e == null) continue;
                var box = Box(new Rect(item.At, item.layout.size));
                if (item is GraphCard)
                {
                    var c = JValue.NewObject();
                    c.Set("id", JValue.Of(item.Id));
                    c.Set("box", box);
                    var head = item.Q(className: "tg-card-head");
                    c.Set("chip", JValue.Of(ChipOf(head)));
                    c.Set("title", JValue.Of(TitleOf(head)));
                    var rows = new List<JValue>();
                    foreach (var row in item.Query(className: "tg-row").ToList())
                    {
                        var r = JValue.NewObject();
                        var name = row.Q<GraphName>();
                        r.Set("label", JValue.Of(name != null ? Words(name) : ""));
                        var type = row.Q<GraphTypeChip>();
                        var shown = type != null ? Words(type) : "";
                        r.Set("shown", shown.Length > 0 ? JValue.Of(shown) : JValue.Null());
                        rows.Add(r);
                    }
                    c.Set("rows", JValue.ArrayOf(rows));
                    c.Set("columns", Columns(item));
                    cards.Add((item.Id, c));
                    continue;
                }
                if (!(item is GraphFrame frame)) continue;
                var f = JValue.NewObject();
                f.Set("id", JValue.Of(item.Id));
                f.Set("box", box);
                var frameHead = item.Q(className: "tg-frame-head");
                f.Set("chip", JValue.Of(ChipOf(frameHead)));
                f.Set("title", JValue.Of(TitleOf(frameHead)));
                frames.Add((item.Id, f));
                if (frame.Level != GraphLevel.Near) continue;
                IEnumerable<GraphNodeView> views = frame.Views.Values;
                if (frame.Entry != null) views = views.Append(frame.Entry);
                foreach (var view in views)
                {
                    var id = view.Id == GraphFrame.EntryId ? frame.Id : view.Id;
                    nodes.Add((id, frame.Id, Node(frame, view, id)));
                }
                Wires(frame, light, wires);
            }
            o.Set("frames", JValue.ArrayOf(frames.OrderBy(x => x.id).Select(x => x.v)));
            o.Set("cards", JValue.ArrayOf(cards.OrderBy(x => x.id).Select(x => x.v)));
            o.Set("nodes", JValue.ArrayOf(nodes.OrderBy(x => x.id).ThenBy(x => x.frame).Select(x => x.v)));
            o.Set("wires", JValue.ArrayOf(wires.OrderBy(w => w.to).ThenBy(w => w.toPort, StringComparer.Ordinal).ThenBy(w => w.from).ThenBy(w => w.fromPort, StringComparer.Ordinal).Select(w => w.v)));
            return o;
        }

        // As the web's test files write it: one frame, card, node or wire a line
        public static string Write(JValue d)
        {
            string List(string key) => $"  \"{key}\": [\n" + string.Join(",\n", d.Arr(key).Items.Select(x => "    " + GraphJson.Write(x))) + "\n  ]";
            return "{\n  \"graph\": " + GraphJson.Write(d.Get("graph")) + ",\n" + string.Join(",\n", new[] { "frames", "cards", "nodes", "wires" }.Select(List)) + "\n}\n";
        }

        static JValue Box(Rect r) => JValue.ArrayOf(new[] { r.x, r.y, r.width, r.height }.Select(n => JValue.Of(Mathf.Round(n))));

        internal static string Hex(Color c) => "#" + ColorUtility.ToHtmlStringRGB(c).ToLowerInvariant();

        // A head's kind, in the words it was given (it's drawn in capitals), and its name
        static string ChipOf(VisualElement head)
        {
            var kind = head?.Q<Label>(className: "tg-kind");
            return kind == null ? "" : kind.userData as string ?? kind.text;
        }

        static string TitleOf(VisualElement head)
        {
            if (head == null) return "";
            var name = head.Q<GraphName>();
            if (name != null) return Words(name);
            return Words(head.Q(className: "tg-frame-title") ?? head.Q(className: "tg-name-fixed"));
        }

        static JValue Node(GraphFrame frame, GraphNodeView view, int id)
        {
            var n = JValue.NewObject();
            n.Set("id", JValue.Of(id));
            n.Set("frame", JValue.Of(frame.Id));
            n.Set("box", Box(new Rect(frame.At + Vector2.one + view.Shown, view.layout.size)));
            var entry = view is GraphEntryView;
            var shape = entry ? "entry" : view.ClassListContains("tg-get") ? "get" : view.ClassListContains("tg-knot") ? "knot" : "node";
            n.Set("shape", JValue.Of(shape));
            var header = view.Q(className: entry ? "tg-entry-head" : "tg-node-header");
            var bg = header?.resolvedStyle.backgroundColor ?? Color.clear;
            n.Set("header", header != null && bg.a > 0 ? JValue.Of(Hex(bg)) : JValue.Null());
            var title = new List<string>();
            if (entry) title.Add(Words(view.Q(className: "tg-entry-sentence")));
            else if (shape == "get") title.Add(Words(view.Q(className: "tg-get-name")));
            else if (shape == "node")
            {
                title.Add(Shown(header, x => x is PortView || x.ClassListContains("tg-port") || x.ClassListContains("tg-more") || x.ClassListContains("tg-node-tag") || x.ClassListContains("tg-op-button")));
                var sub = view.Q(className: "tg-node-sub");
                if (sub != null) title.Add(Shown(sub, x => x is PortView || x.ClassListContains("tg-row-empty")));
            }
            n.Set("title", JValue.Strings(title));
            // Its pins, top to bottom and then left to right as they're seen (pins less than 6 px apart up or down are
            // one row): what each is called (or the operator in its place), its kind, its type's colour and what's
            // shown in it. The entry's are what it takes: its name and its type shown; a filter's has no pin.
            var rows = new List<(float y, float x, JValue row)>();
            (float y, float x) At(VisualElement e)
            {
                var r = e.ChangeCoordinatesTo(frame, e.contentRect);
                return (r.center.y + frame.At.y + 1, r.xMin + frame.At.x + 1);
            }
            foreach (var pin in view.Ports)
            {
                // Its row (a pin in a row's cells is in its first)
                var row = pin.parent;
                if (row != null && row.ClassListContains("tg-cell")) row = row.parent;
                if (row == null || Up(row, "tg-node-sub") != null) continue;
                var first = row.Children().FirstOrDefault(c => c.ClassListContains("tg-c1")) ?? row;
                var lead = first.Children().FirstOrDefault(c => c.ClassListContains("tg-op"));
                var param = entry ? Up(row, "tg-entry-row") : null;
                var label = lead != null ? Words(lead) : param != null ? Words(param.Q(className: "tg-entry-name")) : Words(first.Children().FirstOrDefault(c => c.ClassListContains("tg-port-label")));
                var text = param != null ? Words(param.Q<GraphTypeChip>()) : Shown(row, x => x is PortView || x.ClassListContains("tg-port-label") || x.ClassListContains("tg-op") || x.ClassListContains("tg-row-empty") || x.ClassListContains("tg-row-pick") || x.ClassListContains("tg-port-add"));
                var at = At(pin);
                rows.Add((at.y, at.x, Row(label, Kind(pin.Port), pin.TypeTint, text)));
            }
            if (entry)
                foreach (var r in view.Query(className: "tg-entry-row").ToList())
                {
                    if (r.Q<PortView>() != null) continue;
                    var name = Words(r.Q(className: "tg-entry-name"));
                    var at = At(r);
                    rows.Add((at.y, at.x, Row(name.Length > 0 ? name : Words(r.Q(className: "tg-mode")), "none", null, Words(r.Q<GraphTypeChip>()))));
                }
            // Top to bottom, a row being the pins less than 6 px below its first; each row left to right
            var ordered = new List<JValue>();
            var down = rows.OrderBy(r => r.y).ThenBy(r => r.x).ToList();
            for (var i = 0; i < down.Count;)
            {
                var j = i;
                while (j < down.Count && down[j].y - down[i].y < 6) j++;
                ordered.AddRange(down.Skip(i).Take(j - i).OrderBy(r => r.x).Select(r => r.row));
                i = j;
            }
            n.Set("rows", JValue.ArrayOf(ordered));
            n.Set("columns", Columns(view));
            return n;
        }

        // Its boxes whose rows line up in columns, as they're drawn: where each column with something seen in it
        // starts, from its left; boxes of two or more
        static JValue Columns(VisualElement e) => JValue.ArrayOf(e.Query(className: "tg-cols").ToList()
            .Select(box => GraphColumns.Starts(box, e)).Where(s => s.Count >= 2)
            .Select(s => JValue.ArrayOf(s.Select(x => JValue.Of(Mathf.Round(x))))));

        static string Kind(GraphPort p) => p.Exec ? (p.Input ? "exec-in" : "exec-out") : p.Input ? "in" : "out";

        static JValue Row(string label, string pin, Color? color, string shown)
        {
            var r = JValue.NewObject();
            r.Set("label", JValue.Of(label ?? ""));
            r.Set("pin", JValue.Of(pin));
            r.Set("color", color != null ? JValue.Of(Hex(color.Value)) : JValue.Null());
            r.Set("shown", !string.IsNullOrEmpty(shown) ? JValue.Of(shown) : JValue.Null());
            return r;
        }

        static VisualElement Up(VisualElement e, string className)
        {
            for (; e != null; e = e.parent)
                if (e.ClassListContains(className)) return e;
            return null;
        }

        // An element's words, spaces collapsed: a name's as it's drawn, a label's, a button's
        static string Words(VisualElement e) => e == null ? "" : Shown(e, _ => false);

        // What an element shows, as the page's test reads it: each text, a field's value (or what it says while it's
        // empty), a tick as true or false, a colour as #rrggbb, a chip with a path as one word (Phase.Running); not
        // what `skip` says, nor a quote around a text, nor a tick's word
        static string Shown(VisualElement e, Func<VisualElement, bool> skip)
        {
            var parts = new List<string>();
            void Walk(VisualElement x)
            {
                if (x == null || skip(x) || x is GraphIcon || x.resolvedStyle.display == DisplayStyle.None) return;
                if (x.ClassListContains("tg-path"))
                {
                    parts.Add(Regex.Replace(string.Join("", x.Query<TextElement>().ToList().Select(t => t.text)), @"\s+", ""));
                    return;
                }
                if (x.ClassListContains("tg-inline-quote") || x.ClassListContains("tg-value-bool-word")) return;
                if (x is GraphName name)
                {
                    var label = name.Q<Label>(className: "tg-name-text");
                    parts.Add(label != null ? label.text : name.Value);
                    return;
                }
                if (x is TextField field)
                {
                    parts.Add(field.value.Length > 0 ? field.value : field.textEdition.placeholder ?? "");
                    return;
                }
                if (x is Toggle toggle)
                {
                    parts.Add(toggle.value ? "true" : "false");
                    return;
                }
                if (x is UnityEditor.UIElements.ColorField colour)
                {
                    parts.Add(Hex(colour.value));
                    return;
                }
                if (x is TextElement t)
                {
                    if (!string.IsNullOrEmpty(t.text)) parts.Add(t.text.Trim());
                    if (!(x is Button)) return;
                }
                foreach (var child in x.Children()) Walk(child);
            }
            Walk(e);
            return Regex.Replace(string.Join(" ", parts.Where(p => !string.IsNullOrEmpty(p))), @"\s+", " ").Trim();
        }

        // The wires as the frame draws them, from the file: a data wire from a node's value to an input it isn't
        // shown in (nor a statement's from the call it's drawn as), an arrow from a statement's exec output to the
        // statement it runs, and the entry's to its first
        static void Wires(GraphFrame frame, bool light, List<(int, string, int, string, JValue)> into)
        {
            var g = frame.Graph;
            if (g == null) return;
            var exec = GraphSchema.Look("exec", light);
            foreach (var n in frame.ById.Values)
            {
                var id = n.Wire("id");
                foreach (var p in GraphPorts.Of(n))
                {
                    if (p.Path == null) continue;
                    var from = p.Path.Get(n);
                    if (from == 0) continue;
                    var path = p.Path.ToString();
                    if (p.Exec)
                    {
                        if (!p.Input) into.Add((from, "in", id, path, Wire(id, path, from, "in", exec)));
                        continue;
                    }
                    if (frame.Absorbed.TryGetValue(from, out var statement) && statement == id && path == "value") continue;
                    if (frame.Hidden.Contains(from)) continue;
                    into.Add((id, path, from, "value", Wire(from, "value", id, path, frame.WireColor(from))));
                }
            }
            var first = g.Wire("entry");
            if (first != 0 && !frame.IsValue) into.Add((first, "in", frame.Id, "start", Wire(frame.Id, "start", first, "in", exec)));
        }

        static JValue Wire(int from, string fromPort, int to, string toPort, Color c)
        {
            var w = JValue.NewObject();
            w.Set("from", JValue.ArrayOf(new[] { JValue.Of(from), JValue.Of(fromPort) }));
            w.Set("to", JValue.ArrayOf(new[] { JValue.Of(to), JValue.Of(toPort) }));
            w.Set("color", JValue.Of(Hex(c)));
            return w;
        }
    }
}
