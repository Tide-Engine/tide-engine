using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEditor.Compilation;
using UnityEngine;
using UnityEngine.TestTools;

// Between the game's Tide types and their C# twins (Packages/com.tide.game): a generated component's script opens the
// Tide declaration it's made of, its C# carries the declaration's doc, and a script's C# error on a name Tide renamed
// or removed says what became of it in the Console.
public class TypeBoundaryTests
{
    const BindingFlags Any = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    static readonly Type entries = Type.GetType("UnityEditor.LogEntries, UnityEditor");
    static readonly Type entryType = Type.GetType("UnityEditor.LogEntry, UnityEditor");

    static int Count() => (int)entries.GetMethod("GetCount", Any).Invoke(null, null);

    // The Console's entries from `first` on: message, file and line
    static List<(string message, string file, int line)> From(int first)
    {
        var found = new List<(string, string, int)>();
        var entry = Activator.CreateInstance(entryType);
        entries.GetMethod("StartGettingEntries", Any).Invoke(null, null);
        try
        {
            for (var i = first; i < Count(); i++)
            {
                entries.GetMethod("GetEntryInternal", Any).Invoke(null, new[] { i, entry });
                string Text(string name) => (string)entryType.GetField(name, Any)?.GetValue(entry);
                found.Add((Text("message"), Text("file"), (int)entryType.GetField("line", Any).GetValue(entry)));
            }
        }
        finally
        {
            entries.GetMethod("EndGettingEntries", Any).Invoke(null, null);
        }
        return found;
    }

    static int LineOf(string asset, string text)
    {
        var lines = File.ReadAllLines(Path.Combine(TideSettings.ProjectFolder, asset));
        return Array.FindIndex(lines, l => l.StartsWith(text)) + 1;
    }

    // tide unity builds the Sample as the editor opens it: its outline, once it's written
    static IEnumerator Built()
    {
        var deadline = EditorApplication.timeSinceStartup + 300;
        while (!TideDeclarations.Find("Ball", out _, out _, out _) && EditorApplication.timeSinceStartup < deadline) yield return null;
        Assert.IsTrue(TideDeclarations.Find("Ball", out _, out _, out _), "tide unity built the Sample, writing Library/Tide/outline.json");
    }

    [UnityTest]
    public IEnumerator AComponentsScriptOpensItsTideDeclaration()
    {
        yield return Built();
        var ball = AssetDatabase.LoadAssetAtPath<MonoScript>("Packages/com.tide.game/Components/Ball.cs");
        Assert.IsNotNull(ball);
        Assert.AreEqual("Ball", TideDeclarations.TideNameOf(ball));
        Assert.IsTrue(TideDeclarations.Find("Ball", out var file, out var line, out var column));
        Assert.AreEqual("Assets/Tide/Bounce.tide", file);
        Assert.AreEqual(LineOf(file, "component Ball"), line);
        Assert.AreEqual(11, column);

        // Opening it, as Edit Script and a double click on the Script field do, opens that line
        var opened = new List<(string path, int line, int column)>();
        var before = TideDeclarations.Opener;
        TideDeclarations.Opener = (path, l, c) =>
        {
            opened.Add((path, l, c));
            return true;
        };
        try
        {
            Assert.IsTrue(AssetDatabase.OpenAsset(ball));
        }
        finally
        {
            TideDeclarations.Opener = before;
        }
        Assert.AreEqual(1, opened.Count);
        StringAssert.EndsWith("/Assets/Tide/Bounce.tide", opened[0].path);
        Assert.IsTrue(Path.IsPathRooted(opened[0].path));
        Assert.AreEqual(line, opened[0].line);

        // Another script is no declaration of the game's, and opens as itself
        var tint = AssetDatabase.LoadAssetAtPath<MonoScript>("Assets/Scripts/BallTint.cs");
        Assert.IsNotNull(tint);
        Assert.IsNull(TideDeclarations.TideNameOf(tint));
        Assert.IsFalse(TideDeclarations.Open(tint));
    }

    // Rider's and Visual Studio's hovers: the declaration's comment lines, and the file it's in (not its line, so a line
    // moving above it changes no C#)
    [UnityTest]
    public IEnumerator GeneratedCSharpCarriesTidesDocs()
    {
        yield return Built();
        var ball = File.ReadAllText(Path.Combine(TideSettings.ProjectFolder, "Packages/com.tide.game/Components/Ball.cs")).Replace("\r", "");
        StringAssert.Contains("    /// <summary>\n" +
                              "    /// Each player's ball, which Tide rolls and Unity shows (Roller.prefab)\n" +
                              "    /// <para>Tide's component Ball, declared in Assets/Tide/Bounce.tide.</para>\n" +
                              "    /// </summary>\n", ball);
        StringAssert.Contains("        /// <summary>Metres a second</summary>\n        public global::UnityEngine.Vector3 velocity", ball);
        StringAssert.Contains("        /// <summary>This machine's, as its local handler counts its bounces (Heard)</summary>\n", ball);
        StringAssert.DoesNotContain("line", ball.Split(new[] { "<para>" }, StringSplitOptions.None)[1].Split('\n')[0]);
    }

