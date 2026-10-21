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
                if (t != null && !t.scene && !t.name.StartsWith("UnityEngine.") && t.name != "Transform") yield return t;
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
        static GUIStyle header;
        static GUIStyle stale; // A field the component no longer has

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
            using (new EditorGUI.DisabledScope(playing))
            {
                for (var i = 0; i < components.arraySize; i++)
                {
                    if (i > 0) EditorGUILayout.Space(2);
                    DrawComponent(entity, schema, references, i);
                }
                EditorGUILayout.Space();
                using (new EditorGUILayout.HorizontalScope())
                {
                    GUILayout.FlexibleSpace();
                    if (GUILayout.Button("Add Tide Component", GUILayout.Width(200))) AddMenu(schema, components);
                    GUILayout.FlexibleSpace();
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

        // A component as the inspector sees it: what tide reads (the prefab's values, with the instance's fields over
        // them) and what writing it changes.
        sealed class Part
        {
            public SerializedProperty property; // Its own values
            public string type;
            public TideSchemaType schemaType;
            public bool instance, patching;
            public bool sourced; // A component the prefab has, which the instance's fields patch: reverting gives its values
            public string inherited, own, values;
            public string patch; // What the instance (or anything else) writes over what's under it
            public byte[] defaults;
        }

        Part Read(int index)
        {
            var components = serializedObject.FindProperty("components");
            if (index < 0 || index >= components.arraySize) return null;
            var entity = (TideEntity)target;
            var element = components.GetArrayElementAtIndex(index);
            var schema = TideEditorSchema.Current;
            var p = new Part { property = element.FindPropertyRelative("values"), type = element.FindPropertyRelative("type").stringValue };
            p.schemaType = schema?.Type(schema.TypeIndex(p.type));
            p.instance = PrefabUtility.IsPartOfPrefabInstance(entity);
            p.inherited = p.instance ? Inherited(entity, index, p.type) : "";
            p.own = p.property.stringValue;
            p.patching = p.instance && p.property.prefabOverride;
            p.values = p.instance ? (p.patching ? TideLiterals.Merge(p.inherited, p.own) : p.inherited) : p.own;
            p.sourced = p.instance && Sourced(entity, index, p.type);
            p.patch = p.instance ? (p.patching ? p.own : "") : p.own;
            p.defaults = p.schemaType?.DefaultBytes();
            return p;
        }

        // What tide reads for the `index`th component.
        internal string Values(int index) => Read(index)?.values;

        // What Reset (`field` null: every field) writes: each field its declared default. Null where it changes nothing.
        internal string Resetting(int index, string field = null)
        {
            var p = Read(index);
            if (p == null) return null;
            var schema = TideEditorSchema.Current;
            var under = p.sourced ? p.inherited : "";
            string next;
            if (field != null)
            {
                var f = p.schemaType?.fields.FirstOrDefault(x => x.name == field);
                if (f == null || TideLiterals.Get(p.values, field) == null) return null;
                next = Defaulted(p.patch, f, under, Default(schema, f, p.defaults));
            }
            else if (!p.sourced) next = "";
            else
            {
                next = p.patch;
                foreach (var f in p.schemaType?.fields ?? Array.Empty<TideSchemaField>())
                {
                    next = Defaulted(next, f, under, Default(schema, f, p.defaults)) ?? next;
                }
            }
            return next != null && next != p.patch ? next : null;
        }

        // What Revert to Prefab (`field` null: every field) writes in a prefab instance: the prefab's. Null where it
        // changes nothing.
        internal string Reverting(int index, string field = null)
        {
            var p = Read(index);
            if (p == null || !p.sourced || !p.patching) return null;
            var next = field == null ? "" : TideLiterals.Set(p.patch, field, null);
            return next != p.patch ? next : null;
        }

        internal void ResetFields(int index, string field = null)
        {
            serializedObject.Update();
            var next = Resetting(index, field);
            if (next != null) Write(index, next);
        }

        internal void RevertFields(int index, string field = null)
        {
            serializedObject.Update();
            var next = Reverting(index, field);
            if (next != null) Write(index, next);
        }

        // Writes the `index`th component's own values: in a prefab instance, none reverts its override, so it follows
        // its prefab again. Either is one edit, as a field's is: a prefab asset (a variant) is saved once.
        void Write(int index, string values)
        {
            var p = Read(index);
            if (p == null) return;
            if (p.sourced && string.IsNullOrEmpty(values)) p.property.prefabOverride = false;
            else p.property.stringValue = values;
            serializedObject.ApplyModifiedProperties();
            serializedObject.Update(); // Whether it overrides its prefab now
        }

        void DrawComponent(TideEntity entity, TideSchema schema, SerializedProperty references, int index)
        {
            var p = Read(index);
            var typeName = p.type;
            var type = p.schemaType;
            var key = typeName + "@" + entity.GetHashCode(); // Its instance, as GetInstanceID was
            folded.TryGetValue(key, out var closed);

            // Its name, folding it, and a menu (⋮ or a right click): Reset, Revert to Prefab, Remove Component
            header ??= new GUIStyle(EditorStyles.foldout) { fontStyle = FontStyle.Bold };
            var line = EditorGUILayout.GetControlRect();
            var options = new Rect(line.xMax - 16, line.y + 1, 16, line.height);
            var e = Event.current;
            if (GUI.Button(options, EditorGUIUtility.IconContent("_Menu"), EditorStyles.iconButton)
                || (GUI.enabled && e.type == EventType.ContextClick && line.Contains(e.mousePosition)))
            {
                var menu = new GenericMenu();
                Item(menu, "Reset", Resetting(index) != null, () => ResetFields(index));
                if (p.sourced) Item(menu, "Revert to Prefab", Reverting(index) != null, () => RevertFields(index));
                menu.AddSeparator("");
                menu.AddItem(new GUIContent("Remove Component"), false, () =>
                {
                    serializedObject.Update();
                    var list = serializedObject.FindProperty("components");
                    if (index < list.arraySize && list.GetArrayElementAtIndex(index).FindPropertyRelative("type").stringValue == typeName)
                    {
                        list.DeleteArrayElementAtIndex(index);
                        serializedObject.ApplyModifiedProperties();
                    }
                });
                menu.ShowAsContext();
                if (e.type == EventType.ContextClick) e.Use();
            }
            line.xMax -= 20;
            closed = !EditorGUI.Foldout(line, !closed, string.IsNullOrEmpty(typeName) ? "(no type)" : typeName, true, header);
            folded[key] = closed;
            if (type == null && schema != null)
            {
                EditorGUILayout.HelpBox($"The game has no component {typeName}: it was renamed or removed. tide says so as it builds.", MessageType.Warning);
            }
            if (closed) return;

            using (new EditorGUI.IndentLevelScope())
            {
                if (type == null)
                {
                    EditorGUI.BeginChangeCheck();
                    var edited = EditorGUILayout.DelayedTextField("Values", p.values);
                    if (EditorGUI.EndChangeCheck()) p.property.stringValue = edited;
                    return;
                }
                foreach (var field in type.fields)
                {
                    var literal = TideLiterals.Get(p.values, field.name);
                    var instanceSets = p.patching && TideLiterals.Get(p.own, field.name) != null;
                    var changed = DrawField(schema, field, literal, Default(schema, field, p.defaults), references, instanceSets, out var next);
                    // A right click: Reset to Default, and in a prefab instance, Revert to Prefab
                    if (GUI.enabled && e.type == EventType.ContextClick && GUILayoutUtility.GetLastRect().Contains(e.mousePosition))
                    {
                        var menu = new GenericMenu();
                        var name = field.name;
                        Item(menu, "Reset to Default", Resetting(index, name) != null, () => ResetFields(index, name));
                        if (p.sourced) Item(menu, "Revert to Prefab", Reverting(index, name) != null, () => RevertFields(index, name));
                        menu.ShowAsContext();
                        e.Use();
                    }
                    if (!changed) continue;
                    // A prefab instance keeps the fields it sets; anything else keeps them all
                    p.property.stringValue = TideLiterals.Set(p.patch, field.name, next);
                }
                // What it sets of fields the component no longer has (renamed or removed in code), which tide
                // refuses until they're gone: red, with Remove where this object sets it
                foreach (var set in TideLiterals.Split(p.values))
                {
                    if (type.Field(set.Key) != null) continue;
                    using (new EditorGUILayout.HorizontalScope())
                    {
                        stale ??= new GUIStyle(EditorStyles.label) { normal = { textColor = new Color(1f, 0.42f, 0.38f) } };
                        var label = new GUIContent(set.Key, $"{typeName} has no field '{set.Key}' now: it was renamed or removed. " +
                                                            "tide refuses the value until it's gone.");
                        EditorGUILayout.LabelField(label, new GUIContent(set.Value + "   (no such field)"), stale);
                        if (TideLiterals.Get(p.patch, set.Key) != null)
                        {
                            var name = set.Key;
                            if (GUILayout.Button("Remove", EditorStyles.miniButton, GUILayout.Width(64))) Write(index, TideLiterals.Set(p.patch, name, null));
                        }
                        else
                        {
                            GUILayout.Label("its prefab sets it", EditorStyles.miniLabel, GUILayout.Width(96));
                        }
                    }
                }
            }
        }

        static void Item(GenericMenu menu, string label, bool on, GenericMenu.MenuFunction run)
        {
            if (on) menu.AddItem(new GUIContent(label), false, run);
            else menu.AddDisabledItem(new GUIContent(label));
        }

        // `patch` with `field` back to its declared default (`declared`, its literal): left out where what's under it
        // (`under`, a prefab's values) doesn't set it or sets the default, the default itself where it sets another.
        // Null where it can't say the default under a prefab that sets it (text).
        static string Defaulted(string patch, TideSchemaField field, string under, string declared)
        {
            var below = TideLiterals.Get(under, field.name);
            if (below == null || below == declared) return TideLiterals.Set(patch, field.name, null);
            return declared != null ? TideLiterals.Set(patch, field.name, declared) : null;
        }

        // Whether the prefab a TideEntity is an instance of has its `index`th component, of `type`.
        static bool Sourced(TideEntity entity, int index, string type)
        {
            var source = PrefabUtility.GetCorrespondingObjectFromSource(entity);
            return source != null && index < source.components.Count && source.components[index].type == type;
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
        internal static string Default(TideSchema schema, TideSchemaField field, byte[] defaults)
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
            var label = new GUIContent(ObjectNames.NicifyVariableName(field.name),
                                       field.type + (set ? " (right click: Reset to Default)" : " (its default)"));
            var bold = instanceSets ? EditorStyles.boldFont : null;
            var before = EditorStyles.label.font;
            if (bold != null) EditorStyles.label.font = bold;
            EditorGUI.BeginChangeCheck();
            next = FieldWidget(schema, field, label, set ? literal : shown, references);
            var changed = EditorGUI.EndChangeCheck();
            if (bold != null) EditorStyles.label.font = before;
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
