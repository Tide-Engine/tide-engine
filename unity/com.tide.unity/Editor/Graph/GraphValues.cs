using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEditor.UIElements;
using UnityEngine;
using UnityEngine.UIElements;

namespace Tide.Editor
{
    // A value of the file that isn't code (a field's starting value, a constant's, a setting's), edited by its
    // type's own control: a number, a tick box, text, a vector's numbers, a colour, an enum's choices. With no
    // value, the control shows the type's default, greyed. A value that's more than that is a small graph, opened
    // beside its card.
    static class GraphValues
    {
        public enum Shape { None, Literal, Vector, Color, Choice, Graph }

        static readonly string[] literals = { "int", "float", "char", "string", "bool" };

        static readonly Regex whole = new Regex(@"^-?(0[xX][0-9a-fA-F_]+|0[bB][01_]+|\d[\d_]*)[uUlL]*$");
        static readonly Regex fraction = new Regex(@"^-?(\d[\d_]*)?(\.\d[\d_]*)?([eE][+-]?\d+)?(deg|rad|[fFdD])?$");

        // Which control a type's value has is the schema's (valueControls)
        // Whether a value of a type goes where another is taken: the same type (an optional takes its type), or a
        // number that widens by itself (the spec's: byte and int to long, float and double; float to double). A
        // type that isn't known fits: the check says.
        // A type of several, as the language server says an engine's function takes ("float2|float3"): any of
        // them. A kind of the game's declarations (Spawn's "component"): one of that kind, where `kindOf` says
        // what kind a type is. A type parameter (Json.Write's T): anything.
        public static bool Fits(string have, string want, Func<string, string> kindOf = null)
        {
            if (string.IsNullOrEmpty(have) || string.IsNullOrEmpty(want)) return true;
            if (want.IndexOf('|') >= 0) return want.Split('|').Any(t => Fits(have, t, kindOf));
            if (want == "component" || want == "event" || want == "scene")
            {
                var kind = kindOf?.Invoke(have.Trim().TrimEnd('?'));
                return string.IsNullOrEmpty(kind) || kind == want;
            }
            if (want.Length == 1 && char.IsUpper(want[0])) return true;
            have = have.Trim();
            if (have.StartsWith("const ")) have = have.Substring(6).Trim();
            want = want.Trim().TrimEnd('?');
            have = have.TrimEnd('?');
            if (have == want) return true;
            if (have == "byte") return want == "int" || want == "long" || want == "float" || want == "double";
            if (have == "int") return want == "long" || want == "float" || want == "double";
            if (have == "float" || have == "long") return want == "double";
            return false;
        }

        // What a wanted type is called: itself, or of several ("float2|float3"), "float2 or float3", and of many,
        // what they are
        public static string Words(string type)
        {
            if (string.IsNullOrEmpty(type) || type.IndexOf('|') < 0) return type;
            var list = type.Split('|');
            if (list.Length > 4) return GraphSchema.Word(list.Contains("Color") ? "type.numbersOrColour" : "type.numbers");
            return string.Join(", ", list.Take(list.Length - 1)) + " " + GraphSchema.Word("or") + " " + list[list.Length - 1];
        }

        public static bool IsWhole(string type) => GraphSchema.ValueControl(type) == "whole";
        public static bool IsFraction(string type) => GraphSchema.ValueControl(type) == "decimal";
        public static bool IsVector(string type) => GraphSchema.ValueControl(type) == "vector";

