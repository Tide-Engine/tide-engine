using System.IO;
using UnityEditor;
using UnityEditor.Android;
using UnityEditor.Build;

namespace Tide.Editor
{
    // An Android player's game, into the Gradle project Unity makes for it: the library tide built for each of the
    // player's CPUs (tide unity --android: the game with Tide's host in it, and its pack), with the libraries the
    // game's own C links, among the app's native libraries, where the system finds it by its name; and the
    // permission a match needs to reach other machines. An assembly of its own, compiled only while Android is the
    // build target: Unity's Android editor code is there only with its Android module.
    public sealed class TideAndroidBuild : IPostGenerateGradleAndroidProject
    {
        public int callbackOrder => 0;

        public void OnPostGenerateGradleAndroidProject(string path)
        {
            if (!TideBuildProcessor.Wanted()) return;
            var built = Path.Combine(TideBuildProcessor.Built, "android");
            var placed = 0;
            foreach (var (architecture, abi) in TideBuildProcessor.AndroidCpus)
            {
                if ((PlayerSettings.Android.targetArchitectures & architecture) == 0) continue;
                var from = Path.Combine(built, abi);
                if (!File.Exists(Path.Combine(from, TideLibrary.AndroidLibrary)))
                {
                    throw new BuildFailedException($"Tide: tide built no {TideLibrary.AndroidLibrary} for {abi} in {from}.");
                }
                var into = Path.Combine(path, "src", "main", "jniLibs", abi);
                Directory.CreateDirectory(into);
                foreach (var file in Directory.GetFiles(from, "*.so")) File.Copy(file, Path.Combine(into, Path.GetFileName(file)), true);
                placed++;
            }
            if (placed == 0) throw new BuildFailedException("Tide: the player is for none of the CPUs tide builds Android games for (ARM64, x86-64).");
            Internet(Path.Combine(path, "src", "main", "AndroidManifest.xml"));
        }

        // A match reaches its server, its room's relay and the other players over the network, which an Android app
        // may only with this permission. Unity adds it where it sees C# that uses the network, and Tide's is native.
        static void Internet(string manifest)
        {
            if (!File.Exists(manifest)) return;
            var text = File.ReadAllText(manifest);
            if (text.Contains("android.permission.INTERNET")) return;
            var at = text.IndexOf("<application", System.StringComparison.Ordinal);
            if (at < 0) return;
            File.WriteAllText(manifest, text.Insert(at, "<uses-permission android:name=\"android.permission.INTERNET\" />\n  "));
        }
    }
}
