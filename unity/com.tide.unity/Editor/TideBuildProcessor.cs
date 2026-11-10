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
    // player's StreamingAssets/Tide, where TideLibrary looks; or for WebGL, Tide's program goes in the page; or for
    // Android, the game's one library goes among the app's (TideAndroidBuild). Desktop players for the editor's own
    // platform, WebGL and Android, for now.
    public sealed class TideBuildProcessor : IPreprocessBuildWithReport, IProcessSceneWithReport, IPostprocessBuildWithReport
    {
        public int callbackOrder => 0;

        internal static string Built => Path.Combine(TideSettings.ProjectFolder, "Temp", "TideBuild");

        // The CPUs tide builds Android games for, as Unity and Android name them. x86-64 by its number: Unity 6000.6
        // builds no player for it any more and marks its name obsolete, and the Unity versions before it still do.
        internal static readonly (AndroidArchitecture architecture, string abi)[] AndroidCpus =
        {
            (AndroidArchitecture.ARM64, "arm64-v8a"), ((AndroidArchitecture)(1 << 3), "x86_64"),
        };

        internal static bool Wanted()
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
            var android = target == BuildTarget.Android;
            if (target != native && !web && !android)
            {
                throw new BuildFailedException($"Tide: players build for {native}, WebGL and Android on this machine, for now: the game's library is built by this machine's tide, not for {target}.");
            }
            if (android)
            {
                // Each CPU the player is for needs the game's library for it, or the app starts with no game there
                var asked = PlayerSettings.Android.targetArchitectures;
                var ours = AndroidArchitecture.None;
                foreach (var (architecture, _) in AndroidCpus) ours |= architecture;
                if ((asked & ~ours) != 0)
                {
                    throw new BuildFailedException($"Tide: the player is for {asked & ~ours}, which tide doesn't build games for: in Player Settings > Other Settings > Target Architectures, keep ARM64 alone (and x86-64, where this Unity still has it), with the IL2CPP scripting backend.");
                }
            }
            var tide = TideToolchain.Find(out var where, true, true);
            if (tide == null) throw new BuildFailedException("Tide: " + where);
            var pinned = TideToolchain.Pinned;
            if (Directory.Exists(Built)) Directory.Delete(Built, true);
            Directory.CreateDirectory(Built);
            var start = new ProcessStartInfo
            {
                FileName = tide,
                Arguments = $"unity --once --release{(web ? " --web" : android ? " --android" : "")} -o \"{Built}\" --assets \"{Application.dataPath}\"",
                WorkingDirectory = TideSettings.instance.GameFolderPath,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            if (pinned) start.EnvironmentVariables["TIDE_NO_UPDATE_CHECK"] = "1"; // The package's version: no newer one to hear of
            // The C# of the game's types that goes into the player, as the project has it now (tide may write it again)
            var bound = GameHash(Path.Combine(TideSettings.ProjectFolder, "Packages", "com.tide.game", "TideGame.cs"), "Hash = \"");
            var said = new System.Text.StringBuilder();
            var step = "Building the game";
            var done = 0f;
            void Heard(string line)
            {
                if (line == null) return;
                lock (said)
                {
                    // Its progress is the bar's; what it has to say (errors, warnings, notes) is kept
                    var progress = System.Text.RegularExpressions.Regex.Match(line, @"^tide: (compiled \[(\d+)/(\d+)\].*|compiling .*|linking.*|optimizing .*|resolving .*|built .*)$");
                    if (!progress.Success)
                    {
                        said.AppendLine(line);
                        return;
                    }
                    step = line.Substring(6);
                    if (progress.Groups[2].Success && float.TryParse(progress.Groups[3].Value, out var of) && of > 0)
                    {
                        done = float.Parse(progress.Groups[2].Value) / of;
                    }
                }
            }
            using (var process = new Process { StartInfo = start })
            {
                process.OutputDataReceived += (_, e) => Heard(e.Data);
                process.ErrorDataReceived += (_, e) => Heard(e.Data);
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                try
                {
                    // The editor waits for it: it shows what tide is at meanwhile, where there's someone to see it
                    while (!process.WaitForExit(100))
                    {
                        if (Application.isBatchMode) continue;
                        string at;
                        float fraction;
                        lock (said)
                        {
                            at = step;
                            fraction = done;
                        }
                        EditorUtility.DisplayProgressBar("Tide: building the game for the player", at, fraction);
                    }
                    process.WaitForExit(); // ...and for the last of what it said
                }
                finally
                {
                    if (!Application.isBatchMode) EditorUtility.ClearProgressBar();
                }
                var all = said.ToString().Trim();
                if (process.ExitCode != 0) throw new BuildFailedException("Tide: the game didn't build for the player:\n" + all);
                if (all.Contains(": warning")) Debug.LogWarning("tide: " + all);
            }
            // The game's C# in the project is compiled into the player, and reads the match's values by the layout it
            // was written for. One written for another build of the game (tide in the editor hasn't built the last
            // change yet, or the C# in source control is older than the game) would read nothing in the player.
            var game = GameHash(Path.Combine(Built, "game-hash"), "");
            if (game != null && bound != null && game != bound)
            {
                throw new BuildFailedException(
                    "Tide: the game's C# in Packages/com.tide.game was written for another build of the game than this player's, " +
                    "so the player's scripts couldn't read the match. tide writes it as it builds the game in the editor: wait for " +
                    "the Tide task in the status bar (or press Build now in Project Settings > Tide), let Unity compile, and build " +
                    "the player again. On a build server, commit Packages/com.tide.game as the editor last wrote it. If the game's " +
                    "data differs between debug and release (a field inside #if DEBUG), it can't: the editor's C# is the debug game's.");
            }
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
            if (report.summary.platform == BuildTarget.Android) return; // Placed in the Gradle project (TideAndroidBuild)
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

        // The 16 hex digits after `before` in the file at `path` (a game's hash), in capitals, or null without them.
        static string GameHash(string path, string before)
        {
            if (!File.Exists(path)) return null;
            var text = File.ReadAllText(path);
            var at = before.Length > 0 ? text.IndexOf(before, StringComparison.Ordinal) : 0;
            if (at < 0 || at + before.Length + 16 > text.Length) return null;
            var hash = text.Substring(at + before.Length, 16).ToUpperInvariant();
            foreach (var c in hash)
            {
                if (!Uri.IsHexDigit(c)) return null;
            }
            return hash;
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
