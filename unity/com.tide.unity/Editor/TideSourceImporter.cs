using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace Tide.Editor
{
    // A .tide file: its text, which the inspector shows, with Tide's icon. tide reads the file itself. An importer's
    // file is named as its class, as Unity finds its script by that.
    [ScriptedImporter(2, "tide")]
    sealed class TideSourceImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext ctx)
        {
            ctx.DependsOnArtifact(TideIcons.FilePath);
            var text = new TextAsset(File.ReadAllText(ctx.assetPath));
            ctx.AddObjectToAsset("text", text, AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.FilePath));
            ctx.SetMainObject(text);
        }
    }

    // A .tide file or a tide.packages imported before Tide's importers were has a .meta of Unity's DefaultImporter,
    // which Unity warns of as "<unknown> object at version 1" at each import of the file until the .meta is written
    // again: it's written once, as the importer's.
    sealed class TideMetas : AssetPostprocessor
    {
        static void OnPostprocessAllAssets(string[] imported, string[] deleted, string[] moved, string[] movedFrom)
        {
            foreach (var path in imported)
            {
                var importer = AssetImporter.GetAtPath(path);
                if (!(importer is TideSourceImporter || importer is TidePackagesImporter) || !Old(path)) continue;
                EditorApplication.delayCall += () =>
                {
                    if (importer == null) return;
                    EditorUtility.SetDirty(importer);
                    importer.SaveAndReimport();
                };
            }
        }

        static bool Old(string path)
        {
            var meta = path + ".meta";
            return File.Exists(meta) && !File.ReadAllText(meta).Contains("\nScriptedImporter:");
        }
    }
}
