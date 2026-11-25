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

        // What the return gives is in its input: the parameter and its field are no nodes, and there's no wire
        Assert.IsFalse(frame.Views.ContainsKey(17));
        Assert.IsFalse(frame.Views.ContainsKey(16));
        Assert.IsTrue(frame.Hidden.Contains(16) && frame.Hidden.Contains(17));
        Assert.IsNull(frame.SourcePin(16));
        var gives = frame.Views[15];
        var chip = gives.Q("in:value").Q("chip");
        CollectionAssert.AreEqual(new[] { "ball", ".hits" }, chip.Query<Button>().ToList().Select(b => b.text).ToList());
        Assert.AreEqual("Ball", frame.Entry.ReadPin("ball").Type);
        // ...and one nothing reads is a small node, faded
        Assert.IsTrue(frame.Views[18].ClassListContains("tg-node--unreached"));

        // Each value's type: the chip is in its value's type's colour, and says it
        Assert.AreEqual("int", window.Checked.Types[16]);
        Assert.AreEqual("Ball", window.Checked.Types[17]);
        Assert.AreEqual("int", frame.TypeAt(16));
        StringAssert.StartsWith("int", chip.tooltip);
        Assert.AreEqual(TideGraphTypes.ColorOf("int", window.Light), chip.style.color.value);
        Assert.AreNotEqual(TideGraphTypes.Untyped, chip.style.color.value);
        Assert.AreEqual("int", frame.Wanted(gives.Node, new WirePath("value")), "what a return gives is its code's result");
        // ...and what tidels says of it
        string hover = null;
        TideGraphLanguage.Hover(window, 16, text => hover = text);
        for (var clock = Stopwatch.StartNew(); hover == null && clock.Elapsed.TotalSeconds < 30;) yield return null;
        StringAssert.Contains("int hits", hover);
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

        // The method's node is titled with what it's of, which is no node any more, and has no row for it
        frame = Hit(window);
        var called = frame.ViewOf(method.Wire("id"));
        Assert.IsTrue(frame.Hidden.Contains(18), "the ball is in the title");
        Assert.AreEqual("ball", called.Q(className: "tg-node-head").Q("chip").Q<Button>("part0").text);
        Assert.AreEqual("Bounce", called.Q<TextField>("title").value);
        Assert.IsNull(called.Q("in:object"));
        Assert.AreEqual("height", called.Q("in:args.0").Q<Label>(className: "tg-port-label").text, "its argument is named by the method's parameter");
        Assert.AreEqual("float", frame.Wanted(method, new WirePath("args", 0)), "and takes its type");
        // It picks its member from what its value has
        popup = board.OpenMemberSearch(frame, method.Wire("id"), called);
        yield return Answered(popup);
        Assert.IsTrue(popup.Entries.All(e => e.Group == "Members of Ball"), Titles(popup));
        Assert.IsTrue(popup.Pick("hits"));
        var retargeted = GraphDocument.NodeIn(frame.Graph, method.Wire("id"));
        Assert.AreEqual("member", retargeted.Str("k"));
        Assert.AreEqual("hits", retargeted.Str("member"));
        Assert.AreEqual(18, retargeted.Wire("object"));
        Undo.PerformUndo();
        frame = Hit(window);
        Assert.AreEqual("method", GraphDocument.NodeIn(frame.Graph, method.Wire("id")).Str("k"));
        // A part of a path shown in an input is changed from there on: what the ball has that the return can give
        var values = board.PickValue(frame, GraphDocument.NodeIn(frame.Graph, 15), new WirePath("value"), "value", frame.Views[15], 1);
        yield return Until(() => !values.Asking, "tidels says what a Ball has");
        Assert.AreEqual("ball: which of it?", values.Title);
        Assert.IsTrue(values.Shown.Any(o => o.Title == "hits"), Titles(values));
        Assert.IsFalse(values.Shown.Any(o => o.Title.StartsWith("Bounce")), "a method that gives nothing isn't a value: " + Titles(values));
        window.ClosePopup(values);

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
        frame = Hit(window);
        Assert.IsTrue(frame.Hidden.Contains(read.Wire("id")), "the read is in the member's title, not a node");
        Assert.AreEqual("ball", frame.Views[hits.Wire("id")].Q(className: "tg-node-head").Q("chip").Q<Button>("part0").text);
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
        // "In this game" is what the game declares (tide/graphNames): the engine's own are with the rest
        Assert.AreEqual("enum", window.Checked.Game["Mood"], string.Join(", ", window.Checked.Game.Keys));
        Assert.IsFalse(window.Checked.Game.ContainsKey("Random"));
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Random" && o.Section == GraphSchema.TypeGroup("rest")), "the engine's own, with the rest: " + Titles(picker));
        Assert.IsFalse(picker.Shown.Any(o => o.Section == GraphSchema.TypeGroup("game") && window.Checked.IsGames(o.Title.Replace(" of…", "")) == false), "nothing of the engine's is the game's: " + Titles(picker));
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

    const string SpinGraph =
        "{\"tidegraph\": 1, \"nextId\": 200, \"decls\": [\n" +
        "  {\"kind\": \"scene\", \"id\": 1, \"name\": \"Main\", \"members\": []},\n" +
        "  {\"kind\": \"enum\", \"id\": 70, \"name\": \"Mood\", \"members\": [{\"id\": 71, \"name\": \"Calm\"}, {\"id\": 72, \"name\": \"Wild\"}]},\n" +
        "  {\"kind\": \"const\", \"id\": 60, \"name\": \"SPEED\", \"type\": \"float\", \"value\": {\"root\": 61, \"nodes\": [{\"id\": 61, \"k\": \"float\", \"x\": 0, \"y\": 0, \"text\": \"2.0\"}]}},\n" +
        "  {\"kind\": \"component\", \"id\": 2, \"name\": \"Wobble\", \"members\": [\n" +
        "    {\"kind\": \"field\", \"id\": 3, \"type\": \"float\", \"name\": \"frequency\"},\n" +
        "    {\"kind\": \"field\", \"id\": 4, \"type\": \"float\", \"name\": \"phase\"},\n" +
        "    {\"kind\": \"field\", \"id\": 5, \"type\": \"float3\", \"name\": \"axis\"},\n" +
        "    {\"kind\": \"field\", \"id\": 6, \"type\": \"Mood\", \"name\": \"mood\"},\n" +
        "    {\"kind\": \"field\", \"id\": 7, \"type\": \"Color\", \"name\": \"tint\"}]},\n" +
        "  {\"kind\": \"function\", \"id\": 40, \"x\": 0, \"y\": 600, \"name\": \"Twice\", \"returnType\": \"float\", \"params\": [{\"id\": 41, \"mode\": \"read\", \"type\": \"float\", \"name\": \"amount\"}], \"body\": {\"entry\": 42, \"nodes\": [\n" +
        "    {\"id\": 42, \"k\": \"return\", \"x\": 300, \"y\": 0, \"value\": 43},\n" +
        "    {\"id\": 43, \"k\": \"binary\", \"x\": 0, \"y\": 100, \"op\": \"*\", \"lhs\": 44, \"rhs\": 45},\n" +
        "    {\"id\": 44, \"k\": \"name\", \"x\": 0, \"y\": 236, \"name\": \"amount\"},\n" +
        "    {\"id\": 45, \"k\": \"unary\", \"x\": 0, \"y\": 236, \"op\": \"-\", \"value\": 46},\n" +
        "    {\"id\": 46, \"k\": \"int\", \"x\": 0, \"y\": 236, \"text\": \"2\"}]}},\n" +
        "  {\"kind\": \"system\", \"id\": 10, \"x\": 0, \"y\": 0, \"name\": \"Spin\", \"params\": [{\"id\": 11, \"mode\": \"mut\", \"type\": \"Wobble\", \"name\": \"wobble\"}, {\"id\": 18, \"mode\": \"read\", \"type\": \"Time\", \"name\": \"time\"}],\n" +
        "   \"body\": {\"entry\": 80, \"nodes\": [\n" +
        "    {\"id\": 80, \"k\": \"var\", \"x\": 0, \"y\": 0, \"name\": \"speed\", \"value\": 81, \"next\": 12},\n" +
        "    {\"id\": 81, \"k\": \"float\", \"x\": 0, \"y\": 112, \"text\": \"1.5\"},\n" +
        "    {\"id\": 12, \"k\": \"assign\", \"x\": 552, \"y\": 0, \"op\": \"+=\", \"target\": 13, \"value\": 15, \"next\": 20},\n" +
        "    {\"id\": 13, \"k\": \"member\", \"x\": 552, \"y\": 136, \"object\": 14, \"member\": \"phase\"},\n" +
        "    {\"id\": 14, \"k\": \"name\", \"x\": 552, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 15, \"k\": \"call\", \"x\": 260, \"y\": 136, \"name\": \"Twice\", \"args\": [16]},\n" +
        "    {\"id\": 16, \"k\": \"member\", \"x\": 260, \"y\": 136, \"object\": 17, \"member\": \"frequency\"},\n" +
        "    {\"id\": 17, \"k\": \"name\", \"x\": 260, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 20, \"k\": \"assign\", \"x\": 1152, \"y\": 0, \"op\": \"=\", \"target\": 21, \"value\": 23, \"next\": 30},\n" +
        "    {\"id\": 21, \"k\": \"member\", \"x\": 1152, \"y\": 136, \"object\": 22, \"member\": \"frequency\"},\n" +
        "    {\"id\": 22, \"k\": \"name\", \"x\": 1152, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 23, \"k\": \"method\", \"x\": 860, \"y\": 136, \"object\": 24, \"name\": \"Clamp\", \"args\": [25, 26, 27]},\n" +
        "    {\"id\": 24, \"k\": \"name\", \"x\": 860, \"y\": 136, \"name\": \"Math\"},\n" +
        "    {\"id\": 25, \"k\": \"member\", \"x\": 860, \"y\": 136, \"object\": 28, \"member\": \"frequency\"},\n" +
        "    {\"id\": 28, \"k\": \"name\", \"x\": 860, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 26, \"k\": \"int\", \"x\": 860, \"y\": 136, \"text\": \"0\"},\n" +
        "    {\"id\": 27, \"k\": \"name\", \"x\": 860, \"y\": 136, \"name\": \"SPEED\"},\n" +
        "    {\"id\": 30, \"k\": \"assign\", \"x\": 1484, \"y\": 0, \"op\": \"=\", \"target\": 31, \"value\": 33, \"next\": 150},\n" +
        "    {\"id\": 31, \"k\": \"member\", \"x\": 1484, \"y\": 136, \"object\": 32, \"member\": \"mood\"},\n" +
        "    {\"id\": 32, \"k\": \"name\", \"x\": 1484, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 33, \"k\": \"member\", \"x\": 1484, \"y\": 136, \"shorthand\": true, \"member\": \"Wild\"},\n" +
        "    {\"id\": 150, \"k\": \"assign\", \"x\": 1760, \"y\": 0, \"op\": \"=\", \"target\": 151, \"value\": 153, \"next\": 160},\n" +
        "    {\"id\": 151, \"k\": \"member\", \"x\": 1760, \"y\": 136, \"object\": 152, \"member\": \"axis\"},\n" +
        "    {\"id\": 152, \"k\": \"name\", \"x\": 1760, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 153, \"k\": \"call\", \"x\": 1760, \"y\": 136, \"name\": \"float3\", \"args\": [154, 155, 156]},\n" +
        "    {\"id\": 154, \"k\": \"int\", \"x\": 1760, \"y\": 136, \"text\": \"0\"},\n" +
        "    {\"id\": 155, \"k\": \"float\", \"x\": 1760, \"y\": 136, \"text\": \"1.5\"},\n" +
        "    {\"id\": 156, \"k\": \"unary\", \"x\": 1760, \"y\": 136, \"op\": \"-\", \"value\": 157},\n" +
        "    {\"id\": 157, \"k\": \"int\", \"x\": 1760, \"y\": 136, \"text\": \"2\"},\n" +
        "    {\"id\": 160, \"k\": \"assign\", \"x\": 2100, \"y\": 0, \"op\": \"=\", \"target\": 161, \"value\": 163},\n" +
        "    {\"id\": 161, \"k\": \"member\", \"x\": 2100, \"y\": 136, \"object\": 162, \"member\": \"tint\"},\n" +
        "    {\"id\": 162, \"k\": \"name\", \"x\": 2100, \"y\": 136, \"name\": \"wobble\"},\n" +
        "    {\"id\": 163, \"k\": \"call\", \"x\": 2100, \"y\": 136, \"name\": \"Color\", \"args\": [164, 165, 166]},\n" +
        "    {\"id\": 164, \"k\": \"float\", \"x\": 2100, \"y\": 136, \"text\": \"1.0\"},\n" +
        "    {\"id\": 165, \"k\": \"float\", \"x\": 2100, \"y\": 136, \"text\": \"0.5\"},\n" +
        "    {\"id\": 166, \"k\": \"float\", \"x\": 2100, \"y\": 136, \"text\": \"0.25\"}\n" +
        "  ]}}\n" +
        "]}\n";

    // Simple values are in the input that reads them (the schema's `inline`), a call's arguments are named by its
    // callee's parameters and take their types (tide/graphCalls), a method is titled with what it's of, and an
    // input is picked from what fits it
    [UnityTest]
    public IEnumerator SimpleValuesAreInTheInput()
    {
        NeedTidels();
        var game = GameIn("GraphInputs");
        foreach (var old in Directory.GetFiles(game, "*.tidegraph")) File.Delete(old);
        var path = Path.Combine(game, "Spin.tidegraph");
        File.WriteAllText(path, SpinGraph, new UTF8Encoding(false));
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        GraphShots.Size(window);
        yield return Checked(window);
        var board = window.Board;
        window.Reveal(10);
        var frame = board.Frame(10);

        // A local, five Sets and the two calls that work a value out: eight nodes of the file's thirty-six
        CollectionAssert.AreEquivalent(new[] { 80, 12, 15, 20, 23, 30, 150, 160 }, frame.Views.Keys);
        Assert.AreEqual(36, GraphDocument.Nodes(frame.Graph).Items.Count);
        var opened = window.Doc.Text;
        Assert.IsFalse(window.Dirty, "showing them where they're read changes nothing of the file");
        Assert.AreEqual("1.5", frame.Views[80].Q("in:value").Q<TextField>("typed").value, "a number is typed where it's read");
        Assert.AreEqual("wobble.phase", frame.TextOf(frame.ShownIn(13)));
        Assert.AreEqual(".Wild", frame.TextOf(frame.ShownIn(33)), "a choice of what's expected");
        Assert.AreEqual("+=", frame.Views[12].Q("in:value").Q<Button>("op").text);
        Assert.IsTrue(frame.Views[12].Input("value").Connected, "what's worked out is wired in");
        Assert.IsTrue(frame.Views[12].Input("target").Connected, "its pin is filled: something's in it");
        Assert.IsNull(frame.SourcePin(frame.Views[12].Node.Wire("target")), "and no wire comes to it");
        // A call's arguments are named by its callee's parameters, and take their types
        var twice = frame.Views[15];
        Assert.AreEqual("amount", twice.Q("in:args.0").Q<Label>(className: "tg-port-label").text);
        Assert.AreEqual("float", frame.Wanted(twice.Node, new WirePath("args", 0)));
        Assert.AreEqual("float", frame.Wanted(frame.Views[12].Node, new WirePath("value")), "a Set's value takes what it changes");
        // A method of something shown is titled with it, and has no row for it
        var clamp = frame.Views[23];
        Assert.IsNull(clamp.Q("in:object"));
        Assert.AreEqual("Math", clamp.Q(className: "tg-node-head").Q("chip").Q<Button>("part0").text);
        Assert.AreEqual("Clamp", clamp.Q<TextField>("title").value);
        CollectionAssert.AreEqual(new[] { "x", "a", "b" }, Enumerable.Range(0, 3).Select(i => clamp.Q("in:args." + i).Q<Label>(className: "tg-port-label").text).ToList());
        Assert.AreEqual("0", clamp.Q("in:args.1").Q<TextField>("typed").value);
        Assert.AreEqual("SPEED", clamp.Q("in:args.2").Q("chip").Q<Button>("part0").text, "a constant is a name");
        // What the game declares, by name: the engine's own aren't among them
        Assert.AreEqual("component", window.Checked.Game["Wobble"]);
        Assert.AreEqual("function", window.Checked.Game["Twice"]);
        Assert.IsFalse(window.Checked.Game.ContainsKey("Transform"));
        yield return GraphShots.Take(window, "40-values-in-their-inputs");

        // What goes in Twice's amount: a float typed, what this code has that is one or has one, the game's
        // constants and functions
        GraphShots.Click(twice.Q("in:args.0").Q("chip").Q<Button>("part0"));
        var values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.AreEqual("What goes in 'amount'?", values.Title);
        Assert.AreEqual("float", values.Wanted);
        Assert.AreEqual("What this code has", values.Shown.First(o => o.Title == "speed").Section, "a local declared before it: " + Titles(values));
        Assert.IsTrue(values.Shown.Any(o => o.Title == "wobble") && values.Shown.Any(o => o.Title == "time"), "what has a float in it opens to it: " + Titles(values));
        Assert.AreEqual("Constants", values.Shown.First(o => o.Title == "SPEED").Section);
        Assert.IsTrue(values.Shown.Any(o => o.Title == "Twice(amount)"), "a function that gives a float: " + Titles(values));
        Assert.IsFalse(values.Shown.Any(o => o.Title.StartsWith("Spawn") || o.Title.StartsWith("Send")), "not the engine's steps: " + Titles(values));
        yield return GraphShots.Take(window, "41-what-fits-a-float");
        // What's typed is a value when it reads as a float, and the names found as they're typed
        values.Type("true");
        Assert.IsFalse(values.Shown.Any(o => o.Section == "A value" && o.Title == "true"), "true isn't a float: " + Titles(values));
        values.Type("0.25");
        Assert.AreEqual("0.25", values.Shown[0].Title);
        values.Type("dt");
        Assert.AreEqual("time.dt", values.Shown[0].Title, Titles(values));
        values.Type("math");
        Assert.AreEqual("Math", values.Shown[0].Title, "the engine's namespaces are found by name: " + Titles(values));
        Assert.IsTrue(values.PickCurrent());
        Assert.AreSame(values, window.Popup, "and open to what they have");
        yield return Until(() => !values.Asking, "tidels says what Math has");
        Assert.IsTrue(values.Shown.Any(o => o.Title.StartsWith("Sin(")), Titles(values));
        values.Type("sqrt");
        yield return GraphShots.Take(window, "42-what-math-has");
        Assert.IsTrue(values.PickCurrent());
        frame = board.Frame(10);
        var sqrt = GraphDocument.NodeIn(frame.Graph, GraphDocument.NodeIn(frame.Graph, 15).Arr("args").Items[0].Number > 0 ? (int)GraphDocument.NodeIn(frame.Graph, 15).Arr("args").Items[0].Number : 0);
        Assert.AreEqual("method", sqrt.Str("k"), "a call is a node, wired in");
        Assert.AreEqual("Sqrt", sqrt.Str("name"));
        Assert.AreEqual("Math", GraphDocument.NodeIn(frame.Graph, sqrt.Wire("object")).Str("name"));
        Assert.IsTrue(frame.Views.ContainsKey(sqrt.Wire("id")));
        Assert.IsNull(window.Doc.Find(16), "what was in the input went with it");
        Undo.PerformUndo();
        frame = board.Frame(10);
        Assert.AreEqual(opened, window.Doc.Text);

        // A part of a path clicked changes it from there on
        GraphShots.Click(frame.Views[15].Q("in:args.0").Q("chip").Q<Button>("part1"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what a Wobble has");
        Assert.AreEqual("wobble: which of it?", values.Title);
        Assert.AreNotEqual("‹ Back", values.Shown[0].Title, "Enter takes something it has");
        Assert.IsTrue(values.Choose("phase"));
        yield return CheckedAgain(window);
        StringAssert.Contains("wobble.phase += Twice(wobble.phase);", window.Checked.Text);
        // A local isn't offered for its own value, and a parameter's pin dropped on an input fills it
        frame = board.Frame(10);
        values = board.PickValue(frame, frame.Views[80].Node, new WirePath("value"), "value", frame.Views[80]);
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.IsFalse(values.Shown.Any(o => o.Title == "speed"), Titles(values));
        window.ClosePopup(values);
        Assert.IsTrue(board.Connect(frame.Entry.ReadPin("time"), frame.Views[15].Input("args.0"), out var why), why);
        frame = board.Frame(10);
        Assert.AreEqual("time", frame.Views[15].Q("in:args.0").Q("chip").Q<Button>("part0").text);
        Assert.AreEqual(8, frame.Views.Count, "a chip, not a node");

        // A vector made of numbers alone is in the input, as a box for each number; a colour as its field
        Assert.IsTrue(frame.Hidden.Contains(153) && frame.Hidden.Contains(156) && frame.Hidden.Contains(163));
        var axis = frame.Views[150].Q("in:value");
        Assert.IsNull(frame.SourcePin(frame.Views[150].Node.Wire("value")), "no wire");
        CollectionAssert.AreEqual(new[] { "0", "1.5", "-2" }, axis.Q("numbers").Query<TextField>().ToList().Select(f => f.value).ToList());
        CollectionAssert.AreEqual(new[] { "x", "y", "z" }, axis.Q("numbers").Query<TextField>().ToList().Select(f => f.tooltip).ToList(), "each box is its part, by the type's own names");
        var tint = frame.Views[160].Q("in:value").Q<UnityEditor.UIElements.ColorField>("colour");
        Assert.IsNotNull(tint);
        Assert.AreEqual(new Color(1, 0.5f, 0.25f, 1), tint.value);
        Assert.AreEqual("float3(0, 1.5, -2)", frame.TextOf(frame.ShownIn(153)));
        window.Reveal(150);
        yield return GraphShots.Take(window, "43-a-vector-in-its-input");
        window.Reveal(160);
        yield return GraphShots.Take(window, "44-a-colour-in-its-input");
        window.Reveal(10);
        // A number typed in a box is that number, in the file's own node for it
        var before = GraphDocument.Nodes(frame.Graph).Items.Count;
        frame.TypeInto(GraphDocument.NodeIn(frame.Graph, 153), new WirePath("args", 0), "0.75", frame.Views[150]);
        Assert.AreEqual("float", GraphDocument.NodeIn(frame.Graph, 154).Str("k"), "a whole number becomes a fraction where it is");
        Assert.AreEqual("0.75", GraphDocument.NodeIn(frame.Graph, 154).Str("text"));
        Assert.AreEqual(before, GraphDocument.Nodes(frame.Graph).Items.Count);
        // A colour picked changes the numbers, and the field isn't built again while its picker is open
        frame = board.Frame(10);
        tint = frame.Views[160].Q("in:value").Q<UnityEditor.UIElements.ColorField>("colour");
        tint.value = new Color(0.2f, 0.4f, 0.6f, 1);
        CollectionAssert.AreEqual(new[] { "0.2", "0.4", "0.6" }, new[] { 164, 165, 166 }.Select(id => GraphDocument.NodeIn(frame.Graph, id).Str("text")).ToList());
        Assert.AreSame(tint, board.Frame(10).Views[160].Q("in:value").Q<UnityEditor.UIElements.ColorField>("colour"));
        // A name typed in a box makes the vector a node, wired in, with the name in its row
        frame.TypeInto(GraphDocument.NodeIn(frame.Graph, 153), new WirePath("args", 1), "speed", frame.Views[150]);
        frame = board.Frame(10);
        Assert.IsFalse(frame.Hidden.Contains(153), "no longer numbers alone");
        Assert.IsTrue(frame.Views[150].Input("value").Connected);
        Assert.AreEqual("speed", frame.Views[153].Q("in:args.1").Q("chip").Q<Button>("part0").text);
        yield return CheckedAgain(window);
        StringAssert.Contains("wobble.axis = float3(0.75, speed, -2);", window.Checked.Text);
        StringAssert.Contains("wobble.tint = Color(0.2, 0.4, 0.6);", window.Checked.Text);
        // How a Set changes what it changes, in the schema's words beside the sign
        frame = board.Frame(10);
        var how = frame.Views[12].Q("in:value").Q<Button>("op");
        StringAssert.Contains(GraphSchema.AssignWord("+="), how.tooltip.ToLowerInvariant());
        GraphShots.Click(how);
        var ways = (GraphOptionList)window.Popup;
        Assert.AreEqual(GraphSchema.AssignWord("="), ways.Shown.First(o => o.Title == "=").Detail);
        Assert.AreEqual(GraphSchema.Json.Get("operators").Get("assignInfo").Get("++").Str("label"), ways.Shown.First(o => o.Title == "++").Detail);
        yield return GraphShots.Take(window, "45-how-a-set-changes");
        window.ClosePopup(ways);
        // An enum that's the type an input takes gives its choices, this file's or not: by its name
        List<TideGraphLanguage.Member> anchors = null;
        TideGraphLanguage.MembersOfName(window, "Anchor", (found, _) => anchors = found ?? new List<TideGraphLanguage.Member>());
        yield return Until(() => anchors != null, "tidels says what an enum of the engine's has");
        Assert.IsTrue(anchors.Any(m => m.Label == "UpperLeft" && m.Kind == TideGraphLanguage.KindEnumMember), string.Join(", ", anchors.Select(m => m.Label)));
        Assert.AreEqual(window.Doc.Text, TideLanguageClient.Shared.TextOf(TideLanguageClient.UriOf(path)), "asked by name: tidels has the file as it is");
        values = board.PickValue(frame, frame.Views[160].Node, new WirePath("value"), "value", frame.Views[160]);
        yield return Until(() => !values.Asking, "tidels says what a Color has");
        Assert.AreEqual("Color", values.Wanted);
        Assert.IsTrue(values.Shown.Any(o => o.Title == "Color.white"), "a type's own constants: " + Titles(values));
        window.ClosePopup(values);

        // The function's "-2": a '-' over a number, typed in the input
        window.Reveal(40);
        var body = board.Frame(40);
        CollectionAssert.AreEquivalent(new[] { 42, 43 }, body.Views.Keys);
        Assert.AreEqual("-2", body.Views[43].Q("in:rhs").Q<TextField>("typed").value);
        Assert.AreEqual("amount", body.Views[43].Q("in:lhs").Q("chip").Q<Button>("part0").text);
        window.Close();
    }

    // The owner's system (notes/visual-scripting.md, "Round three"), from an empty file: a component TestFromGraph
    // with a float frequency, a system over it and Transform that multiplies transform.localScale by frequency. A
    // Set's "what" offers only what can be changed; its operator only those the checker takes for what it changes
    // and what it's given (tide/graphOperators), so "*=" on the Transform itself is never offered. Then the
    // board's gestures: moving, renaming, the one menu, Details, My game.
    [UnityTest]
    public IEnumerator TheOwnersSystem() => OwnersSystem(false);

    [UnityTest]
    public IEnumerator TheOwnersSystemInTheLightTheme() => OwnersSystem(true);

    IEnumerator OwnersSystem(bool light)
    {
        NeedTidels();
        var game = GameIn(light ? "GraphOwnerLight" : "GraphOwner");
        foreach (var old in Directory.GetFiles(game, "*.tidegraph")) File.Delete(old);
        // The game's scene, as a game has one: the file being made is its first code
        File.WriteAllText(Path.Combine(game, "main.tide"), "scene Main {\n}\n\nevent(Spawned) Setup(with Main)\n{\n    Spawn(TestFromGraph, Transform);\n}\n");
        var path = Path.Combine(game, "Scale.tidegraph");
        File.WriteAllText(path, TideGraphImporter.EmptyText, new UTF8Encoding(false));
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        window.ShowMyGame(true);
        window.ShowDetails(true);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        yield return Checked(window);
        var board = window.Board;
        yield return GraphShots.Take(window, "50-an-empty-file");

        // A component, named as it's made; its field by the + in its title bar
        GraphShots.Click(board.Q<Button>("start:component"));
        var component = window.Doc.Decls.Items.Single().Wire("id");
        var name = board.Card(component).Q<GraphName>("name");
        Assert.IsTrue(name.Editing, "a new card starts with its name being typed");
        name.Field.SetValueWithoutNotify("TestFromGraph");
        GraphShots.Key(name.Field, KeyCode.Return);
        var field = window.Doc.Get(component).Arr("members").Items.Single();
        GraphShots.Enter(board.Card(component).Q<GraphName>("name" + field.Wire("id")).Field, "frequency");
        GraphShots.Click(board.Card(component).Q<GraphTypeChip>("type" + field.Wire("id")));
        var types = (GraphTypePicker)window.Popup;
        Assert.IsTrue(types.Choose("float"));
        Assert.AreEqual("TestFromGraph", window.Doc.Get(component).Str("name"));
        Assert.IsNull(board.Card(component).Q<Button>(className: "tg-add-row"), "no row under the last one to hit by accident");
        Assert.IsNotNull(board.Card(component).Q(className: "tg-card-head").Q<Button>("add"), "the + is in its title bar");
        yield return GraphShots.Take(window, "51-a-component");

        // A system over it and Transform, which it changes
        var system = board.AddDeclaration("system");
        GraphShots.Enter(board.Frame(system).Q<GraphName>("name").Field, "ScaleTest");
        var frame = board.Frame(system);
        frame.AddParam(window.Doc.Get(system).Arr("params"), "TestFromGraph", "read");
        frame = board.Frame(system);
        frame.AddParam(window.Doc.Get(system).Arr("params"), "Transform", "mut");
        frame = board.Frame(system);
        Assert.IsFalse(board.Card(component).World.Overlaps(frame.World), "a frame added goes beside what's there");
        var search = board.OpenSearch(frame, frame.FreePlace(new Vector2(300, 60), new Vector2(200, 80)), frame.Entry.Ports.First(p => p.Port.Exec).Port);
        yield return Answered(search);
        search.Type("set");
        Assert.IsTrue(search.PickCurrent());
        frame = board.Frame(system);
        var set = frame.Views.Values.Single(v => v.Node.Str("k") == "assign");
        yield return CheckedAgain(window);

        // What it changes: what it takes as "changes", and not what it only reads
        frame = board.Frame(system);
        set = frame.Views[set.Id];
        GraphShots.Click(set.Q("in:target").Q<Button>("pick"));
        var values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.AreEqual("What does it change?", values.Title);
        Assert.AreEqual("What this code can change", values.Shown.First(o => o.Title == "transform").Section, Titles(values));
        Assert.IsTrue(values.Shown.First(o => o.Title == "testFromGraph").Off, "what's only read is listed greyed, with why: " + Titles(values));
        Assert.IsFalse(values.Shown.Any(o => o.Section == "Constants" || o.Section == "A value"), "nothing that can't be changed: " + Titles(values));
        yield return GraphShots.Take(window, "52-what-can-be-changed");
        Assert.IsTrue(values.Choose("transform"));
        yield return Until(() => !values.Asking, "tidels says what a Transform has");
        Assert.IsTrue(values.Shown.Any(o => o.Title == "localScale"), Titles(values));
        Assert.IsFalse(values.Shown.Any(o => o.Title.Contains("(")), "a call's result is nothing to change: " + Titles(values));
        yield return GraphShots.Take(window, "53-transform-opened");
        // All of it, first: then its operators are only "=" (a component on its own)
        Assert.IsTrue(values.Choose("transform"));
        yield return CheckedAgain(window);
        frame = board.Frame(system);
        set = frame.Views[set.Id];
        GraphShots.Click(set.Q("in:value").Q<Button>("op"));
        var ops = (GraphOptionList)window.Popup;
        yield return Until(() => string.IsNullOrEmpty(ops.Status), "tidels says which operators work");
        CollectionAssert.AreEqual(new[] { "=" }, ops.Shown.Select(o => (string)o.Data), "only '=' for a Transform: " + Titles(ops));
        yield return GraphShots.Take(window, "54-only-what-works-on-a-transform");
        window.ClosePopup(ops);

        // Its localScale, by a click on the chip's name, then frequency: "*=" is offered, "=" isn't
        GraphShots.Click(set.Q("in:target").Q("chip").Q<Button>("part0"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.IsTrue(values.Choose("transform"));
        yield return Until(() => !values.Asking, "tidels says what a Transform has");
        Assert.IsTrue(values.Choose("localScale"));
        frame = board.Frame(system);
        set = frame.Views[set.Id];
        GraphShots.Click(set.Q("in:value").Q<Button>("pick"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        values.Type("freq");
        Assert.AreEqual("testFromGraph.frequency", values.Shown[0].Title, Titles(values));
        Assert.IsTrue(values.PickCurrent());
        // A float doesn't "become" a float3: the check says so, and how it's changed is asked next by itself, with
        // only the ways a float3 can be changed by a float
        yield return CheckedAgain(window);
        var stale = window.Checked.Diagnostics.Select(d => d.Text).ToList();
        Assert.IsNotEmpty(stale, "'=' of a float into a float3 is a problem");
        yield return Until(() => window.Popup is GraphOptionList list && list.Title == "How is it changed?", "the operators open by themselves");
        ops = (GraphOptionList)window.Popup;
        yield return Until(() => string.IsNullOrEmpty(ops.Status), "tidels says which operators work");
        var offered = ops.Shown.Select(o => (string)o.Data).ToList();
        CollectionAssert.Contains(offered, "*=", Titles(ops));
        CollectionAssert.DoesNotContain(offered, "=", "a float isn't a float3: " + Titles(ops));
        yield return GraphShots.Take(window, "55-a-float3-by-a-float");
        Assert.IsTrue(ops.Pick("*="));
        yield return CheckedAgain(window);
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        StringAssert.Contains("transform.localScale *= testFromGraph.frequency;", window.Checked.Text);
        // What was said of it before is said nowhere now, its hover text neither
        var said = board.Query<VisualElement>().ToList().Select(e => (e is Label l ? l.text + "\n" : "") + (e.tooltip ?? "")).ToList();
        foreach (var old in stale) Assert.IsFalse(said.Any(s => s.Contains(GraphLook.FirstLine(old))), "gone with its cause: " + old);
        frame = board.Frame(system);
        Assert.AreEqual(1, frame.Views.Count, "one node: the Set, its values in its inputs");
        yield return GraphShots.Take(window, "56-the-system");

        // A node asked for where the Set is goes beside it, not on it
        frame = board.Frame(system);
        set = frame.Views[set.Id];
        search = board.OpenSearch(frame, set.Shown + new Vector2(20, 20), null);
        yield return Answered(search);
        search.Type("break");
        Assert.IsTrue(search.PickCurrent());
        frame = board.Frame(system);
        var placed = frame.Views.Values.Single(v => v.Node.Str("k") == "break");
        Assert.IsFalse(new Rect(placed.Shown, placed.SizeNow).Overlaps(new Rect(frame.Views[set.Id].Shown, frame.Views[set.Id].SizeNow)), "never on the Set");
        // Selected, its Details are its own; deleted from its menu
        Assert.AreEqual("node:" + placed.Id, window.Details.Showing);
        yield return GraphShots.Take(window, "57-placed-beside-it");
        var menu = GraphMenu.Open(window, GraphMenu.ForNode(placed), placed);
        yield return GraphShots.Take(window, "58-a-nodes-menu");
        Assert.IsTrue(menu.Pick(GraphMenu.DeleteWord));
        Assert.IsFalse(board.Frame(system).Views.Values.Any(v => v.Node.Str("k") == "break"));

        // The card moved, and renamed by a double click
        var at = board.Card(component).At;
        board.MoveItemsBy(new GraphItem[] { board.Card(component) }, new Vector2(0, 160));
        Assert.AreEqual(at + new Vector2(0, 160), GraphDocument.PlaceOf(window.Doc.Get(component)));
        window.Reveal(component);
        yield return GraphShots.Settle(3);
        board.SelectItem(board.Card(component), false);
        GraphMenu.Open(window, GraphMenu.ForItem(board.Card(component)), board.Card(component).Q<Button>("more"));
        yield return GraphShots.Take(window, "59-a-cards-menu");
        window.ClosePopup(window.Popup);
        window.Reveal(system);
        yield return GraphShots.Settle(3);
        board.SelectItem(board.Frame(system), false);
        GraphMenu.Open(window, GraphMenu.ForItem(board.Frame(system)), board.Frame(system).Q<Button>("more"));
        yield return GraphShots.Take(window, "60-a-frames-menu");
        window.ClosePopup(window.Popup);
        GraphMenu.Open(window, GraphMenu.ForPin(board.Frame(system).Views[set.Id].Input("target")), board.Frame(system).Views[set.Id].Input("target"));
        yield return GraphShots.Take(window, "61-a-pins-menu");
        window.ClosePopup(window.Popup);
        board.SelectRow(board.Card(component), field.Wire("id"));
        yield return GraphShots.Take(window, "62-a-fields-details");
        board.SelectItem(board.Frame(system), false);
        yield return GraphShots.Take(window, "63-a-systems-details");
        board.ClearSelection();
        yield return GraphShots.Take(window, "64-the-files-details");
        var rename = board.Frame(system).Q<GraphName>("name");
        GraphShots.DoubleClick(rename.Q<Label>());
        Assert.IsTrue(rename.Editing);
        yield return GraphShots.Take(window, "65-renamed-by-a-double-click");
        GraphShots.Enter(rename.Field, "ScaleIt");
        Assert.AreEqual("ScaleIt", window.Doc.Get(system).Str("name"));
        yield return CheckedAgain(window);
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        window.Save();
        window.Close();
        File.Delete(path);

        // A big converted file, found with My game
        var converted = GameIn(light ? "GraphOwnerArenaLight" : "GraphOwnerArena");
        foreach (var old in Directory.GetFiles(converted, "*.tidegraph")) File.Delete(old);
        var arena = Path.Combine(converted, "Arena.tidegraph");
        Assert.IsNull(TideGraphCommand.Convert(Path.Combine(Repository, "examples", "sumo", "arena.tide"), arena));
        window = TideGraphWindow.Open(arena);
        window.ShowMyGame(true);
        window.ShowDetails(true);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        board = window.Board;
        yield return GraphShots.Settle(4);
        yield return GraphShots.Take(window, "66-a-big-file-with-my-game");
        window.MyGame.Type("stand");
        var found = window.MyGame.Shown.First(r => r.title == "Stands");
        window.Reveal(found.id);
        Assert.IsTrue(board.Chosen.Contains(found.id));
        Assert.AreEqual("item:" + found.id, window.Details.Showing, "picked, its Details are shown");
        yield return GraphShots.Take(window, "67-found-with-my-game");
        Assert.IsFalse(window.Dirty, "looking changes nothing");
        window.Close();
        File.Delete(arena);
    }

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
        // The game has a scene already, as a game does, and something in it that will have the component the
        // story makes: the file being made is the first of its code
        File.WriteAllText(Path.Combine(game, "main.tide"), "scene Main {\n}\n\nevent(Spawned) Setup(with Main)\n{\n    Spawn(Wobble);\n}\n");
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
        window.ClosePopup(window.Popup);

        // 2. A component, from the start's first choice: its name is typed, then Enter goes on to its first field
        GraphShots.Click(board.Q<Button>("start:component"));
        var wobble = window.Doc.Decls.Items.Single().Wire("id");
        Assert.AreEqual(wobble, board.Fresh);
        Assert.IsNull(board.Q<Label>(className: "tg-start-title"), "the start goes");
        var name = board.Card(wobble).Q<TextField>("name");
        name.SetValueWithoutNotify("Wobble");
        GraphShots.Key(name, KeyCode.Return);
        var decl = window.Doc.Get(wobble);
        Assert.AreEqual("Wobble", decl.Str("name"));
        var frequency = decl.Arr("members").Items.Single();
        Assert.AreEqual(frequency.Wire("id"), board.Fresh, "Enter on a new card's name starts its first field");
        name = board.Card(wobble).Q<TextField>("name" + frequency.Wire("id"));
        GraphShots.Enter(name, "frequency");
        // Its type, picked: the list completes as it's typed
        GraphShots.Click(board.Card(wobble).Q<GraphTypeChip>("type" + frequency.Wire("id")));
        var picker = (GraphTypePicker)window.Popup;
        yield return Asked(picker);
        Assert.IsTrue(picker.FromServer);
        yield return GraphShots.Take(window, "03-type-picker");
        picker.Type("fl");
        Assert.AreEqual("float", picker.Shown[0].Title);
        Assert.IsTrue(picker.PickCurrent());
        Assert.AreEqual("float", frequency.Str("type"), "the first match is taken");
        // A starting value, typed
        GraphShots.Enter(board.Card(wobble).Q("row" + frequency.Wire("id")).Q(className: "tg-row-value").Q<TextField>(), "2");
        // Enter in the field's name: the next field, of the same type
        name = board.Card(wobble).Q<TextField>("name" + frequency.Wire("id"));
        GraphShots.Key(name, KeyCode.Return);
        var phase = decl.Arr("members").Items[1];
        Assert.AreEqual("float", phase.Str("type"));
        GraphShots.Enter(board.Card(wobble).Q<TextField>("name" + phase.Wire("id")), "phase");
        yield return CheckedAgain(window);
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        StringAssert.Contains("component Wobble {\n    float frequency = 2;\n    float phase;\n}", window.Checked.Text.Replace("\r\n", "\n"));
        yield return GraphShots.Take(window, "04-component-wobble");

        // 3. A system over it, from the add menu: named, then Enter asks what it needs
        var menu = board.OpenAdd(null, window.rootVisualElement.Q<Button>("add"));
        Assert.IsTrue(menu.Pick(GraphSchema.DeclTitle("system")));
        var spin = window.Doc.Decls.Items.Last().Wire("id");
        var frame = board.Frame(spin);
        Assert.IsNotNull(frame);
        Assert.AreEqual(GraphSchema.Sentence("system", false), frame.Entry.Q<Label>(className: "tg-node-title").text);
        name = frame.Q<TextField>("name");
        name.SetValueWithoutNotify("Spin");
        GraphShots.Key(name, KeyCode.Return);
        Assert.AreEqual("Spin", window.Doc.Get(spin).Str("name"));
        yield return Until(() => window.Popup is GraphOptionList, "Enter on a new system's name asks what it needs");
        var needs = (GraphOptionList)window.Popup;
        yield return Until(() => string.IsNullOrEmpty(needs.Status), "tidels says what it can take");
        Assert.AreEqual("Wobble", needs.Shown[0].Title, "the file's own component first: " + Titles(needs));
        yield return GraphShots.Take(window, "05-what-it-needs");
        Assert.IsTrue(needs.Pick("Wobble"));
        frame = board.Frame(spin);
        var takes = window.Doc.Get(spin).Arr("params").Items.Single();
        Assert.AreEqual("read", takes.Str("mode"));
        Assert.AreEqual("wobble", takes.Str("name"));
        Assert.IsNotNull(frame.Entry.RowOf("wobble"), "what it takes has a row");
        // ...which it changes: how it's taken, from "reads" to "changes"
        var modes = frame.OpenModes(frame.Entry.Q<Button>("mode" + takes.Wire("id")), window.Doc.Get(spin).Arr("params"), takes);
        yield return Until(() => !modes.Shown.Any(o => (string)o.Data == "out"), "tidels says which ways a system has");
        yield return GraphShots.Take(window, "06-how-its-taken");
        Assert.IsTrue(modes.Pick(GraphSchema.ParamModeInfo("mut").Label));
        Assert.AreEqual("mut", takes.Str("mode"));
        frame = board.Frame(spin);
        Assert.AreEqual(GraphSchema.ParamModeInfo("mut").Label, frame.Entry.Q<Button>("mode" + takes.Wire("id")).text, "and says how it's taken, in words");
        yield return GraphShots.Take(window, "07-system-spin");

        // 4. What happens: a wire dragged from the entry's arrow and dropped asks, and "Set" is the schema's word
        var search = board.OpenSearch(frame, new Vector2(420, 60), frame.Entry.Ports.First(p => p.Port.Exec).Port);
        yield return Answered(search);
        yield return GraphShots.Take(window, "08-what-happens");
        search.Type("set");
        Assert.AreEqual(GraphSchema.Spec("assign").Title, search.Entries[0].Title, Titles(search));
        Assert.IsTrue(search.PickCurrent());
        frame = board.Frame(spin);
        var set = frame.Views.Values.Single();
        Assert.AreEqual("assign", set.Node.Str("k"));
        Assert.AreEqual("Set", set.Q<Label>(className: "tg-node-title").text);
        // Its two inputs say "pick…": no pin to wire, nothing to know
        Assert.AreEqual("pick…", set.Q("in:target").Q<Button>("pick").text);
        Assert.AreEqual(GraphSchema.PortLabel("target"), set.Q("in:target").Q<Label>(className: "tg-port-label").text);
        Assert.AreEqual("=", set.Q("in:value").Q<Button>("op").text, "how it's changed is on the value's row");
        // The check says they're holes, and the editor says it in its own words, on the rows
        yield return CheckedAgain(window);
        var holes = window.Checked.Diagnostics.Where(d => d.Id == set.Id && d.Port != null).ToList();
        CollectionAssert.AreEquivalent(new[] { "target", "value" }, holes.Select(d => d.Port), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Port} {d.Message}")));
        frame = board.Frame(spin);
        set = frame.Views.Values.Single();
        Assert.IsTrue(set.Q("in:target").ClassListContains("tg-port-row--problem"), "the empty input is marked");
        Assert.IsTrue(set.Q("in:value").ClassListContains("tg-port-row--problem"));
        StringAssert.Contains("Pick what goes in 'what'", set.ProblemText);
        StringAssert.Contains("Pick what goes in 'value'", set.ProblemText);
        StringAssert.DoesNotContain("isn't connected", set.ProblemText, "not in the compiler's words");
        Assert.IsTrue(window.ProblemOptions().Any(o => o.Title == "Pick what goes in 'what'"), "the list says the same");
        yield return GraphShots.Take(window, "09-set-with-nothing-in-it");

        // 5. What it changes: a click on "pick…" lists what this code has, and wobble opens to its fields
        GraphShots.Click(set.Q("in:target").Q<Button>("pick"));
        var values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.AreEqual("What does it change?", values.Title);
        var offered = values.Shown.First(o => o.Title == "wobble");
        Assert.AreEqual("What this code can change", offered.Section, Titles(values));
        Assert.IsTrue(values.Shown.Any(o => o.Title == "Work it out…"), Titles(values));
        yield return GraphShots.Take(window, "10-what-goes-in-what");
        Assert.IsTrue(values.Choose("wobble"));
        Assert.AreSame(values, window.Popup, "it opens to what it has");
        yield return Until(() => !values.Asking, "tidels says what a Wobble has");
        CollectionAssert.IsSubsetOf(new[] { "frequency", "phase" }, values.Shown.Select(o => o.Title).ToList(), Titles(values));
        yield return GraphShots.Take(window, "11-wobble-opened");
        Assert.IsTrue(values.Choose("phase"));
        Assert.IsNull(window.Popup);
        frame = board.Frame(spin);
        set = frame.Views.Values.Single();
        Assert.AreEqual(1, frame.Views.Count, "the name and the field are in the input: no node, no wire");
        CollectionAssert.AreEqual(new[] { "wobble", ".phase" }, set.Q("in:target").Q("chip").Query<Button>().ToList().Select(b => b.text).ToList());
        var target = GraphDocument.NodeIn(frame.Graph, set.Node.Wire("target"));
        Assert.AreEqual("member", target.Str("k"), "the file has a node for each, as tidec writes them");
        Assert.AreEqual("name", GraphDocument.NodeIn(frame.Graph, target.Wire("object")).Str("k"));
        // How it's changed
        GraphShots.Click(set.Q("in:value").Q<Button>("op"));
        var ops = (GraphOptionList)window.Popup;
        yield return GraphShots.Take(window, "12-how-its-changed");
        Assert.IsTrue(ops.Pick("+="));
        yield return CheckedAgain(window);
        frame = board.Frame(spin);
        set = frame.Views.Values.Single();
        Assert.IsFalse(set.Q("in:target").ClassListContains("tg-port-row--problem"), "filled, its mark is gone");
        Assert.AreEqual("Pick what goes in 'value'", set.ProblemText);
        Assert.AreEqual("1 problem", window.ChipText);
        yield return GraphShots.Take(window, "13-what-is-picked");

        // 6. With what: the input takes a float now (what it changes is one), so only what's a float is listed
        GraphShots.Click(set.Q("in:value").Q<Button>("pick"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.AreEqual("float", values.Wanted, "a Set's value takes what it changes");
        yield return GraphShots.Take(window, "14-what-goes-in-value");
        Assert.IsTrue(values.Choose("wobble"));
        yield return Until(() => !values.Asking, "tidels says what a Wobble has");
        Assert.IsFalse(values.Shown.Any(o => (o.Data as string) == "itself"), "a Wobble isn't a float: " + Titles(values));
        Assert.IsTrue(values.Shown.Any(o => (o.Data as string) == "everything"), "what doesn't fit can be asked for");
        yield return GraphShots.Take(window, "15-only-what-fits");
        Assert.IsTrue(values.Choose("frequency"));
        frame = board.Frame(spin);
        Assert.AreEqual(1, frame.Views.Count, "one node");
        yield return CheckedAgain(window);
        StringAssert.Contains("system Spin(mut Wobble wobble) {\n    wobble.phase += wobble.frequency;\n}", window.Checked.Text.Replace("\r\n", "\n"));
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        frame = board.Frame(spin);
        set = frame.Views.Values.Single();
        Assert.IsNull(set.ProblemText);
        // It reads as a sentence: Set / what wobble.phase / += wobble.frequency
        Assert.AreEqual("wobble.phase", frame.TextOf(frame.ShownIn(set.Node.Wire("target"))));
        Assert.AreEqual("wobble.frequency", frame.TextOf(frame.ShownIn(set.Node.Wire("value"))));
        Assert.AreEqual("+=", set.Q("in:value").Q<Button>("op").text);
        yield return GraphShots.Take(window, "16-one-node");
        // Hovering a chip lights the row it comes from and the other reads of it
        var chip = set.Q("in:value").Q("chip");
        frame.Light("wobble", 0, true);
        Assert.IsTrue(frame.Entry.RowOf("wobble").ClassListContains("tg-lit"));
        Assert.IsTrue(set.Q("in:target").Q("chip").ClassListContains("tg-lit") && chip.ClassListContains("tg-lit"));
        yield return GraphShots.Take(window, "17-where-it-comes-from");
        frame.Light("wobble", 0, false);
        Assert.IsFalse(frame.Entry.RowOf("wobble").ClassListContains("tg-lit"));

        // 7. More than a name: "Work it out…" makes a node, wired in, with its own inputs to pick
        GraphShots.Click(chip.Q<Button>("part0"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        Assert.IsTrue(values.Choose("Work it out…"));
        search = (GraphSearchPopup)window.Popup;
        yield return Answered(search);
        search.Type("multiply");
        yield return GraphShots.Take(window, "18-work-it-out");
        StringAssert.StartsWith("Multiply", search.Entries[0].Title, Titles(search));
        Assert.IsTrue(search.PickCurrent());
        frame = board.Frame(spin);
        Assert.AreEqual(2, frame.Views.Count, "the Set and what works its value out");
        var times = frame.Views.Values.First(v => v.Node.Str("k") == "binary");
        set = frame.Views.Values.First(v => v.Node.Str("k") == "assign");
        Assert.AreEqual(times.Id, set.Node.Wire("value"), "wired in");
        Assert.IsNull(set.Q("in:value").Q("chip"), "the value's row has a wire now");
        Assert.AreEqual(4, GraphDocument.Nodes(frame.Graph).Items.Count, "what was in the input went with it");
        Assert.AreEqual("pick…", times.Q("in:lhs").Q<Button>("pick").text);
        Assert.AreEqual("pick…", times.Q("in:rhs").Q<Button>("pick").text);
        yield return GraphShots.Take(window, "19-a-node-wired-in");
        // Its inputs: a field found by typing its name, a number typed
        GraphShots.Click(times.Q("in:lhs").Q<Button>("pick"));
        values = (GraphValuePicker)window.Popup;
        yield return Until(() => !values.Asking, "tidels says what can go there");
        values.Type("freq");
        Assert.AreEqual("wobble.frequency", values.Shown[0].Title, Titles(values));
        yield return GraphShots.Take(window, "20-found-by-typing");
        Assert.IsTrue(values.PickCurrent());
        frame = board.Frame(spin);
        times = frame.Views.Values.First(v => v.Node.Str("k") == "binary");
        GraphShots.Click(times.Q("in:rhs").Q<Button>("pick"));
        values = (GraphValuePicker)window.Popup;
        values.Type("2");
        Assert.AreEqual("2", values.Shown[0].Title, Titles(values));
        Assert.IsTrue(values.PickCurrent());
        yield return CheckedAgain(window);
        StringAssert.Contains("wobble.phase += wobble.frequency * 2;", window.Checked.Text);
        Assert.AreEqual("Checked", window.ChipText, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        frame = board.Frame(spin);
        times = frame.Views.Values.First(v => v.Node.Str("k") == "binary");
        Assert.AreEqual("2", times.Q("in:rhs").Q<TextField>("typed").value, "a number is typed where it is");
        yield return GraphShots.Take(window, "21-worked-out");

        // The Tide isn't on screen until it's asked for
        Assert.IsNull(window.TideView);
        window.ShowTide(true);
        StringAssert.Contains("wobble.phase += wobble.frequency * 2;", window.TideView.Text);
        yield return GraphShots.Take(window, "22-view-as-tide");
        window.ShowTide(false);
        // A value that's more than a number is a small graph of its own, beside its card: picked the same way
        var value = board.OpenValue(new GraphSlot(phase.Wire("id"), "default"));
        Assert.IsNotNull(value);
        Assert.IsTrue(value.IsValue);
        GraphShots.Click(value.Result.Q<Button>("pick"));
        values = (GraphValuePicker)window.Popup;
        Assert.AreEqual("float", values.Wanted, "a field's value takes its type");
        Assert.IsTrue(values.Choose("Work it out…"));
        search = (GraphSearchPopup)window.Popup;
        search.Type("multiply");
        Assert.IsTrue(search.PickCurrent());
        value = board.ValueFrame(value.Slot);
        times = value.Views.Values.Single();
        value.TypeInto(times.Node, new WirePath("lhs"), "2", times);
        value.TypeInto(times.Node, new WirePath("rhs"), "1.5", times);
        Assert.AreEqual(times.Id, value.Graph.Wire("root"), "what was picked for the value is the value");
        yield return CheckedAgain(window);
        StringAssert.Contains("float phase = 2 * 1.5;", window.Checked.Text);
        Assert.IsNotNull(board.Card(wobble).Q("row" + phase.Wire("id")).Q<Button>(className: "tg-link"), "its row says it's worked out, and opens it");
        yield return GraphShots.Take(window, "23-a-value-worked-out");
        board.CloseValue(value.Slot);
        Assert.IsNull(board.ValueFrame(value.Slot));

        // A wire can't leave its frame: said, where it's tried
        var other = board.AddDeclaration("function");
        Assert.IsFalse(board.Connect(board.Frame(spin).Entry.ReadPin("wobble"), board.Frame(other).Entry.Ports.First(p => p.Port.Exec), out var why));
        board.Delete(other);
        window.Save();
        Assert.IsFalse(window.Dirty);
        window.Close();
        File.Delete(path);

        // 8. Converted files, in a game of their own: a small one, then a big one found by its contents
        var converted = GameIn(light ? "GraphStoryArenaLight" : "GraphStoryArena");
        foreach (var old in Directory.GetFiles(converted, "*.tidegraph")) File.Delete(old);
        var small = Path.Combine(converted, "Systems.tidegraph");
        Assert.IsNull(TideGraphCommand.Convert(Path.Combine(Repository, "docs", "examples", "systems.tide"), small));
        window = TideGraphWindow.Open(small);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        board = window.Board;
        yield return GraphShots.Settle(4);
        var code = window.Doc.Elements.Where(e => e.Role == GraphRole.Decl && board.Frame(e.Id) != null).Select(e => board.Frame(e.Id)).ToList();
        Assert.IsNotEmpty(code);
        var inFile = code.Sum(f => GraphDocument.Nodes(f.Graph)?.Items.Count ?? 0);
        foreach (var f in code) f.Classify();
        var drawn = code.Sum(f => f.Visible().Count());
        Assert.Less(drawn, inFile * 0.6f, $"simple values are in the inputs: {drawn} nodes drawn of the file's {inFile}");
        board.FitAll();
        yield return GraphShots.Take(window, "30-a-small-file");
        var busiest = code.OrderByDescending(f => GraphDocument.Nodes(f.Graph)?.Items.Count ?? 0).First();
        window.Reveal(busiest.Id);
        yield return GraphShots.Take(window, "31-its-busiest-code");
        // The same with a node for every name and member, as the file has them: what the inputs spare
        GraphFrame.DrawEveryNode = true;
        try
        {
            board.Reload();
            yield return GraphShots.Settle(4);
            window.Reveal(busiest.Id);
            yield return GraphShots.Take(window, "32-with-a-node-for-each");
        }
        finally
        {
            GraphFrame.DrawEveryNode = false;
        }
        Assert.IsFalse(window.Dirty, "looking changes nothing");
        window.Close();
        File.Delete(small);

        var arena = Path.Combine(converted, "Arena.tidegraph");
        Assert.IsNull(TideGraphCommand.Convert(Path.Combine(Repository, "examples", "sumo", "arena.tide"), arena));
        window = TideGraphWindow.Open(arena);
        GraphShots.Size(window);
        if (light) window.ForceTheme(true);
        board = window.Board;
        yield return GraphShots.Settle(4);
        Assert.Less(board.NodesDrawn, 400, "what's on screen is drawn, of over a thousand nodes");
        yield return GraphShots.Take(window, "33-a-big-file");
        // My game lists it by the schema's groups, and finds one by name
        window.ShowMyGame(true, true);
        Assert.IsNotNull(window.MyGame);
        CollectionAssert.AreEqual(GraphSchema.Groups.Select(g => g.Title.ToUpperInvariant()), window.MyGame.Groups, "by the schema's groups");
        window.MyGame.Type("sta");
        var found = window.MyGame.Shown.First(r => r.title == "Stands");
        yield return GraphShots.Take(window, "34-my-game-searched");
        window.Reveal(found.id);
        Assert.AreEqual(GraphLevel.Near, board.Frame(found.id).Level, "picked, it's brought on screen");
        Assert.IsTrue(board.Chosen.Contains(found.id));
        window.MyGame.Type("");
        yield return GraphShots.Take(window, "35-jumped-to");
        board.ZoomAbout(new Vector2(640, 380), 0.3f);
        Assert.AreEqual(0, board.NodesDrawn, "from far away, names");
        yield return GraphShots.Take(window, "36-from-far-away");
        Assert.IsFalse(window.Dirty, "looking changes nothing");
        window.Close();
        File.Delete(arena);
    }
}