        // What a value's graph is: nothing, one literal, a vector or colour made of literals, a choice of an enum
        // (`Mood.Calm`), or more
        public static Shape ShapeOf(JValue graph, out JValue root, out List<JValue> args)
        {
            root = null;
            args = null;
            if (graph == null) return Shape.None;
            root = GraphDocument.NodeIn(graph, graph.Wire("root"));
            if (root == null) return (graph.Arr("nodes")?.Items.Count ?? 0) == 0 ? Shape.None : Shape.Graph;
            var reached = GraphDocument.Reached(graph);
            var k = root.Str("k");
            if (Array.IndexOf(literals, k) >= 0) return reached.Count == 1 ? Shape.Literal : Shape.Graph;
            if (k == "member" && !root.Flag("shorthand"))
            {
                var of = GraphDocument.NodeIn(graph, root.Wire("object"));
                return of != null && of.Str("k") == "name" && reached.Count == 2 ? Shape.Choice : Shape.Graph;
            }
            if (k != "call" || root.Has("typeArgs") || root.Has("argNames") || root.Flag("local")) return Shape.Graph;
            var name = root.Str("name");
            var isVector = IsVector(name);
            var isColor = name == "Color";
            if (!isVector && !isColor) return Shape.Graph;
            var a = root.Arr("args");
            if (a == null || a.Items.Count == 0) return Shape.Graph;
            args = new List<JValue>();
            foreach (var item in a.Items)
            {
                var n = GraphDocument.NodeIn(graph, JValue.AsId(item));
                if (n == null || (n.Str("k") != "int" && n.Str("k") != "float")) return Shape.Graph;
                args.Add(n);
            }
            if (reached.Count != 1 + args.Count) return Shape.Graph;
            if (isColor) return args.Count == 3 || args.Count == 4 ? Shape.Color : Shape.Graph;
            return args.Count == name[name.Length - 1] - '0' ? Shape.Vector : Shape.Graph;
        }

        // What's typed, as a literal: its kind and its text as the file keeps it. A type it's for decides what it
        // can be; with none, what's typed does.
        public static bool TryLiteral(string typed, string type, out string k, out string text)
        {
            typed = (typed ?? "").Trim();
            k = null;
            text = typed;
            var control = GraphSchema.ValueControl(type);
            if (control == "text")
            {
                k = "string";
                text = Unquote(typed);
                return true;
            }
            if (control == "char")
            {
                var c = typed.Length >= 3 && typed[0] == '\'' && typed[typed.Length - 1] == '\'' ? typed.Substring(1, typed.Length - 2) : typed;
                if (c.Length != 1 && !(c.Length == 2 && c[0] == '\\')) return false;
                k = "char";
                text = "'" + c + "'";
                return true;
            }
            if (typed.Length == 0) return false;
            if (control == "tick" || (type == null && (typed == "true" || typed == "false")))
            {
                if (typed != "true" && typed != "false") return false;
                k = "bool";
                return true;
            }
            if (whole.IsMatch(typed) && !IsFraction(type) || (IsFraction(type) && whole.IsMatch(typed) && !typed.StartsWith("0x") && !typed.StartsWith("0b")))
            {
                k = "int";
                return type == null || IsWhole(type) || IsFraction(type);
            }
            if (fraction.IsMatch(typed) && typed.Any(char.IsDigit))
            {
                k = "float";
                return type == null || IsFraction(type);
            }
            if (type == null && typed.Length >= 2 && typed[0] == '"' && typed[typed.Length - 1] == '"')
            {
                k = "string";
                text = Unquote(typed);
                return true;
            }
            if (type == null && typed.Length >= 3 && typed[0] == '\'' && typed[typed.Length - 1] == '\'')
            {
                k = "char";
                return true;
            }
            return false;
        }

        static string Unquote(string s) => s.Length >= 2 && s[0] == '"' && s[s.Length - 1] == '"' ? s.Substring(1, s.Length - 2) : s;

        // What a type's values are, said when what's typed isn't one
        public static string Says(string type)
        {
            var help = GraphSchema.ValueHelp(type);
            if (IsWhole(type)) return $"{GraphLook.LowerFirst(help)}, like 3 (it's {type})";
            if (IsFraction(type)) return $"{GraphLook.LowerFirst(help)}, like 1.5 (it's {type})";
            return help != null ? GraphLook.LowerFirst(help) : "a value of " + type;
        }

