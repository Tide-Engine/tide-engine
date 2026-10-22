using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Tide.Editor
{
    // Makes the components a project's TideEntity objects list (type and values) the game's own components, which
    // tide writes into Packages/com.tide.game: each listed component becomes its component with its fields, and a
    // prefab instance's fields over its prefab's become the instance's overrides of the prefab's component. Prefabs go
    // before what instances them, so an instance finds its prefab's components there. What it can't read stays listed,
    // as tide reads it as before: a component the game hasn't made yet (or Transform, which is the GameObject's), or
    // a field written as an expression rather than a value.
    // Tools > Tide > Convert TideEntity Lists to Components, or
    // Unity.exe -batchmode -projectPath . -executeMethod Tide.Editor.TideMigration.ConvertProject -quit
    public static class TideMigration
    {
        [Serializable]
        internal sealed class MapField
        {
            public string name, key, form, @enum, element, elementType;
        }

        [Serializable]
        internal sealed class MapComponent
        {
            public string name, guid;
            public MapField[] fields;
        }

        [Serializable]
        internal sealed class MapEnum
        {
            public string name;
            public string[] members;
            public long[] values;
        }

        [Serializable]
        internal sealed class Map
        {
            public string hash;
            public MapComponent[] components;
            public MapEnum[] enums;
        }

        enum Kind
        {
            Own,       // An entity of the asset's own: all its fields
            Added,     // An instance's, which its prefab hasn't there
            Patch,     // An instance's fields over its prefab's
            Inherited, // Its prefab's, as it is
        }

        sealed class Entry
        {
            public string type, values;
            public Kind kind;
        }

        // A TideEntity as it was before anything changed: its GameObject by the path to it in its asset, and each
        // reference likewise (null for none, or for what isn't in the asset).
        sealed class Recorded
        {
            public string path;
            public bool instance;
            public List<Entry> entries = new List<Entry>();
            public List<string> sourceTypes = new List<string>(); // Its prefab's list
            public List<string> references = new List<string>();
        }

        sealed class Plan
        {
            public string path;
            public bool scene;
            public int depth;
            public List<Recorded> entities = new List<Recorded>();
        }

        sealed class Result
        {
            public int converted, assets;
            public readonly List<string> kept = new List<string>();
        }

        const string EntityScript = "4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e"; // TideEntity.cs.meta's

        [MenuItem("Tools/Tide/Convert TideEntity Lists to Components")]
        static void Menu()
        {
            if (!EditorUtility.DisplayDialog("Convert TideEntity lists to components",
                    "Every prefab and scene's TideEntity components become the game's own components (Add Component > " +
                    "Tide), and its prefabs and scenes are saved. Commit or back up the project first.", "Convert", "Cancel"))
            {
                return;
            }
            if (!EditorSceneManager.SaveCurrentModifiedScenesIfUserWantsTo()) return;
            var result = Convert("Assets/", out var error);
            if (error != null)
            {
                EditorUtility.DisplayDialog("Convert TideEntity lists to components", error, "OK");
                return;
            }
            var kept = result.kept.Count == 0 ? "" : $"\n\n{result.kept.Count} stay listed: see the Console.";
            EditorUtility.DisplayDialog("Convert TideEntity lists to components",
                $"{result.converted} components in {result.assets} prefabs and scenes converted.{kept}", "OK");
        }

        public static void ConvertProject()
        {
            var result = Convert("Assets/", out var error);
            if (error != null)
            {
                Debug.LogError("Tide: " + error);
                EditorApplication.Exit(1);
                return;
            }
            Debug.Log($"Tide: {result.converted} components in {result.assets} prefabs and scenes converted, {result.kept.Count} stay listed");
        }

        static Dictionary<string, Type> generated;
        static DateTime generatedSeen;

        // The game's own component of a Tide name, as tide made it into com.tide.game and Unity compiled it, or null.
        internal static Type ComponentType(string name)
        {
            var path = Path.Combine(TideSettings.ProjectFolder, "Packages", "com.tide.game", "components.json");
            try
            {
                var written = File.Exists(path) ? File.GetLastWriteTimeUtc(path) : default;
                if (written != generatedSeen || generated == null)
                {
                    generatedSeen = written;
                    generated = new Dictionary<string, Type>();
                    var map = written == default ? null : JsonUtility.FromJson<Map>(File.ReadAllText(path));
                    foreach (var c in map?.components ?? Array.Empty<MapComponent>())
                    {
                        var script = AssetDatabase.LoadAssetAtPath<MonoScript>(AssetDatabase.GUIDToAssetPath(c.guid));
                        var type = script != null ? script.GetClass() : null;
                        if (type != null) generated[c.name] = type;
                    }
                }
            }
            catch (IOException)
            {
                // tide is writing it: what was read will do
            }
            return name != null && generated != null && generated.TryGetValue(name, out var t) ? t : null;
        }

        // The prefabs and scenes under `folder` alone ("Assets/Levels/"): how many components it converted, and what
        // stays listed.
        internal static int ConvertFolder(string folder, out List<string> kept, out string error)
        {
            var result = Convert(folder, out error);
            kept = result.kept;
            return result.converted;
        }

        static Result Convert(string folder, out string error)
        {
            error = null;
            var result = new Result();
            var mapPath = Path.Combine(TideSettings.ProjectFolder, "Packages", "com.tide.game", "components.json");
            if (!File.Exists(mapPath))
            {
                error = "The game hasn't been built for Unity yet: tide writes its components into Packages/com.tide.game as it builds.";
                return result;
            }
            var map = JsonUtility.FromJson<Map>(File.ReadAllText(mapPath));
            var types = new Dictionary<string, (MapComponent map, Type type)>();
            foreach (var c in map.components ?? Array.Empty<MapComponent>())
            {
                var script = AssetDatabase.LoadAssetAtPath<MonoScript>(AssetDatabase.GUIDToAssetPath(c.guid));
                var type = script != null ? script.GetClass() : null;
                if (type == null)
                {
                    error = $"The game's component {c.name} hasn't compiled yet: let Unity finish compiling, then convert again.";
                    return result;
                }
                types[c.name] = (c, type);
            }
            StageUtility.GoToMainStage(); // An open prefab's stage would save over what this writes
            var plans = Candidates(folder);
            try
            {
                for (var i = 0; i < plans.Count; i++)
                {
                    EditorUtility.DisplayProgressBar("Tide", "Reading " + plans[i].path, (float)i / (plans.Count * 2));
                    var plan = plans[i];
                    Visit(plan, scene =>
                    {
                        Record(plan, scene);
                        return false;
                    });
                }
                for (var i = 0; i < plans.Count; i++)
                {
                    var plan = plans[i];
                    if (plan.entities.Count == 0) continue;
                    EditorUtility.DisplayProgressBar("Tide", "Converting " + plan.path, (float)(plans.Count + i) / (plans.Count * 2));
                    var before = result.converted;
                    Visit(plan, scene => Apply(plan, scene, types, map, result));
                    if (result.converted > before) result.assets++;
                }
            }
            finally
            {
                EditorUtility.ClearProgressBar();
            }
            AssetDatabase.SaveAssets();
            foreach (var kept in result.kept) Debug.LogWarning("Tide: " + kept + " stays listed on its TideEntity");
            return result;
        }

        // The project's prefabs and scenes that have TideEntity lists or change an instance's: prefabs first, each
        // after the prefabs it instances.
        static List<Plan> Candidates(string folder)
        {
            var plans = new List<Plan>();
            foreach (var guid in AssetDatabase.FindAssets("t:Prefab").Concat(AssetDatabase.FindAssets("t:Scene")).Distinct())
            {
                var path = AssetDatabase.GUIDToAssetPath(guid);
                if (!path.StartsWith(folder)) continue;
                string text;
                try
                {
                    text = File.ReadAllText(path);
                }
                catch (IOException)
                {
                    continue;
                }
                if (!text.Contains(EntityScript) && !text.Contains("propertyPath: components.Array")
                    && !text.Contains("propertyPath: references.Array"))
                {
                    continue;
                }
                plans.Add(new Plan { path = path, scene = path.EndsWith(".unity") });
            }
            var depths = new Dictionary<string, int>();
            foreach (var p in plans) p.depth = p.scene ? int.MaxValue : Depth(p.path, depths, 0);
            return plans.OrderBy(p => p.depth).ThenBy(p => p.path, StringComparer.Ordinal).ToList();
        }

        static int Depth(string path, Dictionary<string, int> depths, int guard)
        {
            if (depths.TryGetValue(path, out var known)) return known;
            var depth = 0;
            if (guard < 64)
            {
                foreach (var d in AssetDatabase.GetDependencies(path, false))
                {
                    if (d != path && d.EndsWith(".prefab")) depth = Math.Max(depth, Depth(d, depths, guard + 1) + 1);
                }
            }
            depths[path] = depth;
            return depth;
        }

        // Runs `with` on an asset's objects, a prefab's contents or a scene (opened beside the others where it isn't),
        // and saves the asset where it says it changed it.
        static void Visit(Plan plan, Func<Scene, bool> with)
        {
            if (!plan.scene)
            {
                var root = PrefabUtility.LoadPrefabContents(plan.path);
                try
                {
                    if (with(root.scene)) PrefabUtility.SaveAsPrefabAsset(root, plan.path);
                }
                finally
                {
                    PrefabUtility.UnloadPrefabContents(root);
                }
                return;
            }
            var open = SceneManager.GetSceneByPath(plan.path);
            var opened = !open.IsValid() || !open.isLoaded;
            var scene = opened ? EditorSceneManager.OpenScene(plan.path, OpenSceneMode.Additive) : open;
            try
            {
                if (with(scene)) EditorSceneManager.SaveScene(scene);
            }
            finally
            {
                if (opened) EditorSceneManager.CloseScene(scene, true);
            }
        }

        static IEnumerable<TideEntity> Entities(Scene scene) =>
            scene.GetRootGameObjects().SelectMany(r => r.GetComponentsInChildren<TideEntity>(true));

        static string PathOf(GameObject go)
        {
            var parts = new List<int>();
            var t = go.transform;
            for (; t.parent != null; t = t.parent) parts.Add(t.GetSiblingIndex());
            parts.Add(Array.IndexOf(go.scene.GetRootGameObjects(), t.gameObject));
            parts.Reverse();
            return string.Join("/", parts);
        }

        static GameObject Find(Scene scene, string path)
        {
            if (path == null) return null;
            var parts = path.Split('/').Select(int.Parse).ToArray();
            var roots = scene.GetRootGameObjects();
            if (parts[0] < 0 || parts[0] >= roots.Length) return null;
            var t = roots[parts[0]].transform;
            for (var i = 1; i < parts.Length; i++)
            {
                if (parts[i] >= t.childCount) return null;
                t = t.GetChild(parts[i]);
            }
            return t.gameObject;
        }

        static void Record(Plan plan, Scene scene)
        {
            foreach (var entity in Entities(scene))
            {
                var source = PrefabUtility.GetCorrespondingObjectFromSource(entity);
                var r = new Recorded { path = PathOf(entity.gameObject), instance = source != null };
                if (source != null) r.sourceTypes.AddRange(source.components.Select(c => c.type));
                using (var so = new SerializedObject(entity))
                {
                    var list = so.FindProperty("components");
                    for (var i = 0; i < list.arraySize; i++)
                    {
                        var element = list.GetArrayElementAtIndex(i);
                        var values = element.FindPropertyRelative("values");
                        var e = new Entry { type = element.FindPropertyRelative("type").stringValue, values = values.stringValue };
                        var sourced = source != null && i < r.sourceTypes.Count && r.sourceTypes[i] == e.type;
                        e.kind = source == null ? Kind.Own : !sourced ? Kind.Added : values.prefabOverride ? Kind.Patch : Kind.Inherited;
                        r.entries.Add(e);
                    }
                }
                foreach (var referenced in entity.references)
                {
                    r.references.Add(referenced != null && referenced.scene == scene ? PathOf(referenced) : null);
                }
                plan.entities.Add(r);
            }
        }

        // Whether it changed anything.
        static bool Apply(Plan plan, Scene scene, Dictionary<string, (MapComponent map, Type type)> types, Map map, Result result)
        {
            var changed = false;
            foreach (var r in plan.entities)
            {
                var go = Find(scene, r.path);
                var entity = go != null ? go.GetComponent<TideEntity>() : null;
                if (entity == null) continue;
                var kept = new List<Entry>();
                var converted = new HashSet<string>();
                var touched = false;
                foreach (var e in r.entries)
                {
                    var place = $"{e.type} of {go.name} in {plan.path}";
                    if (e.kind == Kind.Inherited)
                    {
                        // Its prefab's, converted there or listed there still. Entities it names are the instance's
                        // references, which it may have changed: its own fields' values now.
                        if (!types.TryGetValue(e.type, out var inherited)) continue;
                        var there = go.GetComponent(inherited.type);
                        if (there == null) continue;
                        converted.Add(e.type);
                        var named = TideLiterals.Join(TideLiterals.Split(e.values).Where(f => f.Value.StartsWith("@")).ToList());
                        if (named.Length == 0) continue;
                        var why = SetFields(there, inherited.map, named, r, scene, go, map);
                        if (why != null) result.kept.Add(place + ": " + why);
                        touched = true;
                        continue;
                    }
                    touched = true;
                    if (!types.TryGetValue(e.type, out var t))
                    {
                        // Transform's values go over the GameObject's placement, as listed
                        if (e.type != "Transform") result.kept.Add(place + ": the game has no such component made for Unity");
                        kept.Add(e);
                        continue;
                    }
                    var existing = go.GetComponent(t.type);
                    if (e.kind == Kind.Patch && existing == null)
                    {
                        kept.Add(e); // Its prefab's stayed listed: so does what goes over it
                        continue;
                    }
                    var component = existing != null ? existing : go.AddComponent(t.type);
                    var problem = SetFields(component, t.map, e.values, r, scene, go, map);
                    if (problem != null)
                    {
                        if (existing == null) UnityEngine.Object.DestroyImmediate(component);
                        result.kept.Add(place + ": " + problem);
                        kept.Add(e);
                        continue;
                    }
                    converted.Add(e.type);
                    result.converted++;
                }
                // What an instance had in place of its prefab's component (another, or none) takes the prefab's away
                for (var i = 0; r.instance && i < r.sourceTypes.Count; i++)
                {
                    var type = r.sourceTypes[i];
                    var still = i < r.entries.Count && r.entries[i].type == type;
                    if (still || converted.Contains(type) || !types.TryGetValue(type, out var t)) continue;
                    var gone = go.GetComponent(t.type);
                    if (gone == null) continue;
                    UnityEngine.Object.DestroyImmediate(gone);
                    touched = true;
                }
                if (!touched) continue;
                Relist(entity, r, kept, scene);
                changed = true;
            }
            return changed;
        }

        // The TideEntity's list as what stayed: an instance's follows its prefab's again, with what it changed of
        // that or added to it.
        static void Relist(TideEntity entity, Recorded r, List<Entry> kept, Scene scene)
        {
            using var so = new SerializedObject(entity);
            var list = so.FindProperty("components");
            var references = so.FindProperty("references");
            if (r.instance)
            {
                PrefabUtility.RevertPropertyOverride(list, InteractionMode.AutomatedAction);
                PrefabUtility.RevertPropertyOverride(references, InteractionMode.AutomatedAction);
                so.Update();
                foreach (var e in kept)
                {
                    var at = -1;
                    for (var i = 0; e.kind == Kind.Patch && i < list.arraySize; i++)
                    {
                        if (list.GetArrayElementAtIndex(i).FindPropertyRelative("type").stringValue == e.type) at = i;
                    }
                    if (at < 0)
                    {
                        at = list.arraySize++;
                        list.GetArrayElementAtIndex(at).FindPropertyRelative("type").stringValue = e.type;
                    }
                    list.GetArrayElementAtIndex(at).FindPropertyRelative("values").stringValue = e.values;
                }
            }
            else
            {
                list.arraySize = kept.Count;
                for (var i = 0; i < kept.Count; i++)
                {
                    list.GetArrayElementAtIndex(i).FindPropertyRelative("type").stringValue = kept[i].type;
                    list.GetArrayElementAtIndex(i).FindPropertyRelative("values").stringValue = kept[i].values;
                }
            }
            // References only what stayed listed names
            if (!kept.Any(e => e.values.Contains("@"))) references.arraySize = r.instance ? references.arraySize : 0;
            else if (r.instance)
            {
                references.arraySize = r.references.Count;
                for (var i = 0; i < r.references.Count; i++) references.GetArrayElementAtIndex(i).objectReferenceValue = Find(scene, r.references[i]);
            }
            so.ApplyModifiedPropertiesWithoutUndo();
        }

        // Sets a component's fields from a TideEntity's values: null, or what it couldn't read, which leaves it as it was.
        static string SetFields(Component component, MapComponent map, string values, Recorded r, Scene scene, GameObject self, Map game)
        {
            using var so = new SerializedObject(component);
            foreach (var f in TideLiterals.Split(values))
            {
                var field = map.fields.FirstOrDefault(x => x.name == f.Key);
                if (field == null) continue; // A field the component no longer has: left out, as tide leaves it out
                var property = so.FindProperty(field.key);
                if (property == null) return $"its field {f.Key} isn't in its C#";
                if (!Set(property, field, field.form, f.Value, r, scene, self, game)) return $"{f.Key} = {f.Value} isn't a value it can hold";
            }
            so.ApplyModifiedPropertiesWithoutUndo();
            return null;
        }

        static bool Set(SerializedProperty p, MapField field, string form, string literal, Recorded r, Scene scene, GameObject self, Map game)
        {
            literal = literal.Trim();
            if (literal == "default" && form != "list" && form != "literal") return Zero(p);
            switch (form)
            {
                case "number":
                    if (!Number(literal, out var n)) return false;
                    if (p.propertyType == SerializedPropertyType.Integer) p.longValue = (long)n;
                    else if (p.propertyType == SerializedPropertyType.Float) p.doubleValue = n;
                    else return false;
                    return true;
                case "bool":
                    if (literal != "true" && literal != "false") return false;
                    p.boolValue = literal == "true";
                    return true;
                case "float2":
                case "int2":
                    if (!Call(literal, form, 2, 0, out var v2)) return false;
                    if (form == "int2") p.vector2IntValue = new Vector2Int((int)v2[0], (int)v2[1]);
                    else p.vector2Value = new Vector2(v2[0], v2[1]);
                    return true;
                case "float3":
                case "int3":
                    if (!Call(literal, form, 3, 0, out var v3)) return false;
                    if (form == "int3") p.vector3IntValue = new Vector3Int((int)v3[0], (int)v3[1], (int)v3[2]);
                    else p.vector3Value = new Vector3(v3[0], v3[1], v3[2]);
                    return true;
                case "float4":
                    if (!Call(literal, form, 4, 0, out var v4)) return false;
                    p.vector4Value = new Vector4(v4[0], v4[1], v4[2], v4[3]);
                    return true;
                case "quaternion":
                    if (!Call(literal, form, 4, 0, out var q)) return false;
                    p.quaternionValue = new Quaternion(q[0], q[1], q[2], q[3]);
                    return true;
                case "color":
                    if (!Call(literal, "Color", 4, 1, out var c)) return false; // Color(r, g, b) is opaque
                    p.colorValue = new Color(c[0], c[1], c[2], c[3]);
                    return true;
                case "string":
                    if (literal.Length < 2 || literal[0] != '"' || literal[literal.Length - 1] != '"') return false;
                    p.stringValue = TideLiterals.Unquote(literal);
                    return true;
                case "literal":
                    p.stringValue = literal == "default" ? "" : literal;
                    return true;
                case "entity":
                    if (literal == "this")
                    {
                        p.objectReferenceValue = self;
                        return true;
                    }
                    if (literal.Length < 2 || literal[0] != '@' || !int.TryParse(literal.Substring(1), out var k)) return false;
                    if (k < 0 || k >= r.references.Count) return false;
                    p.objectReferenceValue = Find(scene, r.references[k]);
                    return true;
                case "enum":
                {
                    var e = game.enums?.FirstOrDefault(x => x.name == field.@enum);
                    var member = literal.Substring(literal.LastIndexOf('.') + 1);
                    var at = e == null ? -1 : Array.IndexOf(e.members, member);
                    if (at < 0 || at >= e.values.Length) return false;
                    p.intValue = (int)e.values[at];
                    return true;
                }
                case "list":
                {
                    if (literal == "default" || literal == "[]")
                    {
                        p.arraySize = 0;
                        return true;
                    }
                    if (literal.Length < 2 || literal[0] != '[' || literal[literal.Length - 1] != ']') return false;
                    var items = Items(literal.Substring(1, literal.Length - 2));
                    p.arraySize = items.Count;
                    for (var i = 0; i < items.Count; i++)
                    {
                        if (!Set(p.GetArrayElementAtIndex(i), field, field.element, items[i], r, scene, self, game)) return false;
                    }
                    return true;
                }
            }
            return false;
        }

        static bool Zero(SerializedProperty p)
        {
            switch (p.propertyType)
            {
                case SerializedPropertyType.Integer: p.longValue = 0; return true;
                case SerializedPropertyType.Float: p.doubleValue = 0; return true;
                case SerializedPropertyType.Boolean: p.boolValue = false; return true;
                case SerializedPropertyType.Enum: p.intValue = 0; return true;
                case SerializedPropertyType.Vector2: p.vector2Value = default; return true;
                case SerializedPropertyType.Vector3: p.vector3Value = default; return true;
                case SerializedPropertyType.Vector4: p.vector4Value = default; return true;
                case SerializedPropertyType.Vector2Int: p.vector2IntValue = default; return true;
                case SerializedPropertyType.Vector3Int: p.vector3IntValue = default; return true;
                case SerializedPropertyType.Quaternion: p.quaternionValue = default; return true;
                case SerializedPropertyType.Color: p.colorValue = default; return true;
                case SerializedPropertyType.String: p.stringValue = ""; return true;
                case SerializedPropertyType.ObjectReference: p.objectReferenceValue = null; return true;
            }
            return false;
        }

        // A number as Tide writes one: 2, -0.5, 1e3, 2.5f, 0x1F, 1_000.
        static bool Number(string literal, out double value)
        {
            var s = literal.Replace("_", "");
            var negative = s.StartsWith("-");
            var digits = negative ? s.Substring(1) : s;
            if (digits.StartsWith("0x") || digits.StartsWith("0X"))
            {
                var ok = ulong.TryParse(digits.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var hex);
                value = negative ? -(double)hex : hex;
                return ok;
            }
            if (s.EndsWith("f") || s.EndsWith("F")) s = s.Substring(0, s.Length - 1);
            return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out value) && !double.IsNaN(value)
                   && !double.IsInfinity(value);
        }

        // "float3(1, 2, 3)": its numbers, as many as `count` (one is each of them; Color's fourth is `last` if left out).
        static bool Call(string literal, string name, int count, float last, out float[] numbers)
        {
            numbers = new float[count];
            if (!literal.StartsWith(name + "(") || !literal.EndsWith(")")) return false;
            var items = Items(literal.Substring(name.Length + 1, literal.Length - name.Length - 2));
            var single = items.Count == 1;
            var fourth = name == "Color" && items.Count == 3;
            if (!single && !fourth && items.Count != count) return false;
            for (var i = 0; i < count; i++)
            {
                if (fourth && i == 3)
                {
                    numbers[i] = last;
                    continue;
                }
                if (!Number(items[single ? 0 : i], out var n)) return false;
                numbers[i] = (float)n;
            }
            return true;
        }

        // A list's items, split at its commas outside brackets and quotes.
        static List<string> Items(string inner)
        {
            var items = new List<string>();
            var depth = 0;
            var quote = '\0';
            var start = 0;
            for (var i = 0; i <= inner.Length; i++)
            {
                var c = i < inner.Length ? inner[i] : ',';
                if (quote != '\0' && i < inner.Length)
                {
                    if (c == '\\') i++;
                    else if (c == quote) quote = '\0';
                    continue;
                }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') depth--;
                else if (c == ',' && depth == 0)
                {
                    var item = inner.Substring(start, i - start).Trim();
                    if (item.Length > 0 || i < inner.Length) items.Add(item);
                    start = i + 1;
                }
            }
            return items;
        }
    }
}
