using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEngine;

namespace Tide.Editor
{
    // What an element with an id is to the file, found by walking it
    enum GraphRole { Decl, Member, EnumMember, Entry, Param, SampleParam, Payload, Attribute, Accessor, PrefabNode, Node, Note }

    sealed class GraphElement
    {
        public int Id;
        public GraphRole Role;
        public JValue Value;
        public JValue List;     // The array it's in (null for a prefab's root)
        public JValue Owner;    // The declaration or member it belongs to (a node's and note's: the one whose graph it's in)
        public JValue Graph;    // A node's or note's graph
        public GraphSlot Slot;  // A node's or note's graph, as a slot
    }

    // A graph of the file: a body (a routine's, an accessor's) or a value (a constant's, a field's default, a prefab
    // entity's component...) by the element holding it and its key; Index for a prefab's components
    struct GraphSlot : IEquatable<GraphSlot>
    {
        public int Owner;
        public string Key;
        public int Index;

        public GraphSlot(int owner, string key, int index = -1)
        {
            Owner = owner;
            Key = key;
            Index = index;
        }

        public bool IsBody => Key == "body";
        public bool Valid => Owner != 0 && Key != null;
        public bool Equals(GraphSlot o) => Owner == o.Owner && Key == o.Key && Index == o.Index;
        public override bool Equals(object obj) => obj is GraphSlot s && Equals(s);
        public override int GetHashCode() => Owner * 31 + (Key?.GetHashCode() ?? 0) + Index;
        public override string ToString() => Index >= 0 ? $"{Owner}.{Key}[{Index}]" : $"{Owner}.{Key}";
    }

    // A .tidegraph's contents, as JSON, with what an editor does to it. Every element with an id is found by it; ids
    // are unique in the file, and nextId is kept above them all.
    sealed class GraphDocument
    {
        public JValue Root;
        readonly Dictionary<int, GraphElement> index = new Dictionary<int, GraphElement>();
        int maxId;

        public static GraphDocument Parse(string text)
        {
            var doc = new GraphDocument { Root = GraphJson.Parse(text) };
            if (!doc.Root.IsObject) throw new GraphJson.ParseError("a graph's file is an object", 1, 1);
            doc.Reindex();
            return doc;
        }

        public static GraphDocument Load(string path) => Parse(File.ReadAllText(path, Encoding.UTF8));

        public static GraphDocument Empty()
        {
            var root = JValue.NewObject();
            root.Set("tidegraph", JValue.Of(1));
            root.Set("nextId", JValue.Of(1));
            root.Set("decls", JValue.NewArray());
            var doc = new GraphDocument { Root = root };
            doc.Reindex();
            return doc;
        }

        public string Text => GraphJson.WriteFile(Root);

        public void Save(string path)
        {
            File.WriteAllText(path, Text, new UTF8Encoding(false));
        }

