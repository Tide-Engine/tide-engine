using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEngine;

namespace Tide.Editor
{
    // What a .tidegraph holds (notes/visual-scripting.md): tidec's schema (compiler/src/graph_schema.json), its kinds
    // of declaration, member and node, the keys of each in the order tidec writes them, and a node's ports. Every
    // editor of graphs reads the same schema, so none has tables of its own to drift from tidec's.
    //
    // Where it comes from: tidels (tide/graphSchema) when it's running; else `tide graph --schema` (or a test's
    // tidec's `--graph-schema`), kept in the project's Library for the next domain reload with the program it came
    // from and that program's size and time, so it's asked again when the program changes. With none of them, the
    // editor says why instead of editing graphs with a schema it hasn't.
    static class GraphSchema
    {
        public sealed class NodeList
        {
            public string Key, Label, Names; // Its key, what an input of it is called, and the key of its names (a call's argNames)
        }

        public sealed class NodeItems
        {
            public string Key, Label, Exec, Element; // Its key, what an item is called, its exec output, the element order of an item
            public string[] Wires = Array.Empty<string>(); // Its data inputs: a key, or "key[]" for an array of them
        }

        public sealed class NodeSpec
        {
            public string K, Title, Category;
            public bool Exec, Value, Ends;
            public string Map, Declares;
            public string[] Keys = Array.Empty<string>();    // Its own keys, in tidec's order
            public string[] Inputs = Array.Empty<string>();  // Its single data inputs
            public string[] Outputs = Array.Empty<string>(); // Its exec outputs besides next
            public NodeList[] Lists = Array.Empty<NodeList>();
            public NodeItems[] Items = Array.Empty<NodeItems>();
        }

        sealed class KindSpec
        {
            public string Kind, Title, Group;
            public string[] Keys, Modifiers;
            public bool Members, Params, Body;
        }

        sealed class Schema
        {
            public JValue Json;
            public string[] Root;
            public KindSpec[] Decls, Members;
            public Dictionary<string, string[]> Elements = new Dictionary<string, string[]>();
            public string[] AllModifiers, ParamModes, AssignOps, UnaryOps, StepOps, OperatorOps, Types;
            public (string op, string title)[] BinaryOps;
            public Dictionary<string, (string title, Color color)> Categories = new Dictionary<string, (string, Color)>();
            public NodeSpec[] Nodes;
            public Dictionary<string, NodeSpec> ByKind;
            public readonly Dictionary<string, string[]> NodeOrders = new Dictionary<string, string[]>();
        }

        static Schema current;
        static string cacheProgram, cacheStamp; // What the Library's copy is of

        // Why there's no schema, when there isn't: the editor shows it instead of a graph
        public static string Failure { get; private set; }
        // Where the schema in use came from
        public static string Source { get; private set; }
        // The schema changed (tidels gave one other than the one in use): windows build again
        public static event Action Changed;

        public static bool Available => Current != null;

        static Schema Current
        {
            get
            {
                if (current == null && Failure == null) Load();
                return current;
            }
        }

        static Schema S => Current ?? throw new InvalidOperationException("No graph schema: " + Failure);

        // The schema's JSON, as tidec has it
        public static JValue Json => S.Json;

        internal static string CachePath => Path.Combine(TideSettings.ProjectFolder, "Library", "Tide", "graph-schema.json");

        // Reads it again from the program (or the Library's copy): a test that sets a tidec, or a window's "Try again"
        public static void Reload()
        {
            current = null;
            Failure = null;
            Load();
            Changed?.Invoke();
        }

        static string Stamp(string program)
        {
            try
            {
                var info = new FileInfo(program);
                return $"{info.Length}-{info.LastWriteTimeUtc.Ticks}";
            }
            catch (Exception)
            {
                return null;
            }
        }

