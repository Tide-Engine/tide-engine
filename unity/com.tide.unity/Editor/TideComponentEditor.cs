using System;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // A Tide component's inspector: Unity's own, its fields as authored, and in Play its entity's values, which the
    // match decides: shown, not edited, as a change there would reach neither the match nor the scene. In Play its
    // local fields follow, this machine's own, which Unity doesn't keep.
    [CustomEditor(typeof(TideComponent), true)]
    [CanEditMultipleObjects]
    sealed class TideComponentEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var playing = EditorApplication.isPlayingOrWillChangePlaymode;
            using (new EditorGUI.DisabledScope(playing)) DrawDefaultInspector();
            if (!EditorApplication.isPlaying || targets.Length != 1) return;
            var locals = Array.FindAll(target.GetType().GetFields(BindingFlags.Instance | BindingFlags.Public),
                f => f.IsNotSerialized);
            if (locals.Length == 0) return;
            EditorGUILayout.Space();
            EditorGUILayout.LabelField("Local fields", EditorStyles.boldLabel);
            using (new EditorGUI.DisabledScope(true))
            {
                foreach (var f in locals) Show(ObjectNames.NicifyVariableName(f.Name), f.GetValue(target));
            }
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
