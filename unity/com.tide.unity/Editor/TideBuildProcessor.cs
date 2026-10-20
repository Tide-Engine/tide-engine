using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Tide.Editor
{
    // A player build of the project brings the game: before it, tide builds an optimized one (tide unity --once
    // --release) into Temp/TideBuild; each scene's TideRunner gets a link to every Unity prefab the game spawns, so
    // Unity builds them in; after it, the embed, the game's library ("game"), its pack and the map go into the
    // player's StreamingAssets/Tide, where TideLibrary looks; or for WebGL, Tide's program goes in the page. Desktop players
    // for the editor's own platform and WebGL, for now.
    public sealed class TideBuildProcessor : IPreprocessBuildWithReport, IProcessSceneWithReport, IPostprocessBuildWithReport
    {
        public int callbackOrder => 0;

        static string Built => Path.Combine(TideSettings.ProjectFolder, "Temp", "TideBuild");

        static bool Wanted()
        {
            return Directory.Exists(TideSettings.instance.GameFolderPath);
        }

        public void OnPreprocessBuild(BuildReport report)
        {
            if (!Wanted()) return;
            var target = report.summary.platform;
            var native = Application.platform == RuntimePlatform.WindowsEditor ? BuildTarget.StandaloneWindows64
                : Application.platform == RuntimePlatform.OSXEditor ? BuildTarget.StandaloneOSX
                : BuildTarget.StandaloneLinux64;
            var web = target == BuildTarget.WebGL;
            if (target != native && !web)
            {
                throw new BuildFailedException($"Tide: players build for {native} and WebGL on this machine, for now: the game's library is built by this machine's tide, not for {target}.");
            }
            var tide = TideToolchain.Find(out var where);
            if (tide == null) throw new BuildFailedException("Tide: " + where);
            if (Directory.Exists(Built)) Directory.Delete(Built, true);
            Directory.CreateDirectory(Built);
            var start = new ProcessStartInfo
            {
                FileName = tide,
                Arguments = $"unity --once --release{(web ? " --web" : "")} -o \"{Built}\" --assets \"{Application.dataPath}\"",
                WorkingDirectory = TideSettings.instance.GameFolderPath,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            using var process = Process.Start(start);
            var output = process.StandardOutput.ReadToEndAsync();
            var errors = process.StandardError.ReadToEnd();
            process.WaitForExit();
            var said = (output.Result + errors).Trim();
            if (process.ExitCode != 0) throw new BuildFailedException("Tide: the game didn't build for the player:\n" + said);
            if (said.Length > 0) Debug.Log("tide: " + said);
            // A player loads only the scenes it was built with, and the match may load any of these
            var built = new HashSet<string>(EditorBuildSettings.scenes.Where(s => s.enabled).Select(s => s.path));
            foreach (var asset in TideUnityMap.Load(Built).assets)
            {
                if (asset.scene && !built.Contains(asset.path))
                {
                    Debug.LogWarning($"Tide: {asset.path} holds Tide entities, but isn't in Build Settings: a player whose match loads it can't. Add it there.");
                }
            }
        }

        public void OnProcessScene(UnityEngine.SceneManagement.Scene scene, BuildReport report)
        {
            if (report == null || !Wanted()) return; // Play mode in the editor, which loads them itself
            var map = TideUnityMap.Load(Built);
            var links = new List<TidePrefabLink>();
            foreach (var asset in map.assets)
            {
                if (asset.scene) continue;
                var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(AssetDatabase.GUIDToAssetPath(asset.guid));
                if (prefab != null) links.Add(new TidePrefabLink { guid = asset.guid, prefab = prefab });
            }
            var mapPath = Path.Combine(Built, "unity.json");
            var mapText = File.Exists(mapPath) ? File.ReadAllText(mapPath) : "";
            foreach (var root in scene.GetRootGameObjects())
            {
                foreach (var runner in root.GetComponentsInChildren<TideRunner>(true))
                {
                    runner.prefabs = links;
                    runner.map = mapText;
                }
            }
        }

        public void OnPostprocessBuild(BuildReport report)
        {
            if (!Wanted()) return;
            var output = report.summary.outputPath;
            if (report.summary.platform == BuildTarget.WebGL)
            {
                PlaceWeb(output);
                return;
            }
            var data = report.summary.platform == BuildTarget.StandaloneOSX
                ? Path.Combine(output, "Contents", "Resources", "Data")
                : Path.Combine(Path.GetDirectoryName(output) ?? ".", Path.GetFileNameWithoutExtension(output) + "_Data");
            var into = Path.Combine(data, "StreamingAssets", "Tide");
            Directory.CreateDirectory(into);
            var suffix = TideLibrary.LibrarySuffix;
            File.Copy(Path.Combine(Built, TideLibrary.LibraryName), Path.Combine(into, TideLibrary.LibraryName), true);
            var game = Newest(Built, "game-", suffix);
            if (game < 0) throw new BuildFailedException("Tide: tide built no game library in " + Built);
            File.Copy(Path.Combine(Built, $"game-{game}{suffix}"), Path.Combine(into, "game" + suffix), true);
            var pack = Newest(Built, "pack-", "");
            if (pack >= 0) File.Copy(Path.Combine(Built, $"pack-{pack}"), Path.Combine(into, "pack"), true);
            foreach (var name in new[] { "unity.json", "schema.json" })
            {
                var from = Path.Combine(Built, name);
                if (File.Exists(from)) File.Copy(from, Path.Combine(into, name), true);
            }
            // Libraries the game's own C links, which tide put beside its builds
            foreach (var file in Directory.GetFiles(Built, "*" + suffix))
            {
                var name = Path.GetFileName(file);
                if (name.StartsWith("game-") || name == TideLibrary.LibraryName) continue;
                File.Copy(file, Path.Combine(into, name), true);
            }
        }

        // A WebGL player's page gets Tide's program (tide-game.js: tide.js, the game's WebAssembly and its pack),
        // loaded before Unity's. Its threads need the page cross-origin isolated, as any Tide page's do: served with
        // Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp.
        static void PlaceWeb(string output)
        {
            var script = Path.Combine(Built, "tide-game.js");
            if (!File.Exists(script)) throw new BuildFailedException("Tide: tide built no tide-game.js in " + Built);
            File.Copy(script, Path.Combine(output, "tide-game.js"), true);
            var page = Path.Combine(output, "index.html");
            if (!File.Exists(page)) throw new BuildFailedException($"Tide: the WebGL build has no index.html in {output} to put Tide's program in.");
            var html = File.ReadAllText(page);
            const string tag = "<script src=\"tide-game.js\"></script>";
            if (html.Contains(tag)) return;
            var head = html.IndexOf("</head>", StringComparison.OrdinalIgnoreCase);
            if (head < 0) throw new BuildFailedException("Tide: the WebGL build's index.html has no </head> to put Tide's program before.");
            File.WriteAllText(page, html.Insert(head, tag + "\n"));
        }

        // The highest n of `<prefix><n><suffix>` in `folder`, or -1.
        static int Newest(string folder, string prefix, string suffix)
        {
            var most = -1;
            foreach (var file in Directory.GetFiles(folder, prefix + "*" + suffix))
            {
                var name = Path.GetFileName(file);
                var number = name.Substring(prefix.Length, name.Length - prefix.Length - suffix.Length);
                if (int.TryParse(number, out var n) && n > most) most = n;
            }
            return most;
        }
    }
}
