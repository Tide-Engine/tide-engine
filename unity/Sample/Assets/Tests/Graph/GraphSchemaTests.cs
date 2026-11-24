using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NUnit.Framework;
using Tide.Editor;
using UnityEngine;

// The graph editor reads tidec's graph schema (compiler/src/graph_schema.json, `tide graph --schema`), the one every
// graph editor reads: what it uses is that schema, and every node kind's ports are the schema's.
public class GraphSchemaTests
{
    static string tidec;

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
    public void FindTheCompiler()
    {
        tidec = Find("TIDE_TIDEC", "tidec");
        TideGraphCommand.TidecOverride = tidec;
        GraphSchema.Reload();
    }

    [OneTimeTearDown]
    public void Forget()
    {
        TideGraphCommand.TidecOverride = null;
    }

    static void NeedTidec()
    {
        if (tidec == null) Assert.Ignore("No tidec: build this checkout's (cmake --build --preset sanitize --target tidec) or set TIDE_TIDEC");
    }

    static string Printed(string program, string arguments)
    {
        var code = TideGraphCommand.Run(program, arguments, null, out var output, out var errors, 30000);
        Assert.AreEqual(0, code, $"{program} {arguments}: {errors}");
        return output;
    }

    [Test]
    public void TheSchemaIsTidecs()
    {
        NeedTidec();
        Assert.IsTrue(GraphSchema.Available, GraphSchema.Failure);
        var used = TideLanguageClient.Compact(GraphSchema.Json);
        Assert.AreEqual(TideLanguageClient.Compact(GraphJson.Parse(Printed(tidec, "--graph-schema"))), used, "tidec --graph-schema");
        // ...which is `tide graph --schema`'s, where this build has a tide
        var tide = Path.Combine(Path.GetDirectoryName(tidec) ?? "", Application.platform == RuntimePlatform.WindowsEditor ? "tide.exe" : "tide");
        if (File.Exists(tide)) Assert.AreEqual(TideLanguageClient.Compact(GraphJson.Parse(Printed(tide, "graph --schema"))), used, "tide graph --schema");
        // ...and the checkout's file
        var file = Path.Combine(Repository, "compiler", "src", "graph_schema.json");
        if (File.Exists(file)) Assert.AreEqual(TideLanguageClient.Compact(GraphJson.Parse(File.ReadAllText(file))), used, "graph_schema.json");
        // Kept in the Library for the next domain reload, with the program it's of
        var kept = GraphJson.Parse(File.ReadAllText(GraphSchema.CachePath));
        Assert.AreEqual(Path.GetFullPath(tidec), kept.Str("program"));
        Assert.AreEqual(used, TideLanguageClient.Compact(kept.Get("schema")));
        // What the editor's tables are is the schema's
        var nodes = GraphSchema.Json.Arr("nodes").Items;
        CollectionAssert.AreEqual(nodes.Select(n => n.Str("k")), GraphSchema.Nodes.Select(n => n.K));
        CollectionAssert.AreEqual(GraphSchema.Json.Arr("declarations").Items.Select(d => d.Str("kind")), GraphSchema.DeclKinds);
        Assert.AreEqual(GraphSchema.Json.Get("operators").Arr("binary").Items.Count, GraphSchema.BinaryOps.Length);
        CollectionAssert.AreEqual(new[] { "id", "k", "x", "y", "op", "lhs", "rhs", "comment", "next" }, GraphSchema.NodeOrder("binary"));
        CollectionAssert.AreEqual(new[] { "dev", "local", "tweak" }, GraphSchema.Modifiers("const", false));
    }

