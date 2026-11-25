using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // Picks what goes in an input: a search box over a value of the input's type to type in, what this code has
    // (what it takes and the locals declared before the node, each opening to its fields), the game's constants
    // and functions, and "Work it out…", which makes a node wired in. Where the input's type is known (a call's
    // parameter, what a Set changes), only what fits is listed, with a way to see the rest. A name or a path
    // picked is shown in the input (the schema's `inline`): no node, no wire. Enter takes the first match.
    sealed class GraphValuePicker : GraphOptionList
    {
        readonly GraphBoard board;
        readonly GraphFrame frame;
        readonly JValue owner;      // The node whose input it is; null for a value frame's value
        readonly WirePath path;
        readonly string label, wanted;
        // The path picked so far: what's listed is what its last part has
        readonly List<GraphFrame.Part> parts = new List<GraphFrame.Part>();
        // What each type has (its fields and methods), and each of the engine's namespaces ("name:Math"), as the
        // language server says; a key with no entry hasn't been answered
        readonly Dictionary<string, List<TideGraphLanguage.Member>> members = new Dictionary<string, List<TideGraphLanguage.Member>>();
        readonly HashSet<string> asked = new HashSet<string>();
        readonly List<GraphOption> typed = new List<GraphOption>();
        List<TideGraphLanguage.Member> scope; // What completion lists where the value would be written
        bool everything;                      // What doesn't fit is listed too
        int asking;

        public GraphValuePicker(GraphBoard board, GraphFrame frame, JValue owner, WirePath path, string label, int keep)
            : base(null, null, "Search, or type a value", 330, 440)
        {
            this.board = board;
            this.frame = frame;
            this.owner = owner;
            this.path = path;
            this.label = string.IsNullOrEmpty(label) ? "here" : label;
            Window = board.Window;
            frame.Classify();
            wanted = frame.Wanted(owner, path);
            var shown = frame.ShownIn(frame.WireAt(owner, path));
            if (shown != null && keep > 0)
                parts.AddRange(frame.PathOf(shown).Where(n => n.Str("k") != "unary").Take(keep).Select(n => GraphFrame.Part.Of(frame, n)));
            Fill();
            Ask();
        }

        GraphDocument Doc => frame.Doc;
        bool Light => board.Light;

        // Whether a language server's answer is still to come
        internal bool Asking => asking > 0;
        internal string Wanted => wanted;
        internal string PathText => string.Join("", parts.Select((p, i) => i == 0 && p.Kind != "shorthand" ? p.Name : "." + p.Name));

        static bool Simple(string type) => GraphSchema.ValueControl(type) is string c && (c == "whole" || c == "decimal" || c == "tick" || c == "text" || c == "char");
        bool Fits(string have) => everything || GraphValues.Fits(have, wanted);
        static string Key(GraphFrame.Part p) => p.Space ? "name:" + p.Name : p.Type;

        // ---------------------------------------------------------------------------------------------------------
        // What the language server knows

        void Ask()
        {
            asking++;
            Status = "Asking what can go here…";
            TideGraphLanguage.ValuesAt(Window, frame.Slot, owner != null ? owner.Wire("id") : 0, (items, why) =>
            {
                asking--;
                if (WasClosed) return;
                scope = items;
                Fill(true);
            });
            foreach (var read in frame.Reads.Values) AskType(read.Type, false);
            foreach (var local in GraphDocument.Locals(frame.Graph)) AskType(local.Str("type"), false);
            foreach (var p in parts) AskType(Key(p), p.Space);
        }

        // What a type has, asked once: its fields show whether something of it has anything that fits
        void AskType(string type, bool space)
        {
            if (string.IsNullOrEmpty(type) || (!space && Simple(type)) || !asked.Add(type)) return;
            asking++;
            void Got(List<TideGraphLanguage.Member> found, string why)
            {
                asking--;
                if (WasClosed) return;
                members[type] = found ?? new List<TideGraphLanguage.Member>();
                Fill(true);
            }
            if (space) TideGraphLanguage.MembersOfName(Window, type.Substring("name:".Length), Got);
            else TideGraphLanguage.MembersOfType(Window, type, Got);
        }

        // ---------------------------------------------------------------------------------------------------------
        // The list

        void Fill(bool keep = false)
        {
            var options = new List<GraphOption>();
            var hidden = 0;
            if (parts.Count == 0) Top(options, ref hidden);
            else
            {
                Inside(options, ref hidden);
                options.Add(BackRow());
            }
            if (hidden > 0 && !everything)
                options.Add(new GraphOption
                {
                    Title = "Show what doesn't fit too", Detail = hidden == 1 ? "1 more" : hidden + " more", Section = "Something else", KeepOpen = true, Data = "everything",
                    Help = $"Listed here is what's a {wanted}, which '{label}' takes. The rest can be picked too: the check says if it's wrong.",
                    Pick = () =>
                    {
                        everything = true;
                        Fill(true);
                    },
                });
            Title = parts.Count == 0 ? Changing ? "What does it change?" : $"What goes in '{label}'?" : PathText + ": which of it?";
            // Nothing typed is what a Set changes: it's found
            if (Changing) Placeholder = "Search what it can change";
            HelpText = parts.Count > 0 ? null : Changing ? "What this code takes to change, a local that can be changed, or a field of one." : wanted != null ? $"It takes a value of type {wanted}." : null;
            Status = asking > 0 ? "Asking what can go here…" : null;
            Set(options, keep);
        }

        GraphOption Row(string title, string type, string section, Action pick, bool keepOpen = false) => new GraphOption
        {
            Title = title, Detail = type, Section = section, Dot = string.IsNullOrEmpty(type) ? (Color?)null : TideGraphTypes.ColorOf(type, Light), KeepOpen = keepOpen, Pick = pick,
        };

        // Something the code has, or a part of it: taken when it's what the input takes (or has nothing in it to
        // pick), opened to what it has otherwise. With its fields that fit, found by typing their names.
        void Offer(List<GraphOption> options, ref int hidden, GraphFrame.Part part, string section, int priority)
        {
            var all = parts.Append(part).ToList();
            var title = part.Name;
            var type = part.Type;
            if (string.IsNullOrEmpty(type) || Simple(type))
            {
                if (!Fits(type))
                {
                    hidden++;
                    return;
                }
                var simple = Row(title, type, section, () => Take(all));
                simple.Priority = priority;
                simple.Data = part;
                options.Add(simple);
                return;
            }
            members.TryGetValue(type, out var has);
            var fields = has?.Where(m => !m.Callable && m.Kind != TideGraphLanguage.KindKeyword && m.Kind != TideGraphLanguage.KindSnippet).ToList();
            // What a Set changes: a vector or a colour is a value to change as it is; a struct opens to its fields
            var exact = wanted != null ? GraphValues.Fits(type, wanted) : Changing && GraphSchema.ValueControl(type) != null;
            var opens = has == null || has.Count > 0;
            if (!exact && wanted != null && !everything && fields != null && !fields.Any(f => Fits(f.Type) || !Simple(f.Type)) && !has.Any(m => m.Callable && Fits(m.Result)))
            {
                hidden++;
                return;
            }
            var row = exact || !opens ? Row(title, type, section, () => Take(all)) : Row(title, type + "  ›", section, () => Open(part), true);
            row.Dot = TideGraphTypes.ColorOf(type, Light);
            row.Priority = priority;
            row.Data = part;
            row.Help = exact || !opens ? null : "Opens to what it has";
            options.Add(row);
            // Its fields, by typing their names: wobble.phase
            if (fields == null) return;
            var prefix = string.Join("", all.Select((p, i) => i == 0 && p.Kind != "shorthand" ? p.Name : "." + p.Name));
            foreach (var f in fields)
            {
                if (!Fits(f.Type)) continue;
                var field = new GraphFrame.Part { Kind = "member", Name = f.Label, Type = f.Type };
                var found = Row(prefix + "." + f.Label, f.Type, section, () => Take(all.Append(field).ToList()));
                found.Searched = true;
                found.Priority = priority - 1;
                options.Add(found);
            }
        }

        // What a Set changes: only what can be changed is offered (a parameter taken as "changes", a local that can
        // be changed, a field of one of those), and what's only read is listed greyed, with why
        bool Changing => owner?.Str("k") == "assign" && path.Parts.Length == 1 && path.Key == "target";

        void Changeable(List<GraphOption> options)
        {
            const string has = "What this code can change", read = "Only read here";
            // What can be changed first; what's only read after it, greyed
            var only = new List<GraphOption>();
            foreach (var r in frame.Reads.Values)
            {
                var part = new GraphFrame.Part { Kind = "name", Name = r.Name, Type = r.Type };
                if (frame.Changeable(r.Name, 0))
                {
                    var hiddenNone = 0;
                    Offer(options, ref hiddenNone, part, has, 3);
                    continue;
                }
                only.Add(new GraphOption
                {
                    Title = r.Name, Detail = r.Type, Section = read, Off = true, Dot = TideGraphTypes.ColorOf(r.Type, Light),
                    Help = $"It's taken to be read. To change it, make its row in the entry \"{GraphSchema.ParamModeInfo("mut").Label}\".",
                });
            }
            foreach (var local in GraphDocument.Locals(frame.Graph).Where(l => l != owner))
            {
                var name = GraphSchema.LocalName(local);
                if (frame.Reads.ContainsKey(name)) continue;
                var type = local.Str("type");
                AskType(type, false);
                if (frame.Changeable(null, local.Wire("id")))
                {
                    var hiddenNone = 0;
                    Offer(options, ref hiddenNone, new GraphFrame.Part { Kind = "local", Ref = local.Wire("id"), Name = name, Type = type }, has, 3);
                    continue;
                }
                only.Add(new GraphOption
                {
                    Title = name, Detail = type, Section = read, Off = true, Dot = TideGraphTypes.ColorOf(type, Light),
                    Help = $"A local that can't be changed. Tick \"{GraphSchema.FlagInfo("mut").Label}\" in its Details to change it.",
                });
            }
            options.AddRange(only);
            options.Add(new GraphOption
            {
                Title = "Work it out…", Detail = "An element of a list, a field of a call's result: a node wired in", Section = "Something else", Pinned = true, Keywords = "node index element",
                Pick = () => board.WorkOut(frame, owner, path, From, Query.Trim()),
            });
            if (frame.WireAt(owner, path) != 0)
                options.Add(new GraphOption { Title = "Nothing", Detail = "Empties it", Section = "Something else", Pick = () => frame.Edit("Clear " + label, () => frame.SetWire(owner, path, 0, out _)) });
        }

        void Top(List<GraphOption> options, ref int hidden)
        {
            const string value = "A value", has = "What this code has", constants = "Constants", functions = "Functions (a node wired in)", engine = "The engine's", other = "Something else";
            if (Changing)
            {
                Changeable(options);
                return;
            }
            options.AddRange(typed);
            var control = GraphSchema.ValueControl(wanted);
            if (control == "tick")
                foreach (var tick in new[] { "true", "false" })
                {
                    var t = tick;
                    options.Add(new GraphOption { Title = t, Section = value, Priority = 4, Pick = () => Literal("bool", t) });
                }
            // An enum's choices: this file's own, or what the language server says the type's name has; and a
            // type's own constants (Color.white)
            var named = wanted?.TrimEnd('?');
            var choices = named != null ? Doc.EnumMembers(named) : null;
            if (named != null && !Simple(named) && named.IndexOfAny(new[] { '<', '[' }) < 0)
            {
                AskType("name:" + named, true);
                if (members.TryGetValue("name:" + named, out var own))
                {
                    if (choices == null && own.Any(m => m.Kind == TideGraphLanguage.KindEnumMember)) choices = own.Where(m => m.Kind == TideGraphLanguage.KindEnumMember).Select(m => m.Label).ToList();
                    foreach (var constant in own.Where(m => m.Kind == TideGraphLanguage.KindConstant && GraphValues.Fits(m.Type, wanted)))
                    {
                        var path2 = new List<GraphFrame.Part> { new GraphFrame.Part { Kind = "name", Name = named, Space = true }, new GraphFrame.Part { Kind = "member", Name = constant.Label, Type = constant.Type } };
                        options.Add(new GraphOption { Title = named + "." + constant.Label, Section = named, Priority = 3, Dot = TideGraphTypes.ColorOf(wanted, Light), Pick = () => Take(path2) });
                    }
                }
            }
            if (choices != null)
                foreach (var choice in choices)
                {
                    var part = new GraphFrame.Part { Kind = "shorthand", Name = choice, Type = wanted };
                    options.Add(new GraphOption { Title = "." + choice, Section = named, Priority = 4, Dot = TideGraphTypes.ColorOf(wanted, Light), Pick = () => Take(new List<GraphFrame.Part> { part }) });
                }

            // What the code takes, and the locals declared before the node: completion's variables there are the
            // ones in scope; until it answers (and without it), every local of the code
            foreach (var read in frame.Reads.Values) Offer(options, ref hidden, new GraphFrame.Part { Kind = "name", Name = read.Name, Type = read.Type }, has, 3);
            var locals = GraphDocument.Locals(frame.Graph).Where(l => owner == null || l != owner).ToList();
            var inScope = scope?.Where(m => m.Kind == TideGraphLanguage.KindVariable).ToDictionary(m => m.Label, m => m);
            foreach (var local in locals)
            {
                var name = GraphSchema.LocalName(local);
                if (frame.Reads.ContainsKey(name)) continue;
                TideGraphLanguage.Member known = null;
                if (inScope != null && !inScope.TryGetValue(name, out known)) continue;
                var type = !string.IsNullOrEmpty(local.Str("type")) ? local.Str("type") : known != null && !known.Detail.EndsWith(" " + name) ? known.Detail : null;
                if (options.Any(o => o.Data is GraphFrame.Part p && p.Kind == "local" && p.Name == name)) continue;
                AskType(type, false);
                Offer(options, ref hidden, new GraphFrame.Part { Kind = "local", Ref = local.Wire("id"), Name = name, Type = type }, has, 3);
            }
            if (scope != null)
            {
                foreach (var word in scope.Where(m => m.Kind == TideGraphLanguage.KindKeyword))
                {
                    var k = word.Label;
                    var offered = (k == "this" && Fits("Entity")) || (k == "null" && wanted != null && wanted.EndsWith("?"));
                    if (!offered && k != "default" && k != "null") continue;
                    if (GraphSchema.Spec(k) == null) continue;
                    var part = new GraphFrame.Part { Kind = k, Name = k };
                    options.Add(new GraphOption { Title = k, Detail = GraphSchema.Spec(k).Help, Section = k == "this" ? has : value, Searched = !offered, Priority = -1, Pick = () => Take(new List<GraphFrame.Part> { part }) });
                }
                foreach (var c in scope.Where(m => m.Kind == TideGraphLanguage.KindConstant))
                {
                    var type = c.Detail.StartsWith("const ") ? c.Detail.Substring(6).Trim() : c.Detail;
                    if (!Fits(type))
                    {
                        hidden++;
                        continue;
                    }
                    var part = new GraphFrame.Part { Kind = "name", Name = c.Label, Type = type };
                    var row = Row(c.Label, type, constants, () => Take(new List<GraphFrame.Part> { part }));
                    row.Priority = 2;
                    options.Add(row);
                }
                foreach (var f in scope.Where(m => m.Kind == TideGraphLanguage.KindFunction || m.Kind == TideGraphLanguage.KindMethod))
                {
                    // The game's own: the engine's (Spawn, Send) are steps, found under "Work it out…"
                    if (Window.Checked?.IsGames(f.Label) == false) continue;
                    var result = f.Result;
                    if (result == "void" || string.IsNullOrEmpty(result)) continue;
                    if (!Fits(result))
                    {
                        hidden++;
                        continue;
                    }
                    var function = f;
                    var row = Row($"{f.Label}({string.Join(", ", f.Parameters)})", result, functions, () => TakeCall(null, function));
                    row.Keywords = "call function";
                    row.Help = f.Detail;
                    row.Priority = 1;
                    options.Add(row);
                }
                // The engine's namespaces (Math, Random): found by typing their names, opened to what they have
                foreach (var space in scope.Where(m => m.Kind == TideGraphLanguage.KindModule))
                {
                    var part = new GraphFrame.Part { Kind = "name", Name = space.Label, Space = true };
                    options.Add(new GraphOption { Title = space.Label, Detail = GraphLook.FirstLine(space.Detail, 40) + "  ›", Help = space.Detail, Section = engine, Searched = true, KeepOpen = true, Pick = () => Open(part) });
                }
            }
            options.Add(new GraphOption
            {
                Title = "Work it out…", Detail = "An operator, a call: a node wired in", Section = other, Pinned = true, Keywords = "node operator call add multiply compare",
                Help = "Anything more than a value or a name is a node of its own, wired into this input: a sum, a comparison, a call.",
                Pick = () => board.WorkOut(frame, owner, path, From, Query.Trim()),
            });
            if (frame.WireAt(owner, path) != 0)
                options.Add(new GraphOption { Title = "Nothing", Detail = "Empties it", Section = other, Searched = false, Pick = () => frame.Edit("Clear " + label, () => frame.SetWire(owner, path, 0, out _)) });
        }

        // What the last part picked has: itself, its fields (each opening to its own), its methods
        void Inside(List<GraphOption> options, ref int hidden)
        {
            var last = parts[parts.Count - 1];
            var section = "What it has";
            if (!last.Space && last.Kind != "shorthand")
            {
                if (Fits(last.Type) || wanted == null)
                {
                    var whole = Row(PathText, string.IsNullOrEmpty(last.Type) ? "all of it" : "all of it: " + last.Type, section, () => Take(parts.ToList()));
                    whole.Dot = TideGraphTypes.ColorOf(last.Type, Light);
                    whole.Data = "itself";
                    options.Add(whole);
                }
                else hidden++;
            }
            var key = Key(last);
            if (string.IsNullOrEmpty(key) || !members.TryGetValue(key, out var has))
            {
                AskType(key, last.Space);
                return;
            }
            foreach (var m in has)
            {
                if (m.Callable)
                {
                    // A call's result is worked out, and is nothing a Set can change
                    if (Changing) continue;
                    if (m.Result == "void") continue;
                    if (!Fits(m.Result))
                    {
                        hidden++;
                        continue;
                    }
                    var method = m;
                    var row = Row($"{m.Label}({string.Join(", ", m.Parameters)})", m.Result, section, () => TakeCall(parts.ToList(), method));
                    if (string.IsNullOrEmpty(m.Result)) row.Detail = "a call, wired in";
                    row.Keywords = "call method";
                    row.Help = "A node wired in: " + m.Detail;
                    options.Add(row);
                    continue;
                }
                if (m.Kind == TideGraphLanguage.KindKeyword || m.Kind == TideGraphLanguage.KindSnippet) continue;
                AskType(m.Type, false);
                Offer(options, ref hidden, new GraphFrame.Part { Kind = "member", Name = m.Label, Type = m.Type }, section, 2);
            }
            if (has.Count == 0 && asking == 0) options.Add(new GraphOption { Title = "It has nothing to pick", Off = true, Section = section });
        }

        // The way back from what something has, after what it has: Enter takes the first of those
        GraphOption BackRow() => new GraphOption { Title = "‹ Back", Detail = "Esc", Section = "Something else", KeepOpen = true, Data = "back", Pick = () => Back() };

        // Opens something to what it has
        void Open(GraphFrame.Part part)
        {
            parts.Add(part);
            AskType(Key(part), part.Space);
            Fill();
            Opened();
        }

        protected override bool Back()
        {
            if (parts.Count == 0) return false;
            parts.RemoveAt(parts.Count - 1);
            Fill();
            Opened();
            return true;
        }

        // What's typed is itself a value where it reads as one of the input's type: a number, true or false, a text
        protected override void Typed(string text)
        {
            typed.Clear();
            if (parts.Count == 0 && text.Length > 0 && !Changing)
            {
                var control = GraphSchema.ValueControl(wanted);
                var known = Simple(wanted) ? wanted : null;
                if (GraphValues.TryLiteral(text, known, out var k, out var literal))
                    typed.Add(new GraphOption
                    {
                        Title = k == "string" ? "\"" + literal + "\"" : literal, Detail = GraphSchema.Spec(k)?.Help, Section = "A value", Keywords = text, Priority = 5, Data = "typed",
                        Pick = () => Literal(k, literal),
                    });
                else if (control == null && wanted == null && !text.Contains("\""))
                    typed.Add(new GraphOption
                    {
                        Title = "\"" + text + "\"", Detail = "As a text", Section = "A value", Keywords = text, Priority = -1, Data = "typed",
                        Pick = () => Literal("string", text),
                    });
            }
            Fill(true);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Taking

        void Take(List<GraphFrame.Part> picked)
        {
            Close();
            frame.PutPath(owner, path, picked, label);
            board.WatchOperator(owner, path);
        }

        // A value typed or ticked: put in, and a Set's operator looked at once it's checked
        void Literal(string k, string text)
        {
            frame.PutLiteral(owner, path, k, text, label);
            board.WatchOperator(owner, path);
        }

        // A call wired in: a function of the game's, or a method of the path picked (shown in its title: Math.Clamp)
        void TakeCall(List<GraphFrame.Part> of, TideGraphLanguage.Member callee)
        {
            Close();
            var g = frame.Graph;
            if (g == null) return;
            string why = null;
            frame.Edit("Set " + label, () =>
            {
                frame.Classify();
                var reader = owner != null ? frame.ReaderOf(owner.Wire("id")) ?? frame.ViewOf(owner.Wire("id")) : null;
                // Below and left of what reads it, where nothing is
                var at = reader != null && reader.Node != null ? frame.PlaceOfShown(frame.FreePlace(reader.Shown + new Vector2(-240, reader.SizeNow.y + 30), new Vector2(220, 90))) : Vector2.zero;
                var k = of != null ? "method" : "call";
                var order = GraphSchema.NodeOrder(k);
                var call = Doc.AddNode(g, k, at.x, at.y);
                if (of != null) call.Set("object", JValue.Of(frame.MakePath(g, of, at).Last()), order);
                call.Set("name", JValue.Of(callee.Label), order);
                var count = callee.Parameters.Count;
                if (count > 0) call.Set("args", JValue.ArrayOf(Enumerable.Range(0, count).Select(_ => JValue.Null())), order);
                frame.SetWire(owner, path, call.Wire("id"), out why);
            });
            if (why != null) Window.Say(GraphLook.UpperFirst(why), true);
            else board.WatchOperator(owner, path);
        }

        // Picks by a title, as a click on its row does (the story's tests)
        internal bool Choose(string title) => Pick(title);
    }
}
