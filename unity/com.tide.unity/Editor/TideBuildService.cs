using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.SceneManagement;
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
            // An asset import worker is a Unity process of its own, which loads this too: the editor builds, not it
            if (AssetDatabase.IsAssetImportWorkerProcess()) return;
            // A task the editor kept through a script reload, which no one ends now
            foreach (var item in Progress.EnumerateItems())
            {
                if (item.name == TaskName) Progress.Remove(item.id);
            }
            AssemblyReloadEvents.beforeAssemblyReload += Stop;
            EditorApplication.quitting += Stop;
            EditorApplication.update += Drain;
            EditorApplication.playModeStateChanged += RefusePlayWithErrors;
            EditorBuildSettings.sceneListChanged += Changed; // Its first scene is the one the match starts with
            EditorSceneManager.sceneSaved += Saved;
            EditorSceneManager.sceneClosed += Saved;
            EditorSceneManager.sceneOpened += (scene, _) => Saved(scene);
            if (!SessionState.GetBool(SessionKey, false))
            {
                // Copies a last session left, of scenes whose changes it never saved
                SessionState.SetBool(SessionKey, true);
                try
                {
                    if (Directory.Exists(UnsavedFolder)) Directory.Delete(UnsavedFolder, true);
                }
                catch (Exception)
                {
                    // Another editor of the project has them open: it's that one's
                }
            }
            EditorApplication.delayCall += () =>
            {
                if (TideSettings.instance.buildWhileEditing) Start();
            };
        }

        internal static bool Running => process != null && !process.HasExited;

        public static string Describe()
        {
            if (process == null || process.HasExited) return state;
            return Busy ? "building the game" : "building while the game's files change";
        }

        // What tide is doing, as a task in the editor's status bar (Unity's Background Tasks), never the Console: a
        // build, from tide starting or saying "tide: building game-<n>" to how it went, and Play waiting for tide to
        // have built the scenes as they are.
        internal const string TaskName = "Tide";
        static int task = -1;

        internal static bool Busy => task >= 0 && Progress.Exists(task);

        static void Working(string what)
        {
            if (Busy) Progress.SetDescription(task, what);
            else task = Progress.Start(TaskName, what, Progress.Options.Indefinite);
        }

        static void Idle()
        {
            if (Busy) Progress.Remove(task);
            task = -1;
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
                state = $"no Assets folder: {folder}";
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
                Arguments = $"unity -o \"{TideLibrary.BuildFolder}\" --assets \"{Application.dataPath}\" --notified",
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
                Working("Building the game");
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
            Tell("restart");
        }

        // A file of the project that's the game's changed, as Unity saw it (TideChanges): tide, which Unity runs
        // --notified, builds then, rather than going over all of Assets to find out.
        public static void Changed()
        {
            changedSinceBuild = true;
            changes++;
            Tell("changed");
        }

        // Play has the open scenes as the editor has them, saved or not: before Play, a copy of each with changes it
        // hasn't saved goes in Library/Tide/unsaved, which tide reads in its place, and Play waits for tide to have
        // built everything it's been told of. A scene's copy goes once it's saved, or closed, or opened again. The
        // first scene that was never saved, which has no file, has its copy too (Untitled), the Tide scene
        // UnityEngine.Scenes.Untitled, which the runner finds as the scene with no path.
        const string SessionKey = "Tide.Session";
        const string Untitled = "Untitled.unity"; // unity_assets.h's UNITY_UNTITLED_SCENE
        static bool changedSinceBuild; // tide's been told of a change it hasn't said the build of
        static int changes, changesSynced; // Changes told; those told before the "sync" asked last
        static int syncs; // "sync <n>" asked, the last one's answer "tide: synced <n>"
        static bool copying; // Saving a scene's copy, which is no saving of the scene
        static bool waiting, resuming; // Play's on hold till tide's synced; Play again
        internal static bool Synced { get; private set; } = true; // tide said it had built what it was told, last it was asked

        internal static string UnsavedFolder => Path.Combine(TideLibrary.BuildFolder, "unsaved");

        // A scene saved, closed or opened, which has no changes the editor hasn't saved; not as Play comes or goes. The
        // untitled scene's copy goes with the last untitled scene, saved under a name or closed.
        static void Saved(Scene scene)
        {
            if (EditorApplication.isPlayingOrWillChangePlaymode) return;
            Unsaved(scene.path, default, false);
            if (!UntitledOpen().IsValid()) Unsaved(Untitled, default, false);
        }

        static Scene UntitledOpen()
        {
            for (var i = 0; i < SceneManager.sceneCount; i++)
            {
                var scene = SceneManager.GetSceneAt(i);
                if (scene.isLoaded && string.IsNullOrEmpty(scene.path)) return scene;
            }
            return default;
        }

        static string CopyOf(string asset) => "Library/Tide/unsaved/" + asset;

        // Whether the scene `asset` (its path from the project's folder, or Untitled) has a copy, of `open` as the
        // editor has it (`unsaved`), or none: true if that changed it, which tide's then told.
        static bool Unsaved(string asset, Scene open, bool unsaved)
        {
            if (copying || string.IsNullOrEmpty(asset)) return false;
            var copy = Path.Combine(TideSettings.ProjectFolder, CopyOf(asset));
            if (!unsaved)
            {
                if (!File.Exists(copy)) return false;
                File.Delete(copy);
                Changed();
                return true;
            }
            // Saved where Unity likes, then copied: a file Unity saved a scene's copy to is one it deletes as the scene
            // closes
            const string saved = "Temp/TideUnsaved.unity";
            copying = true;
            try
            {
                if (!open.IsValid() || !EditorSceneManager.SaveScene(open, saved, true)) return false;
            }
            finally
            {
                copying = false;
            }
            var now = File.ReadAllBytes(saved);
            File.Delete(saved);
            File.Delete(saved + ".meta");
            if (File.Exists(copy) && File.ReadAllBytes(copy).AsSpan().SequenceEqual(now)) return false;
            Directory.CreateDirectory(Path.GetDirectoryName(copy) ?? ".");
            File.WriteAllBytes(copy, now);
            Changed();
            return true;
        }

        // Copies of the scenes with unsaved changes made, and of the first untitled one, those of others deleted, and
        // tide asked to say when it's built everything it's been told of: false if there's nothing it hasn't built (or
        // it isn't running).
        internal static bool Sync()
        {
            if (process == null || process.HasExited) return false;
            var copied = new System.Collections.Generic.HashSet<string>();
            for (var i = 0; i < SceneManager.sceneCount; i++)
            {
                var scene = SceneManager.GetSceneAt(i);
                if (!scene.isLoaded || !scene.isDirty || string.IsNullOrEmpty(scene.path)) continue;
                copied.Add(scene.path);
                Unsaved(scene.path, scene, true);
            }
            var untitled = UntitledOpen();
            if (untitled.IsValid())
            {
                copied.Add(Untitled);
                Unsaved(Untitled, untitled, true);
            }
            if (Directory.Exists(UnsavedFolder))
            {
                foreach (var copy in Directory.GetFiles(UnsavedFolder, "*.unity", SearchOption.AllDirectories))
                {
                    var scene = Path.GetRelativePath(UnsavedFolder, copy).Replace('\\', '/');
                    if (!copied.Contains(scene)) Unsaved(scene, default, false);
                }
            }
            if (!changedSinceBuild) return false;
            Synced = false;
            changesSynced = changes;
            Tell("sync " + ++syncs);
            Working("Building the scenes for Play");
            return true;
        }

        static void Tell(string line)
        {
            if (process == null || process.HasExited) return;
            try
            {
                process.StandardInput.WriteLine(line);
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
            Idle();
        }

        // A message tide is saying: a diagnostic's first line, then its source line, caret and notes, which come as
        // lines of their own and go in one Console entry.
        static readonly System.Text.StringBuilder pending = new System.Text.StringBuilder();
        static double pendingSince;

        // The last build's errors, until a build succeeds: kept in the Console as Unity keeps its compiler errors,
        // logged again whenever it's cleared, and Play waits for them to be fixed. In SessionState, so a script
        // reload keeps them.
        const string ErrorsKey = "Tide.Errors";
        const string CountKey = "Tide.ConsoleCount";
        const char Separator = '\u001e';
        static readonly System.Collections.Generic.List<string> building = new System.Collections.Generic.List<string>();
        static bool built; // A build ended since the last error

        public static bool HasErrors => SessionState.GetString(ErrorsKey, "").Length > 0;

        // Every error and warning of the last build tide finished, for the .tide files' inspectors: the build going
        // on's gather in `messages` until tide says how it went.
        const string MessagesKey = "Tide.Messages";
        static readonly System.Collections.Generic.List<string> messages = new System.Collections.Generic.List<string>();

        internal static string[] LastMessages
        {
            get
            {
                var all = SessionState.GetString(MessagesKey, "");
                return all.Length > 0 ? all.Split(Separator) : Array.Empty<string>();
            }
        }

        static void Finished()
        {
            SessionState.SetString(MessagesKey, string.Join(Separator.ToString(), messages));
            messages.Clear();
        }

        // Where a message of tide's is: the project's path of the file it names (a prefab's or a scene's Tide text,
        // for one of those), and its line and column. False for one that names none.
        internal static bool Locate(string message, out string file, out int line, out int column)
        {
            ContextOf(ref message, out file, out line, out column);
            return file != null;
        }

        static void Drain()
        {
            while (lines.TryDequeue(out var entry))
            {
                var line = entry.line;
                // A diagnostic's own lines are indented: its source line, its caret and its notes
                if (pending.Length > 0 && line.Length > 0 && line[0] == ' ')
                {
                    pending.Append('\n').Append(line);
                    continue;
                }
                Flush();
                pending.Append(line);
                pendingSince = EditorApplication.timeSinceStartup;
            }
            // A message's lines come together: one that's been alone a moment is whole
            if (pending.Length > 0 && EditorApplication.timeSinceStartup - pendingSince > 0.15) Flush();
            if (process != null && process.HasExited)
            {
                Flush();
                state = $"tide ended with {process.ExitCode}";
                process.Dispose();
                process = null;
                Synced = true; // Nothing more comes
                Idle();
            }
            if (waiting && Synced)
            {
                waiting = false;
                resuming = true;
                // The C# tide wrote of the game with it, compiled before Play, which waits for that
                AssetDatabase.Refresh();
                EditorApplication.isPlaying = true;
            }
            KeepErrorsShown();
        }

        // A line as tide says it, taken at once (tests)
        internal static void Hear(string line, bool error = false)
        {
            lines.Enqueue((line, error));
            Drain();
            Flush();
        }

        static void Flush()
        {
            if (pending.Length == 0) return;
            var message = pending.ToString();
            pending.Clear();
            var first = message.Split('\n')[0];
            // A compiler's error or warning says so; the rest of what tide says is progress, and how a build went
            var isError = first.Contains(": error") || first.StartsWith("tide: can't") || first.Contains("FAILED");
            if (first.StartsWith("tide: building game-"))
            {
                Working(Synced ? "Building the game" : "Building the scenes for Play");
                return;
            }
            if (first.StartsWith("tide: building the game for Unity"))
            {
                // Its first build is over, as it went (its errors came before), and it watches the game's files
                if (Synced) Idle();
                return;
            }
            if (first.StartsWith("tide: synced"))
            {
                if (first != "tide: synced " + syncs) return; // An earlier question's
                if (changes == changesSynced) changedSinceBuild = false;
                Synced = true;
                Idle();
            }
            else if (first.StartsWith("tide: built"))
            {
                if (Synced) Idle(); // Play waiting for tide goes on till it's said it's synced
                Finished();
                building.Clear();
                built = true;
                if (said == 0) RemoveLocated(); // A build with nothing to say: the last one's errors and warnings go
                said = 0;
                if (HasErrors && plainErrors) Debug.Log("tide: the game's errors are fixed");
                plainErrors = false;
                SessionState.SetString(ErrorsKey, "");
            }
            else if (first.Contains("keeps running its last build") || first.Contains("will build once its files are fixed"))
            {
                if (Synced) Idle();
                built = true; // The errors after this are another build's
                said = 0;
                Finished();
            }
            else if (isError)
            {
                messages.Add(message);
                if (built) building.Clear();
                built = false;
                building.Add(message);
                SessionState.SetString(ErrorsKey, string.Join(Separator.ToString(), building));
            }
            else if (first.Contains(": warning"))
            {
                messages.Add(message);
            }
            Log(message, isError);
        }

        // The Console is the user's: only what needs fixing goes there, an error or a warning. Progress is the status
        // Project Settings > Tide shows. One naming its file and line goes in as Unity's compiler messages do, so a
        // double click opens the file there; a build's first takes the last build's away (`again`: the same ones,
        // logged again).
        internal static void Log(string message, bool isError, bool again = false)
        {
            var warning = message.Split('\n')[0].Contains(": warning");
            if (!isError && !warning) return;
            if (!again && said++ == 0) RemoveLocated();
            var context = ContextOf(ref message, out var file, out var line, out var column);
            if (!message.StartsWith("tide: ")) message = "tide: " + message;
            // One naming no file goes in as a compiler message too, with none, so the next build takes it away as well
            if (LogLocated(message, file ?? "", line, column, isError)) return;
            if (isError) plainErrors = true;
            // With no C# stack under it: where it came from is tide, not this package's code
            Debug.LogFormat(isError ? LogType.Error : LogType.Warning, LogOption.NoStacktrace, context, "{0}", message);
        }

        static int said; // Errors and warnings of the build going on, which tide hasn't said the end of
        // An error went in as an ordinary log (a Unity without compiler messages), which stays: a line then says it's fixed
        static bool plainErrors;

        // Unity's own compiler messages (internal, through reflection): an entry with a file and line, which a double
        // click opens there, and which Unity's Clear leaves, as a C# error. Ours go by our identifier. Where this Unity
        // hasn't them, tide's go in as ordinary logs.
        const int ConsoleIdentifier = 0x74696465; // "tide"
        const System.Reflection.BindingFlags Internal =
            System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic;
        static readonly System.Reflection.MethodInfo compilerMessage = typeof(Debug).GetMethod("LogCompilerMessage", Internal);
        static readonly System.Reflection.MethodInfo removeByIdentifier = typeof(Debug).GetMethod("RemoveLogEntriesByIdentifier", Internal);

        static bool LogLocated(string message, string file, int line, int column, bool isError)
        {
            if (compilerMessage == null || removeByIdentifier == null) return false;
            // (message, file, line, column, forEditor, isError, identifier, the object's id: none)
            var parameters = compilerMessage.GetParameters();
            if (parameters.Length != 8) return false;
            var none = parameters[7].ParameterType.IsValueType ? Activator.CreateInstance(parameters[7].ParameterType) : null;
            try
            {
                compilerMessage.Invoke(null, new[] { message, file, line, column, true, isError, ConsoleIdentifier, none });
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        internal static void RemoveLocated()
        {
            if (removeByIdentifier == null) return;
            try
            {
                removeByIdentifier.Invoke(null, new object[] { ConsoleIdentifier });
            }
            catch (Exception)
            {
                // This Unity's differs: they stay
            }
        }

        // The Console's entries, through Unity's own (internal) count: fewer than before means it was cleared.
        static readonly System.Reflection.MethodInfo consoleCount =
            Type.GetType("UnityEditor.LogEntries, UnityEditor")?.GetMethod("GetCount",
                System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public);

        static void KeepErrorsShown()
        {
            if (consoleCount == null) return;
            int count;
            try
            {
                count = (int)consoleCount.Invoke(null, null);
            }
            catch (Exception)
            {
                return;
            }
            var before = SessionState.GetInt(CountKey, 0);
            SessionState.SetInt(CountKey, count);
            if (count >= before || !HasErrors) return;
            RemoveLocated(); // Those Clear left, so none is there twice
            foreach (var error in SessionState.GetString(ErrorsKey, "").Split(Separator)) Log(error, true, true);
            SessionState.SetInt(CountKey, (int)consoleCount.Invoke(null, null));
        }

        // Play waits for tide to build the scenes as they are, saved or not (Sync), and for tide's errors to be fixed,
        // as for a script's. A batch run (tests) saves what it changes, and has Play as it asks.
        static void RefusePlayWithErrors(PlayModeStateChange change)
        {
            if (change != PlayModeStateChange.ExitingEditMode) return;
            if (!resuming && !Application.isBatchMode && Sync())
            {
                EditorApplication.isPlaying = false;
                waiting = true;
                return;
            }
            resuming = false;
            if (!HasErrors) return;
            EditorApplication.isPlaying = false;
            RemoveLocated();
            foreach (var error in SessionState.GetString(ErrorsKey, "").Split(Separator)) Log(error, true, true);
            const string message = "Fix the game's Tide errors before Play: see the Console";
            foreach (SceneView view in SceneView.sceneViews) view.ShowNotification(new GUIContent(message));
            Log("tide: the game has errors: fix them before Play (they're above)", true, true); // The next build takes it away
            // Nobody can fix them in a batch run (tests), which would wait for Play forever: it fails
            if (Application.isBatchMode) EditorApplication.Exit(1);
        }

        static readonly System.Text.RegularExpressions.Regex located =
            new System.Text.RegularExpressions.Regex(@"^(?<path>.+?)(:(?<line>\d+):(?<column>\d+))?: (error|warning|note)");

        // The asset a message is about, for the Console to select: a .tide file of the game, or a prefab or scene,
        // whose line numbers are of the Tide text tide made of it, which the message then names.
        // `file` (the project's path, null for none), `lineNumber` and `column`: where a double click opens it, the
        // Tide text tide read for a prefab or scene.
        static UnityEngine.Object ContextOf(ref string line, out string file, out int lineNumber, out int column)
        {
            file = null;
            lineNumber = column = 0;
            var match = located.Match(line.Split('\n')[0]);
            if (!match.Success) return null;
            int.TryParse(match.Groups["line"].Value, out lineNumber);
            int.TryParse(match.Groups["column"].Value, out column);
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
                if (lineNumber == 0)
                {
                    file = path; // About the whole asset: a double click opens it
                    return AssetDatabase.LoadMainAssetAtPath(asset);
                }
                var text = $"Library/Tide/prefabs/{path}.tprefab";
                line += $" (line {match.Groups["line"].Value} of {text}, the Tide text tide read)";
                file = File.Exists(Path.Combine(project, text)) ? text : path;
                return AssetDatabase.LoadMainAssetAtPath(asset);
            }
            else
            {
                var full = Path.GetFullPath(Path.Combine(TideSettings.instance.GameFolderPath, path)).Replace('\\', '/');
                if (full.StartsWith(project, StringComparison.OrdinalIgnoreCase)) asset = full.Substring(project.Length);
            }
            file = asset;
            return asset != null ? AssetDatabase.LoadMainAssetAtPath(asset) : null;
        }
    }

    // What Unity imports, moves and deletes that's the game's: .tide files, tide.packages, prefabs and scenes, and
    // a package's C, headers and libraries. Everything else in Assets is nothing to tide.
    sealed class TideChanges : AssetPostprocessor
    {
        static readonly string[] gameFiles = { ".tide", ".packages", ".prefab", ".unity" };
        static readonly string[] packageFiles =
            { ".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".inl", ".a", ".lib", ".so", ".dll", ".dylib" };

        static void OnPostprocessAllAssets(string[] imported, string[] deleted, string[] moved, string[] movedFrom)
        {
            if (Matters(imported) || Matters(deleted) || Matters(moved) || Matters(movedFrom)) TideBuildService.Changed();
        }

        internal static bool Matters(string[] paths)
        {
            foreach (var path in paths)
            {
                if (!path.StartsWith("Assets/")) continue;
                var extension = Path.GetExtension(path).ToLowerInvariant();
                if (Array.IndexOf(gameFiles, extension) >= 0) return true;
                if (Array.IndexOf(packageFiles, extension) >= 0 && InPackage(path)) return true;
            }
            return false;
        }

        static bool InPackage(string path)
        {
            for (var folder = Path.GetDirectoryName(path)?.Replace('\\', '/'); !string.IsNullOrEmpty(folder) && folder != "Assets";
                 folder = Path.GetDirectoryName(folder)?.Replace('\\', '/'))
            {
                if (TidePackageFolders.IsPackage(folder)) return true;
            }
            return false;
        }
    }
}
