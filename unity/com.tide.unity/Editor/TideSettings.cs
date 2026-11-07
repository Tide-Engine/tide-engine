using System;
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Tide.Editor
{
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
            EditorGUI.BeginChangeCheck();
            settings.buildWhileEditing = EditorGUILayout.Toggle(new GUIContent("Build while editing", "Build the game whenever its files change"), settings.buildWhileEditing);
            if (EditorGUI.EndChangeCheck())
            {
                settings.Save();
                TideBuildService.Restart();
            }
            EditorGUILayout.Space();
            if (GUILayout.Button("Manage Tide versions...")) TideVersionsWindow.Open();
            var tide = TideToolchain.Find(out var where, false);
            if (TideToolchain.LaunchCommand(Environment.GetCommandLineArgs(), out _) != null)
                EditorGUILayout.HelpBox("This editor was launched with -tide-command, overriding the package version for this session. Restart Unity without that argument to use the project pin.", MessageType.Warning);
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
