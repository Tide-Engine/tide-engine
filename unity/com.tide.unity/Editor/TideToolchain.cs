using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Finds the tide command: yours (Project Settings > Tide, your own setting), then the version this package's
    // commit is, installed side by side (TideVersions: installing it when asked to), then for a package that isn't
    // from git PATH, then where tide's installer puts it (%LOCALAPPDATA%\Tide\bin on Windows, ~/.tide/bin
    // elsewhere). Nothing about this machine is written into the project.
    public static class TideToolchain
    {
        static string ExecutableName => Application.platform == RuntimePlatform.WindowsEditor ? "tide.exe" : "tide";

        // Whether the tide Find gave is the package's own version: its daily look for a newer one says nothing
        public static bool Pinned { get; private set; }

        // The tide command's full path, or null; `where` says how it was found, or what to do. `install`: the
        // package's version may be installed, now in a batch run (a build server), or in the editor once you say so;
        // `wait`: now in the editor too, as a player build waits for it.
        public static string Find(out string where, bool install = true, bool wait = false)
        {
            var tide = FindCommand(out where, install, wait);
            // Local, rebuildable information for IDEs. The commit prevents a branch switch from using the old
            // package's command; explicit developer overrides belong to the same project as Unity's builds.
            var file = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
            if (tide != null)
            {
                TideVersions.PackageCommit(out var commit, out _);
                var locked = Path.Combine(TideSettings.ProjectFolder, "Packages", "packages-lock.json");
                string lockHash = null;
                if (File.Exists(locked))
                {
                    using (var sha = System.Security.Cryptography.SHA256.Create())
                        lockHash = BitConverter.ToString(sha.ComputeHash(File.ReadAllBytes(locked))).Replace("-", "").ToLowerInvariant();
                }
                var text = JsonUtility.ToJson(new Toolchain { commit = commit, lockHash = lockHash, tide = Path.GetFullPath(tide) });
                Directory.CreateDirectory(TideLibrary.BuildFolder);
                if (!File.Exists(file) || File.ReadAllText(file) != text)
                {
                    var temporary = file + ".tmp";
                    File.WriteAllText(temporary, text);
                    if (File.Exists(file)) File.Replace(temporary, file, null);
                    else File.Move(temporary, file);
                }
            }
            else if (File.Exists(file)) File.Delete(file);
            return tide;
        }

        [Serializable]
        sealed class Toolchain { public string commit, lockHash, tide; }

        internal static string MismatchHint()
        {
            var set = TideUserSettings.Loaded.tidePath;
            if (!string.IsNullOrEmpty(set))
                return $" Your tide command overrides the package's version: {set}. In Project Settings > Tide, clear Your tide command (Use package version), or rebuild that custom Tide installation to match the package. Then Build now.";
            var tide = Find(out var where, false);
            return $" Selected tide: {tide ?? where}. In Project Settings > Tide, install the package's version and Build now. A package following dev may be newer than the latest published compiler; use a matching release tag or a matching development build.";
        }

        static string FindCommand(out string where, bool install, bool wait)
        {
            Pinned = false;
            var set = TideUserSettings.Loaded.tidePath;
            if (!string.IsNullOrEmpty(set))
            {
                where = "your setting";
                if (File.Exists(set)) return Path.GetFullPath(set);
                where = $"Your tide command set in Project Settings > Tide isn't there: {set}";
                return null;
            }
            var wanted = TideVersions.Wanted(out var why);
            if (wanted != null)
            {
                Pinned = true;
                if (install) return TideVersions.Ensure(wanted, out where, wait);
                var exe = TideVersions.Executable(wanted);
                where = exe != null ? $"tide {wanted}, which this project's Tide package is"
                    : $"This project's Tide package builds with tide {wanted}, which isn't installed";
                return exe;
            }
            if (why != null)
            {
                where = $"This project's Tide package is from git, and {why}";
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
