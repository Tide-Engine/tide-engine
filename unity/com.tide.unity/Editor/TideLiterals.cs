using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Tide.Editor
{
    // A TideEntity component's values, "a = 1, b = float3(1, 2, 3)": each field by its name and its Tide literal, as
    // tide unity reads them (compiler/cli/unity_assets.c splits them the same way).
    public static class TideLiterals
    {
        public static List<KeyValuePair<string, string>> Split(string values)
        {
            var fields = new List<KeyValuePair<string, string>>();
            if (string.IsNullOrEmpty(values)) return fields;
            var depth = 0;
            var quote = '\0';
            var start = 0;
            for (var i = 0; i <= values.Length; i++)
            {
                var c = i < values.Length ? values[i] : '\0';
                if (quote != '\0' && c != '\0')
                {
                    if (c == '\\') i++;
                    else if (c == quote) quote = '\0';
                    continue;
                }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') depth--;
                if (c != '\0' && !((c == ',' || c == ';') && depth == 0)) continue;
                var part = values.Substring(start, i - start).Trim();
                start = i + 1;
                if (part.Length == 0) continue;
                var eq = part.IndexOf('=');
                if (eq < 0) fields.Add(new KeyValuePair<string, string>(part, ""));
                else fields.Add(new KeyValuePair<string, string>(part.Substring(0, eq).Trim(), part.Substring(eq + 1).Trim()));
            }
            return fields;
        }

        public static string Join(List<KeyValuePair<string, string>> fields)
        {
            var s = new StringBuilder();
            foreach (var f in fields)
            {
                if (s.Length > 0) s.Append(", ");
                s.Append(f.Key).Append(" = ").Append(f.Value);
            }
            return s.ToString();
        }

        public static string Get(string values, string name)
        {
            foreach (var f in Split(values))
            {
                if (f.Key == name) return f.Value;
            }
            return null;
        }

        // `values` with `name` set to `literal`, or left out for null.
        public static string Set(string values, string name, string literal)
        {
            var fields = Split(values);
            var at = fields.FindIndex(f => f.Key == name);
            if (literal == null)
            {
                if (at >= 0) fields.RemoveAt(at);
            }
            else if (at >= 0)
            {
                fields[at] = new KeyValuePair<string, string>(name, literal);
            }
            else
            {
                fields.Add(new KeyValuePair<string, string>(name, literal));
            }
            return Join(fields);
        }

        // `patch`'s fields over `values`', as tide merges a prefab instance's over its prefab's.
        public static string Merge(string values, string patch)
        {
            var fields = Split(values);
            foreach (var p in Split(patch))
            {
                var at = fields.FindIndex(f => f.Key == p.Key);
                if (at >= 0) fields[at] = p;
                else fields.Add(p);
            }
            return Join(fields);
        }

        public static string Number(float value)
        {
            if (float.IsNaN(value) || float.IsInfinity(value)) value = 0;
            return value.ToString("0.#########", CultureInfo.InvariantCulture);
        }

        public static string Call(string name, params float[] numbers)
        {
            var s = new StringBuilder(name).Append('(');
            for (var i = 0; i < numbers.Length; i++)
            {
                if (i > 0) s.Append(", ");
                s.Append(Number(numbers[i]));
            }
            return s.Append(')').ToString();
        }

        // The numbers of a literal like "float3(1, 2, 3)", "Color(1, 0, 0)" or "2.5": as many as `count`, the rest
        // `otherwise`.
        public static float[] Numbers(string literal, int count, float otherwise = 0)
        {
            var numbers = new float[count];
            for (var i = 0; i < count; i++) numbers[i] = otherwise;
            if (string.IsNullOrEmpty(literal)) return numbers;
            var open = literal.IndexOf('(');
            var inner = open >= 0 && literal.EndsWith(")") ? literal.Substring(open + 1, literal.Length - open - 2) : literal;
            var parts = inner.Split(',');
            var single = parts.Length == 1 && open >= 0; // float3(1) is (1, 1, 1)
            for (var i = 0; i < count && (i < parts.Length || single); i++)
            {
                var part = (single ? parts[0] : parts[i]).Trim().TrimEnd('f', 'F');
                if (float.TryParse(part, NumberStyles.Float, CultureInfo.InvariantCulture, out var n)) numbers[i] = n;
            }
            return numbers;
        }

        public static string Text(string value)
        {
            var s = new StringBuilder("\"");
            foreach (var c in value ?? "")
            {
                if (c == '"' || c == '\\') s.Append('\\').Append(c);
                else if (c == '\n') s.Append("\\n");
                else if (c == '\t') s.Append("\\t");
                else s.Append(c);
            }
            return s.Append('"').ToString();
        }

        public static string Unquote(string literal)
        {
            if (string.IsNullOrEmpty(literal) || literal.Length < 2 || literal[0] != '"') return "";
            var s = new StringBuilder();
            for (var i = 1; i < literal.Length - 1; i++)
            {
                var c = literal[i];
                if (c == '\\' && i + 1 < literal.Length - 1)
                {
                    var next = literal[++i];
                    s.Append(next == 'n' ? '\n' : next == 't' ? '\t' : next);
                }
                else
                {
                    s.Append(c);
                }
            }
            return s.ToString();
        }
    }
}
