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
            client.Wanted(uri);
            client.Operate(finish =>
            {
                // Its window closed while it waited its turn: nobody reads the answer
                if (client.IsClosed(uri))
                {
                    finish();
                    return;
                }
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
                        if (types != null && types.IsObject)
                            for (var i = 0; i < types.Keys.Count; i++)
                                if (int.TryParse(types.Keys[i], out var id) && types.Values[i].Kind == JKind.String) result.Types[id] = types.Values[i].String;
                        // What each call's arguments are called and take, and which names are the game's own: a
                        // language server that doesn't answer these leaves them unknown
                        client.Ask("tide/graphCalls", uri, -1, (calls, why3) =>
                        {
                            Calls(result, calls);
                            client.Ask("tide/graphNames", uri, -1, (names, why4) =>
                            {
                                finish();
                                Names(result, names);
                                Diagnostics(result, client.DiagnosticsOf(uri));
                                done(result);
                            });
                        });
                    });
                });
            });
        }

        static void Calls(GraphCheckResult result, JValue calls)
        {
            if (calls == null || !calls.IsObject) return;
            for (var i = 0; i < calls.Keys.Count; i++)
            {
                var c = calls.Values[i];
                if (!int.TryParse(calls.Keys[i], out var id) || !c.IsObject) continue;
                result.Calls[id] = new GraphCall
                {
                    Params = (c.Arr("params")?.Items ?? new List<JValue>()).Select(p => p.String ?? "").ToArray(),
                    Types = (c.Arr("types")?.Items ?? new List<JValue>()).Select(p => p.String ?? "").ToArray(),
                    Args = (c.Arr("args")?.Items ?? new List<JValue>()).Select(p => p.Kind == JKind.Number ? (int)p.Number : -1).ToArray(),
                };
            }
        }

        static void Names(GraphCheckResult result, JValue names)
        {
            if (names == null || !names.IsObject || names.Get("game") == null) return;
            Dictionary<string, string> Of(JValue o)
            {
                var found = new Dictionary<string, string>();
                for (var i = 0; o != null && o.IsObject && i < o.Keys.Count; i++) found[o.Keys[i]] = o.Values[i].String ?? "";
                return found;
            }
            result.Game = Of(names.Get("game"));
            result.Packages = Of(names.Get("packages"));
        }

        static void Diagnostics(GraphCheckResult result, JValue list)
        {
            if (list == null) return;
            foreach (var d in list.Items)
            {
                var severity = (int)(d.Get("severity")?.Number ?? 1);
                var data = d.Get("data");
                var id = data?.Wire("node") ?? 0;
                var diagnostic = new GraphDiagnostic
                {
                    Id = id,
                    Severity = severity == 1 ? "error" : severity == 2 ? "warning" : "note",
                    Message = d.Str("message") ?? "",
                    Port = data?.Str("port"),
                    Index = data?.Get("index")?.Kind == JKind.Number ? (int)data.Get("index").Number : -1,
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
            var n = window.Doc?.Get(node);
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
        // Layout

        // Where tidec's layout puts a graph's nodes (tide/graphLayout, the one tidec's converter uses): `done` gets
        // {"<id>": [x, y]}, or null and why, on the editor's thread
        public static void Layout(JValue graph, Action<JValue, string> done)
        {
            var client = TideLanguageClient.Get(out var why);
            if (client == null || !client.Usable)
            {
                done(null, why ?? client?.Failure ?? "tidels isn't running");
                return;
            }
            var p = JValue.NewObject();
            p.Set("graph", graph.Clone());
            client.Request("tide/graphLayout", p, (r, why2) =>
            {
                if (r != null && r.IsObject) done(r, null);
                else done(null, why2 ?? "tidels couldn't lay the graph out");
            });
        }

        // Where tidec's layout puts a file's declarations on the board (tide/graphLayout with the file: data in a
        // column on the left, code to its right): `done` gets {"<id>": [x, y]}, or null and why
        public static void LayoutBoard(JValue file, Action<JValue, string> done)
        {
            var client = TideLanguageClient.Get(out var why);
            if (client == null || !client.Usable)
            {
                done(null, why ?? client?.Failure ?? "tidels isn't running");
                return;
            }
            var p = JValue.NewObject();
            p.Set("file", file.Clone());
            client.Request("tide/graphLayout", p, (r, why2) =>
            {
                if (r != null && r.IsObject) done(r, null);
                else done(null, why2 ?? "tidels couldn't lay the board out");
            });
        }

        // ---------------------------------------------------------------------------------------------------------
        // What's valid at a place: the types that can go where a type goes, the words a parameter can start with

        // The language server, when it's the one checking a window's graph
        static TideLanguageClient ClientOf(TideGraphWindow window)
        {
            var client = TideLanguageClient.Shared;
            if (window == null || window.Doc == null || client == null || !client.Usable || string.IsNullOrEmpty(window.Path)) return null;
            return window.Checked == null || window.Checked.Checker == "tidels" ? client : null;
        }

        static int Find(GraphText t, string what, int from, int to, bool whole)
        {
            var bytes = Encoding.UTF8.GetBytes(what);
            if (bytes.Length == 0) return -1;
            bool Word(int i) => i >= 0 && i < t.Bytes.Length && (char.IsLetterOrDigit((char)t.Bytes[i]) || t.Bytes[i] == '_');
            for (var i = Math.Max(0, from); i + bytes.Length <= Math.Min(to, t.Bytes.Length); i++)
            {
                var same = true;
                for (var j = 0; same && j < bytes.Length; j++) same = t.Bytes[i + j] == bytes[j];
                if (!same) continue;
                if (whole && (Word(i - 1) || Word(i + bytes.Length))) continue;
                return i;
            }
            return -1;
        }

        // A place's byte offset in the graph's Tide, or -1 when the Tide hasn't it
        static int OffsetOf(GraphText t, GraphTypePlace place)
        {
            if (place == null || !t.Spans.TryGetValue(place.Element, out var span)) return -1;
            if (place.After != null)
            {
                var at = Find(t, place.After, span.start, span.end, false);
                return at < 0 ? -1 : at + Encoding.UTF8.GetByteCount(place.After);
            }
            return string.IsNullOrEmpty(place.Find) ? -1 : Find(t, place.Find, span.start, span.end, true);
        }

        static List<GraphTypeInfo> TypeItems(JValue completion)
        {
            var found = new List<GraphTypeInfo>();
            foreach (var item in Items(completion))
            {
                var kind = (int)(item.Get("kind")?.Number ?? 0);
                if (kind != KindStruct && kind != KindEnum && kind != KindClass && kind != KindInterface && kind != KindTypeParameter && kind != KindEvent) continue;
                var label = item.Str("label");
                if (string.IsNullOrEmpty(label) || label.StartsWith("_TideGraphProbe")) continue;
                var info = new GraphTypeInfo { Name = label, Detail = item.Str("detail") ?? "", Doc = item.Get("documentation")?.Str("value"), Kind = kind };
                // What a generic type is of, from what completion would write: `Pair<${1:T}>`, `Map<$1, $2>`
                var insert = item.Str("insertText") ?? "";
                var open = insert.IndexOf('<');
                if (open > 0 && insert.EndsWith(">"))
                    foreach (var part in GraphLists.Split(insert.Substring(open + 1, insert.Length - open - 2)))
                    {
                        var name = part.Trim().TrimStart('$').Trim('{', '}');
                        var colon = name.IndexOf(':');
                        info.Params.Add(colon >= 0 ? name.Substring(colon + 1) : "type " + (info.Params.Count + 1));
                    }
                found.Add(info);
            }
            return found;
        }

        // The types that can go at a place of a window's graph, as completion lists them there in the graph's Tide.
        // A place that lists none (a function's result, something not written yet) is asked as a field's, in a copy
        // of the file with a field to ask at. `done` gets them, or null and why, on the editor's thread.
        public static void Types(TideGraphWindow window, GraphTypePlace place, Action<List<GraphTypeInfo>, string> done)
        {
            var client = ClientOf(window);
            if (client == null)
            {
                done(null, "the language server isn't checking this file");
                return;
            }
            var uri = UriOf(window.Path);
            var text = window.Doc.Text;
            client.Operate(finish =>
            {
                void End(List<GraphTypeInfo> found, string why)
                {
                    finish();
                    done(found, why);
                }
                // Its window closed while this waited its turn: the file isn't opened again for an answer nobody reads
                if (window == null)
                {
                    End(null, "the window closed");
                    return;
                }
                if (!client.SetText(uri, text))
                {
                    End(null, client.Failure ?? "tidels can't be started");
                    return;
                }
                void Probe()
                {
                    var copy = GraphDocument.Parse(text);
                    var probe = copy.NewDecl("struct", "_TideGraphProbe");
                    var field = copy.NewMember("field", "probe");
                    probe.Arr("members").Items.Add(field);
                    copy.Decls.Items.Add(probe);
                    var fieldId = field.Wire("id");
                    if (!client.SetText(uri, copy.Text))
                    {
                        End(null, client.Failure);
                        return;
                    }
                    client.Ask("tide/graphText", uri, -1, (r, why) =>
                    {
                        var t = GraphText.Of(r);
                        if (t == null || !t.Spans.TryGetValue(fieldId, out var span))
                        {
                            client.SetText(uri, text);
                            End(null, why);
                            return;
                        }
                        client.Ask("tide/graphCompletion", uri, span.start, (items, why2) =>
                        {
                            client.SetText(uri, text);
                            End(TypeItems(items), why2);
                        });
                    });
                }
                if (place == null)
                {
                    Probe();
                    return;
                }
                client.Ask("tide/graphText", uri, -1, (r, why) =>
                {
                    var t = GraphText.Of(r);
                    var offset = t == null ? -1 : OffsetOf(t, place);
                    if (offset < 0)
                    {
                        Probe();
                        return;
                    }
                    client.Ask("tide/graphCompletion", uri, offset, (items, why2) =>
                    {
                        var found = TypeItems(items);
                        if (found.Count == 0) Probe();
                        else End(found, null);
                    });
                });
            });
        }

        // The words completion offers at a place (mut, with, without, query, out…): which ways of taking a
        // parameter a routine's kind has. `done` gets null when it can't be asked.
        public static void Words(TideGraphWindow window, GraphTypePlace place, Action<HashSet<string>> done)
        {
            var client = ClientOf(window);
            if (client == null)
            {
                done(null);
                return;
            }
            var uri = UriOf(window.Path);
            var text = window.Doc.Text;
            client.Operate(finish =>
            {
                if (window == null || !client.SetText(uri, text))
                {
                    finish();
                    done(null);
                    return;
                }
                client.Ask("tide/graphText", uri, -1, (r, why) =>
                {
                    var t = GraphText.Of(r);
                    var offset = t == null ? -1 : OffsetOf(t, place);
                    if (offset < 0)
                    {
                        finish();
                        done(null);
                        return;
                    }
                    client.Ask("tide/graphCompletion", uri, offset, (items, why2) =>
                    {
                        finish();
                        var words = new HashSet<string>();
                        foreach (var item in Items(items))
                        {
                            var kind = (int)(item.Get("kind")?.Number ?? 0);
                            if ((kind == KindKeyword || kind == KindSnippet) && item.Str("label") != null) words.Add(item.Str("label"));
                        }
                        done(words.Count > 0 ? words : null);
                    });
                });
            });
        }

        // Something a type has: an enum's member, a struct's field
        public sealed class Member
        {
            public string Label, Detail = "";
            public int Kind;
            public bool Call; // Completion would write it with brackets

            public bool Callable => Kind == KindMethod || Kind == KindFunction || Call;
            public List<string> Parameters => TideGraphLanguage.Parameters(Detail, Label);

            // What calling it gives, from its signature: `float Twice(float amount)`, `Random.NextInt() -> int`
            public string Result
            {
                get
                {
                    var first = (Detail ?? "").Split('\n')[0];
                    var arrow = first.LastIndexOf(") -> ", StringComparison.Ordinal);
                    if (arrow >= 0)
                    {
                        var result = first.Substring(arrow + 5);
                        var versions = result.IndexOf(" (", StringComparison.Ordinal);
                        return (versions >= 0 ? result.Substring(0, versions) : result).Trim();
                    }
                    var name = first.IndexOf(Label + "(", StringComparison.Ordinal);
                    if (name <= 0) return null;
                    var before = first.Substring(0, name).Trim();
                    // `Math.Sin(x)`: what it gives depends on what it's given, and isn't said
                    if (before.EndsWith(".")) return null;
                    var space = before.IndexOf('<') < 0 ? before.LastIndexOf(' ') : -1;
                    return space >= 0 ? before.Substring(space + 1) : before;
                }
            }

            // The type of something that isn't called: a field's is its detail (`float`), a constant's follows
            // its name (`Math.PI: float`)
            public string Type
            {
                get
                {
                    var first = (Detail ?? "").Split('\n')[0].Trim();
                    var colon = first.LastIndexOf(": ", StringComparison.Ordinal);
                    if (colon >= 0) return first.Substring(colon + 2).Trim();
                    return first.Length > 0 && first.IndexOf(' ') < 0 ? first : null;
                }
            }

            public static Member Of(JValue item) => new Member
            {
                Label = item.Str("label"), Detail = item.Str("detail") ?? "", Kind = (int)(item.Get("kind")?.Number ?? 0), Call = (item.Str("insertText") ?? "").Contains("("),
            };
        }

        // What completion lists where the value of one of a node's inputs would be written (the statement the node
        // is in): the names in scope there, the game's constants and functions, the engine's namespaces. `done`
        // gets null, and why, when it can't be asked.
        public static void ValuesAt(TideGraphWindow window, GraphSlot slot, int node, Action<List<Member>, string> done)
        {
            var client = ClientOf(window);
            if (client == null)
            {
                done(null, "the language server isn't checking this file");
                return;
            }
            var uri = UriOf(window.Path);
            var text = window.Doc.Text;
            var context = new GraphSearchContext
            {
                Doc = window.Doc, Graph = window.Doc.GetGraph(slot), Slot = slot, Window = window,
                Pending = node > 0 ? new GraphPort { Node = node, Input = true, Path = new WirePath("value") } : null,
            };
            if (context.Graph == null)
            {
                done(null, null);
                return;
            }
            client.Operate(finish =>
            {
                if (window == null || !client.SetText(uri, text))
                {
                    finish();
                    done(null, "the window closed");
                    return;
                }
                client.Ask("tide/graphText", uri, -1, (r, why) =>
                {
                    var t = GraphText.Of(r);
                    var offset = t == null ? -1 : Place(context, t);
                    if (offset < 0)
                    {
                        finish();
                        done(null, why);
                        return;
                    }
                    client.Ask("tide/graphCompletion", uri, offset, (items, why2) =>
                    {
                        finish();
                        done(Items(items).Where(i => !string.IsNullOrEmpty(i.Str("label"))).Select(Member.Of).ToList(), why2);
                    });
                });
            });
        }

        // What a name itself has after its dot (a namespace's functions and constants, an enum's choices, a
        // type's own constants), by the name: tide/graphMembers with {"name"}
        public static void MembersOfName(TideGraphWindow window, string name, Action<List<Member>, string> done)
        {
            var client = ClientOf(window);
            name = (name ?? "").TrimEnd('?');
            if (client == null || name.Length == 0 || name.IndexOfAny(new[] { '<', '[' }) >= 0)
            {
                done(null, "the language server isn't checking this file");
                return;
            }
            var uri = UriOf(window.Path);
            var text = window.Doc.Text;
            client.Operate(finish =>
            {
                if (window == null || !client.SetText(uri, text))
                {
                    finish();
                    done(null, "the window closed");
                    return;
                }
                var parameters = JValue.NewObject();
                parameters.Set("name", JValue.Of(name));
                client.Ask("tide/graphMembers", uri, parameters, (items, why) =>
                {
                    finish();
                    done(Items(items).Where(i => !string.IsNullOrEmpty(i.Str("label"))).Select(Member.Of).ToList(), why);
                });
            });
        }

        // What a value of a type has (a struct's fields, a vector's parts), by the type's name: asked with {"type"}
        public static void MembersOfType(TideGraphWindow window, string type, Action<List<Member>, string> done)
        {
            var client = ClientOf(window);
            var name = (type ?? "").TrimEnd('?');
            if (client == null || name.Length == 0 || name.IndexOfAny(new[] { '<', '[' }) >= 0)
            {
                done(null, "the language server isn't checking this file");
                return;
            }
            var uri = UriOf(window.Path);
            var text = window.Doc.Text;
            client.Operate(finish =>
            {
                if (window == null || !client.SetText(uri, text))
                {
                    finish();
                    done(null, "the window closed");
                    return;
                }
                var parameters = JValue.NewObject();
                parameters.Set("type", JValue.Of(name));
                client.Ask("tide/graphMembers", uri, parameters, (items, why) =>
                {
                    finish();
                    var found = new List<Member>();
                    foreach (var item in Items(items))
                        if (!string.IsNullOrEmpty(item.Str("label"))) found.Add(Member.Of(item));
                    done(found, why);
                });
            });
        }

        // Moves a graph's nodes where a layout puts them, and nothing else: how many it moved
        public static int Place(JValue graph, JValue layout)
        {
            var moved = 0;
            foreach (var n in GraphDocument.Nodes(graph).Items)
            {
                var at = layout?.Arr(n.Wire("id").ToString(System.Globalization.CultureInfo.InvariantCulture));
                if (at == null || at.Items.Count < 2 || at.Items[0].Kind != JKind.Number || at.Items[1].Kind != JKind.Number) continue;
                n.Set("x", JValue.Of(at.Items[0].Number));
                n.Set("y", JValue.Of(at.Items[1].Number));
                moved++;
            }
            return moved;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Completion

        // What completion offers where a search is made: the members of a value (tide/graphMembers), or what's at
        // its place
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
            string restore = null;
            if (c.Retarget != 0 && objectNode == 0)
            {
                found(null, "Wire what it's of to see what that has");
                return;
            }
            // A wire from a parameter's pin: what its type has, asked by the type's name
            var typeOfRead = c.Pending != null && c.Pending.IsRead ? ReadType(c) : null;
            if (c.Pending != null && c.Pending.IsRead && typeOfRead == null)
            {
                // A wire from a parameter's or a local's pin: nothing reads it yet, so a copy of the file with a
                // node that does is asked about, and the file is put back
                var copy = GraphDocument.Parse(text);
                var graph = copy.GetGraph(c.Slot);
                if (graph != null)
                {
                    var read = copy.AddNode(graph, c.Pending.Local ? "local" : "name", 0, 0);
                    if (c.Pending.Local) read.Set("ref", JValue.Of(c.Pending.Node), GraphSchema.NodeOrder("local"));
                    else read.Set("name", JValue.Of(c.Pending.Name), GraphSchema.NodeOrder("name"));
                    objectNode = read.Wire("id");
                    restore = text;
                    text = copy.Text;
                }
            }
            client.Operate(finish =>
            {
                void Fail(string why)
                {
                    finish();
                    found(null, why);
                }
                if (window == null)
                {
                    Fail("the window closed");
                    return;
                }
                if (!client.SetText(uri, text))
                {
                    Fail(client.Failure ?? "tidels can't be started");
                    return;
                }
                if (objectNode != 0 || typeOfRead != null)
                {
                    // What the value has: tidels reads the graph with a '.' after it, as after `value.` in a .tide
                    // file; or, for a pin nothing reads yet, what a value of its type has
                    var parameters = JValue.NewObject();
                    if (typeOfRead != null) parameters.Set("type", JValue.Of(typeOfRead));
                    else parameters.Set("node", JValue.Of(objectNode));
                    client.Ask("tide/graphMembers", uri, parameters, (items, why) =>
                    {
                        if (restore != null) client.SetText(uri, restore);
                        finish();
                        found(Members(c, items, objectNode, items?.Str("type") ?? typeOfRead), why);
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

        // The declared type of what a wire was dragged from, when it's a parameter's or a local's pin with one
        static string ReadType(GraphSearchContext c)
        {
            var p = c.Pending;
            string type;
            if (p.Local) type = GraphDocument.NodeIn(c.Graph, p.Node)?.Str("type");
            else type = GraphDocument.ReadsOf(c.Doc.RoutineOf(c.Slot)).FirstOrDefault(r => r.Name == p.Name)?.Type;
            return string.IsNullOrEmpty(type) ? null : type;
        }

        // The node whose members a search is for: the one a retargeted member's object is, or the value a wire
        // was dropped from
        static int ObjectOf(GraphSearchContext c)
        {
            if (c.Retarget != 0) return GraphDocument.NodeIn(c.Graph, c.Retarget)?.Wire("object") ?? 0;
            var p = c.Pending;
            if (p == null || p.Input || p.Exec || p.Node <= 0 || p.IsRead) return 0;
            return p.Node;
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
                if (p.Node == GraphFrame.EntryId) return graph.Wire("entry") != 0 && Start(graph.Wire("entry")) >= 0 ? Start(graph.Wire("entry")) : BodyStart(c, t);
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
        internal const int KindMethod = 2, KindFunction = 3, KindField = 5, KindVariable = 6, KindClass = 7, KindInterface = 8, KindModule = 9, KindTypeParameter = 25,
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
                if (string.IsNullOrEmpty(label)) continue;
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
