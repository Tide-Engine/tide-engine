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
    // What never changes, read by one input, is shown in that input and is no node, as a Blueprint pin's default
    // (the schema's `inline`: a literal, a name in PascalCase or FULL_CASE, a path of members from one, a '-' over a
    // number, a vector of numbers), as tidec's layout gives it no room. A variable (a parameter, a local, a field of
    // one) is a Get node, wired to any number of inputs; what a Set changes, read by it alone, is in its title. A
    // statement that's a call is one node. So the frame keeps, for its graph, which nodes are drawn (Views), which
    // are drawn with another (Absorbed) and which are shown in an input or a title (Hidden).
    sealed class GraphFrame : GraphItem
    {
        public const int EntryId = -1, ResultId = -2;
        // `this`, as Blueprint says it
        internal const string SelfWord = "self";

        public readonly GraphSlot Slot;
        internal readonly Dictionary<int, JValue> ById = new Dictionary<int, JValue>();
        internal readonly Dictionary<int, List<(int node, WirePath path)>> Readers = new Dictionary<int, List<(int, WirePath)>>();
        internal readonly Dictionary<int, int> Absorbed = new Dictionary<int, int>(); // A call drawn with its statement: its id, to the statement's
        internal readonly HashSet<int> Hidden = new HashSet<int>();
        // A Set (or a step) whose title says what it changes: its id, to what it changes (Hidden too)
        internal readonly Dictionary<int, int> Titles = new Dictionary<int, int>();
        // Every node drawn as a node, whatever the schema's `inline` says: for a picture of a file as it was
        internal static bool DrawEveryNode;
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

        // Whether its code gives a value back: a getter, or a function with a result
        internal bool GivesBack => Owner?.Role == GraphRole.Accessor ? Owner.Value.Str("word") == "get" : !string.IsNullOrEmpty(Routine?.Str("returnType")) && Routine.Str("returnType") != "void";
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
            Titles.Clear();
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
                var k = n.Str("k");
                if (k == "name" && !n.Flag("local") && Reads.ContainsKey(n.Str("name") ?? "")) readNames.Add(n.Str("name"));
                else if (k == "local" && n.Wire("ref") != 0) readLocals.Add(n.Wire("ref"));
            }
            if (DrawEveryNode) return;
            // What a Set or a step changes, a name, a local or a member read by it alone, is in its title: Set score,
            // Set speed, Set scale (what it's a member of wired into its Target)
            var state = new Dictionary<int, bool>();
            foreach (var n in list)
            {
                var k = n.Str("k");
                if (!GraphSchema.Titled(k)) continue;
                var key = GraphSchema.Spec(k)?.Inputs.FirstOrDefault();
                var target = key != null ? n.Wire(key) : 0;
                if (target == 0 || Count(target) != 1 || !ById.TryGetValue(target, out var changed)) continue;
                var tk = changed.Str("k");
                if (tk != "name" && tk != "local" && tk != "member") continue;
                state[target] = true;
                Titles[n.Wire("id")] = target;
            }
            // Shown in the input that reads it, as tidec's layout folds it (graph_layout_fold): read by one input
            // of a node, and a literal; a name in PascalCase or FULL_CASE (a type, a namespace, a constant); a member
            // of what is itself shown there (or of nothing: a shorthand); a '-' over a number. A variable is a Get.
            bool Folds(JValue n, int depth)
            {
                var id = n.Wire("id");
                if (state.TryGetValue(id, out var known)) return known;
                state[id] = false;
                var how = Count(id) == 1 && Readers[id][0].node != ResultId && depth < 64 ? GraphSchema.Inline(n.Str("k")) : 0;
                if (how == 0) return false;
                var inputs = GraphPorts.DataWires(n).Where(w => w != 0).ToList();
                var of = inputs.Count == 1 && ById.TryGetValue(inputs[0], out var found) ? found : null;
                bool yes;
                if (how == GraphSchema.InlineAlways) yes = inputs.Count == 0;
                else if (how == GraphSchema.InlineNamed) yes = inputs.Count == 0 && GraphSchema.IsNamed(n.Str("name"));
                else if (how == GraphSchema.InlinePath) yes = inputs.Count == 0 || (of != null && GraphSchema.Inline(of.Str("k")) != GraphSchema.InlineNegative && Folds(of, depth + 1));
                else if (how == GraphSchema.InlineNegative) yes = n.Str("op") == "-" && of != null && (of.Str("k") == "int" || of.Str("k") == "float") && Folds(of, depth + 1);
                // float2(16, -14): a vector or a colour whose arguments are all numbers shown there
                else
                    yes = GraphSchema.InlineNumbersOf(n.Str("name")) && inputs.Count > 0 && inputs.All(w => ById.TryGetValue(w, out var arg)
                        && (arg.Str("k") == "int" || arg.Str("k") == "float" || GraphSchema.Inline(arg.Str("k")) == GraphSchema.InlineNegative) && Folds(arg, depth + 1));
                state[id] = yes;
                return yes;
            }
            foreach (var n in list)
                if (Folds(n, 0)) Hidden.Add(n.Wire("id"));
        }

        // What's shown in an input, when what's wired into it is: the node, else null
        internal JValue ShownIn(int from) => from != 0 && Hidden.Contains(from) && ById.TryGetValue(from, out var n) ? n : null;

        // What a value shown in an input is made of, from where it starts: wobble, then .phase
        internal List<JValue> PathOf(JValue leaf)
        {
            var path = new List<JValue>();
            for (var n = leaf; n != null && path.Count < 64;)
            {
                path.Insert(0, n);
                var how = GraphSchema.Inline(n.Str("k"));
                var inputs = GraphPorts.DataWires(n).Where(w => w != 0).ToList();
                n = inputs.Count == 1 && (how == GraphSchema.InlinePath || how == GraphSchema.InlineNegative) ? ShownIn(inputs[0]) : null;
            }
            return path;
        }

        // One of a path's parts, as it's written: a name, a number, .member
        internal string PartText(JValue n)
        {
            var k = n.Str("k");
            switch (k)
            {
                case "string": return "\"" + n.Str("text") + "\"";
                case "bool": return n.Flag("value") ? "true" : "false";
                case "name": return n.Str("name") ?? "";
                case "local": return ById.TryGetValue(n.Wire("ref"), out var d) ? GraphSchema.LocalName(d) : "?";
                case "member": return "." + n.Str("member");
                case "this": return SelfWord;
                case "unary": return n.Str("op") ?? "";
                case "call": return n.Str("name") + "(" + string.Join(", ", (n.Arr("args")?.Items ?? new List<JValue>()).Select(a => ShownIn(JValue.AsId(a)) is JValue arg ? TextOf(arg) : "")) + ")";
                case "int":
                case "float":
                case "char": return n.Str("text") ?? "";
                default: return k ?? "";
            }
        }

        // A value shown in an input, as it's written: wobble.phase, -2, "text", .Wild
        internal string TextOf(JValue leaf)
        {
            var path = PathOf(leaf);
            // A '-' is written before its number, a member after what it's of
            var sb = new System.Text.StringBuilder();
            foreach (var n in path.Where(n => n.Str("k") == "unary")) sb.Append(PartText(n));
            var first = path.FirstOrDefault(n => n.Str("k") != "unary");
            if (first != null && first.Str("k") == "member" && !first.Flag("shorthand")) sb.Append("…");
            foreach (var n in path.Where(n => n.Str("k") != "unary")) sb.Append(PartText(n));
            return sb.ToString();
        }

        // How many characters wide what's shown in an input is, as tidec's layout counts them (folded_text): a
        // path's names and dots, a box for each of a vector's numbers
        internal int WidthOf(JValue shown) => GraphSchema.Inline(shown.Str("k")) == GraphSchema.InlineNumbers ? 8 * GraphPorts.DataWires(shown).Count(w => w != 0) : TextOf(shown).Length;

        // A value shown in an input and what it's made of: their ids
        internal HashSet<int> ChainOf(int id)
        {
            var chain = new HashSet<int>();
            void Walk(int at)
            {
                if (!Hidden.Contains(at) || !chain.Add(at) || !ById.TryGetValue(at, out var n)) return;
                foreach (var w in GraphPorts.DataWires(n)) Walk(w);
            }
            Walk(id);
            return chain;
        }

        // The node drawn that a value shown in an input is shown on
        internal GraphNodeView ReaderOf(int id)
        {
            for (var i = 0; i < 64 && Hidden.Contains(id); i++)
            {
                if (!Readers.TryGetValue(id, out var readers) || readers.Count == 0) return null;
                id = readers[0].node;
            }
            return ViewOf(id);
        }

        // Whether a Set can change what a name or a local is: a parameter taken as "changes" (or "makes"), a local
        // made to be changed; a field of one of those too. What's only read can't be.
        internal bool Changeable(string name, int local)
        {
            if (local != 0) return ById.TryGetValue(local, out var d) && d.Flag("mut");
            if (name == null || !Reads.TryGetValue(name, out var read) || read.Source == null) return false;
            var mode = read.Source.Str("mode");
            return mode == "mut" || mode == "out";
        }

        internal bool Changeable(GraphPort read) => Changeable(read.Local ? null : read.Name, read.Local ? read.Node : 0);

        // What a Get reads at its start, through its fields and reroutes: a name (ball in ball.position), or the
        // node that declares a local; neither for what's worked out
        internal (string name, int local) RootOf(int id)
        {
            for (var i = 0; i < 64 && ById.TryGetValue(id, out var n); i++)
            {
                var k = n.Str("k");
                if (k == "name") return (n.Str("name"), 0);
                if (k == "local") return (null, n.Wire("ref"));
                if (k == "member" && !n.Flag("shorthand")) id = n.Wire("object");
                else if (k == "reroute") id = n.Wire("value");
                else break;
            }
            return (null, 0);
        }

        // What's wired into an input, and wiring it: a node's input, or a value frame's value (no owner)
        internal int WireAt(JValue owner, WirePath path) => owner != null ? path.Get(owner) : Graph?.Wire("root") ?? 0;

        // The type an input takes, where that's known: a call's parameter's (the language server's), what a Set
        // changes, a condition's, the result its code gives, a local's own; else what's in it now has
        internal string Wanted(JValue owner, WirePath path)
        {
            if (owner == null) return Owner?.Value.Str("type");
            var k = owner.Str("k");
            var key = path.Parts[path.Parts.Length - 1] as string ?? path.Key;
            var id = owner.Wire("id");
            if ((k == "call" || k == "method") && path.Key == "args" && path.Parts.Length == 2 && path.Parts[1] is int index)
            {
                var call = Window.Checked != null && Window.Checked.Calls.TryGetValue(id, out var c) ? c : null;
                var type = call?.At(index).type;
                if (type != null) return type;
            }
            else if (k == "assign" && key == "value" && path.Parts.Length == 1)
            {
                var target = TypeAt(owner.Wire("target"));
                if (target != null) return target;
            }
            // What a Set changes can be of any type: what's in it now says nothing of what's wanted
            else if (k == "assign" && key == "target" && path.Parts.Length == 1) return null;
            else if (key == "cond") return "bool";
            else if (k == "return" && key == "value")
            {
                var result = Routine?.Str("returnType");
                if (!string.IsNullOrEmpty(result) && result != "void") return result;
            }
            else if (GraphSchema.Spec(k)?.Declares != null && key == "value" && !string.IsNullOrEmpty(owner.Str("type"))) return owner.Str("type");
            else if (k == "binary" && path.Parts.Length == 1 && (key == "lhs" || key == "rhs"))
            {
                // Both sides of an "and" are true or false; of the rest, numbers and vectors mix, so nothing's said
                var op = owner.Str("op");
                if (op == "&&" || op == "||") return "bool";
            }
            var now = path.Get(owner);
            var typed = ShownIn(now)?.Str("k");
            if (typed == "int" || typed == "float" || typed == "unary") return null;
            return now != 0 ? TypeAt(now) : null;
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

        // As the page sizes one before it's drawn: the entry's rows, and the nodes by tidec's sizes of them
        public override Vector2 Estimate()
        {
            Classify();
            var pad = GraphLook.FramePad;
            var left = IsValue ? pad : pad + GraphLook.EntryWidth + GraphLook.EntryGap;
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            var first = true;
            foreach (var id in Visible())
            {
                var n = ById[id];
                var at = GraphDocument.PlaceOf(n);
                var size = GraphNodeView.EstimateOf(n, CallOf(id), this);
                if (first || at.x < x0) x0 = at.x;
                if (first || at.y < y0) y0 = at.y;
                if (first || at.x + size.x > x1) x1 = at.x + size.x;
                if (first || at.y + size.y > y1) y1 = at.y + size.y;
                first = false;
            }
            var ox = IsValue ? (first ? 0 : x0) : Mathf.Min(0, x0);
            var oy = IsValue ? (first ? 0 : y0) : Mathf.Min(0, y0);
            var entryH = IsValue ? 60 : 60 + GraphLook.CardRow * (GraphEntryView.RowsOf(this) + 1);
            var w = left + (x1 - ox) + (IsValue ? 60 + 160 : 0) + GraphLook.FrameBottom;
            var h = pad + Mathf.Max(y1 - oy, entryH) + GraphLook.FrameBottom;
            return new Vector2(Mathf.Max(IsValue ? GraphLook.ValueFrameWidth : left + GraphLook.FrameRoomRight, w),
                2 + GraphLook.FrameHead + Mathf.Max(IsValue ? GraphLook.ValueFrameHeight : GraphLook.FrameLeast, h));
        }

        // Where a node at (0, 0) is drawn, in the frame
        internal Vector2 Origin => origin;

        // Where Arrange last put it, by the file's places: while nodes are dragged, Origin moves from it
        Vector2 arranged;

        internal void MarkWires() => wires?.MarkDirtyRepaint();
        internal FrameWires Wires => wires;

        internal void QueueArrange()
        {
            if (arrangeQueued) return;
            arrangeQueued = true;
            schedule.Execute(() =>
            {
                arrangeQueued = false;
                // While its nodes are dragged they're where the drag has them, not where the file does yet
                Arrange(!Board.MovingNodesOf(this));
            });
        }

        // While nodes are dragged (`places`: where each dragged one is going, as the file has places): each is shown
        // there, and the frame grows toward them on every side. Left and up its own place on the board moves by as
        // much as it grows, so what isn't dragged stays where it's seen and the entry stays at its corner; right and
        // down it grows around them as Arrange has it.
        internal void Follow(IReadOnlyDictionary<int, Vector2> places)
        {
            if (Level != GraphLevel.Near || nodes == null) return;
            Vector2 PlaceOf(int id, Vector2 file) => places.TryGetValue(id, out var to) ? to : file;
            // The least place, as the file has it and as the drag has it: the node area starts where Arrange put the
            // first, and grows by as much as the second is less
            Vector2 was = Vector2.zero, min = Vector2.zero;
            foreach (var pair in Views)
            {
                was = Vector2.Min(was, pair.Value.At);
                min = Vector2.Min(min, PlaceOf(pair.Key, pair.Value.At));
            }
            foreach (var pair in Notes)
            {
                was = Vector2.Min(was, pair.Value.At);
                min = Vector2.Min(min, PlaceOf(pair.Key, pair.Value.At));
            }
            var now = arranged + (was - min);
            if (now != origin)
            {
                At -= now - origin;
                origin = now;
                Place();
            }
            foreach (var pair in Views) pair.Value.MoveTo(origin + PlaceOf(pair.Key, pair.Value.At));
            foreach (var pair in Notes) pair.Value.MoveTo(origin + PlaceOf(pair.Key, pair.Value.At));
            Arrange(false);
        }

        // Puts the entry and the nodes where they go, and sizes the frame around them
        internal void Arrange(bool place = true)
        {
            if (Level != GraphLevel.Near || nodes == null) return;
            // As the page places them: the entry at the body's corner, the nodes right of it by the file's places
            // from the least of them (and of 0 in code's frame, so that (0, 0) is always where it was)
            var pad = GraphLook.FramePad;
            var head = GraphLook.FrameHead;
            var left = IsValue ? pad : pad + GraphLook.EntryWidth + GraphLook.EntryGap;
            var top = head + pad;
            if (place)
            {
                var min = IsValue ? new Vector2(float.MaxValue, float.MaxValue) : Vector2.zero;
                foreach (var v in Views.Values) min = Vector2.Min(min, v.At);
                foreach (var note in Notes.Values) min = Vector2.Min(min, note.At);
                if (min.x == float.MaxValue) min = Vector2.zero;
                origin = arranged = new Vector2(left, top) - min;
                Entry?.MoveTo(new Vector2(pad, top));
                foreach (var v in Views.Values) v.MoveTo(origin + v.At);
                foreach (var note in Notes.Values) note.MoveTo(origin + note.At);
            }
            float right = 0, bottom = 0;
            void Take(Vector2 at, Vector2 size)
            {
                right = Mathf.Max(right, at.x + size.x);
                bottom = Mathf.Max(bottom, at.y + size.y);
            }
            if (Entry != null) Take(Entry.Shown, Entry.SizeNow);
            foreach (var v in Views.Values) Take(v.Shown, v.SizeNow);
            foreach (var note in Notes.Values) Take(note.Shown, note.Extent);
            if (Result != null)
            {
                var root = Graph != null ? ViewOf(Graph.Wire("root")) : null;
                Result.MoveTo(new Vector2(Views.Count > 0 ? right + 60 : pad, root != null && root != Result ? root.Shown.y : top));
                Take(Result.Shown, Result.SizeNow);
            }
            if (hint != null)
            {
                hint.style.left = pad + GraphLook.EntryWidth + 30;
                hint.style.top = top + 1;
                if (hint.style.display == DisplayStyle.Flex) Take(new Vector2(left, top), new Vector2(300, 30));
            }
            // It grows around them: as wide as they reach and the room under them, as high as its head, theirs and
            // that room, and no less than the page's least
            var size2 = new Vector2(Mathf.Max(IsValue ? GraphLook.ValueFrameWidth : left + GraphLook.FrameRoomRight, right + GraphLook.FrameBottom),
                2 + head + Mathf.Max(IsValue ? GraphLook.ValueFrameHeight : GraphLook.FrameLeast, bottom - head + GraphLook.FrameBottom));
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
                else if (view.KeepOnce && view.Node == n && view.Call == call)
                {
                    view.KeepOnce = false;
                    view.Signature = sig;
                    continue;
                }
                view.KeepOnce = false;
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
            hint.text = g == null ? GraphSchema.Word("entry.extern", "extern", GraphSchema.KeyInfo("extern").Label) : IsValue ? GraphSchema.Word("value.empty") : "← " + GraphSchema.Word("frame.empty");
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
                head.Add(GraphLook.Kind(member ? GraphSchema.MemberTitle(kind) : GraphSchema.DeclTitle(kind), member ? "behaviour" : GraphSchema.DeclGroup(kind) ?? "more"));
                var owner = member ? e.Owner?.Str("name") : null;
                // Spinner.Turn: what it's of, and its name, as one
                if (owner != null) head.Add(GraphLook.Text(owner + ".", "tg-frame-title", "tg-frame-owner"));
                if (v.Str("name") != null && kind != "operator")
                {
                    var name = GraphLook.Name(v.Str("name"), Rename, "name", "tg-inline--title");
                    name.Named("name");
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
            head.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            // What the compiler says of the routine itself, in its title
            AddProblems(head, Id);
            head.Add(Badge());
            if (Graph != null)
            {
                var add = GraphLook.IconButton("plus", "Add a node (Space, or right-click in the frame)", () => Board.OpenSearch(this, FreePlace(new Vector2(40, 0), new Vector2(200, 80)), null));
                add.name = "add";
                head.Add(add);
            }
            var more = GraphLook.IconButton("more", "Add, rename, copy, delete (or right-click)", null);
            more.clicked += () => GraphMenu.Open(Window, MenuOptions(), more);
            more.name = "more";
            head.Add(more);
        }

        // Where something new of a size goes in the frame, as near a place (in the frame, as a node's is drawn) as
        // there's room: not on a node, not on the entry. Its place as the file has it is this less Origin.
        internal Vector2 FreePlace(Vector2 near, Vector2 size, IEnumerable<Rect> also = null)
        {
            var taken = new List<Rect>(also ?? Enumerable.Empty<Rect>());
            foreach (var v in Views.Values) taken.Add(new Rect(v.Shown, v.SizeNow));
            foreach (var n in Notes.Values) taken.Add(new Rect(n.Shown, n.Extent));
            if (Result != null) taken.Add(new Rect(Result.Shown, Result.SizeNow));
            // Not drawn (far away): the file's places, from where they'd be drawn
            if (Views.Count == 0)
            {
                Classify();
                foreach (var id in Visible())
                    taken.Add(new Rect(origin + GraphDocument.PlaceOf(ById[id]), GraphNodeView.EstimateOf(ById[id], CallOf(id), this)));
            }
            return GraphLook.FreeSpot(near, size, taken);
        }

        // A node's place in the file, from where it would be drawn
        internal Vector2 PlaceOfShown(Vector2 shown) => shown - origin;

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

        // Its menu: the board's one menu, as for every frame (GraphMenu)
        internal List<GraphOption> MenuOptions() => GraphMenu.ForItem(this);

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
                        // Filled when something's in it: a value, a chip or a wire
                        p.Connected = port.Path.Get(n) != 0;
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
            string TypeOf(int id) => id == 0 ? null : TypeAt(id);
            foreach (var view in Views.Values.Append(Result).Where(v => v != null))
                foreach (var (leaf, chip) in view.Chips) GraphNodeView.Tint(chip, TypeOf(leaf), Board.Light);
            foreach (var view in Views.Values)
            {
                view.SetType(TypeOf(view.Call != null ? view.Call.Wire("id") : view.Id));
                foreach (var pin in view.Ports)
                {
                    var p = pin.Port;
                    if (p.Exec) continue;
                    if (p.Local)
                    {
                        // Its declared type, else what a check found of a read of it, else of its value
                        var declared = ById.TryGetValue(p.Node, out var d) ? d.Str("type") : null;
                        var types = Window.Checked?.Types;
                        string Found(int id) => types != null && types.TryGetValue(id, out var t) ? t : null;
                        pin.SetType(!string.IsNullOrEmpty(declared) ? declared
                            : ById.Values.Where(n => n.Str("k") == "local" && n.Wire("ref") == p.Node).Select(n => Found(n.Wire("id"))).FirstOrDefault(t => t != null) ?? Found(d?.Wire("value") ?? 0));
                        continue;
                    }
                    var owner = ById.TryGetValue(p.Node, out var o) ? o : null;
                    if (p.Input) pin.SetType(owner != null && p.Path != null ? TypeOf(p.Path.Get(owner)) : null);
                    else pin.SetType(TypeOf(p.Node == view.Id && view.Call != null ? view.Call.Wire("id") : p.Node));
                }
                view.Relabel();
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
            for (var i = 0; i < 64 && n.Str("k") == "reroute"; i++)
            {
                id = n.Wire("value");
                if (id == 0 || !ById.TryGetValue(id, out n)) return null;
                if (types != null && types.TryGetValue(id, out t)) return t;
            }
            if (n.Str("k") == "name" && Reads.TryGetValue(n.Str("name") ?? "", out var read)) return read.Type;
            if (n.Str("k") == "local" && ById.TryGetValue(n.Wire("ref"), out var d) && !string.IsNullOrEmpty(d.Str("type"))) return d.Str("type");
            if (n.Str("k") == "int" || n.Str("k") == "float" || n.Str("k") == "bool" || n.Str("k") == "string") return n.Str("k");
            return n.Str("k") == "eval" && types != null && types.TryGetValue(n.Wire("value"), out t) ? t : null;
        }

        // A data wire's colour: its value's type's, else its source's category's (past reroutes, which pass a value on)
        internal Color WireColor(int from)
        {
            var type = GraphSchema.TypeColor(TypeAt(from));
            if (type != null) return type.Value;
            ById.TryGetValue(from, out var n);
            for (var i = 0; i < 64 && n != null && n.Str("k") == "reroute" && n.Wire("value") != 0; i++) ById.TryGetValue(n.Wire("value"), out n);
            return GraphSchema.ColorOf(GraphSchema.Spec(n?.Str("k"))?.Category);
        }

        // The pin a value comes out of: its node's own. What's shown in an input has none.
        internal PortView SourcePin(int from)
        {
            if (from == 0 || Hidden.Contains(from)) return null;
            return ViewOf(from)?.DataOut;
        }

        // Lights what reads a parameter or a local, and where it comes from: the entry's row, the node that
        // declares it. Hovering any of them does.
        internal void Light(string name, int local, bool on)
        {
            if (Level != GraphLevel.Near) return;
            foreach (var view in Views.Values.Append(Result).Where(v => v != null))
                foreach (var (key, chip) in view.Reads)
                    if (name != null ? key.name == name : local != 0 && key.local == local) chip.EnableInClassList("tg-lit", on);
            if (name != null) Entry?.RowOf(name)?.EnableInClassList("tg-lit", on);
            else if (local != 0) ViewOf(local)?.LocalRowOf(local)?.EnableInClassList("tg-lit", on);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Wiring

        // What was shown in an input (a number, a name, a path) goes with the wire that read it, all of it
        internal void DropUnread(JValue g, int was, int now = 0)
        {
            if (was == 0 || was == now || !Hidden.Contains(was) || GraphDocument.Readers(g, was) > 0) return;
            var chain = ChainOf(was);
            chain.Remove(now);
            Doc.DeleteNodes(g, chain);
        }

        // Empties an input, as one edit: what was shown in it goes with it (a path's names, a literal); a node wired
        // into it stays where it is, unwired, as Break links leaves it. False when nothing was in it.
        internal bool Empty(JValue owner, WirePath path, string label)
        {
            if (Graph == null || WireAt(owner, path) == 0) return false;
            Edit(string.IsNullOrEmpty(label) ? "Empty it" : "Empty " + label, () => SetWire(owner, path, 0, out _));
            return true;
        }

        // Puts a value in an input: a node's id, or 0 for nothing. What was shown there goes.
        internal bool SetWire(JValue owner, WirePath path, int from, out string why)
        {
            why = null;
            var g = Graph;
            if (g == null) return false;
            var was = WireAt(owner, path);
            if (owner == null) g.Set("root", from != 0 ? JValue.Of(from) : JValue.Null(), GraphSchema.ValueOrder);
            else if (from == 0) path.Set(owner, 0);
            else if (!GraphDocument.Connect(g, owner.Wire("id"), path, from, out why)) return false;
            DropUnread(g, was, from);
            return true;
        }

        // Where a node made for an input of a node goes: beside it, where tidec's layout puts what's shown there
        internal Vector2 PlaceFor(JValue owner)
        {
            if (owner == null) return Vector2.zero;
            var view = ReaderOf(owner.Wire("id")) ?? ViewOf(owner.Wire("id"));
            // Below what reads it, where nothing is: shown in the input, it takes no room, and should it ever be
            // drawn as a node of its own (read by nothing, or by two), it's on nothing else
            return view != null && view.Node != null ? PlaceOfShown(FreePlace(view.Shown + new Vector2(0, view.SizeNow.y + 30), new Vector2(200, 60))) : GraphDocument.PlaceOf(owner);
        }

        // Where the nodes just made for an input go (in an edit, once they're wired): what's shown in an input or a
        // title where its reader is, as tidec's layout has it; the Gets left of the node whose input it is, at the
        // input's row, one after another (Get ball, then position, then x), in free space
        internal void PlaceMade(JValue owner, WirePath path, IList<int> made)
        {
            var g = Graph;
            if (g == null || made.Count == 0) return;
            Classify();
            var drawn = made.Where(id => !Hidden.Contains(id) && ById.ContainsKey(id)).ToList();
            foreach (var id in made.Where(id => Hidden.Contains(id) && ById.ContainsKey(id)))
            {
                var reader = ReaderOf(id)?.Node ?? owner;
                if (reader != null) SetPlace(ById[id], GraphDocument.PlaceOf(reader));
            }
            if (drawn.Count == 0) return;
            var sizes = drawn.Select(id => GraphNodeView.EstimateOf(ById[id], null, this)).ToList();
            const float gap = 28;
            var width = sizes.Sum(s => s.x) + gap * (sizes.Count - 1);
            var height = sizes.Max(s => s.y);
            var extra = new List<Rect>();
            Vector2 near;
            var view = owner != null ? ReaderOf(owner.Wire("id")) ?? ViewOf(owner.Wire("id")) : Result;
            if (view != null && view.Node != null || view == Result && view != null)
            {
                // The input's row, where it's drawn; under the title until it is
                var row = path != null ? view.Q("in:" + path) : null;
                var rowY = row != null && !float.IsNaN(row.layout.height) && row.layout.height > 0 ? row.ChangeCoordinatesTo(view, Vector2.zero).y : 26;
                near = view.Shown + new Vector2(-width - 56, rowY - 2);
            }
            else if (owner != null)
            {
                // Its node isn't drawn yet (just made): left of where it's going
                var at = origin + GraphDocument.PlaceOf(owner);
                var size = GraphNodeView.EstimateOf(owner, null, this);
                extra.Add(new Rect(at, size));
                near = at + new Vector2(-width - 56, 26);
            }
            else near = origin;
            var spot = FreePlace(near, new Vector2(width, height), extra);
            foreach (var (id, size) in drawn.Zip(sizes, (id, size) => (id, size)))
            {
                SetPlace(ById[id], PlaceOfShown(spot));
                spot.x += size.x + gap;
            }
        }

        static void SetPlace(JValue n, Vector2 at)
        {
            n.Set("x", JValue.Of(Mathf.Round(at.x)));
            n.Set("y", JValue.Of(Mathf.Round(at.y)));
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
            // What a Set changes must be something that can be changed
            if (reader?.Str("k") == "assign" && input.Path.Parts.Length == 1 && input.Path.Key == "target")
            {
                var (name, local) = output.IsRead ? (output.Local ? ((string)null, output.Node) : (output.Name, 0)) : RootOf(output.Node);
                if ((name != null || local != 0) && !Changeable(name, local))
                {
                    var part = local != 0 ? new Part { Kind = "local", Ref = local, Name = ById.TryGetValue(local, out var d) ? GraphSchema.LocalName(d) : "it" } : new Part { Kind = "name", Name = name };
                    why = WhyNotChangeable(new[] { part });
                    return false;
                }
            }
            var was = input.Node == ResultId ? g.Wire("root") : reader != null ? input.Path.Get(reader) : 0;
            int from;
            JValue made = null;
            if (output.IsRead)
            {
                // Reading a parameter or a local is a Get: a node of its own, beside the input it's read by
                var at = PlaceFor(reader);
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
            if (made != null) PlaceMade(reader, input.Path, new[] { from });
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
                // A parameter's or a local's pin has no wire of its own: what reads it says so itself
            }
            else
            {
                foreach (var n in GraphDocument.Nodes(g).Items)
                    foreach (var q in GraphPorts.Of(n))
                        if (q.Path != null && !q.Exec && q.Path.Get(n) == p.Node) q.Path.Set(n, 0);
                if (g.Wire("root") == p.Node) g.Set("root", JValue.Null());
            }
        }

        // One part of a value shown in an input, to be made: a name, a local (Ref the node that declares it), a
        // member of the part before it, a member of nothing (a shorthand: .Wild), or this, default, null
        internal sealed class Part
        {
            public string Kind, Name, Type;
            public int Ref;
            public bool Space; // One of the engine's namespaces (Math): it has things, and is nothing by itself

            public static Part Of(GraphFrame frame, JValue n)
            {
                var k = n.Str("k");
                var type = frame.TypeAt(n.Wire("id"));
                if (k == "name") return new Part { Kind = "name", Name = n.Str("name"), Type = type };
                if (k == "local") return new Part { Kind = "local", Ref = n.Wire("ref"), Name = frame.PartText(n), Type = type };
                if (k == "member") return new Part { Kind = n.Flag("shorthand") ? "shorthand" : "member", Name = n.Str("member"), Type = type };
                return new Part { Kind = k, Name = k, Type = type };
            }
        }

        // Makes the nodes of a path (wobble, then .phase), each a node of the file as tidec writes them: their ids
        internal List<int> MakePath(JValue g, IList<Part> parts, Vector2 at)
        {
            var made = new List<int>();
            var last = 0;
            foreach (var part in parts)
            {
                JValue n;
                switch (part.Kind)
                {
                    case "name":
                        n = Doc.AddNode(g, "name", at.x, at.y);
                        n.Set("name", JValue.Of(part.Name), GraphSchema.NodeOrder("name"));
                        break;
                    case "local":
                        n = Doc.AddNode(g, "local", at.x, at.y);
                        n.Set("ref", JValue.Of(part.Ref), GraphSchema.NodeOrder("local"));
                        break;
                    case "member":
                    case "shorthand":
                    {
                        n = Doc.AddNode(g, "member", at.x, at.y);
                        var order = GraphSchema.NodeOrder("member");
                        if (part.Kind == "shorthand") n.SetFlag("shorthand", true, order);
                        else if (last != 0) n.Set("object", JValue.Of(last), order);
                        n.Set("member", JValue.Of(part.Name), order);
                        break;
                    }
                    default:
                        n = Doc.AddNode(g, part.Kind, at.x, at.y);
                        break;
                }
                last = n.Wire("id");
                made.Add(last);
            }
            return made;
        }

        // Puts a path in an input, in place of what was there
        internal void PutPath(JValue owner, WirePath path, IList<Part> parts, string label)
        {
            var g = Graph;
            if (g == null || parts.Count == 0) return;
            string why = null;
            Edit("Set " + label, () =>
            {
                Classify();
                var made = MakePath(g, parts, PlaceFor(owner));
                if (!SetWire(owner, path, made[made.Count - 1], out why)) Doc.DeleteNodes(g, made);
                else PlaceMade(owner, path, made);
            });
            if (why != null) Window.Say(GraphLook.UpperFirst(why), true);
        }

        // Puts a value typed in an input: a number (a '-' before it is a node of its own, as tidec writes it), a
        // text, a tick
        internal void PutLiteral(JValue owner, WirePath path, string k, string text, string label)
        {
            var g = Graph;
            if (g == null) return;
            var negative = (k == "int" || k == "float") && text.StartsWith("-");
            if (negative) text = text.Substring(1);
            Edit("Set " + label, () =>
            {
                Classify();
                var shown = ShownIn(WireAt(owner, path));
                var simple = shown != null && Array.IndexOf(new[] { "int", "float", "string", "char", "bool" }, shown.Str("k")) >= 0;
                if (simple && !negative)
                {
                    GraphValues.SetLiteral(shown, k, text);
                    return;
                }
                var at = PlaceFor(owner);
                var n = Doc.AddNode(g, k, at.x, at.y);
                GraphValues.SetLiteral(n, k, text);
                var made = n.Wire("id");
                if (negative)
                {
                    var minus = Doc.AddNode(g, "unary", at.x, at.y);
                    var order = GraphSchema.NodeOrder("unary");
                    minus.Set("op", JValue.Of("-"), order);
                    minus.Set("value", JValue.Of(made), order);
                    made = minus.Wire("id");
                }
                SetWire(owner, path, made, out _);
            });
        }

        // Puts a colour's numbers in the call that makes it (Color(1, 0.5, 0.25)), each a number shown there
        internal void PutNumbers(JValue call, IList<string> texts, string label)
        {
            var g = Graph;
            if (g == null) return;
            Edit("Set " + label, () =>
            {
                Classify();
                var order = GraphSchema.NodeOrder("call");
                var args = call.Arr("args");
                if (args == null) call.Set("args", args = JValue.NewArray(), order);
                while (args.Items.Count < texts.Count) args.Items.Add(JValue.Null());
                for (var i = 0; i < texts.Count; i++)
                {
                    var arg = ShownIn(JValue.AsId(args.Items[i]));
                    if (arg != null && (arg.Str("k") == "int" || arg.Str("k") == "float"))
                    {
                        GraphValues.SetLiteral(arg, "float", texts[i]);
                        continue;
                    }
                    var at = GraphDocument.PlaceOf(call);
                    var n = Doc.AddNode(g, "float", at.x, at.y);
                    GraphValues.SetLiteral(n, "float", texts[i]);
                    SetWire(call, new WirePath("args", i), n.Wire("id"), out _);
                }
            });
        }

        // The parameter or the local of a name, as a part to read it: null when the code has none of that name
        internal Part ReadOf(string name)
        {
            if (Reads.TryGetValue(name, out var read)) return new Part { Kind = "name", Name = name, Type = read.Type };
            var local = ById.Values.FirstOrDefault(n => Declares(n) && GraphSchema.LocalName(n) == name);
            return local != null ? new Part { Kind = "local", Ref = local.Wire("id"), Name = name, Type = local.Str("type") } : null;
        }

        // What's typed on an input: a number, a text, true or false is that value (of the input's type, where it's
        // known); a name of something the code has, or a path from one (wobble.phase), is read; nothing takes
        // away what was there; anything else opens the picker with it, to find what was meant
        internal void TypeInto(JValue owner, WirePath path, string typed, VisualElement from, string label = "value")
        {
            typed = (typed ?? "").Trim();
            var shown = ShownIn(WireAt(owner, path));
            if (typed.Length == 0)
            {
                if (shown != null) Edit("Clear " + label, () => SetWire(owner, path, 0, out _));
                else Refresh();
                return;
            }
            if (shown != null && TextOf(shown) == typed)
            {
                Refresh();
                return;
            }
            var wanted = Wanted(owner, path);
            if (GraphSchema.ValueControl(wanted) == null) wanted = shown != null && (shown.Str("k") == "string" || shown.Str("k") == "char") ? shown.Str("k") : null;
            if (GraphValues.TryLiteral(typed, wanted, out var k, out var text))
            {
                PutLiteral(owner, path, k, text, label);
                return;
            }
            var names = typed.Split('.');
            var first = names.All(n => n.Length > 0 && GraphLook.Identifier(n) == n) ? ReadOf(names[0]) : null;
            if (first != null)
            {
                var parts = new List<Part> { first };
                parts.AddRange(names.Skip(1).Select(n => new Part { Kind = "member", Name = n }));
                PutPath(owner, path, parts, label);
                return;
            }
            // Not a value in itself: found, not typed
            Refresh();
            Board.PickValue(this, owner, path, label, from, 0, typed);
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

        // The operators a node can have: for a Set, only those the checker takes for what it changes and the value
        // it's given (tide/graphOperators); for an operator node, those it takes for its first value. All of them
        // while it's asked, and when that isn't known.
        internal GraphOptionList PickOperator(GraphNodeView view, JValue n, VisualElement from)
        {
            var k = n.Str("k");
            var list = new GraphOptionList(k == "assign" ? "How is it changed?" : GraphSchema.Spec(k)?.Title, null, "Search", k == "assign" ? 260 : 220);
            HashSet<string> valid = null;
            string type = null;
            void Fill()
            {
                list.Set(GraphSchema.Ops(k).Where(o => valid == null || valid.Contains(o.op)).Select(o => new GraphOption
                {
                    // A Set's sign with what it does in words beside it (the schema's assignInfo)
                    Title = o.title ?? o.op, Detail = o.title != null ? o.op : k == "assign" ? GraphSchema.AssignWord(o.op) : null, Keywords = o.op, Checked = o.op == n.Str("op"), Data = o.op,
                    Pick = () => Edit("Operator", () => n.Set("op", JValue.Of(o.op), GraphSchema.NodeOrder(k))),
                }), true);
                list.HelpText = valid != null && type != null ? $"Those that work on a {type}." : null;
            }
            Fill();
            Window.Open(list, from);
            // What it works on: what a Set changes and what it's given, an operator's first value and its second
            var first = k == "assign" ? n.Wire("target") : k == "binary" ? n.Wire("lhs") : k == "unary" ? n.Wire("value") : k == "step" ? n.Wire("target") : 0;
            var second = k == "assign" ? n.Wire("value") : k == "binary" ? n.Wire("rhs") : 0;
            type = first != 0 ? TypeAt(first) : null;
            var other = second != 0 ? TypeAt(second) : null;
            if (type == null) return list;
            list.Status = "Asking which work here…";
            TideGraphLanguage.Operators(Window, type, other, (ops, why) =>
            {
                if (list.WasClosed) return;
                list.Status = null;
                if (ops == null) return;
                valid = new HashSet<string>();
                foreach (var group in k == "assign" ? new[] { "assign", "step" } : k == "binary" ? new[] { "binary" } : k == "step" ? new[] { "step" } : new[] { "unary" })
                    foreach (var op in GraphLists.Strings(ops.Arr(group))) valid.Add(op);
                Fill();
            });
            return list;
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

        // ---------------------------------------------------------------------------------------------------------
        // Gets, Sets, reroutes and locals, Blueprint's ways (notes/visual-scripting.md, "Round four")

        // A variable, or a field of one, as the parts that read it: wobble, then .phase
        internal static string PathText(IList<Part> parts) => string.Join("", parts.Select((p, i) => i == 0 && p.Kind != "shorthand" ? p.Name : "." + p.Name));

        // Whether a Set can change what parts read: what they start from can be changed
        internal bool Changeable(IList<Part> parts) => parts.Count > 0 && Changeable(parts[0].Kind == "local" ? null : parts[0].Name, parts[0].Kind == "local" ? parts[0].Ref : 0);

        // Why a Set can't change what parts read, in words that say how it could
        internal string WhyNotChangeable(IList<Part> parts)
        {
            if (parts.Count == 0) return null;
            if (parts[0].Kind == "local") return $"{parts[0].Name} can't be changed: tick \"{GraphSchema.FlagInfo("mut").Label}\" in its Details to change it.";
            return $"{parts[0].Name} is taken to be read: to change it, make its row in the entry \"{GraphSchema.ParamModeInfo("mut").Label}\".";
        }

        // A Get of a variable or of a field of one, at a place in the frame (as its nodes are drawn): the nodes it's
        // made of left to right from there, the last one's id
        internal int MakeGet(IList<Part> parts, Vector2 shown)
        {
            var g = Graph;
            if (g == null || parts.Count == 0) return 0;
            var made = 0;
            Edit("Get " + PathText(parts), () =>
            {
                var ids = MakePath(g, parts, Vector2.zero);
                Classify();
                var sizes = ids.Select(id => GraphNodeView.EstimateOf(ById[id], null, this)).ToList();
                var spot = FreePlace(shown, new Vector2(sizes.Sum(s => s.x) + 28 * (sizes.Count - 1), sizes.Max(s => s.y)));
                for (var i = 0; i < ids.Count; i++)
                {
                    SetPlace(ById[ids[i]], PlaceOfShown(spot));
                    spot.x += sizes[i].x + 28;
                }
                made = ids[ids.Count - 1];
            });
            if (made != 0) Board.SelectNode(this, made, false);
            return made;
        }

        // A Set of a variable or of a field of one, at a place in the frame: titled by what it changes, what that's a
        // field of a Get wired into its Target. Its flow is wired in by hand. Its id.
        internal int MakeSet(IList<Part> parts, Vector2 shown)
        {
            var g = Graph;
            if (g == null || parts.Count == 0 || !Slot.IsBody) return 0;
            if (!Changeable(parts))
            {
                Window.Say(WhyNotChangeable(parts), true);
                return 0;
            }
            var made = 0;
            Edit("Set " + PathText(parts), () =>
            {
                var at = PlaceOfShown(FreePlace(shown, new Vector2(220, 90)));
                var set = Doc.AddNode(g, "assign", at.x, at.y);
                var ids = MakePath(g, parts, at);
                new WirePath("target").Set(set, ids[ids.Count - 1]);
                made = set.Wire("id");
                PlaceMade(set, new WirePath("target"), ids);
            });
            if (made != 0) Board.SelectNode(this, made, false);
            return made;
        }

        // Puts a reroute on a data wire at a place of the frame (as its nodes are drawn): the knot is wired from what
        // the wire came from and into what it went to, one edit
        internal int AddReroute(int from, JValue reader, WirePath path, Vector2 shown)
        {
            var g = Graph;
            if (g == null || from == 0) return 0;
            var made = 0;
            Edit("Add a reroute", () =>
            {
                var at = PlaceOfShown(shown - GraphNodeView.KnotSize / 2);
                var knot = Doc.AddNode(g, "reroute", at.x, at.y);
                new WirePath("value").Set(knot, from);
                if (reader == null) g.Set("root", JValue.Of(knot.Wire("id")), GraphSchema.ValueOrder);
                else path.Set(reader, knot.Wire("id"));
                made = knot.Wire("id");
            });
            return made;
        }

        // Before reroutes are deleted: what each one passed on goes straight to what read it, through any other
        // knots being deleted, so deleting a knot never breaks the wire it was on
        internal static void JoinAroundKnots(JValue g, ICollection<int> deleted)
        {
            int Through(int id)
            {
                for (var i = 0; i < 64 && id != 0 && deleted.Contains(id); i++)
                {
                    var knot = GraphDocument.NodeIn(g, id);
                    if (knot == null || knot.Str("k") != "reroute") return id;
                    id = knot.Wire("value");
                }
                return id;
            }
            var knots = deleted.Where(id => GraphDocument.NodeIn(g, id)?.Str("k") == "reroute").ToList();
            if (knots.Count == 0) return;
            foreach (var n in GraphDocument.Nodes(g).Items)
            {
                if (deleted.Contains(n.Wire("id"))) continue;
                foreach (var p in GraphPorts.Of(n))
                {
                    if (!p.Input || p.Exec || p.Path == null) continue;
                    var w = p.Path.Get(n);
                    if (w == 0 || !knots.Contains(w)) continue;
                    var to = Through(w);
                    p.Path.Set(n, deleted.Contains(to) ? 0 : to);
                }
            }
            var root = g.Wire("root");
            if (root != 0 && knots.Contains(root))
            {
                var to = Through(root);
                g.Set("root", to != 0 && !deleted.Contains(to) ? JValue.Of(to) : JValue.Null());
            }
        }

        // A node's value can be kept as a local (Blueprint's Promote to Variable): something worked out that's read,
        // in code with statements
        internal bool CanKeepAsLocal(int node)
        {
            if (!Slot.IsBody || Graph == null || !ById.TryGetValue(node, out var n) || Hidden.Contains(node) || Absorbed.ContainsKey(node)) return false;
            var k = n.Str("k");
            var spec = GraphSchema.Spec(k);
            if (spec == null || !spec.Value || spec.Exec) return false;
            if (k == "name" || k == "local" || k == "member" || k == "reroute" || k == "this" || k == "default" || k == "null" || IsLiteral(k) || k == "outVar" || k == "lambda") return false;
            return Readers.TryGetValue(node, out var readers) && readers.Count > 0 && StatementsReading(node).Count > 0;
        }

        // The statements whose code reads a node, through whatever reads it
        List<int> StatementsReading(int node)
        {
            var found = new List<int>();
            var seen = new HashSet<int>();
            void Up(int id)
            {
                if (!seen.Add(id) || !Readers.TryGetValue(id, out var readers)) return;
                foreach (var (reader, _) in readers)
                {
                    if (reader <= 0 || !ById.TryGetValue(reader, out var r)) continue;
                    if (GraphSchema.IsExec(r.Str("k"))) { if (!found.Contains(reader)) found.Add(reader); }
                    else if (r.Str("k") != "lambda") Up(reader);
                }
            }
            Up(node);
            return found;
        }

        // Where a statement is: the scopes it's in from the body's own (each the statement there that holds it, and
        // its place among that scope's statements), outermost first
        List<(string scope, int statement, int place)> ScopesOf(JValue g, int statement)
        {
            var chain = new List<(string, int, int)>();
            var at = statement;
            for (var guard = 0; guard < 4096 && at != 0; guard++)
            {
                // Back along next to the scope's first statement, counting
                var first = at;
                var place = 0;
                (int node, WirePath path) source;
                for (var steps = 0; ; steps++)
                {
                    source = GraphDocument.ExecSourceOf(g, first);
                    if (source.node == 0 || source.path.Key != "next" || steps > 4096) break;
                    first = source.node;
                    place++;
                }
                if (source.node == 0)
                {
                    chain.Insert(0, ("entry", at, place));
                    break;
                }
                chain.Insert(0, ($"{source.node}:{source.path}", at, place));
                at = source.node;
            }
            return chain;
        }

        // Keeps a node's value as a local, as one edit: a Local declared just before the statement that first reads
        // it (in the scope every reader is in), its value the node, and Gets of the local where the node's wires went,
        // so it's worked out once. The new local's id.
        internal int KeepAsLocal(int node)
        {
            var g = Graph;
            if (g == null) return 0;
            Classify();
            if (!CanKeepAsLocal(node)) return 0;
            var statements = StatementsReading(node);
            var chains = statements.Select(s => ScopesOf(g, s)).ToList();
            // The deepest scope all of them are in, and the first statement of it that holds one
            var depth = 0;
            while (chains.All(c => c.Count > depth) && chains.Select(c => c[depth].scope).Distinct().Count() == 1 && chains.Select(c => c[depth].statement).Distinct().Count() == 1) depth++;
            if (chains.Any(c => c.Count <= depth) && depth > 0) depth--;
            if (chains.Any(c => c.Count <= depth)) return 0;
            var before = chains.Select(c => c[depth]).OrderBy(c => c.place).First().statement;
            var n = ById[node];
            var type = TypeAt(node);
            var k = n.Str("k");
            var wanted = (k == "call" || k == "method") && !string.IsNullOrEmpty(n.Str("name")) ? GraphLook.LowerFirst(n.Str("name")) : NameFor(type);
            var taken = new HashSet<string>(Reads.Keys.Concat(GraphDocument.Locals(g).Select(GraphSchema.LocalName)));
            var name = wanted;
            for (var i = 2; taken.Contains(name); i++) name = wanted + i;
            var readers = Readers[node].Where(r => r.node > 0).ToList();
            var made = 0;
            Edit("Keep it as a local", () =>
            {
                var next = ById[before];
                var at = PlaceOfShown(FreePlace(origin + GraphDocument.PlaceOf(next) + new Vector2(-280, 0), new Vector2(240, 90)));
                var local = Doc.AddNode(g, "var", at.x, at.y);
                var order = GraphSchema.NodeOrder("var");
                local.Set("name", JValue.Of(name), order);
                local.Set("value", JValue.Of(node), order);
                // In the flow just before the statement
                var (from, path) = GraphDocument.ExecSourceOf(g, before);
                if (g.Wire("entry") == before) g.Set("entry", JValue.Of(local.Wire("id")), GraphSchema.BodyOrder);
                else if (from != 0) path.Set(GraphDocument.NodeIn(g, from), local.Wire("id"));
                new WirePath("next").Set(local, before);
                made = local.Wire("id");
                // Gets of it where the node's value went
                foreach (var (reader, input) in readers)
                {
                    var r = GraphDocument.NodeIn(g, reader);
                    if (r == null) continue;
                    var get = Doc.AddNode(g, "local", at.x, at.y);
                    get.Set("ref", JValue.Of(made), GraphSchema.NodeOrder("local"));
                    input.Set(r, get.Wire("id"));
                    PlaceMade(r, input, new[] { get.Wire("id") });
                }
            });
            if (made != 0)
            {
                Board.SelectNode(this, made, false);
                // A new thing starts with its name being typed
                schedule.Execute(() => Board.RenameNode(this, made));
            }
            return made;
        }

        internal void OpenNodeMenu(GraphNodeView view, VisualElement from)
        {
            if (!(Board.Active == this && Board.Nodes.Contains(view.Id))) Board.SelectNode(this, view.Id, false);
            GraphMenu.Open(Window, GraphMenu.ForNode(view), from);
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

        internal static void Swap(JValue list, int a, int b)
        {
            if (a < 0 || b < 0 || a >= list.Items.Count || b >= list.Items.Count) return;
            var item = list.Items[a];
            list.Items.RemoveAt(a);
            list.Items.Insert(b, item);
        }

        // Takes away something the code takes. What read it still says its name, and the check says where: a
        // path with its start gone (.phase of nothing) would say less.
        internal void RemoveParam(JValue list, JValue p)
        {
            var name = p.Str("name");
            Edit("Take away " + (name ?? p.Str("type")), () => list.Items.Remove(p));
        }
    }

    // A frame's wires: from each value's pin to the inputs that read it, and from each step's arrow to the next.
    // Drawn as curves in the colour of the value's type, fainter where what they join is left out. A data wire can
    // be hit (WireAt): the one under the pointer is drawn thicker, a double click on it puts a reroute there, and a
    // right click opens its menu.
    sealed class FrameWires : VisualElement
    {
        readonly GraphFrame frame;

        // A data wire as drawn: what it comes from, the input it goes to (its node, ResultId for a value's own) and
        // the points along it, in the frame
        internal sealed class Wire
        {
            public int From, Reader;
            public WirePath Path;
            public Vector2[] Points;
        }

        readonly List<Wire> drawn = new List<Wire>();
        Wire hot;

        public FrameWires(GraphFrame frame)
        {
            this.frame = frame;
            pickingMode = PickingMode.Ignore;
            AddToClassList("tg-fill");
            generateVisualContent += Draw;
        }

        internal IReadOnlyList<Wire> Drawn => drawn;

        // The data wire nearest a place of the frame, within `radius`; null when none is
        internal Wire WireAt(Vector2 at, float radius)
        {
            Wire best = null;
            var bestD = radius * radius;
            foreach (var w in drawn)
                for (var i = 1; i < w.Points.Length; i++)
                {
                    var d = DistanceSquared(at, w.Points[i - 1], w.Points[i]);
                    if (d >= bestD) continue;
                    bestD = d;
                    best = w;
                }
            return best;
        }

        // The wire under the pointer, drawn thicker; null for none
        internal Wire Hot
        {
            get => hot;
            set
            {
                if (hot == value || hot != null && value != null && hot.From == value.From && hot.Reader == value.Reader && hot.Path.Equals(value.Path)) return;
                hot = value;
                MarkDirtyRepaint();
            }
        }

        static float DistanceSquared(Vector2 p, Vector2 a, Vector2 b)
        {
            var ab = b - a;
            var t = ab.sqrMagnitude > 0 ? Mathf.Clamp01(Vector2.Dot(p - a, ab) / ab.sqrMagnitude) : 0;
            return (a + ab * t - p).sqrMagnitude;
        }

        static Vector2[] Sample(Vector2 a, Vector2 b)
        {
            var dx = Mathf.Max(40, Mathf.Abs(b.x - a.x) * 0.5f);
            Vector2 c1 = a + new Vector2(dx, 0), c2 = b - new Vector2(dx, 0);
            var points = new Vector2[25];
            for (var i = 0; i < points.Length; i++)
            {
                var t = i / (float)(points.Length - 1);
                var u = 1 - t;
                points[i] = u * u * u * a + 3 * u * u * t * c1 + 3 * u * t * t * c2 + t * t * t * b;
            }
            return points;
        }

        void Draw(MeshGenerationContext ctx)
        {
            drawn.Clear();
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
                        var c = frame.WireColor(wired);
                        var a = from.Anchor(this);
                        var b = pin.Anchor(this);
                        // A long wire (a value worked out far from what reads it) is faint, as the page draws it
                        if (faint || Mathf.Abs(b.x - a.x) + Mathf.Abs(b.y - a.y) > 900) c.a *= 0.4f;
                        var wire = new Wire { From = wired, Reader = port.Node, Path = port.Path, Points = Sample(a, b) };
                        drawn.Add(wire);
                        var lit = hot != null && hot.From == wire.From && hot.Reader == wire.Reader && hot.Path.Equals(wire.Path);
                        Curve(p, a, b, c, lit ? 3 : 2);
                    }
                    else if (!port.Input && port.Exec)
                    {
                        var to = frame.ViewOf(wired)?.ExecIn;
                        if (to == null) continue;
                        var c = palette.Exec;
                        c.a *= to.View.ClassListContains("tg-node--unreached") ? 0.35f : 0.8f;
                        Curve(p, pin.Anchor(this), to.Anchor(this), c, 2.5f);
                    }
                }
            }
            var dragging = board.WireFrom;
            if (dragging != null && dragging.Frame == frame)
            {
                var end = board.ChangeCoordinatesTo(this, board.WireEnd);
                var start = dragging.Anchor(this);
                var c = dragging.Port.Exec ? palette.Exec : palette.Muted;
                if (dragging.Port.Input) Curve(p, end, start, c, 2);
                else Curve(p, start, end, c, 2);
            }
        }

        static void Curve(Painter2D p, Vector2 a, Vector2 b, Color c, float width)
        {
            var dx = Mathf.Max(40, Mathf.Abs(b.x - a.x) * 0.5f);
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
            // As the page draws a frame far away: the entry in the look's behaviour colour, each node a block of its
            // kind's colour where it's drawn up close, by tidec's sizes, fainter where it's never reached
            var light = frame.Board.Light;
            var pad = GraphLook.FramePad;
            var left = frame.IsValue ? pad : pad + GraphLook.EntryWidth + GraphLook.EntryGap;
            var top = GraphLook.FrameHead + pad;
            void Box(Rect r, Color c)
            {
                p.fillColor = c;
                p.BeginPath();
                p.MoveTo(new Vector2(r.xMin, r.yMin));
                p.LineTo(new Vector2(r.xMax, r.yMin));
                p.LineTo(new Vector2(r.xMax, r.yMax));
                p.LineTo(new Vector2(r.xMin, r.yMax));
                p.ClosePath();
                p.Fill();
            }
            if (!frame.IsValue) Box(new Rect(pad, top, GraphLook.EntryWidth, 34 + GraphLook.CardRow * (GraphEntryView.RowsOf(frame) + 1)), GraphLook.Fade(GraphSchema.Look("behaviour", light), 0.75f));
            var visible = frame.Visible().ToList();
            float minX = frame.IsValue ? float.MaxValue : 0, minY = frame.IsValue ? float.MaxValue : 0;
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
                var size = GraphNodeView.EstimateOf(n, null, frame);
                var c = GraphLook.Fade(GraphSchema.ColorOf(GraphSchema.Spec(n.Str("k"))?.Category), frame.Reached.Contains(id) ? 0.75f : 0.75f * 0.35f);
                Box(new Rect(left + at.x, top + at.y, size.x, Mathf.Max(40, size.y - 24)), c);
            }
        }
    }
}
