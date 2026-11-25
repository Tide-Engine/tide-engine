using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A frame: code. A titled box that holds a routine's nodes, placed from its corner (a node's x, y are in its
    // frame), with its entry first: what the code takes, each with a pin. It grows around its nodes, and dragging
    // its title moves all of it. A value that's more than a literal has a small frame of its own beside its card.
    //
    // What reads a parameter or a local isn't drawn as a node: its wire comes from the parameter's pin on the entry,
    // or from the pin of the node that declares the local. A number or a text one input reads is typed on that
    // input. A statement that's a call is one node. So the frame keeps, for its graph, which nodes are drawn
    // (Views), which are drawn with another (Absorbed) and which aren't drawn (Hidden).
    sealed class GraphFrame : GraphItem
    {
        public const int EntryId = -1, ResultId = -2;

        public readonly GraphSlot Slot;
        internal readonly Dictionary<int, JValue> ById = new Dictionary<int, JValue>();
        internal readonly Dictionary<int, List<(int node, WirePath path)>> Readers = new Dictionary<int, List<(int, WirePath)>>();
        internal readonly Dictionary<int, int> Absorbed = new Dictionary<int, int>(); // A call drawn with its statement: its id, to the statement's
        internal readonly HashSet<int> Hidden = new HashSet<int>();
        internal readonly Dictionary<string, GraphDocument.Read> Reads = new Dictionary<string, GraphDocument.Read>();
        internal HashSet<int> Reached = new HashSet<int>();
        internal readonly Dictionary<int, GraphNodeView> Views = new Dictionary<int, GraphNodeView>();
        internal readonly Dictionary<int, GraphNoteView> Notes = new Dictionary<int, GraphNoteView>();
        internal GraphEntryView Entry;
        internal GraphResultView Result;
        VisualElement notesLayer;
        readonly HashSet<string> readNames = new HashSet<string>();
        readonly HashSet<int> readLocals = new HashSet<int>();
        VisualElement head, nodes;
        FrameWires wires;
        Label hint;
        string headSignature;
        Vector2 origin;
        bool arrangeQueued;

        public GraphFrame(GraphBoard board, GraphSlot slot) : base(board, slot.Owner)
        {
            Slot = slot;
            AddToClassList("tg-frame");
            if (IsValue) AddToClassList("tg-frame--value");
        }

        public bool IsValue => !Slot.IsBody;
        public JValue Graph => Doc.GetGraph(Slot);
        public GraphElement Owner => Doc.Find(Slot.Owner);
        // What the code's parameters are of: the declaration or member (a property, for its accessors)
        public JValue Routine => IsValue ? null : Doc.RoutineOf(Slot);

        public override string Title
        {
            get
            {
                var e = Owner;
                if (e == null) return "";
                if (!IsValue) return Doc.TitleOf(e);
                var label = GraphSchema.KeyInfo(Slot.Key).Label;
                return $"{label}: {e.Value.Str("name") ?? Doc.TitleOf(e)}";
            }
        }

        static List<JValue> NodesOf(JValue graph) => graph?.Arr("nodes")?.Items ?? new List<JValue>();
        static bool IsLiteral(string k) => k == "int" || k == "float" || k == "string" || k == "bool" || k == "char";
        internal bool Declares(JValue n) => GraphSchema.DeclaresLocal(n);
        internal bool NameIsRead(string name) => readNames.Contains(name);
        internal bool LocalIsRead(int declares) => readLocals.Contains(declares);

        public override IEnumerable<int> Carries()
        {
            var e = Owner;
            if (e == null) yield break;
            if (!IsValue)
            {
                yield return Id;
                var r = Routine;
                foreach (var key in new[] { "params", "payload" })
                {
                    var list = r?.Arr(key);
                    if (list == null) continue;
                    foreach (var p in list.Items)
                    {
                        yield return p.Wire("id");
                        var query = p.Arr("query");
                        if (query != null) foreach (var q in query.Items) yield return q.Wire("id");
                    }
                }
            }
            foreach (var n in NodesOf(Graph)) yield return n.Wire("id");
        }

        // ---------------------------------------------------------------------------------------------------------
        // Which nodes are drawn

        internal void Classify()
        {
            ById.Clear();
            Readers.Clear();
            Absorbed.Clear();
            Hidden.Clear();
            Reads.Clear();
            readNames.Clear();
            readLocals.Clear();
            var g = Graph;
            Reached = new HashSet<int>();
            foreach (var r in GraphDocument.ReadsOf(Routine)) Reads[r.Name] = r;
            if (g == null) return;
            var list = NodesOf(g);
            foreach (var n in list) ById[n.Wire("id")] = n;
            void Reads_(int value, int by, WirePath path)
            {
                if (value == 0) return;
                if (!Readers.TryGetValue(value, out var readers)) Readers[value] = readers = new List<(int, WirePath)>();
                readers.Add((by, path));
            }
            foreach (var n in list)
                foreach (var p in GraphPorts.Of(n))
                    if (p.Input && !p.Exec && p.Path != null) Reads_(p.Path.Get(n), n.Wire("id"), p.Path);
            Reads_(g.Wire("root"), ResultId, new WirePath("root"));
            if (g.Arr("nodes") != null) Reached = GraphDocument.Reached(g);
            int Count(int id) => Readers.TryGetValue(id, out var r) ? r.Count : 0;
            foreach (var n in list)
            {
                if (n.Str("k") != "eval") continue;
                var v = n.Wire("value");
                if (v != 0 && ById.TryGetValue(v, out var call) && (call.Str("k") == "call" || call.Str("k") == "method") && Count(v) == 1 && !Absorbed.ContainsKey(v))
                    Absorbed[v] = n.Wire("id");
            }
            foreach (var n in list)
            {
                var id = n.Wire("id");
                var k = n.Str("k");
                var readers = Count(id);
                if (readers == 0 || !string.IsNullOrEmpty(n.Str("comment"))) continue;
                if (k == "name" && !n.Flag("local") && Reads.ContainsKey(n.Str("name") ?? ""))
                {
                    Hidden.Add(id);
                    readNames.Add(n.Str("name"));
                }
                else if (k == "local" && ById.TryGetValue(n.Wire("ref"), out var declares) && Declares(declares))
                {
                    Hidden.Add(id);
                    readLocals.Add(n.Wire("ref"));
                }
                else if (IsLiteral(k) && readers == 1 && Readers[id][0].node != ResultId) Hidden.Add(id);
            }
        }

        // The ids of the nodes drawn as nodes
        internal IEnumerable<int> Visible() => ById.Keys.Where(id => !Hidden.Contains(id) && !Absorbed.ContainsKey(id));

        public GraphNodeView ViewOf(int id)
        {
            if (id == EntryId) return Entry;
            if (id == ResultId) return Result;
            if (Views.TryGetValue(id, out var v)) return v;
            return Absorbed.TryGetValue(id, out var eval) && Views.TryGetValue(eval, out var ev) ? ev : null;
        }

        // The call drawn with a statement, if one is
        JValue CallOf(int eval)
        {
            foreach (var pair in Absorbed)
                if (pair.Value == eval) return ById[pair.Key];
            return null;
        }

        // The nodes that go with nodes: the calls drawn with them, and what's typed on or read into their inputs
        // and nothing else's
        internal HashSet<int> With(IEnumerable<int> ids)
        {
            var all = new HashSet<int>(ids.Where(id => id > 0));
            foreach (var pair in Absorbed) if (all.Contains(pair.Value)) all.Add(pair.Key);
            var grew = true;
            while (grew)
            {
                grew = false;
                foreach (var hidden in Hidden)
                {
                    if (all.Contains(hidden) || !Readers.TryGetValue(hidden, out var readers)) continue;
                    if (!readers.All(r => all.Contains(r.node))) continue;
                    all.Add(hidden);
                    grew = true;
                }
            }
            return all;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Size

        public override Vector2 Estimate()
        {
            Classify();
            var entry = IsValue ? Vector2.zero : new GraphEntryView(this).Estimate();
            var left = IsValue ? GraphLook.FramePad : GraphLook.FramePad + entry.x + GraphLook.EntryGap;
            float top = GraphLook.FrameHead + 6, right = left + 200, bottom = top + entry.y, minX = 0, minY = 0;
            foreach (var id in Visible())
            {
                var at = GraphDocument.PlaceOf(ById[id]);
                minX = Mathf.Min(minX, at.x);
                minY = Mathf.Min(minY, at.y);
            }
            foreach (var id in Visible())
            {
                var n = ById[id];
                var at = GraphDocument.PlaceOf(n) - new Vector2(minX, minY);
                var size = GraphNodeView.EstimateOf(n, CallOf(id));
                right = Mathf.Max(right, left + at.x + size.x);
                bottom = Mathf.Max(bottom, top + at.y + size.y);
            }
            if (IsValue) right += 160;
            return new Vector2(right + GraphLook.FramePad, bottom + GraphLook.FrameBottom);
        }

        // Where a node at (0, 0) is drawn, in the frame
        internal Vector2 Origin => origin;

        internal void MarkWires() => wires?.MarkDirtyRepaint();

        internal void QueueArrange()
        {
            if (arrangeQueued) return;
            arrangeQueued = true;
            schedule.Execute(() =>
            {
                arrangeQueued = false;
                Arrange();
            });
        }

        // Puts the entry and the nodes where they go, and sizes the frame around them
        internal void Arrange(bool place = true)
        {
            if (Level != GraphLevel.Near || nodes == null) return;
            var entry = Entry != null ? Entry.SizeNow : Vector2.zero;
            var left = IsValue ? GraphLook.FramePad : GraphLook.FramePad + entry.x + GraphLook.EntryGap;
            var top = GraphLook.FrameHead + 6;
            if (place)
            {
                float minX = 0, minY = 0;
                foreach (var v in Views.Values)
                {
                    minX = Mathf.Min(minX, v.At.x);
                    minY = Mathf.Min(minY, v.At.y);
                }
                origin = new Vector2(left - minX, top - minY);
                Entry?.MoveTo(new Vector2(GraphLook.FramePad, top));
                foreach (var v in Views.Values) v.MoveTo(origin + v.At);
                foreach (var note in Notes.Values) note.MoveTo(origin + note.At);
            }
            float right = left, bottom = top + entry.y;
            foreach (var v in Views.Values)
            {
                var size = v.SizeNow;
                right = Mathf.Max(right, v.Shown.x + size.x);
                bottom = Mathf.Max(bottom, v.Shown.y + size.y);
            }
            foreach (var note in Notes.Values)
            {
                right = Mathf.Max(right, note.Shown.x + note.Extent.x);
                bottom = Mathf.Max(bottom, note.Shown.y + note.Extent.y);
            }
            if (Result != null)
            {
                Result.MoveTo(new Vector2(right + (Views.Count > 0 ? 50 : 0), top));
                right = Result.Shown.x + Result.SizeNow.x;
                bottom = Mathf.Max(bottom, top + Result.SizeNow.y);
            }
            if (hint != null)
            {
                hint.style.left = left;
                hint.style.top = top + 4;
                if (hint.style.display == DisplayStyle.Flex) right = Mathf.Max(right, left + 300);
            }
            var headWidth = head != null && !float.IsNaN(head.layout.width) ? 0 : 0;
            var size2 = new Vector2(Mathf.Max(right + GraphLook.FramePad, 260 + headWidth), bottom + GraphLook.FrameBottom);
            wires?.MarkDirtyRepaint();
            if (Measured && (size2 - Size).sqrMagnitude < 0.25f) return;
            Size = size2;
            Measured = true;
            style.width = Size.x;
            style.height = Size.y;
            Board.Resized(this);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Building

        protected override void Cleared()
        {
            Views.Clear();
            Notes.Clear();
            Entry = null;
            Result = null;
            head = nodes = notesLayer = null;
            wires = null;
            hint = null;
            headSignature = null;
        }

        protected override void BuildFar()
        {
            Cleared();
            Classify();
            var far = new FrameSilhouette(this);
            Add(far);
            base.BuildFar();
        }

        protected override void BuildNear()
        {
            Cleared();
            style.width = Size.x;
            style.height = Size.y;
            wires = new FrameWires(this);
            notesLayer = new VisualElement { pickingMode = PickingMode.Ignore };
            notesLayer.AddToClassList("tg-fill");
            Add(notesLayer);
            Add(wires);
            head = GraphLook.Row("tg-frame-head");
            Add(head);
            nodes = new VisualElement { pickingMode = PickingMode.Ignore };
            nodes.AddToClassList("tg-fill");
            Add(nodes);
            hint = GraphLook.Text("", "tg-frame-hint");
            hint.pickingMode = PickingMode.Ignore;
            Add(hint);
            Reconcile();
        }

        void Watch(GraphNodeView v) => v.RegisterCallback<GeometryChangedEvent>(_ => QueueArrange());

        // Built again as it changed: the nodes whose own JSON (or what's typed on them) differs, and no others
        public void Reconcile()
        {
            if (Level != GraphLevel.Near || nodes == null)
            {
                if (Level == GraphLevel.Far) Rebuild();
                return;
            }
            if (Owner == null) return;
            Classify();
            var g = Graph;
            var signature = IsValue ? Title : GraphEntryView.SignatureOf(this);
            if (signature != headSignature)
            {
                headSignature = signature;
                BuildHead();
                if (!IsValue)
                {
                    if (Entry == null)
                    {
                        Entry = new GraphEntryView(this);
                        Watch(Entry);
                        nodes.Add(Entry);
                    }
                    Entry.Build();
                }
            }
            if (IsValue)
            {
                if (Result == null)
                {
                    Result = new GraphResultView(this);
                    Watch(Result);
                    nodes.Add(Result);
                }
                Result.Build();
            }
            var visible = new HashSet<int>(Visible());
            foreach (var gone in Views.Keys.Where(id => !visible.Contains(id)).ToList())
            {
                Views[gone].RemoveFromHierarchy();
                Views.Remove(gone);
            }
            foreach (var id in visible)
            {
                var n = ById[id];
                var call = CallOf(id);
                var sig = GraphNodeView.SignatureOf(this, n, call);
                if (!Views.TryGetValue(id, out var view))
                {
                    view = new GraphNodeView(this, id, n, call);
                    Watch(view);
                    Views[id] = view;
                    nodes.Add(view);
                }
                else if (view.Signature == sig && view.Node == n && view.Call == call) continue;
                view.Node = n;
                view.Call = call;
                view.Signature = sig;
                view.Build();
                view.ShowProblems();
            }
            notesLayer.Clear();
            Notes.Clear();
            foreach (var note in g?.Arr("notes")?.Items ?? new List<JValue>())
            {
                var view = new GraphNoteView(this, note);
                Notes[view.Id] = view;
                notesLayer.Add(view);
            }
            hint.text = g == null ? "Its code is written in C." : IsValue ? "Right-click to add what works the value out, and wire it into The value." : "Drag from the arrow to add what happens";
            hint.style.display = Views.Count == 0 ? DisplayStyle.Flex : DisplayStyle.None;
            Connections();
            Arrange();
            ApplyTypes();
        }

        // Built again whatever changed: a field that refused what was typed shows what it had
        public void Refresh()
        {
            foreach (var v in Views.Values) v.Signature = null;
            headSignature = null;
            Reconcile();
        }

        internal new void Edit(string what, Action change) => Window.Edit(what, change, Id);

        void BuildHead()
        {
            head.Clear();
            var e = Owner;
            var v = e.Value;
            if (IsValue)
            {
                head.Add(GraphLook.Text(Title, "tg-frame-title"));
                head.Add(new VisualElement { style = { flexGrow = 1 } });
                head.Add(GraphLook.IconButton("close", "Close this value's graph", () => Board.CloseValue(Slot)));
                return;
            }
            var kind = v.Str("kind");
            var member = e.Role == GraphRole.Member;
            if (e.Role == GraphRole.Accessor)
            {
                head.Add(GraphLook.Kind(GraphSchema.MemberTitle("property"), "Behaviour"));
                head.Add(GraphLook.Text(Title, "tg-frame-title"));
            }
            else
            {
                head.Add(GraphLook.Kind(member ? GraphSchema.MemberTitle(kind) : GraphSchema.DeclTitle(kind), "Behaviour"));
                var owner = member ? e.Owner?.Str("name") : null;
                if (owner != null) head.Add(GraphLook.Text(owner + ".", "tg-frame-title", "tg-muted"));
                if (v.Str("name") != null && kind != "operator")
                {
                    var name = GraphLook.Name(v.Str("name"), Rename, "name", "tg-inline--title");
                    name.name = "name";
                    name.RegisterCallback<KeyDownEvent>(ev =>
                    {
                        if (ev.keyCode != KeyCode.Return && ev.keyCode != KeyCode.KeypadEnter) return;
                        // Just made, and named: what it needs is next
                        if (Board.Fresh != Id) return;
                        ev.StopPropagation();
                        var typed = GraphLook.Identifier(name.text);
                        if (typed.Length > 0 && typed != v.Str("name")) Rename(typed);
                        Board.Fresh = 0;
                        schedule.Execute(() =>
                        {
                            var needs = Entry?.Q<Button>("needs");
                            if (needs != null) OpenNeeds(needs, Routine?.Arr("params"), false);
                        });
                    }, TrickleDown.TrickleDown);
                    head.Add(name);
                }
                else if (kind == "operator")
                {
                    Button op = null;
                    op = GraphLook.Plain(() =>
                    {
                        var list = new GraphOptionList("Which operator?", null, null, 200);
                        list.Set(GraphSchema.OperatorOps.Select(o => new GraphOption { Title = o, Checked = o == v.Str("op"), Pick = () => Edit("Operator", () => v.Set("op", JValue.Of(o), GraphSchema.MemberOrder(kind))) }));
                        Window.Open(list, op);
                    }, "tg-word");
                    op.text = "operator " + v.Str("op");
                    head.Add(op);
                }
                else head.Add(GraphLook.Text(member ? GraphSchema.MemberTitle(kind) : "", "tg-frame-title"));
            }
            var tags = Tags(e);
            tags.style.paddingBottom = 0;
            head.Add(tags);
            var note = e.Value.Str("comment");
            if (!string.IsNullOrEmpty(note))
            {
                var said = GraphLook.Plain(() => AskNote(e), "tg-note");
                said.text = GraphLook.Brief(note, 90);
                said.tooltip = note + "\n\n(Click to change this note.)";
                said.style.paddingBottom = 0;
                said.style.whiteSpace = WhiteSpace.NoWrap;
                head.Add(said);
            }
            head.Add(new VisualElement { style = { flexGrow = 1 } });
            // What the compiler says of the routine itself, in its title
            AddProblems(head, Id);
            if (Graph != null) head.Add(GraphLook.IconButton("plus", "Add a node here (Space, or right-click in the frame)", () => Board.OpenSearch(this, origin + new Vector2(40, 120), null), "tg-on-hover"));
            var more = GraphLook.IconButton("more", "More for this", null);
            more.clicked += () => OpenMenu(more);
            more.name = "more";
            head.Add(more);
        }

        void Rename(string name)
        {
            var e = Owner;
            if (e.List != null && e.List.Items.Any(o => o != e.Value && o.IsObject && o.Str("name") == name && o.Str("kind") == e.Value.Str("kind") && e.Role == GraphRole.Decl && !GraphSchema.HasParams(o.Str("kind"), false)))
            {
                Window.Say($"There's a '{name}' already.", true);
                Refresh();
                return;
            }
            Edit("Rename", () => e.Value.Set("name", JValue.Of(name), GraphDocument.OrderOf(e)));
        }

        internal List<GraphOption> MenuOptions()
        {
            var e = Owner;
            var options = new List<GraphOption>();
            if (Graph != null)
            {
                options.Add(new GraphOption { Title = "Add a node…", Detail = "Space", Pick = () => Board.OpenSearch(this, origin + new Vector2(40, 120), null) });
                options.Add(new GraphOption { Title = "Tidy up its nodes", Detail = "Lays them out again", Pick = () => Board.Tidy(this) });
                options.Add(new GraphOption { Separator = true });
            }
            CommonOptions(e, options);
            return options;
        }

        void OpenMenu(VisualElement from)
        {
            var list = new GraphOptionList(null, null, null, 300);
            list.Set(MenuOptions());
            Window.Open(list, from);
        }

        public override void ShowProblems()
        {
            base.ShowProblems();
            if (Level != GraphLevel.Near) return;
            foreach (var v in Views.Values) v.ShowProblems();
        }

        // ---------------------------------------------------------------------------------------------------------
        // Wires and types

        // Which pins have a wire
        void Connections()
        {
            var g = Graph;
            if (g == null) return;
            foreach (var view in Views.Values)
            {
                foreach (var p in view.Ports)
                {
                    var port = p.Port;
                    if (port.IsRead) continue;
                    var n = ById.TryGetValue(port.Node, out var found) ? found : null;
                    if (n == null) continue;
                    if (port.Path != null)
                    {
                        var from = port.Path.Get(n);
                        p.Connected = from != 0 && !(Hidden.Contains(from) && ById.TryGetValue(from, out var s) && IsLiteral(s.Str("k")));
                    }
                    else if (port.Exec) p.Connected = g.Wire("entry") == port.Node || GraphDocument.ExecSourceOf(g, port.Node).node != 0;
                    else p.Connected = Readers.TryGetValue(port.Node, out var readers) && readers.Count > 0;
                    p.MarkDirtyRepaint();
                }
            }
        }

        // Each value's type, as the language server checked it: its pin's colour and its wires', and what a pin says
        internal void ApplyTypes()
        {
            if (Level != GraphLevel.Near) return;
            var types = Window.Checked?.Types;
            string TypeOf(int id)
            {
                if (id == 0) return null;
                if (types != null && types.TryGetValue(id, out var t)) return t;
                if (!ById.TryGetValue(id, out var n)) return null;
                if (n.Str("k") == "eval" && types != null && types.TryGetValue(n.Wire("value"), out t)) return t;
                if (n.Str("k") == "name" && Reads.TryGetValue(n.Str("name") ?? "", out var read)) return read.Type;
                if (n.Str("k") == "local" && ById.TryGetValue(n.Wire("ref"), out var d)) return d.Str("type");
                return null;
            }
            foreach (var view in Views.Values)
            {
                view.SetType(TypeOf(view.Call != null ? view.Call.Wire("id") : view.Id));
                foreach (var pin in view.Ports)
                {
                    var p = pin.Port;
                    if (p.Exec) continue;
                    if (p.Local)
                    {
                        if (ById.TryGetValue(p.Node, out var d) && d.Str("type") == null)
                            pin.SetType(ById.Values.Where(n => n.Str("k") == "local" && n.Wire("ref") == p.Node).Select(n => TypeOf(n.Wire("id"))).FirstOrDefault(t => t != null));
                        continue;
                    }
                    var owner = ById.TryGetValue(p.Node, out var o) ? o : null;
                    if (p.Input) pin.SetType(owner != null && p.Path != null ? TypeOf(p.Path.Get(owner)) : null);
                    else pin.SetType(TypeOf(p.Node == view.Id && view.Call != null ? view.Call.Wire("id") : p.Node));
                }
                view.ForgetHover();
            }
            if (Result != null && Graph != null) Result.Ports[0].SetType(TypeOf(Graph.Wire("root")));
            wires?.MarkDirtyRepaint();
        }

        internal string TypeAt(int id)
        {
            var types = Window.Checked?.Types;
            if (types != null && types.TryGetValue(id, out var t)) return t;
            if (!ById.TryGetValue(id, out var n)) return null;
            if (n.Str("k") == "name" && Reads.TryGetValue(n.Str("name") ?? "", out var read)) return read.Type;
            return n.Str("k") == "eval" && types != null && types.TryGetValue(n.Wire("value"), out t) ? t : null;
        }

        // Whether what a node reads (a parameter, a local) is too far from it for a wire: the input names it
        // instead. A wire across a big frame says less than the name.
        internal bool IsFar(JValue reader, JValue read)
        {
            var at = GraphDocument.PlaceOf(Absorbed.TryGetValue(reader.Wire("id"), out var eval) && ById.TryGetValue(eval, out var statement) ? statement : reader);
            if (read.Str("k") == "local")
            {
                if (!ById.TryGetValue(read.Wire("ref"), out var declares)) return false;
                // ...or declared after it on the board (a loop's own variable in its condition): a wire going
                // backwards says less than the name
                var from = GraphDocument.PlaceOf(declares);
                return from.x > at.x + 40 || Mathf.Abs(at.x - from.x) > 1100 || Mathf.Abs(at.y - from.y) > 500;
            }
            return at.x > 760 || at.y > 520;
        }

        // What a read is called: the parameter's name, the local's
        internal string ReadName(JValue read) => read.Str("k") == "local" ? (ById.TryGetValue(read.Wire("ref"), out var d) ? GraphSchema.LocalName(d) : "?") : read.Str("name");

        // The pin a value comes out of: a node's own, a parameter's on the entry, a local's on what declares it
        internal PortView SourcePin(int from)
        {
            if (from == 0) return null;
            if (Hidden.Contains(from) && ById.TryGetValue(from, out var hidden))
            {
                var reader = Readers.TryGetValue(from, out var readers) && readers.Count > 0 && ById.TryGetValue(readers[0].node, out var r) ? r : null;
                if (reader != null && IsFar(reader, hidden)) return null;
                var k = hidden.Str("k");
                if (k == "name") return Entry?.ReadPin(hidden.Str("name"));
                if (k == "local") return ViewOf(hidden.Wire("ref"))?.LocalPin(hidden.Wire("ref"));
                return null;
            }
            return ViewOf(from)?.DataOut;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Wiring

        // A hidden node (a literal, a read) nothing reads any more goes with the wire that read it
        void DropUnread(JValue g, int was, int now = 0)
        {
            if (was == 0 || was == now || !Hidden.Contains(was) || GraphDocument.Readers(g, was) > 0) return;
            Doc.DeleteNodes(g, new[] { was });
        }

        // Wires two pins, whichever way round they were dragged: false, with why, when they can't be
        internal bool Join(GraphPort a, GraphPort b, out string why)
        {
            why = null;
            var g = Graph;
            if (g == null) return false;
            if (a.Input == b.Input || a.Exec != b.Exec)
            {
                why = a.Exec != b.Exec ? "The arrows are the order things happen in, and join arrows. A value joins an input for a value." : a.Input ? "Both of these take a wire: one end must give one." : "Both of these give a wire: one end must take one.";
                return false;
            }
            var output = a.Input ? b : a;
            var input = a.Input ? a : b;
            if (output.Exec)
            {
                if (output.Node == EntryId)
                {
                    if (GraphDocument.ExecSourceOf(g, input.Node).node != 0)
                    {
                        why = "That step already runs after another: a step runs from one place only. Copy it for the other.";
                        return false;
                    }
                    g.Set("entry", JValue.Of(input.Node), GraphSchema.BodyOrder);
                    return true;
                }
                return GraphDocument.Connect(g, output.Node, output.Path, input.Node, out why);
            }
            var reader = input.Node == ResultId ? null : GraphDocument.NodeIn(g, input.Node);
            var was = input.Node == ResultId ? g.Wire("root") : reader != null ? input.Path.Get(reader) : 0;
            int from;
            JValue made = null;
            if (output.IsRead)
            {
                // Reading a parameter or a local is a node of its own in the file, which the board doesn't draw
                var at = reader != null ? GraphDocument.PlaceOf(reader) + new Vector2(-180, 50) : Vector2.zero;
                made = Doc.AddNode(g, output.Local ? "local" : "name", at.x, at.y);
                if (output.Local) made.Set("ref", JValue.Of(output.Node), GraphSchema.NodeOrder("local"));
                else made.Set("name", JValue.Of(output.Name), GraphSchema.NodeOrder("name"));
                from = made.Wire("id");
            }
            else from = output.Node;
            if (input.Node == ResultId) g.Set("root", JValue.Of(from), GraphSchema.ValueOrder);
            else if (!GraphDocument.Connect(g, input.Node, input.Path, from, out why))
            {
                if (made != null) GraphDocument.Nodes(g).Items.Remove(made);
                return false;
            }
            DropUnread(g, was, from);
            return true;
        }

        // Takes the wire off an input (and what was typed on it), or everything off an output
        internal void Unwire(GraphPort p)
        {
            var g = Graph;
            if (g == null) return;
            if (p.Path != null && p.Node == EntryId) g.Set("entry", JValue.Null());
            else if (p.Path != null && p.Node == ResultId)
            {
                var was = g.Wire("root");
                g.Set("root", JValue.Null());
                DropUnread(g, was);
            }
            else if (p.Path != null)
            {
                var n = GraphDocument.NodeIn(g, p.Node);
                if (n == null) return;
                var was = p.Exec ? 0 : p.Path.Get(n);
                p.Path.Set(n, 0);
                DropUnread(g, was);
            }
            else if (p.Input && p.Exec)
            {
                if (g.Wire("entry") == p.Node) g.Set("entry", JValue.Null());
                var (node, path) = GraphDocument.ExecSourceOf(g, p.Node);
                if (node != 0) path.Set(GraphDocument.NodeIn(g, node), 0);
            }
            else if (p.IsRead)
            {
                // Every read of the parameter or the local goes
                var reads = ById.Values.Where(n => Hidden.Contains(n.Wire("id")) && (p.Local ? n.Str("k") == "local" && n.Wire("ref") == p.Node : n.Str("k") == "name" && n.Str("name") == p.Name)).Select(n => n.Wire("id")).ToList();
                if (reads.Count > 0) Doc.DeleteNodes(g, reads);
            }
            else
            {
                foreach (var n in GraphDocument.Nodes(g).Items)
                    foreach (var q in GraphPorts.Of(n))
                        if (q.Path != null && !q.Exec && q.Path.Get(n) == p.Node) q.Path.Set(n, 0);
                if (g.Wire("root") == p.Node) g.Set("root", JValue.Null());
            }
        }

        // What's typed on an input: a number, a text in quotes, true or false makes that value; the name of
        // something the code takes (or a local) wires it; anything else opens the search with it
        internal void TypeInto(JValue owner, WirePath path, string typed, VisualElement from)
        {
            var g = Graph;
            typed = (typed ?? "").Trim();
            var was = path.Get(owner);
            var source = was != 0 && Hidden.Contains(was) && ById.TryGetValue(was, out var s) ? s : null;
            var literal = source != null && IsLiteral(source.Str("k")) ? source : null;
            if (typed.Length == 0)
            {
                if (literal != null)
                    Edit("Clear value", () =>
                    {
                        path.Set(owner, 0);
                        DropUnread(g, was);
                    });
                else Refresh();
                return;
            }
            var port = new GraphPort { Node = owner.Wire("id"), Input = true, Path = path };
            if (GraphValues.TryLiteral(typed, null, out var k, out var text))
            {
                Edit("Set value", () =>
                {
                    if (literal != null)
                    {
                        GraphValues.SetLiteral(literal, k, text);
                        return;
                    }
                    var at = GraphDocument.PlaceOf(owner) + new Vector2(-160, 60);
                    var n = Doc.AddNode(g, k, at.x, at.y);
                    GraphValues.SetLiteral(n, k, text);
                    if (!GraphDocument.Connect(g, owner.Wire("id"), path, n.Wire("id"), out _)) GraphDocument.Nodes(g).Items.Remove(n);
                    else DropUnread(g, was, n.Wire("id"));
                });
                return;
            }
            var local = ById.Values.FirstOrDefault(n => Declares(n) && GraphSchema.LocalName(n) == typed);
            if (Reads.ContainsKey(typed) || local != null)
            {
                var read = Reads.ContainsKey(typed) ? new GraphPort { Node = EntryId, Name = typed } : new GraphPort { Node = local.Wire("id"), Local = true };
                string why = null;
                Edit("Wire " + typed, () => Join(read, port, out why));
                if (why != null) Window.Say(why, true);
                return;
            }
            if (literal != null && literal.Str("k") == "string")
            {
                Edit("Set value", () => GraphValues.SetLiteral(literal, "string", typed));
                return;
            }
            // Not a value in itself: found, not typed
            Refresh();
            var view = ViewOf(owner.Wire("id"));
            var at2 = view != null ? view.Shown + new Vector2(-40, view.SizeNow.y + 10) : origin;
            Board.OpenSearch(this, at2, port, typed);
        }

        // Takes one of a node's inputs that come and go away: an argument, an item, a case
        internal void RemoveInput(JValue owner, GraphPort p)
        {
            var g = Graph;
            var k = owner.Str("k");
            var spec = GraphSchema.Spec(k);
            if (spec == null) return;
            Edit("Take away", () =>
            {
                var wired = new List<int>();
                var key = p.Path.Key;
                var at = p.Path.Parts.Length > 1 && p.Path.Parts[1] is int index ? index : -1;
                if (spec.Map == key)
                {
                    var map = owner.Get(key);
                    var name = p.Path.Parts[1] as string;
                    wired.Add(JValue.AsId(map.Get(name)));
                    map.Remove(name);
                }
                else if (spec.Items.Any(i => i.Key == key))
                {
                    var list = owner.Arr(key);
                    if (list == null || at < 0 || at >= list.Items.Count) return;
                    foreach (var q in GraphPorts.Of(owner))
                        if (q.Path != null && !q.Exec && q.Path.Key == key && q.Path.Parts.Length > 1 && Equals(q.Path.Parts[1], at)) wired.Add(q.Path.Get(owner));
                    // A label of a case that has others goes alone
                    if (p.Path.Parts.Length == 4 && list.Items[at].Arr(p.Path.Parts[2] as string) is JValue labels && labels.Items.Count > 1)
                    {
                        wired.Clear();
                        wired.Add(JValue.AsId(labels.Items[(int)p.Path.Parts[3]]));
                        labels.Items.RemoveAt((int)p.Path.Parts[3]);
                    }
                    else list.Items.RemoveAt(at);
                }
                else
                {
                    var list = owner.Arr(key);
                    if (list == null || at < 0 || at >= list.Items.Count) return;
                    wired.Add(JValue.AsId(list.Items[at]));
                    list.Items.RemoveAt(at);
                    var names = spec.Lists.FirstOrDefault(l => l.Key == key)?.Names;
                    var named = names != null ? owner.Arr(names) : null;
                    if (named != null && at < named.Items.Count) named.Items.RemoveAt(at);
                    if (named != null && named.Items.All(x => string.IsNullOrEmpty(x.String))) owner.Remove(names);
                }
                foreach (var w in wired) DropUnread(g, w);
            });
        }

        internal void PickOperator(GraphNodeView view, JValue n, VisualElement from)
        {
            var k = n.Str("k");
            var list = new GraphOptionList(GraphSchema.Spec(k)?.Title, null, "Search", 220);
            list.Set(GraphSchema.Ops(k).Select(o => new GraphOption
            {
                Title = o.title ?? o.op, Detail = o.title != null ? o.op : null, Keywords = o.op, Checked = o.op == n.Str("op"),
                Pick = () => Edit("Operator", () => n.Set("op", JValue.Of(o.op), GraphSchema.NodeOrder(k))),
            }));
            Window.Open(list, from);
        }

        internal void AskArgNames(GraphNodeView view, JValue call)
        {
            var args = call.Arr("args");
            var count = args?.Items.Count ?? 0;
            var names = Enumerable.Range(0, count).Select(i => call.Arr("argNames") != null && i < call.Arr("argNames").Items.Count ? call.Arr("argNames").Items[i].String ?? "" : "").ToList();
            Window.Open(new GraphPrompt("Names of its arguments", $"One for each of its {count}, separated by commas. Leave one empty for an argument with no name.", string.Join(", ", names), false, text =>
            {
                var typed = text.Split(',').Select(GraphLook.Identifier).ToList();
                while (typed.Count < count) typed.Add("");
                typed = typed.Take(count).ToList();
                Edit("Argument names", () =>
                {
                    if (typed.All(string.IsNullOrEmpty)) call.Remove("argNames");
                    else call.Set("argNames", JValue.Strings(typed), GraphSchema.NodeOrder(call.Str("k")));
                });
            }), view, true);
        }

        internal void OpenNodeMenu(GraphNodeView view, VisualElement from)
        {
            Board.SelectNode(this, view.Id, false);
            var list = new GraphOptionList(null, null, null, 300);
            list.Set(view.MenuOptions());
            Window.Open(list, from);
        }

        // ---------------------------------------------------------------------------------------------------------
        // What the code takes

        // A name for something of a type: wobble for a Wobble, value for a number
        public static string NameFor(string type)
        {
            if (string.IsNullOrEmpty(type)) return "value";
            var t = type.TrimEnd('?');
            var cut = t.IndexOfAny(new[] { '<', '[' });
            if (cut > 0) t = t.Substring(0, cut);
            var dot = t.LastIndexOf('.');
            if (dot >= 0) t = t.Substring(dot + 1);
            if (t.Length == 0 || char.IsLower(t[0])) return cut > 0 ? "values" : "value";
            return GraphLook.LowerFirst(t);
        }

        // Adds something the code takes, as the entry's last row does: its id
        internal int AddParam(JValue list, string type, string mode, bool sample = false, JValue query = null)
        {
            var e = Owner;
            var r = Routine;
            JValue made = null;
            Edit("Add " + (type ?? mode), () =>
            {
                var into = query != null ? query.Arr("query") : r.Arr("params");
                if (into == null)
                {
                    into = JValue.NewArray();
                    if (query != null) query.Set("query", into, GraphSchema.ParamOrder);
                    else r.Set("params", into, GraphDocument.OrderOf(Doc.Find(r.Wire("id"))));
                }
                made = JValue.NewObject();
                made.Set("id", JValue.Of(Doc.NewId()));
                if (!sample) made.Set("mode", JValue.Of(mode));
                made.Set("type", JValue.Of(type));
                if (mode != "with" && mode != "without")
                {
                    var taken = GraphDocument.ReadsOf(r).Select(x => x.Name).ToList();
                    var name = NameFor(type);
                    if (taken.Contains(name))
                        for (var i = 2; ; i++)
                            if (!taken.Contains(name + i))
                            {
                                name += i;
                                break;
                            }
                    made.Set("name", JValue.Of(name));
                }
                into.Items.Add(made);
            });
            return made?.Wire("id") ?? 0;
        }

        // Whether what the code takes are inputs of any type (a function's: its last row's words are a function's),
        // not things an entity or the game has (a system's "+ needs…")
        internal bool TakesInputs
        {
            get
            {
                var e = Owner;
                var r = Routine;
                if (e == null || r == null) return false;
                var words = GraphSchema.AddRow(r.Str("kind"), e.Role != GraphRole.Decl);
                return words == null || words == GraphSchema.AddRow("function", false);
            }
        }

        // What "+ needs…" offers: for code that runs for entities, the components and the things there's one of,
        // as the language server lists them at that place, and filters; for code that's called, any type
        internal GraphPopup OpenNeeds(VisualElement from, JValue list, bool sample, JValue query = null)
        {
            var e = Owner;
            var r = Routine;
            var kind = r.Str("kind");
            var member = e.Role != GraphRole.Decl;
            var place = new GraphTypePlace { Element = e.Id, After = (r.Str("name") ?? kind) + "(" };
            if (query == null && TakesInputs)
            {
                return Board.PickType(new GraphTypeRequest { Title = "An input: what type?", Place = place, Generic = Doc.TypeParamsAt(e.Id) }, from, type => AddParam(list, type, "read", sample));
            }
            var menu = new GraphOptionList(query != null ? "What does the query need?" : "What does it need?", "Each becomes a pin its code reads from.", "Search", 300);
            menu.Window = Window;
            string step = null; // "with" or "without": which component, next
            var found = new List<GraphTypeInfo>();
            void Fill()
            {
                var options = new List<GraphOption>();
                var light = Board.Light;
                var taken = new HashSet<string>((query != null ? query.Arr("query") : r.Arr("params"))?.Items.Select(p => p.Str("type")).Where(t => t != null) ?? Enumerable.Empty<string>());
                foreach (var t in found)
                {
                    var type = t;
                    var component = !t.OneOfAKind;
                    if (step != null && (!component || t.Declared == "scene")) continue;
                    if (taken.Contains(t.Name)) continue;
                    var mode = step ?? "read";
                    options.Add(new GraphOption
                    {
                        Title = t.Name, Detail = step != null ? null : t.BuiltIn ? null : GraphLook.FirstLine(t.Detail, 60), Help = t.Doc, Dot = TideGraphTypes.ColorOf(t.Name, light),
                        Section = step != null ? GraphLook.UpperFirst(GraphSchema.ParamModeInfo(step).Label) + "…" : t.Declared == "scene" ? "A scene it runs in" : component ? "A component each entity has" : "One of a kind", Data = t,
                        Priority = t.Declared == "scene" ? 0 : Doc.Elements.Any(x => x.Role == GraphRole.Decl && x.Value.Str("name") == t.Name) ? 3 : component ? 2 : 1, Pick = () => AddParam(list, type.Name, mode, false, query),
                    });
                }
                // This file's own first, then the other components, then what there's one of
                options = options.OrderByDescending(o => o.Priority).ToList();
                if (step == null)
                    foreach (var filter in new[] { "with", "without" })
                    {
                        var f = filter;
                        var info = GraphSchema.ParamModeInfo(filter);
                        options.Add(new GraphOption
                        {
                            Title = GraphLook.UpperFirst(info.Label) + "…", Detail = info.Help, Tall = true, Section = "Only some entities", KeepOpen = true,
                            Pick = () =>
                            {
                                step = f;
                                menu.Title = GraphLook.UpperFirst(info.Label) + " which component?";
                                Fill();
                                menu.Opened();
                            },
                        });
                    }
                menu.Set(options);
            }
            // Until the language server answers (and without it): what this file declares
            foreach (var d in Doc.Elements.Where(x => x.Role == GraphRole.Decl))
            {
                var k = d.Value.Str("kind");
                if ((k == "component" || k == "singleton") && !string.IsNullOrEmpty(d.Value.Str("name"))) found.Add(new GraphTypeInfo { Name = d.Value.Str("name"), Detail = k });
            }
            Fill();
            Window.Open(menu, from);
            menu.Status = "Asking what it can take…";
            TideGraphLanguage.Types(Window, place, (types, why) =>
            {
                if (menu.WasClosed) return;
                menu.Status = null;
                if (types == null || types.Count == 0) return;
                found.Clear();
                found.AddRange(types.Where(t => !t.BuiltIn && t.Params.Count == 0));
                Fill();
            });
            return menu;
        }

        // How something is taken: the schema's ways by their labels, the rare ones under "More", and of them those
        // the language server takes at that place
        internal GraphOptionList OpenModes(VisualElement from, JValue list, JValue p)
        {
            var e = Owner;
            var r = Routine;
            var current = p.Str("mode") ?? "read";
            var menu = new GraphOptionList("How is it taken?", null, null, 300);
            HashSet<string> valid = null;
            void Fill()
            {
                var options = new List<GraphOption>();
                foreach (var mode in GraphSchema.ParamModes)
                {
                    if (valid != null && mode != "read" && mode != current && !valid.Contains(mode)) continue;
                    var m = mode;
                    var info = GraphSchema.ParamModeInfo(mode);
                    options.Add(new GraphOption
                    {
                        Title = info.Label, Detail = info.Help, Tall = info.Help != null, Section = info.Advanced ? "More" : null, Checked = mode == current, Data = mode,
                        Pick = () => Edit("How it's taken", () => SetMode(r, p, m)),
                    });
                }
                menu.Set(options);
            }
            Fill();
            Window.Open(menu, from);
            var inQuery = !ReferenceEquals(list, r.Arr("params"));
            if (inQuery) return menu;
            TideGraphLanguage.Words(Window, new GraphTypePlace { Element = e.Id, After = (r.Str("name") ?? r.Str("kind")) + "(" }, words =>
            {
                if (menu.WasClosed || words == null || words.Count == 0) return;
                valid = words;
                Fill();
            });
            return menu;
        }

        static void SetMode(JValue routine, JValue p, string mode)
        {
            var o = GraphSchema.ParamOrder;
            var was = p.Str("mode");
            p.Set("mode", JValue.Of(mode), o);
            if (mode == "query" && p.Arr("query") == null)
            {
                p.Set("query", JValue.NewArray(), o);
                p.Remove("type");
                if (string.IsNullOrEmpty(p.Str("name"))) p.Set("name", JValue.Of("entities"), o);
            }
            if (mode != "query") p.Remove("query");
            if (mode != "query" && was == "query" && p.Str("type") == null) p.Set("type", JValue.Of("Transform"), o);
            if (mode == "with" || mode == "without") p.Remove("name");
            else
            {
                p.Remove("ownership");
                p.Remove("scene");
                if (string.IsNullOrEmpty(p.Str("name"))) p.Set("name", JValue.Of(NameFor(p.Str("type"))), o);
            }
        }

        internal void OpenParamMenu(VisualElement from, JValue list, JValue p, bool sample)
        {
            var o = sample ? GraphSchema.SampleParamOrder : GraphSchema.ParamOrder;
            var mode = p.Str("mode") ?? "read";
            var filter = mode == "with" || mode == "without";
            var options = new List<GraphOption>();
            if (!sample)
            {
                foreach (var flag in new[] { "local", "verified", "previous" })
                {
                    var f = flag;
                    var info = GraphSchema.KeyInfo(flag);
                    options.Add(new GraphOption { Title = info.Label, Detail = info.Help, Tall = info.Help != null, Checked = p.Flag(flag), Pick = () => Edit(info.Label, () => p.SetFlag(f, !p.Flag(f), o)) });
                }
                if (filter)
                    options.Add(new GraphOption
                    {
                        Title = "Owning it, not having a component", Detail = "Only (or never) the entities this machine's player owns", Tall = true, Checked = p.Flag("ownership"),
                        Pick = () => Edit("Ownership", () =>
                        {
                            var on = !p.Flag("ownership");
                            p.SetFlag("ownership", on, o);
                            if (on) p.Remove("type");
                            else if (p.Str("type") == null) p.Set("type", JValue.Of("Transform"), o);
                        }),
                    });
                else if (mode != "query")
                {
                    if (p.Get("default") == null)
                        options.Add(new GraphOption { Title = "A value for when it isn't given…", Detail = GraphSchema.KeyInfo("default").Label, Pick = () => Edit("Default", () => p.Set("default", Doc.ValueGraph(GraphValues.IsFraction(p.Str("type")) ? "float" : p.Str("type") == "bool" ? "bool" : p.Str("type") == "string" ? "string" : "int", p.Str("type") == "bool" ? "false" : p.Str("type") == "string" ? "" : GraphValues.IsFraction(p.Str("type")) ? "0.0" : "0"), o)) });
                    else options.Add(new GraphOption { Title = "Always given: no value of its own", Pick = () => Edit("Default", () => p.Remove("default")) });
                }
                options.Add(new GraphOption { Separator = true });
            }
            var at = list.Items.IndexOf(p);
            options.Add(new GraphOption { Title = "Move up", Off = at <= 0, Pick = () => Edit("Move up", () => Swap(list, at, at - 1)) });
            options.Add(new GraphOption { Title = "Move down", Off = at >= list.Items.Count - 1, Pick = () => Edit("Move down", () => Swap(list, at, at + 1)) });
            options.Add(new GraphOption { Title = "Take it away", Pick = () => RemoveParam(list, p) });
            var menu = new GraphOptionList(null, null, null, 300);
            menu.Set(options);
            Window.Open(menu, from);
        }

        static void Swap(JValue list, int a, int b)
        {
            if (a < 0 || b < 0 || a >= list.Items.Count || b >= list.Items.Count) return;
            var item = list.Items[a];
            list.Items.RemoveAt(a);
            list.Items.Insert(b, item);
        }

        // Takes away something the code takes, and the reads of it with it
        internal void RemoveParam(JValue list, JValue p)
        {
            var g = Graph;
            var name = p.Str("name");
            Edit("Take away " + (name ?? p.Str("type")), () =>
            {
                list.Items.Remove(p);
                if (g == null || string.IsNullOrEmpty(name)) return;
                var reads = NodesOf(g).Where(n => n.Str("k") == "name" && n.Str("name") == name && !n.Flag("local") && Hidden.Contains(n.Wire("id"))).Select(n => n.Wire("id")).ToList();
                if (reads.Count > 0) Doc.DeleteNodes(g, reads);
            });
        }
    }

    // A frame's wires: from each value's pin to the inputs that read it, and from each step's arrow to the next.
    // Drawn as curves in the colour of the value's type, fainter where what they join is left out.
    sealed class FrameWires : VisualElement
    {
        readonly GraphFrame frame;

        public FrameWires(GraphFrame frame)
        {
            this.frame = frame;
            pickingMode = PickingMode.Ignore;
            AddToClassList("tg-fill");
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var g = frame.Graph;
            if (g == null) return;
            var p = ctx.painter2D;
            var board = frame.Board;
            var palette = GraphLook.Of(board.Light);
            IEnumerable<GraphNodeView> views = frame.Views.Values;
            if (frame.Entry != null) views = views.Append(frame.Entry);
            if (frame.Result != null) views = views.Append(frame.Result);
            foreach (var view in views)
            {
                var faint = view.ClassListContains("tg-node--unreached");
                foreach (var pin in view.Ports)
                {
                    var port = pin.Port;
                    if (port.Path == null) continue;
                    int wired;
                    if (port.Node == GraphFrame.EntryId) wired = g.Wire("entry");
                    else if (port.Node == GraphFrame.ResultId) wired = g.Wire("root");
                    else wired = frame.ById.TryGetValue(port.Node, out var n) ? port.Path.Get(n) : 0;
                    if (wired == 0) continue;
                    if (port.Input && !port.Exec)
                    {
                        var from = frame.SourcePin(wired);
                        if (from == null || from.View == view && !from.Port.IsRead) continue;
                        var c = TideGraphTypes.ColorOf(pin.Type ?? from.Type, board.Light);
                        c.a = faint ? 0.35f : 0.92f;
                        Curve(p, from.Anchor(this), pin.Anchor(this), c, 2);
                    }
                    else if (!port.Input && port.Exec)
                    {
                        var to = frame.ViewOf(wired)?.ExecIn;
                        if (to == null) continue;
                        var c = palette.Exec;
                        c.a = to.View.ClassListContains("tg-node--unreached") ? 0.35f : 0.95f;
                        Curve(p, pin.Anchor(this), to.Anchor(this), c, 2.5f);
                    }
                }
            }
            var dragging = board.WireFrom;
            if (dragging != null && dragging.Frame == frame)
            {
                var end = board.ChangeCoordinatesTo(this, board.WireEnd);
                var start = dragging.Anchor(this);
                if (dragging.Port.Input) Curve(p, end, start, palette.Drag, 2);
                else Curve(p, start, end, palette.Drag, 2);
            }
        }

        static void Curve(Painter2D p, Vector2 a, Vector2 b, Color c, float width)
        {
            var dx = Mathf.Max(30, Mathf.Abs(b.x - a.x) * 0.5f);
            p.strokeColor = c;
            p.lineWidth = width;
            p.lineCap = LineCap.Round;
            p.BeginPath();
            p.MoveTo(a);
            p.BezierCurveTo(a + new Vector2(dx, 0), b - new Vector2(dx, 0), b);
            p.Stroke();
        }
    }

    // A frame from far away: where its nodes are, as boxes
    sealed class FrameSilhouette : VisualElement
    {
        readonly GraphFrame frame;

        public FrameSilhouette(GraphFrame frame)
        {
            this.frame = frame;
            pickingMode = PickingMode.Ignore;
            AddToClassList("tg-fill");
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var palette = GraphLook.Of(frame.Board.Light);
            var entry = frame.IsValue ? Vector2.zero : new GraphEntryView(frame).Estimate();
            var left = frame.IsValue ? GraphLook.FramePad : GraphLook.FramePad + entry.x + GraphLook.EntryGap;
            var top = GraphLook.FrameHead + 6;
            p.fillColor = palette.FarNode;
            void Box(Rect r)
            {
                p.BeginPath();
                p.MoveTo(new Vector2(r.xMin, r.yMin));
                p.LineTo(new Vector2(r.xMax, r.yMin));
                p.LineTo(new Vector2(r.xMax, r.yMax));
                p.LineTo(new Vector2(r.xMin, r.yMax));
                p.ClosePath();
                p.Fill();
            }
            if (!frame.IsValue) Box(new Rect(GraphLook.FramePad, top, entry.x, entry.y));
            float minX = 0, minY = 0;
            var visible = frame.Visible().ToList();
            foreach (var id in visible)
            {
                var at = GraphDocument.PlaceOf(frame.ById[id]);
                minX = Mathf.Min(minX, at.x);
                minY = Mathf.Min(minY, at.y);
            }
            foreach (var id in visible)
            {
                var n = frame.ById[id];
                var at = GraphDocument.PlaceOf(n) - new Vector2(minX, minY);
                var size = GraphNodeView.EstimateOf(n, null);
                Box(new Rect(left + at.x, top + at.y, size.x, size.y));
            }
        }
    }
}