        // A literal node's value as text to edit
        public static string TextOf(JValue literal)
        {
            if (literal == null) return "";
            return literal.Str("k") == "bool" ? (literal.Flag("value") ? "true" : "false") : literal.Str("text") ?? "";
        }

        public static void SetLiteral(JValue n, string k, string text)
        {
            n.Set("k", JValue.Of(k));
            var order = GraphSchema.NodeOrder(k);
            if (k == "bool")
            {
                n.Remove("text");
                n.Set("value", JValue.Of(text == "true"), order);
            }
            else
            {
                n.Remove("value");
                n.Set("text", JValue.Of(text), order);
            }
        }

        static float Number(JValue n)
        {
            var text = (n.Str("text") ?? "0").TrimEnd('f', 'F', 'd', 'D');
            return float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var f) ? f : 0;
        }

        internal static string Format(float f)
        {
            var s = Math.Round(f, 3).ToString("0.###", CultureInfo.InvariantCulture);
            return s.Contains(".") ? s : s + ".0";
        }

        static JValue Empty()
        {
            var g = JValue.NewObject();
            g.Set("root", JValue.Null());
            g.Set("nodes", JValue.NewArray());
            return g;
        }

        // A value graph of a call to `name` with number literals
        public static JValue Constructor(GraphDocument doc, string name, IList<string> texts)
        {
            var g = Empty();
            var call = doc.NewNode("call", -200, 0);
            call.Set("name", JValue.Of(name), GraphSchema.NodeOrder("call"));
            var args = JValue.NewArray();
            var nodes = g.Arr("nodes");
            nodes.Items.Add(call);
            for (var i = 0; i < texts.Count; i++)
            {
                var k = texts[i].Contains(".") || texts[i].EndsWith("deg") ? "float" : "int";
                var n = doc.NewNode(k, -420, i * 60);
                n.Set("text", JValue.Of(texts[i]), GraphSchema.NodeOrder(k));
                args.Items.Add(JValue.Of(n.Wire("id")));
                nodes.Items.Add(n);
            }
            call.Set("args", args, GraphSchema.NodeOrder("call"));
            g.Set("root", JValue.Of(call.Wire("id")));
            return g;
        }

        // A value graph of an enum's choice, `Mood.Calm`
        public static JValue Choice(GraphDocument doc, string type, string member)
        {
            var g = Empty();
            var of = doc.NewNode("name", -360, 0);
            of.Set("name", JValue.Of(type), GraphSchema.NodeOrder("name"));
            var m = doc.NewNode("member", -180, 0);
            m.Set("object", JValue.Of(of.Wire("id")), GraphSchema.NodeOrder("member"));
            m.Set("member", JValue.Of(member), GraphSchema.NodeOrder("member"));
            g.Arr("nodes").Items.Add(m);
            g.Arr("nodes").Items.Add(of);
            g.Set("root", JValue.Of(m.Wire("id")));
            return g;
        }

        public static void Put(GraphDocument doc, GraphSlot slot, JValue graph)
        {
            var e = doc.Find(slot.Owner);
            if (e == null) return;
            if (slot.Index >= 0)
            {
                var a = e.Value.Arr(slot.Key);
                if (a == null) return;
                if (slot.Index < a.Items.Count) a.Items[slot.Index] = graph;
                else a.Items.Add(graph);
            }
            else e.Value.Set(slot.Key, graph, GraphDocument.OrderOf(e));
        }

        public static void Clear(GraphDocument doc, GraphSlot slot)
        {
            var owner = doc.Get(slot.Owner);
            if (owner == null) return;
            if (slot.Index >= 0)
            {
                var a = owner.Arr(slot.Key);
                if (a != null && slot.Index < a.Items.Count) a.Items.RemoveAt(slot.Index);
            }
            else owner.Remove(slot.Key);
        }

        // The default of a type, as its greyed control says it
        public static string DefaultOf(string type)
        {
            var control = GraphSchema.ValueControl(type);
            if (control == "whole" || control == "decimal") return "0";
            if (control == "tick") return "false";
            if (control == "text") return "\"\"";
            return "default";
        }

