using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Finds the tide command: the project's setting, then PATH, then where
    // tide's installer puts it (%LOCALAPPDATA%\Tide\bin on Windows, ~/.tide/bin
    // elsewhere). Nothing about this machine is written into the project.
    public static class TideToolchain
    {
        static string ExecutableName => Application.platform == RuntimePlatform.WindowsEditor ? "tide.exe" : "tide";

        // The tide command's full path, or null; `where` says how it was found, or what to do.
        public static string Find(out string where)
        {
            var set = TideSettings.instance.tidePath;
            if (!string.IsNullOrEmpty(set))
            {
                where = "the project's setting";
                if (File.Exists(set)) return Path.GetFullPath(set);
                where = $"The tide command set in Project Settings > Tide isn't there: {set}";
                return null;
            }
            var path = Environment.GetEnvironmentVariable("PATH") ?? "";
            foreach (var folder in path.Split(Path.PathSeparator))
            {
                if (string.IsNullOrWhiteSpace(folder)) continue;
                try
                {
                    var candidate = Path.Combine(folder.Trim().Trim('"'), ExecutableName);
                    if (File.Exists(candidate))
                    {
                        where = "on PATH";
                        return candidate;
                    }
                }
                catch (ArgumentException)
                {
                    // A PATH entry that isn't a path
                }
            }
            var installed = Application.platform == RuntimePlatform.WindowsEditor
                ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Tide", "bin", ExecutableName)
                : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".tide", "bin", ExecutableName);
            if (File.Exists(installed))
            {
                where = "where tide's installer puts it";
                return installed;
            }
            where = "tide isn't installed, or isn't on PATH: install it from tide-engine.dev, or set the tide command in Project Settings > Tide.";
            return null;
        }

        // Runs tide with `arguments` in `folder` without holding up the editor, as `tide add` from a tide.packages'
        // inspector: `done` gets its exit code and what it said, on the editor's thread. False if tide isn't found,
        // with `done` told why.
        public static bool Run(string arguments, string folder, Action<int, string> done)
        {
            var tide = Find(out var where);
            if (tide == null)
            {
                done(-1, where);
                return false;
            }
            var said = new StringBuilder();
            var process = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = tide,
                    Arguments = arguments,
                    WorkingDirectory = folder,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                },
            };
            process.OutputDataReceived += (_, e) => { if (e.Data != null) lock (said) said.AppendLine(e.Data); };
            process.ErrorDataReceived += (_, e) => { if (e.Data != null) lock (said) said.AppendLine(e.Data); };
            try
            {
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
            }
            catch (Exception e)
            {
                process.Dispose();
                done(-1, $"can't start {tide}: {e.Message}");
                return false;
            }
            void Wait()
            {
                if (!process.HasExited) return;
                EditorApplication.update -= Wait;
                process.WaitForExit(); // Its output's last lines
                string text;
                lock (said) text = said.ToString().TrimEnd();
                var code = process.ExitCode;
                process.Dispose();
                done(code, text);
            }
            EditorApplication.update += Wait;
            return true;
        }
    }
}
