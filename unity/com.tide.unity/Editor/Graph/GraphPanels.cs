using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A panel at a side of the board, closed until it's asked for: its title and a way to close it
    class GraphSide : VisualElement
    {
        protected readonly TideGraphWindow Window;
        protected readonly VisualElement Body;

        protected GraphSide(TideGraphWindow window, string title, bool left)
        {
            Window = window;
            AddToClassList("tg-side");
            AddToClassList(left ? "tg-side--left" : "tg-side--right");
            RegisterCallback<PointerDownEvent>(e => e.StopPropagation());
            RegisterCallback<WheelEvent>(e => e.StopPropagation());
            var head = GraphLook.Row("tg-side-head");
            head.Add(GraphLook.Text(title, "tg-side-title"));
            head.Add(GraphLook.IconButton("close", "Close", Close));
            Add(head);
            Body = new VisualElement { style = { flexGrow = 1, flexShrink = 1 } };
            Add(Body);
        }

        public virtual void Close()
        {
            RemoveFromHierarchy();
            Window.Board?.Focus();
        }
    }

    // Contents: the file's declarations by group, and a search that jumps to one. Picking one pans the board to it.
    sealed class GraphContents : GraphSide
    {
        readonly TextField query;
        readonly ScrollView list;
        readonly List<(int id, string title)> shown = new List<(int, string)>();

        public GraphContents(TideGraphWindow window) : base(window, "Contents", true)
        {
            query = new TextField();
            query.AddToClassList("tg-search");
            query.textEdition.placeholder = "Jump to…";
            query.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                Fill();
            });
            query.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode == KeyCode.Escape)
                {
                    Close();
                    e.StopPropagation();
                }
                if (e.keyCode != KeyCode.Return && e.keyCode != KeyCode.KeypadEnter) return;
                e.StopPropagation();
                if (shown.Count > 0) Window.Reveal(shown[0].id);
            }, TrickleDown.TrickleDown);
            Body.Add(query);
            list = new ScrollView { style = { flexGrow = 1 } };
            Body.Add(list);
            Fill();
        }

        public void FocusSearch() => query.schedule.Execute(() => query.Focus());

        // What it lists now, in order: the tests' view of it
        internal IReadOnlyList<(int id, string title)> Shown => shown;

        internal void Type(string text) => query.value = text;

        public void Fill()
        {
            list.Clear();
            shown.Clear();
            var doc = Window.Doc;
            if (doc == null) return;
            var typed = (query.value ?? "").Trim().ToLowerInvariant();
            var rows = new List<(string group, int id, string title, string kind, int depth)>();
            void Decls(JValue decls, string space)
            {
                if (decls == null) return;
                foreach (var d in decls.Items)
                {
                    if (!d.IsObject) continue;
                    var kind = d.Str("kind");
                    var e = doc.Find(d.Wire("id"));
                    if (e == null) continue;
                    if (kind == "namespace")
                    {
                        Decls(d.Arr("decls"), (space.Length > 0 ? space + "." : "") + d.Str("name"));
                        continue;
                    }
                    var group = GraphSchema.DeclGroup(kind) ?? "";
                    rows.Add((group, e.Id, (space.Length > 0 ? space + "." : "") + doc.TitleOf(e), GraphSchema.DeclTitle(kind), 0));
                    var members = d.Arr("members");
                    if (members == null || kind == "enum") continue;
                    foreach (var m in members.Items)
                    {
                        if (!m.IsObject) continue;
                        var mk = m.Str("kind");
                        if (GraphSchema.HasBody(mk, true) && doc.Find(m.Wire("id")) is GraphElement me) rows.Add((group, me.Id, doc.TitleOf(me), GraphSchema.MemberTitle(mk), 1));
                        var accessors = m.Arr("accessors");
                        if (accessors == null) continue;
                        foreach (var a in accessors.Items)
                            if (doc.Find(a.Wire("id")) is GraphElement ae) rows.Add((group, ae.Id, doc.TitleOf(ae), GraphSchema.MemberTitle(mk), 1));
                    }
                }
            }
            Decls(doc.Root.Arr("decls"), "");
            foreach (var group in GraphSchema.Groups)
            {
                var of = rows.Where(r => r.group == group.Name && (typed.Length == 0 || (r.title + " " + r.kind).ToLowerInvariant().Contains(typed))).ToList();
                if (of.Count == 0) continue;
                var heading = GraphLook.Row("tg-section");
                heading.Add(GraphLook.Text(group.Title.ToUpperInvariant(), "tg-section-title"));
                heading.Add(GraphLook.Text(group.Summary, "tg-section-summary"));
                list.Add(heading);
                foreach (var r in of)
                {
                    var id = r.id;
                    var row = GraphLook.Row("tg-option");
                    row.style.paddingLeft = 10 + r.depth * 14;
                    row.Add(GraphLook.Text(r.title, "tg-option-title"));
                    row.Add(GraphLook.Text(r.kind, "tg-option-detail"));
                    var item = Window.Board?.Item(id);
                    if (item != null && Window.Board.HasProblems(item)) row.Add(new GraphIcon("warn") { style = { color = new StyleColor(new Color(1f, 0.45f, 0.4f)), marginLeft = 6 }, tooltip = "It has a problem" });
                    row.RegisterCallback<PointerDownEvent>(e =>
                    {
                        if (e.button != 0) return;
                        e.StopPropagation();
                        Window.Reveal(id);
                    });
                    list.Add(row);
                    shown.Add((id, r.title));
                }
            }
            if (shown.Count == 0) list.Add(GraphLook.Text(typed.Length > 0 ? $"Nothing here is called '{query.value.Trim()}'." : "Nothing here yet.", "tg-empty"));
        }
    }

    // The Tide the file stands for: off the screen unless it's asked for
    sealed class GraphTideView : GraphSide
    {
        readonly Label text, note;

        public GraphTideView(TideGraphWindow window) : base(window, "As Tide", false)
        {
            note = GraphLook.Text("", "tg-popup-help");
            note.style.paddingTop = 6;
            Body.Add(note);
            var scroll = new ScrollView(ScrollViewMode.VerticalAndHorizontal) { style = { flexGrow = 1 } };
            text = new Label { enableRichText = false };
            text.selection.isSelectable = true;
            text.AddToClassList("tg-tide");
            text.style.unityFontDefinition = FontDefinition.FromFont(GraphLook.Mono);
            scroll.Add(text);
            Body.Add(scroll);
            Show();
        }

        internal string Text => text.text;

        public void Show()
        {
            var check = Window.Checked;
            note.text = check == null ? "Being worked out…" : check.Failure != null && check.Text.Length == 0 ? check.Failure : "What this file is to Tide: the same as if it were written as text. It follows the board.";
            text.text = check?.Text ?? "";
        }

        public override void Close()
        {
            base.Close();
            Window.TideClosed();
        }
    }

    // The file's own lines: the namespace everything in it is in, and the namespaces it uses
    sealed class GraphFileSettings : GraphPopup
    {
        public GraphFileSettings(TideGraphWindow window)
        {
            Window = window;
            style.width = 320;
            Fill();
        }

        void Fill()
        {
            Clear();
            var doc = Window.Doc;
            Add(GraphLook.Text("This file", "tg-popup-head"));
            Add(GraphLook.Text("The namespace everything in this file is in. Leave it empty for none.", "tg-popup-help"));
            var space = GraphLook.Inline(doc.Root.Str("namespace"), typed => Window.Edit("Namespace", () => doc.Root.SetString("namespace", GraphLook.DottedName(typed), GraphSchema.RootOrder)), "no namespace", "tg-inline--value");
            space.name = "namespace";
            space.style.marginLeft = space.style.marginRight = 8;
            Add(space);
            Add(GraphLook.Text("The namespaces whose names it uses without spelling them out.", "tg-popup-help").Class("tg-using-help"));
            var uses = doc.Root.Arr("using");
            for (var i = 0; uses != null && i < uses.Items.Count; i++)
            {
                var at = i;
                var row = GraphLook.Row("tg-row");
                row.Add(GraphLook.Text(uses.Items[i].String, "tg-option-title"));
                row.Add(new VisualElement { style = { flexGrow = 1 } });
                row.Add(GraphLook.IconButton("close", "Stop using it", () =>
                {
                    Window.Edit("Remove using", () =>
                    {
                        var a = doc.Root.Arr("using");
                        a.Items.RemoveAt(at);
                        if (a.Items.Count == 0) doc.Root.Remove("using");
                    });
                    Fill();
                }));
                Add(row);
            }
            var add = new TextField();
            add.AddToClassList("tg-search");
            add.name = "using";
            add.textEdition.placeholder = "Use a namespace… (Enter adds it)";
            add.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode != KeyCode.Return && e.keyCode != KeyCode.KeypadEnter) return;
                e.StopPropagation();
                Use(add.text);
            }, TrickleDown.TrickleDown);
            Add(add);
        }

        internal void Use(string typed)
        {
            var doc = Window.Doc;
            var name = GraphLook.DottedName(typed);
            if (name.Length == 0) return;
            Window.Edit("Add using", () =>
            {
                var a = doc.Root.Arr("using");
                if (a == null) doc.Root.Set("using", a = JValue.NewArray(), GraphSchema.RootOrder);
                if (!a.Items.Any(x => x.String == name)) a.Items.Add(JValue.Of(name));
            });
            Fill();
            this.Q<TextField>("using")?.Focus();
        }

        public override void Opened() => this.Q<TextField>("namespace")?.Focus();
    }

    // What the board's gestures are, said once asked
    sealed class GraphShortcuts : GraphPopup
    {
        public GraphShortcuts()
        {
            style.width = 380;
            Add(GraphLook.Text("How the board works", "tg-popup-head"));
            foreach (var (keys, what) in new[]
            {
                ("Right-click, or Space", "Add: on the empty board, something new; in a frame, a node"),
                ("Drag from a pin", "Make a wire. Dropped on nothing, it asks what to add"),
                ("Type on an input", "A number, a text in quotes, true or false"),
                ("Alt+click a pin", "Take its wires off"),
                ("Middle button, or Alt+drag", "Move around"),
                ("Wheel", "Zoom, about the pointer"),
                ("F", "Fit what's selected, or everything"),
                ("Ctrl+F", "Contents: jump to anything by name"),
                ("Del", "Delete what's selected"),
                ("Ctrl+C, Ctrl+V, Ctrl+D", "Copy, paste, duplicate"),
                ("Ctrl+Z, Ctrl+Y", "Undo, redo"),
                ("Ctrl+S", "Save"),
            })
            {
                var row = GraphLook.Row("tg-option");
                var k = GraphLook.Text(keys, "tg-option-title");
                k.style.width = 150;
                k.style.whiteSpace = WhiteSpace.Normal;
                row.Add(k);
                var w = GraphLook.Text(what, "tg-option-detail");
                w.style.whiteSpace = WhiteSpace.Normal;
                row.Add(w);
                Add(row);
            }
            focusable = true;
        }

        public override void Opened() => schedule.Execute(Focus);
    }

    // A note of a body: a box of text behind the nodes, moved by its top, sized by its corner
    sealed class GraphNoteView : VisualElement
    {
        public readonly int Id;
        public readonly JValue Note;
        readonly GraphFrame frame;
        public Vector2 Shown;
        bool selected;

        public GraphNoteView(GraphFrame frame, JValue note)
        {
            this.frame = frame;
            Note = note;
            Id = note.Wire("id");
            style.position = Position.Absolute;
            style.width = Mathf.Max(60, (float)(note.Get("w")?.Number ?? 300));
            style.height = Mathf.Max(40, (float)(note.Get("h")?.Number ?? 120));
            style.backgroundColor = new Color(0.86f, 0.74f, 0.25f, 0.16f);
            style.borderTopLeftRadius = style.borderTopRightRadius = style.borderBottomLeftRadius = style.borderBottomRightRadius = 5;
            style.borderTopWidth = style.borderBottomWidth = style.borderLeftWidth = style.borderRightWidth = 1;
            Border(new Color(0.86f, 0.74f, 0.25f, 0.5f));
            var bar = new VisualElement { style = { height = 12, backgroundColor = new Color(0.86f, 0.74f, 0.25f, 0.45f) }, tooltip = "Drag to move the note" };
            Add(bar);
            var text = new TextField { value = note.Str("text") ?? "", multiline = true, isDelayed = true };
            text.AddToClassList("tg-inline");
            text.AddToClassList("tg-text-area");
            text.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                frame.Edit("Note", () => Note.Set("text", JValue.Of(e.newValue), GraphSchema.NoteOrder));
            });
            Add(text);
            var corner = new VisualElement { style = { position = Position.Absolute, right = 0, bottom = 0, width = 12, height = 12, backgroundColor = new Color(0.86f, 0.74f, 0.25f, 0.6f) }, tooltip = "Drag to size the note" };
            var sizing = false;
            Vector2 from = default, was = default;
            corner.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0) return;
                sizing = true;
                from = e.position;
                was = Extent;
                corner.CapturePointer(e.pointerId);
                e.StopPropagation();
            });
            corner.RegisterCallback<PointerMoveEvent>(e =>
            {
                if (!sizing) return;
                var d = ((Vector2)e.position - from) / frame.Board.Zoom;
                style.width = Mathf.Max(60, was.x + d.x);
                style.height = Mathf.Max(40, was.y + d.y);
            });
            corner.RegisterCallback<PointerUpEvent>(e =>
            {
                if (!sizing) return;
                sizing = false;
                corner.ReleasePointer(e.pointerId);
                var size = Extent;
                frame.Edit("Size note", () =>
                {
                    Note.Set("w", JValue.Of(Mathf.Round(size.x)), GraphSchema.NoteOrder);
                    Note.Set("h", JValue.Of(Mathf.Round(size.y)), GraphSchema.NoteOrder);
                });
            });
            Add(corner);
        }

        public Vector2 Extent => new Vector2(style.width.value.value, style.height.value.value);
        public Vector2 At => new Vector2((float)(Note.Get("x")?.Number ?? 0), (float)(Note.Get("y")?.Number ?? 0));

        public void MoveTo(Vector2 local)
        {
            Shown = local;
            style.left = local.x;
            style.top = local.y;
        }

        void Border(Color c) => style.borderTopColor = style.borderBottomColor = style.borderLeftColor = style.borderRightColor = c;

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                Border(value ? new Color(1f, 0.69f, 0.23f) : new Color(0.86f, 0.74f, 0.25f, 0.5f));
            }
        }
    }
}