    // The generated C#'s doc comments are documentation C# reads: compiled with /doc (its files disable warnings, so
    // the XML is what tells), each summary where it belongs, none ill-formed
    [UnityTest]
    public IEnumerator GeneratedDocsAreWellFormedXml()
    {
        yield return Built();
        var package = Path.Combine(TideSettings.ProjectFolder, "Packages", "com.tide.game");
        var sources = Directory.GetFiles(package, "*.cs", SearchOption.AllDirectories);
        const string folder = "Temp/TideDocsProbe";
        Directory.CreateDirectory(folder);
        try
        {
            var modules = Directory.GetFiles(Path.Combine(EditorApplication.applicationContentsPath, "Managed", "UnityEngine"), "UnityEngine*.dll");
            var references = modules.Append(typeof(Tide.TideComponent).Assembly.Location).ToArray();
            var messages = Compile(sources, folder + "/Game.dll", $"/unsafe /doc:\"{folder}/Game.xml\"", references);
            Assert.IsFalse(messages.Any(m => m.type == CompilerMessageType.Error), string.Join("\n", messages.Select(m => m.message).Take(10)));
            var xml = File.ReadAllText(folder + "/Game.xml");
            StringAssert.DoesNotContain("Badly formed XML", xml);
            var doc = new System.Xml.XmlDocument();
            doc.LoadXml(xml);
            string Summary(string member) => doc.SelectSingleNode($"//member[@name='{member}']/summary")?.InnerText ?? "";
            StringAssert.Contains("Each player's ball, which Tide rolls and Unity shows (Roller.prefab)", Summary("T:Tide.Game.Ball"));
            StringAssert.Contains("Tide's component Ball, declared in Assets/Tide/Bounce.tide.", Summary("T:Tide.Game.Ball"));
            StringAssert.Contains("Metres a second", Summary("F:Tide.Game.Ball.velocity"));
            StringAssert.Contains("A ball's trail", Summary("T:Tide.Game.Trail"));
            StringAssert.Contains("Tide's singleton Extras", Summary("T:Tide.Game.Extras"));
        }
        finally
        {
            Directory.Delete(folder, true);
        }
    }

    // A script compiled apart from the project's, by the Roslyn compiler Unity has: its errors as Unity's compile gives
    // them
    static CompilerMessage[] CompileApart(string source, string folder)
    {
        return Compile(new[] { source }, folder + "/Probe.dll", "",
                       typeof(MonoBehaviour).Assembly.Location, typeof(Tide.TideComponent).Assembly.Location, typeof(Tide.Game.Ball).Assembly.Location);
    }

    static CompilerMessage[] Compile(string[] sources, string assembly, string options, params string[] references)
    {
        var contents = EditorApplication.applicationContentsPath;
        var sdk = Path.Combine(contents, "DotNetSdk");
        var dotnet = Path.Combine(sdk, Application.platform == RuntimePlatform.WindowsEditor ? "dotnet.exe" : "dotnet");
        var csc = Directory.GetFiles(Path.Combine(sdk, "sdk"), "csc.dll", SearchOption.AllDirectories)
            .First(p => p.Replace('\\', '/').Contains("/Roslyn/bincore/"));
        references = references.Prepend(Path.Combine(contents, "NetStandard", "ref", "2.1.0", "netstandard.dll")).ToArray();
        // In a response file: the game's sources are many
        var response = assembly + ".rsp";
        File.WriteAllLines(response, new[] { $"/nologo /noconfig /nostdlib /target:library /out:\"{assembly}\" {options}" }
            .Concat(references.Select(r => $"/reference:\"{r}\""))
            .Concat(sources.Select(source => $"\"{source}\"")));
        var arguments = $"exec \"{csc}\" \"@{response}\"";
        var start = new System.Diagnostics.ProcessStartInfo(dotnet, arguments)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        string output;
        using (var process = System.Diagnostics.Process.Start(start))
        {
            output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
            process.WaitForExit();
        }
        var located = new System.Text.RegularExpressions.Regex(@"^(?<file>.+?)\((?<line>\d+),(?<column>\d+)\): (?<kind>error|warning) ");
        var messages = new List<CompilerMessage>();
        foreach (var text in output.Split('\n'))
        {
            var match = located.Match(text.Trim());
            if (!match.Success) continue;
            messages.Add(new CompilerMessage
            {
                message = text.Trim(),
                file = match.Groups["file"].Value,
                line = int.Parse(match.Groups["line"].Value),
                column = int.Parse(match.Groups["column"].Value),
                type = match.Groups["kind"].Value == "error" ? CompilerMessageType.Error : CompilerMessageType.Warning,
            });
        }
        return messages.ToArray();
    }

