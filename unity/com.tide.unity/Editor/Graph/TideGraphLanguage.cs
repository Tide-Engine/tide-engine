using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using UnityEngine;

namespace Tide.Editor
{
    // The Tide a graph stands for, as tidels (or tidec) wrote it: its text, as UTF-8 too, since tidels's places are
    // byte offsets into it, and each element's stretch of it
    sealed class GraphText
    {
        public readonly string Text;
        public readonly byte[] Bytes;
        public readonly Dictionary<int, (int start, int end)> Spans = new Dictionary<int, (int, int)>();

        public GraphText(string text, JValue spans)
        {
            Text = text ?? "";
            Bytes = Encoding.UTF8.GetBytes(Text);
            if (spans == null) return;
            foreach (var s in spans.Items)
            {
                if (s.Items == null || s.Items.Count < 3) continue;
                var id = JValue.AsId(s.Items[2]);
                // An element's first: the outer one, for a node lowered in parts
                if (!Spans.ContainsKey(id)) Spans[id] = ((int)s.Items[0].Number, (int)s.Items[1].Number);
            }
        }

        public static GraphText Of(JValue result) => result == null || !result.IsObject ? null : new GraphText(result.Str("text"), result.Arr("spans"));

        public string Slice(int start, int end) => start < 0 || end > Bytes.Length || end < start ? "" : Encoding.UTF8.GetString(Bytes, start, end - start);

        public int IndexOf(char c, int from, int to)
        {
            for (var i = Math.Max(0, from); i < Math.Min(to, Bytes.Length); i++) if (Bytes[i] == c) return i;
            return -1;
        }

        public int LastIndexOf(char c, int from, int to)
        {
            for (var i = Math.Min(to, Bytes.Length) - 1; i >= Math.Max(0, from); i--) if (Bytes[i] == c) return i;
            return -1;
        }

        // The line of the text an offset is on
        public string LineAt(int offset)
        {
            if (offset < 0 || offset > Bytes.Length) return "";
            var start = offset;
            while (start > 0 && Bytes[start - 1] != '\n') start--;
            var end = offset;
            while (end < Bytes.Length && Bytes[end] != '\n') end++;
            return Slice(start, end).TrimEnd('\r');
        }
    }

    // What the language server knows of a graph: a check (its errors, its Tide and the type of each node's value),
    // hovers, and completions turned into nodes
    static class TideGraphLanguage
    {
        static string UriOf(string path) => TideLanguageClient.UriOf(path);

        // ---------------------------------------------------------------------------------------------------------
        // Checking

        public static void Check(TideLanguageClient client, string path, string json, Action<GraphCheckResult> done, Action<string> failed)
        {
            var uri = UriOf(path);
            client.Operate(finish =>
            {
                if (!client.SetText(uri, json))
                {
                    finish();
                    failed(client.Failure ?? "tidels can't be started");
                    return;
                }
                client.Ask("tide/graphText", uri, -1, (text, why) =>
                {
                    if (why != null)
                    {
                        finish();
                        failed(why);
                        return;
                    }
                    var result = new GraphCheckResult { Checker = "tidels", Json = json, Lowered = GraphText.Of(text) };
                    result.Text = result.Lowered?.Text ?? "";
                    client.Ask("tide/graphTypes", uri, -1, (types, why2) =>
                    {
                        finish();
                        if (types != null && types.IsObject)
                            for (var i = 0; i < types.Keys.Count; i++)
                                if (int.TryParse(types.Keys[i], out var id) && types.Values[i].Kind == JKind.String) result.Types[id] = types.Values[i].String;
                        Diagnostics(result, client.DiagnosticsOf(uri));
                        done(result);
                    });
                });
            });
        }

