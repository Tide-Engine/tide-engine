using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

namespace Tide
{
    // What `tide unity` writes beside each build as unity.json: the prefabs and
    // scenes it read from the project, in the numbers of each entity's
    // UnityEngine.Source { asset, node }, and each node's GameObject as a path of names
    // from the asset's root ("Body/Arm", "Arm#1" for a second Arm under one
    // parent; "" for the root, and for a scene's own entity, which no GameObject
    // is).
    [Serializable]
    public sealed class TideUnityMap
    {
        [Serializable]
        public sealed class Asset
        {
            public string guid;
            public string path;
            public bool scene;
            public string[] nodes;
        }

        public Asset[] assets = Array.Empty<Asset>();

        [NonSerialized] public DateTime written;

        public static TideUnityMap Load(string folder)
        {
            var path = Path.Combine(folder, "unity.json");
            var written = File.GetLastWriteTimeUtc(path);
            try
            {
                if (!File.Exists(path)) return new TideUnityMap { written = written };
                var map = JsonUtility.FromJson<TideUnityMap>(File.ReadAllText(path)) ?? new TideUnityMap();
                map.assets ??= Array.Empty<Asset>();
                map.written = written;
                return map;
            }
            catch (Exception e)
            {
                Debug.LogWarning($"Tide: can't read {path}: {e.Message}");
                return new TideUnityMap { written = written };
            }
        }

        public static TideUnityMap Parse(string json)
        {
            var map = JsonUtility.FromJson<TideUnityMap>(json) ?? new TideUnityMap();
            map.assets ??= Array.Empty<Asset>();
            return map;
        }

        // Whether `tide unity` has written another since this one.
        public bool Stale(string folder)
        {
            return File.GetLastWriteTimeUtc(Path.Combine(folder, "unity.json")) != written;
        }

        public Asset Of(int asset)
        {
            return (uint)asset < (uint)assets.Length ? assets[asset] : null;
        }

        // The GameObject at `path` under `root` (a prefab's root or instance), or null. A path is names from the root,
        // "/" between them and "#1" after a second of one name among siblings; a name's own '/', '#' and '\' have a
        // '\' before them.
        public static Transform Find(Transform root, string path)
        {
            if (root == null) return null;
            if (string.IsNullOrEmpty(path)) return root;
            var at = root;
            foreach (var part in Parts(path))
            {
                at = Child(at, part);
                if (at == null) return null;
            }
            return at;
        }

        // ...in a scene: its first name is one of the scene's roots.
        public static Transform FindInScene(UnityEngine.SceneManagement.Scene scene, string path)
        {
            if (!scene.IsValid() || !scene.isLoaded || string.IsNullOrEmpty(path)) return null;
            var parts = Parts(path);
            Split(parts[0], out var name, out var nth);
            foreach (var root in scene.GetRootGameObjects())
            {
                if (root.name != name || nth-- > 0) continue;
                var at = root.transform;
                for (var i = 1; i < parts.Count && at != null; i++) at = Child(at, parts[i]);
                return at;
            }
            return null;
        }

        static Transform Child(Transform parent, string part)
        {
            Split(part, out var name, out var nth);
            for (var i = 0; i < parent.childCount; i++)
            {
                var child = parent.GetChild(i);
                if (child.name == name && nth-- == 0) return child;
            }
            return null;
        }

        // A path's parts, split at each '/' that has no '\' before it, escapes kept.
        static List<string> Parts(string path)
        {
            var parts = new List<string>();
            var start = 0;
            for (var i = 0; i < path.Length; i++)
            {
                if (path[i] == '\\') i++;
                else if (path[i] == '/')
                {
                    parts.Add(path.Substring(start, i - start));
                    start = i + 1;
                }
            }
            parts.Add(path.Substring(start));
            return parts;
        }

        // A part's name, unescaped, and which of its name among its siblings it is: the number after its last '#'
        // that has no '\' before it, or 0.
        static void Split(string part, out string name, out int nth)
        {
            nth = 0;
            var hash = -1;
            for (var i = 0; i < part.Length; i++)
            {
                if (part[i] == '\\') i++;
                else if (part[i] == '#') hash = i;
            }
            var named = part;
            if (hash > 0 && int.TryParse(part.Substring(hash + 1), out var n))
            {
                nth = n;
                named = part.Substring(0, hash);
            }
            var plain = new System.Text.StringBuilder(named.Length);
            for (var i = 0; i < named.Length; i++)
            {
                if (named[i] == '\\' && i + 1 < named.Length) i++;
                plain.Append(named[i]);
            }
            name = plain.ToString();
        }
    }
}
