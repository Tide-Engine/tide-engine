using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Finds the tide command: this session's -tide-command argument, then the version this package's
    // commit is, installed side by side (TideVersions: installing it when asked to), then for a package that isn't
    // from git PATH, then where tide's installer puts it (%LOCALAPPDATA%\Tide\bin on Windows, ~/.tide/bin
    // elsewhere). Nothing about this machine is written into the project.
    [InitializeOnLoad]
    public static class TideToolchain
    {
        static TideToolchain()
        {
            EditorApplication.quitting += () => EndSession(Environment.GetCommandLineArgs());
        }

        static string ExecutableName => Application.platform == RuntimePlatform.WindowsEditor ? "tide.exe" : "tide";

        internal static string LaunchCommand(string[] arguments, out string error)
        {
            error = null;
            string command = null;
            for (var i = 0; i < arguments.Length; i++)
            {
                if (!string.Equals(arguments[i], "-tide-command", StringComparison.OrdinalIgnoreCase)) continue;
                if (command != null || i + 1 == arguments.Length || string.IsNullOrWhiteSpace(arguments[i + 1]) || arguments[i + 1].StartsWith("-"))
                {
                    error = "Launch Unity with -tide-command followed by one executable path, once; omit it to use the project pin.";
                    return null;
                }
                command = arguments[++i];
            }
            return command;
        }

        internal static long SessionStarted
        {
            get { using (var process = Process.GetCurrentProcess()) return new DateTimeOffset(process.StartTime).ToUnixTimeSeconds(); }
        }

        // Whether the tide Find gave is the package's own version: its daily look for a newer one says nothing
        public static bool Pinned { get; private set; }

        // The tide command's full path, or null; `where` says how it was found, or what to do. `install`: the
        // package's version may be installed, now in a batch run (a build server), or in the editor once you say so;
        // `wait`: now in the editor too, as a player build waits for it.
        public static string Find(out string where, bool install = true, bool wait = false)
            => Find(Environment.GetCommandLineArgs(), out where, install, wait);

        internal static string Find(string[] arguments, out string where, bool install = false, bool wait = false)
        {
            var tide = FindCommand(arguments, out where, install, wait);
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
                var development = LaunchCommand(arguments, out _) != null;
                int sessionPid = 0;
                if (development) using (var process = Process.GetCurrentProcess()) sessionPid = process.Id;
                var text = JsonUtility.ToJson(new Toolchain { format = 2, commit = commit, lockHash = lockHash, tide = Path.GetFullPath(tide),
                    version = Pinned ? TideVersions.Wanted(out _) : null, sessionPid = sessionPid, sessionStarted = development ? SessionStarted : 0 });
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
        sealed class Toolchain { public int format, sessionPid; public long sessionStarted; public string commit, lockHash, tide, version; }

        // Normal selections are reusable. A development selection lasts only as long as this editor process,
        // including across domain reloads; IDEs check its PID and start time after a crash as well.
        internal static void EndSession(string[] arguments)
        {
            if (LaunchCommand(arguments, out _) == null) return;
            var file = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
            if (!File.Exists(file)) return;
            var selected = JsonUtility.FromJson<Toolchain>(File.ReadAllText(file));
            using (var process = Process.GetCurrentProcess())
                if (selected != null && selected.sessionPid == process.Id && selected.sessionStarted == SessionStarted) File.Delete(file);
        }

        internal static string MismatchHint()
            => MismatchHint(Environment.GetCommandLineArgs());

        internal static string MismatchHint(string[] arguments)
        {
            var set = LaunchCommand(arguments, out _);
            if (!string.IsNullOrEmpty(set))
                return $" This session's -tide-command overrides the package's version: {set}. Restart Unity without -tide-command to use the project pin, or rebuild that development compiler to match the package.";
            var tide = Find(out var where, false);
            return $" Selected tide: {tide ?? where}. Open Window > Tide > Versions to install the matching compiler or switch the package and compiler together, then Build now.";
        }

        static string FindCommand(string[] arguments, out string where, bool install, bool wait)
        {
            Pinned = false;
            if (TideVersionsWindow.Switching) { where = "Installing the selected Tide package; builds resume after Unity reloads it"; return null; }
            var set = LaunchCommand(arguments, out var argumentError);
            if (argumentError != null) { where = argumentError; return null; }
            if (!string.IsNullOrEmpty(set))
            {
                where = "-tide-command, for this editor session only";
                if (File.Exists(set)) return Path.GetFullPath(set);
                where = $"This session's -tide-command isn't there: {set}. Correct the launch argument, or omit it to use the project pin.";
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
            where = "tide isn't installed, or isn't on PATH: install it from tide-engine.dev. For engine development, launch Unity with -tide-command <path>.";
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
