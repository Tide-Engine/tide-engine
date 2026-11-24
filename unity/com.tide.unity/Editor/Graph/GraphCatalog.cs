using System;
using System.Collections.Generic;
using UnityEngine;

namespace Tide.Editor
{
    // Where a search is made: the graph a new node goes in
    sealed class GraphSearchContext
    {
        public GraphDocument Doc;
        public JValue Graph;
        public GraphSlot Slot;
        public bool Body => Slot.IsBody;
    }

    // A thing a search offers: what it makes, put in a graph at a place, returning the node to wire
    sealed class GraphSearchEntry
    {
        public string Group, Title, Keywords = "";
        public Func<GraphDocument, JValue, Vector2, JValue> Make;
    }

    // Every node kind of the format, and the locals and parameters a graph's nodes can read
    sealed class GraphCatalog : ITideGraphSearch
    {
        public IEnumerable<GraphSearchEntry> Entries(GraphSearchContext c)
        {
            if (c.Body)
            {
                yield return Statement("Call", "eval call function invoke", "call");
                yield return Statement("Method call", "eval method invoke", "method");
                yield return Node("Flow", "Do (statement of a value)", "eval", "eval statement value");
                yield return Node("Flow", "Local (var)", "var", "var let local variable declare");
                yield return Node("Flow", "Assign =", "assign", "assign set = += -=");
                yield return Node("Flow", "If", "if", "if else branch condition");
                yield return Node("Flow", "While", "while", "while loop");
                yield return Node("Flow", "For", "for", "for loop");
                yield return Node("Flow", "For each", "foreach", "foreach loop each in");
                yield return Node("Flow", "Parallel", "parallel", "parallel grid loop by offset");
                yield return Node("Flow", "Switch", "switch", "switch case default");
                yield return Node("Flow", "Return", "return", "return");
                yield return Node("Flow", "Fail", "fail", "fail error");
                yield return Node("Flow", "Break", "break", "break");
                yield return Node("Flow", "Continue", "continue", "continue");
                yield return Node("Flow", "Block", "block", "block scope braces");
                yield return Node("Flow", "Discard _ =", "discard", "discard _");
                yield return Node("Flow", "Code (Tide text)", "code", "code text tide statements");
            }
            yield return Node("Values", "Int", "int", "int integer number");
            yield return Node("Values", "Float", "float", "float number decimal");
            yield return Node("Values", "Bool", "bool", "bool true false");
            yield return Node("Values", "String", "string", "string text");
            yield return Node("Values", "Char", "char", "char character");
            yield return Node("Values", "this", "this", "this self");
            yield return Node("Values", "default", "default", "default zero");
            yield return Node("Values", "null", "null", "null none");
            yield return Node("Values", "Name", "name", "name constant type namespace parameter");
            yield return Node("Values", "Member .", "member", "member field dot property");
            yield return Node("Values", "Member shorthand .Name", "member", "member shorthand enum", n => { n.Set("shorthand", JValue.Of(true), GraphSchema.NodeOrder("member")); });
            yield return Node("Values", "List [ ]", "list", "list array items");
            yield return Node("Values", "Value of a type { }", "literal", "literal struct construct new init");
            yield return Node("Values", "Text with values $\"\"", "interp", "interp interpolated string format");
            yield return Node("Values", "Lambda =>", "lambda", "lambda function arrow");
            yield return Node("Calls", "Call", "call", "call function invoke");
            yield return Node("Calls", "Method", "method", "method call invoke");
            yield return Node("Calls", "out", "out", "out argument");
            yield return Node("Calls", "out var", "outVar", "out var declare");
            foreach (var (op, title) in GraphSchema.BinaryOps)
            {
                var o = op;
                yield return Node("Math and logic", $"{title} {op}", "binary", $"binary operator {title} {op}", n => n.Set("op", JValue.Of(o), GraphSchema.NodeOrder("binary")));
            }
            yield return Node("Math and logic", "Not !", "unary", "unary not !", n => n.Set("op", JValue.Of("!"), GraphSchema.NodeOrder("unary")));
            yield return Node("Math and logic", "Negate -", "unary", "unary negate minus -", n => n.Set("op", JValue.Of("-"), GraphSchema.NodeOrder("unary")));
            yield return Node("Math and logic", "Complement ~", "unary", "unary complement bitwise ~", n => n.Set("op", JValue.Of("~"), GraphSchema.NodeOrder("unary")));
            yield return Node("Math and logic", "Increment ++x", "step", "step increment ++", n => n.Set("op", JValue.Of("++"), GraphSchema.NodeOrder("step")));
            yield return Node("Math and logic", "Decrement --x", "step", "step decrement --", n => n.Set("op", JValue.Of("--"), GraphSchema.NodeOrder("step")));
            yield return Node("Math and logic", "Increment after x++", "step", "step increment postfix ++", n =>
            {
                n.Set("op", JValue.Of("++"), GraphSchema.NodeOrder("step"));
                n.SetFlag("postfix", true, GraphSchema.NodeOrder("step"));
            });
            yield return Node("Math and logic", "Decrement after x--", "step", "step decrement postfix --", n =>
            {
                n.Set("op", JValue.Of("--"), GraphSchema.NodeOrder("step"));
                n.SetFlag("postfix", true, GraphSchema.NodeOrder("step"));
            });
            yield return Node("Math and logic", "Conditional ? :", "conditional", "conditional ternary ? :");
            yield return Node("Math and logic", "Coalesce ??", "coalesce", "coalesce ?? null");
            yield return Node("Math and logic", "Switch value", "switchExpr", "switch expression arms =>");
            yield return Node("Math and logic", "is", "is", "is type test pattern");
            yield return Node("Math and logic", "has", "has", "has field shader");
            yield return Node("Math and logic", "try", "try", "try error fails");
            yield return Node("Math and logic", "await", "await", "await async");
            yield return Node("Math and logic", "Value or default !", "defaulted", "defaulted ! optional");
            yield return Node("Math and logic", "Index [ ]", "index", "index subscript [ ] grid");
            yield return Node("Text", "Text (Tide)", "text", "text tide expression");

            // The locals declared in the graph, and the routine's parameters
            if (c.Graph != null)
            {
                foreach (var local in GraphDocument.Locals(c.Graph))
                {
                    var r = local.Wire("id");
                    var name = GraphSchema.LocalName(local);
                    yield return Node("Locals", name, "local", "local read " + name, n => n.Set("ref", JValue.Of(r), GraphSchema.NodeOrder("local")));
                }
            }
            foreach (var name in Names(c))
            {
                var nm = name;
                yield return Node("Parameters", nm, "name", "parameter name " + nm, n => n.Set("name", JValue.Of(nm), GraphSchema.NodeOrder("name")));
            }
        }

