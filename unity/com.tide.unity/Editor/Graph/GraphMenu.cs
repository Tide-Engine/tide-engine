using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The menu of a thing on the board (notes/visual-scripting.md, "Round three": "one menu, the same everywhere"). A
    // right click on a card, a frame, a node, a row or a pin and its "…" button open the same list, built here for
    // every kind of thing in the same order, leaving out what doesn't apply: (1) what can be added to it, (2) Rename,
    // (3) Cut, Copy, Paste, Duplicate, Delete, (4) Note…. A pin's is its own: Break links, Pick a value…, Work it
    // out…. Options, attributes and starting values worked out aren't in menus: they're in Details.
    static class GraphMenu
    {
        // The sections, in their order: each option's Data says which it's in
        public const string Add = "add", Rename = "rename", Clipboard = "clipboard", Note = "note", Pin = "pin";
        public static readonly string[] Sections = { Add, Rename, Clipboard, Note };

        // The menu's words and keys: the schema's (`words`, menu.<key>)
        static string Word(string key, params string[] fill) => GraphSchema.Word("menu." + key, fill);

        static string Keys(string key) => GraphSchema.WordKeys("menu." + key);

        public static string RenameWord => Word("rename");
        public static string DeleteWord => Word("delete");
        public static string KeepLocalWord => Word("keepLocal");
        public static string RerouteWord => Word("reroute");

        // "Add a field", "Add an input…": an add of a word the schema has (a kind's addRow, "+ choice"); what code
        // takes is "Add what it needs…"
        public static string AddA(string what)
        {
            if (string.IsNullOrEmpty(what)) return Word("addOwn", "title", "").Trim();
            what = GraphLook.LowerFirst(what.TrimStart('+').Trim());
            var word = what.TrimEnd('…');
            if (word == "needs") return Word("addNeed");
            var article = "aeiou".IndexOf(char.ToLowerInvariant(what[0])) >= 0 ? "an" : "a";
            return Word(word.Length < what.Length ? "addOneMore" : "addOne", "article", article, "word", word);
        }

        // A kind of member to add: "Add a method"; one of its own goes by its name ("Add Sample"), text as it is
        public static string AddMember(string kind)
        {
            var title = GraphSchema.MemberTitle(kind);
            if (GraphSchema.MemberOnce(kind) || kind == "text") return Word("addOwn", "title", title);
            return AddA(title);
        }

        sealed class Builder
        {
            readonly List<(string section, GraphOption option)> all = new List<(string, GraphOption)>();

            public void Put(string section, string title, string keys, Action pick, bool off = false, string help = null)
                => all.Add((section, new GraphOption { Title = title, Detail = keys, Pick = pick, Off = off, Help = help, Data = section }));

            public List<GraphOption> Done()
            {
                var options = new List<GraphOption>();
                foreach (var section in Sections.Append(Pin))
                {
                    var of = all.Where(a => a.section == section).Select(a => a.option).ToList();
                    if (of.Count == 0) continue;
                    if (options.Count > 0) options.Add(new GraphOption { Separator = true });
                    options.AddRange(of);
                }
                return options;
            }
        }

        static void Clipboard_(Builder b, Action cut, Action copy, Action paste, bool canPaste, Action duplicate, Action delete)
        {
            if (cut != null) b.Put(Clipboard, Word("cut"), Keys("cut"), cut);
            if (copy != null) b.Put(Clipboard, Word("copy"), Keys("copy"), copy);
            if (paste != null) b.Put(Clipboard, Word("paste"), Keys("paste"), paste, !canPaste);
            if (duplicate != null) b.Put(Clipboard, Word("duplicate"), Keys("duplicate"), duplicate);
            if (delete != null) b.Put(Clipboard, DeleteWord, Keys("delete"), delete);
        }

        static void Rename_(Builder b, Action rename)
        {
            if (rename != null) b.Put(Rename, RenameWord, Keys("rename"), rename);
        }

        static void Note_(Builder b, Action note)
        {
            if (note != null) b.Put(Note, Word("note"), null, note, false, GraphSchema.KeyInfo("comment").Help);
        }

        // ---------------------------------------------------------------------------------------------------------
        // A card or a frame

        public static List<GraphOption> ForItem(GraphItem item)
        {
            var b = new Builder();
            var board = item.Board;
            var e = item.Element;
            if (e == null) return b.Done();
            if (item is GraphCard card)
                foreach (var (title, add) in card.Adds()) b.Put(Add, title, null, add);
            if (item is GraphFrame frame && !frame.IsValue)
            {
                if (frame.Graph != null)
                {
                    b.Put(Add, Word("addNode"), Keys("addNode"), () => board.OpenSearch(frame, frame.FreePlace(new Vector2(40, 0), new Vector2(200, 80)), null));
                    b.Put(Add, Word("addNote"), null, () => board.AddNote(frame, frame.FreePlace(new Vector2(40, 0), new Vector2(300, 120))));
                }
                // What its code takes, as its entry's last row adds it
                var routine = frame.Routine;
                var member = e.Role != GraphRole.Decl;
                var row = routine != null && e.Role != GraphRole.Accessor ? GraphSchema.AddRow(routine.Str("kind"), member) : null;
                if (row != null && GraphSchema.HasParams(routine.Str("kind"), member))
                    b.Put(Add, AddA(row), null, () => frame.OpenNeeds(frame.Entry?.Q<Button>("needs") ?? (VisualElement)frame, routine.Arr("params"), routine.Str("kind") == "sample"));
            }
            var named = item.Q<GraphName>("name");
            if (named != null) Rename_(b, () => board.Rename(item.Id, null));
            void Select()
            {
                if (!board.Chosen.Contains(item.Id) || board.Nodes.Count > 0) board.SelectItem(item, false);
            }
            var listed = e.List != null;
            var accessor = e.Role == GraphRole.Accessor;
            var once = e.Role == GraphRole.Member && GraphSchema.MemberOnce(e.Value.Str("kind"));
            var clip = GraphBoard.Clipboard;
            var value = item is GraphFrame f && f.IsValue;
            if (!value)
                Clipboard_(b,
                    listed && !accessor ? () => { Select(); board.CopySelection(); board.DeleteSelection(); } : (Action)null,
                    listed && !accessor ? () => { Select(); board.CopySelection(); } : (Action)null,
                    () => board.PasteInto(item),
                    board.CanPasteInto(item, clip),
                    listed && !accessor && !once ? () => { Select(); board.DuplicateSelection(); } : (Action)null,
                    listed ? () => { Select(); board.DeleteSelection(); } : (Action)null);
            Note_(b, value ? null : (Action)(() => item.AskNote(e)));
            return b.Done();
        }

        // ---------------------------------------------------------------------------------------------------------
        // A node of a frame: what grows on it, its name, the clipboard, a note

        public static List<GraphOption> ForNode(GraphNodeView view)
        {
            var b = new Builder();
            var frame = view.Frame;
            var board = frame.Board;
            foreach (var (title, add) in view.Adds()) b.Put(Add, title, null, add);
            if (view.Q<GraphName>() != null) Rename_(b, () => board.RenameNode(frame, view.Id));
            void Select()
            {
                if (!(board.Active == frame && board.Nodes.Contains(view.Id))) board.SelectNode(frame, view.Id, false);
            }
            Clipboard_(b,
                () => { Select(); board.CopySelection(); board.DeleteSelection(); },
                () => { Select(); board.CopySelection(); },
                () => board.PasteInto(frame),
                board.CanPasteInto(frame, GraphBoard.Clipboard),
                () => { Select(); board.DuplicateSelection(); },
                () => { Select(); board.DeleteSelection(); });
            var noted = view.Call ?? view.Node;
            Note_(b, () => view.AskNote(noted));
            return b.Done();
        }

        // ---------------------------------------------------------------------------------------------------------
        // A row: a card's field, choice, setting or entity; an entry's parameter or payload value

        public static List<GraphOption> ForRow(GraphItem item, int id)
        {
            var b = new Builder();
            var board = item.Board;
            var e = item.Doc.Find(id);
            if (e == null) return b.Done();
            var kind = e.Value.Str("kind");
            foreach (var (title, add) in RowAdds(item, e)) b.Put(Add, title, null, add);
            if (item.Q<GraphName>("name" + id) != null) Rename_(b, () => board.Rename(item.Id, "name" + id));
            var listed = e.List != null;
            var once = e.Role == GraphRole.Member && GraphSchema.MemberOnce(kind);
            Clipboard_(b,
                listed ? () => { board.CopyRow(id); board.Delete(id); } : (Action)null,
                listed ? () => board.CopyRow(id) : (Action)null,
                () => board.PasteRows(id),
                board.CanPasteRows(id, GraphBoard.Clipboard),
                listed && !once ? () => board.Duplicate(id) : (Action)null,
                listed ? () => board.Delete(id) : (Action)null);
            if (GraphDocument.OrderOf(e).Contains("comment")) Note_(b, () => item.AskNote(e));
            return b.Done();
        }

        // What a row can have added to it: another of its kind, a property's code, what a code needs
        static IEnumerable<(string, Action)> RowAdds(GraphItem item, GraphElement e)
        {
            var m = e.Value;
            if (item is GraphCard card && (e.Role == GraphRole.EnumMember || e.Role == GraphRole.Entry || e.Role == GraphRole.Member && (m.Str("kind") == "field" || m.Str("kind") == "tweak")))
            {
                var row = card.RowAdd();
                if (row != null) yield return row.Value;
            }
            if (item is GraphFrame code && (e.Role == GraphRole.Param || e.Role == GraphRole.SampleParam) && code.Routine != null)
            {
                var member = code.Owner?.Role != GraphRole.Decl;
                var words = GraphSchema.AddRow(code.Routine.Str("kind"), member);
                if (words != null) yield return (AddA(words), () => code.OpenNeeds(code.Entry?.Q<Button>("needs") ?? (VisualElement)code, code.Routine.Arr("params"), code.Routine.Str("kind") == "sample"));
            }
            if (e.Role == GraphRole.Member && m.Str("kind") == "property")
            {
                var accessors = m.Arr("accessors");
                foreach (var word in new[] { "get", "set" })
                {
                    var w = word;
                    if (accessors != null && accessors.Items.Any(a => a.Str("word") == w)) continue;
                    yield return (Word("addAccessor", "word", $"“{GraphSchema.AccessorInfo(w).Label}”"), () => item.Board.AddAccessor(e, w));
                }
            }
            if (e.Role == GraphRole.Param && m.Str("mode") == "query" && item is GraphFrame frame)
                yield return (Word("addQueryNeed"), () => frame.OpenNeeds(frame.Entry, m.Arr("query"), false, m));
        }

        // ---------------------------------------------------------------------------------------------------------
        // A pin

        public static List<GraphOption> ForPin(PortView pin)
        {
            var b = new Builder();
            var p = pin.Port;
            var frame = pin.Frame;
            var board = frame.Board;
            b.Put(Pin, Word("breakLinks"), Keys("breakLinks"), () => board.BreakLinks(pin), !pin.Connected && !p.IsRead);
            // Where a new node for it goes: beside the pin, where there's room
            Vector2 Near() => frame.FreePlace(pin.View.Shown + (p.Input ? new Vector2(-240, pin.View.SizeNow.y + 30) : new Vector2(pin.View.SizeNow.x + 60, 0)), new Vector2(220, 90));
            if (p.Input && !p.Exec && p.Path != null)
            {
                var owner = p.Node == GraphFrame.ResultId ? null : frame.ById.TryGetValue(p.Node, out var n) ? n : null;
                if (owner != null || p.Node == GraphFrame.ResultId)
                {
                    var label = GraphNodeView.LabelOf(board.Window.Checked, owner, p.Path, p.Label);
                    b.Put(Pin, Word("pick"), null, () => board.PickValue(frame, owner, p.Path, label, pin));
                    b.Put(Pin, Word("work"), null, () => board.WorkOut(frame, owner, p.Path, pin));
                    b.Put(Pin, Word("empty"), Keys("empty"), () => frame.Empty(owner, p.Path, label), frame.WireAt(owner, p.Path) == 0);
                }
            }
            else if (!p.Input && !p.Exec)
            {
                b.Put(Pin, Word("use"), null, () => board.OpenSearch(frame, Near(), p));
                // Blueprint's Promote to Variable: worked out once, read where it was wired
                if (!p.IsRead && frame.CanKeepAsLocal(p.Node))
                    b.Put(Pin, KeepLocalWord, null, () => frame.KeepAsLocal(p.Node), false, Word("keepLocal.summary"));
            }
            else if (!p.Input && p.Exec) b.Put(Pin, Word("next"), null, () => board.OpenSearch(frame, Near(), p));
            else if (p.Node > 0) b.Put(Pin, Word("before"), null, () => board.OpenSearch(frame, Near(), p));
            return b.Done();
        }

        // ---------------------------------------------------------------------------------------------------------
        // A data wire: a reroute on it where it was clicked, or the wire taken away

        public static List<GraphOption> ForWire(GraphFrame frame, FrameWires.Wire wire, Vector2 at)
        {
            var b = new Builder();
            var board = frame.Board;
            b.Put(Add, RerouteWord, Keys("reroute"), () => board.AddReroute(frame, wire, at), false, Word("reroute.summary"));
            b.Put(Pin, Word("breakLink"), null, () =>
            {
                var reader = wire.Reader == GraphFrame.ResultId ? null : frame.ById.TryGetValue(wire.Reader, out var n) ? n : null;
                if (reader != null || wire.Reader == GraphFrame.ResultId) frame.Empty(reader, wire.Path, GraphNodeView.LabelOf(board.Window.Checked, reader, wire.Path, null));
            });
            return b.Done();
        }

        // Opens a menu where the pointer is, or below what it's opened from
        public static GraphOptionList Open(TideGraphWindow window, List<GraphOption> options, VisualElement from, Vector2? at = null)
        {
            var list = new GraphOptionList(null, null, null, 280);
            list.Set(options);
            if (at != null) window.Open(list, new Rect(at.Value, Vector2.zero));
            else window.Open(list, from);
            return list;
        }
    }
}
