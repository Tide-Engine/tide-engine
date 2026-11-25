using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // How the board looks: its sizes (tidec's layout's, compiler/src/graph_layout.c, so what it places doesn't
    // overlap), what's drawn in code (wires, pins, icons) in each of the editor's two themes, and the small pieces
    // every card, frame and popup is made of. The rest is GraphBoard.uss.
    static class GraphLook
    {
        public const string StylePath = "Packages/com.tide.unity/Editor/Graph/GraphBoard.uss";

        // The look's sizes of cards and frames (the schema's, as tidec's layout has them), and the gaps tidec leaves
        // between what it places on the board
        public static float CardWidth => GraphSchema.Size("card", 300);
        public static float CardRow => GraphSchema.Size("cardRow", 26);
        public static float CardHead => GraphSchema.Size("cardHead", 48);
        public static float FrameHead => GraphSchema.Size("frameHead", 48);
        public static float FramePad => GraphSchema.Size("framePad", 16);
        public static float FrameBottom => GraphSchema.Size("frameBottom", 40);
        public static float EntryGap => GraphSchema.Size("entryGap", 60);
        public static float EntryWidth => GraphSchema.Size("entryWidth", 320);
        public const float CardGap = 40, FrameGap = 80, LaneGap = 80;
        // The least a frame is, as the page draws one: room right of its entry to say what to do, and below it to drop
        // a first wire in (a value's frame, which has no entry, its own)
        public const float FrameRoomRight = 330, FrameLeast = 190, ValueFrameWidth = 300, ValueFrameHeight = 130;
        // Where an item draws all of itself, only its name, or nothing
        public const float FarZoom = 0.38f;

        static StyleSheet sheet;
        static Font mono;

        public static StyleSheet Sheet => sheet ? sheet : sheet = AssetDatabase.LoadAssetAtPath<StyleSheet>(StylePath);

        public static Font Mono => mono ? mono : mono = Font.CreateDynamicFontFromOSFont(new[] { "Consolas", "Menlo", "DejaVu Sans Mono", "Courier New" }, 12);

        // ---------------------------------------------------------------------------------------------------------
        // The look: the schema's (GraphSchema.Look), the same as every editor's. GraphBoard.uss lays things out and
        // takes its colours and sizes from these variables, made from the schema for dark or light.

        // The look's colours, in the theme the board is drawn in, for what's painted rather than styled
        public sealed class Palette
        {
            public bool Light;
            public Color Board, Grid, Text, Muted, Accent, Exec, Wire, Node, NodeBorder, Error, Warning, Shadow;
            public Color Get(string name) => GraphSchema.Look(name, Light);
        }

        static readonly Palette[] palettes = new Palette[2];
        static JValue paletteOf;

        public static Palette Of(bool light)
        {
            if (paletteOf != (GraphSchema.Available ? GraphSchema.Json : null))
            {
                palettes[0] = palettes[1] = null;
                paletteOf = GraphSchema.Available ? GraphSchema.Json : null;
            }
            var i = light ? 1 : 0;
            if (palettes[i] != null) return palettes[i];
            Color C(string name) => GraphSchema.Available ? GraphSchema.Look(name, light) : Color.magenta;
            return palettes[i] = new Palette
            {
                Light = light, Board = C("board"), Grid = C("grid"), Text = C("text"), Muted = C("muted"), Accent = C("accent"), Exec = C("exec"), Wire = C("wire"),
                Node = C("node"), NodeBorder = C("nodeBorder"), Error = C("error"), Warning = C("warning"), Shadow = C("shadow"),
            };
        }

        // A size of the look's, in pixels at zoom 1
        public static float Size(string name) => GraphSchema.Size(name);

        // `a` over `b` as CSS's color-mix(in srgb, a p, b) makes it: a share p of a, the rest of b
        public static Color Mix(Color a, float p, Color b)
        {
            var alpha = a.a * p + b.a * (1 - p);
            if (alpha <= 0) return Color.clear;
            Color Pre(Color c) => new Color(c.r * c.a, c.g * c.a, c.b * c.a, c.a);
            var pa = Pre(a);
            var pb = Pre(b);
            return new Color((pa.r * p + pb.r * (1 - p)) / alpha, (pa.g * p + pb.g * (1 - p)) / alpha, (pa.b * p + pb.b * (1 - p)) / alpha, alpha);
        }

        // A colour with a share of its alpha: color-mix(in srgb, c p, transparent)
        public static Color Fade(Color c, float p) => new Color(c.r, c.g, c.b, c.a * p);

        // The variables the board's sheet is drawn with, from the schema: each colour of the look as --tg-<name>
        // (kebab case, as the page's), the entry's title as --tg-head-entry, each size as --tg-<name> in pixels (the
        // font's as --tg-font-size, the grid's as --tg-grid-size), and the mixes of them the page's sheet makes with
        // color-mix, which USS hasn't
        static readonly StyleSheet[] lookSheets = new StyleSheet[2];
        static JValue lookSheetsOf;

        public static StyleSheet LookSheet(bool light)
        {
            if (!GraphSchema.Available) return null;
            if (lookSheetsOf != GraphSchema.Json)
            {
                lookSheets[0] = lookSheets[1] = null;
                lookSheetsOf = GraphSchema.Json;
            }
            var i = light ? 1 : 0;
            if (lookSheets[i] != null) return lookSheets[i];
            var made = ScriptableObject.CreateInstance<StyleSheet>();
            made.hideFlags = HideFlags.HideAndDontSave;
            if (Import(made, LookText(light))) return lookSheets[i] = made;
            UnityEngine.Object.DestroyImmediate(made);
            // Without a way to make one, the dark look as the package has it: the window is usable, if not in light
            return lookSheets[i] = AssetDatabase.LoadAssetAtPath<StyleSheet>(DarkLookPath);
        }

        // The dark look's variables as a file of the package's, for a Unity that can't make a sheet from text: a
        // test keeps it the same as LookText(false) makes from the schema
        public const string DarkLookPath = "Packages/com.tide.unity/Editor/Graph/GraphLookDark.uss";

        // Whether the sheets are made from the schema here, or the package's dark one is used instead
        public static bool LookMade => importable;

        // The look's variables as USS text, for dark or light
        public static string LookText(bool light)
        {
            var sb = new StringBuilder(".tg-look {\n");
            void Var(string name, Color c) => sb.Append("    --tg-").Append(name).Append(": ").Append(Css(c)).Append(";\n");
            foreach (var name in GraphSchema.LookColors) Var(Kebab(name), GraphSchema.Look(name, light));
            Var("head-entry", GraphSchema.Parse(GraphSchema.HeaderHex("entry", light)) ?? Color.clear);
            foreach (var name in GraphSchema.Sizes)
            {
                if (name == "lineHeight" || name == "titleWeight") continue;
                var px = GraphSchema.Size(name).ToString(System.Globalization.CultureInfo.InvariantCulture) + "px";
                sb.Append("    --tg-").Append(name == "font" ? "font-size" : name == "grid" ? "grid-size" : Kebab(name)).Append(": ").Append(px).Append(";\n");
            }
            // The page's sizes of text in em, of the look's font size (USS has no em): --tg-font-<percent>
            var font = GraphSchema.Size("font", 13);
            foreach (var percent in new[] { 80, 82, 85, 86, 88, 90, 92, 93, 95, 105, 110, 115, 135 })
                sb.Append("    --tg-font-").Append(percent).Append(": ").Append((font * percent / 100f).ToString("0.##", System.Globalization.CultureInfo.InvariantCulture)).Append("px;\n");
            // What's in a port's row, in em of the port's 0.92em: --tg-font-port-<percent>
            foreach (var percent in new[] { 85, 88, 92, 95, 105 })
                sb.Append("    --tg-font-port-").Append(percent).Append(": ").Append((font * 0.92f * percent / 100f).ToString("0.##", System.Globalization.CultureInfo.InvariantCulture)).Append("px;\n");
            // A Get's Target, 0.86em of a port's 0.92em
            sb.Append("    --tg-font-get-target: ").Append((font * 0.92f * 0.86f).ToString("0.##", System.Globalization.CultureInfo.InvariantCulture)).Append("px;\n");
            // A line of the page's text: its font size by the look's line height
            sb.Append("    --tg-line: ").Append((font * GraphSchema.Size("lineHeight", 1.35f)).ToString("0.##", System.Globalization.CultureInfo.InvariantCulture)).Append("px;\n");
            Color L(string name) => GraphSchema.Look(name, light);
            // The page's color-mix()es
            Var("text-5", Fade(L("text"), 0.05f));
            Var("text-7", Fade(L("text"), 0.07f));
            Var("accent-14", Fade(L("accent"), 0.14f));
            Var("accent-15", Fade(L("accent"), 0.15f));
            Var("accent-18", Fade(L("accent"), 0.18f));
            Var("accent-20", Fade(L("accent"), 0.20f));
            Var("accent-22", Fade(L("accent"), 0.22f));
            Var("accent-70", Fade(L("accent"), 0.70f));
            Var("error-14", Fade(L("error"), 0.14f));
            Var("error-18", Mix(L("error"), 0.18f, L("panel")));
            Var("muted-70", Fade(L("muted"), 0.70f));
            Var("more-8", Fade(L("more"), 0.08f));
            Var("more-60", Fade(L("more"), 0.60f));
            Var("data-16", Fade(L("data"), 0.16f));
            Var("behaviour-16", Fade(L("behaviour"), 0.16f));
            Var("more-16", Fade(L("more"), 0.16f));
            Var("node-border-55", Fade(L("nodeBorder"), 0.55f));
            Var("entry-border", Mix(L("behaviour"), 0.55f, L("nodeBorder")));
            sb.Append("}\n");
            return sb.ToString();
        }

        // A USS sheet made from text, as the UI Builder makes one: UI Toolkit has no public way to, and USS variables
        // can't be set on an element. Where this Unity hasn't that importer, one error says so, once.
        static bool importable = true;

        static bool Import(StyleSheet into, string text)
        {
            if (!importable) return false;
            try
            {
                var type = typeof(EditorWindow).Assembly.GetType("UnityEditor.UIElements.StyleSheets.StyleSheetImporterImpl");
                var importer = type?.GetConstructor(Type.EmptyTypes)?.Invoke(null);
                var import = type?.GetMethod("Import", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic, null, new[] { typeof(StyleSheet), typeof(string) }, null);
                if (importer != null && import != null)
                {
                    import.Invoke(importer, new object[] { into, text });
                    return true;
                }
            }
            catch (Exception)
            {
                // Said below
            }
            importable = false;
            Debug.LogError("Tide graph: this Unity can't make a sheet from text (UnityEditor.UIElements.StyleSheets.StyleSheetImporterImpl.Import is missing or failed), so the graph window is drawn in the package's dark look whatever the theme, and the schema's own look isn't followed. Tide's Unity package needs updating for this Unity.");
            return false;
        }

        static string Css(Color c) => c.a >= 1
            ? "#" + ColorUtility.ToHtmlStringRGB(c).ToLowerInvariant()
            : $"rgba({Mathf.RoundToInt(c.r * 255)}, {Mathf.RoundToInt(c.g * 255)}, {Mathf.RoundToInt(c.b * 255)}, {c.a.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture)})";

        static string Kebab(string name)
        {
            var sb = new StringBuilder();
            foreach (var c in name)
            {
                if (char.IsUpper(c)) sb.Append('-').Append(char.ToLowerInvariant(c));
                else sb.Append(c);
            }
            return sb.ToString();
        }

        // The look's font (Inter, which ships with the package, under the SIL Open Font License): its regular face,
        // with its bold face as the weight bold text takes, so bold text is drawn and measured in Inter Bold
        public const string FontFolder = "Packages/com.tide.unity/Editor/Graph/Fonts/";
        static UnityEngine.TextCore.Text.FontAsset font;

        public static UnityEngine.TextCore.Text.FontAsset Face
        {
            get
            {
                if (font != null) return font;
                UnityEngine.TextCore.Text.FontAsset Asset(string weight)
                {
                    var file = GraphSchema.Available ? GraphSchema.FontFile(weight) : null;
                    var ttf = file != null ? AssetDatabase.LoadAssetAtPath<Font>(FontFolder + file) : null;
                    if (ttf == null) return null;
                    var a = UnityEngine.TextCore.Text.FontAsset.CreateFontAsset(ttf);
                    if (a != null) a.hideFlags = HideFlags.HideAndDontSave;
                    return a;
                }
                var regular = Asset("regular");
                var bold = Asset("bold");
                if (regular == null) return null;
                // What Inter hasn't (the menu's ⋯, a ▾), from the system's font of symbols, as a browser takes it
                foreach (var family in new[] { "Segoe UI Symbol", "Apple Symbols", "DejaVu Sans" })
                {
                    var symbols = UnityEngine.TextCore.Text.FontAsset.CreateFontAsset(family, "Regular");
                    if (symbols == null) continue;
                    symbols.hideFlags = HideFlags.HideAndDontSave;
                    regular.fallbackFontAssetTable = new List<UnityEngine.TextCore.Text.FontAsset> { symbols };
                    if (bold != null) bold.fallbackFontAssetTable = new List<UnityEngine.TextCore.Text.FontAsset> { symbols };
                    break;
                }
                if (bold != null)
                {
                    var table = regular.fontWeightTable;
                    table[7].regularTypeface = bold;
                    table[6].regularTypeface = bold;
                }
                return font = regular;
            }
        }

        // How wide a digit is in the font at a size: CSS's ch, which the page sizes its number fields by
        static float chPerSize;

        public static float Ch(float fontSize)
        {
            if (chPerSize <= 0)
            {
                var file = GraphSchema.Available ? GraphSchema.FontFile("regular") : null;
                var ttf = file != null ? AssetDatabase.LoadAssetAtPath<Font>(FontFolder + file) : null;
                const int size = 100;
                if (ttf != null && UnityEngine.TextCore.LowLevel.FontEngine.LoadFontFace(ttf, size) == UnityEngine.TextCore.LowLevel.FontEngineError.Success
                    && UnityEngine.TextCore.LowLevel.FontEngine.TryGetGlyphWithUnicodeValue('0', UnityEngine.TextCore.LowLevel.GlyphLoadFlags.LOAD_NO_HINTING, out var zero))
                    chPerSize = zero.metrics.horizontalAdvance / size;
                else chPerSize = 0.6f;
            }
            return chPerSize * fontSize;
        }

        // The look on an element: the board's sheet, its variables for dark or light, and its font
        public static void Apply(VisualElement e, bool light)
        {
            e.AddToClassList("tg-look");
            var look = LookSheet(light);
            if (look != null) e.styleSheets.Add(look);
            if (Face != null) e.style.unityFontDefinition = FontDefinition.FromSDFFont(Face);
            e.style.fontSize = GraphSchema.Available ? GraphSchema.Size("font", 13) : 13;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Pieces

        public static T Class<T>(this T e, params string[] names) where T : VisualElement
        {
            foreach (var n in names) if (!string.IsNullOrEmpty(n)) e.AddToClassList(n);
            return e;
        }

        public static VisualElement Row(params string[] classes)
        {
            var row = new VisualElement();
            foreach (var c in classes) row.AddToClassList(c);
            return row;
        }

        public static Label Text(string text, params string[] classes)
        {
            var l = new Label(text ?? "");
            foreach (var c in classes) l.AddToClassList(c);
            return l;
        }

        // A button that looks as its class says, not as Unity's buttons do
        public static Button Plain(Action click, params string[] classes)
        {
            var b = new Button(click);
            b.RemoveFromClassList(Button.ussClassName);
            foreach (var c in classes) b.AddToClassList(c);
            return b;
        }

        public static Button IconButton(string shape, string tooltip, Action click, params string[] classes)
        {
            var b = Plain(click, "tg-icon-button");
            foreach (var c in classes) b.AddToClassList(c);
            b.tooltip = tooltip;
            b.Add(new GraphIcon(shape) { pickingMode = PickingMode.Ignore });
            return b;
        }

        // A button of an icon and words
        public static Button Labelled(string shape, string text, Action click, params string[] classes)
        {
            var b = Plain(click, classes);
            if (shape != null) b.Add(new GraphIcon(shape) { pickingMode = PickingMode.Ignore });
            b.Add(new Label(text) { pickingMode = PickingMode.Ignore, style = { paddingLeft = 0, paddingRight = 0, marginLeft = 0, marginRight = 0 } });
            return b;
        }

        // A kind in plain words, in its group's colour
        public static Label Kind(string title, string group)
        {
            // In capitals, as the page draws it (USS has no text-transform); its words as they were given kept with it
            var l = new Label((title ?? "").ToUpperInvariant()) { pickingMode = PickingMode.Ignore, userData = title };
            l.AddToClassList("tg-kind");
            if (!string.IsNullOrEmpty(group)) l.AddToClassList("tg-kind--" + group.ToLowerInvariant());
            return l;
        }

        // Text edited where it's drawn: plain until the pointer or the keyboard is on it. `commit` gets what was
        // typed when it's done (Enter, or leaving it) and differs.
        public static TextField Inline(string value, Action<string> commit, string placeholder = null, params string[] classes)
        {
            var f = new TextField { value = value ?? "", isDelayed = true };
            f.AddToClassList("tg-inline");
            foreach (var c in classes) f.AddToClassList(c);
            if (placeholder != null) f.textEdition.placeholder = placeholder;
            f.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                if (e.newValue != e.previousValue) commit(e.newValue);
            });
            return f;
        }

        // A name that's renamed by a double click, F2 or Rename in its menu (GraphName): a click on it selects and
        // drags what it's the name of. `commit` gets what was typed, made a name Tide takes; nothing typed leaves it.
        public static GraphName Name(string value, Action<string> commit, string placeholder = null, params string[] classes)
            => new GraphName(value, commit, placeholder, Identifier, classes);

        // The same, for a name with dots in it (a namespace's)
        public static GraphName DottedNameField(string value, Action<string> commit, string placeholder = null, params string[] classes)
            => new GraphName(value, commit, placeholder, DottedName, classes);

        // What's typed as a name of Tide's: "my speed" is mySpeed, "2nd" is _2nd
        public static string Identifier(string text)
        {
            var sb = new StringBuilder();
            var upper = false;
            foreach (var c in (text ?? "").Trim())
            {
                if (char.IsLetterOrDigit(c) && c < 128 || c == '_')
                {
                    sb.Append(upper && sb.Length > 0 ? char.ToUpperInvariant(c) : c);
                    upper = false;
                }
                else upper = true;
            }
            if (sb.Length > 0 && char.IsDigit(sb[0])) sb.Insert(0, '_');
            return sb.ToString();
        }

        // A name with dots in it: a namespace's, an override's
        public static string DottedName(string text)
        {
            var parts = new List<string>();
            foreach (var part in (text ?? "").Split('.'))
            {
                var name = Identifier(part);
                if (name.Length > 0) parts.Add(name);
            }
            return string.Join(".", parts);
        }

        public static string LowerFirst(string s) => string.IsNullOrEmpty(s) ? s : char.ToLowerInvariant(s[0]) + s.Substring(1);
        public static string UpperFirst(string s) => string.IsNullOrEmpty(s) ? s : char.ToUpperInvariant(s[0]) + s.Substring(1);

        // A note as a card's or a frame's title shows it: its start, on a line or two
        public static string Brief(string s, int most = 96)
        {
            if (string.IsNullOrEmpty(s)) return "";
            s = string.Join(" ", s.Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0));
            return s.Length > most ? s.Substring(0, most - 1).TrimEnd() + "…" : s;
        }

        // The first line of a message, and no more than fits beside a node
        public static string FirstLine(string s, int most = 110)
        {
            if (string.IsNullOrEmpty(s)) return "";
            var i = s.IndexOf('\n');
            if (i >= 0) s = s.Substring(0, i);
            return s.Length > most ? s.Substring(0, most - 1) + "…" : s;
        }

        // The place nearest to `near` where a box of `size` lies on none of `taken` (with a gap around each):
        // what the editor places goes into free space, never on top of what's there
        public static Vector2 FreeSpot(Vector2 near, Vector2 size, IList<Rect> taken, float gap = 16, float step = 24)
        {
            bool Free(Vector2 at)
            {
                var r = new Rect(at.x - gap, at.y - gap, size.x + 2 * gap, size.y + 2 * gap);
                for (var i = 0; i < taken.Count; i++) if (taken[i].Overlaps(r)) return false;
                return true;
            }
            if (Free(near)) return near;
            // Rings around it, nearest first: below and to the right before above and to the left
            for (var ring = 1; ring < 400; ring++)
            {
                Vector2? best = null;
                var bestD = float.MaxValue;
                for (var i = -ring; i <= ring; i++)
                {
                    foreach (var (dx, dy) in new[] { (i, ring), (ring, i), (i, -ring), (-ring, i) })
                    {
                        var at = near + new Vector2(dx, dy) * step;
                        // Below or right first, at a tie
                        var d = new Vector2(dx, dy).sqrMagnitude + (dy < 0 ? 0.5f : 0) + (dx < 0 ? 0.25f : 0);
                        if (d >= bestD || !Free(at)) continue;
                        bestD = d;
                        best = at;
                    }
                }
                if (best != null) return best.Value;
            }
            return near;
        }

        // The element under a pointer event's target that is of a type, or null
        public static T Up<T>(VisualElement e) where T : VisualElement
        {
            for (; e != null; e = e.parent)
                if (e is T t) return t;
            return null;
        }

        // Whether a click is on something that takes it itself: a field, a button, a pin
        public static bool TakesClicks(VisualElement e, VisualElement within)
        {
            for (; e != null && e != within; e = e.parent)
                if (e is TextField || e is Toggle || e is Button || e is BasePopupField<string, string> || e is UnityEditor.UIElements.ColorField || e is PortView) return true;
            return false;
        }
    }

    // A name drawn as its text (notes/visual-scripting.md, "Round three"): a click on it is a click on what it names,
    // which selects it and starts a drag; a double click, F2 or Rename in its menu makes it a field to type in. Enter
    // or a click elsewhere keeps what's typed, Escape takes it back. Something just made starts with it open.
    sealed class GraphName : VisualElement
    {
        readonly Label label;
        public readonly TextField Field;
        readonly Func<string, string> clean;
        readonly Action<string> commit;
        readonly string placeholder;
        string value;
        double lastDown = -1;

        public GraphName(string value, Action<string> commit, string placeholder, Func<string, string> clean, string[] classes)
        {
            this.value = value ?? "";
            this.commit = commit;
            this.clean = clean;
            this.placeholder = placeholder;
            AddToClassList("tg-name");
            foreach (var c in classes) if (!string.IsNullOrEmpty(c)) AddToClassList(c);
            label = new Label { pickingMode = PickingMode.Position };
            label.AddToClassList("tg-name-text");
            Add(label);
            Field = new TextField { value = this.value, isDelayed = true };
            Field.AddToClassList("tg-inline");
            foreach (var c in classes) if (!string.IsNullOrEmpty(c)) Field.AddToClassList(c);
            if (placeholder != null) Field.textEdition.placeholder = placeholder;
            Field.style.display = DisplayStyle.None;
            Field.RegisterValueChangedCallback(e =>
            {
                e.StopPropagation();
                Done(e.newValue);
            });
            Field.RegisterCallback<FocusOutEvent>(_ => Show(false));
            Field.RegisterCallback<KeyDownEvent>(e =>
            {
                if (e.keyCode != KeyCode.Escape) return;
                Field.SetValueWithoutNotify(this.value);
                Show(false);
                e.StopPropagation();
            });
            Add(Field);
            // A double click: the system's count of clicks, or two presses close together on it
            label.RegisterCallback<PointerDownEvent>(e =>
            {
                if (e.button != 0) return;
                var now = EditorApplication.timeSinceStartup;
                var twice = e.clickCount >= 2 || now - lastDown < 0.45;
                lastDown = twice ? -1 : now;
                if (!twice) return;
                // Not taken by what it names (a drag), and the keyboard stays with the field it opens
                label.focusController?.IgnoreEvent(e);
                e.StopImmediatePropagation();
                Edit();
            });
            tooltip = "Double-click to rename (F2)";
            Show(false);
        }

        public string Value => value;
        public bool AllowEmpty; // Nothing typed takes the name away (an `is`'s name, an event's)
        public bool Editing => Field.style.display == DisplayStyle.Flex;

        // What's typed in it now, kept or not
        public string text => Field.text;

        // Its name and its field's, so either is found by it
        public void Named(string n)
        {
            name = n;
            Field.name = n;
        }

        // Opens it to type a new name, with the old one selected
        public void Edit()
        {
            Field.SetValueWithoutNotify(value);
            Show(true);
            Field.Focus();
            Field.schedule.Execute(() =>
            {
                Field.Focus();
                Field.SelectAll();
            });
        }

        void Show(bool editing)
        {
            Field.style.display = editing ? DisplayStyle.Flex : DisplayStyle.None;
            label.style.display = editing ? DisplayStyle.None : DisplayStyle.Flex;
            label.text = value.Length > 0 ? value : placeholder ?? "";
            EnableInClassList("tg-name--empty", value.Length == 0);
        }

        // A name typed elsewhere (Details), kept as if typed here
        public void Commit(string typed) => Done(typed);

        void Done(string typed)
        {
            var name = clean(typed ?? "");
            Show(false);
            if ((name.Length == 0 && !AllowEmpty) || name == value)
            {
                Field.SetValueWithoutNotify(value);
                return;
            }
            value = name;
            Show(false);
            commit(name);
        }
    }

    // A small icon, painted in its text colour: more (three dots), plus, close, down, right, play, search, check,
    // warn, note, code, text
    sealed class GraphIcon : VisualElement
    {
        readonly string shape;

        public GraphIcon(string shape)
        {
            this.shape = shape;
            AddToClassList("tg-icon");
            generateVisualContent += Draw;
        }

        void Draw(MeshGenerationContext ctx)
        {
            var p = ctx.painter2D;
            var r = contentRect;
            if (r.width <= 0 || r.height <= 0) return;
            var c = r.center;
            var s = Mathf.Min(r.width, r.height) / 14f;
            var color = resolvedStyle.color;
            p.strokeColor = color;
            p.fillColor = color;
            p.lineWidth = 1.6f * s;
            p.lineCap = LineCap.Round;
            p.lineJoin = LineJoin.Round;
            Vector2 P(float x, float y) => c + new Vector2(x, y) * s;
            void Line(params Vector2[] points)
            {
                p.BeginPath();
                p.MoveTo(points[0]);
                for (var i = 1; i < points.Length; i++) p.LineTo(points[i]);
                p.Stroke();
            }
            void Dot(Vector2 at, float radius)
            {
                p.BeginPath();
                p.Arc(at, radius * s, 0, 360);
                p.Fill();
            }
            switch (shape)
            {
                case "more":
                    Dot(P(-4.5f, 0), 1.4f);
                    Dot(P(0, 0), 1.4f);
                    Dot(P(4.5f, 0), 1.4f);
                    break;
                case "plus":
                    Line(P(-4.5f, 0), P(4.5f, 0));
                    Line(P(0, -4.5f), P(0, 4.5f));
                    break;
                case "close":
                    Line(P(-3.5f, -3.5f), P(3.5f, 3.5f));
                    Line(P(-3.5f, 3.5f), P(3.5f, -3.5f));
                    break;
                case "down":
                    Line(P(-3.5f, -1.5f), P(0, 2f), P(3.5f, -1.5f));
                    break;
                case "right":
                    Line(P(-1.5f, -3.5f), P(2f, 0), P(-1.5f, 3.5f));
                    break;
                case "play":
                    p.BeginPath();
                    p.MoveTo(P(-3.5f, -4.5f));
                    p.LineTo(P(4.5f, 0));
                    p.LineTo(P(-3.5f, 4.5f));
                    p.ClosePath();
                    p.Fill();
                    break;
                case "search":
                    p.BeginPath();
                    p.Arc(P(-1, -1), 3.6f * s, 0, 360);
                    p.Stroke();
                    Line(P(1.8f, 1.8f), P(5, 5));
                    break;
                case "check":
                    Line(P(-4, 0.5f), P(-1, 3.5f), P(4.5f, -3.5f));
                    break;
                case "warn":
                    Line(P(0, -5), P(5.5f, 4.5f), P(-5.5f, 4.5f), P(0, -5));
                    Line(P(0, -1.2f), P(0, 1.4f));
                    Dot(P(0, 3.1f), 0.5f);
                    break;
                case "note":
                    Line(P(-4, -4.5f), P(4, -4.5f), P(4, 2), P(1, 4.5f), P(-4, 4.5f), P(-4, -4.5f));
                    Line(P(-1.8f, -1.5f), P(1.8f, -1.5f));
                    Line(P(-1.8f, 1), P(0.5f, 1));
                    break;
                case "code":
                    Line(P(-2, -3.5f), P(-5.5f, 0), P(-2, 3.5f));
                    Line(P(2, -3.5f), P(5.5f, 0), P(2, 3.5f));
                    break;
                case "list":
                    Line(P(-2, -3.5f), P(5, -3.5f));
                    Line(P(-2, 0), P(5, 0));
                    Line(P(-2, 3.5f), P(5, 3.5f));
                    Dot(P(-4.8f, -3.5f), 0.8f);
                    Dot(P(-4.8f, 0), 0.8f);
                    Dot(P(-4.8f, 3.5f), 0.8f);
                    break;
            }
        }
    }

    // The colours of values' types: the schema's (GraphSchema.TypeHex), the same in dark and light; the look's muted
    // for a type that isn't known
    static class TideGraphTypes
    {
        public static Color ColorOf(string type, bool light = false) => GraphSchema.Available ? GraphSchema.TypeColor(type) ?? GraphSchema.Look("muted", light) : Color.gray;
    }

    // A type, as it's shown wherever one goes: its colour's dot and its name, and a click (or Enter) opens the
    // picker. Never typed.
    sealed class GraphTypeChip : Button
    {
        public string Type { get; private set; }

        public GraphTypeChip(string type, string unset, bool light, Action<GraphTypeChip> pick)
        {
            RemoveFromClassList(ussClassName);
            AddToClassList("tg-type");
            Type = type;
            var dot = new VisualElement { pickingMode = PickingMode.Ignore };
            dot.AddToClassList("tg-type-dot");
            dot.style.backgroundColor = TideGraphTypes.ColorOf(type, light);
            Add(dot);
            Add(new Label(string.IsNullOrEmpty(type) ? unset ?? "" : type) { pickingMode = PickingMode.Ignore });
            if (string.IsNullOrEmpty(type)) AddToClassList("tg-type--unset");
            tooltip = string.IsNullOrEmpty(type) ? unset : type;
            clicked += () => pick(this);
        }
    }
}