        // What a graph's code reads by name: its routine's parameters, a handler's trigger, a command's payload
        static IEnumerable<string> Names(GraphSearchContext c)
        {
            var routine = c.Doc?.RoutineOf(c.Slot);
            if (routine == null) yield break;
            var parameters = routine.Arr("params");
            if (parameters != null)
            {
                foreach (var p in parameters.Items)
                {
                    var name = p.Str("name");
                    if (!string.IsNullOrEmpty(name) && p.Str("mode") != "with" && p.Str("mode") != "without") yield return name;
                }
            }
            var trigger = routine.Get("trigger");
            if (trigger != null && !string.IsNullOrEmpty(trigger.Str("name"))) yield return trigger.Str("name");
            var payload = routine.Arr("payload");
            if (payload != null) foreach (var p in payload.Items) if (!string.IsNullOrEmpty(p.Str("name"))) yield return p.Str("name");
        }

        static GraphSearchEntry Node(string group, string title, string k, string keywords, Action<JValue> set = null)
            => new GraphSearchEntry
            {
                Group = group, Title = title, Keywords = keywords,
                Make = (doc, graph, at) =>
                {
                    var n = doc.AddNode(graph, k, at.x, at.y);
                    set?.Invoke(n);
                    return n;
                },
            };

        // A call as a statement: an eval with the call in its value, shown as one node
        static GraphSearchEntry Statement(string title, string keywords, string k)
            => new GraphSearchEntry
            {
                Group = "Flow", Title = title, Keywords = keywords,
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
