using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Tide.Editor
{
    // What the newest build says the game is (Library/Tide/outline.json, which tide unity writes beside the schema):
    // its packages as tide found them, and what each of its files declares, for the inspectors of .tide files and
    // tide.packages.
    [Serializable]
    sealed class TideOutline
    {
        public TideOutlinePackage[] packages = Array.Empty<TideOutlinePackage>();
        public string[] builtin = Array.Empty<string>(); // The packages that come with tide, by name
        public TideOutlineDeclaration[] declarations = Array.Empty<TideOutlineDeclaration>();

        static TideOutline current;
        static DateTime seen;

        public static string Path => System.IO.Path.Combine(TideLibrary.BuildFolder, "outline.json");

        // The newest one, read again when tide writes it: null before the game has built
        public static TideOutline Current
        {
            get
            {
                try
                {
                    var written = File.Exists(Path) ? File.GetLastWriteTimeUtc(Path) : default;
                    if (written != seen)
                    {
                        seen = written;
                        current = written == default ? null : Parse(File.ReadAllText(Path));
                    }
                }
                catch (IOException)
                {
                    // tide is writing it: the last one will do
                }
                return current;
            }
        }

        public static TideOutline Parse(string json)
        {
            var outline = JsonUtility.FromJson<TideOutline>(json);
            if (outline == null) return null;
            foreach (var d in outline.declarations) d.asset = AssetOf(d.file);
            foreach (var p in outline.packages) p.asset = AssetOf(p.folder?.TrimEnd('/', '\\'));
            return outline;
        }

        // A file tide named (absolute, or from the Assets folder) as the project names it, "Assets/Tide/Bounce.tide";
        // as given where it's outside the project.
        public static string AssetOf(string file)
        {
            if (string.IsNullOrEmpty(file)) return file;
            var project = TideSettings.ProjectFolder.Replace('\\', '/').TrimEnd('/') + "/";
            var path = file.Replace('\\', '/');
            if (!System.IO.Path.IsPathRooted(path))
            {
                if (path.StartsWith("Assets/") || path.StartsWith("Library/")) return path;
                path = System.IO.Path.GetFullPath(System.IO.Path.Combine(TideSettings.instance.GameFolderPath, path)).Replace('\\', '/');
            }
            return path.StartsWith(project, StringComparison.OrdinalIgnoreCase) ? path.Substring(project.Length) : path;
        }

        // What the file at `asset` declares, in its order
        public List<TideOutlineDeclaration> In(string asset)
        {
            var found = new List<TideOutlineDeclaration>();
            foreach (var d in declarations)
            {
                if (string.Equals(d.asset, asset, StringComparison.OrdinalIgnoreCase)) found.Add(d);
            }
            return found;
        }
    }

    [Serializable]
    sealed class TideOutlinePackage
    {
        public string name;
        public string folder; // Absolute
        public string kind;   // "builtin", "git", "local" or "found" (a folder of Assets with a `package` line)
        public int line;      // The game's tide.packages line for it: 0 for one found
        public string source;
        public string @ref;   // What tide update follows: a branch or a version
        public string commit;
        [NonSerialized] public string asset; // Its folder in the project, or its absolute path
    }

    [Serializable]
    sealed class TideOutlineDeclaration
    {
        public string kind; // "component", "scene", "singleton", "system", "view", "handler", "localHandler", "event", ...
        public string name;
        public string file;
        public int line;
        public int column;
        public bool local;
        public bool async;
        public bool @extern;
        public TideOutlineField[] fields = Array.Empty<TideOutlineField>();
        public string[] members = Array.Empty<string>();
        public string[] handlers = Array.Empty<string>();
        public string[] localHandlers = Array.Empty<string>();
        // Systems, views and handlers
        public bool perEntity;
        public string @event;
        public TideOutlineAccess[] access = Array.Empty<TideOutlineAccess>();
        public string[] with = Array.Empty<string>();
        public string[] without = Array.Empty<string>();
        // ...and in the tick
        public int stage;
        public TideOutlineWait[] after = Array.Empty<TideOutlineWait>();
        public int afterMore;
        public string[] alongside = Array.Empty<string>();
        public int alongsideMore;
        [NonSerialized] public string asset;
    }

    [Serializable]
    sealed class TideOutlineField
    {
        public string name;
        public string type;
        public bool local;
        public string text; // A text field's default, as written between its quotes
    }

    [Serializable]
    sealed class TideOutlineAccess
    {
        public string name;
        public bool writes;
        public bool every; // Any entity's, through World or a query
    }

    [Serializable]
    sealed class TideOutlineWait
    {
        public string name;
        public string why;
    }
}
