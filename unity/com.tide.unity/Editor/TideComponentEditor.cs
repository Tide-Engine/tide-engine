using System;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // A Tide component's inspector: Unity's own, its fields as authored, and in Play its entity's values, which the
    // match decides: shown, not edited, as a change there would reach neither the match nor the scene. In Play what
    // its package answers (TideReadBack) and its local fields follow, which Unity doesn't keep. One with a shape
    // (TideShape) has Edit Shape; one of a mesh's lists (TideMesh), a Mesh to copy them from.
    [CustomEditor(typeof(TideComponent), true)]
    [CanEditMultipleObjects]
    sealed class TideComponentEditor : UnityEditor.Editor
    {
        Mesh copyFrom;

        public override void OnInspectorGUI()
        {
            var playing = EditorApplication.isPlayingOrWillChangePlaymode;
            if (!playing && TideShapes.Read(target as TideComponent, out _))
            {
                // Its shape's handles in the Scene view (TideShapeTool), as a collider's Edit Collider
                EditorGUILayout.EditorToolbarForTarget(EditorGUIUtility.TrTempContent("Edit Shape"), this);
            }
            if (!playing) MeshFields();
            using (new EditorGUI.DisabledScope(playing)) DrawDefaultInspector();
            if (!EditorApplication.isPlaying || targets.Length != 1) return;
            var shown = Array.FindAll(target.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public),
                f => f.IsNotSerialized);
            Section("From its package", Array.FindAll(shown, f => f.IsDefined(typeof(TideReadBackAttribute), false)));
            Section("Local fields", Array.FindAll(shown, f => !f.IsDefined(typeof(TideReadBackAttribute), false)));
        }

        void Section(string title, FieldInfo[] fields)
        {
            if (fields.Length == 0) return;
            EditorGUILayout.Space();
            EditorGUILayout.LabelField(title, EditorStyles.boldLabel);
            using (new EditorGUI.DisabledScope(true))
            {
                foreach (var f in fields) Show(ObjectNames.NicifyVariableName(f.Name), f.GetValue(target));
            }
        }

        // A mesh's lists: how many it has, and a Mesh to copy them from (the GameObject's MeshFilter's to begin with)
        void MeshFields()
        {
            var c = target as TideComponent;
            if (!TideMeshes.Read(c, out var lists)) return;
            if (copyFrom == null) copyFrom = TideMeshes.Filtered(c);
            var vertices = TideMeshes.Count(lists.Vertices, c);
            var said = lists.Indices != null
                ? $"{vertices} vertices, {TideMeshes.Count(lists.Indices, c) / 3} triangles" : $"{vertices} vertices";
            using (new EditorGUILayout.HorizontalScope())
            {
                copyFrom = (Mesh)EditorGUILayout.ObjectField(new GUIContent("Mesh", said), copyFrom, typeof(Mesh), false);
                using (new EditorGUI.DisabledScope(copyFrom == null))
                {
                    if (GUILayout.Button("Copy", GUILayout.Width(52)))
                    {
                        foreach (var t in targets)
                        {
                            if (TideMeshes.Read(t as TideComponent, out var each)) TideMeshes.Fill((TideComponent)t, each, copyFrom);
                        }
                    }
                }
            }
            EditorGUILayout.LabelField(" ", said, EditorStyles.miniLabel);
        }

        static void Show(string label, object value)
        {
            switch (value)
            {
                case int i: EditorGUILayout.IntField(label, i); break;
                case long l: EditorGUILayout.LongField(label, l); break;
                case float f: EditorGUILayout.FloatField(label, f); break;
                case double d: EditorGUILayout.DoubleField(label, d); break;
                case bool b: EditorGUILayout.Toggle(label, b); break;
                case string s: EditorGUILayout.TextField(label, s); break;
                case Vector2 v: EditorGUILayout.Vector2Field(label, v); break;
                case Vector3 v: EditorGUILayout.Vector3Field(label, v); break;
                case Vector4 v: EditorGUILayout.Vector4Field(label, v); break;
                case Vector2Int v: EditorGUILayout.Vector2IntField(label, v); break;
                case Vector3Int v: EditorGUILayout.Vector3IntField(label, v); break;
                case Color c: EditorGUILayout.ColorField(label, c); break;
                case Quaternion q: EditorGUILayout.Vector3Field(label, q.eulerAngles); break;
                case Enum e: EditorGUILayout.EnumPopup(label, e); break;
                case GameObject g: EditorGUILayout.ObjectField(label, g, typeof(GameObject), true); break;
                case System.Collections.ICollection c: EditorGUILayout.LabelField(label, $"{c.Count} items"); break;
                default: EditorGUILayout.LabelField(label, value != null ? value.ToString() : "none"); break;
            }
        }

        public override bool RequiresConstantRepaint() => EditorApplication.isPlaying;
    }
}
