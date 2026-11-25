using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The board: a file as one canvas (notes/visual-scripting.md, "The experience: one board"). Every declaration is
    // on it where its x, y say: a card for data, a frame for code, a tinted region around a namespace block's. It
    // pans (the middle button, or Alt and drag) and zooms (the wheel, about the pointer).
    //
    // Only what's on screen is drawn: an item off screen is an empty box of its size, and from far away an item is
    // its name. An edit builds again the card or the nodes it changed, and nothing else.
    //
    // No click does nothing: a right click or Space on the empty board opens what to add; in a frame, the search
    // for a node of that frame; a wire dropped in its frame, the same search for that wire. A wire can't leave its
    // frame, and the board says why.
    sealed class GraphBoard : VisualElement
    {
        public readonly TideGraphWindow Window;
        readonly VisualElement grid, content, spacesLayer, itemsLayer, valuesLayer, selectBox;
        readonly Label hint;
        VisualElement start;
        readonly Dictionary<int, GraphItem> items = new Dictionary<int, GraphItem>();
        readonly Dictionary<GraphSlot, GraphFrame> values = new Dictionary<GraphSlot, GraphFrame>();
        readonly List<GraphSlot> open = new List<GraphSlot>();
        readonly List<GraphSpace> spaces = new List<GraphSpace>();
        Vector2 pan = new Vector2(40, 40);
        float zoom = 1, farBuiltAt = 1;
        Vector2 mouse; // In the board's space

        // What's selected: nodes (and notes) of one frame, or cards and frames
        public GraphFrame Active { get; private set; }
        public readonly HashSet<int> Nodes = new HashSet<int>();
        public readonly HashSet<int> Chosen = new HashSet<int>();
        // What was just made, while its name is typed: Enter goes on to what comes next, Escape takes it back
        public int Fresh;

        public GraphBoard(TideGraphWindow window)
        {
            Window = window;
            AddToClassList("tg-board");
            focusable = true;
            grid = new VisualElement { pickingMode = PickingMode.Ignore };
            grid.AddToClassList("tg-fill");
            grid.generateVisualContent += DrawGrid;
            Add(grid);
            content = new VisualElement { pickingMode = PickingMode.Ignore, style = { position = Position.Absolute, left = 0, top = 0, width = 1, height = 1, transformOrigin = new TransformOrigin(0, 0, 0) } };
            Add(content);
            spacesLayer = Layer();
            itemsLayer = Layer();
            valuesLayer = Layer();
            selectBox = new VisualElement { pickingMode = PickingMode.Ignore };
            selectBox.AddToClassList("tg-select-box");
            selectBox.style.display = DisplayStyle.None;
            Add(selectBox);
            hint = GraphLook.Text("", "tg-hint");
            hint.pickingMode = PickingMode.Ignore;
            Add(hint);

            RegisterCallback<WheelEvent>(OnWheel);
            RegisterCallback<PointerDownEvent>(OnDown);
            RegisterCallback<PointerMoveEvent>(OnMove);
            RegisterCallback<PointerUpEvent>(OnUp);
            RegisterCallback<KeyDownEvent>(OnKey);
            RegisterCallback<GeometryChangedEvent>(_ => Cull());
            ApplyTransform();
        }

        VisualElement Layer()
        {
            var layer = new VisualElement { pickingMode = PickingMode.Ignore };
            layer.AddToClassList("tg-layer");
            content.Add(layer);
            return layer;
        }

        GraphDocument Doc => Window.Doc;
        public bool Light => Window.Light;
        public float Zoom => zoom;
        internal Vector2 Pan => pan;
        public IEnumerable<GraphItem> Items => items.Values;
        public IEnumerable<GraphFrame> ValueFrames => values.Values;
        public GraphItem Item(int id) => items.TryGetValue(id, out var i) ? i : null;
        public GraphCard Card(int id) => Item(id) as GraphCard;
        public GraphFrame Frame(int id) => Item(id) as GraphFrame;
        public GraphFrame ValueFrame(GraphSlot slot) => values.TryGetValue(slot, out var f) ? f : null;
        internal IReadOnlyList<GraphSpace> Spaces => spaces;

        // The frame a node (or a note) is in
        public GraphFrame FrameOf(int node)
        {
            var e = Doc?.Find(node);
            if (e == null || (e.Role != GraphRole.Node && e.Role != GraphRole.Note)) return null;
            return e.Slot.IsBody ? Frame(e.Slot.Owner) : ValueFrame(e.Slot);
        }

        // The card or frame that shows an element: itself, or what it's a part of
        public GraphItem ItemOf(int id)
        {
            for (var e = Doc?.Find(id); e != null;)
            {
                if ((e.Role == GraphRole.Node || e.Role == GraphRole.Note) && FrameOf(e.Id) is GraphFrame frame) return frame;
                if (items.TryGetValue(e.Id, out var item)) return item;
                var owner = e.Role == GraphRole.Node || e.Role == GraphRole.Note ? e.Slot.Owner : e.Owner?.Wire("id") ?? 0;
                e = owner != 0 && owner != e.Id ? Doc.Find(owner) : null;
            }
            return null;
        }

        // ---------------------------------------------------------------------------------------------------------
        // What's on the board

        static bool IsCode(string kind, bool member) => GraphSchema.HasBody(kind, member);

        void Collect(JValue decls, List<(int id, bool frame)> wants, List<JValue> blocks)
        {
            if (decls == null) return;
            foreach (var d in decls.Items)
            {
                if (!d.IsObject) continue;
                var kind = d.Str("kind");
                var id = d.Wire("id");
                if (id == 0) continue;
                if (kind == "namespace")
                {
                    blocks.Add(d);
                    Collect(d.Arr("decls"), wants, blocks);
                    continue;
                }
                wants.Add((id, IsCode(kind, false)));
                var members = d.Arr("members");
                if (members == null || kind == "enum") continue;
                foreach (var m in members.Items)
                {
                    if (!m.IsObject) continue;
                    if (IsCode(m.Str("kind"), true) && m.Wire("id") != 0) wants.Add((m.Wire("id"), true));
                    var accessors = m.Arr("accessors");
                    if (accessors != null) foreach (var a in accessors.Items) if (a.Wire("id") != 0) wants.Add((a.Wire("id"), true));
                }
            }
        }

        // Makes the board's items those of the file: new ones made, gone ones taken away, each where it goes
        public void Sync()
        {
            if (Doc == null) return;
            var wants = new List<(int id, bool frame)>();
            var blocks = new List<JValue>();
            Collect(Doc.Root.Arr("decls"), wants, blocks);
            var wanted = new HashSet<int>(wants.Select(w => w.id));
            foreach (var gone in items.Keys.Where(id => !wanted.Contains(id) || items[id] is GraphFrame != wants.First(w => w.id == id).frame).ToList())
            {
                items[gone].RemoveFromHierarchy();
                items.Remove(gone);
            }
            foreach (var (id, frame) in wants)
            {
                if (items.ContainsKey(id)) continue;
                GraphItem item = frame ? new GraphFrame(this, new GraphSlot(id, "body")) : (GraphItem)new GraphCard(this, id);
                items[id] = item;
                itemsLayer.Add(item);
            }
            // The values open beside their cards
            open.RemoveAll(slot => Doc.Find(slot.Owner) == null);
            foreach (var gone in values.Keys.Where(slot => !open.Contains(slot)).ToList())
            {
                values[gone].RemoveFromHierarchy();
                values.Remove(gone);
            }
            foreach (var slot in open)
            {
                if (values.ContainsKey(slot)) continue;
                var frame = new GraphFrame(this, slot);
                values[slot] = frame;
                valuesLayer.Add(frame);
            }
            PlaceAll(wants);
            Regions(blocks);
            Chosen.RemoveWhere(id => !items.ContainsKey(id));
            if (Active != null && Active.parent == null) Active = null;
            if (Active == null) Nodes.Clear();
            ShowStart();
            Cull();
        }

        // Each item where the file says; one with no place, beside what's there: data in a column on the left,
        // code to its right, as tidec's layout does
        void PlaceAll(List<(int id, bool frame)> wants)
        {
            float dataY = 0, codeY = 0;
            var codeX = GraphLook.CardWidth + GraphLook.LaneGap;
            foreach (var (id, _) in wants)
            {
                var item = items[id];
                var e = item.Element;
                if (!item.Measured) item.Size = item.Estimate();
                item.Placed = e != null && GraphDocument.HasPlace(e.Value);
                if (!item.Placed) continue;
                item.At = GraphDocument.PlaceOf(e.Value);
                if (item is GraphFrame) codeY = Mathf.Max(codeY, item.At.y + item.Size.y + GraphLook.FrameGap);
                else dataY = Mathf.Max(dataY, item.At.y + item.Size.y + GraphLook.CardGap);
            }
            foreach (var (id, frame) in wants)
            {
                var item = items[id];
                if (!item.Placed)
                {
                    if (frame)
                    {
                        item.At = new Vector2(codeX, codeY);
                        codeY += item.Size.y + GraphLook.FrameGap;
                    }
                    else
                    {
                        item.At = new Vector2(0, dataY);
                        dataY += item.Size.y + GraphLook.CardGap;
                    }
                }
            }
            Settle(wants);
            foreach (var (id, _) in wants) items[id].Place();
            foreach (var pair in values) PlaceValue(pair.Value);
        }

        // tidec's layout places by its own estimate of sizes, and what's drawn can be taller (a note, a hint): an
        // item that would lie on the one above it in its column is shown below it. The file's places stay as they
        // are until something's moved.
        void Settle(List<(int id, bool frame)> wants)
        {
            var sorted = wants.Select(w => items[w.id]).OrderBy(i => i.At.y).ThenBy(i => i.At.x).ToList();
            for (var i = 0; i < sorted.Count; i++)
            {
                var item = sorted[i];
                for (var j = 0; j < i; j++)
                {
                    var above = sorted[j];
                    // In a column of tidec's layout (the same x): what a user put somewhere stays where it was put
                    if (Mathf.Abs(above.At.x - item.At.x) > 0.5f) continue;
                    var bottom = above.At.y + above.Size.y + 16;
                    if (item.At.y < bottom && item.At.y + item.Size.y > above.At.y) item.At = new Vector2(item.At.x, bottom);
                }
            }
        }

        void PlaceValue(GraphFrame frame)
        {
            if (!frame.Measured) frame.Size = frame.Estimate();
            var beside = ItemOf(frame.Slot.Owner);
            var at = beside != null ? beside.At + new Vector2(beside.Size.x + 24, 20 + 30 * open.IndexOf(frame.Slot)) : Vector2.zero;
            frame.At = at;
            frame.Place();
        }

        // A namespace block's region: around what's declared in it
        void Regions(List<JValue> blocks)
        {
            foreach (var s in spaces) s.RemoveFromHierarchy();
            spaces.Clear();
            float emptyY = items.Values.Where(i => i is GraphCard).Select(i => i.At.y + i.Size.y + GraphLook.CardGap).DefaultIfEmpty(0).Max();
            // Inner blocks first, so an outer one goes around them
            foreach (var block in Enumerable.Reverse(blocks))
            {
                var space = new GraphSpace(this, block);
                var any = false;
                var r = new Rect();
                void Take(Rect w)
                {
                    r = any ? Rect.MinMaxRect(Mathf.Min(r.xMin, w.xMin), Mathf.Min(r.yMin, w.yMin), Mathf.Max(r.xMax, w.xMax), Mathf.Max(r.yMax, w.yMax)) : w;
                    any = true;
                }
                foreach (var id in space.Declared(false)) if (items.TryGetValue(id, out var item)) Take(item.World);
                foreach (var inner in spaces) if (space.Holds(inner.Block)) Take(inner.Rect);
                if (!any)
                {
                    r = new Rect(0, emptyY, GraphLook.CardWidth, 90);
                    emptyY += 90 + GraphLook.CardGap + 40;
                    space.Rect = new Rect(r.x - 14, r.y, r.width + 28, r.height);
                }
                else space.Rect = Rect.MinMaxRect(r.xMin - 16, r.yMin - 38, r.xMax + 16, r.yMax + 16);
                spaces.Add(space);
                spacesLayer.Insert(0, space);
            }
        }

        // The innermost namespace block's region a point of the board is in
        GraphSpace SpaceAt(Vector2 world) => spaces.FirstOrDefault(s => s.Rect.Contains(world));

        // An item was measured, or grew: what's placed around it follows
        internal void Resized(GraphItem item)
        {
            if (item is GraphFrame f && f.IsValue) return;
            schedule.Execute(() =>
            {
                if (Doc == null) return;
                var wants = new List<(int id, bool frame)>();
                var blocks = new List<JValue>();
                Collect(Doc.Root.Arr("decls"), wants, blocks);
                if (wants.Any(w => !items.ContainsKey(w.id)) || drag == Drag.MoveItems || drag == Drag.MoveNodes) return;
                PlaceAll(wants);
                Regions(blocks);
                Cull();
            });
        }

        // The file was edited: the items it names are built again as they are now, or every one drawn
        public void Changed(int[] owners)
        {
            Sync();
            if (owners == null || owners.Length == 0)
                foreach (var item in items.Values.Concat(values.Values).ToList()) Again(item);
            else
            {
                var done = new HashSet<GraphItem>();
                foreach (var owner in owners)
                    foreach (var item in ItemsFor(owner))
                        if (done.Add(item)) Again(item);
            }
            // What was built again shows what's selected
            selectionShown = null;
            ShowSelection();
        }

        static void Again(GraphItem item)
        {
            if (item.Level == GraphLevel.None) return;
            if (item is GraphFrame frame && item.Level == GraphLevel.Near) frame.Reconcile();
            else item.Rebuild();
        }

        // The items that show an element: its card or frame, the value frames of it, and what it's a part of
        IEnumerable<GraphItem> ItemsFor(int id)
        {
            for (var e = Doc.Find(id); e != null;)
            {
                if (items.TryGetValue(e.Id, out var item)) yield return item;
                foreach (var pair in values) if (pair.Key.Owner == e.Id) yield return pair.Value;
                var owner = e.Role == GraphRole.Node || e.Role == GraphRole.Note ? e.Slot.Owner : e.Owner?.Wire("id") ?? 0;
                e = owner != 0 && owner != e.Id ? Doc.Find(owner) : null;
            }
        }

        // Builds an item again as it is (a field that refused what was typed shows what it had)
        public void Refresh(int id)
        {
            foreach (var item in ItemsFor(id).ToList())
            {
                if (item is GraphFrame frame) frame.Refresh();
                else item.Rebuild();
            }
        }

        // The whole file changed under the board (an undo, a file read again): everything drawn is built again
        public void Reload()
        {
            Active = null;
            Nodes.Clear();
            Sync();
            foreach (var item in items.Values.Concat(values.Values).ToList()) item.Rebuild();
            if (problemsById.Count > 0) SetProblems(shownProblems);
            selectionShown = null;
            ShowSelection();
        }

        // Draws what's on screen, and nothing else: all of an item near, its name alone from far away
        public void Cull()
        {
            var w = layout.width;
            var h = layout.height;
            if (float.IsNaN(w) || w <= 0 || float.IsNaN(h) || h <= 0)
            {
                w = 1280;
                h = 800;
            }
            var far = zoom < GraphLook.FarZoom;
            var refar = far && (zoom > farBuiltAt * 1.3f || zoom < farBuiltAt / 1.3f);
            if (refar) farBuiltAt = zoom;
            foreach (var item in items.Values.Concat(values.Values))
            {
                var margin = (item.Level == GraphLevel.None ? 200 : 500) / zoom;
                var view = Rect.MinMaxRect(-pan.x / zoom - margin, -pan.y / zoom - margin, (w - pan.x) / zoom + margin, (h - pan.y) / zoom + margin);
                var level = !view.Overlaps(item.World) ? GraphLevel.None : far ? GraphLevel.Far : GraphLevel.Near;
                item.Show(level, refar && level == GraphLevel.Far);
            }
        }

        // How many nodes are drawn as elements now: what a big file costs is what's on screen
        internal int NodesDrawn => items.Values.Concat(values.Values).OfType<GraphFrame>().Sum(f => f.Views.Count);

        // ---------------------------------------------------------------------------------------------------------
        // The start: an empty board says what can be made

        void ShowStart()
        {
            var empty = Doc != null && (Doc.Root.Arr("decls")?.Items.Count ?? 0) == 0;
            hint.text = empty ? "" : "Drag: move  ·  Double-click a name: rename  ·  Right-click: menu  ·  Drag a pin: wire  ·  F: fit";
            if (!empty)
            {
                start?.RemoveFromHierarchy();
                start = null;
                return;
            }
            if (start != null) return;
            start = GraphLook.Row("tg-start");
            start.pickingMode = PickingMode.Ignore;
            start.Add(GraphLook.Text("What do you want to make?", "tg-start-title"));
            start.Add(GraphLook.Text("Pick one to start with. Everything else is a right click away.", "tg-start-sub"));
            var tiles = GraphLook.Row("tg-tiles");
            foreach (var kind in GraphSchema.DeclKinds.Where(GraphSchema.IsCommon))
            {
                var k = kind;
                var tile = GraphLook.Plain(() => AddDeclaration(k), "tg-tile");
                tile.name = "start:" + kind;
                tile.Add(GraphLook.Kind(GraphSchema.Groups.FirstOrDefault(g => g.Name == GraphSchema.DeclGroup(kind))?.Title ?? "", GraphSchema.DeclGroup(kind)));
                tile.Add(GraphLook.Text(GraphSchema.DeclTitle(kind), "tg-tile-title"));
                tile.Add(GraphLook.Text(GraphSchema.DeclSummary(kind), "tg-tile-summary"));
                tiles.Add(tile);
            }
            Button more = null;
            more = GraphLook.Plain(() => OpenAdd(null, more), "tg-tile");
            more.name = "start:more";
            more.Add(GraphLook.Text("More…", "tg-tile-title"));
            more.Add(GraphLook.Text("Everything a file can hold, with a search", "tg-tile-summary"));
            tiles.Add(more);
            start.Add(tiles);
            Add(start);
        }

        // The kinds the start offers as its big choices
        internal IEnumerable<string> StartKinds => GraphSchema.DeclKinds.Where(GraphSchema.IsCommon);

        // What "+ Add" and a right click on the empty board list: every kind under its group, with what it is
        internal List<GraphOption> AddOptions(Vector2? at, JValue into = null)
        {
            var options = new List<GraphOption>();
            foreach (var group in GraphSchema.Groups)
                foreach (var kind in GraphSchema.DeclKinds.Where(k => GraphSchema.DeclGroup(k) == group.Name))
                {
                    var k = kind;
                    options.Add(new GraphOption
                    {
                        Title = GraphSchema.DeclTitle(kind), Detail = GraphSchema.DeclSummary(kind), Tall = true, Section = group.Title, SectionSummary = group.Summary, Keywords = kind, Data = kind,
                        Priority = GraphSchema.IsCommon(kind) ? 1 : 0, Pick = () => AddDeclaration(k, at, into),
                    });
                }
            if (GraphDocument.IsClip(GraphBoard.Clipboard) && GraphJson.Parse(GraphBoard.Clipboard).Arr("decls") != null)
                options.Add(new GraphOption { Title = "Paste", Detail = "What was copied", Section = " ", Pick = () => PasteDeclarations(at ?? ToWorld(mouse)) });
            return options;
        }

        internal GraphOptionList OpenAdd(Vector2? boardPoint, VisualElement from = null)
        {
            var at = boardPoint != null ? ToWorld(boardPoint.Value) : (Vector2?)null;
            var into = boardPoint != null ? SpaceAt(at.Value)?.Block : null;
            var list = new GraphOptionList(into != null ? "Add to " + into.Str("name") : "What do you want to make?", null, "Search what to make", 340, 460);
            list.Set(AddOptions(at, into));
            if (from != null) Window.Open(list, from);
            else Window.Open(list, new Rect(this.LocalToWorld(boardPoint ?? mouse), Vector2.zero));
            return list;
        }

        // A new declaration, as a pick of the start or of the add menu makes one: where it was asked for, else
        // beside what's there (data in the left column, code to its right). Its name is ready to be typed over.
        internal int AddDeclaration(string kind, Vector2? at = null, JValue into = null)
        {
            if (Doc == null) return 0;
            JValue made = null;
            Window.Edit("Add " + GraphSchema.DeclTitle(kind).ToLowerInvariant(), () =>
            {
                KeepPlaces();
                var name = Doc.UniqueName(GraphLook.Identifier(GraphSchema.DeclTitle(kind)));
                made = Doc.NewDecl(kind, name);
                // Where it was asked for, or as near as there's room: never on a card or a frame
                var code = IsCode(kind, false);
                var where = at != null ? GraphLook.FreeSpot(at.Value, code ? new Vector2(560, 220) : new Vector2(GraphLook.CardWidth, 120), items.Values.Select(i => i.World).ToList(), 24, 40) : FreeSpot(code);
                var order = GraphSchema.DeclOrder(kind);
                if (order.Contains("x"))
                {
                    made.Set("x", JValue.Of(Mathf.Round(where.x)), order);
                    made.Set("y", JValue.Of(Mathf.Round(where.y)), order);
                }
                (into?.Arr("decls") ?? Doc.Decls).Items.Add(made);
            });
            if (made == null) return 0;
            var id = made.Wire("id");
            Fresh = id;
            Reveal(id, true);
            return id;
        }

        // Below what's in its column
        Vector2 FreeSpot(bool code)
        {
            var x = code ? GraphLook.CardWidth + GraphLook.LaneGap : 0;
            float y = 0;
            foreach (var item in items.Values)
            {
                if (item is GraphFrame != code) continue;
                if (item.At.x + item.Size.x < x - 10 || item.At.x > x + (code ? 400 : GraphLook.CardWidth)) continue;
                y = Mathf.Max(y, item.At.y + item.Size.y + (code ? GraphLook.FrameGap : GraphLook.CardGap));
            }
            return new Vector2(x, y);
        }

        // Where a member with code just made goes: below the code there is. Its frame's place is the file's from the
        // start. (A kind with no place of its own, a field, is left as it is.)
        internal void PlaceNew(JValue made, IList<string> order)
        {
            if (order == null || !order.Contains("x")) return;
            KeepPlaces();
            var at = FreeSpot(true);
            made.Set("x", JValue.Of(Mathf.Round(at.x)), order);
            made.Set("y", JValue.Of(Mathf.Round(at.y)), order);
        }

        // The frames of a declaration's own code (its methods, its accessors), in a copy of it: each below the
        // code there is, not on the ones it's a copy of
        void Relocate(JValue decl)
        {
            var members = decl.Arr("members");
            if (members == null) return;
            var at = FreeSpot(true);
            void Move(JValue v)
            {
                if (!GraphDocument.HasPlace(v)) return;
                v.Set("x", JValue.Of(Mathf.Round(at.x)));
                v.Set("y", JValue.Of(Mathf.Round(at.y)));
                at.y += 360;
            }
            foreach (var m in members.Items)
            {
                if (!m.IsObject) continue;
                Move(m);
                var accessors = m.Arr("accessors");
                if (accessors != null) foreach (var a in accessors.Items) Move(a);
            }
        }

        // Writes where each item is into a file that didn't say: once something's moved or added, what the board
        // worked out stays put
        void KeepPlaces()
        {
            foreach (var item in items.Values)
            {
                if (item.Placed) continue;
                var e = item.Element;
                if (e != null) GraphDocument.SetPlace(e, item.At);
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // View

        void ApplyTransform()
        {
            content.style.translate = new Translate(pan.x, pan.y);
            content.style.scale = new Scale(new Vector3(zoom, zoom, 1));
            grid.MarkDirtyRepaint();
        }

        public Vector2 ToWorld(Vector2 boardPoint) => (boardPoint - pan) / zoom;

        // A point of the board in a frame, as the frame's nodes are drawn: by the board's own pan and zoom, so it's
        // right before anything is laid out
        public Vector2 FrameLocal(GraphFrame frame, Vector2 boardPoint) => ToWorld(boardPoint) - frame.At;
        public Vector2 ToBoard(Vector2 world) => world * zoom + pan;

        void DrawGrid(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = grid.contentRect;
            if (r.width <= 0 || r.height <= 0) return;
            var palette = GraphLook.Of(Light);
            void Lines(float step, Color c)
            {
                var s = step * zoom;
                if (s < 8) return;
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
            Lines(20, palette.Grid);
            Lines(100, palette.GridStrong);
        }

        static float Mod(float a, float b) => ((a % b) + b) % b;

        void OnWheel(WheelEvent e)
        {
            var at = this.WorldToLocal(e.mousePosition);
            ZoomAbout(at, zoom * (1 - e.delta.y * 0.04f));
            e.StopPropagation();
        }

        internal void ZoomAbout(Vector2 boardPoint, float to)
        {
            var world = ToWorld(boardPoint);
            zoom = Mathf.Clamp(to, 0.02f, 2f);
            pan = boardPoint - world * zoom;
            ApplyTransform();
            Cull();
        }

        Vector2 ViewSize => new Vector2(float.IsNaN(layout.width) || layout.width <= 0 ? 1280 : layout.width, float.IsNaN(layout.height) || layout.height <= 0 ? 760 : layout.height);

        // Shows a part of the board: all of it when it fits at a size that reads, else its top left
        public void FrameRect(Rect world, bool all)
        {
            var size = ViewSize;
            if (world.width <= 0 && world.height <= 0)
            {
                zoom = 1;
                pan = new Vector2(40, 40);
            }
            else
            {
                world = Rect.MinMaxRect(world.xMin - 40, world.yMin - 40, world.xMax + 40, world.yMax + 40);
                var fit = Mathf.Min(size.x / world.width, size.y / world.height);
                if (all || fit >= 0.6f)
                {
                    zoom = Mathf.Clamp(fit, 0.02f, 1f);
                    pan = size / 2 - world.center * zoom;
                }
                else
                {
                    zoom = 1;
                    pan = new Vector2(40, 40) - world.min;
                }
            }
            ApplyTransform();
            Cull();
        }

        Rect Bounds(IEnumerable<GraphItem> of)
        {
            var any = false;
            var b = new Rect();
            foreach (var item in of)
            {
                var r = item.World;
                b = any ? Rect.MinMaxRect(Mathf.Min(b.xMin, r.xMin), Mathf.Min(b.yMin, r.yMin), Mathf.Max(b.xMax, r.xMax), Mathf.Max(b.yMax, r.yMax)) : r;
                any = true;
            }
            return any ? b : new Rect(0, 0, 0, 0);
        }

        // The whole board on screen, however small (F with nothing selected)
        public void FitAll() => FrameRect(Bounds(items.Values), true);

        // A file as it opens: all of it when that reads, else its start
        public void FrameStart() => FrameRect(Bounds(items.Values), false);

        public void FrameSelectionOrAll()
        {
            if (Active != null && Nodes.Count > 0)
            {
                var any = false;
                var b = new Rect();
                foreach (var id in Nodes)
                {
                    var v = Active.ViewOf(id);
                    if (v == null) continue;
                    var r = new Rect(Active.At + v.Shown, v.SizeNow);
                    b = any ? Rect.MinMaxRect(Mathf.Min(b.xMin, r.xMin), Mathf.Min(b.yMin, r.yMin), Mathf.Max(b.xMax, r.xMax), Mathf.Max(b.yMax, r.yMax)) : r;
                    any = true;
                }
                if (any)
                {
                    FrameRect(b, true);
                    return;
                }
            }
            if (Chosen.Count > 0) FrameRect(Bounds(Chosen.Select(Item).Where(i => i != null)), true);
            else FitAll();
        }

        // Brings an element on screen and marks it: a node in its frame, a field on its card, a declaration
        public void Reveal(int id, bool rename = false)
        {
            var e = Doc?.Find(id);
            if (e == null) return;
            if ((e.Role == GraphRole.Node || e.Role == GraphRole.Note) && !e.Slot.IsBody && !open.Contains(e.Slot))
            {
                open.Add(e.Slot);
                Sync();
            }
            var item = ItemOf(id);
            if (item == null) return;
            var size = ViewSize;
            if (zoom < 0.6f) zoom = 1;
            // The item's start in view; all of it, when it fits
            var world = item.World;
            var room = size / zoom;
            var target = new Vector2(world.width + 80 <= room.x ? world.center.x : world.xMin + room.x / 2 - 40, world.height + 80 <= room.y ? world.center.y : world.yMin + room.y / 2 - 40);
            var to = size / 2 - target * zoom;
            // Near what's on screen, and fitting: moved just enough to show all of it, so what's beside it stays
            // in sight (the component a new system is for)
            var seen = new Rect(-pan / zoom, room);
            if (world.width + 120 <= room.x && world.height + 120 <= room.y)
            {
                var dx = world.xMin - 60 < seen.xMin ? world.xMin - 60 - seen.xMin : world.xMax + 60 > seen.xMax ? world.xMax + 60 - seen.xMax : 0;
                var dy = world.yMin - 60 < seen.yMin ? world.yMin - 60 - seen.yMin : world.yMax + 60 > seen.yMax ? world.yMax + 60 - seen.yMax : 0;
                if (Mathf.Abs(dx) < room.x * 0.75f && Mathf.Abs(dy) < room.y * 0.75f) to = pan - new Vector2(dx, dy) * zoom;
            }
            pan = to;
            ApplyTransform();
            Cull();
            var frame = item as GraphFrame;
            if (frame != null && (e.Role == GraphRole.Node || e.Role == GraphRole.Note))
            {
                var view = frame.ViewOf(id) ?? Reader(frame, id);
                if (view != null)
                {
                    var at = frame.At + view.Shown + view.SizeNow / 2;
                    var inView = Rect.MinMaxRect(-pan.x / zoom + 60, -pan.y / zoom + 60, (size.x - pan.x) / zoom - 60, (size.y - pan.y) / zoom - 60);
                    if (!inView.Contains(at))
                    {
                        pan = size / 2 - at * zoom;
                        ApplyTransform();
                        Cull();
                    }
                    SelectNode(frame, view.Id, false);
                }
            }
            else SelectItem(item, false);
            item.Flash();
            if (rename) FocusLater(item.Id, "name");
        }

        // The node that shows a node that isn't drawn (a value typed on an input, a read): the one that reads it
        static GraphNodeView Reader(GraphFrame frame, int hidden)
        {
            var seen = new HashSet<int>();
            while (frame.Readers.TryGetValue(hidden, out var readers) && readers.Count > 0 && seen.Add(hidden))
            {
                var view = frame.ViewOf(readers[0].node);
                if (view != null) return view;
                hidden = readers[0].node;
            }
            return null;
        }

        // Puts the keyboard on a named part of an item, once it's built: the name of what was just made
        public void FocusLater(int item, string name)
        {
            // A name opens to be typed over now, when it's there; anything else is focused once it's built
            if (Item(item)?.Q<VisualElement>(name) is GraphName now)
            {
                now.Edit();
                return;
            }
            schedule.Execute(() =>
            {
                var found = Item(item)?.Q<VisualElement>(name);
                if (found == null) return;
                if (found is GraphName named)
                {
                    named.Edit();
                    return;
                }
                found.Focus();
                if (found is TextField field) field.schedule.Execute(field.SelectAll);
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Selecting

        // A row selected on its card or its frame's entry: a field, a choice, a parameter (Details shows it)
        public int Row { get; private set; }
        string selectionShown;

        // The row a pointer's target is on, by the element it's of (rows carry their element's id)
        static int RowAt(VisualElement target, GraphItem item)
        {
            if (item == null) return 0;
            for (var e = target; e != null && e != item; e = e.parent)
            {
                if (e is GraphNodeView && !(e is GraphEntryView)) return 0;
                if (e.userData is int id && (e.ClassListContains("tg-row") || e.ClassListContains("tg-entry-row"))) return id;
            }
            return 0;
        }

        public void SelectRow(GraphItem item, int id)
        {
            Nodes.Clear();
            Chosen.Clear();
            Active = item as GraphFrame;
            if (items.TryGetValue(item.Id, out var same) && same == item) Chosen.Add(item.Id);
            Row = id;
            Window.Selected(id);
            ShowSelection();
        }

        void ShowSelection()
        {
            if (Row != 0 && (Chosen.Count != 1 || Nodes.Count > 0 || Doc?.Find(Row) == null)) Row = 0;
            // What's selected, said once when it changes: the row lit, Details shown
            var now = $"{Row}|{Active?.Id}|{string.Join(",", Nodes)}|{string.Join(",", Chosen)}";
            if (now != selectionShown)
            {
                selectionShown = now;
                foreach (var row in this.Query<VisualElement>(className: "tg-row--selected").ToList()) row.RemoveFromClassList("tg-row--selected");
                if (Row != 0 && Chosen.Count == 1 && items.TryGetValue(Chosen.First(), out var holder))
                    holder.Query<VisualElement>().Where(v => v.userData is int id && id == Row && (v.ClassListContains("tg-row") || v.ClassListContains("tg-entry-row"))).ForEach(v => v.AddToClassList("tg-row--selected"));
                Window.SelectionChanged();
            }
            foreach (var item in items.Values.Concat(values.Values))
            {
                item.Selected = Chosen.Contains(item.Id) && items.TryGetValue(item.Id, out var same) && same == item || item == Active && Nodes.Count == 0 && Chosen.Count == 0 && item is GraphFrame f && f.IsValue;
                if (!(item is GraphFrame frame)) continue;
                foreach (var pair in frame.Views) pair.Value.Selected = frame == Active && Nodes.Contains(pair.Key);
                foreach (var pair in frame.Notes) pair.Value.Selected = frame == Active && Nodes.Contains(pair.Key);
            }
        }

        public void ClearSelection()
        {
            Chosen.Clear();
            Row = 0;
            Nodes.Clear();
            Active = null;
            ShowSelection();
        }

        public void SelectItem(GraphItem item, bool additive)
        {
            Nodes.Clear();
            Row = 0;
            Active = item as GraphFrame;
            if (!additive) Chosen.Clear();
            if (items.ContainsKey(item.Id) && items[item.Id] == item)
            {
                if (additive && Chosen.Contains(item.Id)) Chosen.Remove(item.Id);
                else Chosen.Add(item.Id);
            }
            Window.Selected(item.Id);
            ShowSelection();
        }

        public void SelectNode(GraphFrame frame, int id, bool additive)
        {
            Row = 0;
            if (frame.Absorbed.TryGetValue(id, out var eval)) id = eval;
            if (Active != frame || !additive) Nodes.Clear();
            Chosen.Clear();
            Active = frame;
            if (additive && Nodes.Contains(id)) Nodes.Remove(id);
            else Nodes.Add(id);
            if (Nodes.Count == 1) Window.Selected(Nodes.First());
            ShowSelection();
        }

        public void SelectAll()
        {
            Row = 0;
            if (Active != null && Active.Level == GraphLevel.Near)
            {
                Chosen.Clear();
                Nodes.Clear();
                foreach (var id in Active.Views.Keys) Nodes.Add(id);
                foreach (var id in Active.Notes.Keys) Nodes.Add(id);
            }
            else
            {
                Nodes.Clear();
                Chosen.Clear();
                foreach (var id in items.Keys) Chosen.Add(id);
            }
            ShowSelection();
        }

        // ---------------------------------------------------------------------------------------------------------
        // Dragging: moving, panning, box selecting, wiring

        enum Drag { None, Pan, Box, MoveNodes, MoveItems, Wire }

        Drag drag;
        int dragPointer = -1;
        Vector2 dragStart, panStart;
        readonly Dictionary<int, Vector2> nodeStart = new Dictionary<int, Vector2>();
        readonly Dictionary<GraphItem, Vector2> itemStart = new Dictionary<GraphItem, Vector2>();
        bool moved;
        GraphFrame boxFrame;      // The frame a box selects nodes of; the board's items when null
        HashSet<int> boxBefore;
        // A drag of nodes: where its frame was and where the frame's (0, 0) was drawn when it started, and where the
        // dragged ones are going, as the file has places
        Vector2 nodeFrameAt, nodeOrigin;
        readonly Dictionary<int, Vector2> nodeTo = new Dictionary<int, Vector2>();
        (GraphFrame frame, Vector2 at, bool any)? rightDown;

        // The wire being drawn: the end that stays, and what was wired before it was taken up
        PortView wireFrom;
        internal Vector2 WireEnd;  // In the board's space
        Action wireUndo;           // Unwires what was taken up, when it's dropped on nothing

        internal PortView WireFrom => drag == Drag.Wire ? wireFrom : null;

        // Whether a frame's nodes are being dragged
        internal bool MovingNodesOf(GraphFrame frame) => drag == Drag.MoveNodes && moved && Active == frame;

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
            var target = e.target as VisualElement;
            mouse = this.WorldToLocal(e.position);
            if (e.button == 2 || (e.button == 0 && e.altKey))
            {
                Focus();
                drag = Drag.Pan;
                dragStart = mouse;
                panStart = pan;
                Capture(e);
                e.StopPropagation();
                return;
            }
            if (GraphLook.TakesClicks(target, this)) return;
            Focus();
            var additive = e.shiftKey || e.actionKey;
            var view = GraphLook.Up<GraphNodeView>(target);
            var note = GraphLook.Up<GraphNoteView>(target);
            var item = GraphLook.Up<GraphItem>(target);
            var frame = item as GraphFrame;
            rightDown = null;
            var rowId = RowAt(target, item);
            // A data wire under the pointer, in a frame's empty inside: a double click puts a reroute on it, a right
            // click opens its menu
            var wire = frame != null && view == null && note == null && rowId == 0 ? WireUnder(frame, mouse) : null;
            if (wire != null && (e.button == 1 || e.button == 0 && e.clickCount >= 2))
            {
                var at = FrameLocal(frame, mouse);
                if (e.button == 1) GraphMenu.Open(Window, GraphMenu.ForWire(frame, wire, at), null, e.position);
                else AddReroute(frame, wire, at);
                e.StopPropagation();
                return;
            }
            if (e.button == 1)
            {
                // One menu, the same for every kind of thing (GraphMenu): a row's, a node's, a card's or a frame's
                // title's; the empty board and the empty inside of a frame open what can be added there
                if (rowId != 0)
                {
                    SelectRow(item, rowId);
                    GraphMenu.Open(Window, GraphMenu.ForRow(item, rowId), null, e.position);
                }
                else if (view != null && view.Id > 0)
                {
                    if (!(Active == view.Frame && Nodes.Contains(view.Id))) SelectNode(view.Frame, view.Id, false);
                    GraphMenu.Open(Window, GraphMenu.ForNode(view), null, e.position);
                }
                else if (item != null && (frame == null || view != null || mouse.y - ToBoard(item.At).y < GraphLook.FrameHead * zoom))
                {
                    if (!Chosen.Contains(item.Id) || Nodes.Count > 0) SelectItem(item, false);
                    GraphMenu.Open(Window, GraphMenu.ForItem(item), null, e.position);
                }
                else rightDown = (frame, mouse, true);
                e.StopPropagation();
                return;
            }
            if (e.button != 0) return;
            e.StopPropagation();
            if (rowId != 0 && !additive)
            {
                // A row of a card or of a frame's entry: selected (Details shows it), and dragged with its item
                SelectRow(item, rowId);
                drag = Drag.MoveItems;
                moved = false;
                dragStart = mouse;
                itemStart.Clear();
                if (items.TryGetValue(item.Id, out var rowItem) && rowItem == item) itemStart[item] = item.At;
                Capture(e);
                return;
            }
            if (frame != null && (note != null || (view != null && view.Id > 0)))
            {
                var id = note != null ? note.Id : view.Id;
                if (additive) SelectNode(frame, id, true);
                else if (!(Active == frame && Nodes.Contains(id))) SelectNode(frame, id, false);
                if (additive) return;
                drag = Drag.MoveNodes;
                moved = false;
                dragStart = mouse;
                StartNodeMove(frame, Nodes);
                Capture(e);
                return;
            }
            if (item != null)
            {
                // A frame's inside selects its nodes with a box; its title, its entry and a card move the item
                var head = frame == null || view != null || mouse.y - ToBoard(item.At).y < GraphLook.FrameHead * zoom;
                if (!head && frame.Level == GraphLevel.Near)
                {
                    if (!additive) SelectItem(frame, false);
                    Active = frame;
                    drag = Drag.Box;
                    boxFrame = frame;
                    dragStart = mouse;
                    boxBefore = additive ? new HashSet<int>(Nodes) : new HashSet<int>();
                    Capture(e);
                    return;
                }
                if (additive) SelectItem(item, true);
                else if (!Chosen.Contains(item.Id) || values.ContainsValue(item as GraphFrame)) SelectItem(item, false);
                if (additive || values.ContainsValue(item as GraphFrame)) return;
                drag = Drag.MoveItems;
                moved = false;
                dragStart = mouse;
                itemStart.Clear();
                foreach (var id in Chosen) if (items.TryGetValue(id, out var chosen)) itemStart[chosen] = chosen.At;
                Capture(e);
                return;
            }
            // The empty board, or a namespace's region: a box that selects cards and frames
            if (!additive) ClearSelection();
            drag = Drag.Box;
            boxFrame = null;
            dragStart = mouse;
            boxBefore = additive ? new HashSet<int>(Chosen) : new HashSet<int>();
            Capture(e);
        }

        void OnMove(PointerMoveEvent e)
        {
            mouse = this.WorldToLocal(e.position);
            switch (drag)
            {
                case Drag.None:
                {
                    // The data wire under the pointer is lit: it can be hit
                    var target = e.target as VisualElement;
                    var over = GraphLook.Up<GraphNodeView>(target) == null && !GraphLook.TakesClicks(target, this) ? GraphLook.Up<GraphFrame>(target) : null;
                    var wire = over != null ? WireUnder(over, mouse) : null;
                    if (hotFrame != null && (hotFrame != over || wire == null) && hotFrame.Wires != null) hotFrame.Wires.Hot = null;
                    hotFrame = wire != null ? over : null;
                    if (wire != null) over.Wires.Hot = wire;
                    break;
                }
                case Drag.Pan:
                    pan = panStart + (mouse - dragStart);
                    ApplyTransform();
                    Cull();
                    break;
                case Drag.Box:
                {
                    var r = Rect.MinMaxRect(Mathf.Min(dragStart.x, mouse.x), Mathf.Min(dragStart.y, mouse.y), Mathf.Max(dragStart.x, mouse.x), Mathf.Max(dragStart.y, mouse.y));
                    if (r.width < 3 && r.height < 3) break;
                    selectBox.style.display = DisplayStyle.Flex;
                    selectBox.style.left = r.x;
                    selectBox.style.top = r.y;
                    selectBox.style.width = r.width;
                    selectBox.style.height = r.height;
                    var world = Rect.MinMaxRect(ToWorld(r.min).x, ToWorld(r.min).y, ToWorld(r.max).x, ToWorld(r.max).y);
                    if (boxFrame != null)
                    {
                        Chosen.Clear();
                        Nodes.Clear();
                        Nodes.UnionWith(boxBefore);
                        foreach (var pair in boxFrame.Views)
                            if (world.Overlaps(new Rect(boxFrame.At + pair.Value.Shown, pair.Value.SizeNow))) Nodes.Add(pair.Key);
                        foreach (var pair in boxFrame.Notes)
                            if (world.Overlaps(new Rect(boxFrame.At + pair.Value.Shown, pair.Value.Extent))) Nodes.Add(pair.Key);
                    }
                    else
                    {
                        Chosen.Clear();
                        Chosen.UnionWith(boxBefore);
                        foreach (var item in items.Values) if (world.Overlaps(item.World)) Chosen.Add(item.Id);
                    }
                    ShowSelection();
                    break;
                }
                case Drag.MoveNodes:
                {
                    var d = (mouse - dragStart) / zoom;
                    if (d.sqrMagnitude > 4) moved = true;
                    if (!moved) break;
                    ShowNodeMove(d);
                    break;
                }
                case Drag.MoveItems:
                {
                    var d = (mouse - dragStart) / zoom;
                    if (d.sqrMagnitude > 4) moved = true;
                    if (!moved) break;
                    foreach (var pair in itemStart)
                    {
                        pair.Key.At = pair.Value + d;
                        pair.Key.Place();
                    }
                    foreach (var pair in values) PlaceValue(pair.Value);
                    break;
                }
                case Drag.Wire:
                    WireEnd = mouse;
                    wireFrom.Frame.MarkWires();
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
                    selectBox.style.display = DisplayStyle.None;
                    break;
                case Drag.MoveNodes:
                    if (moved) CommitNodeMove();
                    break;
                case Drag.MoveItems:
                    if (moved) CommitItemMove();
                    break;
                case Drag.Wire:
                    DropWire(e.position);
                    break;
                case Drag.None:
                    if (e.button == 1 && rightDown != null && (mouse - rightDown.Value.at).sqrMagnitude < 16)
                    {
                        var frame = rightDown.Value.frame;
                        if (frame != null && frame.Graph != null && frame.Level == GraphLevel.Near) OpenSearch(frame, FrameLocal(frame, mouse), null);
                        else if (frame == null) OpenAdd(mouse);
                        else Window.Say(frame.Graph == null ? "Its code is written in C: there's nothing to add here." : "Zoom in to add to this frame.");
                    }
                    rightDown = null;
                    break;
            }
        }

        // Moves cards and frames by an amount, as a drag of them does: an edit, undone like any other
        internal void MoveItemsBy(IEnumerable<GraphItem> moving, Vector2 by)
        {
            itemStart.Clear();
            foreach (var i in moving)
            {
                itemStart[i] = i.At;
                i.At += by;
                i.Place();
            }
            CommitItemMove();
        }

        // Moves nodes of a frame by an amount, as a drag of them does
        internal void MoveNodesBy(GraphFrame frame, IEnumerable<int> ids, Vector2 by)
        {
            Active = frame;
            StartNodeMove(frame, ids.Select(id => frame.ViewOf(id)?.Id ?? 0).Where(id => id != 0).Distinct().ToList());
            ShowNodeMove(by);
            CommitNodeMove();
        }

        // Where a drag of nodes starts: each node's and note's place, its frame's, and where the frame's (0, 0) is
        void StartNodeMove(GraphFrame frame, IEnumerable<int> ids)
        {
            nodeStart.Clear();
            nodeTo.Clear();
            nodeFrameAt = frame.At;
            nodeOrigin = frame.Origin;
            foreach (var n in ids)
            {
                if (frame.Views.TryGetValue(n, out var nv)) nodeStart[n] = nv.Shown;
                else if (frame.Notes.TryGetValue(n, out var note)) nodeStart[n] = note.Shown;
            }
        }

        // The dragged nodes where the pointer has them, anywhere: their frame grows toward them, moving its own
        // place to grow left or up, so nothing else on screen moves (GraphFrame.Follow)
        void ShowNodeMove(Vector2 by)
        {
            nodeTo.Clear();
            foreach (var pair in nodeStart) nodeTo[pair.Key] = pair.Value - nodeOrigin + by;
            Active.Follow(nodeTo);
        }

        void CommitNodeMove()
        {
            var frame = Active;
            var graph = frame.Graph;
            if (graph == null) return;
            var origin = frame.Origin;
            // The frame's own place, when it grew left or up toward them: in the same edit (a value's graph has
            // none of its own, it's beside what it's the value of)
            var grew = !frame.IsValue && frame.Element != null && frame.At != nodeFrameAt;
            var frameAt = frame.At;
            var moves = nodeStart.Keys.Where(id => frame.Views.ContainsKey(id)).ToDictionary(id => id, id => frame.Views[id].Shown - origin);
            var notes = nodeStart.Keys.Where(id => frame.Notes.ContainsKey(id)).ToDictionary(id => id, id => frame.Notes[id].Shown - origin);
            Window.Edit("Move", () =>
            {
                foreach (var pair in moves)
                {
                    var n = GraphDocument.NodeIn(graph, pair.Key);
                    if (n == null) continue;
                    var d = pair.Value - GraphDocument.PlaceOf(n);
                    // What's drawn with it (its call, what's typed on its inputs) moves with it
                    foreach (var with in frame.With(new[] { pair.Key }))
                    {
                        var w = GraphDocument.NodeIn(graph, with);
                        if (w == null) continue;
                        var at = with == pair.Key ? pair.Value : GraphDocument.PlaceOf(w) + d;
                        w.Set("x", JValue.Of(Mathf.Round(at.x)));
                        w.Set("y", JValue.Of(Mathf.Round(at.y)));
                    }
                }
                var list = graph.Arr("notes");
                foreach (var pair in notes)
                {
                    var note = list?.Items.FirstOrDefault(x => x.Wire("id") == pair.Key);
                    if (note == null) continue;
                    note.Set("x", JValue.Of(Mathf.Round(pair.Value.x)), GraphSchema.NoteOrder);
                    note.Set("y", JValue.Of(Mathf.Round(pair.Value.y)), GraphSchema.NoteOrder);
                }
                if (grew)
                {
                    KeepPlaces();
                    GraphDocument.SetPlace(frame.Element, frameAt);
                }
            }, frame.Id);
            // What's placed around it follows, now the drag's done
            Resized(frame);
        }

        void CommitItemMove()
        {
            var places = itemStart.Keys.ToDictionary(i => i, i => i.At);
            Window.Edit("Move", () =>
            {
                KeepPlaces();
                foreach (var pair in places)
                {
                    var e = pair.Key.Element;
                    if (e == null) continue;
                    GraphDocument.SetPlace(e, pair.Value);
                    // Dropped in a namespace's region, or out of one: declared there
                    if (e.Role != GraphRole.Decl) continue;
                    var into = spaces.FirstOrDefault(s => !s.Declared(true).Contains(e.Id) && s.Rect.Contains(new Rect(pair.Value, pair.Key.Size).center))?.Block?.Arr("decls");
                    var inside = spaces.FirstOrDefault(s => s.Block.Arr("decls") == e.List);
                    if (into == null && inside != null && !inside.Rect.Contains(new Rect(pair.Value, pair.Key.Size).center)) into = Doc.Find(inside.Block.Wire("id"))?.List ?? Doc.Decls;
                    if (into == null || into == e.List) continue;
                    e.List.Items.Remove(e.Value);
                    into.Items.Add(e.Value);
                }
            }, places.Keys.Select(i => i.Id).ToArray());
        }

        // A drag from a pin: a new wire from it, or the wire it has, taken up from its other end
        public void BeginWire(PortView port, IPointerEvent e)
        {
            Focus();
            var frame = port.Frame;
            var graph = frame.Graph;
            if (graph == null) return;
            var p = port.Port;
            if (e.altKey)
            {
                Window.Edit("Unwire", () => frame.Unwire(p), frame.Id);
                return;
            }
            mouse = this.WorldToLocal(e.position);
            wireFrom = port;
            wireUndo = null;
            if (p.Input && !p.Exec && port.Connected)
            {
                // A wired input: taken up from where its value comes from
                var from = p.Node == GraphFrame.ResultId ? graph.Wire("root") : frame.ById.TryGetValue(p.Node, out var n) && p.Path != null ? p.Path.Get(n) : 0;
                var source = frame.SourcePin(from);
                if (source != null)
                {
                    wireFrom = source;
                    wireUndo = () => frame.Unwire(p);
                }
            }
            else if (p.Input && p.Exec && port.Connected)
            {
                if (graph.Wire("entry") == p.Node && frame.Entry != null)
                {
                    wireFrom = frame.Entry.Ports.First(x => x.Port.Exec);
                    wireUndo = () => graph.Set("entry", JValue.Null());
                }
                else
                {
                    var (node, path) = GraphDocument.ExecSourceOf(graph, p.Node);
                    var source = frame.ViewOf(node)?.PortAt(node, path, false, true);
                    if (source != null)
                    {
                        wireFrom = source;
                        wireUndo = () => path.Set(GraphDocument.NodeIn(graph, node), 0);
                    }
                }
            }
            drag = Drag.Wire;
            WireEnd = mouse;
            Capture(e);
            frame.MarkWires();
            // The pins it can go to are lit, the others dimmed
            AddToClassList("tg-wiring");
            MarkPins(wireFrom);
        }

        readonly List<PortView> marked = new List<PortView>();

        // Every pin drawn, of every frame near enough to be drawn
        IEnumerable<PortView> PinsDrawn()
        {
            foreach (var frame in items.Values.Concat(values.Values).OfType<GraphFrame>())
            {
                if (frame.Level != GraphLevel.Near) continue;
                IEnumerable<GraphNodeView> views = frame.Views.Values;
                if (frame.Entry != null) views = views.Append(frame.Entry);
                if (frame.Result != null) views = views.Append(frame.Result);
                foreach (var v in views)
                    foreach (var p in v.Ports) yield return p;
            }
        }

        void MarkPins(PortView from)
        {
            UnmarkPins();
            foreach (var p in PinsDrawn())
            {
                if (p == from) continue;
                p.Mark(CanJoin(from, p));
                marked.Add(p);
            }
        }

        void UnmarkPins()
        {
            foreach (var p in marked) p.Mark(null);
            marked.Clear();
        }

        // Whether a wire from one pin can end at another: in its frame, an output to an input, a flow to a flow, and
        // a value to where it fits (by type where both are known; an unknown fits)
        internal static bool CanJoin(PortView from, PortView to)
        {
            if (to == null || from == null || to == from || to.Frame != from.Frame) return false;
            var a = from.Port;
            var b = to.Port;
            if (a.Input == b.Input || a.Exec != b.Exec) return false;
            if (a.Node == b.Node && a.Node > 0) return false;
            if (a.Exec) return true;
            var input = a.Input ? from : to;
            var output = a.Input ? to : from;
            if (input.Port.Path == null) return false;
            var frame = from.Frame;
            var owner = input.Port.Node == GraphFrame.ResultId ? null : frame.ById.TryGetValue(input.Port.Node, out var n) ? n : null;
            if (owner == null && input.Port.Node != GraphFrame.ResultId) return false;
            // A Set's "what" takes only what can be changed: a parameter taken as "changes", a local that can be
            var k = owner?.Str("k");
            if (k == "assign" && input.Port.Path.Parts.Length == 1 && input.Port.Path.Key == "target")
            {
                if (output.Port.IsRead) return frame.Changeable(output.Port);
                // A Get of what's only read, or of a field of it, can't be changed either
                var (name, local) = frame.RootOf(output.Port.Node);
                if (name != null || local != 0) return frame.Changeable(name, local);
            }
            var wanted = frame.Wanted(owner, input.Port.Path);
            return GraphValues.Fits(output.Type, wanted);
        }

        PortView PortUnder(Vector2 panelPosition, PortView from = null)
        {
            for (var e = panel?.Pick(panelPosition); e != null; e = e.parent)
            {
                if (e is PortView p) return p;
                // An input's row is the input
                if (e.ClassListContains("tg-port-row") && e.Q<PortView>() is PortView of && of.Port.Input && !of.Port.Exec) return of;
                // Dropped on a node, not on one of its rows: the pin of it it can go to nearest the pointer, so a
                // wire dropped on a node is never a node made on top of it
                if (e is GraphNodeView view && from != null)
                {
                    var can = view.Ports.Where(x => CanJoin(from, x)).OrderBy(x => Mathf.Abs(x.worldBound.center.y - panelPosition.y)).FirstOrDefault();
                    if (can != null) return can;
                    break;
                }
            }
            // Near a pin is on it
            PortView best = null;
            var bestD = 16f * 16f;
            foreach (var frame in items.Values.Concat(values.Values).OfType<GraphFrame>())
            {
                if (frame.Level != GraphLevel.Near) continue;
                IEnumerable<GraphNodeView> views = frame.Views.Values;
                if (frame.Entry != null) views = views.Append(frame.Entry);
                if (frame.Result != null) views = views.Append(frame.Result);
                foreach (var v in views)
                    foreach (var p in v.Ports)
                    {
                        if (p.resolvedStyle.display == DisplayStyle.None) continue;
                        var d = (p.worldBound.center - panelPosition).sqrMagnitude;
                        if (d >= bestD) continue;
                        bestD = d;
                        best = p;
                    }
            }
            return best;
        }

        // Why a wire can't join two frames, said where it's tried
        internal string WhyNotAcross(GraphFrame from, GraphFrame to)
        {
            var other = to != null ? $" It's {from.Title}'s; that pin is {to.Title}'s." : "";
            return "A wire stays inside its frame." + other + " To use a value in other code, make it something that code takes (\"+ needs…\"), or call the function that makes it.";
        }

        void DropWire(Vector2 panelPosition)
        {
            var from = wireFrom;
            var undo = wireUndo;
            wireFrom = null;
            wireUndo = null;
            RemoveFromClassList("tg-wiring");
            UnmarkPins();
            var frame = from.Frame;
            frame.MarkWires();
            var target = PortUnder(panelPosition, from);
            if (target != null && target != from)
            {
                if (target.Frame != frame)
                {
                    Window.Say(WhyNotAcross(frame, target.Frame), true);
                    return;
                }
                string why = null;
                Window.Edit("Wire", () =>
                {
                    undo?.Invoke();
                    frame.Classify();
                    if (!frame.Join(from.Port, target.Port, out why)) why = why ?? "Those two can't be joined.";
                    else why = null;
                }, frame.Id);
                if (why != null) Window.Say(GraphLook.UpperFirst(why), true);
                return;
            }
            if (undo != null)
            {
                // Dropped on nothing: the wire it took up goes
                Window.Edit("Unwire", undo, frame.Id);
                return;
            }
            if (target == from) return;
            var local = FrameLocal(frame, this.WorldToLocal(panelPosition));
            if (local.x < 0 || local.y < 0 || local.x > frame.Size.x || local.y > frame.Size.y)
            {
                Window.Say(WhyNotAcross(frame, null), true);
                return;
            }
            OpenSearch(frame, local, from.Port);
        }

        GraphFrame hotFrame; // The frame whose wire is lit under the pointer

        // The data wire of a frame near a point of the board, as it's drawn: within a few pixels on screen
        internal FrameWires.Wire WireUnder(GraphFrame frame, Vector2 boardPoint)
        {
            if (frame == null || frame.Level != GraphLevel.Near || frame.Wires == null || frame.Graph == null) return null;
            return frame.Wires.WireAt(FrameLocal(frame, boardPoint), 7 / zoom);
        }

        // A reroute put on a wire at a place of its frame
        internal int AddReroute(GraphFrame frame, FrameWires.Wire wire, Vector2 frameLocal)
        {
            var reader = wire.Reader == GraphFrame.ResultId ? null : frame.ById.TryGetValue(wire.Reader, out var n) ? n : null;
            if (reader == null && wire.Reader != GraphFrame.ResultId) return 0;
            var knot = frame.AddReroute(wire.From, reader, wire.Path, frameLocal);
            if (knot != 0) SelectNode(frame, knot, false);
            return knot;
        }

        // Wires two pins as a drag from one to the other does: false, with why, when they can't be
        internal bool Connect(PortView a, PortView b, out string why)
        {
            if (a.Frame != b.Frame)
            {
                why = WhyNotAcross(a.Frame, b.Frame);
                return false;
            }
            var frame = a.Frame;
            var done = false;
            string said = null;
            Window.Edit("Wire", () => done = frame.Join(a.Port, b.Port, out said), frame.Id);
            why = said;
            return done;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Adding nodes

        internal GraphSearchPopup Search => Window.Popup as GraphSearchPopup;

        // The search for a node of a frame, at a place in it: for a wire dropped there, what suits the wire
        public GraphSearchPopup OpenSearch(GraphFrame frame, Vector2 frameLocal, GraphPort pending, string typed = null)
        {
            var graph = frame.Graph;
            if (graph == null) return null;
            var context = new GraphSearchContext { Doc = Doc, Graph = graph, Slot = frame.Slot, Window = Window, Pending = pending, Frame = frame };
            var popup = new GraphSearchPopup(this, frame, context, frameLocal);
            Window.Open(popup, new Rect(frame.LocalToWorld(frameLocal), Vector2.zero));
            if (!string.IsNullOrEmpty(typed)) popup.Type(typed);
            return popup;
        }

        // The picker of what goes in an input: `keep` of the parts shown there stay, and it's picked from there on
        internal GraphValuePicker PickValue(GraphFrame frame, JValue owner, WirePath path, string label, VisualElement from, int keep = 0, string typed = null)
        {
            if (frame.Graph == null) return null;
            if (owner != null) SelectNode(frame, (frame.ReaderOf(owner.Wire("id")) ?? frame.ViewOf(owner.Wire("id")))?.Id ?? owner.Wire("id"), false);
            var picker = new GraphValuePicker(this, frame, owner, path, label, keep);
            Window.Open(picker, from);
            if (!string.IsNullOrEmpty(typed)) picker.Type(typed);
            return picker;
        }

        // The search for a node that works out what goes in an input, placed beside the node whose input it is
        internal GraphSearchPopup WorkOut(GraphFrame frame, JValue owner, WirePath path, VisualElement from, string typed = null)
        {
            var graph = frame.Graph;
            if (graph == null) return null;
            var port = new GraphPort { Node = owner != null ? owner.Wire("id") : GraphFrame.ResultId, Input = true, Path = path };
            var view = owner != null ? frame.ReaderOf(owner.Wire("id")) ?? frame.ViewOf(owner.Wire("id")) : frame.Result;
            // Below and left of it, off the line its flow runs along, where nothing is
            var at = frame.FreePlace(view != null ? view.Shown + new Vector2(-240, view.SizeNow.y + 30) : frame.Origin, new Vector2(220, 90));
            // What's in the input now is the new node's first input: wobble.frequency, worked out, is "wobble.frequency * …"
            var context = new GraphSearchContext { Doc = Doc, Graph = graph, Slot = frame.Slot, Window = Window, Pending = port, Frame = frame, Wrap = frame.WireAt(owner, path) };
            var popup = new GraphSearchPopup(this, frame, context, at);
            if (from != null && from.panel != null) Window.Open(popup, from);
            else Window.Open(popup, new Rect(frame.LocalToWorld(at), Vector2.zero));
            if (!string.IsNullOrEmpty(typed)) popup.Type(typed);
            return popup;
        }

        // The search for the member a member or method node is: what its value has, as the language server knows it
        public GraphSearchPopup OpenMemberSearch(GraphFrame frame, int node, VisualElement from)
        {
            var graph = frame.Graph;
            if (graph == null || GraphDocument.NodeIn(graph, node) == null) return null;
            var context = new GraphSearchContext { Doc = Doc, Graph = graph, Slot = frame.Slot, Window = Window, Retarget = node, Frame = frame };
            var popup = new GraphSearchPopup(this, frame, context, Vector2.zero);
            Window.Open(popup, from);
            return popup;
        }

        // Makes a member or method node the member a search picked for it
        public void Retarget(GraphFrame frame, GraphSearchEntry entry, int node)
        {
            var n = GraphDocument.NodeIn(frame.Graph, node);
            if (n == null || entry.Apply == null) return;
            Window.Edit("Pick " + entry.Title, () => entry.Apply(n), frame.Id);
            SelectNode(frame, node, false);
        }

        // Makes what a search entry makes at a place of a frame, wired to the pin a wire was dropped from
        public JValue Insert(GraphFrame frame, GraphSearchEntry entry, Vector2 frameLocal, GraphPort pending, int wrap = 0)
        {
            var graph = frame.Graph;
            // Where it was asked for, or as near as there's room: never on a node that's there. A node for an input
            // goes left of its reader, which can be left of every node there is.
            var at = frame.PlaceOfShown(frame.FreePlace(frameLocal, new Vector2(220, 90)));
            JValue made = null;
            Window.Edit("Add " + entry.Title, () =>
            {
                made = entry.Make(Doc, graph, at);
                // What was in the input goes in the new node's first input, before the node takes its place
                var wrapped = made != null && wrap != 0 && pending != null && pending.Input && !pending.Exec ? GraphDocument.NodeIn(graph, wrap) : null;
                var first = wrapped != null ? GraphPorts.Of(made).FirstOrDefault(p => p.Input && !p.Exec && p.Path != null && p.Path.Get(made) == 0) : null;
                if (first == null || !GraphDocument.Connect(graph, made.Wire("id"), first.Path, wrap, out _)) wrapped = null;
                if (made != null && pending != null) WireNew(frame, graph, made, pending);
                // Where what it wraps was drawn, which moves left to make room, with the Gets it alone reads
                if (wrapped != null && frame.ViewOf(wrap) != null && !frame.Hidden.Contains(wrap))
                {
                    var room = GraphNodeView.EstimateOf(made, null, frame).x + 40;
                    var place = GraphDocument.PlaceOf(wrapped);
                    foreach (var id in UpstreamOf(graph, wrap))
                    {
                        var n = GraphDocument.NodeIn(graph, id);
                        n.Set("x", JValue.Of(Mathf.Round(GraphDocument.PlaceOf(n).x - room)));
                    }
                    made.Set("x", JValue.Of(Mathf.Round(place.x)));
                    made.Set("y", JValue.Of(Mathf.Round(place.y)));
                }
            }, frame.Id);
            // What works out a Set's value: its operator is looked at once it's checked
            if (made != null && pending != null && pending.Input && !pending.Exec && frame.ById.TryGetValue(pending.Node, out var reader)) WatchOperator(reader, pending.Path);
            if (made != null && made.Has("k")) SelectNode(frame, made.Wire("id"), false);
            return made;
        }

        // A node and what's read by it alone, all the way up: what moves with it to make room for a node before it
        static HashSet<int> UpstreamOf(JValue graph, int node)
        {
            var chain = new HashSet<int> { node };
            var nodes = GraphDocument.Nodes(graph).Items;
            for (var grew = true; grew;)
            {
                grew = false;
                foreach (var n in nodes.Where(n => chain.Contains(n.Wire("id"))).ToList())
                    foreach (var w in GraphPorts.DataWires(n))
                    {
                        if (chain.Contains(w)) continue;
                        var readers = nodes.Where(r => GraphPorts.DataWires(r).Contains(w)).Select(r => r.Wire("id"));
                        if (graph.Wire("root") == w || !readers.All(chain.Contains)) continue;
                        chain.Add(w);
                        grew = true;
                    }
            }
            return chain;
        }

        // A new node wired to where its wire came from: its first input that suits, or its output, or its flow
        static void WireNew(GraphFrame frame, JValue graph, JValue made, GraphPort pending)
        {
            var ports = GraphPorts.Of(made);
            if (made.Str("k") == "eval" && made.Wire("value") != 0)
            {
                // A call statement: its call's inputs are its inputs
                var call = GraphDocument.NodeIn(graph, made.Wire("value"));
                if (call != null) ports = GraphPorts.Of(call).Concat(ports.Where(p => !(p.Path != null && p.Path.Key == "value"))).ToList();
            }
            GraphPort match;
            if (!pending.Input) match = ports.FirstOrDefault(p => p.Input && p.Exec == pending.Exec && (p.Path == null || p.Path.Get(GraphDocument.NodeIn(graph, p.Node)) == 0));
            else match = ports.FirstOrDefault(p => !p.Input && p.Exec == pending.Exec && (!p.Exec || p.Path?.Key == "next"));
            if (match == null) return;
            frame.Classify();
            frame.Join(pending, match, out _);
        }

        public void AddNote(GraphFrame frame, Vector2 frameLocal)
        {
            var graph = frame.Graph;
            if (graph == null) return;
            var at = Vector2.Max(frameLocal - frame.Origin, Vector2.zero);
            Window.Edit("Add note", () =>
            {
                var list = graph.Arr("notes");
                if (list == null) graph.Set("notes", list = JValue.NewArray(), frame.Slot.IsBody ? GraphSchema.BodyOrder : GraphSchema.ValueOrder);
                var note = JValue.NewObject();
                note.Set("id", JValue.Of(Doc.NewId()));
                note.Set("x", JValue.Of(Mathf.Round(at.x)));
                note.Set("y", JValue.Of(Mathf.Round(at.y)));
                note.Set("w", JValue.Of(300));
                note.Set("h", JValue.Of(120));
                note.Set("text", JValue.Of("Note"));
                list.Items.Add(note);
            }, frame.Id);
        }

        internal GraphTypePicker PickType(GraphTypeRequest request, VisualElement from, Action<string> picked)
        {
            var picker = new GraphTypePicker(Window, request, picked);
            Window.Open(picker, from);
            return picker;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Values, opened as small graphs beside their cards

        public GraphFrame OpenValue(GraphSlot slot)
        {
            if (Doc.GetGraph(slot) == null)
                Window.Edit("Work it out", () =>
                {
                    var g = JValue.NewObject();
                    g.Set("root", JValue.Null());
                    g.Set("nodes", JValue.NewArray());
                    GraphValues.Put(Doc, slot, g);
                }, slot.Owner);
            if (!open.Contains(slot)) open.Add(slot);
            Sync();
            var frame = ValueFrame(slot);
            if (frame == null) return null;
            Cull();
            SelectItem(frame, false);
            return frame;
        }

        public void CloseValue(GraphSlot slot)
        {
            open.Remove(slot);
            // A value with nothing in it is no value
            var g = Doc.GetGraph(slot);
            if (g != null && g.Wire("root") == 0 && (g.Arr("nodes")?.Items.Count ?? 0) == 0 && slot.Index < 0 && slot.Key == "default")
                Window.Edit("No value", () => GraphValues.Clear(Doc, slot), slot.Owner);
            else Sync();
        }

        // ---------------------------------------------------------------------------------------------------------
        // Layout: tidec's own, from the language server

        internal bool LayoutPending { get; private set; }

        // Lays out a frame's nodes as tidec does; without the language server they stay where they are
        public void Tidy(GraphFrame frame)
        {
            var graph = frame.Graph;
            if (graph == null) return;
            LayoutPending = true;
            TideGraphLanguage.Layout(graph, (layout, why) =>
            {
                LayoutPending = false;
                if (Window == null || frame.parent == null) return;
                if (layout == null)
                {
                    Window.Say($"Tidying up is the language server's, which can't now ({why}): the nodes stay where they are.", true);
                    return;
                }
                var now = frame.Graph;
                if (now == null) return;
                Window.Edit("Tidy up", () => TideGraphLanguage.Place(now, layout), frame.Id);
            });
        }

        // Lays out the whole board: data in a column on the left, code to its right
        public void TidyBoard()
        {
            if (Doc == null) return;
            LayoutPending = true;
            TideGraphLanguage.LayoutBoard(Doc.Root, (layout, why) =>
            {
                LayoutPending = false;
                if (Window == null || Doc == null) return;
                if (layout == null)
                {
                    Window.Say($"Tidying up is the language server's, which can't now ({why}): everything stays where it is.", true);
                    return;
                }
                Window.Edit("Tidy up the board", () =>
                {
                    for (var i = 0; i < layout.Keys.Count; i++)
                    {
                        var at = layout.Values[i];
                        if (!int.TryParse(layout.Keys[i], out var id) || !at.IsArray || at.Items.Count < 2) continue;
                        var e = Doc.Find(id);
                        if (e != null) GraphDocument.SetPlace(e, new Vector2((float)at.Items[0].Number, (float)at.Items[1].Number));
                    }
                });
                foreach (var item in items.Values) item.Measured = false;
                Sync();
                FrameStart();
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Keys and the clipboard

        // The clipboard: the system's, so nodes copy between this editor and the IDEs'. A test gives it one of its
        // own (ClipboardRead, ClipboardWrite), and leaves the user's alone.
        internal static Func<string> ClipboardRead;
        internal static Action<string> ClipboardWrite;

        internal static string Clipboard
        {
            get => ClipboardRead != null ? ClipboardRead() : EditorGUIUtility.systemCopyBuffer;
            set
            {
                if (ClipboardWrite != null) ClipboardWrite(value);
                else EditorGUIUtility.systemCopyBuffer = value;
            }
        }

        // Empties the filled input under the pointer, when there is one
        internal bool EmptyInputUnder()
        {
            var under = panel?.Pick(this.LocalToWorld(mouse));
            var view = GraphLook.Up<GraphNodeView>(under);
            return view != null && view.EmptyAt(under);
        }

        void OnKey(KeyDownEvent e)
        {
            var target = e.target as VisualElement;
            if (target is TextField || target?.GetFirstAncestorOfType<TextField>() != null || Window.Popup != null) return;
            var action = e.ctrlKey || e.commandKey;
            switch (e.keyCode)
            {
                case KeyCode.Delete:
                case KeyCode.Backspace:
                    // With the pointer on a filled input, it's emptied rather than what's selected deleted
                    if (!EmptyInputUnder()) DeleteSelection();
                    break;
                case KeyCode.D when action:
                    DuplicateSelection();
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
                case KeyCode.F when action:
                case KeyCode.P when action:
                    Window.ShowMyGame(true, true);
                    break;
                case KeyCode.F:
                    FrameSelectionOrAll();
                    break;
                case KeyCode.F2:
                    RenameSelection();
                    break;
                case KeyCode.Space:
                {
                    var frame = FrameAt(mouse) ?? (Active != null && Nodes.Count > 0 ? Active : null);
                    if (frame != null && frame.Graph != null && frame.Level == GraphLevel.Near) OpenSearch(frame, FrameLocal(frame, mouse), null);
                    else OpenAdd(mouse);
                    break;
                }
                case KeyCode.Escape:
                    ClearSelection();
                    break;
                default:
                    return;
            }
            e.StopPropagation();
        }

        // The frame a point of the board is in
        internal GraphFrame FrameAt(Vector2 boardPoint)
        {
            var world = ToWorld(boardPoint);
            return values.Values.FirstOrDefault(f => f.World.Contains(world)) ?? items.Values.OfType<GraphFrame>().FirstOrDefault(f => f.World.Contains(world));
        }

        // F2: the name of what's selected, opened to be typed over
        public void RenameSelection()
        {
            if (Active != null && Nodes.Count == 1) RenameNode(Active, Nodes.First());
            else if (Row != 0 && Chosen.Count == 1) Rename(Chosen.First(), "name" + Row);
            else if (Chosen.Count == 1) Rename(Chosen.First(), null);
        }

        // Opens a name of an item to be typed over: its own (`name` null), or a row's ("name<id>")
        public void Rename(int item, string name)
        {
            var found = Item(item)?.Q<GraphName>(name ?? "name");
            if (found != null) found.Edit();
            else if (name == null) Window.Say("This has no name of its own to change.");
        }

        // A node's name: a call's, a member's, a name's, the local it makes
        public void RenameNode(GraphFrame frame, int id)
        {
            var view = frame.ViewOf(id);
            var name = view?.Q<GraphName>("title") ?? view?.Q<GraphName>("local") ?? view?.Q<GraphName>();
            if (name != null) name.Edit();
            else Window.Say("This node has no name to change: what it does is its kind.");
        }

        public void DeleteSelection()
        {
            if (Row != 0 && Chosen.Count == 1 && Nodes.Count == 0)
            {
                Delete(Row);
                return;
            }
            if (Active != null && Nodes.Count > 0)
            {
                var frame = Active;
                var graph = frame.Graph;
                if (graph == null) return;
                var ids = frame.With(Nodes);
                ids.UnionWith(Nodes);
                Nodes.Clear();
                Window.Edit("Delete", () =>
                {
                    // A reroute deleted joins what it passed on to what read it
                    GraphFrame.JoinAroundKnots(graph, ids);
                    Doc.DeleteNodes(graph, ids);
                }, frame.Id);
                return;
            }
            if (Chosen.Count == 0) return;
            var chosen = Chosen.ToList();
            Chosen.Clear();
            Window.Edit("Delete", () =>
            {
                foreach (var id in chosen)
                {
                    var e = Doc.Find(id);
                    e?.List?.Items.Remove(e.Value);
                }
            });
        }

        // Takes a declaration, a member or anything else in a list away
        public void Delete(int id)
        {
            var e = Doc.Find(id);
            if (e?.List == null) return;
            var owner = e.Owner?.Wire("id") ?? 0;
            Chosen.Remove(id);
            if (owner != 0 && e.Role != GraphRole.Decl) Window.Edit("Delete " + Doc.TitleOf(e), () => e.List.Items.Remove(e.Value), owner);
            else Window.Edit("Delete " + Doc.TitleOf(e), () => e.List.Items.Remove(e.Value));
        }

        // A copy of a declaration or a member, after it, under a name of its own
        public int Duplicate(int id)
        {
            var e = Doc.Find(id);
            if (e?.List == null) return 0;
            JValue copy = null;
            var item = Item(id);
            Window.Edit("Duplicate", () =>
            {
                KeepPlaces();
                copy = Doc.Duplicate(e.Value);
                var name = copy.Str("name");
                if (!string.IsNullOrEmpty(name)) copy.Set("name", JValue.Of(e.Role == GraphRole.Decl ? Doc.UniqueName(name) : GraphDocument.UniqueIn(e.List, name)));
                if (GraphDocument.HasPlace(copy) && item != null)
                {
                    // Below what it's a copy of, where there's room
                    var at = GraphLook.FreeSpot(item.At + new Vector2(30, item.Size.y + 30), item.Size, items.Values.Select(i => i.World).ToList(), 24, 40);
                    copy.Set("x", JValue.Of(Mathf.Round(at.x)));
                    copy.Set("y", JValue.Of(Mathf.Round(at.y)));
                }
                if (e.Role == GraphRole.Decl) Relocate(copy);
                e.List.Items.Insert(e.List.Items.IndexOf(e.Value) + 1, copy);
            });
            if (copy == null) return 0;
            if (items.ContainsKey(copy.Wire("id"))) Reveal(copy.Wire("id"));
            return copy.Wire("id");
        }

        public void CopySelection()
        {
            if (Row != 0 && Chosen.Count == 1 && Nodes.Count == 0)
            {
                CopyRow(Row);
                return;
            }
            if (Active != null && Nodes.Count > 0)
            {
                var graph = Active.Graph;
                if (graph == null) return;
                var ids = Active.With(Nodes);
                GraphBoard.Clipboard = GraphDocument.Copy(graph, ids, new HashSet<int>(Nodes.Where(Active.Notes.ContainsKey)));
                return;
            }
            if (Chosen.Count == 0) return;
            // Declarations: the same clip, with them under a key of its own
            var clip = JValue.NewObject();
            clip.Set("tidegraphClip", JValue.Of(1));
            clip.Set("nodes", JValue.NewArray());
            var decls = JValue.NewArray();
            foreach (var id in Chosen)
            {
                var e = Doc.Find(id);
                if (e != null && e.Role == GraphRole.Decl) decls.Items.Add(e.Value.Clone());
            }
            if (decls.Items.Count == 0) return;
            clip.Set("decls", decls);
            GraphBoard.Clipboard = GraphJson.Write(clip);
        }

        public void Paste(Vector2 boardPoint)
        {
            var text = GraphBoard.Clipboard;
            if (!GraphDocument.IsClip(text)) return;
            var clip = GraphJson.Parse(text);
            if (clip.Arr("rows") != null)
            {
                // A field, a choice, a parameter: after the row selected, or on the card selected
                if (Row != 0 && CanPasteRows(Row, text)) PasteRows(Row);
                else if (Chosen.Count == 1 && Item(Chosen.First()) is GraphItem into && CanPasteInto(into, text)) PasteInto(into);
                else Window.Say("What's copied is a row: select a card (or a row of it) that can have it, then paste.");
                return;
            }
            if (clip.Arr("decls") != null && (clip.Arr("nodes")?.Items.Count ?? 0) == 0)
            {
                PasteDeclarations(ToWorld(boardPoint));
                return;
            }
            var frame = FrameAt(boardPoint) ?? Active;
            if (frame == null || frame.Graph == null)
            {
                Window.Say("Nodes are pasted into a frame: put the pointer in one.");
                return;
            }
            var graph = frame.Graph;
            // The copied nodes' top left goes where the pointer is
            var all = (clip.Arr("nodes")?.Items ?? new List<JValue>()).Concat(clip.Arr("notes")?.Items ?? new List<JValue>()).ToList();
            var min = all.Count == 0 ? Vector2.zero : new Vector2(all.Min(n => (float)(n.Get("x")?.Number ?? 0)), all.Min(n => (float)(n.Get("y")?.Number ?? 0)));
            var at = frame.World.Contains(ToWorld(boardPoint)) ? FrameLocal(frame, boardPoint) - frame.Origin : min + new Vector2(40, 40);
            // Where there's room for all of it
            at = frame.PlaceOfShown(frame.FreePlace(at + frame.Origin, ClipSize(all, min)));
            List<int> made = null;
            Window.Edit("Paste", () => made = Doc.Paste(graph, text, at - min), frame.Id);
            SelectMade(frame, made);
        }

        // How much room copied nodes take, from their top left
        static Vector2 ClipSize(List<JValue> all, Vector2 min)
        {
            var size = new Vector2(120, 60);
            foreach (var n in all)
            {
                var r = GraphDocument.PlaceOf(n) - min + (n.Has("k") ? GraphNodeView.EstimateOf(n, null) : new Vector2((float)(n.Get("w")?.Number ?? 300), (float)(n.Get("h")?.Number ?? 120)));
                size = Vector2.Max(size, r);
            }
            return size;
        }

        void SelectMade(GraphFrame frame, List<int> made)
        {
            if (made == null) return;
            Chosen.Clear();
            Nodes.Clear();
            Active = frame;
            foreach (var id in made)
            {
                var shown = frame.Absorbed.TryGetValue(id, out var eval) ? eval : id;
                if (frame.Views.ContainsKey(shown) || frame.Notes.ContainsKey(shown)) Nodes.Add(shown);
            }
            ShowSelection();
        }

        void PasteDeclarations(Vector2 world)
        {
            var text = GraphBoard.Clipboard;
            if (!GraphDocument.IsClip(text)) return;
            var decls = GraphJson.Parse(text).Arr("decls");
            if (decls == null || decls.Items.Count == 0) return;
            var min = new Vector2(decls.Items.Min(d => (float)(d.Get("x")?.Number ?? 0)), decls.Items.Min(d => (float)(d.Get("y")?.Number ?? 0)));
            // Where there's room for all of it: never on what's there
            var size = new Vector2(decls.Items.Max(d => (float)(d.Get("x")?.Number ?? 0)) - min.x + 560, decls.Items.Max(d => (float)(d.Get("y")?.Number ?? 0)) - min.y + 220);
            world = GraphLook.FreeSpot(world, size, items.Values.Select(i => i.World).ToList(), 24, 40);
            var made = new List<int>();
            Window.Edit("Paste", () =>
            {
                KeepPlaces();
                foreach (var d in decls.Items)
                {
                    var copy = Doc.Duplicate(d);
                    if (!string.IsNullOrEmpty(copy.Str("name"))) copy.Set("name", JValue.Of(Doc.UniqueName(copy.Str("name"))));
                    if (GraphDocument.HasPlace(copy))
                    {
                        var at = GraphDocument.PlaceOf(copy) - min + world;
                        copy.Set("x", JValue.Of(Mathf.Round(at.x)));
                        copy.Set("y", JValue.Of(Mathf.Round(at.y)));
                    }
                    Relocate(copy);
                    Doc.Decls.Items.Add(copy);
                    Doc.Reindex();
                    made.Add(copy.Wire("id"));
                }
            });
            Chosen.Clear();
            Nodes.Clear();
            foreach (var id in made) if (items.ContainsKey(id)) Chosen.Add(id);
            ShowSelection();
        }

        public void DuplicateSelection()
        {
            if (Row != 0 && Chosen.Count == 1 && Nodes.Count == 0)
            {
                Duplicate(Row);
                return;
            }
            if (Active != null && Nodes.Count > 0)
            {
                var frame = Active;
                var graph = frame.Graph;
                if (graph == null) return;
                var text = GraphDocument.Copy(graph, frame.With(Nodes), new HashSet<int>(Nodes.Where(frame.Notes.ContainsKey)));
                // Beside what it's a copy of, where there's room for all of it
                var clip = GraphJson.Parse(text);
                var all = (clip.Arr("nodes")?.Items ?? new List<JValue>()).Concat(clip.Arr("notes")?.Items ?? new List<JValue>()).ToList();
                var min = all.Count == 0 ? Vector2.zero : new Vector2(all.Min(n => (float)(n.Get("x")?.Number ?? 0)), all.Min(n => (float)(n.Get("y")?.Number ?? 0)));
                var to = frame.PlaceOfShown(frame.FreePlace(min + frame.Origin + new Vector2(40, 40), ClipSize(all, min)));
                List<int> made = null;
                Window.Edit("Duplicate", () => made = Doc.Paste(graph, text, to - min), frame.Id);
                SelectMade(frame, made);
                return;
            }
            foreach (var id in Chosen.ToList()) Duplicate(id);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Rows on the clipboard: a field, a choice, a parameter, copied in the same clip object as nodes are, under
        // "rows", with the key of the list they're from

        static JValue ClipOf(string text)
        {
            if (!GraphDocument.IsClip(text)) return null;
            try
            {
                return GraphJson.Parse(text);
            }
            catch (GraphJson.ParseError)
            {
                return null;
            }
        }

        // The key of the list an element is in, on what holds it: members, entries, params, payload, children
        static string ListKey(GraphElement e)
        {
            var holder = e.Owner;
            if (holder == null || e.List == null) return null;
            for (var i = 0; i < holder.Keys.Count; i++)
                if (holder.Values[i] == e.List) return holder.Keys[i];
            return null;
        }

        public void CopyRow(int id)
        {
            var e = Doc.Find(id);
            var key = e != null ? ListKey(e) : null;
            if (key == null) return;
            var clip = JValue.NewObject();
            clip.Set("tidegraphClip", JValue.Of(1));
            clip.Set("nodes", JValue.NewArray());
            clip.Set("rowsOf", JValue.Of(key));
            clip.Set("rows", JValue.ArrayOf(new[] { e.Value.Clone() }));
            Clipboard = GraphJson.Write(clip);
        }

        // Whether copied rows can go after a row: into the same kind of list, of a kind that list can have
        public bool CanPasteRows(int id, string text)
        {
            var e = Doc.Find(id);
            var clip = ClipOf(text);
            if (e == null || clip?.Arr("rows") == null || ListKey(e) != clip.Str("rowsOf")) return false;
            if (e.Role != GraphRole.Member) return true;
            var offered = GraphDocument.OfferedMemberKinds(e.Owner);
            return clip.Arr("rows").Items.All(r => offered.Contains(r.Str("kind")) || r.Str("kind") == e.Value.Str("kind"));
        }

        // Copied rows after a row, each with an id and a name of its own there
        public void PasteRows(int id)
        {
            var text = Clipboard;
            if (!CanPasteRows(id, text)) return;
            var e = Doc.Find(id);
            var rows = ClipOf(text).Arr("rows").Items;
            var owner = e.Owner?.Wire("id") ?? 0;
            Window.Edit("Paste", () =>
            {
                var at = e.List.Items.IndexOf(e.Value) + 1;
                foreach (var r in rows)
                {
                    var copy = Doc.Duplicate(r);
                    if (!string.IsNullOrEmpty(copy.Str("name"))) copy.Set("name", JValue.Of(GraphDocument.UniqueIn(e.List, copy.Str("name"))));
                    if (GraphDocument.HasPlace(copy)) PlaceNew(copy, GraphDocument.OrderOf(e));
                    e.List.Items.Insert(at++, copy);
                }
            }, owner);
        }

        // Whether what's copied can be pasted into a card or a frame: rows into a card that can have them,
        // declarations beside it, nodes into a frame
        public bool CanPasteInto(GraphItem item, string text)
        {
            var clip = ClipOf(text);
            if (clip == null) return false;
            if (item is GraphFrame frame) return frame.Graph != null && (clip.Arr("nodes")?.Items.Count ?? 0) + (clip.Arr("notes")?.Items.Count ?? 0) > 0;
            if (clip.Arr("decls") != null) return true;
            var rows = clip.Arr("rows");
            if (rows == null || item.Element == null) return false;
            var key = clip.Str("rowsOf");
            if (item.Element.Value.Arr(key) == null && !GraphSchema.HasKey(item.Element.Value.Str("kind"), false, key)) return false;
            var offered = key == "members" ? GraphDocument.OfferedMemberKinds(item.Element.Value) : null;
            return offered == null || rows.Items.All(r => offered.Contains(r.Str("kind")));
        }

        public void PasteInto(GraphItem item)
        {
            var text = Clipboard;
            if (!CanPasteInto(item, text)) return;
            var clip = ClipOf(text);
            if (item is GraphFrame frame)
            {
                Paste(ToBoard(frame.At + frame.FreePlace(new Vector2(40, 0), new Vector2(220, 100))));
                return;
            }
            if (clip.Arr("decls") != null)
            {
                PasteDeclarations(item.At + new Vector2(0, item.Size.y + GraphLook.CardGap));
                return;
            }
            var d = item.Element.Value;
            var key = clip.Str("rowsOf");
            Window.Edit("Paste", () =>
            {
                var list = d.Arr(key);
                if (list == null) d.Set(key, list = JValue.NewArray(), GraphDocument.OrderOf(item.Element));
                foreach (var r in clip.Arr("rows").Items)
                {
                    var copy = Doc.Duplicate(r);
                    if (!string.IsNullOrEmpty(copy.Str("name"))) copy.Set("name", JValue.Of(GraphDocument.UniqueIn(list, copy.Str("name"))));
                    list.Items.Add(copy);
                }
            }, item.Id);
        }

        // A declaration dragged out of My game and dropped on the board: a component or a singleton on a frame's
        // entry is something its code needs; a function in a frame, a call of it there; a constant on an input,
        // what goes in it. Anywhere else, the board says what it can go on.
        public void DropFromMyGame(int id, Vector2 panelPosition)
        {
            var d = Doc?.Find(id);
            if (d == null || d.Role != GraphRole.Decl) return;
            var kind = d.Value.Str("kind");
            var name = d.Value.Str("name");
            var picked = panel?.Pick(panelPosition);
            var view = GraphLook.Up<GraphNodeView>(picked);
            var frame = GraphLook.Up<GraphFrame>(picked);
            switch (kind)
            {
                case "component":
                case "singleton":
                {
                    if (frame == null || frame.IsValue || frame.Routine == null || !GraphSchema.HasParams(frame.Routine.Str("kind"), frame.Owner?.Role != GraphRole.Decl))
                    {
                        Window.Say($"{name} goes on a frame's entry (\"{GraphSchema.Sentence("system", false)}…\"): its code then needs it and reads it by name.", true);
                        return;
                    }
                    if (GraphDocument.ReadsOf(frame.Routine).Any(r => r.Type == name))
                    {
                        Window.Say($"{frame.Title} needs a {name} already.");
                        return;
                    }
                    frame.AddParam(frame.Routine.Arr("params"), name, "read");
                    Reveal(frame.Id);
                    return;
                }
                case "function":
                {
                    if (frame == null || frame.Graph == null || frame.Level != GraphLevel.Near)
                    {
                        Window.Say($"A call of {name} is made in a frame: drop it inside one.", true);
                        return;
                    }
                    var count = d.Value.Arr("params")?.Items.Count ?? 0;
                    var statement = frame.Slot.IsBody;
                    var entry = new GraphSearchEntry
                    {
                        Title = name, Statement = statement,
                        Make = (doc, graph, at) =>
                        {
                            var call = doc.AddNode(graph, "call", at.x - (statement ? 220 : 0), at.y + (statement ? 60 : 0));
                            var order = GraphSchema.NodeOrder("call");
                            call.Set("name", JValue.Of(name), order);
                            if (count > 0) call.Set("args", JValue.ArrayOf(Enumerable.Range(0, count).Select(_ => JValue.Null())), order);
                            if (!statement) return call;
                            var eval = doc.AddNode(graph, "eval", at.x, at.y);
                            eval.Set("value", JValue.Of(call.Wire("id")), GraphSchema.NodeOrder("eval"));
                            return eval;
                        },
                    };
                    Insert(frame, entry, FrameLocal(frame, this.WorldToLocal(panelPosition)), null);
                    return;
                }
                case "const":
                {
                    PortView input = null;
                    for (var e = picked; e != null && input == null; e = e.parent)
                        if (e.ClassListContains("tg-port-row") && e.Q<PortView>() is PortView pin && pin.Port.Input && !pin.Port.Exec) input = pin;
                    if (input == null || view == null)
                    {
                        Window.Say($"{name} goes in an input of a node: drop it on the input's row.", true);
                        return;
                    }
                    var f = input.Frame;
                    var owner = input.Port.Node == GraphFrame.ResultId ? null : f.ById.TryGetValue(input.Port.Node, out var n) ? n : null;
                    if (owner == null && input.Port.Node != GraphFrame.ResultId) return;
                    var label = GraphNodeView.LabelOf(Window.Checked, owner, input.Port.Path, input.Port.Label);
                    f.PutPath(owner, input.Port.Path, new List<GraphFrame.Part> { new GraphFrame.Part { Kind = "name", Name = name, Type = d.Value.Str("type") } }, label);
                    return;
                }
                default:
                    Window.Say("Components and singletons go on a frame's entry, functions into a frame, constants onto an input.");
                    return;
            }
        }

        // The code whose variables My game lists: the frame what's selected is in
        internal GraphFrame CodeInFocus => Active != null && !Active.IsValue && Active.Graph != null && Active.parent != null ? Active : null;

        // What My game says of a variable dragged out: Get or Set it, Blueprint's way
        internal const string GetWord = "Get", SetWord = "Set";

        // A variable of a frame's code, or a field of one, dragged out of My game and dropped: on an input of that
        // code, a Get of it wired in; elsewhere in that code, "Get <name>" or "Set <name>" asked (only a Get when
        // it can't be changed), or a Get at once with Ctrl and a Set with Alt
        public GraphOptionList DropVariable(GraphFrame frame, List<GraphFrame.Part> parts, Vector2 panelPosition, bool get, bool set)
        {
            if (frame == null || frame.Graph == null || parts.Count == 0) return null;
            var text = GraphFrame.PathText(parts);
            var picked = panel?.Pick(panelPosition);
            PortView input = null;
            for (var e = picked; e != null && input == null; e = e.parent)
            {
                if (e is PortView pin && pin.Port.Input && !pin.Port.Exec && pin.Port.Path != null) input = pin;
                else if (e.ClassListContains("tg-port-row") && e.Q<PortView>() is PortView row && row.Port.Input && !row.Port.Exec && row.Port.Path != null) input = row;
            }
            if (input != null)
            {
                if (input.Frame != frame)
                {
                    Window.Say($"{text} is what {frame.Title}'s code has: it can only go in an input there.", true);
                    return null;
                }
                var owner = input.Port.Node == GraphFrame.ResultId ? null : frame.ById.TryGetValue(input.Port.Node, out var n) ? n : null;
                if (owner == null && input.Port.Node != GraphFrame.ResultId) return null;
                if (owner?.Str("k") == "assign" && input.Port.Path.Parts.Length == 1 && input.Port.Path.Key == "target" && !frame.Changeable(parts))
                {
                    Window.Say(frame.WhyNotChangeable(parts), true);
                    return null;
                }
                frame.PutPath(owner, input.Port.Path, parts, GraphNodeView.LabelOf(Window.Checked, owner, input.Port.Path, input.Port.Label));
                return null;
            }
            var boardPoint = this.WorldToLocal(panelPosition);
            if (FrameAt(boardPoint) != frame || frame.Level != GraphLevel.Near)
            {
                Window.Say($"{text} is what {frame.Title}'s code has: drop it in that frame.", true);
                return null;
            }
            return GetOrSet(frame, parts, FrameLocal(frame, boardPoint), get, set, panelPosition);
        }

        // A variable dropped in its code at a place (as its nodes are drawn): a Get with `get`, a Set with `set`, else
        // the menu that asks which, opened where it was dropped
        internal GraphOptionList GetOrSet(GraphFrame frame, List<GraphFrame.Part> parts, Vector2 at, bool get, bool set, Vector2? menuAt = null)
        {
            var text = GraphFrame.PathText(parts);
            if (get)
            {
                frame.MakeGet(parts, at);
                return null;
            }
            if (set)
            {
                frame.MakeSet(parts, at);
                return null;
            }
            var changeable = frame.Changeable(parts) && frame.Slot.IsBody;
            var options = new List<GraphOption>
            {
                new GraphOption { Title = $"{GetWord} {text}", Detail = "Ctrl", Help = "Its value, to wire into any number of inputs", Pick = () => frame.MakeGet(parts, at) },
                new GraphOption { Title = $"{SetWord} {text}", Detail = "Alt", Off = !changeable, Help = changeable ? "A step that changes it" : frame.WhyNotChangeable(parts), Pick = () => frame.MakeSet(parts, at) },
            };
            // Where it was dropped (not laid out, the board's corner)
            var where = menuAt ?? frame.LocalToWorld(at);
            if (float.IsNaN(where.x) || float.IsNaN(where.y)) where = Vector2.zero;
            return GraphMenu.Open(Window, options, null, where);
        }

        // A Set whose value was just picked (notes/visual-scripting.md, "Round three": only what's valid in a Set):
        // once a check of the file as it is says the value's type, and the value doesn't work with the way the Set
        // changes what it changes but works another way (a float into a float3 with "becomes"), its operators are
        // asked next, offering only those. Up to three checks wait for the value's type.
        int watchedSet, watchedChecks;

        internal void WatchOperator(JValue owner, WirePath path)
        {
            if (owner?.Str("k") != "assign" || path == null || path.Parts.Length != 1 || path.Key != "value") return;
            watchedSet = owner.Wire("id");
            watchedChecks = 0;
        }

        // A check came back: the Set picked into, if its value now doesn't fit its operator
        internal void AfterCheck()
        {
            var check = Window.Checked;
            if (watchedSet == 0 || check == null || Doc == null || check.Json != Doc.Text) return;
            var set = watchedSet;
            var frame = FrameOf(set);
            var n = frame?.Graph != null ? GraphDocument.NodeIn(frame.Graph, set) : null;
            if (n == null || n.Wire("value") == 0)
            {
                watchedSet = 0;
                return;
            }
            frame.Classify();
            var value = frame.TypeAt(n.Wire("value"));
            if (value == null && ++watchedChecks < 3) return;
            watchedSet = 0;
            var target = frame.TypeAt(n.Wire("target"));
            if (value == null || target == null || value == target) return;
            TideGraphLanguage.Operators(Window, target, value, (ops, why) =>
            {
                var ways = GraphLists.Strings(ops?.Arr("assign")).ToList();
                var now = frame.Graph != null ? GraphDocument.NodeIn(frame.Graph, set) : null;
                if (ways.Count == 0 || now == null || ways.Contains(now.Str("op") ?? "=") || Window.Popup != null) return;
                var view = frame.ViewOf(set);
                var op = view?.Q("in:value")?.Q<Button>("op");
                frame.PickOperator(view, now, (VisualElement)op ?? (VisualElement)view ?? frame);
            });
        }

        // A property's code for when it's read or set, as a frame of its own
        public void AddAccessor(GraphElement property, string word)
        {
            var m = property.Value;
            JValue made = null;
            Window.Edit("Add " + word, () =>
            {
                var list = m.Arr("accessors");
                if (list == null) m.Set("accessors", list = JValue.NewArray(), GraphSchema.MemberOrder("property"));
                list.Items.Add(made = Doc.NewAccessor(word));
                PlaceNew(made, GraphSchema.AccessorOrder);
            });
            if (made != null) Reveal(made.Wire("id"));
        }

        // Takes a pin's links off: an input's wire or what's in it, an output's wires, a parameter's or a local's
        // reads, a step's flow
        public void BreakLinks(PortView pin)
        {
            var frame = pin.Frame;
            var g = frame.Graph;
            if (g == null) return;
            var p = pin.Port;
            if (p.IsRead)
            {
                frame.Classify();
                var reads = frame.ById.Values.Where(n => p.Local ? n.Str("k") == "local" && n.Wire("ref") == p.Node : n.Str("k") == "name" && !n.Flag("local") && n.Str("name") == p.Name).Select(n => n.Wire("id")).ToList();
                if (reads.Count == 0) return;
                Window.Edit("Break links", () =>
                {
                    // Every input that shows it, or a path from it, or has it wired, is emptied
                    bool Reads(int v) => reads.Contains(v) || frame.ChainOf(v).Overlaps(reads);
                    foreach (var n in GraphDocument.Nodes(g).Items.ToList())
                        foreach (var port in GraphPorts.Of(n))
                        {
                            if (!port.Input || port.Exec || port.Path == null) continue;
                            var v = port.Path.Get(n);
                            // A path's own parts are emptied with the input that shows them
                            if (v == 0 || frame.Hidden.Contains(n.Wire("id")) || reads.Contains(n.Wire("id")) || !Reads(v)) continue;
                            port.Path.Set(n, 0);
                            frame.DropUnread(g, v);
                        }
                    if (g.Wire("root") != 0 && Reads(g.Wire("root")))
                    {
                        var was = g.Wire("root");
                        g.Set("root", JValue.Null());
                        frame.DropUnread(g, was);
                    }
                    Doc.DeleteNodes(g, reads.Where(id => GraphDocument.NodeIn(g, id) != null && GraphDocument.Readers(g, id) == 0).ToList());
                }, frame.Id);
                return;
            }
            Window.Edit("Break links", () => frame.Unwire(p), frame.Id);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Problems: the last check's, each on what it's about

        readonly Dictionary<int, List<GraphDiagnostic>> problemsById = new Dictionary<int, List<GraphDiagnostic>>();
        IReadOnlyList<GraphDiagnostic> shownProblems = new List<GraphDiagnostic>();
        static readonly List<GraphDiagnostic> none = new List<GraphDiagnostic>();

        // Shows a check's problems, and no other: every mark of the check before goes
        public void SetProblems(IReadOnlyList<GraphDiagnostic> diagnostics)
        {
            shownProblems = diagnostics ?? new List<GraphDiagnostic>();
            problemsById.Clear();
            void Put(int id, GraphDiagnostic d)
            {
                if (id == 0) return;
                if (!problemsById.TryGetValue(id, out var list)) problemsById[id] = list = new List<GraphDiagnostic>();
                if (!list.Contains(d)) list.Add(d);
            }
            foreach (var d in shownProblems)
            {
                var e = Doc?.Find(d.Id);
                if (e == null) continue;
                d.Said = Said(e, d);
                Put(d.Id, d);
                // An attribute's is its element's too; a value's node's is its row's
                if (e.Role == GraphRole.Attribute) Put(e.Owner?.Wire("id") ?? 0, d);
                if ((e.Role == GraphRole.Node || e.Role == GraphRole.Note) && !e.Slot.IsBody) Put(e.Slot.Owner, d);
            }
            foreach (var item in items.Values.Concat(values.Values))
                if (item.Level != GraphLevel.None) item.ShowProblems();
        }

        // An input with nothing in it, as the check found it (its node and the input's key): said in the editor's
        // words, by the name the input has on its row. Anything else is the compiler's to say.
        string Said(GraphElement e, GraphDiagnostic d)
        {
            if (d.Port == null || e.Role != GraphRole.Node) return null;
            var path = d.Index >= 0 ? new WirePath(d.Port, d.Index) : new WirePath(d.Port);
            if (path.Get(e.Value) != 0) return null;
            var label = GraphNodeView.LabelOf(Window.Checked, e.Value, path, d.Port);
            return string.IsNullOrEmpty(label) ? "Pick what goes in here" : $"Pick what goes in '{label}'";
        }

        public List<GraphDiagnostic> Problems(int id) => id != 0 && problemsById.TryGetValue(id, out var list) ? list : none;

        public bool HasProblems(GraphItem item) => problemsById.Count > 0 && item.Carries().Any(problemsById.ContainsKey);

        // Whether a member with code has problems in it: its row on its card says so
        public bool ProblemsUnder(int id)
        {
            if (problemsById.Count == 0) return false;
            var frame = Frame(id);
            if (frame != null && HasProblems(frame)) return true;
            var accessors = Doc.Get(id)?.Arr("accessors");
            return accessors != null && accessors.Items.Any(a => Frame(a.Wire("id")) is GraphFrame f && HasProblems(f));
        }

        // Every message drawn on the board now: nothing of a check before the last is among them
        internal List<string> ProblemsShown()
        {
            var said = new List<string>();
            this.Query<VisualElement>(className: "tg-problem").ForEach(e => said.Add(e is Label l ? l.text : e.tooltip));
            return said;
        }
    }

    // A namespace block: a tinted region around what's declared in it, with its name
    sealed class GraphSpace : VisualElement
    {
        public readonly JValue Block;
        readonly GraphBoard board;
        Rect rect;

        public GraphSpace(GraphBoard board, JValue block)
        {
            this.board = board;
            Block = block;
            AddToClassList("tg-space");
            pickingMode = PickingMode.Ignore;
            var head = GraphLook.Row("tg-space-head");
            head.Add(GraphLook.Kind(GraphSchema.DeclTitle("namespace"), "More"));
            var e = board.Window.Doc.Find(block.Wire("id"));
            var name = GraphLook.Inline(block.Str("name"), typed =>
            {
                var clean = GraphLook.DottedName(typed);
                if (clean.Length == 0) board.Changed(null);
                else board.Window.Edit("Rename", () => block.Set("name", JValue.Of(clean), GraphSchema.DeclOrder("namespace")));
            }, "name", "tg-inline--title");
            name.style.flexGrow = 0;
            name.style.minWidth = 80;
            head.Add(name);
            head.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            head.Add(GraphLook.IconButton("plus", "Add something to this namespace", () => board.OpenAdd(board.ToBoard(rect.min + new Vector2(20, 50)))));
            if (e?.List != null) head.Add(GraphLook.IconButton("close", "Take the block away: what's in it stays, outside it", () => board.Window.Edit("Remove namespace", () =>
            {
                var at = e.List.Items.IndexOf(block);
                e.List.Items.RemoveAt(at);
                e.List.Items.InsertRange(at, block.Arr("decls")?.Items ?? new List<JValue>());
            })));
            Add(head);
        }

        public Rect Rect
        {
            get => rect;
            set
            {
                rect = value;
                style.left = value.x;
                style.top = value.y;
                style.width = value.width;
                style.height = value.height;
            }
        }

        // The ids of the declarations (and their members with code) in the block; `deep` those of the blocks in it too
        public IEnumerable<int> Declared(bool deep)
        {
            var decls = Block.Arr("decls");
            if (decls == null) yield break;
            foreach (var d in decls.Items)
            {
                if (!d.IsObject) continue;
                if (d.Str("kind") == "namespace")
                {
                    if (!deep) continue;
                    foreach (var id in new GraphSpace(board, d).Declared(true)) yield return id;
                    continue;
                }
                yield return d.Wire("id");
                var members = d.Arr("members");
                if (members == null) continue;
                foreach (var m in members.Items)
                {
                    if (!m.IsObject) continue;
                    yield return m.Wire("id");
                    var accessors = m.Arr("accessors");
                    if (accessors != null) foreach (var a in accessors.Items) yield return a.Wire("id");
                }
            }
        }

        public bool Holds(JValue block) => Block.Arr("decls")?.Items.Contains(block) ?? false;
    }
}
