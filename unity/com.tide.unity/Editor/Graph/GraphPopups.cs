using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // What opens over the board and closes when something's picked, when Escape is pressed or when a click lands
    // outside it: the window places it by what it was opened from, and keeps it inside the window
    class GraphPopup : VisualElement
    {
        public TideGraphWindow Window;
        public Action Closed;
        internal VisualElement From; // What it was opened from, when that's an element
        internal Rect Anchor;     // Where that is, in the window's space
        internal bool Beside;     // To its right, not below it
        internal bool WasClosed;  // Closed: what's still being asked for it isn't shown

        public GraphPopup()
        {
            AddToClassList("tg-popup");
            RegisterCallback<PointerDownEvent>(e => e.StopPropagation());
            RegisterCallback<WheelEvent>(e => e.StopPropagation());
            RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode != KeyCode.Escape) return;
                Close();
                e.StopPropagation();
            });
        }

        // Shown: the keyboard goes to it
        public virtual void Opened()
        {
        }

        public void Close() => Window?.ClosePopup(this);
    }

    // One thing a list offers: what it's called, what it is in a few words, the heading it's under, and what picking
    // it does
    sealed class GraphOption
    {
        public string Title, Detail, Section, SectionSummary, Keywords = "", Help;
        public bool Tall;          // Its detail on a line of its own under it
        public bool Off;           // Listed, not pickable
        public bool Separator;
        public bool? Checked;      // A tick box's state, for an option that's on or off
        public int Priority;       // Higher ranks first among as close a match
        public Color? Dot;         // A type's colour
        public bool KeepOpen;      // Picking it changes the list, and doesn't close it
        public Action Pick;
        public object Data;
    }

    // A list to pick from, searched as it's typed in: every menu of the board is one (what to add, a type, a node,
    // a card's options), so each is found the same way. Typing narrows it to what has every word typed and ranks
    // the closest title first; arrows move, Enter takes the first match.
    class GraphOptionList : GraphPopup
    {
        readonly TextField query;
        readonly ScrollView list;
        readonly Label head, help, status;
        readonly List<GraphOption> all = new List<GraphOption>();
        readonly List<GraphOption> shown = new List<GraphOption>();
        readonly List<VisualElement> rows = new List<VisualElement>();
        int current;

        public GraphOptionList(string title, string helpText, string placeholder, float width = 280, float height = 440)
        {
            style.width = width;
            style.maxHeight = height;
            focusable = true;
            head = GraphLook.Text(title, "tg-popup-head");
            head.style.display = string.IsNullOrEmpty(title) ? DisplayStyle.None : DisplayStyle.Flex;
            Add(head);
            help = GraphLook.Text(helpText, "tg-popup-help");
            help.style.display = string.IsNullOrEmpty(helpText) ? DisplayStyle.None : DisplayStyle.Flex;
            Add(help);
            if (placeholder != null)
            {
                query = new TextField();
                query.AddToClassList("tg-search");
                query.textEdition.placeholder = placeholder;
                query.RegisterValueChangedCallback(e =>
                {
                    e.StopPropagation();
                    Typed((e.newValue ?? "").Trim());
                    Filter();
                });
                query.RegisterCallback<KeyDownEvent>(OnKey, TrickleDown.TrickleDown);
                Add(query);
            }
            else RegisterCallback<KeyDownEvent>(OnKey);
            status = GraphLook.Text("", "tg-popup-help");
            status.style.display = DisplayStyle.None;
            Add(status);
            list = new ScrollView { style = { flexShrink = 1 } };
            Add(list);
        }

        public string Title
        {
            get => head.text;
            set
            {
                head.text = value ?? "";
                head.style.display = string.IsNullOrEmpty(value) ? DisplayStyle.None : DisplayStyle.Flex;
            }
        }

        public string HelpText
        {
            set
            {
                help.text = value ?? "";
                help.style.display = string.IsNullOrEmpty(value) ? DisplayStyle.None : DisplayStyle.Flex;
            }
        }

        public string Placeholder
        {
            set
            {
                if (query != null) query.textEdition.placeholder = value ?? "";
            }
        }

        // A line under the search: what's still being asked, or why something isn't offered
        public string Status
        {
            get => status.text;
            set
            {
                status.text = value ?? "";
                status.style.display = string.IsNullOrEmpty(value) ? DisplayStyle.None : DisplayStyle.Flex;
            }
        }

        public string Query => query?.value ?? "";
        public IReadOnlyList<GraphOption> Shown => shown;
        public IReadOnlyList<GraphOption> All => all;

        public void Set(IEnumerable<GraphOption> options, bool keepQuery = false)
        {
            all.Clear();
            all.AddRange(options);
            if (!keepQuery) query?.SetValueWithoutNotify("");
            Filter();
        }

        // Adds options that came later (the language server's), the first of them above those there
        public void Merge(IEnumerable<GraphOption> first, IEnumerable<GraphOption> later)
        {
            if (first != null) all.InsertRange(0, first);
            if (later != null) all.AddRange(later);
            Filter(true);
        }

        public override void Opened()
        {
            // Now, and again once it's laid out: what's typed next is its
            if (query != null)
            {
                query.Focus();
                query.schedule.Execute(() => query.Focus());
            }
            else
            {
                Focus();
                schedule.Execute(Focus);
            }
        }

        // Types into the search, as the keyboard would
        public void Type(string text)
        {
            if (query == null) return;
            query.SetValueWithoutNotify(text);
            Typed((text ?? "").Trim());
            Filter();
        }

        // What's typed, before the list is narrowed by it: a list whose options depend on it makes them here
        protected virtual void Typed(string text)
        {
        }

        protected virtual bool Suits(GraphOption option) => true;

        // How well an option matches what's typed: 0 its title, 1 its title's start, 2 a word of its title's
        // start, 3 its title has it, 4 only its heading or keywords
        static int Score(GraphOption o, string typed)
        {
            var title = o.Title.ToLowerInvariant();
            var bare = Bare(title);
            if (title == typed || title.TrimStart('.') == typed || bare == typed) return 0;
            if (title.StartsWith(typed) || title.TrimStart('.').StartsWith(typed)) return 1;
            foreach (var word in title.Split(' ', '.', '(', ',', '<'))
                if (word.StartsWith(typed)) return 2;
            return title.Contains(typed) ? 3 : 4;
        }

        static string Bare(string title)
        {
            var paren = title.IndexOf('(');
            return paren > 0 ? title.Substring(0, paren) : title.Replace(" { }", "").TrimEnd('…');
        }

        void Filter(bool keep = false)
        {
            var typed = Query.ToLowerInvariant().Trim();
            var words = typed.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            var was = keep && current > 0 && current < shown.Count ? shown[current] : null;
            shown.Clear();
            var order = new Dictionary<GraphOption, int>();
            foreach (var o in all)
            {
                if (o.Separator)
                {
                    if (words.Length == 0) shown.Add(o);
                    continue;
                }
                if (!Suits(o)) continue;
                if (words.Length > 0)
                {
                    var text = (o.Title + " " + o.Section + " " + o.Keywords + " " + o.Detail).ToLowerInvariant();
                    if (!words.All(text.Contains)) continue;
                }
                order[o] = shown.Count;
                shown.Add(o);
            }
            var ranked = words.Length > 0;
            if (!ranked && !shown.Any(o => o.Separator))
            {
                // Under its heading, the headings in the order they first come
                var sections = new List<string>();
                foreach (var o in shown) if (!sections.Contains(o.Section ?? "")) sections.Add(o.Section ?? "");
                var grouped = shown.OrderBy(o => sections.IndexOf(o.Section ?? "")).ToList();
                shown.Clear();
                shown.AddRange(grouped);
            }
            if (ranked)
            {
                var scores = shown.ToDictionary(o => o, o => Score(o, typed));
                shown.Sort((a, b) =>
                {
                    var c = scores[a].CompareTo(scores[b]);
                    if (c == 0) c = b.Priority.CompareTo(a.Priority);
                    return c != 0 ? c : order[a].CompareTo(order[b]);
                });
            }
            list.Clear();
            rows.Clear();
            string section = null;
            foreach (var o in shown)
            {
                if (o.Separator)
                {
                    rows.Add(null);
                    list.Add(GraphLook.Row("tg-separator"));
                    continue;
                }
                if (!ranked && o.Section != section)
                {
                    section = o.Section;
                    if (!string.IsNullOrEmpty(section))
                    {
                        var heading = GraphLook.Row("tg-section");
                        heading.Add(GraphLook.Text(section.ToUpperInvariant(), "tg-section-title"));
                        if (!string.IsNullOrEmpty(o.SectionSummary)) heading.Add(GraphLook.Text(o.SectionSummary, "tg-section-summary"));
                        list.Add(heading);
                    }
                }
                var option = o;
                var row = GraphLook.Row("tg-option");
                if (o.Tall) row.AddToClassList("tg-option--tall");
                if (o.Off) row.AddToClassList("tg-option--off");
                row.tooltip = o.Help ?? "";
                var title = row;
                if (o.Tall && (o.Checked != null || o.Dot != null))
                {
                    title = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center } };
                    row.Add(title);
                }
                if (o.Checked != null)
                {
                    var tick = GraphLook.Row("tg-option-check");
                    if (o.Checked == true) tick.Add(new GraphIcon("check"));
                    title.Add(tick);
                }
                if (o.Dot != null)
                {
                    var dot = GraphLook.Row("tg-type-dot");
                    dot.style.backgroundColor = o.Dot.Value;
                    title.Add(dot);
                }
                title.Add(GraphLook.Text(o.Title, "tg-option-title"));
                var detail = ranked && !o.Tall && string.IsNullOrEmpty(o.Detail) ? o.Section : o.Detail;
                if (!string.IsNullOrEmpty(detail)) row.Add(GraphLook.Text(detail, "tg-option-detail"));
                row.RegisterCallback<PointerDownEvent>(e =>
                {
                    if (e.button != 0) return;
                    e.StopPropagation();
                    Pick(option);
                });
                // The pointer moved onto it: not a row that came under a pointer that's still, which would take
                // Enter away from the first match of what's being typed
                row.RegisterCallback<PointerMoveEvent>(_ =>
                {
                    var at = shown.IndexOf(option);
                    if (at < 0 || at == current || option.Off) return;
                    current = at;
                    Highlight(false);
                });
                rows.Add(row);
                list.Add(row);
            }
            current = was != null ? Math.Max(0, shown.IndexOf(was)) : 0;
            if (current < shown.Count && !Pickable(current)) current = Step(current, 1);
            Highlight(true);
        }

        bool Pickable(int i) => i >= 0 && i < shown.Count && !shown[i].Separator && !shown[i].Off;

        int Step(int from, int by)
        {
            for (var i = from + by; i >= 0 && i < shown.Count; i += by)
                if (Pickable(i)) return i;
            return Pickable(from) ? from : -1;
        }

        void Highlight(bool scroll)
        {
            for (var i = 0; i < rows.Count; i++) rows[i]?.EnableInClassList("tg-option--current", i == current);
            if (scroll && current >= 0 && current < rows.Count && rows[current] != null && list.panel != null) list.ScrollTo(rows[current]);
        }

        void OnKey(KeyDownEvent e)
        {
            switch (e.keyCode)
            {
                case KeyCode.DownArrow:
                    current = Step(current, 1);
                    Highlight(true);
                    break;
                case KeyCode.UpArrow:
                    current = Step(current, -1);
                    Highlight(true);
                    break;
                case KeyCode.Return:
                case KeyCode.KeypadEnter:
                    PickCurrent();
                    break;
                case KeyCode.Escape:
                    if (!Back()) Close();
                    break;
                default:
                    return;
            }
            e.StopPropagation();
#pragma warning disable CS0618 // Unity 6 has no other way to keep a text field from taking the key too
            e.PreventDefault();
#pragma warning restore CS0618
        }

        // Escape, in a list that has a step to go back to: true when it went back
        protected virtual bool Back() => false;

        public bool PickCurrent()
        {
            if (!Pickable(current)) return false;
            Pick(shown[current]);
            return true;
        }

        // Picks the option shown with a title, as a click on it would: false when it isn't offered
        public bool Pick(string title)
        {
            var o = shown.FirstOrDefault(x => !x.Separator && !x.Off && x.Title == title);
            if (o == null) return false;
            Pick(o);
            return true;
        }

        public void Pick(GraphOption o)
        {
            if (o == null || o.Off || o.Separator) return;
            if (!o.KeepOpen) Close();
            o.Pick?.Invoke();
        }
    }

    // What a type is picked for: the element whose key it is (the language server says what's valid at its place in
    // the file's Tide), what else can be picked there, and which types suit
    sealed class GraphTypeRequest
    {
        public string Title;            // "Type of frequency"
        public string Current;
        public GraphTypePlace Place;    // Where it goes, for the language server; null asks at a field's place
        public bool AllowVoid;          // A function's result can be nothing
        public string Infer;            // What picking no type is called, where a type can be left to be worked out
        public Func<GraphTypeInfo, bool> Only; // Which of what's valid suit: events only, for a handler
        public List<string> Generic = new List<string>(); // The type parameters in scope
        public bool NoBuilders;         // Only named types: a component a system takes
    }

    // Where a type goes, in the Tide the graph stands for: in an element's stretch of it, at the type as it's
    // written now (Find) or right after some text of it (After: a routine's "Name(", a handler's "event(")
    sealed class GraphTypePlace
    {
        public int Element;
        public string Find, After;
    }

    // A type the language server (or the schema) says can go at a place
    sealed class GraphTypeInfo
    {
        public string Name, Detail, Doc;
        public int Kind;                 // The language server's kind of it (a class is a thing there's one of)
        public List<string> Params = new List<string>(); // What a generic type is of: List<T>'s T

        // What declares it, when the game or the engine does: component, struct, enum, singleton...
        public string Declared
        {
            get
            {
                var first = (Detail ?? "").Split('\n')[0].Trim();
                if (first.StartsWith("local ")) first = first.Substring(6);
                var word = first.Split(' ', '<')[0];
                return word == "singleton" || GraphSchema.Known(word, false) ? word : null;
            }
        }

        public bool OneOfAKind => Kind == TideGraphLanguage.KindClass || Declared == "singleton";
        public bool BuiltIn => Declared == null && Kind != TideGraphLanguage.KindClass;
    }

    // Picks a type: a search that completes as it's typed. First the common types and the game's own, then types
    // made of others ("List of…", which asks what it holds with the same list), then the rest. What's listed is
    // what the language server says is valid at the type's place; until it answers (and without it), the schema's
    // types and the file's own. Nothing is typed in as a type: one is always picked.
    sealed class GraphTypePicker : GraphOptionList
    {
        readonly GraphTypeRequest request;
        readonly Action<string> picked;
        readonly List<GraphTypeInfo> valid = new List<GraphTypeInfo>();
        // A type being made of others: each builder with what's been picked for it so far
        readonly List<(GraphSchema.TypeBuilder builder, List<string> picked)> building = new List<(GraphSchema.TypeBuilder, List<string>)>();
        bool answered;

        public GraphTypePicker(TideGraphWindow window, GraphTypeRequest request, Action<string> picked) : base(request.Title, null, "Search types", 290)
        {
            Window = window;
            this.request = request;
            this.picked = picked;
            Fallback();
            Build();
            TideGraphLanguage.Types(window, request.Place, (types, why) =>
            {
                if (WasClosed) return;
                if (types != null && types.Count > 0)
                {
                    valid.Clear();
                    valid.AddRange(types);
                    answered = true;
                }
                Asked = true;
                Build(true);
            });
        }

        // Whether the language server has said what's valid (or that it can't)
        internal bool Asked { get; private set; }
        internal bool FromServer => answered;

        // Without the language server: the schema's types and the file's own
        void Fallback()
        {
            valid.Clear();
            foreach (var t in GraphSchema.BuiltinTypes)
                if (t.IndexOf('<') < 0 && t != "void") valid.Add(new GraphTypeInfo { Name = t, Detail = "built-in type" });
            var doc = Window.Doc;
            if (doc == null) return;
            foreach (var e in doc.Elements)
            {
                if (e.Role != GraphRole.Decl || string.IsNullOrEmpty(e.Value.Str("name"))) continue;
                var kind = e.Value.Str("kind");
                if (Array.IndexOf(TypeKinds, kind) >= 0) valid.Add(new GraphTypeInfo { Name = e.Value.Str("name"), Detail = kind });
            }
        }

        // The kinds of declaration that make a type: those with members, an enum and an alias
        static string[] TypeKinds => GraphSchema.DataKinds.Concat(new[] { "enum", "alias" }).ToArray();

        bool Mine(string name) => Window.Doc != null && Window.Doc.Elements.Any(e => e.Role == GraphRole.Decl && e.Value.Str("name") == name);

        void Build(bool keepQuery = false)
        {
            var slot = Slot;
            if (slot.number)
            {
                Title = slot.title;
                Placeholder = slot.what;
                Numbers(Query.Trim());
                return;
            }
            Title = slot.title ?? request.Title;
            Placeholder = "Search types";
            var options = new List<GraphOption>();
            var light = Window.Light;
            var top = building.Count == 0;
            GraphOption Option(GraphTypeInfo t, string section, int priority) => new GraphOption
            {
                Title = t.Params.Count > 0 ? t.Name + " of…" : t.Name, Section = section, Detail = t.BuiltIn ? null : t.Declared ?? GraphLook.FirstLine(t.Detail, 50), Help = t.Doc,
                Dot = TideGraphTypes.ColorOf(t.Name, light), Priority = priority, Keywords = t.Name + " " + (t.Detail ?? ""), Data = t, KeepOpen = true,
                Pick = () =>
                {
                    if (t.Params.Count == 0)
                    {
                        Take(t.Name);
                        return;
                    }
                    // A generic type of the game's: what it's of is picked next, as for a list
                    building.Add((new GraphSchema.TypeBuilder
                    {
                        Title = t.Name + " of…", Type = t.Name + "<" + string.Join(", ", t.Params.Select((_, i) => "{" + i + "}")) + ">", Slots = t.Params.Select(p => "its " + p).ToArray(),
                    }, new List<string>()));
                    Build();
                },
            };
            // A generic type the schema has a builder for is made with it ("List of…"), and listed there
            bool Built(GraphTypeInfo t) => t.Params.Count > 0 && GraphSchema.TypeBuilders.Any(b => b.Type.StartsWith(t.Name + "<"));
            var suits = valid.Where(t => (request.Only == null || request.Only(t)) && !Built(t) && !(request.NoBuilders && t.Params.Count > 0)).ToList();
            var taken = new HashSet<string>();
            if (top && request.Infer != null)
                options.Add(new GraphOption { Title = request.Infer, Section = Group("common"), Detail = "worked out from its value", Priority = 4, KeepOpen = true, Pick = () => Finish("") });
            foreach (var name in GraphSchema.TypeCommon)
            {
                var t = suits.FirstOrDefault(x => x.Name == name);
                if (t != null && taken.Add(name)) options.Add(Option(t, Group("common"), 3));
            }
            // Its own type parameters, this file's types, then the rest of the game's (and the engine's)
            foreach (var g in request.Generic)
                if (taken.Add(g)) options.Add(new GraphOption { Title = g, Section = Group("own"), Detail = "generic", Dot = TideGraphTypes.ColorOf(g, light), Priority = 3, KeepOpen = true, Pick = () => Take(g) });
            foreach (var t in suits.Where(t => !t.BuiltIn && Mine(t.Name)))
                if (taken.Add(t.Name)) options.Add(Option(t, Group("file"), 3));
            foreach (var t in suits.Where(t => !t.BuiltIn))
                if (taken.Add(t.Name)) options.Add(Option(t, Group("game"), 2));
            if (!request.NoBuilders)
                foreach (var b in GraphSchema.TypeBuilders)
                {
                    var builder = b;
                    options.Add(new GraphOption
                    {
                        Title = b.Title, Section = Group("builders"), Keywords = b.Type + " " + string.Join(" ", b.Slots), Priority = 1, KeepOpen = true,
                        Pick = () =>
                        {
                            building.Add((builder, new List<string>()));
                            Build();
                        },
                    });
                }
            // Generic types with no builder of their own: of what, is asked next
            foreach (var t in suits.Where(t => t.BuiltIn && t.Params.Count > 0))
                if (taken.Add(t.Name)) options.Add(Option(t, Group("builders"), 1));
            if (top && request.AllowVoid)
                options.Add(new GraphOption { Title = "void", Section = Group("rest"), Detail = "nothing", Dot = TideGraphTypes.ColorOf("void", light), KeepOpen = true, Pick = () => Finish("void") });
            foreach (var t in suits.Where(t => t.BuiltIn))
                if (taken.Add(t.Name)) options.Add(Option(t, Group("rest"), 0));
            Status = !Asked ? "Asking what fits here…" : suits.Count == 0 ? "Nothing that fits here is declared yet." : null;
            Set(options, keepQuery);
        }

        // A heading of the picker, in the schema's words (typePicker.groups)
        static string Group(string name) => GraphSchema.TypeGroup(name);

        // What's being asked for now: the type itself, or what a type made of others holds
        (string title, string what, bool number) Slot
        {
            get
            {
                if (building.Count == 0) return (null, null, false);
                var (builder, got) = building[building.Count - 1];
                var index = got.Count;
                var what = index < builder.Slots.Length ? builder.Slots[index] : "what it holds";
                // A slot the schema counts (how many a fixed array holds) is a number, not a type
                var number = Array.IndexOf(builder.Counts, index) >= 0;
                return (builder.Title.TrimEnd('…', '.').Trim() + ": " + what, what, number);
            }
        }

        void Numbers(string typed)
        {
            var options = new List<GraphOption>();
            var digits = typed.Length > 0 && typed.All(char.IsDigit) && typed.TrimStart('0').Length > 0;
            if (digits) options.Add(new GraphOption { Title = typed.TrimStart('0'), Keywords = typed, KeepOpen = true, Pick = () => Take(typed.TrimStart('0')) });
            else
                foreach (var n in new[] { "2", "3", "4", "8", "16", "32" })
                {
                    var number = n;
                    options.Add(new GraphOption { Title = n, KeepOpen = true, Pick = () => Take(number) });
                }
            Status = typed.Length > 0 && !digits ? "A whole number, 1 or more." : null;
            Set(options, true);
        }

        protected override void Typed(string text)
        {
            if (!Slot.number) return;
            Numbers(text);
        }

        protected override bool Suits(GraphOption option) => true;

        protected override bool Back()
        {
            if (building.Count == 0) return false;
            var (builder, got) = building[building.Count - 1];
            if (got.Count > 0) got.RemoveAt(got.Count - 1);
            else building.RemoveAt(building.Count - 1);
            Build();
            return true;
        }

        // A type picked: the type itself, or the next of what a type made of others holds
        void Take(string type)
        {
            while (building.Count > 0)
            {
                var (builder, got) = building[building.Count - 1];
                got.Add(type);
                if (got.Count < builder.Slots.Length)
                {
                    Build();
                    Opened();
                    return;
                }
                type = string.Format(builder.Type, got.Cast<object>().ToArray());
                building.RemoveAt(building.Count - 1);
            }
            Finish(type);
        }

        void Finish(string type)
        {
            Close();
            picked(type);
        }

        // Picks a type by its name, as the list does when it's clicked (the story's tests)
        internal bool Choose(string title) => Pick(title);
    }
}
