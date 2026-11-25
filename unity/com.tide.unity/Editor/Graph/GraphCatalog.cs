using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;

namespace Tide.Editor
{
    // Where a search is made: the graph a new node goes in, the window and the frame it's in, the pin a wire was
    // dropped from, and the member or method node whose member it picks (Retarget), if it's for one
    sealed class GraphSearchContext
    {
        public GraphDocument Doc;
        public JValue Graph;
        public GraphSlot Slot;
        public TideGraphWindow Window;
        public GraphFrame Frame;
        public GraphPort Pending;
        public int Retarget;
        // What was in the input a node is worked out for: wired into the new node's first input ("Work it out…")
        public int Wrap;
        public bool Body => Slot.IsBody;
    }

    // A thing a search offers: what it makes, put in a graph at a place, returning the node to wire. Detail says
    // what it is; the language server's rank above the catalog's when Priority is higher. Statement is a step of
    // the flow (it goes on an arrow, not into a value's input). Apply makes a node the member or method picked, for
    // a search that retargets one. Command does something else than make a node (a note, a paste).
    sealed class GraphSearchEntry
    {
        public string Group, Title, Keywords = "", Detail;
        public int Priority;
        public bool Statement;
        public bool Reads; // A Get of something the code takes, or of a local
        public string Variable, Type; // A Get's or a Set's variable, and its type
        public string Op;  // The operator a node of an operator is made with
        public Func<GraphDocument, JValue, Vector2, JValue> Make;
        public Action<JValue> Apply;
        public Action Command;
    }

    // Every node kind of the schema, under its category's title and with what the schema says it does, and Gets and
    // Sets of what a graph's code takes and of its locals, as Blueprint's context menu has them
    sealed class GraphCatalog : ITideGraphSearch
    {
        public const string VariablesGroup = "Variables";

        public IEnumerable<GraphSearchEntry> Entries(GraphSearchContext c)
        {
            // What the code takes and its locals: the first things a value comes from
            var variables = new List<(string name, string type, bool local, int r)>();
            if (c.Body)
                foreach (var read in GraphDocument.ReadsOf(c.Doc?.RoutineOf(c.Slot)))
                    variables.Add((read.Name, read.Type, false, 0));
            if (c.Graph != null)
                foreach (var local in GraphDocument.Locals(c.Graph))
                    variables.Add((GraphSchema.LocalName(local), local.Str("type"), true, local.Wire("id")));
            foreach (var v in variables)
            {
                var (name, type, local, r) = v;
                var get = local
                    ? Node(VariablesGroup, "Get " + name, "local", "get local read variable " + name, type, n => n.Set("ref", JValue.Of(r), GraphSchema.NodeOrder("local")), true)
                    : Node(VariablesGroup, "Get " + name, "name", "get parameter name read variable " + name, type, n => n.Set("name", JValue.Of(name), GraphSchema.NodeOrder("name")), true);
                get.Variable = name;
                get.Type = type;
                yield return get;
            }
            // A Set of what can be changed: a step of the flow, titled by it
            if (c.Body && c.Frame != null)
                foreach (var v in variables)
                {
                    var (name, type, local, r) = v;
                    if (!c.Frame.Changeable(local ? null : name, local ? r : 0)) continue;
                    yield return new GraphSearchEntry
                    {
                        Group = VariablesGroup, Title = "Set " + name, Keywords = "set assign change variable " + name, Detail = type, Statement = true, Variable = name, Type = type, Priority = 1,
                        Make = (doc, graph, at) =>
                        {
                            var set = doc.AddNode(graph, "assign", at.x, at.y);
                            var target = doc.AddNode(graph, local ? "local" : "name", at.x - 150, at.y + 26);
                            if (local) target.Set("ref", JValue.Of(r), GraphSchema.NodeOrder("local"));
                            else target.Set("name", JValue.Of(name), GraphSchema.NodeOrder("name"));
                            new WirePath("target").Set(set, target.Wire("id"));
                            return set;
                        },
                    };
                }
            if (c.Body)
            {
                yield return Statement("Call a function", "eval call function invoke do", "call", GraphSchema.Spec("call")?.Help);
                yield return Statement("Call a method", "eval method invoke do", "method", GraphSchema.Spec("method")?.Help);
            }
            foreach (var spec in GraphSchema.Nodes)
            {
                if (spec.K == "local") continue; // A local is read from its pin, or from the entries above
                if (spec.Exec && !c.Body) continue;
                var group = GraphSchema.CategoryTitle(spec.Category);
                var ops = GraphSchema.Ops(spec.K);
                if (ops.Length > 0 && !spec.Exec)
                {
                    // An operator each: Add +, Not !
                    foreach (var (op, title) in ops)
                    {
                        var o = op;
                        var k = spec.K;
                        var entry = Node(group, $"{title ?? spec.Title} {op}", k, $"{k} operator {title} {op}", spec.Help, n => n.Set("op", JValue.Of(o), GraphSchema.NodeOrder(k)));
                        entry.Op = o;
                        yield return entry;
                    }
                    continue;
                }
                // `this`, as Blueprint says it
                var made = Node(group, spec.K == "this" ? GraphFrame.SelfWord : spec.Title, spec.K, spec.K + " " + spec.Title, spec.Help);
                made.Statement = spec.Exec;
                yield return made;
            }
        }

        static GraphSearchEntry Node(string group, string title, string k, string keywords, string detail, Action<JValue> set = null, bool reads = false)
            => new GraphSearchEntry
            {
                Group = group, Title = title, Keywords = keywords, Detail = detail, Reads = reads, Priority = reads ? 1 : 0,
                Make = (doc, graph, at) =>
                {
                    var n = doc.AddNode(graph, k, at.x, at.y);
                    set?.Invoke(n);
                    return n;
                },
            };

        // A call as a step: an eval with the call in its value, shown as one node
        static GraphSearchEntry Statement(string title, string keywords, string k, string detail)
            => new GraphSearchEntry
            {
                Group = GraphSchema.CategoryTitle(GraphSchema.Spec("eval")?.Category), Title = title, Keywords = keywords, Detail = detail, Statement = true,
                Make = (doc, graph, at) =>
                {
                    var call = doc.AddNode(graph, k, at.x - 220, at.y + 60);
                    var eval = doc.AddNode(graph, "eval", at.x, at.y);
                    eval.Set("value", JValue.Of(call.Wire("id")), GraphSchema.NodeOrder("eval"));
                    return eval;
                },
            };
    }
}
