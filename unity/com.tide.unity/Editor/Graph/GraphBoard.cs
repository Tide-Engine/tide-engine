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
                    if (above.At.x >= item.At.x + item.Size.x || item.At.x >= above.At.x + above.Size.x) continue;
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
                if (wants.Any(w => !items.ContainsKey(w.id)) || drag == Drag.MoveItems) return;
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
            {
                foreach (var item in items.Values.Concat(values.Values).ToList()) Again(item);
                return;
            }
            var done = new HashSet<GraphItem>();
            foreach (var owner in owners)
                foreach (var item in ItemsFor(owner))
                    if (done.Add(item)) Again(item);
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
            hint.text = empty ? "" : "Right-click or Space: add   ·   Drag from a pin: wire   ·   Middle button or Alt+drag: move around   ·   F: fit";
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
                var where = at ?? FreeSpot(IsCode(kind, false));
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
            schedule.Execute(() =>
            {
                var found = Item(item)?.Q<VisualElement>(name);
                if (found == null) return;
                found.Focus();
                if (found is TextField field) field.schedule.Execute(field.SelectAll);
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // Selecting

        void ShowSelection()
        {
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
            Nodes.Clear();
            Active = null;
            ShowSelection();
        }

        public void SelectItem(GraphItem item, bool additive)
        {
            Nodes.Clear();
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
        Vector2 nodeMin;
        (GraphFrame frame, Vector2 at, bool any)? rightDown;

        // The wire being drawn: the end that stays, and what was wired before it was taken up
        PortView wireFrom;
        internal Vector2 WireEnd;  // In the board's space
        Action wireUndo;           // Unwires what was taken up, when it's dropped on nothing

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
            if (e.button == 1)
            {
                if (view != null && view.Id > 0)
                {
                    if (!(Active == view.Frame && Nodes.Contains(view.Id))) SelectNode(view.Frame, view.Id, false);
                    var list = new GraphOptionList(null, null, null, 300);
                    list.Set(view.MenuOptions());
                    Window.Open(list, new Rect(e.position, Vector2.zero));
                }
                else if (item is GraphCard card)
                {
                    SelectItem(card, false);
                    var list = new GraphOptionList(null, null, null, 300);
                    list.Set(card.MenuOptions());
                    Window.Open(list, new Rect(e.position, Vector2.zero));
                }
                else rightDown = (frame, mouse, true);
                e.StopPropagation();
                return;
            }
            if (e.button != 0) return;
            e.StopPropagation();
            if (frame != null && (note != null || (view != null && view.Id > 0)))
            {
                var id = note != null ? note.Id : view.Id;
                if (additive) SelectNode(frame, id, true);
                else if (!(Active == frame && Nodes.Contains(id))) SelectNode(frame, id, false);
                if (additive) return;
                drag = Drag.MoveNodes;
                moved = false;
                dragStart = mouse;
                nodeStart.Clear();
                nodeMin = Vector2.zero;
                foreach (var v in frame.Views.Values) nodeMin = Vector2.Min(nodeMin, v.At);
                foreach (var n in Nodes)
                {
                    if (frame.Views.TryGetValue(n, out var nv)) nodeStart[n] = nv.Shown;
                    else if (frame.Notes.TryGetValue(n, out var note2)) nodeStart[n] = note2.Shown;
                }
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
                    foreach (var pair in nodeStart)
                    {
                        // Not left of or above where the frame's nodes start
                        var to = Vector2.Max(pair.Value + d, Active.Origin + nodeMin);
                        if (Active.Views.TryGetValue(pair.Key, out var v)) v.MoveTo(to);
                        else if (Active.Notes.TryGetValue(pair.Key, out var note)) note.MoveTo(to);
                    }
                    Active.Arrange(false);
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
                        if (frame != null && frame.Graph != null && frame.Level == GraphLevel.Near) OpenSearch(frame, this.ChangeCoordinatesTo(frame, mouse), null);
                        else if (frame == null) OpenAdd(mouse);
                        else Window.Say(frame.Graph == null ? "Its code is written in C: there's nothing to add here." : "Zoom in to add to this frame.");
                    }
                    rightDown = null;
                    break;
            }
        }

        void CommitNodeMove()
        {
            var frame = Active;
            var graph = frame.Graph;
            if (graph == null) return;
            var origin = frame.Origin;
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
            }, frame.Id);
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
            // The inputs show where a wire can end
            AddToClassList("tg-wiring");
        }

        PortView PortUnder(Vector2 panelPosition)
        {
            for (var e = panel?.Pick(panelPosition); e != null; e = e.parent)
            {
                if (e is PortView p) return p;
                // An input's row is the input: its pin shows only once it has a wire
                if (e.ClassListContains("tg-port-row") && e.Q<PortView>() is PortView of && of.Port.Input && !of.Port.Exec) return of;
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
            var frame = from.Frame;
            frame.MarkWires();
            var target = PortUnder(panelPosition);
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
            var local = this.ChangeCoordinatesTo(frame, this.WorldToLocal(panelPosition));
            if (local.x < 0 || local.y < 0 || local.x > frame.Size.x || local.y > frame.Size.y)
            {
                Window.Say(WhyNotAcross(frame, null), true);
                return;
            }
            OpenSearch(frame, local, from.Port);
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
            // Left of it and lower, where nothing is yet
            var at = view != null ? view.Shown + new Vector2(-250, view.SizeNow.y + 24) : frame.Origin;
            for (var tries = 0; tries < 12 && frame.Views.Values.Any(v => new Rect(v.Shown, v.SizeNow).Overlaps(new Rect(at, new Vector2(200, 80)))); tries++) at.y += 90;
            var context = new GraphSearchContext { Doc = Doc, Graph = graph, Slot = frame.Slot, Window = Window, Pending = port, Frame = frame };
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
        public JValue Insert(GraphFrame frame, GraphSearchEntry entry, Vector2 frameLocal, GraphPort pending)
        {
            var graph = frame.Graph;
            // A node for an input goes left of its reader, which can be left of every node there is
            var at = pending != null && pending.Input && !pending.Exec ? frameLocal - frame.Origin : Vector2.Max(frameLocal - frame.Origin, Vector2.zero);
            JValue made = null;
            Window.Edit("Add " + entry.Title, () =>
            {
                made = entry.Make(Doc, graph, at);
                if (made != null && pending != null) WireNew(frame, graph, made, pending);
            }, frame.Id);
            if (made != null && made.Has("k")) SelectNode(frame, made.Wire("id"), false);
            return made;
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

        void OnKey(KeyDownEvent e)
        {
            var target = e.target as VisualElement;
            if (target is TextField || target?.GetFirstAncestorOfType<TextField>() != null || Window.Popup != null) return;
            var action = e.ctrlKey || e.commandKey;
            switch (e.keyCode)
            {
                case KeyCode.Delete:
                case KeyCode.Backspace:
                    DeleteSelection();
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
                    Window.ShowContents(true);
                    break;
                case KeyCode.F:
                    FrameSelectionOrAll();
                    break;
                case KeyCode.F2:
                    if (Chosen.Count == 1) FocusLater(Chosen.First(), "name");
                    break;
                case KeyCode.Space:
                {
                    var frame = FrameAt(mouse) ?? (Active != null && Nodes.Count > 0 ? Active : null);
                    if (frame != null && frame.Graph != null && frame.Level == GraphLevel.Near) OpenSearch(frame, this.ChangeCoordinatesTo(frame, mouse), null);
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

        public void DeleteSelection()
        {
            if (Active != null && Nodes.Count > 0)
            {
                var frame = Active;
                var graph = frame.Graph;
                if (graph == null) return;
                var ids = frame.With(Nodes);
                ids.UnionWith(Nodes);
                Nodes.Clear();
                Window.Edit("Delete", () => Doc.DeleteNodes(graph, ids), frame.Id);
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
                    copy.Set("x", JValue.Of(Mathf.Round(item.At.x + 30)));
                    copy.Set("y", JValue.Of(Mathf.Round(item.At.y + item.Size.y + 30)));
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
            var at = frame.World.Contains(ToWorld(boardPoint)) ? Vector2.Max(this.ChangeCoordinatesTo(frame, boardPoint) - frame.Origin, Vector2.zero) : min + new Vector2(40, 40);
            List<int> made = null;
            Window.Edit("Paste", () => made = Doc.Paste(graph, text, at - min), frame.Id);
            SelectMade(frame, made);
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
            if (Active != null && Nodes.Count > 0)
            {
                var frame = Active;
                var graph = frame.Graph;
                if (graph == null) return;
                var text = GraphDocument.Copy(graph, frame.With(Nodes), new HashSet<int>(Nodes.Where(frame.Notes.ContainsKey)));
                List<int> made = null;
                Window.Edit("Duplicate", () => made = Doc.Paste(graph, text, new Vector2(40, 40)), frame.Id);
                SelectMade(frame, made);
                return;
            }
            foreach (var id in Chosen.ToList()) Duplicate(id);
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
