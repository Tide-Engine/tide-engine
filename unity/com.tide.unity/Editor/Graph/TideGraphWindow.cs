using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The document a window edits, as Unity's Undo keeps it: the file's JSON as it would be saved, recorded before
    // each edit.
    sealed class TideGraphState : ScriptableObject
    {
        public string json = "";
    }

    // The graph editor of a .tidegraph file: one board (notes/visual-scripting.md, "The experience: one board").
    // Above it a bar: what to add, the contents, whether the file checks, and the board's menu. The Tide the file
    // stands for, the contents and the file's own lines are there when asked for, and not before.
    public sealed class TideGraphWindow : EditorWindow
    {
        [SerializeField] string path;
        [SerializeField] string saved = "";       // The file's text as last read or written
        [SerializeField] TideGraphState state;
        [SerializeField] int selected;            // The element last selected or shown
        [SerializeField] bool showTide;
        [SerializeField] bool myGameClosed, detailsClosed;

        internal GraphDocument Doc;
        internal GraphBoard Board;
        internal GraphCheckResult Checked;
        internal string ReadError;   // Why the file couldn't be read
        internal string Path => path;
        internal int SelectedElement => selected;

        VisualElement shell, body, overlay, shade, chip, row;
        Label titleLabel, chipLabel;
        VisualElement toast;
        GraphPopup popup;
        GraphMyGame myGame;
        GraphDetails details;
        GraphTideView tide;
        double checkAt = -1;
        bool checking, checkAgain, framed, gone;
        DateTime fileTime;
        double nextFileLook;
        string waiting;            // The text read while there's no graph schema to edit it with
        bool waitingFromFile;      // ...read from the file, not Undo's state
        bool? forcedLight;

        // The editor's light theme, or one a test asks for
        internal bool Light => forcedLight ?? !EditorGUIUtility.isProSkin;
        internal GraphPopup Popup => popup;
        internal VisualElement Overlay => overlay;
        internal GraphMyGame MyGame => myGame != null && myGame.parent != null ? myGame : null;
        internal GraphDetails Details => details != null && details.parent != null ? details : null;
        internal GraphTideView TideView => tide != null && tide.parent != null ? tide : null;
        internal string LastSaid { get; private set; }

        // Opens a graph's file in its window, or a new one
        public static TideGraphWindow Open(string assetPath)
        {
            var full = System.IO.Path.GetFullPath(assetPath);
            foreach (var w in Resources.FindObjectsOfTypeAll<TideGraphWindow>())
            {
                if (w.path != null && string.Equals(System.IO.Path.GetFullPath(w.path), full, StringComparison.OrdinalIgnoreCase))
                {
                    w.Focus();
                    return w;
                }
            }
            var window = CreateWindow<TideGraphWindow>();
            window.path = assetPath;
            // Its panels as the last window's were left
            window.myGameClosed = !EditorPrefs.GetBool(MyGamePref, true);
            window.detailsClosed = !EditorPrefs.GetBool(DetailsPref, true);
            window.minSize = new Vector2(640, 360);
            window.Load();
            window.Show();
            return window;
        }

        const string MyGamePref = "Tide.GraphWindow.MyGame", DetailsPref = "Tide.GraphWindow.Details";

        void OnEnable()
        {
            if (state == null)
            {
                state = CreateInstance<TideGraphState>();
                state.hideFlags = HideFlags.HideAndDontSave;
            }
            Undo.undoRedoPerformed += UndoRedo;
            EditorApplication.update += Tick;
            GraphSchema.Changed += SchemaChanged;
            saveChangesMessage = "This graph has changes that aren't saved. Save them?";
            if (!string.IsNullOrEmpty(path) && Doc == null)
            {
                // After a domain reload: the state Undo kept, else the file
                if (!string.IsNullOrEmpty(state.json)) ReadText(state.json);
                else Load();
            }
        }

        void OnDisable()
        {
            Undo.undoRedoPerformed -= UndoRedo;
            EditorApplication.update -= Tick;
            GraphSchema.Changed -= SchemaChanged;
        }

        // A tide installed or switched while windows were open (an update: the window asked while the new tide was on
        // its way, and found none): the schema and tidels are looked for again, and every graph is checked again
        [InitializeOnLoadMethod]
        static void Hooks() => TideToolchain.Changed += () => LookAgain(true);

        // Coming back to a window that has no schema or whose tidels gave up looks again too (tide may have been
        // installed outside Unity: `tide upgrade`)
        void OnFocus()
        {
            var client = TideLanguageClient.Shared;
            if (GraphSchema.Failure != null || (client != null && !client.Usable)) LookAgain(false);
        }

        static void LookAgain(bool changed)
        {
            var client = TideLanguageClient.Shared;
            if (changed || (client != null && !client.Usable)) TideLanguageClient.ShutdownAll();
            if (GraphSchema.Failure != null) GraphSchema.Reload(); // Its windows build again (SchemaChanged)
            foreach (var window in Resources.FindObjectsOfTypeAll<TideGraphWindow>())
            {
                if (window != null && window.Doc != null) window.CheckSoon(0);
            }
        }

        // The graph schema came, or changed (tidels's isn't the one read before): the window builds again with it
        void SchemaChanged()
        {
            if (this == null) return;
            if (!GraphSchema.Available)
            {
                if (Doc != null) waiting = Doc.Text;
                Doc = null;
                BuildNoSchema();
                return;
            }
            if (Doc == null && waiting != null)
            {
                var text = waiting;
                var fromFile = waitingFromFile;
                waiting = null;
                ReadText(text);
                if (fromFile) saved = state.json;
                UpdateTitle();
                return;
            }
            if (Doc != null) Build();
        }

        void OnDestroy()
        {
            // Closed: a check that ends now (the server's stopping fails those it was asked) starts no other
            gone = true;
            TideGraphServices.Checker.Closed(path);
            if (state != null)
            {
                Undo.ClearUndo(state);
                DestroyImmediate(state);
            }
        }

        // Reads the file again, over what's here
        internal void Load()
        {
            string text;
            try
            {
                text = File.ReadAllText(path, Encoding.UTF8);
                fileTime = File.GetLastWriteTimeUtc(path);
            }
            catch (IOException e)
            {
                ReadError = $"{path} can't be read: {e.Message}";
                Doc = GraphDocument.Empty();
                Build();
                return;
            }
            ReadText(text);
            if (Doc == null) waitingFromFile = true;
            else saved = state.json;
            UpdateTitle();
        }

        void ReadText(string text)
        {
            if (!GraphSchema.Available)
            {
                // Nothing says what the file's kinds are: it waits for a schema, and the window says why
                waiting = text;
                Doc = null;
                BuildNoSchema();
                return;
            }
            try
            {
                Doc = GraphDocument.Parse(text);
                ReadError = null;
            }
            catch (GraphJson.ParseError e)
            {
                Doc = GraphDocument.Empty();
                ReadError = $"{path}:{e.Message}. Fix the file in a text editor; it's left as it is until then.";
            }
            state.json = Doc.Text;
            framed = false;
            Build();
            CheckSoon(0);
        }

        public override void SaveChanges()
        {
            Save();
            base.SaveChanges();
        }

        internal bool Dirty => Doc != null && ReadError == null && state.json != saved;

        internal void Save()
        {
            if (Doc == null || ReadError != null || string.IsNullOrEmpty(path)) return;
            var text = Doc.Text;
            File.WriteAllText(path, text, new UTF8Encoding(false));
            saved = text;
            fileTime = File.GetLastWriteTimeUtc(path);
            if (path.StartsWith("Assets/") || path.StartsWith("Packages/")) AssetDatabase.ImportAsset(path);
            UpdateTitle();
        }

        // An edit: recorded for Undo, then made, then shown. `owners` are the declarations, members or frames whose
        // card or nodes it changed, which alone are built again; with none, everything on screen is.
        internal void Edit(string name, Action change, params int[] owners)
        {
            if (Doc == null || ReadError != null) return;
            Undo.IncrementCurrentGroup();
            Undo.RecordObject(state, name);
            change();
            Doc.Reindex();
            state.json = Doc.Text;
            Board?.Changed(owners);
            MyGame?.Fill();
            Details?.Fill();
            UpdateTitle();
            CheckSoon();
        }

        // What's selected changed: Details shows it, and My game the variables of the code it's in
        internal void SelectionChanged()
        {
            Details?.Fill();
            MyGame?.Selected();
        }

        void UndoRedo()
        {
            if (state == null || Doc == null || state.json == Doc.Text) return;
            try
            {
                Doc = GraphDocument.Parse(state.json);
            }
            catch (GraphJson.ParseError)
            {
                return;
            }
            ClosePopup(popup);
            Board?.Reload();
            MyGame?.Fill();
            Details?.Fill();
            UpdateTitle();
            CheckSoon();
        }

        // What's selected, for the title and for whoever asks
        internal void Selected(int id) => selected = id;

        void UpdateTitle()
        {
            var name = string.IsNullOrEmpty(path) ? "Graph" : System.IO.Path.GetFileNameWithoutExtension(path);
            titleContent = new GUIContent(name, AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.FilePath), path);
            hasUnsavedChanges = Dirty;
            if (titleLabel != null)
            {
                titleLabel.text = name + (Dirty ? "  •  not saved" : "");
                titleLabel.tooltip = path ?? "";
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Layout

        void CreateGUI()
        {
            if (Doc != null) Build();
            else if (waiting != null) BuildNoSchema();
        }

        // The theme the board is drawn in, whatever the editor's: a test's pictures of both
        internal void ForceTheme(bool? light)
        {
            forcedLight = light;
            if (Doc != null) Build();
        }

        VisualElement Shell()
        {
            var root = rootVisualElement;
            root.Clear();
            popup = null;
            shade = null;
            myGame = null;
            details = null;
            tide = null;
            shell = new VisualElement();
            shell.AddToClassList("tg-root");
            shell.EnableInClassList("tg-light", Light);
            var sheet = GraphLook.Sheet;
            if (sheet != null) shell.styleSheets.Add(sheet);
            // The schema's look: its colours for dark or light, its sizes and its font
            GraphLook.Apply(shell, Light);
            if (forcedLight != null && forcedLight.Value == EditorGUIUtility.isProSkin)
            {
                // The other theme than the editor's: its own sheet of Unity's variables, on the board alone
                var other = EditorSheet(forcedLight.Value);
                if (other != null) shell.styleSheets.Insert(0, other);
            }
            root.Add(shell);
            return shell;
        }

        static StyleSheet EditorSheet(bool light)
        {
            var utility = typeof(UnityEditor.UIElements.Toolbar).Assembly.GetType("UnityEditor.UIElements.UIElementsEditorUtility");
            var method = utility?.GetMethod(light ? "GetCommonLightStyleSheet" : "GetCommonDarkStyleSheet", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public);
            return method?.Invoke(null, null) as StyleSheet;
        }

        // The window without a schema: why, and a button that asks for it again
        void BuildNoSchema()
        {
            Board = null;
            var s = Shell();
            var banner = GraphLook.Text(GraphSchema.Failure ?? "There's no graph schema.", "tg-banner");
            s.Add(banner);
            var again = GraphLook.Plain(GraphSchema.Reload, "tg-link");
            again.text = "Try again";
            again.style.alignSelf = Align.FlexStart;
            again.style.marginLeft = 8;
            again.style.marginTop = 6;
            s.Add(again);
        }

        void Build()
        {
            if (!GraphSchema.Available)
            {
                BuildNoSchema();
                return;
            }
            var s = Shell();
            s.RegisterCallback<KeyDownEvent>(Keys, TrickleDown.TrickleDown);

            var bar = GraphLook.Row("tg-bar");
            Button add = null;
            // The schema's "+ Add", its + drawn as the icon
            add = GraphLook.Labelled("plus", GraphSchema.Word("toolbar.add").TrimStart('+').Trim(), () => Board.OpenAdd(null, add), "tg-bar-button", "tg-bar-button--primary");
            add.name = "add";
            add.tooltip = GraphSchema.Word("toolbar.add.tip");
            bar.Add(add);
            var toMine = GraphLook.Labelled("list", GraphSchema.Word("toolbar.myGame"), () => ShowMyGame(MyGame == null, MyGame == null), "tg-bar-button");
            toMine.name = "my-game-button";
            toMine.tooltip = GraphSchema.Word("toolbar.myGame.tip");
            bar.Add(toMine);
            Button find = null;
            find = findButton = GraphLook.Labelled(null, GraphSchema.Word("toolbar.find"), () => OpenFind(find), "tg-bar-button");
            find.name = "find";
            find.tooltip = GraphSchema.Word("toolbar.find.tip");
            bar.Add(find);
            titleLabel = GraphLook.Text("", "tg-title");
            bar.Add(titleLabel);
            Button chipButton = null;
            chipButton = GraphLook.Plain(() => OpenProblems(chipButton), "tg-chip");
            chipButton.name = "check";
            chipButton.Add(GraphLook.Row("tg-chip-dot"));
            chipLabel = new Label(GraphSchema.Word("status.checking")) { pickingMode = PickingMode.Ignore };
            chipButton.Add(chipLabel);
            chip = chipButton;
            bar.Add(chip);
            bar.Add(GraphLook.Row("tg-bar-sep"));
            // Undo and redo: Unity's own, which this window's edits are recorded in
            var undo = GraphLook.Plain(Undo.PerformUndo, "tg-bar-button", "tg-bar-button--bare", "tg-bar-tool");
            undo.text = GraphSchema.Word("toolbar.undo");
            undo.name = "undo";
            undo.tooltip = GraphSchema.Word("toolbar.undo.tip");
            bar.Add(undo);
            var redo = GraphLook.Plain(Undo.PerformRedo, "tg-bar-button", "tg-bar-button--bare", "tg-bar-tool");
            redo.text = GraphSchema.Word("toolbar.redo");
            redo.name = "redo";
            redo.tooltip = GraphSchema.Word("toolbar.redo.tip");
            bar.Add(redo);
            bar.Add(GraphLook.Row("tg-bar-sep"));
            Button more = null;
            var toDetails = GraphLook.Labelled(null, GraphSchema.Word("toolbar.details"), () => ShowDetails(Details == null), "tg-bar-button");
            toDetails.name = "details-button";
            toDetails.tooltip = GraphSchema.Word("toolbar.details.tip");
            bar.Add(toDetails);
            more = GraphLook.IconButton("more", GraphSchema.Word("toolbar.more.tip"), () => OpenMenu(more), "tg-bar-button", "tg-bar-button--bare");
            more.name = "menu";
            bar.Add(more);
            s.Add(bar);

            if (ReadError != null)
            {
                s.Add(GraphLook.Text(ReadError, "tg-banner"));
                var again = GraphLook.Plain(Load, "tg-link");
                again.text = "Read it again";
                again.style.alignSelf = Align.FlexStart;
                again.style.marginLeft = 8;
                again.style.marginTop = 6;
                s.Add(again);
                Board = null;
                UpdateTitle();
                return;
            }

            // My game on the left, the board, Details on the right
            row = GraphLook.Row("tg-body");
            s.Add(row);
            body = new VisualElement { style = { flexGrow = 1, flexShrink = 1 } };
            body.AddToClassList("tg-board-area");
            row.Add(body);
            Board = new GraphBoard(this);
            body.Add(Board);
            // The zoom buttons, at the board's bottom right: − 100% + Fit
            var zoomBar = GraphLook.Row("tg-zoom");
            zoomBar.name = "zoom";
            Button ZoomButton(string text, string tip, Action click, string name)
            {
                var b = GraphLook.Plain(click, "tg-zoom-button");
                if (zoomBar.childCount > 0) b.AddToClassList("tg-zoom-button--next");
                b.text = text;
                b.tooltip = tip;
                b.name = name;
                zoomBar.Add(b);
                return b;
            }
            ZoomButton(GraphSchema.Word("zoom.out"), GraphSchema.Word("zoom.out.tip"), () => Board.ZoomStep(1 / 1.25f), "zoom-out");
            var zoomLabel = ZoomButton("100%", GraphSchema.Word("zoom.full.tip"), () => Board.ZoomStep(1 / Board.Zoom), "zoom-label");
            zoomLabel.AddToClassList("tg-zoom-label");
            ZoomButton(GraphSchema.Word("zoom.in"), GraphSchema.Word("zoom.in.tip"), () => Board.ZoomStep(1.25f), "zoom-in");
            ZoomButton(GraphSchema.Word("zoom.fit"), GraphSchema.Word("zoom.fit.tip"), () => Board.FitAll(), "zoom-fit");
            Board.ZoomChanged += () => zoomLabel.text = Mathf.RoundToInt(Board.Zoom * 100) + "%";
            body.Add(zoomBar);
            if (!myGameClosed) ShowMyGame(true);
            if (!detailsClosed) ShowDetails(true);
            overlay = new VisualElement { pickingMode = PickingMode.Ignore };
            overlay.AddToClassList("tg-fill");
            s.Add(overlay);
            Board.Sync();
            Board.RegisterCallback<GeometryChangedEvent>(_ =>
            {
                if (framed || float.IsNaN(Board.layout.width) || Board.layout.width <= 0) return;
                framed = true;
                Board.FrameStart();
                if (selected != 0 && Doc.Find(selected) != null) Board.Reveal(selected);
            });
            if (showTide) ShowTide(true);
            UpdateTitle();
            ShowCheck();
        }

        void Keys(KeyDownEvent e)
        {
            if ((e.ctrlKey || e.commandKey) && e.keyCode == KeyCode.S)
            {
                Save();
                e.StopPropagation();
            }
            // Escape closes what's open, wherever the keyboard is
            else if (e.keyCode == KeyCode.Escape && popup != null && !(e.target is VisualElement t && popup.Contains(t)))
            {
                ClosePopup(popup);
                e.StopPropagation();
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Popups: one at a time, over everything, closed by a pick, by Escape or by a click outside

        // Opens a popup below the element it's opened from, or to its right
        internal void Open(GraphPopup p, VisualElement from, bool beside = false)
        {
            p.From = from;
            Open(p, from.worldBound, beside);
        }

        // Opens a popup below what it's opened from (`from`, in the panel's space), or to its right
        internal void Open(GraphPopup p, Rect from, bool beside = false)
        {
            if (overlay == null) return;
            ClosePopup(popup);
            popup = p;
            p.Window = this;
            p.Beside = beside;
            p.Anchor = new Rect(overlay.WorldToLocal(from.position), from.size);
            shade = GraphLook.Row("tg-shade");
            shade.RegisterCallback<PointerDownEvent>(e =>
            {
                ClosePopup(p);
                e.StopPropagation();
            });
            overlay.Add(shade);
            overlay.Add(p);
            Fit(p);
            p.RegisterCallback<GeometryChangedEvent>(_ => Fit(p));
            p.Opened();
        }

        // Where a popup goes: by what it was opened from, and inside the window
        void Fit(GraphPopup p)
        {
            var w = overlay.layout.width;
            var h = overlay.layout.height;
            var size = p.layout.size;
            // What it was opened from, where it is now: it may not have been laid out when it was clicked
            if (p.From != null && p.From.panel != null && !float.IsNaN(p.From.worldBound.width) && p.From.worldBound.width > 0)
                p.Anchor = new Rect(overlay.WorldToLocal(p.From.worldBound.position), p.From.worldBound.size);
            var a = p.Anchor;
            var left = p.Beside ? a.xMax + 8 : a.xMin;
            var top = p.Beside ? a.yMin : a.yMax + 2;
            if (!float.IsNaN(w) && !float.IsNaN(size.x) && size.x > 0)
            {
                // No room below what it's opened from: beside it, where it hides less of it
                if (!p.Beside && top + size.y > h - 4 && a.width > 0)
                {
                    if (a.xMax + 8 + size.x <= w - 4) left = a.xMax + 8;
                    else if (a.xMin - 8 - size.x >= 4) left = a.xMin - 8 - size.x;
                    top = Mathf.Max(4, Mathf.Min(a.yMin, h - size.y - 4));
                }
                if (left + size.x > w - 4) left = p.Beside && a.xMin - size.x - 8 >= 0 ? a.xMin - size.x - 8 : Mathf.Max(4, w - size.x - 4);
                if (top + size.y > h - 4) top = Mathf.Max(4, h - size.y - 4);
            }
            p.style.left = Mathf.Round(left);
            p.style.top = Mathf.Round(top);
        }

        internal void ClosePopup(GraphPopup p)
        {
            if (p == null || p != popup) return;
            p.WasClosed = true;
            popup = null;
            shade?.RemoveFromHierarchy();
            shade = null;
            p.RemoveFromHierarchy();
            p.Closed?.Invoke();
            if (popup == null) Board?.Focus();
        }

        // Says something for a few seconds, at the board's foot: why what was tried can't be
        internal void Say(string text, bool bad = false) => Say(text, bad, null, null);

        // ...with a button that does something about it: Undo, after something was deleted
        internal void Say(string text, bool bad, string action, Action run)
        {
            LastSaid = text;
            if (body == null) return;
            toast?.RemoveFromHierarchy();
            VisualElement said;
            if (run == null)
            {
                said = GraphLook.Text(text, "tg-toast");
                said.pickingMode = PickingMode.Ignore;
            }
            else
            {
                said = GraphLook.Row("tg-toast", "tg-toast--action");
                said.Add(GraphLook.Text(text));
                var button = GraphLook.Plain(() =>
                {
                    said.RemoveFromHierarchy();
                    run();
                }, "tg-toast-button");
                button.text = action;
                button.name = "toast-action";
                said.Add(button);
            }
            toast = said;
            said.EnableInClassList("tg-toast--bad", bad);
            body.Add(said);
            said.schedule.Execute(() =>
            {
                said.RemoveFromHierarchy();
                if (toast == said) toast = null;
            }).ExecuteLater(5200);
        }

        internal List<GraphOption> MenuOptions()
        {
            return new List<GraphOption>
            {
                new GraphOption { Title = GraphSchema.Word("menu.save"), Detail = GraphSchema.WordKeys("menu.save"), Off = !Dirty, Pick = Save },
                new GraphOption { Separator = true },
                new GraphOption { Title = GraphSchema.Word("menu.fit"), Detail = GraphSchema.WordKeys("menu.fit"), Pick = () => Board.FitAll() },
                new GraphOption { Title = GraphSchema.Word("menu.tidyBoard"), Detail = GraphSchema.Word("menu.tidyBoard.summary"), Pick = () => Board.TidyBoard() },
                new GraphOption { Separator = true },
                new GraphOption { Title = GraphSchema.Word("menu.viewTide"), Detail = GraphSchema.Word("menu.viewTide.summary"), Checked = TideView != null, Pick = () => ShowTide(TideView == null) },
                new GraphOption { Title = "Namespace and using…", Detail = "The file's own lines", Pick = () => Open(new GraphFileSettings(this), chip) },
                new GraphOption { Separator = true },
                new GraphOption { Title = GraphSchema.Word("menu.help"), Detail = "Keys and gestures", Pick = () => Open(new GraphShortcuts(), chip) },
            };
        }

        void OpenMenu(VisualElement from)
        {
            var list = new GraphOptionList(null, null, null, 300);
            list.Set(MenuOptions());
            Open(list, from);
        }

        // My game, docked on the left: opened (with the keyboard in its search, for Ctrl+F) or closed, and left so
        internal void ShowMyGame(bool show, bool search = false)
        {
            myGameClosed = !show;
            EditorPrefs.SetBool(MyGamePref, show);
            if (row == null) return;
            if (!show)
            {
                MyGame?.RemoveFromHierarchy();
                Board?.Focus();
                return;
            }
            if (MyGame == null)
            {
                myGame = new GraphMyGame(this);
                row.Insert(0, myGame);
            }
            if (search) myGame.FocusSearch();
        }

        // Details, docked on the right
        internal void ShowDetails(bool show)
        {
            detailsClosed = !show;
            EditorPrefs.SetBool(DetailsPref, show);
            if (row == null) return;
            if (!show)
            {
                Details?.RemoveFromHierarchy();
                Board?.Focus();
                return;
            }
            if (Details == null)
            {
                details = new GraphDetails(this);
                row.Add(details);
            }
            details.Fill();
        }

        internal void ShowTide(bool show)
        {
            showTide = show;
            if (body == null) return;
            if (!show)
            {
                TideView?.RemoveFromHierarchy();
                return;
            }
            if (TideView == null)
            {
                tide = new GraphTideView(this);
                body.Add(tide);
            }
            tide.Show();
        }

        internal void TideClosed() => showTide = false;

        // ---------------------------------------------------------------------------------------------------------
        // Checking: what's wrong with the file, a moment after the last edit. What's shown is the last check's.

        internal void CheckSoon(double delay = 0.35)
        {
            checkAt = EditorApplication.timeSinceStartup + delay;
            ShowChip();
        }

        void Tick()
        {
            var now = EditorApplication.timeSinceStartup;
            if (checkAt >= 0 && now >= checkAt)
            {
                checkAt = -1;
                if (checking) checkAgain = true;
                else RunCheck();
            }
            if (now >= nextFileLook && !string.IsNullOrEmpty(path))
            {
                nextFileLook = now + 1;
                LookAtFile();
            }
        }

        void RunCheck()
        {
            if (Doc == null || ReadError != null || gone) return;
            checking = true;
            var text = Doc.Text;
            TideGraphServices.Checker.Check(path, text, result =>
            {
                checking = false;
                if (this == null || gone) return;
                if (result.Json == null) result.Json = text;
                Checked = result;
                ShowCheck();
                if (checkAgain)
                {
                    checkAgain = false;
                    RunCheck();
                }
            });
        }

        internal bool CheckPending => checking || checkAt >= 0;

        void ShowChip()
        {
            if (chip == null) return;
            var errors = Checked?.Diagnostics.Count(d => d.Severity == "error") ?? 0;
            var warnings = Checked?.Diagnostics.Count(d => d.Severity == "warning") ?? 0;
            var busy = Checked == null || CheckPending;
            var failed = Checked?.Failure != null;
            // As the page says it: a check that couldn't run is "Not checked", in amber like a warning
            chip.EnableInClassList("tg-chip--bad", !busy && errors > 0);
            chip.EnableInClassList("tg-chip--warn", !busy && errors == 0 && (failed || warnings > 0));
            chip.EnableInClassList("tg-chip--ok", !busy && errors == 0 && !failed && warnings == 0);
            chipLabel.text = busy ? GraphSchema.Word("status.checking")
                : errors > 0 ? GraphSchema.Word(errors == 1 ? "status.problem" : "status.problems", "count", errors.ToString())
                : failed ? GraphSchema.Word("status.off")
                : warnings > 0 ? GraphSchema.Word(warnings == 1 ? "status.warning" : "status.warnings", "count", warnings.ToString())
                : GraphSchema.Word("status.checked");
            var click = errors + warnings > 0 ? GraphSchema.Word("status.list.tip") : busy ? "Click for the list." : failed ? GraphSchema.Word("status.off.tip") : GraphSchema.Word("status.checked.tip");
            chip.tooltip = (failed ? Checked.Failure + "\n" : "") + CheckerSays + "\n" + click;
        }

        internal string ChipText => chipLabel?.text;

        void ShowCheck()
        {
            ShowChip();
            if (Checked == null || Board == null) return;
            // A check of the file as it was before the last edit: the one of the file as it is comes next
            if (Checked.Json != null && Doc != null && Checked.Json != Doc.Text && CheckPending) return;
            var before = new HashSet<string>(Board.ProblemsShown());
            Board.SetProblems(Checked.Diagnostics);
            foreach (var frame in Board.Items.OfType<GraphFrame>().Concat(Board.ValueFrames)) frame.ApplyTypes();
            // A problem gone: its hover text, if it's showing, goes with it
            if (before.Count > 0 && !before.IsSubsetOf(Board.ProblemsShown())) CloseTooltip();
            Board.AfterCheck();
            TideView?.Show();
            MyGame?.Fill();
        }

        // The editor's tooltip, if one is showing: Unity shows it until the pointer moves, whatever the element's
        // tooltip has become since
        static void CloseTooltip()
        {
            var view = typeof(EditorWindow).Assembly.GetType("UnityEditor.TooltipView");
            view?.GetMethod("Close", BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null)?.Invoke(null, null);
        }

        // The panels taken off the board for a moment and put back as they were left, without changing how
        // they're left (a test of the board's own gestures)
        internal void ShowPanels(bool shown)
        {
            if (!shown)
            {
                MyGame?.RemoveFromHierarchy();
                Details?.RemoveFromHierarchy();
                return;
            }
            if (!myGameClosed) ShowMyGame(true);
            if (!detailsClosed) ShowDetails(true);
        }

        // Which checked the graph: the language server, with the game's code, or tidec, which only reads the file
        internal string CheckerSays => Checked == null ? "Not checked yet."
            : Checked.Checker == "tidels" ? "Checked by tidels, the language server, with the game's code."
            : "Read by tidec alone: only what the file itself gets wrong is found" + (Checked.CheckerNote != null ? " (no tidels: " + Checked.CheckerNote.Split('\n')[0] + ")." : ".");

        // What the check chip lists: each problem, and where it is
        internal List<GraphOption> ProblemOptions()
        {
            var options = new List<GraphOption>();
            if (Checked == null) return options;
            if (Checked.Failure != null) options.Add(new GraphOption { Title = GraphSchema.Word("problems.off"), Detail = GraphSchema.Word("problems.off.why", "why", Checked.Failure), Tall = true, Off = true });
            foreach (var d in Checked.Diagnostics)
            {
                var diagnostic = d;
                var where = Board?.ItemOf(d.Id)?.Title;
                var lines = d.Text.Split('\n');
                options.Add(new GraphOption
                {
                    Title = lines[0], Detail = string.Join("\n", lines.Skip(1).Concat(where != null ? new[] { "In " + where } : new string[0])), Tall = true, Help = d.Detail,
                    Section = GraphSchema.Word(d.Severity == "error" ? "problems.group.problems" : d.Severity == "warning" ? "problems.group.look" : "problems.group.notes"), Off = Doc.Find(d.Id) == null,
                    Pick = () => Reveal(diagnostic.Id),
                });
            }
            if (options.Count == 0) options.Add(new GraphOption { Title = GraphSchema.Word("problems.none"), Detail = CheckerSays, Tall = true, Off = true });
            return options;
        }

        // Find…: the file's declarations and the members with code of their own, by name, under their kinds; a pick
        // goes to it
        Button findButton;
        internal VisualElement FindButton => findButton;

        internal GraphOptionList OpenFind(VisualElement from)
        {
            if (Doc == null || Board == null) return null;
            var options = new List<GraphOption>();
            foreach (var e in Doc.Elements)
            {
                if (Board.Item(e.Id) == null) continue;
                var id = e.Id;
                var kind = e.Role == GraphRole.Decl ? GraphSchema.DeclTitle(e.Value.Str("kind")) : e.Role == GraphRole.Accessor ? GraphSchema.MemberTitle("property") : GraphSchema.MemberTitle(e.Value.Str("kind"));
                var group = e.Role == GraphRole.Decl ? GraphSchema.DeclGroup(e.Value.Str("kind")) : "Behaviour";
                var problems = Board.HasProblems(Board.Item(id));
                options.Add(new GraphOption
                {
                    Title = Doc.TitleOf(e), Detail = kind, Keywords = kind, Data = id, Section = GraphSchema.Groups.FirstOrDefault(g => g.Name == group)?.Title ?? group,
                    Help = problems ? "It has a problem" : null, Pick = () => Reveal(id),
                });
            }
            var list = new GraphOptionList(null, null, GraphSchema.Word("jump.search"), 360, 440);
            list.Set(options.OrderBy(o => Array.FindIndex(GraphSchema.Groups, g => g.Title == o.Section)));
            Open(list, from);
            return list;
        }

        void OpenProblems(VisualElement from)
        {
            var list = new GraphOptionList(null, null, null, 420);
            list.Set(ProblemOptions());
            Open(list, from);
        }

        // Shows an element: a node in its frame, a field on its card (a build's error on it, from the Console)
        internal void Reveal(int id)
        {
            if (Doc?.Find(id) == null || Board == null) return;
            Board.Reveal(id);
            selected = id;
        }

        // Shows what a diagnostic is about
        internal void JumpTo(GraphDiagnostic d) => Reveal(d.Id);

        // The file changed on disk: read again if nothing here is unsaved
        void LookAtFile()
        {
            try
            {
                if (!File.Exists(path)) return;
                var time = File.GetLastWriteTimeUtc(path);
                if (time == fileTime) return;
                fileTime = time;
                if (Dirty) return;
                var text = File.ReadAllText(path, Encoding.UTF8);
                if (text == saved) return;
                Undo.RecordObject(state, "Read the graph again");
                ReadText(text);
                if (Doc != null) saved = state.json;
                else waitingFromFile = true;
                UpdateTitle();
            }
            catch (IOException)
            {
                // Being written: the next look reads it
            }
        }
    }
}
