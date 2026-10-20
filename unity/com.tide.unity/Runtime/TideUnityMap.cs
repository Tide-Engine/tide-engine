using System;
using System.IO;
using UnityEngine;

namespace Tide
{
    // What `tide unity` writes beside each build as unity.json: the prefabs and
    // scenes it read from the project, in the numbers of each entity's
    // Unity.Source { asset, node }, and each node's GameObject as a path of names
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

        // The GameObject at `path` under `root` (a prefab's root or instance), or null.
        public static Transform Find(Transform root, string path)
        {
            if (root == null) return null;
            if (string.IsNullOrEmpty(path)) return root;
            var at = root;
            foreach (var part in path.Split('/'))
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
            var slash = path.IndexOf('/');
            var first = slash < 0 ? path : path.Substring(0, slash);
            Split(first, out var name, out var nth);
            foreach (var root in scene.GetRootGameObjects())
            {
                if (root.name != name || nth-- > 0) continue;
                return slash < 0 ? root.transform : Find(root.transform, path.Substring(slash + 1));
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

        static void Split(string part, out string name, out int nth)
        {
            var hash = part.LastIndexOf('#');
            nth = 0;
            if (hash > 0 && int.TryParse(part.Substring(hash + 1), out nth))
            {
                name = part.Substring(0, hash);
                return;
            }
            nth = 0;
            name = part;
        }
    }
}
