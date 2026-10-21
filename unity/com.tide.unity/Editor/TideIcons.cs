using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Tide's icons in the Project window: its three waves on a .tide file, in a box on a tide.packages, and on a
    // package's folder (one whose tide.packages says `package Name`).
    static class TideIcons
    {
        public const string Folder = "Packages/com.tide.unity/Editor/Icons/";
        public const string FilePath = Folder + "TideFile.png";
        public const string PackagesPath = Folder + "TidePackages.png";
        public const string BadgePath = Folder + "TidePackageBadge.png";

        static Texture2D badge;

        public static Texture2D Badge => badge ? badge : badge = AssetDatabase.LoadAssetAtPath<Texture2D>(BadgePath);
    }

    // A package's folder gets the waves' badge over its folder icon, in both of the Project window's views.
    [InitializeOnLoad]
    static class TidePackageFolders
    {
        static readonly Dictionary<string, bool> known = new Dictionary<string, bool>();

        static TidePackageFolders()
        {
            EditorApplication.projectWindowItemOnGUI += Draw;
            EditorApplication.projectChanged += known.Clear;
        }

        static void Draw(string guid, Rect rect)
        {
            if (Event.current.type != EventType.Repaint) return;
            var path = AssetDatabase.GUIDToAssetPath(guid);
            if (string.IsNullOrEmpty(path) || !IsPackage(path) || TideIcons.Badge == null) return;
            // The folder's icon: the row's start in a list, the square above the name in the grid
            var icon = rect.height > 20 ? new Rect(rect.x, rect.y, rect.width, rect.width) : new Rect(rect.x, rect.y, rect.height, rect.height);
            var size = Mathf.Round(icon.width * 0.6f);
            GUI.DrawTexture(new Rect(icon.xMax - size, icon.yMax - size, size, size), TideIcons.Badge, ScaleMode.ScaleToFit);
        }

        // Whether a folder's tide.packages says `package Name`, before any line but comments and blank ones
        internal static bool IsPackage(string path)
        {
            if (known.TryGetValue(path, out var package)) return package;
            package = false;
            var file = Path.Combine(path, "tide.packages");
            if (AssetDatabase.IsValidFolder(path) && File.Exists(file))
            {
                foreach (var line in File.ReadLines(file))
                {
                    var text = line.Split('#')[0].Trim();
                    if (text.Length == 0) continue;
                    package = text.StartsWith("package ");
                    break;
                }
            }
            known[path] = package;
            return package;
        }
    }
}
