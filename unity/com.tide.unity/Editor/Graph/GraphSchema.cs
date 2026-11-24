using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace Tide.Editor
{
    // What a .tidegraph holds (notes/visual-scripting.md): its kinds of declaration, member and node, the keys of
    // each in the order tidec writes them, and which of a node's keys are wires.
    static class GraphSchema
    {
        public static readonly string[] RootOrder = { "tidegraph", "nextId", "namespace", "using", "decls" };

        // Declarations, as the add menu lists them
        public static readonly string[] DeclKinds =
        {
            "component", "singleton", "struct", "event", "input", "scene", "enum", "const", "asset", "alias", "settings",
            "function", "system", "view", "handler", "command", "audio", "frag", "vert", "compute", "prefab", "namespace", "text",
        };

        public static readonly string[] DataKinds = { "component", "singleton", "struct", "event", "input", "scene" };
        public static readonly string[] RoutineKinds = { "function", "audio", "system", "view", "frag", "vert", "compute" };

        public static readonly string[] MemberKinds = { "field", "property", "method", "operator", "tweak", "sample", "sanitize", "predict", "prepare", "text" };

        static readonly string[] Routine =
            { "kind", "id", "comment", "name", "overrides", "modifiers", "attributes", "mut", "returnType", "typeParams", "where", "params", "fails", "extern", "body" };

        static readonly Dictionary<string, string[]> declOrder = new Dictionary<string, string[]>
        {
            ["text"] = new[] { "kind", "id", "comment", "text" },
            ["namespace"] = new[] { "kind", "id", "comment", "name", "decls" },
            ["data"] = new[] { "kind", "id", "comment", "name", "modifiers", "attributes", "typeParams", "where", "members" },
            ["enum"] = new[] { "kind", "id", "comment", "name", "modifiers", "attributes", "backing", "members" },
            ["const"] = new[] { "kind", "id", "comment", "name", "modifiers", "attributes", "type", "value" },
            ["alias"] = new[] { "kind", "id", "comment", "name", "attributes", "type" },
            ["settings"] = new[] { "kind", "id", "comment", "entries" },
            ["routine"] = Routine,
            ["handler"] = new[] { "kind", "id", "comment", "name", "overrides", "modifiers", "attributes", "trigger", "params", "body" },
            ["command"] = new[] { "kind", "id", "comment", "name", "overrides", "modifiers", "attributes", "payload", "params", "body" },
            ["prefab"] = new[] { "kind", "id", "comment", "name", "overrides", "modifiers", "attributes", "root" },
        };

        static readonly Dictionary<string, string[]> memberOrder = new Dictionary<string, string[]>
        {
            ["field"] = new[] { "kind", "id", "comment", "local", "type", "name", "attributes", "default" },
            ["property"] = new[] { "kind", "id", "comment", "type", "name", "accessors" },
            ["method"] = Routine,
            ["operator"] = new[] { "kind", "id", "comment", "returnType", "op", "params", "fails", "body" },
            ["tweak"] = new[] { "kind", "id", "comment", "type", "name", "value" },
            ["sample"] = new[] { "kind", "id", "comment", "name", "params", "body" },
            ["sanitize"] = new[] { "kind", "id", "comment", "body" },
            ["predict"] = new[] { "kind", "id", "comment", "body" },
            ["prepare"] = new[] { "kind", "id", "comment", "params", "body" },
            ["text"] = new[] { "kind", "id", "comment", "text" },
        };

        public static readonly string[] ParamOrder = { "id", "comment", "mode", "query", "ownership", "local", "verified", "previous", "scene", "role", "type", "name", "default" };
        public static readonly string[] SampleParamOrder = { "id", "comment", "type", "name" };
        public static readonly string[] PayloadOrder = { "id", "comment", "attributes", "type", "name", "default" };
        public static readonly string[] TriggerOrder = { "verified", "changed", "type", "name" };
        public static readonly string[] AttributeOrder = { "id", "name", "args" };
        public static readonly string[] EnumMemberOrder = { "id", "comment", "name", "value" };
        public static readonly string[] EntryOrder = { "id", "comment", "name", "value" };
        public static readonly string[] AccessorOrder = { "id", "comment", "word", "body" };
        public static readonly string[] PrefabNodeOrder = { "id", "comment", "name", "base", "components", "children" };
        public static readonly string[] WhereOrder = { "param", "rules" };
        public static readonly string[] NoteOrder = { "id", "x", "y", "w", "h", "text" };
        public static readonly string[] BodyOrder = { "entry", "nodes", "notes" };
        public static readonly string[] ValueOrder = { "root", "nodes", "notes" };
        public static readonly string[] ArmOrder = { "pattern", "value" };
        public static readonly string[] InitOrder = { "name", "value" };
        public static readonly string[] CaseOrder = { "labels", "body" };
        public static readonly string[] LambdaParamOrder = { "mut", "type", "name" };

        public static string[] DeclOrder(string kind)
        {
            if (Array.IndexOf(DataKinds, kind) >= 0) return declOrder["data"];
            if (Array.IndexOf(RoutineKinds, kind) >= 0) return declOrder["routine"];
            if (kind == "asset") return declOrder["const"];
            return declOrder.TryGetValue(kind ?? "", out var o) ? o : declOrder["routine"];
        }

        public static string[] MemberOrder(string kind) => memberOrder.TryGetValue(kind ?? "", out var o) ? o : memberOrder["field"];

        // The words that go before a declaration, in the order tidec writes them
        public static readonly string[] AllModifiers = { "dev", "local", "async", "worker", "build", "cached", "virtual", "override", "tweak" };

        // The words that apply to a kind of declaration or member
        public static string[] Modifiers(string kind, bool member)
        {
            if (member) return kind == "method" ? new[] { "dev", "virtual", "override" } : Array.Empty<string>();
            switch (kind)
            {
                case "function": return new[] { "dev", "local", "async", "worker", "build", "cached", "virtual", "override" };
                case "system": return new[] { "dev", "virtual", "override" };
                case "view": return new[] { "dev", "local", "virtual", "override" };
                case "handler": return new[] { "dev", "local", "async", "virtual", "override" };
                case "frag":
                case "vert": return new[] { "dev", "virtual", "override" };
                case "command": return new[] { "dev", "local" };
                case "component": return new[] { "dev" };
                case "const": return new[] { "dev", "local", "tweak" };
                case "singleton":
                case "struct":
                case "event":
                case "input":
                case "scene":
                case "enum":
                case "asset":
                case "prefab":
                case "audio":
                case "compute": return new[] { "dev", "local" };
                default: return Array.Empty<string>();
            }
        }

        public static bool HasBody(string kind, bool member)
        {
            if (member) return kind == "method" || kind == "operator" || kind == "sample" || kind == "sanitize" || kind == "predict" || kind == "prepare";
            return Array.IndexOf(RoutineKinds, kind) >= 0 || kind == "handler" || kind == "command";
        }

        public static bool HasParams(string kind, bool member)
        {
            if (member) return kind == "method" || kind == "operator" || kind == "prepare";
            return Array.IndexOf(RoutineKinds, kind) >= 0 || kind == "handler" || kind == "command";
        }

        public static readonly string[] ParamModes = { "read", "mut", "in", "out", "with", "without", "query" };
        public static readonly string[] AssignOps = { "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "??=", "++", "--" };
        public static readonly string[] UnaryOps = { "!", "-", "~" };
        public static readonly string[] StepOps = { "++", "--" };
        public static readonly string[] OperatorOps = { "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&", "|", "^", "<<", ">>", "!", "~" };

        public static readonly (string op, string title)[] BinaryOps =
        {
            ("+", "Add"), ("-", "Subtract"), ("*", "Multiply"), ("/", "Divide"), ("%", "Remainder"),
            ("==", "Equal"), ("!=", "Not equal"), ("<", "Less than"), ("<=", "Less or equal"), (">", "Greater than"), (">=", "Greater or equal"),
            ("&&", "And"), ("||", "Or"), ("&", "Bitwise and"), ("|", "Bitwise or"), ("^", "Exclusive or"), ("<<", "Shift left"), (">>", "Shift right"),
        };

        public static readonly string[] BuiltinTypes =
        {
            "int", "uint", "long", "ulong", "short", "ushort", "byte", "sbyte", "float", "double", "bool", "string", "char", "void",
            "float2", "float3", "float4", "int2", "int3", "int4", "uint2", "uint3", "uint4", "bool2", "bool3", "bool4",
            "float2x2", "float3x3", "float4x4", "quaternion", "Color", "Entity", "PlayerID", "Devices", "World",
            "List<T>", "Map<K, V>", "Set<T>", "Grid2<T>", "Grid3<T>", "Texture", "Mesh", "Sound",
        };

        // Node kinds, as the canvas and its search show them
        public enum Category { Flow, Calls, Values, Math, Locals, Text }

        public static Color ColorOf(Category c)
        {
            switch (c)
            {
                case Category.Flow: return new Color(0.36f, 0.38f, 0.44f);
                case Category.Calls: return new Color(0.20f, 0.42f, 0.70f);
                case Category.Values: return new Color(0.24f, 0.55f, 0.32f);
                case Category.Math: return new Color(0.62f, 0.45f, 0.18f);
                case Category.Locals: return new Color(0.50f, 0.32f, 0.62f);
                default: return new Color(0.48f, 0.36f, 0.30f);
            }
        }

        public sealed class NodeSpec
        {
            public string K, Title;
            public Category Category;
            public bool Exec;
            public string[] Keys = Array.Empty<string>(); // Its own keys, in tidec's order
        }

        static NodeSpec S(string k, string title, Category c, bool exec, params string[] keys)
            => new NodeSpec { K = k, Title = title, Category = c, Exec = exec, Keys = keys };

        public static readonly NodeSpec[] Nodes =
        {
            // Statements
            S("var", "Local", Category.Locals, true, "mut", "type", "name", "value", "shared"),
            S("assign", "Assign", Category.Flow, true, "op", "target", "value"),
            S("eval", "Do", Category.Flow, true, "value", "stacked", "block", "withBlock"),
            S("if", "If", Category.Flow, true, "cond", "then", "else"),
            S("while", "While", Category.Flow, true, "cond", "body"),
            S("for", "For", Category.Flow, true, "init", "cond", "step", "body"),
            S("foreach", "For each", Category.Flow, true, "indexed", "type", "name", "collection", "body"),
            S("parallel", "Parallel", Category.Flow, true, "indexed", "type", "name", "grid", "by", "offset", "body"),
            S("switch", "Switch", Category.Flow, true, "value", "cases"),
            S("return", "Return", Category.Flow, true, "value"),
            S("fail", "Fail", Category.Flow, true, "value"),
            S("break", "Break", Category.Flow, true),
            S("continue", "Continue", Category.Flow, true),
            S("block", "Block", Category.Flow, true, "body"),
            S("discard", "Discard", Category.Flow, true, "value"),
            S("code", "Code", Category.Text, true, "text", "inputs"),
            // Values
            S("int", "Int", Category.Values, false, "text"),
            S("float", "Float", Category.Values, false, "text"),
            S("bool", "Bool", Category.Values, false, "value"),
            S("string", "String", Category.Values, false, "text"),
            S("char", "Char", Category.Values, false, "text"),
            S("this", "this", Category.Values, false),
            S("default", "default", Category.Values, false),
            S("null", "null", Category.Values, false),
            S("name", "Name", Category.Values, false, "name", "local"),
            S("local", "Local", Category.Locals, false, "ref"),
            S("member", "Member", Category.Values, false, "shorthand", "object", "member"),
            S("list", "List", Category.Values, false, "items"),
            S("literal", "Value of a type", Category.Values, false, "type", "inits"),
            S("interp", "Text with values", Category.Values, false, "parts", "formats", "values"),
            S("lambda", "Lambda", Category.Values, false, "params", "body"),
            // Calls
            S("call", "Call", Category.Calls, false, "name", "typeArgs", "local", "args", "argNames"),
            S("method", "Method", Category.Calls, false, "object", "name", "typeArgs", "args", "argNames"),
            S("out", "out", Category.Calls, false, "target"),
            S("outVar", "out var", Category.Locals, false, "mut", "type", "name"),
            // Math and logic
            S("binary", "Binary", Category.Math, false, "op", "lhs", "rhs"),
            S("unary", "Unary", Category.Math, false, "op", "value"),
            S("step", "Step", Category.Math, false, "op", "postfix", "target"),
            S("conditional", "Conditional", Category.Math, false, "cond", "then", "else"),
            S("coalesce", "Coalesce ??", Category.Math, false, "lhs", "rhs"),
            S("switchExpr", "Switch value", Category.Math, false, "value", "arms"),
            S("is", "is", Category.Math, false, "value", "type", "binding"),
            S("has", "has", Category.Math, false, "subject", "type", "field"),
            S("try", "try", Category.Math, false, "value"),
            S("await", "await", Category.Math, false, "value"),
            S("defaulted", "Value or default !", Category.Math, false, "value"),
            S("index", "Index", Category.Math, false, "object", "index"),
            // Text
            S("text", "Text", Category.Text, false, "text", "inputs"),
        };

        static Dictionary<string, NodeSpec> byKind;

        public static NodeSpec Spec(string k)
        {
            if (byKind == null) byKind = Nodes.ToDictionary(n => n.K);
            return k != null && byKind.TryGetValue(k, out var s) ? s : null;
        }

        public static bool IsExec(string k) => Spec(k)?.Exec ?? false;

        static readonly Dictionary<string, string[]> nodeOrders = new Dictionary<string, string[]>();

        // A node's keys in order: id, kind and place, its own, then its comment and the statement after it
        public static string[] NodeOrder(string k)
        {
            if (k == null) k = "";
            if (nodeOrders.TryGetValue(k, out var o)) return o;
            var own = Spec(k)?.Keys ?? Array.Empty<string>();
            o = new[] { "id", "k", "x", "y" }.Concat(own).Concat(new[] { "comment", "next" }).ToArray();
            nodeOrders[k] = o;
            return o;
        }

        // The node kinds that declare a local
        public static bool DeclaresLocal(JValue node)
        {
            var k = node.Str("k");
            return k == "var" || k == "foreach" || k == "parallel" || k == "outVar" || (k == "is" && !string.IsNullOrEmpty(node.Str("binding")));
        }

        public static string LocalName(JValue node) => node.Str("k") == "is" ? node.Str("binding") : node.Str("name");
    }

    // Where a node holds a wire: a path of keys and indexes from the node, ["args", 2], ["cases", 0, "labels", 1]
    sealed class WirePath : IEquatable<WirePath>
    {
        public readonly object[] Parts;

        public WirePath(params object[] parts)
        {
            Parts = parts;
        }

        public string Key => Parts[0] as string;

        public int Get(JValue node)
        {
            var v = Resolve(node);
            return JValue.AsId(v);
        }

        JValue Resolve(JValue v)
        {
            foreach (var p in Parts)
            {
                if (v == null) return null;
                if (p is string key) v = v.Get(key);
                else
                {
                    var i = (int)p;
                    v = v.IsArray && i < v.Items.Count ? v.Items[i] : null;
                }
            }
            return v;
        }

        // Sets the wire, 0 for none: a key of the node's or of an arm's, case's or field's goes when it's 0, as tidec
        // writes them; an item of a list and a text's named input stays, as null
        public void Set(JValue node, int id)
        {
            var parent = node;
            for (var i = 0; i < Parts.Length - 1; i++)
            {
                var p = Parts[i];
                if (p is string key) parent = parent?.Get(key);
                else
                {
                    var index = (int)p;
                    parent = parent != null && parent.IsArray && index < parent.Items.Count ? parent.Items[index] : null;
                }
            }
            if (parent == null) return;
            var last = Parts[Parts.Length - 1];
            if (last is int at)
            {
                if (parent.IsArray && at < parent.Items.Count) parent.Items[at] = id == 0 ? JValue.Null() : JValue.Of(id);
                return;
            }
            var name = (string)last;
            if (Parts.Length == 2 && Parts[0] as string == "inputs")
            {
                parent.Set(name, id == 0 ? JValue.Null() : JValue.Of(id));
                return;
            }
            if (Parts.Length > 1)
            {
                // An arm's pattern and a field's value are written as null; a case's body goes
                if (name == "body") parent.SetWire(name, id, GraphSchema.CaseOrder);
                else parent.Set(name, id == 0 ? JValue.Null() : JValue.Of(id), name == "pattern" ? GraphSchema.ArmOrder : GraphSchema.InitOrder);
                return;
            }
            parent.SetWire(name, id, GraphSchema.NodeOrder(node.Str("k")));
        }

        public bool Equals(WirePath other) => other != null && Parts.SequenceEqual(other.Parts);
        public override bool Equals(object obj) => Equals(obj as WirePath);
        public override int GetHashCode() => string.Join("/", Parts).GetHashCode();
        public override string ToString() => string.Join(".", Parts);
    }

    // A port of a node on the canvas
    sealed class GraphPort
    {
        public int Node;          // The node whose JSON holds it
        public bool Input;        // An input (data in, or the statement's exec in) or an output
        public bool Exec;
        public WirePath Path;     // Where its wire is kept: data inputs and exec outputs; null for exec in and data out
        public string Label;
        public string Group;      // A list it's one of ("args", "items" ...), which grows and shrinks
        public int Index = -1;    // Its place in that list
    }

    static class GraphPorts
    {
        static readonly string[] noKeys = Array.Empty<string>();

        // The single data inputs of a kind, in the order its text reads them
        public static string[] DataKeys(string k)
        {
            switch (k)
            {
                case "member": return new[] { "object" };
                case "method": return new[] { "object" };
                case "out": return new[] { "target" };
                case "binary":
                case "coalesce": return new[] { "lhs", "rhs" };
                case "unary":
                case "try":
                case "defaulted":
                case "await":
                case "is":
                case "switchExpr":
                case "var":
                case "discard":
                case "eval":
                case "return":
                case "fail":
                case "switch": return new[] { "value" };
                case "step": return new[] { "target" };
                case "conditional": return new[] { "cond", "then", "else" };
                case "index": return new[] { "object" };
                case "lambda": return new[] { "body" };
                case "assign": return new[] { "target", "value" };
                case "if":
                case "while":
                case "for": return new[] { "cond" };
                case "foreach": return new[] { "collection" };
                case "parallel": return new[] { "grid", "by", "offset" };
                default: return noKeys;
            }
        }

        // The exec outputs of a kind other than next
        public static string[] ExecKeys(string k)
        {
            switch (k)
            {
                case "if": return new[] { "then", "else" };
                case "while":
                case "foreach":
                case "parallel":
                case "block": return new[] { "body" };
                case "for": return new[] { "init", "step", "body" };
                case "eval": return new[] { "block" };
                default: return noKeys;
            }
        }

        // Every port of a node, in the order the canvas shows them
        public static List<GraphPort> Of(JValue n)
        {
            var id = n.Wire("id");
            var k = n.Str("k");
            var exec = GraphSchema.IsExec(k);
            var ports = new List<GraphPort>();
            if (exec) ports.Add(new GraphPort { Node = id, Input = true, Exec = true, Label = "" });
            if (!exec || k == "eval") ports.Add(new GraphPort { Node = id, Input = false, Exec = false, Label = "" });
            foreach (var key in DataKeys(k))
            {
                if (k == "member" && n.Flag("shorthand")) continue;
                ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(key), Label = key });
            }
            switch (k)
            {
                case "call":
                case "method":
                    List(ports, n, id, "args", i =>
                    {
                        var names = n.Arr("argNames");
                        var name = names != null && i < names.Items.Count ? names.Items[i].String : null;
                        return string.IsNullOrEmpty(name) ? $"arg {i + 1}" : name + ":";
                    });
                    break;
                case "list": List(ports, n, id, "items", i => $"item {i + 1}"); break;
                case "interp": List(ports, n, id, "values", i => $"value {i + 1}"); break;
                case "index": List(ports, n, id, "index", i => i == 0 ? "index" : $"index {i + 1}"); break;
                case "literal":
                {
                    var inits = n.Arr("inits");
                    for (var i = 0; inits != null && i < inits.Items.Count; i++)
                        ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath("inits", i, "value"), Label = inits.Items[i].Str("name") ?? "", Group = "inits", Index = i });
                    break;
                }
                case "switchExpr":
                {
                    var arms = n.Arr("arms");
                    for (var i = 0; arms != null && i < arms.Items.Count; i++)
                    {
                        ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath("arms", i, "pattern"), Label = $"arm {i + 1} pattern", Group = "arms", Index = i });
                        ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath("arms", i, "value"), Label = $"arm {i + 1} value", Group = "arms", Index = i });
                    }
                    break;
                }
                case "text":
                case "code":
                {
                    var inputs = n.Get("inputs");
                    for (var i = 0; inputs != null && inputs.IsObject && i < inputs.Keys.Count; i++)
                        ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath("inputs", inputs.Keys[i]), Label = inputs.Keys[i], Group = "inputs", Index = i });
                    break;
                }
                case "switch":
                {
                    var cases = n.Arr("cases");
                    for (var i = 0; cases != null && i < cases.Items.Count; i++)
                    {
                        var labels = cases.Items[i].Arr("labels");
                        for (var l = 0; labels != null && l < labels.Items.Count; l++)
                            ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath("cases", i, "labels", l), Label = $"case {i + 1}", Group = "labels", Index = i });
                    }
                    break;
                }
            }
            if (exec)
            {
                foreach (var key in ExecKeys(k)) ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath(key), Label = key });
                if (k == "switch")
                {
                    var cases = n.Arr("cases");
                    for (var i = 0; cases != null && i < cases.Items.Count; i++)
                        ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath("cases", i, "body"), Label = $"case {i + 1}", Group = "cases", Index = i });
                }
                if (k != "return" && k != "fail" && k != "break" && k != "continue")
                    ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath("next"), Label = "next" });
                else if (n.Wire("next") != 0)
                    ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath("next"), Label = "next" });
            }
            return ports;
        }

        static void List(List<GraphPort> ports, JValue n, int id, string key, Func<int, string> label)
        {
            var a = n.Arr(key);
            for (var i = 0; a != null && i < a.Items.Count; i++)
                ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(key, i), Label = label(i), Group = key, Index = i });
        }

        // The ids a node's data inputs are wired from (tidec's graph_data_wires)
        public static IEnumerable<int> DataWires(JValue n)
        {
            foreach (var p in Of(n))
            {
                if (!p.Input || p.Exec || p.Path == null) continue;
                var w = p.Path.Get(n);
                if (w != 0) yield return w;
            }
        }

        // The statements a node goes on to: next and its code's
        public static IEnumerable<int> ExecWires(JValue n)
        {
            foreach (var p in Of(n))
            {
                if (p.Input || !p.Exec) continue;
                var w = p.Path.Get(n);
                if (w != 0) yield return w;
            }
        }
    }
}
