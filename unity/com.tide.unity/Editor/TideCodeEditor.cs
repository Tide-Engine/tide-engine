using System;
using System.Linq;
using Unity.CodeEditor;
using UnityEditor;

namespace Tide.Editor
{
    // .tide files and tide.packages open in the external code editor (Preferences > External Tools), as C# scripts
    // do, from the Project window, the inspector's Open and the Console, and are in the projects it generates. Rider's
    // and Visual Studio's packages open only their own extensions and those Editor Settings' "Additional extensions
    // to include" lists, declining the rest, so the package lists Tide's there.
    [InitializeOnLoad]
    static class TideCodeEditor
    {
        internal static readonly string[] Extensions = { "tide", "packages" };

        static TideCodeEditor()
        {
            EditorApplication.delayCall += Include;
        }

        internal static void Include()
        {
            var listed = EditorSettings.projectGenerationUserExtensions ?? Array.Empty<string>();
            var missing = Extensions.Where(e => !listed.Any(l => string.Equals(l.TrimStart('.'), e, StringComparison.OrdinalIgnoreCase))).ToArray();
            if (missing.Length == 0) return;
            EditorSettings.projectGenerationUserExtensions = listed.Concat(missing).ToArray();
            try
            {
                CodeEditor.CurrentEditor?.SyncAll(); // Its project then has them, and it opens them
            }
            catch (Exception)
            {
                // An editor whose package can't sync now does when it next opens a file
            }
        }
    }
}