    // Every node kind's ports, made as the schema says: its single inputs, lists, items (a case's labels, an arm's
    // pattern and value), named inputs, exec outputs and next
    [Test]
    public void PortsOfEveryKindAreTheSchemas()
    {
        NeedTidec();
        var doc = GraphDocument.Empty();
        foreach (var spec in GraphSchema.Json.Arr("nodes").Items)
        {
            var k = spec.Str("k");
            var n = doc.NewNode(k, 0, 0);
            var inputs = new List<string>();
            var outputs = new List<string>();
            foreach (var key in Strings(spec.Arr("inputs"))) inputs.Add(key);
            foreach (var list in spec.Arr("lists")?.Items ?? new List<JValue>())
            {
                n.Set(list.Str("key"), JValue.ArrayOf(new[] { JValue.Null(), JValue.Null() }));
                if (list.Str("names") != null) n.Set(list.Str("names"), JValue.Strings(new[] { "", "named" }));
                inputs.Add(list.Str("key") + ".0");
                inputs.Add(list.Str("key") + ".1");
            }
            foreach (var items in spec.Arr("items")?.Items ?? new List<JValue>())
            {
                var key = items.Str("key");
                var made = JValue.NewArray();
                for (var i = 0; i < 2; i++)
                {
                    var item = JValue.NewObject();
                    foreach (var wire in Strings(items.Arr("wires")))
                    {
                        if (wire.EndsWith("[]"))
                        {
                            var w = wire.Substring(0, wire.Length - 2);
                            item.Set(w, JValue.ArrayOf(new[] { JValue.Null(), JValue.Null() }));
                            inputs.Add($"{key}.{i}.{w}.0");
                            inputs.Add($"{key}.{i}.{w}.1");
                        }
                        else
                        {
                            item.Set(wire, JValue.Null());
                            inputs.Add($"{key}.{i}.{wire}");
                        }
                    }
                    if (items.Str("exec") != null) outputs.Add($"{key}.{i}.{items.Str("exec")}");
                    made.Items.Add(item);
                }
                n.Set(key, made);
            }
            if (spec.Str("map") != null)
            {
                var map = JValue.NewObject();
                map.Set("a", JValue.Null());
                map.Set("b", JValue.Null());
                n.Set(spec.Str("map"), map);
                inputs.Add(spec.Str("map") + ".a");
                inputs.Add(spec.Str("map") + ".b");
            }
            var exec = spec.Flag("exec");
            if (exec)
            {
                outputs.InsertRange(0, Strings(spec.Arr("outputs")));
                if (!spec.Flag("ends")) outputs.Add("next");
            }

            var ports = GraphPorts.Of(n);
            Assert.AreEqual(exec, ports.Any(p => p.Input && p.Exec), $"{k}: an exec input iff it's a statement");
            Assert.AreEqual(spec.Flag("value"), ports.Any(p => !p.Input && !p.Exec), $"{k}: an output iff it has a value");
            CollectionAssert.AreEquivalent(inputs, ports.Where(p => p.Input && !p.Exec).Select(p => p.Path.ToString()), $"{k}'s data inputs");
            CollectionAssert.AreEquivalent(outputs, ports.Where(p => !p.Input && p.Exec).Select(p => p.Path.ToString()), $"{k}'s exec outputs");
            if (spec.Flag("ends"))
            {
                n.Set("next", JValue.Of(99));
                Assert.IsTrue(GraphPorts.Of(n).Any(p => p.Path?.ToString() == "next"), $"{k}: a next that's wired is shown");
                n.Remove("next");
            }
            // Each wire is kept where tidec reads it, in tidec's key order, and goes as tidec writes it
            foreach (var p in ports.Where(p => p.Path != null))
            {
                p.Path.Set(n, 7);
                Assert.AreEqual(7, p.Path.Get(n), $"{k}: {p.Path}");
                var order = GraphSchema.NodeOrder(k);
                var ranks = n.Keys.Select(key => Array.IndexOf(order, key)).Where(r => r >= 0).ToList();
                CollectionAssert.IsOrdered(ranks, $"{k}'s keys in tidec's order after setting {p.Path}: {string.Join(", ", n.Keys)}");
                p.Path.Set(n, 0);
                Assert.AreEqual(0, p.Path.Get(n));
                if (p.Path.Parts.Length == 1) Assert.IsFalse(n.Has(p.Path.Key), $"{k}: {p.Path} goes when nothing's wired");
            }
        }
        // A call's named argument is labelled with its name
        var call = doc.NewNode("call", 0, 0);
        call.Set("args", JValue.ArrayOf(new[] { JValue.Null(), JValue.Null() }));
        call.Set("argNames", JValue.Strings(new[] { "", "crit" }));
        CollectionAssert.AreEqual(new[] { "arg 1", "crit:" }, GraphPorts.Of(call).Where(p => p.Input).Select(p => p.Label));
        // A shorthand member has no object
        var member = doc.NewNode("member", 0, 0);
        member.SetFlag("shorthand", true);
        Assert.IsFalse(GraphPorts.Of(member).Any(p => p.Input));
    }

    static IEnumerable<string> Strings(JValue a) => a?.Items.Select(i => i.String) ?? Enumerable.Empty<string>();
}
