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
// lowers to the Tide they stand for, and its window opens, edits, undoes, saves and closes cleanly.
public class GraphTests
{
    static string tidec;
    static ITideGraphChecker checker;

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
    }

    [OneTimeTearDown]
    public void Forget()
    {
        TideGraphCommand.TidecOverride = null;
        TideGraphServices.Checker = checker;
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

    // Every node kind the search offers makes the kind it says, and every kind of the format is offered
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
        Assert.IsTrue(entries.Any(e => e.Group == "Locals" && e.Title == "v"));
        Assert.IsTrue(entries.Any(e => e.Group == "Parameters" && e.Title == "speed"));
        Assert.AreEqual(GraphSchema.BinaryOps.Length, entries.Count(e => e.Make != null && e.Keywords.StartsWith("binary")));
        // A value's graph has no statements
        var value = new GraphSearchContext { Doc = b.Doc, Graph = b.Graph, Slot = new GraphSlot(b.Function.Wire("id"), "value") };
        Assert.IsFalse(TideGraphServices.Searches.SelectMany(s => s.Entries(value)).Any(e => e.Group == "Flow"));
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

    // Declarations and members of every kind, made as the outline makes them, lower to Tide
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
        Assert.AreEqual(new GraphSlot(b.Function.Wire("id"), "body"), window.Slot, "the first body opens");
        Assert.AreEqual(2, window.Canvas.Views.Count, "the statement and the body's start");
        var original = window.Doc.Text;

        // An edit through the window: recorded, shown, and undone
        var graph = window.Doc.GetGraph(window.Slot);
        window.Edit("Add break", () =>
        {
            var n = window.Doc.AddNode(graph, "break", 200, 0);
            GraphDocument.NodeIn(graph, graph.Wire("entry")).Set("next", JValue.Of(n.Wire("id")));
        });
        Assert.IsTrue(window.Dirty);
        Assert.AreEqual(3, window.Canvas.Views.Count);
        var edited = window.Doc.Text;
        Undo.PerformUndo();
        Assert.AreEqual(original, window.Doc.Text);
        Assert.AreEqual(2, window.Canvas.Views.Count);
        Undo.PerformRedo();
        Assert.AreEqual(edited, window.Doc.Text);

        // Every element shown in the inspector, every graph in the canvas
        foreach (var e in window.Doc.Elements.ToList()) window.Select(e.Id);
        window.Select(GraphOutlineView.FileRow);

        // What tidec says of it
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsNotNull(window.Checked);
        Assert.IsNull(window.Checked.Failure);
        Assert.IsEmpty(window.Checked.Diagnostics);
        StringAssert.Contains("return;\n    break;", window.Checked.Text.Replace("\r\n", "\n"));

        window.Save();
        Assert.IsFalse(window.Dirty);
        Assert.AreEqual(edited, File.ReadAllText(path));
        window.Close();
    }

    [UnityTest]
    public IEnumerator TidecsErrorsMarkTheirNodes()
    {
        NeedTidec();
        var b = new Body();
        var add = b.N("binary", "op", "+", "lhs", b.N("int", "text", "1"));
        b.Run(b.N("return", "value", add));
        var path = Write("Broken.tidegraph", b.Doc.Text);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        window.CheckSoon(0);
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsNotNull(window.Checked);
        var error = window.Checked.Diagnostics.FirstOrDefault(d => d.Id == add);
        Assert.IsNotNull(error, "tidec names the node: " + string.Join("; ", window.Checked.Diagnostics.Select(d => $"@{d.Id} {d.Message}")));
        StringAssert.Contains("rhs", error.Message);
        Assert.IsTrue(window.Canvas.Views.ContainsKey(add));
        StringAssert.Contains("rhs", window.Canvas.Views[add].tooltip);
        window.JumpTo(error);
        Assert.AreEqual(add, window.SelectedElement);
        Assert.IsTrue(window.Canvas.Selection.Contains(add));
        window.Close();
    }

    // Every declaration, member and graph of real games' files shows in the window
    [Test]
    public void ConvertedGamesShowInTheWindow()
    {
        NeedTidec();
        using var logged = new Logged();
        foreach (var tide in Directory.GetFiles(Path.Combine(Repository, "examples", "sumo"), "*.tide").Concat(Directory.GetFiles(Path.Combine(Repository, "examples", "towers"), "*.tide")))
        {
            var path = Path.Combine(Scratch, Path.GetFileNameWithoutExtension(tide) + ".tidegraph");
            Assert.IsNull(TideGraphCommand.Convert(tide, path));
            var window = TideGraphWindow.Open(path);
            Assert.IsNull(window.ReadError);
            var slots = new HashSet<GraphSlot>();
            foreach (var e in window.Doc.Elements.ToList())
            {
                window.Select(e.Id);
                if (e.Role == GraphRole.Node) slots.Add(e.Slot);
            }
            foreach (var slot in slots)
            {
                window.Show(slot);
                var nodes = GraphDocument.Nodes(window.Doc.GetGraph(slot)).Items.Count;
                Assert.AreEqual(nodes, window.Canvas.Views.Count(v => v.Key > 0) + window.Canvas.Absorbed.Count, $"{path} {slot}");
            }
            Assert.IsFalse(window.Dirty, "looking changes nothing");
            window.Close();
        }
    }

    static GraphSearchEntry Entry(GraphCanvas canvas, string title)
    {
        var popup = canvas.Q<GraphSearchPopup>();
        Assert.IsNotNull(popup, "the search is open");
        var entry = popup.Shown.FirstOrDefault(e => e.Title == title);
        Assert.IsNotNull(entry, $"the search offers '{title}': {string.Join(", ", popup.Shown.Select(e => e.Title))}");
        return entry;
    }

    // A graph made from nothing as the outline, the search, the canvas's wires and the inspector make it
    [UnityTest]
    public IEnumerator AGraphIsMadeInTheWindow()
    {
        NeedTidec();
        var path = Write("Made.tidegraph", TideGraphImporter.EmptyText);
        using var logged = new Logged();
        var window = TideGraphWindow.Open(path);
        var canvas = window.Canvas;

        // A function, from the outline's menu: selected, and its body open
        window.Outline.AddDecl("function", null);
        var f = window.Doc.Decls.Items.Last();
        Assert.AreEqual("function", f.Str("kind"));
        Assert.AreEqual(new GraphSlot(f.Wire("id"), "body"), window.Slot);
        Assert.AreEqual(f.Wire("id"), window.SelectedElement);

        // Renamed in the inspector
        var name = window.Inspector.Query<TextField>().ToList().First(t => t.label == "name");
        name.value = "Greet";
        Assert.AreEqual("Greet", f.Str("name"));

        // A call wired from the body's start: one node, the statement with its call
        var start = canvas.Views[GraphNodeView.EntryId].Ports[0];
        canvas.OpenSearch(new Vector2(200, 100), start.Port);
        Assert.IsFalse(canvas.Q<GraphSearchPopup>().Shown.Any(e => e.Group != "Flow"), "a statement's flow takes a statement");
        canvas.Insert(Entry(canvas, "Call"), new Vector2(200, 100), start.Port);
        var graph = window.Doc.GetGraph(window.Slot);
        var eval = GraphDocument.NodeIn(graph, graph.Wire("entry"));
        Assert.AreEqual("eval", eval.Str("k"));
        Assert.IsTrue(canvas.Absorbed.ContainsKey(eval.Wire("value")), "the call is drawn with its statement");
        Assert.IsNull(canvas.Q<GraphSearchPopup>(), "the search closes");

        // Its name typed on the node
        var call = GraphDocument.NodeIn(graph, eval.Wire("value"));
        var callName = canvas.Views[eval.Wire("id")].Query<TextField>().ToList().First(t => t.value == "Function");
        callName.value = "Print";
        Assert.AreEqual("Print", call.Str("name"));

        // An if after it, from the search on the canvas, wired by its ports
        canvas.OpenSearch(new Vector2(500, 100), null);
        canvas.Insert(Entry(canvas, "If"), new Vector2(500, 100), null);
        var @if = GraphDocument.Nodes(graph).Items.Last(n => n.Str("k") == "if");
        Assert.IsTrue(canvas.Views[@if.Wire("id")].resolvedStyle.opacity < 1 || !GraphDocument.Reached(graph).Contains(@if.Wire("id")), "nothing runs it yet");
        var next = canvas.Views[eval.Wire("id")].PortAt(eval.Wire("id"), new WirePath("next"), false, true);
        var into = canvas.Views[@if.Wire("id")].ExecIn;
        Assert.IsTrue(canvas.Connect(next.Port, into.Port, out var why), why);
        Assert.AreEqual(@if.Wire("id"), eval.Wire("next"));
        Assert.IsFalse(canvas.Connect(canvas.Views[GraphNodeView.EntryId].Ports[0].Port, canvas.Views[@if.Wire("id")].ExecIn.Port, out _), "a statement runs from one place");

        // Its condition: a value from the search, wired into the input it was dropped from
        var cond = canvas.Views[@if.Wire("id")].PortAt(@if.Wire("id"), new WirePath("cond"), true, false);
        canvas.OpenSearch(new Vector2(300, 300), cond.Port);
        canvas.Insert(Entry(canvas, "Bool"), new Vector2(300, 300), cond.Port);
        var boolean = GraphDocument.NodeIn(graph, @if.Wire("cond"));
        Assert.AreEqual("bool", boolean.Str("k"));

        // Copied, pasted, deleted and undone
        canvas.SelectOnly(boolean.Wire("id"));
        canvas.CopySelection();
        var count = GraphDocument.Nodes(graph).Items.Count;
        canvas.Paste(new Vector2(50, 400));
        graph = window.Doc.GetGraph(window.Slot);
        Assert.AreEqual(count + 1, GraphDocument.Nodes(graph).Items.Count);
        canvas.DeleteSelection();
        graph = window.Doc.GetGraph(window.Slot);
        Assert.AreEqual(count, GraphDocument.Nodes(graph).Items.Count);
        Undo.PerformUndo();
        Assert.AreEqual(count + 1, GraphDocument.Nodes(window.Doc.GetGraph(window.Slot)).Items.Count);
        canvas.SelectAll();
        canvas.Duplicate();
        Assert.AreEqual(2 * (count + 1), GraphDocument.Nodes(window.Doc.GetGraph(window.Slot)).Items.Count);
        Undo.PerformUndo();
        Undo.PerformUndo();
        Assert.AreEqual(count, GraphDocument.Nodes(window.Doc.GetGraph(window.Slot)).Items.Count);

        // A note, and the layout
        canvas.AddNote(new Vector2(10, 10));
        Assert.AreEqual(1, canvas.Notes.Count);
        canvas.AutoLayout();
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); canvas.LayoutPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsFalse(canvas.LayoutPending, "tidels lays it out");

        // What it lowers to
        window.CheckSoon(0);
        for (var clock = System.Diagnostics.Stopwatch.StartNew(); window.CheckPending && clock.Elapsed.TotalSeconds < 60;) yield return null;
        Assert.IsNull(window.Checked.Failure);
        Assert.IsEmpty(window.Checked.Diagnostics, string.Join("; ", window.Checked.Diagnostics.Select(d => d.Message)));
        Assert.AreEqual("void Greet() {\n    Print();\n    if (false) {\n    }\n}", window.Checked.Text.Replace("\r\n", "\n").Trim('\n'));

        // The outline duplicates, moves and deletes
        var e = window.Doc.Find(f.Wire("id"));
        window.Outline.Duplicate(e);
        Assert.AreEqual(2, window.Doc.Decls.Items.Count);
        Assert.AreEqual("Greet2", window.Doc.Decls.Items[1].Str("name"));
        window.Outline.Move(window.Doc.Find(window.Doc.Decls.Items[1].Wire("id")), -1);
        Assert.AreEqual("Greet2", window.Doc.Decls.Items[0].Str("name"));
        window.Outline.Delete(window.Doc.Find(window.Doc.Decls.Items[0].Wire("id")));
        Assert.AreEqual(1, window.Doc.Decls.Items.Count);
        window.Outline.AddMember(window.Doc.NewDecl("struct", "S"), "field");

        window.Save();
        Assert.AreEqual(window.Doc.Text, File.ReadAllText(path));
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
        window.Close();
    }
}
