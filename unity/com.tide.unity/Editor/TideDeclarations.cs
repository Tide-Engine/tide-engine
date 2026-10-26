using System;
using System.IO;
using UnityEditor;
using UnityEditor.Callbacks;
using UnityEngine;

namespace Tide.Editor
{
    // A component's script tide wrote (Packages/com.tide.game) is made of the game's Tide declaration: opening it
    // (a double click on a component's Script field, Edit Script, or the script in the Project window) opens the .tide
    // file at that declaration in the external code editor instead, as a .tide error in the Console does. Its line is
    // the newest build's (Library/Tide/outline.json): the generated C# names the file alone, so a line moving above it
    // changes no C# (owner's decision). Opened at a line of its own (a C# error in it), the script opens as itself.
    public static class TideDeclarations
    {
        // How a file is opened at a line and column; tests see what's opened rather than starting a code editor
        internal static Func<string, int, int, bool> Opener =
            (path, line, column) => UnityEditorInternal.InternalEditorUtility.OpenFileAtLineExternal(path, line, column);

        [OnOpenAsset(0)]
#if UNITY_6000_2_OR_NEWER
        static bool OnOpenAsset(EntityId id, int line) => line < 1 && Open(EditorUtility.EntityIdToObject(id) as MonoScript);
#else
        static bool OnOpenAsset(int id, int line) => line < 1 && Open(EditorUtility.InstanceIDToObject(id) as MonoScript);
#endif

        // The Tide name of the component a script tide wrote is, or null for any other script
        public static string TideNameOf(MonoScript script)
        {
            var type = script != null ? script.GetClass() : null;
            if (type == null || !typeof(TideComponent).IsAssignableFrom(type)) return null;
            var value = type.GetNestedType("Value");
            var attributes = value?.GetCustomAttributes(typeof(TideTypeAttribute), false);
            return attributes != null && attributes.Length > 0 ? ((TideTypeAttribute)attributes[0]).Name : null;
        }

        // Where a component of the game is declared, by its Tide name: its file as the project names it (or its
        // absolute path, outside the project), its line and column. False before the game has built, or for a name
        // the newest build hasn't.
        public static bool Find(string tideName, out string file, out int line, out int column)
        {
            file = null;
            line = column = 0;
            var outline = TideOutline.Current;
            if (outline == null || string.IsNullOrEmpty(tideName)) return false;
            foreach (var d in outline.declarations)
            {
                if (d.kind != "component" || d.name != tideName || string.IsNullOrEmpty(d.asset)) continue;
                file = d.asset;
                line = d.line;
                column = d.column;
                return true;
            }
            return false;
        }

        // Opens the declaration a generated component's script is of: false for another script, or one whose
        // declaration the newest build hasn't, which then opens as itself.
        public static bool Open(MonoScript script)
        {
            var name = TideNameOf(script);
            if (name == null || !Find(name, out var file, out var line, out var column)) return false;
            var path = Path.IsPathRooted(file) ? file : Path.GetFullPath(Path.Combine(TideSettings.ProjectFolder, file));
            return Opener(path.Replace('\\', '/'), line, column);
        }
    }
}