        public JValue Decls
        {
            get
            {
                var d = Root.Arr("decls");
                if (d == null)
                {
                    d = JValue.NewArray();
                    Root.Set("decls", d, GraphSchema.RootOrder);
                }
                return d;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Ids

        public int NextId => Root.Wire("nextId");

        public int NewId()
        {
            var id = Math.Max(NextId, maxId + 1);
            if (id < 1) id = 1;
            maxId = id;
            Root.Set("nextId", JValue.Of(id + 1), GraphSchema.RootOrder);
            return id;
        }

        public GraphElement Find(int id) => id != 0 && index.TryGetValue(id, out var e) ? e : null;
        public JValue Get(int id) => Find(id)?.Value;
        public IEnumerable<GraphElement> Elements => index.Values;

        // Finds every element again, after an edit; nextId stays above every id
        public void Reindex()
        {
            index.Clear();
            maxId = 0;
            WalkDecls(Root.Arr("decls"), null);
            if (NextId <= maxId) Root.Set("nextId", JValue.Of(maxId + 1), GraphSchema.RootOrder);
        }

        void Add(JValue v, GraphRole role, JValue list, JValue owner, JValue graph = null, GraphSlot slot = default)
        {
            var id = v.Wire("id");
            if (id == 0) return;
            if (id > maxId) maxId = id;
            index[id] = new GraphElement { Id = id, Role = role, Value = v, List = list, Owner = owner, Graph = graph, Slot = slot };
        }

        void WalkDecls(JValue decls, JValue owner)
        {
            if (decls == null) return;
            foreach (var d in decls.Items)
            {
                if (!d.IsObject) continue;
                Add(d, GraphRole.Decl, decls, owner);
                WalkDecl(d);
            }
        }

        void WalkDecl(JValue d)
        {
            var kind = d.Str("kind");
            var id = d.Wire("id");
            WalkAttributes(d);
            if (kind == "namespace") WalkDecls(d.Arr("decls"), d);
            WalkParams(d, d.Arr("params"), GraphRole.Param);
            var members = d.Arr("members");
            if (members != null)
            {
                foreach (var m in members.Items)
                {
                    if (!m.IsObject) continue;
                    if (kind == "enum")
                    {
                        Add(m, GraphRole.EnumMember, members, d);
                        WalkGraph(m.Get("value"), m, new GraphSlot(m.Wire("id"), "value"));
                    }
                    else
                    {
                        Add(m, GraphRole.Member, members, d);
                        WalkMember(m);
                    }
                }
            }
            var entries = d.Arr("entries");
            if (entries != null)
            {
                foreach (var e in entries.Items)
                {
                    Add(e, GraphRole.Entry, entries, d);
                    WalkGraph(e.Get("value"), e, new GraphSlot(e.Wire("id"), "value"));
                }
            }
            var payload = d.Arr("payload");
            if (payload != null)
            {
                foreach (var p in payload.Items)
                {
                    Add(p, GraphRole.Payload, payload, d);
                    WalkAttributes(p);
                    WalkGraph(p.Get("default"), p, new GraphSlot(p.Wire("id"), "default"));
                }
            }
            var root = d.Get("root");
            if (kind == "prefab" && root != null && root.IsObject) WalkPrefab(root, null, d);
            WalkGraph(d.Get("value"), d, new GraphSlot(id, "value"));
            WalkGraph(d.Get("body"), d, new GraphSlot(id, "body"));
        }

        void WalkMember(JValue m)
        {
            var id = m.Wire("id");
            WalkAttributes(m);
            WalkParams(m, m.Arr("params"), m.Str("kind") == "sample" ? GraphRole.SampleParam : GraphRole.Param);
            var accessors = m.Arr("accessors");
            if (accessors != null)
            {
                foreach (var a in accessors.Items)
                {
                    Add(a, GraphRole.Accessor, accessors, m);
                    WalkGraph(a.Get("body"), a, new GraphSlot(a.Wire("id"), "body"));
                }
            }
            WalkGraph(m.Get("default"), m, new GraphSlot(id, "default"));
            WalkGraph(m.Get("value"), m, new GraphSlot(id, "value"));
            WalkGraph(m.Get("body"), m, new GraphSlot(id, "body"));
        }

        void WalkAttributes(JValue v)
        {
            var attributes = v.Arr("attributes");
            if (attributes == null) return;
            foreach (var a in attributes.Items) Add(a, GraphRole.Attribute, attributes, v);
        }

        void WalkParams(JValue owner, JValue list, GraphRole role)
        {
            if (list == null) return;
            foreach (var p in list.Items)
            {
                if (!p.IsObject) continue;
                Add(p, role, list, owner);
                WalkGraph(p.Get("default"), p, new GraphSlot(p.Wire("id"), "default"));
                var query = p.Arr("query");
                if (query != null) WalkParams(owner, query, GraphRole.Param);
            }
        }

        void WalkPrefab(JValue p, JValue list, JValue owner)
        {
            Add(p, GraphRole.PrefabNode, list, owner);
            var id = p.Wire("id");
            var components = p.Arr("components");
            for (var i = 0; components != null && i < components.Items.Count; i++) WalkGraph(components.Items[i], p, new GraphSlot(id, "components", i));
            var children = p.Arr("children");
            if (children != null) foreach (var c in children.Items) WalkPrefab(c, children, owner);
        }

        void WalkGraph(JValue g, JValue owner, GraphSlot slot)
        {
            if (g == null || !g.IsObject) return;
            var nodes = g.Arr("nodes");
            if (nodes != null) foreach (var n in nodes.Items) Add(n, GraphRole.Node, nodes, owner, g, slot);
            var notes = g.Arr("notes");
            if (notes != null) foreach (var n in notes.Items) Add(n, GraphRole.Note, notes, owner, g, slot);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Graphs

        public JValue GetGraph(GraphSlot slot)
        {
            var owner = Get(slot.Owner);
            if (owner == null) return null;
            var v = owner.Get(slot.Key);
            if (slot.Index >= 0) v = v != null && v.IsArray && slot.Index < v.Items.Count ? v.Items[slot.Index] : null;
            return v != null && v.IsObject ? v : null;
        }

        // The graph of a slot, made empty if it isn't there
        public JValue EnsureGraph(GraphSlot slot)
        {
            var g = GetGraph(slot);
            if (g != null) return g;
            var owner = Get(slot.Owner);
            if (owner == null) return null;
            g = JValue.NewObject();
            g.Set(slot.IsBody ? "entry" : "root", JValue.Null());
            g.Set("nodes", JValue.NewArray());
            if (slot.Index >= 0)
            {
                var a = owner.Arr(slot.Key);
                if (a == null || slot.Index >= a.Items.Count) return null;
                a.Items[slot.Index] = g;
            }
            else owner.Set(slot.Key, g, OrderOf(Find(slot.Owner)));
            return g;
        }

        // The key order of an element's kind
        public static IList<string> OrderOf(GraphElement e)
        {
            if (e == null) return null;
            switch (e.Role)
            {
                case GraphRole.Decl: return GraphSchema.DeclOrder(e.Value.Str("kind"));
                case GraphRole.Member: return GraphSchema.MemberOrder(e.Value.Str("kind"));
                case GraphRole.EnumMember: return GraphSchema.EnumMemberOrder;
                case GraphRole.Entry: return GraphSchema.EntryOrder;
                case GraphRole.Param: return GraphSchema.ParamOrder;
                case GraphRole.SampleParam: return GraphSchema.SampleParamOrder;
                case GraphRole.Payload: return GraphSchema.PayloadOrder;
                case GraphRole.Attribute: return GraphSchema.AttributeOrder;
                case GraphRole.Accessor: return GraphSchema.AccessorOrder;
                case GraphRole.PrefabNode: return GraphSchema.PrefabNodeOrder;
                case GraphRole.Node: return GraphSchema.NodeOrder(e.Value.Str("k"));
                case GraphRole.Note: return GraphSchema.NoteOrder;
                default: return null;
            }
        }

        public static JValue Nodes(JValue graph)
        {
            var nodes = graph.Arr("nodes");
            if (nodes == null)
            {
                nodes = JValue.NewArray();
                graph.Set("nodes", nodes, graph.Has("entry") ? GraphSchema.BodyOrder : GraphSchema.ValueOrder);
            }
            return nodes;
        }

        public static JValue NodeIn(JValue graph, int id)
        {
            var nodes = graph?.Arr("nodes");
            if (nodes == null || id == 0) return null;
            foreach (var n in nodes.Items) if (n.Wire("id") == id) return n;
            return null;
        }

        // A new node of a kind in a graph, at a place, with what its kind starts with
        public JValue AddNode(JValue graph, string k, float x, float y)
        {
            var n = NewNode(k, x, y);
            Nodes(graph).Items.Add(n);
            return n;
        }

        public JValue NewNode(string k, float x, float y)
        {
            var order = GraphSchema.NodeOrder(k);
            var n = JValue.NewObject();
            n.Set("id", JValue.Of(NewId()));
            n.Set("k", JValue.Of(k));
            n.Set("x", JValue.Of(Mathf.Round(x)));
            n.Set("y", JValue.Of(Mathf.Round(y)));
            switch (k)
            {
                case "int": n.Set("text", JValue.Of("0"), order); break;
                case "float": n.Set("text", JValue.Of("0.0"), order); break;
                case "char": n.Set("text", JValue.Of("'a'"), order); break;
                case "string": n.Set("text", JValue.Of(""), order); break;
                case "bool": n.Set("value", JValue.Of(false), order); break;
                case "name": n.Set("name", JValue.Of("name"), order); break;
                case "member": n.Set("member", JValue.Of("member"), order); break;
                case "call": n.Set("name", JValue.Of("Function"), order); n.Set("args", JValue.NewArray(), order); break;
                case "method": n.Set("name", JValue.Of("Method"), order); n.Set("args", JValue.NewArray(), order); break;
                case "outVar": n.Set("name", JValue.Of("result"), order); break;
                case "binary": n.Set("op", JValue.Of("+"), order); break;
                case "unary": n.Set("op", JValue.Of("!"), order); break;
                case "step": n.Set("op", JValue.Of("++"), order); break;
                case "switchExpr":
                {
                    var arms = JValue.NewArray();
                    arms.Items.Add(NewArm());
                    n.Set("arms", arms, order);
                    break;
                }
                case "interp": n.Set("parts", JValue.Strings(new[] { "" }), order); n.Set("values", JValue.NewArray(), order); break;
                case "index": n.Set("index", JValue.ArrayOf(new[] { JValue.Null() }), order); break;
                case "list": n.Set("items", JValue.NewArray(), order); break;
                case "literal": n.Set("type", JValue.Of("Type"), order); n.Set("inits", JValue.NewArray(), order); break;
                case "lambda": n.Set("params", JValue.NewArray(), order); break;
                case "is": n.Set("type", JValue.Of("Type"), order); break;
                case "has": n.Set("subject", JValue.Of("V"), order); n.Set("type", JValue.Of("float3"), order); n.Set("field", JValue.Of("normal"), order); break;
                case "text":
                case "code": n.Set("text", JValue.Of(""), order); n.Set("inputs", JValue.NewObject(), order); break;
                case "var": n.Set("name", JValue.Of("value"), order); break;
                case "assign": n.Set("op", JValue.Of("="), order); break;
                case "foreach": n.Set("name", JValue.Of("item"), order); break;
                case "parallel": n.Set("name", JValue.Of("at"), order); break;
                case "switch":
                {
                    var cases = JValue.NewArray();
                    cases.Items.Add(NewCase());
                    n.Set("cases", cases, order);
                    break;
                }
            }
            return n;
        }

        public static JValue NewArm()
        {
            var arm = JValue.NewObject();
            arm.Set("pattern", JValue.Null());
            arm.Set("value", JValue.Null());
            return arm;
        }

        public static JValue NewCase()
        {
            var c = JValue.NewObject();
            c.Set("labels", JValue.ArrayOf(new[] { JValue.Null() }));
            return c;
        }

        // Connects a data output (`from`) into an input, or an exec output to a statement (`from` the statement's
        // id): false, with why, when the format can't have it
        public static bool Connect(JValue graph, int node, WirePath path, int from, out string why)
        {
            why = null;
            var n = NodeIn(graph, node);
            var source = NodeIn(graph, from);
            if (n == null || source == null)
            {
                why = "a wire joins two nodes of one graph";
                return false;
            }
            var exec = GraphPorts.IsExecPath(n, path);
            if (exec)
            {
                if (!GraphSchema.IsExec(source.Str("k")))
                {
                    why = $"'{source.Str("k")}' is a value, not a statement";
                    return false;
                }
                if (from == node)
                {
                    why = "a statement can't follow itself";
                    return false;
                }
                var other = ExecSourceOf(graph, from);
                if (other.node != 0 && !(other.node == node && other.path.Equals(path)))
                {
                    why = "a statement runs from one place only: copy it for the other";
                    return false;
                }
                if (graph.Wire("entry") == from)
                {
                    why = "the body's first statement runs from its start";
                    return false;
                }
            }
            else
            {
                var k = source.Str("k");
                if (GraphSchema.IsExec(k) && k != "eval")
                {
                    why = $"'{k}' is a statement, which has no value";
                    return false;
                }
                if (from == node || Reads(graph, from, node))
                {
                    why = "a value can't be made of itself";
                    return false;
                }
            }
            path.Set(n, from);
            return true;
        }

        // Whether node `a` reads node `b`, through any of its inputs
        public static bool Reads(JValue graph, int a, int b)
        {
            var seen = new HashSet<int>();
            var stack = new Stack<int>();
            stack.Push(a);
            while (stack.Count > 0)
            {
                var id = stack.Pop();
                if (!seen.Add(id)) continue;
                if (id == b) return true;
                var n = NodeIn(graph, id);
                if (n == null) continue;
                foreach (var w in GraphPorts.DataWires(n)) stack.Push(w);
            }
            return false;
        }

        // The exec output that goes to a statement, if one does
        public static (int node, WirePath path) ExecSourceOf(JValue graph, int target)
        {
            foreach (var n in Nodes(graph).Items)
            {
                if (!GraphSchema.IsExec(n.Str("k"))) continue;
                foreach (var p in GraphPorts.Of(n))
                    if (!p.Input && p.Exec && p.Path.Get(n) == target) return (n.Wire("id"), p.Path);
            }
            return (0, null);
        }

        public static void Disconnect(JValue graph, int node, WirePath path)
        {
            var n = NodeIn(graph, node);
            if (n != null) path.Set(n, 0);
        }

        // Removes nodes and every wire to them; a statement taken from a chain leaves the statements around it joined
        public void DeleteNodes(JValue graph, ICollection<int> ids)
        {
            var nodes = Nodes(graph);
            var gone = new HashSet<int>(ids);
            // Joins a chain around each statement that goes
            foreach (var id in ids)
            {
                var n = NodeIn(graph, id);
                if (n == null || !GraphSchema.IsExec(n.Str("k"))) continue;
                var next = n.Wire("next");
                while (next != 0 && gone.Contains(next)) next = NodeIn(graph, next)?.Wire("next") ?? 0;
                if (graph.Wire("entry") == id) graph.Set("entry", next != 0 ? JValue.Of(next) : JValue.Null());
                var (from, path) = ExecSourceOf(graph, id);
                if (from != 0 && !gone.Contains(from)) path.Set(NodeIn(graph, from), next);
                n.Remove("next");
            }
            nodes.Items.RemoveAll(n => gone.Contains(n.Wire("id")));
            var notes = graph.Arr("notes");
            notes?.Items.RemoveAll(n => gone.Contains(n.Wire("id")));
            foreach (var n in nodes.Items)
            {
                foreach (var p in GraphPorts.Of(n))
                    if (p.Path != null && gone.Contains(p.Path.Get(n))) p.Path.Set(n, 0);
            }
            if (gone.Contains(graph.Wire("root"))) graph.Set("root", JValue.Null());
            if (gone.Contains(graph.Wire("entry"))) graph.Set("entry", JValue.Null());
            Reindex();
        }

        // The nodes `ids` with what's between them, as text to copy: wires to nodes not among them are dropped, but
        // a local's read keeps its declaration's id
        public static string Copy(JValue graph, ICollection<int> ids, ICollection<int> noteIds = null)
        {
            var clip = JValue.NewObject();
            clip.Set("tidegraphClip", JValue.Of(1));
            var nodes = JValue.NewArray();
            foreach (var n in Nodes(graph).Items)
                if (ids.Contains(n.Wire("id"))) nodes.Items.Add(n.Clone());
            clip.Set("nodes", nodes);
            var notes = JValue.NewArray();
            var graphNotes = graph.Arr("notes");
            if (graphNotes != null && noteIds != null)
                foreach (var n in graphNotes.Items)
                    if (noteIds.Contains(n.Wire("id"))) notes.Items.Add(n.Clone());
            if (notes.Items.Count > 0) clip.Set("notes", notes);
            return GraphJson.Write(clip);
        }

        public static bool IsClip(string text)
        {
            if (string.IsNullOrEmpty(text) || !text.TrimStart().StartsWith("{")) return false;
            try
            {
                return GraphJson.Parse(text).Has("tidegraphClip");
            }
            catch (GraphJson.ParseError)
            {
                return false;
            }
        }

        // Pastes copied nodes into a graph, `offset` from where they were, with ids of this file's: their wires
        // between them follow, and the rest are dropped. Returns the new nodes' and notes' ids.
        public List<int> Paste(JValue graph, string clipText, Vector2 offset)
        {
            var made = new List<int>();
            JValue clip;
            try
            {
                clip = GraphJson.Parse(clipText);
            }
            catch (GraphJson.ParseError)
            {
                return made;
            }
            var nodes = clip.Arr("nodes");
            if (nodes == null) return made;
            var map = new Dictionary<int, int>();
            foreach (var n in nodes.Items) map[n.Wire("id")] = NewId();
            var target = Nodes(graph);
            foreach (var original in nodes.Items)
            {
                var n = original.Clone();
                n.Set("id", JValue.Of(map[original.Wire("id")]));
                n.Set("x", JValue.Of(Mathf.Round((float)(n.Get("x")?.Number ?? 0) + offset.x)));
                n.Set("y", JValue.Of(Mathf.Round((float)(n.Get("y")?.Number ?? 0) + offset.y)));
                foreach (var p in GraphPorts.Of(n))
                {
                    if (p.Path == null) continue;
                    var w = p.Path.Get(n);
                    if (w != 0) p.Path.Set(n, map.TryGetValue(w, out var to) ? to : 0);
                }
                if (n.Str("k") == "local")
                {
                    var r = n.Wire("ref");
                    if (map.TryGetValue(r, out var to)) n.Set("ref", JValue.Of(to));
                }
                target.Items.Add(n);
                made.Add(n.Wire("id"));
            }
            var notes = clip.Arr("notes");
            if (notes != null)
            {
                var list = graph.Arr("notes");
                if (list == null)
                {
                    list = JValue.NewArray();
                    graph.Set("notes", list, graph.Has("entry") ? GraphSchema.BodyOrder : GraphSchema.ValueOrder);
                }
                foreach (var original in notes.Items)
                {
                    var n = original.Clone();
                    n.Set("id", JValue.Of(NewId()));
                    n.Set("x", JValue.Of(Mathf.Round((float)(n.Get("x")?.Number ?? 0) + offset.x)));
                    n.Set("y", JValue.Of(Mathf.Round((float)(n.Get("y")?.Number ?? 0) + offset.y)));
                    list.Items.Add(n);
                    made.Add(n.Wire("id"));
                }
            }
            Reindex();
            return made;
        }

        // What runs and what's read: the statements reached from the entry and the values they (or the root) read.
        // tidec leaves the rest out, and the canvas dims it.
        public static HashSet<int> Reached(JValue graph)
        {
            var reached = new HashSet<int>();
            var nodes = new Dictionary<int, JValue>();
            foreach (var n in Nodes(graph).Items) nodes[n.Wire("id")] = n;
            var stack = new Stack<int>();
            var entry = graph.Wire("entry");
            var root = graph.Wire("root");
            if (entry != 0) stack.Push(entry);
            if (root != 0) stack.Push(root);
            while (stack.Count > 0)
            {
                var id = stack.Pop();
                if (!nodes.TryGetValue(id, out var n) || !reached.Add(id)) continue;
                foreach (var w in GraphPorts.DataWires(n)) stack.Push(w);
                if (GraphSchema.IsExec(n.Str("k"))) foreach (var w in GraphPorts.ExecWires(n)) stack.Push(w);
            }
            return reached;
        }

        // The locals declared in a graph, with their names: what a local node can read
        public static IEnumerable<JValue> Locals(JValue graph)
        {
            foreach (var n in Nodes(graph).Items)
                if (GraphSchema.DeclaresLocal(n) && !string.IsNullOrEmpty(GraphSchema.LocalName(n))) yield return n;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Declarations and members

        // A new declaration of a kind, with what its kind starts with
        public JValue NewDecl(string kind, string name)
        {
            var order = GraphSchema.DeclOrder(kind);
            var d = JValue.NewObject();
            d.Set("kind", JValue.Of(kind));
            d.Set("id", JValue.Of(NewId()));
            if (kind == "text")
            {
                d.Set("text", JValue.Of("// Tide text"), order);
                return d;
            }
            if (kind != "settings") d.Set("name", JValue.Of(name), order);
            if (kind == "namespace") d.Set("decls", JValue.NewArray(), order);
            else if (Array.IndexOf(GraphSchema.DataKinds, kind) >= 0 || kind == "enum") d.Set("members", JValue.NewArray(), order);
            else if (kind == "const" || kind == "asset")
            {
                d.Set("type", JValue.Of(kind == "asset" ? "Texture" : "int"), order);
                d.Set("value", ValueGraph(kind == "asset" ? "string" : "int", kind == "asset" ? "image.png" : "0"), order);
            }
            else if (kind == "alias") d.Set("type", JValue.Of("int"), order);
            else if (kind == "settings") d.Set("entries", JValue.NewArray(), order);
            else if (kind == "prefab")
            {
                var root = JValue.NewObject();
                root.Set("id", JValue.Of(NewId()));
                root.Set("components", JValue.NewArray());
                root.Set("children", JValue.NewArray());
                d.Set("root", root, order);
            }
            else if (kind == "handler")
            {
                var trigger = JValue.NewObject();
                trigger.Set("type", JValue.Of("Spawned"));
                d.Set("trigger", trigger, order);
                d.Set("params", JValue.NewArray(), order);
                d.Set("body", EmptyBody(), order);
            }
            else if (kind == "command")
            {
                d.Set("payload", JValue.NewArray(), order);
                d.Set("params", JValue.NewArray(), order);
                d.Set("body", EmptyBody(), order);
            }
            else
            {
                if (kind == "function") d.Set("returnType", JValue.Of("void"), order);
                d.Set("params", JValue.NewArray(), order);
                d.Set("body", EmptyBody(), order);
            }
            return d;
        }

        public JValue NewMember(string kind, string name)
        {
            var order = GraphSchema.MemberOrder(kind);
            var m = JValue.NewObject();
            m.Set("kind", JValue.Of(kind));
            m.Set("id", JValue.Of(NewId()));
            switch (kind)
            {
                case "field":
                    m.Set("type", JValue.Of("int"), order);
                    m.Set("name", JValue.Of(name), order);
                    break;
                case "property":
                {
                    m.Set("type", JValue.Of("int"), order);
                    m.Set("name", JValue.Of(name), order);
                    var accessors = JValue.NewArray();
                    accessors.Items.Add(NewAccessor("get"));
                    m.Set("accessors", accessors, order);
                    break;
                }
                case "method":
                    m.Set("name", JValue.Of(name), order);
                    m.Set("returnType", JValue.Of("void"), order);
                    m.Set("params", JValue.NewArray(), order);
                    m.Set("body", EmptyBody(), order);
                    break;
                case "operator":
                    m.Set("returnType", JValue.Of("int"), order);
                    m.Set("op", JValue.Of("+"), order);
                    m.Set("params", JValue.NewArray(), order);
                    m.Set("body", EmptyBody(), order);
                    break;
                case "tweak":
                    m.Set("type", JValue.Of("float"), order);
                    m.Set("name", JValue.Of(name), order);
                    m.Set("value", ValueGraph("float", "1.0"), order);
                    break;
                case "sample":
                    m.Set("name", JValue.Of("Sample"), order);
                    m.Set("params", JValue.NewArray(), order);
                    m.Set("body", EmptyBody(), order);
                    break;
                case "sanitize":
                case "predict":
                    m.Set("body", EmptyBody(), order);
                    break;
                case "prepare":
                    m.Set("params", JValue.NewArray(), order);
                    m.Set("body", EmptyBody(), order);
                    break;
                case "text":
                    m.Set("text", JValue.Of("// Tide text"), order);
                    break;
            }
            return m;
        }

        public JValue NewAccessor(string word)
        {
            var a = JValue.NewObject();
            a.Set("id", JValue.Of(NewId()));
            a.Set("word", JValue.Of(word));
            a.Set("body", EmptyBody());
            return a;
        }

        public JValue NewParam(string type, string name)
        {
            var p = JValue.NewObject();
            p.Set("id", JValue.Of(NewId()));
            p.Set("mode", JValue.Of("read"));
            p.Set("type", JValue.Of(type));
            p.Set("name", JValue.Of(name));
            return p;
        }

        public JValue NewNamed(string name, string valueKind = null, string valueText = null)
        {
            var e = JValue.NewObject();
            e.Set("id", JValue.Of(NewId()));
            e.Set("name", JValue.Of(name));
            if (valueKind != null) e.Set("value", ValueGraph(valueKind, valueText));
            return e;
        }

        public JValue NewAttribute(string name)
        {
            var a = JValue.NewObject();
            a.Set("id", JValue.Of(NewId()));
            a.Set("name", JValue.Of(name));
            return a;
        }

        public JValue NewPrefabNode(string name)
        {
            var p = JValue.NewObject();
            p.Set("id", JValue.Of(NewId()));
            p.Set("name", JValue.Of(name));
            p.Set("components", JValue.NewArray());
            p.Set("children", JValue.NewArray());
            return p;
        }

        public static JValue EmptyBody()
        {
            var g = JValue.NewObject();
            g.Set("entry", JValue.Null());
            g.Set("nodes", JValue.NewArray());
            return g;
        }

        // A value graph of one literal node
        public JValue ValueGraph(string k, string text)
        {
            var g = JValue.NewObject();
            var n = NewNode(k, -180, 0);
            if (k == "bool") n.Set("value", JValue.Of(text == "true"), GraphSchema.NodeOrder(k));
            else if (text != null) n.Set("text", JValue.Of(text), GraphSchema.NodeOrder(k));
            g.Set("root", JValue.Of(n.Wire("id")));
            var nodes = JValue.NewArray();
            nodes.Items.Add(n);
            g.Set("nodes", nodes);
            return g;
        }

        // A copy of an element with new ids for everything with one in it, and the nodes' wires and locals' reads
        // following
        public JValue Duplicate(JValue v)
        {
            var copy = v.Clone();
            Renumber(copy);
            return copy;
        }

        void Renumber(JValue v)
        {
            if (v.IsArray)
            {
                foreach (var item in v.Items) Renumber(item);
                return;
            }
            if (!v.IsObject) return;
            var nodes = v.Arr("nodes");
            if (nodes != null && (v.Has("entry") || v.Has("root")))
            {
                var map = new Dictionary<int, int>();
                foreach (var n in nodes.Items) map[n.Wire("id")] = NewId();
                foreach (var n in nodes.Items)
                {
                    n.Set("id", JValue.Of(map[n.Wire("id")]));
                    foreach (var p in GraphPorts.Of(n))
                    {
                        if (p.Path == null) continue;
                        var w = p.Path.Get(n);
                        if (w != 0 && map.TryGetValue(w, out var to)) p.Path.Set(n, to);
                    }
                    if (n.Str("k") == "local" && map.TryGetValue(n.Wire("ref"), out var r)) n.Set("ref", JValue.Of(r));
                }
                foreach (var key in new[] { "entry", "root" })
                    if (map.TryGetValue(v.Wire(key), out var to)) v.Set(key, JValue.Of(to));
                var notes = v.Arr("notes");
                if (notes != null) foreach (var n in notes.Items) n.Set("id", JValue.Of(NewId()));
                return;
            }
            if (v.Has("id") && v.Get("id").Kind == JKind.Number) v.Set("id", JValue.Of(NewId()));
            foreach (var child in v.Values) Renumber(child);
        }

        // The declarations and members of the file with names: the types it declares, for the type lists
        public IEnumerable<string> DeclaredTypes()
        {
            var types = new[] { "component", "singleton", "struct", "event", "input", "scene", "enum", "alias" };
            foreach (var e in index.Values)
                if (e.Role == GraphRole.Decl && Array.IndexOf(types, e.Value.Str("kind")) >= 0 && !string.IsNullOrEmpty(e.Value.Str("name")))
                    yield return e.Value.Str("name");
        }

        // The declaration or member a graph's slot is in: its routine, for its parameters
        public JValue RoutineOf(GraphSlot slot)
        {
            var e = Find(slot.Owner);
            if (e == null) return null;
            if (e.Role == GraphRole.Accessor) return e.Owner;
            return e.Value;
        }

        // A readable name of an element, for the outline and the canvas's title
        public string Describe(GraphElement e)
        {
            if (e == null) return "";
            var v = e.Value;
            var kind = v.Str("kind");
            var name = v.Str("name");
            switch (e.Role)
            {
                case GraphRole.Decl:
                    if (kind == "settings") return "settings";
                    if (kind == "text") return "text: " + FirstLine(v.Str("text"));
                    if (kind == "handler") return $"event({v.Get("trigger")?.Str("type")}) {name}";
                    if (kind == "command") return $"command {name}";
                    return $"{kind} {name}";
                case GraphRole.Member:
                    switch (kind)
                    {
                        case "field": return $"{v.Str("type")} {name}";
                        case "property": return $"{v.Str("type")} {name} {{ }}";
                        case "method": return $"{name}()";
                        case "operator": return $"operator {v.Str("op")}";
                        case "tweak": return $"tweak {name}";
                        case "sample": return $"{name}()";
                        case "sanitize": return "Sanitize()";
                        case "predict": return "Predict()";
                        case "prepare": return "prepare()";
                        default: return "text: " + FirstLine(v.Str("text"));
                    }
                case GraphRole.Accessor: return v.Str("word");
                case GraphRole.PrefabNode: return string.IsNullOrEmpty(name) ? "root entity" : $"entity {name}";
                case GraphRole.Param: return $"{v.Str("type")} {name}".Trim();
                case GraphRole.Node: return v.Str("k");
                default: return name ?? e.Role.ToString();
            }
        }

        static string FirstLine(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            var i = s.IndexOf('\n');
            return i < 0 ? s : s.Substring(0, i) + " ...";
        }
    }
}
