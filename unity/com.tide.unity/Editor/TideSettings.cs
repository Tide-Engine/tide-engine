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
        [Tooltip("The folder of the game's .tide files and C, from the project's folder. Only its C is the game's: " +
                 "keep other plugins' C sources out of it.")]
        public string gameFolder = "Assets/Tide";

        [Tooltip("The tide command to build with; empty to find it: on PATH, or where tide's installer puts it.")]
        public string tidePath = "";

        [Tooltip("Build the game whenever its files change, while the editor is open.")]
        public bool buildWhileEditing = true;

        public void Save()
        {
            Save(true);
        }

        public static string ProjectFolder => Path.GetDirectoryName(Application.dataPath) ?? ".";

        public string GameFolderPath => Path.GetFullPath(Path.Combine(ProjectFolder, string.IsNullOrEmpty(gameFolder) ? "Assets/Tide" : gameFolder));
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
            settings.gameFolder = EditorGUILayout.TextField(new GUIContent("Game folder", "The game's .tide files and C, from the project's folder"), settings.gameFolder);
            using (new EditorGUILayout.HorizontalScope())
            {
                settings.tidePath = EditorGUILayout.TextField(new GUIContent("tide command", "Empty: found on PATH or where tide's installer puts it"), settings.tidePath);
                if (GUILayout.Button("Browse", GUILayout.Width(70)))
                {
                    var picked = EditorUtility.OpenFilePanel("The tide command", "", Application.platform == RuntimePlatform.WindowsEditor ? "exe" : "");
                    if (!string.IsNullOrEmpty(picked)) settings.tidePath = picked;
                }
            }
            settings.buildWhileEditing = EditorGUILayout.Toggle(new GUIContent("Build while editing", "Build the game whenever its files change"), settings.buildWhileEditing);
            if (EditorGUI.EndChangeCheck())
            {
                settings.Save();
                TideBuildService.Restart();
            }
            EditorGUILayout.Space();
            var tide = TideToolchain.Find(out var where);
            EditorGUILayout.HelpBox(tide != null ? $"tide: {tide} ({where})" : where, tide != null ? MessageType.Info : MessageType.Warning);
            EditorGUILayout.LabelField("Build", TideBuildService.Describe());
            using (new EditorGUILayout.HorizontalScope())
            {
                if (GUILayout.Button("Build now")) TideBuildService.Restart();
                if (GUILayout.Button("Stop building")) TideBuildService.Stop();
            }
        }
    }
}