        // The control of the value in a slot, for a value of `type` (null when it isn't known: an enum member's
        // number, a setting). `optional` values can be none; `item` is the card or frame it's on.
        public static VisualElement Control(TideGraphWindow w, GraphSlot slot, string type, bool optional, int item, string what)
        {
            var doc = w.Doc;
            var graph = doc.GetGraph(slot);
            var shape = ShapeOf(graph, out var root, out var args);
            var box = GraphLook.Row("tg-row-value");
            void Edit(string name, Action change) => w.Edit(name, change, item);
            void Set(JValue made) => Edit("Set " + what, () => Put(doc, slot, made));
            void Unset() => Edit("Clear " + what, () => Clear(doc, slot));

            if (shape == Shape.Graph)
            {
                var open = GraphLook.Labelled("code", "worked out", () => w.Board.OpenValue(slot), "tg-link");
                open.tooltip = "This value is more than a number or a text: open the small graph that works it out";
                box.Add(open);
                return box;
            }
            if (shape == Shape.Choice)
            {
                var enumName = GraphDocument.NodeIn(graph, root.Wire("object"))?.Str("name");
                box.Add(ChoiceButton(w, slot, type ?? enumName, root.Str("member"), optional, item, what));
                return box;
            }
            var control = GraphSchema.ValueControl(type);
            if (control == "tick" || (type == null && root?.Str("k") == "bool"))
            {
                var t = new Toggle { value = root != null && root.Flag("value") };
                if (root == null) t.AddToClassList("tg-faint");
                t.tooltip = root == null ? "Starts as false" : "";
                t.RegisterValueChangedCallback(e =>
                {
                    e.StopPropagation();
                    Set(doc.ValueGraph("bool", e.newValue ? "true" : "false"));
                });
                box.Add(t);
                return box;
            }
            if (control == "colour" || shape == Shape.Color)
            {
                var none = shape != Shape.Color;
                var c = none ? new Color(1, 1, 1, 1) : new Color(Number(args[0]), Number(args[1]), Number(args[2]), args.Count > 3 ? Number(args[3]) : 1);
                var field = new ColorField { value = c, showAlpha = true };
                if (none) field.AddToClassList("tg-faint");
                field.RegisterValueChangedCallback(e =>
                {
                    e.StopPropagation();
                    var v = e.newValue;
                    Set(Constructor(doc, "Color", new[] { Format(v.r), Format(v.g), Format(v.b), Format(v.a) }));
                });
                box.Add(field);
                return box;
            }
            if (IsVector(type) || shape == Shape.Vector)
            {
                var name = shape == Shape.Vector ? root.Str("name") : type;
                var count = name[name.Length - 1] - '0';
                var zero = name.StartsWith("int") ? "0" : "0.0";
                box.AddToClassList("tg-row-value--wide");
                var fields = new List<TextField>();
                for (var i = 0; i < count; i++)
                {
                    var at = i;
                    var f = GraphLook.Inline(shape == Shape.Vector ? args[i].Str("text") : "", _ =>
                    {
                        var texts = fields.Select(x => x.value.Trim()).ToList();
                        if (texts.All(t => t.Length == 0))
                        {
                            if (optional && shape != Shape.None) Unset();
                            return;
                        }
                        for (var j = 0; j < texts.Count; j++)
                        {
                            if (texts[j].Length == 0) texts[j] = zero;
                            if (!TryLiteral(texts[j], name.StartsWith("int") ? "int" : "float", out var _, out var _))
                            {
                                w.Say($"{what} is a {name}: each part is a number", true);
                                w.Board.Refresh(item);
                                return;
                            }
                        }
                        Set(Constructor(doc, name, texts));
                    }, "0", "tg-inline--value");
                    fields.Add(f);
                    box.Add(f);
                }
                return box;
            }
            // A value whose type has choices or parts of its own, with none yet: said, and set from a list
            var literalType = control == "text" || control == "char" || control == "whole" || control == "decimal";
            if (shape == Shape.None && type != null && !literalType)
            {
                box.Add(ChoiceButton(w, slot, type, null, optional, item, what));
                return box;
            }
            {
                var kind = root?.Str("k");
                var current = root == null ? "" : kind == "char" ? (root.Str("text") ?? "").Trim('\'') : root.Str("text") ?? "";
                var wants = type ?? (kind == "string" || kind == "char" ? kind : null);
                var f = GraphLook.Inline(current, typed =>
                {
                    if (typed.Trim().Length == 0 && (GraphSchema.ValueControl(wants) != "text" || optional))
                    {
                        if (optional && root != null) Unset();
                        else w.Board.Refresh(item);
                        return;
                    }
                    if (!TryLiteral(typed, wants, out var k, out var text))
                    {
                        w.Say(wants != null ? $"{what} takes {Says(wants)}." : $"'{typed}' isn't a number, true, false or a text in quotes. For more than that, use \"Work it out\" in its menu.", true);
                        w.Board.Refresh(item);
                        return;
                    }
                    if (root != null) Edit("Set " + what, () => SetLiteral(root, k, text));
                    else Set(doc.ValueGraph(k, text));
                }, type != null ? DefaultOf(type) : "none", "tg-inline--value");
                f.tooltip = root == null && type != null ? $"Starts as {DefaultOf(type)}: type a value to start with another" : "";
                box.Add(f);
            }
            return box;
        }

