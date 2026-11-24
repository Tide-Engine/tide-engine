using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEditor.Callbacks;
using UnityEngine;

namespace Tide.Editor
{
    // A .tidegraph file: its JSON as a TextAsset, with Tide's file icon. tide reads the file itself; double-clicking
    // it opens the graph editor. An importer's file is named as its class, as Unity finds its script by that.
    [ScriptedImporter(1, "tidegraph")]
    sealed class TideGraphImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext ctx)
        {
            ctx.DependsOnArtifact(TideIcons.FilePath);
            var text = new TextAsset(File.ReadAllText(ctx.assetPath));
            ctx.AddObjectToAsset("text", text, AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.FilePath));
            ctx.SetMainObject(text);
        }

        public static bool IsGraph(string path) => path != null && path.EndsWith(".tidegraph", System.StringComparison.OrdinalIgnoreCase);

        // A line is a node's id: the Console's graph errors (TideBuildService) are logged at their node's
        [OnOpenAsset(0)]
#if UNITY_6000_2_OR_NEWER
        static bool OnOpenAsset(EntityId id, int line) => Open(AssetDatabase.GetAssetPath(EditorUtility.EntityIdToObject(id)), line);
#else
        static bool OnOpenAsset(int id, int line) => Open(AssetDatabase.GetAssetPath(EditorUtility.InstanceIDToObject(id)), line);
#endif

        static bool Open(string path, int node)
        {
            if (!IsGraph(path)) return false;
            var window = TideGraphWindow.Open(path);
            if (node > 0) window.Reveal(node);
            return true;
        }

        // An empty graph's file, as tide writes one
        internal static string EmptyText => GraphDocument.Empty().Text;

        [MenuItem("Assets/Create/Tide/Graph", false, 81)]
        static void Create()
        {
            var path = Create(SelectedFolder());
            var made = AssetDatabase.LoadAssetAtPath<TextAsset>(path);
            Selection.activeObject = made;
            EditorGUIUtility.PingObject(made);
        }

        // Writes an empty graph in a folder of the project: its path
        internal static string Create(string folder)
        {
            var path = AssetDatabase.GenerateUniqueAssetPath(folder.TrimEnd('/') + "/New Graph.tidegraph");
            File.WriteAllText(path, EmptyText, new System.Text.UTF8Encoding(false));
            AssetDatabase.ImportAsset(path);
            return path;
        }

        // The folder the Project window's selection is, or is in
        static string SelectedFolder()
        {
            var path = Selection.activeObject != null ? AssetDatabase.GetAssetPath(Selection.activeObject) : null;
            if (string.IsNullOrEmpty(path)) return "Assets";
            return AssetDatabase.IsValidFolder(path) ? path : Path.GetDirectoryName(path)?.Replace('\\', '/') ?? "Assets";
        }

        [MenuItem("Assets/Convert to Tide Graph", false, 82)]
        static void Convert()
        {
            foreach (var o in Selection.objects)
            {
                var path = AssetDatabase.GetAssetPath(o);
                if (!path.EndsWith(".tide")) continue;
                var graph = Path.ChangeExtension(path, ".tidegraph");
                if (File.Exists(graph) && !EditorUtility.DisplayDialog("Convert to Tide Graph", $"{graph} is there already. Replace it?", "Replace", "Cancel")) continue;
                var why = TideGraphCommand.Convert(path, graph);
                if (why != null)
                {
                    Debug.LogError($"Tide: {path} wasn't converted: {why}");
                    continue;
                }
                AssetDatabase.ImportAsset(graph);
                Debug.LogWarning($"Tide: {path} is converted to {graph}, and both declare the same things: delete one of them, or move it out of the game, before the game builds.");
            }
        }

        [MenuItem("Assets/Convert to Tide Graph", true)]
        static bool CanConvert()
        {
            foreach (var o in Selection.objects)
                if (AssetDatabase.GetAssetPath(o).EndsWith(".tide")) return true;
            return false;
        }
    }
}
