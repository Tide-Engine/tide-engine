using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Tide.Generator
{
    // Just enough JSON for the schema tide writes: objects, arrays, strings, numbers, true, false and null.
    // Objects are Dictionary<string, object>, arrays List<object>, numbers double.
    static class Json
    {
        public static object Parse(string text)
        {
            var at = 0;
            var value = Value(text, ref at);
            Space(text, ref at);
            return at == text.Length ? value : null;
        }

        static void Space(string s, ref int at)
        {
            while (at < s.Length && char.IsWhiteSpace(s[at])) at++;
        }

        static object Value(string s, ref int at)
        {
            Space(s, ref at);
            if (at >= s.Length) return null;
            switch (s[at])
            {
                case '{':
                {
                    var o = new Dictionary<string, object>();
                    at++;
                    Space(s, ref at);
                    if (at < s.Length && s[at] == '}') { at++; return o; }
                    while (at < s.Length)
                    {
                        Space(s, ref at);
                        var key = String(s, ref at);
                        Space(s, ref at);
                        if (key == null || at >= s.Length || s[at] != ':') return null;
                        at++;
                        o[key] = Value(s, ref at);
                        Space(s, ref at);
                        if (at < s.Length && s[at] == ',') { at++; continue; }
                        if (at < s.Length && s[at] == '}') { at++; return o; }
                        return null;
                    }
                    return null;
                }
                case '[':
                {
                    var a = new List<object>();
                    at++;
                    Space(s, ref at);
                    if (at < s.Length && s[at] == ']') { at++; return a; }
                    while (at < s.Length)
                    {
                        a.Add(Value(s, ref at));
                        Space(s, ref at);
                        if (at < s.Length && s[at] == ',') { at++; continue; }
                        if (at < s.Length && s[at] == ']') { at++; return a; }
                        return null;
                    }
                    return null;
                }
                case '"':
                    return String(s, ref at);
                case 't':
                    at += 4;
                    return true;
                case 'f':
                    at += 5;
                    return false;
                case 'n':
                    at += 4;
                    return null;
                default:
                {
                    var start = at;
                    while (at < s.Length && "+-0123456789.eE".IndexOf(s[at]) >= 0) at++;
                    return double.TryParse(s.Substring(start, at - start), NumberStyles.Float, CultureInfo.InvariantCulture, out var d) ? (object)d : null;
                }
            }
        }

        static string String(string s, ref int at)
        {
            if (at >= s.Length || s[at] != '"') return null;
            at++;
            var b = new StringBuilder();
            while (at < s.Length && s[at] != '"')
            {
                var c = s[at++];
                if (c != '\\' || at >= s.Length) { b.Append(c); continue; }
                var e = s[at++];
                switch (e)
                {
                    case 'n': b.Append('\n'); break;
                    case 't': b.Append('\t'); break;
                    case 'r': b.Append('\r'); break;
                    case 'b': b.Append('\b'); break;
                    case 'f': b.Append('\f'); break;
                    case 'u':
                        if (at + 4 <= s.Length && int.TryParse(s.Substring(at, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var u))
                        {
                            b.Append((char)u);
                            at += 4;
                        }
                        break;
                    default: b.Append(e); break;
                }
            }
            at++;
            return b.ToString();
        }
    }
}