        static void Load()
        {
            var have = TideGraphCommand.SchemaCommand(out var program, out var arguments, out var why);
            var stamp = have ? Stamp(program) : null;
            JValue cached = null;
            try
            {
                if (File.Exists(CachePath)) cached = GraphJson.Parse(File.ReadAllText(CachePath, Encoding.UTF8));
            }
            catch (Exception)
            {
                cached = null; // A copy that doesn't read is asked for again
            }
            var cachedSchema = cached?.Get("schema");
            if (have && cachedSchema != null && cached.Str("program") == program && cached.Str("stamp") == stamp && Use(cachedSchema, "the Library's copy of " + Path.GetFileName(program) + "'s"))
            {
                cacheProgram = program;
                cacheStamp = stamp;
                return;
            }
            if (have)
            {
                var code = TideGraphCommand.Run(program, arguments, null, out var output, out var errors, 30000);
                if (code == 0)
                {
                    try
                    {
                        if (Use(GraphJson.Parse(output), $"{Path.GetFileName(program)} {arguments}"))
                        {
                            cacheProgram = program;
                            cacheStamp = stamp;
                            Keep();
                            return;
                        }
                        why = $"{Path.GetFileName(program)} {arguments} gave no graph schema";
                    }
                    catch (GraphJson.ParseError e)
                    {
                        why = $"{Path.GetFileName(program)} {arguments} gave a schema that doesn't read: {e.Message}";
                    }
                }
                else
                {
                    var said = (errors + output).Trim();
                    why = $"{Path.GetFileName(program)} {arguments} failed" + (said.Length > 0 ? ": " + said.Split('\n')[0] : $" (exit code {code})");
                }
            }
            // The program isn't there or failed: the copy of the last one's, if there is one
            if (cachedSchema != null && Use(cachedSchema, "the Library's copy (" + why + ")")) return;
            Failure = "Graphs need Tide's graph schema, which comes from tide (`tide graph --schema`) or tidels, and there's none: " + why;
        }

        // Writes the schema in use to the Library, for the next domain reload
        static void Keep()
        {
            if (current == null || cacheProgram == null) return;
            try
            {
                var o = JValue.NewObject();
                o.Set("program", JValue.Of(cacheProgram));
                o.Set("stamp", JValue.Of(cacheStamp));
                o.Set("schema", current.Json);
                Directory.CreateDirectory(Path.GetDirectoryName(CachePath) ?? ".");
                File.WriteAllText(CachePath, GraphJson.WriteFile(o), new UTF8Encoding(false));
            }
            catch (IOException)
            {
                // Read from the program again next time
            }
        }

        // The schema tidels has: used, and kept, when it isn't the one in use
        public static void Adopt(JValue json, string from)
        {
            if (json == null || !json.IsObject) return;
            if (current != null && TideLanguageClient.Compact(current.Json) == TideLanguageClient.Compact(json)) return;
            if (!Use(json, from)) return;
            Failure = null;
            Keep();
            Changed?.Invoke();
        }

        static bool Use(JValue json, string from)
        {
            var s = Read(json);
            if (s == null) return false;
            current = s;
            Source = from;
            return true;
        }

        static string[] Strings(JValue a) => a != null && a.IsArray ? a.Items.Where(i => i.Kind == JKind.String).Select(i => i.String).ToArray() : Array.Empty<string>();

