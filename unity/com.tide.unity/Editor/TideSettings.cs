using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
    // Your own Tide settings for this project, kept in UserSettings/TideSettings.asset, which source control leaves
    // out: a path on this machine is nobody else's.
    [FilePath("UserSettings/TideSettings.asset", FilePathAttribute.Location.ProjectFolder)]
    public sealed class TideUserSettings : ScriptableSingleton<TideUserSettings>
    {
        [Tooltip("A tide command to build with, over the version this project's Tide package is: a build of tide you're working on.")]
        public string tidePath = "";

        [SerializeField] bool moved; // The project's own setting of this, before there was one of yours, moved here

        public void Save()
        {
            Save(true);
        }

        // Once: the tide command the project's settings named, when that was where it was kept
        public static TideUserSettings Loaded
        {
            get
            {
                var settings = instance;
                if (settings.moved) return settings;
                settings.moved = true;
                try
                {
                    var old = File.ReadAllText(Path.Combine(TideSettings.ProjectFolder, "ProjectSettings", "TideSettings.asset"));
                    var m = System.Text.RegularExpressions.Regex.Match(old, @"^\s*tidePath:\s*(\S.*?)\s*$",
                        System.Text.RegularExpressions.RegexOptions.Multiline);
                    if (m.Success && string.IsNullOrEmpty(settings.tidePath)) settings.tidePath = m.Groups[1].Value.Trim('\'', '"');
                }
                catch (IOException)
                {
                    // No project settings of Tide's
                }
                settings.Save();
                return settings;
            }
        }
    }

    // The project's Tide settings (Project Settings > Tide), kept in
    // ProjectSettings/TideSettings.asset, which goes in source control.
    [FilePath("ProjectSettings/TideSettings.asset", FilePathAttribute.Location.ProjectFolder)]
    public sealed class TideSettings : ScriptableSingleton<TideSettings>
    {
        [Tooltip("Build the game whenever its files change, while the editor is open.")]
        public bool buildWhileEditing = true;

        public void Save()
        {
            Save(true);
        }

        public static string ProjectFolder => Path.GetDirectoryName(Application.dataPath) ?? ".";

        // The game: every .tide file in Assets, and its packages, the folders in it with a tide.packages saying
        // `package Name`, whose C is the only C tide builds
        public string GameFolderPath => Path.GetFullPath(Application.dataPath);
    }

    sealed class TideSettingsProvider : SettingsProvider
    {
        TideSettingsProvider() : base("Project/Tide", SettingsScope.Project)
        {
        }

        [SettingsProvider]
        static SettingsProvider Create()
        {
            return new TideSettingsProvider { keywords = new[] { "Tide", "netcode", "tide command" } };
        }

        public override void OnGUI(string searchContext)
        {
            var settings = TideSettings.instance;
            var user = TideUserSettings.Loaded;
            EditorGUI.BeginChangeCheck();
            using (new EditorGUILayout.HorizontalScope())
            {
                user.tidePath = EditorGUILayout.TextField(new GUIContent("Your tide command",
                    "Yours alone: a tide to build with over the version this project's Tide package is, like a build of tide you're working on. Empty: the package's version, or tide on PATH or where tide's installer puts it."),
                    user.tidePath);
                if (GUILayout.Button("Browse", GUILayout.Width(70)))
                {
                    var picked = EditorUtility.OpenFilePanel("The tide command", "", Application.platform == RuntimePlatform.WindowsEditor ? "exe" : "");
                    if (!string.IsNullOrEmpty(picked)) user.tidePath = picked;
                }
            }
            settings.buildWhileEditing = EditorGUILayout.Toggle(new GUIContent("Build while editing", "Build the game whenever its files change"), settings.buildWhileEditing);
            if (EditorGUI.EndChangeCheck())
            {
                settings.Save();
                user.Save();
                TideBuildService.Restart();
            }
            EditorGUILayout.Space();
            var tide = TideToolchain.Find(out var where, false);
            EditorGUILayout.HelpBox(tide != null ? $"tide: {tide} ({where})" : where, tide != null ? MessageType.Info : MessageType.Warning);
            var wanted = TideVersions.Wanted(out _);
            if (tide == null && wanted != null && GUILayout.Button($"Install tide {wanted}")) TideVersions.Begin(wanted);
            EditorGUILayout.LabelField("Build", TideBuildService.Describe());
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Build now")) TideBuildService.Restart();
                if (GUILayout.Button("Stop building")) TideBuildService.Stop();
            }
        }
    }
}
