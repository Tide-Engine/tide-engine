using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // How much of an item is drawn: nothing while it's off screen, its name alone from far away, all of it near
    enum GraphLevel { None, Far, Near }

    // A thing on the board: a card (data) or a frame (code). It's where the file says (x, y), is drawn only while
    // it's on screen, and is built again alone when what it shows changes.
    abstract class GraphItem : VisualElement
    {
        public readonly GraphBoard Board;
        public readonly int Id;      // The declaration, member or accessor it's of
        public Vector2 At;           // On the board
        public Vector2 Size;         // As measured when it's drawn, as estimated until then
        public bool Placed;          // Its place is the file's, not one the board worked out
        public bool Measured;
        public GraphLevel Level { get; private set; }
        bool selected;

        protected GraphItem(GraphBoard board, int id)
        {
            Board = board;
            Id = id;
            AddToClassList("tg-item");
        }

        public TideGraphWindow Window => Board.Window;
        public GraphDocument Doc => Board.Window.Doc;
        public GraphElement Element => Doc.Find(Id);
        public Rect World => new Rect(At, Size);
        public abstract string Title { get; }
        public abstract Vector2 Estimate();
        protected abstract void BuildNear();

        protected void Edit(string what, Action change) => Window.Edit(what, change, Id);

        public void Place()
        {
            style.left = At.x;
            style.top = At.y;
        }

        public void Show(GraphLevel level, bool again = false)
        {
            if (level == Level && !again) return;
            Level = level;
            problemRows.Clear();
            Clear();
            EnableInClassList("tg-item--far", level == GraphLevel.Far);
            if (level == GraphLevel.None)
            {
                Cleared();
                return;
            }
            if (Element == null) return;
            if (level == GraphLevel.Near) BuildNear();
            else BuildFar();
            ShowProblems();
        }

        // Built again as it is now, after what it shows changed
        public void Rebuild() => Show(Level, true);

        protected virtual void Cleared()
        {
        }

        // From far away: its name, as big as it is small
        protected virtual void BuildFar()
        {
            style.width = Size.x;
            style.height = Size.y;
            var title = GraphLook.Text(Title, "tg-far-title");
            title.pickingMode = PickingMode.Ignore;
            // As big on screen as a title is up close, but no bigger than the item has room for
            var size = Mathf.Clamp(14f / Mathf.Max(Board.Zoom, 0.02f), 13, Mathf.Max(13, Mathf.Min(Size.y * 0.4f, Size.x / Mathf.Max(4, Title.Length) * 1.4f)));
            title.style.fontSize = size;
            Add(title);
        }

        public bool Selected
        {
            get => selected;
            set
            {
                selected = value;
                EnableInClassList("tg-selected", value);
            }
        }

        // Its border lit for a moment: what a jump went to
        public void Flash()
        {
            AddToClassList("tg-flash");
            schedule.Execute(() => RemoveFromClassList("tg-flash")).ExecuteLater(900);
        }

        // Where each element's problems are said: under its row. Every check fills these again from what it
        // found, so a message is never one of a check before, and nothing's rebuilt under the keyboard for it.
        readonly List<(int id, VisualElement row, VisualElement holder)> problemRows = new List<(int, VisualElement, VisualElement)>();

        public virtual void ShowProblems()
        {
            ShowBadge();
            EnableInClassList("tg-has-problem", Board.HasProblems(this));
            problemRows.RemoveAll(p => p.holder.parent == null);
            foreach (var (id, row, holder) in problemRows) Fill(id, row, holder);
        }

        // The ids whose problems are this item's to show: its own, its members', its parameters'
        public abstract IEnumerable<int> Carries();

        // How many problems it has in it, on its title bar, as the page shows it: the errors in red, else the
        // warnings in amber, nothing when there are none
        protected Label badge;

        protected Label Badge()
        {
            badge = GraphLook.Text("", "tg-badge");
            badge.name = "badge";
            badge.style.display = DisplayStyle.None;
            ShowBadge();
            return badge;
        }

        void ShowBadge()
        {
            if (badge == null) return;
            var said = Carries().SelectMany(Board.Problems).Distinct().ToList();
            var errors = said.Count(d => d.Severity == "error");
            var count = errors > 0 ? errors : said.Count(d => d.Severity == "warning");
            badge.text = count.ToString();
            badge.style.display = count > 0 ? DisplayStyle.Flex : DisplayStyle.None;
            badge.EnableInClassList("tg-badge--warning", errors == 0);
            badge.tooltip = errors > 0 ? (errors == 1 ? "1 problem" : $"{errors} problems") : count == 1 ? "1 warning" : $"{count} warnings";
        }

        internal string BadgeText => badge != null && badge.style.display != DisplayStyle.None ? badge.text : null;

        // ---------------------------------------------------------------------------------------------------------
        // What cards and frames both have: a note, options, attributes

        // The tags under a name: the options that are on, what it's generic over, its attributes
        protected VisualElement Tags(GraphElement e)
        {
            var v = e.Value;
            var member = e.Role == GraphRole.Member;
            var kind = v.Str("kind");
            var tags = GraphLook.Row("tg-tags");
            foreach (var m in Strings(v.Arr("modifiers")))
            {
                var info = GraphSchema.ModifierInfo(m);
                var tag = GraphLook.Text(info.Label, "tg-tag");
                tag.tooltip = info.Help ?? "";
                tags.Add(tag);
            }
            if (v.Str("overrides") != null) tags.Add(Tag($"{GraphSchema.KeyInfo("overrides").Label} {v.Str("overrides")}", () => AskReplaces(e)));
            foreach (var flag in new[] { "extern", "mut" })
                if (v.Flag(flag) && e.Role != GraphRole.Param) tags.Add(GraphLook.Text(GraphSchema.KeyInfo(flag).Label, "tg-tag"));
            var generic = Strings(v.Arr("typeParams")).ToList();
            if (generic.Count > 0) tags.Add(Tag($"{GraphSchema.KeyInfo("typeParams").Label} {string.Join(", ", generic)}", () => AskGeneric(e)));
            var where = v.Arr("where");
            if (where != null)
                foreach (var rule in where.Items)
                    tags.Add(Tag($"{rule.Str("param")}: {string.Join(", ", Strings(rule.Arr("rules")))}", () => AskRules(e, rule)));
            if (v.Str("backing") != null) tags.Add(GraphLook.Text($"{GraphSchema.KeyInfo("backing").Label} {v.Str("backing")}", "tg-tag"));
            if (v.Str("fails") != null) tags.Add(GraphLook.Text($"{GraphSchema.KeyInfo("fails").Label} {v.Str("fails")}", "tg-tag"));
            var attributes = v.Arr("attributes");
            if (attributes != null)
                foreach (var a in attributes.Items)
                {
                    var attribute = a;
                    tags.Add(Tag(AttributeText(a), () => AskAttribute(e, attribute)));
                }
            tags.style.display = tags.childCount > 0 ? DisplayStyle.Flex : DisplayStyle.None;
            return tags;
        }

        static Button Tag(string text, Action click)
        {
            var b = GraphLook.Plain(click, "tg-tag");
            b.text = text;
            b.tooltip = "Click to change it";
            return b;
        }

        internal static IEnumerable<string> Strings(JValue a) => a == null ? Enumerable.Empty<string>() : a.Items.Where(i => i.Kind == JKind.String).Select(i => i.String);

        public static string AttributeText(JValue a) => "[" + a.Str("name") + (a.Has("args") ? "(" + a.Str("args") + ")" : "") + "]";

        // A note (the file's comment), under the name: a click edits it
        protected VisualElement Note(GraphElement e)
        {
            var text = e.Value.Str("comment");
            if (string.IsNullOrEmpty(text)) return null;
            var note = GraphLook.Plain(() => AskNote(e), "tg-note");
            note.text = GraphLook.Brief(text);
            note.tooltip = text + "\n\n(Click to change this note.)";
            return note;
        }

        internal void AskNote(GraphElement e)
        {
            var info = GraphSchema.KeyInfo("comment");
            Window.Open(new GraphPrompt(info.Label, info.Help, e.Value.Str("comment"), true, text =>
                Edit(info.Label, () => e.Value.SetString("comment", text.TrimEnd(), GraphDocument.OrderOf(e)))), this, true);
        }

        internal void AskGeneric(GraphElement e)
        {
            var info = GraphSchema.KeyInfo("typeParams");
            Window.Open(new GraphPrompt(info.Label, (info.Help ?? "") + "\nNames, separated by commas. Leave it empty for none.", string.Join(", ", Strings(e.Value.Arr("typeParams"))), false, text =>
            {
                var names = text.Split(',').Select(GraphLook.Identifier).Where(n => n.Length > 0).Distinct().ToList();
                Edit(info.Label, () =>
                {
                    if (names.Count == 0)
                    {
                        e.Value.Remove("typeParams");
                        e.Value.Remove("where");
                    }
                    else e.Value.Set("typeParams", JValue.Strings(names), GraphDocument.OrderOf(e));
                });
            }), this, true);
        }

        internal void AskRules(GraphElement e, JValue rule)
        {
            var info = GraphSchema.KeyInfo("where");
            var param = rule?.Str("param") ?? Strings(e.Value.Arr("typeParams")).FirstOrDefault() ?? "T";
            Window.Open(new GraphPrompt($"{info.Label}: {param}", (info.Help ?? "") + "\nSeparated by commas. Leave it empty for none.", string.Join(", ", Strings(rule?.Arr("rules"))), false, text =>
            {
                var rules = text.Split(',').Select(GraphLook.Identifier).Where(n => n.Length > 0).Distinct().ToList();
                Edit(info.Label, () =>
                {
                    var list = e.Value.Arr("where");
                    if (rules.Count == 0)
                    {
                        if (rule != null) list?.Items.Remove(rule);
                        if (list != null && list.Items.Count == 0) e.Value.Remove("where");
                        return;
                    }
                    if (rule == null)
                    {
                        if (list == null) e.Value.Set("where", list = JValue.NewArray(), GraphDocument.OrderOf(e));
                        rule = JValue.NewObject();
                        rule.Set("param", JValue.Of(param));
                        list.Items.Add(rule);
                    }
                    rule.Set("rules", JValue.Strings(rules), GraphSchema.WhereOrder);
                });
            }), this, true);
        }

        internal void AskReplaces(GraphElement e)
        {
            var info = GraphSchema.KeyInfo("overrides");
            Window.Open(new GraphPrompt(info.Label, "The declaration of a package this takes the place of, with its namespace: Combat.Move", e.Value.Str("overrides"), false, text =>
                Edit(info.Label, () => e.Value.SetString("overrides", GraphLook.DottedName(text), GraphDocument.OrderOf(e)))), this, true);
        }

        // An attribute, as Tide writes it between its brackets: `Stage(Phase.Move, 2)`. Nothing typed takes it away.
        internal void AskAttribute(GraphElement e, JValue attribute)
        {
            var info = GraphSchema.KeyInfo("attributes");
            var now = attribute == null ? "" : AttributeText(attribute).Trim('[', ']');
            Window.Open(new GraphPrompt(info.Label, (info.Help ?? "") + "\nAs Tide writes it between the brackets. Leave it empty to take it away.", now, false, text =>
            {
                text = text.Trim().Trim('[', ']').Trim();
                var paren = text.IndexOf('(');
                var name = GraphLook.DottedName(paren >= 0 ? text.Substring(0, paren) : text);
                var args = paren >= 0 ? text.Substring(paren + 1).TrimEnd().TrimEnd(')') : null;
                Edit(info.Label, () =>
                {
                    var list = e.Value.Arr("attributes");
                    if (name.Length == 0)
                    {
                        if (attribute != null) list?.Items.Remove(attribute);
                        if (list != null && list.Items.Count == 0) e.Value.Remove("attributes");
                        return;
                    }
                    if (attribute == null)
                    {
                        if (list == null) e.Value.Set("attributes", list = JValue.NewArray(), GraphDocument.OrderOf(e));
                        attribute = Doc.NewAttribute(name);
                        list.Items.Add(attribute);
                    }
                    attribute.Set("name", JValue.Of(name), GraphSchema.AttributeOrder);
                    if (args != null) attribute.Set("args", JValue.Of(args), GraphSchema.AttributeOrder);
                    else attribute.Remove("args");
                });
            }), this, true);
        }

        internal static void ToggleModifier(GraphElement e, string word)
        {
            var list = Strings(e.Value.Arr("modifiers")).ToList();
            if (!list.Remove(word)) list.Add(word);
            // In the order tidec writes them
            list = GraphSchema.AllModifiers.Where(list.Contains).Concat(list.Where(x => !GraphSchema.AllModifiers.Contains(x))).ToList();
            if (list.Count == 0) e.Value.Remove("modifiers");
            else e.Value.Set("modifiers", JValue.Strings(list), GraphDocument.OrderOf(e));
            if (word == "override" && !list.Contains(word)) e.Value.Remove("overrides");
        }

        internal static void ToggleFlag(GraphElement e, string key)
        {
            var order = GraphDocument.OrderOf(e);
            var on = !e.Value.Flag(key);
            e.Value.SetFlag(key, on, order);
            if (key != "extern") return;
            // Code written in C has no body here
            if (on) e.Value.Remove("body");
            else if (!e.Value.Has("body")) e.Value.Set("body", GraphDocument.EmptyBody(), order);
        }

        // The problems of an element, as lines under its row: a place for them, filled now and by every check
        internal VisualElement AddProblems(VisualElement into, int id, VisualElement row = null)
        {
            var holder = new VisualElement();
            holder.AddToClassList("tg-problems");
            into.Add(holder);
            problemRows.Add((id, row, holder));
            Fill(id, row, holder);
            return holder;
        }

        void Fill(int id, VisualElement row, VisualElement holder)
        {
            holder.Clear();
            var said = Board.Problems(id);
            var error = said.Any(d => d.Severity == "error");
            row?.EnableInClassList("tg-row--problem", error);
            row?.EnableInClassList("tg-row--warning", said.Count > 0 && !error);
            foreach (var d in said)
            {
                var line = GraphLook.Text(GraphLook.FirstLine(d.Text, 160), "tg-problem-text", "tg-problem");
                if (d.Severity != "error") line.AddToClassList("tg-problem-text--warning");
                line.tooltip = d.Text + (string.IsNullOrEmpty(d.Detail) ? "" : "\n\n" + d.Detail);
                holder.Add(line);
            }
        }
    }

    // A few words asked for where a list can't offer them: a note, the names something is generic over, an attribute
    sealed class GraphPrompt : GraphPopup
    {
        readonly TextField field;
        readonly Action<string> done;

        public GraphPrompt(string title, string help, string value, bool lines, Action<string> done)
        {
            this.done = done;
            style.width = 320;
            Add(GraphLook.Text(title, "tg-popup-head"));
            if (!string.IsNullOrEmpty(help)) Add(GraphLook.Text(help, "tg-popup-help"));
            field = new TextField { value = value ?? "", multiline = lines };
            field.AddToClassList(lines ? "tg-text-area" : "tg-search");
            if (lines) field.style.marginLeft = field.style.marginRight = field.style.marginBottom = 6;
            field.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode != KeyCode.Return && e.keyCode != KeyCode.KeypadEnter) return;
                if (lines && !e.ctrlKey && !e.commandKey) return;
                e.StopPropagation();
                Commit();
            }, TrickleDown.TrickleDown);
            Add(field);
            var row = GraphLook.Row();
            row.style.flexDirection = FlexDirection.Row;
            row.style.justifyContent = Justify.FlexEnd;
            row.style.paddingRight = 6;
            row.Add(GraphLook.Text(lines ? "Ctrl+Enter keeps it" : "Enter keeps it", "tg-faint"));
            row.Add(GraphLook.Plain(Commit, "tg-link").Class("tg-prompt-ok"));
            ((Button)row[1]).text = "Done";
            Add(row);
        }

        public string Value
        {
            get => field.value;
            set => field.value = value;
        }

        public override void Opened() => field.schedule.Execute(() => field.Focus());

        public void Commit()
        {
            var text = field.value;
            Close();
            done(text);
        }
    }

    // A card: a declaration that is data. Its kind in plain words, its name, and a row for each field (name, type,
    // starting value), each edited where it's drawn; a last row adds one. Its "..." menu has what else its kind
    // can have, and nothing its kind can't.
    sealed class GraphCard : GraphItem
    {
        public GraphCard(GraphBoard board, int id) : base(board, id)
        {
            AddToClassList("tg-card");
            RegisterCallback<GeometryChangedEvent>(e =>
            {
                if (Level != GraphLevel.Near || e.newRect.height <= 0) return;
                var size = new Vector2(GraphLook.CardWidth, e.newRect.height);
                if (Measured && (size - Size).sqrMagnitude < 0.25f) return;
                Size = size;
                Measured = true;
                Board.Resized(this);
            });
        }

        public override string Title => Doc.TitleOf(Element);

        JValue Decl => Element?.Value;
        string Kind => Decl?.Str("kind");

        public override Vector2 Estimate()
        {
            var d = Decl;
            if (d == null) return new Vector2(GraphLook.CardWidth, GraphLook.CardHead);
            var rows = (d.Arr("members")?.Items.Count ?? 0) + (d.Arr("entries")?.Items.Count ?? 0);
            var text = d.Str("text");
            if (text != null) rows += text.Count(c => c == '\n') + 1;
            return new Vector2(GraphLook.CardWidth, GraphLook.CardHead + GraphLook.CardRow * (Mathf.Min(rows, 24) + 1));
        }

        public override IEnumerable<int> Carries()
        {
            var e = Element;
            if (e == null) yield break;
            yield return Id;
            foreach (var key in new[] { "members", "entries" })
            {
                var list = e.Value.Arr(key);
                if (list == null) continue;
                foreach (var m in list.Items)
                    if (m.IsObject) yield return m.Wire("id");
            }
            foreach (var p in PrefabNodes(e.Value.Get("root"))) yield return p.Wire("id");
        }

        static IEnumerable<JValue> PrefabNodes(JValue p)
        {
            if (p == null || !p.IsObject) yield break;
            yield return p;
            var children = p.Arr("children");
            if (children == null) yield break;
            foreach (var c in children.Items)
                foreach (var n in PrefabNodes(c)) yield return n;
        }

        protected override void BuildFar()
        {
            style.height = Size.y;
            base.BuildFar();
            style.width = StyleKeyword.Null;
        }

        // The marks on the rows of members with code: whether their frames have a problem
        readonly List<(int id, VisualElement mark)> codeMarks = new List<(int, VisualElement)>();

        public override void ShowProblems()
        {
            base.ShowProblems();
            foreach (var (id, mark) in codeMarks) mark.style.display = Board.ProblemsUnder(id) ? DisplayStyle.Flex : DisplayStyle.None;
        }

        protected override void BuildNear()
        {
            style.height = StyleKeyword.Null;
            codeMarks.Clear();
            var e = Element;
            var d = e.Value;
            var kind = Kind;
            var head = GraphLook.Row("tg-card-head");
            head.Add(GraphLook.Kind(GraphSchema.DeclTitle(kind), GraphSchema.DeclGroup(kind)));
            if (d.Has("name") || GraphSchema.HasKey(kind, false, "name"))
            {
                var name = GraphLook.Name(d.Str("name"), Rename, "name", "tg-inline--title");
                name.Named("name");
                name.RegisterCallback<KeyDownEvent>(ev =>
                {
                    if (ev.keyCode != KeyCode.Return && ev.keyCode != KeyCode.KeypadEnter) return;
                    // A card just made, named: its first field is next
                    if (Board.Fresh != Id || !CanAdd("field")) return;
                    ev.StopPropagation();
                    var typed = GraphLook.Identifier(name.text);
                    AddField(typed.Length > 0 && typed != d.Str("name") ? typed : null);
                }, TrickleDown.TrickleDown);
                head.Add(name);
            }
            // What it is, where it has no name (the settings)
            else head.Add(GraphLook.Text(d.Has("text") ? "" : GraphSchema.DeclSummary(kind) ?? "", "tg-name-fixed"));
            head.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            head.Add(Badge());
            // Its rows are added from its title bar, not from a row under the last one that's hit by accident
            var adds = RowAdd();
            if (adds != null)
            {
                var add = GraphLook.IconButton("plus", adds.Value.title, adds.Value.add);
                add.name = "add";
                head.Add(add);
            }
            var more = GraphLook.IconButton("more", "Add, rename, copy, delete (or right-click)", null);
            more.clicked += () => GraphMenu.Open(Window, MenuOptions(), more);
            more.name = "more";
            head.Add(more);
            Add(head);
            var tags = Tags(e);
            if (tags.childCount > 0) Add(tags);
            var note = Note(e);
            if (note != null) Add(note);
            AddProblems(this, Id);
            Rows = GraphLook.Row("tg-rows");
            Add(Rows);

            switch (kind)
            {
                case "enum":
                    Choices(d);
                    break;
                case "settings":
                    Entries(d);
                    break;
                case "const":
                case "asset":
                {
                    var row = GraphLook.Row("tg-row");
                    row.Add(GraphLook.Text(GraphSchema.KeyInfo("type").Label, "tg-port-label"));
                    row.Add(TypeChip(e, "type", d.Str("type"), null));
                    row.Add(new VisualElement { style = { flexGrow = 1 } });
                    row.Add(GraphValues.Control(Window, new GraphSlot(Id, "value"), kind == "asset" ? "string" : d.Str("type"), false, Id, d.Str("name") ?? kind).Class("tg-row-value--wide"));
                    Rows.Add(row);
                    break;
                }
                case "alias":
                {
                    var row = GraphLook.Row("tg-row");
                    row.Add(GraphLook.Text("is another name for", "tg-port-label"));
                    row.Add(TypeChip(e, "type", d.Str("type"), null));
                    Rows.Add(row);
                    break;
                }
                case "text":
                {
                    var text = new TextField { value = d.Str("text") ?? "", multiline = true, isDelayed = true };
                    text.AddToClassList("tg-text-area");
                    text.style.unityFontDefinition = FontDefinition.FromFont(GraphLook.Mono);
                    text.RegisterValueChangedCallback(ev =>
                    {
                        ev.StopPropagation();
                        Edit("Tide text", () => d.Set("text", JValue.Of(ev.newValue), GraphSchema.DeclOrder(kind)));
                    });
                    Rows.Add(text);
                    break;
                }
                case "prefab":
                    Prefab(d.Get("root"), 0);
                    break;
                default:
                    Members(d);
                    break;
            }
            SpaceRows();
        }

        // ---------------------------------------------------------------------------------------------------------
        // Names

        void Rename(string name)
        {
            var e = Element;
            if (Doc.Elements.Any(o => o.Role == GraphRole.Decl && o.Id != Id && o.Value.Str("name") == name && o.List == e.List))
            {
                Window.Say($"There's a '{name}' in this file already: two things can't have one name.", true);
                Board.Refresh(Id);
                return;
            }
            Edit("Rename", () => e.Value.Set("name", JValue.Of(name), GraphDocument.OrderOf(e)));
        }

        // What a row's "name" field does with what's typed: no two of a card's rows share a name
        bool NameTaken(JValue list, JValue but, string name)
        {
            if (list == null || !list.Items.Any(o => o != but && o.IsObject && o.Str("name") == name)) return false;
            Window.Say($"{Title} has a '{name}' already.", true);
            Board.Refresh(Id);
            return true;
        }

        GraphTypeChip TypeChip(GraphElement of, string key, string type, string unset, Func<GraphTypeInfo, bool> only = null)
        {
            var chip = new GraphTypeChip(type, unset, Board.Light, from => Board.PickType(new GraphTypeRequest
            {
                Title = $"{GraphSchema.KeyInfo(key).Label} of {of.Value.Str("name") ?? Title}", Current = type, Only = only, Generic = Doc.TypeParamsAt(of.Id),
                Place = string.IsNullOrEmpty(type) ? null : new GraphTypePlace { Element = of.Id, Find = type },
            }, from, picked =>
            {
                Edit("Set type", () => of.Value.Set(key, JValue.Of(picked), GraphDocument.OrderOf(of)));
                // The keyboard stays on the type: Tab goes on to its value
                Board.FocusLater(Id, "type" + of.Id);
            }));
            chip.name = "type" + of.Id;
            return chip;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Members

        bool CanAdd(string memberKind) => GraphDocument.OfferedMemberKinds(Decl).Contains(memberKind);

        // Its rows: its fields, choices, settings, its value
        VisualElement Rows;

        // What an empty card says goes in it, and how
        static Label Empty(string what) => GraphLook.Text(GraphSchema.Word("card.rows.empty", "what", (what ?? "").TrimEnd('…').ToLowerInvariant()), "tg-rows-empty");

        // Its rows two pixels apart, as the page's
        void SpaceRows()
        {
            var first = true;
            foreach (var c in Rows.Children())
            {
                if (!c.ClassListContains("tg-row") && !c.ClassListContains("tg-rows-empty")) continue;
                c.style.marginTop = first ? 0 : 2;
                first = false;
            }
        }

        // The words of a kind's last row (the schema's addRow), without the plus the row draws
        static string RowWords(string kind)
        {
            var words = GraphSchema.AddRow(kind, false);
            return words == null ? null : words.StartsWith("+") ? words.Substring(1).Trim() : words;
        }

        // The kinds of member its "..." menu offers: its kind's, but fields (its last row adds those)
        internal List<string> OfferedMembers() => GraphDocument.OfferedMemberKinds(Decl);

        void Members(JValue d)
        {
            var members = d.Arr("members");
            var count = members?.Items.Count ?? 0;
            for (var i = 0; i < count; i++)
            {
                var m = members.Items[i];
                if (m.IsObject) Member(members, m, i == count - 1);
            }
            if (!CanAdd("field")) return;
            // An empty card says what goes in it, and where the button is that adds one
            if (count == 0) Rows.Add(Empty(RowWords(Kind) ?? GraphSchema.MemberTitle("field")));
        }

        // What its title bar's "+" adds: a field, a choice, a setting; null for a kind with no rows to add
        internal (string title, Action add)? RowAdd()
        {
            var kind = Kind;
            if (kind == "enum") return (GraphMenu.AddA(RowWords("enum") ?? "choice"), () => AddNamed("members", "Choice", null));
            if (kind == "settings") return (GraphMenu.AddA(RowWords("settings") ?? "setting"), () => AddNamed("entries", "setting", "0"));
            if (CanAdd("field")) return (GraphMenu.AddA(RowWords(kind) ?? GraphSchema.MemberTitle("field")), () => AddField());
            return null;
        }

        // What its menu offers to add: its rows, then each other kind of member its kind can have
        internal IEnumerable<(string title, Action add)> Adds()
        {
            var row = RowAdd();
            if (row != null) yield return row.Value;
            foreach (var kind in OfferedMembers())
            {
                if (kind == "field") continue;
                var k = kind;
                yield return (GraphMenu.AddMember(kind), () => AddMember(k));
            }
        }

        void Member(JValue list, JValue m, bool last)
        {
            var id = m.Wire("id");
            var e = Doc.Find(id);
            if (e == null) return;
            var kind = m.Str("kind");
            var row = GraphLook.Row("tg-row");
            row.name = "row" + id;
            row.userData = id;
            switch (kind)
            {
                case "field":
                case "tweak":
                {
                    if (kind == "tweak") row.Add(GraphLook.Text(GraphSchema.MemberTitle(kind), "tg-tag"));
                    if (m.Flag("local"))
                    {
                        var info = GraphSchema.KeyInfo("local");
                        var tag = GraphLook.Text(info.Label, "tg-tag");
                        tag.tooltip = info.Help ?? "";
                        row.Add(tag);
                    }
                    var name = GraphLook.Name(m.Str("name"), typed =>
                    {
                        if (!NameTaken(list, m, typed)) Edit("Rename", () => m.Set("name", JValue.Of(typed), GraphSchema.MemberOrder(kind)));
                    }, "name");
                    name.Named("name" + id);
                    name.RegisterCallback<KeyDownEvent>(ev => FieldKeys(ev, name, list, m, last && kind == "field"), TrickleDown.TrickleDown);
                    row.Add(name);
                    row.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
                    row.Add(TypeChip(e, "type", m.Str("type"), null));
                    row.Add(GraphValues.Control(Window, new GraphSlot(id, kind == "tweak" ? "value" : "default"), m.Str("type"), kind == "field", Id, m.Str("name")));
                    break;
                }
                case "property":
                {
                    var name = GraphLook.Name(m.Str("name"), typed =>
                    {
                        if (!NameTaken(list, m, typed)) Edit("Rename", () => m.Set("name", JValue.Of(typed), GraphSchema.MemberOrder(kind)));
                    }, "name");
                    name.Named("name" + id);
                    row.Add(name);
                    row.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
                    row.Add(TypeChip(e, "type", m.Str("type"), null));
                    var accessors = m.Arr("accessors");
                    if (accessors != null)
                        foreach (var a in accessors.Items)
                        {
                            var accessor = a.Wire("id");
                            var link = GraphLook.Plain(() => Board.Reveal(accessor), "tg-link");
                            link.text = a.Str("word");
                            link.tooltip = "Go to its code";
                            row.Add(link);
                        }
                    break;
                }
                case "text":
                {
                    var text = new TextField { value = m.Str("text") ?? "", multiline = true, isDelayed = true };
                    text.AddToClassList("tg-text-area");
                    text.style.unityFontDefinition = FontDefinition.FromFont(GraphLook.Mono);
                    text.RegisterValueChangedCallback(ev =>
                    {
                        ev.StopPropagation();
                        Edit("Tide text", () => m.Set("text", JValue.Of(ev.newValue), GraphSchema.MemberOrder(kind)));
                    });
                    row.Add(text);
                    break;
                }
                default:
                {
                    // A member with code: its frame is its own, and this row goes to it
                    var link = GraphLook.Labelled("code", Doc.TitleOf(e).Substring(Math.Max(0, Doc.TitleOf(e).IndexOf('.') + 1)), () => Board.Reveal(id), "tg-link");
                    link.tooltip = (GraphSchema.MemberSummary(kind) ?? GraphSchema.MemberTitle(kind)) + ". Go to its code.";
                    row.Add(GraphLook.Text(GraphSchema.MemberTitle(kind), "tg-tag"));
                    row.Add(link);
                    row.Add(new VisualElement { style = { flexGrow = 1 } });
                    var mark = new GraphIcon("warn") { tooltip = "Its code has a problem", style = { color = new StyleColor(new Color(1f, 0.45f, 0.4f)) } };
                    mark.style.display = Board.ProblemsUnder(id) ? DisplayStyle.Flex : DisplayStyle.None;
                    codeMarks.Add((id, mark));
                    row.Add(mark);
                    break;
                }
            }
            var more = GraphLook.IconButton("more", "Rename, copy, delete (or right-click)", null, "tg-on-hover");
            more.clicked += () => GraphMenu.Open(Window, GraphMenu.ForRow(this, id), more);
            row.Add(more);
            Rows.Add(row);
            var note = m.Str("comment");
            if (!string.IsNullOrEmpty(note))
            {
                var line = GraphLook.Plain(() => AskNote(e), "tg-note");
                line.text = GraphLook.Brief(note);
                line.tooltip = note;
                Rows.Add(line);
            }
            var attributes = m.Arr("attributes");
            AddProblems(Rows, id, row);
        }

        // Enter in a field's name keeps what's typed and goes on: to a new field after the last, else to the next
        void FieldKeys(KeyDownEvent ev, GraphName name, JValue list, JValue m, bool last)
        {
            var id = m.Wire("id");
            if (ev.keyCode == KeyCode.Escape && Board.Fresh == id)
            {
                // A row just added and left: gone again
                ev.StopPropagation();
                Board.Fresh = 0;
                Edit("Remove field", () => list.Items.Remove(m));
                Board.FocusLater(Id, "add");
                return;
            }
            if (ev.keyCode != KeyCode.Return && ev.keyCode != KeyCode.KeypadEnter) return;
            ev.StopPropagation();
            var typed = GraphLook.Identifier(name.text);
            var renamed = typed.Length > 0 && typed != m.Str("name");
            if (renamed && NameTaken(list, m, typed)) return;
            if (last)
            {
                AddField(null, renamed ? (m, typed) : ((JValue, string)?)null);
                return;
            }
            if (renamed) Edit("Rename", () => m.Set("name", JValue.Of(typed), GraphSchema.MemberOrder(m.Str("kind"))));
            var at = list.Items.IndexOf(m);
            for (var i = at + 1; i < list.Items.Count; i++)
            {
                var k = list.Items[i].Str("kind");
                if (k != "field" && k != "tweak" && k != "property") continue;
                Board.FocusLater(Id, "name" + list.Items[i].Wire("id"));
                return;
            }
            Board.FocusLater(Id, "add");
        }

        // A new field, its name ready to be typed over: of the type of the field before it, since fields of a
        // kind come together, else the first of the common types. `cardName` renames the card in the same step.
        internal int AddField(string cardName = null, (JValue member, string name)? rename = null)
        {
            var d = Decl;
            if (!CanAdd("field")) return 0;
            JValue made = null;
            Edit("Add field", () =>
            {
                if (cardName != null) d.Set("name", JValue.Of(cardName), GraphSchema.DeclOrder(Kind));
                if (rename != null) rename.Value.member.Set("name", JValue.Of(rename.Value.name));
                var members = d.Arr("members");
                if (members == null) d.Set("members", members = JValue.NewArray(), GraphSchema.DeclOrder(Kind));
                var before = members.Items.LastOrDefault(x => x.IsObject && x.Str("kind") == "field");
                made = Doc.NewMember("field", GraphDocument.UniqueIn(members, "value"));
                made.Set("type", JValue.Of(before?.Str("type") ?? GraphSchema.TypeCommon.FirstOrDefault() ?? "int"), GraphSchema.MemberOrder("field"));
                members.Items.Add(made);
            });
            if (made == null) return 0;
            Board.Fresh = made.Wire("id");
            Board.FocusLater(Id, "name" + made.Wire("id"));
            return made.Wire("id");
        }

        // A member of another kind, from the "..." menu: a method, a property, an operator...
        internal int AddMember(string kind)
        {
            var d = Decl;
            if (!CanAdd(kind)) return 0;
            if (kind == "field") return AddField();
            JValue made = null;
            Window.Edit("Add " + GraphSchema.MemberTitle(kind).ToLowerInvariant(), () =>
            {
                var members = d.Arr("members");
                if (members == null) d.Set("members", members = JValue.NewArray(), GraphSchema.DeclOrder(Kind));
                var name = kind == "method" || kind == "property" ? GraphDocument.UniqueIn(members, GraphSchema.MemberTitle(kind)) : kind == "tweak" ? GraphDocument.UniqueIn(members, "tweak") : null;
                made = Doc.NewMember(kind, name);
                if (kind == "operator") made.Set("returnType", JValue.Of(d.Str("name") ?? "int"), GraphSchema.MemberOrder(kind));
                Board.PlaceNew(made, GraphSchema.MemberOrder(kind));
                members.Items.Add(made);
            });
            if (made == null) return 0;
            var id = made.Wire("id");
            if (GraphSchema.HasBody(kind, true)) Board.Reveal(id, true);
            else if (kind == "property") Board.FocusLater(Id, "name" + id);
            else Board.FocusLater(Id, "name" + id);
            return id;
        }

        // ---------------------------------------------------------------------------------------------------------
        // An enum's choices, the settings' entries

        void Choices(JValue d)
        {
            var members = d.Arr("members");
            var count = members?.Items.Count ?? 0;
            for (var i = 0; i < count; i++) Named(members, members.Items[i], i == count - 1, true, "choice", null);
            if (count == 0) Rows.Add(Empty(RowWords("enum") ?? "choice"));
        }

        void Entries(JValue d)
        {
            var entries = d.Arr("entries");
            var count = entries?.Items.Count ?? 0;
            for (var i = 0; i < count; i++) Named(entries, entries.Items[i], i == count - 1, false, "setting", null);
            if (count == 0) Rows.Add(Empty(RowWords("settings") ?? "setting"));
        }

        void Named(JValue list, JValue item, bool last, bool optionalValue, string what, string type)
        {
            var id = item.Wire("id");
            var e = Doc.Find(id);
            if (e == null) return;
            var row = GraphLook.Row("tg-row");
            row.name = "row" + id;
            row.userData = id;
            var name = GraphLook.Name(item.Str("name"), typed =>
            {
                if (!NameTaken(list, item, typed)) Edit("Rename", () => item.Set("name", JValue.Of(typed), GraphDocument.OrderOf(e)));
            }, "name");
            name.Named("name" + id);
            name.RegisterCallback<KeyDownEvent>(ev =>
            {
                if (ev.keyCode == KeyCode.Escape && Board.Fresh == id)
                {
                    ev.StopPropagation();
                    Board.Fresh = 0;
                    Edit("Remove " + what, () => list.Items.Remove(item));
                    Board.FocusLater(Id, "add");
                    return;
                }
                if ((ev.keyCode != KeyCode.Return && ev.keyCode != KeyCode.KeypadEnter) || !last) return;
                ev.StopPropagation();
                var typed = GraphLook.Identifier(name.text);
                var renamed = typed.Length > 0 && typed != item.Str("name");
                if (renamed && NameTaken(list, item, typed)) return;
                AddNamed(what == "choice" ? "members" : "entries", what == "choice" ? "Choice" : "setting", what == "choice" ? null : "0", renamed ? (item, typed) : ((JValue, string)?)null);
            }, TrickleDown.TrickleDown);
            row.Add(name);
            row.Add(new VisualElement { style = { flexGrow = 1 }, pickingMode = PickingMode.Ignore });
            var value = GraphValues.Control(Window, new GraphSlot(id, "value"), type, optionalValue, Id, item.Str("name"));
            if (optionalValue)
            {
                // A choice's number is the next one unless it's given: asked for only by who points at it
                var given = item.Get("value") != null;
                var equals = GraphLook.Text("=", "tg-port-label");
                if (!given)
                {
                    equals.AddToClassList("tg-on-hover");
                    value.AddToClassList("tg-on-hover");
                    var field = value.Q<TextField>();
                    if (field != null) field.textEdition.placeholder = "its number";
                }
                row.Add(equals);
            }
            row.Add(value);
            var more = GraphLook.IconButton("more", "Rename, copy, delete (or right-click)", null, "tg-on-hover");
            more.clicked += () => GraphMenu.Open(Window, GraphMenu.ForRow(this, id), more);
            row.Add(more);
            Rows.Add(row);
            AddProblems(Rows, id, row);
        }

        int AddNamed(string key, string name, string value, (JValue item, string name)? rename = null)
        {
            var d = Decl;
            JValue made = null;
            Edit("Add " + name.ToLowerInvariant(), () =>
            {
                if (rename != null) rename.Value.item.Set("name", JValue.Of(rename.Value.name));
                var list = d.Arr(key);
                if (list == null) d.Set(key, list = JValue.NewArray(), GraphSchema.DeclOrder(Kind));
                made = Doc.NewNamed(GraphDocument.UniqueIn(list, name), value != null ? "int" : null, value);
                list.Items.Add(made);
            });
            if (made == null) return 0;
            Board.Fresh = made.Wire("id");
            Board.FocusLater(Id, "name" + made.Wire("id"));
            return made.Wire("id");
        }

        // ---------------------------------------------------------------------------------------------------------
        // A prefab: its entities, each with its components

        void Prefab(JValue p, int depth)
        {
            if (p == null || !p.IsObject) return;
            var id = p.Wire("id");
            var e = Doc.Find(id);
            if (e == null) return;
            var row = GraphLook.Row("tg-row");
            row.userData = id;
            row.style.paddingLeft = 6 + depth * 14;
            if (e.List == null) row.Add(GraphLook.Text("The entity", "tg-port-label"));
            else
            {
                var name = GraphLook.Name(p.Str("name"), typed => Edit("Rename", () => p.Set("name", JValue.Of(typed), GraphSchema.PrefabNodeOrder)), "name");
                name.Named("name" + id);
                row.Add(name);
                if (p.Str("base") != null) row.Add(GraphLook.Text("from " + p.Str("base"), "tg-tag"));
            }
            row.Add(new VisualElement { style = { flexGrow = 1 } });
            var child = GraphLook.IconButton("plus", "Add a child entity", () => Edit("Add child entity", () =>
            {
                var children = p.Arr("children");
                if (children == null) p.Set("children", children = JValue.NewArray(), GraphSchema.PrefabNodeOrder);
                children.Items.Add(Doc.NewPrefabNode(GraphDocument.UniqueIn(children, "Child")));
            }), "tg-on-hover");
            row.Add(child);
            if (e.List != null)
            {
                var more = GraphLook.IconButton("more", "Rename, copy, delete (or right-click)", null, "tg-on-hover");
                more.clicked += () => GraphMenu.Open(Window, GraphMenu.ForRow(this, id), more);
                row.Add(more);
            }
            Rows.Add(row);
            var components = p.Arr("components");
            var line = GraphLook.Row("tg-tags");
            line.style.paddingLeft = 12 + depth * 14;
            for (var i = 0; components != null && i < components.Items.Count; i++)
            {
                var slot = new GraphSlot(id, "components", i);
                var graph = components.Items[i];
                var root = graph.IsObject ? GraphDocument.NodeIn(graph, graph.Wire("root")) : null;
                var title = root?.Str("k") == "literal" ? root.Str("type") : root?.Str("k") == "call" ? root.Str("name") : "component";
                var link = GraphLook.Plain(() => Board.OpenValue(slot), "tg-link");
                link.text = title;
                link.tooltip = "Open what this component starts as";
                line.Add(link);
            }
            Button add = null;
            add = GraphLook.Labelled("plus", "component", () => Board.PickType(new GraphTypeRequest { Title = "A component of this entity", NoBuilders = true, Only = t => t.Declared == "component" },
                add, type => Edit("Add component", () =>
                {
                    var list = p.Arr("components");
                    if (list == null) p.Set("components", list = JValue.NewArray(), GraphSchema.PrefabNodeOrder);
                    var g = JValue.NewObject();
                    var n = Doc.NewNode("literal", 0, 0);
                    n.Set("type", JValue.Of(type), GraphSchema.NodeOrder("literal"));
                    g.Set("root", JValue.Of(n.Wire("id")));
                    g.Set("nodes", JValue.ArrayOf(new[] { n }));
                    list.Items.Add(g);
                })), "tg-link");
            line.Add(add);
            Rows.Add(line);
            AddProblems(Rows, id, row);
            var kids = p.Arr("children");
            if (kids != null) foreach (var c in kids.Items) Prefab(c, depth + 1);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Menus

        // Its menu: the board's one menu, as for every card (GraphMenu)
        internal List<GraphOption> MenuOptions() => GraphMenu.ForItem(this);

        // A row moved earlier or later among its card's (from Details)
        internal void Move(int id, int by)
        {
            var e = Doc.Find(id);
            if (e?.List != null) Move(e, by);
        }

        void Move(GraphElement e, int by)
        {
            var at = e.List.Items.IndexOf(e.Value);
            var to = at + by;
            if (at < 0 || to < 0 || to >= e.List.Items.Count) return;
            Edit(by < 0 ? "Move up" : "Move down", () =>
            {
                e.List.Items.RemoveAt(at);
                e.List.Items.Insert(to, e.Value);
            });
        }
    }
}
