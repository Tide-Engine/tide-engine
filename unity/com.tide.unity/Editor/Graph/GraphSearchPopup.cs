using System;
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The search for a node to add to a frame: every kind the schema has, under its category's title and with what
    // it does, what the frame's code takes and its locals, and what the language server's completion offers there
    // (the members of a value a wire was dropped from, or the names, functions and types in scope), which come in
    // when it answers. For a wire, only what suits the wire. For a member or method node (Retarget), only what its
    // value has, and picking one makes the node that member.
    sealed class GraphSearchPopup : GraphOptionList
    {
        readonly GraphBoard board;
        readonly GraphFrame frame;
        readonly GraphSearchContext context;
        readonly Vector2 at;       // In the frame
        readonly GraphPort pending;
        readonly List<GraphSearchEntry> entries;
        readonly List<GraphSearchEntry> typed = new List<GraphSearchEntry>();
        int asking;
        bool everything;               // What doesn't suit the wire is listed too
        HashSet<string> validOps;      // The operators that work on the value wanted, as the checker says

        public GraphSearchPopup(GraphBoard board, GraphFrame frame, GraphSearchContext context, Vector2 at)
            : base(Heading(frame, context), null, context.Retarget != 0 ? "Search what it has" : "Search, or type a number or a text", 330, 420)
        {
            this.board = board;
            this.frame = frame;
            this.context = context;
            this.at = at;
            pending = context.Pending;
            Window = board.Window;
            entries = context.Retarget != 0 ? new List<GraphSearchEntry>() : Commands().Concat(TideGraphServices.Searches.SelectMany(s => s.Entries(context))).ToList();
            // A wire from something the code takes: another read of it isn't what it's for
            if (pending != null && pending.IsRead) entries.RemoveAll(e => e.Reads);
            // A wire from an input: the variables of the type it takes first, then the other variables
            if (pending != null && pending.Input && !pending.Exec && pending.Path != null)
            {
                var owner = pending.Node == GraphFrame.ResultId ? null : frame.ById.TryGetValue(pending.Node, out var n) ? n : null;
                var wanted = frame.Wanted(owner, pending.Path);
                var gets = entries.Where(e => e.Reads).ToList();
                var fitting = gets.Where(e => wanted != null && GraphValues.Fits(e.Type, wanted)).ToList();
                foreach (var e in fitting) e.Priority = 3;
                entries.RemoveAll(e => e.Reads);
                entries.InsertRange(0, fitting.Concat(gets.Where(e => !fitting.Contains(e))));
            }
            Fill();
            AskLive();
            AskOperators();
        }

        // For a value wanted of a known type, the operators that work on it (tide/graphOperators): others aren't
        // offered. A condition takes comparisons of anything, so its operators aren't narrowed.
        void AskOperators()
        {
            if (pending == null || !pending.Input || pending.Exec || pending.Path == null) return;
            var owner = pending.Node == GraphFrame.ResultId ? null : frame.ById.TryGetValue(pending.Node, out var n) ? n : null;
            var wanted = frame.Wanted(owner, pending.Path);
            if (string.IsNullOrEmpty(wanted) || wanted == "bool") return;
            asking++;
            TideGraphLanguage.Operators(board.Window, wanted, null, (ops, why) =>
            {
                asking--;
                if (WasClosed || ops == null) return;
                validOps = new HashSet<string>(GraphLists.Strings(ops.Arr("binary")).Concat(GraphLists.Strings(ops.Arr("unary"))).Concat(GraphLists.Strings(ops.Arr("step"))));
                Fill(true);
            });
        }

        static string Heading(GraphFrame frame, GraphSearchContext c)
        {
            if (c.Retarget != 0) return "What its value has";
            var p = c.Pending;
            if (p == null) return "Add to " + frame.Title;
            if (p.Exec) return p.Input ? "What happens before it?" : "What happens next?";
            return p.Input ? "What works it out?" : p.IsRead ? $"Do what with {p.Name ?? p.Label}?" : "Do what with this value?";
        }

        GraphOption Option(GraphSearchEntry e) => new GraphOption
        {
            Title = e.Title, Section = e.Group, Detail = GraphLook.FirstLine(e.Detail, 70), Help = e.Detail, Keywords = e.Keywords ?? "", Priority = e.Priority, Data = e,
            Pick = () => PickEntry(e),
        };

        void Fill(bool keep = false)
        {
            var options = typed.Concat(entries).Select(Option).ToList();
            // For a wire: what suits it, and a way to everything
            if (pending != null && !everything)
                options.Add(new GraphOption
                {
                    Title = "Show everything", Detail = "Not only what suits this wire", Section = "Something else", Pinned = true, KeepOpen = true, Data = "everything",
                    Pick = () =>
                    {
                        everything = true;
                        Fill(true);
                    },
                });
            Set(options, keep);
        }

        // What the language server's searches find, merged in as they come
        void AskLive()
        {
            foreach (var s in TideGraphServices.Searches.OfType<ITideGraphLiveSearch>())
            {
                if (!s.Asks(context)) continue;
                asking++;
                Status = "Asking the language server…";
                s.Ask(context, (found, why) =>
                {
                    asking--;
                    if (WasClosed) return;
                    Merge(found);
                    Status = asking > 0 ? "Asking the language server…" : found == null || found.Count == 0 ? why : null;
                    Fill(true);
                });
            }
            if (asking == 0 && context.Retarget != 0) Status = "The language server isn't checking this file, so what its value has can't be known. Type the name on the node instead.";
        }

        // Whether a language server's answer is still to come
        internal bool Asking => asking > 0;

        // The entries shown, as the list shows them
        internal List<GraphSearchEntry> Entries => Shown.Where(o => o.Data is GraphSearchEntry).Select(o => (GraphSearchEntry)o.Data).ToList();

        // A search's entries, but those the catalog has already (a local or parameter of the same name) and those
        // offered twice
        void Merge(List<GraphSearchEntry> found)
        {
            if (found == null) return;
            var taken = new HashSet<string>(entries.Select(e => e.Group + "\n" + e.Title));
            var names = new HashSet<string>(entries.Where(e => e.Reads).Select(e => e.Variable ?? e.Title));
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
            entries.InsertRange(0, first);
            entries.AddRange(later);
        }

        IEnumerable<GraphSearchEntry> Commands()
        {
            if (pending != null) yield break;
            yield return new GraphSearchEntry { Group = "Board", Title = "Note", Keywords = "note comment box", Detail = "A box of text behind the nodes", Command = () => board.AddNote(frame, at) };
            if (GraphDocument.IsClip(GraphBoard.Clipboard))
                yield return new GraphSearchEntry { Group = "Board", Title = "Paste", Keywords = "paste clipboard", Detail = "What was copied", Command = () => board.Paste(board.ToBoard(frame.At + at)) };
        }

        // What's typed is itself a value, where a value goes: a number, true or false, or a text
        protected override void Typed(string text)
        {
            var had = typed.Count;
            typed.Clear();
            var value = context.Retarget == 0 && (pending == null || !pending.Exec);
            if (value && text.Length > 0)
            {
                if (GraphValues.TryLiteral(text, null, out var k, out var literal))
                    typed.Add(new GraphSearchEntry
                    {
                        Group = "A value", Title = k == "string" ? "\"" + literal + "\"" : literal, Keywords = text, Detail = GraphSchema.Spec(k)?.Help, Priority = 3,
                        Make = (doc, graph, p) =>
                        {
                            var n = doc.AddNode(graph, k, p.x, p.y);
                            GraphValues.SetLiteral(n, k, literal);
                            return n;
                        },
                    });
                else if (!text.Contains("\""))
                    typed.Add(new GraphSearchEntry
                    {
                        Group = "A value", Title = "\"" + text + "\"", Keywords = text, Detail = "As a text", Priority = -1,
                        Make = (doc, graph, p) =>
                        {
                            var n = doc.AddNode(graph, "string", p.x, p.y);
                            GraphValues.SetLiteral(n, "string", text);
                            return n;
                        },
                    });
            }
            if (had > 0 || typed.Count > 0) Fill(true);
        }

        // Entries that suit the wire it's for: a value for a value's input, a step for a flow
        protected override bool Suits(GraphOption option)
        {
            if (!(option.Data is GraphSearchEntry e)) return true;
            if (context.Retarget != 0) return e.Apply != null;
            if (pending == null || everything) return true;
            if (validOps != null && e.Op != null && !validOps.Contains(e.Op)) return false;
            if (e.Command != null) return false;
            if (pending.Exec) return e.Statement;
            if (pending.Input) return !e.Statement;
            return true;
        }

        void PickEntry(GraphSearchEntry e)
        {
            if (context.Retarget != 0)
            {
                board.Retarget(frame, e, context.Retarget);
                return;
            }
            if (e.Command != null)
            {
                e.Command();
                return;
            }
            board.Insert(frame, e, at, pending, context.Wrap);
        }
    }
}
