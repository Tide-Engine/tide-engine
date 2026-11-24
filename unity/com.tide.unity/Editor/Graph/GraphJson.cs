using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Tide.Editor
{
    enum JKind { Null, Bool, Number, String, Array, Object }

    // A JSON value of a .tidegraph file. Objects keep their keys in order, and keys this package doesn't know are
    // kept as read, so that a newer format's extras survive an edit.
    sealed class JValue
    {
        public JKind Kind;
        public bool Bool;
        public double Number;
        public string String;
        public List<JValue> Items;   // An array's
        public List<string> Keys;    // An object's, in order
        public List<JValue> Values;  // An object's, by Keys

        public static JValue Null() => new JValue { Kind = JKind.Null };
        public static JValue Of(bool b) => new JValue { Kind = JKind.Bool, Bool = b };
        public static JValue Of(double n) => new JValue { Kind = JKind.Number, Number = n };
        public static JValue Of(string s) => s == null ? Null() : new JValue { Kind = JKind.String, String = s };
        public static JValue NewArray() => new JValue { Kind = JKind.Array, Items = new List<JValue>() };
        public static JValue NewObject() => new JValue { Kind = JKind.Object, Keys = new List<string>(), Values = new List<JValue>() };

        public static JValue ArrayOf(IEnumerable<JValue> items)
        {
            var a = NewArray();
            a.Items.AddRange(items);
            return a;
        }

        public static JValue Strings(IEnumerable<string> items)
        {
            var a = NewArray();
            foreach (var s in items) a.Items.Add(Of(s));
            return a;
        }

        public bool IsObject => Kind == JKind.Object;
        public bool IsArray => Kind == JKind.Array;
        public bool IsNull => Kind == JKind.Null;
        public int Count => Kind == JKind.Array ? Items.Count : Kind == JKind.Object ? Keys.Count : 0;

        public JValue Get(string key)
        {
            if (Kind != JKind.Object) return null;
            var i = Keys.IndexOf(key);
            return i < 0 ? null : Values[i];
        }

        public bool Has(string key) => Kind == JKind.Object && Keys.IndexOf(key) >= 0;

        // A string member, or null
        public string Str(string key)
        {
            var v = Get(key);
            return v != null && v.Kind == JKind.String ? v.String : null;
        }

        public bool Flag(string key)
        {
            var v = Get(key);
            return v != null && v.Kind == JKind.Bool && v.Bool;
        }

        // A wire: the id a member holds, or 0 for none
        public int Wire(string key) => AsId(Get(key));

        public static int AsId(JValue v) => v != null && v.Kind == JKind.Number ? (int)v.Number : 0;

        public JValue Arr(string key)
        {
            var v = Get(key);
            return v != null && v.Kind == JKind.Array ? v : null;
        }

        // Sets a member; a new one goes where `order` puts it among those there, or last
        public void Set(string key, JValue value, IList<string> order = null)
        {
            if (value == null)
            {
                Remove(key);
                return;
            }
            var i = Keys.IndexOf(key);
            if (i >= 0)
            {
                Values[i] = value;
                return;
            }
            var at = Keys.Count;
            if (order != null)
            {
                var rank = order.IndexOf(key);
                if (rank >= 0)
                {
                    // Before the first key there that the order puts after it
                    for (var k = 0; k < Keys.Count; k++)
                    {
                        var other = order.IndexOf(Keys[k]);
                        if (other > rank)
                        {
                            at = k;
                            break;
                        }
                    }
                }
            }
            Keys.Insert(at, key);
            Values.Insert(at, value);
        }

        public void SetString(string key, string value, IList<string> order = null)
        {
            if (string.IsNullOrEmpty(value)) Remove(key);
            else Set(key, Of(value), order);
        }

        // A flag that's written only when true, as tidec writes them
        public void SetFlag(string key, bool value, IList<string> order = null)
        {
            if (value) Set(key, Of(true), order);
            else Remove(key);
        }

        public void SetWire(string key, int id, IList<string> order = null)
        {
            if (id == 0) Remove(key);
            else Set(key, Of(id), order);
        }

        public void Remove(string key)
        {
            if (Kind != JKind.Object) return;
            var i = Keys.IndexOf(key);
            if (i < 0) return;
            Keys.RemoveAt(i);
            Values.RemoveAt(i);
        }

        public JValue Clone()
        {
            var c = new JValue { Kind = Kind, Bool = Bool, Number = Number, String = String };
            if (Kind == JKind.Array)
            {
                c.Items = new List<JValue>(Items.Count);
                foreach (var item in Items) c.Items.Add(item.Clone());
            }
            else if (Kind == JKind.Object)
            {
                c.Keys = new List<string>(Keys);
                c.Values = new List<JValue>(Values.Count);
                foreach (var v in Values) c.Values.Add(v.Clone());
            }
            return c;
        }

        public override string ToString() => GraphJson.Write(this);
    }

    // Reads any JSON, and writes it as tidec does: UTF-8 with LF line endings, two-space indentation, an object or
    // array with nothing nested in it on one line, and each node, parameter, payload value and trigger on one line.
    static class GraphJson
    {
        public sealed class ParseError : Exception
        {
            public readonly int Line, Column;

            public ParseError(string message, int line, int column) : base($"{line}:{column}: {message}")
            {
                Line = line;
                Column = column;
            }
        }

        public static JValue Parse(string text)
        {
            var r = new Reader { text = text ?? "" };
            if (r.text.Length > 0 && r.text[0] == '﻿') r.at = 1;
            r.Space();
            var v = r.Value();
            r.Space();
            if (r.at < r.text.Length) r.Fail("more after the file's value");
            return v;
        }

        sealed class Reader
        {
            public string text;
            public int at;

            public void Fail(string what)
            {
                int line = 1, column = 1;
                for (var i = 0; i < at && i < text.Length; i++)
                {
                    if (text[i] == '\n')
                    {
                        line++;
                        column = 1;
                    }
                    else column++;
                }
                throw new ParseError(what, line, column);
            }

            public void Space()
            {
                while (at < text.Length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) at++;
            }

            bool Word(string w)
            {
                if (string.CompareOrdinal(text, at, w, 0, w.Length) != 0) return false;
                at += w.Length;
                return true;
            }

            public JValue Value()
            {
                if (at >= text.Length) Fail("the file ends where a value should be");
                var c = text[at];
                if (c == '{') return Container(true);
                if (c == '[') return Container(false);
                if (c == '"') return JValue.Of(StringValue());
                if (Word("true")) return JValue.Of(true);
                if (Word("false")) return JValue.Of(false);
                if (Word("null")) return JValue.Null();
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    var start = at;
                    if (text[at] == '-') at++;
                    while (at < text.Length && "0123456789.eE+-".IndexOf(text[at]) >= 0) at++;
                    if (!double.TryParse(text.Substring(start, at - start), NumberStyles.Float, CultureInfo.InvariantCulture, out var n))
                    {
                        at = start;
                        Fail("a number that isn't one");
                    }
                    return JValue.Of(n);
                }
                Fail($"'{c}' where a value should be");
                return null;
            }

            JValue Container(bool isObject)
            {
                at++;
                var v = isObject ? JValue.NewObject() : JValue.NewArray();
                Space();
                var close = isObject ? '}' : ']';
                if (at < text.Length && text[at] == close)
                {
                    at++;
                    return v;
                }
                while (true)
                {
                    Space();
                    if (isObject)
                    {
                        if (at >= text.Length || text[at] != '"') Fail("a key, in quotes");
                        var key = StringValue();
                        Space();
                        if (at >= text.Length || text[at] != ':') Fail("':' after a key");
                        at++;
                        Space();
                        var value = Value();
                        var i = v.Keys.IndexOf(key);
                        if (i >= 0) v.Values[i] = value;
                        else
                        {
                            v.Keys.Add(key);
                            v.Values.Add(value);
                        }
                    }
                    else
                    {
                        v.Items.Add(Value());
                    }
                    Space();
                    if (at >= text.Length) Fail($"the file ends before '{close}'");
                    if (text[at] == ',')
                    {
                        at++;
                        continue;
                    }
                    if (text[at] == close)
                    {
                        at++;
                        return v;
                    }
                    Fail($"',' or '{close}'");
                }
            }

            string StringValue()
            {
                at++;
                var sb = new StringBuilder();
                while (true)
                {
                    if (at >= text.Length) Fail("a string that doesn't end");
                    var c = text[at++];
                    if (c == '"') return sb.ToString();
                    if (c != '\\')
                    {
                        sb.Append(c);
                        continue;
                    }
                    if (at >= text.Length) Fail("a string that doesn't end");
                    var e = text[at++];
                    switch (e)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            var code = 0;
                            if (at + 4 > text.Length || !int.TryParse(text.Substring(at, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out code))
                                Fail("'\\u' and four hex digits");
                            at += 4;
                            sb.Append((char)code);
                            break;
                        default: Fail($"'\\{e}', which isn't an escape"); break;
                    }
                }
            }
        }

        // The file's text: what Write gives, and the newline after it
        public static string WriteFile(JValue root) => Write(root) + "\n";

        public static string Write(JValue v)
        {
            var sb = new StringBuilder();
            WriteValue(sb, v, 0, false);
            return sb.ToString();
        }

        // Members whose objects tidec writes one to a line however much is in them
        static bool OneLineItems(string key) => key == "nodes" || key == "params" || key == "query" || key == "payload";

        static bool Nested(JValue v)
        {
            var items = v.Kind == JKind.Array ? v.Items : v.Values;
            foreach (var item in items)
            {
                if (item.Kind == JKind.Object) return true;
                if (item.Kind == JKind.Array && Nested(item)) return true;
            }
            return false;
        }

        static void WriteValue(StringBuilder sb, JValue v, int depth, bool oneLine)
        {
            switch (v.Kind)
            {
                case JKind.Null: sb.Append("null"); return;
                case JKind.Bool: sb.Append(v.Bool ? "true" : "false"); return;
                case JKind.Number: sb.Append(NumberText(v.Number)); return;
                case JKind.String: WriteString(sb, v.String); return;
            }
            var isObject = v.Kind == JKind.Object;
            var flat = oneLine || !Nested(v);
            sb.Append(isObject ? '{' : '[');
            var count = isObject ? v.Keys.Count : v.Items.Count;
            if (count == 0)
            {
                sb.Append(isObject ? '}' : ']');
                return;
            }
            var first = true;
            for (var i = 0; i < count; i++)
            {
                var item = isObject ? v.Values[i] : v.Items[i];
                // An empty comment says nothing, and isn't written
                if (isObject && v.Keys[i] == "comment" && item.Kind == JKind.String && item.String.Length == 0) continue;
                if (!first) sb.Append(flat ? ", " : ",");
                first = false;
                if (!flat)
                {
                    sb.Append('\n');
                    Indent(sb, depth + 1);
                }
                var itemOneLine = flat;
                if (isObject)
                {
                    WriteString(sb, v.Keys[i]);
                    sb.Append(": ");
                    if (v.Keys[i] == "trigger" && item.Kind == JKind.Object) itemOneLine = true;
                    if (OneLineItems(v.Keys[i]) && item.Kind == JKind.Array)
                    {
                        WriteArrayOfLines(sb, item, depth + 1, flat);
                        continue;
                    }
                }
                WriteValue(sb, item, depth + 1, itemOneLine);
            }
            if (!flat)
            {
                sb.Append('\n');
                Indent(sb, depth);
            }
            sb.Append(isObject ? '}' : ']');
        }

        // An array whose objects are each written on one line
        static void WriteArrayOfLines(StringBuilder sb, JValue a, int depth, bool oneLine)
        {
            var flat = oneLine || !Nested(a);
            if (a.Items.Count == 0)
            {
                sb.Append("[]");
                return;
            }
            sb.Append('[');
            for (var i = 0; i < a.Items.Count; i++)
            {
                if (i > 0) sb.Append(flat ? ", " : ",");
                if (!flat)
                {
                    sb.Append('\n');
                    Indent(sb, depth + 1);
                }
                var item = a.Items[i];
                WriteValue(sb, item, depth + 1, flat || item.Kind == JKind.Object);
            }
            if (!flat)
            {
                sb.Append('\n');
                Indent(sb, depth);
            }
            sb.Append(']');
        }

        static void Indent(StringBuilder sb, int depth)
        {
            for (var i = 0; i < depth; i++) sb.Append("  ");
        }

        public static string NumberText(double n)
        {
            if (n == Math.Floor(n) && Math.Abs(n) < 9e15) return ((long)n).ToString(CultureInfo.InvariantCulture);
            return n.ToString("R", CultureInfo.InvariantCulture);
        }

        static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (var c in s)
            {
                if (c == '"') sb.Append("\\\"");
                else if (c == '\\') sb.Append("\\\\");
                else if (c == '\n') sb.Append("\\n");
                else if (c == '\r') sb.Append("\\r");
                else if (c == '\t') sb.Append("\\t");
                else if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                else sb.Append(c);
            }
            sb.Append('"');
        }
    }
}
