using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The canvas of the graph open: a grid that pans (middle mouse, or Alt and drag) and zooms (the wheel, about
    // the pointer), its nodes and notes, and the wires between them. Drag from a port to a port to wire them, from a
    // wired input or statement's input to unwire; drop a wire on nothing, press Space or right-click to add a node.
    sealed class GraphCanvas : VisualElement
    {
        public readonly TideGraphWindow Window;
        readonly VisualElement grid, content, notesLayer, nodesLayer, selectRect, empty;
        readonly WireLayer wires;
        readonly Dictionary<int, GraphNodeView> views = new Dictionary<int, GraphNodeView>();
        readonly Dictionary<int, NoteView> notes = new Dictionary<int, NoteView>();
        readonly Dictionary<int, int> absorbed = new Dictionary<int, int>(); // A merged call's id, to its eval's
        readonly Dictionary<int, string> errors = new Dictionary<int, string>();
        HashSet<int> reached = new HashSet<int>();
        Vector2 pan;
        float zoom = 1;
        Vector2 mouse; // In the canvas's space
        GraphSearchPopup search;

        public readonly HashSet<int> Selection = new HashSet<int>();

        public GraphCanvas(TideGraphWindow window)
        {
            Window = window;
            style.flexGrow = 1;
            style.overflow = Overflow.Hidden;
            style.backgroundColor = new Color(0.13f, 0.13f, 0.14f);
            focusable = true;

            grid = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 0, top = 0, right = 0, bottom = 0 } };
            grid.generateVisualContent += DrawGrid;
            Add(grid);
            content = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 0, top = 0, width = 1, height = 1, transformOrigin = new TransformOrigin(0, 0, 0) } };
            Add(content);
            notesLayer = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 0, top = 0 } };
            content.Add(notesLayer);
            wires = new WireLayer(this);
            content.Add(wires);
            nodesLayer = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 0, top = 0 } };
            content.Add(nodesLayer);
            selectRect = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, display = DisplayStyle.None, backgroundColor = new Color(0.4f, 0.6f, 1f, 0.12f), borderTopWidth = 1, borderBottomWidth = 1, borderLeftWidth = 1, borderRightWidth = 1, borderTopColor = new Color(0.5f, 0.7f, 1f), borderBottomColor = new Color(0.5f, 0.7f, 1f), borderLeftColor = new Color(0.5f, 0.7f, 1f), borderRightColor = new Color(0.5f, 0.7f, 1f) } };
            Add(selectRect);
            empty = new Label("Select a declaration with code, or a value's graph, to see it here.") { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 20, top = 20, color = new Color(0.6f, 0.6f, 0.6f) } };
            Add(empty);

            RegisterCallback<WheelEvent>(OnWheel);
            RegisterCallback<PointerDownEvent>(OnDown);
            RegisterCallback<PointerMoveEvent>(OnMove);
            RegisterCallback<PointerUpEvent>(OnUp);
            RegisterCallback<KeyDownEvent>(OnKey);
            RegisterCallback<GeometryChangedEvent>(_ => Redraw());
            ApplyTransform();
        }

        public GraphSlot Slot => Window.Slot;
        public JValue Graph => Window.Doc?.GetGraph(Slot);
        GraphDocument Doc => Window.Doc;
        public IReadOnlyDictionary<int, GraphNodeView> Views => views;
        public IReadOnlyDictionary<int, int> Absorbed => absorbed;
        public IReadOnlyDictionary<int, NoteView> Notes => notes;
        internal Vector2 Pan => pan;
        internal float Zoom => zoom;

        // ---------------------------------------------------------------------------------------------------------
        // Building

        public void Rebuild()
        {
            nodesLayer.Clear();
            notesLayer.Clear();
            views.Clear();
            notes.Clear();
            absorbed.Clear();
            CloseSearch();
            var graph = Graph;
            empty.style.display = graph == null ? DisplayStyle.Flex : DisplayStyle.None;
            if (graph == null)
            {
                if (Slot.Valid && Doc.Find(Slot.Owner) != null) empty.style.display = DisplayStyle.None;
                Redraw();
                if (Slot.Valid && Doc.Find(Slot.Owner) != null) ShowEmptySlot();
                return;
            }
            reached = GraphDocument.Reached(graph);
            var nodes = GraphDocument.Nodes(graph).Items;
            var byId = new Dictionary<int, JValue>();
            foreach (var n in nodes) byId[n.Wire("id")] = n;
            // A call read only by its eval is drawn with it
            var readers = new Dictionary<int, int>();
            foreach (var n in nodes)
                foreach (var w in GraphPorts.DataWires(n))
                    readers[w] = readers.TryGetValue(w, out var c) ? c + 1 : 1;
            if (graph.Wire("root") != 0) readers[graph.Wire("root")] = readers.TryGetValue(graph.Wire("root"), out var r) ? r + 1 : 1;
            foreach (var n in nodes)
            {
                if (n.Str("k") != "eval") continue;
                var v = n.Wire("value");
                if (v != 0 && byId.TryGetValue(v, out var call) && (call.Str("k") == "call" || call.Str("k") == "method") && readers.TryGetValue(v, out var count) && count == 1 && !absorbed.ContainsKey(v))
                    absorbed[v] = n.Wire("id");
            }
            foreach (var n in nodes)
            {
                var id = n.Wire("id");
                if (absorbed.ContainsKey(id)) continue;
                JValue call = null;
                var v = n.Wire("value");
                if (n.Str("k") == "eval" && absorbed.TryGetValue(v, out var eval) && eval == id) call = byId[v];
                var view = new GraphNodeView(this, n, call);
                views[id] = view;
                nodesLayer.Add(view);
                NodeEvents(view);
            }
            // The start of a body, the value of an expression
            if (Slot.IsBody)
            {
                var first = views.TryGetValue(graph.Wire("entry"), out var f) ? f.At - new Vector2(150, 0) : Origin(nodes) - new Vector2(170, 0);
                var start = new GraphNodeView(this, GraphNodeView.EntryId, first);
                views[GraphNodeView.EntryId] = start;
                nodesLayer.Add(start);
                NodeEvents(start);
            }
            else
            {
                var root = graph.Wire("root");
                var at = views.TryGetValue(root, out var rv) ? rv.At + new Vector2(260, 0) : Origin(nodes) + new Vector2(60, 0);
                var value = new GraphNodeView(this, GraphNodeView.ResultId, at);
                views[GraphNodeView.ResultId] = value;
                nodesLayer.Add(value);
                NodeEvents(value);
            }
            var noteList = graph.Arr("notes");
            if (noteList != null)
                foreach (var note in noteList.Items)
                {
                    var nv = new NoteView(this, note);
                    notes[nv.Id] = nv;
                    notesLayer.Add(nv);
                    nv.RegisterCallback<PointerDownEvent>(e =>
                    {
                        if (e.button != 0 || IsField(e.target as VisualElement)) return;
                        SelectFor(nv.Id, e);
                        e.StopPropagation();
                    });
                }
            Selection.RemoveWhere(id => !views.ContainsKey(id) && !notes.ContainsKey(id) && !absorbed.ContainsKey(id));
            foreach (var pair in views) pair.Value.Selected = Selection.Contains(pair.Key);
            foreach (var pair in notes) pair.Value.Selected = Selection.Contains(pair.Key);
            RefreshDimming();
            ApplyErrors();
            ApplyTypes();
            Redraw();
        }

        // A slot that has no graph yet: a button makes it
        void ShowEmptySlot()
        {
            var make = new Button(() => Window.Edit("Make graph", () => Doc.EnsureGraph(Slot))) { text = "Make this graph", style = { position = Position.Absolute, left = 20, top = 20 } };
            nodesLayer.Add(make);
        }

        static Vector2 Origin(List<JValue> nodes)
        {
            if (nodes.Count == 0) return Vector2.zero;
            return new Vector2(nodes.Min(n => (float)(n.Get("x")?.Number ?? 0)), nodes.Min(n => (float)(n.Get("y")?.Number ?? 0)));
        }

        void NodeEvents(GraphNodeView view)
        {
            view.RegisterCallback<PointerDownEvent>(e =>
            {
                if (IsField(e.target as VisualElement)) return;
                if (e.button == 0) SelectFor(view.Id, e);
                if (e.button == 0 || e.button == 1) e.StopPropagation();
            });
            if (view.Id <= 0) return;
            view.AddManipulator(new ContextualMenuManipulator(e =>
            {
                if (!Selection.Contains(view.Id)) SelectOnly(view.Id);
                e.menu.AppendAction("Edit comment", _ => EditComment(view));
                e.menu.AppendAction("Copy", _ => CopySelection());
                e.menu.AppendAction("Duplicate", _ => Duplicate());
                e.menu.AppendAction("Delete", _ => DeleteSelection());
                e.StopPropagation();
            }));
        }

        // Whether a click is on a field of a node or note, which takes it
        internal static bool IsField(VisualElement e)
        {
            for (; e != null; e = e.parent)
            {
                if (e is TextField || e is Toggle || e is Button || e is BasePopupField<string, string>) return true;
                if (e is GraphNodeView || e is NoteView || e is GraphCanvas) return false;
            }
            return false;
        }

        // Dims what tidec leaves out: statements nothing runs, values nothing reads
        public void RefreshDimming()
        {
            var graph = Graph;
            if (graph == null) return;
            reached = GraphDocument.Reached(graph);
            foreach (var pair in views)
            {
                if (pair.Key < 0) continue;
                pair.Value.RefreshTitle();
                pair.Value.SetDim(!reached.Contains(pair.Key));
                foreach (var p in pair.Value.Ports) p.Connected = IsConnected(graph, p);
                foreach (var p in pair.Value.Ports) p.MarkDirtyRepaint();
            }
            Redraw();
        }

        bool IsConnected(JValue graph, PortView p)
        {
            var port = p.Port;
            if (port.Node == GraphNodeView.EntryId) return graph.Wire("entry") != 0;
            if (port.Node == GraphNodeView.ResultId) return graph.Wire("root") != 0;
            var n = GraphDocument.NodeIn(graph, port.Node);
            if (n == null) return false;
            if (port.Path != null) return port.Path.Get(n) != 0;
            if (port.Exec) return GraphDocument.ExecSourceOf(graph, port.Node).node != 0 || graph.Wire("entry") == port.Node;
            return GraphDocument.Nodes(graph).Items.Any(o => GraphPorts.DataWires(o).Contains(port.Node)) || graph.Wire("root") == port.Node;
        }

        public void MarkDiagnostics(IEnumerable<GraphDiagnostic> diagnostics)
        {
            errors.Clear();
            foreach (var d in diagnostics)
            {
                if (d.Id == 0) continue;
                var id = absorbed.TryGetValue(d.Id, out var eval) ? eval : d.Id;
                errors[id] = errors.TryGetValue(id, out var said) ? said + "\n" + d.Message : $"{d.Severity}: {d.Message}";
            }
            ApplyErrors();
        }

        void ApplyErrors()
        {
            foreach (var pair in views) pair.Value.SetError(errors.TryGetValue(pair.Key, out var m) ? m : null);
        }

        // The type of each node's value, as the language server checked it: on its output, on the inputs it's
        // wired to and on the wires between them, in its type's colour, and in its node's tooltip
        IReadOnlyDictionary<int, string> types = new Dictionary<int, string>();

        public void MarkTypes(IReadOnlyDictionary<int, string> checkedTypes)
        {
            types = checkedTypes ?? new Dictionary<int, string>();
            foreach (var view in views.Values) view.ForgetHover();
            ApplyTypes();
            Redraw();
        }

        public string TypeOf(int id)
        {
            if (types.TryGetValue(id, out var t)) return t;
            // A statement whose value is kept: its value's
            var n = GraphDocument.NodeIn(Graph, id);
            return n != null && n.Str("k") == "eval" && types.TryGetValue(n.Wire("value"), out t) ? t : null;
        }

        void ApplyTypes()
        {
            var graph = Graph;
            if (graph == null) return;
            foreach (var view in views.Values)
            {
                if (view.Id <= 0) continue;
                view.SetType(TypeOf(view.Call != null ? view.Call.Wire("id") : view.Id));
                foreach (var port in view.Ports)
                {
                    var p = port.Port;
                    if (p.Exec) continue;
                    var n = GraphDocument.NodeIn(graph, p.Node);
                    var from = p.Input ? (n != null && p.Path != null ? p.Path.Get(n) : 0) : p.Node == view.Id && view.Call != null ? view.Call.Wire("id") : p.Node;
                    port.SetType(from != 0 ? TypeOf(from) : null);
                }
            }
        }

        // A wire from a value: its type's colour
        internal Color WireColor(int from, bool dim)
        {
            var c = TideGraphTypes.ColorOf(TypeOf(from));
            c.a = dim ? 0.35f : 0.9f;
            return c;
        }

        // ---------------------------------------------------------------------------------------------------------
        // View

        void ApplyTransform()
        {
            content.style.translate = new Translate(pan.x, pan.y);
            content.style.scale = new Scale(new Vector3(zoom, zoom, 1));
            grid.MarkDirtyRepaint();
        }

        public void Redraw()
        {
            wires.MarkDirtyRepaint();
            grid.MarkDirtyRepaint();
        }

        public Vector2 ToGraph(Vector2 canvasPoint) => (canvasPoint - pan) / zoom;
        public Vector2 ToCanvas(Vector2 graphPoint) => graphPoint * zoom + pan;

        void DrawGrid(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = grid.contentRect;
            if (r.width <= 0 || r.height <= 0) return;
            void Lines(float step, Color c)
            {
                var s = step * zoom;
                if (s < 6) return;
                p.strokeColor = c;
                p.lineWidth = 1;
                p.BeginPath();
                for (var x = Mod(pan.x, s); x < r.width; x += s)
                {
                    p.MoveTo(new Vector2(x, 0));
                    p.LineTo(new Vector2(x, r.height));
                }
                for (var y = Mod(pan.y, s); y < r.height; y += s)
                {
                    p.MoveTo(new Vector2(0, y));
                    p.LineTo(new Vector2(r.width, y));
                }
                p.Stroke();
            }
            Lines(20, new Color(1, 1, 1, 0.035f));
            Lines(100, new Color(1, 1, 1, 0.07f));
        }

        static float Mod(float a, float b) => ((a % b) + b) % b;

        void OnWheel(WheelEvent e)
        {
            var at = this.WorldToLocal(e.mousePosition);
            var g = ToGraph(at);
            zoom = Mathf.Clamp(zoom * (1 - e.delta.y * 0.04f), 0.15f, 2.5f);
            pan = at - g * zoom;
            ApplyTransform();
            e.StopPropagation();
        }

        // Bounds of nodes, in the graph's space
        Rect Bounds(IEnumerable<int> ids)
        {
            var any = false;
            var b = new Rect();
            foreach (var id in ids)
            {
                Rect r;
                if (views.TryGetValue(id, out var v))
                {
                    var size = new Vector2(float.IsNaN(v.layout.width) || v.layout.width <= 0 ? 160 : v.layout.width, float.IsNaN(v.layout.height) || v.layout.height <= 0 ? 80 : v.layout.height);
                    r = new Rect(v.At, size);
                }
                else if (notes.TryGetValue(id, out var nv)) r = nv.Rect;
                else continue;
                b = any ? Rect.MinMaxRect(Mathf.Min(b.xMin, r.xMin), Mathf.Min(b.yMin, r.yMin), Mathf.Max(b.xMax, r.xMax), Mathf.Max(b.yMax, r.yMax)) : r;
                any = true;
            }
            return any ? b : new Rect(0, 0, 0, 0);
        }

        public void Frame(IEnumerable<int> ids)
        {
            var b = Bounds(ids);
            var size = new Vector2(float.IsNaN(layout.width) || layout.width <= 0 ? 800 : layout.width, float.IsNaN(layout.height) || layout.height <= 0 ? 500 : layout.height);
            if (b.width <= 0 && b.height <= 0)
            {
                zoom = 1;
                pan = size / 2;
                ApplyTransform();
                return;
            }
            b.xMin -= 40;
            b.yMin -= 40;
            b.xMax += 40;
            b.yMax += 40;
            zoom = Mathf.Clamp(Mathf.Min(size.x / b.width, size.y / b.height), 0.15f, 1f);
            pan = size / 2 - b.center * zoom;
            ApplyTransform();
        }

        public void FrameAll() => Frame(views.Keys.Concat(notes.Keys).ToList());

        public void FrameSelectionOrAll()
        {
            if (Selection.Count > 0) Frame(Selection.ToList());
            else FrameAll();
        }

        // A layout asked of tidels that hasn't come back
        internal bool LayoutPending { get; private set; }

        // Lays out the graph shown as tidec does, with tidels's layout; without tidels the nodes stay where they are
        public void AutoLayout()
        {
            var graph = Graph;
            if (graph == null) return;
            var slot = Window.Slot;
            LayoutPending = true;
            TideGraphLanguage.Layout(graph, (layout, why) =>
            {
                LayoutPending = false;
                if (Window == null || Window.Canvas != this) return;
                if (layout == null)
                {
                    Window.ShowNotification(new GUIContent($"Auto layout is tidels's, which can't now ({why}): the nodes stay where they are"));
                    return;
                }
                var now = Graph;
                if (now == null || !Window.Slot.Equals(slot)) return;
                Window.Edit("Auto layout", () => TideGraphLanguage.Place(now, layout), GraphRedraw.Canvas);
                FrameAll();
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Selecting

        public void SelectOnly(int id)
        {
            Selection.Clear();
            Selection.Add(absorbed.TryGetValue(id, out var eval) ? eval : id);
            ShowSelection();
        }

        void ShowSelection()
        {
            foreach (var pair in views) pair.Value.Selected = Selection.Contains(pair.Key);
            foreach (var pair in notes) pair.Value.Selected = Selection.Contains(pair.Key);
        }

        void SelectFor(int id, IPointerEvent e)
        {
            Focus();
            var additive = e.shiftKey || e.actionKey;
            if (additive)
            {
                if (!Selection.Remove(id)) Selection.Add(id);
            }
            else if (!Selection.Contains(id))
            {
                Selection.Clear();
                Selection.Add(id);
            }
            ShowSelection();
            if (id > 0 && Selection.Count == 1) Window.Select(id, false);
            if (!additive) BeginMove(e);
        }

        public void SelectAll()
        {
            Selection.Clear();
            foreach (var id in views.Keys) if (id > 0) Selection.Add(id);
            foreach (var id in notes.Keys) Selection.Add(id);
            ShowSelection();
        }

        // The ids selected with the calls drawn with them
        HashSet<int> SelectedNodes()
        {
            var ids = new HashSet<int>(Selection.Where(id => id > 0));
            foreach (var pair in absorbed) if (ids.Contains(pair.Value)) ids.Add(pair.Key);
            return ids;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Dragging: moving, panning, box selecting, wiring

        enum Drag { None, Pan, Box, Move, Wire, Note, Resize }

        Drag drag;
        int dragPointer = -1;
        Vector2 dragStart;          // In the canvas's space
        Vector2 panStart;
        readonly Dictionary<int, Vector2> moveStart = new Dictionary<int, Vector2>();
        readonly Dictionary<int, Rect> noteStart = new Dictionary<int, Rect>();
        NoteView dragNote;
        bool moved, rightOnCanvas;
        HashSet<int> boxBefore;

        // The wire being drawn: the end that stays, and what was wired before it was taken up
        PortView wireFrom;
        internal Vector2 WireEnd;   // In the graph's space
        Action wireUndo;            // Unwires what was taken up, when it's dropped on nothing

        internal PortView WireFrom => drag == Drag.Wire ? wireFrom : null;

        void Capture(IPointerEvent e)
        {
            dragPointer = e.pointerId;
            this.CapturePointer(e.pointerId);
        }

        void Release()
        {
            if (dragPointer >= 0 && this.HasPointerCapture(dragPointer)) this.ReleasePointer(dragPointer);
            dragPointer = -1;
            drag = Drag.None;
        }

        void OnDown(PointerDownEvent e)
        {
            if (IsField(e.target as VisualElement)) return;
            Focus();
            mouse = this.WorldToLocal(e.position);
            if (e.button == 2 || (e.button == 0 && e.altKey))
            {
                drag = Drag.Pan;
                dragStart = mouse;
                panStart = pan;
                Capture(e);
                e.StopPropagation();
                return;
            }
            if (e.button == 0 && Graph != null)
            {
                drag = Drag.Box;
                dragStart = mouse;
                boxBefore = e.shiftKey || e.actionKey ? new HashSet<int>(Selection) : new HashSet<int>();
                if (boxBefore.Count == 0)
                {
                    Selection.Clear();
                    ShowSelection();
                }
                Capture(e);
                e.StopPropagation();
                return;
            }
            if (e.button == 1 && Graph != null)
            {
                dragStart = mouse;
                rightOnCanvas = true;
                e.StopPropagation();
            }
        }

        void BeginMove(IPointerEvent e)
        {
            mouse = this.WorldToLocal(e.position);
            drag = Drag.Move;
            moved = false;
            dragStart = mouse;
            moveStart.Clear();
            noteStart.Clear();
            foreach (var id in Selection)
            {
                if (views.TryGetValue(id, out var v)) moveStart[id] = v.At;
                if (notes.TryGetValue(id, out var nv)) noteStart[id] = nv.Rect;
            }
            Capture(e);
        }

        public void BeginNoteDrag(NoteView note, IPointerEvent e, bool resize)
        {
            if (!resize)
            {
                SelectFor(note.Id, e);
                return;
            }
            Focus();
            mouse = this.WorldToLocal(e.position);
            drag = Drag.Resize;
            dragNote = note;
            moved = false;
            dragStart = mouse;
            noteStart.Clear();
            noteStart[note.Id] = note.Rect;
            Capture(e);
        }

        void OnMove(PointerMoveEvent e)
        {
            mouse = this.WorldToLocal(e.position);
            switch (drag)
            {
                case Drag.Pan:
                    pan = panStart + (mouse - dragStart);
                    ApplyTransform();
                    break;
                case Drag.Box:
                {
                    var r = Rect.MinMaxRect(Mathf.Min(dragStart.x, mouse.x), Mathf.Min(dragStart.y, mouse.y), Mathf.Max(dragStart.x, mouse.x), Mathf.Max(dragStart.y, mouse.y));
                    selectRect.style.display = DisplayStyle.Flex;
                    selectRect.style.left = r.x;
                    selectRect.style.top = r.y;
                    selectRect.style.width = r.width;
                    selectRect.style.height = r.height;
                    var g = Rect.MinMaxRect(ToGraph(r.min).x, ToGraph(r.min).y, ToGraph(r.max).x, ToGraph(r.max).y);
                    Selection.Clear();
                    Selection.UnionWith(boxBefore);
                    foreach (var pair in views)
                        if (pair.Key > 0 && g.Overlaps(new Rect(pair.Value.At, new Vector2(pair.Value.layout.width, pair.Value.layout.height)))) Selection.Add(pair.Key);
                    foreach (var pair in notes)
                        if (g.Overlaps(pair.Value.Rect)) Selection.Add(pair.Key);
                    ShowSelection();
                    break;
                }
                case Drag.Move:
                {
                    var d = (mouse - dragStart) / zoom;
                    if (d.sqrMagnitude > 1) moved = true;
                    foreach (var pair in moveStart) views[pair.Key].Place(pair.Value + d);
                    foreach (var pair in noteStart)
                    {
                        var r = pair.Value;
                        notes[pair.Key].Place(new Rect(r.position + d, r.size));
                    }
                    Redraw();
                    break;
                }
                case Drag.Resize:
                {
                    var d = (mouse - dragStart) / zoom;
                    moved = true;
                    var r = noteStart[dragNote.Id];
                    dragNote.Place(new Rect(r.position, r.size + d));
                    break;
                }
                case Drag.Wire:
                    WireEnd = ToGraph(mouse);
                    wires.MarkDirtyRepaint();
                    break;
            }
        }

        void OnUp(PointerUpEvent e)
        {
            mouse = this.WorldToLocal(e.position);
            var was = drag;
            Release();
            switch (was)
            {
                case Drag.Box:
                    selectRect.style.display = DisplayStyle.None;
                    if (Selection.Count == 1 && Selection.First() > 0) Window.Select(Selection.First(), false);
                    break;
                case Drag.Move:
                    if (moved) CommitMove();
                    break;
                case Drag.Resize:
                    if (moved) CommitMove();
                    break;
                case Drag.Wire:
                    DropWire(e);
                    break;
                case Drag.None:
                    if (e.button == 1 && rightOnCanvas && Graph != null && (mouse - dragStart).sqrMagnitude < 16) OpenSearch(mouse, null);
                    rightOnCanvas = false;
                    break;
            }
        }

        void CommitMove()
        {
            var graph = Graph;
            var moves = moveStart.Keys.Where(id => id > 0).ToDictionary(id => id, id => views[id].At);
            var starts = new Dictionary<int, Vector2>(moveStart);
            var sized = noteStart.Keys.ToDictionary(id => id, id => notes[id].Rect);
            Window.Edit("Move", () =>
            {
                foreach (var pair in moves)
                {
                    var n = GraphDocument.NodeIn(graph, pair.Key);
                    if (n == null) continue;
                    var d = pair.Value - starts[pair.Key];
                    n.Set("x", JValue.Of(Mathf.Round(pair.Value.x)));
                    n.Set("y", JValue.Of(Mathf.Round(pair.Value.y)));
                    // A call drawn with its statement moves with it
                    foreach (var a in absorbed.Where(a => a.Value == pair.Key))
                    {
                        var c = GraphDocument.NodeIn(graph, a.Key);
                        if (c == null) continue;
                        c.Set("x", JValue.Of(Mathf.Round((float)(c.Get("x")?.Number ?? 0) + d.x)));
                        c.Set("y", JValue.Of(Mathf.Round((float)(c.Get("y")?.Number ?? 0) + d.y)));
                    }
                }
                var list = graph.Arr("notes");
                foreach (var pair in sized)
                {
                    var note = list?.Items.FirstOrDefault(x => x.Wire("id") == pair.Key);
                    if (note == null) continue;
                    note.Set("x", JValue.Of(Mathf.Round(pair.Value.x)), GraphSchema.NoteOrder);
                    note.Set("y", JValue.Of(Mathf.Round(pair.Value.y)), GraphSchema.NoteOrder);
                    note.Set("w", JValue.Of(Mathf.Round(pair.Value.width)), GraphSchema.NoteOrder);
                    note.Set("h", JValue.Of(Mathf.Round(pair.Value.height)), GraphSchema.NoteOrder);
                }
            }, GraphRedraw.None);
        }

        // A drag from a port: a new wire from it, or the wire it has taken up from its other end
        public void BeginWire(PortView port, IPointerEvent e)
        {
            Focus();
            var graph = Graph;
            if (graph == null) return;
            if (e.altKey)
            {
                Unwire(port);
                return;
            }
            mouse = this.WorldToLocal(e.position);
            wireFrom = port;
            wireUndo = null;
            var p = port.Port;
            // A wired input: taken up from its value's output
            if (p.Input && !p.Exec && port.Connected)
            {
                var from = Wired(graph, p);
                var source = ViewOf(from)?.DataOut;
                if (source != null)
                {
                    wireFrom = source;
                    wireUndo = () => SetInput(graph, p, 0);
                }
            }
            // A statement's input wired from somewhere: taken up from there
            else if (p.Input && p.Exec && port.Connected)
            {
                if (graph.Wire("entry") == p.Node)
                {
                    wireFrom = views[GraphNodeView.EntryId].Ports[0];
                    wireUndo = () => graph.Set("entry", JValue.Null());
                }
                else
                {
                    var (node, path) = GraphDocument.ExecSourceOf(graph, p.Node);
                    var source = ViewOf(node)?.PortAt(node, path, false, true);
                    if (source != null)
                    {
                        wireFrom = source;
                        wireUndo = () => path.Set(GraphDocument.NodeIn(graph, node), 0);
                    }
                }
            }
            drag = Drag.Wire;
            WireEnd = ToGraph(mouse);
            Capture(e);
            wires.MarkDirtyRepaint();
        }

        GraphNodeView ViewOf(int id)
        {
            if (views.TryGetValue(id, out var v)) return v;
            return absorbed.TryGetValue(id, out var eval) && views.TryGetValue(eval, out var ev) ? ev : null;
        }

        static int Wired(JValue graph, GraphPort p)
        {
            if (p.Node == GraphNodeView.ResultId) return graph.Wire("root");
            if (p.Node == GraphNodeView.EntryId) return graph.Wire("entry");
            var n = GraphDocument.NodeIn(graph, p.Node);
            return n != null && p.Path != null ? p.Path.Get(n) : 0;
        }

        static void SetInput(JValue graph, GraphPort p, int id)
        {
            if (p.Node == GraphNodeView.ResultId) graph.Set("root", id != 0 ? JValue.Of(id) : JValue.Null(), GraphSchema.ValueOrder);
            else if (p.Node == GraphNodeView.EntryId) graph.Set("entry", id != 0 ? JValue.Of(id) : JValue.Null(), GraphSchema.BodyOrder);
            else
            {
                var n = GraphDocument.NodeIn(graph, p.Node);
                if (n != null) p.Path.Set(n, id);
            }
        }

        // Alt and click on a port: every wire it has goes
        void Unwire(PortView port)
        {
            var graph = Graph;
            var p = port.Port;
            Window.Edit("Unwire", () =>
            {
                if (p.Path != null) SetInput(graph, p, 0);
                else if (p.Input && p.Exec)
                {
                    if (graph.Wire("entry") == p.Node) graph.Set("entry", JValue.Null());
                    var (node, path) = GraphDocument.ExecSourceOf(graph, p.Node);
                    if (node != 0) path.Set(GraphDocument.NodeIn(graph, node), 0);
                }
                else
                {
                    foreach (var n in GraphDocument.Nodes(graph).Items)
                        foreach (var q in GraphPorts.Of(n))
                            if (q.Path != null && !q.Exec && q.Path.Get(n) == p.Node) q.Path.Set(n, 0);
                    if (graph.Wire("root") == p.Node) graph.Set("root", JValue.Null());
                }
            }, GraphRedraw.Canvas | GraphRedraw.Inspector);
        }

        PortView PortUnder(Vector2 worldPosition)
        {
            for (var e = panel?.Pick(worldPosition); e != null; e = e.parent)
                if (e is PortView p) return p;
            // Near a port is on it
            PortView best = null;
            var bestD = 14f * 14f;
            foreach (var v in views.Values)
                foreach (var p in v.Ports)
                {
                    var d = (p.worldBound.center - worldPosition).sqrMagnitude;
                    if (d < bestD)
                    {
                        bestD = d;
                        best = p;
                    }
                }
            return best;
        }

        void DropWire(IPointerEvent e)
        {
            var from = wireFrom;
            var undo = wireUndo;
            wireFrom = null;
            wireUndo = null;
            var graph = Graph;
            var target = PortUnder(e.position);
            if (target != null && target != from)
            {
                Window.Edit("Wire", () =>
                {
                    undo?.Invoke();
                    if (!Join(graph, from.Port, target.Port, out var why)) Window.ShowNotification(new GUIContent(why));
                }, GraphRedraw.Canvas | GraphRedraw.Inspector);
                return;
            }
            if (undo != null)
            {
                // Dropped on nothing: the wire it took up goes
                Window.Edit("Unwire", undo, GraphRedraw.Canvas | GraphRedraw.Inspector);
                return;
            }
            OpenSearch(this.WorldToLocal(e.position), from.Port);
        }

        // Wires two ports as a drag from one to the other does: false, with why, when they can't be
        internal bool Connect(GraphPort a, GraphPort b, out string why)
        {
            var graph = Graph;
            var done = false;
            string said = null;
            Window.Edit("Wire", () => done = Join(graph, a, b, out said), GraphRedraw.Canvas | GraphRedraw.Inspector);
            why = said;
            return done;
        }

        // Wires two ports, whichever way round they were dragged
        bool Join(JValue graph, GraphPort a, GraphPort b, out string why)
        {
            why = null;
            if (a.Input == b.Input || a.Exec != b.Exec)
            {
                why = a.Exec != b.Exec ? "A statement's flow joins a statement, a value joins a value's input" : "A wire joins an output to an input";
                return false;
            }
            var output = a.Input ? b : a;
            var input = a.Input ? a : b;
            if (output.Exec)
            {
                if (output.Node == GraphNodeView.EntryId)
                {
                    if (GraphDocument.ExecSourceOf(graph, input.Node).node != 0)
                    {
                        why = "a statement runs from one place only: copy it for the other";
                        return false;
                    }
                    graph.Set("entry", JValue.Of(input.Node), GraphSchema.BodyOrder);
                    return true;
                }
                return GraphDocument.Connect(graph, output.Node, output.Path, input.Node, out why);
            }
            if (input.Node == GraphNodeView.ResultId)
            {
                graph.Set("root", JValue.Of(output.Node), GraphSchema.ValueOrder);
                return true;
            }
            return GraphDocument.Connect(graph, input.Node, input.Path, output.Node, out why);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Adding nodes

        public void OpenSearch(Vector2 at, GraphPort pending)
        {
            CloseSearch();
            var graph = Graph;
            if (graph == null) return;
            search = new GraphSearchPopup(this, new GraphSearchContext { Doc = Doc, Graph = graph, Slot = Slot, Window = Window, Pending = pending }, at, pending);
            Add(search);
            search.Begin();
        }

        // The search for the member a member or method node is: its object's members, as the language server knows them
        public void OpenMemberSearch(int node, Vector2 at)
        {
            CloseSearch();
            var graph = Graph;
            if (graph == null || GraphDocument.NodeIn(graph, node) == null) return;
            search = new GraphSearchPopup(this, new GraphSearchContext { Doc = Doc, Graph = graph, Slot = Slot, Window = Window, Retarget = node }, at, null);
            Add(search);
            search.Begin();
        }

        // Makes a member or method node the member a search picked for it
        public void Retarget(GraphSearchEntry entry, int node)
        {
            CloseSearch();
            var graph = Graph;
            var n = GraphDocument.NodeIn(graph, node);
            if (n == null || entry.Apply == null) return;
            Window.Edit("Pick " + entry.Title, () => entry.Apply(n));
            SelectOnly(node);
        }

        public void CloseSearch()
        {
            search?.RemoveFromHierarchy();
            search = null;
        }

        // Makes what a search entry makes at a place in the canvas, wired to the port a wire was dropped from
        public void Insert(GraphSearchEntry entry, Vector2 canvasPoint, GraphPort pending)
        {
            CloseSearch();
            var graph = Graph;
            var at = ToGraph(canvasPoint);
            JValue made = null;
            Window.Edit("Add " + entry.Title, () =>
            {
                made = entry.Make(Doc, graph, at);
                if (made != null && pending != null) WireNew(graph, made, pending);
            });
            if (made != null)
            {
                SelectOnly(made.Wire("id"));
                Window.Select(made.Wire("id"), false);
            }
        }

        // A new node wired to where its wire came from: its first input that suits, or its output, or its flow
        void WireNew(JValue graph, JValue made, GraphPort pending)
        {
            var id = made.Wire("id");
            var ports = GraphPorts.Of(made);
            if (made.Str("k") == "eval" && made.Wire("value") != 0)
            {
                // A call statement: its call's inputs are its inputs
                var call = GraphDocument.NodeIn(graph, made.Wire("value"));
                if (call != null) ports = GraphPorts.Of(call).Concat(ports.Where(p => !(p.Path != null && p.Path.Key == "value"))).ToList();
            }
            GraphPort match = null;
            if (!pending.Input) match = ports.FirstOrDefault(p => p.Input && p.Exec == pending.Exec && (p.Path == null || p.Path.Get(GraphDocument.NodeIn(graph, p.Node)) == 0));
            else match = ports.FirstOrDefault(p => !p.Input && p.Exec == pending.Exec && (p.Exec ? p.Path?.Key == "next" : true));
            if (match == null) return;
            Join(graph, pending, match, out _);
        }

        public void AddNote(Vector2 canvasPoint)
        {
            var graph = Graph;
            if (graph == null) return;
            var at = ToGraph(canvasPoint);
            Window.Edit("Add note", () =>
            {
                var list = graph.Arr("notes");
                if (list == null) graph.Set("notes", list = JValue.NewArray(), Slot.IsBody ? GraphSchema.BodyOrder : GraphSchema.ValueOrder);
                var note = JValue.NewObject();
                note.Set("id", JValue.Of(Doc.NewId()));
                note.Set("x", JValue.Of(Mathf.Round(at.x)));
                note.Set("y", JValue.Of(Mathf.Round(at.y)));
                note.Set("w", JValue.Of(300));
                note.Set("h", JValue.Of(120));
                note.Set("text", JValue.Of("Note"));
                list.Items.Add(note);
            }, GraphRedraw.Canvas);
        }

        public void EditComment(GraphNodeView view)
        {
            var n = view.Node;
            if (n == null) return;
            var field = new TextField { value = n.Str("comment") ?? "", multiline = true, style = { position = Position.Absolute, left = view.At.x, top = view.At.y - 46, width = 280, height = 42 } };
            nodesLayer.Add(field);
            var done = false;
            void Finish()
            {
                if (done) return;
                done = true;
                var text = field.value;
                field.RemoveFromHierarchy();
                if (text != (n.Str("comment") ?? "")) Window.Edit("Comment", () => n.SetString("comment", text, GraphSchema.NodeOrder(n.Str("k"))), GraphRedraw.Canvas | GraphRedraw.Inspector);
            }
            field.RegisterCallback<FocusOutEvent>(_ => Finish());
            field.schedule.Execute(() => field.Focus());
        }

        // ---------------------------------------------------------------------------------------------------------
        // Keys and the clipboard

        void OnKey(KeyDownEvent e)
        {
            if (search != null || e.target is TextField || (e.target as VisualElement)?.GetFirstAncestorOfType<TextField>() != null) return;
            var action = e.ctrlKey || e.commandKey;
            switch (e.keyCode)
            {
                case KeyCode.Delete:
                case KeyCode.Backspace:
                    DeleteSelection();
                    break;
                case KeyCode.D when action:
                    Duplicate();
                    break;
                case KeyCode.C when action:
                    CopySelection();
                    break;
                case KeyCode.X when action:
                    CopySelection();
                    DeleteSelection();
                    break;
                case KeyCode.V when action:
                    Paste(mouse);
                    break;
                case KeyCode.A when action:
                    SelectAll();
                    break;
                case KeyCode.F:
                    FrameSelectionOrAll();
                    break;
                case KeyCode.Space:
                    OpenSearch(mouse, null);
                    break;
                case KeyCode.Escape:
                    Selection.Clear();
                    ShowSelection();
                    break;
                default:
                    return;
            }
            e.StopPropagation();
        }

        public void DeleteSelection()
        {
            var graph = Graph;
            if (graph == null) return;
            var ids = SelectedNodes();
            if (ids.Count == 0) return;
            Window.Edit("Delete", () => Doc.DeleteNodes(graph, ids));
            Selection.Clear();
        }

        public void CopySelection()
        {
            var graph = Graph;
            if (graph == null) return;
            var ids = SelectedNodes();
            EditorGUIUtility.systemCopyBuffer = GraphDocument.Copy(graph, ids, new HashSet<int>(Selection.Where(notes.ContainsKey)));
        }

        public void Paste(Vector2 canvasPoint)
        {
            var graph = Graph;
            var text = EditorGUIUtility.systemCopyBuffer;
            if (graph == null || !GraphDocument.IsClip(text)) return;
            // The copied nodes' top left goes where the pointer is
            var clip = GraphJson.Parse(text);
            var items = (clip.Arr("nodes")?.Items ?? new List<JValue>()).Concat(clip.Arr("notes")?.Items ?? new List<JValue>()).ToList();
            var min = items.Count == 0 ? Vector2.zero : new Vector2(items.Min(n => (float)(n.Get("x")?.Number ?? 0)), items.Min(n => (float)(n.Get("y")?.Number ?? 0)));
            var offset = ToGraph(canvasPoint) - min;
            List<int> made = null;
            Window.Edit("Paste", () => made = Doc.Paste(graph, text, offset));
            if (made == null) return;
            Selection.Clear();
            Selection.UnionWith(made.Where(id => views.ContainsKey(id) || notes.ContainsKey(id)));
            ShowSelection();
        }

        public void Duplicate()
        {
            var graph = Graph;
            if (graph == null || Selection.Count == 0) return;
            var text = GraphDocument.Copy(graph, SelectedNodes(), new HashSet<int>(Selection.Where(notes.ContainsKey)));
            List<int> made = null;
            Window.Edit("Duplicate", () => made = Doc.Paste(graph, text, new Vector2(40, 40)));
            if (made == null) return;
            Selection.Clear();
            Selection.UnionWith(made.Where(id => views.ContainsKey(id) || notes.ContainsKey(id)));
            ShowSelection();
        }
    }

    // The wires: from each value's output to the inputs that read it, and from each statement's flow to the next.
    // Drawn as curves, dimmer where what they join is left out.
    sealed class WireLayer : VisualElement
    {
        const float Far = 50000;
        readonly GraphCanvas canvas;

        public WireLayer(GraphCanvas canvas)
        {
            this.canvas = canvas;
            pickingMode = PickingMode.Ignore;
            style.position = Position.Absolute;
            style.left = -Far;
            style.top = -Far;
            style.width = Far * 2;
            style.height = Far * 2;
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var graph = canvas.Graph;
            if (graph == null) return;
            var p = ctx.painter2D;
            foreach (var view in canvas.Views.Values)
            {
                foreach (var port in view.Ports)
                {
                    var g = port.Port;
                    if (g.Path == null) continue;
                    int wired;
                    if (g.Node == GraphNodeView.EntryId) wired = graph.Wire("entry");
                    else if (g.Node == GraphNodeView.ResultId) wired = graph.Wire("root");
                    else
                    {
                        var n = GraphDocument.NodeIn(graph, g.Node);
                        wired = n != null ? g.Path.Get(n) : 0;
                    }
                    if (wired == 0) continue;
                    var other = canvas.Views.TryGetValue(wired, out var ov) ? ov : canvas.Absorbed.TryGetValue(wired, out var ev) && canvas.Views.TryGetValue(ev, out var evv) ? evv : null;
                    if (other == null || other == view) continue;
                    if (g.Input && !g.Exec)
                    {
                        var from = other.DataOut;
                        if (from == null) continue;
                        Curve(p, from.Anchor(this), port.Anchor(this), canvas.WireColor(wired, view.resolvedStyle.opacity < 1), 2);
                    }
                    else if (!g.Input && g.Exec)
                    {
                        var to = other.ExecIn;
                        if (to == null) continue;
                        Curve(p, port.Anchor(this), to.Anchor(this), new Color(1, 1, 1, other.resolvedStyle.opacity < 1 ? 0.35f : 0.95f), 2.5f);
                    }
                }
            }
            var dragging = canvas.WireFrom;
            if (dragging != null)
            {
                var end = canvas.WireEnd + new Vector2(Far, Far);
                var start = dragging.Anchor(this);
                if (dragging.Port.Input) Curve(p, end, start, new Color(1, 0.85f, 0.3f), 2);
                else Curve(p, start, end, new Color(1, 0.85f, 0.3f), 2);
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
}