        static void Diagnostics(GraphCheckResult result, JValue list)
        {
            if (list == null) return;
            foreach (var d in list.Items)
            {
                var severity = (int)(d.Get("severity")?.Number ?? 1);
                var id = d.Get("data")?.Wire("node") ?? 0;
                var diagnostic = new GraphDiagnostic
                {
                    Id = id,
                    Severity = severity == 1 ? "error" : severity == 2 ? "warning" : "note",
                    Message = d.Str("message") ?? "",
                };
                if (result.Lowered != null && result.Lowered.Spans.TryGetValue(id, out var span)) diagnostic.Detail = result.Lowered.LineAt(span.start).Trim();
                var related = d.Arr("relatedInformation");
                if (related != null)
                    foreach (var r in related.Items)
                        diagnostic.Detail += (diagnostic.Detail.Length > 0 ? "\n" : "") + "note: " + r.Str("message");
                result.Diagnostics.Add(diagnostic);
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Hovers

        // What tidels says of a node: the hover at its name in the Tide, as text
        public static void Hover(TideGraphWindow window, int node, Action<string> done)
        {
            var checkedText = window.Checked;
            var client = TideLanguageClient.Shared;
            if (client == null || checkedText?.Lowered == null || checkedText.Checker != "tidels") return;
            var graph = window.Doc?.GetGraph(window.Slot);
            var n = GraphDocument.NodeIn(graph, node);
            var offset = NameOffset(checkedText.Lowered, n, node);
            if (offset < 0) return;
            var uri = UriOf(window.Path);
            client.Operate(finish =>
            {
                if (client.TextOf(uri) != checkedText.Json)
                {
                    finish();
                    return;
                }
                client.Ask("tide/graphHover", uri, offset, (r, why) =>
                {
                    finish();
                    var value = r?.Get("contents")?.Str("value");
                    if (value != null) done(Plain(value));
                });
            });
        }

        // Markdown as a tooltip shows it: code fences gone
        internal static string Plain(string markdown)
        {
            var lines = markdown.Replace("\r\n", "\n").Split('\n').Where(l => !l.TrimStart().StartsWith("```"));
            return string.Join("\n", lines).Replace("`", "").Trim();
        }

        // Where a node's name is in its Tide: a member's member, a method's name, a call's name, else its start
        static int NameOffset(GraphText t, JValue n, int id)
        {
            if (!t.Spans.TryGetValue(id, out var span)) return -1;
            switch (n?.Str("k"))
            {
                case "member":
                {
                    var name = n.Str("member") ?? "";
                    return Math.Max(span.start, span.end - Encoding.UTF8.GetByteCount(name));
                }
                case "method":
                {
                    var from = t.Spans.TryGetValue(n.Wire("object"), out var o) ? o.end : span.start;
                    var dot = t.IndexOf('.', from, span.end);
                    return dot >= 0 ? dot + 1 : span.start;
                }
                case "call":
                    return n.Flag("local") ? Math.Min(span.end, span.start + 6) : span.start;
                default:
                    return span.start;
            }
        }

        // ---------------------------------------------------------------------------------------------------------
        // Completion

        const string Probe = "tideGraphProbe";

        // What completion offers where a search is made: the members of a wired value, or what's at its place
        public static void Complete(GraphSearchContext c, Action<List<GraphSearchEntry>, string> found)
        {
            var window = c.Window;
            var client = TideLanguageClient.Shared;
            if (window == null || client == null || !client.Usable || string.IsNullOrEmpty(window.Path) || c.Graph == null)
            {
                found(null, null);
                return;
            }
            var uri = UriOf(window.Path);
            var text = c.Doc.Text;
            var objectNode = ObjectOf(c);
            if (c.Retarget != 0 && objectNode == 0)
            {
                found(null, "Wire its object to see what it has");
                return;
            }
            client.Operate(finish =>
            {
                void Fail(string why)
                {
                    client.EndProbe(uri);
                    finish();
                    found(null, why);
                }
                if (!client.SetText(uri, text))
                {
                    Fail(client.Failure ?? "tidels can't be started");
                    return;
                }
                if (objectNode != 0)
                {
                    // A copy of the graph with `object.tideGraphProbe` in it: completion after its '.'
                    var probe = MemberProbe(text, c.Slot, objectNode, out var probeId);
                    if (probe == null || !client.SetText(uri, probe, true))
                    {
                        Fail("The value can't be asked about");
                        return;
                    }
                    client.Ask("tide/graphText", uri, -1, (r, why) =>
                    {
                        var t = GraphText.Of(r);
                        if (t == null || !t.Spans.TryGetValue(probeId, out var span))
                        {
                            Fail(why ?? "tidels can't read the graph");
                            return;
                        }
                        var offset = span.end - Encoding.UTF8.GetByteCount(Probe);
                        // The value's type, read in the probe (where nothing read it, the check gave it none)
                        client.Ask("tide/graphTypes", uri, -1, (types, _) =>
                        {
                            var type = types?.Str(objectNode.ToString());
                            client.Ask("tide/graphCompletion", uri, offset, (items, why2) =>
                            {
                                client.EndProbe(uri);
                                finish();
                                found(Members(c, items, objectNode, type), why2);
                            });
                        });
                    });
                    return;
                }
                client.Ask("tide/graphText", uri, -1, (r, why) =>
                {
                    var t = GraphText.Of(r);
                    var offset = t == null ? -1 : Place(c, t);
                    if (offset < 0)
                    {
                        Fail(why);
                        return;
                    }
                    client.Ask("tide/graphCompletion", uri, offset, (items, why2) =>
                    {
                        finish();
                        found(Names(c, items), why2);
                    });
                });
            });
        }

        // The node whose members a search is for: the one a retargeted member's object is, or the value a wire
        // was dropped from
        static int ObjectOf(GraphSearchContext c)
        {
            if (c.Retarget != 0) return GraphDocument.NodeIn(c.Graph, c.Retarget)?.Wire("object") ?? 0;
            var p = c.Pending;
            if (p == null || p.Input || p.Exec || p.Node <= 0) return 0;
            return p.Node;
        }

        // A copy of the file with a member node on the object, where the object's value is read (or as a
        // statement of its own, or the expression's value): its text, and the member node's id
        internal static string MemberProbe(string text, GraphSlot slot, int objectNode, out int probeId)
        {
            probeId = 0;
            GraphDocument doc;
            try
            {
                doc = GraphDocument.Parse(text);
            }
            catch (GraphJson.ParseError)
            {
                return null;
            }
            var graph = doc.GetGraph(slot);
            if (graph == null || GraphDocument.NodeIn(graph, objectNode) == null) return null;
            var m = doc.AddNode(graph, "member", 0, 0);
            var order = GraphSchema.NodeOrder("member");
            m.Set("object", JValue.Of(objectNode), order);
            m.Set("member", JValue.Of(Probe), order);
            probeId = m.Wire("id");
            var reached = GraphDocument.Reached(graph);
            // Where something reached reads it: read through the member instead
            foreach (var n in GraphDocument.Nodes(graph).Items)
            {
                if (n == m || !reached.Contains(n.Wire("id"))) continue;
                foreach (var p in GraphPorts.Of(n))
                {
                    if (!p.Input || p.Exec || p.Path == null || p.Path.Get(n) != objectNode) continue;
                    p.Path.Set(n, probeId);
                    return doc.Text;
                }
            }
            if (graph.Wire("root") == objectNode || !slot.IsBody)
            {
                graph.Set("root", JValue.Of(probeId), GraphSchema.ValueOrder);
                return doc.Text;
            }
            // A statement of its own, after the body's last
            var eval = doc.AddNode(graph, "eval", 0, 0);
            eval.Set("value", JValue.Of(probeId), GraphSchema.NodeOrder("eval"));
            var last = LastStatement(graph);
            if (last == null) graph.Set("entry", JValue.Of(eval.Wire("id")), GraphSchema.BodyOrder);
            else last.Set("next", JValue.Of(eval.Wire("id")), GraphSchema.NodeOrder(last.Str("k")));
            return doc.Text;
        }

        // The body's last statement along next from its start
        static JValue LastStatement(JValue graph)
        {
            JValue last = null;
            var seen = new HashSet<int>();
            for (var id = graph.Wire("entry"); id != 0 && seen.Add(id);)
            {
                var n = GraphDocument.NodeIn(graph, id);
                if (n == null) break;
                last = n;
                id = n.Wire("next");
            }
            return last;
        }

        // Where in the graph's Tide a node made by the search would go, as a byte offset: after the statement a
        // flow comes from, before the statement it goes to, at the statement a value is for; else at the body's end
        internal static int Place(GraphSearchContext c, GraphText t)
        {
            var graph = c.Graph;
            var p = c.Pending;
            int Start(int id) => t.Spans.TryGetValue(id, out var s) ? s.start : -1;
            if (p != null && p.Exec && !p.Input)
            {
                if (p.Node == GraphNodeView.EntryId) return graph.Wire("entry") != 0 && Start(graph.Wire("entry")) >= 0 ? Start(graph.Wire("entry")) : BodyStart(c, t);
                if (t.Spans.TryGetValue(p.Node, out var s))
                {
                    var key = p.Path?.Parts[0] as string;
                    if (key == "next" || key == null) return s.end;
                    var n = GraphDocument.NodeIn(graph, p.Node);
                    var first = n != null ? p.Path.Get(n) : 0;
                    if (first != 0 && Start(first) >= 0) return Start(first);
                    var brace = key == "else" ? t.LastIndexOf('{', s.start, s.end) : t.IndexOf('{', s.start, s.end);
                    return brace >= 0 ? brace + 1 : s.end;
                }
            }
            else if (p != null && p.Exec && p.Input)
            {
                if (Start(p.Node) >= 0) return Start(p.Node);
            }
            else if (p != null && p.Node > 0)
            {
                var statement = StatementOf(graph, t, p.Node);
                if (statement >= 0) return statement;
            }
            if (!c.Slot.IsBody)
            {
                var root = graph.Wire("root");
                return root != 0 ? Start(root) : -1;
            }
            var last = LastStatement(graph);
            if (last != null && t.Spans.TryGetValue(last.Wire("id"), out var ls)) return ls.end;
            return BodyStart(c, t);
        }

        // Just inside the braces of the body's routine
        static int BodyStart(GraphSearchContext c, GraphText t)
        {
            if (!t.Spans.TryGetValue(c.Slot.Owner, out var s)) return -1;
            var brace = t.IndexOf('{', s.start, s.end);
            return brace >= 0 ? brace + 1 : -1;
        }

        // The start of the innermost statement whose Tide holds a node's
        static int StatementOf(JValue graph, GraphText t, int node)
        {
            if (!t.Spans.TryGetValue(node, out var inner)) return -1;
            var best = -1;
            var size = int.MaxValue;
            foreach (var n in GraphDocument.Nodes(graph).Items)
            {
                if (!GraphSchema.IsExec(n.Str("k")) || !t.Spans.TryGetValue(n.Wire("id"), out var s)) continue;
                if (s.start <= inner.start && inner.end <= s.end && s.end - s.start < size)
                {
                    size = s.end - s.start;
                    best = s.start;
                }
            }
            return best;
        }

        // Completion's kinds (LSP's CompletionItemKind)
        const int KindMethod = 2, KindFunction = 3, KindField = 5, KindVariable = 6, KindClass = 7, KindInterface = 8, KindModule = 9,
            KindProperty = 10, KindEnum = 13, KindKeyword = 14, KindSnippet = 15, KindEnumMember = 20, KindConstant = 21, KindStruct = 22, KindEvent = 23;

        static IEnumerable<JValue> Items(JValue completion)
        {
            var items = completion?.Arr("items") ?? (completion != null && completion.IsArray ? completion : null);
            return items?.Items ?? Enumerable.Empty<JValue>();
        }

        // After `object.`: its fields as member nodes, its methods as method nodes with their parameters as inputs
        static List<GraphSearchEntry> Members(GraphSearchContext c, JValue completion, int objectNode, string type)
        {
            var list = new List<GraphSearchEntry>();
            if (type == null) c.Window?.Checked?.Types.TryGetValue(objectNode, out type);
            var group = type != null ? $"Members of {type}" : "Members";
            foreach (var item in Items(completion))
            {
                var label = item.Str("label");
                if (string.IsNullOrEmpty(label) || label == Probe) continue;
                var kind = (int)(item.Get("kind")?.Number ?? 0);
                if (kind == KindKeyword || kind == KindSnippet) continue;
                var detail = item.Str("detail") ?? "";
                var callable = kind == KindMethod || kind == KindFunction || (item.Str("insertText") ?? "").Contains("(");
                if (callable)
                {
                    var names = Parameters(detail, label);
                    list.Add(new GraphSearchEntry
                    {
                        Group = group, Title = $"{label}({string.Join(", ", names)})", Keywords = "method " + detail, Detail = detail, Priority = 2,
                        Make = (doc, graph, at) => NewMethod(doc, graph, at, label, names.Count),
                        Apply = n => Retarget(n, "method", label, names.Count),
                    });
                }
                else
                {
                    list.Add(new GraphSearchEntry
                    {
                        Group = group, Title = label, Keywords = "member field " + detail, Detail = detail, Priority = 2,
                        Make = (doc, graph, at) =>
                        {
                            var n = doc.AddNode(graph, "member", at.x, at.y);
                            n.Set("member", JValue.Of(label), GraphSchema.NodeOrder("member"));
                            return n;
                        },
                        Apply = n => Retarget(n, "member", label, 0),
                    });
                }
            }
            return list;
        }

        static JValue NewMethod(GraphDocument doc, JValue graph, Vector2 at, string name, int args)
        {
            var n = doc.AddNode(graph, "method", at.x, at.y);
            var order = GraphSchema.NodeOrder("method");
            n.Set("name", JValue.Of(name), order);
            if (args > 0) n.Set("args", JValue.ArrayOf(Enumerable.Range(0, args).Select(_ => JValue.Null())), order);
            return n;
        }

        static JValue NewCall(GraphDocument doc, JValue graph, Vector2 at, string name, int args)
        {
            var n = doc.AddNode(graph, "call", at.x, at.y);
            var order = GraphSchema.NodeOrder("call");
            n.Set("name", JValue.Of(name), order);
            if (args > 0) n.Set("args", JValue.ArrayOf(Enumerable.Range(0, args).Select(_ => JValue.Null())), order);
            return n;
        }

        // A member or method node made the member or method picked, its object and place kept, the arguments it
        // has kept as far as there are as many
        static void Retarget(JValue n, string k, string name, int args)
        {
            var order = GraphSchema.NodeOrder(k);
            var kept = n.Arr("args");
            n.Set("k", JValue.Of(k));
            if (k == "member")
            {
                foreach (var key in new[] { "name", "typeArgs", "args", "argNames" }) n.Remove(key);
                n.Set("member", JValue.Of(name), order);
                return;
            }
            n.Remove("member");
            n.Remove("shorthand");
            n.Remove("argNames");
            n.Set("name", JValue.Of(name), order);
            var list = JValue.NewArray();
            for (var i = 0; i < args; i++) list.Items.Add(kept != null && i < kept.Items.Count ? kept.Items[i] : JValue.Null());
            if (args > 0) n.Set("args", list, order);
            else n.Remove("args");
        }

        // At a place: the names in scope, the game's functions and types, constants, enum members, the built-in
        // namespaces, as name, call and literal nodes
        static List<GraphSearchEntry> Names(GraphSearchContext c, JValue completion)
        {
            var list = new List<GraphSearchEntry>();
            var locals = new HashSet<string>(GraphDocument.Locals(c.Graph).Select(GraphSchema.LocalName));
            var statement = c.Body && (c.Pending == null || c.Pending.Exec);
            foreach (var item in Items(completion))
            {
                var label = item.Str("label");
                if (string.IsNullOrEmpty(label)) continue;
                var kind = (int)(item.Get("kind")?.Number ?? 0);
                var detail = item.Str("detail") ?? "";
                switch (kind)
                {
                    case KindKeyword:
                    case KindSnippet:
                        continue; // The catalog's statements and values
                    case KindFunction:
                    case KindMethod:
                    {
                        var names = Parameters(detail, label);
                        var title = $"{label}({string.Join(", ", names)})";
                        if (statement)
                            list.Add(new GraphSearchEntry
                            {
                                Group = "Functions", Title = title, Keywords = "call function " + detail, Detail = detail, Statement = true,
                                Make = (doc, graph, at) =>
                                {
                                    var call = NewCall(doc, graph, at - new Vector2(220, -60), label, names.Count);
                                    var eval = doc.AddNode(graph, "eval", at.x, at.y);
                                    eval.Set("value", JValue.Of(call.Wire("id")), GraphSchema.NodeOrder("eval"));
                                    return eval;
                                },
                            });
                        else
                            list.Add(new GraphSearchEntry
                            {
                                Group = "Functions", Title = title, Keywords = "call function " + detail, Detail = detail,
                                Make = (doc, graph, at) => NewCall(doc, graph, at, label, names.Count),
                            });
                        break;
                    }
                    case KindVariable:
                    case KindField:
                    case KindProperty:
                        if (locals.Contains(label)) continue; // The catalog reads the graph's locals
                        list.Add(Name("Names", label, detail));
                        break;
                    case KindConstant:
                        list.Add(Name("Constants", label, detail));
                        break;
                    case KindModule:
                        list.Add(Name("Namespaces", label, detail));
                        break;
                    case KindEnum:
                        list.Add(Name("Types", label, detail));
                        break;
                    case KindEnumMember:
                        list.Add(new GraphSearchEntry
                        {
                            Group = "Enum members", Title = "." + label, Keywords = "enum member shorthand " + label + " " + detail, Detail = detail,
                            Make = (doc, graph, at) =>
                            {
                                var n = doc.AddNode(graph, "member", at.x, at.y);
                                var order = GraphSchema.NodeOrder("member");
                                n.Set("member", JValue.Of(label), order);
                                n.SetFlag("shorthand", true, order);
                                return n;
                            },
                        });
                        break;
                    case KindStruct:
                    case KindClass:
                    case KindInterface:
                    case KindEvent:
                        // The game's structs, components and events as values made of them; built-in types by name
                        var what = detail.StartsWith("local ") ? detail.Substring(6) : detail;
                        if (what == "struct" || what == "component" || what == "event" || kind == KindEvent)
                            list.Add(new GraphSearchEntry
                            {
                                Group = "Types", Title = label + " { }", Keywords = "literal value type struct " + label + " " + detail, Detail = detail,
                                Make = (doc, graph, at) =>
                                {
                                    var n = doc.AddNode(graph, "literal", at.x, at.y);
                                    n.Set("type", JValue.Of(label), GraphSchema.NodeOrder("literal"));
                                    return n;
                                },
                            });
                        else list.Add(Name("Types", label, detail));
                        break;
                    default:
                        list.Add(Name("Names", label, detail));
                        break;
                }
            }
            return list;
        }

        static GraphSearchEntry Name(string group, string label, string detail)
            => new GraphSearchEntry
            {
                Group = group, Title = label, Keywords = "name " + detail, Detail = detail,
                Make = (doc, graph, at) =>
                {
                    var n = doc.AddNode(graph, "name", at.x, at.y);
                    n.Set("name", JValue.Of(label), GraphSchema.NodeOrder("name"));
                    return n;
                },
            };

        // The parameters' names of a signature, `void Bounce(float height, int times = 1)` → height, times
        internal static List<string> Parameters(string detail, string name)
        {
            var names = new List<string>();
            if (string.IsNullOrEmpty(detail)) return names;
            detail = detail.Split('\n')[0];
            var at = detail.IndexOf(name + "(", StringComparison.Ordinal);
            var open = at >= 0 ? at + name.Length : detail.IndexOf('(', Math.Max(0, detail.IndexOf(name, StringComparison.Ordinal)));
            if (open < 0) return names;
            var depth = 0;
            var part = new StringBuilder();
            for (var i = open + 1; i < detail.Length; i++)
            {
                var ch = detail[i];
                if (ch == '(' || ch == '<' || ch == '[' || ch == '{') depth++;
                if ((ch == ')' || ch == '>' || ch == ']' || ch == '}') && depth > 0)
                {
                    depth--;
                    part.Append(ch);
                    continue;
                }
                if (ch == ')' || (ch == ',' && depth == 0))
                {
                    var p = ParameterName(part.ToString());
                    if (p != null) names.Add(p);
                    part.Clear();
                    if (ch == ')') break;
                    continue;
                }
                part.Append(ch);
            }
            return names;
        }

        static string ParameterName(string p)
        {
            var eq = p.IndexOf('=');
            if (eq >= 0) p = p.Substring(0, eq);
            p = p.Replace("...", "").Trim();
            if (p.Length == 0) return null;
            var end = p.Length;
            var start = end;
            while (start > 0 && (char.IsLetterOrDigit(p[start - 1]) || p[start - 1] == '_')) start--;
            return start < end ? p.Substring(start, end - start) : p;
        }
    }

    // tidels's completion, as the search's entries: asked for each search, answered when it's back
    sealed class TidelsGraphSearch : ITideGraphSearch, ITideGraphLiveSearch
    {
        public IEnumerable<GraphSearchEntry> Entries(GraphSearchContext context) => Enumerable.Empty<GraphSearchEntry>();

        public bool Asks(GraphSearchContext context) => context.Window != null && TideLanguageClient.Shared != null && TideLanguageClient.Shared.Usable
                                                        && context.Window.Checked?.Checker == "tidels";

        public void Ask(GraphSearchContext context, Action<List<GraphSearchEntry>, string> found) => TideGraphLanguage.Complete(context, found);
    }

    // tidels's checks, and tidec's where there's no tidels or it fails
    sealed class TideGraphChecker : ITideGraphChecker
    {
        readonly TidecGraphChecker tidec = new TidecGraphChecker();

        public void Check(string path, string json, Action<GraphCheckResult> done)
        {
            var client = TideLanguageClient.Get(out var why);
            if (client == null || !client.Usable)
            {
                Fallback(path, json, done, why ?? client?.Failure);
                return;
            }
            TideGraphLanguage.Check(client, path, json, done, failed => Fallback(path, json, done, failed));
        }

        void Fallback(string path, string json, Action<GraphCheckResult> done, string why)
        {
            tidec.Check(path, json, result =>
            {
                result.CheckerNote = why;
                done(result);
            });
        }

        public void Closed(string path)
        {
            if (string.IsNullOrEmpty(path)) return;
            TideLanguageClient.Shared?.Close(TideLanguageClient.UriOf(path));
        }
    }
}
