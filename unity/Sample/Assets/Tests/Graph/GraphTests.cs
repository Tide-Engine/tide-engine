using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using NUnit.Framework;
using Tide.Editor;
using UnityEditor;
using UnityEngine;
using UnityEngine.TestTools;
using UnityEngine.UIElements;

// The graph editor of .tidegraph files: its file's layout is tidec's byte for byte, its edits make graphs tidec
// lowers to the Tide they stand for, and its window (one board: cards for data, frames for code) offers what's valid
// and nothing else, edits each thing where it's drawn, undoes, saves and closes cleanly.
public class GraphTests
{
    static string tidec;
    static ITideGraphChecker checker;
    static string clipboard = ""; // The tests' own: the user's clipboard isn't theirs to copy into

    // This checkout: the package's folder is unity/com.tide.unity in it
    static string Repository
    {
        get
        {
            var package = UnityEditor.PackageManager.PackageInfo.FindForAssetPath("Packages/com.tide.unity");
            return Path.GetFullPath(Path.Combine(package.resolvedPath, "..", ".."));
        }
    }

    // TIDE_TIDEC, else a build of this checkout's
    static string FindTidec()
    {
        var set = Environment.GetEnvironmentVariable("TIDE_TIDEC");
        if (!string.IsNullOrEmpty(set) && File.Exists(set)) return set;
        var exe = Application.platform == RuntimePlatform.WindowsEditor ? "tidec.exe" : "tidec";
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
        tidec = FindTidec();
        TideGraphCommand.TidecOverride = tidec;
        // The graph schema of that tidec
        GraphSchema.Reload();
        // What tidec lowers a graph to: these graphs are the format's, not games tidels would check (GraphLanguageTests)
        checker = TideGraphServices.Checker;
        TideGraphServices.Checker = new TidecGraphChecker();
        GraphBoard.ClipboardRead = () => clipboard;
        GraphBoard.ClipboardWrite = text => clipboard = text;
    }

    [OneTimeTearDown]
    public void Forget()
    {
        TideGraphCommand.TidecOverride = null;
        TideGraphServices.Checker = checker;
        GraphBoard.ClipboardRead = null;
        GraphBoard.ClipboardWrite = null;
    }

    static void NeedTidec()
    {
        if (tidec == null) Assert.Ignore("No tidec: build this checkout's (cmake --build --preset sanitize --target tidec) or set TIDE_TIDEC");
    }

    static string Scratch
    {
        get
        {
            var folder = Path.Combine(Path.GetFullPath("Temp"), "GraphTests");
            Directory.CreateDirectory(folder);
            return folder;
        }
    }

    // The Tide tidec writes for a graph
    static string Lower(GraphDocument doc)
    {
        NeedTidec();
        var file = Path.Combine(Scratch, "Lowered.tidegraph");
        File.WriteAllText(file, doc.Text, new UTF8Encoding(false));
        Assert.IsTrue(TideGraphCommand.GraphTextCommand(file, out var program, out var arguments, out var why), why);
        var code = TideGraphCommand.Run(program, arguments, Scratch, out var output, out var errors);
        Assert.AreEqual(0, code, $"tidec doesn't lower the graph:\n{errors}\n{doc.Text}");
        return output.Replace("\r\n", "\n").Trim('\n');
    }

    // ---------------------------------------------------------------------------------------------------------
    // The file

    [Test]
    public void ConvertedGraphsSaveAsTidecWritesThem()
    {
        NeedTidec();
        var files = new List<string>();
        foreach (var folder in new[] { "examples/sumo", "examples/towers", "compiler/tests/e2e" })
            files.AddRange(Directory.GetFiles(Path.Combine(Repository, folder), "*.tide"));
        Assert.Greater(files.Count, 300);
        var differ = new List<string>();
        var converted = 0;
        foreach (var tide in files)
        {
            var graph = Path.Combine(Scratch, "Converted.tidegraph");
            if (File.Exists(graph)) File.Delete(graph);
            var why = TideGraphCommand.Convert(tide, graph);
            Assert.IsNull(why, $"{tide} doesn't convert: {why}");
            converted++;
            var bytes = File.ReadAllBytes(graph);
            var text = Encoding.UTF8.GetString(bytes);
            var doc = GraphDocument.Parse(text);
            var saved = Path.Combine(Scratch, "Saved.tidegraph");
            doc.Save(saved);
            if (!File.ReadAllBytes(saved).SequenceEqual(bytes)) differ.Add(tide);
            // Ids are unique, and nextId is above them all
            var ids = new HashSet<int>();
            foreach (var e in doc.Elements) Assert.IsTrue(ids.Add(e.Id), $"{tide}: id {e.Id} twice");
            Assert.Greater(doc.NextId, ids.Count == 0 ? 0 : ids.Max(), tide);
        }
        Assert.IsEmpty(differ, $"Saved otherwise than tidec wrote them: {string.Join(", ", differ)}");
        Assert.AreEqual(files.Count, converted);
    }

    [Test]
    public void UnknownKeysAreKept()
    {
        var text = "{\n  \"tidegraph\": 1,\n  \"nextId\": 3,\n  \"extra\": {\"a\": [1, 2]},\n  \"decls\": [\n    {\"kind\": \"text\", \"id\": 1, \"text\": \"// hi\", \"future\": true}\n  ]\n}\n";
        var doc = GraphDocument.Parse(text);
        Assert.AreEqual(text, doc.Text);
        var id = doc.NewId();
        Assert.AreEqual(3, id);
        Assert.AreEqual(4, doc.NextId);
    }

    [Test]
    public void AnEmptyGraphIsAFile()
    {
        var text = GraphDocument.Empty().Text;
        // As tidec writes an empty file's: nothing nested in it, so on one line
        Assert.AreEqual("{\"tidegraph\": 1, \"nextId\": 1, \"decls\": []}\n", text);
        if (tidec != null) Assert.AreEqual("", Lower(GraphDocument.Parse(text)));
    }

    // ---------------------------------------------------------------------------------------------------------
    // Edits

    // A function F and its body, with nodes made by kind and keys: an int is a wire (0 none), a string a string,
    // a bool a flag, an int[] wires, a string[] strings
    sealed class Body
    {
        public readonly GraphDocument Doc = GraphDocument.Empty();
        public readonly JValue Function;
        public JValue Graph => Function.Get("body");

        public Body(string returnType = "void")
        {
            Function = Doc.NewDecl("function", "F");
            Function.Set("returnType", JValue.Of(returnType));
            Doc.Decls.Items.Add(Function);
            Doc.Reindex();
        }

        public int N(string k, params object[] keys)
        {
            var n = Doc.AddNode(Graph, k, 0, 0);
            var order = GraphSchema.NodeOrder(k);
            for (var i = 0; i < keys.Length; i += 2)
            {
                var key = (string)keys[i];
                var v = keys[i + 1];
                switch (v)
                {
                    case int id: n.Set(key, id == 0 ? JValue.Null() : JValue.Of(id), order); break;
                    case string s: n.Set(key, JValue.Of(s), order); break;
                    case bool b: n.Set(key, JValue.Of(b), order); break;
                    case int[] ids: n.Set(key, JValue.ArrayOf(ids.Select(x => x == 0 ? JValue.Null() : JValue.Of(x))), order); break;
                    case string[] ss: n.Set(key, JValue.Strings(ss), order); break;
                    case JValue j: n.Set(key, j, order); break;
                }
            }
            return n.Wire("id");
        }

        public JValue Node(int id) => GraphDocument.NodeIn(Graph, id);

        // Statements one after another, from the body's start
        public void Run(params int[] statements)
        {
            Graph.Set("entry", JValue.Of(statements[0]));
            for (var i = 0; i + 1 < statements.Length; i++) Node(statements[i]).Set("next", JValue.Of(statements[i + 1]), GraphSchema.NodeOrder(Node(statements[i]).Str("k")));
        }

        public void Wire(int node, string key, int from)
        {
            Assert.IsTrue(GraphDocument.Connect(Graph, node, new WirePath(key), from, out var why), why);
        }

        public string Text()
        {
            Doc.Reindex();
            return Lower(Doc);
        }

        // The body's lines, without F's
        public string Code()
        {
            var lines = Text().Split('\n');
            return string.Join("\n", lines.Skip(1).Take(lines.Length - 2).Select(l => l.Length >= 4 ? l.Substring(4) : l));
        }

        // What `return value;` says
        public string Expr(int value)
        {
            Run(N("return", "value", value));
            return Code();
        }
    }

    [Test]
    public void NodesWiredMakeTheTideTheyStandFor()
    {
        NeedTidec();
        var b = new Body();
        var p = b.Doc.NewParam("int", "a");
        b.Function.Arr("params").Items.Add(p);
        var @if = b.N("if");
        var less = b.N("binary", "op", "<");
        var a = b.N("name", "name", "a");
        var three = b.N("int", "text", "3");
        var add = b.N("assign", "op", "+=");
        var x = b.N("name", "name", "x");
        var one = b.N("int", "text", "1");
        b.Wire(less, "lhs", a);
        b.Wire(less, "rhs", three);
        b.Wire(@if, "cond", less);
        b.Wire(add, "target", x);
        b.Wire(add, "value", one);
        b.Wire(@if, "then", add);
        b.Graph.Set("entry", JValue.Of(@if));
        Assert.AreEqual("void F(int a) {\n    if (a < 3) {\n        x += 1;\n    }\n}", b.Text());
    }

