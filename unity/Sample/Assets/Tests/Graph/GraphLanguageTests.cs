using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UIElements;

// The graph editor with Tide's language server (tidels): a graph's errors are tidels's, type-checked with the
// game's code, on what they're about, and gone with their cause; each value has its type; a type is picked from
// what's valid at its place; the search offers what completion offers there, and what it makes lowers to the Tide
// it stands for; the server starts again when it stops, and goes when the last graph's window closes. And the
// story a first-time user goes through, from an empty file to a system that changes a component, step by step.
public class GraphLanguageTests
{
    static string tidels, tidec;
    ITideGraphChecker checker;

    static string Repository
    {
        get
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.tide.unity");
            return Path.GetFullPath(Path.Combine(package.resolvedPath, "..", ".."));
        }
    }

    // An environment variable's program, else a build of this checkout's
    static string Find(string variable, string name)
    {
        var set = Environment.GetEnvironmentVariable(variable);
        if (!string.IsNullOrEmpty(set) && File.Exists(set)) return set;
        var exe = Application.platform == RuntimePlatform.WindowsEditor ? name + ".exe" : name;
        foreach (var preset in new[] { "sanitize", "debug", "release" })
        {
            var path = Path.Combine(Repository, "build", preset, "bin", exe);
            if (File.Exists(path)) return path;
        }
        return null;
    }

    [OneTimeSetUp]
    public void FindTheServer()
    {
        tidels = Find("TIDE_TIDELS", "tidels");
        tidec = Find("TIDE_TIDEC", "tidec");
        TideGraphCommand.TidelsOverride = tidels;
        TideGraphCommand.TidecOverride = tidec;
        // The graph schema of that tidec
        GraphSchema.Reload();
        checker = TideGraphServices.Checker;
        TideGraphServices.Checker = new TideGraphChecker();
    }

    [OneTimeTearDown]
    public void Forget()
    {
        TideLanguageClient.ShutdownAll();
        TideGraphCommand.TidelsOverride = null;
        TideGraphCommand.TidecOverride = null;
        TideGraphServices.Checker = checker;
    }

    [TearDown]
    public void CloseWindows()
    {
        if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null) LogAssert.ignoreFailingMessages = true;
        foreach (var w in Resources.FindObjectsOfTypeAll<TideGraphWindow>()) w.Close();
        // Graphs of the game in Temp that only one test reads: another's would declare their Ball again
        foreach (var name in new[] { "Schema.tidegraph", "Layout.tidegraph", "Members.tidegraph", "Types.tidegraph" })
        {
            try
            {
                File.Delete(Path.Combine(Game, name));
            }
            catch (IOException)
            {
                // Read again by the next test's server, which says so
            }
        }
    }

    static void NeedTidels()
    {
        if (tidels == null) Assert.Ignore("No tidels: build this checkout's (cmake --build --preset sanitize --target tidels) or set TIDE_TIDELS");
    }

    // A game of its own in Temp: its tide.packages makes the folder a game, which tidels reads the graph with
    static string GameIn(string folder)
    {
        folder = Path.Combine(Path.GetFullPath("Temp"), folder);
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "tide.packages"), "# The graph editor's language tests' game\n");
        return folder;
    }

    static string Game => GameIn("GraphLanguageTests");

    // A ball with a field and a method, and a function that calls what isn't there, returns the ball's hits, and
    // has the ball's name a second time with nothing reading it
    const string BallGraph =
        "{\"tidegraph\": 1, \"nextId\": 60, \"decls\": [\n" +
        "  {\"kind\": \"scene\", \"id\": 1, \"name\": \"Main\", \"members\": []},\n" +
        "  {\"kind\": \"struct\", \"id\": 2, \"name\": \"Ball\", \"members\": [\n" +
        "    {\"kind\": \"field\", \"id\": 3, \"type\": \"int\", \"name\": \"hits\"},\n" +
        "    {\"kind\": \"method\", \"id\": 4, \"name\": \"Bounce\", \"returnType\": \"void\", \"params\": [{\"id\": 5, \"mode\": \"read\", \"type\": \"float\", \"name\": \"height\"}], \"body\": {\"entry\": null, \"nodes\": []}}\n" +
        "  ]},\n" +
        "  {\"kind\": \"function\", \"id\": 10, \"name\": \"Hit\", \"returnType\": \"int\", \"params\": [{\"id\": 11, \"mode\": \"read\", \"type\": \"Ball\", \"name\": \"ball\"}],\n" +
        "   \"body\": {\"entry\": 12, \"nodes\": [\n" +
        "    {\"id\": 12, \"k\": \"eval\", \"x\": 0, \"y\": 0, \"value\": 13, \"next\": 15},\n" +
        "    {\"id\": 13, \"k\": \"call\", \"x\": -220, \"y\": 60, \"name\": \"Missing\"},\n" +
        "    {\"id\": 15, \"k\": \"return\", \"x\": 300, \"y\": 0, \"value\": 16},\n" +
        "    {\"id\": 16, \"k\": \"member\", \"x\": 100, \"y\": 80, \"object\": 17, \"member\": \"hits\"},\n" +
        "    {\"id\": 17, \"k\": \"name\", \"x\": -60, \"y\": 80, \"name\": \"ball\"},\n" +
        "    {\"id\": 18, \"k\": \"name\", \"x\": 0, \"y\": 300, \"name\": \"ball\"}\n" +
        "  ]}}\n" +
        "]}\n";

    static string WriteGame(string name, string text)
    {
        var path = Path.Combine(Game, name);
        File.WriteAllText(path, text, new UTF8Encoding(false));
        return path;
    }

    // Hit's code: its frame, on screen
    static GraphFrame Hit(TideGraphWindow window)
    {
        window.Reveal(10);
        var frame = window.Board.Frame(10);
        Assert.IsNotNull(frame);
        Assert.AreEqual(GraphLevel.Near, frame.Level);
        return frame;
    }

    static IEnumerator Checked(TideGraphWindow window)
    {
        for (var clock = Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 90;) yield return null;
        Assert.IsFalse(window.CheckPending, "the check finishes");
        Assert.IsNotNull(window.Checked);
        Assert.AreEqual("tidels", window.Checked.Checker, "tidels checks it: " + window.Checked.CheckerNote + " " + TideLanguageClient.Shared?.Errors);
    }

    // The check of the file as it is now: one after the edit just made
    static IEnumerator CheckedAgain(TideGraphWindow window)
    {
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        Assert.AreEqual(window.Doc.Text, window.Checked.Json, "the check is of the file as it is");
    }

    static IEnumerator Answered(GraphSearchPopup popup)
    {
        for (var clock = Stopwatch.StartNew(); popup.Asking && clock.Elapsed.TotalSeconds < 90;) yield return null;
        Assert.IsFalse(popup.Asking, "tidels answers the search");
    }

    static IEnumerator Asked(GraphTypePicker picker)
    {
        for (var clock = Stopwatch.StartNew(); !picker.Asked && clock.Elapsed.TotalSeconds < 90;) yield return null;
        Assert.IsTrue(picker.Asked, "tidels says what's valid there");
    }

    static IEnumerator Until(Func<bool> done, string what)
    {
        for (var clock = Stopwatch.StartNew(); !done() && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsTrue(done(), what);
    }

    static string Titles(GraphSearchPopup popup) => string.Join(", ", popup.Entries.Select(e => $"{e.Title} [{e.Group}]"));
    static string Titles(GraphOptionList list) => string.Join(", ", list.Shown.Select(o => $"{o.Title} [{o.Section}]"));

    sealed class Logged : IDisposable
    {
        readonly List<string> said = new List<string>();

        public Logged()
        {
            LogAssert.ignoreFailingMessages = true;
            Application.logMessageReceived += Log;
        }

        void Log(string message, string stack, LogType type)
        {
            if (type == LogType.Log) return;
            if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null && message.StartsWith("No graphic device is available")) return;
            said.Add($"{type}: {message}\n{stack}");
        }

        public void Dispose()
        {
            Application.logMessageReceived -= Log;
            Assert.IsEmpty(said, "Logged:\n" + string.Join("\n", said));
        }
    }

    [UnityTest]
    public IEnumerator TidelsChecksTheGraphWithTypes()
    {
        NeedTidels();
        var path = WriteGame("Ball.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var frame = Hit(window);

        // The unknown call's error, type-checked, on its node: said under the statement it's drawn with
        var error = window.Checked.Diagnostics.FirstOrDefault(d => d.Id == 13);
        Assert.IsNotNull(error, "tidels names the call: " + string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
        Assert.AreEqual("error", error.Severity);
        StringAssert.Contains("Missing", error.Message);
        StringAssert.Contains("Missing()", error.Detail, "with its line of Tide");
        StringAssert.Contains("Missing", frame.Views[12].ProblemText);
        StringAssert.Contains("tidels", window.CheckerSays);
        StringAssert.Contains("problem", window.ChipText);
        StringAssert.Contains("return ball.hits;", window.Checked.Text.Replace("\r\n", "\n"));
        // A click on it in the chip's list selects its node
        var listed = window.ProblemOptions().First(o => o.Title.Contains("Missing"));
        StringAssert.Contains("Hit", listed.Detail, "the list says where it is");
        listed.Pick();
        Assert.IsTrue(window.Board.Nodes.Contains(12));

        // What reads the parameter isn't a node: its wire comes from the parameter's pin, of the parameter's type
        Assert.IsFalse(frame.Views.ContainsKey(17));
        Assert.IsTrue(frame.Hidden.Contains(17));
        var pin = frame.Entry.ReadPin("ball");
        Assert.AreSame(pin, frame.SourcePin(17));
        Assert.AreEqual("Ball", pin.Type);
        // ...and one nothing reads is a small node, faded
        Assert.IsTrue(frame.Views[18].ClassListContains("tg-node--unreached"));

        // Each value's type: on the node and its pin, and on the input it's wired to
        Assert.AreEqual("int", window.Checked.Types[16]);
        Assert.AreEqual("Ball", window.Checked.Types[17]);
        var member = frame.Views[16];
        Assert.AreEqual("int", member.TypeText);
        Assert.AreEqual("int", member.DataOut.Type);
        Assert.AreEqual("Ball", member.Input("object").Type, "what it's of");
        Assert.AreNotEqual(TideGraphTypes.Untyped, member.DataOut.Tint);
        // ...and what tidels says of it
        string hover = null;
        TideGraphLanguage.Hover(window, 16, text => hover = text);
        for (var clock = Stopwatch.StartNew(); hover == null && clock.Elapsed.TotalSeconds < 30;) yield return null;
        StringAssert.Contains("int hits", hover);
        member.SetHover(hover);
        StringAssert.Contains("int hits", member.Q(className: "tg-node-head").tooltip);
        window.Close();
    }

    [UnityTest]
    public IEnumerator TheSearchOffersWhatCompletionDoes()
    {
        NeedTidels();
        var path = WriteGame("Search.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var board = window.Board;
        var frame = Hit(window);

        // A wire from the ball dropped on nothing: its fields and its method, with the method's parameter
        var popup = board.OpenSearch(frame, new Vector2(500, 300), frame.Views[18].DataOut.Port);
        yield return Answered(popup);
        Assert.IsTrue(popup.Entries.Any(e => e.Title == "hits" && e.Group == "Members of Ball"), Titles(popup));
        var bounce = popup.Entries.FirstOrDefault(e => e.Title == "Bounce(height)");
        Assert.IsNotNull(bounce, Titles(popup));
        Assert.AreEqual("Members of Ball", popup.Entries[0].Group, "the value's members come first");
        Assert.AreEqual(window.Doc.Text, window.Checked.Json, "asking leaves the graph as it was");

        // The method, made from it: wired to the ball, with an input for its parameter
        Assert.IsTrue(popup.Pick("Bounce(height)"));
        Assert.IsNull(window.Popup);
        var graph = frame.Graph;
        var method = GraphDocument.Nodes(graph).Items.Last(n => n.Str("k") == "method");
        Assert.AreEqual("Bounce", method.Str("name"));
        Assert.AreEqual(18, method.Wire("object"));
        Assert.AreEqual(1, method.Arr("args").Items.Count);
        // Its height typed on its input, and called as a statement before the others: what it lowers to
        frame.TypeInto(method, new WirePath("args", 0), "2.5", frame.Views[method.Wire("id")]);
        window.Edit("Call it", () =>
        {
            var eval = window.Doc.AddNode(graph, "eval", 0, 200);
            eval.Set("value", JValue.Of(method.Wire("id")));
            eval.Set("next", JValue.Of(graph.Wire("entry")));
            graph.Set("entry", JValue.Of(eval.Wire("id")));
        }, frame.Id);
        yield return CheckedAgain(window);
        StringAssert.Contains("ball.Bounce(2.5);\n    Missing();", window.Checked.Text.Replace("\r\n", "\n"));
        Assert.IsFalse(window.Checked.Diagnostics.Any(d => d.Severity == "error" && d.Id != 13), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));

        // A member node picks its member from what its value has
        popup = board.OpenMemberSearch(frame, 16, frame.Views[16]);
        yield return Answered(popup);
        Assert.IsTrue(popup.Entries.All(e => e.Group == "Members of Ball"), Titles(popup));
        Assert.IsTrue(popup.Pick("Bounce(height)"));
        var retargeted = GraphDocument.NodeIn(frame.Graph, 16);
        Assert.AreEqual("method", retargeted.Str("k"));
        Assert.AreEqual("Bounce", retargeted.Str("name"));
        Assert.AreEqual(17, retargeted.Wire("object"));
        Undo.PerformUndo();
        frame = Hit(window);
        Assert.AreEqual("member", GraphDocument.NodeIn(frame.Graph, 16).Str("k"));

        // Where a step goes: the game's functions as calls, its types as values of them, the parameter once
        popup = board.OpenSearch(frame, new Vector2(400, 200), null);
        yield return Answered(popup);
        Assert.IsTrue(popup.Entries.Any(e => e.Title == "Hit(ball)" && e.Statement), Titles(popup));
        Assert.IsTrue(popup.Entries.Any(e => e.Title == "Ball { }"), Titles(popup));
        Assert.IsTrue(popup.Entries.Any(e => e.Title == "Math" && e.Group == "Namespaces"), Titles(popup));
        Assert.AreEqual(1, popup.Entries.Count(e => e.Title == "ball"), "the parameter, once: " + Titles(popup));
        // What's typed is a value in itself
        popup.Type("42");
        Assert.AreEqual("42", popup.Entries[0].Title);
        Assert.AreEqual("A value", popup.Entries[0].Group);
        // An arrow takes the call as a step, and no value
        popup = board.OpenSearch(frame, new Vector2(400, 200), frame.Entry.Ports.First(p => p.Port.Exec).Port);
        yield return Answered(popup);
        Assert.IsTrue(popup.Entries.Any(e => e.Title == "Hit(ball)"), Titles(popup));
        Assert.IsFalse(popup.Entries.Any(e => e.Title == "Ball { }"), "an arrow takes no value");
        window.ClosePopup(popup);

        // A wire from the parameter's own pin, which no node reads there yet: what the ball has, and the file as it was
        var text = window.Doc.Text;
        popup = board.OpenSearch(frame, new Vector2(500, 400), frame.Entry.ReadPin("ball").Port);
        yield return Answered(popup);
        Assert.AreEqual("hits", popup.Entries[0].Title, Titles(popup));
        Assert.AreEqual("Members of Ball", popup.Entries[0].Group);
        Assert.IsFalse(popup.Entries.Any(e => e.Reads), "not the parameter again");
        Assert.AreEqual(text, window.Doc.Text, "asking changes nothing");
        Assert.AreEqual(text, TideLanguageClient.Shared.TextOf(TideLanguageClient.UriOf(path)), "and tidels has the file as it is again");
        Assert.IsTrue(popup.Pick("hits"));
        var hits = GraphDocument.Nodes(frame.Graph).Items.Last(n => n.Str("k") == "member");
        var read = GraphDocument.NodeIn(frame.Graph, hits.Wire("object"));
        Assert.AreEqual("name", read.Str("k"));
        Assert.AreEqual("ball", read.Str("name"));
        Assert.IsTrue(frame.Hidden.Contains(read.Wire("id")), "the read is a wire from the pin, not a node");
        window.Close();
    }

    // A type is picked from what's valid at its place: what completion lists there in the graph's Tide
    [UnityTest]
    public IEnumerator TypesArePickedFromWhatsValidThere()
    {
        NeedTidels();
        // A game of its own: its scene and its types are no other test's
        var path = Path.Combine(GameIn("GraphTypes"), "Types.tidegraph");
        File.WriteAllText(path,
            "{\"tidegraph\": 1, \"nextId\": 60, \"decls\": [\n" +
            "  {\"kind\": \"scene\", \"id\": 1, \"name\": \"Main\", \"members\": []},\n" +
            "  {\"kind\": \"enum\", \"id\": 40, \"name\": \"Mood\", \"members\": [{\"id\": 41, \"name\": \"Calm\"}, {\"id\": 42, \"name\": \"Wild\"}]},\n" +
            "  {\"kind\": \"component\", \"id\": 2, \"name\": \"Wobble\", \"members\": [\n" +
            "    {\"kind\": \"field\", \"id\": 3, \"type\": \"float\", \"name\": \"frequency\"},\n" +
            "    {\"kind\": \"field\", \"id\": 4, \"type\": \"Mood\", \"name\": \"mood\"}\n" +
            "  ]},\n" +
            "  {\"kind\": \"event\", \"id\": 6, \"name\": \"Shaken\", \"members\": [{\"kind\": \"field\", \"id\": 7, \"type\": \"int\", \"name\": \"times\"}]},\n" +
            "  {\"kind\": \"struct\", \"id\": 30, \"name\": \"Pair\", \"typeParams\": [\"T\"], \"members\": [\n" +
            "    {\"kind\": \"field\", \"id\": 31, \"type\": \"T\", \"name\": \"a\"}\n" +
            "  ]},\n" +
            "  {\"kind\": \"system\", \"id\": 10, \"name\": \"Shake\", \"params\": [{\"id\": 11, \"mode\": \"mut\", \"type\": \"Wobble\", \"name\": \"wobble\"}],\n" +
            "   \"body\": {\"entry\": null, \"nodes\": []}},\n" +
            "  {\"kind\": \"handler\", \"id\": 20, \"name\": \"OnShaken\", \"trigger\": {\"type\": \"Shaken\", \"name\": \"shaken\"}, \"params\": [],\n" +
            "   \"body\": {\"entry\": null, \"nodes\": []}}\n" +
            "]}\n", new UTF8Encoding(false));
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var board = window.Board;

        // A field's type: the common ones first, then the game's own (the enum, the component, the struct)
        window.Reveal(2);
        GraphShots.Click(board.Card(2).Q<GraphTypeChip>("type3"));
        var picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsTrue(picker.FromServer, "what's listed is what tidels says is valid there");
        CollectionAssert.AreEqual(GraphSchema.TypeCommon, picker.Shown.Take(GraphSchema.TypeCommon.Length).Select(o => o.Title).ToArray(), Titles(picker));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Mood" && o.Section == GraphSchema.TypeGroup("file")), Titles(picker));
        Assert.IsFalse(picker.Shown.Any(o => o.Title == "Shaken"), "an event isn't a field's type, and isn't offered: " + Titles(picker));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Pair of…"), "a generic struct asks what it's of: " + Titles(picker));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "List of…" && o.Section == GraphSchema.TypeGroup("builders")), Titles(picker));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Random" && o.Section == GraphSchema.TypeGroup("game")), "the engine's own, under the game's: " + Titles(picker));
        Assert.IsFalse(picker.Shown.Any(o => o.Title == "Wobble" || o.Title == "Transform"), "a component isn't a field's type, and isn't offered");
        Assert.IsFalse(picker.Shown.Any(o => o.Title == "List" || o.Title == "void" || o.Title == "mut"), "nothing that isn't a type one can pick: " + Titles(picker));
        // Typing completes: the first match is taken by Enter
        picker.Type("mo");
        Assert.AreEqual("Mood", picker.Shown[0].Title);
        picker.Type("pair");
        picker.PickCurrent();
        StringAssert.Contains("Pair", picker.Title);
        Assert.IsTrue(picker.Choose("int"));
        Assert.AreEqual("Pair<int>", window.Doc.Get(3).Str("type"));
        Undo.PerformUndo();

        // In a generic struct, its own parameter is a type
        window.Reveal(30);
        GraphShots.Click(board.Card(30).Q<GraphTypeChip>("type31"));
        picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "T" && o.Section == GraphSchema.TypeGroup("own")), Titles(picker));
        window.ClosePopup(picker);

        // What a system needs: components and the things there's one of, the file's own first; no numbers
        window.Reveal(10);
        var frame = board.Frame(10);
        var needs = (GraphOptionList)frame.OpenNeeds(frame.Entry.Q<Button>("needs"), window.Doc.Get(10).Arr("params"), false);
        yield return Until(() => string.IsNullOrEmpty(needs.Status), "tidels says what a system can take");
        Assert.IsFalse(needs.Shown.Any(o => o.Title == "int" || o.Title == "Mood" || o.Title == "Pair"), "a system takes components, not values: " + Titles(needs));
        Assert.IsFalse(needs.Shown.Any(o => o.Title == "Wobble"), "what it takes already isn't offered again");
        Assert.IsTrue(needs.Shown.Any(o => o.Title == "Transform" && o.Section == "A component each entity has"), Titles(needs));
        Assert.IsTrue(needs.Shown.Any(o => o.Title == "Time" && o.Section == "One of a kind"), Titles(needs));
        // A filter: which component, next
        Assert.IsTrue(needs.Pick(GraphLook.UpperFirst(GraphSchema.ParamModeInfo("without").Label) + "…"));
        Assert.AreSame(needs, window.Popup);
        Assert.IsTrue(needs.Shown.All(o => o.Section.StartsWith("Without")), Titles(needs));
        Assert.IsTrue(needs.Pick("Transform"));
        var filter = window.Doc.Get(10).Arr("params").Items.Last();
        Assert.AreEqual("without", filter.Str("mode"));
        Assert.AreEqual("Transform", filter.Str("type"));
        Assert.IsFalse(filter.Has("name"), "a filter has no name");
        frame = board.Frame(10);
        Assert.IsNull(frame.Entry.ReadPin("transform"), "and no pin");

        // How a parameter is taken: the ways a system has, by their labels
        var modes = frame.OpenModes(frame.Entry.Q<Button>("mode11"), window.Doc.Get(10).Arr("params"), window.Doc.Get(11));
        yield return Until(() => !modes.Shown.Any(o => (string)o.Data == "out"), "tidels says which ways a system has");
        CollectionAssert.Contains(modes.Shown.Select(o => o.Title).ToList(), GraphSchema.ParamModeInfo("read").Label);
        CollectionAssert.Contains(modes.Shown.Select(o => o.Title).ToList(), GraphSchema.ParamModeInfo("with").Label);
        Assert.IsFalse(modes.Shown.Any(o => (string)o.Data == "in" || (string)o.Data == "out"), "not a function's: " + Titles(modes));
        Assert.IsTrue(modes.Shown.First(o => (string)o.Data == "mut").Checked == true);
        window.ClosePopup(modes);

        // A handler's event: events only
        window.Reveal(20);
        GraphShots.Click(board.Frame(20).Entry.Q<GraphTypeChip>("event"));
        picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Shaken" && o.Section == GraphSchema.TypeGroup("file")), Titles(picker));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Spawned"), "and the engine's own: " + Titles(picker));
        Assert.IsFalse(picker.Shown.Any(o => o.Title == "Wobble" || o.Title == "int" || o.Title == "List of…"), "events only: " + Titles(picker));
        window.ClosePopup(picker);

        // A type that isn't one (a file written by hand: the board can't make it) is said on its row, in the
        // compiler's words, and gone from everywhere once one that is one is picked
        window.Edit("A wrong type", () => window.Doc.Get(3).Set("type", JValue.Of("Flaot")), 2);
        yield return CheckedAgain(window);
        var wrong = window.Checked.Diagnostics.FirstOrDefault(d => d.Id == 3);
        Assert.IsNotNull(wrong, "tidels names the field: " + string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
        window.Reveal(2);
        var row = board.Card(2).Q("row3");
        Assert.IsTrue(row.ClassListContains("tg-row--problem"), "its row is marked");
        Assert.IsTrue(board.Card(2).Query<Label>(className: "tg-problem").ToList().Any(l => l.text.Contains("Flaot")), "and says what's wrong: " + string.Join(" | ", board.ProblemsShown()));
        Assert.IsTrue(board.Card(2).ClassListContains("tg-has-problem"));
        var name = board.Card(2).Q<TextField>("name4");
        name.Focus();
        GraphShots.Click(board.Card(2).Q<GraphTypeChip>("type3"));
        picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsFalse(picker.Shown.Any(o => o.Title == "Flaot"), "what isn't a type isn't offered");
        Assert.IsTrue(picker.Choose("float"));
        var same = board.Card(2).Q("row4");
        yield return CheckedAgain(window);
        Assert.IsFalse(window.Checked.Diagnostics.Any(d => d.Id == 3));
        Assert.IsFalse(board.Card(2).Q("row3").ClassListContains("tg-row--problem"), "the mark goes with its cause");
        Assert.IsFalse(board.ProblemsShown().Any(t => t.Contains("Flaot")), "and the message, everywhere: " + string.Join(" | ", board.ProblemsShown()));
        Assert.IsFalse(board.Card(2).ClassListContains("tg-has-problem"));
        Assert.AreSame(same, board.Card(2).Q("row4"), "a check builds nothing again: what's being typed isn't lost");

        // An enum field's starting value: its choices
        window.Reveal(2);
        GraphShots.Click(board.Card(2).Q("row4").Q(className: "tg-row-value").Q<Button>());
        var choices = (GraphOptionList)window.Popup;
        CollectionAssert.AreEqual(new[] { "Calm", "Wild" }, choices.Shown.Where(o => o.Section == "Mood").Select(o => o.Title));
        Assert.IsTrue(choices.Pick("Wild"));
        yield return CheckedAgain(window);
        StringAssert.Contains("Mood mood = Mood.Wild;", window.Checked.Text);
        Assert.IsFalse(window.Checked.Diagnostics.Any(d => d.Severity == "error"), string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        window.Close();
    }

    [UnityTest]
    public IEnumerator TidelsStartsAgainAndStopsWithTheLastWindow()
    {
        NeedTidels();
        var path = WriteGame("Restart.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var client = TideLanguageClient.Shared;
        Assert.IsNotNull(client);
        var first = client.ProcessId;
        Assert.AreNotEqual(0, first);
        var starts = client.Starts;

        // Killed: it starts again, and checks as before
        using (var p = Process.GetProcessById(first)) p.Kill();
        for (var clock = Stopwatch.StartNew(); (client.ProcessId == 0 || client.ProcessId == first) && clock.Elapsed.TotalSeconds < 30;) yield return null;
        Assert.AreEqual(starts + 1, client.Starts, "started again");
        Assert.AreNotEqual(first, client.ProcessId);
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        Assert.IsTrue(window.Checked.Diagnostics.Any(d => d.Id == 13), "the restarted server has the graph");

        // The last window closed: no tidels left
        var second = client.ProcessId;
        window.Close();
        Assert.IsNull(TideLanguageClient.Shared, "no window, no server: " + client.OpenDocuments);
        var gone = false;
        for (var clock = Stopwatch.StartNew(); !gone && clock.Elapsed.TotalSeconds < 15;)
        {
            try
            {
                using (var p = Process.GetProcessById(second)) gone = p.HasExited;
            }
            catch (ArgumentException)
            {
                gone = true;
            }
            if (!gone) yield return null;
        }
        Assert.IsTrue(gone, "tidels is shut down");
    }

    // The schema tidels serves (tide/graphSchema) is the one the editor reads
    [UnityTest]
    public IEnumerator TidelsServesTheSchemaTheEditorReads()
    {
        NeedTidels();
        var path = WriteGame("Schema.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        JValue served = null;
        string why = null;
        var done = false;
        TideLanguageClient.Shared.Request("tide/graphSchema", JValue.Null(), (r, w) =>
        {
            served = r;
            why = w;
            done = true;
        });
        yield return Until(() => done, "tidels answers");
        Assert.IsNull(why);
        Assert.AreEqual(TideLanguageClient.Compact(served), TideLanguageClient.Compact(GraphSchema.Json));
        window.Close();
    }

    // Tidying up is tidels's (tide/graphLayout), tidec's own, whatever it is: a frame's statements along next and
    // their values below them; the whole board, data to the left of code. Without tidels nothing moves.
    [UnityTest]
    public IEnumerator TidyingUpIsTidecsFromTidels()
    {
        NeedTidels();
        var path = WriteGame("Layout.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var board = window.Board;
        var frame = Hit(window);
        double X(int id) => window.Doc.Get(id).Get("x").Number;
        double Y(int id) => window.Doc.Get(id).Get("y").Number;
        var text = window.Checked.Text;

        board.Tidy(frame);
        yield return Until(() => !board.LayoutPending, "tidels lays it out");
        Assert.Less(X(12), X(15), "statements left to right along next");
        Assert.Greater(Y(13), Y(12), "a statement's value below its row");
        Assert.Greater(Y(16), Y(15), "the return's value below its row");
        Assert.Greater(Y(18), Y(16), "what nothing reads on a row below");
        // tidels's layout of the body, as it gives it
        JValue layout = null;
        TideGraphLanguage.Layout(board.Frame(10).Graph, (l, _) => layout = l);
        yield return Until(() => layout != null, "tidels gives the layout");
        foreach (var id in new[] { 12, 13, 15, 16, 17, 18 })
        {
            Assert.AreEqual(layout.Arr(id.ToString()).Items[0].Number, X(id), $"@{id}'s x");
            Assert.AreEqual(layout.Arr(id.ToString()).Items[1].Number, Y(id), $"@{id}'s y");
        }
        yield return CheckedAgain(window);
        Assert.AreEqual(text, window.Checked.Text, "the layout moves nodes, nothing else");
        Undo.PerformUndo();
        Assert.AreEqual(300, X(15), "undone");

        // The whole board: each declaration, and each member with code, where tidec's board layout puts it
        Assert.IsFalse(GraphDocument.HasPlace(window.Doc.Get(2)), "this file says no places");
        var shown = board.Card(2).At;
        var before = window.Doc.Text;
        Assert.AreEqual(new Vector2(GraphLook.CardWidth + GraphLook.LaneGap, 0), board.Frame(4).At, "the board works them out: code to the right of data");
        Assert.AreEqual(before, window.Doc.Text, "and writes nothing by looking");
        JValue places = null;
        TideGraphLanguage.LayoutBoard(window.Doc.Root, (l, _) => places = l);
        yield return Until(() => places != null, "tidels gives the board's layout");
        board.TidyBoard();
        yield return Until(() => !board.LayoutPending, "tidels lays the board out");
        foreach (var id in new[] { 1, 2, 4, 10 })
        {
            Assert.AreEqual(places.Arr(id.ToString()).Items[0].Number, X(id), $"@{id}'s x");
            Assert.AreEqual(places.Arr(id.ToString()).Items[1].Number, Y(id), $"@{id}'s y");
        }
        Assert.Less(X(2), X(10), "data to the left of code");
        CollectionAssert.AreEqual(new[] { "kind", "id", "x", "y", "name" }, window.Doc.Get(2).Keys.Take(5), "x and y where tidec writes them");
        yield return CheckedAgain(window);
        Assert.AreEqual(text, window.Checked.Text, "and nothing else changes");
        Undo.PerformUndo();
        Assert.IsFalse(GraphDocument.HasPlace(window.Doc.Get(2)), "undone");

        // Without tidels: nothing moves
        TideGraphCommand.TidelsOverride = Path.Combine(Game, "not-tidels.exe");
        try
        {
            board.Tidy(Hit(window));
            Assert.IsFalse(board.LayoutPending);
            Assert.AreEqual(300, X(15));
            StringAssert.Contains("stay where they are", window.LastSaid);
        }
        finally
        {
            TideGraphCommand.TidelsOverride = tidels;
        }
        window.Close();
    }

    // What a value has is tidels's (tide/graphMembers), a value something reads or one nothing reads: a struct's
    // field, and its method, which the search makes a method node with an input for its parameter
    [UnityTest]
    public IEnumerator MembersOfAValueAreTidelss()
    {
        NeedTidels();
        var path = WriteGame("Members.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        yield return Checked(window);
        var frame = Hit(window);
        var client = TideLanguageClient.Shared;
        var uri = TideLanguageClient.UriOf(path);
        foreach (var node in new[] { 17, 18 })
        {
            JValue items = null;
            var done = false;
            var p = JValue.NewObject();
            p.Set("node", JValue.Of(node));
            client.Ask("tide/graphMembers", uri, p, (r, _) =>
            {
                items = r;
                done = true;
            });
            yield return Until(() => done, "tidels answers");
            var labels = items?.Arr("items")?.Items.Select(i => i.Str("label")).ToList() ?? new List<string>();
            CollectionAssert.Contains(labels, "hits", $"@{node}: " + string.Join(", ", labels));
            CollectionAssert.Contains(labels, "Bounce", $"@{node}: " + string.Join(", ", labels));
        }

        // As the search has them
        List<GraphSearchEntry> found = null;
        var context = new GraphSearchContext { Doc = window.Doc, Graph = frame.Graph, Slot = frame.Slot, Window = window, Frame = frame, Pending = frame.Views[18].DataOut.Port };
        TideGraphLanguage.Complete(context, (e, _) => found = e ?? new List<GraphSearchEntry>());
        yield return Until(() => found != null, "the search is answered");
        var hits = found.FirstOrDefault(e => e.Title == "hits");
        Assert.IsNotNull(hits, string.Join(", ", found.Select(e => e.Title)));
        Assert.AreEqual("Members of Ball", hits.Group);
        var bounce = found.FirstOrDefault(e => e.Title == "Bounce(height)");
        Assert.IsNotNull(bounce, string.Join(", ", found.Select(e => e.Title)));
        Assert.AreEqual(window.Doc.Text, window.Checked.Json, "asking leaves the graph as it was");
        var scratch = GraphDocument.Parse(window.Doc.Text);
        var made = bounce.Make(scratch, scratch.GetGraph(frame.Slot), Vector2.zero);
        Assert.AreEqual("method", made.Str("k"));
        Assert.AreEqual("Bounce", made.Str("name"));
        Assert.AreEqual(1, made.Arr("args").Items.Count);
        var field = hits.Make(scratch, scratch.GetGraph(frame.Slot), Vector2.zero);
        Assert.AreEqual("member", field.Str("k"));
        Assert.AreEqual("hits", field.Str("member"));

        // What a type has, by its name: an enum's choices would come this way
        List<TideGraphLanguage.Member> members = null;
        TideGraphLanguage.MembersOfType(window, "Ball", (m, _) => members = m ?? new List<TideGraphLanguage.Member>());
        yield return Until(() => members != null, "tidels says what Ball has");
        Assert.AreEqual(window.Doc.Text, client.TextOf(uri), "and has the file as it is again");
        window.Close();
    }

    // A graph in the project's Assets is read with the game Unity's Tide builds, and a build's error on a graph
    // opens its window on the node it names
    [UnityTest]
    public IEnumerator AGraphInAssetsIsTheGamesAndBuildErrorsOpenItsNode()
    {
        NeedTidels();
        const string folder = "Assets/GraphLanguageTestFiles";
        if (!AssetDatabase.IsValidFolder(folder)) AssetDatabase.CreateFolder("Assets", "GraphLanguageTestFiles");
        try
        {
            var path = folder + "/Hook.tidegraph";
            File.WriteAllText(path,
                "{\"tidegraph\": 1, \"nextId\": 30, \"decls\": [\n" +
                "  {\"kind\": \"function\", \"id\": 10, \"name\": \"GraphHook\", \"returnType\": \"void\", \"params\": [],\n" +
                "   \"body\": {\"entry\": 12, \"nodes\": [\n" +
                "    {\"id\": 12, \"k\": \"eval\", \"x\": 0, \"y\": 0, \"value\": 13},\n" +
                "    {\"id\": 13, \"k\": \"call\", \"x\": -220, \"y\": 60, \"name\": \"NotThere\"}\n" +
                "  ]}}\n" +
                "]}\n", new UTF8Encoding(false));
            AssetDatabase.ImportAsset(path);
            using var logged = new Logged();

            // The Console's message for its node, double-clicked
            Assert.IsTrue(TideBuildService.Locate(path + ":@13: error: unknown function 'NotThere'", out var file, out var line, out _));
            Assert.AreEqual(path, file);
            Assert.AreEqual(13, line, "logged at its node");
            Assert.IsTrue(AssetDatabase.OpenAsset(AssetDatabase.LoadMainAssetAtPath(path), line));
            var window = Resources.FindObjectsOfTypeAll<TideGraphWindow>().Single();
            Assert.AreEqual(13, window.SelectedElement);
            Assert.IsTrue(window.Board.Nodes.Contains(12), "its node, drawn with its statement");
            Assert.AreEqual(10, window.Board.Active.Id, "in its frame");

            // Checked with the Sample's game: its event is a value the search offers, its commands are calls
            yield return Checked(window);
            Assert.IsTrue(window.Checked.Diagnostics.Any(d => d.Id == 13 && d.Message.Contains("NotThere")), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
            var frame = window.Board.Frame(10);
            var popup = window.Board.OpenSearch(frame, new Vector2(400, 200), null);
            yield return Answered(popup);
            Assert.IsTrue(popup.Entries.Any(e => e.Title == "Bounced { }"), Titles(popup));
            Assert.IsTrue(popup.Entries.Any(e => e.Title == "Toss(on)"), Titles(popup));
            window.ClosePopup(popup);
            window.Close();
        }
        finally
        {
            AssetDatabase.DeleteAsset(folder);
        }
    }

    [Test]
    public void SignaturesGiveTheirParametersNames()
    {
        CollectionAssert.AreEqual(new[] { "height", "times" }, TideGraphLanguage.Parameters("void Bounce(float height, int times = 1)", "Bounce"));
        CollectionAssert.AreEqual(new[] { "a", "b" }, TideGraphLanguage.Parameters("T Max<T>(T a, T b)", "Max"));
        CollectionAssert.AreEqual(new[] { "components" }, TideGraphLanguage.Parameters("Spawn(components...) -> Entity", "Spawn"));
        CollectionAssert.AreEqual(new[] { "m" }, TideGraphLanguage.Parameters("void Put(Map<int, float> m)", "Put"));
        CollectionAssert.IsEmpty(TideGraphLanguage.Parameters("void Run()", "Run"));
        if (Application.platform == RuntimePlatform.WindowsEditor) Assert.AreEqual("file:///C:/a%20b/c.tidegraph", TideLanguageClient.UriOf("C:/a b/c.tidegraph"));
        Assert.IsTrue(TideLanguageClient.SameUri("file:///C:/a%20b/c.tidegraph", "file:///C:/a b/c.tidegraph"));
    }

    // ---------------------------------------------------------------------------------------------------------
    // The story: what a first-time user does, through what the board's own clicks and keys call. With
    // TIDE_GRAPH_SHOTS set, and a graphics device, each step is a picture too.

    [UnityTest]
    public IEnumerator TheStoryFromAnEmptyFile() => Story(false);

    [UnityTest]
    public IEnumerator TheStoryInTheLightTheme() => Story(true);

    IEnumerator Story(bool light)
    {
        NeedTidels();
        // A game of its own for each telling: what tidels knows of one isn't the other's
        var game = GameIn(light ? "GraphStoryLight" : "GraphStory");
        foreach (var old in Directory.GetFiles(game, "*.tidegraph")) File.Delete(old);
        // The game has a scene already, as a game does: the file being made is the first of its code
        File.WriteAllText(Path.Combine(game, "main.tide"), "scene Main {\n}\n");
        var path = Path.Combine(game, "Story.tidegraph");
        File.WriteAllText(path, TideGraphImporter.EmptyText, new UTF8Encoding(false));
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        yield return Checked(window);
        var board = window.Board;

        // 1. An empty file says what can be made
        Assert.AreEqual("What do you want to make?", board.Q<Label>(className: "tg-start-title").text);
        Assert.AreEqual("Checked", window.ChipText);
        yield return GraphShots.Take(window, "01-empty-file");
        board.OpenAdd(null, window.rootVisualElement.Q<Button>("add"));
        yield return GraphShots.Take(window, "02-add-menu");
        ((GraphOptionList)window.Popup).Type("ev");
        yield return GraphShots.Take(window, "03-add-menu-searched");
        window.ClosePopup(window.Popup);

        // 2. A component, from the start's first choice: its name is typed, then Enter goes on to its first field
        GraphShots.Click(board.Q<Button>("start:component"));
        var wobble = window.Doc.Decls.Items.Single().Wire("id");
        Assert.AreEqual(wobble, board.Fresh);
        Assert.IsNull(board.Q<Label>(className: "tg-start-title"), "the start goes");
        yield return GraphShots.Take(window, "04-new-component");
        var name = board.Card(wobble).Q<TextField>("name");
        name.SetValueWithoutNotify("Wobble");
        GraphShots.Key(name, KeyCode.Return);
        var decl = window.Doc.Get(wobble);
        Assert.AreEqual("Wobble", decl.Str("name"));
        var frequency = decl.Arr("members").Items.Single();
        Assert.AreEqual(frequency.Wire("id"), board.Fresh, "Enter on a new card's name starts its first field");
        yield return GraphShots.Take(window, "05-first-field");
        name = board.Card(wobble).Q<TextField>("name" + frequency.Wire("id"));
        GraphShots.Enter(name, "frequency");
        // Its type, picked: the list completes as it's typed
        GraphShots.Click(board.Card(wobble).Q<GraphTypeChip>("type" + frequency.Wire("id")));
        var picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsTrue(picker.FromServer);
        yield return GraphShots.Take(window, "06-type-picker");
        picker.Type("fl");
        Assert.AreEqual("float", picker.Shown[0].Title);
        yield return GraphShots.Take(window, "07-type-picker-typed");
        Assert.IsTrue(picker.PickCurrent());
        Assert.AreEqual("float", frequency.Str("type"), "the first match is taken");
        // Enter in the field's name: the next field, of the same type
        name = board.Card(wobble).Q<TextField>("name" + frequency.Wire("id"));
        GraphShots.Key(name, KeyCode.Return);
        var amplitude = decl.Arr("members").Items[1];
        Assert.AreEqual("float", amplitude.Str("type"));
        GraphShots.Enter(board.Card(wobble).Q<TextField>("name" + amplitude.Wire("id")), "amplitude");
        // A starting value, typed
        GraphShots.Enter(board.Card(wobble).Q("row" + amplitude.Wire("id")).Q(className: "tg-row-value").Q<TextField>(), "0.5");
        yield return CheckedAgain(window);
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        StringAssert.Contains("component Wobble {\n    float frequency;\n    float amplitude = 0.5;\n}", window.Checked.Text.Replace("\r\n", "\n"));
        yield return GraphShots.Take(window, "08-two-fields");
        // The pointer on a row shows what else there is for it, and the keyboard is where a type is picked
        GraphShots.Hover(window, board.Card(wobble).Q("row" + frequency.Wire("id")));
        board.Card(wobble).Q<GraphTypeChip>("type" + amplitude.Wire("id")).Focus();
        yield return GraphShots.Take(window, "08b-pointer-and-keyboard");
        board.Focus();
        // What else a component can have: its menu
        GraphShots.Click(board.Card(wobble).Q<Button>("more"));
        CollectionAssert.AreEqual(new[] { "method", "property", "tweak", "text" }, ((GraphOptionList)window.Popup).Shown.Where(o => o.Section == "Add").Select(o => (string)o.Data));
        yield return GraphShots.Take(window, "09-card-menu");
        window.ClosePopup(window.Popup);

        // 3. A system over it, from the add menu: named, then Enter asks what it needs
        var menu = board.OpenAdd(null, window.rootVisualElement.Q<Button>("add"));
        Assert.IsTrue(menu.Pick(GraphSchema.DeclTitle("system")));
        var shake = window.Doc.Decls.Items.Last().Wire("id");
        var frame = board.Frame(shake);
        Assert.IsNotNull(frame);
        Assert.AreEqual(GraphSchema.Sentence("system", false), frame.Entry.Q<Label>(className: "tg-node-title").text);
        yield return GraphShots.Take(window, "10-new-system");
        name = frame.Q<TextField>("name");
        name.SetValueWithoutNotify("Shake");
        GraphShots.Key(name, KeyCode.Return);
        Assert.AreEqual("Shake", window.Doc.Get(shake).Str("name"));
        yield return Until(() => window.Popup is GraphOptionList, "Enter on a new system's name asks what it needs");
        var needs = (GraphOptionList)window.Popup;
        yield return Until(() => string.IsNullOrEmpty(needs.Status), "tidels says what it can take");
        Assert.AreEqual("Wobble", needs.Shown[0].Title, "the file's own component first: " + Titles(needs));
        yield return GraphShots.Take(window, "11-what-it-needs");
        Assert.IsTrue(needs.Pick("Wobble"));
        frame = board.Frame(shake);
        var takes = window.Doc.Get(shake).Arr("params").Items.Single();
        Assert.AreEqual("read", takes.Str("mode"));
        Assert.AreEqual("wobble", takes.Str("name"));
        var pin = frame.Entry.ReadPin("wobble");
        Assert.IsNotNull(pin, "what it takes has a pin");
        Assert.AreEqual(GraphSchema.ParamModeInfo("read").Label, frame.Entry.Q<Button>("mode" + takes.Wire("id")).text, "and says how it's taken, in words");
        yield return GraphShots.Take(window, "12-takes-wobble");

        // 4. wobble.frequency, wired into something: a wire dragged from the pin and dropped asks what to do with it
        var search = board.OpenSearch(frame, new Vector2(440, 150), pin.Port);
        yield return Answered(search);
        Assert.AreEqual("frequency", search.Entries[0].Title, Titles(search));
        yield return GraphShots.Take(window, "13-wire-dropped");
        Assert.IsTrue(search.Pick("frequency"));
        frame = board.Frame(shake);
        var member = frame.Views.Values.Single();
        Assert.AreEqual("member", member.Node.Str("k"));
        Assert.AreSame(frame.Entry.ReadPin("wobble"), frame.SourcePin(member.Node.Wire("object")), "wired from the parameter's pin");
        // What happens: from the entry's arrow
        search = board.OpenSearch(frame, new Vector2(700, 60), frame.Entry.Ports.First(p => p.Port.Exec).Port);
        yield return Answered(search);
        yield return GraphShots.Take(window, "14-what-happens");
        search.Type("assign");
        Assert.IsTrue(search.PickCurrent());
        frame = board.Frame(shake);
        var assign = frame.Views.Values.First(v => v.Node.Str("k") == "assign");
        // A step just added has nothing wired into it: the file is still checked, and the pins that need a wire
        // say so themselves
        yield return CheckedAgain(window);
        var holes = window.Checked.Diagnostics.Where(d => d.Id == assign.Id && d.Port != null).ToList();
        CollectionAssert.AreEquivalent(new[] { "target", "value" }, holes.Select(d => d.Port), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Port} {d.Message}")));
        StringAssert.Contains("system Shake", window.Checked.Text, "the rest of the file is still read");
        frame = board.Frame(shake);
        assign = frame.Views.Values.First(v => v.Node.Str("k") == "assign");
        Assert.IsTrue(assign.Q("in:target").ClassListContains("tg-port-row--problem"), "the empty pin is marked");
        Assert.IsTrue(assign.Q("in:value").ClassListContains("tg-port-row--problem"));
        yield return GraphShots.Take(window, "14b-nothing-wired-yet");
        member = frame.Views.Values.First(v => v.Node.Str("k") == "member");
        Assert.IsTrue(board.Connect(member.DataOut, assign.Input("target"), out var why), why);
        // Its value, typed on its input
        frame.TypeInto(assign.Node, new WirePath("value"), "2.5", assign);
        Assert.AreEqual(2, board.Frame(shake).Views.Count, "the member and the assignment: the read and the number aren't nodes");
        yield return CheckedAgain(window);
        StringAssert.Contains("system Shake(Wobble wobble) {\n    wobble.frequency = 2.5;\n}", window.Checked.Text.Replace("\r\n", "\n"));
        frame = board.Frame(shake);
        Assert.IsFalse(frame.Query<VisualElement>(className: "tg-port-row--problem").ToList().Any(r => r.name == "in:target" || r.name == "in:value"), "wired, the pins' marks are gone");

        // 5. An error, on what it's about, in the compiler's words: the system only reads the wobble it changes
        Assert.AreEqual("1 problem", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        frame = board.Frame(shake);
        member = frame.Views.Values.First(v => v.Node.Str("k") == "member");
        StringAssert.Contains("read-only", member.ProblemText);
        Assert.AreEqual(1, board.ProblemsShown().Count(t => t.Contains("read-only")), "said once, where it is");
        yield return GraphShots.Take(window, "15-a-problem");
        GraphShots.Click(window.rootVisualElement.Q<Button>("check"));
        var problems = (GraphOptionList)window.Popup;
        StringAssert.Contains("read-only", problems.Shown[0].Title);
        StringAssert.Contains("mut", problems.Shown[0].Detail, "with the compiler's note of what to do");
        yield return GraphShots.Take(window, "16-problems-list");
        window.ClosePopup(problems);
        // Fixed where it is: how the wobble is taken, from "reads" to "changes"
        var modes = frame.OpenModes(frame.Entry.Q<Button>("mode" + takes.Wire("id")), window.Doc.Get(shake).Arr("params"), takes);
        yield return Until(() => !modes.Shown.Any(o => (string)o.Data == "out"), "tidels says which ways a system has");
        yield return GraphShots.Take(window, "17-how-its-taken");
        Assert.IsTrue(modes.Pick(GraphSchema.ParamModeInfo("mut").Label));
        Assert.AreEqual("mut", takes.Str("mode"));
        // The mark stays until the next check says otherwise, and then nothing of it is left anywhere
        yield return CheckedAgain(window);
        Assert.IsFalse(window.Checked.Diagnostics.Any(d => d.Severity == "error"), string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        Assert.IsFalse(board.ProblemsShown().Any(t => t.Contains("read-only")), "no message of the check before stays: " + string.Join(" | ", board.ProblemsShown()));
        Assert.IsNull(board.Frame(shake).Views.Values.First(v => v.Node.Str("k") == "member").ProblemText);
        Assert.IsFalse(board.Frame(shake).Query<VisualElement>(className: "tg-node").ToList().Any(n => n.ClassListContains("tg-has-problem")), "and no node is marked");
        Assert.IsFalse(window.ProblemOptions().Any(o => o.Title.Contains("read-only")));
        StringAssert.Contains("system Shake(mut Wobble wobble) {\n    wobble.frequency = 2.5;\n}", window.Checked.Text.Replace("\r\n", "\n"));
        // What the compiler has left to say is the next step of the story: nothing has a Wobble yet. It's on the
        // system it's about, as a thing to look at, not a problem.
        var left = window.Checked.Diagnostics.Single();
        Assert.AreEqual("warning", left.Severity);
        StringAssert.Contains("never runs", left.Message);
        Assert.AreEqual(shake, left.Id);
        Assert.AreEqual("1 thing to look at", window.ChipText);
        StringAssert.Contains("never runs", board.Frame(shake).Q<Label>(className: "tg-problem").text);
        yield return GraphShots.Take(window, "18-fixed");

        // The Tide isn't on screen until it's asked for
        Assert.IsNull(window.TideView);
        window.ShowTide(true);
        StringAssert.Contains("wobble.frequency = 2.5;", window.TideView.Text);
        yield return GraphShots.Take(window, "19-view-as-tide");
        window.ShowTide(false);
        GraphShots.Click(window.rootVisualElement.Q<Button>("menu"));
        yield return GraphShots.Take(window, "20-board-menu");
        window.ClosePopup(window.Popup);
        // A value that's more than a number is a small graph of its own, beside its card
        var value = board.OpenValue(new GraphSlot(frequency.Wire("id"), "default"));
        Assert.IsNotNull(value);
        Assert.IsTrue(value.IsValue);
        var made = board.OpenSearch(value, new Vector2(40, 80), value.Result.Ports[0].Port);
        made.Type("multiply");
        Assert.IsTrue(made.PickCurrent());
        value = board.ValueFrame(value.Slot);
        var times = value.Views.Values.Single();
        value.TypeInto(times.Node, new WirePath("lhs"), "2", times);
        value.TypeInto(times.Node, new WirePath("rhs"), "1.5", times);
        Assert.AreEqual(times.Id, value.Graph.Wire("root"), "what was picked for the value is the value");
        yield return CheckedAgain(window);
        StringAssert.Contains("float frequency = 2 * 1.5;", window.Checked.Text);
        Assert.IsNotNull(board.Card(wobble).Q("row" + frequency.Wire("id")).Q<Button>(className: "tg-link"), "its row says it's worked out, and opens it");
        yield return GraphShots.Take(window, "20b-a-value-worked-out");
        board.CloseValue(value.Slot);
        Assert.IsNull(board.ValueFrame(value.Slot));
        // An event and what hears it, in a namespace of their own: a region around them
        var space = board.AddDeclaration("namespace");
        var block = window.Doc.Get(space);
        var shaken = board.AddDeclaration("event", new Vector2(0, 420), block);
        window.Edit("Name", () => window.Doc.Get(shaken).Set("name", JValue.Of("Shaken")), shaken);
        var hears = board.AddDeclaration("handler", new Vector2(380, 420), block);
        GraphShots.Click(board.Frame(hears).Entry.Q<GraphTypeChip>("event"));
        var events = (GraphTypePicker)window.Popup;
        yield return Asked(events);
        Assert.IsTrue(events.Choose("Shaken"), Titles(events));
        Assert.AreEqual("Shaken", window.Doc.Get(hears).Get("trigger").Str("type"));
        Assert.AreEqual(1, board.Spaces.Count, "a namespace is a region");
        Assert.IsTrue(board.Spaces[0].Rect.Contains(board.Frame(hears).World.center) && board.Spaces[0].Rect.Contains(board.Card(shaken).World.center), "around what's declared in it");
        window.Reveal(hears);
        yield return GraphShots.Take(window, "20c-a-namespace");
        window.Reveal(shake);

        // A wire can't leave its frame: said, where it's tried
        var other = board.AddDeclaration("function");
        Assert.IsFalse(board.Connect(board.Frame(shake).Entry.ReadPin("wobble"), board.Frame(other).Entry.Ports.First(p => p.Port.Exec), out why));
        window.Say(GraphLook.UpperFirst(board.WhyNotAcross(board.Frame(shake), board.Frame(other))), true);
        yield return GraphShots.Take(window, "21-a-wire-cant-leave");
        board.Delete(other);
        window.Save();
        Assert.IsFalse(window.Dirty);
        window.Close();

        // 6. A big converted file, in a game of its own: found by its contents
        var arena = Path.Combine(GameIn(light ? "GraphStoryArenaLight" : "GraphStoryArena"), "Arena.tidegraph");
        File.Delete(path);
        Assert.IsNull(TideGraphCommand.Convert(Path.Combine(Repository, "examples", "sumo", "arena.tide"), arena));
        window = TideGraphWindow.Open(arena);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        board = window.Board;
        yield return GraphShots.Settle(4);
        Assert.Less(board.NodesDrawn, 400, "what's on screen is drawn, of over a thousand nodes");
        yield return GraphShots.Take(window, "22-a-big-file");
        window.ShowContents(true);
        Assert.IsNotNull(window.Contents);
        yield return GraphShots.Take(window, "23-contents");
        window.Contents.Type("sta");
        var found = window.Contents.Shown.First(r => r.title == "Stands");
        yield return GraphShots.Take(window, "24-contents-searched");
        window.Reveal(found.id);
        Assert.AreEqual(GraphLevel.Near, board.Frame(found.id).Level, "picked, it's brought on screen");
        Assert.IsTrue(board.Chosen.Contains(found.id));
        window.ShowContents(false);
        yield return GraphShots.Take(window, "25-jumped-to");
        board.ZoomAbout(new Vector2(640, 380), 0.3f);
        Assert.AreEqual(0, board.NodesDrawn, "from far away, names");
        yield return GraphShots.Take(window, "26-from-far-away");
        Assert.IsFalse(window.Dirty, "looking changes nothing");
        window.Close();
        File.Delete(arena);
    }
}
