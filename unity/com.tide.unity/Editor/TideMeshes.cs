using System.Collections.Generic;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Tide components whose lists are a mesh's (TideMesh, from its package's unity.json): filled from a Unity Mesh, the
    // GameObject's MeshFilter's as the component's added, or any from its inspector. Unity's triangles are as Tide's,
    // their front the side they're clockwise from, so they go in as they are; vertices are the mesh's own, unscaled,
    // as the component's scale field takes the GameObject's.
    static class TideMeshes
    {
        internal struct Lists
        {
            public FieldInfo Vertices, Indices;
        }

        internal static bool Read(TideComponent component, out Lists lists)
        {
            lists = default;
            if (component == null) return false;
            var type = component.GetType();
            var of = type.GetCustomAttribute<TideMeshAttribute>(false);
            if (of == null) return false;
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public;
            lists.Vertices = type.GetField(of.Vertices, flags);
            lists.Indices = string.IsNullOrEmpty(of.Indices) ? null : type.GetField(of.Indices, flags);
            return lists.Vertices != null && lists.Vertices.FieldType == typeof(List<Vector3>)
                && (lists.Indices == null || lists.Indices.FieldType == typeof(List<int>));
        }

        internal static int Count(FieldInfo field, TideComponent c) =>
            field?.GetValue(c) is System.Collections.ICollection items ? items.Count : 0;

        // The mesh's vertices and triangles (every submesh's) into the component's lists, with Undo
        internal static void Fill(TideComponent c, Lists lists, Mesh mesh)
        {
            Undo.RecordObject(c, "Copy the Mesh");
            lists.Vertices.SetValue(c, new List<Vector3>(mesh.vertices));
            if (lists.Indices != null)
            {
                var indices = new List<int>();
                for (var i = 0; i < mesh.subMeshCount; i++)
                {
                    if (mesh.GetTopology(i) == MeshTopology.Triangles) indices.AddRange(mesh.GetTriangles(i));
                }
                lists.Indices.SetValue(c, indices);
            }
            EditorUtility.SetDirty(c);
            PrefabUtility.RecordPrefabInstancePropertyModifications(c);
        }

        internal static Mesh Filtered(TideComponent c)
        {
            var filter = c.GetComponent<MeshFilter>();
            return filter != null ? filter.sharedMesh : null;
        }

        [InitializeOnLoadMethod]
        static void Watch()
        {
            ObjectFactory.componentWasAdded += Added;
        }

        // Added from Add Component: an empty one takes the GameObject's mesh
        static void Added(Component added)
        {
            var c = added as TideComponent;
            if (!Read(c, out var lists) || Count(lists.Vertices, c) > 0) return;
            var mesh = Filtered(c);
            if (mesh != null) Fill(c, lists, mesh);
        }
    }
}