    [Test]
    public void EveryStatementKind()
    {
        NeedTidec();
        Body b;
        string S(Func<Body, int[]> make)
        {
            b = new Body();
            b.Run(make(b));
            return b.Code();
        }
        Assert.AreEqual("int v = 5;", S(g => new[] { g.N("var", "type", "int", "name", "v", "value", g.N("int", "text", "5")) }));
        Assert.AreEqual("mut var v = 5;", S(g => new[] { g.N("var", "mut", true, "name", "v", "value", g.N("int", "text", "5")) }));
        Assert.AreEqual("shared int v;", S(g => new[] { g.N("var", "type", "int", "name", "v", "shared", true) }));
        Assert.AreEqual("x = 1;", S(g => new[] { g.N("assign", "op", "=", "target", g.N("name", "name", "x"), "value", g.N("int", "text", "1")) }));
        Assert.AreEqual("x++;", S(g => new[] { g.N("assign", "op", "++", "target", g.N("name", "name", "x")) }));
        Assert.AreEqual("Print(\"hi\");", S(g => new[] { g.N("eval", "value", g.N("call", "name", "Print", "args", new[] { g.N("string", "text", "hi") })) }));
        Assert.AreEqual("list.Add(1);", S(g => new[] { g.N("eval", "value", g.N("method", "object", g.N("name", "name", "list"), "name", "Add", "args", new[] { g.N("int", "text", "1") })) }));
        Assert.AreEqual("Vertical() {\n    Label();\n}", S(g => new[] { g.N("eval", "value", g.N("call", "name", "Vertical"), "block", g.N("eval", "value", g.N("call", "name", "Label"))) }));
        Assert.AreEqual("Vertical() { }", S(g => new[] { g.N("eval", "value", g.N("call", "name", "Vertical"), "withBlock", true) }));
        Assert.AreEqual("Blend(), Clip() {\n    Draw();\n}", S(g =>
        {
            var draw = g.N("eval", "value", g.N("call", "name", "Draw"));
            var clip = g.N("eval", "value", g.N("call", "name", "Clip"), "stacked", true, "block", draw);
            return new[] { g.N("eval", "value", g.N("call", "name", "Blend"), "block", clip) };
        }));
        Assert.AreEqual("if (c) {\n    x = 1;\n} else {\n    x = 2;\n}", S(g => new[]
        {
            g.N("if", "cond", g.N("name", "name", "c"),
                "then", g.N("assign", "op", "=", "target", g.N("name", "name", "x"), "value", g.N("int", "text", "1")),
                "else", g.N("assign", "op", "=", "target", g.N("name", "name", "x"), "value", g.N("int", "text", "2"))),
        }));
        Assert.AreEqual("while (c) {\n    continue;\n}", S(g => new[] { g.N("while", "cond", g.N("name", "name", "c"), "body", g.N("continue")) }));
        Assert.AreEqual("for (int i = 0; i < 3; i++) {\n    break;\n}", S(g =>
        {
            var i = g.N("var", "type", "int", "name", "i", "value", g.N("int", "text", "0"));
            var less = g.N("binary", "op", "<", "lhs", g.N("local", "ref", i), "rhs", g.N("int", "text", "3"));
            var step = g.N("assign", "op", "++", "target", g.N("local", "ref", i));
            return new[] { g.N("for", "init", i, "cond", less, "step", step, "body", g.N("break")) };
        }));
        Assert.AreEqual("foreach (var item in list) {\n    Use(item);\n}", S(g =>
        {
            var loop = g.N("foreach", "name", "item", "collection", g.N("name", "name", "list"));
            g.Node(loop).Set("body", JValue.Of(g.N("eval", "value", g.N("call", "name", "Use", "args", new[] { g.N("local", "ref", loop) }))));
            return new[] { loop };
        }));
        Assert.AreEqual("indexed foreach (Cell c in cells) {\n}", S(g => new[] { g.N("foreach", "indexed", true, "type", "Cell", "name", "c", "collection", g.N("name", "name", "cells")) }));
        Assert.AreEqual("parallel (var at in g by 8 offset 1) {\n}", S(g => new[] { g.N("parallel", "name", "at", "grid", g.N("name", "name", "g"), "by", g.N("int", "text", "8"), "offset", g.N("int", "text", "1")) }));
        Assert.AreEqual("switch (s) {\ncase 1:\ncase 2:\n    x = 1;\ndefault:\n    break;\n}", S(g =>
        {
            var cases = JValue.NewArray();
            var first = JValue.NewObject();
            first.Set("labels", JValue.ArrayOf(new[] { JValue.Of(g.N("int", "text", "1")), JValue.Of(g.N("int", "text", "2")) }));
            first.Set("body", JValue.Of(g.N("assign", "op", "=", "target", g.N("name", "name", "x"), "value", g.N("int", "text", "1"))));
            cases.Items.Add(first);
            var second = JValue.NewObject();
            second.Set("labels", JValue.ArrayOf(new[] { JValue.Null() }));
            second.Set("body", JValue.Of(g.N("break")));
            cases.Items.Add(second);
            return new[] { g.N("switch", "value", g.N("name", "name", "s"), "cases", cases) };
        }));
        Assert.AreEqual("return;", S(g => new[] { g.N("return") }));
        Assert.AreEqual("fail Oops;", S(g => new[] { g.N("fail", "value", g.N("name", "name", "Oops")) }));
        Assert.AreEqual("{\n    x = 1;\n}", S(g => new[] { g.N("block", "body", g.N("assign", "op", "=", "target", g.N("name", "name", "x"), "value", g.N("int", "text", "1"))) }));
        Assert.AreEqual("_ = F();", S(g => new[] { g.N("discard", "value", g.N("call", "name", "F")) }));
        var inputs = JValue.NewObject();
        Assert.AreEqual("Debug((7));", S(g =>
        {
            inputs.Set("a", JValue.Of(g.N("int", "text", "7")));
            return new[] { g.N("code", "text", "Debug(a);", "inputs", inputs) };
        }));
        // A statement's value read after it: kept in a local of its own
        b = new Body("int");
        var kept = b.N("eval", "value", b.N("call", "name", "Roll"));
        b.Run(kept, b.N("return", "value", kept));
        Assert.AreEqual($"var _graph{kept} = Roll();\nreturn _graph{kept};", b.Code());
        // A comment before a statement
        b = new Body();
        var said = b.N("break", "comment", "Out of here\ntwo lines");
        b.Run(said);
        Assert.AreEqual("// Out of here\n// two lines\nbreak;", b.Code());
    }

    [Test]
    public void EveryValueKind()
    {
        NeedTidec();
        string E(Func<Body, int> make)
        {
            var b = new Body("int");
            var code = b.Expr(make(b));
            StringAssert.StartsWith("return ", code);
            return code.Substring(7, code.Length - 8);
        }
        Assert.AreEqual("12", E(g => g.N("int", "text", "12")));
        Assert.AreEqual("1.5", E(g => g.N("float", "text", "1.5")));
        Assert.AreEqual("'a'", E(g => g.N("char", "text", "'a'")));
        Assert.AreEqual("\"hi\"", E(g => g.N("string", "text", "hi")));
        Assert.AreEqual("true", E(g => g.N("bool", "value", true)));
        Assert.AreEqual("this", E(g => g.N("this")));
        Assert.AreEqual("default", E(g => g.N("default")));
        Assert.AreEqual("null", E(g => g.N("null")));
        Assert.AreEqual("speed", E(g => g.N("name", "name", "speed")));
        Assert.AreEqual("local Fx", E(g => g.N("name", "name", "Fx", "local", true)));
        Assert.AreEqual("t.position", E(g => g.N("member", "object", g.N("name", "name", "t"), "member", "position")));
        Assert.AreEqual(".Red", E(g => g.N("member", "shorthand", true, "member", "Red")));
        Assert.AreEqual("local Hit<int>(1, crit: 2)", E(g => g.N("call", "name", "Hit", "typeArgs", new[] { "int" }, "local", true, "args", new[] { g.N("int", "text", "1"), g.N("int", "text", "2") }, "argNames", new[] { "", "crit" })));
        Assert.AreEqual("t.Get<int>()", E(g => g.N("method", "object", g.N("name", "name", "t"), "name", "Get", "typeArgs", new[] { "int" })));
        Assert.AreEqual("TryGet(out mut var r, out Health h, out x, out _)", E(g => g.N("call", "name", "TryGet", "args", new[]
        {
            g.N("outVar", "mut", true, "name", "r"), g.N("outVar", "type", "Health", "name", "h"), g.N("out", "target", g.N("name", "name", "x")), g.N("out", "target", g.N("name", "name", "_")),
        })));
        foreach (var (op, _) in GraphSchema.BinaryOps)
            Assert.AreEqual($"a {op} b", E(g => g.N("binary", "op", op, "lhs", g.N("name", "name", "a"), "rhs", g.N("name", "name", "b"))));
        Assert.AreEqual("(a + b) * c", E(g => g.N("binary", "op", "*", "lhs", g.N("binary", "op", "+", "lhs", g.N("name", "name", "a"), "rhs", g.N("name", "name", "b")), "rhs", g.N("name", "name", "c"))));
        foreach (var op in GraphSchema.UnaryOps) Assert.AreEqual($"{op}a", E(g => g.N("unary", "op", op, "value", g.N("name", "name", "a"))));
        Assert.AreEqual("++x", E(g => g.N("step", "op", "++", "target", g.N("name", "name", "x"))));
        Assert.AreEqual("x--", E(g => g.N("step", "op", "--", "postfix", true, "target", g.N("name", "name", "x"))));
        Assert.AreEqual("c ? 1 : 2", E(g => g.N("conditional", "cond", g.N("name", "name", "c"), "then", g.N("int", "text", "1"), "else", g.N("int", "text", "2"))));
        Assert.AreEqual("s switch { 1 => 10, _ => 0 }", E(g =>
        {
            var arms = JValue.NewArray();
            var one = JValue.NewObject();
            one.Set("pattern", JValue.Of(g.N("int", "text", "1")));
            one.Set("value", JValue.Of(g.N("int", "text", "10")));
            arms.Items.Add(one);
            var rest = JValue.NewObject();
            rest.Set("pattern", JValue.Null());
            rest.Set("value", JValue.Of(g.N("int", "text", "0")));
            arms.Items.Add(rest);
            return g.N("switchExpr", "value", g.N("name", "name", "s"), "arms", arms);
        }));
        Assert.AreEqual("$\"a {x:F2} b\"", E(g => g.N("interp", "parts", new[] { "a ", " b" }, "formats", new[] { "F2" }, "values", new[] { g.N("name", "name", "x") })));
        Assert.AreEqual("g[1, 2]", E(g => g.N("index", "object", g.N("name", "name", "g"), "index", new[] { g.N("int", "text", "1"), g.N("int", "text", "2") })));
        Assert.AreEqual("[1, 2]", E(g => g.N("list", "items", new[] { g.N("int", "text", "1"), g.N("int", "text", "2") })));
        Assert.AreEqual("Transform { position = p }", E(g =>
        {
            var inits = JValue.NewArray();
            var init = JValue.NewObject();
            init.Set("name", JValue.Of("position"));
            init.Set("value", JValue.Of(g.N("name", "name", "p")));
            inits.Items.Add(init);
            return g.N("literal", "type", "Transform", "inits", inits);
        }));
        Assert.AreEqual("(float p) => p * 2", E(g =>
        {
            var ps = JValue.NewArray();
            var p = JValue.NewObject();
            p.Set("type", JValue.Of("float"));
            p.Set("name", JValue.Of("p"));
            ps.Items.Add(p);
            return g.N("lambda", "params", ps, "body", g.N("binary", "op", "*", "lhs", g.N("name", "name", "p"), "rhs", g.N("int", "text", "2")));
        }));
        Assert.AreEqual("a ?? b", E(g => g.N("coalesce", "lhs", g.N("name", "name", "a"), "rhs", g.N("name", "name", "b"))));
        Assert.AreEqual("v is Hit h", E(g => g.N("is", "value", g.N("name", "name", "v"), "type", "Hit", "binding", "h")));
        Assert.AreEqual("V has float3 normal", E(g => g.N("has", "subject", "V", "type", "float3", "field", "normal")));
        Assert.AreEqual("try F()", E(g => g.N("try", "value", g.N("call", "name", "F"))));
        Assert.AreEqual("await F()", E(g => g.N("await", "value", g.N("call", "name", "F"))));
        Assert.AreEqual("F()!", E(g => g.N("defaulted", "value", g.N("call", "name", "F"))));
        Assert.AreEqual("((1) + (2) + x.a)", E(g =>
        {
            var inputs = JValue.NewObject();
            inputs.Set("a", JValue.Of(g.N("int", "text", "1")));
            inputs.Set("b", JValue.Of(g.N("int", "text", "2")));
            return g.N("text", "text", "a + b + x.a", "inputs", inputs);
        }));
        // A local's read
        var lb = new Body("int");
        var v = lb.N("var", "type", "int", "name", "v", "value", lb.N("int", "text", "1"));
        lb.Run(v, lb.N("return", "value", lb.N("local", "ref", v)));
        Assert.AreEqual("int v = 1;\nreturn v;", lb.Code());
    }

