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
            Add(GraphLook.Text(GraphSchema.Word("details.file"), "tg-popup-head"));
            Add(GraphLook.Text(GraphSchema.Word("prompt.namespace.help"), "tg-popup-help"));
            var space = GraphLook.Inline(doc.Root.Str("namespace"), typed => Window.Edit("Namespace", () => doc.Root.SetString("namespace", GraphLook.DottedName(typed), GraphSchema.RootOrder)), GraphSchema.Word("details.none"), "tg-inline--value");
            space.name = "namespace";
            space.style.marginLeft = space.style.marginRight = 8;
            Add(space);
            Add(GraphLook.Text(GraphSchema.Word("menu.uses.summary"), "tg-popup-help").Class("tg-using-help"));
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

    // What the board's gestures are, said once asked: the schema's words (help.1, help.2...), each its keys and
    // what they do
    sealed class GraphShortcuts : GraphPopup
    {
        public GraphShortcuts()
        {
            style.width = 380;
            Add(GraphLook.Text(GraphSchema.Word("help.title"), "tg-popup-head"));
            for (var i = 1; GraphSchema.HasWord("help." + i); i++)
            {
                var what = GraphSchema.Word("help." + i);
                var row = GraphLook.Row("tg-option");
                var k = GraphLook.Text(GraphSchema.WordKeys("help." + i), "tg-option-title");
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
