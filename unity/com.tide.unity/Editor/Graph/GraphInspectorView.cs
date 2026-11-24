using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // The keys of what's selected: a declaration's, a member's, a parameter's, a node's... each edited as the
    // format has it (notes/visual-scripting.md).
    sealed class GraphInspectorView : VisualElement
    {
        readonly TideGraphWindow w;
        readonly ScrollView scroll;

        public GraphInspectorView(TideGraphWindow window)
        {
            w = window;
            style.flexGrow = 1;
            style.minWidth = 200;
            scroll = new ScrollView { style = { flexGrow = 1, paddingLeft = 4, paddingRight = 4 } };
            Add(scroll);
        }

        GraphDocument Doc => w.Doc;
        VisualElement Box => scrollTarget ?? scroll.contentContainer;

        // Edits from here redraw the outline and canvas, but not the field typed in
        void Set(string what, Action change) => w.Edit(what, change, GraphRedraw.Outline | GraphRedraw.Canvas);
        void Restructure(string what, Action change) => w.Edit(what, change);

        public void Rebuild()
        {
            scrollTarget = null;
            Box.Clear();
            if (Doc == null) return;
            var id = w.SelectedElement;
            if (id == GraphOutlineView.FileRow)
            {
                File();
                return;
            }
            var e = Doc.Find(id);
            if (e == null)
            {
                Box.Add(new Label("Select a declaration, a member or a node.") { style = { whiteSpace = WhiteSpace.Normal, marginTop = 6 } });
                return;
            }
            Box.Add(new Label(Doc.Describe(e)) { style = { unityFontStyleAndWeight = FontStyle.Bold, fontSize = 13, marginTop = 4, marginBottom = 4, whiteSpace = WhiteSpace.Normal } });
            switch (e.Role)
            {
                case GraphRole.Decl: Decl(e); break;
                case GraphRole.Member: Member(e); break;
                case GraphRole.EnumMember:
                    Comment(e);
                    Name(e);
                    Box.Add(GraphFields.ValueSlot(w, "value", e.Id, "value", true));
                    break;
                case GraphRole.Entry:
                    Comment(e);
                    Name(e);
                    Box.Add(GraphFields.ValueSlot(w, "value", e.Id, "value", false));
                    break;
                case GraphRole.Accessor:
                    Comment(e);
                    Box.Add(GraphFields.Choice("word", new[] { "get", "set" }, e.Value.Str("word"), v => Set("Accessor", () => e.Value.Set("word", JValue.Of(v), GraphSchema.AccessorOrder))));
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    break;
                case GraphRole.PrefabNode: Prefab(e); break;
                case GraphRole.Param:
                case GraphRole.SampleParam:
                case GraphRole.Payload:
                    Box.Add(new Label("Edited with its declaration:") { style = { whiteSpace = WhiteSpace.Normal } });
                    Box.Add(new Button(() => w.Select(e.Owner.Wire("id"), false)) { text = "Select " + Doc.Describe(Doc.Find(e.Owner.Wire("id"))) });
                    break;
                case GraphRole.Attribute:
                    Attribute(e.Value, e.List);
                    break;
                case GraphRole.Node: Node(e); break;
                case GraphRole.Note:
                    Box.Add(GraphFields.Text("text", e.Value.Str("text"), v => Set("Note", () => e.Value.Set("text", JValue.Of(v), GraphSchema.NoteOrder)), true));
                    break;
            }
            Diagnostics(id);
        }

        // What tidec says of it, and of its parameters, attributes and payload
        void Diagnostics(int id)
        {
            bool Of(int about)
            {
                for (var e = Doc.Find(about); e != null; )
                {
                    if (e.Id == id) return true;
                    if (e.Role != GraphRole.Param && e.Role != GraphRole.SampleParam && e.Role != GraphRole.Payload && e.Role != GraphRole.Attribute) return false;
                    var owner = Doc.Find(e.Owner?.Wire("id") ?? 0);
                    if (owner == e) return false;
                    e = owner;
                }
                return false;
            }
            var said = w.Checked?.Diagnostics.Where(d => Of(d.Id)).ToList();
            if (said == null || said.Count == 0) return;
            foreach (var d in said) Box.Add(new HelpBox($"{d.Message}\n{d.Detail}", d.Severity == "error" ? HelpBoxMessageType.Error : HelpBoxMessageType.Warning));
        }

        // ---------------------------------------------------------------------------------------------------------
        // The file

        void File()
        {
            Box.Add(GraphFields.Header("File"));
            Box.Add(GraphFields.Text("namespace", Doc.Root.Str("namespace"), v => Set("Namespace", () => Doc.Root.SetString("namespace", v.Trim(), GraphSchema.RootOrder))));
            Box.Add(GraphFields.Header("using"));
            var usings = GraphFields.Strings(Doc.Root.Arr("using")).ToList();
            for (var i = 0; i < usings.Count; i++)
            {
                var at = i;
                var row = Row();
                var f = GraphFields.Text(null, usings[i], v => Set("Using", () => Doc.Root.Arr("using").Items[at] = JValue.Of(v.Trim())));
                f.style.flexGrow = 1;
                row.Add(f);
                row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove using", () =>
                {
                    var a = Doc.Root.Arr("using");
                    a.Items.RemoveAt(at);
                    if (a.Items.Count == 0) Doc.Root.Remove("using");
                })));
                Box.Add(row);
            }
            Box.Add(GraphFields.SmallButton("+ using", () => Restructure("Add using", () =>
            {
                var a = Doc.Root.Arr("using");
                if (a == null) Doc.Root.Set("using", a = JValue.NewArray(), GraphSchema.RootOrder);
                a.Items.Add(JValue.Of("Namespace"));
            })));
            Box.Add(new Label($"format {Doc.Root.Wire("tidegraph")}, next id {Doc.NextId}") { style = { color = new Color(0.6f, 0.6f, 0.6f), marginTop = 8 } });
        }

        static VisualElement Row() => new VisualElement { style = { flexDirection = FlexDirection.Row, alignItems = Align.Center } };

        // ---------------------------------------------------------------------------------------------------------
        // Declarations and members

        void Comment(GraphElement e)
        {
            var order = GraphDocument.OrderOf(e);
            Box.Add(GraphFields.Text("comment", e.Value.Str("comment"), v => Set("Comment", () => e.Value.SetString("comment", v, order)), true));
        }

        void Name(GraphElement e, string label = "name")
        {
            Box.Add(GraphFields.Text(label, e.Value.Str("name"), v => Set("Rename", () => e.Value.Set("name", JValue.Of(v.Trim()), GraphDocument.OrderOf(e)))));
        }

        void TypeKey(GraphElement e, string key, string label = null, bool optional = false, string placeholder = null)
        {
            var order = GraphDocument.OrderOf(e);
            Box.Add(GraphFields.Type(label ?? key, e.Value.Str(key), Doc, v => Set("Set " + key, () =>
            {
                v = v.Trim();
                if (optional) e.Value.SetString(key, v, order);
                else e.Value.Set(key, JValue.Of(v), order);
            }), placeholder));
        }

        void Flag(GraphElement e, string key, string label = null)
        {
            var order = GraphDocument.OrderOf(e);
            Box.Add(GraphFields.Toggle(label ?? key, e.Value.Flag(key), v => Set("Set " + key, () => e.Value.SetFlag(key, v, order))));
        }

        void Modifiers(GraphElement e, bool member)
        {
            var kind = e.Value.Str("kind");
            var applies = GraphSchema.Modifiers(kind, member);
            var have = GraphFields.Strings(e.Value.Arr("modifiers")).ToList();
            var words = GraphSchema.AllModifiers.Where(m => applies.Contains(m) || have.Contains(m)).ToList();
            if (words.Count == 0) return;
            Box.Add(GraphFields.Header("modifiers"));
            var row = new VisualElement { style = { flexDirection = FlexDirection.Row, flexWrap = Wrap.Wrap } };
            foreach (var word in words)
            {
                var m = word;
                var t = new Toggle(m) { value = have.Contains(m), style = { marginRight = 8 } };
                t.Q<Label>().style.minWidth = 0;
                t.RegisterValueChangedCallback(ev => Restructure("Modifiers", () =>
                {
                    var list = GraphFields.Strings(e.Value.Arr("modifiers")).Where(x => x != m).ToList();
                    if (ev.newValue) list.Add(m);
                    // In the order tidec writes them
                    list = GraphSchema.AllModifiers.Where(list.Contains).Concat(list.Where(x => !GraphSchema.AllModifiers.Contains(x))).ToList();
                    if (list.Count == 0) e.Value.Remove("modifiers");
                    else e.Value.Set("modifiers", JValue.Strings(list), GraphDocument.OrderOf(e));
                    if (m == "override" && !ev.newValue) e.Value.Remove("overrides");
                }));
                row.Add(t);
            }
            Box.Add(row);
            if (have.Contains("override"))
                Box.Add(GraphFields.Text("overrides", e.Value.Str("overrides"), v => Set("Overrides", () => e.Value.SetString("overrides", v.Trim(), GraphDocument.OrderOf(e)))));
        }

        void Attributes(GraphElement e) => AttributesOf(e.Value, GraphDocument.OrderOf(e));

        void AttributesOf(JValue owner, IList<string> order)
        {
            Box.Add(GraphFields.Header("attributes"));
            var a = owner.Arr("attributes");
            if (a != null) foreach (var attribute in a.Items) Attribute(attribute, a);
            Box.Add(GraphFields.SmallButton("+ attribute", () => Restructure("Add attribute", () =>
            {
                var list = owner.Arr("attributes");
                if (list == null) owner.Set("attributes", list = JValue.NewArray(), order);
                list.Items.Add(Doc.NewAttribute("Attribute"));
            })));
        }

        void Attribute(JValue attribute, JValue list)
        {
            var row = Row();
            row.Add(new Label("[") { style = { unityTextAlign = TextAnchor.MiddleLeft } });
            var name = GraphFields.Text(null, attribute.Str("name"), v => Set("Attribute", () => attribute.Set("name", JValue.Of(v.Trim()), GraphSchema.AttributeOrder)));
            name.style.width = 90;
            row.Add(name);
            var parens = new Toggle { value = attribute.Has("args"), tooltip = "Parentheses: an attribute with arguments" };
            parens.RegisterValueChangedCallback(ev => Restructure("Attribute", () =>
            {
                if (ev.newValue) attribute.Set("args", JValue.Of(""), GraphSchema.AttributeOrder);
                else attribute.Remove("args");
            }));
            row.Add(parens);
            if (attribute.Has("args"))
            {
                var args = GraphFields.Text(null, attribute.Str("args"), v => Set("Attribute", () => attribute.Set("args", JValue.Of(v), GraphSchema.AttributeOrder)));
                args.style.flexGrow = 1;
                args.tooltip = "What's between the parentheses, as Tide";
                row.Add(args);
            }
            row.Add(new Label("]") { style = { unityTextAlign = TextAnchor.MiddleLeft } });
            if (list != null) row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove attribute", () => list.Items.Remove(attribute))));
            Box.Add(row);
        }

        void TypeParams(GraphElement e)
        {
            var order = GraphDocument.OrderOf(e);
            Box.Add(GraphFields.List("type params", GraphFields.Strings(e.Value.Arr("typeParams")), list => Restructure("Type parameters", () =>
            {
                if (list.Count == 0) e.Value.Remove("typeParams");
                else e.Value.Set("typeParams", JValue.Strings(list), order);
            })));
            var where = e.Value.Arr("where");
            for (var i = 0; where != null && i < where.Items.Count; i++)
            {
                var rule = where.Items[i];
                var row = Row();
                row.Add(new Label("where") { style = { width = 40, unityTextAlign = TextAnchor.MiddleLeft } });
                var p = GraphFields.Text(null, rule.Str("param"), v => Set("Where", () => rule.Set("param", JValue.Of(v.Trim()), GraphSchema.WhereOrder)));
                p.style.width = 40;
                row.Add(p);
                row.Add(new Label(":"));
                var rules = GraphFields.List(null, GraphFields.Strings(rule.Arr("rules")), list => Set("Where", () => rule.Set("rules", JValue.Strings(list), GraphSchema.WhereOrder)));
                rules.style.flexGrow = 1;
                rules.tooltip = "Rules, separated by commas: comparable, hashable...";
                row.Add(rules);
                var w2 = where;
                row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove where", () =>
                {
                    w2.Items.Remove(rule);
                    if (w2.Items.Count == 0) e.Value.Remove("where");
                })));
                Box.Add(row);
            }
            Box.Add(GraphFields.SmallButton("+ where", () => Restructure("Add where", () =>
            {
                var list = e.Value.Arr("where");
                if (list == null) e.Value.Set("where", list = JValue.NewArray(), order);
                var rule = JValue.NewObject();
                rule.Set("param", JValue.Of(GraphFields.Strings(e.Value.Arr("typeParams")).FirstOrDefault() ?? "T"));
                rule.Set("rules", JValue.Strings(new[] { "comparable" }));
                list.Items.Add(rule);
            })));
        }

        void Decl(GraphElement e)
        {
            var v = e.Value;
            var kind = v.Str("kind");
            Comment(e);
            switch (kind)
            {
                case "text":
                    Box.Add(GraphFields.Text("text", v.Str("text"), t => Set("Text", () => v.Set("text", JValue.Of(t), GraphSchema.DeclOrder(kind))), true));
                    return;
                case "namespace":
                    Name(e);
                    return;
                case "settings":
                    Box.Add(new Label("Its entries are in the outline: add one from its menu.") { style = { whiteSpace = WhiteSpace.Normal } });
                    return;
            }
            Name(e);
            Modifiers(e, false);
            Attributes(e);
            if (Array.IndexOf(GraphSchema.DataKinds, kind) >= 0)
            {
                TypeParams(e);
                if (kind == "scene")
                {
                    Box.Add(GraphFields.Toggle("has members", v.Get("members")?.IsArray ?? false, on => Restructure("Scene", () =>
                        v.Set("members", on ? JValue.NewArray() : JValue.Null(), GraphSchema.DeclOrder(kind)))));
                }
                return;
            }
            switch (kind)
            {
                case "enum":
                    TypeKey(e, "backing", "backing", true, "int");
                    return;
                case "const":
                case "asset":
                    TypeKey(e, "type");
                    Box.Add(GraphFields.ValueSlot(w, "value", e.Id, "value", false));
                    return;
                case "alias":
                    TypeKey(e, "type");
                    return;
                case "prefab":
                    Box.Add(new Label("Its entities are in the outline.") { style = { whiteSpace = WhiteSpace.Normal } });
                    return;
                case "handler":
                    Trigger(e);
                    Params(e, "params");
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    return;
                case "command":
                    Payload(e);
                    Params(e, "params");
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    return;
            }
            Routine(e, kind == "function");
        }

        // A function's shape, a method's and the like: [mut] Ret Name<T>(params) fails E where ... { body }
        void Routine(GraphElement e, bool function)
        {
            var v = e.Value;
            var kind = v.Str("kind");
            var method = e.Role == GraphRole.Member;
            if (function || method)
            {
                Flag(e, "mut", "mut");
                TypeKey(e, "returnType", "returns");
                TypeParams(e);
            }
            Params(e, "params");
            if (function || method) TypeKey(e, "fails", "fails", true, "no error type");
            if (function || kind == "system")
            {
                Box.Add(GraphFields.Toggle("extern", v.Flag("extern"), on => Restructure("Extern", () =>
                {
                    var order = GraphDocument.OrderOf(e);
                    v.SetFlag("extern", on, order);
                    if (on) v.Remove("body");
                    else if (!v.Has("body")) v.Set("body", GraphDocument.EmptyBody(), order);
                })));
            }
            if (!v.Flag("extern")) Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
        }

        void Member(GraphElement e)
        {
            var v = e.Value;
            var kind = v.Str("kind");
            var order = GraphSchema.MemberOrder(kind);
            Comment(e);
            switch (kind)
            {
                case "field":
                    Flag(e, "local");
                    TypeKey(e, "type");
                    Name(e);
                    Attributes(e);
                    Box.Add(GraphFields.ValueSlot(w, "default", e.Id, "default", true));
                    break;
                case "property":
                    TypeKey(e, "type");
                    Name(e);
                    Box.Add(new Label("Its accessors are in the outline.") { style = { whiteSpace = WhiteSpace.Normal } });
                    break;
                case "method":
                    Name(e);
                    Modifiers(e, true);
                    Attributes(e);
                    Routine(e, false);
                    break;
                case "operator":
                    TypeKey(e, "returnType", "returns");
                    Box.Add(GraphFields.Choice("op", GraphSchema.OperatorOps, v.Str("op"), op => Set("Operator", () => v.Set("op", JValue.Of(op), order))));
                    Params(e, "params");
                    TypeKey(e, "fails", "fails", true, "no error type");
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    break;
                case "tweak":
                    TypeKey(e, "type");
                    Name(e);
                    Box.Add(GraphFields.ValueSlot(w, "value", e.Id, "value", false));
                    break;
                case "sample":
                    Name(e, "name");
                    SampleParams(e);
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    break;
                case "sanitize":
                case "predict":
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    break;
                case "prepare":
                    Params(e, "params");
                    Box.Add(new Button(() => w.Show(new GraphSlot(e.Id, "body"))) { text = "Open body" });
                    break;
                case "text":
                    Box.Add(GraphFields.Text("text", v.Str("text"), t => Set("Text", () => v.Set("text", JValue.Of(t), order)), true));
                    break;
            }
        }

        void Prefab(GraphElement e)
        {
            var v = e.Value;
            Comment(e);
            if (e.List != null)
            {
                Name(e);
                Box.Add(GraphFields.Text("base", v.Str("base"), b => Set("Base", () => v.SetString("base", b.Trim(), GraphSchema.PrefabNodeOrder))));
            }
            Box.Add(GraphFields.Header("components"));
            var components = v.Arr("components");
            for (var i = 0; components != null && i < components.Items.Count; i++)
                Box.Add(GraphFields.ValueSlot(w, $"{i + 1}", e.Id, "components", true, i));
            Box.Add(GraphFields.SmallButton("+ component", () => Restructure("Add component", () =>
            {
                var list = v.Arr("components");
                if (list == null) v.Set("components", list = JValue.NewArray(), GraphSchema.PrefabNodeOrder);
                var g = JValue.NewObject();
                var n = Doc.NewNode("literal", -200, 0);
                n.Set("type", JValue.Of("Component"), GraphSchema.NodeOrder("literal"));
                g.Set("root", JValue.Of(n.Wire("id")));
                g.Set("nodes", JValue.ArrayOf(new[] { n }));
                list.Items.Add(g);
            })));
        }

        void Trigger(GraphElement e)
        {
            var v = e.Value;
            var t = v.Get("trigger");
            // Made when it's first edited
            JValue Trigger()
            {
                var made = v.Get("trigger");
                if (made != null && made.IsObject) return made;
                made = JValue.NewObject();
                v.Set("trigger", made, GraphDocument.OrderOf(e));
                return made;
            }
            Box.Add(GraphFields.Header("trigger"));
            Box.Add(GraphFields.Toggle("verified", t?.Flag("verified") ?? false, on => Set("Trigger", () => Trigger().SetFlag("verified", on, GraphSchema.TriggerOrder))));
            Box.Add(GraphFields.Toggle("Changed<>", t?.Flag("changed") ?? false, on => Set("Trigger", () => Trigger().SetFlag("changed", on, GraphSchema.TriggerOrder))));
            Box.Add(GraphFields.Type("type", t?.Str("type"), Doc, type => Set("Trigger", () => Trigger().Set("type", JValue.Of(type.Trim()), GraphSchema.TriggerOrder))));
            Box.Add(GraphFields.Text("name", t?.Str("name"), name => Set("Trigger", () => Trigger().SetString("name", name.Trim(), GraphSchema.TriggerOrder))));
        }

        // A command's payload: its fields, as a struct's are written
        void Payload(GraphElement e)
        {
            var v = e.Value;
            Box.Add(GraphFields.Header("payload"));
            var payload = v.Arr("payload");
            for (var i = 0; payload != null && i < payload.Items.Count; i++)
            {
                var p = payload.Items[i];
                var box = Group();
                var row = Row();
                var type = GraphFields.Type(null, p.Str("type"), Doc, t => Set("Payload", () => p.Set("type", JValue.Of(t.Trim()), GraphSchema.PayloadOrder)));
                type.style.flexGrow = 1;
                row.Add(type);
                var name = GraphFields.Text(null, p.Str("name"), n => Set("Payload", () => p.Set("name", JValue.Of(n.Trim()), GraphSchema.PayloadOrder)));
                name.style.flexGrow = 1;
                row.Add(name);
                var list = payload;
                row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove payload value", () => list.Items.Remove(p))));
                box.Add(row);
                var inner = Box;
                scrollTarget = box;
                AttributesOf(p, GraphSchema.PayloadOrder);
                Box.Add(GraphFields.ValueSlot(w, "default", p.Wire("id"), "default", true));
                scrollTarget = null;
                inner.Add(box);
            }
            Box.Add(GraphFields.SmallButton("+ payload value", () => Restructure("Add payload value", () =>
            {
                var list = v.Arr("payload");
                if (list == null) v.Set("payload", list = JValue.NewArray(), GraphDocument.OrderOf(e));
                var p = JValue.NewObject();
                p.Set("id", JValue.Of(Doc.NewId()));
                p.Set("type", JValue.Of("int"));
                p.Set("name", JValue.Of("value"));
                list.Items.Add(p);
            })));
        }

        // While a group is being filled, Box adds to it
        VisualElement scrollTarget;

        static VisualElement Group() => new VisualElement
        {
            style =
            {
                borderLeftWidth = 2, borderLeftColor = new Color(0.4f, 0.4f, 0.4f), paddingLeft = 4, marginTop = 2, marginBottom = 4,
            },
        };

        void SampleParams(GraphElement e)
        {
            var v = e.Value;
            Box.Add(GraphFields.Header("parameters"));
            var list = v.Arr("params");
            for (var i = 0; list != null && i < list.Items.Count; i++)
            {
                var p = list.Items[i];
                var row = Row();
                var type = GraphFields.Type(null, p.Str("type"), Doc, t => Set("Parameter", () => p.Set("type", JValue.Of(t.Trim()), GraphSchema.SampleParamOrder)));
                type.style.flexGrow = 1;
                row.Add(type);
                var name = GraphFields.Text(null, p.Str("name"), n => Set("Parameter", () => p.Set("name", JValue.Of(n.Trim()), GraphSchema.SampleParamOrder)));
                name.style.flexGrow = 1;
                row.Add(name);
                var l = list;
                row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove parameter", () => l.Items.Remove(p))));
                Box.Add(row);
            }
            Box.Add(GraphFields.SmallButton("+ parameter", () => Restructure("Add parameter", () =>
            {
                var l = v.Arr("params");
                if (l == null) v.Set("params", l = JValue.NewArray(), GraphSchema.MemberOrder("sample"));
                var p = JValue.NewObject();
                p.Set("id", JValue.Of(Doc.NewId()));
                p.Set("type", JValue.Of("Devices"));
                p.Set("name", JValue.Of("devices"));
                l.Items.Add(p);
            })));
        }

        // A parameter list: mode, its words, type, name and default; a query's own parameters inside it
        void Params(GraphElement e, string key)
        {
            Box.Add(GraphFields.Header("parameters"));
            ParamList(e.Value, key, GraphDocument.OrderOf(e), 0);
        }

        void ParamList(JValue owner, string key, IList<string> order, int depth)
        {
            var list = owner.Arr(key);
            for (var i = 0; list != null && i < list.Items.Count; i++) Param(list, i, depth);
            Box.Add(GraphFields.SmallButton(depth == 0 ? "+ parameter" : "+ query parameter", () => Restructure("Add parameter", () =>
            {
                var l = owner.Arr(key);
                if (l == null) owner.Set(key, l = JValue.NewArray(), order);
                l.Items.Add(Doc.NewParam(depth == 0 ? "int" : "Transform", depth == 0 ? "value" : "transform"));
            })));
        }

        void Param(JValue list, int index, int depth)
        {
            var p = list.Items[index];
            var o = GraphSchema.ParamOrder;
            var box = Group();
            box.style.marginLeft = depth * 10;
            var mode = p.Str("mode") ?? "read";
            var row = Row();
            var modes = GraphFields.Choice(null, GraphSchema.ParamModes, mode, m => Restructure("Parameter mode", () =>
            {
                p.Set("mode", JValue.Of(m), o);
                if (m == "query" && p.Arr("query") == null)
                {
                    p.Set("query", JValue.NewArray(), o);
                    p.Remove("type");
                }
                if (m != "query") p.Remove("query");
                if (m != "with" && m != "without") p.Remove("ownership");
            }));
            modes.style.width = 80;
            row.Add(modes);
            if (mode != "query" && !p.Flag("ownership"))
            {
                var type = GraphFields.Type(null, p.Str("type"), Doc, t => Set("Parameter type", () => p.Set("type", JValue.Of(t.Trim()), o)));
                type.style.flexGrow = 1;
                row.Add(type);
            }
            if (mode != "with" && mode != "without")
            {
                var name = GraphFields.Text(null, p.Str("name"), n => Set("Parameter name", () => p.Set("name", JValue.Of(n.Trim()), o)));
                name.style.flexGrow = 1;
                name.style.minWidth = 50;
                row.Add(name);
            }
            row.Add(GraphFields.SmallButton("▲", () => Restructure("Move parameter", () => MoveItem(list, index, -1)), "Move up"));
            row.Add(GraphFields.SmallButton("▼", () => Restructure("Move parameter", () => MoveItem(list, index, 1)), "Move down"));
            row.Add(GraphFields.SmallButton("✕", () => Restructure("Remove parameter", () => list.Items.RemoveAt(index))));
            box.Add(row);
            var words = new VisualElement { style = { flexDirection = FlexDirection.Row, flexWrap = Wrap.Wrap } };
            void Word(string k, string label = null)
            {
                var t = new Toggle(label ?? k) { value = p.Flag(k), style = { marginRight = 6 } };
                t.Q<Label>().style.minWidth = 0;
                t.RegisterValueChangedCallback(ev => Restructure("Parameter", () =>
                {
                    p.SetFlag(k, ev.newValue, o);
                    if (k == "ownership" && ev.newValue) p.Remove("type");
                }));
                words.Add(t);
            }
            if (mode != "query")
            {
                Word("local");
                Word("verified");
                Word("previous");
                if (mode == "with" || mode == "without")
                {
                    Word("scene");
                    Word("ownership");
                }
            }
            box.Add(words);
            if (mode != "query")
            {
                var role = GraphFields.Text("role", p.Str("role"), r => Set("Parameter role", () => p.SetString("role", r.Trim(), o)));
                role.tooltip = "An image parameter's Raw, Linear or Raw(float)";
                box.Add(role);
            }
            var outer = scrollTarget;
            scrollTarget = box;
            if (mode == "query") ParamList(p, "query", o, depth + 1);
            else if (mode != "with" && mode != "without") Box.Add(GraphFields.ValueSlot(w, "default", p.Wire("id"), "default", true));
            scrollTarget = outer;
            Box.Add(box);
        }

        static void MoveItem(JValue list, int at, int by)
        {
            var to = at + by;
            if (to < 0 || to >= list.Items.Count) return;
            var item = list.Items[at];
            list.Items.RemoveAt(at);
            list.Items.Insert(to, item);
        }

        // ---------------------------------------------------------------------------------------------------------
        // Nodes

        void Node(GraphElement e)
        {
            var n = e.Value;
            var k = n.Str("k");
            var spec = GraphSchema.Spec(k);
            Box.Add(new Label($"{(spec != null ? spec.Title : k)} node {e.Id}") { style = { color = new Color(0.65f, 0.65f, 0.65f) } });
            Box.Add(GraphFields.Text("comment", n.Str("comment"), v => w.Edit("Comment", () => n.SetString("comment", v, GraphSchema.NodeOrder(k)), GraphRedraw.Canvas), true));
            foreach (var field in GraphNodeFields.Build(w, e.Graph, n, false)) Box.Add(field);
            if (spec == null) Box.Add(new HelpBox($"'{k}' isn't a kind of node this editor knows: its keys are kept as they are.", HelpBoxMessageType.Warning));
        }
    }
}