    // Every node kind of the schema is offered by the search, under its category's title and with what it does, and
    // what it makes is the kind it says
    [Test]
    public void TheSearchOffersEveryKind()
    {
        var b = new Body();
        b.Function.Arr("params").Items.Add(b.Doc.NewParam("int", "speed"));
        var v = b.N("var", "type", "int", "name", "v");
        b.Run(v);
        b.Doc.Reindex();
        var context = new GraphSearchContext { Doc = b.Doc, Graph = b.Graph, Slot = new GraphSlot(b.Function.Wire("id"), "body") };
        var entries = TideGraphServices.Searches.SelectMany(s => s.Entries(context)).ToList();
        var kinds = new HashSet<string>();
        foreach (var e in entries)
        {
            var made = e.Make(b.Doc, b.Graph, Vector2.zero);
            if (made == null) continue;
            kinds.Add(made.Str("k"));
            // A call statement is an eval with its call
            if (made.Str("k") == "eval" && made.Wire("value") != 0) kinds.Add(GraphDocument.NodeIn(b.Graph, made.Wire("value")).Str("k"));
        }
        foreach (var spec in GraphSchema.Nodes) Assert.IsTrue(kinds.Contains(spec.K), $"The search doesn't offer '{spec.K}'");
        Assert.IsTrue(entries.Any(e => e.Reads && e.Title == "v"), "the local");
        Assert.IsTrue(entries.Any(e => e.Reads && e.Title == "speed"), "what the code takes");
        Assert.AreEqual(GraphSchema.BinaryOps.Length, entries.Count(e => e.Make != null && e.Keywords.StartsWith("binary")));
        // In the schema's words: a kind's title, its category's, and what it does
        var schema = GraphSchema.Json;
        foreach (var n in schema.Arr("nodes").Items)
        {
            if (n.Str("k") == "local" || (schema.Get("operators").Has(n.Str("k")) && !n.Flag("exec"))) continue;
            var entry = entries.FirstOrDefault(e => e.Title == n.Str("title") && e.Detail == n.Str("help"));
            Assert.IsNotNull(entry, n.Str("k") + " is offered by its title, with its help");
            Assert.AreEqual(schema.Arr("categories").Items.Single(c => c.Str("name") == n.Str("category")).Str("title"), entry.Group);
            Assert.AreEqual(n.Flag("exec"), entry.Statement);
        }
        // A value's graph has no statements
        var value = new GraphSearchContext { Doc = b.Doc, Graph = b.Graph, Slot = new GraphSlot(b.Function.Wire("id"), "value") };
        Assert.IsFalse(TideGraphServices.Searches.SelectMany(s => s.Entries(value)).Any(e => e.Statement));
        // Every node the search made has an id of its own
        b.Doc.Reindex();
        var ids = GraphDocument.Nodes(b.Graph).Items.Select(n => n.Wire("id")).ToList();
        Assert.AreEqual(ids.Count, ids.Distinct().Count());
    }

    [Test]
    public void WiresFollowTheFormatsRules()
    {
        var b = new Body();
        var first = b.N("break");
        var second = b.N("continue");
        var third = b.N("return");
        b.Run(first);
        // A statement runs from one place only
        Assert.IsTrue(GraphDocument.Connect(b.Graph, first, new WirePath("next"), second, out _));
        Assert.IsFalse(GraphDocument.Connect(b.Graph, third, new WirePath("next"), second, out var why));
        StringAssert.Contains("one place", why);
        Assert.IsFalse(GraphDocument.Connect(b.Graph, second, new WirePath("next"), first, out _), "the body's start runs from its start");
        // A statement has no value, a value isn't a statement
        var add = b.N("binary", "op", "+");
        Assert.IsFalse(GraphDocument.Connect(b.Graph, add, new WirePath("lhs"), first, out _));
        Assert.IsFalse(GraphDocument.Connect(b.Graph, first, new WirePath("next"), add, out _));
        // An input takes one wire: a new one replaces it
        var x = b.N("name", "name", "x");
        var y = b.N("name", "name", "y");
        Assert.IsTrue(GraphDocument.Connect(b.Graph, add, new WirePath("lhs"), x, out _));
        Assert.IsTrue(GraphDocument.Connect(b.Graph, add, new WirePath("lhs"), y, out _));
        Assert.AreEqual(y, b.Node(add).Wire("lhs"));
        // No value made of itself
        Assert.IsFalse(GraphDocument.Connect(b.Graph, add, new WirePath("rhs"), add, out _));
        var outer = b.N("unary", "op", "-", "value", add);
        Assert.IsFalse(GraphDocument.Connect(b.Graph, add, new WirePath("rhs"), outer, out _));
        // Lists of inputs
        var call = b.N("call", "name", "F", "args", new[] { 0, 0 });
        Assert.IsTrue(GraphDocument.Connect(b.Graph, call, new WirePath("args", 1), x, out _));
        Assert.AreEqual("[null, " + x + "]", GraphJson.Write(b.Node(call).Get("args")));
        GraphDocument.Disconnect(b.Graph, call, new WirePath("args", 1));
        Assert.AreEqual("[null, null]", GraphJson.Write(b.Node(call).Get("args")));
        GraphDocument.Disconnect(b.Graph, add, new WirePath("lhs"));
        Assert.IsFalse(b.Node(add).Has("lhs"), "an unwired key isn't written");
        // What's reached: the statements from the start and what they read
        var reached = GraphDocument.Reached(b.Graph);
        Assert.IsTrue(reached.Contains(first) && reached.Contains(second));
        Assert.IsFalse(reached.Contains(third) || reached.Contains(add));
    }

    [Test]
    public void DeletingAStatementJoinsTheOnesAroundIt()
    {
        var b = new Body();
        var a = b.N("break");
        var c = b.N("continue");
        var d = b.N("return", "value", b.N("int", "text", "1"));
        b.Run(a, c, d);
        b.Doc.DeleteNodes(b.Graph, new[] { c });
        Assert.AreEqual(d, b.Node(a).Wire("next"));
        b.Doc.DeleteNodes(b.Graph, new[] { a });
        Assert.AreEqual(d, b.Graph.Wire("entry"));
        // A value deleted leaves its input unwired
        var value = b.Node(d).Wire("value");
        b.Doc.DeleteNodes(b.Graph, new[] { value });
        Assert.IsFalse(b.Node(d).Has("value"));
        Assert.IsNull(b.Doc.Find(value));
    }

    [Test]
    public void CopiesPasteWithIdsOfTheirOwn()
    {
        var b = new Body();
        var outside = b.N("var", "type", "int", "name", "o");
        var v = b.N("var", "type", "int", "name", "v", "value", b.N("int", "text", "1"));
        var readV = b.N("local", "ref", v);
        var readO = b.N("local", "ref", outside);
        var sum = b.N("binary", "op", "+", "lhs", readV, "rhs", readO);
        var other = b.N("name", "name", "z");
        var assign = b.N("assign", "op", "=", "target", other, "value", sum);
        b.Run(outside, v, assign);
        var copied = new[] { v, readV, readO, sum, assign };
        var clip = GraphDocument.Copy(b.Graph, copied);
        Assert.IsTrue(GraphDocument.IsClip(clip));
        // Into another file's graph
        var into = new Body();
        into.N("break");
        var made = into.Doc.Paste(into.Graph, clip, new Vector2(100, 50));
        Assert.AreEqual(copied.Length, made.Count);
        var ids = into.Doc.Elements.Select(e => e.Id).ToList();
        Assert.AreEqual(ids.Count, ids.Distinct().Count(), "ids are unique in the file");
        Assert.Greater(into.Doc.NextId, ids.Max());
        var nodes = made.Select(id => into.Node(id)).ToList();
        var newV = nodes.First(n => n.Str("k") == "var");
        var newSum = nodes.First(n => n.Str("k") == "binary");
        var newAssign = nodes.First(n => n.Str("k") == "assign");
        var newReadV = into.Node(newSum.Wire("lhs"));
        Assert.AreEqual(newV.Wire("id"), newReadV.Wire("ref"), "a read of a local copied with it follows it");
        Assert.AreEqual(outside, into.Node(newSum.Wire("rhs")).Wire("ref"), "a read of a local not copied keeps its declaration's id");
        Assert.AreEqual(newSum.Wire("id"), newAssign.Wire("value"));
        Assert.IsFalse(newAssign.Has("target"), "a wire to a node not copied is dropped");
        Assert.AreEqual(100 + (b.Node(v).Get("x")?.Number ?? 0), newV.Get("x").Number);
        // Into the same graph
        var again = b.Doc.Paste(b.Graph, clip, Vector2.zero);
        Assert.AreEqual(copied.Length, again.Count);
        Assert.IsEmpty(again.Intersect(copied));
    }