        static Schema Read(JValue j)
        {
            if (j == null || !j.IsObject || j.Arr("nodes") == null || j.Arr("declarations") == null || j.Get("elements") == null) return null;
            var s = new Schema { Json = j, Root = Strings(j.Arr("root")) };
            KindSpec Kind(JValue d) => new KindSpec
            {
                Kind = d.Str("kind"), Title = d.Str("title"), Group = d.Str("group"), Keys = Strings(d.Arr("keys")), Modifiers = Strings(d.Arr("modifiers")),
                Members = d.Flag("members"), Params = d.Flag("params"), Body = d.Flag("body"),
            };
            s.Decls = j.Arr("declarations").Items.Select(Kind).ToArray();
            s.Members = (j.Arr("members")?.Items ?? new List<JValue>()).Select(Kind).ToArray();
            var elements = j.Get("elements");
            for (var i = 0; elements.IsObject && i < elements.Keys.Count; i++) s.Elements[elements.Keys[i]] = Strings(elements.Values[i]);
            s.AllModifiers = Strings(j.Arr("allModifiers"));
            s.ParamModes = Strings(j.Arr("paramModes"));
            var ops = j.Get("operators");
            s.BinaryOps = (ops?.Arr("binary")?.Items ?? new List<JValue>()).Select(o => (o.Str("op"), o.Str("title") ?? o.Str("op"))).ToArray();
            s.UnaryOps = (ops?.Arr("unary")?.Items ?? new List<JValue>()).Select(o => o.Str("op")).ToArray();
            s.AssignOps = Strings(ops?.Arr("assign"));
            s.StepOps = Strings(ops?.Arr("step"));
            s.OperatorOps = Strings(ops?.Arr("overloadable"));
            s.Types = Strings(j.Arr("types"));
            foreach (var c in j.Arr("categories")?.Items ?? new List<JValue>())
            {
                if (!ColorUtility.TryParseHtmlString(c.Str("color") ?? "", out var color)) color = Color.gray;
                s.Categories[c.Str("name") ?? ""] = (c.Str("title"), color);
            }
            s.Nodes = j.Arr("nodes").Items.Select(n => new NodeSpec
            {
                K = n.Str("k"), Title = n.Str("title"), Category = n.Str("category"), Exec = n.Flag("exec"), Value = n.Flag("value"), Ends = n.Flag("ends"),
                Map = n.Str("map"), Declares = n.Str("declares"), Keys = Strings(n.Arr("keys")), Inputs = Strings(n.Arr("inputs")), Outputs = Strings(n.Arr("outputs")),
                Lists = (n.Arr("lists")?.Items ?? new List<JValue>()).Select(l => new NodeList { Key = l.Str("key"), Label = l.Str("label"), Names = l.Str("names") }).ToArray(),
                Items = (n.Arr("items")?.Items ?? new List<JValue>()).Select(it => new NodeItems
                {
                    Key = it.Str("key"), Label = it.Str("label"), Exec = it.Str("exec"), Wires = Strings(it.Arr("wires")),
                    Element = ElementOf(s, it),
                }).ToArray(),
            }).Where(n => n.K != null).ToArray();
            s.ByKind = new Dictionary<string, NodeSpec>();
            foreach (var n in s.Nodes) s.ByKind[n.K] = n;
            return s;
        }

        // The element order of an item of a node (a case, an arm, an init): the one it names, else the one its key
        // is the plural of
        static string ElementOf(Schema s, JValue item)
        {
            var named = item.Str("element");
            if (named != null) return named;
            var key = item.Str("key") ?? "";
            return key.EndsWith("s") && s.Elements.ContainsKey(key.Substring(0, key.Length - 1)) ? key.Substring(0, key.Length - 1) : null;
        }

        public static string[] Element(string name) => S.Elements.TryGetValue(name, out var o) ? o : Array.Empty<string>();

        public static string[] RootOrder => S.Root;

        // Declarations, as the add menu lists them
        public static string[] DeclKinds => S.Decls.Select(d => d.Kind).ToArray();
        // Those with members
        public static string[] DataKinds => S.Decls.Where(d => d.Members).Select(d => d.Kind).ToArray();
        public static string[] MemberKinds => S.Members.Select(d => d.Kind).ToArray();

        public static string DeclTitle(string kind) => S.Decls.FirstOrDefault(d => d.Kind == kind)?.Title ?? kind;
        public static string DeclGroup(string kind) => S.Decls.FirstOrDefault(d => d.Kind == kind)?.Group;
        public static string MemberTitle(string kind) => S.Members.FirstOrDefault(d => d.Kind == kind)?.Title ?? kind;

        static KindSpec KindOf(string kind, bool member) => (member ? S.Members : S.Decls).FirstOrDefault(d => d.Kind == kind);

        // A kind's keys in order; one the schema hasn't (a newer file's) keeps its keys where they are
        public static string[] DeclOrder(string kind) => KindOf(kind, false)?.Keys ?? Array.Empty<string>();
        public static string[] MemberOrder(string kind) => KindOf(kind, true)?.Keys ?? Array.Empty<string>();