    [Test]
    public void AScriptsErrorOnARenamedNameSaysWhatBecameOfIt()
    {
        Assume.That(entries != null && entryType != null, "this Unity has the Console's entries");
        LogAssert.ignoreFailingMessages = true;
        // The record tide keeps, as it would be after renames and a removal
        var record = TideRenames.RecordPath;
        var had = File.Exists(record) ? File.ReadAllText(record) : null;
        Directory.CreateDirectory(Path.GetDirectoryName(record) ?? ".");
        File.WriteAllText(record, "{\"renamed\": [\n" +
                                  "  {\"was\": \"Ball.speed\", \"now\": \"Ball.velocity\", \"file\": \"Assets/Tide/Bounce.tide\", \"line\": 124},\n" +
                                  "  {\"was\": \"Spinner.rate\", \"now\": \"Spinner.speed\", \"file\": \"Assets/Tide/Bounce.tide\", \"line\": 17},\n" +
                                  "  {\"was\": \"Paddle\", \"removed\": true}\n]}\n");
        // A script of the old names
        const string folder = "Temp/TideRenameProbe";
        Directory.CreateDirectory(folder);
        const string probe = folder + "/Probe.cs";
        File.WriteAllText(probe, "using Tide.Game;\n\npublic static class Probe\n{\n    public static void Use(Ball ball, Tide.Game.Spinner spinner)\n    {\n" +
                                 "        ball.speed = default;\n        spinner.rate = 1;\n        Paddle paddle = null;\n    }\n}\n");
        try
        {
            var messages = CompileApart(probe, folder);
            Assert.AreEqual(3, messages.Count(m => m.type == CompilerMessageType.Error), string.Join("\n", messages.Select(m => m.message)));

            var notes = TideRenames.NotesFor(messages, TideRenames.Read());
            var texts = notes.Select(n => n.Message).ToArray();
            CollectionAssert.Contains(texts, "tide: `Ball.speed` is now `Ball.velocity` (Assets/Tide/Bounce.tide:124)");
            CollectionAssert.Contains(texts, "tide: `Spinner.rate` is now `Spinner.speed` (Assets/Tide/Bounce.tide:17)");
            CollectionAssert.Contains(texts, "tide: `Paddle` was removed from the game's Tide code");
            var renamed = notes.First(n => n.Message.Contains("Ball.speed"));
            Assert.AreEqual(("Assets/Tide/Bounce.tide", 124), (renamed.File, renamed.Line), "a rename's note opens the new declaration");
            var removed = notes.First(n => n.Message.Contains("Paddle"));
            StringAssert.EndsWith("Probe.cs", removed.File.Replace('\\', '/'));
            Assert.AreEqual(9, removed.Line, "a removal's note opens the script where it's used");
            // Errors the record doesn't explain have none
            Assert.AreEqual(3, notes.Count);

            // In the Console beside Unity's errors, each once, until the scripts compile again
            TideRenames.Clear();
            var before = Count();
            TideRenames.Explain(messages);
            TideRenames.Explain(messages);
            var logged = From(before);
            Assert.AreEqual(3, logged.Count);
            Assert.IsTrue(logged.Any(e => e.message.StartsWith("tide: `Ball.speed` is now `Ball.velocity`") && e.file == "Assets/Tide/Bounce.tide" && e.line == 124));
            TideRenames.Clear();
            Assert.AreEqual(before, Count());

            // Errors the Console has already, as the editor's first compile leaves them before the package loads
            const int compile = 0x74696478;
            TideBuildService.LogLocated("Assets/Scripts/Probe.cs(7,14): error CS1061: 'Ball' does not contain a definition for 'speed'",
                                        "Assets/Scripts/Probe.cs", 7, 14, true, compile);
            try
            {
                Assert.IsTrue(TideRenames.ConsoleErrors().Any(m => m.message.Contains("'speed'")));
                before = Count();
                TideRenames.ExplainConsole();
                var explained = From(before);
                Assert.AreEqual(1, explained.Count);
                StringAssert.StartsWith("tide: `Ball.speed` is now `Ball.velocity` (Assets/Tide/Bounce.tide:124)", explained[0].message);
            }
            finally
            {
                TideBuildService.RemoveLocated(compile);
            }
        }
        finally
        {
            TideRenames.Clear();
            if (had != null) File.WriteAllText(record, had);
            else File.Delete(record);
            Directory.Delete(folder, true);
        }
    }
}