    [Test]
    public void DuplicatesAndLayoutsLowerTheSame()
    {
        NeedTidec();
        var b = new Body();
        b.Function.Arr("params").Items.Add(b.Doc.NewParam("int", "a"));
        var loop = b.N("foreach", "name", "item", "collection", b.N("name", "name", "items"));
        b.Node(loop).Set("body", JValue.Of(b.N("eval", "value", b.N("call", "name", "Use", "args", new[] { b.N("local", "ref", loop), b.N("name", "name", "a") }))));
        b.Run(b.N("var", "type", "int", "name", "n", "value", b.N("int", "text", "2")), loop);
        var before = b.Text();
        // A layout as tidels gives one (tide/graphLayout): it moves the nodes, and nothing else
        var layout = JValue.NewObject();
        var place = 0;
        foreach (var n in GraphDocument.Nodes(b.Graph).Items) layout.Set(n.Wire("id").ToString(), JValue.ArrayOf(new[] { JValue.Of(100 * place), JValue.Of(40 * place++) }));
        Assert.AreEqual(GraphDocument.Nodes(b.Graph).Items.Count, TideGraphLanguage.Place(b.Graph, layout));
        Assert.AreEqual(100.0, GraphDocument.Nodes(b.Graph).Items[1].Get("x").Number);
        Assert.AreEqual(before, b.Text(), "laying out moves nodes, nothing else");
        var copy = b.Doc.Duplicate(b.Function);
        copy.Set("name", JValue.Of("G"));
        b.Doc.Decls.Items.Add(copy);
        b.Doc.Reindex();
        var ids = b.Doc.Elements.Select(e => e.Id).ToList();
        Assert.AreEqual(ids.Count, ids.Distinct().Count());
        var text = b.Text();
        Assert.AreEqual(before + "\n" + before.Replace("void F(", "void G("), text);
    }

    // Declarations and members of every kind, made as the board makes them, lower to Tide
    [Test]
    public void EveryDeclarationAndMemberKind()
    {
        NeedTidec();
        var doc = GraphDocument.Empty();
        foreach (var kind in GraphSchema.DeclKinds) doc.Decls.Items.Add(doc.NewDecl(kind, char.ToUpperInvariant(kind[0]) + kind.Substring(1)));
        var type = doc.Decls.Items.First(d => d.Str("kind") == "input");
        foreach (var kind in GraphSchema.MemberKinds) type.Arr("members").Items.Add(doc.NewMember(kind, "m" + kind));
        doc.Reindex();
        var text = Lower(doc);
        foreach (var line in new[]
        {
            "component Component {", "singleton Singleton {", "struct Struct {", "event Event {", "input Input {", "scene Scene {", "enum Enum {",
            "const int Const = 0;", "asset Texture Asset = \"image.png\";", "using Alias = int;", "settings {", "void Function() {", "system System() {",
            "view View() {", "event(Spawned) Handler() {", "command() Command() {", "audio Audio() {", "frag Frag() {", "vert Vert() {", "compute Compute() {",
            "prefab Prefab {", "namespace Namespace {", "// Tide text",
            "int mfield;", "int mproperty {", "get {", "void mmethod() {", "int operator +() {", "tweak float mtweak = 1.0;", "Sample() {", "Sanitize() {", "Predict() {", "prepare() {",
        })
            StringAssert.Contains(line, text);
    }

    // ---------------------------------------------------------------------------------------------------------
    // The importer and the window

    [TearDown]
    public void CloseWindows()
    {
        // A window left by a test that failed: Unity says again, closing it, that there's no graphics device
        if (SystemInfo.graphicsDeviceType == UnityEngine.Rendering.GraphicsDeviceType.Null) LogAssert.ignoreFailingMessages = true;
        foreach (var w in Resources.FindObjectsOfTypeAll<TideGraphWindow>()) w.Close();
    }

    // The warnings and errors logged while a window is open, but for Unity's own, in a run with no graphics
    // device, that a window has none to draw with
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

    // A graph's file for a window to open: in Temp, outside the game, so that a Sample building while it's edited
    // doesn't build it
    static string Write(string name, string text)
    {
        var path = Path.Combine(Scratch, name);
        File.WriteAllText(path, text, new UTF8Encoding(false));
        return path;
    }

    static IEnumerator Checked(TideGraphWindow window)
    {
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsFalse(window.CheckPending, "the check finishes");
    }

    [Test]
    public void AGraphImportsWithTidesIcon()
    {
        // An empty graph, as Assets > Create > Tide > Graph makes one
        const string folder = "Assets/GraphTestFiles";
        if (!AssetDatabase.IsValidFolder(folder)) AssetDatabase.CreateFolder("Assets", "GraphTestFiles");
        try
        {
            var path = TideGraphImporter.Create(folder);
            Assert.AreEqual(folder + "/New Graph.tidegraph", path);
            Assert.IsInstanceOf<TideGraphImporter>(AssetImporter.GetAtPath(path));
            var text = AssetDatabase.LoadMainAssetAtPath(path) as TextAsset;
            Assert.IsNotNull(text);
            Assert.AreEqual(TideGraphImporter.EmptyText, text.text);
            // Tide's icon, not plain text's
            System.IO.File.WriteAllText(folder + "/plain.txt", "text");
            AssetDatabase.ImportAsset(folder + "/plain.txt");
            Assert.IsNotNull(AssetDatabase.GetCachedIcon(path));
            Assert.AreNotEqual(AssetDatabase.GetCachedIcon(folder + "/plain.txt"), AssetDatabase.GetCachedIcon(path));
            // The importer's script loads, as Unity finds it by its file's name
            var script = new SerializedObject(AssetImporter.GetAtPath(path)).FindProperty("m_Script").objectReferenceValue as MonoScript;
            Assert.IsNotNull(script);
            Assert.AreEqual(typeof(TideGraphImporter), script.GetClass());
        }
        finally
        {
            AssetDatabase.DeleteAsset(folder);
        }
    }

    [UnityTest]
    public IEnumerator TheWindowEditsUndoesSavesAndCloses()
    {
        NeedTidec();
        var b = new Body();
        b.Run(b.N("return"));
        var path = Write("Edited.tidegraph", b.Doc.Text);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        Assert.IsNotNull(window.Doc);
        Assert.IsNull(window.ReadError);
        Assert.IsFalse(window.Dirty);
        var board = window.Board;
        var frame = board.Frame(b.Function.Wire("id"));
        Assert.IsNotNull(frame, "the function is a frame on the board");
        Assert.AreEqual(GraphLevel.Near, frame.Level, "on screen, so drawn");
        Assert.AreEqual(1, frame.Views.Count, "its statement");
        Assert.IsNotNull(frame.Entry, "and its entry");
        var original = window.Doc.Text;

        // An edit through the window: recorded, shown, and undone
        var graph = frame.Graph;
        window.Edit("Add break", () =>
        {
            var n = window.Doc.AddNode(graph, "break", 200, 0);
            GraphDocument.NodeIn(graph, graph.Wire("entry")).Set("next", JValue.Of(n.Wire("id")));
        }, frame.Id);
        Assert.IsTrue(window.Dirty);
        Assert.AreEqual(2, board.Frame(frame.Id).Views.Count);
        var edited = window.Doc.Text;
        Undo.PerformUndo();
        Assert.AreEqual(original, window.Doc.Text);
        Assert.AreEqual(1, board.Frame(frame.Id).Views.Count);
        Undo.PerformRedo();
        Assert.AreEqual(edited, window.Doc.Text);
        Assert.AreEqual(2, board.Frame(frame.Id).Views.Count);

        // What tidec says of it
        yield return Checked(window);
        Assert.IsNotNull(window.Checked);
        Assert.IsNull(window.Checked.Failure);
        Assert.IsEmpty(window.Checked.Diagnostics);
        Assert.AreEqual("Checked", window.ChipText);
        StringAssert.Contains("return;\n    break;", window.Checked.Text.Replace("\r\n", "\n"));
        // The Tide isn't on screen until it's asked for
        Assert.IsNull(window.TideView);
        window.ShowTide(true);
        StringAssert.Contains("return;", window.TideView.Text);
        window.ShowTide(false);
        Assert.IsNull(window.TideView);

        window.Save();
        Assert.IsFalse(window.Dirty);
        Assert.AreEqual(edited, File.ReadAllText(path));
        window.Close();
    }

