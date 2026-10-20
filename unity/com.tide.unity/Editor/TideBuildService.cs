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
                Arguments = $"unity -o \"{TideLibrary.BuildFolder}\"",
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
                if (line.Contains(": error") || line.StartsWith("tide: can't") || line.Contains("FAILED")) Debug.LogError("tide: " + line);
                else if (line.Contains(": warning")) Debug.LogWarning("tide: " + line);
                else if (line.Length > 0) Debug.Log("tide: " + line);
            }
            if (process != null && process.HasExited)
            {
                state = $"tide ended with {process.ExitCode}";
                process.Dispose();
                process = null;
            }
        }
    }
}
