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
        public bool Reads; // A read of something the code takes, or of a local
        public Func<GraphDocument, JValue, Vector2, JValue> Make;
        public Action<JValue> Apply;
        public Action Command;
    }

    // Every node kind of the schema, under its category's title and with what the schema says it does, and the
    // reads of what a graph's code takes and of its locals
    sealed class GraphCatalog : ITideGraphSearch
    {
        public IEnumerable<GraphSearchEntry> Entries(GraphSearchContext c)
        {
            // What the code takes and its locals: the first things a value comes from
            foreach (var read in GraphDocument.ReadsOf(c.Doc?.RoutineOf(c.Slot)))
            {
                if (!c.Body) break;
                var name = read.Name;
                yield return Node("This code takes", name, "name", "parameter name read " + name, read.Type, n => n.Set("name", JValue.Of(name), GraphSchema.NodeOrder("name")), true);
            }
            if (c.Graph != null)
                foreach (var local in GraphDocument.Locals(c.Graph))
                {
                    var r = local.Wire("id");
                    var name = GraphSchema.LocalName(local);
                    yield return Node("Locals", name, "local", "local read " + name, local.Str("type"), n => n.Set("ref", JValue.Of(r), GraphSchema.NodeOrder("local")), true);
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
                        yield return entry;
                    }
                    continue;
                }
                var made = Node(group, spec.Title, spec.K, spec.K + " " + spec.Title, spec.Help);
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
