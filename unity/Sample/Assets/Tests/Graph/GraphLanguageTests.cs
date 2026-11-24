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
// game's code and on their nodes; each node's value has its type; the search offers what completion offers there,
// and what it makes lowers to the Tide it stands for; the server starts again when it stops, and goes when the last
// graph's window closes.
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
        foreach (var name in new[] { "Schema.tidegraph", "Layout.tidegraph", "Members.tidegraph" })
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
    static string Game
    {
        get
        {
            var folder = Path.Combine(Path.GetFullPath("Temp"), "GraphLanguageTests");
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, "tide.packages"), "# The graph editor's language tests' game\n");
            return folder;
        }
    }

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

    // Hit's code, in the canvas (the first open is Bounce's, which has none)
    static void ShowHit(TideGraphWindow window) => window.Show(new GraphSlot(10, "body"));

    static IEnumerator Checked(TideGraphWindow window)
    {
        for (var clock = Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 90;) yield return null;
        Assert.IsFalse(window.CheckPending, "the check finishes");
        Assert.IsNotNull(window.Checked);
        Assert.AreEqual("tidels", window.Checked.Checker, "tidels checks it: " + window.Checked.CheckerNote + " " + TideLanguageClient.Shared?.Errors);
    }

    static IEnumerator Answered(GraphSearchPopup popup)
    {
        for (var clock = Stopwatch.StartNew(); popup.Asking && clock.Elapsed.TotalSeconds < 90;) yield return null;
        Assert.IsFalse(popup.Asking, "tidels answers the search");
    }

    static GraphSearchPopup Popup(GraphCanvas canvas)
    {
        var popup = canvas.Q<GraphSearchPopup>();
        Assert.IsNotNull(popup, "the search is open");
        return popup;
    }

    static string Titles(GraphSearchPopup popup) => string.Join(", ", popup.Shown.Select(e => $"{e.Title} [{e.Group}]"));

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
        ShowHit(window);
        yield return Checked(window);

        // The unknown call's error, type-checked, on its node; drawn on the statement it's drawn with
        var error = window.Checked.Diagnostics.FirstOrDefault(d => d.Id == 13);
        Assert.IsNotNull(error, "tidels names the call: " + string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
        Assert.AreEqual("error", error.Severity);
        StringAssert.Contains("Missing", error.Message);
        StringAssert.Contains("Missing()", error.Detail, "with its line of Tide");
        StringAssert.Contains("Missing", window.Canvas.Views[12].tooltip);
        StringAssert.Contains("tidels", window.CheckerSays);
        StringAssert.Contains("return ball.hits;", window.Checked.Text.Replace("\r\n", "\n"));
        // A click on it selects its node
        window.JumpTo(error);
        Assert.IsTrue(window.Canvas.Selection.Contains(12));

        // Each value's type: on the node and its output, and on the input it's wired to
        Assert.AreEqual("int", window.Checked.Types[16]);
        Assert.AreEqual("Ball", window.Checked.Types[17]);
        var member = window.Canvas.Views[16];
        Assert.AreEqual("int", member.TypeText);
        Assert.AreEqual("int", member.DataOut.Type);
        Assert.AreEqual("Ball", member.Ports.First(p => p.Port.Input && !p.Port.Exec).Type, "its object's input");
        Assert.AreNotEqual(TideGraphTypes.Untyped, member.DataOut.Tint);
        // ...and what tidels says of it in its tooltip
        string hover = null;
        TideGraphLanguage.Hover(window, 16, text => hover = text);
        for (var clock = Stopwatch.StartNew(); hover == null && clock.Elapsed.TotalSeconds < 30;) yield return null;
        StringAssert.Contains("int hits", hover);
        member.SetHover(hover);
        StringAssert.Contains("int hits", member.tooltip);
        window.Close();
    }

    [UnityTest]
    public IEnumerator TheSearchOffersWhatCompletionDoes()
    {
        NeedTidels();
        var path = WriteGame("Search.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        ShowHit(window);
        yield return Checked(window);
        var canvas = window.Canvas;

        // A wire from the ball dropped on nothing: its fields and its method, with the method's parameter
        canvas.OpenSearch(new Vector2(300, 300), canvas.Views[18].DataOut.Port);
        var popup = Popup(canvas);
        yield return Answered(popup);
        Assert.IsTrue(popup.Shown.Any(e => e.Title == "hits" && e.Group == "Members of Ball"), Titles(popup));
        var bounce = popup.Shown.FirstOrDefault(e => e.Title == "Bounce(height)");
        Assert.IsNotNull(bounce, Titles(popup));
        Assert.AreEqual(0, popup.Shown.ToList().IndexOf(popup.Shown.First(e => e.Group == "Members of Ball")), "the value's members come first");
        Assert.AreEqual(window.Doc.Text, window.Checked.Json, "asking leaves the graph as it was");

        // The method, made from it: wired to the ball, with an input for its parameter
        canvas.Insert(bounce, new Vector2(300, 300), canvas.Views[18].DataOut.Port);
        var graph = window.Doc.GetGraph(window.Slot);
        var method = GraphDocument.Nodes(graph).Items.Last(n => n.Str("k") == "method");
        Assert.AreEqual("Bounce", method.Str("name"));
        Assert.AreEqual(18, method.Wire("object"));
        Assert.AreEqual(1, method.Arr("args").Items.Count);
        // Called as a statement before the others, with a value for its height: what it lowers to
        window.Edit("Call it", () =>
        {
            var height = window.Doc.AddNode(graph, "float", 0, 400);
            height.Set("text", JValue.Of("2.5"));
            method.Arr("args").Items[0] = JValue.Of(height.Wire("id"));
            var eval = window.Doc.AddNode(graph, "eval", -200, 0);
            eval.Set("value", JValue.Of(method.Wire("id")));
            eval.Set("next", JValue.Of(graph.Wire("entry")));
            graph.Set("entry", JValue.Of(eval.Wire("id")));
        });
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        StringAssert.Contains("ball.Bounce(2.5);\n    Missing();", window.Checked.Text.Replace("\r\n", "\n"));
        Assert.IsFalse(window.Checked.Diagnostics.Any(d => d.Severity == "error" && d.Id != 13), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));

        // A member node picks its member from its object's
        canvas.OpenMemberSearch(16, new Vector2(100, 100));
        popup = Popup(canvas);
        yield return Answered(popup);
        Assert.IsTrue(popup.Shown.All(e => e.Group == "Members of Ball"), Titles(popup));
        canvas.Retarget(popup.Shown.First(e => e.Title == "Bounce(height)"), 16);
        var retargeted = GraphDocument.NodeIn(window.Doc.GetGraph(window.Slot), 16);
        Assert.AreEqual("method", retargeted.Str("k"));
        Assert.AreEqual("Bounce", retargeted.Str("name"));
        Assert.AreEqual(17, retargeted.Wire("object"));
        Undo.PerformUndo();
        Assert.AreEqual("member", GraphDocument.NodeIn(window.Doc.GetGraph(window.Slot), 16).Str("k"));

        // Where a statement goes: the game's functions as calls, its types as values of them, the parameter once
        canvas.OpenSearch(new Vector2(200, 200), null);
        popup = Popup(canvas);
        yield return Answered(popup);
        Assert.IsTrue(popup.Shown.Any(e => e.Title == "Hit(ball)" && e.Statement), Titles(popup));
        Assert.IsTrue(popup.Shown.Any(e => e.Title == "Ball { }"), Titles(popup));
        Assert.IsTrue(popup.Shown.Any(e => e.Title == "Math" && e.Group == "Namespaces"), Titles(popup));
        Assert.AreEqual(1, popup.Shown.Count(e => e.Title == "ball"), "the parameter, once: " + Titles(popup));
        // A statement's flow takes the call as a statement
        canvas.OpenSearch(new Vector2(200, 200), canvas.Views[GraphNodeView.EntryId].Ports[0].Port);
        popup = Popup(canvas);
        yield return Answered(popup);
        Assert.IsTrue(popup.Shown.Any(e => e.Title == "Hit(ball)"), Titles(popup));
        Assert.IsFalse(popup.Shown.Any(e => e.Title == "Ball { }"), "a statement's flow takes no value");
        canvas.CloseSearch();
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
        Assert.IsNull(TideLanguageClient.Shared);
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

    static IEnumerator Until(Func<bool> done, string what)
    {
        for (var clock = Stopwatch.StartNew(); !done() && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsTrue(done(), what);
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

    // Auto layout is tidels's (tide/graphLayout), tidec's own: statements along next, a statement's values to its
    // left; without tidels the nodes stay where they are
    [UnityTest]
    public IEnumerator AutoLayoutIsTidecsFromTidels()
    {
        NeedTidels();
        var path = WriteGame("Layout.tidegraph", BallGraph);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        ShowHit(window);
        yield return Checked(window);
        double X(int id) => GraphDocument.NodeIn(window.Doc.GetGraph(window.Slot), id).Get("x").Number;
        double Y(int id) => GraphDocument.NodeIn(window.Doc.GetGraph(window.Slot), id).Get("y").Number;
        var text = window.Checked.Text;

        window.Canvas.AutoLayout();
        yield return Until(() => !window.Canvas.LayoutPending, "tidels lays it out");
        Assert.Less(X(12), X(15), "statements left to right along next");
        Assert.Less(X(13), X(12), "a statement's value to its left");
        Assert.Less(X(16), X(15), "the return's value to its left");
        Assert.Less(X(17), X(16), "...and that value's to its left");
        Assert.Greater(Y(18), Y(16), "what nothing reads on a row below");
        // tidels's layout of the body, as it gives it
        JValue layout = null;
        TideGraphLanguage.Layout(window.Doc.GetGraph(window.Slot), (l, _) => layout = l);
        yield return Until(() => layout != null, "tidels gives the layout");
        foreach (var id in new[] { 12, 13, 15, 16, 17, 18 })
        {
            Assert.AreEqual(layout.Arr(id.ToString()).Items[0].Number, X(id), $"@{id}'s x");
            Assert.AreEqual(layout.Arr(id.ToString()).Items[1].Number, Y(id), $"@{id}'s y");
        }
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        Assert.AreEqual(text, window.Checked.Text, "the layout moves nodes, nothing else");
        Undo.PerformUndo();
        Assert.AreEqual(300, X(15), "undone");

        // Without tidels: nothing moves
        TideGraphCommand.TidelsOverride = Path.Combine(Game, "not-tidels.exe");
        try
        {
            window.Canvas.AutoLayout();
            Assert.IsFalse(window.Canvas.LayoutPending);
            Assert.AreEqual(300, X(15));
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
        ShowHit(window);
        yield return Checked(window);
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
        var context = new GraphSearchContext { Doc = window.Doc, Graph = window.Doc.GetGraph(window.Slot), Slot = window.Slot, Window = window, Pending = window.Canvas.Views[17].DataOut.Port };
        TideGraphLanguage.Complete(context, (e, _) => found = e ?? new List<GraphSearchEntry>());
        yield return Until(() => found != null, "the search is answered");
        var hits = found.FirstOrDefault(e => e.Title == "hits");
        Assert.IsNotNull(hits, string.Join(", ", found.Select(e => e.Title)));
        Assert.AreEqual("Members of Ball", hits.Group);
        var bounce = found.FirstOrDefault(e => e.Title == "Bounce(height)");
        Assert.IsNotNull(bounce, string.Join(", ", found.Select(e => e.Title)));
        Assert.AreEqual(window.Doc.Text, window.Checked.Json, "asking leaves the graph as it was");
        var scratch = GraphDocument.Parse(window.Doc.Text);
        var made = bounce.Make(scratch, scratch.GetGraph(window.Slot), Vector2.zero);
        Assert.AreEqual("method", made.Str("k"));
        Assert.AreEqual("Bounce", made.Str("name"));
        Assert.AreEqual(1, made.Arr("args").Items.Count);
        var field = hits.Make(scratch, scratch.GetGraph(window.Slot), Vector2.zero);
        Assert.AreEqual("member", field.Str("k"));
        Assert.AreEqual("hits", field.Str("member"));
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
            Assert.IsTrue(window.Canvas.Selection.Contains(12), "its node, drawn with its statement");

            // Checked with the Sample's game: its event is a value the search offers, its commands are calls
            yield return Checked(window);
            Assert.IsTrue(window.Checked.Diagnostics.Any(d => d.Id == 13 && d.Message.Contains("NotThere")), string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
            window.Canvas.OpenSearch(new Vector2(200, 200), null);
            var popup = Popup(window.Canvas);
            yield return Answered(popup);
            Assert.IsTrue(popup.Shown.Any(e => e.Title == "Bounced { }"), Titles(popup));
            Assert.IsTrue(popup.Shown.Any(e => e.Title == "Toss(on)"), Titles(popup));
            window.Canvas.CloseSearch();
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
}
