using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A node's own keys, edited: on the node itself (inline, compact) and in the inspector. Each kind's keys are
    // the notes' (notes/visual-scripting.md, Nodes); lists of inputs grow and shrink here.
    static class GraphNodeFields
    {
        public static List<VisualElement> Build(TideGraphWindow w, JValue graph, JValue n, bool inline)
        {
            var fields = new List<VisualElement>();
            var k = n.Str("k");
            var order = GraphSchema.NodeOrder(k);
            var doc = w.Doc;

            // A value edit redraws what else shows it; a change of ports redraws the node too
            void Val(string what, Action a) => w.Edit(what, a, inline ? GraphRedraw.Inspector : GraphRedraw.Canvas);
            void Struct(string what, Action a) => w.Edit(what, a, GraphRedraw.Canvas | GraphRedraw.Inspector);

            string L(string label) => inline ? null : label;

            VisualElement Text(string key, string label = null, bool multiline = false, string placeholder = null)
            {
                var f = GraphFields.Text(L(label ?? key), n.Str(key), v => Val("Set " + key, () => n.Set(key, JValue.Of(v), order)), multiline);
                if (placeholder != null) f.textEdition.placeholder = placeholder;
                f.tooltip = label ?? key;
                if (inline) f.style.minWidth = 60;
                return f;
            }

            VisualElement OptionalText(string key, string label = null, string placeholder = null)
            {
                var f = GraphFields.Text(L(label ?? key), n.Str(key), v => Val("Set " + key, () => n.SetString(key, v.Trim(), order)));
                if (placeholder != null) f.textEdition.placeholder = placeholder;
                f.tooltip = label ?? key;
                if (inline) f.style.minWidth = 60;
                return f;
            }

            VisualElement Type(string key, string placeholder = null, bool optional = true)
            {
                var t = GraphFields.Type(L(key), n.Str(key), doc, v => Val("Set " + key, () =>
                {
                    if (optional) n.SetString(key, v.Trim(), order);
                    else n.Set(key, JValue.Of(v.Trim()), order);
                }), placeholder);
                t.tooltip = key;
                return t;
            }

            VisualElement Flag(string key, string label = null, bool structural = false)
            {
                var t = new Toggle(label ?? key) { value = n.Flag(key), tooltip = key };
                t.Q<Label>().style.minWidth = 0;
                t.RegisterValueChangedCallback(e =>
                {
                    if (structural) Struct("Set " + key, () => n.SetFlag(key, e.newValue, order));
                    else Val("Set " + key, () => n.SetFlag(key, e.newValue, order));
                });
                return t;
            }

            VisualElement Op(IList<string> ops)
            {
                var p = GraphFields.Choice(L("op"), ops, n.Str("op"), v => Val("Set operator", () => n.Set("op", JValue.Of(v), order)));
                p.tooltip = "operator";
                return p;
            }

            VisualElement Buttons(params (string text, Action click)[] buttons)
            {
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row, flexWrap = Wrap.Wrap } };
                foreach (var (text, click) in buttons) row.Add(GraphFields.SmallButton(text, click));
                return row;
            }

            VisualElement Flags(params VisualElement[] items)
            {
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row, flexWrap = Wrap.Wrap } };
                foreach (var i in items) row.Add(i);
                return row;
            }

            // Adds and removes the last of a list of inputs
            VisualElement Grow(string key, string what, int max = int.MaxValue, Action<JValue> added = null, Action<JValue> removed = null)
            {
                var a = n.Arr(key);
                var count = a?.Items.Count ?? 0;
                var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                if (count < max)
                    row.Add(GraphFields.SmallButton("+ " + what, () => Struct("Add " + what, () =>
                    {
                        var list = n.Arr(key);
                        if (list == null) n.Set(key, list = JValue.NewArray(), order);
                        list.Items.Add(JValue.Null());
                        added?.Invoke(list);
                    })));
                if (count > 0)
                    row.Add(GraphFields.SmallButton("− " + what, () => Struct("Remove " + what, () =>
                    {
                        var list = n.Arr(key);
                        list.Items.RemoveAt(list.Items.Count - 1);
                        removed?.Invoke(list);
                    })));
                return row;
            }

            switch (k)
            {
                case "int":
                case "float":
                case "char":
                    fields.Add(Text("text", k));
                    break;
                case "string":
                    fields.Add(Text("text", "text", !inline));
                    break;
                case "bool":
                {
                    var t = new Toggle(inline ? "" : "value") { value = n.Flag("value") };
                    t.RegisterValueChangedCallback(e => Val("Set value", () => n.Set("value", JValue.Of(e.newValue), order)));
                    fields.Add(t);
                    break;
                }
                case "name":
                    fields.Add(Text("name"));
                    fields.Add(Flags(Flag("local")));
                    break;
                case "local":
                {
                    var locals = GraphDocument.Locals(graph).ToList();
                    var names = locals.Select(l => $"{GraphSchema.LocalName(l)} ({l.Wire("id")})").ToList();
                    var current = locals.FindIndex(l => l.Wire("id") == n.Wire("ref"));
                    if (current < 0) names.Insert(0, $"missing ({n.Wire("ref")})");
                    var p = GraphFields.Choice(L("reads"), names, names[Math.Max(current, 0)], v =>
                    {
                        var i = names.IndexOf(v) - (current < 0 ? 1 : 0);
                        if (i >= 0 && i < locals.Count) Struct("Read local", () => n.Set("ref", JValue.Of(locals[i].Wire("id")), order));
                    });
                    fields.Add(p);
                    break;
                }
                case "member":
                    fields.Add(Text("member"));
                    fields.Add(Flags(Flag("shorthand", ".Name", true)));
                    break;
                case "call":
                case "method":
                    fields.Add(Text("name"));
                    fields.Add(GraphFields.List(L("type args"), GraphFields.Strings(n.Arr("typeArgs")), list => Val("Type arguments", () =>
                    {
                        if (list.Count == 0) n.Remove("typeArgs");
                        else n.Set("typeArgs", JValue.Strings(list), order);
                    })));
                    if (k == "call") fields.Add(Flags(Flag("local")));
                    if (!inline)
                    {
                        var args = n.Arr("args");
                        for (var i = 0; args != null && i < args.Items.Count; i++)
                        {
                            var at = i;
                            fields.Add(GraphFields.Text($"arg {i + 1} name", ArgName(n, i), v => Val("Argument name", () => SetArgName(n, at, v.Trim()))));
                        }
                    }
                    fields.Add(Grow("args", "arg", int.MaxValue,
                        _ => { if (n.Arr("argNames") != null) n.Arr("argNames").Items.Add(JValue.Of("")); },
                        _ =>
                        {
                            var names = n.Arr("argNames");
                            if (names != null && names.Items.Count > 0) names.Items.RemoveAt(names.Items.Count - 1);
                            TidyArgNames(n);
                        }));
                    break;
                case "outVar":
                    fields.Add(Flags(Flag("mut")));
                    fields.Add(Type("type", "var"));
                    fields.Add(Text("name"));
                    break;
                case "binary":
                    fields.Add(Op(GraphSchema.BinaryOps.Select(o => o.op).ToList()));
                    break;
                case "unary":
                    fields.Add(Op(GraphSchema.UnaryOps));
                    break;
                case "step":
                    fields.Add(Op(GraphSchema.StepOps));
                    fields.Add(Flags(Flag("postfix", "after (x++)")));
                    break;
                case "switchExpr":
                    fields.Add(Buttons(("+ arm", () => Struct("Add arm", () => n.Arr("arms")?.Items.Add(GraphDocument.NewArm()))),
                        ("− arm", () => Struct("Remove arm", () =>
                        {
                            var arms = n.Arr("arms");
                            if (arms != null && arms.Items.Count > 0) arms.Items.RemoveAt(arms.Items.Count - 1);
                        }))));
                    break;
                case "interp":
                {
                    var parts = n.Arr("parts");
                    var formats = n.Arr("formats");
                    for (var i = 0; parts != null && i < parts.Items.Count; i++)
                    {
                        var at = i;
                        fields.Add(GraphFields.Text(L($"text {i + 1}"), parts.Items[i].String, v => Val("Text", () => n.Arr("parts").Items[at] = JValue.Of(v))));
                        if (i < parts.Items.Count - 1)
                        {
                            var format = formats != null && i < formats.Items.Count ? formats.Items[i].String : "";
                            var f = GraphFields.Text(L($"format {i + 1}"), format, v => Val("Format", () => SetFormat(n, at, v.Trim())));
                            f.textEdition.placeholder = "format";
                            fields.Add(f);
                        }
                    }
                    fields.Add(Buttons(("+ value", () => Struct("Add value", () =>
                        {
                            n.Arr("values")?.Items.Add(JValue.Null());
                            n.Arr("parts")?.Items.Add(JValue.Of(""));
                            n.Arr("formats")?.Items.Add(JValue.Of(""));
                        })),
                        ("− value", () => Struct("Remove value", () =>
                        {
                            var values = n.Arr("values");
                            var p = n.Arr("parts");
                            if (values == null || values.Items.Count == 0 || p == null || p.Items.Count < 2) return;
                            values.Items.RemoveAt(values.Items.Count - 1);
                            var last = p.Items[p.Items.Count - 1].String;
                            p.Items.RemoveAt(p.Items.Count - 1);
                            p.Items[p.Items.Count - 1] = JValue.Of(p.Items[p.Items.Count - 1].String + last);
                            var f = n.Arr("formats");
                            if (f != null && f.Items.Count > values.Items.Count) f.Items.RemoveAt(f.Items.Count - 1);
                            if (f != null && f.Items.All(x => string.IsNullOrEmpty(x.String))) n.Remove("formats");
                        }))));
                    break;
                }
                case "index":
                    fields.Add(Grow("index", "index", 3));
                    break;
                case "list":
                    fields.Add(Grow("items", "item"));
                    break;
                case "literal":
                {
                    fields.Add(Type("type", null, false));
                    var inits = n.Arr("inits");
                    for (var i = 0; inits != null && i < inits.Items.Count; i++)
                    {
                        var init = inits.Items[i];
                        var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                        var f = GraphFields.Text(L("field"), init.Str("name"), v => Struct("Field name", () => init.Set("name", JValue.Of(v.Trim()), GraphSchema.InitOrder)));
                        f.style.flexGrow = 1;
                        row.Add(f);
                        row.Add(GraphFields.SmallButton("✕", () => Struct("Remove field", () => n.Arr("inits").Items.Remove(init))));
                        fields.Add(row);
                    }
                    fields.Add(GraphFields.SmallButton("+ field", () => Struct("Add field", () =>
                    {
                        var list = n.Arr("inits");
                        if (list == null) n.Set("inits", list = JValue.NewArray(), order);
                        var init = JValue.NewObject();
                        init.Set("name", JValue.Of("value"));
                        init.Set("value", JValue.Null());
                        list.Items.Add(init);
                    })));
                    break;
                }
                case "lambda":
                {
                    var ps = n.Arr("params");
                    for (var i = 0; ps != null && i < ps.Items.Count; i++)
                    {
                        var p = ps.Items[i];
                        var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                        var mut = new Toggle("mut") { value = p.Flag("mut") };
                        mut.Q<Label>().style.minWidth = 0;
                        mut.RegisterValueChangedCallback(e => Val("Lambda parameter", () => p.SetFlag("mut", e.newValue, GraphSchema.LambdaParamOrder)));
                        row.Add(mut);
                        var type = GraphFields.Type(null, p.Str("type"), doc, v => Val("Lambda parameter", () => p.Set("type", JValue.Of(v.Trim()), GraphSchema.LambdaParamOrder)));
                        type.style.flexGrow = 1;
                        row.Add(type);
                        var name = GraphFields.Text(null, p.Str("name"), v => Val("Lambda parameter", () => p.Set("name", JValue.Of(v.Trim()), GraphSchema.LambdaParamOrder)));
                        name.style.flexGrow = 1;
                        row.Add(name);
                        row.Add(GraphFields.SmallButton("✕", () => Struct("Remove parameter", () => n.Arr("params").Items.Remove(p))));
                        fields.Add(row);
                    }
                    fields.Add(GraphFields.SmallButton("+ parameter", () => Struct("Add parameter", () =>
                    {
                        var list = n.Arr("params");
                        if (list == null) n.Set("params", list = JValue.NewArray(), order);
                        var p = JValue.NewObject();
                        p.Set("type", JValue.Of("float"));
                        p.Set("name", JValue.Of("x"));
                        list.Items.Add(p);
                    })));
                    break;
                }
                case "is":
                    fields.Add(Type("type", null, false));
                    fields.Add(OptionalText("binding", "binding", "no binding"));
                    break;
                case "has":
                    fields.Add(Text("subject"));
                    fields.Add(Type("type", null, false));
                    fields.Add(Text("field"));
                    break;
                case "text":
                case "code":
                {
                    fields.Add(Text("text", "Tide", true));
                    var inputs = n.Get("inputs");
                    for (var i = 0; inputs != null && inputs.IsObject && i < inputs.Keys.Count; i++)
                    {
                        var at = i;
                        var row = new VisualElement { style = { flexDirection = FlexDirection.Row } };
                        var f = GraphFields.Text(L("input"), inputs.Keys[i], v => Struct("Rename input", () =>
                        {
                            var o = n.Get("inputs");
                            v = v.Trim();
                            if (v.Length > 0 && !o.Keys.Contains(v)) o.Keys[at] = v;
                        }));
                        f.style.flexGrow = 1;
                        row.Add(f);
                        row.Add(GraphFields.SmallButton("✕", () => Struct("Remove input", () =>
                        {
                            var o = n.Get("inputs");
                            o.Keys.RemoveAt(at);
                            o.Values.RemoveAt(at);
                        })));
                        fields.Add(row);
                    }
                    fields.Add(GraphFields.SmallButton("+ input", () => Struct("Add input", () =>
                    {
                        var o = n.Get("inputs");
                        if (o == null || !o.IsObject) n.Set("inputs", o = JValue.NewObject(), order);
                        var name = "a";
                        for (var c = 'a'; c <= 'z' && o.Keys.Contains(name); c++) name = c.ToString();
                        if (o.Keys.Contains(name)) name = "input" + o.Keys.Count;
                        o.Set(name, JValue.Null());
                    })));
                    break;
                }
                case "var":
                    fields.Add(Flags(Flag("mut"), Flag("shared")));
                    fields.Add(Type("type", "var"));
                    fields.Add(Text("name"));
                    break;
                case "assign":
                    fields.Add(Op(GraphSchema.AssignOps));
                    break;
                case "eval":
                    fields.Add(Flags(Flag("stacked", "stacked", true), Flag("withBlock", "takes a block", true)));
                    break;
                case "foreach":
                case "parallel":
                    fields.Add(Flags(Flag("indexed")));
                    fields.Add(Type("type", "var"));
                    fields.Add(Text("name"));
                    break;
                case "switch":
                {
                    var cases = n.Arr("cases");
                    for (var i = 0; cases != null && i < cases.Items.Count; i++)
                    {
                        var c = cases.Items[i];
                        var row = new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center } };
                        row.Add(new Label($"case {i + 1}") { style = { width = 46, fontSize = 10 } });
                        row.Add(GraphFields.SmallButton("+ label", () => Struct("Add label", () =>
                        {
                            var labels = c.Arr("labels");
                            if (labels == null) c.Set("labels", labels = JValue.NewArray(), GraphSchema.CaseOrder);
                            labels.Items.Add(JValue.Null());
                        })));
                        if ((c.Arr("labels")?.Items.Count ?? 0) > 1)
                            row.Add(GraphFields.SmallButton("− label", () => Struct("Remove label", () =>
                            {
                                var labels = c.Arr("labels");
                                labels.Items.RemoveAt(labels.Items.Count - 1);
                            })));
                        row.Add(GraphFields.SmallButton("✕", () => Struct("Remove case", () => n.Arr("cases").Items.Remove(c))));
                        fields.Add(row);
                    }
                    fields.Add(GraphFields.SmallButton("+ case", () => Struct("Add case", () =>
                    {
                        var list = n.Arr("cases");
                        if (list == null) n.Set("cases", list = JValue.NewArray(), order);
                        list.Items.Add(GraphDocument.NewCase());
                    })));
                    if (!inline) fields.Add(new Label("A case label with nothing wired into it is 'default'.") { style = { whiteSpace = WhiteSpace.Normal, color = new Color(0.65f, 0.65f, 0.65f) } });
                    break;
                }
            }
            return fields;
        }

        public static string ArgName(JValue n, int i)
        {
            var names = n.Arr("argNames");
            return names != null && i < names.Items.Count ? names.Items[i].String ?? "" : "";
        }

        // argNames has one name for every argument, "" for an unnamed one, or is absent when none is named
        public static void SetArgName(JValue n, int i, string name)
        {
            var args = n.Arr("args");
            var count = args?.Items.Count ?? 0;
            var names = n.Arr("argNames");
            if (names == null)
            {
                if (string.IsNullOrEmpty(name)) return;
                names = JValue.NewArray();
                n.Set("argNames", names, GraphSchema.NodeOrder(n.Str("k")));
            }
            while (names.Items.Count < count) names.Items.Add(JValue.Of(""));
            if (i < names.Items.Count) names.Items[i] = JValue.Of(name ?? "");
            TidyArgNames(n);
        }

        static void TidyArgNames(JValue n)
        {
            var names = n.Arr("argNames");
            if (names != null && names.Items.All(x => string.IsNullOrEmpty(x.String))) n.Remove("argNames");
        }

        // formats has one for every value, "" for none, or is absent when there's none
        static void SetFormat(JValue n, int i, string format)
        {
            var count = n.Arr("values")?.Items.Count ?? 0;
            var formats = n.Arr("formats");
            if (formats == null)
            {
                if (string.IsNullOrEmpty(format)) return;
                formats = JValue.NewArray();
                n.Set("formats", formats, GraphSchema.NodeOrder("interp"));
            }
            while (formats.Items.Count < count) formats.Items.Add(JValue.Of(""));
            if (i < formats.Items.Count) formats.Items[i] = JValue.Of(format ?? "");
            if (formats.Items.All(x => string.IsNullOrEmpty(x.String))) n.Remove("formats");
        }
    }
}
