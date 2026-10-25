using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Tide components that are a height field (TideTerrain, from its package's unity.json): filled from a Unity Terrain's
    // data, the GameObject's Terrain's as the component's added, or any from its inspector. The GameObject's origin is
    // the terrain's corner, as a height field's is its body's. Fewer samples where the terrain has more than the
    // package takes (TideGeometry.Sample).
    static class TideTerrains
    {
        internal static TideTerrainAttribute Of(TideComponent c) =>
            c != null ? c.GetType().GetCustomAttribute<TideTerrainAttribute>(false) : null;

        static void Set(TideComponent c, string field, object value)
        {
            if (string.IsNullOrEmpty(field)) return;
            var f = c.GetType().GetField(field, BindingFlags.Instance | BindingFlags.Public);
            if (f != null && f.FieldType.IsInstanceOfType(value)) f.SetValue(c, value);
        }

        // The terrain's heights, holes, counts, scale and bounds into the component, with Undo
        internal static TideGeometry.Field Fill(TideComponent c, TerrainData data)
        {
            var of = Of(c);
            var field = TideGeometry.Sample(data);
            Undo.RecordObject(c, "Copy the Terrain");
            Set(c, of.Heights, field.Heights);
            Set(c, of.Holes, field.Holes);
            Set(c, of.Columns, field.Columns);
            Set(c, of.Rows, field.Rows);
            Set(c, of.Scale, field.Scale);
            Set(c, of.Minimum, 0f);
            Set(c, of.Maximum, 1f);
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
            return field;
        }

        internal static TerrainData Found(TideComponent c)
        {
            var terrain = c.GetComponent<Terrain>();
            return terrain != null ? terrain.terrainData : null;
        }

        [InitializeOnLoadMethod]
        static void Watch()
        {
            ObjectFactory.componentWasAdded += Added;
        }

        // Added from Add Component: one on a Terrain's GameObject takes its data
        static void Added(Component added)
        {
            var c = added as TideComponent;
            if (Of(c) == null) return;
            var data = Found(c);
            if (data != null) Fill(c, data);
        }
    }
}
