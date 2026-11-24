using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The search for a node to add: every kind the format has, grouped, the graph's locals and parameters, and what
    // the language server's completion offers there (the members of a value a wire was dropped from, or the names,
    // functions and types in scope), which come in when it answers. Typing narrows it (every word typed is in an
    // entry's title, group or keywords) and ranks it, the closest title first; arrows and Enter pick. For a member or
    // method node (Retarget) it offers only its object's members, and picking one makes the node that member.
    sealed class GraphSearchPopup : VisualElement
    {
        readonly GraphCanvas canvas;
        readonly GraphSearchContext context;
        readonly Vector2 at;
        readonly GraphPort pending;
        readonly TextField query;
        readonly ScrollView list;
        readonly Label waiting;
        readonly List<GraphSearchEntry> all;
        readonly List<GraphSearchEntry> shown = new List<GraphSearchEntry>();
        readonly List<VisualElement> rows = new List<VisualElement>();
        int current;
        int asking;

        public GraphSearchPopup(GraphCanvas canvas, GraphSearchContext context, Vector2 at, GraphPort pending)
        {
            this.canvas = canvas;
            this.context = context;
            this.at = at;
            this.pending = pending;
            all = context.Retarget != 0 ? new List<GraphSearchEntry>() : Commands().Concat(TideGraphServices.Searches.SelectMany(s => s.Entries(context))).ToList();
            style.position = Position.Absolute;
            style.left = Mathf.Max(0, at.x);
            style.top = Mathf.Max(0, at.y);
            style.width = 300;
            style.height = 320;
            style.backgroundColor = new Color(0.2f, 0.2f, 0.21f, 0.98f);
            style.borderTopWidth = style.borderBottomWidth = style.borderLeftWidth = style.borderRightWidth = 1;
            style.borderTopColor = style.borderBottomColor = style.borderLeftColor = style.borderRightColor = new Color(0, 0, 0, 0.7f);
            style.borderTopLeftRadius = style.borderTopRightRadius = style.borderBottomLeftRadius = style.borderBottomRightRadius = 4;
            query = new TextField { style = { marginLeft = 4, marginRight = 4, marginTop = 4 } };
            query.textEdition.placeholder = context.Retarget != 0 ? "A member of its object" : pending == null ? "Search for a node" : "A node to wire";
            query.RegisterValueChangedCallback(_ => Filter());
            query.RegisterCallback<KeyDownEvent>(OnKey, TrickleDown.TrickleDown);
            query.RegisterCallback<FocusOutEvent>(_ => schedule.Execute(() =>
            {
                if (panel != null && focusController?.focusedElement is VisualElement f && (f == this || Contains(f))) return;
                if (parent != null) canvas.CloseSearch();
            }).ExecuteLater(100));
            Add(query);
            waiting = new Label { style = { fontSize = 10, color = new Color(0.65f, 0.65f, 0.65f), paddingLeft = 6, whiteSpace = WhiteSpace.Normal, display = DisplayStyle.None } };
            Add(waiting);
            list = new ScrollView { style = { flexGrow = 1 } };
            Add(list);
            RegisterCallback<PointerDownEvent>(e => e.StopPropagation());
            RegisterCallback<WheelEvent>(e => e.StopPropagation());
            // Kept inside the canvas
            canvas.RegisterCallback<GeometryChangedEvent>(Fit);
            Filter();
            AskLive();
        }

        // What the language server's searches find, merged in as they come
        void AskLive()
        {
            foreach (var s in TideGraphServices.Searches.OfType<ITideGraphLiveSearch>())
            {
                if (!s.Asks(context)) continue;
                asking++;
                Say("Asking tidels…");
                s.Ask(context, (found, why) =>
                {
                    asking--;
                    if (parent == null) return;
                    Merge(found);
                    Say(asking > 0 ? "Asking tidels…" : found == null || found.Count == 0 ? why : null);
                    Filter(true);
                });
            }
            if (asking == 0 && context.Retarget != 0) Say("tidels isn't checking this graph: its members can't be known");
        }

        void Say(string text)
        {
            waiting.text = text ?? "";
            waiting.style.display = string.IsNullOrEmpty(text) ? DisplayStyle.None : DisplayStyle.Flex;
        }

        // Whether a language server's answer is still to come
        internal bool Asking => asking > 0;

        // A search's entries, but those the catalog has already (a local or parameter of the same name) and those
        // offered twice
        void Merge(List<GraphSearchEntry> found)
        {
            if (found == null) return;
            var taken = new HashSet<string>(all.Select(e => e.Group + "\n" + e.Title));
            var names = new HashSet<string>(all.Where(e => e.Group == "Locals" || e.Group == "Parameters").Select(e => e.Title));
            var first = new List<GraphSearchEntry>();
            var later = new List<GraphSearchEntry>();
            foreach (var e in found)
            {
                if (context.Retarget != 0 && e.Apply == null) continue;
                if (!taken.Add(e.Group + "\n" + e.Title)) continue;
                if (e.Group != "Members" && !e.Group.StartsWith("Members ") && names.Contains(e.Title)) continue;
                (e.Priority > 0 ? first : later).Add(e);
            }
            // Members of the value wired come first, the names in scope after the catalog
            all.InsertRange(0, first);
            all.AddRange(later);
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
            style.left = Mathf.Clamp(at.x, 0, Mathf.Max(0, w - 300));
            style.top = Mathf.Clamp(at.y, 0, Mathf.Max(0, h - 320));
        }

        public void Begin()
        {
            query.schedule.Execute(() => query.Focus());
        }

        // Entries that suit the wire it's for: a value for a value's input, a statement for a flow
        bool Suits(GraphSearchEntry e)
        {
            if (context.Retarget != 0) return e.Apply != null;
            if (pending == null) return true;
            if (pending.Exec) return e.Group == "Flow" || e.Statement;
            if (pending.Input) return (e.Group != "Flow" && !e.Statement) || e.Title.StartsWith("Call") || e.Title.StartsWith("Method");
            return true;
        }

        internal IReadOnlyList<GraphSearchEntry> Shown => shown;

        // How well an entry matches what's typed: 0 its title, 1 its title's start, 2 a word of its title's start,
        // 3 its title has it, 4 only its group or keywords
        static int Score(GraphSearchEntry e, string typed)
        {
            var title = e.Title.ToLowerInvariant();
            if (title == typed || title.TrimStart('.') == typed || Bare(title) == typed) return 0;
            if (title.StartsWith(typed) || title.TrimStart('.').StartsWith(typed)) return 1;
            foreach (var word in title.Split(' ', '.', '(', ',', '<'))
                if (word.StartsWith(typed)) return 2;
            return title.Contains(typed) ? 3 : 4;
        }

        static string Bare(string title)
        {
            var paren = title.IndexOf('(');
            return paren > 0 ? title.Substring(0, paren) : title.Replace(" { }", "");
        }

        void Filter(bool keep = false)
        {
            var typed = (query.value ?? "").ToLowerInvariant().Trim();
            var words = typed.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            var was = keep && current > 0 && current < shown.Count ? shown[current] : null;
            shown.Clear();
            var order = new Dictionary<GraphSearchEntry, int>();
            foreach (var e in all)
            {
                if (!Suits(e)) continue;
                var text = (e.Title + " " + e.Group + " " + e.Keywords).ToLowerInvariant();
                if (words.All(text.Contains))
                {
                    order[e] = shown.Count;
                    shown.Add(e);
                }
            }
            // Typed: the closest first, and among as close, the language server's own above the catalog's
            var ranked = words.Length > 0;
            if (ranked)
            {
                var scores = shown.ToDictionary(e => e, e => Score(e, typed));
                shown.Sort((a, b) =>
                {
                    var c = scores[a].CompareTo(scores[b]);
                    if (c == 0) c = b.Priority.CompareTo(a.Priority);
                    return c != 0 ? c : order[a].CompareTo(order[b]);
                });
            }
            list.Clear();
            rows.Clear();
            string group = null;
            foreach (var e in shown)
            {
                if (!ranked && e.Group != group)
                {
                    group = e.Group;
                    list.Add(new Label(group) { style = { unityFontStyleAndWeight = FontStyle.Bold, color = new Color(0.7f, 0.7f, 0.7f), paddingLeft = 4, marginTop = 3, fontSize = 10 } });
                }
                var entry = e;
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row, paddingLeft = 12, paddingTop = 1, paddingBottom = 1 }, tooltip = e.Detail ?? "" };
                row.Add(new Label(e.Title) { style = { flexShrink = 1, overflow = Overflow.Hidden } });
                if (ranked) row.Add(new Label(e.Group) { style = { marginLeft = 6, fontSize = 10, color = new Color(0.55f, 0.55f, 0.55f), flexShrink = 0, unityTextAlign = TextAnchor.MiddleLeft } });
                row.RegisterCallback<PointerDownEvent>(ev =>
                {
                    Pick(entry);
                    ev.StopPropagation();
                });
                rows.Add(row);
                list.Add(row);
            }
            current = was != null ? Math.Max(0, shown.IndexOf(was)) : 0;
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
            if (context.Retarget != 0)
            {
                canvas.Retarget(e, context.Retarget);
                return;
            }
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