        public static string[] ParamOrder => Element("param");
        public static string[] SampleParamOrder => Element("sampleParam");
        public static string[] PayloadOrder => Element("payload");
        public static string[] TriggerOrder => Element("trigger");
        public static string[] AttributeOrder => Element("attribute");
        public static string[] EnumMemberOrder => Element("enumMember");
        public static string[] EntryOrder => Element("entry");
        public static string[] AccessorOrder => Element("accessor");
        public static string[] PrefabNodeOrder => Element("prefabNode");
        public static string[] WhereOrder => Element("where");
        public static string[] NoteOrder => Element("note");
        public static string[] BodyOrder => Element("body");
        public static string[] ValueOrder => Element("value");
        public static string[] ArmOrder => Element("arm");
        public static string[] InitOrder => Element("init");
        public static string[] CaseOrder => Element("case");
        public static string[] LambdaParamOrder => Element("lambdaParam");

        // The words that go before a declaration, in the order tidec writes them
        public static string[] AllModifiers => S.AllModifiers;

        // The words that apply to a kind of declaration or member
        public static string[] Modifiers(string kind, bool member) => KindOf(kind, member)?.Modifiers ?? Array.Empty<string>();
        public static bool HasBody(string kind, bool member) => KindOf(kind, member)?.Body ?? false;
        public static bool HasParams(string kind, bool member) => KindOf(kind, member)?.Params ?? false;

        public static string[] ParamModes => S.ParamModes;
        public static string[] AssignOps => S.AssignOps;
        public static string[] UnaryOps => S.UnaryOps;
        public static string[] StepOps => S.StepOps;
        public static string[] OperatorOps => S.OperatorOps;
        public static (string op, string title)[] BinaryOps => S.BinaryOps;
        public static string[] BuiltinTypes => S.Types;

        // A node category's colour, as the canvas draws its nodes' heads
        public static Color ColorOf(string category) => category != null && S.Categories.TryGetValue(category, out var c) ? c.color : new Color(0.4f, 0.4f, 0.4f);
        public static string CategoryTitle(string category) => category != null && S.Categories.TryGetValue(category, out var c) ? c.title : category;

        public static NodeSpec[] Nodes => S.Nodes;

        public static NodeSpec Spec(string k) => k != null && S.ByKind.TryGetValue(k, out var s) ? s : null;

        public static bool IsExec(string k) => Spec(k)?.Exec ?? false;

        // A node's keys in order: id, kind and place, its own, then its comment and the statement after it
        public static string[] NodeOrder(string k)
        {
            var s = S;
            if (k == null) k = "";
            if (s.NodeOrders.TryGetValue(k, out var o)) return o;
            var own = Spec(k)?.Keys ?? Array.Empty<string>();
            o = Element("node").Concat(own).Concat(Element("nodeEnd")).ToArray();
            s.NodeOrders[k] = o;
            return o;
        }

        // The node kinds that declare a local, with a name for it
        public static bool DeclaresLocal(JValue node)
        {
            var key = Spec(node.Str("k"))?.Declares;
            return key != null && !string.IsNullOrEmpty(node.Str(key));
        }

        public static string LocalName(JValue node)
        {
            var key = Spec(node.Str("k"))?.Declares;
            return key != null ? node.Str(key) : node.Str("name");
        }
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

