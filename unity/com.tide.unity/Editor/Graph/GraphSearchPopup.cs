using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The search for a node to add: every kind the format has, grouped, and the graph's locals and parameters.
    // Typing narrows it (every word typed is in an entry's title, group or keywords); arrows and Enter pick.
    sealed class GraphSearchPopup : VisualElement
    {
        readonly GraphCanvas canvas;
        readonly GraphSearchContext context;
        readonly Vector2 at;
        readonly GraphPort pending;
        readonly TextField query;
        readonly ScrollView list;
        readonly List<GraphSearchEntry> all;
        readonly List<GraphSearchEntry> shown = new List<GraphSearchEntry>();
        readonly List<VisualElement> rows = new List<VisualElement>();
        int current;

        public GraphSearchPopup(GraphCanvas canvas, GraphSearchContext context, Vector2 at, GraphPort pending)
        {
            this.canvas = canvas;
            this.context = context;
            this.at = at;
            this.pending = pending;
            all = Commands().Concat(TideGraphServices.Searches.SelectMany(s => s.Entries(context))).ToList();
            style.position = Position.Absolute;
            style.left = Mathf.Max(0, at.x);
            style.top = Mathf.Max(0, at.y);
            style.width = 260;
            style.height = 320;
            style.backgroundColor = new Color(0.2f, 0.2f, 0.21f, 0.98f);
            style.borderTopWidth = style.borderBottomWidth = style.borderLeftWidth = style.borderRightWidth = 1;
            style.borderTopColor = style.borderBottomColor = style.borderLeftColor = style.borderRightColor = new Color(0, 0, 0, 0.7f);
            style.borderTopLeftRadius = style.borderTopRightRadius = style.borderBottomLeftRadius = style.borderBottomRightRadius = 4;
            query = new TextField { style = { marginLeft = 4, marginRight = 4, marginTop = 4 } };
            query.textEdition.placeholder = pending == null ? "Search for a node" : "A node to wire";
            query.RegisterValueChangedCallback(_ => Filter());
            query.RegisterCallback<KeyDownEvent>(OnKey, TrickleDown.TrickleDown);
            query.RegisterCallback<FocusOutEvent>(_ => schedule.Execute(() =>
            {
                if (panel != null && focusController?.focusedElement is VisualElement f && (f == this || Contains(f))) return;
                if (parent != null) canvas.CloseSearch();
            }).ExecuteLater(100));
            Add(query);
            list = new ScrollView { style = { flexGrow = 1 } };
            Add(list);
            RegisterCallback<PointerDownEvent>(e => e.StopPropagation());
            RegisterCallback<WheelEvent>(e => e.StopPropagation());
            // Kept inside the canvas
            canvas.RegisterCallback<GeometryChangedEvent>(Fit);
            Filter();
        }

        IEnumerable<GraphSearchEntry> Commands()
        {
            if (pending != null) yield break;
            yield return new GraphSearchEntry { Group = "Canvas", Title = "Add note", Keywords = "note comment box", Make = (doc, graph, p) => { canvas.AddNote(canvas.ToCanvas(p)); return null; } };
            if (GraphDocument.IsClip(EditorGUIUtility.systemCopyBuffer))
                yield return new GraphSearchEntry { Group = "Canvas", Title = "Paste", Keywords = "paste clipboard", Make = (doc, graph, p) => { canvas.Paste(canvas.ToCanvas(p)); return null; } };
        }

        void Fit(GeometryChangedEvent e)
        {
            var w = canvas.layout.width;
            var h = canvas.layout.height;
            if (float.IsNaN(w) || float.IsNaN(h)) return;
            style.left = Mathf.Clamp(at.x, 0, Mathf.Max(0, w - 260));
            style.top = Mathf.Clamp(at.y, 0, Mathf.Max(0, h - 320));
        }

        public void Begin()
        {
            query.schedule.Execute(() => query.Focus());
        }

        // Entries that suit the wire it's for: a value for a value's input, a statement for a flow
        bool Suits(GraphSearchEntry e)
        {
            if (pending == null) return true;
            if (pending.Exec) return e.Group == "Flow";
            if (pending.Input) return e.Group != "Flow" || e.Title.StartsWith("Call") || e.Title.StartsWith("Method");
            return true;
        }

        internal IReadOnlyList<GraphSearchEntry> Shown => shown;

        void Filter()
        {
            var words = (query.value ?? "").ToLowerInvariant().Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            shown.Clear();
            foreach (var e in all)
            {
                if (!Suits(e)) continue;
                var text = (e.Title + " " + e.Group + " " + e.Keywords).ToLowerInvariant();
                if (words.All(text.Contains)) shown.Add(e);
            }
            list.Clear();
            rows.Clear();
            string group = null;
            foreach (var e in shown)
            {
                if (e.Group != group)
                {
                    group = e.Group;
                    list.Add(new Label(group) { style = { unityFontStyleAndWeight = FontStyle.Bold, color = new Color(0.7f, 0.7f, 0.7f), paddingLeft = 4, marginTop = 3, fontSize = 10 } });
                }
                var entry = e;
                var row = new Label(e.Title) { style = { paddingLeft = 12, paddingTop = 1, paddingBottom = 1 } };
                row.RegisterCallback<PointerDownEvent>(ev =>
                {
                    Pick(entry);
                    ev.StopPropagation();
                });
                rows.Add(row);
                list.Add(row);
            }
            current = 0;
            Highlight();
        }

        void Highlight()
        {
            for (var i = 0; i < rows.Count; i++) rows[i].style.backgroundColor = i == current ? new Color(0.17f, 0.36f, 0.53f) : new Color(0, 0, 0, 0);
            if (current < rows.Count && list.panel != null) list.ScrollTo(rows[current]);
        }

        void OnKey(KeyDownEvent e)
        {
            switch (e.keyCode)
            {
                case KeyCode.DownArrow:
                    current = Mathf.Min(current + 1, rows.Count - 1);
                    Highlight();
                    break;
                case KeyCode.UpArrow:
                    current = Mathf.Max(current - 1, 0);
                    Highlight();
                    break;
                case KeyCode.Return:
                case KeyCode.KeypadEnter:
                    if (current < shown.Count) Pick(shown[current]);
                    break;
                case KeyCode.Escape:
                    canvas.CloseSearch();
                    canvas.Focus();
                    break;
                default:
                    return;
            }
            e.StopPropagation();
        }

        void Pick(GraphSearchEntry e)
        {
            if (e.Group == "Canvas")
            {
                canvas.CloseSearch();
                e.Make(context.Doc, context.Graph, canvas.ToGraph(at));
                return;
            }
            canvas.Insert(e, at, pending);
        }
    }
}
