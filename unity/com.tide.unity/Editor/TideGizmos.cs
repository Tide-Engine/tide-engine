using System.Text;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // The Scene view's labels: a selected TideEntity's Tide components, and while
    // a match runs, which entity a GameObject shows (its index and generation,
    // and whether it's this machine's).
    static class TideGizmos
    {
        static GUIStyle style;

        static GUIStyle Style => style ??= new GUIStyle(EditorStyles.miniLabel)
        {
            normal = { textColor = new Color(0.55f, 0.85f, 1f) },
            richText = false,
        };

        [DrawGizmo(GizmoType.Selected | GizmoType.Active)]
        static void Authored(TideEntity entity, GizmoType type)
        {
            if (Application.isPlaying && entity.GetComponent<TideEntityView>() != null) return; // The view's label says it
            var text = new StringBuilder("Tide entity");
            foreach (var component in entity.components)
            {
                if (string.IsNullOrEmpty(component.type)) continue;
                text.Append('\n').Append(component.type);
            }
            Handles.Label(entity.transform.position, text.ToString(), Style);
        }

        [DrawGizmo(GizmoType.Selected | GizmoType.NonSelected)]
        static void Shown(TideEntityView view, GizmoType type)
        {
            if (!Application.isPlaying || !view.Showing) return;
            var e = view.Entity;
            var label = $"Tide #{e.Index}:{e.Generation}{(e.Mine ? " (mine)" : e.Owned ? " (owned)" : "")}";
            if ((type & GizmoType.Selected) == 0 && SceneView.lastActiveSceneView != null
                && Vector3.Distance(SceneView.lastActiveSceneView.camera.transform.position, view.transform.position) > 40f)
            {
                return; // Far away and not selected: no clutter
            }
            Handles.Label(view.transform.position, label, Style);
        }
    }
}