        // A value picked from a list: an enum's choices where the type is one, and the default
        static VisualElement ChoiceButton(TideGraphWindow w, GraphSlot slot, string type, string current, bool optional, int item, string what)
        {
            Button b = null;
            b = GraphLook.Plain(() => OpenChoices(w, slot, type, current, optional, item, what, b), "tg-word");
            b.Add(new Label(current ?? "default") { pickingMode = PickingMode.Ignore });
            if (current == null) b.AddToClassList("tg-faint");
            b.Add(new GraphIcon("down") { pickingMode = PickingMode.Ignore });
            b.tooltip = current == null ? $"Starts as {type}'s default: pick what it starts as" : $"{type}.{current}";
            return b;
        }

        static void OpenChoices(TideGraphWindow w, GraphSlot slot, string type, string current, bool optional, int item, string what, VisualElement from)
        {
            var doc = w.Doc;
            var list = new GraphOptionList("Starting value of " + what, GraphSchema.ChoiceHelp, null, 240);
            List<GraphOption> Options(IEnumerable<string> members)
            {
                var options = new List<GraphOption>();
                foreach (var m in members)
                {
                    var member = m;
                    options.Add(new GraphOption
                    {
                        Title = m, Section = type, Checked = m == current,
                        Pick = () => w.Edit("Set " + what, () => Put(doc, slot, Choice(doc, type, member)), item),
                    });
                }
                if (optional)
                    options.Add(new GraphOption { Title = "The default", Section = "Or", Checked = current == null, Detail = "no starting value", Pick = () => w.Edit("Clear " + what, () => Clear(doc, slot), item) });
                options.Add(new GraphOption { Title = "Work it out…", Section = "Or", Detail = "a small graph", Pick = () => w.Board.OpenValue(slot) });
                return options;
            }
            var own = doc.EnumMembers(type);
            list.Set(Options(own ?? new List<string>()));
            w.Open(list, from);
            if (own != null) return;
            // Not an enum of this file: the language server says whether it's one of the game's, and its choices
            list.Status = "Asking what " + type + " can be…";
            TideGraphLanguage.MembersOfName(w, type, (members, why) =>
            {
                if (list.WasClosed) return;
                var choices = members?.Where(m => m.Kind == TideGraphLanguage.KindEnumMember).Select(m => m.Label).ToList() ?? new List<string>();
                list.Status = null;
                list.Set(Options(choices));
            });
        }
    }
}
