using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A port's dot: a circle for a value, a triangle for a statement's flow; filled when wired
    sealed class PortView : VisualElement
    {
        public readonly GraphPort Port;
        public readonly GraphNodeView View;
        public bool Connected;
        public Color Tint = new Color(0.75f, 0.85f, 1f);
        public string Type; // The type of the value on it, as the language server checked it

        public PortView(GraphNodeView view, GraphPort port)
        {
            View = view;
            Port = port;
            style.width = 12;
            style.height = 12;
            style.flexShrink = 0;
            style.marginLeft = 2;
            style.marginRight = 2;
            if (port.Exec) Tint = Color.white;
            tooltip = port.Label;
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = contentRect;
            var c = r.center;
            p.strokeColor = Tint;
            p.fillColor = Tint;
            p.lineWidth = 1.5f;
            p.BeginPath();
            if (Port.Exec)
            {
                p.MoveTo(new Vector2(c.x - 4, c.y - 5));
                p.LineTo(new Vector2(c.x + 5, c.y));
                p.LineTo(new Vector2(c.x - 4, c.y + 5));
                p.ClosePath();
            }
            else
            {
                p.Arc(c, 4.5f, 0, 360);
            }
            if (Connected) p.Fill();
            p.Stroke();
        }

        // Where wires join it, in another element's space
        public Vector2 Anchor(VisualElement space) => this.ChangeCoordinatesTo(space, new Vector2(layout.width / 2, layout.height / 2));

        // The type of its value: its colour, and its tooltip says it
        public void SetType(string type)
        {
            if (Port.Exec || type == Type) return;
            Type = type;
            Tint = TideGraphTypes.ColorOf(type);
            tooltip = string.IsNullOrEmpty(type) ? Port.Label : string.IsNullOrEmpty(Port.Label) ? type : $"{Port.Label}: {type}";
            MarkDirtyRepaint();
        }
    }

    // The colours of values' types, Blueprint-like: a few kinds of their own, any other a hue of its name's
    static class TideGraphTypes
    {
        public static readonly Color Untyped = new Color(0.75f, 0.85f, 1f);

        public static Color ColorOf(string type)
        {
            if (string.IsNullOrEmpty(type)) return Untyped;
            var t = type.TrimEnd('?');
            var generic = t.IndexOf('<');
            var name = generic > 0 ? t.Substring(0, generic) : t;
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

    // A node on the canvas: its title in its category's colour, its ports and its own fields. An eval whose value
    // is a call (or method call) nothing else reads is drawn as one node, Blueprint style: the call's ports and
    // fields with the statement's flow.
    sealed class GraphNodeView : VisualElement
    {
        public const int EntryId = -1, ResultId = -2;

        public readonly GraphCanvas Canvas;
        public readonly int Id;          // The node's id; EntryId or ResultId for the graph's start and value
        public readonly JValue Node;     // Null for the start and value
        public readonly JValue Call;     // The call drawn with it, when it's merged
        public readonly List<PortView> Ports = new List<PortView>();
        readonly Label comment;
        readonly VisualElement header;
        Label titleLabel;
        bool selected, error, dim;
        string errorText, typeText, hoverText; // What its tooltip says: what's wrong with it, its value's type, tidels's hover
        bool hoverAsked;
        Color baseBorder = new Color(0.1f, 0.1f, 0.1f);

        public GraphNodeView(GraphCanvas canvas, JValue node, JValue call)
        {
            Canvas = canvas;
            Node = node;
            Call = call;
            Id = node.Wire("id");
            Build(out header, out comment);
            Place(new Vector2((float)(node.Get("x")?.Number ?? 0), (float)(node.Get("y")?.Number ?? 0)));
        }

        // The graph's start (a body's entry) or its value (an expression's root)
        public GraphNodeView(GraphCanvas canvas, int pseudo, Vector2 at)
        {
            Canvas = canvas;
            Id = pseudo;
            Build(out header, out comment);
            Place(at);
        }

        public Vector2 At => new Vector2(style.left.value.value, style.top.value.value);

        public void Place(Vector2 at)
        {
            style.left = at.x;
            style.top = at.y;
        }

        void Build(out VisualElement head, out Label note)
        {
            style.position = Position.Absolute;
            style.minWidth = 120;
            style.backgroundColor = new Color(0.16f, 0.16f, 0.17f, 0.96f);
            SetBorder(baseBorder, 1);
            style.borderTopLeftRadius = style.borderTopRightRadius = style.borderBottomLeftRadius = style.borderBottomRightRadius = 5;
            pickingMode = PickingMode.Position;

            note = new Label { style = { position = UnityEngine.UIElements.Position.Absolute, bottom = new Length(100, LengthUnit.Percent), left = 0, color = new Color(0.85f, 0.85f, 0.6f), whiteSpace = WhiteSpace.Normal, maxWidth = 360, fontSize = 11, unityFontStyleAndWeight = FontStyle.Italic } };
            Add(note);

            head = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center, paddingLeft = 2, paddingRight = 2, height = 22, borderTopLeftRadius = 4, borderTopRightRadius = 4 } };
            Add(head);
            var body = new VisualElement { style = { paddingLeft = 2, paddingRight = 4, paddingBottom = 4, paddingTop = 2 } };
            Add(body);

            var w = Canvas.Window;
            if (Id == EntryId || Id == ResultId)
            {
                head.style.backgroundColor = new Color(0.55f, 0.18f, 0.18f);
                var entry = Id == EntryId;
                if (!entry) head.Add(AddPort(new GraphPort { Node = ResultId, Input = true, Exec = false, Path = new WirePath("root"), Label = "value" }));
                head.Add(new Label(entry ? "Start" : "Value") { style = { flexGrow = 1, unityFontStyleAndWeight = FontStyle.Bold, color = Color.white, paddingLeft = 4, paddingRight = 4 } });
                if (entry) head.Add(AddPort(new GraphPort { Node = EntryId, Input = false, Exec = true, Path = new WirePath("entry"), Label = "first statement" }));
                var routine = w.Doc.RoutineOf(Canvas.Slot);
                if (entry && routine != null)
                {
                    var names = routine.Arr("params")?.Items.Select(p => p.Str("name")).Where(s => !string.IsNullOrEmpty(s)).ToList();
                    if (names != null && names.Count > 0)
                        body.Add(new Label(string.Join(", ", names)) { style = { color = new Color(0.7f, 0.7f, 0.7f), fontSize = 10, whiteSpace = WhiteSpace.Normal, maxWidth = 200 } });
                }
                return;
            }

            var k = Node.Str("k");
            var spec = GraphSchema.Spec(Call != null ? Call.Str("k") : k);
            head.style.backgroundColor = GraphSchema.ColorOf(spec?.Category ?? "text");
            var ports = GraphPorts.Of(Node);
            var callPorts = Call != null ? GraphPorts.Of(Call) : new List<GraphPort>();
            var execIn = ports.FirstOrDefault(p => p.Input && p.Exec);
            var dataOut = ports.FirstOrDefault(p => !p.Input && !p.Exec);
            if (execIn != null) head.Add(AddPort(execIn));
            titleLabel = new Label(Title()) { style = { flexGrow = 1, unityFontStyleAndWeight = FontStyle.Bold, color = Color.white, paddingLeft = 3, paddingRight = 3 } };
            head.Add(titleLabel);
            if (dataOut != null) head.Add(AddPort(dataOut));

            // The node's own fields
            var graph = Canvas.Graph;
            if (Call != null) foreach (var f in GraphNodeFields.Build(w, graph, Call, true)) body.Add(Compact(f));
            foreach (var f in GraphNodeFields.Build(w, graph, Node, true)) body.Add(Compact(f));
            // A member or method: what its object has, from the language server
            var named = Call ?? Node;
            var tk = named.Str("k");
            if (tk == "method" || (tk == "member" && !named.Flag("shorthand")))
            {
                var id = named.Wire("id");
                var pick = GraphFields.SmallButton("Pick member…", () => Canvas.OpenMemberSearch(id, Canvas.ToCanvas(At + new Vector2(0, 30))));
                pick.tooltip = "Its object's fields and methods, as tidels knows them";
                body.Add(pick);
            }
            RegisterCallback<PointerEnterEvent>(_ => AskHover());

            // Its inputs: the call's when merged, but not the statement's value, which is the call
            var inputs = new List<GraphPort>();
            inputs.AddRange(callPorts.Where(p => p.Input && !p.Exec));
            inputs.AddRange(ports.Where(p => p.Input && !p.Exec && !(Call != null && p.Path.Key == "value")));
            foreach (var p in inputs)
            {
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center, height = 20 } };
                row.Add(AddPort(p));
                var owner = p.Node == Id ? Node : Call;
                var ok = owner?.Str("k");
                if (p.Group == "args" && (ok == "call" || ok == "method"))
                {
                    var at = p.Index;
                    var target = owner;
                    var name = GraphFields.Text(null, GraphNodeFields.ArgName(owner, at), v => w.Edit("Argument name", () => GraphNodeFields.SetArgName(target, at, v.Trim()), GraphRedraw.Inspector));
                    name.textEdition.placeholder = $"arg {at + 1}";
                    name.tooltip = "The argument's name, for a named argument";
                    name.style.width = 70;
                    row.Add(name);
                }
                else row.Add(new Label(p.Label) { style = { fontSize = 11, color = new Color(0.8f, 0.8f, 0.8f) } });
                body.Add(row);
            }
            // Its flow out
            foreach (var p in ports.Where(p => !p.Input && p.Exec))
            {
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center, justifyContent = Justify.FlexEnd, height = 20 } };
                row.Add(new Label(p.Label) { style = { fontSize = 11, color = new Color(0.85f, 0.85f, 0.85f) } });
                row.Add(AddPort(p));
                body.Add(row);
            }
            note.text = Node.Str("comment") ?? "";
            note.style.display = string.IsNullOrEmpty(note.text) ? DisplayStyle.None : DisplayStyle.Flex;
            note.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.clickCount == 2)
                {
                    Canvas.EditComment(this);
                    e.StopPropagation();
                }
            });
        }

        static VisualElement Compact(VisualElement f)
        {
            f.style.maxWidth = 260;
            f.style.fontSize = 11;
            return f;
        }

        PortView AddPort(GraphPort p)
        {
            var v = new PortView(this, p);
            Ports.Add(v);
            v.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0) return;
                Canvas.BeginWire(v, e);
                e.StopPropagation();
            });
            return v;
        }

        // Its title again, after an edit of what it says
        public void RefreshTitle()
        {
            if (titleLabel != null) titleLabel.text = Title();
        }

        public string Title()
        {
            var n = Call ?? Node;
            var k = n.Str("k");
            switch (k)
            {
                case "call": return (n.Flag("local") ? "local " : "") + n.Str("name") + TypeArgs(n) + "()";
                case "method": return "." + n.Str("name") + TypeArgs(n) + "()";
                case "member": return "." + n.Str("member");
                case "name": return n.Str("name");
                case "local":
                {
                    var decl = GraphDocument.NodeIn(Canvas.Graph, n.Wire("ref"));
                    return decl != null ? GraphSchema.LocalName(decl) : "missing local";
                }
                case "binary": return n.Str("op");
                case "unary": return n.Str("op") + "x";
                case "step": return n.Flag("postfix") ? "x" + n.Str("op") : n.Str("op") + "x";
                case "var": return (n.Flag("mut") ? "mut " : "") + (n.Str("type") ?? "var") + " " + n.Str("name");
                case "assign": return "Assign " + n.Str("op");
                case "foreach": return "foreach " + n.Str("name");
                case "parallel": return "parallel " + n.Str("name");
                case "outVar": return "out " + (n.Str("type") ?? "var") + " " + n.Str("name");
                case "literal": return n.Str("type") + " { }";
                case "is": return "is " + n.Str("type");
                case "eval": return "Do";
            }
            return GraphSchema.Spec(k)?.Title ?? k ?? "?";
        }

        static string TypeArgs(JValue n)
        {
            var t = n.Arr("typeArgs");
            return t == null || t.Items.Count == 0 ? "" : "<" + string.Join(", ", GraphFields.Strings(t)) + ">";
        }

        void SetBorder(Color c, float width)
        {
            style.borderTopColor = style.borderBottomColor = style.borderLeftColor = style.borderRightColor = c;
            style.borderTopWidth = style.borderBottomWidth = style.borderLeftWidth = style.borderRightWidth = width;
        }

        void Restyle()
        {
            if (error) SetBorder(new Color(1f, 0.25f, 0.2f), 2);
            else if (selected) SetBorder(new Color(1f, 0.7f, 0.2f), 2);
            else SetBorder(baseBorder, 1);
            style.opacity = dim ? 0.45f : 1f;
        }

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                Restyle();
            }
        }

        public void SetDim(bool d)
        {
            dim = d;
            Restyle();
        }

        public void SetError(string message)
        {
            error = message != null;
            errorText = message;
            Retooltip();
            Restyle();
        }

        // Its value's type, as the language server checked it: a new check's, so what tidels said of it is asked again
        public void SetType(string type)
        {
            if (type == typeText) return;
            typeText = type;
            hoverText = null;
            hoverAsked = false;
            Retooltip();
        }

        public string TypeText => typeText;
        public string HoverText => hoverText;

        public void SetHover(string text)
        {
            hoverText = text;
            Retooltip();
        }

        void Retooltip()
        {
            var parts = new List<string>();
            if (!string.IsNullOrEmpty(errorText)) parts.Add(errorText);
            if (!string.IsNullOrEmpty(hoverText)) parts.Add(hoverText);
            else if (!string.IsNullOrEmpty(typeText)) parts.Add(typeText);
            tooltip = string.Join("\n\n", parts);
        }

        // What tidels says of it, asked the first time the pointer's on it after a check
        internal void AskHover()
        {
            if (Id <= 0 || hoverAsked) return;
            hoverAsked = true;
            TideGraphLanguage.Hover(Canvas.Window, (Call ?? Node).Wire("id"), text =>
            {
                if (panel != null) SetHover(text);
            });
        }

        // A new check: what tidels says of it is asked again
        public void ForgetHover()
        {
            hoverAsked = false;
            hoverText = null;
            Retooltip();
        }

        public PortView PortAt(int node, WirePath path, bool input, bool exec)
        {
            foreach (var p in Ports)
            {
                if (p.Port.Node != node || p.Port.Input != input || p.Port.Exec != exec) continue;
                if (path == null ? p.Port.Path == null : path.Equals(p.Port.Path)) return p;
            }
            return null;
        }

        public PortView ExecIn => Ports.FirstOrDefault(p => p.Port.Input && p.Port.Exec);
        public PortView DataOut => Ports.FirstOrDefault(p => !p.Port.Input && !p.Port.Exec && p.Port.Node == Id);
    }

    // A note box of a body: drawn behind the nodes, moved by its top, sized by its corner, its text edited in it
    sealed class NoteView : VisualElement
    {
        public readonly int Id;
        public readonly JValue Note;
        readonly GraphCanvas canvas;
        bool selected;

        public NoteView(GraphCanvas canvas, JValue note)
        {
            this.canvas = canvas;
            Note = note;
            Id = note.Wire("id");
            style.position = Position.Absolute;
            Place(new Rect((float)(note.Get("x")?.Number ?? 0), (float)(note.Get("y")?.Number ?? 0), (float)(note.Get("w")?.Number ?? 300), (float)(note.Get("h")?.Number ?? 120)));
            style.backgroundColor = new Color(0.35f, 0.33f, 0.18f, 0.45f);
            style.borderTopLeftRadius = style.borderTopRightRadius = style.borderBottomLeftRadius = style.borderBottomRightRadius = 4;
            var bar = new VisualElement { style = { height = 14, backgroundColor = new Color(0.5f, 0.47f, 0.25f, 0.8f) } };
            bar.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0) return;
                canvas.BeginNoteDrag(this, e, false);
                e.StopPropagation();
            });
            Add(bar);
            var text = GraphFields.Text(null, note.Str("text"), v => canvas.Window.Edit("Note", () => Note.Set("text", JValue.Of(v), GraphSchema.NoteOrder), GraphRedraw.Inspector), true);
            text.style.flexGrow = 1;
            text.style.opacity = 0.9f;
            Add(text);
            style.flexDirection = FlexDirection.Column;
            var corner = new VisualElement { style = { position = Position.Absolute, right = 0, bottom = 0, width = 12, height = 12, backgroundColor = new Color(0.6f, 0.56f, 0.3f, 0.9f) } };
            corner.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0) return;
                canvas.BeginNoteDrag(this, e, true);
                e.StopPropagation();
            });
            Add(corner);
        }

        public Rect Rect => new Rect(style.left.value.value, style.top.value.value, style.width.value.value, style.height.value.value);

        public void Place(Rect r)
        {
            style.left = r.x;
            style.top = r.y;
            style.width = Mathf.Max(60, r.width);
            style.height = Mathf.Max(40, r.height);
        }

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                var c = value ? new Color(1f, 0.7f, 0.2f) : new Color(0, 0, 0, 0);
                style.borderTopColor = style.borderBottomColor = style.borderLeftColor = style.borderRightColor = c;
                style.borderTopWidth = style.borderBottomWidth = style.borderLeftWidth = style.borderRightWidth = value ? 2 : 0;
            }
        }
    }
}
