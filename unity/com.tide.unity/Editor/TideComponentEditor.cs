using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // A Tide component's inspector: Unity's own, its fields as authored, and in Play its entity's values, which the
    // match decides: shown, not edited, as a change there would reach neither the match nor the scene.
    [CustomEditor(typeof(TideComponent), true)]
    [CanEditMultipleObjects]
    sealed class TideComponentEditor : UnityEditor.Editor
    {
        public override void OnInspectorGUI()
        {
            var playing = EditorApplication.isPlayingOrWillChangePlaymode;
            using (new EditorGUI.DisabledScope(playing)) DrawDefaultInspector();
        }

        public override bool RequiresConstantRepaint() => EditorApplication.isPlaying;
    }
}
