using System;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.Compilation;

namespace Tide.Editor
{
    // Keeps the generated C# types (Tide.Game) the newest build's: when Library/Tide/schema.json says another hash
    // than the one they were made from, Unity compiles scripts again, from scratch, so the generator reads it.
    // Unity's build cache knows nothing of the schema, so nothing less would make it run.
    [InitializeOnLoad]
    public static class TideSchemaWatcher
    {
        const string AskedKey = "Tide.SchemaCompileAsked";
        static DateTime seen;

        static TideSchemaWatcher()
        {
            EditorApplication.update += Check;
        }

        public static string SchemaPath => Path.Combine(Tide.TideLibrary.BuildFolder, "schema.json");

        static void Check()
        {
            var path = SchemaPath;
            DateTime written;
            try
            {
                if (!File.Exists(path)) return;
                written = File.GetLastWriteTimeUtc(path);
            }
            catch (IOException)
            {
                return;
            }
            if (written == seen) return;
            seen = written;
            string text;
            try
            {
                text = File.ReadAllText(path);
            }
            catch (IOException)
            {
                seen = default; // tide is writing it: look again
                return;
            }
            var match = Regex.Match(text, "\"hash\":\"([0-9A-Fa-f]+)\"");
            if (!match.Success) return;
            var hash = match.Groups[1].Value;
            if (hash == CompiledHash()) return;
            // Once a hash: a generator that can't read it mustn't make the editor compile forever
            if (SessionState.GetString(AskedKey, "") == hash) return;
            SessionState.SetString(AskedKey, hash);
            UnityEngine.Debug.Log($"Tide: the game's types changed (schema {hash}): compiling scripts again");
            CompilationPipeline.RequestScriptCompilation(RequestScriptCompilationOptions.CleanBuildCache);
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
