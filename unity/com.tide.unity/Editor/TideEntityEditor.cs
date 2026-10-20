using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // The newest build's schema, as the editor's inspectors read it.
    public static class TideEditorSchema
    {
        static TideSchema schema;
        static DateTime seen;

        public static TideSchema Current
        {
            get
            {
                var path = TideSchemaWatcher.SchemaPath;
                try
                {
                    var written = File.Exists(path) ? File.GetLastWriteTimeUtc(path) : default;
                    if (written != seen)
                    {
                        seen = written;
                        schema = written == default ? null : TideSchema.Parse(File.ReadAllText(path));
                    }
                }
                catch (IOException)
                {
                    // tide is writing it: the last one will do
                }
                return schema;
            }
        }

        // The match components an entity can be given in Unity: not tide's UnityEngine ones (Source, which tide gives
        // each, and the scenes, which are their own entities), nor
        // Transform, which is the GameObject's.
        public static IEnumerable<TideSchemaType> Components(TideSchema s)
        {
            if (s == null) yield break;
            foreach (var index in s.components)
            {
                var t = s.Type(index);
                if (t != null && !t.name.StartsWith("UnityEngine.") && t.name != "Transform") yield return t;
            }
        }
    }

    // TideEntity's inspector: its Tide components, each field by what its type is
    // in the game's schema. In a prefab instance, what it changes is the fields
    // the instance sets, over its prefab's.
    [CustomEditor(typeof(TideEntity))]
    [CanEditMultipleObjects]
    public sealed class TideEntityEditor : UnityEditor.Editor
    {
        static readonly Dictionary<string, bool> folded = new Dictionary<string, bool>();
        bool raw;

        public override void OnInspectorGUI()
        {
            serializedObject.Update();
            var schema = TideEditorSchema.Current;
            if (schema == null)
            {
                EditorGUILayout.HelpBox("The game hasn't been built yet: its components show once tide has built it " +
                                        "(Project Settings > Tide).", MessageType.Info);
            }
            if (targets.Length > 1)
            {
                EditorGUILayout.HelpBox("Select one entity to edit its components.", MessageType.None);
                return;
            }
            var entity = (TideEntity)target;
            var components = serializedObject.FindProperty("components");
            var references = serializedObject.FindProperty("references");
            // In Play the match has its entities already: what's here is what a match starts with, edited outside Play
            var playing = EditorApplication.isPlayingOrWillChangePlaymode;
            if (playing) EditorGUILayout.LabelField("What a match starts with: edit it outside Play", EditorStyles.miniLabel);
            using (new EditorGUI.DisabledScope(playing))
            {
                for (var i = 0; i < components.arraySize; i++)
                {
                    if (!DrawComponent(entity, schema, components, references, i)) break; // Removed: the rest next time
                }
                EditorGUILayout.Space();
                using (new EditorGUILayout.HorizontalScope())
                {
                    GUILayout.FlexibleSpace();
                    if (GUILayout.Button("Add Tide Component", GUILayout.Width(200))) AddMenu(schema, components);
                    GUILayout.FlexibleSpace();
                }
            }
            raw = EditorGUILayout.Foldout(raw, "As tide reads it", true);
            if (raw)
            {
                using (new EditorGUI.IndentLevelScope())
                using (new EditorGUI.DisabledScope(playing))
                {
                    EditorGUILayout.PropertyField(components, true);
                    EditorGUILayout.PropertyField(references, true);
                }
            }
            serializedObject.ApplyModifiedProperties();
        }

        void AddMenu(TideSchema schema, SerializedProperty components)
        {
            var menu = new GenericMenu();
            var have = new HashSet<string>();
            for (var i = 0; i < components.arraySize; i++) have.Add(components.GetArrayElementAtIndex(i).FindPropertyRelative("type").stringValue);
            foreach (var t in TideEditorSchema.Components(schema).OrderBy(t => t.name))
            {
                var name = t.name;
                if (have.Contains(name)) menu.AddDisabledItem(new GUIContent(name.Replace('.', '/')));
                else menu.AddItem(new GUIContent(name.Replace('.', '/')), false, () => Add(name));
            }
            if (menu.GetItemCount() == 0) menu.AddDisabledItem(new GUIContent("The game has no components yet"));
            menu.ShowAsContext();
        }

        void Add(string type)
        {
            serializedObject.Update();
            var components = serializedObject.FindProperty("components");
            components.arraySize++;
            var added = components.GetArrayElementAtIndex(components.arraySize - 1);
            added.FindPropertyRelative("type").stringValue = type;
            added.FindPropertyRelative("values").stringValue = "";
            serializedObject.ApplyModifiedProperties();
        }

        bool DrawComponent(TideEntity entity, TideSchema schema, SerializedProperty components, SerializedProperty references, int index)
        {
            var element = components.GetArrayElementAtIndex(index);
            var typeProperty = element.FindPropertyRelative("type");
            var valuesProperty = element.FindPropertyRelative("values");
            var typeName = typeProperty.stringValue;
            var type = schema?.Type(schema.TypeIndex(typeName));
            var key = typeName + "@" + entity.GetHashCode(); // Its instance, as GetInstanceID was
            folded.TryGetValue(key, out var closed);
            using (new EditorGUILayout.HorizontalScope(EditorStyles.toolbar))
            {
                closed = !EditorGUILayout.Foldout(!closed, string.IsNullOrEmpty(typeName) ? "(no type)" : typeName, true);
                folded[key] = closed;
                GUILayout.FlexibleSpace();
                if (GUILayout.Button("Remove", EditorStyles.toolbarButton, GUILayout.Width(60)))
                {
                    components.DeleteArrayElementAtIndex(index);
                    return false;
                }
            }
            if (type == null && schema != null)
            {
                EditorGUILayout.HelpBox($"The game has no component {typeName}: it was renamed or removed. tide says so as it builds.", MessageType.Warning);
            }
            if (closed) return true;

            // What tide reads: the prefab's values, with the instance's fields over them
            var instance = PrefabUtility.IsPartOfPrefabInstance(entity);
            var inherited = instance ? Inherited(entity, index, typeName) : "";
            var own = valuesProperty.stringValue;
            var patching = instance && valuesProperty.prefabOverride;
            var values = instance ? (patching ? TideLiterals.Merge(inherited, own) : inherited) : own;

            using (new EditorGUI.IndentLevelScope())
            {
                if (type == null)
                {
                    EditorGUI.BeginChangeCheck();
                    var edited = EditorGUILayout.DelayedTextField("Values", values);
                    if (EditorGUI.EndChangeCheck()) valuesProperty.stringValue = edited;
                    return true;
                }
                var defaults = type.DefaultBytes();
                foreach (var field in type.fields)
                {
                    var literal = TideLiterals.Get(values, field.name);
                    var instanceSets = patching && TideLiterals.Get(own, field.name) != null;
                    var changed = DrawField(schema, field, literal, Default(schema, field, defaults), references, instanceSets, out var next);
                    if (!changed) continue;
                    // A prefab instance keeps the fields it sets; anything else keeps them all
                    var basis = instance ? (patching ? own : "") : own;
                    valuesProperty.stringValue = TideLiterals.Set(basis, field.name, next);
                }
            }
            return true;
        }

        // The values the prefab a TideEntity is an instance of gives its `index`th component, as tide merges them.
        static string Inherited(TideEntity entity, int index, string type)
        {
            var source = PrefabUtility.GetCorrespondingObjectFromSource(entity);
            if (source == null || index >= source.components.Count || source.components[index].type != type) return "";
            var own = source.components[index].values ?? "";
            if (!PrefabUtility.IsPartOfPrefabInstance(source)) return own;
            using var so = new SerializedObject(source);
            var values = so.FindProperty("components").GetArrayElementAtIndex(index).FindPropertyRelative("values");
            var inherited = Inherited(source, index, type);
            return values.prefabOverride ? TideLiterals.Merge(inherited, own) : inherited;
        }

        // A field's declared default as a literal, from its component's default bytes: null where it can't say.
        static string Default(TideSchema schema, TideSchemaField field, byte[] defaults)
        {
            if (defaults == null || field.offset + field.size > defaults.Length) return null;
            var at = (int)field.offset;
            float F(int i) => BitConverter.ToSingle(defaults, at + i * 4);
            var invariant = System.Globalization.CultureInfo.InvariantCulture;
            switch (field.type)
            {
            case "float": return TideLiterals.Number(F(0));
            case "int": return BitConverter.ToInt32(defaults, at).ToString(invariant);
            case "uint": return BitConverter.ToUInt32(defaults, at).ToString(invariant);
            case "long": return BitConverter.ToInt64(defaults, at).ToString(invariant);
            case "bool": return defaults[at] != 0 ? "true" : "false";
            case "float2": return TideLiterals.Call("float2", F(0), F(1));
            case "float3": return TideLiterals.Call("float3", F(0), F(1), F(2));
            case "float4": return TideLiterals.Call("float4", F(0), F(1), F(2), F(3));
            case "int2": return TideLiterals.Call("int2", BitConverter.ToInt32(defaults, at), BitConverter.ToInt32(defaults, at + 4));
            case "int3":
                return TideLiterals.Call("int3", BitConverter.ToInt32(defaults, at), BitConverter.ToInt32(defaults, at + 4),
                                         BitConverter.ToInt32(defaults, at + 8));
            case "Color": return TideLiterals.Call("Color", F(0), F(1), F(2), F(3));
            case "quaternion": return TideLiterals.Call("quaternion", F(0), F(1), F(2), F(3));
            }
            if (field.kind == "enum" && field.@ref >= 0 && field.@ref < schema.enums.Length)
            {
                var e = schema.enums[field.@ref];
                var i = Array.IndexOf(e.values, BitConverter.ToInt32(defaults, at));
                return i >= 0 ? e.name + "." + e.members[i] : null;
            }
            return null;
        }

        // One field, from its literal (null: its default, `shown`, which a field nothing sets shows): true with `next`
        // the literal written, null to unset it.
        static bool DrawField(TideSchema schema, TideSchemaField field, string literal, string shown, SerializedProperty references,
                              bool instanceSets, out string next)
        {
            next = literal;
            var set = literal != null;
            var label = new GUIContent(ObjectNames.NicifyVariableName(field.name), field.type + (set ? "" : " (its default)"));
            using var row = new EditorGUILayout.HorizontalScope();
            var bold = instanceSets ? EditorStyles.boldFont : null;
            var before = EditorStyles.label.font;
            if (bold != null) EditorStyles.label.font = bold;
            EditorGUI.BeginChangeCheck();
            next = FieldWidget(schema, field, label, set ? literal : shown, references);
            var changed = EditorGUI.EndChangeCheck();
            if (bold != null) EditorStyles.label.font = before;
            if (set && GUILayout.Button(new GUIContent("×", "Back to its default"), EditorStyles.miniButton, GUILayout.Width(20)))
            {
                next = null;
                return true;
            }
            if (!set) GUILayout.Space(24);
            return changed;
        }

        static string FieldWidget(TideSchema schema, TideSchemaField field, GUIContent label, string literal, SerializedProperty references)
        {
            switch (field.type)
            {
            case "float":
                return TideLiterals.Number(EditorGUILayout.FloatField(label, TideLiterals.Numbers(literal, 1)[0]));
            case "int":
            case "uint":
            case "long":
            {
                var n = (int)TideLiterals.Numbers(literal, 1)[0];
                return EditorGUILayout.IntField(label, n).ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            case "bool":
                return EditorGUILayout.Toggle(label, literal == "true") ? "true" : "false";
            case "float2":
            {
                var v = TideLiterals.Numbers(literal, 2);
                var e = EditorGUILayout.Vector2Field(label, new Vector2(v[0], v[1]));
                return TideLiterals.Call("float2", e.x, e.y);
            }
            case "float3":
            {
                var v = TideLiterals.Numbers(literal, 3);
                var e = EditorGUILayout.Vector3Field(label, new Vector3(v[0], v[1], v[2]));
                return TideLiterals.Call("float3", e.x, e.y, e.z);
            }
            case "float4":
            {
                var v = TideLiterals.Numbers(literal, 4);
                var e = EditorGUILayout.Vector4Field(label, new Vector4(v[0], v[1], v[2], v[3]));
                return TideLiterals.Call("float4", e.x, e.y, e.z, e.w);
            }
            case "int2":
            {
                var v = TideLiterals.Numbers(literal, 2);
                var e = EditorGUILayout.Vector2IntField(label, new Vector2Int((int)v[0], (int)v[1]));
                return TideLiterals.Call("int2", e.x, e.y);
            }
            case "int3":
            {
                var v = TideLiterals.Numbers(literal, 3);
                var e = EditorGUILayout.Vector3IntField(label, new Vector3Int((int)v[0], (int)v[1], (int)v[2]));
                return TideLiterals.Call("int3", e.x, e.y, e.z);
            }
            case "Color":
            {
                var v = TideLiterals.Numbers(literal, 4, 1);
                if (literal == null) v = new[] { 1f, 1f, 1f, 1f };
                var e = EditorGUILayout.ColorField(label, new Color(v[0], v[1], v[2], v[3]));
                return e.a >= 1f ? TideLiterals.Call("Color", e.r, e.g, e.b) : TideLiterals.Call("Color", e.r, e.g, e.b, e.a);
            }
            case "quaternion":
            {
                var v = TideLiterals.Numbers(literal, 4);
                if (literal == null) v[3] = 1;
                var q = new Quaternion(v[0], v[1], v[2], v[3]);
                var e = EditorGUILayout.Vector3Field(label, q.eulerAngles);
                var t = Quaternion.Euler(e);
                return TideLiterals.Call("quaternion", t.x, t.y, t.z, t.w);
            }
            case "string":
                return TideLiterals.Text(EditorGUILayout.TextField(label, TideLiterals.Unquote(literal)));
            }
            if (field.kind == "entity") return EntityWidget(label, literal, references);
            if (field.kind == "enum" && field.@ref >= 0 && field.@ref < schema.enums.Length)
            {
                var e = schema.enums[field.@ref];
                var members = e.members;
                var current = literal != null ? Array.IndexOf(members, literal.Substring(literal.LastIndexOf('.') + 1)) : 0;
                var picked = EditorGUILayout.Popup(label, Math.Max(0, current), members);
                return picked >= 0 && picked < members.Length ? e.name + "." + members[picked] : literal;
            }
            // Anything else as its Tide literal
            return NullIfEmpty(EditorGUILayout.DelayedTextField(label, literal ?? ""));
        }

        static string NullIfEmpty(string s)
        {
            return string.IsNullOrWhiteSpace(s) ? null : s;
        }

        // An entity field: the GameObject of the entity it names, kept in TideEntity.references and written `@n`.
        static string EntityWidget(GUIContent label, string literal, SerializedProperty references)
        {
            GameObject current = null;
            var at = -1;
            if (literal != null && literal.StartsWith("@") && int.TryParse(literal.Substring(1), out at) && at >= 0 && at < references.arraySize)
            {
                current = references.GetArrayElementAtIndex(at).objectReferenceValue as GameObject;
            }
            var self = ((Component)references.serializedObject.targetObject).gameObject;
            if (literal == "this") current = self;
            var picked = (GameObject)EditorGUILayout.ObjectField(label, current, typeof(GameObject), true);
            if (picked == current) return literal;
            if (picked == null) return null;
            if (picked == self) return "this";
            if (picked.GetComponent<TideEntity>() == null)
            {
                Debug.LogWarning($"Tide: {picked.name} has no TideEntity, so it's no entity to name.", picked);
                return literal;
            }
            // The same reference again, else a new one
            for (var i = 0; i < references.arraySize; i++)
            {
                if (references.GetArrayElementAtIndex(i).objectReferenceValue == picked) return "@" + i;
            }
            references.arraySize++;
            references.GetArrayElementAtIndex(references.arraySize - 1).objectReferenceValue = picked;
            return "@" + (references.arraySize - 1);
        }
    }
}
