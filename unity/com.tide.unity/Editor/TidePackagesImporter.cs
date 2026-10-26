using System.IO;
using UnityEditor;
using UnityEditor.AssetImporters;
using UnityEngine;

namespace Tide.Editor
{
    // A tide.packages: the game's packages, or a package's own name and what it needs. Another file ending in
    // .packages is text with no icon.
    [ScriptedImporter(2, "packages")]
    sealed class TidePackagesImporter : ScriptedImporter
    {
        public override void OnImportAsset(AssetImportContext ctx)
        {
            var ours = Path.GetFileName(ctx.assetPath) == "tide.packages";
            if (ours) ctx.DependsOnArtifact(TideIcons.PackagesPath);
            var text = new TextAsset(File.ReadAllText(ctx.assetPath));
            ctx.AddObjectToAsset("text", text, ours ? AssetDatabase.LoadAssetAtPath<Texture2D>(TideIcons.PackagesPath) : null);
            ctx.SetMainObject(text);
        }
    }
}
