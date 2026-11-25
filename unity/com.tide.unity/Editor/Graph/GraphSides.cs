using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A panel docked at a side of the board (notes/visual-scripting.md, "Round three"): My game on the left, Details
    // on the right. Each can be closed from its head, and stays as it was left (the window keeps it, and new
    // windows take the last one's).
    abstract class GraphDock : VisualElement
    {
        protected readonly TideGraphWindow Window;
        protected readonly VisualElement Body;
        readonly Label title;

        protected GraphDock(TideGraphWindow window, string heading, bool left)
        {
            Window = window;
            AddToClassList("tg-dock");
            AddToClassList(left ? "tg-dock--left" : "tg-dock--right");
            RegisterCallback<WheelEvent>(e => e.StopPropagation());
            var head = GraphLook.Row("tg-side-head");
            title = GraphLook.Text(heading, "tg-side-title");
            head.Add(title);
            var close = GraphLook.IconButton("close", "Close (the bar's button opens it again)", Close);
            close.name = "close";
            head.Add(close);
            Add(head);
            Body = new ScrollView { style = { flexGrow = 1, flexShrink = 1 } };
            Body.AddToClassList("tg-dock-body");
            Add(Body);
        }

        public string Heading => title.text;

        protected abstract void Close();

        public abstract void Fill();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Details: what's selected, as Blueprint's Details panel has it. Its name, type and starting value; its kind's
    // options as ticks, each with its help under it; "Advanced", closed, for the rest; its note. With nothing
    // selected, the file's own: its namespace and what it uses.

    sealed class GraphDetails : GraphDock
    {
        static bool advancedOpen; // As the user left it, for the session

        public GraphDetails(TideGraphWindow window) : base(window, "Details", false)
        {
            name = "details";
            Fill();
        }

        protected override void Close() => Window.ShowDetails(false);

        GraphBoard Board => Window.Board;
        GraphDocument Doc => Window.Doc;

        // What it shows now, in words: the tests' view of it
        internal string Showing { get; private set; }

        public override void Fill()
        {
            Body.Clear();
            var board = Board;
            if (board == null || Doc == null) return;
            if (board.Active != null && board.Nodes.Count == 1 && board.Active.ViewOf(board.Nodes.First()) is GraphNodeView view && view.Node != null) Node(board.Active, view);
            else if (board.Row != 0 && board.Chosen.Count == 1 && board.Item(board.Chosen.First()) is GraphItem holder && Doc.Find(board.Row) != null) Row(holder, Doc.Find(board.Row));
            else if (board.Chosen.Count == 1 && board.Nodes.Count == 0 && board.Item(board.Chosen.First()) is GraphItem item && item.Element != null) Item(item);
            else if (board.Chosen.Count + board.Nodes.Count > 1)
            {
                Showing = "many";
                Body.Add(GraphLook.Text($"{board.Chosen.Count + board.Nodes.Count} things are selected. Pick one to see its details.", "tg-empty"));
            }
            else File();
        }

        // ---------------------------------------------------------------------------------------------------------
        // Pieces

        void Head(string kind, string group, string help)
        {
            var row = GraphLook.Row("tg-details-head");
            row.Add(GraphLook.Kind(kind, group));
            Body.Add(row);
            if (!string.IsNullOrEmpty(help)) Body.Add(GraphLook.Text(help, "tg-details-help"));
        }

        VisualElement Line(string label, VisualElement control, string help = null, VisualElement into = null)
        {
            var box = new VisualElement();
            box.AddToClassList("tg-details-line");
            var row = GraphLook.Row("tg-details-row");
            row.Add(GraphLook.Text(label, "tg-details-label"));
            if (control != null) row.Add(control);
            box.Add(row);
            if (!string.IsNullOrEmpty(help)) box.Add(GraphLook.Text(help, "tg-details-help"));
            (into ?? Body).Add(box);
            return box;
        }

        // An option as a tick, with its help written under it
        Toggle Tick(string label, string help, bool on, Action toggle, VisualElement into = null, string data = null)
        {
            var box = new VisualElement();
            box.AddToClassList("tg-details-tick");
            // The box first, then its words: the ticks line up whatever their words' lengths
            var t = new Toggle { value = on, text = label };
            t.AddToClassList("tg-details-toggle");
            if (data != null) t.name = "tick:" + data;
            t.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                toggle();
            });
            box.Add(t);
            if (!string.IsNullOrEmpty(help)) box.Add(GraphLook.Text(help, "tg-details-help", "tg-details-help--tick"));
            (into ?? Body).Add(box);
            return t;
        }

        TextField Text(string label, string value, Action<string> commit, string placeholder = null, VisualElement into = null, bool lines = false)
        {
            var f = new TextField { value = value ?? "", isDelayed = true, multiline = lines };
            f.AddToClassList(lines ? "tg-text-area" : "tg-details-text");
            if (placeholder != null) f.textEdition.placeholder = placeholder;
            f.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                if (e.newValue != e.previousValue) commit(e.newValue);
            });
            if (label != null) Line(label, f, null, into);
            else (into ?? Body).Add(f);
            return f;
        }

        Foldout Advanced()
        {
            var f = new Foldout { text = "Advanced", value = advancedOpen };
            f.name = "advanced";
            f.AddToClassList("tg-details-advanced");
            f.RegisterValueChangedCallback(e =>
            {
                if (e.target == f) advancedOpen = e.newValue;
            });
            return f;
        }

        void Note(GraphElement e, IList<string> order, int owner)
        {
            var info = GraphSchema.KeyInfo("comment");
            var box = new VisualElement();
            box.AddToClassList("tg-details-line");
            box.Add(GraphLook.Text(info.Label, "tg-details-label"));
            var f = new TextField { value = e.Value.Str("comment") ?? "", isDelayed = true, multiline = true };
            f.name = "note";
            f.AddToClassList("tg-text-area");
            f.textEdition.placeholder = info.Help;
            f.RegisterValueChangedCallback(ev =>
            {
                ev.StopPropagation();
                Window.Edit(info.Label, () => e.Value.SetString("comment", ev.newValue.TrimEnd(), order), owner);
            });
            box.Add(f);
            Body.Add(box);
        }

        // ---------------------------------------------------------------------------------------------------------
        // A card or a frame: a declaration, a member with code, an accessor

        void Item(GraphItem item)
        {
            var e = item.Element;
            var v = e.Value;
            var kind = v.Str("kind");
            var member = e.Role == GraphRole.Member;
            Showing = "item:" + item.Id;
            if (item is GraphFrame valueFrame && valueFrame.IsValue)
            {
                Head(valueFrame.Title, "Data", "A value worked out by a small graph. Close it from its title.");
                return;
            }
            var title = e.Role == GraphRole.Accessor ? GraphSchema.MemberTitle("property") : member ? GraphSchema.MemberTitle(kind) : GraphSchema.DeclTitle(kind);
            Head(title, member || e.Role == GraphRole.Accessor ? "Behaviour" : GraphSchema.DeclGroup(kind), member ? GraphSchema.MemberSummary(kind) : GraphSchema.DeclSummary(kind));
            var order = GraphDocument.OrderOf(e);
            if (v.Has("name") || GraphSchema.HasKey(kind, member, "name"))
            {
                var f = Text(GraphSchema.KeyInfo("name").Label, v.Str("name"), typed =>
                {
                    var named = item.Q<GraphName>("name");
                    if (named != null) named.Commit(typed);
                    else
                    {
                        var clean = GraphLook.Identifier(typed);
                        if (clean.Length > 0) Window.Edit("Rename", () => v.Set("name", JValue.Of(clean), order));
                    }
                });
                f.name = "name";
            }
            if (kind == "const" || kind == "asset" || kind == "alias")
            {
                var type = v.Str("type");
                Line(GraphSchema.KeyInfo("type").Label, new GraphTypeChip(type, null, Window.Light, from => Board.PickType(new GraphTypeRequest { Title = GraphSchema.KeyInfo("type").Label, Current = type }, from,
                    picked => Window.Edit("Set type", () => v.Set("type", JValue.Of(picked), order), item.Id))));
                if (kind != "alias") Line(GraphSchema.KeyInfo("value").Label, GraphValues.Control(Window, new GraphSlot(e.Id, "value"), kind == "asset" ? "string" : type, false, item.Id, v.Str("name") ?? kind));
            }
            if (kind == "handler" && !member) Trigger(item, v);
            var advanced = Advanced();
            Options(e, kind, member, advanced.contentContainer, item.Id);
            // The rare: what replaces it, attributes, generics, errors, storage
            if (v.Str("overrides") != null || Strings(v.Arr("modifiers")).Contains("override"))
                Text(GraphSchema.KeyInfo("overrides").Label, v.Str("overrides"), typed => Window.Edit(GraphSchema.KeyInfo("overrides").Label, () => v.SetString("overrides", GraphLook.DottedName(typed), order), item.Id), "Combat.Move", advanced.contentContainer);
            if (GraphSchema.HasKey(kind, member, "attributes") || e.Role == GraphRole.Accessor) Attributes(item, e, advanced.contentContainer);
            if (GraphSchema.HasKey(kind, member, "typeParams"))
            {
                var info = GraphSchema.KeyInfo("typeParams");
                var said = string.Join(", ", Strings(v.Arr("typeParams")));
                var b = GraphLook.Plain(() => item.AskGeneric(e), "tg-link");
                b.text = said.Length > 0 ? said : "none";
                Line(info.Label, b, info.Help, advanced.contentContainer);
                if (said.Length > 0)
                {
                    var rules = GraphLook.Plain(() => item.AskRules(e, null), "tg-link");
                    rules.text = (v.Arr("where")?.Items.Count ?? 0) > 0 ? string.Join("; ", v.Arr("where").Items.Select(r => r.Str("param") + ": " + string.Join(", ", Strings(r.Arr("rules"))))) : "none";
                    Line(GraphSchema.KeyInfo("where").Label, rules, GraphSchema.KeyInfo("where").Help, advanced.contentContainer);
                }
            }
            if (GraphSchema.HasKey(kind, member, "fails"))
            {
                var info = GraphSchema.KeyInfo("fails");
                Line(info.Label, new GraphTypeChip(v.Str("fails"), "It can't fail", Window.Light, from => Board.PickType(new GraphTypeRequest { Title = info.Label, Current = v.Str("fails"), Infer = "It can't fail" }, from,
                    type => Window.Edit(info.Label, () => v.SetString("fails", type, order), item.Id))), info.Help, advanced.contentContainer);
            }
            if (GraphSchema.HasKey(kind, member, "backing"))
            {
                var info = GraphSchema.KeyInfo("backing");
                Line(info.Label, new GraphTypeChip(v.Str("backing"), "int (the usual)", Window.Light, from => Board.PickType(new GraphTypeRequest { Title = info.Label, Current = v.Str("backing"), Infer = "int (the usual)", NoBuilders = true, Only = t => GraphValues.IsWhole(t.Name) }, from,
                    type => Window.Edit(info.Label, () => v.SetString("backing", type, order), item.Id))), info.Help, advanced.contentContainer);
            }
            if (kind == "scene" && !member)
                Tick("Nothing of its own", "A scene that's only a name: scene Arena;", v.Get("members") != null && v.Get("members").Kind == JKind.Null, () => Window.Edit("Scene", () =>
                {
                    if (v.Get("members") != null && v.Get("members").Kind == JKind.Null) v.Set("members", JValue.NewArray(), order);
                    else v.Set("members", JValue.Null(), order);
                }, item.Id), advanced.contentContainer);
            if (advanced.contentContainer.childCount > 0) Body.Add(advanced);
            if (item is GraphFrame frame && frame.Graph != null)
            {
                var tidy = GraphLook.Labelled(null, "Tidy up its nodes", () => Board.Tidy(frame), "tg-link");
                tidy.name = "tidy";
                tidy.tooltip = "Lays its nodes out again, as tidec does";
                Body.Add(tidy);
            }
            if (GraphDocument.OrderOf(e).Contains("comment")) Note(e, order, item.Id);
        }

        static IEnumerable<string> Strings(JValue a) => GraphLists.Strings(a);

        // Its kind's options, each a tick with its help under it: the rare ones (the schema's `advanced`) in Advanced
        void Options(GraphElement e, string kind, bool member, VisualElement advanced, int owner)
        {
            var v = e.Value;
            if (e.Role == GraphRole.Decl || member)
            {
                var have = Strings(v.Arr("modifiers")).ToList();
                foreach (var m in GraphSchema.AllModifiers.Where(m => GraphSchema.Modifiers(kind, member).Contains(m) || have.Contains(m)))
                {
                    var word = m;
                    var info = GraphSchema.ModifierInfo(m);
                    Tick(info.Label, info.Help, have.Contains(m), () => Window.Edit(info.Label, () => GraphItem.ToggleModifier(e, word), owner), info.Advanced ? advanced : null, m);
                }
                foreach (var flag in new[] { "mut", "extern" })
                {
                    if (!GraphSchema.HasKey(kind, member, flag)) continue;
                    var key = flag;
                    var info = GraphSchema.KeyInfo(flag);
                    Tick(info.Label, info.Help, v.Flag(flag), () => Window.Edit(info.Label, () => GraphItem.ToggleFlag(e, key), owner), info.Advanced ? advanced : null, flag);
                }
            }
        }

        // A handler's event: as the server confirmed it, a field's change
        void Trigger(GraphItem item, JValue r)
        {
            var e = item.Element;
            JValue Made()
            {
                var made = r.Get("trigger");
                if (made != null && made.IsObject) return made;
                made = JValue.NewObject();
                r.Set("trigger", made, GraphDocument.OrderOf(e));
                return made;
            }
            var t = r.Get("trigger");
            var verified = GraphSchema.KeyInfo("verified");
            Tick(verified.Label, verified.Help, t?.Flag("verified") ?? false, () => Window.Edit(verified.Label, () => Made().SetFlag("verified", !(Made().Flag("verified")), GraphSchema.TriggerOrder), item.Id), null, "verified");
            var changed = t?.Flag("changed") ?? false;
            Tick("A field's change, not an event", "Hears a component's field changing: Changed<Health.current>", changed, () => Window.Edit("Change", () => Made().SetFlag("changed", !changed, GraphSchema.TriggerOrder), item.Id), null, "changed");
        }

        // Its attributes, each changed or taken away by a click, and one more added
        void Attributes(GraphItem item, GraphElement e, VisualElement into)
        {
            var info = GraphSchema.KeyInfo("attributes");
            var box = GraphLook.Row("tg-tags");
            foreach (var a in e.Value.Arr("attributes")?.Items ?? new List<JValue>())
            {
                var attribute = a;
                var b = GraphLook.Plain(() => item.AskAttribute(e, attribute), "tg-tag");
                b.text = GraphItem.AttributeText(a);
                b.tooltip = "Click to change it, or to take it away";
                box.Add(b);
            }
            var add = GraphLook.Labelled("plus", "an attribute…", () => item.AskAttribute(e, null), "tg-link");
            add.name = "add-attribute";
            box.Add(add);
            Line(info.Label, null, info.Help, into).Add(box);
        }

        // ---------------------------------------------------------------------------------------------------------
        // A row: a field, a choice, a setting, a parameter...

        void Row(GraphItem item, GraphElement e)
        {
            var v = e.Value;
            var kind = v.Str("kind");
            var id = e.Id;
            Showing = "row:" + id;
            var order = GraphDocument.OrderOf(e);
            string title, help = null;
            switch (e.Role)
            {
                case GraphRole.Member:
                    title = GraphSchema.MemberTitle(kind);
                    help = GraphSchema.MemberSummary(kind);
                    break;
                case GraphRole.EnumMember: title = "Choice"; break;
                case GraphRole.Entry: title = GraphSchema.DeclTitle("settings"); break;
                case GraphRole.Param: title = GraphSchema.KeyInfo("params").Label; break;
                case GraphRole.Payload: title = GraphSchema.KeyInfo("payload").Label; break;
                default: title = Doc.Describe(e); break;
            }
            Head(title, "Data", help);
            var named = item.Q<GraphName>("name" + id);
            if (v.Has("name") || named != null)
            {
                var f = Text(GraphSchema.KeyInfo("name").Label, v.Str("name"), typed =>
                {
                    var n = item.Q<GraphName>("name" + id);
                    if (n != null) n.Commit(typed);
                    else
                    {
                        var clean = GraphLook.Identifier(typed);
                        if (clean.Length > 0) Window.Edit("Rename", () => v.Set("name", JValue.Of(clean), order), item.Id);
                    }
                });
                f.name = "name";
            }
            var frame = item as GraphFrame;
            if (e.Role == GraphRole.Param && frame != null)
            {
                var mode = v.Str("mode") ?? "read";
                var info = GraphSchema.ParamModeInfo(mode);
                Button how = null;
                how = GraphLook.Plain(() => frame.OpenModes(how, e.List, v), "tg-word");
                how.text = info.Label;
                how.name = "mode";
                Line("Taken", how, info.Help);
            }
            if (v.Has("type") || kind == "field" || kind == "tweak" || kind == "property")
            {
                var type = v.Str("type");
                Line(GraphSchema.KeyInfo("type").Label, new GraphTypeChip(type, null, Window.Light, from => Board.PickType(new GraphTypeRequest { Title = "Type of " + (v.Str("name") ?? title), Current = type, Place = string.IsNullOrEmpty(type) ? null : new GraphTypePlace { Element = id, Find = type } }, from,
                    picked => Window.Edit("Set type", () => v.Set("type", JValue.Of(picked), order), item.Id))));
            }
            // Its starting value: typed in, or worked out by a small graph
            var key = kind == "field" || e.Role == GraphRole.Param ? "default" : kind == "tweak" || e.Role == GraphRole.EnumMember || e.Role == GraphRole.Entry ? "value" : null;
            if (key != null && !(e.Role == GraphRole.Param && (v.Str("mode") == "with" || v.Str("mode") == "without" || v.Str("mode") == "query")))
            {
                var slot = new GraphSlot(id, key);
                var box = GraphLook.Row("tg-details-value");
                box.Add(GraphValues.Control(Window, slot, v.Str("type") ?? (e.Role == GraphRole.EnumMember ? "int" : null), key == "default", item.Id, v.Str("name")));
                var work = GraphLook.Plain(() =>
                {
                    Board.OpenValue(slot);
                }, "tg-link");
                work.text = "Work it out…";
                work.name = "work-out";
                work.tooltip = "A small graph, for more than a number or a text";
                box.Add(work);
                if (v.Get(key) != null)
                {
                    var clear = GraphLook.IconButton("close", "No starting value of its own", () => Window.Edit("Clear value", () => v.Remove(key), item.Id));
                    clear.name = "clear-value";
                    box.Add(clear);
                }
                Line(GraphSchema.KeyInfo(key == "default" ? "default" : "value").Label, box);
            }
            if (kind == "property")
            {
                var links = GraphLook.Row("tg-tags");
                foreach (var a in v.Arr("accessors")?.Items ?? new List<JValue>())
                {
                    var accessor = a.Wire("id");
                    var link = GraphLook.Plain(() => Board.Reveal(accessor), "tg-link");
                    link.text = GraphSchema.AccessorInfo(a.Str("word")).Label;
                    links.Add(link);
                }
                Line("Its code", links);
            }
            var advanced = Advanced();
            if (kind == "field")
            {
                var info = GraphSchema.KeyInfo("local");
                Tick(info.Label, info.Help, v.Flag("local"), () => Window.Edit(info.Label, () => v.SetFlag("local", !v.Flag("local"), order), item.Id), info.Advanced ? advanced.contentContainer : null, "local");
            }
            if (e.Role == GraphRole.Param)
            {
                var o = order;
                var mode = v.Str("mode") ?? "read";
                var filter = mode == "with" || mode == "without";
                foreach (var flag in new[] { "local", "verified", "previous" })
                {
                    var f = flag;
                    var info = GraphSchema.KeyInfo(flag);
                    Tick(info.Label, info.Help, v.Flag(flag), () => Window.Edit(info.Label, () => v.SetFlag(f, !v.Flag(f), o), item.Id), info.Advanced ? advanced.contentContainer : null, flag);
                }
                if (filter)
                    Tick("Owning it, not having a component", "Only (or never) the entities this machine's player owns", v.Flag("ownership"), () => Window.Edit("Ownership", () =>
                    {
                        var on = !v.Flag("ownership");
                        v.SetFlag("ownership", on, o);
                        if (on) v.Remove("type");
                        else if (v.Str("type") == null) v.Set("type", JValue.Of("Transform"), o);
                    }, item.Id), advanced.contentContainer, "ownership");
            }
            if (GraphSchema.HasKey(kind, true, "attributes") && e.Role == GraphRole.Member) Attributes(item, e, advanced.contentContainer);
            // Its place among its card's rows
            if (e.List != null && e.List.Items.Count > 1)
            {
                var at = e.List.Items.IndexOf(v);
                var place = GraphLook.Row("tg-tags");
                var up = GraphLook.Labelled(null, "Earlier", () => Window.Edit("Move up", () => GraphFrame.Swap(e.List, at, at - 1), item.Id), "tg-link");
                up.name = "earlier";
                up.SetEnabled(at > 0);
                var down = GraphLook.Labelled(null, "Later", () => Window.Edit("Move down", () => GraphFrame.Swap(e.List, at, at + 1), item.Id), "tg-link");
                down.name = "later";
                down.SetEnabled(at < e.List.Items.Count - 1);
                place.Add(up);
                place.Add(down);
                Line("Order", place, null, advanced.contentContainer);
            }
            if (advanced.contentContainer.childCount > 0) Body.Add(advanced);
            if (order.Contains("comment")) Note(e, order, item.Id);
        }

        // ---------------------------------------------------------------------------------------------------------
        // A node: its own flags, a call's type arguments and argument names, its note

        void Node(GraphFrame frame, GraphNodeView view)
        {
            Showing = "node:" + view.Id;
            var shown = view.Call ?? view.Node;
            var spec = GraphSchema.Spec(shown.Str("k"));
            // A variable's or a field's read is a Get, as Blueprint calls it
            var get = view.Call == null && GraphNodeView.IsGet(shown.Str("k"));
            Head(get ? GraphBoard.GetWord : spec?.Title ?? shown.Str("k"), "Behaviour", get ? "Its value: one Get can be wired to any number of inputs." : spec?.Help);
            var advanced = Advanced();
            foreach (var v in view.Call != null ? new[] { view.Call, view.Node } : new[] { view.Node })
            {
                var k = v.Str("k");
                var s = GraphSchema.Spec(k);
                if (s == null) continue;
                var order = GraphSchema.NodeOrder(k);
                foreach (var key in s.Keys)
                {
                    if (!GraphSchema.IsFlag(key)) continue;
                    var flag = key;
                    var target = v;
                    var words = GraphSchema.FlagInfo(key);
                    Tick(words.Label, words.Help, v.Flag(key), () => frame.Edit(words.Label, () => target.SetFlag(flag, !target.Flag(flag), order)), words.Advanced ? advanced.contentContainer : null, key);
                }
                if (k == "call" || k == "method")
                {
                    var target = v;
                    var types = GraphLook.Row("tg-tags");
                    foreach (var t in GraphLists.Strings(v.Arr("typeArgs"))) types.Add(GraphLook.Text(t, "tg-tag"));
                    var add = GraphLook.Labelled("plus", "a type…", () => Board.PickType(new GraphTypeRequest { Title = "Type argument", Generic = Doc.TypeParamsAt(frame.Slot.Owner) }, view, type => frame.Edit("Type argument", () =>
                    {
                        var args = GraphLists.Strings(target.Arr("typeArgs")).ToList();
                        args.Add(type);
                        target.Set("typeArgs", JValue.Strings(args), order);
                    })), "tg-link");
                    types.Add(add);
                    Line("Type arguments", null, "For a call that names its types: Get<Health>()", advanced.contentContainer).Add(types);
                    if ((v.Arr("args")?.Items.Count ?? 0) > 0)
                    {
                        var names = GraphLook.Plain(() => frame.AskArgNames(view, target), "tg-link");
                        names.text = v.Arr("argNames") != null ? string.Join(", ", GraphLists.Strings(v.Arr("argNames")).Select(x => x.Length > 0 ? x : "_")) : "none";
                        Line("Argument names", names, "A call that says which input is which: crit: true", advanced.contentContainer);
                    }
                }
            }
            if (advanced.contentContainer.childCount > 0) Body.Add(advanced);
            var e = Doc.Find(shown.Wire("id"));
            if (e != null) Note(e, GraphSchema.NodeOrder(shown.Str("k")), frame.Id);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Nothing selected: the file's own lines

        void File()
        {
            Showing = "file";
            var doc = Doc;
            Head("This file", "More", "Select something on the board to see its details here.");
            var space = Text("Namespace", doc.Root.Str("namespace"), typed => Window.Edit("Namespace", () => doc.Root.SetString("namespace", GraphLook.DottedName(typed), GraphSchema.RootOrder)), "no namespace");
            space.name = "namespace";
            Body.Add(GraphLook.Text("The namespace everything in this file is in. Leave it empty for none.", "tg-details-help"));
            var used = doc.Root.Arr("using");
            var list = GraphLook.Row("tg-tags");
            for (var i = 0; used != null && i < used.Items.Count; i++)
            {
                var at = i;
                var b = GraphLook.Plain(() => Window.Edit("Remove using", () =>
                {
                    var a = doc.Root.Arr("using");
                    a.Items.RemoveAt(at);
                    if (a.Items.Count == 0) doc.Root.Remove("using");
                }), "tg-tag");
                b.text = used.Items[i].String + "  ×";
                b.tooltip = "Stop using it";
                list.Add(b);
            }
            var uses = Line("Uses", null, "The namespaces whose names it uses without spelling them out.");
            uses.Add(list);
            var add = new TextField();
            add.AddToClassList("tg-details-text");
            add.name = "using";
            add.textEdition.placeholder = "Use a namespace… (Enter adds it)";
            add.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode != KeyCode.Return && e.keyCode != KeyCode.KeypadEnter) return;
                e.StopPropagation();
                var name = GraphLook.DottedName(add.text);
                if (name.Length == 0) return;
                Window.Edit("Add using", () =>
                {
                    var a = doc.Root.Arr("using");
                    if (a == null) doc.Root.Set("using", a = JValue.NewArray(), GraphSchema.RootOrder);
                    if (!a.Items.Any(x => x.String == name)) a.Items.Add(JValue.Of(name));
                });
            }, TrickleDown.TrickleDown);
            uses.Add(add);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // My game: the file's declarations by the schema's groups, as Blueprint's My Blueprint panel has them. A "+" on
    // each group's heading adds one of its kinds; a click pans to one, a double click renames it; dragged out, a
    // component or a singleton onto a frame's entry adds it as a need, a function into a frame makes its call, a
    // constant onto an input fills it. Ctrl+F finds one by name.

    sealed class GraphMyGame : GraphDock
    {
        readonly TextField query;
        readonly VisualElement list;
        readonly List<(int id, string title)> shown = new List<(int, string)>();

        public GraphMyGame(TideGraphWindow window) : base(window, "My game", true)
        {
            name = "my-game";
            query = new TextField();
            query.AddToClassList("tg-search");
            query.name = "find";
            query.textEdition.placeholder = "Find… (Ctrl+F)";
            query.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                Fill();
            });
            query.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode == KeyCode.Escape)
                {
                    query.value = "";
                    Window.Board?.Focus();
                    e.StopPropagation();
                }
                if (e.keyCode != KeyCode.Return && e.keyCode != KeyCode.KeypadEnter) return;
                e.StopPropagation();
                if (shown.Count > 0) Window.Reveal(shown[0].id);
            }, TrickleDown.TrickleDown);
            Insert(1, query);
            list = new VisualElement();
            Body.Add(list);
            Fill();
        }

        protected override void Close() => Window.ShowMyGame(false);

        public void FocusSearch() => query.schedule.Execute(() => query.Focus());

        // What it lists now, in order: the tests' view of it
        internal IReadOnlyList<(int id, string title)> Shown => shown;

        internal void Type(string text) => query.value = text;

        // The groups' headings, in order (the selected code's variables are before them)
        internal List<string> Groups => list.Query<Label>(className: "tg-section-title").ToList().Where(l => l.parent?.name != "variables").Select(l => l.text).ToList();

        public override void Fill()
        {
            list.Clear();
            shown.Clear();
            var doc = Window.Doc;
            if (doc == null) return;
            var typed = (query.value ?? "").Trim().ToLowerInvariant();
            var rows = new List<(string group, int id, string title, string kind, int depth, GraphElement e)>();
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
                    rows.Add((group, e.Id, (space.Length > 0 ? space + "." : "") + doc.TitleOf(e), GraphSchema.DeclTitle(kind), 0, e));
                    var members = d.Arr("members");
                    if (members == null || kind == "enum") continue;
                    foreach (var m in members.Items)
                    {
                        if (!m.IsObject) continue;
                        var mk = m.Str("kind");
                        if (GraphSchema.HasBody(mk, true) && doc.Find(m.Wire("id")) is GraphElement me) rows.Add((group, me.Id, doc.TitleOf(me), GraphSchema.MemberTitle(mk), 1, me));
                        var accessors = m.Arr("accessors");
                        if (accessors == null) continue;
                        foreach (var a in accessors.Items)
                            if (doc.Find(a.Wire("id")) is GraphElement ae) rows.Add((group, ae.Id, doc.TitleOf(ae), GraphSchema.MemberTitle(mk), 1, ae));
                    }
                }
            }
            Decls(doc.Root.Arr("decls"), "");
            Variables(typed);
            foreach (var group in GraphSchema.Groups)
            {
                var of = rows.Where(r => r.group == group.Name && (typed.Length == 0 || (r.title + " " + r.kind).ToLowerInvariant().Contains(typed))).ToList();
                if (typed.Length > 0 && of.Count == 0) continue;
                var heading = GraphLook.Row("tg-section", "tg-mine-group");
                var words = GraphLook.Text(group.Title.ToUpperInvariant(), "tg-section-title");
                words.tooltip = group.Summary;
                heading.Add(words);
                heading.Add(new VisualElement { style = { flexGrow = 1 } });
                var g = group;
                Button add = null;
                add = GraphLook.IconButton("plus", $"Add to {group.Title}: {string.Join(", ", GraphSchema.DeclKinds.Where(k => GraphSchema.DeclGroup(k) == g.Name).Select(GraphSchema.DeclTitle))}", () => AddOf(g, add));
                add.name = "add:" + group.Name;
                heading.Add(add);
                list.Add(heading);
                if (of.Count == 0) list.Add(GraphLook.Text(group.Summary + ". The + adds one.", "tg-empty"));
                foreach (var r in of) list.Add(Row(r.id, r.title, r.kind, r.depth, r.e));
            }
            if (typed.Length > 0 && shown.Count == 0) list.Add(GraphLook.Text($"Nothing here is called '{query.value.Trim()}'.", "tg-empty"));
        }

        // ---------------------------------------------------------------------------------------------------------
        // Variables: what the selected code has (its parameters, its locals), as Blueprint's My Blueprint lists a
        // graph's. Each opens to its fields; dragged onto the board it asks "Get <name>" or "Set <name>" (Ctrl makes
        // a Get, Alt a Set), and dropped on an input it's a Get wired in.

        GraphFrame variablesOf;  // The code whose variables are listed
        readonly HashSet<string> opened = new HashSet<string>();  // Variables and fields opened to their fields
        readonly Dictionary<string, List<TideGraphLanguage.Member>> fieldsOf = new Dictionary<string, List<TideGraphLanguage.Member>>();
        readonly HashSet<string> asked = new HashSet<string>();
        string fieldsText; // The file as it was when what the types have was asked
        readonly List<(string title, List<GraphFrame.Part> parts)> variables = new List<(string, List<GraphFrame.Part>)>();

        // What it lists of the selected code's variables, in order: the tests' view of it
        internal IReadOnlyList<(string title, List<GraphFrame.Part> parts)> VariablesShown => variables;
        internal GraphFrame VariablesOf => variablesOf;
        internal const string VariablesTitle = "Variables";

        // The selection moved: the variables are those of the code it's in now
        internal void Selected()
        {
            if (Window.Board?.CodeInFocus != variablesOf) Fill();
        }

        // Opens a variable or a field (by its path, ball.position) to its fields, or closes it
        internal void Open(string path, bool open)
        {
            if (open) opened.Add(path);
            else opened.Remove(path);
            Fill();
        }

        static bool Simple(string type) => GraphSchema.ValueControl(type) is string c && (c == "whole" || c == "decimal" || c == "tick" || c == "text" || c == "char");

        void Variables(string typed)
        {
            variables.Clear();
            var frame = Window.Board?.CodeInFocus;
            if (frame != variablesOf)
            {
                opened.Clear();
                variablesOf = frame;
            }
            // What a type has is asked again once the file has changed (a field added to a component)
            if (opened.Count > 0 && Window.Doc.Text != fieldsText)
            {
                fieldsOf.Clear();
                asked.Clear();
                fieldsText = Window.Doc.Text;
            }
            var heading = GraphLook.Row("tg-section", "tg-mine-group");
            var words = GraphLook.Text(VariablesTitle.ToUpperInvariant(), "tg-section-title");
            words.tooltip = "What the selected code has: what it takes and its locals. Drag one onto its code to get or set it.";
            heading.Add(words);
            heading.name = "variables";
            if (frame != null) heading.Add(GraphLook.Text(frame.Title, "tg-option-detail", "tg-mine-of"));
            list.Add(heading);
            if (frame == null)
            {
                list.Add(GraphLook.Text("Select some code to see what it has: what it takes and its locals.", "tg-empty"));
                return;
            }
            frame.Classify();
            var all = new List<GraphFrame.Part>();
            foreach (var read in frame.Reads.Values) all.Add(new GraphFrame.Part { Kind = "name", Name = read.Name, Type = read.Type });
            foreach (var local in GraphDocument.Locals(frame.Graph))
            {
                var name = GraphSchema.LocalName(local);
                if (frame.Reads.ContainsKey(name) || all.Any(p => p.Kind == "local" && p.Name == name)) continue;
                all.Add(new GraphFrame.Part { Kind = "local", Ref = local.Wire("id"), Name = name, Type = !string.IsNullOrEmpty(local.Str("type")) ? local.Str("type") : TypeOfLocal(frame, local) });
            }
            if (all.Count == 0)
            {
                list.Add(GraphLook.Text("It takes nothing and has no locals yet.", "tg-empty"));
                return;
            }
            foreach (var part in all) Variable(frame, new List<GraphFrame.Part> { part }, typed, 0);
        }

        // A local with no type of its own: what its reads were checked to be
        static string TypeOfLocal(GraphFrame frame, JValue local)
        {
            foreach (var n in frame.ById.Values)
                if (n.Str("k") == "local" && n.Wire("ref") == local.Wire("id") && frame.TypeAt(n.Wire("id")) is string t) return t;
            var value = local.Wire("value");
            return value != 0 ? frame.TypeAt(value) : null;
        }

        void Variable(GraphFrame frame, List<GraphFrame.Part> parts, string typed, int depth)
        {
            var part = parts[parts.Count - 1];
            var path = GraphFrame.PathText(parts);
            var type = part.Type;
            var opens = !string.IsNullOrEmpty(type) && !Simple(type) && type.IndexOfAny(new[] { '<', '[' }) < 0;
            var isOpen = opens && opened.Contains(path);
            if (typed.Length == 0 || path.ToLowerInvariant().Contains(typed) || isOpen)
                list.Add(VariableRow(frame, parts, path, type, depth, opens, isOpen));
            if (!isOpen) return;
            if (!fieldsOf.TryGetValue(type, out var fields))
            {
                // Listed once it's answered (which may be at once, without the language server: then nothing)
                if (asked.Add(type))
                    TideGraphLanguage.MembersOfType(Window, type, (found, _) =>
                    {
                        fieldsOf[type] = found ?? new List<TideGraphLanguage.Member>();
                        schedule.Execute(() =>
                        {
                            if (parent != null) Fill();
                        });
                    });
                list.Add(GraphLook.Text("Asking what it has…", "tg-empty", "tg-mine-asking"));
                return;
            }
            foreach (var f in fields.Where(m => !m.Callable && m.Kind != TideGraphLanguage.KindKeyword && m.Kind != TideGraphLanguage.KindSnippet))
                Variable(frame, parts.Append(new GraphFrame.Part { Kind = "member", Name = f.Label, Type = f.Type }).ToList(), typed, depth + 1);
            if (fields.Count == 0) list.Add(GraphLook.Text("It has no fields.", "tg-empty"));
        }

        VisualElement VariableRow(GraphFrame frame, List<GraphFrame.Part> parts, string path, string type, int depth, bool opens, bool isOpen)
        {
            var row = GraphLook.Row("tg-option", "tg-mine-row", "tg-mine-var");
            row.name = "var:" + path;
            row.style.paddingLeft = 6 + depth * 14;
            variables.Add((path, parts));
            if (opens)
            {
                var arrow = GraphLook.IconButton(isOpen ? "down" : "right", isOpen ? "Close its fields" : "Open it to its fields", () => Open(path, !isOpen), "tg-mine-arrow");
                arrow.name = "open";
                row.Add(arrow);
            }
            else row.Add(new VisualElement { style = { width = 18, flexShrink = 0 } });
            var dot = GraphLook.Row("tg-type-dot");
            dot.style.backgroundColor = TideGraphTypes.ColorOf(type, Window.Light);
            row.Add(dot);
            row.Add(GraphLook.Text(depth == 0 ? path : parts[parts.Count - 1].Name, "tg-option-title"));
            row.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            row.Add(GraphLook.Text(type ?? "", "tg-option-detail"));
            var changeable = frame.Changeable(parts);
            row.tooltip = $"{path}{(string.IsNullOrEmpty(type) ? "" : ": " + type)}\nDrag it onto its code to get or set it (Ctrl: a Get, Alt: a Set), or onto an input to wire a Get of it in." + (changeable ? "" : "\nIt's only read here: it can't be set.");
            // What it reads is lit on the board under the pointer
            var root = parts[0];
            row.RegisterCallback<PointerEnterEvent>(_ => frame.Light(root.Kind == "local" ? null : root.Name, root.Kind == "local" ? root.Ref : 0, true));
            row.RegisterCallback<PointerLeaveEvent>(_ => frame.Light(root.Kind == "local" ? null : root.Name, root.Kind == "local" ? root.Ref : 0, false));
            DragsVariable(row, frame, parts, path);
            return row;
        }

        // A click goes to where it comes from (the entry's row, the node that declares a local); a drag carries a
        // Get or a Set of it out onto the board
        void DragsVariable(VisualElement row, GraphFrame frame, List<GraphFrame.Part> parts, string path)
        {
            Vector2 start = default;
            var down = false;
            var dragging = false;
            Label ghost = null;
            row.RegisterCallback<PointerDownEvent>(ev =>
            {
                if (ev.button != 0 || GraphLook.TakesClicks(ev.target as VisualElement, row)) return;
                down = true;
                dragging = false;
                start = ev.position;
                row.CapturePointer(ev.pointerId);
                ev.StopPropagation();
            });
            row.RegisterCallback<PointerMoveEvent>(ev =>
            {
                if (!down) return;
                if (!dragging && ((Vector2)ev.position - start).sqrMagnitude > 36)
                {
                    dragging = true;
                    ghost = GraphLook.Text(path, "tg-mine-ghost");
                    ghost.pickingMode = PickingMode.Ignore;
                    Window.Overlay?.Add(ghost);
                }
                if (ghost != null && ghost.parent != null)
                {
                    var at = ghost.parent.WorldToLocal(ev.position);
                    ghost.style.left = at.x + 12;
                    ghost.style.top = at.y + 6;
                    ghost.text = ev.actionKey || ev.ctrlKey ? $"{GraphBoard.GetWord} {path}" : ev.altKey ? $"{GraphBoard.SetWord} {path}" : path;
                }
            });
            row.RegisterCallback<PointerUpEvent>(ev =>
            {
                if (!down) return;
                down = false;
                if (row.HasPointerCapture(ev.pointerId)) row.ReleasePointer(ev.pointerId);
                ghost?.RemoveFromHierarchy();
                ghost = null;
                ev.StopPropagation();
                if (dragging)
                {
                    dragging = false;
                    Window.Board?.DropVariable(frame, parts, ev.position, ev.actionKey || ev.ctrlKey, ev.altKey);
                    return;
                }
                var root = parts[0];
                if (root.Kind == "local")
                {
                    Window.Board?.SelectNode(frame, root.Ref, false);
                    return;
                }
                var read = frame.Reads.TryGetValue(root.Name, out var r) ? r : null;
                if (read != null && read.Id != 0) Window.Board?.SelectRow(frame, read.Id);
            });
        }

        // A group's kinds, to add one of
        void AddOf(GraphSchema.GroupSpec group, VisualElement from)
        {
            var kinds = GraphSchema.DeclKinds.Where(k => GraphSchema.DeclGroup(k) == group.Name).ToList();
            var options = kinds.Select(k => new GraphOption { Title = GraphSchema.DeclTitle(k), Detail = GraphSchema.DeclSummary(k), Tall = true, Data = k, Pick = () => Window.Board?.AddDeclaration(k) }).ToList();
            var menu = new GraphOptionList(group.Title, group.Summary, null, 300);
            menu.Set(options);
            Window.Open(menu, from, true);
        }

        VisualElement Row(int id, string title, string kind, int depth, GraphElement e)
        {
            var row = GraphLook.Row("tg-option", "tg-mine-row");
            row.userData = id;
            row.style.paddingLeft = 10 + depth * 14;
            var group = e.Role == GraphRole.Decl ? GraphSchema.DeclGroup(e.Value.Str("kind")) : "Behaviour";
            var dot = GraphLook.Row("tg-mine-dot", "tg-mine-dot--" + group);
            row.Add(dot);
            // Its name: a double click renames it, as on the board
            GraphName name = null;
            if (e.Role == GraphRole.Decl && e.Value.Has("name") && depth == 0)
            {
                var shortName = e.Value.Str("name");
                name = GraphLook.Name(shortName, typed =>
                {
                    if (Window.Doc.Elements.Any(o => o.Role == GraphRole.Decl && o.Id != id && o.Value.Str("name") == typed && o.List == e.List))
                    {
                        Window.Say($"There's a '{typed}' in this file already: two things can't have one name.", true);
                        Fill();
                        return;
                    }
                    Window.Edit("Rename", () => e.Value.Set("name", JValue.Of(typed), GraphDocument.OrderOf(e)), id);
                }, "name");
                name.Named("name" + id);
                name.AddToClassList("tg-mine-name");
                if (title != shortName && title.EndsWith(shortName)) row.Add(GraphLook.Text(title.Substring(0, title.Length - shortName.Length), "tg-option-detail", "tg-mine-space"));
                row.Add(name);
            }
            else row.Add(GraphLook.Text(title, "tg-option-title"));
            row.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            row.Add(GraphLook.Text(kind, "tg-option-detail"));
            var item = Window.Board?.Item(id);
            if (item != null && Window.Board.HasProblems(item)) row.Add(new GraphIcon("warn") { style = { color = new StyleColor(new Color(1f, 0.45f, 0.4f)), marginLeft = 6 }, tooltip = "It has a problem" });
            row.tooltip = "Click to go to it, double-click to rename it" + (Dragged(e) != null ? ", drag it out: " + Dragged(e) : "");
            Drags(row, id, title, e);
            shown.Add((id, title));
            return row;
        }

        // What dragging it out does, in words; null for what does nothing
        static string Dragged(GraphElement e)
        {
            if (e.Role != GraphRole.Decl) return null;
            switch (e.Value.Str("kind"))
            {
                case "component":
                case "singleton": return "onto a frame's entry, it's something that code needs";
                case "function": return "into a frame, it's a call of it";
                case "const": return "onto an input, it's what goes in it";
                default: return null;
            }
        }

        // A click goes to it; a drag carries it out onto the board
        void Drags(VisualElement row, int id, string title, GraphElement e)
        {
            Vector2 start = default;
            var down = false;
            var dragging = false;
            Label ghost = null;
            row.RegisterCallback<PointerDownEvent>(ev =>
            {
                if (ev.button != 0) return;
                if (ev.clickCount >= 2)
                {
                    row.Q<GraphName>()?.Edit();
                    ev.StopPropagation();
                    return;
                }
                if (GraphLook.TakesClicks(ev.target as VisualElement, row)) return;
                down = true;
                dragging = false;
                start = ev.position;
                row.CapturePointer(ev.pointerId);
                ev.StopPropagation();
            });
            row.RegisterCallback<PointerMoveEvent>(ev =>
            {
                if (!down) return;
                if (!dragging && ((Vector2)ev.position - start).sqrMagnitude > 36 && Dragged(e) != null)
                {
                    dragging = true;
                    ghost = GraphLook.Text(title, "tg-mine-ghost");
                    ghost.pickingMode = PickingMode.Ignore;
                    Window.Overlay?.Add(ghost);
                }
                if (ghost != null && ghost.parent != null)
                {
                    var at = ghost.parent.WorldToLocal(ev.position);
                    ghost.style.left = at.x + 12;
                    ghost.style.top = at.y + 6;
                }
            });
            row.RegisterCallback<PointerUpEvent>(ev =>
            {
                if (!down) return;
                down = false;
                if (row.HasPointerCapture(ev.pointerId)) row.ReleasePointer(ev.pointerId);
                ghost?.RemoveFromHierarchy();
                ghost = null;
                if (dragging)
                {
                    dragging = false;
                    Window.Board?.DropFromMyGame(id, ev.position);
                }
                else Window.Reveal(id);
                ev.StopPropagation();
            });
        }
    }
}
