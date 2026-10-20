using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEngine;
using Debug = UnityEngine.Debug;

namespace Tide.Editor
{
    // While the editor is open, `tide unity` builds the game into
    // Library/Tide: the embed library once, then a game library each time the
    // game's files change, which a running game swaps in (TideRunner). tide's
    // lines go to the Console. tide ends when its input closes, so it never
    // outlives the editor.
    [InitializeOnLoad]
    public static class TideBuildService
    {
        static Process process;
        static readonly ConcurrentQueue<(string line, bool error)> lines = new ConcurrentQueue<(string, bool)>();
        static string state = "not building";

        static TideBuildService()
        {
            AssemblyReloadEvents.beforeAssemblyReload += Stop;
            EditorApplication.quitting += Stop;
            EditorApplication.update += Drain;
            EditorApplication.delayCall += () =>
            {
                if (TideSettings.instance.buildWhileEditing) Start();
            };
        }

        public static string Describe()
        {
            return process != null && !process.HasExited ? "building while the game's files change" : state;
        }

        public static void Restart()
        {
            Stop();
            Start();
        }

        public static void Start()
        {
            if (process != null && !process.HasExited) return;
            var settings = TideSettings.instance;
            var folder = settings.GameFolderPath;
            if (!Directory.Exists(folder))
            {
                state = $"no game folder: {settings.gameFolder} (Project Settings > Tide)";
                return;
            }
            var tide = TideToolchain.Find(out var where);
            if (tide == null)
            {
                state = where;
                Debug.LogWarning("Tide: " + where);
                return;
            }
            var start = new ProcessStartInfo
            {
                FileName = tide,
                // The project's prefabs and scenes are the game's too: their TideEntity objects are its prefabs
                Arguments = $"unity -o \"{TideLibrary.BuildFolder}\" --assets \"{Application.dataPath}\"",
                WorkingDirectory = folder,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            try
            {
                process = new Process { StartInfo = start, EnableRaisingEvents = true };
                process.OutputDataReceived += (_, e) => { if (e.Data != null) lines.Enqueue((e.Data, false)); };
                process.ErrorDataReceived += (_, e) => { if (e.Data != null) lines.Enqueue((e.Data, true)); };
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                state = "building";
            }
            catch (Exception e)
            {
                process = null;
                state = $"can't start {tide}: {e.Message}";
                Debug.LogError("Tide: " + state);
            }
        }

        // Starts the running game over, as Ctrl+R does under tide run: match, local state and the game's C, with no
        // carrying over.
        public static void RestartGame()
        {
            if (process == null || process.HasExited) return;
            try
            {
                process.StandardInput.WriteLine("restart");
                process.StandardInput.Flush();
            }
            catch (Exception)
            {
                // It ended meanwhile
            }
        }

        public static void Stop()
        {
            if (process == null) return;
            try
            {
                if (!process.HasExited)
                {
                    process.StandardInput.Close(); // tide's sign to end
                    if (!process.WaitForExit(3000)) process.Kill();
                }
            }
            catch (Exception)
            {
                // It ended meanwhile
            }
            process.Dispose();
            process = null;
            state = "stopped";
        }

        static void Drain()
        {
            while (lines.TryDequeue(out var entry))
            {
                // A compiler's error or warning says so; the rest of what tide says is progress
                var line = entry.line;
                var context = ContextOf(ref line);
                if (line.Contains(": error") || line.StartsWith("tide: can't") || line.Contains("FAILED")) Debug.LogError("tide: " + line, context);
                else if (line.Contains(": warning")) Debug.LogWarning("tide: " + line, context);
                else if (line.Length > 0) Debug.Log("tide: " + line, context);
            }
            if (process != null && process.HasExited)
            {
                state = $"tide ended with {process.ExitCode}";
                process.Dispose();
                process = null;
            }
        }

        static readonly System.Text.RegularExpressions.Regex located =
            new System.Text.RegularExpressions.Regex(@"^(?<path>.+?):(?<line>\d+):(?<column>\d+): (error|warning|note)");

        // The asset a message is about, for the Console to select: a .tide file of the game, or a prefab or scene,
        // whose line numbers are of the Tide text tide made of it, which the message then names.
        static UnityEngine.Object ContextOf(ref string line)
        {
            var match = located.Match(line);
            if (!match.Success) return null;
            var path = match.Groups["path"].Value.Replace('\\', '/');
            var project = TideSettings.ProjectFolder.Replace('\\', '/').TrimEnd('/') + "/";
            string asset = null;
            if (Path.IsPathRooted(path))
            {
                if (path.StartsWith(project, StringComparison.OrdinalIgnoreCase)) asset = path.Substring(project.Length);
            }
            else if (path.StartsWith("Assets/") && File.Exists(Path.Combine(project, path)))
            {
                asset = path; // A prefab's or a scene's
                line += $" (line {match.Groups["line"].Value} of Library/Tide/prefabs/{path}.tprefab, the Tide text tide read)";
            }
            else
            {
                var full = Path.GetFullPath(Path.Combine(TideSettings.instance.GameFolderPath, path)).Replace('\\', '/');
                if (full.StartsWith(project, StringComparison.OrdinalIgnoreCase)) asset = full.Substring(project.Length);
            }
            return asset != null ? AssetDatabase.LoadMainAssetAtPath(asset) : null;
        }
    }
}