    // An error is on what it's about, and gone with its cause: nothing of a check before the last stays anywhere
    [UnityTest]
    public IEnumerator TidecsErrorsMarkTheirNodesAndGoWithTheirCause()
    {
        NeedTidec();
        var b = new Body();
        var add = b.N("binary", "op", "+", "lhs", b.N("int", "text", "1"));
        b.Run(b.N("return", "value", add));
        var path = Write("Broken.tidegraph", b.Doc.Text);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        var error = window.Checked.Diagnostics.FirstOrDefault(d => d.Id == add);
        Assert.IsNotNull(error, "tidec names the node: " + string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
        StringAssert.Contains("rhs", error.Message);
        var board = window.Board;
        var frame = board.Frame(b.Function.Wire("id"));
        StringAssert.Contains("rhs", frame.Views[add].ProblemText, "said under its node");
        Assert.IsTrue(board.ProblemsShown().Any(t => t.Contains("rhs")));
        StringAssert.Contains("problem", window.ChipText);
        Assert.IsTrue(window.ProblemOptions().Any(o => o.Title.Contains("rhs")), "and in the chip's list");
        window.JumpTo(error);
        Assert.AreEqual(add, window.SelectedElement);
        Assert.IsTrue(board.Nodes.Contains(add));

        // Its cause removed (a value typed on the empty input): the next check has no problem, and none is drawn
        frame.TypeInto(frame.Views[add].Node, new WirePath("rhs"), "2", frame.Views[add]);
        yield return null;
        yield return Checked(window);
        Assert.IsEmpty(window.Checked.Diagnostics, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        Assert.IsEmpty(board.ProblemsShown(), "no message of the check before stays");
        Assert.IsNull(board.Frame(frame.Id).Views[add].ProblemText);
        Assert.AreEqual("Checked", window.ChipText);
        Assert.IsTrue(window.ProblemOptions().All(o => o.Off), "the list says nothing's wrong");
        window.Close();
    }

    // Every declaration, member and node of real games' files is on the board, and looking changes nothing
    [Test]
    public void ConvertedGamesShowOnTheBoard()
    {
        NeedTidec();
        using var logged = new Logged();
        foreach (var tide in Directory.GetFiles(Path.Combine(Repository, "examples", "sumo"), "*.tide").Concat(Directory.GetFiles(Path.Combine(Repository, "examples", "towers"), "*.tide")))
        {
            var path = Path.Combine(Scratch, Path.GetFileNameWithoutExtension(tide) + ".tidegraph");
            Assert.IsNull(TideGraphCommand.Convert(tide, path));
            var window = TideGraphWindow.Open(path);
            Assert.IsNull(window.ReadError);
            var board = window.Board;
            foreach (var e in window.Doc.Elements.ToList())
            {
                var item = board.ItemOf(e.Id);
                if (e.Role == GraphRole.Decl && e.Value.Str("kind") == "namespace") continue;
                if ((e.Role == GraphRole.Node || e.Role == GraphRole.Note) && !e.Slot.IsBody) continue; // A value's graph is on its card's row
                Assert.IsNotNull(item, $"{path}: @{e.Id} ({e.Role}) is on the board");
            }
            // Each frame, brought on screen, draws every node of its code: as a node, with its statement, or on
            // the input that reads it
            foreach (var frame in board.Items.OfType<GraphFrame>().ToList())
            {
                window.Reveal(frame.Id);
                Assert.AreEqual(GraphLevel.Near, frame.Level, $"{path}: {frame.Title} is drawn when it's shown");
                var nodes = frame.Graph?.Arr("nodes")?.Items.Count ?? 0;
                Assert.AreEqual(nodes, frame.Views.Count + frame.Absorbed.Count + frame.Hidden.Count, $"{path}: {frame.Title}");
                foreach (var hidden in frame.Hidden) Assert.IsTrue(frame.Readers.ContainsKey(hidden), $"{path}: what isn't drawn as a node is read by one");
            }
            foreach (var card in board.Items.OfType<GraphCard>().ToList())
            {
                window.Reveal(card.Id);
                Assert.AreEqual(GraphLevel.Near, card.Level);
                Assert.Greater(card.childCount, 0);
            }
            Assert.IsFalse(window.Dirty, "looking changes nothing");
            Assert.AreEqual(File.ReadAllText(path), window.Doc.Text);
            window.Close();
        }
    }

    // The biggest converted file: only what's on screen is drawn, an edit builds its own node again and no other,
    // and from far away a frame is its name
    [UnityTest]
    public IEnumerator AThousandNodeFileOpens()
    {
        NeedTidec();
        var path = Path.Combine(Scratch, "Arena.tidegraph");
        Assert.IsNull(TideGraphCommand.Convert(Path.Combine(Repository, "examples", "sumo", "arena.tide"), path));
        using var logged = new Logged();
        var clock = System.Diagnostics.Stopwatch.StartNew();
        var window = TideGraphWindow.Open(path);
        yield return null;
        var opened = clock.Elapsed.TotalSeconds;
        var board = window.Board;
        var nodes = window.Doc.Elements.Count(e => e.Role == GraphRole.Node);
        Assert.Greater(nodes, 1000);
        Assert.Less(opened, 5, "it opens at once");
        Assert.Less(board.NodesDrawn, nodes / 4, "what's off screen isn't drawn");
        var frames = board.Items.OfType<GraphFrame>().ToList();
        Assert.IsTrue(frames.Any(f => f.Level == GraphLevel.None), "a frame off screen is an empty box");
        // The last frame, far down the board: shown, it's drawn, and the first isn't any more
        var last = frames.OrderByDescending(f => f.At.y).First();
        var first = frames.OrderBy(f => f.At.y).First();
        window.Reveal(last.Id);
        Assert.AreEqual(GraphLevel.Near, last.Level);
        Assert.AreEqual(GraphLevel.None, first.Level);
        Assert.Greater(last.Views.Count, 0);
        // An edit of one node builds that node's view again, and keeps the others
        var view = last.Views.Values.First(v => v.Node.Str("k") == "call" || v.Call != null || v.Node.Str("k") == "binary" || v.Node.Str("k") == "var");
        var other = last.Views.Values.First(v => v != view);
        var before = other.childCount > 0 ? other[0] : null;
        var was = view[0];
        last.Edit("Note", () => (view.Call ?? view.Node).Set("comment", JValue.Of("looked at")));
        Assert.AreSame(view, last.Views[view.Id], "the same view");
        Assert.AreNotSame(was, view[0], "built again");
        Assert.AreSame(before, other.childCount > 0 ? other[0] : null, "the others aren't");
        Undo.PerformUndo();
        // From far away: names, no nodes
        board.FitAll();
        Assert.AreEqual(0, board.NodesDrawn);
        Assert.IsTrue(frames.All(f => board.Frame(f.Id).Level != GraphLevel.Near), "zoom " + board.Zoom);
        Assert.IsTrue(frames.Any(f => board.Frame(f.Id).Level == GraphLevel.Far));
        Assert.IsFalse(window.Dirty);
        window.Close();
    }

    // What "+ Add" and a right click on the board list is the schema's: every kind once, under its group, with
    // what it is; the start's big choices are the schema's common kinds
    [Test]
    public void TheAddMenuIsTheSchemas()
    {
        var path = Write("Add.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var board = window.Board;
        var options = board.AddOptions(null);
        var schema = GraphSchema.Json;
        var kinds = schema.Arr("declarations").Items;
        options = options.Where(o => o.Data is string).ToList(); // But "Paste", when something's copied
        CollectionAssert.AreEquivalent(kinds.Select(d => d.Str("title")), options.Select(o => o.Title), "every kind, by its title");
        foreach (var d in kinds)
        {
            var option = options.Single(o => (string)o.Data == d.Str("kind"));
            var group = schema.Arr("groups").Items.Single(g => g.Str("name") == d.Str("group"));
            Assert.AreEqual(group.Str("title"), option.Section, d.Str("kind") + " is under its group");
            Assert.AreEqual(group.Str("summary"), option.SectionSummary);
            Assert.AreEqual(d.Str("summary"), option.Detail, d.Str("kind") + " says what it is");
        }
        // In the groups' order: Data, Behaviour, More
        CollectionAssert.AreEqual(schema.Arr("groups").Items.Select(g => g.Str("title")), options.Select(o => o.Section).Distinct());
        CollectionAssert.AreEqual(kinds.Where(d => d.Flag("common")).Select(d => d.Str("kind")), board.StartKinds);
        // An empty board says what can be made, with those as its choices
        foreach (var kind in board.StartKinds) Assert.IsNotNull(board.Q<Button>("start:" + kind), kind);
        Assert.IsNotNull(board.Q<Button>("start:more"));
        // The menu itself, searched: typing narrows it
        var menu = board.OpenAdd(new Vector2(200, 200));
        Assert.AreSame(menu, window.Popup);
        menu.Type("every tick");
        Assert.AreEqual(GraphSchema.DeclTitle("system"), menu.Shown.First().Title);
        menu.Type("");
        // A pick makes it, and the start goes
        Assert.IsTrue(menu.Pick(GraphSchema.DeclTitle("enum")));
        Assert.IsNull(window.Popup);
        Assert.AreEqual("enum", window.Doc.Decls.Items.Single().Str("kind"));
        Assert.IsNull(board.Q<Button>("start:more"));
        window.Close();
    }

    // A card's menu offers the members its kind can have and no other, and one of a kind only until it has one
    [Test]
    public void OnlyValidMembersAreOffered()
    {
        var path = Write("Members.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var board = window.Board;
        var schema = GraphSchema.Json;
        foreach (var d in schema.Arr("declarations").Items)
        {
            var kind = d.Str("kind");
            if (GraphSchema.HasBody(kind, false) || kind == "namespace") continue;
            var id = board.AddDeclaration(kind);
            var card = board.Card(id);
            Assert.IsNotNull(card, kind + " is a card");
            var allowed = (d.Arr("memberKinds")?.Items ?? new List<JValue>()).Select(m => m.String).ToList();
            var offered = card.MenuOptions().Where(o => o.Section == "Add").Select(o => (string)o.Data).ToList();
            CollectionAssert.AreEqual(allowed.Where(m => m != "field"), offered, $"a {kind}'s menu adds its kind's members");
            // (An enum's last row adds a choice, the settings' a setting)
            if (kind != "enum" && kind != "settings") Assert.AreEqual(allowed.Contains("field"), card.Q<Button>("add") != null, $"a {kind} has a row that adds a field iff it can have fields");
            // Its options are its kind's modifiers, by their labels
            var options = card.MenuOptions().Where(o => o.Section == GraphSchema.KeyInfo("modifiers").Label).Select(o => o.Title).ToList();
            CollectionAssert.AreEqual((d.Arr("modifiers")?.Items ?? new List<JValue>()).Select(m => GraphSchema.ModifierInfo(m.String).Label), options, kind);
            // "Generic over" only where the kind has type parameters
            var keys = d.Arr("keys").Items.Select(k => k.String).ToList();
            Assert.AreEqual(keys.Contains("typeParams"), card.MenuOptions().Any(o => o.Title.StartsWith(GraphSchema.KeyInfo("typeParams").Label)), kind);
        }
        // Sample is an input's alone, and once
        var component = board.Card(window.Doc.Decls.Items.First(x => x.Str("kind") == "component").Wire("id"));
        Assert.IsFalse(component.OfferedMembers().Contains("sample"), "a component can't have a Sample");
        Assert.AreEqual(0, component.AddMember("sample"), "and isn't given one");
        var input = board.Card(window.Doc.Decls.Items.First(x => x.Str("kind") == "input").Wire("id"));
        CollectionAssert.Contains(input.OfferedMembers(), "sample");
        var sample = input.AddMember("sample");
        Assert.AreNotEqual(0, sample);
        Assert.IsNotNull(board.Frame(sample), "its code is a frame of its own");
        Assert.AreEqual("Input.Sample", board.Frame(sample).Title);
        input = board.Card(input.Id);
        CollectionAssert.DoesNotContain(input.OfferedMembers(), "sample", "one Sample: not offered again");
        Assert.IsFalse(input.MenuOptions().Any(o => (o.Data as string) == "sample"));
        Assert.AreEqual(0, input.AddMember("sample"));
        // An operator is a struct's alone
        var schemaStruct = window.Doc.Decls.Items.First(x => x.Str("kind") == "struct").Wire("id");
        CollectionAssert.Contains(board.Card(schemaStruct).OfferedMembers(), "operator");
        Assert.IsFalse(board.Card(window.Doc.Decls.Items.First(x => x.Str("kind") == "event").Wire("id")).OfferedMembers().Contains("method"), "an event has fields alone");
        window.Close();
    }

    // A card's rows are edited where they're drawn: a name typed, a field added by its last row, a value by its
    // type's control, and nothing a type can't be
    [Test]
    public void ACardsFieldsAreEditedInPlace()
    {
        NeedTidec();
        var path = Write("Card.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var board = window.Board;
        var id = board.AddDeclaration("component");
        Assert.AreEqual(id, board.Fresh, "its name is ready to be typed over");
        GraphShots.Enter(board.Card(id).Q<TextField>("name"), "Wobble");
        var decl = window.Doc.Get(id);
        Assert.AreEqual("Wobble", decl.Str("name"));
        Assert.AreEqual("A value every one of these has", board.Card(id).Q<Label>(className: "tg-empty")?.text, "an empty card says what goes in it");

        // The last row adds a field, of the first of the common types
        GraphShots.Click(board.Card(id).Q<Button>("add"));
        var frequency = decl.Arr("members").Items.Single();
        Assert.AreEqual("field", frequency.Str("kind"));
        Assert.AreEqual(GraphSchema.TypeCommon[0], frequency.Str("type"));
        GraphShots.Enter(board.Card(id).Q<TextField>("name" + frequency.Wire("id")), "frequency");
        Assert.AreEqual("frequency", frequency.Str("name"));
        // Its type is picked from a list, never typed: the list, searched, takes the first match
        GraphShots.Click(board.Card(id).Q<GraphTypeChip>("type" + frequency.Wire("id")));
        var picker = window.Popup as GraphTypePicker;
        Assert.IsNotNull(picker, "a click on the type opens the picker");
        CollectionAssert.AreEqual(GraphSchema.TypeCommon, picker.Shown.Take(GraphSchema.TypeCommon.Length).Select(o => o.Title), "the common types first");
        Assert.IsTrue(picker.Shown.Any(o => o.Title == "Wobble" && o.Section == GraphSchema.TypeGroup("file")), "the file's own");
        picker.Type("flo");
        Assert.AreEqual("float", picker.Shown.First().Title);
        Assert.IsTrue(picker.PickCurrent());
        Assert.AreEqual("float", frequency.Str("type"));
        Assert.IsNull(window.Popup);
        // A type made of others: what it holds is picked with the same list
        GraphShots.Click(board.Card(id).Q<GraphTypeChip>("type" + frequency.Wire("id")));
        picker = (GraphTypePicker)window.Popup;
        picker.Type("list");
        Assert.AreEqual("List of…", picker.Shown.First().Title);
        picker.PickCurrent();
        Assert.AreSame(picker, window.Popup, "it asks what the list holds");
        StringAssert.Contains("what it holds", picker.Title);
        Assert.IsTrue(picker.Choose("int"));
        Assert.AreEqual("List<int>", frequency.Str("type"));
        Undo.PerformUndo();
        Assert.AreEqual("float", window.Doc.Get(frequency.Wire("id")).Str("type"));
        decl = window.Doc.Get(id);
        frequency = decl.Arr("members").Items.Single();

        // Enter in the last field's name goes on to a new field, of the same type
        var name = board.Card(id).Q<TextField>("name" + frequency.Wire("id"));
        GraphShots.Key(name, KeyCode.Return);
        Assert.AreEqual(2, decl.Arr("members").Items.Count);
        var amplitude = decl.Arr("members").Items[1];
        Assert.AreEqual("float", amplitude.Str("type"));
        Assert.AreEqual(amplitude.Wire("id"), board.Fresh);
        // ...and Escape in the new one, left as it is, takes it back
        GraphShots.Key(board.Card(id).Q<TextField>("name" + amplitude.Wire("id")), KeyCode.Escape);
        Assert.AreEqual(1, decl.Arr("members").Items.Count);
        var second = board.Card(id).AddField();
        amplitude = window.Doc.Get(second);
        // Two fields can't share a name: said, and left as it was
        GraphShots.Enter(board.Card(id).Q<TextField>("name" + second), "frequency");
        Assert.AreNotEqual("frequency", amplitude.Str("name"));
        StringAssert.Contains("already", window.LastSaid);
        GraphShots.Enter(board.Card(id).Q<TextField>("name" + second), "amp litude");
        Assert.AreEqual("ampLitude", amplitude.Str("name"), "what's typed is made a name");

        // A starting value, by the type's control: a float takes a number and nothing else
        TextField Value(int field) => board.Card(id).Q("row" + field).Q<VisualElement>(className: "tg-row-value").Q<TextField>();
        var text = window.Doc.Text;
        GraphShots.Enter(Value(second), "fast");
        Assert.AreEqual(text, window.Doc.Text, "not a number: nothing changes");
        StringAssert.Contains("number", window.LastSaid);
        GraphShots.Enter(Value(second), "1.5");
        Assert.AreEqual("float", GraphDocument.NodeIn(amplitude.Get("default"), amplitude.Get("default").Wire("root")).Str("k"));
        GraphShots.Enter(Value(second), "");
        Assert.IsNull(amplitude.Get("default"), "nothing typed is no starting value");
        // A bool is a tick box, a vector its numbers, an enum its choices
        var flag = window.Doc.Get(board.Card(id).AddField());
        window.Edit("bool", () => flag.Set("type", JValue.Of("bool")), id);
        var toggle = board.Card(id).Q("row" + flag.Wire("id")).Q<Toggle>();
        Assert.IsNotNull(toggle);
        toggle.value = true;
        Assert.IsTrue(GraphDocument.NodeIn(flag.Get("default"), flag.Get("default").Wire("root")).Flag("value"));
        var where = window.Doc.Get(board.Card(id).AddField());
        window.Edit("float3", () => where.Set("type", JValue.Of("float3")), id);
        var parts = board.Card(id).Q("row" + where.Wire("id")).Query<TextField>().ToList().Where(f => f.name != "name" + where.Wire("id")).ToList();
        Assert.AreEqual(3, parts.Count);
        GraphShots.Enter(parts[1], "2");
        var enumId = board.AddDeclaration("enum");
        window.Edit("Mood", () =>
        {
            var e = window.Doc.Get(enumId);
            e.Set("name", JValue.Of("Mood"));
            e.Arr("members").Items.Add(window.Doc.NewNamed("Calm"));
            e.Arr("members").Items.Add(window.Doc.NewNamed("Wild"));
        });
        var mood = window.Doc.Get(board.Card(id).AddField());
        window.Edit("Mood", () => mood.Set("type", JValue.Of("Mood")), id);
        GraphShots.Click(board.Card(id).Q("row" + mood.Wire("id")).Q<VisualElement>(className: "tg-row-value").Q<Button>());
        var choices = window.Popup as GraphOptionList;
        Assert.IsNotNull(choices);
        CollectionAssert.AreEqual(new[] { "Calm", "Wild" }, choices.Shown.Where(o => o.Section == "Mood").Select(o => o.Title));
        Assert.IsTrue(choices.Pick("Wild"));

        var tide = Lower(window.Doc);
        StringAssert.Contains("component Wobble {\n    float frequency;\n    float ampLitude;\n    bool value = true;\n    float3 value2 = float3(0.0, 2, 0.0);\n    Mood value3 = Mood.Wild;\n}", tide);
        window.Close();
    }

    // A frame's code is added to from its entry: what it takes are rows with pins, a read of one is a wire from
    // its pin (no node), a value typed on an input is that value (no node), and a wire can't leave its frame
    [UnityTest]
    public IEnumerator AFrameIsMadeOnTheBoard()
    {
        NeedTidec();
        var path = Write("Made.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var board = window.Board;

        // A function, from the add menu: a frame, its name ready
        var f = board.AddDeclaration("function");
        var frame = board.Frame(f);
        Assert.IsNotNull(frame);
        Assert.AreEqual(f, window.SelectedElement);
        GraphShots.Enter(frame.Q<TextField>("name"), "Greet");
        var decl = window.Doc.Get(f);
        Assert.AreEqual("Greet", decl.Str("name"));
        Assert.AreEqual(GraphSchema.Sentence("function", false), frame.Entry.Q<Label>(className: "tg-node-title").text, "its entry reads as its kind's sentence");
        Assert.AreEqual("Drag from the arrow to add what happens", frame.Q<Label>(className: "tg-frame-hint").text, "an empty frame says what to do");

        // An input, added from the entry: a row with a pin
        var times = frame.AddParam(null, "int", "read");
        frame = board.Frame(f);
        Assert.AreEqual("value", window.Doc.Get(times).Str("name"), "named for its type");
        GraphShots.Enter(frame.Entry.Q<TextField>("name" + times), "times");
        Assert.IsNotNull(frame.Entry.ReadPin("times"));
        Assert.IsFalse(frame.Entry.ReadPin("times").Connected);

        // A call wired from the entry's arrow: one node, the statement with its call
        var start = frame.Entry.Ports.First(p => p.Port.Exec);
        var search = board.OpenSearch(frame, new Vector2(520, 60), start.Port);
        Assert.AreSame(search, window.Popup);
        Assert.IsTrue(search.Entries.All(e => e.Statement), "an arrow takes a step");
        Assert.IsTrue(search.Pick("Call a function"));
        Assert.IsNull(window.Popup, "the search closes");
        var graph = frame.Graph;
        var eval = GraphDocument.NodeIn(graph, graph.Wire("entry"));
        Assert.AreEqual("eval", eval.Str("k"));
        Assert.IsTrue(frame.Absorbed.ContainsKey(eval.Wire("value")), "the call is drawn with its statement");
        Assert.AreEqual(1, frame.Views.Count);
        var call = GraphDocument.NodeIn(graph, eval.Wire("value"));
        GraphShots.Enter(frame.Views[eval.Wire("id")].Q<TextField>("title"), "Print");
        Assert.AreEqual("Print", call.Str("name"));
        // An argument, and the parameter dragged into it: its name in the input, and no node or wire for the read
        GraphShots.Click(frame.Views[eval.Wire("id")].Q<Button>("add:arg"));
        var arg = frame.Views[eval.Wire("id")].Input("args.0");
        Assert.IsNotNull(arg);
        Assert.IsTrue(board.Connect(frame.Entry.ReadPin("times"), arg, out var why), why);
        var read = GraphDocument.NodeIn(graph, call.Arr("args").Items[0].Number > 0 ? (int)call.Arr("args").Items[0].Number : 0);
        Assert.AreEqual("name", read.Str("k"));
        Assert.AreEqual("times", read.Str("name"));
        Assert.IsTrue(frame.Hidden.Contains(read.Wire("id")), "the read isn't a node on the board");
        Assert.AreEqual(1, frame.Views.Count);
        Assert.IsTrue(frame.Entry.ReadPin("times").Connected, "the parameter's pin says it's used");
        Assert.IsNull(frame.SourcePin(read.Wire("id")), "no wire: the input says what it reads");
        Assert.AreEqual("times", frame.Views[eval.Wire("id")].Q("in:args.0").Q("chip").Q<Button>("part0").text);
        Assert.AreEqual("argument 1", frame.Views[eval.Wire("id")].Q("in:args.0").Q<Label>(className: "tg-port-label").text, "until a check names it");
        // Renaming the parameter renames what reads it
        GraphShots.Enter(frame.Entry.Q<TextField>("name" + times), "count");
        Assert.AreEqual("count", read.Str("name"));

        // An if after it, from the search in the frame, wired by its pins
        search = board.OpenSearch(frame, new Vector2(800, 60), null);
        Assert.IsTrue(search.Pick("If"));
        var @if = GraphDocument.Nodes(graph).Items.Last(n => n.Str("k") == "if");
        Assert.IsTrue(frame.Views[@if.Wire("id")].ClassListContains("tg-node--unreached"), "nothing runs it yet: faded");
        Assert.IsTrue(board.Connect(frame.Views[eval.Wire("id")].Output("next"), frame.Views[@if.Wire("id")].ExecIn, out why), why);
        Assert.AreEqual(@if.Wire("id"), eval.Wire("next"));
        Assert.IsFalse(frame.Views[@if.Wire("id")].ClassListContains("tg-node--unreached"));
        Assert.IsFalse(board.Connect(frame.Entry.Ports.First(p => p.Port.Exec), frame.Views[@if.Wire("id")].ExecIn, out why), "a step runs from one place");
        StringAssert.Contains("one place", why);
        // Its condition typed on its input: true, with no node drawn for it
        frame.TypeInto(@if, new WirePath("cond"), "true", frame.Views[@if.Wire("id")]);
        var cond = GraphDocument.NodeIn(graph, @if.Wire("cond"));
        Assert.AreEqual("bool", cond.Str("k"));
        Assert.IsTrue(frame.Hidden.Contains(cond.Wire("id")));
        Assert.AreEqual(2, frame.Views.Count);
        Assert.IsTrue(frame.Views[@if.Wire("id")].Q("in:cond").Q<Toggle>("ticked").value, "a tick is its control");
        // Typing nothing takes it away again, and the input says "pick…"
        frame.TypeInto(@if, new WirePath("cond"), "", frame.Views[@if.Wire("id")]);
        Assert.AreEqual(0, @if.Wire("cond"));
        Assert.IsNull(window.Doc.Find(cond.Wire("id")), "what was typed is gone with it");
        Assert.AreEqual("pick…", frame.Views[@if.Wire("id")].Q("in:cond").Q<Button>("pick").text);
        Assert.AreEqual("bool", frame.Wanted(@if, new WirePath("cond")), "a condition takes true or false");
        // A path typed is a node for each part in the file, and one chip on the board; a '-' before a number is
        // a node of its own too, as tidec writes it
        frame.TypeInto(call, new WirePath("args", 0), "count.digits.first", frame.Views[eval.Wire("id")]);
        var leaf = GraphDocument.NodeIn(graph, call.Arr("args").Items[0].Number > 0 ? (int)call.Arr("args").Items[0].Number : 0);
        Assert.AreEqual("member", leaf.Str("k"));
        Assert.AreEqual("first", leaf.Str("member"));
        Assert.AreEqual("digits", GraphDocument.NodeIn(graph, leaf.Wire("object")).Str("member"));
        Assert.AreEqual("count", GraphDocument.NodeIn(graph, GraphDocument.NodeIn(graph, leaf.Wire("object")).Wire("object")).Str("name"));
        Assert.IsNull(window.Doc.Find(read.Wire("id")), "what was in the input went with it");
        Assert.AreEqual("count.digits.first", frame.TextOf(frame.ShownIn(leaf.Wire("id"))));
        CollectionAssert.AreEqual(new[] { "count", ".digits", ".first" }, frame.Views[eval.Wire("id")].Q("in:args.0").Q("chip").Query<Button>().ToList().Select(b => b.text).ToList());
        Assert.AreEqual(2, frame.Views.Count, "none of it is a node on the board");
        var nodes = GraphDocument.Nodes(graph).Items.Count;
        frame.TypeInto(call, new WirePath("args", 0), "-3", frame.Views[eval.Wire("id")]);
        var minus = GraphDocument.NodeIn(graph, call.Arr("args").Items[0].Number > 0 ? (int)call.Arr("args").Items[0].Number : 0);
        Assert.AreEqual("unary", minus.Str("k"));
        Assert.AreEqual("-", minus.Str("op"));
        Assert.AreEqual("3", GraphDocument.NodeIn(graph, minus.Wire("value")).Str("text"));
        Assert.AreEqual(nodes - 1, GraphDocument.Nodes(graph).Items.Count, "the three of the path went, and two came");
        Assert.AreEqual("-3", frame.Views[eval.Wire("id")].Q("in:args.0").Q<TextField>("typed").value);
        Assert.AreEqual(2, frame.Views.Count);
        // A value two inputs read is a node, wired to both: it has to be seen to be one
        frame.TypeInto(@if, new WirePath("cond"), "false", frame.Views[@if.Wire("id")]);
        var shared = @if.Wire("cond");
        Assert.IsTrue(frame.Hidden.Contains(shared));
        window.Edit("Read twice", () => call.Arr("args").Items.Add(JValue.Of(shared)), f);
        frame = board.Frame(f);
        Assert.IsFalse(frame.Hidden.Contains(shared), "read by two inputs, it's a node");
        Assert.IsTrue(frame.Views.ContainsKey(shared));
        Assert.IsTrue(frame.Views[@if.Wire("id")].Input("cond").Connected);
        Assert.AreSame(frame.Views[shared].DataOut, frame.SourcePin(shared));
        // ...and every node is one, for a picture of the file as it is
        GraphFrame.DrawEveryNode = true;
        try
        {
            frame.Classify();
            Assert.IsEmpty(frame.Hidden);
        }
        finally
        {
            GraphFrame.DrawEveryNode = false;
            frame.Classify();
        }
        window.Edit("Read once", () => call.Arr("args").Items.RemoveAt(1), f);
        frame = board.Frame(f);
        Assert.IsTrue(frame.Hidden.Contains(shared), "read by one again, it's in the input again");
        // The parameter's name typed puts it back
        frame.TypeInto(call, new WirePath("args", 0), "count", frame.Views[eval.Wire("id")]);
        Assert.AreEqual("count", frame.TextOf(frame.ShownIn(call.Arr("args").Items[0].Number > 0 ? (int)call.Arr("args").Items[0].Number : 0)));

        // A wire can't leave its frame, and the board says why
        var g = board.AddDeclaration("function");
        var other = board.Frame(g);
        var text = window.Doc.Text;
        Assert.IsFalse(board.Connect(frame.Entry.ReadPin("count"), other.Entry.Ports.First(p => p.Port.Exec), out why));
        StringAssert.Contains("stays inside its frame", why);
        var into = board.OpenSearch(other, new Vector2(520, 60), other.Entry.Ports.First(p => p.Port.Exec).Port);
        into.Pick("Return");
        other = board.Frame(g);
        var ret = other.Views.Values.Single();
        text = window.Doc.Text;
        Assert.IsFalse(board.Connect(frame.Entry.ReadPin("count"), ret.Input("value"), out why));
        StringAssert.Contains("stays inside its frame", why);
        StringAssert.Contains("Greet", why);
        Assert.AreEqual(text, window.Doc.Text, "nothing changes");
        board.Delete(g);
        Assert.IsNull(board.Frame(g));

        // Copied, pasted, deleted and undone
        frame = board.Frame(f);
        graph = frame.Graph;
        board.SelectNode(frame, @if.Wire("id"), false);
        board.CopySelection();
        var count = GraphDocument.Nodes(graph).Items.Count;
        board.Paste(new Vector2(700, 500));
        graph = board.Frame(f).Graph;
        Assert.AreEqual(count + 2, GraphDocument.Nodes(graph).Items.Count, "the if, and the value typed on it");
        board.DeleteSelection();
        Assert.AreEqual(count, GraphDocument.Nodes(board.Frame(f).Graph).Items.Count);
        Undo.PerformUndo();
        Assert.AreEqual(count + 2, GraphDocument.Nodes(board.Frame(f).Graph).Items.Count);
        board.SelectItem(board.Frame(f), false);
        board.SelectAll();
        board.DuplicateSelection();
        Assert.AreEqual(2 * (count + 2), GraphDocument.Nodes(board.Frame(f).Graph).Items.Count);
        Undo.PerformUndo();
        Undo.PerformUndo();
        Assert.AreEqual(count, GraphDocument.Nodes(board.Frame(f).Graph).Items.Count);

        // A note, and the layout
        frame = board.Frame(f);
        board.AddNote(frame, new Vector2(400, 300));
        Assert.AreEqual(1, board.Frame(f).Notes.Count);
        board.Tidy(board.Frame(f));
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); board.LayoutPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsFalse(board.LayoutPending, "tidels lays it out");

        // What it lowers to
        window.CheckSoon(0);
        yield return null;
        yield return Checked(window);
        Assert.IsNull(window.Checked.Failure);
        Assert.IsEmpty(window.Checked.Diagnostics, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        Assert.AreEqual("void Greet(int count) {\n    Print(count);\n    if (false) {\n    }\n}", window.Checked.Text.Replace("\r\n", "\n").Trim('\n'));

        // Duplicated, moved to a place of its own, and deleted
        var copy = board.Duplicate(f);
        Assert.AreEqual(2, window.Doc.Decls.Items.Count);
        Assert.AreEqual("Greet2", window.Doc.Get(copy).Str("name"));
        Assert.AreNotEqual(board.Frame(f).At, board.Frame(copy).At, "beside the one it's a copy of");
        Assert.IsTrue(GraphDocument.HasPlace(window.Doc.Get(copy)), "where it is is the file's");
        board.Delete(copy);
        Assert.AreEqual(1, window.Doc.Decls.Items.Count);

        window.Save();
        Assert.AreEqual(window.Doc.Text, File.ReadAllText(path));
        // What's written is in tidec's key order: x and y after id
        var keys = window.Doc.Get(f).Keys;
        CollectionAssert.AreEqual(new[] { "kind", "id", "x", "y", "name" }, keys.Take(5));
        window.Close();
    }

    // The mouse and the keys, as the system sends them: no click does nothing. Needs the window laid out, which a
    // run with no graphics device (-nographics) doesn't do: there this is left out, and says so.
    [UnityTest]
    public IEnumerator TheMouseAndTheKeysWorkTheBoard()
    {
        NeedTidec();
        var b = new Body();
        b.Function.Arr("params").Items.Add(b.Doc.NewParam("int", "count"));
        var ret = b.N("return");
        var print = b.N("eval", "value", b.N("call", "name", "Print", "args", new[] { 0 }));
        b.Node(ret).Set("x", JValue.Of(420));
        b.Node(print).Set("x", JValue.Of(60));
        b.Node(print).Set("y", JValue.Of(160));
        var other = b.Doc.NewDecl("function", "Other");
        b.Doc.Decls.Items.Add(other);
        b.Doc.Reindex();
        var path = Write("Mouse.tidegraph", b.Doc.Text);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        GraphShots.Size(window);
        yield return GraphShots.Settle(6);
        var board = window.Board;
        if (float.IsNaN(board.layout.width) || board.layout.width <= 0)
        {
            window.Close();
            Assert.Ignore("The window isn't laid out without a graphics device (-nographics): the mouse has nothing to land on. Run it with one, on a desktop of its own.");
        }
        var f = b.Function.Wire("id");
        var frame = board.Frame(f);
        var text = window.Doc.Text;

        // A right click on the empty board: what to add, there
        var empty = new Vector2(board.worldBound.xMax - 60, board.worldBound.yMax - 80);
        GraphShots.Click(window, empty, 1);
        var add = window.Popup as GraphOptionList;
        Assert.IsNotNull(add, "a right click on the board opens what to add");
        Assert.AreEqual("What do you want to make?", add.Title);
        // Typing narrows it, and Enter takes the first match; Escape closes it without a pick
        yield return GraphShots.Settle(2);
        GraphShots.Keys(window, "enu");
        yield return GraphShots.Settle(2);
        Assert.AreEqual(GraphSchema.DeclTitle("enum"), add.Shown[0].Title, "what's typed narrows the list");
        GraphShots.Press(window, KeyCode.Escape);
        Assert.IsNull(window.Popup, "Escape closes it");
        Assert.AreEqual(text, window.Doc.Text);
        // Space there does the same
        GraphShots.Mouse(window, EventType.MouseMove, empty);
        GraphShots.Press(window, KeyCode.Space);
        Assert.IsNotNull(window.Popup as GraphOptionList, "Space on the board opens what to add");
        Assert.IsFalse(window.Popup is GraphSearchPopup);
        // A click outside it closes it
        GraphShots.Click(window, empty + new Vector2(-400, -200));
        Assert.IsNull(window.Popup, "a click outside closes it");

        // A click on a node selects it, and a drag moves it: where it is is the file's, and Undo's
        var returns = frame.Views[ret];
        var was = GraphDocument.PlaceOf(returns.Node);
        var head = new Vector2(returns.worldBound.x + 60, returns.worldBound.y + 10);
        GraphShots.Click(window, head);
        Assert.IsTrue(board.Nodes.Contains(ret), "a click selects the node");
        Assert.AreSame(frame, board.Active);
        GraphShots.Drag(window, head, head + new Vector2(80, 40));
        Assert.AreEqual(was + new Vector2(80, 40), GraphDocument.PlaceOf(window.Doc.Get(ret)), "a drag moves it");
        Undo.PerformUndo();
        Assert.AreEqual(was, GraphDocument.PlaceOf(window.Doc.Get(ret)));
        yield return GraphShots.Settle(3);
        frame = board.Frame(f);

        // A wire dragged from the entry's arrow to a step's arrow
        var start = frame.Entry.Ports.First(p => p.Port.Exec);
        GraphShots.Drag(window, GraphShots.Middle(start), GraphShots.Middle(frame.Views[print].ExecIn));
        Assert.AreEqual(print, frame.Graph.Wire("entry"), "a drag from pin to pin wires them");
        yield return GraphShots.Settle(3);
        // ...and from a parameter's pin to an input: its name in the input, with no node or wire of its own
        frame = board.Frame(f);
        GraphShots.Drag(window, GraphShots.Middle(frame.Entry.ReadPin("count")), GraphShots.Middle(frame.Views[print].Input("args.0")));
        var call = GraphDocument.NodeIn(frame.Graph, window.Doc.Get(print).Wire("value"));
        var read = GraphDocument.NodeIn(frame.Graph, JValue.AsId(call.Arr("args").Items[0]));
        Assert.IsNotNull(read, "a drag from a parameter's pin to an input reads it");
        Assert.AreEqual("count", read.Str("name"));
        Assert.IsTrue(frame.Hidden.Contains(read.Wire("id")));
        yield return GraphShots.Settle(3);
        // Alt and a click on the input empties it, and the read goes with it
        frame = board.Frame(f);
        var input = GraphShots.Middle(frame.Views[print].Input("args.0"));
        GraphShots.Mouse(window, EventType.MouseDown, input, 0, EventModifiers.Alt);
        GraphShots.Mouse(window, EventType.MouseUp, input, 0, EventModifiers.Alt);
        Assert.IsNull(window.Doc.Find(read.Wire("id")), "Alt+click empties the input");
        yield return GraphShots.Settle(3);

        // A wire dropped on nothing in its frame asks what to add for it
        frame = board.Frame(f);
        var next = frame.Views[print].Output("next");
        var inside = new Vector2(frame.worldBound.xMax - 30, frame.worldBound.yMax - 12);
        GraphShots.Drag(window, GraphShots.Middle(next), inside);
        var search = window.Popup as GraphSearchPopup;
        Assert.IsNotNull(search, "a wire dropped in its frame opens the search for it");
        Assert.AreEqual("What happens next?", search.Title);
        Assert.IsTrue(search.Entries.All(e => e.Statement));
        GraphShots.Press(window, KeyCode.Escape);
        Assert.IsNull(window.Popup);
        // ...and one dropped outside its frame, or on another frame's pin, is told why not
        text = window.Doc.Text;
        GraphShots.Drag(window, GraphShots.Middle(next), empty);
        Assert.IsNull(window.Popup);
        StringAssert.Contains("stays inside its frame", window.LastSaid);
        window.Reveal(other.Wire("id"));
        yield return GraphShots.Settle(4);
        frame = board.Frame(f);
        var across = board.Frame(other.Wire("id"));
        if (frame.Level == GraphLevel.Near && frame.Views[print].Output("next").worldBound.yMin > board.worldBound.yMin)
        {
            GraphShots.Drag(window, GraphShots.Middle(frame.Views[print].Output("next")), GraphShots.Middle(across.Entry.Ports.First(p => p.Port.Exec)));
            StringAssert.Contains("Other", window.LastSaid, "it says whose pin that is");
        }
        Assert.AreEqual(text, window.Doc.Text, "and nothing is wired");
        window.Reveal(f);
        yield return GraphShots.Settle(4);

        // A right click in a frame, and Space with the pointer in it: the search for that frame
        frame = board.Frame(f);
        inside = new Vector2(frame.worldBound.xMax - 30, frame.worldBound.yMax - 12);
        GraphShots.Click(window, inside, 1);
        search = window.Popup as GraphSearchPopup;
        Assert.IsNotNull(search, "a right click in a frame opens its search");
        Assert.AreEqual("Add to F", search.Title);
        // What's typed and Enter: the node, where the click was
        yield return GraphShots.Settle(2);
        GraphShots.Keys(window, "brea");
        yield return GraphShots.Settle(2);
        Assert.AreEqual("Break", search.Entries[0].Title);
        GraphShots.Press(window, KeyCode.Return);
        Assert.IsNull(window.Popup);
        var made = GraphDocument.Nodes(board.Frame(f).Graph).Items.Last();
        Assert.AreEqual("break", made.Str("k"), "Enter takes the first match");
        Assert.IsTrue(board.Nodes.Contains(made.Wire("id")), "and it's selected");
        yield return GraphShots.Settle(3);
        // Delete takes what's selected away
        GraphShots.Press(window, KeyCode.Delete);
        Assert.IsNull(window.Doc.Find(made.Wire("id")), "Delete deletes the selected node");
        GraphShots.Mouse(window, EventType.MouseMove, inside);
        GraphShots.Press(window, KeyCode.Space);
        Assert.IsNotNull(window.Popup as GraphSearchPopup, "Space in a frame opens its search");
        GraphShots.Press(window, KeyCode.Escape);

        // A frame is moved by its title, with all that's in it
        yield return GraphShots.Settle(2);
        frame = board.Frame(f);
        var title = new Vector2(frame.worldBound.x + 300, frame.worldBound.y + 14);
        var place = frame.At;
        GraphShots.Drag(window, title, title + new Vector2(30, 50));
        Assert.AreEqual(place + new Vector2(30, 50), GraphDocument.PlaceOf(window.Doc.Get(f)), "a drag of its title moves the frame");
        Assert.AreEqual(place + new Vector2(30, 50), board.Frame(f).At);
        Undo.PerformUndo();
        yield return GraphShots.Settle(3);

        // A box dragged in a frame selects its nodes; on the board, cards and frames
        frame = board.Frame(f);
        var corner = new Vector2(frame.worldBound.xMax - 6, frame.worldBound.yMax - 6);
        GraphShots.Drag(window, corner, new Vector2(frame.worldBound.x + 260, frame.worldBound.y + 40));
        CollectionAssert.IsSubsetOf(new[] { ret, print }, board.Nodes.ToList(), "a box in a frame selects its nodes");
        GraphShots.Press(window, KeyCode.Escape);
        Assert.IsEmpty(board.Nodes);

        // The wheel zooms about the pointer, and F shows everything
        var zoom = board.Zoom;
        window.SendEvent(new Event { type = EventType.ScrollWheel, mousePosition = inside, delta = new Vector2(0, 3) });
        Assert.Less(board.Zoom, zoom, "the wheel zooms out");
        GraphShots.Click(window, empty);
        GraphShots.Press(window, KeyCode.F);
        yield return GraphShots.Settle(2);

        // A click on a type opens the picker by it, and what's typed with Enter picks
        var result = board.Frame(f).Entry.Q<GraphTypeChip>("result");
        if (result.worldBound.yMin > board.worldBound.yMin && result.worldBound.yMax < board.worldBound.yMax)
        {
            GraphShots.Click(window, GraphShots.Middle(result));
            var picker = window.Popup as GraphTypePicker;
            Assert.IsNotNull(picker, "a click on a type opens the picker");
            yield return GraphShots.Settle(2);
            GraphShots.Keys(window, "boo");
            yield return GraphShots.Settle(2);
            Assert.AreEqual("bool", picker.Shown[0].Title);
            GraphShots.Press(window, KeyCode.Return);
            Assert.AreEqual("bool", window.Doc.Get(f).Str("returnType"), "Enter takes the first match");
        }
        window.Close();
    }

    // Both of the editor's themes build the board, with Unity's own variables: the light one too
    [Test]
    public void TheBoardBuildsInBothThemes()
    {
        var b = new Body();
        b.Run(b.N("return"));
        var path = Write("Themes.tidegraph", b.Doc.Text);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        foreach (var light in new[] { true, false })
        {
            window.ForceTheme(light);
            Assert.AreEqual(light, window.Light);
            Assert.AreEqual(light, window.rootVisualElement.Q(className: "tg-root").ClassListContains("tg-light"));
            Assert.IsNotNull(window.Board.Frame(b.Function.Wire("id")));
        }
        window.ForceTheme(null);
        window.Close();
    }

    // A file that changes on disk is read again, when nothing in the window is unsaved
    [UnityTest]
    public IEnumerator AFileChangedOnDiskIsReadAgain()
    {
        var path = Write("Changed.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var b = new Body();
        b.Run(b.N("break"));
        File.WriteAllText(path, b.Doc.Text);
        File.SetLastWriteTimeUtc(path, DateTime.UtcNow.AddSeconds(5));
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); window.Doc.Decls.Items.Count == 0 && clock.Elapsed.TotalSeconds < 10;) yield return null;
        Assert.AreEqual(b.Doc.Text, window.Doc.Text);
        Assert.IsFalse(window.Dirty);
        Assert.IsNotNull(window.Board.Frame(b.Function.Wire("id")), "and shown");
        window.Close();
    }
}
