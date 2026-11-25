using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using NUnit.Framework;
using Tide.Editor;
using UnityEngine;

// The graph editor reads tidec's graph schema (compiler/src/graph_schema.json, `tide graph --schema`), the one every
// graph editor reads: what it uses is that schema, every node kind's ports are the schema's, and what the board says
// and offers (plain words, groups, which members a kind can have, which types come first) is the schema's too.
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

    // What the board says and offers comes from the schema: its words for kinds, keys, options and the ways a
    // parameter is taken, its groups, which members each kind can have, and where a declaration's place is written
    [Test]
    public void TheWordingAndTheRulesAreTheSchemas()
    {
        NeedTidec();
        var j = GraphSchema.Json;
        var groups = j.Arr("groups").Items;
        CollectionAssert.AreEqual(groups.Select(g => g.Str("title")), GraphSchema.Groups.Select(g => g.Title));
        var memberKinds = j.Arr("members").Items.Select(m => m.Str("kind")).ToList();
        foreach (var d in j.Arr("declarations").Items)
        {
            var kind = d.Str("kind");
            Assert.AreEqual(d.Str("title"), GraphSchema.DeclTitle(kind));
            Assert.IsTrue(groups.Any(g => g.Str("name") == GraphSchema.DeclGroup(kind)), kind + " is in a group the add menu has");
            Assert.IsFalse(string.IsNullOrEmpty(GraphSchema.DeclSummary(kind)), kind + " says what it is");
            Assert.AreEqual(d.Str("summary"), GraphSchema.DeclSummary(kind));
            Assert.AreEqual(d.Flag("common"), GraphSchema.IsCommon(kind));
            CollectionAssert.AreEqual(Strings(d.Arr("memberKinds")), GraphSchema.MemberKindsOf(kind));
            foreach (var m in GraphSchema.MemberKindsOf(kind)) CollectionAssert.Contains(memberKinds, m, $"{kind}'s member kind {m} is one the schema has");
            Assert.AreEqual(d.Flag("members"), GraphSchema.MemberKindsOf(kind).Length > 0, kind + " has member kinds iff it has members");
            // Code says how its entry reads
            if (d.Flag("body")) Assert.IsFalse(string.IsNullOrEmpty(GraphSchema.Sentence(kind, false)), kind + " has a sentence for its entry");
            // Where it is on the board is written after its id, but a namespace block's (it's around what's in it)
            var keys = Strings(d.Arr("keys")).ToList();
            Assert.AreEqual(kind != "namespace", keys.Contains("x") && keys.Contains("y"), kind);
            if (keys.Contains("x")) CollectionAssert.AreEqual(new[] { "kind", "id", "x", "y" }, keys.Take(4), kind);
            Assert.AreEqual(keys.Contains("typeParams"), GraphSchema.HasKey(kind, false, "typeParams"));
        }
        Assert.IsFalse(GraphSchema.HasKey("component", false, "typeParams"), "a component isn't generic");
        Assert.IsFalse(GraphSchema.HasKey("system", false, "returnType"), "a system has no result");
        foreach (var m in j.Arr("members").Items)
        {
            var kind = m.Str("kind");
            Assert.AreEqual(m.Str("summary"), GraphSchema.MemberSummary(kind));
            Assert.AreEqual(m.Flag("once"), GraphSchema.MemberOnce(kind));
            // A member with code is a frame of its own, with a place
            Assert.AreEqual(m.Flag("body"), Strings(m.Arr("keys")).Contains("x"), kind);
        }
        CollectionAssert.Contains(GraphSchema.AccessorOrder, "x");
        // Sample is an input's alone, an operator a struct's
        Assert.AreEqual(new[] { "input" }, j.Arr("declarations").Items.Where(d => Strings(d.Arr("memberKinds")).Contains("sample")).Select(d => d.Str("kind")).ToArray());
        Assert.AreEqual(new[] { "struct" }, j.Arr("declarations").Items.Where(d => Strings(d.Arr("memberKinds")).Contains("operator")).Select(d => d.Str("kind")).ToArray());
        // The words: a key's, an option's, a way of taking a parameter's
        var keyInfo = j.Get("keyInfo");
        for (var i = 0; i < keyInfo.Keys.Count; i++)
        {
            var info = GraphSchema.KeyInfo(keyInfo.Keys[i]);
            Assert.AreEqual(keyInfo.Values[i].Str("label"), info.Label);
            Assert.AreEqual(keyInfo.Values[i].Str("help"), info.Help);
            Assert.AreEqual(keyInfo.Values[i].Flag("advanced"), info.Advanced);
        }
        Assert.AreEqual("Generic over", GraphSchema.KeyInfo("typeParams").Label);
        Assert.IsTrue(GraphSchema.KeyInfo("typeParams").Advanced && GraphSchema.KeyInfo("comment").Advanced, "what's rare is behind More");
        foreach (var modifier in GraphSchema.AllModifiers) Assert.AreNotEqual(modifier, GraphSchema.ModifierInfo(modifier).Label, modifier + " has words of its own");
        Assert.AreEqual("Dev builds only", GraphSchema.ModifierInfo("dev").Label);
        foreach (var mode in GraphSchema.ParamModes) Assert.AreNotEqual(mode == "without" ? null : mode, GraphSchema.ParamModeInfo(mode).Label, mode);
        Assert.AreEqual("changes", GraphSchema.ParamModeInfo("mut").Label);
        Assert.AreEqual("reads", GraphSchema.ParamModeInfo("read").Label);
        // The types offered first are types, and a type made of others asks for as many as it has places
        foreach (var common in GraphSchema.TypeCommon) CollectionAssert.Contains(GraphSchema.BuiltinTypes, common);
        Assert.IsNotEmpty(GraphSchema.TypeBuilders);
        foreach (var builder in GraphSchema.TypeBuilders)
            for (var i = 0; i < builder.Slots.Length; i++)
                StringAssert.Contains("{" + i + "}", builder.Type, builder.Title);
        Assert.AreEqual("List<int>", string.Format(GraphSchema.TypeBuilders.First(b => b.Type.StartsWith("List<")).Type, "int"));
        // The words the editors would else each make up: a node's flags and pins, the last row of a card or an
        // entry, a property's accessors, the picker's headings, which control edits a type's value
        var flags = j.Get("flagInfo");
        Assert.IsNotNull(flags);
        foreach (var n in j.Arr("nodes").Items)
            foreach (var key in Strings(n.Arr("keys")))
                if (n.Get(key) == null && (key == "mut" || key == "indexed" || key == "shared" || key == "postfix" || key == "withBlock" || key == "stacked" || key == "shorthand" || key == "local"))
                    Assert.IsTrue(GraphSchema.IsFlag(key), $"{n.Str("k")}'s {key} has words");
        Assert.AreEqual(flags.Get("mut").Str("label"), GraphSchema.FlagInfo("mut").Label);
        Assert.IsFalse(GraphSchema.IsFlag("name"));
        Assert.AreEqual(j.Get("portInfo").Get("object").Str("label"), GraphSchema.PortLabel("object"));
        Assert.AreEqual("next", GraphSchema.PortLabel("next"), "a pin with no words of its own is shown by its key");
        foreach (var d in j.Arr("declarations").Items) Assert.AreEqual(d.Str("addRow"), GraphSchema.AddRow(d.Str("kind"), false));
        foreach (var m in j.Arr("members").Items)
        {
            Assert.AreEqual(m.Str("addRow"), GraphSchema.AddRow(m.Str("kind"), true));
            Assert.AreEqual(m.Str("sentence"), GraphSchema.Sentence(m.Str("kind"), true));
            if (m.Flag("body")) Assert.IsFalse(string.IsNullOrEmpty(m.Str("sentence")), m.Str("kind") + "'s entry has a sentence");
        }
        StringAssert.StartsWith("+", GraphSchema.AddRow("component", false));
        Assert.AreNotEqual(GraphSchema.AddRow("function", false), GraphSchema.AddRow("system", false), "what a function takes is asked for otherwise than what a system needs");
        foreach (var word in new[] { "get", "set" }) Assert.AreEqual(j.Get("accessorInfo").Get(word).Str("label"), GraphSchema.AccessorInfo(word).Label);
        foreach (var group in new[] { "common", "file", "game", "builders", "own", "rest" }) Assert.AreEqual(j.Get("typePicker").Get("groups").Str(group), GraphSchema.TypeGroup(group));
        Assert.AreEqual("whole", GraphSchema.ValueControl("int"));
        Assert.AreEqual("decimal", GraphSchema.ValueControl("float"));
        Assert.AreEqual("tick", GraphSchema.ValueControl("bool"));
        Assert.AreEqual("vector", GraphSchema.ValueControl("float3"));
        Assert.AreEqual("colour", GraphSchema.ValueControl("Color"));
        Assert.IsNull(GraphSchema.ValueControl("Entity"), "a type with no control of its own");
        Assert.IsTrue(GraphSchema.TypeBuilders.Any(b => b.Counts.Length > 0), "a fixed array's size is a number, not a type");

        // Every node says what it does, and the operators are their kinds'
        foreach (var n in GraphSchema.Nodes) Assert.IsFalse(string.IsNullOrEmpty(n.Help), n.K + " has help");
        Assert.AreEqual(GraphSchema.BinaryOps.Length, GraphSchema.Ops("binary").Length);
        Assert.IsNotNull(GraphSchema.Ops("binary").First().title);
        CollectionAssert.AreEqual(GraphSchema.AssignOps, GraphSchema.Ops("assign").Select(o => o.op));
        Assert.IsEmpty(GraphSchema.Ops("if"));
    }

    static IEnumerable<string> Strings(JValue a) => a?.Items.Select(i => i.String) ?? Enumerable.Empty<string>();
}