        // Sets the wire, 0 for none, as tidec writes them: a key of the node's, and an item's exec output, goes when
        // it's 0; an item's data input (an arm's pattern, a field's value), an item of a list and a named input
        // stay, as null
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
            var wire = id == 0 ? JValue.Null() : JValue.Of(id);
            if (last is int at)
            {
                if (parent.IsArray && at < parent.Items.Count) parent.Items[at] = wire;
                return;
            }
            var name = (string)last;
            var spec = GraphSchema.Spec(node.Str("k"));
            if (Parts.Length == 2 && spec?.Map == Key)
            {
                parent.Set(name, wire);
                return;
            }
            if (Parts.Length > 1)
            {
                var items = spec?.Items.FirstOrDefault(it => it.Key == Key);
                var order = items?.Element != null ? GraphSchema.Element(items.Element) : null;
                if (items != null && items.Exec == name) parent.SetWire(name, id, order);
                else parent.Set(name, wire, order);
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

    // A node's ports, from the schema's: its single inputs, its lists and items of inputs, its named inputs, its exec
    // outputs and its items', and next
    static class GraphPorts
    {
        // The exec outputs of a kind other than next and its items'
        public static string[] ExecKeys(string k) => GraphSchema.Spec(k)?.Outputs ?? Array.Empty<string>();

        // Whether a path is a statement's flow out: next, the code it holds, an item's code (a case's body)
        public static bool IsExecPath(JValue n, WirePath path)
        {
            var spec = GraphSchema.Spec(n.Str("k"));
            if (spec == null || !spec.Exec) return false;
            if (path.Parts.Length == 1) return path.Key == "next" || Array.IndexOf(spec.Outputs, path.Key) >= 0;
            return path.Parts.Length == 3 && spec.Items.Any(it => it.Key == path.Key && it.Exec != null && it.Exec == path.Parts[2] as string);
        }

        // Every port of a node, in the order the canvas shows them
        public static List<GraphPort> Of(JValue n)
        {
            var id = n.Wire("id");
            var spec = GraphSchema.Spec(n.Str("k"));
            var ports = new List<GraphPort>();
            var exec = spec != null && spec.Exec;
            if (exec) ports.Add(new GraphPort { Node = id, Input = true, Exec = true, Label = "" });
            // A kind this editor doesn't know is drawn as a value
            if (spec == null || spec.Value) ports.Add(new GraphPort { Node = id, Input = false, Exec = false, Label = "" });
            if (spec == null) return ports;
            foreach (var key in spec.Inputs)
            {
                // A shorthand member (`.Name`) has no object
                if (key == "object" && n.Flag("shorthand")) continue;
                ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(key), Label = key });
            }
            foreach (var list in spec.Lists)
            {
                var a = n.Arr(list.Key);
                var names = list.Names != null ? n.Arr(list.Names) : null;
                for (var i = 0; a != null && i < a.Items.Count; i++)
                {
                    var name = names != null && i < names.Items.Count ? names.Items[i].String : null;
                    var label = string.IsNullOrEmpty(name) ? $"{list.Label} {i + 1}" : name + ":";
                    ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(list.Key, i), Label = label, Group = list.Key, Index = i });
                }
            }
            foreach (var items in spec.Items)
            {
                var a = n.Arr(items.Key);
                for (var i = 0; a != null && i < a.Items.Count; i++)
                {
                    var item = a.Items[i];
                    // Named by the item's own key where the label is one (a field's name), else counted
                    var named = item.IsObject && item.Get(items.Label)?.Kind == JKind.String ? item.Str(items.Label) : null;
                    var label = named ?? $"{items.Label} {i + 1}";
                    foreach (var wire in items.Wires)
                    {
                        if (wire.EndsWith("[]"))
                        {
                            var key = wire.Substring(0, wire.Length - 2);
                            var wires = item.Arr(key);
                            for (var l = 0; wires != null && l < wires.Items.Count; l++)
                                ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(items.Key, i, key, l), Label = label, Group = key, Index = i });
                        }
                        else
                        {
                            var text = named != null || items.Wires.Length == 1 ? label : $"{label} {wire}";
                            ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(items.Key, i, wire), Label = text, Group = items.Key, Index = i });
                        }
                    }
                }
            }
            if (spec.Map != null)
            {
                var inputs = n.Get(spec.Map);
                for (var i = 0; inputs != null && inputs.IsObject && i < inputs.Keys.Count; i++)
                    ports.Add(new GraphPort { Node = id, Input = true, Path = new WirePath(spec.Map, inputs.Keys[i]), Label = inputs.Keys[i], Group = spec.Map, Index = i });
            }
            if (exec)
            {
                foreach (var key in spec.Outputs) ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath(key), Label = key });
                foreach (var items in spec.Items)
                {
                    if (items.Exec == null) continue;
                    var a = n.Arr(items.Key);
                    for (var i = 0; a != null && i < a.Items.Count; i++)
                        ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath(items.Key, i, items.Exec), Label = $"{items.Label} {i + 1}", Group = items.Key, Index = i });
                }
                // A statement that ends its flow (return, break ...) has a next only when one is wired
                if (!spec.Ends || n.Wire("next") != 0)
                    ports.Add(new GraphPort { Node = id, Input = false, Exec = true, Path = new WirePath("next"), Label = "next" });
            }
            return ports;
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
