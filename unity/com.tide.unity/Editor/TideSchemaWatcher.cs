using System;
using System.IO;
using UnityEditor;

namespace Tide.Editor
{
    // Keeps the game's C# (Tide.Game) the newest build's. tide unity writes each new schema into the project's
    // Packages/com.tide.game with the C# made of it (its components and types): a refresh is all it takes, which this
    // asks for as soon as the schema changes rather than when the editor next has focus.
    [InitializeOnLoad]
    public static class TideSchemaWatcher
    {
        static DateTime seen;

        static TideSchemaWatcher()
        {
            EditorApplication.update += Check;
        }

        public static string SchemaPath => Path.Combine(Tide.TideLibrary.BuildFolder, "schema.json");
        public static string PackageSchemaPath => Path.Combine(TideSettings.ProjectFolder, "Packages", "com.tide.game", "schema.json");

        static void Check()
        {
            DateTime written;
            try
            {
                var path = PackageSchemaPath;
                if (!File.Exists(path)) return;
                written = File.GetLastWriteTimeUtc(path);
            }
            catch (IOException)
            {
                return;
            }
            if (written == seen || EditorApplication.isCompiling || EditorApplication.isUpdating) return;
            var first = seen == default; // What the editor opened with, which it has compiled
            seen = written;
            if (!first) AssetDatabase.Refresh();
        }

        // The hash the generated types were made from, or "" before there were any.
        public static string CompiledHash()
        {
            var type = Type.GetType("Tide.Game.TideGameSchema, Tide.Game");
            var field = type?.GetField("Hash");
            return field?.GetRawConstantValue() as string ?? "";
        }
    }
}
