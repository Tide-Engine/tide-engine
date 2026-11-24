using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.UIElements;
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

    // The graph editor of a .tidegraph file: its declarations on the left, the graph of what's selected in the
    // middle, what's selected's keys on the right, and below the graph the Tide that tidec writes for the file with
    // what tidec says is wrong with it.
    public sealed class TideGraphWindow : EditorWindow
    {
        [SerializeField] string path;
        [SerializeField] string saved = "";       // The file's text as last read or written
        [SerializeField] TideGraphState state;
        [SerializeField] int selected;            // The element the inspector shows
        [SerializeField] int slotOwner;           // The graph the canvas shows
        [SerializeField] string slotKey;
        [SerializeField] int slotIndex = -1;
        [SerializeField] bool showTide = true;

        internal GraphDocument Doc;
        internal GraphOutlineView Outline;
        internal GraphCanvas Canvas;
        internal GraphInspectorView Inspector;
        internal GraphCheckResult Checked;
        internal HashSet<int> Selected => Canvas?.Selection ?? new HashSet<int>();
        internal string ReadError;   // Why the file couldn't be read
        internal string Path => path;

        Label titleLabel;
        Label tideText;
        VisualElement tidePanel;
        ScrollView diagnosticsList;
        Label status;
        double checkAt = -1;
        bool checking, checkAgain;
        DateTime fileTime;
        double nextFileLook;

        internal GraphSlot Slot
        {
            get => new GraphSlot(slotOwner, slotKey, slotIndex);
            set
            {
                slotOwner = value.Owner;
                slotKey = value.Key;
                slotIndex = value.Index;
            }
        }

        internal int SelectedElement => selected;

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
            window.minSize = new Vector2(640, 360);
            window.Load();
            window.Show();
            return window;
        }

        void OnEnable()
        {
            if (state == null)
            {
                state = CreateInstance<TideGraphState>();
                state.hideFlags = HideFlags.HideAndDontSave;
            }
            Undo.undoRedoPerformed += UndoRedo;
            EditorApplication.update += Tick;
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
        }

        void OnDestroy()
        {
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
            saved = state.json;
            UpdateTitle();
        }

        void ReadText(string text)
        {
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
            if (!Slot.Valid || Doc.Find(slotOwner) == null) Slot = FirstSlot();
            Build();
            CheckSoon(0);
        }

        GraphSlot FirstSlot()
        {
            foreach (var e in Doc.Elements.OrderBy(e => e.Id))
                if ((e.Role == GraphRole.Decl || e.Role == GraphRole.Member || e.Role == GraphRole.Accessor) && e.Value.Get("body") != null && e.Value.Get("body").IsObject)
                    return new GraphSlot(e.Id, "body");
            return default;
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

        // An edit: recorded for Undo, then made, then shown
        internal void Edit(string name, Action change, GraphRedraw redraw = GraphRedraw.All)
        {
            if (Doc == null || ReadError != null) return;
            Undo.IncrementCurrentGroup();
            Undo.RecordObject(state, name);
            change();
            Doc.Reindex();
            state.json = Doc.Text;
            if ((redraw & GraphRedraw.Outline) != 0) Outline?.Rebuild();
            if ((redraw & GraphRedraw.Canvas) != 0) Canvas?.Rebuild();
            else Canvas?.RefreshDimming();
            if ((redraw & GraphRedraw.Inspector) != 0) Inspector?.Rebuild();
            UpdateTitle();
            CheckSoon();
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
            Refresh(true);
            CheckSoon();
        }

        // Selects an element: the inspector shows it, the outline marks it, and a graph of its own opens
        internal void Select(int id, bool openGraph = true)
        {
            if (Outline == null) return;
            var e = Doc.Find(id);
            // A parameter, payload value or attribute is edited with what it's of
            while (e != null && (e.Role == GraphRole.Param || e.Role == GraphRole.SampleParam || e.Role == GraphRole.Payload || e.Role == GraphRole.Attribute))
            {
                var owner = Doc.Find(e.Owner?.Wire("id") ?? 0);
                if (owner == null || owner == e) break;
                e = owner;
            }
            if (e != null) id = e.Id;
            selected = id;
            if (openGraph && e != null)
            {
                if (e.Role == GraphRole.Node || e.Role == GraphRole.Note)
                {
                    if (!e.Slot.Equals(Slot)) Show(e.Slot);
                    Canvas.SelectOnly(id);
                }
                else if (e.Value.Get("body") != null && e.Value.Get("body").IsObject && e.Value.Str("kind") != "property") Show(new GraphSlot(id, "body"));
            }
            Outline.Mark(id);
            Inspector.Rebuild();
        }

        // Shows a graph in the canvas
        internal void Show(GraphSlot slot)
        {
            if (Canvas == null) return;
            Slot = slot;
            Canvas.Rebuild();
            Canvas.FrameAll();
            UpdateTitle();
        }

        internal void Refresh(bool outline)
        {
            if (Outline == null) return;
            if (outline) Outline.Rebuild();
            Canvas.Rebuild();
            Inspector.Rebuild();
            UpdateTitle();
        }

        void UpdateTitle()
        {
            var name = string.IsNullOrEmpty(path) ? "Graph" : System.IO.Path.GetFileNameWithoutExtension(path);
            titleContent = new GUIContent(name, AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.FilePath), path);
            hasUnsavedChanges = Dirty;
            if (titleLabel != null)
            {
                var where = Doc?.Find(Slot.Owner);
                titleLabel.text = (where != null ? Doc.Describe(where) + " > " + Slot.Key + (Slot.Index >= 0 ? $" {Slot.Index + 1}" : "") : "Nothing open") + (Dirty ? "  (unsaved)" : "");
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Layout

        void CreateGUI()
        {
            if (Doc != null) Build();
        }

        void Build()
        {
            var root = rootVisualElement;
            root.Clear();
            root.style.flexDirection = FlexDirection.Column;
            root.RegisterCallback<KeyDownEvent>(Keys, TrickleDown.TrickleDown);

            var bar = new Toolbar();
            bar.Add(new ToolbarButton(Save) { text = "Save", tooltip = "Save the graph (Ctrl+S)" });
            bar.Add(new ToolbarButton(() => Canvas.AutoLayout()) { text = "Auto Layout", tooltip = "Lay out the graph shown as tidec does" });
            bar.Add(new ToolbarButton(() => Canvas.FrameSelectionOrAll()) { text = "Frame", tooltip = "Frame the selection, or the whole graph (F)" });
            var tide = new ToolbarToggle { text = "Tide", value = showTide, tooltip = "Show the Tide the file stands for" };
            tide.RegisterValueChangedCallback(e =>
            {
                showTide = e.newValue;
                tidePanel.style.display = showTide ? DisplayStyle.Flex : DisplayStyle.None;
            });
            bar.Add(tide);
            bar.Add(new ToolbarSpacer());
            titleLabel = new Label { style = { unityTextAlign = TextAnchor.MiddleLeft, marginLeft = 8 } };
            bar.Add(titleLabel);
            root.Add(bar);

            if (ReadError != null)
            {
                root.Add(new HelpBox(ReadError, HelpBoxMessageType.Error));
                var again = new Button(Load) { text = "Read again" };
                root.Add(again);
                return;
            }

            var split = new TwoPaneSplitView(0, 230, TwoPaneSplitViewOrientation.Horizontal);
            split.style.flexGrow = 1;
            Outline = new GraphOutlineView(this);
            split.Add(Outline);
            var right = new TwoPaneSplitView(1, 300, TwoPaneSplitViewOrientation.Horizontal);
            var center = new VisualElement { style = { flexGrow = 1, flexDirection = FlexDirection.Column } };
            Canvas = new GraphCanvas(this);
            center.Add(Canvas);
            center.Add(BuildTidePanel());
            right.Add(center);
            Inspector = new GraphInspectorView(this);
            right.Add(Inspector);
            split.Add(right);
            root.Add(split);
            status = new Label { style = { paddingLeft = 4, height = 18, unityTextAlign = TextAnchor.MiddleLeft } };
            root.Add(status);
            Refresh(true);
            Canvas.FrameAll();
            ShowCheck();
        }

        VisualElement BuildTidePanel()
        {
            tidePanel = new VisualElement { style = { height = 220, flexShrink = 0, borderTopWidth = 1, borderTopColor = new Color(0, 0, 0, 0.5f), flexDirection = FlexDirection.Row } };
            tidePanel.style.display = showTide ? DisplayStyle.Flex : DisplayStyle.None;
            var textScroll = new ScrollView(ScrollViewMode.VerticalAndHorizontal) { style = { flexGrow = 2 } };
            tideText = new Label { enableRichText = false, selection = { isSelectable = true } };
            tideText.style.unityFontDefinition = FontDefinition.FromFont(Font.CreateDynamicFontFromOSFont(new[] { "Consolas", "Menlo", "DejaVu Sans Mono", "Courier New" }, 12));
            tideText.style.whiteSpace = WhiteSpace.Pre;
            tideText.style.paddingLeft = 6;
            textScroll.Add(tideText);
            tidePanel.Add(textScroll);
            diagnosticsList = new ScrollView { style = { flexGrow = 1, minWidth = 200, borderLeftWidth = 1, borderLeftColor = new Color(0, 0, 0, 0.4f) } };
            tidePanel.Add(diagnosticsList);
            return tidePanel;
        }

        void Keys(KeyDownEvent e)
        {
            if ((e.ctrlKey || e.commandKey) && e.keyCode == KeyCode.S)
            {
                Save();
                e.StopPropagation();
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Checking: tidec's Tide for the file and what it says is wrong, a moment after the last edit

        internal void CheckSoon(double delay = 0.6)
        {
            checkAt = EditorApplication.timeSinceStartup + delay;
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
            if (Doc == null || ReadError != null) return;
            checking = true;
            var text = Doc.Text;
            TideGraphServices.Checker.Check(path, text, result =>
            {
                checking = false;
                if (this == null) return;
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

        void ShowCheck()
        {
            if (tideText == null || Checked == null) return;
            tideText.text = Checked.Failure != null && Checked.Text.Length == 0 ? "" : Checked.Text;
            diagnosticsList.Clear();
            if (Checked.Failure != null) diagnosticsList.Add(new HelpBox(Checked.Failure, HelpBoxMessageType.Warning));
            foreach (var d in Checked.Diagnostics)
            {
                var row = new Label($"{d.Severity}: {d.Message}") { tooltip = d.Detail, style = { whiteSpace = WhiteSpace.Normal, paddingLeft = 4, paddingTop = 2, paddingBottom = 2 } };
                row.style.color = d.Severity == "error" ? new Color(1f, 0.45f, 0.4f) : d.Severity == "warning" ? new Color(1f, 0.8f, 0.3f) : new Color(0.75f, 0.75f, 0.75f);
                var diagnostic = d;
                row.RegisterCallback<ClickEvent>(_ => JumpTo(diagnostic));
                diagnosticsList.Add(row);
            }
            if (status != null)
            {
                var errors = Checked.Diagnostics.Count(d => d.Severity == "error");
                status.text = Checked.Failure != null ? "tidec: " + Checked.Failure.Split('\n')[0] : errors > 0 ? $"{errors} error(s)" : "tidec reads the graph";
            }
            Canvas?.MarkDiagnostics(Checked.Diagnostics);
            Outline?.MarkDiagnostics(Checked.Diagnostics);
        }

        // Shows what a diagnostic is about: its node in its graph, or its element in the outline
        internal void JumpTo(GraphDiagnostic d)
        {
            var e = Doc.Find(d.Id);
            if (e == null) return;
            if (e.Role == GraphRole.Node || e.Role == GraphRole.Note)
            {
                if (!e.Slot.Equals(Slot)) Show(e.Slot);
                Canvas.SelectOnly(d.Id);
                Canvas.Frame(new[] { d.Id });
                selected = d.Id;
                Inspector.Rebuild();
            }
            else Select(d.Id, false);
        }

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
                saved = state.json;
                UpdateTitle();
            }
            catch (IOException)
            {
                // Being written: the next look reads it
            }
        }
    }
}
