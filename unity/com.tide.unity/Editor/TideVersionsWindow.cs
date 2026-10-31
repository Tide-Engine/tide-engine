using System;
using System.IO;
using UnityEditor;
using UnityEditor.PackageManager;
using UnityEditor.PackageManager.Requests;
using UnityEngine;

namespace Tide.Editor
{
    // Package changes go through UPM so manifest/lock updates, imports and script reloads stay Unity's.
    public sealed class TideVersionsWindow : EditorWindow
    {
        TideVersions.Release[] releases = Array.Empty<TideVersions.Release>();
        string error, selected, compiler, selection, compilerVersion;
        bool nightlies = true;
        Vector2 scroll;
        static AddRequest request;
        internal static bool Switching { get; private set; }
        static string operation;

        [MenuItem("Window/Tide/Versions")]
        public static void Open()
        {
            var window = GetWindow<TideVersionsWindow>("Tide Versions");
            window.minSize = new Vector2(460, 360);
        }

        void OnEnable() => EditorApplication.delayCall += Refresh;
        void OnDisable() => EditorApplication.delayCall -= Refresh;

        void Refresh()
        {
            if (this == null || Switching) return;
            error = null;
            try
            {
                TideVersions.Refresh();
                releases = TideVersions.Published();
                Array.Sort(releases, (a, b) => CompareVersions(b.tag_name, a.tag_name));
                compiler = TideToolchain.Find(out selection, false);
                compilerVersion = null;
                if (compiler != null)
                {
                    var stamp = Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(compiler)), "VERSION");
                    if (File.Exists(stamp)) compilerVersion = File.ReadAllText(stamp).Trim();
                }
            }
            catch (Exception e) { error = e.Message; }
            Repaint();
        }

        // Semantic order, including numeric nightly suffixes (nightly.10 follows nightly.9).
        internal static int CompareVersions(string a, string b)
        {
            var x = TideVersions.OfTag(a)?.Split(new[] { '-', '.' });
            var y = TideVersions.OfTag(b)?.Split(new[] { '-', '.' });
            if (x == null || y == null) return x == null ? (y == null ? 0 : -1) : 1;
            for (var i = 0; i < Math.Max(x.Length, y.Length); i++)
            {
                if (i == x.Length || i == y.Length)
                    return i == 3 ? (i == x.Length ? 1 : -1) : (i == x.Length ? -1 : 1);
                var xn = int.TryParse(x[i], out var xi);
                var yn = int.TryParse(y[i], out var yi);
                var c = xn && yn ? xi.CompareTo(yi) : xn != yn ? (xn ? -1 : 1) : string.CompareOrdinal(x[i], y[i]);
                if (c != 0) return c;
            }
            return 0;
        }

        internal static string PackageUrl(string packageId, string revision)
        {
            const string prefix = "com.tide.unity@";
            if (packageId == null || !packageId.StartsWith(prefix, StringComparison.Ordinal)
                || (revision != "dev" && TideVersions.OfTag(revision) == null))
                throw new ArgumentException("Select a published Tide release or dev.");
            var url = packageId.Substring(prefix.Length);
            var fragment = url.IndexOf('#');
            if (fragment >= 0) url = url.Substring(0, fragment);
            if (!url.Contains("://") && !url.StartsWith("git@", StringComparison.Ordinal))
                throw new ArgumentException("Version switching requires a Git Tide package.");
            return url + "#" + revision;
        }

        void OnGUI()
        {
            var info = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(TideVersions).Assembly);
            var git = info != null && info.source == PackageSource.Git && info.git != null;
            EditorGUILayout.LabelField("This project", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("Unity package", git ? info.git.revision : "Local package");
            if (git) EditorGUILayout.SelectableLabel(info.git.hash, GUILayout.Height(EditorGUIUtility.singleLineHeight));
            EditorGUILayout.LabelField("Required embed ABI", TideLibrary.Abi.ToString());
            EditorGUILayout.LabelField("Compiler version", compilerVersion ?? "Not resolved");
            EditorGUILayout.HelpBox(compiler == null ? selection ?? "Check for versions to resolve the compiler." : compiler,
                compiler == null ? MessageType.Warning : MessageType.Info);
            if (TideToolchain.LaunchCommand(Environment.GetCommandLineArgs(), out _) != null)
                EditorGUILayout.HelpBox("This editor uses -tide-command. Restart without it to use a release's compiler.", MessageType.Warning);
            if (!git)
                EditorGUILayout.HelpBox("This package comes from a local folder. Use a Git package to switch releases here; local development uses -tide-command or the installed compiler.", MessageType.Info);
            if (error != null) EditorGUILayout.HelpBox(error, MessageType.Error);
            if (operation != null) EditorGUILayout.HelpBox(operation, Switching ? MessageType.Info : MessageType.Warning);

            using (new EditorGUI.DisabledScope(Switching || EditorApplication.isPlayingOrWillChangePlaymode || EditorApplication.isCompiling))
            {
                if (GUILayout.Button("Check for updates")) Refresh();
                using (new EditorGUI.DisabledScope(!git))
                {
                    if (GUILayout.Button("Update / switch to dev")) Change(info.packageId, "dev");
                }
                EditorGUILayout.HelpBox("Dev may be ahead of published compilers. A release switches the package and compiler together. Package changes update Packages/manifest.json and packages-lock.json for your team.", MessageType.Info);
                nightlies = EditorGUILayout.Toggle("Show nightly / prerelease", nightlies);
                scroll = EditorGUILayout.BeginScrollView(scroll);
                foreach (var release in releases)
                {
                    if (!nightlies && release.prerelease) continue;
                    var version = TideVersions.OfTag(release.tag_name);
                    if (version == null) continue;
                    using (new EditorGUILayout.HorizontalScope())
                    {
                        EditorGUILayout.LabelField(version + (release.prerelease ? " (prerelease)" : " (stable)"));
                        var available = TideVersions.CanInstall(release);
                        using (new EditorGUI.DisabledScope(!git || !available))
                        {
                            if (GUILayout.Button(available ? "Use version" : "Unavailable on this platform", GUILayout.Width(185)))
                                selected = release.tag_name;
                        }
                    }
                }
                EditorGUILayout.EndScrollView();
                if (selected != null)
                {
                    var tag = selected;
                    selected = null;
                    Change(info.packageId, tag);
                }
            }
        }

        internal void Change(string packageId, string revision)
        {
            try
            {
                var url = PackageUrl(packageId, revision);
                Switching = true;
                TideBuildService.Stop();
                // IDEs must not reuse the old selection while the package is changing.
                var cache = Path.Combine(TideLibrary.BuildFolder, "toolchain.json");
                if (File.Exists(cache)) File.Delete(cache);
                var version = TideVersions.OfTag(revision);
                if (version != null)
                {
                    var exe = TideVersions.Install(version, (f, what) => EditorUtility.DisplayProgressBar("Tide Versions", what, f), out var why);
                    if (exe == null) throw new IOException(why);
                }
                operation = revision == "dev" ? "Updating the dev package. A matching published compiler is required before builds can resume."
                    : $"Switching the Unity package to {revision}. Unity will reload scripts, then use its compiler.";
                request = Client.Add(url);
                EditorApplication.update += Watch;
            }
            catch (Exception e)
            {
                Switching = false;
                error = e.Message;
                if (TideSettings.instance.buildWhileEditing) TideBuildService.Restart();
            }
            finally { EditorUtility.ClearProgressBar(); }
        }

        static void Watch()
        {
            if (!request.IsCompleted) return;
            EditorApplication.update -= Watch;
            Switching = false;
            operation = request.Status == StatusCode.Success ? null : "The package could not be switched: " + request.Error.message;
            request = null;
            TideVersions.Refresh();
            if (TideSettings.instance.buildWhileEditing) TideBuildService.Restart();
            foreach (var window in Resources.FindObjectsOfTypeAll<TideVersionsWindow>()) window.Refresh();
        }
    }
}
