using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A pin: a circle for a value, an arrow for the flow of statements; filled when wired, in its value's type's
    // colour. A drag from one makes a wire.
    sealed class PortView : VisualElement
    {
        public readonly GraphPort Port;
        public readonly GraphNodeView View;
        public bool Connected;
        public string Type; // The type of the value on it, as the language server checked it (or as it's declared)
        bool hover;

        public PortView(GraphNodeView view, GraphPort port)
        {
            View = view;
            Port = port;
            AddToClassList("tg-port");
            tooltip = port.Label ?? "";
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
                if (e.button != 0) return;
                View.Frame.Board.BeginWire(this, e);
                e.StopPropagation();
            });
        }

        public GraphFrame Frame => View.Frame;
        bool Light => View.Frame.Board.Light;

        // On a node's title, which is dark in both themes, a pin is light
        bool OnTitle => parent != null && parent.ClassListContains("tg-node-head");

        public Color Tint => Port.Exec ? (OnTitle ? Color.white : GraphLook.Of(Light).Exec) : TideGraphTypes.ColorOf(Type, Light && !OnTitle);

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = contentRect;
            var c = r.center;
            var tint = Tint;
            p.strokeColor = tint;
            p.fillColor = tint;
            p.lineWidth = hover ? 2.4f : 1.6f;
            p.lineJoin = LineJoin.Round;
            p.BeginPath();
            if (Port.Exec)
            {
                p.MoveTo(new Vector2(c.x - 3.5f, c.y - 5));
                p.LineTo(new Vector2(c.x + 4.5f, c.y));
                p.LineTo(new Vector2(c.x - 3.5f, c.y + 5));
                p.ClosePath();
            }
            else p.Arc(c, 4.5f, 0, 360);
            if (Connected || hover) p.Fill();
            p.Stroke();
        }

        // Where wires join it, in another element's space
        public Vector2 Anchor(VisualElement space) => this.ChangeCoordinatesTo(space, new Vector2(layout.width / 2, layout.height / 2));

        public void SetType(string type)
        {
            if (Port.Exec || type == Type) return;
            Type = type;
            tooltip = string.IsNullOrEmpty(type) ? Port.Label ?? "" : string.IsNullOrEmpty(Port.Label) ? type : $"{Port.Label}: {type}";
            MarkDirtyRepaint();
        }
    }

    // A node in a frame: its title in its category's colour, its pins, and its own values on it. A statement that
    // is a call is one node (the call with the statement's flow). What reads a parameter or a local, and a number
    // or a text only one input reads, aren't nodes of their own: a wire from the parameter's or local's pin, and a
    // value typed on the input.
    class GraphNodeView : VisualElement
    {
        public readonly GraphFrame Frame;
        public readonly int Id;          // The node's id; GraphFrame.EntryId or ResultId for the frame's entry and value
        public JValue Node;              // Null for the entry and value
        public JValue Call;              // The call drawn with it, when it's a statement that is one
        public readonly List<PortView> Ports = new List<PortView>();
        public string Signature;         // What it was built from: built again when that changes
        protected VisualElement Head, Body;
        Label problem;
        string typeText, hoverText;
        bool hoverAsked, selected;

        public GraphNodeView(GraphFrame frame, int id, JValue node, JValue call)
        {
            Frame = frame;
            Id = id;
            Node = node;
            Call = call;
            AddToClassList("tg-node");
            RegisterCallback<PointerEnterEvent>(_ => AskHover());
        }

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
        public virtual Vector2 Estimate() => EstimateOf(Node, Call);

        public static Vector2 EstimateOf(JValue n, JValue call)
        {
            var widest = 0;
            var ports = 1;
            foreach (var v in new[] { n, call })
            {
                if (v == null) continue;
                foreach (var value in v.Values)
                    if (value.Kind == JKind.String && value.String.Length > widest) widest = value.String.Length;
                ports += GraphPorts.Of(v).Count(p => p.Path != null);
            }
            return new Vector2(140 + Mathf.Min(260, widest * 8), 34 + 22 * ports);
        }

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                EnableInClassList("tg-selected", value);
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
                    var from = p.Path.Get(v);
                    if (from == 0 || !frame.Hidden.Contains(from) || !frame.ById.TryGetValue(from, out var source)) continue;
                    sb.Append('<').Append(GraphJson.Write(source)).Append(frame.IsFar(v, source) ? "far>" : ">");
                }
                if (frame.Declares(v))
                    sb.Append(frame.Readers.TryGetValue(v.Wire("id"), out var readers) && readers.Count > 0 ? "R" : "");
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
            hiddenRows.Clear();
            problem = null;
            var n = Node;
            var shown = Call ?? n;
            var k = shown.Str("k");
            var spec = GraphSchema.Spec(k);
            var own = GraphSchema.Spec(n.Str("k"));
            EnableInClassList("tg-node--unreached", !Frame.Reached.Contains(Id));
            var small = k == "name" || IsLiteral(k) || k == "this" || k == "default" || k == "null";
            EnableInClassList("tg-node--small", small);

            var note = shown.Str("comment") ?? n.Str("comment");
            if (!string.IsNullOrEmpty(note))
            {
                var said = GraphLook.Text(note, "tg-node-note");
                said.tooltip = "A note. Change it from the node's menu.";
                Add(said);
            }

            Head = GraphLook.Row("tg-node-head");
            Head.style.backgroundColor = GraphSchema.ColorOf(spec?.Category ?? "text");
            Add(Head);
            Body = GraphLook.Row("tg-node-body");
            Add(Body);

            var ports = GraphPorts.Of(n);
            var callPorts = Call != null ? GraphPorts.Of(Call) : new List<GraphPort>();
            var execIn = ports.FirstOrDefault(p => p.Input && p.Exec);
            if (execIn != null) Head.Add(Pin(execIn));
            Title(shown, k, spec);
            var more = GraphLook.IconButton("more", "More for this node", null, "tg-on-hover");
            more.clicked += () => Frame.OpenNodeMenu(this, more);
            Head.Add(more);
            var next = ports.FirstOrDefault(p => !p.Input && p.Exec && p.Path?.Key == "next" && p.Path.Parts.Length == 1);
            var dataOut = ports.FirstOrDefault(p => !p.Input && !p.Exec);
            if (next != null) Head.Add(Pin(next));
            else if (dataOut != null && execIn == null) Head.Add(Pin(dataOut));

            Fields(shown, k);

            // Its inputs: the call's when it's drawn with one, but not the statement's value, which is the call
            foreach (var p in callPorts.Where(p => p.Input && !p.Exec)) InputRow(Call, p);
            foreach (var p in ports.Where(p => p.Input && !p.Exec && !(Call != null && p.Path.Key == "value" && p.Path.Parts.Length == 1))) InputRow(n, p);
            Growers(shown);
            if (Call != null && Call != shown) Growers(Call);

            // The local it declares: a pin to read it from
            foreach (var v in Call != null ? new[] { n, Call } : new[] { n })
                if (GraphSchema.Spec(v.Str("k"))?.Declares != null) LocalRow(v);
            // Its value, when it's a statement whose value can be read after it
            if (execIn != null && dataOut != null && (Call != null || n.Wire("value") != 0))
            {
                var row = GraphLook.Row("tg-port-row", "tg-port-row--out");
                row.Add(GraphLook.Text("result", "tg-port-label"));
                dataOut.Label = "result";
                row.Add(Pin(dataOut));
                Body.Add(row);
            }
            // Its flow out, but next
            foreach (var p in ports.Where(p => !p.Input && p.Exec && p != next))
            {
                var row = GraphLook.Row("tg-port-row", "tg-port-row--out");
                row.Add(GraphLook.Text(Word(p), "tg-port-label"));
                row.Add(Pin(p));
                Body.Add(row);
            }
            if (spec == null) Body.Add(GraphLook.Text($"'{k}' isn't a kind of node this editor knows: it's kept as it is.", "tg-empty"));
            Body.style.display = Body.childCount > 0 ? DisplayStyle.Flex : DisplayStyle.None;
        }

        static bool IsLiteral(string k) => k == "int" || k == "float" || k == "string" || k == "bool" || k == "char";

        protected PortView Pin(GraphPort p)
        {
            var v = new PortView(this, p);
            Ports.Add(v);
            return v;
        }

        // What a pin is called: the schema's words for its key (portInfo), its label for one of a list
        static string Word(GraphPort p)
        {
            if (p.Path == null) return p.Label ?? "";
            if (p.Path.Parts.Length == 1) return GraphSchema.PortLabel(p.Path.Key);
            return p.Label ?? "";
        }

        // ---------------------------------------------------------------------------------------------------------
        // The title

        void Title(JValue shown, string k, GraphSchema.NodeSpec spec)
        {
            void Words(string text) => Head.Add(GraphLook.Text(text, "tg-node-title"));
            TextField Named(string key, bool dotted = false)
            {
                var order = GraphSchema.NodeOrder(k);
                var f = GraphLook.Inline(shown.Str(key), typed =>
                {
                    var name = dotted ? GraphLook.DottedName(typed) : GraphLook.Identifier(typed);
                    if (name.Length == 0)
                    {
                        Frame.Refresh();
                        return;
                    }
                    Frame.Edit("Rename", () => shown.Set(key, JValue.Of(name), order));
                }, "name");
                f.name = "title";
                f.style.flexGrow = 1;
                Head.Add(f);
                return f;
            }
            var ops = GraphSchema.Ops(k);
            switch (k)
            {
                case "call":
                    if (shown.Flag("local")) Head.Add(GraphLook.Text("local", "tg-port-label"));
                    Named("name");
                    break;
                case "method":
                case "member":
                {
                    Head.Add(GraphLook.Text(".", "tg-node-title", "tg-node-dot"));
                    Named(k == "member" ? "member" : "name");
                    if (!shown.Flag("shorthand"))
                    {
                        var id = shown.Wire("id");
                        Head.Add(GraphLook.IconButton("down", "Pick from what its value has", () => Frame.Board.OpenMemberSearch(Frame, id, this)));
                    }
                    break;
                }
                case "name":
                    Named("name", true);
                    break;
                case "local":
                {
                    var decl = Frame.ById.TryGetValue(shown.Wire("ref"), out var d) ? d : null;
                    Words(decl != null ? GraphSchema.LocalName(decl) : "a local that's gone");
                    break;
                }
                default:
                    if (ops.Length > 0)
                    {
                        var op = shown.Str("op");
                        var title = ops.FirstOrDefault(o => o.op == op).title;
                        Button pick = null;
                        pick = GraphLook.Plain(() => Frame.PickOperator(this, shown, pick), "tg-word");
                        pick.Add(new Label(title != null ? $"{title}  {op}" : $"{spec?.Title}  {op}") { pickingMode = PickingMode.Ignore, style = { color = Color.white, unityFontStyleAndWeight = FontStyle.Bold } });
                        pick.Add(new GraphIcon("down") { pickingMode = PickingMode.Ignore, style = { color = new Color(1, 1, 1, 0.7f) } });
                        pick.name = "op";
                        pick.style.flexGrow = 1;
                        Head.Add(pick);
                    }
                    else Words(spec?.Title ?? k ?? "?");
                    break;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Its own values

        void Fields(JValue n, string k)
        {
            var order = GraphSchema.NodeOrder(k);
            VisualElement Line(params VisualElement[] items)
            {
                var row = GraphLook.Row("tg-node-field");
                foreach (var i in items) if (i != null) row.Add(i);
                Body.Add(row);
                return row;
            }
            GraphTypeChip TypeOf(string key, string title, string infer, JValue of = null, IList<string> ofOrder = null, Func<GraphTypeInfo, bool> only = null)
            {
                var target = of ?? n;
                var chip = new GraphTypeChip(target.Str(key), infer, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = title, Current = target.Str(key), Infer = infer, Only = only, Generic = Doc.TypeParamsAt(Frame.Slot.Owner),
                }, from, type => Frame.Edit("Set type", () =>
                {
                    if (string.IsNullOrEmpty(type)) target.Remove(key);
                    else target.Set(key, JValue.Of(type), ofOrder ?? order);
                })));
                return chip;
            }
            TextField Named(string key, string placeholder, bool optional = false, JValue of = null, IList<string> ofOrder = null)
            {
                var target = of ?? n;
                return GraphLook.Inline(target.Str(key), typed =>
                {
                    var name = GraphLook.Identifier(typed);
                    if (name.Length == 0 && !optional)
                    {
                        Frame.Refresh();
                        return;
                    }
                    Frame.Edit("Rename", () => target.SetString(key, name, ofOrder ?? order));
                }, placeholder);
            }
            switch (k)
            {
                case "int":
                case "float":
                case "char":
                case "string":
                {
                    var f = GraphLook.Inline(k == "char" ? (n.Str("text") ?? "").Trim('\'') : n.Str("text"), typed =>
                    {
                        if (!GraphValues.TryLiteral(typed, k == "string" || k == "char" ? k : null, out var kind, out var text) || (kind != "int" && kind != "float" && kind != k))
                        {
                            Window.Say($"That isn't {GraphValues.Says(k)}.", true);
                            Frame.Refresh();
                            return;
                        }
                        Frame.Edit("Set value", () => GraphValues.SetLiteral(n, k == "string" || k == "char" ? k : kind, text));
                    }, k == "string" ? "text" : "0", "tg-inline--value");
                    f.name = "value";
                    Line(f);
                    break;
                }
                case "bool":
                {
                    var t = new Toggle { value = n.Flag("value") };
                    t.RegisterValueChangedCallback(e =>
                    {
                        e.StopPropagation();
                        Frame.Edit("Set value", () => n.Set("value", JValue.Of(e.newValue), order));
                    });
                    Line(t);
                    break;
                }
                case "call":
                case "method":
                {
                    var args = GraphLists.Strings(n.Arr("typeArgs")).ToList();
                    if (args.Count == 0) break;
                    var row = Line(GraphLook.Text("of", "tg-port-label"));
                    for (var i = 0; i < args.Count; i++)
                    {
                        var at = i;
                        var chip = new GraphTypeChip(args[i], null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest { Title = "Type argument", Current = args[at], Infer = "Take it away", Generic = Doc.TypeParamsAt(Frame.Slot.Owner) },
                            from, type => Frame.Edit("Type argument", () =>
                            {
                                if (string.IsNullOrEmpty(type)) args.RemoveAt(at);
                                else args[at] = type;
                                if (args.Count == 0) n.Remove("typeArgs");
                                else n.Set("typeArgs", JValue.Strings(args), order);
                            })));
                        row.Add(chip);
                    }
                    break;
                }
                case "var":
                case "foreach":
                case "parallel":
                case "outVar":
                    Line(GraphLook.Text("of type", "tg-port-label"), TypeOf("type", "Type of " + (n.Str("name") ?? "the local"), "worked out"));
                    break;
                case "literal":
                    Line(GraphLook.Text("a", "tg-port-label"), TypeOf("type", "A value of which type?", null, null, null, t => !t.BuiltIn || t.Name == "Color" || t.Name == "Rect"));
                    break;
                case "is":
                    Line(GraphLook.Text("is a", "tg-port-label"), TypeOf("type", "Which type?", null));
                    break;
                case "has":
                    Line(Named("subject", "type"), GraphLook.Text("has", "tg-port-label"), TypeOf("type", "A field of which type?", null), Named("field", "field"));
                    break;
                case "interp":
                {
                    var f = GraphLook.Inline(Template(n), typed => Frame.Edit("Text", () => SetTemplate(n, typed)), "text, with {} where a value goes", "tg-inline--value");
                    f.tooltip = "The text. Each {} is a value wired in below; {:F2} gives it a format.";
                    f.name = "value";
                    Line(f);
                    break;
                }
                case "text":
                case "code":
                {
                    var f = new TextField { value = n.Str("text") ?? "", multiline = true, isDelayed = true };
                    f.AddToClassList("tg-text-area");
                    f.style.unityFontDefinition = FontDefinition.FromFont(GraphLook.Mono);
                    f.style.minWidth = 200;
                    f.style.maxWidth = 380;
                    f.tooltip = "Tide, as text. An input's name in it stands for what's wired into that input.";
                    f.RegisterValueChangedCallback(e =>
                    {
                        e.StopPropagation();
                        Frame.Edit("Tide text", () => n.Set("text", JValue.Of(e.newValue), order));
                    });
                    Line(f);
                    break;
                }
                case "lambda":
                {
                    var ps = n.Arr("params");
                    for (var i = 0; ps != null && i < ps.Items.Count; i++)
                    {
                        var p = ps.Items[i];
                        var row = Line(TypeOf("type", "Type of " + p.Str("name"), null, p, GraphSchema.LambdaParamOrder), Named("name", "name", false, p, GraphSchema.LambdaParamOrder));
                        row.Add(GraphLook.IconButton("close", "Remove this input", () => Frame.Edit("Remove input", () => n.Arr("params").Items.Remove(p)), "tg-on-hover"));
                    }
                    break;
                }
            }
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

        // ---------------------------------------------------------------------------------------------------------
        // Inputs

        void InputRow(JValue owner, GraphPort p)
        {
            var row = GraphLook.Row("tg-port-row");
            var from = p.Path.Get(owner);
            var hidden = from != 0 && Frame.Hidden.Contains(from) && Frame.ById.TryGetValue(from, out var source) ? source : null;
            var literal = hidden != null && IsLiteral(hidden.Str("k")) ? hidden : null;
            var pin = Pin(p);
            pin.Connected = from != 0 && literal == null;
            row.Add(pin);
            row.name = "in:" + p.Path;
            // What it reads from far away is named here, not wired across the frame
            var far = hidden != null && literal == null && Frame.IsFar(owner, hidden) ? Frame.ReadName(hidden) : null;

            var spec = GraphSchema.Spec(owner.Str("k"));
            var items = p.Path.Parts.Length >= 3 ? spec?.Items.FirstOrDefault(i => i.Key == p.Path.Key) : null;
            var item = items != null ? owner.Arr(items.Key)?.Items[(int)p.Path.Parts[1]] : null;
            // An item with a name of its own (a value of a type's field): the name is typed on its row
            if (items != null && item != null && item.IsObject && item.Get(items.Label)?.Kind == JKind.String)
            {
                var order = items.Element != null ? GraphSchema.Element(items.Element) : null;
                var key = items.Label;
                var name = GraphLook.Inline(item.Str(key), typed =>
                {
                    var clean = GraphLook.Identifier(typed);
                    if (clean.Length == 0) Frame.Refresh();
                    else Frame.Edit("Rename", () => item.Set(key, JValue.Of(clean), order));
                }, "field");
                name.style.maxWidth = 110;
                row.Add(name);
            }
            else if (spec?.Map == p.Path.Key)
            {
                // A named input of a text or code node: its name is what stands for it in the text
                var at = p.Index;
                var name = GraphLook.Inline(p.Label, typed =>
                {
                    var clean = GraphLook.Identifier(typed);
                    var map = owner.Get(spec.Map);
                    if (clean.Length == 0 || map.Keys.Contains(clean)) Frame.Refresh();
                    else Frame.Edit("Rename input", () => map.Keys[at] = clean);
                }, "name");
                name.style.maxWidth = 110;
                row.Add(name);
            }
            else
            {
                var word = Word(p);
                if (!string.IsNullOrEmpty(word)) row.Add(GraphLook.Text(word, "tg-port-label"));
            }
            if (far != null)
            {
                var tag = GraphLook.Text(far, "tg-read");
                tag.tooltip = $"Reads {far}, which is too far for a wire to be worth drawing. Drag from this pin to change it.";
                row.Add(tag);
            }

            // What's typed on it: a number, a text, true or false; nothing typed is nothing wired
            if (literal != null || (from == 0 && TakesTyped(p)))
            {
                var box = GraphLook.Row("tg-pin-value");
                var path = p.Path;
                if (literal != null && literal.Str("k") == "bool")
                {
                    var t = new Toggle { value = literal.Flag("value") };
                    t.RegisterValueChangedCallback(e =>
                    {
                        e.StopPropagation();
                        Frame.Edit("Set value", () => literal.Set("value", JValue.Of(e.newValue)));
                    });
                    box.Add(t);
                }
                else
                {
                    var k = literal?.Str("k");
                    var text = literal == null ? "" : k == "string" ? "\"" + literal.Str("text") + "\"" : literal.Str("text") ?? "";
                    var f = GraphLook.Inline(text, typed => Frame.TypeInto(owner, path, typed, row), from == 0 ? "…" : "", "tg-inline--value");
                    f.tooltip = from == 0 ? "Type a number, a text in quotes, true or false. Or drag a wire here." : "";
                    f.name = "typed";
                    box.Add(f);
                }
                row.Add(box);
            }
            // One of a list that grows: taken away from its row
            if (p.Group != null && (spec?.Lists.Any(l => l.Key == p.Path.Key) == true || spec?.Map == p.Path.Key || items != null))
            {
                var port = p;
                row.Add(new VisualElement { style = { flexGrow = 1 } });
                row.Add(GraphLook.IconButton("close", "Take this away", () => Frame.RemoveInput(owner, port), "tg-on-hover"));
            }
            if (hidden != null) hiddenRows.Add((from, row));
            Body.Add(row);
        }

        // The rows of inputs that show a node that isn't drawn (a value typed, a read): its problems are the row's
        readonly List<(int id, VisualElement row)> hiddenRows = new List<(int, VisualElement)>();

        // Whether a value can be typed on an input: not on what a member is of, a loop goes through or an
        // assignment changes, which take a name or a wire
        static bool TakesTyped(GraphPort p)
        {
            if (p.Exec || p.Path == null) return false;
            var key = p.Path.Parts[p.Path.Parts.Length - 1] as string ?? p.Path.Key;
            return key != "object" && key != "target" && key != "collection" && key != "grid";
        }

        // The rows that add to a node's lists: an argument, an item, a case, a field
        void Growers(JValue n)
        {
            var k = n.Str("k");
            var spec = GraphSchema.Spec(k);
            if (spec == null) return;
            var order = GraphSchema.NodeOrder(k);
            void Grow(string what, Action add)
            {
                var b = GraphLook.Labelled("plus", what, () => Frame.Edit("Add " + what, add), "tg-add-row", "tg-node-add");
                b.name = "add:" + what;
                Body.Add(b);
            }
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
                });
            }
            foreach (var items in spec.Items)
            {
                var it = items;
                var what = it.Element == "init" ? "field" : it.Label;
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
                });
            }
            if (spec.Map != null)
                Grow("input", () =>
                {
                    var o = n.Get(spec.Map);
                    if (o == null || !o.IsObject) n.Set(spec.Map, o = JValue.NewObject(), order);
                    var name = "a";
                    for (var c = 'a'; c <= 'z' && o.Keys.Contains(name); c++) name = c.ToString();
                    if (o.Keys.Contains(name)) name = "input" + o.Keys.Count;
                    o.Set(name, JValue.Null());
                });
            if (k == "lambda")
                Grow("input", () =>
                {
                    var list = n.Arr("params");
                    if (list == null) n.Set("params", list = JValue.NewArray(), order);
                    var p = JValue.NewObject();
                    p.Set("type", JValue.Of("float"));
                    p.Set("name", JValue.Of(GraphDocument.UniqueIn(list, "x")));
                    list.Items.Add(p);
                });
        }

        // The local a node declares: its name, typed here, and the pin it's read from
        void LocalRow(JValue n)
        {
            var k = n.Str("k");
            var key = GraphSchema.Spec(k).Declares;
            var order = GraphSchema.NodeOrder(k);
            var optional = k == "is";
            var row = GraphLook.Row("tg-port-row", "tg-port-row--out");
            row.Add(GraphLook.Text(optional ? "as" : "named", "tg-port-label"));
            var name = GraphLook.Inline(n.Str(key), typed =>
            {
                var clean = GraphLook.Identifier(typed);
                if (clean.Length == 0 && !optional) Frame.Refresh();
                else Frame.Edit("Rename", () => n.SetString(key, clean, order));
            }, optional ? "no name" : "name");
            name.name = "local";
            name.style.maxWidth = 120;
            row.Add(name);
            if (!string.IsNullOrEmpty(n.Str(key)))
            {
                var pin = Pin(new GraphPort { Node = n.Wire("id"), Input = false, Exec = false, Local = true, Label = n.Str(key) });
                pin.Connected = Frame.LocalIsRead(n.Wire("id"));
                pin.SetType(n.Str("type"));
                row.Add(pin);
            }
            Body.Add(row);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Its menu: what's rare for its kind, a note, copy, duplicate, delete

        internal List<GraphOption> MenuOptions()
        {
            var options = new List<GraphOption>();
            foreach (var v in Call != null ? new[] { Call, Node } : new[] { Node })
            {
                var k = v.Str("k");
                var spec = GraphSchema.Spec(k);
                if (spec == null) continue;
                var order = GraphSchema.NodeOrder(k);
                // Its own flags, in the schema's words (flagInfo): the rare ones under "More"
                foreach (var key in spec.Keys)
                {
                    if (!GraphSchema.IsFlag(key)) continue;
                    var flag = key;
                    var target = v;
                    var words = GraphSchema.FlagInfo(key);
                    options.Add(new GraphOption
                    {
                        Title = words.Label, Detail = words.Help, Tall = words.Help != null, Section = words.Advanced ? "More" : null, Checked = v.Flag(key), Data = key,
                        Pick = () => Frame.Edit(words.Label, () => target.SetFlag(flag, !target.Flag(flag), order)),
                    });
                }
                if (k == "call" || k == "method")
                {
                    var target = v;
                    options.Add(new GraphOption
                    {
                        Title = "Add a type argument…", Detail = "For a call that names its types: Get<Health>()", Tall = true,
                        Pick = () => Frame.Board.PickType(new GraphTypeRequest { Title = "Type argument", Generic = Doc.TypeParamsAt(Frame.Slot.Owner) }, this, type => Frame.Edit("Type argument", () =>
                        {
                            var args = GraphLists.Strings(target.Arr("typeArgs")).ToList();
                            args.Add(type);
                            target.Set("typeArgs", JValue.Strings(args), order);
                        })),
                    });
                    if ((target.Arr("args")?.Items.Count ?? 0) > 0)
                        options.Add(new GraphOption { Title = "Name its arguments…", Detail = "A call that says which input is which: crit: true", Tall = true, Pick = () => Frame.AskArgNames(this, target) });
                }
            }
            if (options.Count > 0) options.Add(new GraphOption { Separator = true });
            var noted = Call ?? Node;
            var info = GraphSchema.KeyInfo("comment");
            options.Add(new GraphOption
            {
                Title = info.Label + "…", Pick = () => Window.Open(new GraphPrompt(info.Label, info.Help, noted.Str("comment"), true, text =>
                    Frame.Edit(info.Label, () => noted.SetString("comment", text.TrimEnd(), GraphSchema.NodeOrder(noted.Str("k"))))), this, true),
            });
            options.Add(new GraphOption { Title = "Copy", Detail = "Ctrl+C", Pick = () => Frame.Board.CopySelection() });
            options.Add(new GraphOption { Title = "Duplicate", Detail = "Ctrl+D", Pick = () => Frame.Board.DuplicateSelection() });
            options.Add(new GraphOption { Title = "Delete", Detail = "Del", Pick = () => Frame.Board.DeleteSelection() });
            return options;
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
                    if (Frame.Hidden.Contains(w)) said.AddRange(Frame.Board.Problems(w));
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
                foreach (var d in Frame.Board.Problems(id)) Mark(row, d.Message);
            foreach (var d in said)
                if (d.Port != null) Mark(this.Q("in:" + d.Port + (d.Index >= 0 ? "." + d.Index : "")) ?? this.Query<VisualElement>().Where(e => e.name != null && e.name.StartsWith("in:" + d.Port + (d.Index >= 0 ? "." + d.Index + "." : "."))).First(), d.Message);
            this.Query<VisualElement>(className: "tg-port-row").ForEach(row =>
            {
                var of = marked.TryGetValue(row, out var list) ? list : null;
                row.EnableInClassList("tg-port-row--problem", of != null);
                row.tooltip = of != null ? string.Join("\n", of) : "";
            });
            if (said.Count == 0) return;
            problem = GraphLook.Text(string.Join("\n", said.Select(d => GraphLook.FirstLine(d.Message))), "tg-node-problem", "tg-problem");
            if (said.All(d => d.Severity != "error")) problem.AddToClassList("tg-node-problem--warning");
            problem.tooltip = string.Join("\n\n", said.Select(d => d.Message + (string.IsNullOrEmpty(d.Detail) ? "" : "\n" + d.Detail)));
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
            if (spec?.Help != null) parts.Add(spec.Help);
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

        public override void Build()
        {
            Clear();
            Ports.Clear();
            Head = GraphLook.Row("tg-node-head");
            Head.style.backgroundColor = new Color(0.33f, 0.41f, 0.79f);
            var root = Graph?.Wire("root") ?? 0;
            var pin = Pin(new GraphPort { Node = GraphFrame.ResultId, Input = true, Exec = false, Path = new WirePath("root"), Label = "the value" });
            pin.Connected = root != 0;
            Head.Add(pin);
            Head.Add(GraphLook.Text("The value", "tg-node-title"));
            Add(Head);
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

        public override Vector2 Estimate()
        {
            var r = Routine;
            var rows = (r?.Arr("params")?.Items.Count ?? 0) + (r?.Arr("payload")?.Items.Count ?? 0) + 1;
            return new Vector2(260, 34 + GraphLook.CardRow * rows);
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

        public override void Build()
        {
            Clear();
            Ports.Clear();
            var e = Owner;
            var r = Routine;
            if (e == null || r == null) return;
            var kind = r.Str("kind");
            var member = e.Role != GraphRole.Decl;
            Head = GraphLook.Row("tg-node-head");
            Head.style.backgroundColor = new Color(0.33f, 0.41f, 0.79f);
            Head.Add(GraphLook.Text(Sentence(), "tg-node-title"));
            if (Graph != null)
            {
                var start = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = true, Path = new WirePath("entry"), Label = "Where the code starts: drag from here" });
                start.Connected = Graph.Wire("entry") != 0;
                Head.Add(start);
            }
            Add(Head);
            Body = GraphLook.Row("tg-node-body");
            Add(Body);
            if (e.Role == GraphRole.Accessor)
            {
                var help = GraphSchema.AccessorInfo(e.Value.Str("word")).Help;
                if (help != null) Body.Add(GraphLook.Text(help, "tg-empty"));
                return;
            }

            if (kind == "handler") Trigger(e, r);
            var payload = r.Arr("payload");
            if (kind == "command")
            {
                for (var i = 0; payload != null && i < payload.Items.Count; i++) Payload(e, payload, payload.Items[i]);
                var info = GraphSchema.KeyInfo("payload");
                var add = GraphLook.Labelled("plus", "a value it's called with…", () => AddPayload(null), "tg-add-row");
                add.name = "add-payload";
                add.tooltip = info.Label;
                Body.Add(add);
            }
            var parameters = r.Arr("params");
            var sample = e.Role == GraphRole.Member && kind == "sample";
            var addRow = GraphSchema.AddRow(kind, member);
            if (addRow != null || parameters != null)
            {
                for (var i = 0; parameters != null && i < parameters.Items.Count; i++) Param(parameters, parameters.Items[i], 0, sample);
                var function = Frame.TakesInputs;
                if ((parameters?.Items.Count ?? 0) == 0 && kind != "handler" && kind != "command")
                    Body.Add(GraphLook.Text(function ? "It takes nothing yet." : "Nothing yet. Add what it needs: its code reads each from a pin.", "tg-empty"));
                Button needs = null;
                needs = GraphLook.Labelled("plus", RowWords(addRow) ?? RowWords(GraphSchema.AddRow("function", false)), () => Frame.OpenNeeds(needs, parameters, sample), "tg-add-row");
                needs.name = "needs";
                needs.tooltip = function ? "Something it's given when it's called" : "A component each entity must have, the world, the time, a filter";
                Body.Add(needs);
            }
            if (GraphSchema.HasKey(kind, member, "returnType"))
            {
                var row = GraphLook.Row("tg-entry-row", "tg-entry-result");
                row.Add(GraphLook.Text(GraphSchema.KeyInfo("returnType").Label, "tg-port-label"));
                var type = r.Str("returnType");
                var chip = new GraphTypeChip(type, "void", Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = $"{GraphSchema.KeyInfo("returnType").Label} of {r.Str("name")}", Current = type, AllowVoid = true, Generic = Doc.TypeParamsAt(e.Id),
                }, from, picked => Frame.Edit("Set result", () => r.Set("returnType", JValue.Of(picked), GraphDocument.OrderOf(e)))));
                chip.name = "result";
                row.Add(chip);
                Body.Add(row);
            }
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
            if (changed) row.Add(GraphLook.Text("a change of", "tg-port-label"));
            if (changed)
            {
                var path = GraphLook.Inline(type, typed => Frame.Edit("Event", () => Made().Set("type", JValue.Of(GraphLook.DottedName(typed)), GraphSchema.TriggerOrder)), "Component.field");
                path.tooltip = "The field whose change it hears: Health.current";
                row.Add(path);
            }
            else
            {
                var chip = new GraphTypeChip(type, "pick an event", Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = "Which event does it hear?", Current = type, NoBuilders = true, Only = x => x.Declared == "event" || x.Kind == TideGraphLanguage.KindEvent,
                    Place = new GraphTypePlace { Element = e.Id, After = "event(" },
                }, from, picked => Frame.Edit("Event", () => Made().Set("type", JValue.Of(picked), GraphSchema.TriggerOrder))));
                chip.name = "event";
                row.Add(chip);
            }
            row.Add(GraphLook.Text("as", "tg-port-label"));
            var name = GraphLook.Inline(t?.Str("name"), typed =>
            {
                var clean = GraphLook.Identifier(typed);
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
            }, "no name");
            name.tooltip = "The name its code reads the event by";
            row.Add(name);
            var more = GraphLook.IconButton("more", "More for the event", null, "tg-on-hover");
            more.clicked += () =>
            {
                var options = new List<GraphOption>();
                var verified = GraphSchema.KeyInfo("verified");
                options.Add(new GraphOption { Title = verified.Label, Detail = verified.Help, Checked = t?.Flag("verified") ?? false, Pick = () => Frame.Edit(verified.Label, () => Made().SetFlag("verified", !(t?.Flag("verified") ?? false), GraphSchema.TriggerOrder)) });
                options.Add(new GraphOption { Title = "A field's change, not an event", Detail = "Hears a component's field changing: Changed<Health.current>", Tall = true, Checked = changed, Pick = () => Frame.Edit("Change", () => Made().SetFlag("changed", !changed, GraphSchema.TriggerOrder)) });
                var menu = new GraphOptionList(null, null, null, 300);
                menu.Set(options);
                Window.Open(menu, more);
            };
            row.Add(more);
            if (!string.IsNullOrEmpty(t?.Str("name")))
            {
                var pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = t.Str("name"), Label = t.Str("name") });
                pin.Connected = Frame.NameIsRead(t.Str("name"));
                pin.SetType(changed ? null : type);
                row.Add(pin);
            }
            Body.Add(row);
        }

        void Payload(GraphElement e, JValue list, JValue p)
        {
            var id = p.Wire("id");
            var row = GraphLook.Row("tg-entry-row");
            row.name = "row" + id;
            var type = p.Str("type");
            row.Add(new GraphTypeChip(type, null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest { Title = "Type of " + p.Str("name"), Current = type, Place = new GraphTypePlace { Element = id, Find = type } },
                from, picked => Frame.Edit("Set type", () => p.Set("type", JValue.Of(picked), GraphSchema.PayloadOrder)))));
            row.Add(NameOf(list, p, GraphSchema.PayloadOrder));
            row.Add(GraphLook.IconButton("close", "Take this away", () => Frame.Edit("Remove", () => list.Items.Remove(p)), "tg-on-hover"));
            var pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = p.Str("name"), Label = p.Str("name") });
            pin.Connected = Frame.NameIsRead(p.Str("name"));
            pin.SetType(type);
            row.Add(pin);
            Body.Add(row);
            Problems(id, row);
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

        TextField NameOf(JValue list, JValue p, IList<string> order)
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
            name.name = "name" + p.Wire("id");
            return name;
        }

        void Param(JValue list, JValue p, int depth, bool sample)
        {
            var id = p.Wire("id");
            var order = sample ? GraphSchema.SampleParamOrder : GraphSchema.ParamOrder;
            var mode = sample ? "read" : p.Str("mode") ?? "read";
            var filter = mode == "with" || mode == "without";
            var row = GraphLook.Row("tg-entry-row");
            row.name = "row" + id;
            row.style.paddingLeft = 6 + depth * 14;
            if (!sample)
            {
                var info = GraphSchema.ParamModeInfo(mode);
                Button word = null;
                word = GraphLook.Plain(() => Frame.OpenModes(word, list, p), "tg-word");
                word.text = info.Label;
                word.tooltip = (info.Help ?? "") + "\nClick to change how it's taken.";
                word.name = "mode" + id;
                row.Add(word);
            }
            foreach (var flag in new[] { "local", "verified", "previous" })
                if (p.Flag(flag))
                {
                    var info = GraphSchema.KeyInfo(flag);
                    var tag = GraphLook.Text(info.Label, "tg-tag");
                    tag.tooltip = info.Help ?? "";
                    row.Add(tag);
                }
            if (p.Flag("ownership")) row.Add(GraphLook.Text("ownership", "tg-port-label"));
            else if (mode != "query")
            {
                if (p.Flag("scene")) row.Add(GraphLook.Text("scene", "tg-port-label"));
                var type = p.Str("type");
                var chip = new GraphTypeChip(type, null, Frame.Board.Light, from => Frame.Board.PickType(new GraphTypeRequest
                {
                    Title = filter ? "Which component?" : "Type of " + p.Str("name"), Current = type, Generic = Doc.TypeParamsAt(Frame.Slot.Owner), NoBuilders = filter,
                    Place = new GraphTypePlace { Element = id, Find = type },
                }, from, picked => Frame.Edit("Set type", () => p.Set("type", JValue.Of(picked), order))));
                chip.name = "type" + id;
                row.Add(chip);
                if (p.Str("role") != null) row.Add(GraphLook.Text(p.Str("role"), "tg-tag"));
            }
            if (!filter) row.Add(NameOf(list, p, order));
            else row.Add(new VisualElement { style = { flexGrow = 1 } });
            var more = GraphLook.IconButton("more", "More for this", null, "tg-on-hover");
            more.clicked += () => Frame.OpenParamMenu(more, list, p, sample);
            row.Add(more);
            if (!filter && !string.IsNullOrEmpty(p.Str("name")) && depth == 0)
            {
                var pin = Pin(new GraphPort { Node = GraphFrame.EntryId, Input = false, Exec = false, Name = p.Str("name"), Label = p.Str("name") });
                pin.Connected = Frame.NameIsRead(p.Str("name"));
                pin.SetType(mode == "query" ? null : p.Str("type"));
                row.Add(pin);
            }
            Body.Add(row);
            if (p.Get("default") != null)
            {
                var value = GraphLook.Row("tg-entry-row");
                value.style.paddingLeft = 20 + depth * 14;
                value.Add(GraphLook.Text("when not given", "tg-port-label"));
                value.Add(GraphValues.Control(Window, new GraphSlot(id, "default"), p.Str("type"), true, Frame.Id, p.Str("name")));
                Body.Add(value);
            }
            Problems(id, row);
            if (mode != "query") return;
            var query = p.Arr("query");
            for (var i = 0; query != null && i < query.Items.Count; i++) Param(query, query.Items[i], depth + 1, false);
            Button add = null;
            add = GraphLook.Labelled("plus", "the query needs…", () => Frame.OpenNeeds(add, p.Arr("query"), false, p), "tg-add-row");
            add.style.paddingLeft = 8 + (depth + 1) * 14;
            Body.Add(add);
        }

        void Problems(int id, VisualElement row) => Frame.AddProblems(Body, id, row);
    }
}
