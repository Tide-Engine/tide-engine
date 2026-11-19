using System;
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Callbacks;
using UnityEditor.iOS.Xcode;

namespace Tide.Editor
{
    // An iOS player's game, into the Xcode project Unity makes for it. iOS loads no library of an app's by name, so
    // the game is linked into the player, and what compiles for iOS is Xcode, on a Mac: tide wrote the game, the
    // engine and the platform layer as sources with each file's flags (tide unity --ios: <built>/ios/Tide and its
    // sources.txt), and this adds them to the project as a static library of their own, TideGame, which
    // UnityFramework links, so the project builds on a Mac that has no tide, whichever system exported it. A target
    // of their own because UnityFramework's files are all compiled with Unity's prefix header before them, whose
    // names (Apple's MacTypes.h among them) aren't the game's to avoid. Each file keeps its own flags there (Compiler
    // Flags in Xcode's Build Phases): -ffp-contract=off and -fno-fast-math among them, without which the game's
    // floats would differ from other machines' and its match would leave theirs. The game's pack goes among the
    // app's files (Data/Raw/Tide),
    // and Info.plist says why the app looks for devices on the local network (a match's players on one Wi-Fi) and,
    // where the game listens to the microphone, why it records. An assembly of its own, compiled only while iOS is
    // the build target: Unity's Xcode editor code is there only with its iOS module.
    public static class TideIosBuild
    {
        // Where the sources go in the Xcode project, from its root
        public const string Folder = "Libraries/Tide";
        // The static library the sources are built into, which UnityFramework links
        public const string Target = "TideGame";

        [PostProcessBuild(100)]
        public static void OnPostprocessBuild(BuildTarget target, string path)
        {
            if (target != BuildTarget.iOS || !TideBuildProcessor.Wanted()) return;
            Place(Path.Combine(TideBuildProcessor.Built, "ios"), path);
        }

        // Puts what tide wrote in `built` (its Tide folder and pack) into the Xcode project at `path`.
        public static void Place(string built, string path)
        {
            var from = Path.Combine(built, "Tide");
            var listed = Path.Combine(from, "sources.txt");
            if (!File.Exists(listed)) throw new BuildFailedException($"Tide: tide wrote no sources for the iOS player in {from}.");
            var lines = File.ReadAllLines(listed);
            if (lines.Length == 0 || lines[0] != "tide sources 1")
            {
                throw new BuildFailedException($"Tide: {listed} is another tide's than this package reads: update the package or tide so they agree.");
            }
            var into = Path.Combine(path, Folder);
            if (Directory.Exists(into)) Directory.Delete(into, true); // An earlier build's, where Unity appends to a project
            Copy(from, into);

            var projectPath = PBXProject.GetPBXProjectPath(path);
            var project = new PBXProject();
            project.ReadFromFile(projectPath);
            var framework = project.GetUnityFrameworkTargetGuid();
            var game = project.TargetGuidByName(Target);
            if (game == null)
            {
                game = project.AddTarget(Target, ".a", "com.apple.product-type.library.static");
                project.AddSourcesBuildPhase(game);
                project.AddTargetDependency(framework, game);
                project.AddFileToBuildSection(framework, project.GetFrameworksBuildPhaseByTarget(framework), project.GetTargetProductFileRef(game));
            }
            // Its files alone, as tide says to compile them: no header before them, no modules, and nothing installed
            project.SetBuildProperty(game, "PRODUCT_NAME", Target);
            project.SetBuildProperty(game, "GCC_PREFIX_HEADER", "");
            project.SetBuildProperty(game, "GCC_PRECOMPILE_PREFIX_HEADER", "NO");
            project.SetBuildProperty(game, "CLANG_ENABLE_MODULES", "NO");
            project.SetBuildProperty(game, "SKIP_INSTALL", "YES");
            var microphone = false;
            var sources = 0;
            for (var i = 1; i < lines.Length; i++)
            {
                if (lines[i].Length == 0) continue;
                var parts = lines[i].Split('\t');
                if (parts[0] == "framework" && parts.Length == 2)
                {
                    project.AddFrameworkToProject(framework, parts[1] + ".framework", false);
                }
                else if (parts[0] == "needs" && parts.Length == 2)
                {
                    if (parts[1] == "microphone") microphone = true;
                }
                else if (parts[0] == "source" && parts.Length == 3)
                {
                    var file = Folder + "/" + parts[1];
                    if (!File.Exists(Path.Combine(path, file)))
                    {
                        throw new BuildFailedException($"Tide: {listed} lists {parts[1]}, which tide didn't write.");
                    }
                    // `@` is the folder itself, wherever the project is built (quoted: its path may have spaces)
                    var words = parts[2].Split(' ');
                    for (var w = 0; w < words.Length; w++)
                    {
                        var at = words[w].IndexOf('@');
                        if (at >= 0) words[w] = words[w].Substring(0, at) + "\"$(SRCROOT)/" + Folder + words[w].Substring(at + 1) + "\"";
                    }
                    var flags = string.Join(" ", words);
                    var guid = project.FindFileGuidByProjectPath(file);
                    if (guid == null) guid = project.AddFile(file, file, PBXSourceTree.Source);
                    else project.RemoveFileFromBuild(game, guid); // Listed again with this build's flags
                    project.AddFileToBuildWithFlags(game, guid, flags);
                    sources++;
                }
                else
                {
                    throw new BuildFailedException($"Tide: {listed} has a line this package doesn't know (`{lines[i]}`): update the package or tide so they agree.");
                }
            }
            if (sources == 0) throw new BuildFailedException($"Tide: {listed} lists no file to compile.");
            project.WriteToFile(projectPath);

            // The pack, where a player's files are (Application.streamingAssetsPath), which TideLibrary reads
            var raw = Path.Combine(path, "Data", "Raw", "Tide");
            Directory.CreateDirectory(raw);
            var pack = Path.Combine(built, "pack");
            var packed = Path.Combine(raw, "pack");
            if (File.Exists(pack)) File.Copy(pack, packed, true);
            else if (File.Exists(packed)) File.Delete(packed);

            Permissions(Path.Combine(path, "Info.plist"), microphone);
        }

        // What iOS shows the player as it asks: without the text, it refuses, or ends an app that records.
        static void Permissions(string path, bool microphone)
        {
            var plist = new PlistDocument();
            plist.ReadFromFile(path);
            var root = plist.root;
            // Players on one network reach each other directly (a host's address, a room's players): iOS asks first
            if (root["NSLocalNetworkUsageDescription"] == null)
            {
                root.SetString("NSLocalNetworkUsageDescription", "Finds and plays with players on your local network.");
            }
            if (microphone && root["NSMicrophoneUsageDescription"] == null)
            {
                root.SetString("NSMicrophoneUsageDescription", "Lets the players of your match hear you.");
            }
            plist.WriteToFile(path);
        }

        static void Copy(string from, string into)
        {
            Directory.CreateDirectory(into);
            foreach (var file in Directory.GetFiles(from)) File.Copy(file, Path.Combine(into, Path.GetFileName(file)), true);
            foreach (var folder in Directory.GetDirectories(from)) Copy(folder, Path.Combine(into, Path.GetFileName(folder)));
        }
    }
}
