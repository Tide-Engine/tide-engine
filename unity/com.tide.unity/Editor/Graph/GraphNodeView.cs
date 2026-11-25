using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A pin, as the page draws one: a dot for a value, in its value's type's colour (or the look's muted while the
    // type isn't known), ringed in it and hollow while nothing's in an input; an arrow for the flow of statements. A
    // drag from one makes a wire.
    sealed class PortView : VisualElement
    {
        public readonly GraphPort Port;
        public readonly GraphNodeView View;
        public bool Connected;
        public string Type; // The type of the value on it, as a check has it (or as the file declares it)
        bool hover;

        public PortView(GraphNodeView view, GraphPort port)
        {
            View = view;
            Port = port;
            AddToClassList(port.Exec ? "tg-dot-exec" : "tg-dot");
            if (port.Input && !port.Exec) AddToClassList("tg-dot--in");
            Retip();
            generateVisualContent += Draw;
            RegisterCallback<PointerEnterEvent>(_ =>
            {
                hover = true;
                MarkDirtyRepaint();
            });
            RegisterCallback<PointerLeaveEvent>(_ =>
            {
                hover = false;
                MarkDirtyRepaint();
            });
            RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button == 1)
                {
                    // Its own menu: break its links, pick what goes in it, work it out
                    GraphMenu.Open(Frame.Window, GraphMenu.ForPin(this), this, e.position);
                    e.StopPropagation();
                    return;
                }
                if (e.button != 0) return;
                View.Frame.Board.BeginWire(this, e);
                e.StopPropagation();
            });
        }

        // What a pin under the pointer says: what it is, its type, and that it's dragged from
        void Retip()
        {
            if (Port.Exec)
            {
                tooltip = GraphSchema.Word(Port.Input ? "title.arrow" : "pin.next");
                return;
            }
            var label = string.IsNullOrEmpty(Port.Label) ? GraphSchema.Word("value.input") : Port.Label;
            tooltip = GraphSchema.Word("pin.connect", "what", string.IsNullOrEmpty(Type) ? label : label + ": " + Type);
        }

        // While a wire is dragged: whether it can end here (lit), or can't (dimmed); neither when none is
        public void Mark(bool? can)
        {
            EnableInClassList("tg-port--can", can == true);
            EnableInClassList("tg-port--cannot", can == false);
            parent?.EnableInClassList("tg-port-row--can", can == true && Port.Input && !Port.Exec);
            MarkDirtyRepaint();
        }

        public GraphFrame Frame => View.Frame;
        bool Light => View.Frame.Board.Light;

        // Its colour: an arrow's the look's exec, a value's its type's, or the look's muted while that isn't known
        public Color Tint => Port.Exec ? GraphSchema.Look("exec", Light) : GraphSchema.TypeColor(Type) ?? GraphSchema.Look("muted", Light);

        // Its type's colour as the page's pins say it: none while the type isn't known, and none for an arrow
        public Color? TypeTint => Port.Exec ? null : GraphSchema.TypeColor(Type);

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = contentRect;
            if (r.width <= 0 || r.height <= 0) return;
            var c = r.center;
            var lit = hover || ClassListContains("tg-port--can");
            var accent = GraphSchema.Look("accent", Light);
            if (Port.Exec)
            {
                // A triangle as wide as its box and as tall
                p.fillColor = lit ? accent : Tint;
                p.BeginPath();
                p.MoveTo(new Vector2(r.xMin, r.yMin));
                p.LineTo(new Vector2(r.xMax, c.y));
                p.LineTo(new Vector2(r.xMin, r.yMax));
                p.ClosePath();
                p.Fill();
                return;
            }
            var node = GraphSchema.Look("node", Light);
            var outer = Mathf.Min(r.width, r.height) / 2;
            var empty = Port.Input && !Connected;
            var tint = Tint;
            if (!empty && !Connected) tint.a *= 0.75f;
            if (lit)
            {
                p.strokeColor = accent;
                p.lineWidth = 3;
                p.BeginPath();
                p.Arc(c, outer + 1.5f, 0, 360);
                p.Stroke();
            }
            // Its ring, its border in the node's colour, and its middle
            p.strokeColor = empty ? GraphSchema.Look("muted", Light) : tint;
            p.lineWidth = empty ? 1.5f : 1;
            p.BeginPath();
            p.Arc(c, outer + p.lineWidth / 2, 0, 360);
            p.Stroke();
            p.fillColor = node;
            p.BeginPath();
            p.Arc(c, outer, 0, 360);
            p.Fill();
            if (empty) return;
            p.fillColor = tint;
            p.BeginPath();
            p.Arc(c, Mathf.Max(1, outer - GraphSchema.Size("pinBorder", 2)), 0, 360);
            p.Fill();
        }

        // Where wires join it, in another element's space
        public Vector2 Anchor(VisualElement space) => this.ChangeCoordinatesTo(space, new Vector2(layout.width / 2, layout.height / 2));

        public void SetType(string type)
        {
            if (Port.Exec || type == Type) return;
            Type = type;
            Retip();
            MarkDirtyRepaint();
        }
    }

    // A node in a frame: its title in its category's colour, its inputs and its own values on it. A statement that
    // is a call is one node (the call with the statement's flow). An input's row is its name, then what's in it:
    // a number's, a tick's or a text's own control, a chip with a name that never changes or a path from one
    // (Math.PI, .Run) in its type's colour, a pin with a wire from a Get or from the node that works it out, or
    // "pick…" when nothing is. A Get is compact (its name and its pin), a reroute a knot; a Set says what it changes
    // in its title (Set score), and a call on a type or a namespace says it beneath its name (Clamp, Math).
    class GraphNodeView : VisualElement
    {
        public readonly GraphFrame Frame;
        public readonly int Id;          // The node's id; GraphFrame.EntryId or ResultId for the frame's entry and value
        public JValue Node;              // Null for the entry and value
        public JValue Call;              // The call drawn with it, when it's a statement that is one
        public readonly List<PortView> Ports = new List<PortView>();
        public string Signature;         // What it was built from: built again when that changes
        // The next change is its own control's, still being dragged (a colour in its picker): it isn't built again
        internal bool KeepOnce;
        protected VisualElement Head, Body;
        Label problem;
        // What its inputs show: each chip with the node it shows (coloured by its type), and with the parameter
        // or local it reads; each row's name; the arguments' names, which a check gives
        internal readonly List<(int leaf, VisualElement chip)> Chips = new List<(int, VisualElement)>();
        internal readonly List<((string name, int local) key, VisualElement chip)> Reads = new List<((string, int), VisualElement)>();
        readonly Dictionary<VisualElement, string> rowLabels = new Dictionary<VisualElement, string>();
        readonly List<(Label label, JValue owner, WirePath path)> argLabels = new List<(Label, JValue, WirePath)>();
        readonly List<(Button add, JValue call, string word)> argGrowers = new List<(Button, JValue, string)>();
        readonly Dictionary<int, VisualElement> localRows = new Dictionary<int, VisualElement>();
        string typeText, hoverText;
        bool hoverAsked, selected;

        public GraphNodeView(GraphFrame frame, int id, JValue node, JValue call)
        {
            Frame = frame;
            Id = id;
            Node = node;
            Call = call;
            AddToClassList("tg-node");
            RegisterCallback<PointerEnterEvent>(_ =>
            {
                AskHover();
                if (lights != null) Frame.Light(lights.Value.name, lights.Value.local, true);
            });
            RegisterCallback<PointerLeaveEvent>(_ =>
            {
                if (lights != null) Frame.Light(lights.Value.name, lights.Value.local, false);
            });
        }

        // The variable a Get reads, which is lit with its other Gets and where it comes from under the pointer
        (string name, int local)? lights;

        protected TideGraphWindow Window => Frame.Window;
        protected GraphDocument Doc => Frame.Doc;
        protected JValue Graph => Frame.Graph;

        public Vector2 At => Node != null ? GraphDocument.PlaceOf(Node) : Vector2.zero;

        // Where it's drawn in its frame, as it's dragged: set by the frame
        public Vector2 Shown;

        public void MoveTo(Vector2 local)
        {
            Shown = local;
            style.left = local.x;
            style.top = local.y;
        }

        public Vector2 SizeNow
        {
            get
            {
                var l = layout;
                return float.IsNaN(l.width) || l.width <= 0 ? Estimate() : l.size;
            }
        }

        // tidec's layout's own estimate of a node's size (graph_layout_measure), until it's drawn
        public virtual Vector2 Estimate() => EstimateOf(Node, Call, Frame);

        // As wide as its widest input's row too: the input's name, then what's shown in it (graph_layout_fold). A Get
        // is its name and its pin (and a member's Target pin), a reroute a knot.
        public static Vector2 EstimateOf(JValue n, JValue call, GraphFrame frame = null)
        {
            if (call == null && n != null && IsGet(n.Str("k"))) return GetSize(n, frame);
            if (call == null && n?.Str("k") == "reroute") return KnotSize;
            var widest = 0;
            var ports = 1;
            var width = 0f;
            foreach (var v in new[] { n, call })
            {
                if (v == null) continue;
                foreach (var value in v.Values)
                    if (value.Kind == JKind.String && value.String.Length > widest) widest = value.String.Length;
                ports += GraphPorts.Of(v).Count(p => p.Path != null);
                if (frame == null) continue;
                foreach (var w in GraphPorts.DataWires(v))
                {
                    var shown = frame.ShownIn(w);
                    if (shown != null) width = Mathf.Max(width, 100 + Mathf.Min(260, frame.WidthOf(shown) * 8 + 24));
                }
            }
            return new Vector2(Mathf.Max(width, 140 + Mathf.Min(260, widest * 8)), 34 + 22 * ports);
        }

        // The kinds drawn as a compact Get: a variable read (a name, a local, this) or a field of a value
        internal static bool IsGet(string k) => k == "name" || k == "local" || k == "member" || k == "this";

        internal static readonly Vector2 KnotSize = new Vector2(34, 18);

        static Vector2 GetSize(JValue n, GraphFrame frame)
        {
            var k = n.Str("k");
            var text = k == "member" ? n.Str("member") : k == "local" ? (frame != null && frame.ById.TryGetValue(n.Wire("ref"), out var d) ? GraphSchema.LocalName(d) : "local") : k == "this" ? GraphFrame.SelfWord : n.Str("name");
            // As tidec's layout measures one (graph_layout_measure): its name, and a field's Target pin with its word
            var width = 48f + Mathf.Min(200, 8 * (text?.Length ?? 4));
            if (k == "member" && !n.Flag("shorthand"))
            {
                width += 60;
                var of = frame?.ShownIn(n.Wire("object"));
                if (of != null) width += Mathf.Min(200, frame.WidthOf(of) * 7.5f + 18);
            }
            return new Vector2(width, 32);
        }

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                EnableInClassList("tg-selected", value);
                TintGet();
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Building

        // What a node view is built from, as text: the node (but where it is), the call drawn with it, and what's
        // typed on its inputs
        public static string SignatureOf(GraphFrame frame, JValue n, JValue call)
        {
            var sb = new StringBuilder();
            void Write(JValue v)
            {
                for (var i = 0; i < v.Keys.Count; i++)
                {
                    if (v.Keys[i] == "x" || v.Keys[i] == "y") continue;
                    sb.Append(v.Keys[i]).Append(':').Append(GraphJson.Write(v.Values[i])).Append(';');
                }
                foreach (var p in GraphPorts.Of(v))
                {
                    if (p.Path == null || p.Exec) continue;
                    // What's shown in its inputs is part of it
                    foreach (var id in frame.ChainOf(p.Path.Get(v)))
                        if (frame.ById.TryGetValue(id, out var source))
                        {
                            sb.Append('<').Append(GraphJson.Write(source)).Append('>');
                            // A local in a Set's title says its declaration's name
                            if (source.Str("k") == "local" && frame.ById.TryGetValue(source.Wire("ref"), out var made)) sb.Append(GraphSchema.LocalName(made));
                        }
                }
                if (frame.Declares(v))
                    sb.Append(frame.Readers.TryGetValue(v.Wire("id"), out var readers) && readers.Count > 0 ? "R" : "");
                // A Get of a local says its name, which is its declaration's
                if (v.Str("k") == "local" && frame.ById.TryGetValue(v.Wire("ref"), out var declared)) sb.Append("=").Append(GraphSchema.LocalName(declared));
            }
            Write(n);
            if (call != null) Write(call);
            sb.Append(frame.Reached.Contains(n.Wire("id")) ? "+" : "-");
            return sb.ToString();
        }

        public virtual void Build()
        {
            Clear();
            Ports.Clear();
            ResetShown();
            problem = null;
            Head = Body = null;
            var n = Node;
            var shown = Call ?? n;
            var k = shown.Str("k");
            var spec = GraphSchema.Spec(k);
            EnableInClassList("tg-node--unreached", !Frame.Reached.Contains(Id));
            var get = Call == null && IsGet(k);
            var knot = Call == null && k == "reroute";
            EnableInClassList("tg-get", get);
            EnableInClassList("tg-knot", knot);
            EnableInClassList("tg-get-member", get && k == "member" && !n.Flag("shorthand"));
            EnableInClassList("tg-chipnode", false);
            EnableInClassList("tg-exec", false);
            style.backgroundColor = StyleKeyword.Null;
            style.borderLeftColor = style.borderRightColor = style.borderTopColor = style.borderBottomColor = StyleKeyword.Null;
            getName = null;
            if (get)
            {
                BuildGet(n, k);
                return;
            }
            if (knot)
            {
                BuildKnot(n);
                return;
            }
            BuildNode(n, shown, k, spec);
        }

        static bool IsLiteral(string k) => k == "int" || k == "float" || k == "string" || k == "bool" || k == "char";

        JValue titled; // The method whose title says what it's of
        VisualElement titleTarget; // What it's of, shown beneath its name
        JValue changes; // What a Set's title says it changes
        string changesKey;
        VisualElement getName; // A Get's name

        protected void ResetShown()
        {
            lights = null;
            titleTarget = null;
            changes = null;
            changesKey = null;
            hiddenRows.Clear();
            Chips.Clear();
            Reads.Clear();
            rowLabels.Clear();
            argLabels.Clear();
            argGrowers.Clear();
            localRows.Clear();
            inputs.Clear();
            titled = null;
        }

        // A chip in its value's type's colour: its fill a little of it over the node, its edge half of it
        internal static void Tint(VisualElement chip, string type, bool light)
        {
            var c = GraphSchema.TypeColor(type) ?? GraphSchema.Look("muted", light);
            var node = GraphSchema.Look("node", light);
            chip.style.backgroundColor = GraphLook.Mix(c, 0.16f, node);
            var edge = GraphLook.Fade(c, 0.5f);
            chip.style.borderLeftColor = edge;
            chip.style.borderRightColor = edge;
            chip.style.borderTopColor = edge;
            chip.style.borderBottomColor = edge;
            var dot = chip.Q(className: "tg-path-dot");
            if (dot != null) dot.style.backgroundColor = c;
            chip.EnableInClassList("tg-path-plain", GraphSchema.TypeColor(type) == null);
            if (!string.IsNullOrEmpty(type)) chip.tooltip = type;
        }

        // The × of a filled input, seen while the pointer is on its row: empties it (Delete or Backspace there does
        // too)
        VisualElement EmptyButton(JValue owner, WirePath path, string label)
        {
            var b = GraphLook.Plain(() => Frame.Empty(owner, path, LabelOf(Window.Checked, owner, path, label)), "tg-row-empty");
            b.text = GraphSchema.Word("close");
            b.tooltip = string.IsNullOrEmpty(label) ? GraphSchema.Word("input.empty.tip.it") : GraphSchema.Word("input.empty.tip", "input", label);
            b.name = "empty";
            return b;
        }

        // The inputs drawn on it, by their rows (and what's in them): Delete or Backspace with the pointer on one
        // empties it
        readonly Dictionary<VisualElement, (JValue owner, WirePath path, string label)> inputs = new Dictionary<VisualElement, (JValue, WirePath, string)>();

        // Empties the input whose row (or value) holds `target`, when one does and something is in it
        internal bool EmptyAt(VisualElement target)
        {
            for (var e = target; e != null && e != this; e = e.parent)
                if (inputs.TryGetValue(e, out var input)) return Frame.Empty(input.owner, input.path, input.label);
            return false;
        }

        // What an input is called: an argument by its callee's parameter (the last check's), or by its place in a
        // vector's or a colour's numbers, a single input by the schema's word for its key, what a method or a field
        // is of its Target
        internal static string LabelOf(GraphCheckResult check, JValue owner, WirePath path, string fallback)
        {
            if (owner == null) return fallback ?? "";
            var k = owner.Str("k");
            if ((k == "call" || k == "method") && path.Key == "args" && path.Parts.Length == 2 && path.Parts[1] is int index)
            {
                var names = owner.Arr("argNames");
                var written = names != null && index < names.Items.Count ? names.Items[index].String : null;
                if (!string.IsNullOrEmpty(written)) return written;
                GraphCall call = null;
                check?.Calls.TryGetValue(owner.Wire("id"), out call);
                var known = call?.At(index).name;
                if (!string.IsNullOrEmpty(known)) return known;
                var count = owner.Arr("args")?.Items.Count ?? 0;
                var control = k == "call" ? GraphSchema.ValueControl(owner.Str("name")) : null;
                if (control == "vector" && count > 1 && count <= 4) return "xyzw"[index].ToString();
                if (control == "colour" && count >= 3 && count <= 4) return new[] { "red", "green", "blue", "alpha" }[index];
                // ...until a check names it, the schema's word for one of the list and which
                var word = GraphSchema.Spec(k)?.Lists.FirstOrDefault(l => l.Key == path.Key)?.Label ?? path.Key;
                return $"{word} {index + 1}";
            }
            if (path.Parts.Length == 1) return path.Key == "object" && (k == "method" || k == "member") ? TargetWord : GraphSchema.PortLabel(path.Key);
            return fallback ?? "";
        }

        // What a method's or a field's value is of, as Blueprint calls it
        internal static string TargetWord => GraphSchema.Word("target");

        // The arguments' names, as the check that just came back has them; and the row that adds one says which
        // comes next, and goes once the call has all its callee takes
        internal void Relabel()
        {
            foreach (var (add, call, word) in argGrowers)
            {
                GraphCall callee = null;
                Window.Checked?.Calls.TryGetValue(call.Wire("id"), out callee);
                var given = call.Arr("args")?.Items.Count ?? 0;
                add.style.display = callee != null && given >= callee.Params.Length ? DisplayStyle.None : DisplayStyle.Flex;
            }
            foreach (var (label, owner, path) in argLabels)
            {
                var text = LabelOf(Window.Checked, owner, path, label.text);
                if (label.text == text) continue;
                label.text = text;
                var row = label.parent;
                if (row != null && rowLabels.ContainsKey(row)) rowLabels[row] = text;
            }
        }

        internal VisualElement LocalRowOf(int declares) => localRows.TryGetValue(declares, out var row) ? row : null;

        protected PortView Pin(GraphPort p)
        {
            var v = new PortView(this, p);
            Ports.Add(v);
            return v;
        }

        // ---------------------------------------------------------------------------------------------------------
        // A node, as the page draws one (tools/tide-graph/src/frames.js, renderNode): its title bar (its arrow in,
        // what it does in its own words, what a Set changes, its menu, its first output), what a call on a type is
        // of beneath it, its note, its own values, and its inputs on the left and its outputs on the right

        // A row's items `gap` apart, as a flex gap: a space between each two
        protected static void Spaced(VisualElement row, float gap, IEnumerable<VisualElement> items)
        {
            var first = true;
            var down = row.ClassListContains("tg-col");
            foreach (var item in items)
            {
                if (item == null) continue;
                if (!first) row.Add(Gap(gap, down));
                row.Add(item);
                first = false;
            }
        }

        protected static VisualElement Gap(float size, bool down = false)
        {
            var g = new VisualElement { pickingMode = PickingMode.Ignore };
            g.AddToClassList("tg-gap");
            if (down) g.style.height = size;
            else g.style.width = size;
            return g;
        }

        protected static VisualElement Grow()
        {
            var g = new VisualElement { pickingMode = PickingMode.Ignore };
            g.AddToClassList("tg-grow");
            return g;
        }

        // Its ports as the page has them: a statement drawn with its call takes the call's inputs (its value's label
        // is none), a call holds code only when it's made to, Set by one takes no value, and a Return in code that
        // gives nothing back takes none; the local it declares is an output of its own
        List<GraphPort> PortsOf(JValue n, JValue call)
        {
            var ports = GraphPorts.Of(n);
            var k = n.Str("k");
            bool Block(GraphPort p) => p.Exec && !p.Input && p.Path != null && p.Path.Parts.Length == 1 && p.Path.Key == "block";
            if (call != null)
            {
                ports = ports.Where(p => !(p.Input && !p.Exec)).Concat(GraphPorts.Of(call).Where(p => p.Input && !p.Exec)).ToList();
                if (!n.Flag("withBlock") && n.Wire("block") == 0) ports = ports.Where(p => !Block(p)).ToList();
                foreach (var p in ports)
                    if (!p.Input && !p.Exec) p.Label = "";
            }
            else if (k == "eval" && !n.Flag("withBlock") && n.Wire("block") == 0) ports = ports.Where(p => !Block(p)).ToList();
            else if (k == "assign" && GraphSchema.StepOps.Contains(n.Str("op")) && n.Wire("value") == 0)
                ports = ports.Where(p => !(p.Input && !p.Exec && p.Path?.Key == "value")).ToList();
            else if (k == "return" && n.Wire("value") == 0 && !Frame.IsValue && !Frame.GivesBack)
                ports = ports.Where(p => !(p.Input && !p.Exec && p.Path?.Key == "value")).ToList();
            foreach (var p in ports)
            {
                if (p.Exec && !p.Input) p.Label = p.Path != null && p.Path.Parts.Length == 1 && p.Path.Key == "next" ? "" : p.Group != null ? p.Label : GraphSchema.PortLabel(p.Path?.Key);
                else if (p.Input && !p.Exec) p.Label = LabelOf(Window.Checked, call != null && p.Node == call.Wire("id") ? call : n, p.Path, p.Label);
            }
            if (GraphSchema.DeclaresLocal(n))
                ports.Add(new GraphPort { Node = n.Wire("id"), Input = false, Exec = false, Local = true, Label = GraphSchema.LocalName(n) });
            return ports;
        }

        // A pin on the title bar: an arrow in or out, or a value's
        VisualElement HeadPin(GraphPort p, string tip = null)
        {
            var row = GraphLook.Row("tg-port", p.Input ? "tg-in" : "tg-out", "tg-head-port");
            var pin = Pin(p);
            row.Add(pin);
            if (tip != null) pin.tooltip = tip;
            if (!p.Exec) row.name = (p.Input ? "in:" : "out:") + p.Path;
            return row;
        }

        // An output's row: its words (a local's name, then, otherwise) and its pin
        VisualElement OutRow(GraphPort p)
        {
            var row = GraphLook.Row("tg-port", "tg-out");
            if (p.Local) row.AddToClassList("tg-pin");
            var items = new List<VisualElement>();
            if (!string.IsNullOrEmpty(p.Label)) items.Add(GraphLook.Text(p.Label, "tg-port-label"));
            var pin = Pin(p);
            items.Add(pin);
            Spaced(row, 5, items);
            if (p.Local)
            {
                var declares = p.Node;
                pin.Connected = Frame.LocalIsRead(declares);
                pin.tooltip = GraphSchema.Word("pin.param", "name", p.Label);
                localRows[declares] = row;
                row.RegisterCallback<PointerEnterEvent>(_ => Frame.Light(null, declares, true));
                row.RegisterCallback<PointerLeaveEvent>(_ => Frame.Light(null, declares, false));
            }
            return row;
        }

        void BuildNode(JValue n, JValue shown, string k, GraphSchema.NodeSpec spec)
        {
            var own = GraphSchema.Spec(n.Str("k"));
            var ports = PortsOf(n, Call);
            var ins = ports.Where(p => p.Input && !p.Exec).ToList();
            var outs = ports.Where(p => !p.Input && !p.Exec).ToList();
            var execIn = ports.FirstOrDefault(p => p.Input && p.Exec);
            var execOuts = ports.Where(p => !p.Input && p.Exec).ToList();
            var category = spec?.Category;
            EnableInClassList("tg-exec", own?.Exec ?? false);
            if (spec == null)
            {
                var unknown = GraphLook.Row("tg-node-header", "tg-node-head");
                unknown.Add(GraphLook.Text(k ?? "?", "tg-node-title"));
                Add(unknown);
                Add(GraphLook.Text(GraphSchema.Word("node.unknown", "kind", k), "tg-node-unknown"));
                foreach (var p in ports) Add(p.Input && !p.Exec ? InputRow(n, p, null, null) : OutRow(p));
                return;
            }

            // Its own values, by what each key holds
            var order = GraphSchema.NodeOrder(k);
            var fields = new List<VisualElement>();
            var tags = new List<VisualElement>();
            VisualElement primary = null, op = null;
            var primaryKey = new[] { "name", "member" }.FirstOrDefault(key => spec.Keys.Contains(key) && KeyType(key, shown) == "string");
            var titledOp = false;
            foreach (var key in spec.Keys)
            {
                var type = KeyType(key, shown);
                if (key == "ref" && k == "local") continue;
                if (key == "op")
                {
                    var now = shown.Str("op");
                    if (k == "assign")
                    {
                        // How Set changes what it sets: its sign on a button, and a menu of what each does
                        Button b = null;
                        b = GraphLook.Plain(() => Frame.PickOperator(this, shown, b), "tg-op", "tg-op-button");
                        b.text = now ?? "=";
                        b.name = "op";
                        var said = GraphSchema.AssignWord(now);
                        b.tooltip = said != null ? GraphSchema.Word("op.assign.tip", "words", said) : GraphSchema.Word("op.assign");
                        op = b;
                        continue;
                    }
                    if (k == "binary" || k == "unary")
                    {
                        // An operator is its node's title, by what it's called: picking another there changes the node
                        Button b = null;
                        b = GraphLook.Plain(() => Frame.PickOperator(this, shown, b), "tg-op", "tg-op-title");
                        b.name = "op";
                        b.tooltip = GraphSchema.Word("op.title.tip");
                        Spaced(b, 6, new VisualElement[] { GraphLook.Text(OpTitle(now, k == "unary")), GraphLook.Text(now ?? "", "tg-op-sign") });
                        op = b;
                        titledOp = true;
                        continue;
                    }
                    Button pick = null;
                    pick = GraphLook.Plain(() => Frame.PickOperator(this, shown, pick), "tg-op", "tg-op-button");
                    pick.text = now ?? "";
                    pick.name = "op";
                    op = pick;
                    continue;
                }
                if (type == "flag")
                {
                    if (k == "bool" && key == "value")
                    {
                        var t = new Toggle { value = shown.Flag("value") };
                        t.name = "ticked";
                        t.RegisterValueChangedCallback(e =>
                        {
                            e.StopPropagation();
                            Frame.Edit("Set value", () => shown.Set("value", JValue.Of(e.newValue), order));
                        });
                        var box = GraphLook.Row("tg-value-bool");
                        Spaced(box, 5, new VisualElement[] { t, GraphLook.Text(GraphSchema.Word(shown.Flag("value") ? "value.yes" : "value.no")) });
                        fields.Add(box);
                    }
                    else if (shown.Flag(key))
                    {
                        var info = GraphSchema.IsFlag(key) ? GraphSchema.FlagInfo(key) : GraphSchema.KeyInfo(key);
                        var tag = GraphLook.Text((info.Label ?? key).ToLowerInvariant(), "tg-node-tag");
                        tag.tooltip = info.Help ?? "";
                        tags.Add(tag);
                    }
                    continue;
                }
                if (key == "typeArgs")
                {
                    foreach (var t in GraphLists.Strings(shown.Arr("typeArgs"))) tags.Add(GraphLook.Text(t, "tg-node-tag"));
                    continue;
                }
                if (key == "parts")
                {
                    var f = GraphLook.Inline(Template(shown), typed => Frame.Edit("Text", () => SetTemplate(shown, typed)), GraphSchema.Word("value.text"), "tg-node-input", "tg-node-template");
                    f.tooltip = GraphSchema.Word("node.template.tip");
                    f.name = "value";
                    var font = GraphSchema.Size("font", 13);
                    Fit(f, Template(shown), 10, font, "Score: {}");
                    f.style.minWidth = 12 * GraphLook.Ch(font);
                    f.style.maxWidth = 240;
                    fields.Add(f);
                    continue;
                }
                if (key == "params" && k == "lambda")
                {
                    fields.Add(LambdaParams(shown, order));
                    continue;
                }
                if (type == "type")
                {
                    var optional = spec.Declares != null || k == "lambda";
                    var at = key;
                    var chip = new GraphTypeChip(shown.Str(key), GraphSchema.Word(optional ? "node.anyType" : "node.pickType"), Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                    {
                        Title = GraphSchema.KeyInfo(at).Label, Current = shown.Str(at), Infer = optional ? GraphSchema.Word("node.inferType") : null, Generic = Doc.TypeParamsAt(Frame.Slot.Owner),
                    }, from, picked => Frame.Edit("Set type", () =>
                    {
                        if (string.IsNullOrEmpty(picked)) shown.Remove(at);
                        else shown.Set(at, JValue.Of(picked), order);
                    })));
                    chip.name = "type";
                    var field = GraphLook.Row("tg-field");
                    var items = new List<VisualElement>();
                    if (key != "type") items.Add(GraphLook.Text(GraphSchema.KeyInfo(key).Label, "tg-field-label"));
                    items.Add(chip);
                    Spaced(field, 5, items);
                    fields.Add(field);
                    continue;
                }
                if (type == "string" && key != "formats" && key != "argNames")
                {
                    // A name reads as text and is renamed by a double click
                    var at = key;
                    var dotted = key == "name" && (k == "call" || k == "name");
                    void Commit(string name) => Frame.Edit("Rename", () =>
                    {
                        if (name.Length == 0 && at != "name" && at != "member") shown.Remove(at);
                        else shown.Set(at, JValue.Of(name), order);
                    });
                    var f = dotted ? GraphLook.DottedNameField(shown.Str(key), Commit, key, "tg-node-input") : GraphLook.Name(shown.Str(key), Commit, key == "binding" ? GraphSchema.Word("node.binding") : key, "tg-node-input");
                    f.AllowEmpty = key != "name" && key != "member";
                    if (key == primaryKey)
                    {
                        // The local it declares is found by its name's field, else it's the node's title
                        f.Named(spec.Declares == key ? "local" : "title");
                        f.AddToClassList("tg-node-name");
                        primary = f;
                    }
                    else
                    {
                        f.Named(key);
                        fields.Add(f);
                    }
                    continue;
                }
                if (type == "text") fields.Add(TextField(shown, k, order));
            }

            // A value with nothing in it to wire, and one value of its own or none: a pill, with its pin beside it
            var words = k == "binary" || k == "unary" ? OpTitle(shown.Str("op"), k == "unary") : spec.Title;
            var pill = !spec.Exec && Call == null && ins.Count == 0 && execOuts.Count == 0 && fields.Count + (primary != null ? 1 : 0) <= 1 && op == null && spec.Lists.Length == 0 && spec.Items.Length == 0 && spec.Map == null && outs.Count == 1;
            var headerColor = GraphSchema.Parse(GraphSchema.HeaderHex(category, Frame.Board.Light));
            var bar = GraphSchema.ColorOf(category);
            var more = GraphLook.Plain(null, "tg-more", "tg-node-more");
            more.text = GraphSchema.Word("more");
            more.tooltip = GraphSchema.Word("more.tip");
            more.name = "more";
            more.clicked += () => Frame.OpenNodeMenu(this, more);
            if (pill)
            {
                EnableInClassList("tg-chipnode", true);
                style.borderLeftColor = bar;
                var o = outs[0];
                var lone = k == "local" ? GraphLook.Text(Frame.TextOf(shown), "tg-node-name") : null;
                var row = GraphLook.Row("tg-port", "tg-out", "tg-chip-row");
                Head = row;
                var items = new List<VisualElement>();
                if (fields.Count == 0 && primary == null && lone == null) items.Add(GraphLook.Text(words, "tg-node-title"));
                if (k == "member" && shown.Flag("shorthand")) items.Add(GraphLook.Text(".", "tg-node-dot"));
                items.Add(primary);
                items.Add(lone);
                items.AddRange(fields);
                items.AddRange(tags);
                items.Add(Pin(o));
                Spaced(row, 4, items);
                Add(row);
                Retooltip();
                return;
            }

            Head = GraphLook.Row("tg-node-header", "tg-node-head");
            if (headerColor != null) Head.style.backgroundColor = headerColor.Value;
            Head.style.borderLeftColor = bar;
            var head = new List<VisualElement>();
            if (execIn != null) head.Add(HeadPin(execIn));
            // What a method is called on is its Target: a row wired from a node (a Get), or, when it's a type or a
            // namespace (a name shown there), beneath the title as Blueprint's "Target is Math"
            var objectPort = k == "method" ? ins.FirstOrDefault(p => p.Path.Parts.Length == 1 && p.Path.Key == "object") : null;
            var objectWire = objectPort != null ? Frame.WireAt(shown, objectPort.Path) : 0;
            var objectShown = objectWire != 0 && Frame.ShownIn(objectWire) != null;
            // What a Set or a step changes, when it's read by it alone, is in its title: "Set score"; a field's
            // Target, what it's a field of, is the first row
            GraphPort targetPort = null, ofTarget = null;
            if (Call == null && Frame.Titles.TryGetValue(Id, out var target) && Frame.ById.TryGetValue(target, out var changed))
            {
                changes = changed;
                changesKey = spec.Inputs.FirstOrDefault();
                targetPort = ins.FirstOrDefault(p => p.Path.Parts.Length == 1 && p.Path.Key == changesKey);
                if (changed.Str("k") == "member" && !changed.Flag("shorthand"))
                    ofTarget = GraphPorts.Of(changed).First(q => q.Input && !q.Exec && q.Path?.Key == "object");
                if (ofTarget != null) ofTarget.Label = TargetWord;
            }
            var rowInputs = new List<(JValue owner, GraphPort port)>();
            if (ofTarget != null) rowInputs.Add((changes, ofTarget));
            foreach (var p in ins)
                if (p != targetPort && !(objectShown && p == objectPort)) rowInputs.Add((OwnerOf(p, n), p));
            // A value made of one other with nothing else to set: its input on the title's row, one line in all
            GraphPort headIn = null;
            if (objectPort == null && changes == null && execIn == null && op == null && rowInputs.Count == 1 && rowInputs[0].port.Group == null && fields.Count == 0
                && spec.Lists.Length == 0 && spec.Items.Length == 0 && spec.Map == null)
            {
                var w = Frame.WireAt(rowInputs[0].owner, rowInputs[0].port.Path);
                if (w != 0 && Frame.ShownIn(w) == null) headIn = rowInputs[0].port;
            }
            if (headIn != null)
            {
                var row = HeadPin(headIn, GraphSchema.Word("pin.headIn", "input", headIn.Label));
                rowLabels[row] = headIn.Label;
                inputs[row] = (rowInputs[0].owner, headIn.Path, headIn.Label);
                head.Add(row);
            }
            // How Set changes what it sets (=, +=) reads between the two: its choice is the name of its value's row.
            // An operator's own choice (Multiply, Less than) is its node's title.
            var alone = op != null && k == "assign" && !ins.Any(p => p.Path.Parts.Length == 1 && p.Path.Key == "value");
            var opRow = op != null && !titledOp ? rowInputs.Where(r => r.port.Group == null && r.port != headIn).Select(r => r.port).LastOrDefault() : null;
            if (primary != null && k != "method" && k != "call") head.Add(GraphLook.Text(words, "tg-node-title"));
            head.Add(primary ?? (titledOp ? op : GraphLook.Text(words, "tg-node-title")));
            if (changes != null) head.Add(ChangesButton(shown));
            if (opRow == null && !titledOp) head.Add(op);
            head.AddRange(tags);
            head.Add(Grow());
            head.Add(more);
            // The first output on the title's row keeps the node short: next for a statement, the value else
            var headOut = own?.Exec == true ? execOuts.FirstOrDefault(p => p.Path != null && p.Path.Parts.Length == 1 && p.Path.Key == "next") : outs.FirstOrDefault(p => !p.Local);
            if (headOut != null) head.Add(HeadPin(headOut, GraphSchema.Word(headOut.Exec ? "pin.next" : "pin.value")));
            Spaced(Head, GraphSchema.Size("gap", 4), head);
            Add(Head);

            if (objectShown)
            {
                // "Clamp", and "Math" beneath it: each part clicked to change it from there
                titled = shown;
                var sub = GraphLook.Row("tg-port", "tg-in", "tg-inline", "tg-node-sub");
                sub.name = "of:object";
                var pin = Pin(objectPort);
                pin.AddToClassList("tg-sub-dot");
                pin.Connected = true;
                var chip = PathChip(shown, objectPort.Path, TargetWord, objectWire);
                titleTarget = chip;
                foreach (var id in Frame.ChainOf(objectWire)) hiddenRows.Add((id, sub));
                Spaced(sub, 5, new[] { pin, chip, EmptyButton(shown, objectPort.Path, TargetWord) });
                inputs[sub] = (shown, objectPort.Path, TargetWord);
                rowLabels[sub] = TargetWord;
                Add(sub);
            }
            var note = n.Str("comment") ?? shown.Str("comment");
            if (!string.IsNullOrEmpty(note))
            {
                var said = GraphLook.Text(note, "tg-node-comment");
                said.tooltip = note;
                Add(said);
            }
            if (fields.Count > 0)
            {
                Body = GraphLook.Row("tg-node-fields");
                GraphColumns.Box(Body);
                foreach (var f in fields)
                {
                    var line = GraphLook.Row("tg-field-row");
                    if (f.ClassListContains("tg-field") && f.Q(className: "tg-field-label") != null)
                    {
                        // name | control
                        GraphColumns.Sort(f, (c, i) => c.ClassListContains("tg-field-label") ? 1 : 2);
                        line.AddToClassList("tg-colrow");
                        foreach (var cell in f.Children().ToList()) line.Add(cell);
                    }
                    else line.Add(f);
                    if (Body.childCount > 0) Body.Add(Gap(2, true));
                    Body.Add(line);
                }
                Add(Body);
            }
            var left = GraphLook.Row("tg-col", "tg-col-in");
            GraphColumns.Box(left);
            var right = GraphLook.Row("tg-col", "tg-col-out");
            var leftRows = new List<VisualElement>();
            // Another argument, item or field is added by a + at the end of the last one's row
            string group = null;
            (JValue owner, GraphPort port)? last = null;
            VisualElement lastRow = null;
            void AddMore()
            {
                if (last == null) return;
                var plus = GroupAdd(last.Value.owner, last.Value.port.Group, true);
                if (plus != null)
                {
                    // In the rest of the row's cells, with its ×
                    var rest = lastRow.Q(className: "tg-c3");
                    if (rest == null)
                    {
                        rest = new VisualElement { pickingMode = PickingMode.Ignore };
                        rest.AddToClassList("tg-cell");
                        rest.AddToClassList("tg-c3");
                        rest.style.marginLeft = 5;
                        lastRow.Insert(lastRow.Children().Count(c => c.ClassListContains("tg-cell")), rest);
                    }
                    if (rest.childCount > 0) plus.style.marginLeft = 5;
                    rest.style.display = DisplayStyle.Flex;
                    rest.Add(plus);
                }
                last = null;
            }
            foreach (var (owner, p) in rowInputs)
            {
                if (p == headIn) continue;
                if (p.Group != group)
                {
                    AddMore();
                    group = p.Group;
                }
                var row = InputRow(owner, p, p == opRow && !alone ? op : null, p == opRow && alone ? op : null);
                leftRows.Add(row);
                if (p.Group != null)
                {
                    last = (owner, p);
                    lastRow = row;
                }
            }
            AddMore();
            foreach (var key in EmptyGroups(shown, ins))
            {
                var add = GroupAdd(shown, key, false);
                if (add != null) leftRows.Add(add);
            }
            var rightRows = outs.Concat(execOuts).Where(p => p != headOut).Select(OutRow).ToList();
            var rowGap = GraphSchema.Size("rowGap", 1);
            Spaced(left, rowGap, leftRows);
            Spaced(right, rowGap, rightRows);
            if (leftRows.Count > 0 || rightRows.Count > 0)
            {
                var portsRow = GraphLook.Row("tg-node-ports");
                portsRow.Add(left);
                portsRow.Add(Gap(14));
                portsRow.Add(right);
                Add(portsRow);
                if (Body == null) Body = portsRow;
            }
            Head.EnableInClassList("tg-node-header--only", childCount == 1);
            Retooltip();
        }

        // Which node an input is of: the call's, for a statement drawn with one
        JValue OwnerOf(GraphPort p, JValue n) => Call != null && p.Node == Call.Wire("id") ? Call : n;

        // What an operator is called (Multiply), for a binary or unary one: its own list's title, else the other's
        static string OpTitle(string op, bool unary)
        {
            var own = unary ? GraphSchema.UnaryOpTitles : GraphSchema.BinaryOps;
            var other = unary ? GraphSchema.BinaryOps : GraphSchema.UnaryOpTitles;
            return own.FirstOrDefault(o => o.op == op).title ?? other.FirstOrDefault(o => o.op == op).title ?? op ?? "";
        }

        // What a key of a node holds, as the page's schema says (schema.js, keyType): its wire, a flag, a type, a
        // name or a text
        static string KeyType(string key, JValue owner)
        {
            var spec = GraphSchema.Spec(owner.Str("k"));
            if (key == "value" && owner.Str("k") == "bool") return "flag";
            if (spec != null && (spec.Inputs.Contains(key) || spec.Outputs.Contains(key) || key == "next" || spec.Lists.Any(l => l.Key == key) || spec.Items.Any(i => i.Key == key) || spec.Map == key)) return "wire";
            if (key == "params" && owner.Str("k") == "lambda") return "list";
            switch (key)
            {
                case "kind":
                case "id":
                case "k":
                case "x":
                case "y":
                    return "fixed";
                case "comment": return "comment";
                case "text": return "text";
                case "typeArgs": return "strings";
                case "mut":
                case "shared":
                case "extern":
                case "indexed":
                case "postfix":
                case "local":
                case "verified":
                case "previous":
                case "changed":
                case "withBlock":
                case "stacked":
                case "shorthand":
                case "ownership":
                case "scene":
                    return "flag";
                case "typeParams":
                case "argNames":
                case "using":
                case "rules":
                case "parts":
                case "formats":
                case "modifiers":
                    return "strings";
                case "type":
                case "returnType":
                case "fails":
                case "backing":
                    return "type";
            }
            return "string";
        }

        // The lists, items or named inputs of a node that have nothing in them yet: each still gets its way to add
        static IEnumerable<string> EmptyGroups(JValue n, List<GraphPort> ins)
        {
            var spec = GraphSchema.Spec(n.Str("k"));
            var have = new HashSet<string>(ins.Select(p => p.Group).Where(g => g != null));
            foreach (var l in spec?.Lists ?? Array.Empty<GraphSchema.NodeList>())
                if (!have.Contains(l.Key) && l.Key != "values") yield return l.Key;
            foreach (var i in spec?.Items ?? Array.Empty<GraphSchema.NodeItems>())
                if (!have.Contains(i.Key) && i.Key != "values") yield return i.Key;
            if (spec?.Map != null && !have.Contains(spec.Map) && spec.Map != "values") yield return spec.Map;
        }

        // The way to add to a node's list, items or named inputs: a row of its own ("+ argument") while there are
        // none, `inline` a + at the end of the last one's row
        VisualElement GroupAdd(JValue n, string key, bool inline)
        {
            var spec = GraphSchema.Spec(n.Str("k"));
            if (spec == null || n.Str("k") == "interp") return null;
            var grower = GrowersOf(n, spec).FirstOrDefault(g => g.key == key);
            if (grower.add == null) return null;
            var b = GraphLook.Plain(() => Frame.Edit(GraphMenu.AddA(grower.what), grower.add), "tg-port-add");
            if (inline) b.AddToClassList("tg-port-add-inline");
            b.text = inline ? "+" : "+ " + grower.what;
            b.tooltip = GraphSchema.Word("add.another", "what", grower.what);
            b.name = "add:" + grower.what;
            if (key == "args" && (n.Str("k") == "call" || n.Str("k") == "method")) argGrowers.Add((b, n, grower.what));
            return b;
        }

        // A text a node holds: a number's, a text's, a character's, Tide's
        VisualElement TextField(JValue n, string k, IList<string> order)
        {
            var wide = k == "text" || k == "code";
            var text = k == "char" ? (n.Str("text") ?? "").Trim('\'') : n.Str("text");
            var f = GraphLook.Inline(text, typed =>
            {
                if (wide)
                {
                    Frame.Edit("Set value", () => n.Set("text", JValue.Of(typed), order));
                    return;
                }
                if (!GraphValues.TryLiteral(typed, k == "string" || k == "char" ? k : null, out var kind, out var made) || kind != "int" && kind != "float" && kind != k)
                {
                    Window.Say(GraphSchema.Word("check.number", "text", typed), true);
                    Frame.Refresh();
                    return;
                }
                Frame.Edit("Set value", () => GraphValues.SetLiteral(n, k == "string" || k == "char" ? k : kind, made));
            }, k == "string" ? GraphSchema.Word("value.text") : wide ? "Tide" : "0", "tg-node-input");
            f.name = "value";
            if (wide)
            {
                f.multiline = true;
                f.AddToClassList("tg-node-code");
                f.style.unityFontDefinition = FontDefinition.FromFont(GraphLook.Mono);
            }
            else Fit(f, text, 3, GraphSchema.Size("font", 13));
            return f;
        }

        // A field as wide as its text and a letter more, as the page sizes one: `least` letters at the least
        static void Fit(TextField f, string text, int least, float fontSize, string placeholder = "0")
        {
            var letters = Mathf.Max(least, (string.IsNullOrEmpty(text) ? placeholder ?? "" : text).Length + 1);
            f.style.width = letters * GraphLook.Ch(fontSize) + 12;
        }

        // A lambda's inputs: a type and a name for each, and a way to add one
        VisualElement LambdaParams(JValue n, IList<string> order)
        {
            var box = GraphLook.Row("tg-lambda-params");
            var ps = n.Arr("params");
            for (var i = 0; ps != null && i < ps.Items.Count; i++)
            {
                var p = ps.Items[i];
                var row = GraphLook.Row("tg-lambda-param");
                var chip = new GraphTypeChip(p.Str("type"), null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest { Title = GraphSchema.KeyInfo("type").Label, Current = p.Str("type") },
                    from, type => Frame.Edit("Set type", () => p.Set("type", JValue.Of(type), GraphSchema.LambdaParamOrder))));
                row.Add(chip);
                row.Add(GraphLook.Name(p.Str("name"), name => Frame.Edit("Rename", () => p.Set("name", JValue.Of(name), GraphSchema.LambdaParamOrder)), "name"));
                var gone = p;
                var x = GraphLook.Plain(() => Frame.Edit("Remove input", () => n.Arr("params").Items.Remove(gone)), "tg-icon");
                x.text = GraphSchema.Word("close");
                x.tooltip = GraphSchema.Word("lambda.delete");
                row.Add(x);
                box.Add(row);
            }
            var add = GraphLook.Plain(() => Frame.Edit(GraphSchema.Word("lambda.add"), () =>
            {
                var list = n.Arr("params");
                if (list == null) n.Set("params", list = JValue.NewArray(), order);
                var p = JValue.NewObject();
                p.Set("type", JValue.Of("float"));
                p.Set("name", JValue.Of(GraphDocument.UniqueIn(list, "x")));
                list.Items.Add(p);
            }), "tg-port-add");
            add.text = GraphSchema.Word("lambda.add");
            add.tooltip = GraphSchema.Word("lambda.add.tip");
            box.Add(add);
            return box;
        }

        // What a Set (or a step) changes, in its title: its name, underlined in its type's colour, clicked for the
        // picker of what this code can change
        Button ChangesButton(JValue owner)
        {
            var path = new WirePath(changesKey);
            var ck = changes.Str("k");
            var what = ck == "member" ? (changes.Flag("shorthand") ? "." : "") + changes.Str("member") : Frame.TextOf(changes);
            Button b = null;
            b = GraphLook.Plain(() => Frame.Board.PickValue(Frame, owner, path, LabelOf(Window.Checked, owner, path, "what"), b), "tg-set-target");
            b.text = what ?? "?";
            b.name = "changes";
            var type = Frame.TypeAt(changes.Wire("id"));
            b.tooltip = type != null ? GraphSchema.Word("set.target.typed", "article", Article(type), "type", type) : GraphSchema.Word("set.target");
            var c = GraphSchema.TypeColor(type) ?? GraphSchema.Look("muted", Frame.Board.Light);
            b.style.borderBottomColor = GraphLook.Fade(c, 0.8f);
            // What it changes is lit with the variable's Gets and where it comes from
            var variable = ck == "name" && Frame.Reads.ContainsKey(changes.Str("name") ?? "") ? changes.Str("name") : null;
            var local = ck == "local" ? changes.Wire("ref") : 0;
            if (variable != null || local != 0)
            {
                Reads.Add(((variable, local), b));
                b.RegisterCallback<PointerEnterEvent>(_ => Frame.Light(variable, local, true));
                b.RegisterCallback<PointerLeaveEvent>(_ => Frame.Light(variable, local, false));
            }
            return b;
        }

        static string Article(string word) => GraphSchema.Word(!string.IsNullOrEmpty(word) && "aeiouAEIOU".IndexOf(word[0]) >= 0 ? "an.lower" : "a.lower");

        // ---------------------------------------------------------------------------------------------------------
        // Inputs

        // An input's row: its pin, its name (or `lead` in its place: how a Set changes), then what's in it. A
        // number, a tick or text is edited there; a name or a path is a chip; a wire from a node ends at its pin;
        // nothing yet is "pick…". A filled one has its × at the end.
        protected VisualElement InputRow(JValue owner, GraphPort p, VisualElement lead, VisualElement trail)
        {
            var from = Frame.WireAt(owner, p.Path);
            var shown = Frame.ShownIn(from);
            var row = GraphLook.Row("tg-port", "tg-in");
            if (from == 0 || shown != null) row.AddToClassList("tg-inline");
            row.name = "in:" + p.Path;
            var pin = Pin(p);
            pin.Connected = from != 0;
            var label = p.Label ?? "";
            // Its cells, in the columns of its node's inputs: its pin and name (or the operator in its place), what's in
            // it, and the rest
            var items = new List<(VisualElement, int)> { (pin, 1) };
            if (lead != null) items.Add((lead, 1));
            else if (!string.IsNullOrEmpty(label))
            {
                var word = GraphLook.Text(label, "tg-port-label");
                items.Add((word, 1));
                if (p.Path.Key == "args" && (owner.Str("k") == "call" || owner.Str("k") == "method")) argLabels.Add((word, owner, p.Path));
            }
            if (from == 0) items.Add((PickButton(owner, p.Path, label), 2));
            else if (shown != null) items.Add((InlineValue(owner, p.Path, label, from), 2));
            items.Add((trail, 3));
            if (from != 0) items.Add((EmptyButton(owner, p.Path, label), 3));
            GraphColumns.Row(row, items);
            rowLabels[row] = label;
            inputs[row] = (owner, p.Path, label);
            // Alt and a click empties it: what was in it, or its wire
            var emptied = p.Path;
            row.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0 || !e.altKey || Frame.WireAt(owner, emptied) == 0) return;
                e.StopPropagation();
                Frame.Empty(owner, emptied, label);
            }, TrickleDown.TrickleDown);
            foreach (var id in Frame.ChainOf(from)) hiddenRows.Add((id, row));
            return row;
        }

        // Opens the value picker of an input under `at`; `keep` is the place in the path shown there to change it
        // from
        void Open(JValue owner, WirePath path, string label, VisualElement at, int keep = 0, string typed = null)
            => Frame.Board.PickValue(Frame, owner, path, LabelOf(Window.Checked, owner, path, label), at, keep, typed);

        // Opens the value picker of an input of it, by its path, as a click on what's in it does
        internal GraphValuePicker OpenPicker(string path)
        {
            var row = this.Q("in:" + path);
            if (row == null || !inputs.TryGetValue(row, out var input)) return null;
            return Frame.Board.PickValue(Frame, input.owner, input.path, LabelOf(Window.Checked, input.owner, input.path, input.label), row);
        }

        Button PickButton(JValue owner, WirePath path, string label)
        {
            Button pick = null;
            pick = GraphLook.Plain(() => Open(owner, path, label, pick), "tg-pick");
            pick.text = GraphSchema.Word("input.pick");
            pick.name = "pick";
            pick.tooltip = GraphSchema.Word("input.pick.tip");
            return pick;
        }

        // The ▾ beside a value typed in an input: the picker, for something else
        Button OtherButton(JValue owner, WirePath path, string label, VisualElement at)
        {
            var b = GraphLook.Plain(() => Open(owner, path, label, at), "tg-row-pick");
            b.text = "▾";
            b.tooltip = GraphSchema.Word("input.other");
            b.name = "other";
            return b;
        }

        // A simple value where it's read: a number's, a tick's or a text's own control, a vector's numbers, a
        // colour's swatch, or a chip with a name or a path
        VisualElement InlineValue(JValue owner, WirePath path, string label, int w)
        {
            var n = Frame.ShownIn(w);
            var k = n.Str("k");
            var box = GraphLook.Row("tg-inline-value");
            var items = new List<VisualElement>();
            var font = GraphSchema.Size("font", 13);
            inputs[box] = (owner, path, label);
            if (k == "int" || k == "float" || k == "unary")
            {
                var inner = k == "unary" ? Frame.ById.TryGetValue(n.Wire("value"), out var v) ? v : null : n;
                var text = (k == "unary" ? "-" : "") + (inner?.Str("text") ?? "");
                var f = GraphLook.Inline(text, typed => Frame.TypeInto(owner, path, typed, box, LabelOf(Window.Checked, owner, path, label)), "0", "tg-inline-number");
                f.tooltip = GraphSchema.Word("input.number");
                f.name = "typed";
                Fit(f, text, 2, font * 0.92f * 0.95f);
                items.Add(f);
            }
            else if (k == "call")
            {
                // A vector's numbers, one for each of its arguments, or a colour's swatch
                var args = n.Arr("args")?.Items ?? new List<JValue>();
                var texts = args.Select(a => Frame.ShownIn(JValue.AsId(a)) is JValue s ? Frame.TextOf(s) : "").ToList();
                var name = n.Str("name");
                if (GraphSchema.ValueControl(name) == "colour" && args.Count >= 3)
                {
                    float Number(int i) => float.TryParse(texts[i].TrimEnd('f', 'F', 'd', 'D').Replace("_", ""), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var f) ? f : 0;
                    var alpha = args.Count == 4;
                    var field = new UnityEditor.UIElements.ColorField { value = new Color(Number(0), Number(1), Number(2), alpha ? Number(3) : 1), showAlpha = alpha, showEyeDropper = false };
                    field.name = "colour";
                    field.AddToClassList("tg-value-color");
                    field.tooltip = GraphSchema.Word("input.colour", "value", Frame.TextOf(n));
                    field.RegisterValueChangedCallback(e =>
                    {
                        e.StopPropagation();
                        var c = e.newValue;
                        var made = new List<string> { GraphValues.Format(c.r), GraphValues.Format(c.g), GraphValues.Format(c.b) };
                        if (alpha) made.Add(GraphValues.Format(c.a));
                        // The field stays as it is while its picker is open: the change is its own
                        KeepOnce = true;
                        Frame.PutNumbers(n, made, label);
                    });
                    items.Add(GraphLook.Text(name, "tg-port-type"));
                    items.Add(field);
                }
                else
                {
                    var vector = GraphLook.Row("tg-inline-vector");
                    vector.name = "numbers";
                    var axes = new List<VisualElement>();
                    for (var i = 0; i < args.Count; i++)
                    {
                        var at = new WirePath("args", i);
                        var f = GraphLook.Inline(texts[i], typed => Frame.TypeInto(n, at, typed, vector, LabelOf(Window.Checked, n, at, label)), "0", "tg-inline-number", "tg-inline-axis");
                        f.name = "number" + i;
                        // Each box is its part, by the type's own names: float3: x
                        var part = LabelOf(Window.Checked, n, at, null);
                        f.tooltip = part.Length == 1 ? name + ": " + part : name;
                        Fit(f, texts[i], 1, font * 0.92f * 0.95f);
                        axes.Add(f);
                    }
                    Spaced(vector, 3, axes);
                    items.Add(vector);
                }
            }
            else if (k == "bool")
            {
                var t = new Toggle { value = n.Flag("value") };
                t.name = "ticked";
                t.RegisterValueChangedCallback(e =>
                {
                    e.StopPropagation();
                    Frame.Edit("Set value", () => n.Set("value", JValue.Of(e.newValue)));
                });
                var tick = GraphLook.Row("tg-value-bool");
                Spaced(tick, 5, new VisualElement[] { t, GraphLook.Text(GraphSchema.Word(n.Flag("value") ? "value.yes" : "value.no"), "tg-value-bool-word") });
                items.Add(tick);
            }
            else if (k == "string")
            {
                var f = GraphLook.Inline(n.Str("text"), typed => Frame.TypeInto(owner, path, "\"" + typed + "\"", box, LabelOf(Window.Checked, owner, path, label)), GraphSchema.Word("value.text"), "tg-inline-text");
                f.tooltip = GraphSchema.Word("input.text");
                f.name = "typed";
                Fit(f, n.Str("text"), 3, font * 0.92f, GraphSchema.Word("value.text"));
                items.Add(GraphLook.Text("“", "tg-inline-quote"));
                items.Add(f);
                items.Add(GraphLook.Text("”", "tg-inline-quote"));
            }
            else if (k == "char")
            {
                var text = (n.Str("text") ?? "").Trim('\'');
                var f = GraphLook.Inline(text, typed => Frame.TypeInto(owner, path, "'" + typed + "'", box, LabelOf(Window.Checked, owner, path, label)), "a", "tg-inline-number");
                f.tooltip = GraphSchema.Word("input.char");
                f.name = "typed";
                Fit(f, text, 1, font * 0.92f * 0.95f, "a");
                items.Add(f);
            }
            else
            {
                box.Add(PathChip(owner, path, label, w));
                return box;
            }
            items.Add(OtherButton(owner, path, label, box));
            Spaced(box, 2, items);
            return box;
        }

        // A name or a path as a chip in its type's colour: each part is clicked to change the path from there
        VisualElement PathChip(JValue owner, WirePath path, string label, int w)
        {
            var leaf = Frame.ShownIn(w);
            var chip = GraphLook.Row("tg-path");
            chip.name = "chip";
            var dot = new VisualElement { pickingMode = PickingMode.Ignore };
            dot.AddToClassList("tg-path-dot");
            chip.Add(dot);
            var parts = Frame.PathOf(leaf);
            for (var i = 0; i < parts.Count; i++)
            {
                var at = i;
                if (i > 0) chip.Add(GraphLook.Text(".", "tg-path-sep"));
                var part = GraphLook.Plain(() => Open(owner, path, label, chip, at), "tg-path-part");
                part.text = i == 0 ? Frame.TextOf(parts[0]) : parts[i].Str("member") ?? Frame.PartText(parts[i]);
                part.name = "part" + i;
                part.tooltip = i == 0 ? GraphSchema.Word("chip.first") : GraphSchema.Word("chip.part", "of", Frame.PartText(parts[i - 1]));
                chip.Add(part);
            }
            Chips.Add((leaf.Wire("id"), chip));
            Tint(chip, Frame.TypeAt(leaf.Wire("id")), Frame.Board.Light);
            // What it reads is lit with it: the parameter's row, the node that declares the local, its other reads
            var root = parts[0];
            (string name, int local) key = root.Str("k") == "name" && Frame.Reads.ContainsKey(root.Str("name") ?? "") ? (root.Str("name"), 0) : root.Str("k") == "local" ? (null, root.Wire("ref")) : (null, 0);
            if (key.name != null || key.local != 0)
            {
                Reads.Add((key, chip));
                chip.RegisterCallback<PointerEnterEvent>(_ => Frame.Light(key.name, key.local, true));
                chip.RegisterCallback<PointerLeaveEvent>(_ => Frame.Light(key.name, key.local, false));
            }
            return chip;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Gets and reroutes

        // A Get: no title bar, the variable's name and its value pin, in its type's colour. A field of a value has
        // its Target pin (what it's a field of) on its left. Hovering it lights where the variable comes from and
        // its other Gets.
        void BuildGet(JValue n, string k)
        {
            Head = GraphLook.Row("tg-get-row");
            Add(Head);
            var ports = GraphPorts.Of(n);
            var items = new List<VisualElement>();
            if (k == "member" && !n.Flag("shorthand"))
            {
                var of = ports.First(p => p.Input && !p.Exec && p.Path?.Key == "object");
                of.Label = TargetWord;
                items.Add(InputRow(n, of, null, null));
            }
            string variable = null;
            var local = 0;
            var text = k == "member" ? (n.Flag("shorthand") ? "." : "") + n.Str("member") : Frame.TextOf(n);
            switch (k)
            {
                case "name":
                {
                    var order = GraphSchema.NodeOrder(k);
                    var f = GraphLook.DottedNameField(n.Str("name"), name => Frame.Edit("Rename", () => n.Set("name", JValue.Of(name), order)), "name", "tg-get-name");
                    f.Named("title");
                    getName = f;
                    if (Frame.Reads.ContainsKey(n.Str("name") ?? "")) variable = n.Str("name");
                    break;
                }
                case "member":
                {
                    var order = GraphSchema.NodeOrder(k);
                    var f = GraphLook.Name(n.Str("member"), name => Frame.Edit("Rename", () => n.Set("member", JValue.Of(name), order)), "field", "tg-get-name");
                    f.Named("title");
                    getName = f;
                    break;
                }
                case "local":
                    getName = GraphLook.Text(text, "tg-get-name");
                    local = n.Wire("ref");
                    break;
                default:
                    getName = GraphLook.Text(text, "tg-get-name");
                    break;
            }
            items.Add(getName);
            var output = ports.First(p => !p.Input && !p.Exec);
            output.Label = "";
            var outRow = GraphLook.Row("tg-port", "tg-out", "tg-get-out");
            var pin = Pin(output);
            pin.tooltip = GraphSchema.Word("pin.get");
            outRow.Add(pin);
            items.Add(outRow);
            Spaced(Head, 6, items);
            if (variable != null || local != 0)
            {
                lights = (variable, local);
                Reads.Add((lights.Value, this));
            }
            TintGet();
        }

        // A reroute: a knot on a wire, its input on the left and its value on the right, which goes to any number of
        // inputs. Dragged by its middle.
        void BuildKnot(JValue n)
        {
            var ports = GraphPorts.Of(n);
            var into = ports.First(p => p.Input && !p.Exec && p.Path?.Key == "value");
            var inRow = GraphLook.Row("tg-port", "tg-in", "tg-knot-in");
            inRow.name = "in:value";
            var pin = Pin(into);
            pin.Connected = n.Wire("value") != 0;
            inRow.Add(pin);
            inputs[inRow] = (n, into.Path, "value");
            rowLabels[inRow] = "";
            Add(inRow);
            var body = new VisualElement();
            body.AddToClassList("tg-knot-body");
            Add(body);
            var outRow = GraphLook.Row("tg-port", "tg-out", "tg-knot-out");
            outRow.Add(Pin(ports.First(p => !p.Input && !p.Exec)));
            Add(outRow);
            Head = body;
            TintGet();
        }

        // A Get's and a knot's colours: its type's, a little of it over the node and its edge half of it
        void TintGet()
        {
            if (getName == null && !ClassListContains("tg-knot")) return;
            var light = Frame.Board.Light;
            var known = GraphSchema.TypeColor(typeText);
            if (ClassListContains("tg-knot"))
            {
                var body = this.Q(className: "tg-knot-body");
                if (body != null) body.style.backgroundColor = known ?? GraphSchema.Look("muted", light);
                return;
            }
            var c = known ?? GraphSchema.ColorOf(GraphSchema.Spec(Node?.Str("k"))?.Category);
            style.backgroundColor = GraphLook.Mix(c, 0.14f, GraphSchema.Look("node", light));
            var edge = selected ? new StyleColor(StyleKeyword.Null) : new StyleColor(GraphLook.Mix(c, 0.55f, GraphSchema.Look("nodeBorder", light)));
            style.borderLeftColor = edge;
            style.borderRightColor = edge;
            style.borderTopColor = edge;
            style.borderBottomColor = edge;
        }

        // An interpolated text as one line: its parts with {} (or {:format}) where each value goes
        static string Template(JValue n)
        {
            var parts = n.Arr("parts");
            var formats = n.Arr("formats");
            var sb = new StringBuilder();
            for (var i = 0; parts != null && i < parts.Items.Count; i++)
            {
                sb.Append(parts.Items[i].String);
                if (i >= parts.Items.Count - 1) continue;
                var format = formats != null && i < formats.Items.Count ? formats.Items[i].String : "";
                sb.Append(string.IsNullOrEmpty(format) ? "{}" : "{:" + format + "}");
            }
            return sb.ToString();
        }

        static void SetTemplate(JValue n, string text)
        {
            var parts = new List<string>();
            var formats = new List<string>();
            var part = new StringBuilder();
            for (var i = 0; i < text.Length; i++)
            {
                var close = text[i] == '{' ? text.IndexOf('}', i) : -1;
                var inside = close > i ? text.Substring(i + 1, close - i - 1) : null;
                if (inside != null && (inside.Length == 0 || inside[0] == ':'))
                {
                    parts.Add(part.ToString());
                    part.Clear();
                    formats.Add(inside.Length > 0 ? inside.Substring(1).Trim() : "");
                    i = close;
                    continue;
                }
                part.Append(text[i]);
            }
            parts.Add(part.ToString());
            var order = GraphSchema.NodeOrder("interp");
            var values = n.Arr("values") ?? JValue.NewArray();
            while (values.Items.Count < formats.Count) values.Items.Add(JValue.Null());
            while (values.Items.Count > formats.Count) values.Items.RemoveAt(values.Items.Count - 1);
            n.Set("parts", JValue.Strings(parts), order);
            if (formats.Any(f => f.Length > 0)) n.Set("formats", JValue.Strings(formats), order);
            else n.Remove("formats");
            n.Set("values", values, order);
        }

        // The rows of inputs that show a node that isn't drawn (a value typed, a name, a path): its problems are
        // the row's
        readonly List<(int id, VisualElement row)> hiddenRows = new List<(int, VisualElement)>();

        // What can be added to a node's lists: another argument, item, case, field, input; and the list it's of
        static List<(string what, Action add, string key)> GrowersOf(JValue n, GraphSchema.NodeSpec spec)
        {
            var growers = new List<(string, Action, string)>();
            var k = n.Str("k");
            var order = GraphSchema.NodeOrder(k);
            void Grow(string what, Action add, string key) => growers.Add((what, add, key));
            foreach (var list in spec.Lists)
            {
                if (k == "interp") continue; // Its {} make its values
                var l = list;
                if (k == "index" && (n.Arr(l.Key)?.Items.Count ?? 0) >= 3) continue;
                Grow(l.Label, () =>
                {
                    var a = n.Arr(l.Key);
                    if (a == null) n.Set(l.Key, a = JValue.NewArray(), order);
                    a.Items.Add(JValue.Null());
                    if (l.Names != null) n.Arr(l.Names)?.Items.Add(JValue.Of(""));
                }, l.Key);
            }
            foreach (var items in spec.Items)
            {
                var it = items;
                var what = it.Element == "init" ? GraphSchema.Word("add.field") : it.Label;
                Grow(what, () =>
                {
                    var a = n.Arr(it.Key);
                    if (a == null) n.Set(it.Key, a = JValue.NewArray(), order);
                    var item = JValue.NewObject();
                    if (it.Element == "init") item.Set("name", JValue.Of("value"));
                    foreach (var wire in it.Wires)
                    {
                        if (wire.EndsWith("[]")) item.Set(wire.Substring(0, wire.Length - 2), JValue.ArrayOf(new[] { JValue.Null() }));
                        else item.Set(wire, JValue.Null());
                    }
                    a.Items.Add(item);
                }, it.Key);
            }
            if (spec.Map != null)
                Grow(GraphSchema.Word("add.input.word"), () =>
                {
                    var o = n.Get(spec.Map);
                    if (o == null || !o.IsObject) n.Set(spec.Map, o = JValue.NewObject(), order);
                    var name = "a";
                    for (var c = 'a'; c <= 'z' && o.Keys.Contains(name); c++) name = c.ToString();
                    if (o.Keys.Contains(name)) name = "input" + o.Keys.Count;
                    o.Set(name, JValue.Null());
                }, spec.Map);
            return growers;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Its menu: what's rare for its kind, a note, copy, duplicate, delete

        // Its menu: the board's one menu for a node (GraphMenu). Its flags, type arguments and argument names are
        // in Details.
        internal List<GraphOption> MenuOptions() => GraphMenu.ForNode(this);

        // What its menu offers to add: another of each of its lists (an argument, an item, a case)
        internal IEnumerable<(string title, Action add)> Adds()
        {
            foreach (var v in Call != null ? new[] { Call, Node } : new[] { Node })
            {
                if (v == null) continue;
                var k = v.Str("k");
                var spec = GraphSchema.Spec(k);
                if (spec == null) continue;
                foreach (var (what, add, _) in GrowersOf(v, spec)) yield return (GraphMenu.AddA(what), () => Frame.Edit(GraphMenu.AddA(what), add));
            }
        }

        // A note on it, asked for
        internal void AskNote(JValue noted)
        {
            var info = GraphSchema.KeyInfo("comment");
            Window.Open(new GraphPrompt(info.Label, info.Help, noted.Str("comment"), true, text =>
                Frame.Edit(info.Label, () => noted.SetString("comment", text.TrimEnd(), GraphSchema.NodeOrder(noted.Str("k"))))), this, true);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Problems, types and what the language server says of it

        // The problems of the node, of the call drawn with it and of what's typed on its inputs: said under it
        public void ShowProblems()
        {
            if (Node == null) return;
            var said = new List<GraphDiagnostic>(Frame.Board.Problems(Id));
            if (Call != null) said.AddRange(Frame.Board.Problems(Call.Wire("id")));
            foreach (var v in Call != null ? new[] { Node, Call } : new[] { Node })
                foreach (var w in GraphPorts.DataWires(v))
                    foreach (var id in Frame.ChainOf(w)) said.AddRange(Frame.Board.Problems(id));
            problem?.RemoveFromHierarchy();
            problem = null;
            EnableInClassList("tg-has-problem", said.Count > 0);
            // On the row of the pin it names (an input with nothing wired), or of the input that shows what it's about
            var marked = new Dictionary<VisualElement, List<string>>();
            void Mark(VisualElement row, string message)
            {
                if (row == null) return;
                if (!marked.TryGetValue(row, out var list)) marked[row] = list = new List<string>();
                list.Add(message);
            }
            foreach (var (id, row) in hiddenRows)
                foreach (var d in Frame.Board.Problems(id)) Mark(row, d.Text);
            foreach (var d in said)
                if (d.Port != null && !hiddenRows.Any(h => h.id == d.Id)) Mark(this.Q("in:" + d.Port + (d.Index >= 0 ? "." + d.Index : "")) ?? this.Query<VisualElement>().Where(e => e.name != null && e.name.StartsWith("in:" + d.Port + (d.Index >= 0 ? "." + d.Index + "." : "."))).First(), d.Text);
            foreach (var row in rowLabels.Keys.Concat(hiddenRows.Select(h => h.row)).Distinct().ToList())
            {
                var of = marked.TryGetValue(row, out var list) ? list : null;
                row.EnableInClassList("tg-port-row--problem", of != null);
                row.tooltip = of != null ? string.Join("\n", of) : "";
            }
            if (said.Count == 0) return;
            problem = GraphLook.Text(string.Join("\n", said.Select(d => GraphLook.FirstLine(d.Text)).Distinct()), "tg-node-problem", "tg-problem");
            if (said.All(d => d.Severity != "error")) problem.AddToClassList("tg-node-problem--warning");
            problem.tooltip = string.Join("\n\n", said.Select(d => d.Text + (string.IsNullOrEmpty(d.Detail) ? "" : "\n" + d.Detail)));
            problem.pickingMode = PickingMode.Ignore;
            Add(problem);
        }

        public string ProblemText => problem?.text;
        public string TypeText => typeText;
        public string HoverText => hoverText;

        public void SetType(string type)
        {
            if (type == typeText) return;
            typeText = type;
            hoverText = null;
            hoverAsked = false;
            Retooltip();
            TintGet();
        }

        public void SetHover(string text)
        {
            hoverText = text;
            Retooltip();
        }

        public void ForgetHover()
        {
            hoverAsked = false;
            hoverText = null;
            Retooltip();
        }

        void Retooltip()
        {
            var spec = Node != null ? GraphSchema.Spec((Call ?? Node).Str("k")) : null;
            var parts = new List<string>();
            if (!string.IsNullOrEmpty(hoverText)) parts.Add(hoverText);
            else if (!string.IsNullOrEmpty(typeText)) parts.Add(typeText);
            if (getName != null) parts.Add("A Get: its pin goes to any number of inputs.");
            else if (spec?.Help != null) parts.Add(spec.Help);
            if (Head != null) Head.tooltip = string.Join("\n\n", parts);
        }

        // What the language server says of it, asked the first time the pointer's on it after a check
        internal void AskHover()
        {
            if (Id <= 0 || hoverAsked || Node == null) return;
            hoverAsked = true;
            Retooltip();
            TideGraphLanguage.Hover(Window, (Call ?? Node).Wire("id"), text =>
            {
                if (panel != null) SetHover(text);
            });
        }

        public PortView PortAt(int node, WirePath path, bool input, bool exec)
        {
            foreach (var p in Ports)
            {
                if (p.Port.Node != node || p.Port.Input != input || p.Port.Exec != exec || p.Port.IsRead) continue;
                if (path == null ? p.Port.Path == null : path.Equals(p.Port.Path)) return p;
            }
            return null;
        }

        public PortView ExecIn => Ports.FirstOrDefault(p => p.Port.Input && p.Port.Exec);
        public PortView DataOut => Ports.FirstOrDefault(p => !p.Port.Input && !p.Port.Exec && !p.Port.IsRead && p.Port.Node == Id);
        public PortView LocalPin(int declares) => Ports.FirstOrDefault(p => p.Port.Local && p.Port.Node == declares);
        public PortView ReadPin(string name) => Ports.FirstOrDefault(p => p.Port.Name == name);
        public PortView Input(string path) => Ports.FirstOrDefault(p => p.Port.Input && !p.Port.Exec && p.Port.Path != null && p.Port.Path.ToString() == path);
        public PortView Output(string path) => Ports.FirstOrDefault(p => !p.Port.Input && p.Port.Exec && p.Port.Path != null && p.Port.Path.ToString() == path);
    }

    static class GraphLists
    {
        public static IEnumerable<string> Strings(JValue a) => a == null ? Enumerable.Empty<string>() : a.Items.Where(i => i.Kind == JKind.String).Select(i => i.String);

        // Splits "Map<K, V>, int" at the commas between types, not inside angle brackets or parentheses
        public static List<string> Split(string text)
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
                if (c == '<' || c == '(' || c == '[' || c == '{') depth++;
                else if ((c == '>' || c == ')' || c == ']' || c == '}') && depth > 0) depth--;
            }
            return parts;
        }
    }

    // A value frame's end: what the value is
    sealed class GraphResultView : GraphNodeView
    {
        public GraphResultView(GraphFrame frame) : base(frame, GraphFrame.ResultId, null, null)
        {
            AddToClassList("tg-node--small");
        }

        public override Vector2 Estimate() => new Vector2(120, 30);

        // Its title, what the value is, and the input its value is wired into
        public override void Build()
        {
            Clear();
            Ports.Clear();
            ResetShown();
            AddToClassList("tg-result");
            var key = Frame.Slot.Key == "components" ? "value" : Frame.Slot.Key ?? "value";
            Head = GraphLook.Row("tg-node-header", "tg-node-head");
            Head.Add(GraphLook.Text(GraphSchema.KeyInfo(key).Label, "tg-node-title"));
            Add(Head);
            var port = new GraphPort { Node = GraphFrame.ResultId, Input = true, Exec = false, Path = new WirePath("root"), Label = Frame.Wanted(null, new WirePath("root")) ?? GraphSchema.Word("search.itsValue") };
            if (Graph != null) Add(InputRow(null, port, null, null));
        }
    }

    // A frame's first node: what its code takes. The kind's sentence ("Every tick, for each entity with"), then a
    // row for each thing it takes, with a pin its code reads it from: how it's taken ("reads", "changes"), its
    // type (picked) and its name. Filters ("only with", "without") have no pin. A last row adds one. Its arrow is
    // where the code starts.
    sealed class GraphEntryView : GraphNodeView
    {
        public GraphEntryView(GraphFrame frame) : base(frame, GraphFrame.EntryId, null, null)
        {
            AddToClassList("tg-entry");
        }

        GraphElement Owner => Frame.Owner;
        JValue Routine => Frame.Routine;

        // As the page sizes it before it's drawn: as wide as the look says, and a row for each thing it takes
        public override Vector2 Estimate() => new Vector2(GraphLook.EntryWidth, 34 + GraphLook.CardRow * (RowsOf(Frame) + 1));

        // How many rows of what it takes it has: its parameters, its values, its event, its result
        internal static int RowsOf(GraphFrame frame)
        {
            var r = frame.Routine;
            if (r == null) return 0;
            return (r.Arr("params")?.Items.Count ?? 0) + (r.Arr("payload")?.Items.Count ?? 0) + (r.Get("trigger") != null ? 1 : 0) + (r.Has("returnType") ? 1 : 0);
        }

        public static string SignatureOf(GraphFrame frame)
        {
            var e = frame.Owner;
            if (e == null) return "";
            var sb = new StringBuilder(frame.Doc.TitleOf(e));
            foreach (var v in new[] { e.Value, frame.Routine })
            {
                if (v == null) continue;
                for (var i = 0; i < v.Keys.Count; i++)
                {
                    var key = v.Keys[i];
                    if (key == "x" || key == "y" || key == "body" || key == "members" || key == "accessors") continue;
                    sb.Append(key).Append(':').Append(GraphJson.Write(v.Values[i])).Append(';');
                }
            }
            foreach (var read in frame.Reads.Values) sb.Append(frame.NameIsRead(read.Name) ? "R" : "-");
            sb.Append(frame.Graph?.Wire("entry") != 0 ? "E" : "e");
            return sb.ToString();
        }

        // How the entry reads: its kind's sentence, an accessor's label
        string Sentence()
        {
            var e = Owner;
            var kind = e.Value.Str("kind");
            if (e.Role == GraphRole.Accessor) return GraphSchema.AccessorInfo(e.Value.Str("word")).Label;
            var member = e.Role == GraphRole.Member;
            return GraphSchema.Sentence(kind, member) ?? (member ? GraphSchema.MemberTitle(kind) : GraphSchema.DeclTitle(kind));
        }

        // The last row's words, without the plus the row draws
        static string RowWords(string addRow) => addRow == null ? null : addRow.StartsWith("+") ? addRow.Substring(1).Trim() : addRow;

        readonly Dictionary<string, VisualElement> rowsByName = new Dictionary<string, VisualElement>();

        // The row of something the code takes, by its name
        internal VisualElement RowOf(string name) => name != null && rowsByName.TryGetValue(name, out var row) ? row : null;

        // A row of something with a name: hovering it lights what reads it
        void Named(string name, VisualElement row, PortView pin)
        {
            if (string.IsNullOrEmpty(name)) return;
            rowsByName[name] = row;
            pin.tooltip = GraphSchema.Word("pin.param", "name", name);
            row.RegisterCallback<PointerEnterEvent>(_ => Frame.Light(name, 0, true));
            row.RegisterCallback<PointerLeaveEvent>(_ => Frame.Light(name, 0, false));
        }

        public override void Build()
        {
            Clear();
            Ports.Clear();
            rowsByName.Clear();
            var e = Owner;
            var r = Routine;
            if (e == null || r == null) return;
            var kind = r.Str("kind");
            var member = e.Role != GraphRole.Decl;
            style.width = GraphLook.EntryWidth;
            Head = GraphLook.Row("tg-entry-head");
            Head.Add(GraphLook.Text(Sentence(), "tg-entry-sentence", "tg-node-title"));
            if (Graph != null)
            {
                var row = GraphLook.Row("tg-port", "tg-out", "tg-entry-start");
                var start = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = true, Path = new WirePath("entry"), Label = "" });
                start.Connected = Graph.Wire("entry") != 0;
                start.tooltip = GraphSchema.Word("pin.start");
                row.Add(start);
                Head.Add(row);
            }
            Add(Head);
            Body = GraphLook.Row("tg-entry-rows");
            Add(Body);
            if (e.Role == GraphRole.Accessor)
            {
                if (e.Value.Str("word") == "set")
                {
                    var row = GraphLook.Row("tg-entry-row");
                    var type = e.Owner?.Str("type");
                    var pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = "value", Label = "value" });
                    pin.Connected = Frame.NameIsRead("value");
                    pin.SetType(type);
                    var pinRow = GraphLook.Row("tg-port", "tg-out", "tg-pin");
                    pinRow.Add(pin);
                    Spaced(row, 6, new VisualElement[] { Fixed(GraphSchema.KeyInfo("value").Label), new GraphTypeChip(type, null, Frame.Board.Light, _ => { }).Class("tg-type-fixed"), GraphLook.Text("value", "tg-entry-name", "tg-muted"), pinRow });
                    Named("value", row, pin);
                    Body.Add(row);
                }
                return;
            }

            if (kind == "handler") Trigger(e, r);
            var payload = r.Arr("payload");
            if (kind == "command")
            {
                for (var i = 0; payload != null && i < payload.Items.Count; i++) Payload(e, payload, payload.Items[i]);
                var add = GraphLook.Plain(() => AddPayload(null), "tg-add-row");
                add.text = "+ " + GraphSchema.KeyInfo("payload").Label.ToLowerInvariant() + "…";
                add.name = "add-payload";
                add.tooltip = GraphSchema.Word("entry.payload.tip");
                Body.Add(add);
            }
            var parameters = r.Arr("params");
            var sample = e.Role == GraphRole.Member && kind == "sample";
            var addRow = GraphSchema.AddRow(kind, member);
            if (addRow != null || parameters != null)
            {
                for (var i = 0; parameters != null && i < parameters.Items.Count; i++) Param(parameters, parameters.Items[i], 0, sample);
                Button needs = null;
                needs = GraphLook.Plain(() => Frame.OpenNeeds(needs, parameters, sample), "tg-add-row");
                needs.text = addRow ?? GraphSchema.AddRow("function", false);
                needs.name = "needs";
                needs.tooltip = GraphSchema.Word("entry.needs.tip");
                Body.Add(needs);
            }
            if (GraphSchema.HasKey(kind, member, "returnType"))
            {
                var row = GraphLook.Row("tg-entry-row", "tg-entry-result");
                row.name = "result-row";
                var type = r.Str("returnType");
                var chip = new GraphTypeChip(type, "void", Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = GraphSchema.KeyInfo("returnType").Label, Current = type, AllowVoid = true, Generic = Doc.TypeParamsAt(e.Id),
                }, from, picked => Frame.Edit("Set result", () => r.Set("returnType", JValue.Of(picked), GraphDocument.OrderOf(e)))));
                chip.name = "result";
                Spaced(row, 6, new VisualElement[] { Fixed(GraphSchema.KeyInfo("returnType").Label), chip });
                Body.Add(row);
            }
            if (r.Flag("extern")) Body.Add(GraphLook.Text(GraphSchema.Word("entry.extern", "extern", GraphSchema.KeyInfo("extern").Label), "tg-entry-note"));
            Columns();
        }

        // Its rows in columns: how each is taken (and local, verified, previous) | its type (and scene) | its name
        // (and its default) | its pin
        void Columns()
        {
            // A query's own rows are a box of their own under it
            var boxes = new List<VisualElement> { Body };
            VisualElement query = null;
            foreach (var c in Body.Children().ToList())
            {
                if (!c.ClassListContains("tg-entry-sub"))
                {
                    query = null;
                    continue;
                }
                if (query == null)
                {
                    query = GraphLook.Row("tg-entry-query");
                    Body.Insert(Body.IndexOf(c), query);
                    boxes.Add(query);
                }
                c.RemoveFromClassList("tg-entry-sub");
                query.Add(c);
            }
            foreach (var box in boxes) Columns(box);
        }

        void Columns(VisualElement box)
        {
            GraphColumns.Box(box);
            foreach (var row in box.Children().Where(c => c.ClassListContains("tg-entry-row")).ToList())
            {
                var parts = row.Children().Where(c => !c.ClassListContains("tg-gap") && !c.ClassListContains("tg-grow")).ToList();
                var type = parts.FindIndex(c => c is GraphTypeChip || c.ClassListContains("tg-type"));
                var name = parts.FindIndex(c => c is GraphName || c.ClassListContains("tg-entry-name") || c is TextField);
                GraphColumns.Sort(row, (c, i) =>
                {
                    if (c.ClassListContains("tg-row-more")) return 0;
                    if (c.ClassListContains("tg-pin")) return 4;
                    var at = parts.IndexOf(c);
                    if (c.ClassListContains("tg-mode")) return 1;
                    if (type >= 0 && at == type) return 2;
                    if (name >= 0 && at >= name) return 3;
                    if (type >= 0 && at > type) return 2;
                    if (type < 0 && name < 0) return 2;
                    return type >= 0 && at < type ? 1 : 3;
                });
            }
        }

        // How a row is taken, where it can't be changed: "trigger", "payload", "result"
        static Label Fixed(string word) => GraphLook.Text((word ?? "").ToLowerInvariant(), "tg-mode", "tg-mode-fixed");

        // A row's pin, at its end
        VisualElement PinRow(PortView pin)
        {
            var row = GraphLook.Row("tg-port", "tg-out", "tg-pin");
            row.Add(pin);
            return row;
        }

        void Trigger(GraphElement e, JValue r)
        {
            var t = r.Get("trigger");
            JValue Made()
            {
                var made = r.Get("trigger");
                if (made != null && made.IsObject) return made;
                made = JValue.NewObject();
                r.Set("trigger", made, GraphDocument.OrderOf(e));
                return made;
            }
            var row = GraphLook.Row("tg-entry-row");
            row.name = "trigger";
            var type = t?.Str("type");
            var changed = t?.Flag("changed") ?? false;
            var items = new List<VisualElement> { Fixed(GraphSchema.KeyInfo("trigger").Label) };
            if (changed)
            {
                var path = GraphLook.Inline(type, typed => Frame.Edit("Event", () => Made().Set("type", JValue.Of(GraphLook.DottedName(typed)), GraphSchema.TriggerOrder)), "Component.field", "tg-entry-name");
                path.tooltip = GraphSchema.Word("entry.changed.tip");
                items.Add(path);
            }
            else
            {
                var chip = new GraphTypeChip(type, GraphSchema.Word("entry.event"), Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = GraphSchema.Word("entry.event"), Current = type, NoBuilders = true, Only = x => x.Declared == "event" || x.Kind == TideGraphLanguage.KindEvent,
                    Place = new GraphTypePlace { Element = e.Id, After = "event(" },
                }, from, picked => Frame.Edit("Event", () => Made().Set("type", JValue.Of(picked), GraphSchema.TriggerOrder))));
                chip.name = "event";
                items.Add(chip);
            }
            var name = GraphLook.Name(t?.Str("name"), clean =>
            {
                Frame.Edit("Rename", () =>
                {
                    var made = Made();
                    if (clean.Length == 0) made.Remove("name");
                    else GraphDocument.RenameRead(r, made, clean);
                    // Its keys in tidec's order
                    var typeNow = made.Get("type");
                    made.Remove("type");
                    made.Remove("name");
                    if (typeNow != null) made.Set("type", typeNow, GraphSchema.TriggerOrder);
                    if (clean.Length > 0) made.Set("name", JValue.Of(clean), GraphSchema.TriggerOrder);
                });
            }, GraphSchema.Word("placeholder.lower"), "tg-entry-name");
            name.AllowEmpty = true;
            name.Named("trigger-name");
            items.Add(name);
            PortView pin = null;
            if (!string.IsNullOrEmpty(t?.Str("name")))
            {
                pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = t.Str("name"), Label = t.Str("name") });
                pin.Connected = Frame.NameIsRead(t.Str("name"));
                pin.SetType(changed ? null : type);
                items.Add(PinRow(pin));
            }
            Spaced(row, 6, items);
            if (pin != null) Named(t.Str("name"), row, pin);
            Body.Add(row);
        }

        void Payload(GraphElement e, JValue list, JValue p)
        {
            var id = p.Wire("id");
            var row = GraphLook.Row("tg-entry-row");
            row.name = "row" + id;
            row.userData = id;
            var type = p.Str("type");
            var items = new List<VisualElement> { Fixed(GraphSchema.KeyInfo("payload").Label) };
            items.Add(new GraphTypeChip(type, null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest { Title = GraphSchema.KeyInfo("type").Label, Current = type, Place = new GraphTypePlace { Element = id, Find = type } },
                from, picked => Frame.Edit("Set type", () => p.Set("type", JValue.Of(picked), GraphSchema.PayloadOrder)))));
            items.Add(NameOf(list, p, GraphSchema.PayloadOrder));
            var pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = p.Str("name"), Label = p.Str("name") });
            pin.Connected = Frame.NameIsRead(p.Str("name"));
            pin.SetType(type);
            items.Add(PinRow(pin));
            Spaced(row, 6, items);
            row.Add(RowMore(id));
            Named(p.Str("name"), row, pin);
            Body.Add(row);
            Problems(id, row);
        }

        // A row's menu, seen while the pointer is on it, over its end: it takes no room in the row
        VisualElement RowMore(int id)
        {
            var more = GraphLook.Plain(null, "tg-more", "tg-row-more", "tg-on-hover");
            more.text = GraphSchema.Word("more");
            more.tooltip = GraphSchema.Word("more.tip");
            more.clicked += () => GraphMenu.Open(Window, GraphMenu.ForRow(Frame, id), more);
            return more;
        }

        internal int AddPayload(string type)
        {
            var e = Owner;
            var r = Routine;
            JValue made = null;
            void Add(string picked) => Frame.Edit("Add value", () =>
            {
                var list = r.Arr("payload");
                if (list == null) r.Set("payload", list = JValue.NewArray(), GraphDocument.OrderOf(e));
                made = JValue.NewObject();
                made.Set("id", JValue.Of(Doc.NewId()));
                made.Set("type", JValue.Of(picked));
                made.Set("name", JValue.Of(GraphDocument.UniqueIn(list, GraphFrame.NameFor(picked))));
                list.Items.Add(made);
            });
            if (type != null) Add(type);
            else Frame.Board.PickType(new GraphTypeRequest { Title = "A value it's called with" }, this, Add);
            return made?.Wire("id") ?? 0;
        }

        GraphName NameOf(JValue list, JValue p, IList<string> order)
        {
            var r = Routine;
            var name = GraphLook.Name(p.Str("name"), typed =>
            {
                if (GraphDocument.ReadsOf(r).Any(x => x.Source != p && x.Name == typed))
                {
                    Window.Say($"It takes a '{typed}' already.", true);
                    Frame.Refresh();
                    return;
                }
                Frame.Edit("Rename", () =>
                {
                    GraphDocument.RenameRead(r, p, typed);
                    // The name where tidec writes it
                    p.Remove("name");
                    p.Set("name", JValue.Of(typed), order);
                });
            }, "name");
            name.Named("name" + p.Wire("id"));
            return name;
        }

        void Param(JValue list, JValue p, int depth, bool sample)
        {
            var id = p.Wire("id");
            var order = sample ? GraphSchema.SampleParamOrder : GraphSchema.ParamOrder;
            var mode = sample ? "read" : p.Str("mode") ?? "read";
            var filter = mode == "with" || mode == "without";
            var row = GraphLook.Row("tg-entry-row");
            if (filter) row.AddToClassList("tg-entry-filter");
            if (depth > 0) row.AddToClassList("tg-entry-sub");
            row.name = "row" + id;
            row.userData = id;
            if (!string.IsNullOrEmpty(p.Str("comment"))) row.tooltip = p.Str("comment");
            var items = new List<VisualElement>();
            if (!sample)
            {
                var info = GraphSchema.ParamModeInfo(mode);
                Button word = null;
                word = GraphLook.Plain(() => Frame.OpenModes(word, list, p), "tg-mode");
                word.text = info.Label;
                word.tooltip = GraphSchema.Word("entry.mode.tip", "help", info.Help ?? GraphSchema.Word("details.mode"));
                word.name = "mode" + id;
                items.Add(word);
            }
            foreach (var flag in new[] { "local", "verified", "previous" })
                if (p.Flag(flag))
                {
                    var tag = GraphLook.Text(flag, "tg-row-tag");
                    tag.tooltip = GraphSchema.KeyInfo(flag).Label;
                    items.Add(tag);
                }
            if (p.Flag("ownership")) items.Add(GraphLook.Text("ownership", "tg-type", "tg-type-fixed"));
            else if (mode != "query")
            {
                var type = p.Str("type");
                var chip = new GraphTypeChip(type, null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = GraphSchema.KeyInfo("type").Label, Current = type, Generic = Doc.TypeParamsAt(Frame.Slot.Owner), NoBuilders = filter,
                    Place = new GraphTypePlace { Element = id, Find = type },
                }, from, picked => Frame.Edit("Set type", () => p.Set("type", JValue.Of(picked), order))));
                chip.name = "type" + id;
                items.Add(chip);
            }
            if (p.Flag("scene")) items.Add(GraphLook.Text("scene", "tg-row-tag"));
            if (filter) items.Add(Grow());
            else items.Add(NameOf(list, p, order).Class("tg-entry-name"));
            PortView pin = null;
            if (!filter && !string.IsNullOrEmpty(p.Str("name")) && depth == 0)
            {
                pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = p.Str("name"), Label = p.Str("name") });
                pin.Connected = Frame.NameIsRead(p.Str("name"));
                pin.SetType(mode == "query" ? null : p.Str("type"));
                items.Add(PinRow(pin));
            }
            Spaced(row, 6, items);
            row.Add(RowMore(id));
            if (pin != null) Named(p.Str("name"), row, pin);
            Body.Add(row);
            if (p.Get("default") != null)
            {
                var value = GraphLook.Row("tg-entry-row", "tg-entry-sub");
                value.Add(GraphLook.Text(GraphSchema.KeyInfo("default").Label, "tg-mode", "tg-mode-fixed"));
                value.Add(GraphValues.Control(Window, new GraphSlot(id, "default"), p.Str("type"), true, Frame.Id, p.Str("name")));
                Body.Add(value);
            }
            Problems(id, row);
            if (mode != "query") return;
            var query = p.Arr("query");
            for (var i = 0; query != null && i < query.Items.Count; i++) Param(query, query.Items[i], depth + 1, false);
            Button add = null;
            add = GraphLook.Plain(() => Frame.OpenNeeds(add, p.Arr("query"), false, p), "tg-add-row", "tg-entry-sub");
            add.text = GraphSchema.Word("entry.query.add");
            add.tooltip = GraphSchema.Word("entry.query.tip");
            Body.Add(add);
        }

        void Problems(int id, VisualElement row) => Frame.AddProblems(Body, id, row);
    }
}
